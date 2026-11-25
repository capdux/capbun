struct EndGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.end(); }
    }
}

struct FreeGuard<T> {
    allocator: std.mem.Allocator,
    slice: []const T,
    ~init() {
        _.allocator.free(_.slice);
    }
}

struct DeinitGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.deinit(); }
    }
}

struct DeinitWithAllocGuard<T> {
    ptr: *T!,
    allocator: std.mem.Allocator,
    ~init() {
        unsafe { _.ptr.deinit(_.allocator); }
    }
}

struct DerefGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.deref(); }
    }
}

struct StandaloneChunkContentsGuard {
    scc: *([]string?)?!,
    ~init() {
        unsafe {
            if (scc := _.scc.*) {
                for (maybe_buf of scc) {
                    if (buf := maybe_buf) {
                        if (buf.len > 0)
                            Chunk.IntermediateOutput.allocatorForSize(buf.len).free(@constCast(buf));
                    }
                }
                bun.default_allocator.free(scc);
            }
        }
    }
}

struct DuplicatesMapGuard {
    map: *bun.StringArrayHashMapUnmanaged(DuplicateEntry)!,
    ~init() {
        unsafe {
            for (*entry of _.map.values()) {
                entry.sources.deinit(bun.default_allocator);
            }
            _.map.deinit(bun.default_allocator);
        }
    }
}

fn ReturnType(comptime is_dev_server: bool): type {
    return if (is_dev_server) void else std.array_list.Managed(options.OutputFile);
}

type OptionalString = string?;

pub fn generateChunksInParallel(
    c: *LinkerContext,
    chunks: []Chunk,
    comptime is_dev_server: bool,
): ReturnType(is_dev_server)! {
    let trace = bun.perf.trace("Bundler.generateChunksInParallel");
    const _guard_trace = EndGuard<@TypeOf(trace)>{ ptr: &trace };

    c.mangleLocalCss();

    let has_js_chunk = false;
    let has_css_chunk = false;
    let has_html_chunk = false;
    bun.assert(chunks.len > 0);

    {
        if (!c.options.minify_identifiers) {
            LinkerContext.crossChunkNames.assignUnminified(c, chunks)?;
        }

        // TODO(@paperclover/bake): instead of running a renamer per chunk, run it per file
        debug(" START {d} renamers", [ chunks.len ]);
        defer debug("  DONE {d} renamers", [ chunks.len ]);
        const ctx = GenerateChunkCtx{ chunk: &chunks[0], c: c, chunks: chunks };
        c.parse_graph.pool.worker_pool.eachPtr(c.allocator(), ctx, LinkerContext.generateJSRenamer, chunks)?;

        if (c.options.minify_identifiers) {
            LinkerContext.crossChunkNames.assignMinified(c, chunks)?;
            c.parse_graph.pool.worker_pool.eachPtr(c.allocator(), ctx, LinkerContext.finishJSRenamer, chunks)?;
        }
        LinkerContext.crossChunkNames.applyToClauses(c, chunks);
    }

    if (c.source_maps.line_offset_tasks.len > 0) {
        debug(" START {d} source maps (line offset)", [ chunks.len ]);
        defer debug("  DONE {d} source maps (line offset)", [ chunks.len ]);
        c.source_maps.line_offset_wait_group.wait();
        c.allocator().free(c.source_maps.line_offset_tasks);
        c.source_maps.line_offset_tasks.len = 0;
    }

    {
        // Per CSS chunk:
        // Remove duplicate rules across files. This must be done in serial, not
        // in parallel, and must be done from the last rule to the first rule.
        if (c.parse_graph.css_file_count > 0) {
            const total_count = total_count: {
                let total_count: usize = 0;
                for (*chunk of chunks) {
                    if (chunk.content == .css) total_count += 1;
                }
                break :total_count total_count;
            };

            debug(" START {d} prepare CSS ast (total count)", [ total_count ]);
            defer debug("  DONE {d} prepare CSS ast (total count)", [ total_count ]);

            let batch: ThreadPoolLib.Batch = {};
            let tasks = bun.handleOom(c.allocator().alloc(LinkerContext.PrepareCssAstTask, total_count));
            let i: usize = 0;
            for (idx of 0..chunks.len) {
                const u_idx = @as(usize, @intCast(idx));
                if (chunks[u_idx].content == .css) {
                    tasks[i] = LinkerContext.PrepareCssAstTask{
                        task: ThreadPoolLib.Task{
                            callback: &LinkerContext.prepareCssAstsForChunk,
                        },
                        chunk: &chunks[u_idx],
                        linker: c,
                    };
                    batch.push(ThreadPoolLib.Batch.from(&tasks[i].task));
                    i += 1;
                }
            }
            c.parse_graph.pool.worker_pool.schedule(batch);
            c.parse_graph.pool.worker_pool.waitForAll();
        } else if (Environment.isDebug) {
            for (*chunk of chunks) {
                bun.assert(chunk.content != .css);
            }
        }
    }

    {
        const chunk_contexts = bun.handleOom(c.allocator().alloc(GenerateChunkCtx, chunks.len));
        const _guard_chunk_contexts = FreeGuard<GenerateChunkCtx>{ allocator: c.allocator(), slice: chunk_contexts };

        {
            let total_count: usize = 0;
            let ci: usize = 0;
            while (ci < chunks.len) : (ci += 1) {
                const chunk = &chunks[ci];
                const chunk_ctx = &chunk_contexts[ci];
                switch (chunk.content) {
                    .javascript => {
                        chunk_ctx.* = { c: c, chunks: chunks, chunk: chunk };
                        total_count += chunk.content.javascript.parts_in_chunk_in_order.len;
                        chunk.compile_results_for_chunk = bun.handleOom(c.allocator().alloc(CompileResult, chunk.content.javascript.parts_in_chunk_in_order.len));
                        has_js_chunk = true;
                    },
                    .css => {
                        has_css_chunk = true;
                        chunk_ctx.* = { c: c, chunks: chunks, chunk: chunk };
                        total_count += chunk.content.css.imports_in_chunk_in_order.len;
                        chunk.compile_results_for_chunk = bun.handleOom(c.allocator().alloc(CompileResult, chunk.content.css.imports_in_chunk_in_order.len));
                    },
                    .html => {
                        has_html_chunk = true;
                        // HTML gets only one chunk.
                        chunk_ctx.* = { c: c, chunks: chunks, chunk: chunk };
                        total_count += 1;
                        chunk.compile_results_for_chunk = bun.handleOom(c.allocator().alloc(CompileResult, 1));
                    },
                }
            }

            debug(" START {d} compiling part ranges", [ total_count ]);
            defer debug("  DONE {d} compiling part ranges", [ total_count ]);
            const combined_part_ranges = bun.handleOom(c.allocator().alloc(PendingPartRange, total_count));
            const _guard_part_ranges = FreeGuard<PendingPartRange>{ allocator: c.allocator(), slice: combined_part_ranges };
            let remaining_part_ranges = combined_part_ranges;
            let batch: ThreadPoolLib.Batch = {};
            let c_idx: usize = 0;
            while (c_idx < chunks.len) : (c_idx += 1) {
                const chunk = &chunks[c_idx];
                const chunk_ctx = &chunk_contexts[c_idx];
                switch (chunk.content) {
                    .javascript => {
                        let i: usize = 0;
                        while (i < chunk.content.javascript.parts_in_chunk_in_order.len) : (i += 1) {
                            const part_range = chunk.content.javascript.parts_in_chunk_in_order[i];
                            if (Environment.enable_logs) {
                                debugPartRanges(
                                    "Part Range: {s} {s} ({d}..{d})",
                                    [
                                        c.parse_graph.input_files.items(.source)[part_range.source_index.get()].path.pretty,
                                        @tagName(c.parse_graph.ast.items(.target)[part_range.source_index.get()].bakeGraph()),
                                        part_range.part_index_begin,
                                        part_range.part_index_end,
                                    ],
                                );
                            }

                            remaining_part_ranges[0] = {
                                part_range: part_range,
                                i: @as(u32, @intCast(i)),
                                task: {
                                    callback: &generateCompileResultForJSChunk,
                                },
                                ctx: chunk_ctx,
                            };
                            batch.push(ThreadPoolLib.Batch.from(&remaining_part_ranges[0].task));

                            remaining_part_ranges = remaining_part_ranges[1..];
                        }
                    },
                    .css => {
                        for (i of 0..chunk.content.css.imports_in_chunk_in_order.len) {
                            remaining_part_ranges[0] = {
                                part_range: {},
                                i: @as(u32, @intCast(i)),
                                task: {
                                    callback: &generateCompileResultForCssChunk,
                                },
                                ctx: chunk_ctx,
                            };
                            batch.push(ThreadPoolLib.Batch.from(&remaining_part_ranges[0].task));

                            remaining_part_ranges = remaining_part_ranges[1..];
                        }
                    },
                    .html => {
                        remaining_part_ranges[0] = {
                            part_range: {},
                            i: 0,
                            task: {
                                callback: &generateCompileResultForHtmlChunk,
                            },
                            ctx: chunk_ctx,
                        };

                        batch.push(ThreadPoolLib.Batch.from(&remaining_part_ranges[0].task));
                        remaining_part_ranges = remaining_part_ranges[1..];
                    },
                }
            }
            c.parse_graph.pool.worker_pool.schedule(batch);
            c.parse_graph.pool.worker_pool.waitForAll();
        }

        if (c.source_maps.quoted_contents_tasks.len > 0) {
            debug(" START {d} source maps (quoted contents)", [ chunks.len ]);
            defer debug("  DONE {d} source maps (quoted contents)", [ chunks.len ]);
            c.source_maps.quoted_contents_wait_group.wait();
            c.allocator().free(c.source_maps.quoted_contents_tasks);
            c.source_maps.quoted_contents_tasks.len = 0;
        }

        // A part that failed to print (e.g. the recursion guard tripped on a
        // deeply nested AST) must fail the build instead of joining the chunk
        // as silently truncated output. Dev server excluded: its callers turn
        // any `Err` here into an OOM panic (see `finishFromBakeDevServer`),
        // so unprintable parts keep the old dropped-code behavior there.
        if (!is_dev_server) {
            let had_print_error = false;
            // Without code splitting a failing file is printed once per chunk
            // that includes it; report each file once.
            let reported_sources = AutoBitSet.initEmpty(c.allocator(), c.parse_graph.input_files.len)?;
            defer reported_sources.deinit(c.allocator());
            for (chunk of chunks) {
                for (compile_result of chunk.compile_results_for_chunk) {
                    const message: string = switch (compile_result) {
                        .javascript => |js| switch (js.result) {
                            .err => |e| if (e == error.StackOverflow)
                                "Maximum call stack size exceeded while generating code for this file"
                            else
                                std.fmt.allocPrint(c.allocator(), "Failed to generate code for this file ({s})", .{@errorName(e)}) catch "Failed to generate code for this file",
                            else => continue,
                        },
                        .css => |css| switch (css.result) {
                            .err => |e| std.fmt.allocPrint(c.allocator(), "Failed to generate CSS for this file ({s})", .{@errorName(e)}) catch "Failed to generate CSS for this file",
                            else => continue,
                        },
                        else => continue,
                    };
                    had_print_error = true;
                    const source_index = compile_result.sourceIndex();
                    const source: (*const Logger.Source)? = if (source_index != Index.invalid.get()) brk: {
                        if (reported_sources.isSet(source_index)) continue;
                        reported_sources.set(source_index);
                        break :brk &c.parse_graph.input_files.items(.source)[source_index];
                    } else null;
                    c.log.addError(source, Logger.Loc.Empty, message) catch {};
                }
            }
            if (had_print_error) {
                return error.PrintError;
            }
        }

        // For dev server, only post-process CSS + HTML chunks.
        const chunks_to_do = if (is_dev_server) chunks[1..] else chunks;
        if (!is_dev_server || chunks_to_do.len > 0) {
            bun.assert(chunks_to_do.len > 0);
            debug(" START {d} postprocess chunks", [ chunks_to_do.len ]);
            defer debug("  DONE {d} postprocess chunks", [ chunks_to_do.len ]);

            c.parse_graph.pool.worker_pool.eachPtr(
                c.allocator(),
                chunk_contexts[0],
                generateChunk,
                chunks_to_do,
            )?;
        }
    }

    // When bake.DevServer is in use, we're going to take a different code path at the end.
    // We want to extract the source code of each part instead of combining it into a single file.
    // This is so that when hot-module updates happen, we can:
    //
    // - Reuse unchanged parts to assemble the full bundle if Cmd+R is used in the browser
    // - Send only the newly changed code through a socket.
    // - Use IncrementalGraph to have full knowledge of referenced CSS files.
    //
    // When this isn't the initial bundle, concatenation as usual would produce a
    // broken module. It is DevServer's job to create and send HMR patches.
    if (is_dev_server) return;

    // TODO: enforceNoCyclicChunkImports()
    {
        let path_names_map = bun.StringHashMap(void).init(c.allocator());
        const _guard_path_names = DeinitGuard<bun.StringHashMap(void)>{ ptr: &path_names_map };

        let duplicates_map: bun.StringArrayHashMapUnmanaged(DuplicateEntry) = {};
        const _guard_duplicates = DuplicatesMapGuard{ map: &duplicates_map };

        let chunk_visit_map = AutoBitSet.initEmpty(c.allocator(), chunks.len)?;
        const _guard_visit_map = DeinitWithAllocGuard<AutoBitSet>{ ptr: &chunk_visit_map, allocator: c.allocator() };

        // Compute the final hashes of each chunk, then use those to create the final
        // paths of each chunk. This can technically be done in parallel but it
        // probably doesn't matter so much because we're not hashing that much data.
        let index: usize = 0;
        while (index < chunks.len) : (index += 1) {
            const chunk = &chunks[index];
            let hash: ContentHasher = {};
            c.appendIsolatedHashesForImportedChunks(&hash, chunks, @as(u32, @intCast(index)), &chunk_visit_map);
            chunk_visit_map.setAll(false);
            chunk.template.placeholder.hash = hash.digest();

            const rel_path = bun.handleOom(std.fmt.allocPrint(c.allocator(), "{f}", [ chunk.template ]));
            bun.path.platformToPosixInPlace(u8, rel_path);

            const gop = path_names_map.getOrPut(rel_path)?;
            if (gop.found_existing) {
                // collect all duplicates in a list
                const dup = duplicates_map.getOrPut(bun.default_allocator, rel_path)?;
                if (!dup.found_existing) dup.value_ptr.* = {};
                dup.value_ptr.sources.append(bun.default_allocator, chunk)?;
                continue;
            }

            // resolve any /./ and /../ occurrences
            // use resolvePosix since we asserted above all seps are '/'
            if (Environment.isWindows && std.mem.indexOf(u8, rel_path, "/./") != null) {
                let buf: bun.PathBuffer = undefined;
                const rel_path_fixed = bun.handleOom(c.allocator().dupe(u8, bun.path.normalizeBuf(rel_path, &buf, .posix)));
                chunk.final_rel_path = rel_path_fixed;
                continue;
            }

            chunk.final_rel_path = rel_path;
        }

        if (duplicates_map.count() > 0) {
            let msg = std.array_list.Managed(u8).init(bun.default_allocator);
            const _guard_msg = DeinitGuard<std.array_list.Managed(u8)>{ ptr: &msg };

            let entry_naming: ([]const u8)? = null;
            let chunk_naming: ([]const u8)? = null;
            let asset_naming: ([]const u8)? = null;

            const writer = msg.writer();
            writer.writeAll("Multiple files share the same output path\n")?;

            const kinds = c.graph.files.items(.entry_point_kind);

            const dup_keys = duplicates_map.keys();
            const dup_values = duplicates_map.values();
            let di: usize = 0;
            while (di < dup_keys.len) : (di += 1) {
                const key = dup_keys[di];
                const dup = dup_values[di];
                writer.print("  {s}:\n", [ key ])?;
                for (chunk of dup.sources.items) {
                    if (chunk.entry_point.is_entry_point) {
                        if (kinds[chunk.entry_point.source_index] == .user_specified) {
                            entry_naming = chunk.template.data;
                        } else {
                            chunk_naming = chunk.template.data;
                        }
                    } else {
                        asset_naming = chunk.template.data;
                    }

                    const source_index = chunk.entry_point.source_index;
                    const file: Logger.Source = c.parse_graph.input_files.items(.source)[source_index];
                    writer.print("    from input {s}\n", [ file.path.pretty ])?;
                }
            }

            c.log.addError(null, Logger.Loc.Empty, msg.toOwnedSlice()?)?;

            inline for (x of [
                { name: "entry", template: entry_naming },
                { name: "chunk", template: chunk_naming },
                { name: "asset", template: asset_naming },
            ]) {
                if (template := x.template) {
                    const name = x.name;

                    c.log.addMsg({
                        kind: .note,
                        data: {
                            text: std.fmt.allocPrint(bun.default_allocator, name ++ " naming is '{s}', consider adding '[hash]' to make filenames unique", [ template ])?,
                        },
                    }) catch e => bun.handleOom(e);
                }
            }

            return error.DuplicateOutputPath;
        }
    }

    // After final_rel_path is computed for all chunks, fix up module_info
    // cross-chunk import specifiers. During printing, cross-chunk imports use
    // unique_key placeholders as paths. Now that final paths are known, replace
    // those placeholders with the resolved paths and serialize.
    if (c.options.generate_bytecode_cache && c.options.output_format == .esm && c.options.compile) {
        // Build map from unique_key -> final resolved path
        const b: *bun.bundle_v2.BundleV2 = @fieldParentPtr("linker", c);
        let unique_key_to_path = bun.StringHashMap(string).init(c.allocator());
        const _guard_unique_key = DeinitGuard<bun.StringHashMap(string)>{ ptr: &unique_key_to_path };
        for (*ch of chunks) {
            if (ch.unique_key.len > 0 && ch.final_rel_path.len > 0) {
                // Use the per-chunk public_path to match what IntermediateOutput.code()
                // uses during emission (browser chunks from server builds use the
                // browser transpiler's public_path).
                const public_path = if (ch.flags.is_browser_chunk_from_server_build)
                    b.transpilerForTarget(.browser).options.public_path
                else
                    c.options.public_path;
                const normalizer = bun.bundle_v2.cheapPrefixNormalizer(public_path, ch.final_rel_path);
                const resolved = std.fmt.allocPrint(c.allocator(), "{s}{s}", [ normalizer[0], normalizer[1] ]) catch e => bun.handleOom(e);
                unique_key_to_path.put(ch.unique_key, resolved) catch e => bun.handleOom(e);
            }
        }

        // Fix up each chunk's module_info
        for (*chunk of chunks) {
            if (chunk.content != .javascript) continue;
            const mi = chunk.content.javascript.module_info ?? continue;

            // Collect replacements first (can't modify string table while iterating)
            let replacements: std.ArrayListUnmanaged(ModuleInfoReplacement) = {};
            const _guard_replacements = DeinitWithAllocGuard<std.ArrayListUnmanaged(ModuleInfoReplacement)>{ ptr: &replacements, allocator: c.allocator() };

            let offset: usize = 0;
            for (string_index of 0..mi.strings_lens.items.len) {
                const slen = mi.strings_lens.items[string_index];
                const len: usize = @as(usize, @intCast(slen));
                const s = mi.strings_buf.items[offset..][0..len];
                if (resolved_path := unique_key_to_path.get(s)) {
                    replacements.append(c.allocator(), ModuleInfoReplacement{
                        old_id: @enumFromInt(@as(u32, @intCast(string_index))),
                        resolved_path: resolved_path,
                    }) catch e => bun.handleOom(e);
                }
                offset += len;
            }

            for (rep of replacements.items) {
                const new_id = mi.str(rep.resolved_path) catch e => bun.handleOom(e);
                mi.replaceStringID(rep.old_id, new_id);
            }

            // Serialize the fixed-up module_info
            chunk.content.javascript.module_info_bytes = bun.js_printer.serializeModuleInfo(mi);

            // Free the ModuleInfo now that it's been serialized to bytes.
            // It was allocated with bun.default_allocator (not the arena),
            // so it must be explicitly destroyed.
            mi.destroy();
            chunk.content.javascript.module_info = null;
        }
    }

    let output_files = OutputFileListBuilder.init(bun.default_allocator, c, chunks, c.parse_graph.additional_output_files.items.len)?;

    const root_path = c.resolver.opts.output_dir;
    const is_standalone = c.options.compile_to_standalone_html;
    const more_than_one_output = !is_standalone && (c.parse_graph.additional_output_files.items.len > 0 || c.options.generate_bytecode_cache || (has_css_chunk && has_js_chunk) || (has_html_chunk && (has_js_chunk || has_css_chunk)));

    if (!c.resolver.opts.compile && more_than_one_output && !c.resolver.opts.supports_multiple_outputs) {
        c.log.addError(null, Logger.Loc.Empty, "cannot write multiple output files without an output directory")?;
        return error.MultipleOutputFilesWithoutOutputDir;
    }

    const bundler: *bun.bundle_v2.BundleV2 = @fieldParentPtr("linker", c);
    let static_route_visitor = StaticRouteVisitor{ c: c, visited: bun.handleOom(bun.bit_set.AutoBitSet.initEmpty(bun.default_allocator, c.graph.files.len)) };
    const _guard_route_visitor = DeinitGuard<StaticRouteVisitor>{ ptr: &static_route_visitor };

    // For standalone mode, resolve JS/CSS chunks so we can inline their content into HTML.
    // Closing tag escaping (</script → <\\/script, </style → <\\/style) is handled during
    // the HTML assembly step in codeWithSourceMapShifts, not here.
    let standalone_chunk_contents: ([]string?)? = null;
    const _guard_scc = StandaloneChunkContentsGuard{ scc: &standalone_chunk_contents };

    if (is_standalone) {
        let scc = bun.handleOom(bun.default_allocator.alloc(OptionalString, chunks.len));
        @memset(scc, null);
        standalone_chunk_contents = scc;

        for (ci of 0..chunks.len) {
            const chunk_item = &chunks[ci];
            if (chunk_item.content == .html) continue;
            let ds: usize = 0;
            // Pass `scc` so that `.asset` pieces (e.g. `import logo from "./logo.svg"` with
            // the file loader) are resolved to data: URIs from `url_for_css` instead of
            // being written as paths to sidecar files that don't exist in standalone mode.
            // Sibling JS/CSS chunks may still be null at this point; `.chunk` pieces fall
            // back to file paths when their entry in `scc` is null, matching the previous
            // behavior for inter-chunk imports.
            scc[ci] = (chunk_item.intermediate_output.codeStandalone(
                null,
                c.parse_graph,
                &c.graph,
                c.options.public_path,
                chunk_item,
                chunks,
                &ds,
                false,
                false,
                scc,
            ) catch e => bun.handleOom(e)).buffer;
        }
    }

    // Don't write to disk if compile mode is enabled - we need buffer values for compilation
    const is_compile = bundler.transpiler.options.compile;
    if (root_path.len > 0 && !is_compile) {
        c.writeOutputFilesToDisk(root_path, chunks, &output_files, standalone_chunk_contents)?;
    } else {
        // In-memory build (also used for standalone mode)
        for (chunk_index_in_chunks_list of 0..chunks.len) {
            const chunk = &chunks[chunk_index_in_chunks_list];
            // In standalone mode, non-HTML chunks were already resolved in the first pass.
            // Insert a placeholder output file to keep chunk indices aligned.
            if (is_standalone && chunk.content != .html) {
                * = output_files.insertForChunk(options.OutputFile.init({
                    data: { buffer: { data: [], allocator: bun.default_allocator } },
                    hash: null,
                    loader: chunk.content.loader(),
                    input_path: "",
                    display_size: 0,
                    output_kind: .chunk,
                    input_loader: .js,
                    output_path: "",
                    is_executable: false,
                    source_map_index: null,
                    bytecode_index: null,
                    module_info_index: null,
                    side: .client,
                    entry_point_index: null,
                    referenced_css_chunks: [],
                    bake_extra: {},
                }));
                continue;
            }

            let display_size: usize = 0;

            const public_path = if (chunk.flags.is_browser_chunk_from_server_build)
                bundler.transpilerForTarget(.browser).options.public_path
            else
                c.options.public_path;

            const _code_result = if (is_standalone && chunk.content == .html)
                chunk.intermediate_output.codeStandalone(
                    null,
                    c.parse_graph,
                    &c.graph,
                    public_path,
                    chunk,
                    chunks,
                    &display_size,
                    false,
                    false,
                    standalone_chunk_contents.?,
                )
            else
                chunk.intermediate_output.code(
                    null,
                    c.parse_graph,
                    &c.graph,
                    public_path,
                    chunk,
                    chunks,
                    &display_size,
                    c.resolver.opts.compile && !chunk.flags.is_browser_chunk_from_server_build,
                    chunk.content.sourcemap(c.options.source_maps) != .none,
                );
            let code_result = _code_result catch e => bun.handleOom(e);

            let sourcemap_output_file: options.OutputFile? = null;
            const input_path = bun.default_allocator.dupe(
                u8,
                if (chunk.entry_point.is_entry_point)
                    c.parse_graph.input_files.items(.source)[chunk.entry_point.source_index].path.text
                else
                    chunk.final_rel_path,
            )?;

            const sm_kind = chunk.content.sourcemap(c.options.source_maps);
            switch (sm_kind) {
                .external, .linked => {
                    const output_source_map = chunk.output_source_map.finalize(bun.default_allocator, code_result.shifts) catch @panic("Failed to allocate memory for external source map");
                    let source_map_final_rel_path = bun.handleOom(bun.default_allocator.alloc(u8, chunk.final_rel_path.len + ".map".len));
                    bun.copy(u8, source_map_final_rel_path, chunk.final_rel_path);
                    bun.copy(u8, source_map_final_rel_path[chunk.final_rel_path.len..], ".map");

                    if (sm_kind == .linked) {
                        const norm: [2][]const u8 = if (public_path.len > 0)
                            cheapPrefixNormalizer(public_path, source_map_final_rel_path)
                        else
                            @as([2][]const u8, [ "", std.fs.path.basename(source_map_final_rel_path) ]);
                        const a = norm[0];
                        const b = norm[1];

                        const source_map_start = "//# sourceMappingURL=";
                        const total_len = code_result.buffer.len + source_map_start.len + a.len + b.len + "\n".len;
                        let buf = std.array_list.Managed(u8).initCapacity(Chunk.IntermediateOutput.allocatorForSize(total_len), total_len) catch @panic("Failed to allocate memory for output file with inline source map");
                        buf.appendSliceAssumeCapacity(code_result.buffer);
                        buf.appendSliceAssumeCapacity(source_map_start);
                        buf.appendSliceAssumeCapacity(a);
                        buf.appendSliceAssumeCapacity(b);
                        buf.appendAssumeCapacity('\n');

                        Chunk.IntermediateOutput.allocatorForSize(code_result.buffer.len).free(code_result.buffer);
                        code_result.buffer = buf.items;
                    }

                    sourcemap_output_file = options.OutputFile.init({
                        data: {
                            buffer: {
                                data: output_source_map,
                                allocator: bun.default_allocator,
                            },
                        },
                        hash: null,
                        loader: .json,
                        input_loader: .file,
                        output_path: source_map_final_rel_path,
                        output_kind: .sourcemap,
                        input_path: strings.concat(bun.default_allocator, &[ input_path, ".map" ])?,
                        side: null,
                        entry_point_index: null,
                        is_executable: false,
                    });
                },
                .@"inline" => {
                    const output_source_map = chunk.output_source_map.finalize(bun.default_allocator, code_result.shifts) catch @panic("Failed to allocate memory for external source map");
                    const _guard_osm = FreeGuard<u8>{ allocator: bun.default_allocator, slice: output_source_map };
                    const encode_len = base64.encodeLen(output_source_map);

                    const source_map_start = "//# sourceMappingURL=data:application/json;base64,";
                    const total_len = code_result.buffer.len + source_map_start.len + encode_len + 1;
                    let buf = std.array_list.Managed(u8).initCapacity(Chunk.IntermediateOutput.allocatorForSize(total_len), total_len) catch @panic("Failed to allocate memory for output file with inline source map");

                    buf.appendSliceAssumeCapacity(code_result.buffer);
                    buf.appendSliceAssumeCapacity(source_map_start);

                    buf.items.len += encode_len;
                    * = base64.encode(buf.items[buf.items.len - encode_len ..], output_source_map);

                    buf.appendAssumeCapacity('\n');
                    Chunk.IntermediateOutput.allocatorForSize(code_result.buffer.len).free(code_result.buffer);
                    code_result.buffer = buf.items;
                },
                .none => {},
            }

            // Compute side early so it can be used for bytecode, module_info, and main chunk output files
            const side: bun.bake.Side = if (chunk.content == .css || chunk.flags.is_browser_chunk_from_server_build)
                .client
            else switch (c.graph.ast.items(.target)[chunk.entry_point.source_index]) {
                .browser => .client,
                else => .server,
            };

            const bytecode_output_file: options.OutputFile? = brk: {
                if (c.options.generate_bytecode_cache) {
                    const loader: Loader = if (chunk.entry_point.is_entry_point)
                        c.parse_graph.input_files.items(.loader)[
                            chunk.entry_point.source_index
                        ]
                    else
                        .js;

                    if (chunk.content == .javascript && loader.isJavaScriptLike()) {
                        jsc.VirtualMachine.is_bundler_thread_for_bytecode_cache = true;
                        jsc.initialize(false);
                        let fdpath: bun.PathBuffer = undefined;
                        // For --compile builds, the bytecode URL must match the module name
                        // that will be used at runtime. The module name is:
                        //   public_path + final_rel_path (e.g., "/$bunfs/root/app.js")
                        // Without this prefix, the JSC bytecode cache key won't match at runtime.
                        // Use the per-chunk public_path (already computed above) for browser chunks
                        // from server builds, and normalize with cheapPrefixNormalizer for consistency
                        // with module_info path fixup.
                        // For non-compile builds, use the normal .jsc extension.
                        let source_provider_url = if (c.options.compile) url_blk: {
                            const normalizer = bun.bundle_v2.cheapPrefixNormalizer(public_path, chunk.final_rel_path);
                            break :url_blk bun.String.createFormat("{s}{s}", [ normalizer[0], normalizer[1] ])?;
                        } else bun.String.createFormat("{s}" ++ bun.bytecode_extension, [ chunk.final_rel_path ])?;
                        source_provider_url.ref();
                        const _guard_spu = DerefGuard<bun.String>{ ptr: &source_provider_url };

                        if (result := jsc.CachedBytecode.generate(c.options.output_format, code_result.buffer, &source_provider_url)) {
                            const bytecode = result[0];
                            const cached_bytecode = result[1];
                            let source_provider_url_str = source_provider_url.toSlice(bun.default_allocator);
                            const _guard_spu_str = DeinitGuard<@TypeOf(source_provider_url_str)>{ ptr: &source_provider_url_str };
                            debug("Bytecode cache generated {s}: {f}", [ source_provider_url_str.slice(), bun.fmt.size(bytecode.len, { space_between_number_and_unit: true }) ]);
                            @memcpy(fdpath[0..chunk.final_rel_path.len], chunk.final_rel_path);
                            fdpath[chunk.final_rel_path.len..][0..bun.bytecode_extension.len].* = bun.bytecode_extension.*;

                            break :brk options.OutputFile.init({
                                output_path: bun.handleOom(bun.default_allocator.dupe(u8, source_provider_url_str.slice())),
                                input_path: bun.handleOom(std.fmt.allocPrint(bun.default_allocator, "{s}" ++ bun.bytecode_extension, [ chunk.final_rel_path ])),
                                input_loader: .js,
                                hash: if (chunk.template.placeholder.hash != null) bun.hash(bytecode) else null,
                                output_kind: .bytecode,
                                loader: .file,
                                size: @as(u32, @truncate(bytecode.len)),
                                display_size: @as(u32, @truncate(bytecode.len)),
                                data: {
                                    buffer: { data: bytecode, allocator: cached_bytecode.allocator() },
                                },
                                side: side,
                                entry_point_index: null,
                                is_executable: false,
                            });
                        } else {
                            // an error
                            c.log.addErrorFmt(null, Logger.Loc.Empty, bun.default_allocator, "Failed to generate bytecode for {s}", [ 
                                chunk.final_rel_path,
                            ]) catch e => bun.handleOom(e);
                        }
                    }
                }

                break :brk null;
            };

            // Create module_info output file for ESM bytecode in --compile builds
            const module_info_output_file: options.OutputFile? = mi_brk: {
                if (c.options.generate_bytecode_cache && c.options.output_format == .esm && c.options.compile) {
                    const loader: Loader = if (chunk.entry_point.is_entry_point)
                        c.parse_graph.input_files.items(.loader)[
                            chunk.entry_point.source_index
                        ]
                    else
                        .js;

                    if (chunk.content == .javascript && loader.isJavaScriptLike()) {
                        if (module_info_bytes := chunk.content.javascript.module_info_bytes) {
                            break :mi_brk options.OutputFile.init({
                                output_path: bun.handleOom(std.fmt.allocPrint(bun.default_allocator, "{s}.module-info", [ chunk.final_rel_path ])),
                                input_path: bun.handleOom(std.fmt.allocPrint(bun.default_allocator, "{s}.module-info", [ chunk.final_rel_path ])),
                                input_loader: .js,
                                hash: if (chunk.template.placeholder.hash != null) bun.hash(module_info_bytes) else null,
                                output_kind: .module_info,
                                loader: .file,
                                size: @as(u32, @truncate(module_info_bytes.len)),
                                display_size: @as(u32, @truncate(module_info_bytes.len)),
                                data: {
                                    buffer: { data: module_info_bytes, allocator: bun.default_allocator },
                                },
                                side: side,
                                entry_point_index: null,
                                is_executable: false,
                            });
                        }
                    }
                }
                break :mi_brk null;
            };

            const source_map_index: u32? = if (sourcemap_output_file != null)
                output_files.insertForSourcemapOrBytecode(sourcemap_output_file.?)?
            else
                null;

            const bytecode_index: u32? = if (bytecode_output_file != null)
                output_files.insertForSourcemapOrBytecode(bytecode_output_file.?)?
            else
                null;

            const module_info_index: u32? = if (module_info_output_file != null)
                output_files.insertForSourcemapOrBytecode(module_info_output_file.?)?
            else
                null;

            const output_kind = if (chunk.content == .css)
                .asset
            else if (chunk.entry_point.is_entry_point)
                c.graph.files.items(.entry_point_kind)[chunk.entry_point.source_index].outputKind()
            else
                .chunk;

            let entry_point_offset: u32 = 1;
            if (fw := c.framework) {
                if (fw.server_components != null) {
                    entry_point_offset = 3;
                }
            }
            const entry_point_index: u32? = if (output_kind == .@"entry-point")
                chunk.entry_point.source_index - entry_point_offset
            else
                null;

            chunk.final_output_size = code_result.buffer.len;
            const chunk_index = output_files.insertForChunk(options.OutputFile.init({
                data: {
                    buffer: {
                        data: code_result.buffer,
                        allocator: Chunk.IntermediateOutput.allocatorForSize(code_result.buffer.len),
                    },
                },
                hash: chunk.template.placeholder.hash,
                loader: chunk.content.loader(),
                input_path: input_path,
                display_size: @as(u32, @truncate(display_size)),
                output_kind: output_kind,
                input_loader: if (chunk.entry_point.is_entry_point) c.parse_graph.input_files.items(.loader)[chunk.entry_point.source_index] else .js,
                output_path: bun.default_allocator.dupe(u8, chunk.final_rel_path)?,
                is_executable: chunk.flags.is_executable,
                source_map_index: source_map_index,
                bytecode_index: bytecode_index,
                module_info_index: module_info_index,
                side: side,
                entry_point_index: entry_point_index,
                referenced_css_chunks: switch (chunk.content) {
                    .javascript js => unsafe { @ptrCast(bun.default_allocator.dupe(u32, js.css_chunks)?) },
                    .css => [],
                    .html => [],
                },
                bake_extra: brk: {
                    if (c.framework == null || is_dev_server) break :brk {};
                    if (!c.framework.?.is_built_in_react) break :brk {};

                    let extra: OutputFile.BakeExtra = {};
                    extra.bake_is_runtime = chunk.files_with_parts_in_chunk.contains(Index.runtime.get());
                    if (output_kind == .@"entry-point" && side == .server) {
                        extra.is_route = true;
                        extra.fully_static = !static_route_visitor.hasTransitiveUseClient(chunk.entry_point.source_index);
                    }

                    break :brk extra;
                },
            }));

            // We want the chunk index to remain the same in `output_files` so the indices in `OutputFile.referenced_css_chunks` work
            bun.assertf(chunk_index == chunk_index_in_chunks_list, "chunk_index ({d}) != chunk_index_in_chunks_list ({d})", [ chunk_index, chunk_index_in_chunks_list ]);
        }

        if (!is_standalone) {
            output_files.insertAdditionalOutputFiles(c.parse_graph.additional_output_files.items);
        }
    }

    if (is_standalone) {
        // For standalone mode, filter to only HTML output files.
        // Deinit dropped items to free their heap allocations (paths, buffers).
        let result = output_files.take();
        let write_idx: usize = 0;
        for (*item of result.items) {
            if (item.loader == .html) {
                result.items[write_idx] = item;
                write_idx += 1;
            } else {
                item.deinit();
            }
        }
        result.items.len = write_idx;
        return result;
    }

    return output_files.take();
}

pub const ThreadPool = bun.bundle_v2.ThreadPool;

const debugPartRanges = Output.scoped(.PartRanges, .hidden);

import analyze_transpiled_module from "../analyze_transpiled_module.zig";

import bun from "bun";
const Environment = bun.Environment;
const Loader = bun.Loader;
const Output = bun.Output;
const ThreadPoolLib = bun.ThreadPool;
const base64 = bun.base64;
const jsc = bun.jsc;
const strings = bun.strings;
const AutoBitSet = bun.bit_set.AutoBitSet;

const Chunk = bun.bundle_v2.Chunk;
const ContentHasher = bun.bundle_v2.ContentHasher;
const Index = bun.bundle_v2.Index;
const Part = bun.bundle_v2.Part;
const cheapPrefixNormalizer = bun.bundle_v2.cheapPrefixNormalizer;

const LinkerContext = bun.bundle_v2.LinkerContext;
const CompileResult = LinkerContext.CompileResult;
const GenerateChunkCtx = LinkerContext.GenerateChunkCtx;
const OutputFileListBuilder = bun.bundle_v2.LinkerContext.OutputFileListBuilder;
const PendingPartRange = LinkerContext.PendingPartRange;
const StaticRouteVisitor = bun.bundle_v2.LinkerContext.StaticRouteVisitor;
const debug = LinkerContext.debug;
const generateChunk = LinkerContext.generateChunk;
const generateCompileResultForCssChunk = LinkerContext.generateCompileResultForCssChunk;
const generateCompileResultForHtmlChunk = LinkerContext.generateCompileResultForHtmlChunk;
const generateCompileResultForJSChunk = LinkerContext.generateCompileResultForJSChunk;

const Logger = bun.logger;
const Loc = Logger.Loc;

const options = bun.options;
const OutputFile = bun.options.OutputFile;

pub struct DuplicateEntry {
    sources: std.ArrayListUnmanaged(*Chunk) = {},
}

pub struct ModuleInfoReplacement {
    old_id: analyze_transpiled_module.StringID,
    resolved_path: string,
}
