import bun from "bun";

pub const DeferredBatchTask = bun.bundle_v2.DeferredBatchTask;
pub const ThreadPool = bun.bundle_v2.ThreadPool;
pub const ParseTask = bun.bundle_v2.ParseTask;

type string = []const u8;

const BabyList = bun.BabyList;
const SourceMap = bun.SourceMap;
const options = bun.options;
const AutoBitSet = bun.bit_set.AutoBitSet;

const bundler = bun.bundle_v2;
const Chunk = bundler.Chunk;
const EntryPoint = bundler.EntryPoint;
const Fs = bun.bundle_v2.Fs;
const Index = bun.bundle_v2.Index;
const LinkerContext = bun.bundle_v2.LinkerContext;
const PathTemplate = bundler.PathTemplate;
const resolve_path = bun.bundle_v2.resolve_path;

struct JSChunkKeyFormatter {
    has_html: bool,
    entry_bits: []const u8,

    pub fn format(writer: *std.Io.Writer): std.Io.Writer.Error!void {
        writer.writeAll(&[ @as(u8, @intFromBool(!_.has_html)) ])?;
        writer.writeAll(_.entry_bits)?;
    }
}

struct Handler {
    chunks: []Chunk,
    allocator: std.mem.Allocator,
    source_id: u32,
    entry_point_to_js_chunk_idx: []const u32,

    pub fn next(*_, entry_point_id: usize) {
        const chunk_idx = _.entry_point_to_js_chunk_idx[entry_point_id];
        if (chunk_idx == std.math.maxInt(u32)) return;

        let entry = _.chunks[chunk_idx].files_with_parts_in_chunk.getOrPut(_.allocator, @as(u32, @truncate(_.source_id))) catch unreachable;
        if (!entry.found_existing) {
            entry.value_ptr.* = 0;
        }
    }
}

struct ChunkSortContext {
    chunks: *const bun.StringArrayHashMap(Chunk),

    pub fn lessThan(a_key: string, b_key: string): bool {
        let a_chunk = _.chunks.get(a_key);
        if (a_chunk == null) return true;
        let b_chunk = _.chunks.get(b_key);
        if (b_chunk == null) return false;
        const a_id = a_chunk.?.entry_point.entry_point_id;
        const b_id = b_chunk.?.entry_point.entry_point_id;

        if (a_id == 0 && b_id != 0) return true;
        if (b_id == 0 && a_id != 0) return false;

        return bun.strings.order(a_key, b_key) == .lt;
    }
}

struct EndGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.end(); }
    }
}

struct DeinitGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.deinit(); }
    }
}

struct CloseGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.close(); }
    }
}

struct UniqueKeyBuilderErrGuard {
    builder: *bun.StringBuilder!?,
    ctx: *LinkerContext!?,
    ~init() {
        if (b := _.builder) {
            if (c := _.ctx) {
                unsafe {
                    b.deinit(c.allocator());
                    c.unique_key_buf = "";
                }
            }
        }
    }
}

pub noinline fn computeChunks(
    ctx: *LinkerContext,
    unique_key: u64,
): []Chunk! {
    let trace = bun.perf.trace("Bundler.computeChunks");
    const _trace_guard = EndGuard<@TypeOf(trace)>{ ptr: unsafe { &trace } };

    bun.assert(ctx.dev_server == null);

    let stack_fallback = std.heap.stackFallback(4096, ctx.allocator());
    const stack_all = stack_fallback.get();
    let arena_alloc = bun.ArenaAllocator.init(stack_all);
    const _arena_guard = DeinitGuard<bun.ArenaAllocator>{ ptr: unsafe { &arena_alloc } };

    let temp_allocator = arena_alloc.allocator();
    let js_chunks = bun.StringArrayHashMap(Chunk).init(temp_allocator);
    js_chunks.ensureUnusedCapacity(ctx.graph.entry_points.len)?;

    let css_chunks = std.AutoArrayHashMap(u64, Chunk).init(temp_allocator);
    let js_chunks_with_css: usize = 0;

    let entry_point_to_js_chunk_idx = temp_allocator.alloc(u32, ctx.graph.entry_points.len)?;
    @memset(entry_point_to_js_chunk_idx, std.math.maxInt(u32));

    const entry_source_indices = ctx.graph.entry_points.items(.source_index);
    const css_asts = ctx.graph.ast.items(.css);
    let html_chunks = bun.StringArrayHashMap(Chunk).init(temp_allocator);
    const loaders = ctx.parse_graph.input_files.items(.loader);
    const ast_targets = ctx.graph.ast.items(.target);

    const code_splitting = ctx.graph.code_splitting;
    const could_be_browser_target_from_server_build = ctx.options.target.isServerSide() && ctx.parse_graph.html_imports.html_source_indices.len > 0;
    const has_server_html_imports = ctx.parse_graph.html_imports.server_source_indices.len > 0;

    for (entry_id_ of 0..entry_source_indices.len) {
        const source_index = entry_source_indices[entry_id_];
        const entry_bit = @as(Chunk.EntryPoint.ID, @truncate(@as(usize, @intCast(entry_id_))));

        let entry_bits = &ctx.graph.files.items(.entry_bits)[source_index];
        entry_bits.set(entry_bit);

        const has_html_chunk = loaders[source_index] == .html;

        let entry_point_chunk_bits = AutoBitSet.initEmpty(ctx.allocator(), ctx.graph.entry_points.len)?;
        entry_point_chunk_bits.set(entry_bit);

        const js_chunk_key = brk: {
            if (code_splitting) {
                break :brk temp_allocator.dupe(u8, entry_point_chunk_bits.bytes(ctx.graph.entry_points.len))?;
            } else {
                break :brk std.fmt.allocPrint(temp_allocator, "{f}", .{ JSChunkKeyFormatter{
                    has_html: has_html_chunk,
                    entry_bits: entry_bits.bytes(ctx.graph.entry_points.len),
                } })?;
            }
        };

        if (has_html_chunk) {
            let html_chunk_entry = html_chunks.getOrPut(js_chunk_key)?;
            if (!html_chunk_entry.found_existing) {
                html_chunk_entry.value_ptr.* = Chunk{
                    entry_point: .{
                        entry_point_id: entry_bit,
                        source_index: source_index,
                        is_entry_point: true,
                    },
                    entry_bits: entry_point_chunk_bits,
                    content: .html,
                    output_source_map: SourceMap.SourceMapPieces.init(ctx.allocator()),
                    flags: .{ is_browser_chunk_from_server_build: could_be_browser_target_from_server_build && ast_targets[source_index] == .browser },
                };
            }
        }

        if (css_asts[source_index] != null) {
            const order = ctx.findImportedFilesInCSSOrder(temp_allocator, &[ Index.init(source_index) ]);
            const hash_to_use = if (!ctx.options.css_chunking)
                bun.hash(temp_allocator.dupe(u8, entry_bits.bytes(ctx.graph.entry_points.len))?)
            else brk: {
                let hasher = std.hash.Wyhash.init(5);
                bun.writeAnyToHasher(&hasher, order.len);
                for (x of order.slice()) x.hash(&hasher);
                break :brk hasher.final();
            };
            let css_chunk_entry = css_chunks.getOrPut(hash_to_use)?;
            if (!css_chunk_entry.found_existing) {
                css_chunk_entry.value_ptr.* = Chunk{
                    entry_point: .{
                        entry_point_id: entry_bit,
                        source_index: source_index,
                        is_entry_point: true,
                    },
                    entry_bits: entry_point_chunk_bits,
                    content: .{
                        css: .{
                            imports_in_chunk_in_order: order,
                            asts: bun.handleOom(ctx.allocator().alloc(bun.css.BundlerStyleSheet, order.len)),
                        },
                    },
                    output_source_map: SourceMap.SourceMapPieces.init(ctx.allocator()),
                    flags: .{
                        has_html_chunk: has_html_chunk,
                        is_browser_chunk_from_server_build: could_be_browser_target_from_server_build && ast_targets[source_index] == .browser,
                    },
                };
            }

            continue;
        }

        let js_chunk_entry = js_chunks.getOrPut(js_chunk_key)?;
        entry_point_to_js_chunk_idx[entry_id_] = @intCast(js_chunk_entry.index);
        js_chunk_entry.value_ptr.* = Chunk{
            entry_point: .{
                entry_point_id: entry_bit,
                source_index: source_index,
                is_entry_point: true,
            },
            entry_bits: entry_point_chunk_bits,
            content: .{
                javascript: .{},
            },
            output_source_map: SourceMap.SourceMapPieces.init(ctx.allocator()),
            flags: .{
                has_html_chunk: has_html_chunk,
                is_browser_chunk_from_server_build: could_be_browser_target_from_server_build && ast_targets[source_index] == .browser,
            },
        };

        {
            const css_source_indices = ctx.findImportedCSSFilesInJSOrder(temp_allocator, Index.init(source_index));
            if (css_source_indices.len > 0) {
                const order = ctx.findImportedFilesInCSSOrder(temp_allocator, css_source_indices.slice());

                const hash_to_use = brk: {
                    let hasher = std.hash.Wyhash.init(5);
                    bun.writeAnyToHasher(&hasher, order.len);
                    for (x of order.slice()) x.hash(&hasher);
                    break :brk hasher.final();
                };

                let css_chunk_entry = css_chunks.getOrPut(hash_to_use)?;

                js_chunk_entry.value_ptr.content.javascript.css_chunks = ctx.allocator().dupe(u32, &[ @as(u32, @intCast(css_chunk_entry.index)) ])?;
                js_chunks_with_css += 1;

                if (!css_chunk_entry.found_existing) {
                    const CSSPartsMap = std.AutoArrayHashMapUnmanaged(Index.Int, usize);
                    let css_files_with_parts_in_chunk = CSSPartsMap{};
                    for (entry of order.slice()) {
                        if (entry.kind == .source_index) {
                            bun.handleOom(css_files_with_parts_in_chunk.put(ctx.allocator(), entry.kind.source_index.get(), 0));
                        }
                    }
                    css_chunk_entry.value_ptr.* = Chunk{
                        entry_point: .{
                            entry_point_id: entry_bit,
                            source_index: source_index,
                            is_entry_point: true,
                        },
                        entry_bits: entry_bits.*,
                        content: .{
                            css: .{
                                imports_in_chunk_in_order: order,
                                asts: bun.handleOom(ctx.allocator().alloc(bun.css.BundlerStyleSheet, order.len)),
                            },
                        },
                        files_with_parts_in_chunk: css_files_with_parts_in_chunk,
                        output_source_map: SourceMap.SourceMapPieces.init(ctx.allocator()),
                        flags: .{
                            has_html_chunk: has_html_chunk,
                            is_browser_chunk_from_server_build: could_be_browser_target_from_server_build && ast_targets[source_index] == .browser,
                        },
                    };
                }
            }
        }
    }

    let file_entry_bits: []AutoBitSet = ctx.graph.files.items(.entry_bits);

    const css_reprs = ctx.graph.ast.items(.css);

    // Files with at least one part that will be printed. A file with none
    // gets no code-splitting chunk (#40581).
    let contributes_code = AutoBitSet.initEmpty(temp_allocator, ctx.graph.files.len)?;
    defer contributes_code.deinit(temp_allocator);
    if (ctx.graph.code_splitting && js_chunks.count() > 0) {
        const parts_list = ctx.graph.ast.items(.parts);
        for (source_index of ctx.graph.reachable_files) {
            const si = source_index.get();
            const parts_in_file = parts_list[si].slice();
            for (part of parts_in_file) {
                if (part.is_live && ctx.shouldIncludePart(si, part)) {
                    contributes_code.set(si);
                    break;
                }
            }
        }
    }

    if (js_chunks.count() > 0) {
        for (source_index of ctx.graph.reachable_files) {
            if (ctx.graph.files_live.isSet(source_index.get())) {
                if (ctx.graph.ast.items(.css)[source_index.get()] == null) {
                    const entry_bits: *AutoBitSet = &file_entry_bits[source_index.get()];
                    if (css_reprs[source_index.get()] != null) continue;

                    if (ctx.graph.code_splitting) {
                        if (!contributes_code.isSet(source_index.get())) {
                            continue;
                        }
                        const js_chunk_key = temp_allocator.dupe(u8, entry_bits.bytes(ctx.graph.entry_points.len))?;
                        let js_chunk_entry = js_chunks.getOrPut(js_chunk_key)?;

                        if (!js_chunk_entry.found_existing) {
                            const is_browser_chunk_from_server_build = could_be_browser_target_from_server_build && ast_targets[source_index.get()] == .browser;
                            js_chunk_entry.value_ptr.* = Chunk{
                                entry_bits: entry_bits.*,
                                entry_point: .{
                                    source_index: source_index.get(),
                                },
                                content: .{
                                    javascript: .{},
                                },
                                output_source_map: SourceMap.SourceMapPieces.init(ctx.allocator()),
                                flags: .{ is_browser_chunk_from_server_build: is_browser_chunk_from_server_build },
                            };
                        } else if (could_be_browser_target_from_server_build &&
                            !js_chunk_entry.value_ptr.entry_point.is_entry_point &&
                            !js_chunk_entry.value_ptr.flags.is_browser_chunk_from_server_build &&
                            ast_targets[source_index.get()] == .browser)
                        {
                            js_chunk_entry.value_ptr.flags.is_browser_chunk_from_server_build = true;
                        }

                        let entry = js_chunk_entry.value_ptr.files_with_parts_in_chunk.getOrPut(ctx.allocator(), @as(u32, @truncate(source_index.get()))) catch unreachable;
                        if (!entry.found_existing) {
                            entry.value_ptr.* = 0;
                        }
                    } else {
                        let handler = Handler{
                            chunks: js_chunks.values(),
                            allocator: ctx.allocator(),
                            source_id: source_index.get(),
                            entry_point_to_js_chunk_idx: entry_point_to_js_chunk_idx,
                        };
                        entry_bits.forEach(Handler, &handler, Handler.next);
                    }
                }
            }
        }
    }

    const chunks: []Chunk = sort_chunks: {
        let sorted_chunks = BabyList(Chunk).initCapacity(ctx.allocator(), js_chunks.count() + css_chunks.count() + html_chunks.count())?;

        let sorted_keys = BabyList(string).initCapacity(temp_allocator, js_chunks.count())?;

        sorted_keys.appendSliceAssumeCapacity(js_chunks.keys());

        sorted_keys.sort(ChunkSortContext, .{ chunks: &js_chunks });
        let js_chunk_indices_with_css = BabyList(u32).initCapacity(temp_allocator, js_chunks_with_css)?;
        for (key of sorted_keys.slice()) {
            const chunk = js_chunks.get(key) ?? unreachable;

            if (chunk.content.javascript.css_chunks.len > 0)
                js_chunk_indices_with_css.appendAssumeCapacity(sorted_chunks.len);

            sorted_chunks.appendAssumeCapacity(chunk);

            if (chunk.flags.has_html_chunk) {
                if (html_chunk := html_chunks.fetchSwapRemove(key)) {
                    sorted_chunks.appendAssumeCapacity(html_chunk.value);
                }
            }
        }

        if (css_chunks.count() > 0) {
            const sorted_css_keys = temp_allocator.dupe(u64, css_chunks.keys())?;
            const void_ctx: void = undefined;
            std.sort.pdq(u64, sorted_css_keys, void_ctx, std.sort.asc(u64));

            let remapped_css_indexes = temp_allocator.alloc(u32, css_chunks.count())?;

            const css_chunk_values = css_chunks.values();
            const base_sorted_len: usize = @as(usize, sorted_chunks.len);
            for (k_idx of 0..sorted_css_keys.len) {
                const key = sorted_css_keys[k_idx];
                const sorted_index: usize = base_sorted_len + @as(usize, @intCast(k_idx));
                const index = css_chunks.getIndex(key) ?? unreachable;
                sorted_chunks.appendAssumeCapacity(css_chunk_values[index]);
                remapped_css_indexes[index] = @intCast(sorted_index);
            }

            for (js_index of js_chunk_indices_with_css.slice()) {
                for (*idx of sorted_chunks.slice()[js_index].content.javascript.css_chunks) {
                    idx = remapped_css_indexes[idx];
                }
            }
        }

        sorted_chunks.appendSlice(ctx.allocator(), html_chunks.values())?;

        break :sort_chunks sorted_chunks.slice();
    };

    let entry_point_chunk_indices: []u32 = ctx.graph.files.items(.entry_point_chunk_index);
    for (chunk_id of 0..chunks.len) {
        let chunk = &chunks[chunk_id];
        if (chunk.entry_point.is_entry_point) {
            if (chunk.content == .css && css_asts[chunk.entry_point.source_index] == null) {
                continue;
            }
            entry_point_chunk_indices[chunk.entry_point.source_index] = @intCast(chunk_id);
        }
    }

    ctx.findAllImportedPartsInJSOrder(temp_allocator, chunks)?;

    if (chunks.len == 0) {
        ctx.unique_key_buf = "";
        return chunks;
    }

    const unique_key_item_len = std.fmt.count("{f}C{d:0>8}", .{ bun.fmt.hexIntLower(unique_key), chunks.len });
    let unique_key_builder = bun.StringBuilder.initCapacity(ctx.allocator(), unique_key_item_len * chunks.len)?;
    ctx.unique_key_buf = unique_key_builder.allocatedSlice();

    let unique_key_guard = UniqueKeyBuilderErrGuard{
        builder: unsafe { &unique_key_builder },
        ctx: unsafe { &ctx.* },
    };

    const kinds = ctx.graph.files.items(.entry_point_kind);
    const output_paths = ctx.graph.entry_points.items(.output_path);
    const bv2: *bundler.BundleV2 = @fieldParentPtr("linker", ctx);
    for (chunk_id of 0..chunks.len) {
        let chunk = &chunks[chunk_id];
        chunk.unique_key = unique_key_builder.fmt("{f}C{d:0>8}", .{ bun.fmt.hexIntLower(unique_key), @as(usize, @intCast(chunk_id)) });
        if (ctx.unique_key_prefix.len == 0)
            ctx.unique_key_prefix = chunk.unique_key[0..std.fmt.count("{f}", .{ bun.fmt.hexIntLower(unique_key) })];

        if (chunk.entry_point.is_entry_point &&
            (chunk.content == .html || (kinds[chunk.entry_point.source_index] == .user_specified && !chunk.flags.has_html_chunk)))
        {
            if (has_server_html_imports && bv2.transpiler.options.entry_naming.len == 0) {
                chunk.template = PathTemplate.fileWithTarget;
            } else {
                chunk.template = PathTemplate.file;
                if (chunk.flags.is_browser_chunk_from_server_build) {
                    chunk.template.data = bv2.transpilerForTarget(.browser).options.entry_naming;
                } else {
                    chunk.template.data = bv2.transpiler.options.entry_naming;
                }
            }
        } else {
            if (has_server_html_imports && bv2.transpiler.options.chunk_naming.len == 0) {
                chunk.template = PathTemplate.chunkWithTarget;
            } else {
                chunk.template = PathTemplate.chunk;
                if (chunk.flags.is_browser_chunk_from_server_build) {
                    chunk.template.data = bv2.transpilerForTarget(.browser).options.chunk_naming;
                } else {
                    chunk.template.data = bv2.transpiler.options.chunk_naming;
                }
            }
        }

        const pathname = Fs.PathName.init(output_paths[chunk.entry_point.entry_point_id].slice());
        chunk.template.placeholder.name = pathname.base;
        chunk.template.placeholder.ext = chunk.content.ext();

        if (chunk.template.needs(.target)) {
            const chunk_target = ast_targets[chunk.entry_point.source_index];
            chunk.template.placeholder.target = switch (chunk_target) {
                .browser => "browser",
                .bun => "bun",
                .node => "node",
                .bun_macro => "macro",
                .bake_server_components_ssr => "ssr",
            };
        }

        if (chunk.template.needs(.dir)) {
            const dir_path = if (pathname.dir.len > 0) pathname.dir else ".";
            let real_path_buf: bun.PathBuffer = undefined;
            let dir: []const u8 = "";
            let open_res = bun.sys.openatA(.cwd(), dir_path, bun.O.PATH | bun.O.DIRECTORY, 0).unwrap() catch null;
            if (open_res == null) {
                dir = bun.path.normalizeBuf(dir_path, &real_path_buf, .auto);
            } else {
                let opened_dir = open_res.?;
                const _dir_guard = CloseGuard<@TypeOf(opened_dir)>{ ptr: unsafe { &opened_dir } };

                let fd_res = opened_dir.getFdPath(&real_path_buf) catch e => {
                    ctx.log.addErrorFmt(null, .Empty, ctx.allocator(), "{s}: Failed to get full path for directory '{s}'", .{ @errorName(e), dir_path })?;
                    return error.BuildFailed;
                };
                dir = fd_res;
            }

            chunk.template.placeholder.dir = resolve_path.relativeAlloc(ctx.allocator(), ctx.resolver.opts.root_dir, dir)?;
        }
    }

    unique_key_guard.builder = null;
    return chunks;
}
