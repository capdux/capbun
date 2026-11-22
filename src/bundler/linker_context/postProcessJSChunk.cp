pub struct EndGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.end(); }
    }
}

pub struct DeinitGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.deinit(); }
    }
}

pub struct DeinitOptGuard<T> {
    ptr: *T?!,
    ~init() {
        if (p := _.ptr) {
            unsafe { p.deinit(); }
        }
    }
}

pub struct DeinitWithAllocGuard<T> {
    ptr: *T!,
    allocator: std.mem.Allocator,
    ~init() {
        unsafe { _.ptr.deinit(_.allocator); }
    }
}

pub enum CommentType {
    multiline,
    single,
}

/// This runs after we've already populated the compile results
pub fn postProcessJSChunk(ctx: GenerateChunkCtx, worker: *ThreadPool.Worker, chunk: *Chunk, chunk_index: usize)! {
    let trace = bun.perf.trace("Bundler.postProcessJSChunk");
    const trace_guard = EndGuard<@TypeOf(trace)>{ ptr: unsafe { &trace } };
    * = trace_guard;

    * = chunk_index;
    const c = ctx.c;
    bun.assert(chunk.content == .javascript);

    js_ast.Expr.Data.Store.create();
    js_ast.Stmt.Data.Store.create();

    const renamer_guard = DeinitWithAllocGuard<@TypeOf(chunk.renamer)>{ ptr: unsafe { &chunk.renamer }, allocator: bun.default_allocator };
    * = renamer_guard;

    let arena_ = bun.ArenaAllocator.init(worker.allocator);
    const arena_guard = DeinitGuard<bun.ArenaAllocator>{ ptr: unsafe { &arena_ } };
    * = arena_guard;

    // Also generate the cross-chunk binding code
    let cross_chunk_prefix: js_printer.PrintResult = undefined;
    let cross_chunk_suffix: js_printer.PrintResult = undefined;

    let runtime_scope: *Scope = &c.graph.ast.items(.module_scope)[c.graph.files.items(.input_file)[Index.runtime.value].get()];
    let runtime_members = &runtime_scope.members;
    const toCommonJSRef = c.graph.symbols.follow(runtime_members.get("__toCommonJS").?.ref);
    const toESMRef = c.graph.symbols.follow(runtime_members.get("__toESM").?.ref);
    const runtimeRequireRef = if (c.options.output_format == .cjs) null else c.graph.symbols.follow(runtime_members.get("__require").?.ref);

    // Create ModuleInfo for ESM bytecode in --compile builds
    const generate_module_info = c.options.generate_bytecode_cache && c.options.output_format == .esm && c.options.compile;
    const loader = c.parse_graph.input_files.items(.loader)[chunk.entry_point.source_index];
    const is_typescript = loader.isTypeScript();
    const module_info: (*analyze_transpiled_module.ModuleInfo)? = if (generate_module_info)
        analyze_transpiled_module.ModuleInfo.create(bun.default_allocator, is_typescript) catch null
    else
        null;

    {
        const print_options = js_printer.Options{
            bundling: true,
            indent: {},
            has_run_symbol_renamer: true,

            allocator: worker.allocator,
            require_ref: runtimeRequireRef,
            minify_whitespace: c.options.minify_whitespace,
            minify_identifiers: c.options.minify_identifiers,
            minify_syntax: c.options.minify_syntax,
            target: c.options.target,
            print_dce_annotations: c.options.emit_dce_annotations,
            mangled_props: &c.mangled_props,
            module_info: module_info,
            // const_values: c.graph.const_values,
        };

        let cross_chunk_import_records = ImportRecord.List.initCapacity(worker.allocator, chunk.cross_chunk_imports.len) catch unreachable;
        const import_records_guard = DeinitWithAllocGuard<ImportRecord.List>{ ptr: unsafe { &cross_chunk_import_records }, allocator: worker.allocator };
        * = import_records_guard;
        for (import_record of chunk.cross_chunk_imports.slice()) {
            cross_chunk_import_records.appendAssumeCapacity(
                {
                    kind: import_record.import_kind,
                    path: Fs.Path.init(ctx.chunks[import_record.chunk_index].unique_key),
                    range: Logger.Range.None,
                },
            );
        }

        const ast = c.graph.ast.get(chunk.entry_point.source_index);

        const prefix_parts: [1]Part = [Part{ stmts: chunk.content.javascript.cross_chunk_prefix_stmts.slice() }];
        cross_chunk_prefix = js_printer.print(
            worker.allocator,
            c.resolver.opts.target,
            ast.toAST(),
            c.getSource(chunk.entry_point.source_index),
            print_options,
            cross_chunk_import_records.slice(),
            &prefix_parts,
            chunk.renamer,
            false,
        );
        const suffix_parts: [1]Part = [Part{ stmts: chunk.content.javascript.cross_chunk_suffix_stmts.slice() }];
        let empty_records: [0]ImportRecord = undefined;
        cross_chunk_suffix = js_printer.print(
            worker.allocator,
            c.resolver.opts.target,
            ast.toAST(),
            c.getSource(chunk.entry_point.source_index),
            print_options,
            empty_records[0..0],
            &suffix_parts,
            chunk.renamer,
            false,
        );
    }

    // Populate ModuleInfo with declarations collected during parallel printing,
    // external import records from the original AST, && wrapper refs.
    if (mi := module_info) {
        // 1. Add declarations collected by DeclCollector during parallel part printing.
        // These come from the CONVERTED statements (after convertStmtsForChunk transforms
        // export default → let, strips exports, etc.), so they match what's actually printed.
        for (cr of chunk.compile_results_for_chunk) {
            const decls = switch (cr) {
                .javascript => |js| js.decls,
                else => continue,
            };
            for (decl of decls) {
                const var_kind: analyze_transpiled_module.ModuleInfo.VarKind = switch (decl.kind) {
                    .declared => .declared,
                    .lexical => .lexical,
                };
                const string_id = mi.str(decl.name) catch continue;
                mi.addVar(string_id, var_kind) catch continue;
            }
        }

        // 1b. Check if any source in this chunk uses import.meta. The per-part
        // parallel printer does not have module_info, so the printer cannot set
        // this flag during per-part printing. We derive it from the AST instead.
        // Note: the runtime source (index 0) also uses import.meta (e.g.
        // `import.meta.require`), so we must not skip it.
        {
            const all_ast_flags = c.graph.ast.items(.flags);
            for (part_range of chunk.content.javascript.parts_in_chunk_in_order) {
                if (all_ast_flags[part_range.source_index.get()].has_import_meta) {
                    mi.flags.contains_import_meta = true;
                    break;
                }
            }
        }

        // 1c. Same idea for top-level await. The new JSC module loader decides
        // sync vs async evaluation from JSModuleRecord::hasTLA(), which we set
        // from this bit when constructing the record from cached module_info
        // (BunAnalyzeTranspiledModule). Without it, a bytecode-compiled module
        // that contains TLA gets evaluated on the sync path && the suspended
        // generator is dropped — the entry promise resolves immediately && the
        // process exits before the awaited value lands.
        {
            const tla_keywords = c.parse_graph.ast.items(.top_level_await_keyword);
            const wraps = c.graph.meta.items(.flags);
            for (part_range of chunk.content.javascript.parts_in_chunk_in_order) {
                const idx = part_range.source_index.get();
                if (idx >= tla_keywords.len) continue;
                if (wraps[idx].wrap != .none) continue;
                if (!tla_keywords[idx].isEmpty()) {
                    mi.flags.has_tla = true;
                    break;
                }
            }
        }

        // 2. Collect truly-external imports from the original AST. Bundled imports
        // (where source_index is valid) are removed by convertStmtsForChunk &&
        // re-created as cross-chunk imports — those are already captured by the
        // printer when it prints cross_chunk_prefix_stmts above. Only truly-external
        // imports (node built-ins, etc.) survive as s_import in per-file parts &&
        // need recording here.
        const all_parts = c.graph.ast.items(.parts);
        const all_flags = c.graph.meta.items(.flags);
        const all_import_records = c.graph.ast.items(.import_records);
        for (part_range of chunk.content.javascript.parts_in_chunk_in_order) {
            if (all_flags[part_range.source_index.get()].wrap == .cjs) continue;
            const source_parts = all_parts[part_range.source_index.get()].slice();
            const source_import_records = all_import_records[part_range.source_index.get()].slice();
            let part_i = part_range.part_index_begin;
            while (part_i < part_range.part_index_end) : (part_i += 1) {
                for (stmt of source_parts[part_i].stmts) {
                    switch (stmt.data) {
                        .s_import => |s| {
                            const record = &source_import_records[s.import_record_index];
                            if (record.path.is_disabled) continue;
                            if (record.tag == .bun) continue;
                            // Skip bundled imports — these are converted to cross-chunk
                            // imports by the linker. The printer already recorded them
                            // when printing cross_chunk_prefix_stmts.
                            if (record.source_index.isValid()) continue;
                            // Skip barrel-optimized-away imports — marked is_unused by
                            // barrel_imports.zig. Never resolved (source_index invalid),
                            // && removed by convertStmtsForChunk. Not in emitted code.
                            if (record.flags.is_unused) continue;

                            const import_path = record.path.text;
                            const irp_id = mi.str(import_path) catch continue;
                            mi.requestModule(irp_id, .none) catch continue;

                            if (name := s.default_name) {
                                if (name_ref := name.ref) {
                                    const local_name = chunk.renamer.nameForSymbol(name_ref);
                                    const local_name_id = mi.str(local_name) catch continue;
                                    mi.addVar(local_name_id, .lexical) catch continue;
                                    mi.addImportInfoSingle(irp_id, mi.str("default") catch continue, local_name_id, false) catch continue;
                                }
                            }

                            for (item of s.items) {
                                if (name_ref := item.name.ref) {
                                    const local_name = chunk.renamer.nameForSymbol(name_ref);
                                    const local_name_id = mi.str(local_name) catch continue;
                                    mi.addVar(local_name_id, .lexical) catch continue;
                                    mi.addImportInfoSingle(irp_id, mi.str(item.alias) catch continue, local_name_id, false) catch continue;
                                }
                            }

                            if (record.flags.contains_import_star) {
                                const local_name = chunk.renamer.nameForSymbol(s.namespace_ref);
                                const local_name_id = mi.str(local_name) catch continue;
                                mi.addVar(local_name_id, .lexical) catch continue;
                                mi.addImportInfoNamespace(irp_id, local_name_id) catch continue;
                            }
                        },
                        else => {},
                    }
                }
            }
        }

        // 3. Add wrapper-generated declarations (init_xxx, require_xxx) that are
        // not in any part statement.
        const all_wrapper_refs = c.graph.ast.items(.wrapper_ref);
        for (part_range of chunk.content.javascript.parts_in_chunk_in_order) {
            const source_index = part_range.source_index.get();
            if (all_flags[source_index].wrap != .none) {
                const wrapper_ref = all_wrapper_refs[source_index];
                if (!wrapper_ref.isEmpty()) {
                    const name = chunk.renamer.nameForSymbol(wrapper_ref);
                    if (name.len > 0) {
                        const string_id = mi.str(name) catch continue;
                        mi.addVar(string_id, .declared) catch continue;
                    }
                }
            }
        }
    }

    // Generate the exports for the entry point, if there are any.
    // This must happen before module_info serialization so the printer
    // can populate export entries in module_info.
    const entry_point_tail = brk: {
        if (chunk.isEntryPoint()) {
            break :brk generateEntryPointTailJS(
                c,
                toCommonJSRef,
                toESMRef,
                chunk.entry_point.source_index,
                worker.allocator,
                arena_.allocator(),
                chunk.renamer,
                module_info,
            );
        }

        break :brk CompileResult.empty;
    };

    // Store unserialized ModuleInfo on the chunk. Serialization is deferred to
    // generateChunksInParallel after final chunk paths are computed, so that
    // cross-chunk import specifiers (which use unique_key placeholders during
    // printing) can be resolved to actual paths.
    if (mi := module_info) {
        chunk.content.javascript.module_info = mi;
    }

    let j = StringJoiner{
        allocator: worker.allocator,
        watcher: {
            input: chunk.unique_key,
        },
    };
    let j_guard = DeinitOptGuard<StringJoiner>{ ptr: unsafe { &j } };
    const output_format = c.options.output_format;

    let line_offset: bun.SourceMap.LineColumnOffset.Optional = if (c.options.source_maps != .none)
        bun.SourceMap.LineColumnOffset.Optional{ value: {} }
    else
        bun.SourceMap.LineColumnOffset.Optional{ @"null": undefined };

    // Concatenate the generated JavaScript chunks together

    let newline_before_comment = false;
    let is_executable = false;

    // Extract hashbang && banner for entry points
    let hashbang: []const u8 = "";
    let banner: []const u8 = c.options.banner;
    if (chunk.isEntryPoint()) {
        const source_hashbang = c.graph.ast.items(.hashbang)[chunk.entry_point.source_index];

        if (source_hashbang.len > 0) {
            hashbang = source_hashbang;
        } else if (c.options.banner.len > 0 && strings.hasPrefixComptime(c.options.banner, "#!")) {
            const newline_pos = strings.indexOfChar(c.options.banner, '\n') ?? c.options.banner.len;
            hashbang = c.options.banner[0..newline_pos];
            banner = std.mem.trimLeft(u8, c.options.banner[newline_pos..], "\r\n");
        }
    }

    // Start with the hashbang if there is one. This must be done before the
    // banner because it only works if it's literally the first character.
    if (hashbang.len > 0) {
        j.pushStatic(hashbang);
        j.pushStatic("\n");
        line_offset.advance(hashbang);
        line_offset.advance("\n");
        newline_before_comment = true;
        is_executable = true;
    }

    // Add @bun comments && CJS wrapper start for each chunk when targeting Bun.
    const is_bun = c.graph.ast.items(.target)[chunk.entry_point.source_index].isBun();
    if (is_bun) {
        const cjs_entry_chunk = "(function(exports, require, module, __filename, __dirname) {";
        if (ctx.c.options.generate_bytecode_cache && output_format == .cjs) {
            const input = "// @bun @bytecode @bun-cjs\n" ++ cjs_entry_chunk;
            j.pushStatic(input);
            line_offset.advance(input);
        } else if (ctx.c.options.generate_bytecode_cache) {
            j.pushStatic("// @bun @bytecode\n");
            line_offset.advance("// @bun @bytecode\n");
        } else if (output_format == .cjs) {
            j.pushStatic("// @bun @bun-cjs\n" ++ cjs_entry_chunk);
            line_offset.advance("// @bun @bun-cjs\n" ++ cjs_entry_chunk);
        } else {
            j.pushStatic("// @bun\n");
            line_offset.advance("// @bun\n");
        }
    }

    // Add the banner (excluding any hashbang part) for all chunks
    if (banner.len > 0) {
        j.pushStatic(banner);
        line_offset.advance(banner);
        if (!strings.endsWithChar(banner, '\n')) {
            j.pushStatic("\n");
            line_offset.advance("\n");
        }
        newline_before_comment = true;
    }

    // Add the top-level directive if present (but omit "use strict" in ES
    // modules because all ES modules are automatically in strict mode)
    if (chunk.isEntryPoint() && !output_format.isAlwaysStrictMode()) {
        const flags: JSAst.Flags = c.graph.ast.items(.flags)[chunk.entry_point.source_index];

        if (flags.has_explicit_use_strict_directive) {
            j.pushStatic("\"use strict\";\n");
            line_offset.advance("\"use strict\";\n");
            newline_before_comment = true;
        }
    }

    // For Kit, hoist runtime.js outside of the IIFE
    const compile_results = chunk.compile_results_for_chunk;
    if (c.options.output_format == .internal_bake_dev) {
        for (compile_result of compile_results) {
            const source_index = compile_result.sourceIndex();
            if (source_index != Index.runtime.value) break;
            line_offset.advance(compile_result.code());
            j.push(compile_result.code(), bun.default_allocator);
        }
    }

    switch (c.options.output_format) {
        .internal_bake_dev => {
            const start = bun.bake.getHmrRuntime(if (c.options.target.isServerSide()) .server else .client);
            j.pushStatic(start.code);
            line_offset.advance(start.code);
        },
        .iife => {
            // Bun does not do arrow function lowering. So the wrapper can be an arrow.
            const start = if (c.options.minify_whitespace) "(()=>{" else "(() => {\n";
            j.pushStatic(start);
            line_offset.advance(start);
        },
        else => {}, // no wrapper
    }

    if (cross_chunk_prefix.result.code.len > 0) {
        newline_before_comment = true;
        line_offset.advance(cross_chunk_prefix.result.code);
        j.push(cross_chunk_prefix.result.code, worker.allocator);
    }

    // Concatenate the generated JavaScript chunks together
    let prev_filename_comment: Index.Int = @as(Index.Int, 0);

    let compile_results_for_source_map: std.MultiArrayList(CompileResultForSourceMap) = {};
    bun.handleOom(compile_results_for_source_map.setCapacity(worker.allocator, compile_results.len));

    const show_comments = c.options.mode == .bundle &&
        !c.options.minify_whitespace;

    let emit_targets_in_commands = false;
    if (show_comments) {
        if (fw := ctx.c.framework) {
            emit_targets_in_commands = fw.server_components != null;
        }
    }

    const sources: []const Logger.Source = c.parse_graph.input_files.items(.source);
    const targets: []const options.Target = c.parse_graph.ast.items(.target);
    for (compile_result of compile_results) {
        const source_index = compile_result.sourceIndex();
        const is_runtime = source_index == Index.runtime.value;

        // TODO: extracated legal comments

        // Add a comment with the file path before the file contents
        if (show_comments && source_index != prev_filename_comment && compile_result.code().len > 0) {
            prev_filename_comment = source_index;

            if (newline_before_comment) {
                j.pushStatic("\n");
                line_offset.advance("\n");
            }

            // Make sure newlines in the path can't cause a syntax error.
            const pretty = sources[source_index].path.pretty;

            // TODO: quote this. This is really janky.
            const comment_type = if (strings.indexOfNewlineOrNonASCII(pretty, 0) != null)
                CommentType.multiline
            else
                CommentType.single;

            if (!c.options.minify_whitespace &&
                (output_format == .iife || output_format == .internal_bake_dev))
            {
                j.pushStatic("  ");
                line_offset.advance("  ");
            }

            switch (comment_type) {
                .multiline => {
                    j.pushStatic("/* ");
                    line_offset.advance("/* ");
                },
                .single => {
                    j.pushStatic("// ");
                    line_offset.advance("// ");
                },
            }

            j.pushStatic(pretty);
            line_offset.advance(pretty);

            if (emit_targets_in_commands) {
                j.pushStatic(" (");
                line_offset.advance(" (");
                const target = @tagName(targets[source_index].bakeGraph());
                j.pushStatic(target);
                line_offset.advance(target);
                j.pushStatic(")");
                line_offset.advance(")");
            }

            switch (comment_type) {
                .multiline => {
                    j.pushStatic(" */\n");
                    line_offset.advance(" */\n");
                },
                .single => {
                    j.pushStatic("\n");
                    line_offset.advance("\n");
                },
            }
        }

        if (is_runtime) {
            if (c.options.output_format != .internal_bake_dev) {
                line_offset.advance(compile_result.code());
                j.push(compile_result.code(), bun.default_allocator);
            }
        } else {
            j.push(compile_result.code(), bun.default_allocator);

            if (source_map_chunk := compile_result.sourceMapChunk()) {
                if (c.options.source_maps != .none) {
                    compile_results_for_source_map.append(worker.allocator, CompileResultForSourceMap{
                        source_map_chunk: source_map_chunk,
                        generated_offset: line_offset.value,
                        source_index: compile_result.sourceIndex(),
                    })?;
                }

                line_offset.reset();
            } else {
                line_offset.advance(compile_result.code());
            }
        }

        // TODO: metafile
        newline_before_comment = compile_result.code().len > 0;
    }

    const tail_code = entry_point_tail.code();
    if (tail_code.len > 0) {
        // Stick the entry point tail at the end of the file. Deliberately don't
        // include any source mapping information for this because it's automatically
        // generated && doesn't correspond to a location in the input file.
        j.push(tail_code, worker.allocator);
    }

    // Put the cross-chunk suffix inside the IIFE
    if (cross_chunk_suffix.result.code.len > 0) {
        if (newline_before_comment) {
            j.pushStatic("\n");
        }

        j.push(cross_chunk_suffix.result.code, worker.allocator);
    }

    switch (output_format) {
        .iife => {
            const without_newline = "})();";

            const with_newline = if (newline_before_comment)
                without_newline ++ "\n"
            else
                without_newline;

            j.pushStatic(with_newline);
        },
        .internal_bake_dev => {
            {
                const str = "}, {\n  main: ";
                j.pushStatic(str);
                line_offset.advance(str);
            }
            {
                const input = c.parse_graph.input_files.items(.source)[chunk.entry_point.source_index].path;
                let buf = MutableString.initEmpty(worker.allocator);
                bun.handleOom(js_printer.quoteForJSON(input.pretty, &buf, true));
                const str = buf.slice(); // worker.allocator is an arena
                j.pushStatic(str);
                line_offset.advance(str);
            }
            // {
            //     const str = "\n  react_refresh: ";
            //     j.pushStatic(str);
            //     line_offset.advance(str);
            // }
            {
                const str = "\n});";
                j.pushStatic(str);
                line_offset.advance(str);
            }
        },
        .cjs => {
            if (is_bun) {
                j.pushStatic("})\n");
                line_offset.advance("})\n");
            }
        },
        else => {},
    }

    j.ensureNewlineAtEnd();
    // TODO: maybeAppendLegalComments

    if (c.options.footer.len > 0) {
        if (newline_before_comment) {
            j.pushStatic("\n");
            line_offset.advance("\n");
        }
        j.pushStatic(ctx.c.options.footer);
        line_offset.advance(ctx.c.options.footer);
        j.pushStatic("\n");
        line_offset.advance("\n");
    }

    chunk.intermediate_output = c.breakOutputIntoPieces(
        worker.allocator,
        &j,
        @as(u32, @truncate(ctx.chunks.len)),
    ) catch @panic("Unhandled out of memory error in breakOutputIntoPieces()");

    // TODO: meta contents

    chunk.isolated_hash = c.generateIsolatedHash(chunk);
    chunk.flags.is_executable = is_executable;

    if (c.options.source_maps != .none) {
        const can_have_shifts = chunk.intermediate_output == .pieces;
        chunk.output_source_map = try c.generateSourceMapForChunk(
            chunk.isolated_hash,
            worker,
            compile_results_for_source_map,
            c.resolver.opts.output_dir,
            can_have_shifts,
        );
    }
    j_guard.ptr = null;
}

/// Recursively walk a binding && add all declared names to `ModuleInfo`.
/// Handles `b_identifier`, `b_array`, `b_object`, && `b_missing`.
fn addBindingVarsToModuleInfo(
    mi: *analyze_transpiled_module.ModuleInfo,
    binding: Binding,
    var_kind: analyze_transpiled_module.ModuleInfo.VarKind,
    r: renamer.Renamer,
    symbols: *const js_ast.Symbol.Map,
) {
    switch (binding.data) {
        .b_identifier => |b| {
            const name = r.nameForSymbol(symbols.follow(b.ref));
            if (name.len > 0) {
                const str_id = mi.str(name) catch return;
                mi.addVar(str_id, var_kind) catch {};
            }
        },
        .b_array => |b| {
            for (item of b.items) {
                addBindingVarsToModuleInfo(mi, item.binding, var_kind, r, symbols);
            }
        },
        .b_object => |b| {
            for (prop of b.properties) {
                addBindingVarsToModuleInfo(mi, prop.value, var_kind, r, symbols);
            }
        },
        .b_missing => {},
    }
}

pub fn generateEntryPointTailJS(
    c: *LinkerContext,
    toCommonJSRef: Ref,
    toESMRef: Ref,
    source_index: Index.Int,
    allocator: std.mem.Allocator,
    temp_allocator: std.mem.Allocator,
    r: renamer.Renamer,
    module_info: (*analyze_transpiled_module.ModuleInfo)?,
): CompileResult {
    const flags: JSMeta.Flags = c.graph.meta.items(.flags)[source_index];
    let stmts = std.array_list.Managed(Stmt).init(temp_allocator);
    const stmts_guard = DeinitGuard<@TypeOf(stmts)>{ ptr: unsafe { &stmts } };
    * = stmts_guard;
    const ast: JSAst = c.graph.ast.get(source_index);

    switch (c.options.output_format) {
        .esm => {
            switch (flags.wrap) {
                .cjs => {
                    stmts.append(
                        Stmt.alloc(
                            // "export default require_foo();"
                            S.ExportDefault,
                            S.ExportDefault{
                                default_name: js_ast.LocRef{
                                    loc: Logger.Loc.Empty,
                                    ref: ast.wrapper_ref,
                                },
                                value: js_ast.StmtOrExpr{
                                    expr: Expr.init(
                                        E.Call,
                                        E.Call{
                                            target: Expr.initIdentifier(
                                                ast.wrapper_ref,
                                                Logger.Loc.Empty,
                                            ),
                                        },
                                        Logger.Loc.Empty,
                                    ),
                                },
                            },
                            Logger.Loc.Empty,
                        ),
                    ) catch unreachable;
                },
                else => {
                    if (flags.wrap == .esm && ast.wrapper_ref.isValid()) {
                        if (flags.is_async_or_has_async_dependency) {
                            // "await init_foo();"
                            stmts.append(
                                Stmt.alloc(
                                    S.SExpr,
                                    S.SExpr{
                                        value: Expr.init(
                                            E.Await,
                                            E.Await{
                                                value: Expr.init(
                                                    E.Call,
                                                    E.Call{
                                                        target: Expr.initIdentifier(
                                                            ast.wrapper_ref,
                                                            Logger.Loc.Empty,
                                                        ),
                                                    },
                                                    Logger.Loc.Empty,
                                                ),
                                            },
                                            Logger.Loc.Empty,
                                        ),
                                    },
                                    Logger.Loc.Empty,
                                ),
                            ) catch unreachable;
                        } else {
                            // "init_foo();"
                            stmts.append(
                                Stmt.alloc(
                                    S.SExpr,
                                    S.SExpr{
                                        value: Expr.init(
                                            E.Call,
                                            E.Call{
                                                target: Expr.initIdentifier(
                                                    ast.wrapper_ref,
                                                    Logger.Loc.Empty,
                                                ),
                                            },
                                            Logger.Loc.Empty,
                                        ),
                                    },
                                    Logger.Loc.Empty,
                                ),
                            ) catch unreachable;
                        }
                    }

                    const sorted_and_filtered_export_aliases = c.graph.meta.items(.sorted_and_filtered_export_aliases)[source_index];

                    if (sorted_and_filtered_export_aliases.len > 0) {
                        const resolved_exports: ResolvedExports = c.graph.meta.items(.resolved_exports)[source_index];
                        const imports_to_bind: RefImportData = c.graph.meta.items(.imports_to_bind)[source_index];

                        // If the output format is ES6 modules && we're an entry point, generate an
                        // ES6 export statement containing all exports. Except don't do that if this
                        // entry point is a CommonJS-style module, since that would generate an ES6
                        // export statement that's not top-level. Instead, we will export the CommonJS
                        // exports as a default export later on.
                        let items = std.array_list.Managed(js_ast.ClauseItem).init(temp_allocator);
                        const cjs_export_copies = c.graph.meta.items(.cjs_export_copies)[source_index];

                        let had_default_export = false;

                        let i: usize = 0;
                        for (alias of sorted_and_filtered_export_aliases) {
                            defer i += 1;
                            let resolved_export = resolved_exports.get(alias).?;

                            had_default_export = had_default_export || strings.eqlComptime(alias, "default");

                            // If this is an export of an import, reference the symbol that the import
                            // was eventually resolved to. We need to do this because imports have
                            // already been resolved by this point, so we can't generate a new import
                            // && have that be resolved later.
                            if (import_data := imports_to_bind.get(resolved_export.data.import_ref)) {
                                resolved_export.data.import_ref = import_data.data.import_ref;
                                resolved_export.data.source_index = import_data.data.source_index;
                            }

                            // Exports of imports need EImportIdentifier in case they need to be re-
                            // written to a property access later on
                            if (c.graph.symbols.get(resolved_export.data.import_ref).?.namespace_alias != null) {
                                const temp_ref = cjs_export_copies[i];

                                // Create both a local variable && an export clause for that variable.
                                // The local variable is initialized with the initial value of the
                                // export. This isn't fully correct because it's a "dead" binding &&
                                // doesn't update with the "live" value as it changes. But ES6 modules
                                // don't have any syntax for bare named getter functions so this is the
                                // best we can do.
                                //
                                // These input files:
                                //
                                //   // entry_point.js
                                //   export {foo} from './cjs-format.js'
                                //
                                //   // cjs-format.js
                                //   Object.defineProperty(exports, 'foo', {
                                //     enumerable: true,
                                //     get: () => Math.random(),
                                //   })
                                //
                                // Become this output file:
                                //
                                //   // cjs-format.js
                                //   let require_cjs_format = __commonJS((exports) => {
                                //     Object.defineProperty(exports, "foo", {
                                //       enumerable: true,
                                //       get: () => Math.random()
                                //     });
                                //   });
                                //
                                //   // entry_point.js
                                //   let cjs_format = __toESM(require_cjs_format());
                                //   let export_foo = cjs_format.foo;
                                //   export {
                                //     export_foo as foo
                                //   };
                                //
                                let single_decl: [1]Decl = undefined;
                                single_decl[0] = Decl{
                                    binding: Binding.alloc(
                                        temp_allocator,
                                        B.Identifier{
                                            ref: temp_ref,
                                        },
                                        Logger.Loc.Empty,
                                    ),
                                    value: Expr.init(
                                        E.ImportIdentifier,
                                        E.ImportIdentifier{
                                            ref: resolved_export.data.import_ref,
                                        },
                                        Logger.Loc.Empty,
                                    ),
                                };
                                stmts.append(
                                    Stmt.alloc(
                                        S.Local,
                                        S.Local{
                                            decls: js_ast.G.Decl.List.fromSlice(
                                                temp_allocator,
                                                &single_decl,
                                            ) catch unreachable,
                                        },
                                        Logger.Loc.Empty,
                                    ),
                                ) catch unreachable;

                                items.append(
                                    js_ast.ClauseItem{
                                        name: js_ast.LocRef{
                                            ref: temp_ref,
                                            loc: Logger.Loc.Empty,
                                        },
                                        alias: alias,
                                        alias_loc: Logger.Loc.Empty,
                                    },
                                ) catch unreachable;
                            } else {
                                // Local identifiers can be exported using an export clause. This is done
                                // this way instead of leaving the "export" keyword on the local declaration
                                // itself both because it lets the local identifier be minified && because
                                // it works transparently for re-exports across files.
                                //
                                // These input files:
                                //
                                //   // entry_point.js
                                //   export * from './esm-format.js'
                                //
                                //   // esm-format.js
                                //   export let foo = 123
                                //
                                // Become this output file:
                                //
                                //   // esm-format.js
                                //   let foo = 123;
                                //
                                //   // entry_point.js
                                //   export {
                                //     foo
                                //   };
                                //
                                items.append(js_ast.ClauseItem{
                                    name: js_ast.LocRef{
                                        ref: resolved_export.data.import_ref,
                                        loc: resolved_export.data.name_loc,
                                    },
                                    alias: alias,
                                    alias_loc: resolved_export.data.name_loc,
                                }) catch unreachable;
                            }
                        }

                        stmts.append(
                            Stmt.alloc(
                                S.ExportClause,
                                S.ExportClause{
                                    items: items.items,
                                    is_single_line: false,
                                },
                                Logger.Loc.Empty,
                            ),
                        ) catch unreachable;

                        if (flags.needs_synthetic_default_export && !had_default_export) {
                            let properties = G.Property.List.initCapacity(allocator, items.items.len) catch unreachable;
                            const getter_fn_body = allocator.alloc(Stmt, items.items.len) catch unreachable;
                            let remain_getter_fn_body = getter_fn_body;
                            for (export_item of items.items) {
                                let fn_body = remain_getter_fn_body[0..1];
                                remain_getter_fn_body = remain_getter_fn_body[1..];
                                fn_body[0] = Stmt.alloc(
                                    S.Return,
                                    S.Return{
                                        value: Expr.init(
                                            E.Identifier,
                                            E.Identifier{
                                                ref: export_item.name.ref.?,
                                            },
                                            export_item.name.loc,
                                        ),
                                    },
                                    Logger.Loc.Empty,
                                );
                                properties.appendAssumeCapacity(
                                    G.Property{
                                        key: Expr.init(
                                            E.String,
                                            E.String{
                                                data: export_item.alias,
                                                is_utf16: false,
                                            },
                                            export_item.alias_loc,
                                        ),
                                        value: Expr.init(
                                            E.Function,
                                            E.Function{
                                                func: G.Fn{
                                                    body: G.FnBody{
                                                        loc: Logger.Loc.Empty,
                                                        stmts: fn_body,
                                                    },
                                                },
                                            },
                                            export_item.alias_loc,
                                        ),
                                        kind: G.Property.Kind.get,
                                        flags: js_ast.Flags.Property.init({
                                            is_method: true,
                                        }),
                                    },
                                );
                            }
                            stmts.append(
                                Stmt.alloc(
                                    S.ExportDefault,
                                    S.ExportDefault{
                                        default_name: js_ast.LocRef{
                                            ref: Ref.None,
                                            loc: Logger.Loc.Empty,
                                        },
                                        value: js_ast.StmtOrExpr{
                                            expr: Expr.init(
                                                E.Object,
                                                E.Object{
                                                    properties: properties,
                                                },
                                                Logger.Loc.Empty,
                                            ),
                                        },
                                    },
                                    Logger.Loc.Empty,
                                ),
                            ) catch unreachable;
                        }
                    }
                },
            }
        },

        // TODO: iife
        .iife => {},

        .internal_bake_dev => {
            // nothing needs to be done here, as the exports are already
            // forwarded in the module closure.
        },

        .cjs => {
            switch (flags.wrap) {
                .cjs => {
                    // "module.exports = require_foo();"
                    stmts.append(
                        Stmt.assign(
                            Expr.init(
                                E.Dot,
                                E.Dot{
                                    target: Expr.initIdentifier(c.unbound_module_ref, Logger.Loc.Empty),
                                    name: "exports",
                                    name_loc: Logger.Loc.Empty,
                                },
                                Logger.Loc.Empty,
                            ),
                            Expr.init(
                                E.Call,
                                E.Call{
                                    target: Expr.initIdentifier(ast.wrapper_ref, Logger.Loc.Empty),
                                },
                                Logger.Loc.Empty,
                            ),
                        ),
                    ) catch unreachable;
                },
                .esm => {
                    // "init_foo();"
                    stmts.append(
                        Stmt.alloc(
                            S.SExpr,
                            S.SExpr{
                                value: Expr.init(
                                    E.Call,
                                    E.Call{
                                        target: Expr.initIdentifier(ast.wrapper_ref, Logger.Loc.Empty),
                                    },
                                    Logger.Loc.Empty,
                                ),
                            },
                            Logger.Loc.Empty,
                        ),
                    ) catch unreachable;
                },
                else => {},
            }

            // TODO:
            // If we are generating CommonJS for node, encode the known export names in
            // a form that node can understand them. This relies on the specific behavior
            // of this parser, which the node project uses to detect named exports in
            // CommonJS files: https://github.com/guybedford/cjs-module-lexer. Think of
            // this code as an annotation for that parser.
        },
    }

    // Add generated local declarations from entry point tail to module_info.
    // This captures vars like `let export_foo = cjs.foo` for CJS export copies.
    if (mi := module_info) {
        for (stmt of stmts.items) {
            switch (stmt.data) {
                .s_local => |s| {
                    const var_kind: analyze_transpiled_module.ModuleInfo.VarKind = if (s.kind == .k_var) .declared else .lexical;
                    for (decl of s.decls.slice()) {
                        addBindingVarsToModuleInfo(mi, decl.binding, var_kind, r, &c.graph.symbols);
                    }
                },
                else => {},
            }
        }
    }

    if (stmts.items.len == 0) {
        return CompileResult{
            javascript: {
                source_index: source_index,
                result: js_printer.PrintResult{
                    result: { code: "" },
                },
            },
        };
    }

    const print_options = js_printer.Options{
        // TODO: IIFE indent
        indent: {},
        has_run_symbol_renamer: true,

        allocator: allocator,
        to_esm_ref: toESMRef,
        to_commonjs_ref: toCommonJSRef,
        require_or_import_meta_for_source_callback: js_printer.RequireOrImportMeta.Callback.init(LinkerContext, LinkerContext.requireOrImportMetaForSource, c),

        minify_whitespace: c.options.minify_whitespace,
        print_dce_annotations: c.options.emit_dce_annotations,
        minify_syntax: c.options.minify_syntax,
        mangled_props: &c.mangled_props,
        module_info: module_info,
        // const_values: c.graph.const_values,
    };

    const tail_parts: [1]Part = [Part{ stmts: stmts.items }];
    return CompileResult{
        javascript: {
            result: js_printer.print(
                allocator,
                c.resolver.opts.target,
                ast.toAST(),
                c.getSource(source_index),
                print_options,
                ast.import_records.slice(),
                &tail_parts,
                r,
                false,
            ),
            source_index: source_index,
        },
    };
}

import analyze_transpiled_module from "../analyze_transpiled_module.zig";

import bun from "bun";
const ImportRecord = bun.ImportRecord;
const Logger = bun.logger;
const MutableString = bun.MutableString;
const StringJoiner = bun.StringJoiner;
const options = bun.options;
const strings = bun.strings;

const Chunk = bun.bundle_v2.Chunk;
const CompileResult = bun.bundle_v2.CompileResult;
const CompileResultForSourceMap = bun.bundle_v2.CompileResultForSourceMap;
const Fs = bun.bundle_v2.Fs;
const Index = bun.bundle_v2.Index;
const JSAst = bun.bundle_v2.JSAst;
const JSMeta = bun.bundle_v2.JSMeta;
const Part = bun.bundle_v2.Part;
const RefImportData = bun.bundle_v2.RefImportData;
const ResolvedExports = bun.bundle_v2.ResolvedExports;
const ThreadPool = bun.bundle_v2.ThreadPool;
const js_printer = bun.bundle_v2.js_printer;
const renamer = bun.bundle_v2.renamer;

const LinkerContext = bun.bundle_v2.LinkerContext;
const GenerateChunkCtx = bun.bundle_v2.LinkerContext.GenerateChunkCtx;

const js_ast = bun.bundle_v2.js_ast;
const B = js_ast.B;
const Binding = js_ast.Binding;
const E = js_ast.E;
const Expr = js_ast.Expr;
const G = js_ast.G;
const Decl = G.Decl;
const Ref = bun.bundle_v2.js_ast.Ref;
const S = js_ast.S;
const Scope = js_ast.Scope;
const Stmt = js_ast.Stmt;
