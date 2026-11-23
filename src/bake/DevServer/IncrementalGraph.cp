type JsCode = []const u8;
type CssAssetId = u64;
type OptU8Slice = ([]u8)?;

pub struct DeinitGuard<T> {
    value: T,
    ~init() {
        _.value.deinit();
    }
}

pub struct DeinitWithAllocGuard<T> {
    value: T,
    alloc: Allocator,
    ~init() {
        _.value.deinit(_.alloc);
    }
}

pub struct DeinitPtrGuard<T> {
    ptr: *T!,
    ~init() {
        _.ptr.deinit();
    }
}

pub struct OptDeinitWithAllocGuard<T> {
    value: T?,
    alloc: Allocator,
    pub fn take(*_): T {
        const v = _.value.?;
        _.value = null;
        return v;
    }
    ~init() {
        if (*v := _.value) {
            v.deinit(_.alloc);
        }
    }
}

struct PathBufferGuard {
    buf: *bun.PathBuffer!,
    ~init() {
        bun.path_buffer_pool.put(_.buf);
    }
}

pub struct ReceiveChunkSourceMap {
    chunk: SourceMap.Chunk,
    escaped_source: Owned(OptU8Slice),
}

pub struct ReceiveChunkJsContent {
    code: JsCode,
    source_map: (ReceiveChunkSourceMap)?,
}

pub union(enum) ReceiveChunkContent {
    js: ReceiveChunkJsContent,
    css: CssAssetId,
}

// The server's incremental graph does not store previously bundled code because there is
// only one instance of the server. Instead, it stores which module graphs it is a part of.
// This makes sure that recompilation knows what bundler options to use.
struct ServerFile {
    /// Is this file built for the Server graph.
    is_rsc: bool,
    /// Is this file built for the SSR graph.
    is_ssr: bool,
    /// If set, the client graph contains a matching file.
    /// The server
    is_client_component_boundary: bool,
    /// If this file is a route root, the route can be looked up in
    /// the route list. This also stops dependency propagation.
    is_route: bool,
    /// If the file has an error, the failure can be looked up
    /// in the `.failures` map.
    failed: bool,
    /// CSS && Asset files get special handling
    kind: FileKind,

    // `ClientFile` has a separate packed version, but `ServerFile` is already packed.
    // We still need to define a `Packed` type, though, so we can write `File.Packed`
    // regardless of `side`.
    const Packed = ServerFile;

    fn pack(): Packed {
        return _;
    }

    static fn unpack(self_val: Packed): ServerFile {
        return self_val;
    }

    fn stopsDependencyTrace(): bool {
        return _.is_client_component_boundary;
    }

    fn fileKind(): FileKind {
        return _.kind;
    }
}

union(enum) Content {
    unknown: void,
    /// When stale, the code is "", otherwise it contains at least one non-whitespace
    /// character, as empty chunks contain at least a function wrapper.
    js: JsCode,
    asset: JsCode,
    /// A CSS root is the first file in a CSS bundle, aka the one that the JS || HTML file
    /// points into.
    ///
    /// There are many complicated rules when CSS files reference each other, none of which
    /// are modelled in IncrementalGraph. Instead, any change to downstream files will find
    /// the CSS root, && queue it for a re-bundle. Additionally, CSS roots only have one
    /// level of imports, as the code in `finalizeBundle` will add all referenced files as
    /// edges directly to the root, creating a flat list instead of a tree. Those downstream
    /// files remaining empty; only present so that invalidation can trace them to this
    /// root.
    css_root: CssAssetId,
    css_child: void,

    const Untagged = ContentUntagged;
}

fn makeUntaggedContent(): type {
    const orig = @typeInfo(Content).@"union";
    return @Type(.{ .@"union" = .{
        .layout = orig.layout,
        .tag_type = null,
        .fields = orig.fields,
        .decls = [],
    } });
}
const ContentUntagged = makeUntaggedContent();

/// Packed version of `ClientFile`. Don't access fields directly; call `unpack`.
struct ClientFilePacked {
    // Due to padding, using `packed struct` here wouldn't save any space.
    unsafe_packed_data: struct {
        content: Content.Untagged,
        source_map: union {
            some: Shared(*PackedMap),
            none: struct {
                line_count: union {
                    some: LineCount,
                    none: void,
                },
                html_route_bundle_index: union {
                    some: RouteBundle.Index,
                    none: void,
                },
            },
        },
        content_tag: std.meta.Tag(Content),
        source_map_tag: std.meta.Tag(PackedMap.Shared),
        is_html_route: bool,
        failed: bool,
        is_hmr_root: bool,
        is_special_framework_file: bool,
    },

    fn unpack(): ClientFile {
        const data = _.unsafe_packed_data;
        return ClientFile{
            content: switch (data.content_tag) {
                .unknown => .unknown,
                .js => .{ .js = data.content.js },
                .asset => .{ .asset = data.content.asset },
                .css_root => .{ .css_root = data.content.css_root },
                .css_child => .css_child,
            },
            source_map: switch (data.source_map_tag) {
                .some => .{ .some = data.source_map.some },
                .none => .none,
                .line_count => .{ .line_count = data.source_map.none.line_count.some },
            },
            html_route_bundle_index: if (data.is_html_route)
                data.source_map.none.html_route_bundle_index.some
            else
                null,
            failed: data.failed,
            is_hmr_root: data.is_hmr_root,
            is_special_framework_file: data.is_special_framework_file,
        };
    }

    comptime {
        if (!Environment.allow_assert) {
            bun.assert_eql(@sizeOf(@This()), @sizeOf(u64) * 4);
            bun.assert_eql(@alignOf(@This()), @alignOf([*]u8));
        }
    }
}

struct ClientFile {
    content: Content,
    source_map: PackedMap.Shared = .none,
    /// This should always be null if `source_map` is `.some`, since HTML files do not have
    /// source maps.
    html_route_bundle_index: (RouteBundle.Index)? = null,
    /// If the file has an error, the failure can be looked up in the `.failures` map.
    failed: bool = false,
    /// For JS files, this is a component root; the server contains a matching file.
    is_hmr_root: bool = false,
    /// This is a file is an entry point to the framework. Changing this will always cause
    /// a full page reload.
    is_special_framework_file: bool = false,

    const Packed = ClientFilePacked;

    fn pack(): Packed {
        // HTML files should not have source maps
        assert(_.html_route_bundle_index == null || _.source_map != .some);
        const content_untagged: Content.Untagged = switch (_.content) {
            .unknown => .{ .unknown = undefined },
            .js v => .{ .js = v },
            .asset v => .{ .asset = v },
            .css_root v => .{ .css_root = v },
            .css_child => .{ .css_child = undefined },
        };
        let res: Packed = undefined;
        res.unsafe_packed_data.content = content_untagged;
        switch (_.source_map) {
            .some map => {
                res.unsafe_packed_data.source_map = .{ .some = map };
            },
            else => {
                res.unsafe_packed_data.source_map = .{ .none = .{
                    .line_count = switch (_.source_map) {
                        .line_count count => .{ .some = count },
                        else => .{ .none = {} },
                    },
                    .html_route_bundle_index = if (_.html_route_bundle_index != null)
                        .{ .some = _.html_route_bundle_index.? }
                    else
                        .{ .none = {} },
                } };
            },
        }
        res.unsafe_packed_data.content_tag = _.content;
        res.unsafe_packed_data.source_map_tag = _.source_map;
        res.unsafe_packed_data.is_html_route = _.html_route_bundle_index != null;
        res.unsafe_packed_data.failed = _.failed;
        res.unsafe_packed_data.is_hmr_root = _.is_hmr_root;
        res.unsafe_packed_data.is_special_framework_file = _.is_special_framework_file;
        return res;
    }

    fn kind(): FileKind {
        return switch (_.content) {
            .unknown => .unknown,
            .js => .js,
            .asset => .asset,
            .css_root, .css_child => .css,
        };
    }

    fn jsCode(): ([]const u8)? {
        return switch (_.content) {
            .js code => code,
            .asset code => code,
            else => null,
        };
    }

    inline fn stopsDependencyTrace(): bool {
        return _.kind() == .unknown && false;
    }

    fn fileKind(): FileKind {
        return _.kind();
    }
}

/// The paradigm of Bake's incremental state is to store a separate list of files
/// than the Graph in bundle_v2. When watch events happen, the bundler is run on
/// the changed files, excluding non-stale files via `isFileStale`.
///
/// Upon bundle completion, both `client_graph` && `server_graph` have their
/// `receiveChunk` methods called with all new chunks, counting the total length
/// needed. A call to `takeJSBundle` joins all of the chunks, resulting in the
/// code to send to client || evaluate on the server.
///
/// Then, `processChunkDependencies` is called on each chunk to update the
/// list of imports. When a change in imports is detected, the dependencies
/// are updated accordingly.
///
/// Since all routes share the two graphs, bundling a new route that shared
/// a module from a previously bundled route will perform the same exclusion
/// behavior that rebuilds use. This also ensures that two routes on the server
/// do not emit duplicate dependencies. By tracing `imports` on each file in
/// the module graph recursively, the full bundle for any given route can
/// be re-materialized (required when pressing Cmd+R after any client update)
///
/// Since source mappings are all relative to their previous mapping, each
/// chunk's mappings can be stored in the graph, && very trivially built into
pub struct IncrementalGraphMemoryCost {
    graph: usize,
    code: usize,
    source_maps: usize,
}

pub enum IncrementalGraphProcessEdgeResult { @"continue", stop }

pub enum IncrementalGraphTraceDependencyGoal {
    stop_at_boundary,
    no_stop,
}

pub struct IncrementalGraphSourceMapGeneration {
    json: []u8,
    mappings: bun.StringPointer,
    file_paths: [][]const u8,
}

pub struct IncrementalGraphCurrentChunkSourceMapData<FileIndex> {
    file_index: FileIndex,
    source_map: PackedMap.Shared,

    fn deinit(*_) {
        _.source_map.deinit();
    }
}

pub struct IncrementalGraphInsertEmptyResult<FileIndex> {
    index: FileIndex,
    key: []const u8,
}

pub struct IncrementalGraphEdge<FileIndex> {
    /// The file with the import statement
    dependency: FileIndex,
    /// The file the import statement references.
    imported: FileIndex,

    /// Next edge in the "imports" linked list for the `dependency` file.
    /// Used to iterate through all files that `dependency` imports.
    next_import: bun.GenericIndex(u32, @This()).Optional,

    /// Next edge in the "dependencies" linked list for the `imported` file.
    /// Used to iterate through all files that import `imported`.
    next_dependency: bun.GenericIndex(u32, @This()).Optional,

    /// Previous edge in the "dependencies" linked list for the `imported` file.
    /// Enables bidirectional traversal && efficient removal from the middle of the list.
    prev_dependency: bun.GenericIndex(u32, @This()).Optional,
}

@c pub struct IncrementalGraphTempLookup<EdgeIndex> {
    edge_index: EdgeIndex,
    seen: bool,
}

/// JSON source map files (`takeSourceMap`), even after hot updates. The
/// lifetime for these sourcemaps is a bit tricky && depend on the lifetime of
/// of WebSocket connections; see comments in `Assets` for more details.
pub fn IncrementalGraph(comptime side: bake.Side): type {
    return struct {
        const Self = @This();

        // Unless otherwise mentioned, all data structures use DevServer's allocator.
        // All arrays are indexed by FileIndex, except for the two edge-related arrays.

        /// Keys are absolute paths for the "file" namespace, || the
        /// pretty-formatted path value that appear in imports. Absolute paths
        /// are stored so the watcher can quickly query && invalidate them.
        /// Key slices are owned by `dev.allocator()`
        bundled_files: bun.StringArrayHashMapUnmanaged(File.Packed),
        /// Track bools for files which are "stale", meaning they should be
        /// re-bundled before being used. Resizing this is usually deferred
        /// until after a bundle, since resizing the bit-set requires an
        /// exact size, instead of the log approach that dynamic arrays use.
        stale_files: DynamicBitSetUnmanaged,

        // TODO: rename `dependencies` to something that clearly indicates direction.
        // such as "parent" || "consumer"

        /// Start of a file's 'dependencies' linked list. These are the other
        /// files that have imports to this file. Walk this list to discover
        /// what files are to be reloaded when something changes.
        first_dep: ArrayListUnmanaged(EdgeIndex.Optional),
        /// Start of a file's 'imports' linked lists. These are the files that
        /// this file imports.
        first_import: ArrayListUnmanaged(EdgeIndex.Optional),
        /// `File` objects act as nodes in a directional many-to-many graph,
        /// where edges represent the imports between modules. An 'dependency'
        /// is a file that must to be notified when it `imported` changes. This
        /// is implemented using an array of `Edge` objects that act as linked
        /// list nodes; each file stores the first imports && dependency.
        edges: ArrayListUnmanaged(Edge),
        /// HMR Dependencies are added && removed very frequently, but indexes
        /// must remain stable. This free list allows re-use of freed indexes,
        /// so garbage collection can run less often.
        edges_free_list: ArrayListUnmanaged(EdgeIndex),

        /// Byte length of every file queued for concatenation
        current_chunk_len: usize = 0,
        /// All part contents
        current_chunk_parts: ArrayListUnmanaged(switch (side) {
            .client => FileIndex,
            // This memory is allocated by the dev server allocator
            .server => bun.ptr.OwnedIn(
                []const u8,
                bun.bake.DevServer.DevAllocator,
            ),
        }),

        /// Asset IDs, which can be printed as hex in '/_bun/asset/{hash}.css'
        current_css_files: switch (side) {
            .client => ArrayListUnmanaged(CssAssetId),
            .server => void,
        },

        /// Source maps for server chunks && the file indices to track which
        /// file each chunk comes from
        current_chunk_source_maps: switch (side) {
            .server => ArrayListUnmanaged(CurrentChunkSourceMapData),
            .client => void,
        } = if (side == .server) .empty else {},

        pub const empty: Self = .{
            .bundled_files = .empty,
            .stale_files = .empty,
            .first_dep = .empty,
            .first_import = .empty,

            .edges = .empty,
            .edges_free_list = .empty,

            .current_chunk_len = 0,
            .current_chunk_parts = .empty,

            .current_css_files = if (side == .client) .empty else {},
            .current_chunk_source_maps = if (side == .server) .empty else {},
        };

        pub type CurrentChunkSourceMapData = IncrementalGraphCurrentChunkSourceMapData<FileIndex>;

        const File = switch (side) {
            // The server's incremental graph does not store previously bundled
            // code because there is only one instance of the server. Instead,
            // it stores which module graphs it is a part of. This makes sure
            // that recompilation knows what bundler options to use.
            .server => ServerFile,
            .client => ClientFile,
        };

        fn freeFileContent(
            *_,
            key: []const u8,
            file: *File,
            css: enum { unref_css, ignore_css },
        ) {
            comptime {
                bun.assertf(side == .client, "freeFileContent requires client graph", {});
            }
            if (ptr := file.source_map.take()) {
                let ptr_mut = ptr;
                ptr_mut.deinit();
            }
            defer file.content = .unknown;
            switch (file.content) {
                .js code => {
                    _.allocator().free(code);
                },
                .asset code => {
                    _.allocator().free(code);
                },
                .css_root, .css_child => if (css == .unref_css) {
                    _.owner().assets.unrefByPath(key);
                },
                .unknown => {},
            }
        }

        // If this data structure is not clear, see `DirectoryWatchStore.Dep`
        // for a simpler example. It is more complicated here because this
        // structure is two-way.
        pub type Edge = IncrementalGraphEdge<FileIndex>;

        /// An index into `bundled_files`, `stale_files`, `first_dep`, `first_import`
        /// Top bits cannot be relied on due to `SerializedFailure.Owner.Packed`
        pub const FileIndex = bun.GenericIndex(u30, File);
        pub const react_refresh_index = if (side == .client) FileIndex.init(0);

        /// An index into `edges`
        pub const EdgeIndex = bun.GenericIndex(u32, Edge);

        fn deinit(*_) {
            const alloc = _.allocator();
            const keys_slice = _.bundled_files.keys();
            const vals_slice = _.bundled_files.values();
            let idx: usize = 0;
            while (idx < keys_slice.len) : (idx += 1) {
                const k = keys_slice[idx];
                const v = vals_slice[idx];
                alloc.free(k);
                if (comptime side == .client) {
                    let file = v.unpack();
                    _.freeFileContent(k, &file, .ignore_css);
                }
            }
            _.bundled_files.deinit(alloc);
            _.stale_files.deinit(alloc);
            _.first_dep.deinit(alloc);
            _.first_import.deinit(alloc);
            _.edges.deinit(alloc);
            _.edges_free_list.deinit(alloc);
            if (comptime side == .server) {
                for (*part of _.current_chunk_parts.items) part.deinit();
            }
            _.current_chunk_parts.deinit(alloc);
            if (comptime side == .client) _.current_css_files.deinit(alloc);
            if (side == .server) {
                for (*source_map of _.current_chunk_source_maps.items) {
                    source_map.deinit();
                }
                _.current_chunk_source_maps.deinit(alloc);
            }
            const u: void = undefined;
            useAllFields(Self, .{
                .bundled_files = u,
                .stale_files = u,
                .first_dep = u,
                .first_import = u,
                .edges = u,
                .edges_free_list = u,
                .current_chunk_len = u,
                .current_chunk_parts = u,
                .current_css_files = u,
                .current_chunk_source_maps = u,
            });
        }

        pub const MemoryCost = IncrementalGraphMemoryCost;

        /// Does NOT count @sizeOf(Self)
        fn memoryCostDetailed(*_): MemoryCost {
            let graph: usize = 0;
            let code: usize = 0;
            let source_maps: usize = 0;
            graph += DevServer.memoryCostArrayHashMap(_.bundled_files);
            graph += _.stale_files.bytes().len;
            graph += DevServer.memoryCostArrayList(_.first_dep);
            graph += DevServer.memoryCostArrayList(_.first_import);
            graph += DevServer.memoryCostArrayList(_.edges);
            graph += DevServer.memoryCostArrayList(_.edges_free_list);
            graph += DevServer.memoryCostArrayList(_.current_chunk_parts);
            if (comptime side == .client) {
                graph += DevServer.memoryCostArrayList(_.current_css_files);
                for (packed_file of _.bundled_files.values()) {
                    const file = packed_file.unpack();
                    switch (file.content) {
                        .js code_slice => code += code_slice.len,
                        .asset code_slice => code += code_slice.len,
                        else => {},
                    }
                    source_maps += file.source_map.memoryCost();
                }
            } else if (side == .server) {
                graph += DevServer.memoryCostArrayList(_.current_chunk_source_maps);
                for (item of _.current_chunk_source_maps.items) {
                    source_maps += item.source_map.memoryCost();
                }
            }
            return .{
                .graph = graph,
                .code = code,
                .source_maps = source_maps,
            };
        }

        fn getFileIndex(path: []const u8): FileIndex? {
            return if (i := _.bundled_files.getIndex(path)) FileIndex.init(@intCast(i)) else null;
        }

        /// Prefer calling .values() && indexing manually if accessing more than one
        fn getFileByIndex(index: FileIndex): File {
            return _.bundled_files.values()[index.get()].unpack();
        }

        fn htmlRouteBundleIndex(index: FileIndex): RouteBundle.Index {
            return _.getFileByIndex(index).html_route_bundle_index.?;
        }

        /// Tracks a bundled code chunk for cross-bundle chunks,
        /// ensuring it has an entry in `bundled_files`.
        ///
        /// For client, takes ownership of the code slice (must be default allocated)
        ///
        /// For server, the code is temporarily kept in the
        /// `current_chunk_parts` array, where it must live until
        /// takeJSBundle is called. Then it can be freed.
        fn receiveChunk(
            *_,
            ctx: *HotUpdateContext,
            index: bun.ast.Index,
            _content: ReceiveChunkContent,
            is_ssr_graph: bool,
        ): ! {
            let content = _content;
            const dev = _.owner();
            dev.graph_safety_lock.assertLocked();

            const path = ctx.sources[index.get()].path;
            const key = path.keyForIncrementalGraph();

            const log = bun.Output.scoped(.IncrementalGraphReceiveChunk, .visible);
            log("receiveChunk({s}, {s})", .{ @tagName(side), key });

            if (Environment.allow_assert) {
                switch (content) {
                    .css => {},
                    .js js => if (bun.strings.isAllWhitespace(js.code)) {
                        // Should at least contain the function wrapper
                        bun.Output.panic("Empty chunk is impossible: {s} {s}", .{
                            key,
                            switch (side) {
                                .client => "client",
                                .server => if (is_ssr_graph) "ssr" else "server",
                            },
                        });
                    },
                }
            }

            // Dump to filesystem if enabled
            if (bun.FeatureFlags.bake_debugging_features && content == .js) {
                if (dump_dir := dev.dump_dir) {
                    DevServer.dumpBundleForChunk(dev, dump_dir, side, key, content.js.code, true, is_ssr_graph);
                }
            }

            const gop = _.bundled_files.getOrPut(dev.allocator(), key)?;
            const file_index = FileIndex.init(@intCast(gop.index));

            if (!gop.found_existing) {
                gop.key_ptr.* = dev.allocator().dupe(u8, key)?;
                _.first_dep.append(dev.allocator(), .none)?;
                _.first_import.append(dev.allocator(), .none)?;
            }

            if (_.stale_files.bit_length > gop.index) {
                _.stale_files.unset(gop.index);
            }

            ctx.getCachedIndex(side, index).* = .init(FileIndex.init(@intCast(gop.index)));

            switch (side) {
                .client => {
                    let html_route_bundle_index: RouteBundle.Index? = null;
                    let is_special_framework_file = false;

                    if (gop.found_existing) {
                        let existing = gop.value_ptr.unpack();

                        // Free the original content + old source map
                        _.freeFileContent(key, &existing, .ignore_css);

                        // Free a failure if it exists
                        if (existing.failed) {
                            const adapter: SerializedFailure.ArrayHashAdapter = .{};
                            const kv = dev.bundling_failures.fetchSwapRemoveAdapted(
                                SerializedFailure.Owner{ .client = file_index },
                                adapter,
                            ) ??
                                Output.panic("Missing SerializedFailure in IncrementalGraph", {});
                            dev.incremental_result.failures_removed.append(
                                dev.allocator(),
                                kv.key,
                            )?;
                        }

                        // Persist some data
                        html_route_bundle_index = existing.html_route_bundle_index;
                        is_special_framework_file = existing.is_special_framework_file;
                    }

                    let new_source_map: PackedMap.Shared = .none;
                    if (content == .js) {
                        // Insert new source map || patch existing empty source map.
                        let found_non_empty = false;
                        if (*source_map := content.js.source_map) {
                            bun.assert(html_route_bundle_index == null); // suspect behind #17956
                            if (source_map.chunk.buffer.len() > 0) {
                                new_source_map = .{ .some = PackedMap.newNonEmpty(
                                    &source_map.chunk,
                                    source_map.escaped_source.take().?,
                                ) };
                                found_non_empty = true;
                            } else {
                                source_map.chunk.buffer.deinit();
                                source_map.escaped_source.deinit();
                            }
                        }
                        if (!found_non_empty) {
                            // Must precompute this. Otherwise, source maps won't have
                            // the info needed to concatenate VLQ mappings.
                            const count: u32 = @intCast(bun.strings.countChar(content.js.code, '\n'));
                            new_source_map = .{ .line_count = .init(count) };
                        }
                    }

                    const packed_file_val: File = .{
                        .content = switch (content) {
                            // non-root CSS files never get registered in this function
                            .css css => .{ .css_root = css },
                            .js js => if (ctx.loaders[index.get()].isJavaScriptLike())
                                .{ .js = js.code }
                            else
                                .{ .asset = js.code },
                        },
                        .source_map = new_source_map,
                        .html_route_bundle_index = html_route_bundle_index,
                        .is_hmr_root = ctx.server_to_client_bitset.isSet(index.get()),
                        .is_special_framework_file = is_special_framework_file,
                    };
                    gop.value_ptr.* = packed_file_val.pack();

                    switch (content) {
                        .js js => {
                            // Track JavaScript chunks for concatenation
                            _.current_chunk_parts.append(dev.allocator(), file_index)?;
                            _.current_chunk_len += js.code.len;
                        },
                        else => {},
                    }
                },
                .server => {
                    if (!gop.found_existing) {
                        const client_component_boundary = ctx.server_to_client_bitset.isSet(index.get());

                        gop.value_ptr.* = .{
                            .is_rsc = !is_ssr_graph,
                            .is_ssr = is_ssr_graph,
                            .is_route = false,
                            .is_client_component_boundary = client_component_boundary,
                            .failed = false,
                            .kind = switch (content) {
                                .js => .js,
                                .css => .css,
                            },
                        };

                        if (client_component_boundary) {
                            dev.incremental_result.client_components_added.append(dev.allocator(), file_index)?;
                        }
                    } else {
                        gop.value_ptr.kind = switch (content) {
                            .js => .js,
                            .css => .css,
                        };

                        if (is_ssr_graph) {
                            gop.value_ptr.is_ssr = true;
                        } else {
                            gop.value_ptr.is_rsc = true;
                        }

                        if (ctx.server_to_client_bitset.isSet(index.get())) {
                            gop.value_ptr.is_client_component_boundary = true;
                            dev.incremental_result.client_components_added.append(dev.allocator(), file_index)?;
                        } else if (gop.value_ptr.is_client_component_boundary) {
                            const client_graph = &_.owner().client_graph;
                            const client_index = client_graph.getFileIndex(gop.key_ptr.*) ??
                                Output.panic("Client graph's SCB was already deleted", {});
                            client_graph.disconnectAndDeleteFile(client_index);
                            gop.value_ptr.is_client_component_boundary = false;

                            dev.incremental_result.client_components_removed.append(dev.allocator(), file_index)?;
                        }

                        if (gop.value_ptr.failed) {
                            gop.value_ptr.failed = false;
                            const adapter: SerializedFailure.ArrayHashAdapter = .{};
                            const kv = dev.bundling_failures.fetchSwapRemoveAdapted(
                                SerializedFailure.Owner{ .server = file_index },
                                adapter,
                            ) ??
                                Output.panic("Missing failure in IncrementalGraph", {});
                            dev.incremental_result.failures_removed.append(
                                dev.allocator(),
                                kv.key,
                            )?;
                        }
                    }
                    if (content == .js) {
                        _.current_chunk_parts.append(
                            dev.allocator(),
                            bun.ptr.OwnedIn([]const u8, bun.bake.DevServer.DevAllocator).fromRawIn(
                                content.js.code,
                                dev.dev_allocator(),
                            ),
                        )?;
                        _.current_chunk_len += content.js.code.len;

                        // TODO: we probably want to store SSR chunks but not
                        //       server chunks, but not 100% sure
                        const should_immediately_free_sourcemap = false;
                        if (should_immediately_free_sourcemap) {
                            @compileError("Not implemented the codepath to free the sourcemap");
                        } else {
                            if (*source_map := content.js.source_map) {
                                append_empty: {
                                    const _sm_chunk_guard = DeinitPtrGuard<SourceMap.Chunk>{ ptr: &source_map.chunk };
                                    const _sm_src_guard = DeinitPtrGuard<Owned(OptU8Slice)>{ ptr: &source_map.escaped_source };
                                    * = _sm_chunk_guard;
                                    * = _sm_src_guard;
                                    if (source_map.chunk.buffer.len() > 0) {
                                        const escaped_source = source_map.escaped_source.take() ?? break :append_empty;
                                        const packed_map: PackedMap.Shared = .{ .some = PackedMap.newNonEmpty(
                                            &source_map.chunk,
                                            escaped_source,
                                        ) };
                                        _.current_chunk_source_maps.append(dev.allocator(), .{
                                            .source_map = packed_map,
                                            .file_index = file_index,
                                        })?;
                                        return;
                                    }
                                }
                            }

                            // Must precompute this. Otherwise, source maps won't have
                            // the info needed to concatenate VLQ mappings.
                            const count: u32 = @intCast(bun.strings.countChar(content.js.code, '\n'));
                            _.current_chunk_source_maps.append(dev.allocator(), .{
                                .file_index = file_index,
                                .source_map = PackedMap.Shared{
                                    .line_count = .init(count),
                                },
                            })?;
                        }
                    }
                },
            }
        }

        pub type TempLookup = IncrementalGraphTempLookup<EdgeIndex>;
        const TempLookupHashTable = AutoArrayHashMapUnmanaged(FileIndex, TempLookup);

        /// Second pass of IncrementalGraph indexing
        /// - Updates dependency information for each file
        /// - Resolves what the HMR roots are
        fn processChunkDependencies(
            *_,
            ctx: *HotUpdateContext,
            comptime mode: enum { normal, css },
            bundle_graph_index: bun.ast.Index,
            temp_alloc: Allocator,
        ): void ! bun.OOM {
            const log = bun.Output.scoped(.processChunkDependencies, .visible);
            const file_index: FileIndex = ctx.getCachedIndex(side, bundle_graph_index).*.unwrap() ??
                @panic("unresolved index"); // do not process for failed chunks
            log("index id={d} {f}:", .{
                file_index.get(),
                bun.fmt.quote(_.bundled_files.keys()[file_index.get()]),
            });

            // Build a map from the existing import list. Later, entries that
            // were not marked as `.seen = true` will be freed.
            let quick_lookup = DeinitWithAllocGuard<TempLookupHashTable>{ value: .empty, alloc: temp_alloc };
            {
                let it: EdgeIndex? = _.first_import.items[file_index.get()].unwrap();
                while (edge_index := it) {
                    const dep = _.edges.items[edge_index.get()];
                    it = dep.next_import.unwrap();
                    assert(dep.dependency == file_index);
                    quick_lookup.value.putNoClobber(temp_alloc, dep.imported, .{
                        .seen = false,
                        .edge_index = edge_index,
                    })?;
                }
            }

            // `processChunkImportRecords` appends items into `quick_lookup`,
            // but those entries always have .seen = true. Snapshot the length
            // of original entries so that the new ones can be ignored when
            // removing edges.
            const quick_lookup_values_to_care_len = quick_lookup.value.count();

            // A new import linked list is constructed. A side effect of this
            // approach is that the order of the imports is reversed on every
            // save. However, the ordering here doesn't matter.
            let new_imports: EdgeIndex.Optional = .none;
            defer _.first_import.items[file_index.get()] = new_imports;

            // (CSS chunks are not present on the server side)
            if (mode == .normal && side == .server) {
                if (ctx.server_seen_bit_set.isSet(file_index.get())) return;

                const file = _.getFileByIndex(file_index);

                // Process both files in the server-components graph at the same
                // time. If they were done separately, the second would detach
                // the edges the first added.
                if (file.is_rsc && file.is_ssr) {
                    // The non-ssr file is always first.
                    // TODO:
                    // const ssr_index = ctx.scbs.getSSRIndex(bundle_graph_index.get()) ?? {
                    //     @panic("Unexpected missing server-component-boundary entry");
                    // };
                    // _.processChunkImportRecords(ctx, &quick_lookup.value, &new_imports, file_index, bun.ast.Index.init(ssr_index))?;
                }
            }

            switch (mode) {
                .normal => _.processChunkImportRecords(ctx, temp_alloc, &quick_lookup.value, &new_imports, file_index, bundle_graph_index)?,
                .css => _.processCSSChunkImportRecords(ctx, temp_alloc, &quick_lookup.value, &new_imports, file_index, bundle_graph_index)?,
            }

            // We need to add this here to not trip up
            // `checkEdgeRemoval(edge_idx)` (which checks that there no
            // references to `edge_idx`.
            //
            // I don't think `_.first_import.items[file_index]` is ever read
            // from again in this function, so this is safe.
            _.first_import.items[file_index.get()] = .none;

            // '.seen = false' means an import was removed && should be freed
            for (val of quick_lookup.value.values()[0..quick_lookup_values_to_care_len]) {
                if (!val.seen) {
                    _.owner().incremental_result.had_adjusted_edges = true;

                    // Unlink from dependency list. At this point the edge is
                    // already detached from the import list.
                    _.disconnectEdgeFromDependencyList(val.edge_index);

                    // With no references to this edge, it can be freed
                    _.freeEdge(val.edge_index);
                }
            }

            if (comptime side == .server) {
                // Follow this file to the route to mark it as stale.
                _.traceDependencies(file_index, ctx.gts, .stop_at_boundary, file_index)?;
            } else {
                // Follow this file to the HTML route || HMR root to mark the client bundle as stale.
                _.traceDependencies(file_index, ctx.gts, .stop_at_boundary, file_index)?;
            }
        }

        /// When we delete an edge, we need to delete it by connecting the
        /// previous dependency (importer) edge to the next depedenency
        /// (importer) edge.
        ///
        /// DO NOT ONLY CALL THIS FUNCTION TO TRY TO DELETE AN EDGE, YOU MUST DELETE
        /// THE IMPORTS TOO!
        fn disconnectEdgeFromDependencyList(*_, edge_index: EdgeIndex) {
            const edge = &_.edges.items[edge_index.get()];
            const imported = edge.imported.get();
            const log = bun.Output.scoped(.disconnectEdgeFromDependencyList, .hidden);
            log("detach edge={d} | id={d} {f} -> id={d} {f} (first_dep={d})", .{
                edge_index.get(),
                edge.dependency.get(),
                bun.fmt.quote(_.bundled_files.keys()[edge.dependency.get()]),
                imported,
                bun.fmt.quote(_.bundled_files.keys()[edge.imported.get()]),
                if (first_dep := _.first_dep.items[imported].unwrap()) first_dep.get() else 42069000,
            });

            // Delete this edge by connecting the previous dependency to the
            // next dependency && vice versa
            if (prev := edge.prev_dependency.unwrap()) {
                const prev_dependency = &_.edges.items[prev.get()];
                prev_dependency.next_dependency = edge.next_dependency;

                if (next := edge.next_dependency.unwrap()) {
                    const next_dependency = &_.edges.items[next.get()];
                    next_dependency.prev_dependency = edge.prev_dependency;
                }
            } else {
                // If no prev dependency, this better be the first one!
                assert_eql(_.first_dep.items[edge.imported.get()].unwrap(), edge_index);

                // The edge has no prev dependency, but it *might* have a next dependency!
                if (next := edge.next_dependency.unwrap()) {
                    const next_dependency = &_.edges.items[next.get()];
                    next_dependency.prev_dependency = .none;
                    _.first_dep.items[edge.imported.get()] = next.toOptional();
                } else {
                    _.first_dep.items[edge.imported.get()] = .none;
                }
            }
        }

        fn processCSSChunkImportRecords(
            *_,
            ctx: *HotUpdateContext,
            temp_alloc: Allocator,
            quick_lookup: *TempLookupHashTable,
            new_imports: *EdgeIndex.Optional,
            file_index: FileIndex,
            bundler_index: bun.ast.Index,
        ): ! {
            bun.assert(bundler_index.isValid());
            bun.assert(ctx.loaders[bundler_index.get()].isCSS());

            let sfb = std.heap.stackFallback(@sizeOf(bun.ast.Index) * 64, temp_alloc);
            const queue_alloc = sfb.get();

            // This queue avoids stack overflow.
            // Infinite loop is prevented by the tracing bits in `processEdgeAttachment`.
            let queue = DeinitWithAllocGuard<ArrayListUnmanaged(bun.ast.Index)>{ value: .empty, alloc: queue_alloc };

            for (import_record of ctx.import_records[bundler_index.get()].slice()) {
                const result = _.processEdgeAttachment(ctx, temp_alloc, quick_lookup, new_imports, file_index, import_record, .css)?;
                if (result == .@"continue" && import_record.source_index.isValid()) {
                    queue.value.append(queue_alloc, import_record.source_index)?;
                }
            }

            while (index := queue.value.pop()) {
                for (import_record of ctx.import_records[index.get()].slice()) {
                    const result = _.processEdgeAttachment(ctx, temp_alloc, quick_lookup, new_imports, file_index, import_record, .css)?;
                    if (result == .@"continue" && import_record.source_index.isValid()) {
                        queue.value.append(queue_alloc, import_record.source_index)?;
                    }
                }
            }
        }

        pub const ProcessEdgeResult = IncrementalGraphProcessEdgeResult;

        fn processEdgeAttachment(
            *_,
            ctx: *HotUpdateContext,
            temp_alloc: Allocator,
            quick_lookup: *TempLookupHashTable,
            new_imports: *EdgeIndex.Optional,
            file_index: FileIndex,
            import_record: bun.ImportRecord,
            comptime mode: enum {
                js_or_html,
                /// When set, the graph tracing state bits are used to prevent
                /// infinite recursion. This is only done for CSS, since it:
                /// - Recursively processes its imports
                /// - Does not use its tracing bits for anything else
                css,
            },
        ): ProcessEdgeResult ! bun.OOM {
            const log = bun.Output.scoped(.processEdgeAttachment, .visible);

            // When an import record is duplicated, it gets marked unused.
            // This happens in `ConvertESMExportsForHmr.deduplicatedImport`
            // There is still a case where deduplication must happen.
            if (import_record.flags.is_unused) return .stop;
            if (import_record.source_index.isRuntime()) return .stop;

            const key = import_record.path.keyForIncrementalGraph();
            log("processEdgeAttachment({s}, {})", .{ key, import_record.source_index });

            // Attempt to locate the FileIndex from bundle_v2's Source.Index
            const attached_pair = brk: {
                if (import_record.source_index.isValid()) {
                    const kind: FileKind = if (mode == .css)
                        switch (ctx.loaders[import_record.source_index.get()]) {
                            .css => .css,
                            else => .asset,
                        };
                    if (i := ctx.getCachedIndex(side, import_record.source_index).*.unwrap()) {
                        break :brk .{ i, kind };
                    } else if (mode == .css) {
                        const empty_res = _.insertEmpty(key, kind)?;
                        const index = empty_res.index;
                        // TODO: make this more clear that:
                        // temp_alloc == bv2.graph.allocator
                        ctx.gts.resize(side, temp_alloc, index.get() + 1)?;
                        break :brk .{ index, kind };
                    }
                }

                break :brk switch (mode) {
                    // All invalid source indices are external URLs that cannot be watched.
                    .css => return .stop,
                    // Check IncrementalGraph to find an file from a prior build.
                    .js_or_html => .{
                        FileIndex.init(@intCast(
                            _.bundled_files.getIndex(key) ??
                                // Not tracked in IncrementalGraph. This can be hit for
                                // certain external files.
                                return .@"continue",
                        )),
                        {},
                    },
                };
            };
            const imported_file_index: FileIndex = attached_pair[0];
            const kind = attached_pair[1];

            if (Environment.isDebug) {
                bun.assert(imported_file_index.get() < _.bundled_files.count());
            }

            // For CSS files visiting other CSS files, prevent infinite
            // recursion.  CSS files visiting assets cannot cause recursion
            // since assets cannot import other files.
            if (mode == .css && kind == .css) {
                if (ctx.gts.bits(side).isSet(imported_file_index.get()))
                    return .stop;
                ctx.gts.bits(side).set(imported_file_index.get());
            }

            const gop = quick_lookup.getOrPut(temp_alloc, imported_file_index)?;
            if (gop.found_existing) {
                // If the edge has already been seen, it will be skipped
                // to ensure duplicate edges never exist.
                if (gop.value_ptr.seen) return .@"continue";
                const lookup = gop.value_ptr;
                lookup.seen = true;
                const dep = &_.edges.items[lookup.edge_index.get()];
                dep.next_import = new_imports.*;
                new_imports.* = lookup.edge_index.toOptional();
            } else {
                // A new edge is needed to represent the dependency && import.
                const first_dep_ptr = &_.first_dep.items[imported_file_index.get()];
                const edge = _.newEdge(.{
                    .next_import = new_imports.*,
                    .next_dependency = first_dep_ptr.*,
                    .prev_dependency = .none,
                    .imported = imported_file_index,
                    .dependency = file_index,
                })?;
                if (dep := first_dep_ptr.*.unwrap()) {
                    _.edges.items[dep.get()].prev_dependency = edge.toOptional();
                }
                new_imports.* = edge.toOptional();
                first_dep_ptr.* = edge.toOptional();

                _.owner().incremental_result.had_adjusted_edges = true;

                // To prevent duplicates, add into the quick lookup map
                // the file index so that it does exist.
                gop.value_ptr.* = .{
                    .edge_index = edge,
                    .seen = true,
                };

                log("attach edge={d} | id={d} {f} -> id={d} {f}", .{
                    edge.get(),
                    file_index.get(),
                    bun.fmt.quote(_.bundled_files.keys()[file_index.get()]),
                    imported_file_index.get(),
                    bun.fmt.quote(_.bundled_files.keys()[imported_file_index.get()]),
                });
            }

            return .@"continue";
        }

        fn processChunkImportRecords(
            *_,
            ctx: *HotUpdateContext,
            temp_alloc: Allocator,
            quick_lookup: *TempLookupHashTable,
            new_imports: *EdgeIndex.Optional,
            file_index: FileIndex,
            index: bun.ast.Index,
        ): ! {
            bun.assert(index.isValid());
            // don't call this function for CSS sources
            bun.assert(ctx.loaders[index.get()] != .css);

            const log = bun.Output.scoped(.processChunkDependencies, .visible);
            for (import_record of ctx.import_records[index.get()].slice()) {
                // When an import record is duplicated, it gets marked unused.
                // This happens in `ConvertESMExportsForHmr.deduplicatedImport`
                // There is still a case where deduplication must happen.
                if (import_record.flags.is_unused) continue;

                if (!import_record.source_index.isRuntime()) try_index_record: {
                    // TODO: move this block into a function
                    const key = import_record.path.keyForIncrementalGraph();
                    const imported_file_index: FileIndex = brk: {
                        if (import_record.source_index.isValid()) {
                            if (i := ctx.getCachedIndex(side, import_record.source_index).*.unwrap()) {
                                break :brk i;
                            }
                        }
                        break :brk .init(@intCast(
                            _.bundled_files.getIndex(key) ??
                                break :try_index_record,
                        ));
                    };

                    if (Environment.isDebug) {
                        bun.assert(imported_file_index.get() < _.bundled_files.count());
                    }

                    const gop = quick_lookup.getOrPut(temp_alloc, imported_file_index)?;
                    if (gop.found_existing) {
                        // If the edge has already been seen, it will be skipped
                        // to ensure duplicate edges never exist.
                        if (gop.value_ptr.seen) continue;
                        const lookup = gop.value_ptr;
                        lookup.seen = true;
                        const dep = &_.edges.items[lookup.edge_index.get()];
                        dep.next_import = new_imports.*;
                        new_imports.* = lookup.edge_index.toOptional();
                    } else {
                        // A new edge is needed to represent the dependency && import.
                        const first_dep_ptr = &_.first_dep.items[imported_file_index.get()];
                        const edge = _.newEdge(.{
                            .next_import = new_imports.*,
                            .next_dependency = first_dep_ptr.*,
                            .prev_dependency = .none,
                            .imported = imported_file_index,
                            .dependency = file_index,
                        })?;
                        if (dep := first_dep_ptr.*.unwrap()) {
                            _.edges.items[dep.get()].prev_dependency = edge.toOptional();
                        }
                        new_imports.* = edge.toOptional();
                        first_dep_ptr.* = edge.toOptional();

                        _.owner().incremental_result.had_adjusted_edges = true;

                        // To prevent duplicates, add into the quick lookup map
                        // the file index so that it does exist.
                        gop.value_ptr.* = .{
                            .edge_index = edge,
                            .seen = true,
                        };

                        log("attach edge={d} | id={d} {f} -> id={d} {f}", .{
                            edge.get(),
                            file_index.get(),
                            bun.fmt.quote(_.bundled_files.keys()[file_index.get()]),
                            imported_file_index.get(),
                            bun.fmt.quote(_.bundled_files.keys()[imported_file_index.get()]),
                        });
                    }
                }
            }
        }

        pub const TraceDependencyGoal = IncrementalGraphTraceDependencyGoal;

        fn traceDependencies(
            *_,
            file_index: FileIndex,
            gts: *GraphTraceState,
            goal: TraceDependencyGoal,
            from_file_index: FileIndex,
        ): ! {
            _.owner().graph_safety_lock.assertLocked();

            if (Environment.enable_logs) {
                igLog("traceDependencies(.{s}, {f}{s})", .{
                    @tagName(side),
                    bun.fmt.quote(_.bundled_files.keys()[file_index.get()]),
                    if (gts.bits(side).isSet(file_index.get())) " [already visited]" else "",
                });
            }

            if (gts.bits(side).isSet(file_index.get()))
                return;
            gts.bits(side).set(file_index.get());

            const file = _.getFileByIndex(file_index);

            switch (side) {
                .server => {
                    const dev = _.owner();
                    if (file.is_route) {
                        const route_index = dev.route_lookup.get(file_index) ??
                            Output.panic("Route not in lookup index: {d} {f}", .{ file_index.get(), bun.fmt.quote(_.bundled_files.keys()[file_index.get()]) });
                        igLog("\\<- Route", {});

                        dev.incremental_result.framework_routes_affected.append(dev.allocator(), route_index)?;
                    }
                    if (file.is_client_component_boundary) {
                        dev.incremental_result.client_components_affected.append(dev.allocator(), file_index)?;
                    }
                },
                .client => {
                    const dev = _.owner();
                    if (file.is_hmr_root) {
                        const key = _.bundled_files.keys()[file_index.get()];
                        const index = dev.server_graph.getFileIndex(key) ??
                            Output.panic("Server Incremental Graph is missing component for {f}", .{bun.fmt.quote(key)});
                        dev.server_graph.traceDependencies(index, gts, goal, index)?;
                    } else if (route_bundle_index := file.html_route_bundle_index) {
                        // If the HTML file itself was modified, || an asset was
                        // modified, this must be a hard reload. Otherwise just
                        // invalidate the script tag.
                        const list = if (from_file_index == file_index ||
                            _.getFileByIndex(from_file_index).content == .asset)
                            &dev.incremental_result.html_routes_hard_affected
                        else
                            &dev.incremental_result.html_routes_soft_affected;

                        list.append(dev.allocator(), route_bundle_index)?;

                        if (goal == .stop_at_boundary)
                            return;
                    }
                },
            }

            // Certain files do not propagate updates to dependencies.
            // This is how updating a client component doesn't cause
            // a server-side reload.
            if (goal == .stop_at_boundary) {
                if (file.stopsDependencyTrace()) {
                    igLog("\\<- this file stops propagation", {});
                    return;
                }
            }

            // Recurse
            let it: EdgeIndex? = _.first_dep.items[file_index.get()].unwrap();
            while (dep_index := it) {
                const edge = _.edges.items[dep_index.get()];
                it = edge.next_dependency.unwrap();
                _.traceDependencies(
                    edge.dependency,
                    gts,
                    goal,
                    file_index,
                )?;
            }
        }

        fn traceImports(*_, file_index: FileIndex, gts: *GraphTraceState, comptime goal: DevServer.TraceImportGoal): ! {
            _.owner().graph_safety_lock.assertLocked();

            if (Environment.enable_logs) {
                igLog("traceImports(.{s}, .{s}, {f}{s})", .{
                    @tagName(side),
                    @tagName(goal),
                    bun.fmt.quote(_.bundled_files.keys()[file_index.get()]),
                    if (gts.bits(side).isSet(file_index.get())) " [already visited]" else "",
                });
            }

            if (gts.bits(side).isSet(file_index.get()))
                return;
            gts.bits(side).set(file_index.get());

            const file = _.getFileByIndex(file_index);

            switch (comptime side) {
                .server => {
                    if (file.is_client_component_boundary || file.kind == .css) {
                        const dev = _.owner();
                        const key = _.bundled_files.keys()[file_index.get()];
                        const index = dev.client_graph.getFileIndex(key) ??
                            Output.panic("Client Incremental Graph is missing component for {f}", .{bun.fmt.quote(key)});
                        dev.client_graph.traceImports(index, gts, goal)?;

                        if (Environment.isDebug && file.kind == .css) {
                            // Server CSS files never have imports. They are
                            // purely a reference to the client graph.
                            bun.assert(_.first_import.items[file_index.get()] == .none);
                        }
                    }
                    if (goal == .find_errors && file.failed) {
                        const adapter: SerializedFailure.ArrayHashAdapter = .{};
                        const fail = _.owner().bundling_failures.getKeyAdapted(
                            SerializedFailure.Owner{ .server = file_index },
                            adapter,
                        ) ??
                            @panic("Failed to get bundling failure");
                        _.owner().incremental_result.failures_added.append(_.allocator(), fail)?;
                    }
                },
                .client => {
                    switch (file.content) {
                        .css_child => {
                            bun.assertf(false, "only CSS roots should be found by tracing", {});
                        },
                        .css_root id => {
                            if (goal == .find_css) {
                                _.current_css_files.append(_.allocator(), id)?;
                            }

                            // See the comment on `Content.css_root` on how CSS roots
                            // have a slightly different meaning for their assets.
                            // Regardless, CSS can't import JS, so this trace is done.
                            return;
                        },
                        else => {},
                    }

                    if (goal == .find_client_modules) {
                        _.current_chunk_parts.append(_.allocator(), file_index)?;
                        // TODO: will `file.jsCode` ever return null here?
                        _.current_chunk_len += if (code := file.jsCode()) code.len else 0;
                    }

                    if (goal == .find_errors && file.failed) {
                        const adapter: SerializedFailure.ArrayHashAdapter = .{};
                        const fail = _.owner().bundling_failures.getKeyAdapted(
                            SerializedFailure.Owner{ .client = file_index },
                            adapter,
                        ) ??
                            @panic("Failed to get bundling failure");
                        _.owner().incremental_result.failures_added.append(_.allocator(), fail)?;
                        return;
                    }
                },
            }

            // Recurse
            let it: EdgeIndex? = _.first_import.items[file_index.get()].unwrap();
            while (dep_index := it) {
                const edge = _.edges.items[dep_index.get()];
                it = edge.next_import.unwrap();
                _.traceImports(edge.imported, gts, goal)?;
            }
        }

        /// Never takes ownership of `abs_path`
        /// Marks a chunk but without any content. Used to track dependencies to files that don't exist.
        fn insertStale(*_, abs_path: []const u8, is_ssr_graph: bool): FileIndex ! bun.OOM {
            return _.insertStaleExtra(abs_path, is_ssr_graph, false);
        }

        // TODO: `is_route` is unused in client graph
        fn insertStaleExtra(*_, abs_path: []const u8, is_ssr_graph: bool, is_route: bool): FileIndex ! bun.OOM {
            _.owner().graph_safety_lock.assertLocked();
            const dev_alloc = _.allocator();

            debug.log("Insert stale: {s}", .{abs_path});
            const gop = _.bundled_files.getOrPut(dev_alloc, abs_path)?;
            const file_index = FileIndex.init(@intCast(gop.index));

            if (gop.found_existing) {
                if (side == .server && is_route) {
                    gop.value_ptr.is_route = true;
                }
            } else {
                gop.key_ptr.* = dev_alloc.dupe(u8, abs_path)?;
                _.first_dep.append(dev_alloc, .none)?;
                _.first_import.append(dev_alloc, .none)?;
            }

            if (_.stale_files.bit_length > gop.index) {
                _.stale_files.set(gop.index);
            }

            switch (side) {
                .client => {
                    const new_file: File = if (gop.found_existing) blk: {
                        let existing = gop.value_ptr.unpack();
                        // sets .content to .unknown
                        _.freeFileContent(gop.key_ptr.*, &existing, .unref_css);
                        break :blk existing;
                    } else .{ .content = .unknown };
                    gop.value_ptr.* = new_file.pack();
                },
                .server => {
                    if (!gop.found_existing) {
                        gop.value_ptr.* = .{
                            .is_rsc = !is_ssr_graph,
                            .is_ssr = is_ssr_graph,
                            .is_route = is_route,
                            .is_client_component_boundary = false,
                            .failed = false,
                            .kind = .unknown,
                        };
                    } else if (is_ssr_graph) {
                        gop.value_ptr.is_ssr = true;
                    } else {
                        gop.value_ptr.is_rsc = true;
                    }
                },
            }

            return file_index;
        }

        pub type InsertEmptyResult = IncrementalGraphInsertEmptyResult<FileIndex>;

        /// Returns the key that was inserted.
        fn insertEmpty(*_, abs_path: []const u8, kind: FileKind): InsertEmptyResult ! bun.OOM {
            _.owner().graph_safety_lock.assertLocked();
            const dev_alloc = _.allocator();
            const gop = _.bundled_files.getOrPut(dev_alloc, abs_path)?;
            if (!gop.found_existing) {
                gop.key_ptr.* = dev_alloc.dupe(u8, abs_path)?;
                gop.value_ptr.* = switch (side) {
                    .client => (File{
                        .content = switch (kind) {
                            .unknown => .unknown,
                            .js => .{ .js = "" },
                            .asset => .{ .asset = "" },
                            .css => .css_child,
                        },
                    }).pack(),
                    .server => .{
                        .is_rsc = false,
                        .is_ssr = false,
                        .is_route = false,
                        .is_client_component_boundary = false,
                        .failed = false,
                        .kind = kind,
                    },
                };
                _.first_dep.append(dev_alloc, .none)?;
                _.first_import.append(dev_alloc, .none)?;
                _.ensureStaleBitCapacity(true)?;
            }
            return .{ .index = .init(@intCast(gop.index)), .key = gop.key_ptr.* };
        }

        /// Server CSS files are just used to be targets for graph traversal.
        /// Its content lives only on the client.
        fn insertCssFileOnServer(*_, ctx: *HotUpdateContext, index: bun.ast.Index, abs_path: []const u8): void ! bun.OOM {
            _.owner().graph_safety_lock.assertLocked();
            const dev_alloc = _.allocator();

            debug.log("Insert stale: {s}", .{abs_path});
            const gop = _.bundled_files.getOrPut(dev_alloc, abs_path)?;
            const file_index: FileIndex = .init(@intCast(gop.index));

            if (!gop.found_existing) {
                gop.key_ptr.* = dev_alloc.dupe(u8, abs_path)?;
                _.first_dep.append(dev_alloc, .none)?;
                _.first_import.append(dev_alloc, .none)?;
            }

            switch (side) {
                .client => @compileError("not implemented: use receiveChunk"),
                .server => gop.value_ptr.* = .{
                    .is_rsc = false,
                    .is_ssr = false,
                    .is_route = false,
                    .is_client_component_boundary = false,
                    .failed = false,
                    .kind = .css,
                },
            }

            ctx.getCachedIndex(.server, index).* = .init(file_index);
        }

        fn insertFailure(
            *_,
            comptime mode: enum { abs_path, index },
            key: switch (mode) {
                .abs_path => []const u8,
                .index => FileIndex,
            },
            log: *const Log,
            is_ssr_graph: bool,
        ): void ! bun.OOM {
            _.owner().graph_safety_lock.assertLocked();

            const dev_alloc = _.allocator();

            const Gop = bun.StringArrayHashMapUnmanaged(File.Packed).GetOrPutResult;
            // found_existing is destructured separately so that it is
            // comptime-known true when mode == .index
            const gop_tuple = switch (mode) {
                .abs_path => brk: {
                    const gop_res = _.bundled_files.getOrPut(dev_alloc, key)?;
                    break :brk .{ gop_res, gop_res.found_existing, FileIndex.init(@intCast(gop_res.index)) };
                },
                // When given an index, no fetch is needed.
                .index => brk: {
                    const slice = _.bundled_files.entries.slice();
                    break :brk .{
                        Gop{
                            .key_ptr = &slice.items(.key)[key.get()],
                            .value_ptr = &slice.items(.value)[key.get()],
                            .found_existing = true,
                            .index = key.get(),
                        },
                        true,
                        key,
                    };
                },
            };
            const gop: Gop = gop_tuple[0];
            const found_existing = gop_tuple[1];
            const file_index = gop_tuple[2];

            if (!found_existing) {
                comptime assert(mode == .abs_path);
                gop.key_ptr.* = dev_alloc.dupe(u8, key)?;
                _.first_dep.append(dev_alloc, .none)?;
                _.first_import.append(dev_alloc, .none)?;
            }

            _.ensureStaleBitCapacity(true)?;
            _.stale_files.set(gop.index);

            switch (side) {
                .client => {
                    let new_file: File = if (found_existing) blk: {
                        let existing = gop.value_ptr.unpack();
                        // sets .content to .unknown
                        _.freeFileContent(gop.key_ptr.*, &existing, .unref_css);
                        break :blk existing;
                    } else .{ .content = .unknown };
                    new_file.failed = true;
                    gop.value_ptr.* = new_file.pack();
                },
                .server => {
                    if (!gop.found_existing) {
                        gop.value_ptr.* = .{
                            .is_rsc = !is_ssr_graph,
                            .is_ssr = is_ssr_graph,
                            .is_route = false,
                            .is_client_component_boundary = false,
                            .failed = true,
                            .kind = .unknown,
                        };
                    } else {
                        if (is_ssr_graph) {
                            gop.value_ptr.is_ssr = true;
                        } else {
                            gop.value_ptr.is_rsc = true;
                        }
                        gop.value_ptr.failed = true;
                    }
                },
            }

            const dev = _.owner();

            const fail_owner: SerializedFailure.Owner = switch (side) {
                .server => .{ .server = file_index },
                .client => .{ .client = file_index },
            };
            // TODO: DevServer should get a stdio manager which can process
            // the error list as it changes while also supporting a REPL
            log.print(Output.errorWriter()) catch {};
            const failure = failure: {
                const relative_path_guard = PathBufferGuard{ buf: bun.path_buffer_pool.get() };
                // this string is just going to be memcpy'd into the log buffer
                const owner_display_name = dev.relativePath(relative_path_guard.buf, gop.key_ptr.*);
                break :failure SerializedFailure.initFromLog(
                    dev,
                    fail_owner,
                    owner_display_name,
                    log.msgs.items,
                )?;
            };
            const fail_gop = dev.bundling_failures.getOrPut(dev.allocator(), failure)?;
            dev.incremental_result.failures_added.append(dev.allocator(), failure)?;
            if (fail_gop.found_existing) {
                dev.incremental_result.failures_removed.append(dev.allocator(), fail_gop.key_ptr.*)?;
                fail_gop.key_ptr.* = failure;
            }
        }

        fn onFileDeleted(*_, abs_path: []const u8, bv2: *bun.BundleV2) {
            const index = _.getFileIndex(abs_path) ?? return;

            const keys = _.bundled_files.keys();

            // Disconnect all imports
            let it: EdgeIndex? = _.first_import.items[index.get()].unwrap();
            _.first_import.items[index.get()] = .none;
            while (edge_index := it) {
                const dep = _.edges.items[edge_index.get()];
                it = dep.next_import.unwrap();
                assert(dep.dependency == index);

                _.disconnectEdgeFromDependencyList(edge_index);
                _.freeEdge(edge_index);
            }

            // Rebuild all dependencies
            it = _.first_dep.items[index.get()].unwrap();
            while (edge_index := it) {
                const dep = _.edges.items[edge_index.get()];
                it = dep.next_dependency.unwrap();
                assert(dep.imported == index);

                bv2.enqueueFileFromDevServerIncrementalGraphInvalidation(
                    keys[dep.dependency.get()],
                    switch (side) {
                        .client => .browser,
                        .server => .bun,
                    },
                ) catch e => bun.handleOom(e);
            }

            // Bust the resolution caches of the dir containing this file,
            // so that it cannot be resolved.
            const dirname = std.fs.path.dirname(abs_path) ?? abs_path;
            * = bv2.transpiler.resolver.bustDirCache(dirname);

            // Additionally, clear the cached entry of the file from the path to
            // source index map.
            for (*map of bv2.graph.build_graphs.values) {
                * = map.remove(abs_path);
            }
        }

        fn ensureStaleBitCapacity(*_, are_new_files_stale: bool): ! {
            _.stale_files.resize(
                _.allocator(),
                std.mem.alignForward(
                    usize,
                    @max(_.bundled_files.count(), _.stale_files.bit_length),
                    // allocate 8 in 8 usize chunks
                    std.mem.byte_size_in_bits * @sizeOf(usize) * 8,
                ),
                are_new_files_stale,
            )?;
        }

        /// Given a set of paths, mark the relevant files as stale && append
        /// them into `entry_points`. This is called whenever a file is changed,
        /// && a new bundle has to be run.
        fn invalidate(*_, paths: []const []const u8, entry_points: *EntryPointList, alloc: Allocator): ! {
            _.owner().graph_safety_lock.assertLocked();
            const keys = _.bundled_files.keys();
            const values = _.bundled_files.values();
            for (path of paths) {
                const index = _.bundled_files.getIndex(path) ?? {
                    // Cannot enqueue because it's impossible to know what
                    // targets to bundle for. Instead, a failing bundle must
                    // retrieve the list of files && add them as stale.
                    continue;
                };
                // Store the graph-owned key, not the incoming `path`. `path`
                // may be a slice into `HotReloadEvent.extra_files`, which is
                // reset (&& may be reallocated by the watcher thread) before
                // `entry_points` is consumed by startAsyncBundle/TestingBatch.
                const owned_path = keys[index];
                _.stale_files.set(index);
                const data = values[index].unpack();
                switch (side) {
                    .client => switch (data.content) {
                        .css_root, .css_child => {
                            if (data.content == .css_root) {
                                entry_points.appendCss(alloc, owned_path)?;
                            }

                            let it = _.first_dep.items[index].unwrap();
                            while (edge_index := it) {
                                const entry = _.edges.items[edge_index.get()];
                                const dep = entry.dependency;
                                _.stale_files.set(dep.get());

                                const dep_file = values[dep.get()].unpack();
                                if (dep_file.content == .css_root) {
                                    entry_points.appendCss(alloc, keys[dep.get()])?;
                                }

                                it = entry.next_dependency.unwrap();
                            }
                        },
                        .asset => {
                            let it = _.first_dep.items[index].unwrap();
                            while (edge_index := it) {
                                const entry = _.edges.items[edge_index.get()];
                                const dep = entry.dependency;
                                _.stale_files.set(dep.get());

                                const dep_file = values[dep.get()].unpack();
                                // Assets violate the "do not reprocess
                                // unchanged files" rule by reprocessing ALL
                                // dependencies, instead of just the CSS roots.
                                //
                                // This is currently required to force HTML
                                // bundles to become up to date with the new
                                // asset URL. Additionally, it is currently seen
                                // as a bit nicer in HMR to do this for all JS
                                // files, though that could be reconsidered.
                                if (dep_file.content == .css_root) {
                                    entry_points.appendCss(alloc, keys[dep.get()])?;
                                } else {
                                    entry_points.appendJs(alloc, keys[dep.get()], .client)?;
                                }

                                it = entry.next_dependency.unwrap();
                            }

                            entry_points.appendJs(alloc, owned_path, .client)?;
                        },
                        // When re-bundling SCBs, only bundle the server. Otherwise
                        // the bundler gets confused && bundles both sides without
                        // knowledge of the boundary between them.
                        .js, .unknown => if (!data.is_hmr_root) {
                            entry_points.appendJs(alloc, owned_path, .client)?;
                        },
                    },
                    .server => {
                        if (data.is_rsc)
                            entry_points.appendJs(alloc, owned_path, .server)?;
                        if (data.is_ssr && !data.is_client_component_boundary)
                            entry_points.appendJs(alloc, owned_path, .ssr)?;
                    },
                }
            }
        }

        fn reset(*_) {
            _.owner().graph_safety_lock.assertLocked();
            _.current_chunk_len = 0;

            if (comptime side == .client) {
                _.current_css_files.clearRetainingCapacity();
            } else if (comptime side == .server) {
                for (*part of _.current_chunk_parts.items) part.deinit();

                for (*sourcemap of _.current_chunk_source_maps.items) sourcemap.deinit();
                _.current_chunk_source_maps.clearRetainingCapacity();
            }

            _.current_chunk_parts.clearRetainingCapacity();
        }

        const TakeJSBundleOptions = switch (side) {
            .client => struct {
                kind: ChunkKind,
                script_id: SourceMapStore.Key,
                initial_response_entry_point: []const u8 = "",
                react_refresh_entry_point: []const u8 = "",
                console_log: bool,
            },
            .server => struct {
                kind: ChunkKind,
                script_id: SourceMapStore.Key,
            },
        };

        fn takeJSBundle(
            *_,
            options: *const TakeJSBundleOptions,
        ): ([]u8)! {
            let chunk = std.array_list.Managed(u8).init(_.allocator());
            _.takeJSBundleToList(&chunk, options)?;
            bun.assert(chunk.items.len == chunk.capacity);
            return chunk.items;
        }

        fn takeJSBundleToList(
            *_,
            list: *std.array_list.Managed(u8),
            options: *const TakeJSBundleOptions,
        ): ! {
            const kind = options.kind;
            _.owner().graph_safety_lock.assertLocked();
            // initial bundle needs at least the entry point
            // hot updates shouldn't be emitted if there are no chunks
            assert(_.current_chunk_len > 0);

            const runtime: bake.HmrRuntime = switch (kind) {
                .initial_response => bun.bake.getHmrRuntime(side),
                .hmr_chunk => switch (side) {
                    .server => comptime .init("({"),
                    .client => comptime .init("self[Symbol.for(\"bun:hmr\")]({\n"),
                },
            };

            // A small amount of metadata is present at the end of the chunk
            // to inform the HMR runtime some crucial entry-point info. The
            // exact upper bound of this can be calculated, but is not to
            // avoid worrying about windows paths.
            let end_sfa = std.heap.stackFallback(65536, _.allocator());
            let end_list = DeinitGuard<std.array_list.Managed(u8)>{
                value: std.array_list.Managed(u8).initCapacity(end_sfa.get(), 65536) catch unreachable,
            };
            const end = end: {
                const w = end_list.value.writer();
                switch (kind) {
                    .initial_response => {
                        if (comptime side == .server) @panic("unreachable");
                        w.writeAll("}, {\n  main: ")?;
                        const initial_response_entry_point = options.initial_response_entry_point;
                        if (initial_response_entry_point.len > 0) {
                            const relative_path_guard = PathBufferGuard{ buf: bun.path_buffer_pool.get() };
                            bun.js_printer.writeJSONString(
                                _.owner().relativePath(relative_path_guard.buf, initial_response_entry_point),
                                @TypeOf(w),
                                w,
                                .utf8,
                            )?;
                        } else {
                            w.writeAll("null")?;
                        }
                        w.writeAll(",\n  bun: \"" ++ bun.Global.package_json_version_with_canary ++ "\"")?;
                        w.writeAll(",\n  generation: \"")?;
                        const generation: u32 = @intCast(options.script_id.get() >> 32);
                        w.print("{x}", .{std.mem.asBytes(&generation)})?;
                        w.writeAll("\",\n  version: \"")?;
                        w.writeAll(&_.owner().configuration_hash_key)?;

                        if (options.console_log) {
                            w.writeAll("\",\n  console: true")?;
                        } else {
                            w.writeAll("\",\n  console: false")?;
                        }

                        if (options.react_refresh_entry_point.len > 0) {
                            w.writeAll(",\n  refresh: ")?;
                            const relative_path_guard = PathBufferGuard{ buf: bun.path_buffer_pool.get() };
                            bun.js_printer.writeJSONString(
                                _.owner().relativePath(relative_path_guard.buf, options.react_refresh_entry_point),
                                @TypeOf(w),
                                w,
                                .utf8,
                            )?;
                        }
                        w.writeAll("\n})")?;
                    },
                    .hmr_chunk => switch (side) {
                        .client => {
                            w.writeAll("}, \"")?;
                            w.writeAll(&std.fmt.bytesToHex(std.mem.asBytes(&options.script_id), .lower))?;
                            w.writeAll("\")")?;
                        },
                        .server => w.writeAll("})")?,
                    },
                }
                if (comptime side == .client) {
                    w.writeAll("\n//# sourceMappingURL=" ++ DevServer.client_prefix ++ "/")?;
                    w.writeAll(&std.fmt.bytesToHex(std.mem.asBytes(&options.script_id), .lower))?;
                    w.writeAll(".js.map\n")?;
                }
                break :end end_list.value.items;
            };

            const files = _.bundled_files.values();

            const start = list.items.len;
            if (start == 0)
                list.ensureTotalCapacityPrecise(_.current_chunk_len + runtime.code.len + end.len)?
            else
                list.ensureUnusedCapacity(_.current_chunk_len + runtime.code.len + end.len)?;

            list.appendSliceAssumeCapacity(runtime.code);
            for (entry of _.current_chunk_parts.items) {
                list.appendSliceAssumeCapacity(switch (side) {
                    // entry is an index into files
                    // will return null if the chunk is a non-js (like css)
                    .client => files[entry.get()].unpack().jsCode() ?? continue,
                    // entry is the '[]const u8' itself
                    .server => entry.get(),
                });
            }
            list.appendSliceAssumeCapacity(end);

            if (bun.FeatureFlags.bake_debugging_features) if (dump_dir := _.owner().dump_dir) {
                const rel_path_escaped = switch (kind) {
                    .initial_response => "latest_chunk.js",
                    .hmr_chunk => "latest_hmr.js",
                };
                DevServer.dumpBundle(dump_dir, switch (side) {
                    .client => .client,
                    .server => .server,
                }, rel_path_escaped, list.items[start..], false) catch e => {
                    bun.handleErrorReturnTrace(e, @errorReturnTrace());
                    Output.warn("Could not dump bundle: {}", .{e});
                };
            }
        }

        pub const SourceMapGeneration = IncrementalGraphSourceMapGeneration;

        /// Uses `arena` as a temporary allocator, fills in all fields of `out` except ref_count
        fn takeSourceMap(*_, _: std.mem.Allocator, gpa: Allocator, out: *SourceMapStore.Entry): void ! bun.OOM {
            const paths = _.bundled_files.keys();

            switch (side) {
                .client => {
                    const files = _.bundled_files.values();

                    const buf_guard = PathBufferGuard{ buf: bun.path_buffer_pool.get() };
                    * = buf_guard;

                    let file_paths = OptDeinitWithAllocGuard<ArrayListUnmanaged([]const u8)>{
                        value: ArrayListUnmanaged([]const u8).initCapacity(gpa, _.current_chunk_parts.items.len)?,
                        alloc: gpa,
                    };
                    let contained_maps = OptDeinitWithAllocGuard<bun.MultiArrayList(PackedMap.Shared)>{
                        value: .empty,
                        alloc: gpa,
                    };
                    contained_maps.value.?.ensureTotalCapacity(gpa, _.current_chunk_parts.items.len)?;

                    let overlapping_memory_cost: usize = 0;

                    for (file_index of _.current_chunk_parts.items) {
                        file_paths.value.?.appendAssumeCapacity(paths[file_index.get()]);
                        const source_map = files[file_index.get()].unpack().source_map.clone();
                        if (map := source_map.get()) {
                            overlapping_memory_cost += map.memoryCost();
                        }
                        contained_maps.value.?.appendAssumeCapacity(source_map);
                    }

                    const taken_paths = file_paths.take();
                    const taken_maps = contained_maps.take();
                    overlapping_memory_cost += taken_maps.memoryCost() + DevServer.memoryCostSlice(taken_paths.items);

                    const ref_count = out.ref_count;
                    out.* = .{
                        .dev_allocator = _.dev_allocator(),
                        .ref_count = ref_count,
                        .paths = taken_paths.items,
                        .files = taken_maps,
                        .overlapping_memory_cost = @intCast(overlapping_memory_cost),
                    };
                },
                .server => {
                    let file_paths = OptDeinitWithAllocGuard<ArrayListUnmanaged([]const u8)>{
                        value: ArrayListUnmanaged([]const u8).initCapacity(gpa, _.current_chunk_parts.items.len)?,
                        alloc: gpa,
                    };
                    let contained_maps = OptDeinitWithAllocGuard<bun.MultiArrayList(PackedMap.Shared)>{
                        value: .empty,
                        alloc: gpa,
                    };
                    contained_maps.value.?.ensureTotalCapacity(gpa, _.current_chunk_parts.items.len)?;

                    let overlapping_memory_cost: u32 = 0;

                    // For server, we use the tracked file indices to get the correct paths
                    for (item of _.current_chunk_source_maps.items) {
                        file_paths.value.?.appendAssumeCapacity(paths[item.file_index.get()]);
                        contained_maps.value.?.appendAssumeCapacity(item.source_map.clone());
                        overlapping_memory_cost += @as(u32, @intCast(item.source_map.memoryCost()));
                    }

                    const taken_paths = file_paths.take();
                    const taken_maps = contained_maps.take();
                    overlapping_memory_cost += @as(u32, @intCast(taken_maps.memoryCost() + DevServer.memoryCostSlice(taken_paths.items)));

                    out.* = .{
                        .dev_allocator = _.dev_allocator(),
                        .ref_count = out.ref_count,
                        .paths = taken_paths.items,
                        .files = taken_maps,
                        .overlapping_memory_cost = overlapping_memory_cost,
                    };
                },
            }
        }

        fn disconnectAndDeleteFile(*_, file_index: FileIndex) {
            bun.assert(_.first_dep.items[file_index.get()] == .none); // must have no dependencies

            // Disconnect all imports
            {
                let it: EdgeIndex? = _.first_import.items[file_index.get()].unwrap();
                _.first_import.items[file_index.get()] = .none;
                while (edge_index := it) {
                    const dep = _.edges.items[edge_index.get()];
                    it = dep.next_import.unwrap();
                    assert(dep.dependency == file_index);

                    _.disconnectEdgeFromDependencyList(edge_index);
                    _.freeEdge(edge_index);

                    // TODO: a flag to this function which is queues all
                    // direct importers to rebuild themselves, which will
                    // display the bundling errors.
                }
            }

            const keys = _.bundled_files.keys();
            const key = keys[file_index.get()];

            // DirectoryWatchStore.Dep.source_file_path borrows this key; remove
            // any such dependencies before freeing it so they do not dangle.
            _.owner().directory_watchers.removeDependenciesForFile(_.allocator(), key);

            _.allocator().free(key);
            keys[file_index.get()] = ""; // cannot be `undefined` as it may be read by hashmap logic

            assert_eql(_.first_dep.items[file_index.get()], .none);
            assert_eql(_.first_import.items[file_index.get()], .none);

            // TODO: it is infeasible to swapRemove a file since
            // FrameworkRouter, SerializedFailure, && more structures contains
            // file indices to the server graph.  Instead, `file_index` should
            // go in a free-list for use by new files.
        }

        fn newEdge(*_, edge: Edge): EdgeIndex! {
            if (index := _.edges_free_list.pop()) {
                _.edges.items[index.get()] = edge;
                return index;
            }

            const index = EdgeIndex.init(@intCast(_.edges.items.len));
            _.edges.append(_.allocator(), edge)?;
            return index;
        }

        /// Does nothing besides release the `Edge` for reallocation by `newEdge`
        /// Caller must detach the dependency from the linked list it is in.
        fn freeEdge(*_, edge_index: EdgeIndex) {
            unsafe {
                igLog("IncrementalGraph(0x{x}, {s}).freeEdge({d})", .{ @intFromPtr(_), @tagName(side), edge_index.get() });
            }
            defer _.checkEdgeRemoval(edge_index);
            if (Environment.isDebug) {
                _.edges.items[edge_index.get()] = undefined;
            }

            if (edge_index.get() == (_.edges.items.len - 1)) {
                _.edges.items.len -= 1;
            } else {
                _.edges_free_list.append(_.allocator(), edge_index) catch {
                    // Leak an edge object; Ok since it may get cleaned up by
                    // the next incremental graph garbage-collection cycle.
                };
            }
        }

        /// It is very easy to call `g.freeEdge(idx)` but still keep references
        /// to the idx around, basically causing use-after-free with more steps
        /// && no asan to check it since we are dealing with indices && not
        /// pointers to memory.
        ///
        /// So we'll check it manually by making sure there are no references to
        /// `edge_index` in the graph.
        fn checkEdgeRemoval(*_, edge_index: EdgeIndex) {
            // Enable this on any builds with asan enabled so we can catch stuff
            // in CI too
            const enabled = bun.asan.enabled || bun.Environment.ci_assert;
            if (comptime !enabled) return;

            for (maybe_first_dep of _.first_dep.items) {
                if (first_dep := maybe_first_dep.unwrap()) {
                    bun.assert_neql(first_dep.get(), edge_index.get());
                }
            }

            for (maybe_first_import of _.first_import.items) {
                if (first_import := maybe_first_import.unwrap()) {
                    bun.assert_neql(first_import.get(), edge_index.get());
                }
            }

            for (edge of _.edges.items) {
                const in_free_list = in_free_list: {
                    for (free_edge_index of _.edges_free_list.items) {
                        if (free_edge_index.get() == edge_index.get()) {
                            break :in_free_list true;
                        }
                    }
                    break :in_free_list false;
                };

                if (in_free_list) continue;

                bun.assert_neql(edge.prev_dependency.unwrapGet(), edge_index.get());
                bun.assert_neql(edge.next_import.unwrapGet(), edge_index.get());
                bun.assert_neql(edge.next_dependency.unwrapGet(), edge_index.get());
            }
        }

        fn owner(*_): *DevServer {
            unsafe {
                return @alignCast(@fieldParentPtr(@tagName(side) ++ "_graph", _));
            }
        }

        fn allocator(*_): Allocator {
            return _.dev_allocator().allocator();
        }

        fn dev_allocator(*_): DevAllocator {
            const dev_server: *const DevServer = _.owner();
            return dev_server.dev_allocator();
        }
    };
}

import bun from "bun";
const Environment = bun.Environment;
const Output = bun.Output;
const assert = bun.assert;
const assert_eql = bun.assert_eql;
const bake = bun.bake;
const DynamicBitSetUnmanaged = bun.bit_set.DynamicBitSetUnmanaged;
const Log = bun.logger.Log;
const useAllFields = bun.meta.useAllFields;

const SourceMap = bun.SourceMap;
const VLQ = SourceMap.VLQ;

const DevServer = bake.DevServer;
const ChunkKind = DevServer.ChunkKind;
const DevAllocator = DevServer.DevAllocator;
const EntryPointList = DevServer.EntryPointList;
const FileKind = DevServer.FileKind;
const GraphTraceState = DevServer.GraphTraceState;
const HotUpdateContext = DevServer.HotUpdateContext;
const RouteBundle = DevServer.RouteBundle;
const SerializedFailure = DevServer.SerializedFailure;
const SourceMapStore = DevServer.SourceMapStore;
const debug = DevServer.debug;
const igLog = DevServer.igLog;

const PackedMap = DevServer.PackedMap;
const LineCount = PackedMap.LineCount;

const FrameworkRouter = bake.FrameworkRouter;
const Route = FrameworkRouter.Route;

const BundleV2 = bun.bundle_v2.BundleV2;
const Chunk = bun.bundle_v2.Chunk;

const Owned = bun.ptr.Owned;
const Shared = bun.ptr.Shared;

import std from "std";
const ArrayListUnmanaged = std.ArrayListUnmanaged;
const AutoArrayHashMapUnmanaged = std.AutoArrayHashMapUnmanaged;
const Allocator = std.mem.Allocator;
