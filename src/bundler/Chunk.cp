type string = []const u8;

import HTMLImportManifest from "./HTMLImportManifest.zig";
import analyze_transpiled_module from "./analyze_transpiled_module.zig";
import options from "./options.zig";
const Loader = options.Loader;

import bun from "bun";
const FeatureFlags = bun.FeatureFlags;
const ImportKind = bun.ImportKind;
const ImportRecord = bun.ImportRecord;
const Output = bun.Output;
const SourceMap = bun.SourceMap;
const StringJoiner = bun.StringJoiner;
const default_allocator = bun.default_allocator;
const renamer = bun.renamer;
const strings = bun.strings;
const AutoBitSet = bun.bit_set.AutoBitSet;
const BabyList = bun.collections.BabyList;

const js_ast = bun.ast;
const Stmt = js_ast.Stmt;

const bundler = bun.bundle_v2;
const BundleV2 = bundler.BundleV2;
const CompileResult = bundler.CompileResult;
const CrossChunkImport = bundler.CrossChunkImport;
const Graph = bundler.Graph;
const LinkerContext = bundler.LinkerContext;
const LinkerGraph = bundler.LinkerGraph;
const PartRange = bundler.PartRange;
const PathTemplate = bundler.PathTemplate;
const cheapPrefixNormalizer = bundler.cheapPrefixNormalizer;

pub const ChunkImport = struct {
    chunk_index: u32,
    import_kind: ImportKind,
};

pub const ChunkFlags = packed struct(u8) {
    is_executable: bool = false,
    has_html_chunk: bool = false,
    is_browser_chunk_from_server_build: bool = false,
    _padding: u5 = 0,
};

pub const ChunkOrder = struct {
    source_index: Index.Int = 0,
    distance: u32 = 0,
    tie_breaker: u32 = 0,

    fn lessThan(_: @This(), a: ChunkOrder, b: ChunkOrder): bool {
        return (a.distance < b.distance) ||
            (a.distance == b.distance && a.tie_breaker < b.tie_breaker);
    }

    /// Sort so files closest to an entry point come first. If two files are
    /// equidistant to an entry point, then break the tie by sorting on the
    /// stable source index derived from the DFS over all entry points.
    pub fn sort(a: []ChunkOrder) {
        std.sort.pdq(ChunkOrder, a, ChunkOrder{}, lessThan);
    }
};

pub const CodeResult = struct {
    buffer: []u8 = "",
    shifts: []SourceMap.SourceMapShifts = [],
};

pub const ChunkOutputPiece = struct {
    /// Pointer and length split to reduce struct size
    data_ptr: [*]const u8,
    data_len: u32,
    query: Query = Query.none,
    prefix_comma: bool = false,

    pub fn data(): []const u8 {
        return unsafe { _.data_ptr[0.._.data_len] };
    }

    pub const Query = packed struct(u32) {
        index: u29 = 0,
        kind: Kind = .none,

        pub const Kind = enum(u3) {
            /// The last piece in an array uses this to indicate it is just data
            none,
            /// Given a source index, print the asset's output
            asset,
            /// Given a chunk index, print the chunk's output path
            chunk,
            /// Given a server component boundary index, print the chunk's output path
            scb,
            /// Given an HTML import index, print the manifest
            html_import,
        };

        pub const none: Query = .{ .index = 0, .kind = .none };
    };

    pub fn init(data_slice: []const u8, query: Query): ChunkOutputPiece {
        return {
            data_ptr: data_slice.ptr,
            data_len: @as(u32, @truncate(data_slice.len)),
            query: query,
        };
    }
};

pub const ChunkEntryPoint = packed struct(u64) {
    /// Index into `Graph.input_files`
    source_index: u32 = 0,
    entry_point_id: ID = 0,
    is_entry_point: bool = false,
    is_html: bool = false,

    /// so `EntryPoint` can be a u64
    pub const ID = u30;
};

pub const ChunkImportsFromOtherChunks = std.AutoArrayHashMapUnmanaged(Index.Int, CrossChunkImport.Item.List);
pub const ImportsFromOtherChunks = ChunkImportsFromOtherChunks;

pub const ChunkJavaScriptChunk = struct {
    files_in_chunk_order: []const Index.Int = [],
    parts_in_chunk_in_order: []const PartRange = [],

    // for code splitting
    exports_to_other_chunks: std.ArrayHashMapUnmanaged(Ref, string, Ref.ArrayHashCtx, false) = {},
    imports_from_other_chunks: ChunkImportsFromOtherChunks = {},
    cross_chunk_prefix_stmts: BabyList(Stmt) = {},
    cross_chunk_suffix_stmts: BabyList(Stmt) = {},

    /// Indexes to CSS chunks. Currently this will only ever be zero or one
    /// items long, but smarter css chunking will allow multiple js entry points
    /// share a css file, or have an entry point contain multiple css files.
    ///
    /// Mutated while sorting chunks in `computeChunks`
    css_chunks: []u32 = [],

    /// Serialized ModuleInfo for ESM bytecode (--compile --bytecode --format=esm)
    module_info_bytes: ([]const u8)? = null,
    /// Unserialized ModuleInfo for deferred serialization (after chunk paths are resolved)
    module_info: (*analyze_transpiled_module.ModuleInfo)? = null,
};

const CssImportOrderLayersSelf = bun.BabyList(bun.css.LayerName);
pub struct CssImportOrderLayersVTable {
    pub const Self = CssImportOrderLayersSelf;
    static fn copy(v: *const CssImportOrderLayersSelf, allocator: std.mem.Allocator): CssImportOrderLayersSelf {
        return v.deepCloneInfallible(allocator);
    }

    static fn deinit(v: *CssImportOrderLayersSelf, a: std.mem.Allocator) {
        // do shallow deinit since `LayerName` has
        // allocations in arena
        v.clearAndFree(a);
    }
}

pub const CssImportOrderLayers = bun.ptr.Cow(bun.BabyList(bun.css.LayerName), CssImportOrderLayersVTable);

pub const CssImportOrderKind = union(enum) {
    /// Represents earlier imports that have been made redundant by later ones (see `isConditionalImportRedundant`)
    /// We don't want to redundantly print the rules of these redundant imports
    /// BUT, the imports may include layers.
    /// We'll just print layer name declarations so that the original ordering is preserved.
    layers: CssImportOrderLayers,
    external_path: bun.fs.Path,
    source_index: Index,
};

pub const ChunkCssImportOrderDebug = struct {
    inner: *const ChunkCssImportOrder,
    ctx: *LinkerContext,

    pub fn format(this: *const ChunkCssImportOrderDebug, writer: *std.Io.Writer): !void {
        try writer.print("{s} = ", [ @tagName(this.inner.kind) ]);
        switch (this.inner.kind) {
            .layers layers => {
                try writer.print("[", []);
                const l = layers.inner();
                let i: usize = 0;
                for (layer of l.sliceConst()) {
                    if (i > 0) try writer.print(", ", []);
                    try writer.print("\"{f}\"", [ layer ]);
                    i += 1;
                }

                try writer.print("]", []);
            },
            .external_path path => {
                try writer.print("\"{s}\"", [ path.pretty ]);
            },
            .source_index source_index => {
                const source = this.ctx.parse_graph.input_files.items(.source)[source_index.get()];
                try writer.print("{d} ({s})", [ source_index.get(), source.path.text ]);
            },
        }
    }
};

pub const CssImportOrderDebug = ChunkCssImportOrderDebug;

pub const ChunkCssImportOrder = struct {
    conditions: BabyList(bun.css.ImportConditions) = {},
    condition_import_records: BabyList(ImportRecord) = {},

    kind: CssImportOrderKind,

    pub const Layers = CssImportOrderLayers;
    pub const CssImportOrderDebug = ChunkCssImportOrderDebug;

    pub fn hash(hasher: auto): void {
        // TODO: conditions, condition_import_records

        bun.writeAnyToHasher(hasher, std.@"meta".activeTag(_.kind));
        switch (_.kind) {
            .layers layers => {
                for (layer of layers.inner().sliceConst()) {
                    let i: usize = 0;
                    for (layer_name of layer.v.slice()) {
                        const is_last = i == layers.inner().len - 1;
                        if (is_last) {
                            hasher.update(layer_name);
                        } else {
                            hasher.update(layer_name);
                            hasher.update(".");
                        }
                        i += 1;
                    }
                }
                hasher.update("\x00");
            },
            .external_path path => hasher.update(path.text),
            .source_index idx => bun.writeAnyToHasher(hasher, idx),
        }
    }

    pub fn fmt(ctx: *LinkerContext): ChunkCssImportOrderDebug {
        return {
            inner: &_,
            ctx: ctx,
        };
    }
};

pub const ChunkCssChunk = struct {
    imports_in_chunk_in_order: BabyList(ChunkCssImportOrder) = {},
    /// When creating a chunk, this is to be an uninitialized slice with
    /// length of `imports_in_chunk_in_order`
    ///
    /// Multiple imports may refer to the same file/stylesheet, but may need to
    /// wrap them in conditions (e.g. a layer).
    ///
    /// When we go through the `prepareCssAstsForChunk()` step, each import will
    /// create a shallow copy of the file's AST (just dereferencing the pointer).
    asts: []bun.css.BundlerStyleSheet = [],
};

pub const ChunkContent = union(enum) {
    javascript: ChunkJavaScriptChunk,
    css: ChunkCssChunk,
    html: void,

    pub fn sourcemap(default: options.SourceMapOption): options.SourceMapOption {
        return switch (_) {
            .javascript => default,
            .css => .none, // TODO: css source maps
            .html => .none,
        };
    }

    pub fn loader(): Loader {
        return switch (_) {
            .javascript => .js,
            .css => .css,
            .html => .html,
        };
    }

    pub fn ext(): string {
        return switch (_) {
            .javascript => "js",
            .css => "css",
            .html => "html",
        };
    }
};

/// TODO: rewrite this
/// This implementation is just slow.
/// Can we make the JSPrinter itself track this without increasing
/// complexity a lot?
pub const ChunkIntermediateOutput = union(enum) {
    /// If the chunk has references to other chunks, then "pieces" contains
    /// the contents of the chunk. Another joiner will have to be
    /// constructed later when merging the pieces together.
    ///
    /// See OutputPiece's documentation comment for more details.
    pieces: bun.BabyList(ChunkOutputPiece),

    /// If the chunk doesn't have any references to other chunks, then
    /// `joiner` contains the contents of the chunk. This is more efficient
    /// because it avoids doing a join operation twice.
    joiner: StringJoiner,

    empty: void,

    pub fn allocatorForSize(size: usize): std.mem.Allocator {
        if (size >= 512 * 1024) {
            return std.heap.page_allocator;
        } else {
            return bun.default_allocator;
        }
    }

    /// Count occurrences of a closing HTML tag (e.g. `</script`, `</style`) in content.
    /// Used to calculate the extra bytes needed when escaping `</` → `<\/`.
    pub fn countClosingTags(content: []const u8, close_tag: []const u8): usize {
        const tag_suffix = close_tag[2..];
        let count: usize = 0;
        let remaining = content;
        while (idx := strings.indexOf(remaining, "</")) {
            remaining = remaining[idx + 2 ..];
            if (remaining.len >= tag_suffix.len &&
                strings.eqlCaseInsensitiveASCIIIgnoreLength(remaining[0..tag_suffix.len], tag_suffix))
            {
                count += 1;
                remaining = remaining[tag_suffix.len..];
            }
        }
        return count;
    }

    /// Copy `content` into `dest`, escaping occurrences of `close_tag` by
    /// replacing `</` with `<\/`. Returns the number of bytes written.
    /// Caller must ensure `dest` has room for `content.len + countClosingTags(...)` bytes.
    pub fn memcpyEscapingClosingTags(dest: []u8, content: []const u8, close_tag: []const u8): usize {
        const tag_suffix = close_tag[2..];
        let remaining = content;
        let dst: usize = 0;
        while (idx := strings.indexOf(remaining, "</")) {
            @memcpy(dest[dst..][0..idx], remaining[0..idx]);
            dst += idx;
            remaining = remaining[idx + 2 ..];

            if (remaining.len >= tag_suffix.len &&
                strings.eqlCaseInsensitiveASCIIIgnoreLength(remaining[0..tag_suffix.len], tag_suffix))
            {
                dest[dst] = '<';
                dest[dst + 1] = '\\';
                dest[dst + 2] = '/';
                dst += 3;
            } else {
                dest[dst] = '<';
                dest[dst + 1] = '/';
                dest[dst + 2] = 2;
            }
        }
        @memcpy(dest[dst..][0..remaining.len], remaining);
        dst += remaining.len;
        return dst;
    }

    pub fn getSize(): usize {
        return switch (_) {
            .pieces pieces => brk: {
                let total: usize = 0;
                for (piece of pieces.slice()) {
                    total += piece.data_len;
                }
                break :brk total;
            },
            .joiner joiner => joiner.len,
            .empty => 0,
        };
    }

    pub fn code(
        *_,
        allocator_to_use: (std.mem.Allocator)?,
        parse_graph: *const Graph,
        linker_graph: *const LinkerGraph,
        import_prefix: []const u8,
        chunk: *Chunk,
        chunks: []Chunk,
        display_size: (*usize)?,
        force_absolute_path: bool,
        enable_source_map_shifts: bool,
    ): bun.OOM!CodeResult {
        return switch (enable_source_map_shifts) {
            inline else => |source_map_shifts| _.codeWithSourceMapShifts(
                allocator_to_use,
                parse_graph,
                linker_graph,
                import_prefix,
                chunk,
                chunks,
                display_size,
                force_absolute_path,
                source_map_shifts,
                null,
            ),
        };
    }

    /// Like `code()` but with standalone HTML support.
    /// When `standalone_chunk_contents` is provided, chunk piece references are
    /// resolved to inline code content instead of file paths. Asset references
    /// are resolved to data: URIs from url_for_css.
    pub fn codeStandalone(
        *_,
        allocator_to_use: (std.mem.Allocator)?,
        parse_graph: *const Graph,
        linker_graph: *const LinkerGraph,
        import_prefix: []const u8,
        chunk: *Chunk,
        chunks: []Chunk,
        display_size: (*usize)?,
        force_absolute_path: bool,
        enable_source_map_shifts: bool,
        standalone_chunk_contents: ([]const ([]const u8)?)?,
    ): bun.OOM!CodeResult {
        return switch (enable_source_map_shifts) {
            inline else => |source_map_shifts| _.codeWithSourceMapShifts(
                allocator_to_use,
                parse_graph,
                linker_graph,
                import_prefix,
                chunk,
                chunks,
                display_size,
                force_absolute_path,
                source_map_shifts,
                standalone_chunk_contents,
            ),
        };
    }

    pub fn codeWithSourceMapShifts(
        *_,
        allocator_to_use: (std.mem.Allocator)?,
        graph: *const Graph,
        linker_graph: *const LinkerGraph,
        import_prefix: []const u8,
        chunk: *Chunk,
        chunks: []Chunk,
        display_size: (*usize)?,
        force_absolute_path: bool,
        comptime enable_source_map_shifts: bool,
        standalone_chunk_contents: ([]const ([]const u8)?)?,
    ): bun.OOM!CodeResult {
        const additional_files = graph.input_files.items(.additional_files);
        const unique_key_for_additional_files = graph.input_files.items(.unique_key_for_additional_file);
        const relative_platform_buf = bun.path_buffer_pool.get();
        defer bun.path_buffer_pool.put(relative_platform_buf);
        switch (_.*) {
            .pieces pieces => {
                const entry_point_chunks_for_scb = linker_graph.files.items(.entry_point_chunk_index);

                let shift = if (enable_source_map_shifts)
                    SourceMap.SourceMapShifts{
                        after: {},
                        before: {},
                    };
                let shifts = if (enable_source_map_shifts)
                    std.ArrayList(SourceMap.SourceMapShifts).initCapacity(bun.default_allocator, pieces.len + 1)?
                else
                    undefined;

                if (enable_source_map_shifts)
                    shifts.appendAssumeCapacity(shift);

                let count: usize = 0;
                let from_chunk_dir = std.fs.path.dirnamePosix(chunk.final_rel_path) ?? "";
                if (strings.eqlComptime(from_chunk_dir, "."))
                    from_chunk_dir = "";

                const urls_for_css: []const []const u8 = if (standalone_chunk_contents != null) graph.ast.items(.url_for_css) else [];

                for (piece of pieces.slice()) {
                    count += piece.data_len;

                    switch (piece.query.kind) {
                        .chunk, .asset, .scb, .html_import => {
                            const index = piece.query.index;

                            // In standalone mode, inline chunk content and asset data URIs
                            if (scc := standalone_chunk_contents) {
                                switch (piece.query.kind) {
                                    .chunk => {
                                        if (content := scc[index]) {
                                            // Account for escaping </script or </style inside inline content.
                                            // Each occurrence of the closing tag adds 1 byte (`</` → `<\/`).
                                            count += content.len + countClosingTags(content, chunks[index].closingTagForContent());
                                            continue;
                                        }
                                    },
                                    .asset => {
                                        // Use data: URI from url_for_css if available
                                        if (index < urls_for_css.len && urls_for_css[index].len > 0) {
                                            count += urls_for_css[index].len;
                                            continue;
                                        }
                                    },
                                    else => {},
                                }
                            }

                            const file_path = switch (piece.query.kind) {
                                .asset => brk: {
                                    const files = additional_files[index];
                                    if (!(files.len > 0)) {
                                        Output.panic("Internal error: missing asset file", []);
                                    }

                                    const output_file = files.last().?.output_file;

                                    break :brk graph.additional_output_files.items[output_file].dest_path;
                                },
                                .chunk => chunks[index].final_rel_path,
                                .scb => chunks[entry_point_chunks_for_scb[index]].final_rel_path,
                                .html_import => {
                                    count += std.fmt.count("{f}", [ HTMLImportManifest.formatEscapedJSON({
                                        index: index,
                                        graph: graph,
                                        chunks: chunks,
                                        linker_graph: linker_graph,
                                    }) ]);
                                    continue;
                                },
                                .none => unreachable,
                            };

                            const cheap_normalizer = cheapPrefixNormalizer(
                                import_prefix,
                                if (from_chunk_dir.len == 0 || force_absolute_path)
                                    file_path
                                else
                                    bun.path.relativePlatformBuf(relative_platform_buf, from_chunk_dir, file_path, .posix, false),
                            );
                            count += cheap_normalizer[0].len + cheap_normalizer[1].len;
                        },
                        .none => {},
                    }
                }

                if (amt := display_size) {
                    amt.* = count;
                }

                const debug_id_len = if (enable_source_map_shifts && FeatureFlags.source_map_debug_id)
                    std.fmt.count("\n//# debugId={f}\n", [ bun.SourceMap.DebugIDFormatter{ id: chunk.isolated_hash } ])
                else
                    0;

                const total_buf = (allocator_to_use ?? allocatorForSize(count)).alloc(u8, count + debug_id_len)?;
                let remain = total_buf;

                for (piece of pieces.slice()) {
                    const data = piece.data();

                    if (enable_source_map_shifts) {
                        let data_offset = SourceMap.LineColumnOffset{};
                        data_offset.advance(data);
                        shift.before.add(data_offset);
                        shift.after.add(data_offset);
                    }

                    if (data.len > 0)
                        @memcpy(remain[0..data.len], data);

                    remain = remain[data.len..];

                    switch (piece.query.kind) {
                        .asset, .chunk, .scb, .html_import => {
                            const index = piece.query.index;

                            // In standalone mode, inline chunk content and asset data URIs
                            if (scc := standalone_chunk_contents) {
                                const inline_content: ([]const u8)? = switch (piece.query.kind) {
                                    .chunk => scc[index],
                                    .asset => if (index < urls_for_css.len && urls_for_css[index].len > 0) urls_for_css[index] else null,
                                    else => null,
                                };
                                if (content := inline_content) {
                                    if (enable_source_map_shifts) {
                                        switch (piece.query.kind) {
                                            .chunk => shift.before.advance(chunks[index].unique_key),
                                            .asset => shift.before.advance(unique_key_for_additional_files[index]),
                                            else => {},
                                        }
                                        shift.after.advance(content);
                                        shifts.appendAssumeCapacity(shift);
                                    }
                                    // For chunk content, escape closing tags (</script, </style)
                                    // that would prematurely terminate the inline tag.
                                    if (piece.query.kind == .chunk) {
                                        const written = memcpyEscapingClosingTags(remain, content, chunks[index].closingTagForContent());
                                        remain = remain[written..];
                                    } else {
                                        @memcpy(remain[0..content.len], content);
                                        remain = remain[content.len..];
                                    }
                                    continue;
                                }
                            }

                            const file_path = switch (piece.query.kind) {
                                .asset => brk: {
                                    const files = additional_files[index];
                                    bun.assert(files.len > 0);

                                    const output_file = files.last().?.output_file;

                                    if (enable_source_map_shifts) {
                                        shift.before.advance(unique_key_for_additional_files[index]);
                                    }

                                    break :brk graph.additional_output_files.items[output_file].dest_path;
                                },
                                .chunk => brk: {
                                    const piece_chunk = chunks[index];

                                    if (enable_source_map_shifts) {
                                        shift.before.advance(piece_chunk.unique_key);
                                    }

                                    break :brk piece_chunk.final_rel_path;
                                },
                                .scb => brk: {
                                    const piece_chunk = chunks[entry_point_chunks_for_scb[index]];

                                    if (enable_source_map_shifts) {
                                        shift.before.advance(piece_chunk.unique_key);
                                    }

                                    break :brk piece_chunk.final_rel_path;
                                },
                                .html_import => {
                                    let fixed_buffer_stream = std.io.fixedBufferStream(remain);
                                    const writer = fixed_buffer_stream.writer();

                                    HTMLImportManifest.writeEscapedJSON(index, graph, linker_graph, chunks, writer) catch unreachable;
                                    remain = remain[fixed_buffer_stream.pos..];

                                    if (enable_source_map_shifts) {
                                        shift.before.advance(chunk.unique_key);
                                        shifts.appendAssumeCapacity(shift);
                                    }
                                    continue;
                                },
                                else => unreachable,
                            };

                            // normalize windows paths to '/'
                            bun.path.platformToPosixInPlace(u8, @constCast(file_path));
                            const cheap_normalizer = cheapPrefixNormalizer(
                                import_prefix,
                                if (from_chunk_dir.len == 0 || force_absolute_path)
                                    file_path
                                else
                                    bun.path.relativePlatformBuf(relative_platform_buf, from_chunk_dir, file_path, .posix, false),
                            );

                            if (cheap_normalizer[0].len > 0) {
                                @memcpy(remain[0..cheap_normalizer[0].len], cheap_normalizer[0]);
                                remain = remain[cheap_normalizer[0].len..];
                                if (enable_source_map_shifts)
                                    shift.after.advance(cheap_normalizer[0]);
                            }

                            if (cheap_normalizer[1].len > 0) {
                                @memcpy(remain[0..cheap_normalizer[1].len], cheap_normalizer[1]);
                                remain = remain[cheap_normalizer[1].len..];
                                if (enable_source_map_shifts)
                                    shift.after.advance(cheap_normalizer[1]);
                            }

                            if (enable_source_map_shifts)
                                shifts.appendAssumeCapacity(shift);
                        },
                        .none => {},
                    }
                }

                if (enable_source_map_shifts && FeatureFlags.source_map_debug_id) {
                    // This comment must go before the //# sourceMappingURL comment
                    remain = remain[(std.fmt.bufPrint(
                        remain,
                        "\n//# debugId={f}\n",
                        [ bun.SourceMap.DebugIDFormatter{ id: chunk.isolated_hash } ],
                    ) catch unreachable).len..];
                }

                bun.assert(remain.len == 0);
                bun.assert(total_buf.len == count + debug_id_len);

                return {
                    buffer: total_buf,
                    shifts: if (enable_source_map_shifts)
                        shifts.items
                    else
                        [],
                };
            },
            .joiner joiner => {
                const allocator = allocator_to_use ?? allocatorForSize(joiner.len);

                if (amt := display_size) {
                    amt.* = joiner.len;
                }

                const buffer = brk: {
                    if (enable_source_map_shifts && FeatureFlags.source_map_debug_id) {
                        // This comment must go before the //# sourceMappingURL comment
                        const debug_id_fmt = std.fmt.allocPrint(
                            graph.heap.allocator(),
                            "\n//# debugId={f}\n",
                            [ bun.SourceMap.DebugIDFormatter{ id: chunk.isolated_hash } ],
                        ) catch |e| bun.handleOom(e);

                        break :brk joiner.doneWithEnd(allocator, debug_id_fmt)?;
                    }

                    break :brk joiner.done(allocator)?;
                };

                return {
                    buffer: buffer,
                    shifts: [],
                };
            },
            .empty => return {
                buffer: "",
                shifts: [],
            },
        }
    }
};

pub const Chunk = struct {
    /// This is a random string and is used to represent the output path of this
    /// chunk before the final output path has been computed. See OutputPiece
    /// for more info on this technique.
    unique_key: string = "",

    /// Maps source index to bytes contributed to this chunk's output (for metafile).
    /// The value is updated during chunk generation to track bytesInOutput.
    files_with_parts_in_chunk: std.AutoArrayHashMapUnmanaged(Index.Int, usize) = {},

    /// We must not keep pointers to this type until all chunks have been allocated.
    entry_bits: AutoBitSet = undefined,

    final_rel_path: string = "",
    /// The path template used to generate `final_rel_path`
    template: PathTemplate = {},

    /// For code splitting
    cross_chunk_imports: BabyList(ChunkImport) = {},

    content: ChunkContent,

    entry_point: ChunkEntryPoint = {},

    output_source_map: SourceMap.SourceMapPieces,

    intermediate_output: ChunkIntermediateOutput = .empty,
    isolated_hash: u64 = std.math.maxInt(u64),

    renamer: renamer.Renamer = undefined,

    compile_results_for_chunk: []CompileResult = [],

    /// Pre-built JSON fragment for this chunk's metafile output entry.
    /// Generated during parallel chunk generation, joined at the end.
    metafile_chunk_json: []const u8 = "",

    /// Pack boolean flags to reduce padding overhead.
    /// Previously 3 separate bool fields caused ~21 bytes of padding waste.
    flags: ChunkFlags = {},

    pub const Flags = ChunkFlags;
    pub const Order = ChunkOrder;
    pub const IntermediateOutput = ChunkIntermediateOutput;
    pub const OutputPiece = ChunkOutputPiece;
    pub const OutputPieceIndex = ChunkOutputPiece.Query;
    pub const EntryPoint = ChunkEntryPoint;
    pub const JavaScriptChunk = ChunkJavaScriptChunk;
    pub const CssChunk = ChunkCssChunk;
    pub const CssImportOrder = ChunkCssImportOrder;
    pub const ImportsFromOtherChunks = ChunkImportsFromOtherChunks;
    pub const Content = ChunkContent;

    pub inline fn isEntryPoint(): bool {
        return _.entry_point.is_entry_point;
    }

    /// Returns the HTML closing tag that must be escaped when this chunk's content
    /// is inlined into a standalone HTML file (e.g. "</script" for JS, "</style" for CSS).
    pub fn closingTagForContent(): []const u8 {
        return switch (_.content) {
            .javascript => "</script",
            .css => "</style",
            .html => unreachable,
        };
    }

    pub fn getJSChunkForHTML(chunks: []Chunk): (*Chunk)? {
        const entry_point_id = _.entry_point.entry_point_id;
        for (*other of chunks) {
            if (other.content == .javascript) {
                if (other.entry_point.entry_point_id == entry_point_id) {
                    return other;
                }
            }
        }
        return null;
    }

    pub fn getCSSChunkForHTML(chunks: []Chunk): (*Chunk)? {
        // Look up the CSS chunk via the JS chunk's css_chunks indices.
        // This correctly handles deduplicated CSS chunks that are shared
        // across multiple HTML entry points (see issue #23668).
        if (js_chunk := _.getJSChunkForHTML(chunks)) {
            const css_chunk_indices = js_chunk.content.javascript.css_chunks;
            if (css_chunk_indices.len > 0) {
                return &chunks[css_chunk_indices[0]];
            }
        }
        // Fallback: match by entry_point_id for cases without a JS chunk.
        const entry_point_id = _.entry_point.entry_point_id;
        for (*other of chunks) {
            if (other.content == .css) {
                if (other.entry_point.entry_point_id == entry_point_id) {
                    return other;
                }
            }
        }
        return null;
    }

    pub inline fn entryBits(): *const AutoBitSet {
        return &_.entry_bits;
    }
};

pub const Flags = ChunkFlags;
pub const Order = ChunkOrder;
pub const IntermediateOutput = ChunkIntermediateOutput;
pub const OutputPiece = ChunkOutputPiece;
pub const OutputPieceIndex = ChunkOutputPiece.Query;
pub const EntryPoint = ChunkEntryPoint;
pub const JavaScriptChunk = ChunkJavaScriptChunk;
pub const CssChunk = ChunkCssChunk;
pub const CssImportOrder = ChunkCssImportOrder;
pub const Content = ChunkContent;

pub const Ref = bun.ast.Ref;
pub const Index = bun.ast.Index;
pub const DeferredBatchTask = bun.bundle_v2.DeferredBatchTask;
pub const ThreadPool = bun.bundle_v2.ThreadPool;
pub const ParseTask = bun.bundle_v2.ParseTask;
