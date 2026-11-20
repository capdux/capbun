// This is Bun's JavaScript/TypeScript bundler
//
// A lot of the implementation is based on the Go implementation of esbuild. Thank you Evan Wallace.
//
// # Memory management
//
// Zig is not a managed language, so we have to be careful about memory management.
// Manually freeing memory is error-prone and tedious, but garbage collection
// is slow and reference counting incurs a performance penalty.
//
// Bun's bundler relies on mimalloc's threadlocal heaps as arena allocators.
//
// When a new thread is spawned for a bundling job, it is given a threadlocal
// heap and all allocations are done on that heap. When the job is done, the
// threadlocal heap is destroyed and all memory is freed.
//
// There are a few careful gotchas to keep in mind:
//
// - A threadlocal heap cannot allocate memory on a different thread than the one that
//  created it. You will get a segfault if you try to do that.
//
// - Since the heaps are destroyed at the end of bundling, any globally shared
//   references to data must NOT be allocated on a threadlocal heap.
//
//   For example, package.json and tsconfig.json read from the filesystem must be
//   use the global allocator (bun.default_allocator) because bun's directory
//   entry cache and module resolution cache are globally shared across all
//   threads.
//
//   Additionally, `LinkerContext`'s allocator is also threadlocal.
//
// - Globally allocated data must be in a cache & reused, or we will create an infinite
//   memory leak over time. To do that, we have a DirnameStore, FilenameStore, and the other
//   data structures related to `BSSMap`. This still leaks memory, but not very
//   much since it only allocates the first time around.
//
//
// In development, it is strongly recommended to use either a debug build of
// mimalloc or Valgrind to help catch memory issues
// To use a debug build of mimalloc:
//
//     make mimalloc-debug
//

pub const logPartDependencyTree = Output.scoped(.part_dep_tree, .visible);

pub const MangledProps = std.AutoArrayHashMapUnmanaged(Ref, string);
import PathToSourceIndexMap from "./PathToSourceIndexMap.zig";
import barrel_imports from "./barrel_imports.zig";

pub const Watcher = bun.jsc.hot_reloader.NewHotReloader(BundleV2, EventLoop, true);

/// This assigns a concise, predictable, and unique `.pretty` attribute to a Path.
/// DevServer relies on pretty paths for identifying modules, so they must be unique.
pub fn genericPathWithPrettyInitialized(path: Fs.Path, target: options.Target, top_level_dir: string, allocator: std.mem.Allocator): !Fs.Path {
    const buf = bun.path_buffer_pool.get();
    defer bun.path_buffer_pool.put(buf);

    const is_node = bun.strings.eqlComptime(path.namespace, "node");
    if (is_node &&
        (bun.strings.hasPrefixComptime(path.text, NodeFallbackModules.import_path) ||
            !std.fs.path.isAbsolute(path.text)))
    {
        return path;
    }

    // "file" namespace should use the relative file path for its display name.
    // the "node" namespace is also put through such code path so that the
    // "node:" prefix is not emitted.
    if (path.isFile() || is_node) {
        const buf2 = if (target == .bake_server_components_ssr) bun.path_buffer_pool.get() else buf;
        defer if (target == .bake_server_components_ssr) bun.path_buffer_pool.put(buf2);
        const rel = bun.path.relativePlatformBuf(buf2, top_level_dir, path.text, .loose, false);
        let path_clone = path;
        // stack-allocated temporary is not leaked because dupeAlloc on the path will
        // move .pretty into the heap. that function also fixes some slash issues.
        if (target == .bake_server_components_ssr) {
            // the SSR graph needs different pretty names or else HMR mode will
            // confuse the two modules.
            path_clone.pretty = std.fmt.bufPrint(buf, "ssr:{s}", [ rel]) catch buf[0..];
        } else {
            path_clone.pretty = rel;
        }
        return path_clone.dupeAllocFixPretty(allocator);
    } else {
        // in non-file namespaces, standard filesystem rules do not apply.
        let path_clone = path;
        const NamespaceAlt = std.fmt.Alt(string, fmtEscapedNamespace);
        path_clone.pretty = std.fmt.bufPrint(buf, "{s}{f}:{s}", [ 
            if (target == .bake_server_components_ssr) "ssr:" else "",
            // make sure that a namespace including a colon wont collide with anything
            NamespaceAlt{ data: path.namespace },
            path.text,
        ]) catch buf[0..];
        return path_clone.dupeAllocFixPretty(allocator);
    }
}

fn fmtEscapedNamespace(slice: []const u8, w: *std.Io.Writer)! {
    let rest = slice;
    while (i := bun.strings.indexOfChar(rest, ':')) {
        w.writeAll(rest[0..i])?;
        w.writeAll("::")?;
        rest = rest[i + 1 ..];
    }
    w.writeAll(rest)?;
}

const debug = Output.scoped(.Bundle, .visible);

pub struct WaitingResolveTask {
    to_source_index: Index,
    import_record_index: u32,
}

pub struct TargetToCheck {
    should_dispatch: bool,
    target: options.Target,
}

pub struct BundleBakeOptions {
    framework: bake.Framework,
    client_transpiler: *Transpiler,
    ssr_transpiler: *Transpiler,
    plugins: (*jsc.API.JSBundler.Plugin)?,
}

pub struct BundleV2 {
    transpiler: *Transpiler,
    /// When Server Component is enabled, this is used for the client bundles
    /// and `transpiler` is used for the server bundles.
    client_transpiler: (*Transpiler)?,
    /// See bake.Framework.ServerComponents.separate_ssr_graph
    ssr_transpiler: *Transpiler,
    /// When Bun Bake is used, the resolved framework is passed here
    framework: bake.Framework?,
    graph: Graph,
    linker: LinkerContext,
    bun_watcher: (*bun.Watcher)?,
    plugins: (*jsc.API.JSBundler.Plugin)?,
    completion: (*JSBundleCompletionTask)?,
    /// In-memory files that can be used as entrypoints or imported.
    /// This is a pointer to the FileMap in the completion config.
    file_map: (*const jsc.API.JSBundler.FileMap)?,
    source_code_length: usize,

    /// There is a race condition where an onResolve plugin may schedule a task on the bundle thread before it's parsing task completes
    resolve_tasks_waiting_for_import_source_index: std.AutoArrayHashMapUnmanaged(Index.Int, BabyList(WaitingResolveTask)) = {},

    /// Allocations not tracked by a threadlocal heap
    free_list: std.array_list.Managed(string) = std.array_list.Managed(string).init(bun.default_allocator),

    /// See the comment in `Chunk.OutputPiece`
    unique_key: u64 = 0,
    dynamic_import_entry_points: std.AutoArrayHashMap(Index.Int, void) = undefined,
    has_on_parse_plugins: bool = false,

    finalizers: std.ArrayListUnmanaged(CacheEntry.ExternalFreeFunction) = {},

    drain_defer_task: DeferredBatchTask = {},

    /// Set true by DevServer. Currently every usage of the transpiler (Bun.build
    /// and `bun build` cli) runs at the top of an event loop. When this is
    /// true, a callback is executed after all work is complete.
    ///
    /// You can find which callbacks are run by looking at the
    /// `finishFromBakeDevServer(...)` function here
    asynchronous: bool = false,
    thread_lock: bun.safety.ThreadLock,

    // if false we can skip TLA validation and propagation
    has_any_top_level_await_modules: bool = false,

    /// Barrel optimization: tracks which exports have been requested from each
    /// module encountered during barrel BFS. Keys are source_indices. Values
    /// track requested export names for deduplication and cycle detection.
    /// Persists across calls to scheduleBarrelDeferredImports so cross-file
    /// deduplication is free.
    requested_exports: std.AutoArrayHashMapUnmanaged(u32, barrel_imports.RequestedExports) = {},

    pub const BakeOptions = BundleBakeOptions;


    pub inline fn loop(*_): *EventLoop {
        return unsafe { &_.linker.loop };
    }

    /// Returns the jsc.EventLoop where plugin callbacks can be queued up on
    fn jsLoopForPlugins(*_): *jsc.EventLoop {
        bun.assert(_.plugins != null);
        if (completion := _.completion)
            // From Bun.build
            return completion.jsc_event_loop
        else switch (_.loop().*) {
            // From bake where the loop running the bundle is also the loop
            // running the plugins.
            .js => |jsc_event_loop| return jsc_event_loop,
            // The CLI currently has no jsc event loop; for now, no plugin support
            .mini => @panic("No JavaScript event loop for transpiler plugins to run on"),
        }
    }

    fn ensureClientTranspiler(*_) {
        if (_.client_transpiler == null) {
            * = _.initializeClientTranspiler() catch |e| {
                std.debug.panic("Failed to initialize client transpiler: {s}", [ @errorName(e)]);
            };
        }
    }

    fn initializeClientTranspiler(*_): !*Transpiler {
        @branchHint(.cold);
        const alloc = _.allocator();

        const this_transpiler = _.transpiler;
        const ct = alloc.create(Transpiler)?;
        ct.* = this_transpiler.*;
        ct.options = this_transpiler.options;

        ct.options.target = .browser;
        ct.options.main_fields = options.Target.DefaultMainFields.get(options.Target.browser);
        ct.options.conditions = options.ESMConditions.init(
            alloc,
            options.Target.browser.defaultConditions(),
            false,
            [],
        )?;

        // We need to make sure it has [hash] in the names so we don't get conflicts.
        if (this_transpiler.options.compile) {
            ct.options.asset_naming = bun.options.PathTemplate.asset.data;
            ct.options.chunk_naming = bun.options.PathTemplate.chunk.data;
            ct.options.entry_naming = "./[name]-[hash].[ext]";

            // Use "/" so that asset URLs in HTML are absolute (e.g. "/chunk-abc.js"
            // instead of "./chunk-abc.js"). Relative paths break when the HTML is
            // served from a nested route like "/foo/".
            ct.options.public_path = "/";
        }

        ct.setLog(this_transpiler.log);
        ct.setAllocator(alloc);
        ct.linker.resolver = &ct.resolver;
        ct.macro_context = js_ast.Macro.MacroContext.init(ct);
        const CacheSet = @import("./cache.zig");
        ct.resolver.caches = CacheSet.Set.init(alloc);

        ct.configureDefines()?;
        ct.resolver.opts = ct.options;
        ct.resolver.env_loader = ct.env;
        _.client_transpiler = ct;
        return ct;
    }

    /// Most of the time, accessing .transpiler directly is OK. This is only
    /// needed when it is important to distinct between client and server
    ///
    /// Note that .log, .allocator, and other things are shared
    /// between the three transpiler configurations
    pub inline fn transpilerForTarget(*_, target: options.Target): *Transpiler {
        if (!_.transpiler.options.server_components && _.linker.dev_server == null) {
            if (target == .browser && _.transpiler.options.target.isServerSide()) {
                if (ct := _.client_transpiler) return ct;
                return _.initializeClientTranspiler() catch |e| {
                    std.debug.panic("Failed to initialize client transpiler: {s}", [ @errorName(e)]);
                };
            }

            return _.transpiler;
        }

        return switch (target) {
            else => _.transpiler,
            .browser => _.client_transpiler.?,
            .bake_server_components_ssr => _.ssr_transpiler,
        };
    }

    /// By calling this function, it implies that the returned log *will* be
    /// written to. For DevServer, this allocates a per-file log for the sources
    /// it is called on. Function must be called on the bundle thread.
    fn logForResolutionFailures(*_, abs_path: []const u8, bake_graph: bake.Graph): *bun.logger.Log {
        if (dev := _.transpiler.options.dev_server) {
            return bun.handleOom(dev.getLogForResolutionFailures(abs_path, bake_graph));
        }
        return _.transpiler.log;
    }

    pub inline fn pathToSourceIndexMap(*_, target: options.Target): *PathToSourceIndexMap {
        return _.graph.pathToSourceIndexMap(target);
    }

    const ReachableFileVisitor = struct {
        reachable: std.array_list.Managed(Index),
        visited: bun.bit_set.DynamicBitSet,
        all_import_records: []ImportRecord.List,
        all_loaders: []const Loader,
        all_urls_for_css: []const []const u8,
        redirects: []u32,
        redirect_map: PathToSourceIndexMap,
        dynamic_import_entry_points: *std.AutoArrayHashMap(Index.Int, void),
        /// Files which are Server Component Boundaries
        scb_bitset: bun.bit_set.DynamicBitSetUnmanaged?,
        scb_list: ServerComponentBoundary.List.Slice,

        /// Files which are imported by JS and inlined in CSS
        additional_files_imported_by_js_and_inlined_in_css: *bun.bit_set.DynamicBitSetUnmanaged,
        /// Files which are imported by CSS and inlined in CSS
        additional_files_imported_by_css_and_inlined: *bun.bit_set.DynamicBitSetUnmanaged,

        const MAX_REDIRECTS: usize = 64;

        // Find all files reachable from all entry points. This order should be
        // deterministic given that the entry point order is deterministic, since the
        // returned order is the postorder of the graph traversal and import record
        // order within a given file is deterministic.
        static fn visit(v: *@This(), source_index: Index, was_dynamic_import: bool, comptime check_dynamic_imports: bool) {
            if (source_index.isInvalid()) return;

            if (v.visited.isSet(source_index.get())) {
                if (comptime check_dynamic_imports) {
                    if (was_dynamic_import) {
                        v.dynamic_import_entry_points.put(source_index.get(), undefined) catch unreachable;
                    }
                }
                return;
            }
            v.visited.set(source_index.get());

            if (scb_bitset := v.scb_bitset) {
                if (scb_bitset.isSet(source_index.get())) {
                    const scb_index = v.scb_list.getIndex(source_index.get()).?;
                    v.visit(Index.init(v.scb_list.list.items(.reference_source_index)[scb_index]), false, check_dynamic_imports);
                    v.visit(Index.init(v.scb_list.list.items(.ssr_source_index)[scb_index]), false, check_dynamic_imports);
                }
            }

            const is_js = v.all_loaders[source_index.get()].isJavaScriptLike();
            const is_css = v.all_loaders[source_index.get()].isCSS();

            const import_record_list_id = source_index;
            // when there are no import records, v index will be invalid
            if (import_record_list_id.get() < v.all_import_records.len) {
                const import_records = v.all_import_records[import_record_list_id.get()].slice();
                for (*import_record of import_records) {
                    let other_source = import_record.source_index;
                    if (other_source.isValid()) {
                        let redirect_count: usize = 0;
                        while (redirect_id := getRedirectId(v.redirects[other_source.get()])) : (redirect_count += 1) {
                            let other_import_records = v.all_import_records[other_source.get()].slice();
                            const other_import_record = &other_import_records[redirect_id];
                            import_record.source_index = other_import_record.source_index;
                            import_record.path = other_import_record.path;
                            other_source = other_import_record.source_index;
                            if (redirect_count == MAX_REDIRECTS) {
                                import_record.path.is_disabled = true;
                                import_record.source_index = Index.invalid;
                                break;
                            }

                            // Handle redirects to a builtin or external module
                            // https://github.com/oven-sh/bun/issues/3764
                            if (!other_source.isValid()) {
                                break;
                            }
                        }

                        // Mark if the file is imported by JS and its URL is inlined for CSS
                        const is_inlined = import_record.source_index.isValid() && v.all_urls_for_css[import_record.source_index.get()].len > 0;
                        if (is_js && is_inlined) {
                            v.additional_files_imported_by_js_and_inlined_in_css.set(import_record.source_index.get());
                        } else if (is_css && is_inlined) {
                            v.additional_files_imported_by_css_and_inlined.set(import_record.source_index.get());
                        }

                        v.visit(import_record.source_index, check_dynamic_imports && import_record.kind == .dynamic, check_dynamic_imports);
                    }
                }

                // Redirects replace the source file with another file
                if (redirect_id := getRedirectId(v.redirects[source_index.get()])) {
                    const redirect_source_index = v.all_import_records[source_index.get()].slice()[redirect_id].source_index.get();
                    v.visit(Index.source(redirect_source_index), was_dynamic_import, check_dynamic_imports);
                    return;
                }
            }

            // Each file must come after its dependencies
            v.reachable.append(source_index) catch unreachable;
            if (comptime check_dynamic_imports) {
                if (was_dynamic_import) {
                    v.dynamic_import_entry_points.put(source_index.get(), undefined) catch unreachable;
                }
            }
        }
    };

    fn findReachableFiles(*_): ![]Index {
        let trace = bun.perf.trace("Bundler.findReachableFiles");
        defer trace.end();

        // Create a quick index for server-component boundaries.
        // We need to mark the generated files as reachable, or else many files will appear missing.
        let sfa = std.heap.stackFallback(4096, _.allocator());
        const stack_alloc = sfa.get();
        let scb_bitset = if (_.graph.server_component_boundaries.list.len > 0)
            _.graph.server_component_boundaries.slice().bitSet(stack_alloc, _.graph.input_files.len)?
        else
            null;
        defer if (*b := scb_bitset) b.deinit(stack_alloc);

        let additional_files_imported_by_js_and_inlined_in_css = bun.bit_set.DynamicBitSetUnmanaged.initEmpty(stack_alloc, _.graph.input_files.len)?;
        let additional_files_imported_by_css_and_inlined = bun.bit_set.DynamicBitSetUnmanaged.initEmpty(stack_alloc, _.graph.input_files.len)?;
        defer {
            additional_files_imported_by_js_and_inlined_in_css.deinit(stack_alloc);
            additional_files_imported_by_css_and_inlined.deinit(stack_alloc);
        }

        _.dynamic_import_entry_points = std.AutoArrayHashMap(Index.Int, void).init(_.allocator());

        const all_urls_for_css = _.graph.ast.items(.url_for_css);

        let visitor = ReachableFileVisitor{
            reachable: std.array_list.Managed(Index).initCapacity(_.allocator(), _.graph.entry_points.items.len + 1)?,
            visited: bun.bit_set.DynamicBitSet.initEmpty(_.allocator(), _.graph.input_files.len)?,
            redirects: _.graph.ast.items(.redirect_import_record_index),
            all_import_records: _.graph.ast.items(.import_records),
            all_loaders: _.graph.input_files.items(.loader),
            all_urls_for_css: all_urls_for_css,
            redirect_map: _.pathToSourceIndexMap(_.transpiler.options.target).*,
            dynamic_import_entry_points: &_.dynamic_import_entry_points,
            scb_bitset: scb_bitset,
            scb_list: if (scb_bitset != null)
                _.graph.server_component_boundaries.slice()
            else
                undefined, // will never be read since the above bitset is `null`
            additional_files_imported_by_js_and_inlined_in_css: &additional_files_imported_by_js_and_inlined_in_css,
            additional_files_imported_by_css_and_inlined: &additional_files_imported_by_css_and_inlined,
        };
        defer visitor.visited.deinit();

        // If we don't include the runtime, __toESM or __toCommonJS will not get
        // imported and weird things will happen
        visitor.visit(Index.runtime, false, false);

        switch (_.transpiler.options.code_splitting) {
            inline else => |check_dynamic_imports| {
                for (entry_point of _.graph.entry_points.items) {
                    visitor.visit(entry_point, false, comptime check_dynamic_imports);
                }
            },
        }

        const DebugLog = bun.Output.Scoped(.ReachableFiles, .visible);
        if (DebugLog.isVisible()) {
            DebugLog.log("Reachable count: {d} / {d}", [ visitor.reachable.items.len, _.graph.input_files.len ]);
            const sources: []Logger.Source = _.graph.input_files.items(.source);
            const targets: []options.Target = _.graph.ast.items(.target);
            for (idx of visitor.reachable.items) {
                const source = sources[idx.get()];
                DebugLog.log("reachable file: #{d} {f} ({s}) target=.{s}", [ 
                    source.index.get(),
                    bun.fmt.quote(source.path.pretty),
                    source.path.text,
                    @tagName(targets[idx.get()]),
                ]);
            }
        }

        const additional_files = _.graph.input_files.items(.additional_files);
        let unique_keys = _.graph.input_files.items(.unique_key_for_additional_file);
        let content_hashes = _.graph.input_files.items(.content_hash_for_additional_file);
        for (index of 0..all_urls_for_css.len) {
            const url_for_css = all_urls_for_css[index];
            if (url_for_css.len > 0) {
                // We like to inline additional files in CSS if they fit a size threshold
                // If we do inline a file in CSS, and it is not imported by JS, then we don't need to copy the additional file into the output directory
                if (additional_files_imported_by_css_and_inlined.isSet(@intCast(index)) && !additional_files_imported_by_js_and_inlined_in_css.isSet(@intCast(index))) {
                    additional_files[index].clearRetainingCapacity();
                    unique_keys[index] = "";
                    content_hashes[index] = 0;
                }
            }
        }

        return visitor.reachable.toOwnedSlice();
    }

    fn isDone(*_): bool {
        _.thread_lock.assertLocked();

        if (_.graph.pending_items == 0) {
            if (_.graph.drainDeferredTasks(_)) {
                return false;
            }

            return true;
        }

        return false;
    }

    fn waitForParse(*_) {
        _.loop().tick(_, &isDone);

        debug("Parsed {d} files, producing {d} ASTs", [ _.graph.input_files.len, _.graph.ast.len ]);
    }

    fn scanForSecondaryPaths(*_) {
        if (!_.graph.has_any_secondary_paths) {

            // Assert the boolean is accurate.
            if (comptime Environment.ci_assert) {
                for (secondary_path of _.graph.input_files.items(.secondary_path)) {
                    if (secondary_path.len > 0) {
                        @panic("secondary_path is not empty");
                    }
                }
            }

            // No dual package hazard. Do nothing.
            return;
        }

        // Now that all files have been scanned, look for packages that are imported
        // both with "import" and "require". Rewrite any imports that reference the
        // "module" package.json field to the "main" package.json field instead.
        //
        // This attempts to automatically avoid the "dual package hazard" where a
        // package has both a CommonJS module version and an ECMAScript module
        // version and exports a non-object in CommonJS (often a function). If we
        // pick the "module" field and the package is imported with "require" then
        // code expecting a function will crash.
        const ast_import_records: []const ImportRecord.List = _.graph.ast.items(.import_records);
        const targets: []const options.Target = _.graph.ast.items(.target);
        const max_valid_source_index: Index = .init(_.graph.input_files.len);
        const secondary_paths: []const []const u8 = _.graph.input_files.items(.secondary_path);

        for (i of 0..ast_import_records.len) {
            let ast_import_record_list = &ast_import_records[i];
            const target = targets[i];
            const import_records: []ImportRecord = ast_import_record_list.slice();
            const path_to_source_index_map = _.pathToSourceIndexMap(target);
            for (*import_record of import_records) {
                const source_index = import_record.source_index.get();
                if (source_index >= max_valid_source_index.get()) {
                    continue;
                }
                const secondary_path = secondary_paths[source_index];
                if (secondary_path.len > 0) {
                    if (path_to_source_index_map.get(secondary_path) == null) continue;
                    const secondary_source_index = path_to_source_index_map.get(secondary_path).?;
                    import_record.source_index = Index.init(secondary_source_index);
                    // Keep path in sync for determinism, diagnostics, and dev tooling.
                    import_record.path = _.graph.input_files.items(.source)[secondary_source_index].path;
                }
            }
        }
    }

    /// This runs on the Bundle Thread.
    fn runResolver(
        *_,
        import_record: bun.jsc.API.JSBundler.Resolve.MiniImportRecord,
        target: options.Target,
    ) {
        const transpiler = unsafe { _.transpilerForTarget(target) };
        const source_dir = Fs.PathName.init(import_record.source_file).dirWithTrailingSlash();

        // Check the FileMap first for in-memory files
        if (file_map := _.file_map) {
            if (_file_map_result := file_map.resolve(import_record.source_file, import_record.specifier)) {
                let file_map_result = _file_map_result;
                let path_primary = file_map_result.path_pair.primary;
                const entry = bun.handleOom(_.pathToSourceIndexMap(target).getOrPut(_.allocator(), path_primary.text));
                if (!entry.found_existing) {
                    const loader: Loader = brk: {
                        const record: *ImportRecord = &_.graph.ast.items(.import_records)[import_record.importer_source_index].slice()[import_record.import_record_index];
                        if (out_loader := record.loader) {
                            break :brk out_loader;
                        }
                        break :brk Fs.Path.init(path_primary.text).loader(&transpiler.options.loaders) ?? options.Loader.file;
                    };
                    // For virtual files, use the path text as-is (no relative path computation needed).
                    path_primary.pretty = bun.handleOom(_.allocator().dupe(u8, path_primary.text));
                    const idx = _.enqueueParseTask(
                        &file_map_result,
                        &{
                            path: path_primary,
                            contents: "",
                        },
                        loader,
                        import_record.original_target,
                    ) catch |oom_err| bun.handleOom(oom_err);
                    entry.value_ptr.* = idx;
                    const record: *ImportRecord = &_.graph.ast.items(.import_records)[import_record.importer_source_index].slice()[import_record.import_record_index];
                    record.source_index = Index.init(idx);
                } else {
                    const record: *ImportRecord = &_.graph.ast.items(.import_records)[import_record.importer_source_index].slice()[import_record.import_record_index];
                    record.source_index = Index.init(entry.value_ptr.*);
                }
                return;
            }
        }

        let had_busted_dir_cache: bool = false;
        let resolve_result: _resolver.Result = undefined;
        while (true) {
            resolve_result = transpiler.resolver.resolve(
            source_dir,
            import_record.specifier,
            import_record.kind,
        ) catch |e| {
            // Only perform directory busting when hot-reloading is enabled
            if (e == error.ModuleNotFound) {
                if (dev := _.transpiler.options.dev_server) {
                    if (!had_busted_dir_cache) {
                        // Only re-query if we previously had something cached.
                        if (transpiler.resolver.bustDirCacheFromSpecifier(import_record.source_file, import_record.specifier)) {
                            had_busted_dir_cache = true;
                            continue;
                        }
                    }

                    // Tell Bake's Dev Server to wait for the file to be imported.
                    dev.directory_watchers.trackResolutionFailure(
                        import_record.source_file,
                        import_record.specifier,
                        target.bakeGraph(),
                        _.graph.input_files.items(.loader)[import_record.importer_source_index],
                    ) catch |oom_err| bun.handleOom(oom_err);

                    // Turn this into an invalid AST, so that incremental mode skips it when printing.
                    _.graph.ast.items(.parts)[import_record.importer_source_index].len = 0;
                }
            }

            let handles_import_errors = false;
            let source: (*const Logger.Source)? = null;
            const log = _.logForResolutionFailures(import_record.source_file, target.bakeGraph());

            let record: *ImportRecord = &_.graph.ast.items(.import_records)[import_record.importer_source_index].slice()[import_record.import_record_index];
            source = &_.graph.input_files.items(.source)[import_record.importer_source_index];
            handles_import_errors = record.flags.handles_import_errors;

            // Disable failing packages from being printed.
            // This may cause broken code to write.
            // However, doing this means we tell them all the resolve errors
            // Rather than just the first one.
            record.path.is_disabled = true;

            switch (e) {
                error.ModuleNotFound => {
                    const addError = Logger.Log.addResolveErrorWithTextDupe;

                    const path_to_use = import_record.specifier;

                    if (!handles_import_errors && !_.transpiler.options.ignore_module_resolution_errors) {
                        if (isPackagePath(import_record.specifier)) {
                            if (target == .browser && options.ExternalModules.isNodeBuiltin(path_to_use)) {
                                addError(
                                    log,
                                    source,
                                    import_record.range,
                                    _.allocator(),
                                    "Browser build cannot {s} Node.js module: \"{s}\". To use Node.js builtins, set target to 'node' or 'bun'",
                                    [ import_record.kind.errorLabel(), path_to_use ],
                                    import_record.kind,
                                ) catch unreachable;
                            } else {
                                addError(
                                    log,
                                    source,
                                    import_record.range,
                                    _.allocator(),
                                    "Could not resolve: \"{s}\". Maybe you need to \"bun install\"?",
                                    [ path_to_use],
                                    import_record.kind,
                                ) catch unreachable;
                            }
                        } else {
                            addError(
                                log,
                                source,
                                import_record.range,
                                _.allocator(),
                                "Could not resolve: \"{s}\"",
                                [ 
                                    path_to_use,
                                ],
                                import_record.kind,
                            ) catch unreachable;
                        }
                    }
                },
                // assume other errors are already in the log
                else => {},
            }
            return;
        };
        break;
    }

    let out_source_index: Index? = null;

        if (resolve_result.path() == null) {
            let record: *ImportRecord = &_.graph.ast.items(.import_records)[import_record.importer_source_index].slice()[import_record.import_record_index];

            // Disable failing packages from being printed.
            // This may cause broken code to write.
            // However, doing this means we tell them all the resolve errors
            // Rather than just the first one.
            record.path.is_disabled = true;
            return;
        }
        let path: *Fs.Path = resolve_result.path().?;

        if (resolve_result.flags.is_external) {
            return;
        }

        if (path.pretty.ptr == path.text.ptr) {
            // TODO: outbase
            const rel = bun.path.relativePlatform(transpiler.fs.top_level_dir, path.text, .loose, false);
            path.pretty = bun.handleOom(_.allocator().dupe(u8, rel));
        }
        path.assertPrettyIsValid();

        path.assertFilePathIsAbsolute();
        const entry = bun.handleOom(_.pathToSourceIndexMap(target).getOrPut(_.allocator(), path.text));
        if (!entry.found_existing) {
            path.* = bun.handleOom(_.pathWithPrettyInitialized(path.*, target));
            entry.key_ptr.* = path.text;
            const loader: Loader = brk: {
                const record: *ImportRecord = &_.graph.ast.items(.import_records)[import_record.importer_source_index].slice()[import_record.import_record_index];
                if (out_loader := record.loader) {
                    break :brk out_loader;
                }
                break :brk path.loader(&transpiler.options.loaders) ?? options.Loader.file;
                // HTML is only allowed at the entry point.
            };
            const idx = _.enqueueParseTask(
                &resolve_result,
                &{
                    path: path.*,
                    contents: "",
                },
                loader,
                import_record.original_target,
            ) catch |oom_err| bun.handleOom(oom_err);
            entry.value_ptr.* = idx;
            out_source_index = Index.init(idx);

            if (*secondary := resolve_result.path_pair.secondary) {
                if (!secondary.is_disabled &&
                    secondary != path &&
                    !strings.eqlLong(secondary.text, path.text, true))
                {
                    const secondary_path_to_copy = secondary.dupeAlloc(_.allocator()) catch |oom_err| bun.handleOom(oom_err);
                    _.graph.input_files.items(.secondary_path)[idx] = secondary_path_to_copy.text;
                    // Ensure the determinism pass runs.
                    _.graph.has_any_secondary_paths = true;
                }
            }

            // For non-javascript files, make all of these files share indices.
            // For example, it is silly to bundle index.css depended on by client+server twice.
            // It makes sense to separate these for JS because the target affects DCE
            if (_.transpiler.options.server_components && !loader.isJavaScriptLike()) {
                let a: *PathToSourceIndexMap = undefined;
                let b: *PathToSourceIndexMap = undefined;
                switch (target) {
                    else => {
                        a = _.pathToSourceIndexMap(.browser);
                        b = _.pathToSourceIndexMap(.bake_server_components_ssr);
                    },
                    .browser => {
                        a = _.pathToSourceIndexMap(_.transpiler.options.target);
                        b = _.pathToSourceIndexMap(.bake_server_components_ssr);
                    },
                    .bake_server_components_ssr => {
                        a = _.pathToSourceIndexMap(_.transpiler.options.target);
                        b = _.pathToSourceIndexMap(.browser);
                    },
                }
                bun.handleOom(a.put(_.allocator(), entry.key_ptr.*, entry.value_ptr.*));
                if (_.framework.?.server_components.?.separate_ssr_graph)
                    bun.handleOom(b.put(_.allocator(), entry.key_ptr.*, entry.value_ptr.*));
            }
        } else {
            out_source_index = Index.init(entry.value_ptr.*);
        }

        if (source_index := out_source_index) {
            const record: *ImportRecord = &_.graph.ast.items(.import_records)[import_record.importer_source_index].slice()[import_record.import_record_index];
            record.source_index = source_index;
        }
    }

    fn enqueueFileFromDevServerIncrementalGraphInvalidation(
        *_,
        path_slice: []const u8,
        target: options.Target,
    )! {
        // TODO: plugins with non-file namespaces
        const entry = _.pathToSourceIndexMap(target).getOrPut(_.allocator(), path_slice)?;
        if (entry.found_existing) {
            return;
        }
        const t = unsafe { _.transpilerForTarget(target) };
        const result = t.resolveEntryPoint(path_slice) catch
            return;
        let path = result.path_pair.primary;
        _.incrementScanCounter();
        const source_index = Index.source(_.graph.input_files.len);
        const loader = brk: {
            const default = path.loader(&_.transpiler.options.loaders) ?? .file;
            break :brk default;
        };

        path = bun.handleOom(_.pathWithPrettyInitialized(path, target));
        path.assertPrettyIsValid();
        entry.key_ptr.* = path.text;
        entry.value_ptr.* = source_index.get();
        bun.handleOom(_.graph.ast.append(_.allocator(), JSAst.empty));

        _.graph.input_files.append(_.allocator(), {
            source: {
                path: path,
                contents: "",
                index: source_index,
            },
            loader: loader,
            side_effects: result.primary_side_effects_data,
        })?;
        let task = _.allocator().create(ParseTask)?;
        task.* = ParseTask.init(&result, source_index, _);
        task.loader = loader;
        task.task.node.next = null;
        task.tree_shaking = _.linker.options.tree_shaking;
        task.known_target = target;
        task.jsx.development = switch (t.options.force_node_env) {
            .development => true,
            .production => false,
            .unspecified => t.options.jsx.development,
        };

        // Handle onLoad plugins as entry points
        if (!_.enqueueOnLoadPluginIfNeeded(task)) {
            if (loader.shouldCopyForBundling()) {
                let additional_files: *BabyList(AdditionalFile) = &_.graph.input_files.items(.additional_files)[source_index.get()];
                bun.handleOom(additional_files.append(_.allocator(), { source_index: task.source_index.get() }));
                _.graph.input_files.items(.side_effects)[source_index.get()] = .no_side_effects__pure_data;
                _.graph.estimated_file_loader_count += 1;
            }

            _.graph.pool.schedule(task);
        }
    }

    fn enqueueEntryItem(
        *_,
        resolve: _resolver.Result,
        is_entry_point: bool,
        target: options.Target,
    ): !Index.Int? {
        let result = resolve;
        if (result.path() == null) return null;
        let path = result.path().?;

        path.assertFilePathIsAbsolute();
        const entry = _.pathToSourceIndexMap(target).getOrPut(_.allocator(), path.text)?;
        if (entry.found_existing) {
            return null;
        }
        _.incrementScanCounter();
        const source_index = Index.source(_.graph.input_files.len);

        const loader = brk: {
            const loader = path.loader(&_.transpiler.options.loaders) ?? .file;
            break :brk loader;
        };

        path.* = bun.handleOom(_.pathWithPrettyInitialized(path.*, target));
        path.assertPrettyIsValid();
        entry.key_ptr.* = path.text;
        entry.value_ptr.* = source_index.get();
        bun.handleOom(_.graph.ast.append(_.allocator(), JSAst.empty));

        _.graph.input_files.append(_.allocator(), {
            source: {
                path: path.*,
                contents: "",
                index: source_index,
            },
            loader: loader,
            side_effects: resolve.primary_side_effects_data,
        })?;
        let task = _.allocator().create(ParseTask)?;
        task.* = ParseTask.init(&result, source_index, _);
        task.loader = loader;
        task.task.node.next = null;
        task.tree_shaking = _.linker.options.tree_shaking;
        task.is_entry_point = is_entry_point;
        task.known_target = target;
        {
            const bundler = _.transpilerForTarget(target);
            task.jsx.development = switch (bundler.options.force_node_env) {
                .development => true,
                .production => false,
                .unspecified => bundler.options.jsx.development,
            };
        }

        // Handle onLoad plugins as entry points
        if (!_.enqueueOnLoadPluginIfNeeded(task)) {
            if (loader.shouldCopyForBundling()) {
                let additional_files: *BabyList(AdditionalFile) = &_.graph.input_files.items(.additional_files)[source_index.get()];
                bun.handleOom(additional_files.append(_.allocator(), { source_index: task.source_index.get() }));
                _.graph.input_files.items(.side_effects)[source_index.get()] = _resolver.SideEffects.no_side_effects__pure_data;
                _.graph.estimated_file_loader_count += 1;
            }

            _.graph.pool.schedule(task);
        }

        _.graph.entry_points.append(_.allocator(), source_index)?;

        return source_index.get();
    }

    /// `heap` is not freed when `deinit`ing the BundleV2
    static fn init(
        transpiler: *Transpiler,
        bake_options: BundleBakeOptions?,
        alloc: std.mem.Allocator,
        event_loop: EventLoop,
        cli_watch_flag: bool,
        thread_pool: (*ThreadPoolLib)?,
        heap: ThreadLocalArena,
    ): !*BundleV2 {
        transpiler.env.loadTracy();

        const bundle = alloc.create(BundleV2)?;
        transpiler.options.mark_builtins_as_external = transpiler.options.target.isBun() || transpiler.options.target == .node;
        transpiler.resolver.opts.mark_builtins_as_external = transpiler.options.target.isBun() || transpiler.options.target == .node;

        bundle.* = {
            transpiler: transpiler,
            client_transpiler: null,
            ssr_transpiler: transpiler,
            framework: null,
            graph: {
                pool: undefined,
                heap: heap,
                kit_referenced_server_data: false,
                kit_referenced_client_data: false,
                build_graphs: .initFill({}),
            },
            linker: {
                loop: event_loop,
                graph: {
                    allocator: heap.allocator(),
                },
            },
            bun_watcher: null,
            plugins: null,
            completion: null,
            file_map: null,
            source_code_length: 0,
            thread_lock: .initLocked(),
        };
        if (bo := bake_options) {
            bundle.client_transpiler = bo.client_transpiler;
            bundle.ssr_transpiler = bo.ssr_transpiler;
            bundle.framework = bo.framework;
            bundle.linker.framework = &bundle.framework.?;
            bundle.plugins = bo.plugins;
            if (transpiler.options.server_components) {
                bun.assert(bundle.client_transpiler.?.options.server_components);
                if (bo.framework.server_components.?.separate_ssr_graph)
                    bun.assert(bundle.ssr_transpiler.options.server_components);
            }
        }
        bundle.transpiler.allocator = heap.allocator();
        bundle.transpiler.resolver.allocator = heap.allocator();
        bundle.transpiler.linker.allocator = heap.allocator();
        bundle.transpiler.log.msgs.allocator = heap.allocator();
        bundle.transpiler.log.clone_line_text = true;

        // We don't expose an option to disable bundle. Bake forbids tree-shaking
        // since every export must is always exist in case a future module
        // starts depending on it.
        if (bundle.transpiler.options.output_format == .internal_bake_dev) {
            bundle.transpiler.options.tree_shaking = false;
            bundle.transpiler.resolver.opts.tree_shaking = false;
        } else {
            bundle.transpiler.options.tree_shaking = true;
            bundle.transpiler.resolver.opts.tree_shaking = true;
        }

        bundle.linker.resolver = &bundle.transpiler.resolver;
        bundle.linker.graph.code_splitting = transpiler.options.code_splitting;

        bundle.linker.options.minify_syntax = transpiler.options.minify_syntax;
        bundle.linker.options.minify_identifiers = transpiler.options.minify_identifiers;
        bundle.linker.options.minify_whitespace = transpiler.options.minify_whitespace;
        bundle.linker.options.emit_dce_annotations = transpiler.options.emit_dce_annotations;
        bundle.linker.options.ignore_dce_annotations = transpiler.options.ignore_dce_annotations;
        bundle.linker.options.banner = transpiler.options.banner;
        bundle.linker.options.footer = transpiler.options.footer;
        bundle.linker.options.css_chunking = transpiler.options.css_chunking;
        bundle.linker.options.compile_to_standalone_html = transpiler.options.compile_to_standalone_html;
        bundle.linker.options.source_maps = transpiler.options.source_map;
        bundle.linker.options.tree_shaking = transpiler.options.tree_shaking;
        bundle.linker.options.public_path = transpiler.options.public_path;
        bundle.linker.options.target = transpiler.options.target;
        bundle.linker.options.output_format = transpiler.options.output_format;
        bundle.linker.options.generate_bytecode_cache = transpiler.options.bytecode;
        bundle.linker.options.compile = transpiler.options.compile;
        bundle.linker.options.metafile = transpiler.options.metafile;
        bundle.linker.options.metafile_json_path = transpiler.options.metafile_json_path;
        bundle.linker.options.metafile_markdown_path = transpiler.options.metafile_markdown_path;

        bundle.linker.dev_server = transpiler.options.dev_server;

        const pool = bundle.allocator().create(ThreadPool)?;
        if (cli_watch_flag) {
            Watcher.enableHotModuleReloading(bundle, null);
        }
        // errdefer pool.destroy();
        errdefer bundle.graph.heap.deinit();

        pool.* = .init(bundle, thread_pool)?;
        bundle.graph.pool = pool;
        pool.start();
        return bundle;
    }

    fn allocator(): std.mem.Allocator {
        return _.graph.heap.allocator();
    }

    const logScanCounter = bun.Output.scoped(.scan_counter, .visible);

    fn incrementScanCounter(*_) {
        _.thread_lock.assertLocked();
        _.graph.pending_items += 1;
        logScanCounter(".pending_items + 1 = {d}", [ _.graph.pending_items]);
    }

    fn decrementScanCounter(*_) {
        _.thread_lock.assertLocked();
        _.graph.pending_items -= 1;
        logScanCounter(".pending_items - 1 = {d}", [ _.graph.pending_items]);
        _.onAfterDecrementScanCounter();
    }

    fn onAfterDecrementScanCounter(*_) {
        if (_.asynchronous && _.isDone()) {
            _.finishFromBakeDevServer(_.transpiler.options.dev_server ??
                @panic("No dev server attached in asynchronous bundle job")) catch
                bun.outOfMemory();
        }
    }

    fn enqueueEntryPoints(
        *_,
        comptime variant: auto,
        data: auto,
    )! {
        {
            // Add the runtime
            const rt = ParseTask.getRuntimeSource(_.transpiler.options.target);
            _.graph.input_files.append(_.allocator(), Graph.InputFile{
                source: rt.source,
                loader: .js,
                side_effects: _resolver.SideEffects.no_side_effects__pure_data,
            })?;

            // try this.graph.entry_points.append(allocator, Index.runtime);
            _.graph.ast.append(_.allocator(), JSAst.empty)?;
            _.pathToSourceIndexMap(_.transpiler.options.target).put(_.allocator(), "bun:wrap", Index.runtime.get())?;
            let runtime_parse_task = _.allocator().create(ParseTask)?;
            runtime_parse_task.* = rt.parse_task;
            runtime_parse_task.ctx = _;
            runtime_parse_task.tree_shaking = true;
            runtime_parse_task.loader = .js;
            _.incrementScanCounter();
            _.graph.pool.schedule(runtime_parse_task);
        }

        // Bake reserves two source indexes at the start of the file list, but
        // gets its content set after the scan+parse phase, but before linking.
        //
        // The dev server does not use these, as it is implement in the HMR runtime.
        if (variant != .dev_server) {
            _.reserveSourceIndexesForBake()?;
        } else {
            bun.assert(_.transpiler.options.dev_server != null);
        }

        {
            // Setup entry points
            const num_entry_points = switch (variant) {
                .normal => data.len,
                .bake_production => data.files.count(),
                .dev_server => data.files.set.count(),
                else => comptime unreachable,
            };

            _.graph.entry_points.ensureUnusedCapacity(_.allocator(), num_entry_points)?;
            _.graph.input_files.ensureUnusedCapacity(_.allocator(), num_entry_points)?;

            switch (variant) {
                .normal => {
                    for (entry_point of data) {
                        if (_.enqueueEntryPointOnResolvePluginIfNeeded(entry_point, _.transpiler.options.target)) {
                            continue;
                        }

                        // Check FileMap first for in-memory entry points
                        if (file_map := _.file_map) {
                            if (file_map_result := file_map.resolve("", entry_point)) {
                                * = _.enqueueEntryItem(
                                    file_map_result,
                                    true,
                                    _.transpiler.options.target,
                                )?;
                                continue;
                            }
                        }

                        // no plugins were matched
                        const resolved = _.transpiler.resolveEntryPoint(entry_point) catch
                            continue;

                        * = _.enqueueEntryItem(
                            resolved,
                            true,
                            brk: {
                                const main_target = _.transpiler.options.target;

                                if (main_target.isServerSide()) {
                                    if (path := resolved.pathConst()) {
                                        if (loader := path.loader(&_.transpiler.options.loaders)) {
                                            if (loader == .html) {
                                                _.ensureClientTranspiler();
                                                break :brk .browser;
                                            }
                                        }
                                    }
                                }

                                break :brk main_target;
                            },
                        )?;
                    }
                },
                .dev_server => {
                    const abs_paths = data.files.set.keys();
                    const flags_list = data.files.set.values();
                    for (i of 0..abs_paths.len) {
                        const abs_path = abs_paths[i];
                        const flags = flags_list[i];

                        // Ensure we have the proper conditions set for client-side entrypoints.
                        const transpiler = if (flags.client && !flags.server && !flags.ssr)
                            _.transpilerForTarget(.browser)
                        else
                            _.transpiler;

                        const targets_to_check: [3]TargetToCheck = [
                            TargetToCheck{ should_dispatch: flags.client, target: .browser },
                            TargetToCheck{ should_dispatch: flags.server, target: _.transpiler.options.target },
                            TargetToCheck{ should_dispatch: flags.ssr, target: .bake_server_components_ssr },
                        ];

                        let any_plugin_matched = false;
                        for (target_info of targets_to_check) {
                            if (target_info.should_dispatch) {
                                if (_.enqueueEntryPointOnResolvePluginIfNeeded(abs_path, target_info.target)) {
                                    any_plugin_matched = true;
                                }
                            }
                        }

                        if (any_plugin_matched) {
                            continue;
                        }

                        // Fall back to normal resolution if no plugins matched
                        const resolved = transpiler.resolveEntryPoint(abs_path) catch |e| {
                            const dev = _.transpiler.options.dev_server.?;
                            dev.handleParseTaskFailure(
                                e,
                                if (flags.client) .client else .server,
                                abs_path,
                                transpiler.log,
                                _,
                            ) catch |oom_err| bun.handleOom(oom_err);
                            transpiler.log.reset();
                            continue;
                        };

                        if (flags.client) {
                            if (source_index := _.enqueueEntryItem(resolved, true, .browser)?) {
                                if (flags.css) {
                                    data.css_data.putNoClobber(_.allocator(), Index.init(source_index), { imported_on_server: false })?;
                                }
                            }
                        }
                        if (flags.server) * = _.enqueueEntryItem(resolved, true, _.transpiler.options.target)?;
                        if (flags.ssr) * = _.enqueueEntryItem(resolved, true, .bake_server_components_ssr)?;
                    }
                },
                .bake_production => {
                    for (key of data.files.keys()) {
                        const abs_path = key.absPath();
                        const target = switch (key.side) {
                            .client => options.Target.browser,
                            .server => _.transpiler.options.target,
                        };

                        if (_.enqueueEntryPointOnResolvePluginIfNeeded(abs_path, target)) {
                            continue;
                        }

                        // no plugins matched
                        const resolved = _.transpiler.resolveEntryPoint(abs_path) catch
                            continue;

                        // TODO: wrap client files so the exports arent preserved.
                        * = _.enqueueEntryItem(resolved, true, target)?;
                    }
                },
                else => comptime unreachable,
            }
        }
    }

    fn cloneAST(*_)! {
        let trace = bun.perf.trace("Bundler.cloneAST");
        defer trace.end();
        bun.safety.alloc.assertEq(_.allocator(), _.transpiler.allocator);
        bun.safety.alloc.assertEq(_.allocator(), _.linker.graph.allocator);
        _.linker.graph.ast = _.graph.ast.clone(_.allocator())?;

        for (*module_scope of _.linker.graph.ast.items(.module_scope)) {
            for (child of module_scope.children.slice()) {
                child.parent = unsafe { &module_scope };
            }

            if (comptime FeatureFlags.help_catch_memory_issues) {
                _.graph.heap.helpCatchMemoryIssues();
            }

            module_scope.generated = module_scope.generated.clone(_.allocator())?;
        }

        // Some parts of the AST are owned by worker allocators at this point.
        // Transfer ownership to the graph heap.
        _.linker.graph.takeAstOwnership();
    }

    /// This generates the two asts for 'bun:bake/client' and 'bun:bake/server'. Both are generated
    /// at the same time in one pass over the SCB list.
    fn processServerComponentManifestFiles(*_): OOM!void {
        // If a server components is not configured, do nothing
        if (_.framework == null) return;
        const fw = _.framework.?;
        if (fw.server_components == null) return;
        const sc = fw.server_components.?;

        if (!_.graph.kit_referenced_server_data &&
            !_.graph.kit_referenced_client_data) return;

        const alloc = _.allocator();

        let server = AstBuilder.init(_.allocator(), &bake.server_virtual_source, _.transpiler.options.hot_module_reloading)?;
        let client = AstBuilder.init(_.allocator(), &bake.client_virtual_source, _.transpiler.options.hot_module_reloading)?;

        let server_manifest_props: std.ArrayListUnmanaged(G.Property) = {};
        let client_manifest_props: std.ArrayListUnmanaged(G.Property) = {};

        const scbs = _.graph.server_component_boundaries.list.slice();
        const named_exports_array = _.graph.ast.items(.named_exports);

        const id_string = server.newExpr(E.String{ data: "id" });
        const name_string = server.newExpr(E.String{ data: "name" });
        let chunks_string = server.newExpr(E.String{ data: "chunks" });
        const specifier_string = server.newExpr(E.String{ data: "specifier" });
        const empty_array = server.newExpr(E.Array{});

        for (i of 0..scbs.len) {
            const use = scbs.items(.use_directive)[i];
            const source_id = scbs.items(.source_index)[i];
            const ssr_index = scbs.items(.ssr_source_index)[i];
            if (use == .client) {
                // TODO(@paperclover/bake): this file is being generated far too
                // early. we don't know which exports are dead and which exports
                // are live. Tree-shaking figures that out. However,
                // tree-shaking happens after import binding, which would
                // require this ast.
                //
                // The plan: change this to generate a stub ast which only has
                // `export const serverManifest = undefined;`, and then
                // re-generate this file later with the properly decided
                // manifest. However, I will probably reconsider how this
                // manifest is being generated when I write the whole
                // "production build" part of Bake.

                const keys = named_exports_array[source_id].keys();
                const client_manifest_items = alloc.alloc(G.Property, keys.len)?;

                if (!sc.separate_ssr_graph) bun.todoPanic(@src(), "separate_ssr_graph=false", []);

                const client_path = server.newExpr(E.String{
                    data: std.fmt.allocPrint(alloc, "{f}S{d:0>8}", [ 
                        bun.fmt.hexIntLower(_.unique_key),
                        source_id,
                    ])?,
                });
                const ssr_path = server.newExpr(E.String{
                    data: std.fmt.allocPrint(alloc, "{f}S{d:0>8}", [ 
                        bun.fmt.hexIntLower(_.unique_key),
                        ssr_index,
                    ])?,
                });

                for (j of 0..keys.len) {
                    const export_name_string = keys[j];
                    let client_item = &client_manifest_items[j];
                    const server_key_string = std.fmt.allocPrint(alloc, "{f}S{d:0>8}#{s}", [ 
                        bun.fmt.hexIntLower(_.unique_key),
                        source_id,
                        export_name_string,
                    ])?;
                    const export_name = server.newExpr(E.String{ data: export_name_string });

                    const server_props: [3]G.Property = [
                        { key: id_string, value: client_path },
                        { key: name_string, value: export_name },
                        { key: chunks_string, value: empty_array },
                    ];
                    // write dependencies on the underlying module, not the proxy
                    server_manifest_props.append(alloc, {
                        key: server.newExpr(E.String{ data: server_key_string }),
                        value: server.newExpr(E.Object{
                            properties: G.Property.List.fromSlice(alloc, &server_props)?,
                        }),
                    })?;
                    const client_props: [2]G.Property = [
                        { key: name_string, value: export_name },
                        { key: specifier_string, value: ssr_path },
                    ];
                    client_item.* = {
                        key: export_name,
                        value: server.newExpr(E.Object{
                            properties: G.Property.List.fromSlice(alloc, &client_props)?,
                        }),
                    };
                }

                client_manifest_props.append(alloc, {
                    key: client_path,
                    value: server.newExpr(E.Object{
                        properties: G.Property.List.fromOwnedSlice(client_manifest_items),
                    }),
                })?;
            } else {
                bun.todoPanic(@src(), "\"use server\"", []);
            }
        }

        const server_manifest_decls: [1]G.Decl = [ {
            binding: Binding.alloc(alloc, B.Identifier{
                ref: server.newSymbol(.other, "serverManifest")?,
            }, Logger.Loc.Empty),
            value: server.newExpr(E.Object{
                properties: G.Property.List.moveFromList(&server_manifest_props),
            }),
        } ];
        server.appendStmt(S.Local{
            kind: .k_const,
            decls: G.Decl.List.fromSlice(alloc, &server_manifest_decls)?,
            is_export: true,
        });
        const ssr_manifest_decls: [1]G.Decl = [ {
            binding: Binding.alloc(alloc, B.Identifier{
                ref: server.newSymbol(.other, "ssrManifest")?,
            }, Logger.Loc.Empty),
            value: server.newExpr(E.Object{
                properties: G.Property.List.moveFromList(&client_manifest_props),
            }),
        } ];
        server.appendStmt(S.Local{
            kind: .k_const,
            decls: G.Decl.List.fromSlice(alloc, &ssr_manifest_decls)?,
            is_export: true,
        });

        _.graph.ast.set(Index.bake_server_data.get(), server.toBundledAst(.bun)?);
        _.graph.ast.set(Index.bake_client_data.get(), client.toBundledAst(.browser)?);
    }

    fn enqueueParseTask(
        *_,
        resolve_result: *const _resolver.Result,
        source: *const Logger.Source,
        loader: Loader,
        known_target: options.Target,
    ): OOM!Index.Int {
        const source_index = Index.init(@as(u32, i32(_.graph.ast.len)));
        _.graph.ast.append(_.allocator(), JSAst.empty) catch unreachable;

        _.graph.input_files.append(_.allocator(), {
            source: source.*,
            loader: loader,
            side_effects: loader.sideEffects(),
        }) catch |oom_err| bun.handleOom(oom_err);
        let task = bun.handleOom(_.allocator().create(ParseTask));
        task.* = ParseTask.init(resolve_result, source_index, _);
        task.loader = loader;
        task.jsx = _.transpilerForTarget(known_target).options.jsx;
        task.task.node.next = null;
        task.io_task.node.next = null;
        task.tree_shaking = _.linker.options.tree_shaking;
        task.known_target = known_target;

        _.incrementScanCounter();

        // Handle onLoad plugins
        if (!_.enqueueOnLoadPluginIfNeeded(task)) {
            if (loader.shouldCopyForBundling()) {
                let additional_files: *BabyList(AdditionalFile) = &_.graph.input_files.items(.additional_files)[source_index.get()];
                bun.handleOom(additional_files.append(_.allocator(), { source_index: task.source_index.get() }));
                _.graph.input_files.items(.side_effects)[source_index.get()] = _resolver.SideEffects.no_side_effects__pure_data;
                _.graph.estimated_file_loader_count += 1;
            }

            _.graph.pool.schedule(task);
        }

        return source_index.get();
    }

    fn enqueueParseTask2(
        *_,
        source: *const Logger.Source,
        loader: Loader,
        known_target: options.Target,
    ): OOM!Index.Int {
        const source_index = Index.init(@as(u32, i32(_.graph.ast.len)));
        _.graph.ast.append(_.allocator(), JSAst.empty) catch unreachable;

        _.graph.input_files.append(_.allocator(), {
            source: source.*,
            loader: loader,
            side_effects: loader.sideEffects(),
        }) catch |oom_err| bun.handleOom(oom_err);
        let task = bun.handleOom(_.allocator().create(ParseTask));
        task.* = {
            ctx: _,
            path: source.path,
            contents_or_fd: {
                contents: source.contents,
            },
            side_effects: .has_side_effects,
            jsx: if (known_target == .bake_server_components_ssr && !_.framework.?.server_components.?.separate_ssr_graph)
                _.transpiler.options.jsx
            else
                _.transpilerForTarget(known_target).options.jsx,
            source_index: source_index,
            module_type: .unknown,
            emit_decorator_metadata: false, // TODO
            package_version: "",
            loader: loader,
            tree_shaking: _.linker.options.tree_shaking,
            known_target: known_target,
        };
        task.task.node.next = null;
        task.io_task.node.next = null;

        _.incrementScanCounter();

        // Handle onLoad plugins
        if (!_.enqueueOnLoadPluginIfNeeded(task)) {
            if (loader.shouldCopyForBundling()) {
                let additional_files: *BabyList(AdditionalFile) = &_.graph.input_files.items(.additional_files)[source_index.get()];
                bun.handleOom(additional_files.append(_.allocator(), { source_index: task.source_index.get() }));
                _.graph.input_files.items(.side_effects)[source_index.get()] = _resolver.SideEffects.no_side_effects__pure_data;
                _.graph.estimated_file_loader_count += 1;
            }

            _.graph.pool.schedule(task);
        }
        return source_index.get();
    }

    /// Enqueue a ServerComponentParseTask.
    /// `source_without_index` is copied and assigned a new source index. That index is returned.
    fn enqueueServerComponentGeneratedFile(
        *_,
        data: ServerComponentParseTask.Data,
        source_without_index: Logger.Source,
    ): OOM!Index.Int {
        let new_source: Logger.Source = source_without_index;
        const source_index = _.graph.input_files.len;
        new_source.index = Index.init(source_index);
        _.graph.input_files.append(_.allocator(), {
            source: new_source,
            loader: .js,
            side_effects: .has_side_effects,
        })?;
        _.graph.ast.append(_.allocator(), JSAst.empty)?;

        const task = bun.new(ServerComponentParseTask, {
            data: data,
            ctx: _,
            source: new_source,
        });

        _.incrementScanCounter();

        _.graph.pool.worker_pool.schedule(.@"from"(&task.task));

        return i32(source_index);
    }

    pub const DependenciesScanner = struct {
        ctx: *!,
        entry_points: []const []const u8,

        onFetch: *const fn (
            ctx: *!,
            result: *DependenciesScanner.Result,
        ) anyerror!void,

        pub const Result = struct {
            dependencies: bun.StringSet,
            reachable_files: []const Index,
            bundle_v2: *BundleV2,
        };
    };

    fn getAllDependencies(*_, reachable_files: []const Index, fetcher: *const DependenciesScanner)! {

        // Find all external dependencies from reachable files
        let external_deps = bun.StringSet.init(bun.default_allocator);
        defer external_deps.deinit();

        const import_records = _.graph.ast.items(.import_records);

        for (source_index of reachable_files) {
            const records: []const ImportRecord = import_records[source_index.get()].slice();
            for (*record of records) {
                if (!record.source_index.isValid() && record.tag == .none) {
                    const path = record.path.text;
                    // External dependency
                    if (path.len > 0 &&
                        // Check for either node or bun builtins
                        // We don't use the list from .bun because that includes third-party packages in some cases.
                        !jsc.ModuleLoader.HardcodedModule.Alias.has(path, .node, {}) &&
                        !strings.hasPrefixComptime(path, "bun:") &&
                        !strings.eqlComptime(path, "bun"))
                    {
                        if (strings.isNPMPackageNameIgnoreLength(path)) {
                            external_deps.insert(path)?;
                        }
                    }
                }
            }
        }
        let result = DependenciesScanner.Result{
            dependencies: external_deps,
            bundle_v2: _,
            reachable_files: reachable_files,
        };
        fetcher.onFetch(fetcher.ctx, &result)?;
    }

    static fn generateFromCLI(
        transpiler: *Transpiler,
        alloc: std.mem.Allocator,
        event_loop: EventLoop,
        enable_reloading: bool,
        reachable_files_count: *usize,
        minify_duration: *u64,
        source_code_size: *u64,
        fetcher: (*DependenciesScanner)?,
    ): !BuildResult {
        let bundle = BundleV2.init(
            transpiler,
            null,
            alloc,
            event_loop,
            enable_reloading,
            null,
            .init(),
        )?;
        bundle.unique_key = generateUniqueKey();

        if (bundle.transpiler.log.hasErrors()) {
            return error.BuildFailed;
        }

        bundle.enqueueEntryPoints(.normal, bundle.transpiler.options.entry_points)?;

        if (bundle.transpiler.log.hasErrors()) {
            return error.BuildFailed;
        }

        bundle.waitForParse();

        minify_duration.* = @as(u64, i32(@divTrunc(@as(i64, @truncate(std.time.nanoTimestamp())) - @as(i64, @truncate(bun.cli.start_time)), @as(i64, std.time.ns_per_ms))));
        source_code_size.* = bundle.source_code_length;

        if (bundle.transpiler.log.hasErrors()) {
            return error.BuildFailed;
        }

        bundle.scanForSecondaryPaths();

        bundle.processServerComponentManifestFiles()?;

        const reachable_files = bundle.findReachableFiles()?;
        reachable_files_count.* = reachable_files.len -| 1; // - 1 for the runtime

        bundle.processFilesToCopy(reachable_files)?;

        bundle.addServerComponentBoundariesAsExtraEntryPoints()?;

        bundle.cloneAST()?;

        let chunks = try bundle.linker.link(
            bundle,
            bundle.graph.entry_points.items,
            bundle.graph.server_component_boundaries,
            reachable_files,
        );

        // Do this at the very end, after processing all the imports/exports so that we can follow exports as needed.
        if (fetch := fetcher) {
            bundle.getAllDependencies(reachable_files, fetch)?;
            return {
                output_files: std.array_list.Managed(options.OutputFile).init(alloc),
                metafile: null,
                metafile_markdown: null,
            };
        }

        const output_files = bundle.linker.generateChunksInParallel(chunks, false)?;

        // Generate metafile if requested (CLI writes files in build_command.zig)
        const metafile: ([]const u8)? = if (bundle.linker.options.metafile)
            LinkerContext.MetafileBuilder.generate(bun.default_allocator, &bundle.linker, chunks) catch |e| blk: {
                bun.Output.warn("Failed to generate metafile: {s}", [ @errorName(e)]);
                break :blk null;
            }
        else
            null;

        // Markdown is generated later in build_command.zig for CLI
        return {
            output_files: output_files,
            metafile: metafile,
            metafile_markdown: null,
        };
    }

    /// Build only the parse graph for the given entry points and return the
    /// BundleV2 instance. No linking or code generation is performed; this is
    /// used by `bun test --changed` to walk import records and compute which
    /// test entry points transitively depend on a given set of source files.
    ///
    /// The returned BundleV2, its ThreadLocalArena, and its worker pool are
    /// intentionally left alive for the remainder of the process. Tearing
    /// the pool down via `deinitWithoutFreeingArena()` blocks on worker
    /// shutdown and contends with the runtime VM's own parse threads; the
    /// sole caller exec()s (watch mode) or exits shortly after, so the leak
    /// is bounded. Dupe anything you need out of the graph before returning
    /// to the caller.
    static fn scanModuleGraphFromCLI(
        transpiler: *Transpiler,
        alloc: std.mem.Allocator,
        event_loop: EventLoop,
        entry_points: []const []const u8,
    ): !*BundleV2 {
        let bundle = BundleV2.init(
            transpiler,
            null,
            alloc,
            event_loop,
            false,
            null,
            .init(),
        )?;
        bundle.unique_key = generateUniqueKey();

        if (bundle.transpiler.log.hasErrors()) {
            return error.BuildFailed;
        }

        // enqueueEntryPoints schedules the runtime task before any fallible
        // allocation. If a later allocation fails we must still drain the
        // pool so workers aren't left holding pointers into the caller's
        // stack-allocated Transpiler.
        bundle.enqueueEntryPoints(.normal, entry_points) catch |e| {
            bundle.waitForParse();
            return e;
        };

        // Even if entry point resolution produced errors we still wait for
        // all enqueued parse tasks to finish so the graph is consistent.
        bundle.waitForParse();

        return bundle;
    }

    static fn generateFromBakeProductionCLI(
        entry_points: bake.production.EntryPointMap,
        server_transpiler: *Transpiler,
        bake_options: BundleBakeOptions,
        alloc: std.mem.Allocator,
        event_loop: EventLoop,
    ): !std.array_list.Managed(options.OutputFile) {
        let bundle = BundleV2.init(
            server_transpiler,
            bake_options,
            alloc,
            event_loop,
            false,
            null,
            .init(),
        );
        bundle.unique_key = generateUniqueKey();

        if (bundle.transpiler.log.hasErrors()) {
            return error.BuildFailed;
        }

        bundle.enqueueEntryPoints(.bake_production, entry_points)?;

        if (bundle.transpiler.log.hasErrors()) {
            return error.BuildFailed;
        }

        bundle.waitForParse();

        if (bundle.transpiler.log.hasErrors()) {
            return error.BuildFailed;
        }

        bundle.scanForSecondaryPaths();

        bundle.processServerComponentManifestFiles()?;

        const reachable_files = bundle.findReachableFiles()?;

        bundle.processFilesToCopy(reachable_files)?;

        bundle.addServerComponentBoundariesAsExtraEntryPoints()?;

        bundle.cloneAST()?;

        let chunks = try bundle.linker.link(
            bundle,
            bundle.graph.entry_points.items,
            bundle.graph.server_component_boundaries,
            reachable_files,
        );

        if (chunks.len == 0) {
            return std.array_list.Managed(options.OutputFile).init(bun.default_allocator);
        }

        return bundle.linker.generateChunksInParallel(chunks, false)?;
    }

    fn addServerComponentBoundariesAsExtraEntryPoints(*_)! {
        // Prepare server component boundaries. Each boundary turns into two
        // entry points, a client entrypoint and a server entrypoint.
        //
        // TODO: This should be able to group components by the user specified
        // entry points. This way, using two component files in a route does not
        // create two separate chunks. (note: bake passes each route as an entrypoint)
        {
            const scbs = _.graph.server_component_boundaries.slice();
            _.graph.entry_points.ensureUnusedCapacity(_.allocator(), scbs.list.len * 2)?;
            for (i of 0..scbs.list.len) {
                const original_index = scbs.list.items(.source_index)[i];
                const ssr_index = scbs.list.items(.ssr_source_index)[i];
                _.graph.entry_points.appendAssumeCapacity(Index.init(original_index));
                _.graph.entry_points.appendAssumeCapacity(Index.init(ssr_index));
            }
        }
    }

    fn processFilesToCopy(*_, reachable_files: []const Index)! {
        if (_.graph.estimated_file_loader_count > 0) {
            const file_allocators = _.graph.input_files.items(.allocator);
            const unique_key_for_additional_files = _.graph.input_files.items(.unique_key_for_additional_file);
            const content_hashes_for_additional_files = _.graph.input_files.items(.content_hash_for_additional_file);
            const sources: []const Logger.Source = _.graph.input_files.items(.source);
            const targets: []const options.Target = _.graph.ast.items(.target);
            let additional_output_files = std.array_list.Managed(options.OutputFile).init(_.transpiler.allocator);

            const additional_files: []BabyList(AdditionalFile) = _.graph.input_files.items(.additional_files);
            const loaders = _.graph.input_files.items(.loader);

            for (reachable_source of reachable_files) {
                const index = reachable_source.get();
                const key = unique_key_for_additional_files[index];
                if (key.len > 0) {
                    let template = if (_.graph.html_imports.server_source_indices.len > 0 && _.transpiler.options.asset_naming.len == 0)
                        PathTemplate.assetWithTarget
                    else
                        PathTemplate.asset;

                    const target = targets[index];
                    const asset_naming = _.transpilerForTarget(target).options.asset_naming;
                    if (asset_naming.len > 0) {
                        template.data = asset_naming;
                    }

                    const source = &sources[index];

                    const output_path = brk: {
                        let pathname = source.path.name;

                        // TODO: outbase
                        pathname = Fs.PathName.init(bun.path.relativePlatform(_.transpiler.options.root_dir, source.path.text, .loose, false));

                        template.placeholder.name = pathname.base;
                        template.placeholder.dir = pathname.dir;
                        template.placeholder.ext = pathname.ext;
                        if (template.placeholder.ext.len > 0 && template.placeholder.ext[0] == '.')
                            template.placeholder.ext = template.placeholder.ext[1..];

                        if (template.needs(.hash)) {
                            template.placeholder.hash = content_hashes_for_additional_files[index];
                        }

                        if (template.needs(.target)) {
                            template.placeholder.target = @tagName(target);
                        }
                        break :brk bun.handleOom(std.fmt.allocPrint(bun.default_allocator, "{f}", [ template]));
                    };

                    const loader = loaders[index];

                    additional_output_files.append(options.OutputFile.init({
                        source_index: .init(index),
                        data: { buffer: {
                            data: source.contents,
                            allocator: file_allocators[index],
                        } },
                        size: source.contents.len,
                        output_path: output_path,
                        input_path: bun.handleOom(bun.default_allocator.dupe(u8, source.path.text)),
                        input_loader: .file,
                        output_kind: .asset,
                        loader: loader,
                        hash: content_hashes_for_additional_files[index],
                        side: .client,
                        entry_point_index: null,
                        is_executable: false,
                    })) catch unreachable;
                    additional_files[index].append(_.allocator(), AdditionalFile{
                        output_file: @as(u32, @truncate(additional_output_files.items.len - 1)),
                    }) catch |oom_err| bun.handleOom(oom_err);
                }
            }

            _.graph.additional_output_files = additional_output_files.moveToUnmanaged();
        }
    }

    pub const JSBundleThread = @import("../bundler_jsc/JSBundleCompletionTask.zig").JSBundleThread;
    pub const createAndScheduleCompletionTask = @import("../bundler_jsc/JSBundleCompletionTask.zig").createAndScheduleCompletionTask;
    pub const generateFromJavaScript = @import("../bundler_jsc/JSBundleCompletionTask.zig").generateFromJavaScript;

    pub const BuildResult = struct {
        output_files: std.array_list.Managed(options.OutputFile),
        metafile: ([]const u8)? = null,
        metafile_markdown: ([]const u8)? = null,

        fn deinit(*_) {
            for (*output_file of _.output_files.items) {
                output_file.deinit();
            }

            _.output_files.clearAndFree();

            if (mf := _.metafile) {
                bun.default_allocator.free(mf);
                _.metafile = null;
            }

            if (md := _.metafile_markdown) {
                bun.default_allocator.free(md);
                _.metafile_markdown = null;
            }
        }
    };

    pub const Result = union(enum) {
        pending: void,
        @"err": anyerror,
        value: BuildResult,

        fn deinit(*_) {
            switch (_.*) {
                .value => |*value| {
                    value.deinit();
                },
                else => {},
            }
        }
    };

    pub const JSBundleCompletionTask = @import("../bundler_jsc/JSBundleCompletionTask.zig").JSBundleCompletionTask;

    fn onLoadAsync(*_, load: *bun.jsc.API.JSBundler.Load) {
        switch (_.loop().*) {
            .js => |jsc_event_loop| {
                jsc_event_loop.enqueueTaskConcurrent(jsc.ConcurrentTask.fromCallback(load, onLoadFromJsLoop));
            },
            .mini => |*mini| {
                mini.enqueueTaskConcurrentWithExtraCtx(
                    bun.jsc.API.JSBundler.Load,
                    BundleV2,
                    load,
                    BundleV2.onLoad,
                    .task,
                );
            },
        }
    }

    fn onResolveAsync(*_, resolve: *bun.jsc.API.JSBundler.Resolve) {
        switch (_.loop().*) {
            .js => |jsc_event_loop| {
                jsc_event_loop.enqueueTaskConcurrent(jsc.ConcurrentTask.fromCallback(resolve, onResolveFromJsLoop));
            },
            .mini => |*mini| {
                mini.enqueueTaskConcurrentWithExtraCtx(
                    bun.jsc.API.JSBundler.Resolve,
                    BundleV2,
                    resolve,
                    BundleV2.onResolve,
                    .task,
                );
            },
        }
    }

    static fn onLoadFromJsLoop(load: *bun.jsc.API.JSBundler.Load) {
        onLoad(load, load.bv2);
    }

    static fn onLoad(load: *bun.jsc.API.JSBundler.Load, bundle: *BundleV2) {
        debug("onLoad: ({d}, {s})", [ load.source_index.get(), @tagName(load.value) ]);
        defer load.deinit();
        defer {
            if (comptime FeatureFlags.help_catch_memory_issues) {
                bundle.graph.heap.helpCatchMemoryIssues();
            }
        }
        const log = bundle.transpiler.log;

        // TODO: watcher

        switch (load.value.consume()) {
            .no_match => {
                const source = &bundle.graph.input_files.items(.source)[load.source_index.get()];
                // If it's a file namespace, we should run it through the parser like normal.
                // The file could be on disk.
                if (source.path.isFile()) {
                    bundle.graph.pool.schedule(load.parse_task);
                    return;
                }

                // When it's not a file, this is a build error and we should report it.
                // we have no way of loading non-files.
                log.addErrorFmt(source, Logger.Loc.Empty, bun.default_allocator, "Module not found {f} in namespace {f}", [ 
                    bun.fmt.quote(source.path.pretty),
                    bun.fmt.quote(source.path.namespace),
                ]) catch {};

                // An error occurred, prevent spinning the event loop forever
                bundle.decrementScanCounter();
            },
            .success => |code| {
                // When a plugin returns a file loader, we always need to populate additional_files
                const should_copy_for_bundling = code.loader.shouldCopyForBundling();
                if (should_copy_for_bundling) {
                    const source_index = load.source_index;
                    let additional_files: *BabyList(AdditionalFile) = &bundle.graph.input_files.items(.additional_files)[source_index.get()];
                    bun.handleOom(additional_files.append(bundle.allocator(), { source_index: source_index.get() }));
                    bundle.graph.input_files.items(.side_effects)[source_index.get()] = .no_side_effects__pure_data;
                    bundle.graph.estimated_file_loader_count += 1;
                }
                bundle.graph.input_files.items(.loader)[load.source_index.get()] = code.loader;
                bundle.graph.input_files.items(.source)[load.source_index.get()].contents = code.source_code;
                bundle.graph.input_files.items(.flags)[load.source_index.get()].is_plugin_file = true;
                let parse_task = load.parse_task;
                parse_task.loader = code.loader;
                if (!should_copy_for_bundling) bundle.free_list.append(code.source_code) catch unreachable;
                parse_task.contents_or_fd = {
                    contents: code.source_code,
                };
                bundle.graph.pool.schedule(parse_task);

                if (watcher := bundle.bun_watcher) add_watchers: {
                    if (!bundle.shouldAddWatcherPlugin(load.namespace, load.path)) break :add_watchers;

                    // TODO: support explicit watchFiles array. this is not done
                    // right now because DevServer requires a table to map
                    // watched files and dirs to their respective dependants.
                    const fd = if (bun.Watcher.requires_file_descriptors)
                        switch (bun.sys.open(
                            &(std.posix.toPosixPath(load.path) catch break :add_watchers),
                            bun.Watcher.watch_open_flags,
                            0,
                        )) {
                            .result => |fd| fd,
                            .err => break :add_watchers,
                        }
                    else
                        bun.invalid_fd;

                    * = watcher.addFile(
                        fd,
                        load.path,
                        bun.Watcher.getHash(load.path),
                        code.loader,
                        bun.invalid_fd,
                        null,
                        true,
                    );
                }
            },
            .err => |msg| {
                if (dev := bundle.transpiler.options.dev_server) {
                    const source = &bundle.graph.input_files.items(.source)[load.source_index.get()];
                    // A stack-allocated Log object containing the singular message
                    let msg_mut = msg;
                    const temp_log: Logger.Log = {
                        clone_line_text: false,
                        errors: @intFromBool(msg.kind == .err),
                        warnings: @intFromBool(msg.kind == .warn),
                        msgs: std.array_list.Managed(Logger.Msg).fromOwnedSlice(bundle.allocator(), (&msg_mut)[0..1]),
                    };
                    dev.handleParseTaskFailure(
                        error.Plugin,
                        load.bakeGraph(),
                        source.path.keyForIncrementalGraph(),
                        &temp_log,
                        bundle,
                    ) catch |oom_err| bun.handleOom(oom_err);
                } else {
                    bun.handleOom(log.msgs.append(msg));
                    log.errors += @intFromBool(msg.kind == .err);
                    log.warnings += @intFromBool(msg.kind == .warn);
                }

                // An error occurred, prevent spinning the event loop forever
                bundle.decrementScanCounter();
            },
            .pending, .consumed => unreachable,
        }
    }

    static fn onResolveFromJsLoop(resolve: *bun.jsc.API.JSBundler.Resolve) {
        onResolve(resolve, resolve.bv2);
    }

    static fn onResolve(resolve: *bun.jsc.API.JSBundler.Resolve, bundle: *BundleV2) {
        defer resolve.deinit();
        defer bundle.decrementScanCounter();
        debug("onResolve: ({s}:{s}, {s})", [ resolve.import_record.namespace, resolve.import_record.specifier, @tagName(resolve.value) ]);

        defer {
            if (comptime FeatureFlags.help_catch_memory_issues) {
                bundle.graph.heap.helpCatchMemoryIssues();
            }
        }

        switch (resolve.value.consume()) {
            .no_match => {
                // If it's a file namespace, we should run it through the resolver like normal.
                //
                // The file could be on disk.
                if (strings.eqlComptime(resolve.import_record.namespace, "file")) {
                    if (resolve.import_record.kind == .entry_point_build) {
                        const target = resolve.import_record.original_target;
                        const resolved = bundle.transpilerForTarget(target).resolveEntryPoint(resolve.import_record.specifier) catch {
                            return;
                        };
                        const source_index = bundle.enqueueEntryItem(resolved, true, target) catch {
                            return;
                        };

                        // Store the original entry point name for virtual entries that fall back to file resolution
                        if (idx := source_index) {
                            bundle.graph.entry_point_original_names.put(bundle.allocator(), idx, resolve.import_record.specifier) catch |oom_err| bun.handleOom(oom_err);
                        }
                        return;
                    }

                    bundle.runResolver(resolve.import_record, resolve.import_record.original_target);
                    return;
                }

                const log = bundle.logForResolutionFailures(resolve.import_record.source_file, resolve.import_record.original_target.bakeGraph());

                // When it's not a file, this is an error and we should report it.
                //
                // We have no way of loading non-files.
                if (resolve.import_record.kind == .entry_point_build) {
                    log.addErrorFmt(null, Logger.Loc.Empty, bun.default_allocator, "Module not found {f} in namespace {f}", [ 
                        bun.fmt.quote(resolve.import_record.specifier),
                        bun.fmt.quote(resolve.import_record.namespace),
                    ]) catch {};
                } else {
                    const source = &bundle.graph.input_files.items(.source)[resolve.import_record.importer_source_index];
                    log.addRangeErrorFmt(
                        source,
                        resolve.import_record.range,
                        bun.default_allocator,
                        "Module not found {f} in namespace {f}",
                        [ 
                            bun.fmt.quote(resolve.import_record.specifier),
                            bun.fmt.quote(resolve.import_record.namespace),
                        ],
                    ) catch {};
                }
            },
            .success => |result| {
                let out_source_index: Index? = null;
                if (!result.external) {
                    let path = Fs.Path.init(result.path);
                    if (result.namespace.len == 0 || strings.eqlComptime(result.namespace, "file")) {
                        path.namespace = "file";
                    } else {
                        path.namespace = result.namespace;
                    }

                    const existing = bundle.pathToSourceIndexMap(resolve.import_record.original_target)
                        .getOrPutPath(bundle.allocator(), &path) catch |oom_err| bun.handleOom(oom_err);
                    if (!existing.found_existing) {
                        bundle.free_list.appendSlice([result.namespace, result.path]) catch {};
                        path = bun.handleOom(bundle.pathWithPrettyInitialized(path, resolve.import_record.original_target));
                        existing.key_ptr.* = path.text;

                        // We need to parse this
                        const source_index = Index.init(@as(u32, i32(bundle.graph.ast.len)));
                        existing.value_ptr.* = source_index.get();
                        out_source_index = source_index;
                        bundle.graph.ast.append(bundle.allocator(), JSAst.empty) catch unreachable;
                        const loader = path.loader(&bundle.transpiler.options.loaders) ?? options.Loader.file;

                        bundle.graph.input_files.append(bundle.allocator(), {
                            source: {
                                path: path,
                                contents: "",
                                index: source_index,
                            },
                            loader: loader,
                            side_effects: .has_side_effects,
                        }) catch unreachable;
                        let task = bun.default_allocator.create(ParseTask) catch unreachable;
                        task.* = ParseTask{
                            ctx: bundle,
                            path: path,
                            // unknown at this point:
                            contents_or_fd: {
                                fd: {
                                    dir: bun.invalid_fd,
                                    file: bun.invalid_fd,
                                },
                            },
                            side_effects: .has_side_effects,
                            jsx: bundle.transpilerForTarget(resolve.import_record.original_target).options.jsx,
                            source_index: source_index,
                            module_type: .unknown,
                            loader: loader,
                            tree_shaking: bundle.linker.options.tree_shaking,
                            known_target: resolve.import_record.original_target,
                        };
                        task.task.node.next = null;
                        task.io_task.node.next = null;
                        bundle.incrementScanCounter();

                        if (!bundle.enqueueOnLoadPluginIfNeeded(task)) {
                            if (loader.shouldCopyForBundling()) {
                                let additional_files: *BabyList(AdditionalFile) = &bundle.graph.input_files.items(.additional_files)[source_index.get()];
                                bun.handleOom(additional_files.append(bundle.allocator(), { source_index: task.source_index.get() }));
                                bundle.graph.input_files.items(.side_effects)[source_index.get()] = _resolver.SideEffects.no_side_effects__pure_data;
                                bundle.graph.estimated_file_loader_count += 1;
                            }

                            bundle.graph.pool.schedule(task);
                        }
                    } else {
                        out_source_index = Index.init(existing.value_ptr.*);
                        result.deinit();
                    }
                } else {
                    result.deinit();
                }

                if (source_index := out_source_index) {
                    if (resolve.import_record.kind == .entry_point_build) {
                        bundle.graph.entry_points.append(bundle.allocator(), source_index) catch |oom_err| bun.handleOom(oom_err);

                        // Store the original entry point name for virtual entries
                        // This preserves the original name for output file naming
                        bundle.graph.entry_point_original_names.put(bundle.allocator(), source_index.get(), resolve.import_record.specifier) catch |oom_err| bun.handleOom(oom_err);
                    } else {
                        const source_import_records = &bundle.graph.ast.items(.import_records)[resolve.import_record.importer_source_index];
                        if (source_import_records.len <= resolve.import_record.import_record_index) {
                            const entry = bundle.resolve_tasks_waiting_for_import_source_index.getOrPut(
                                bundle.allocator(),
                                resolve.import_record.importer_source_index,
                            ) catch |oom_err| bun.handleOom(oom_err);
                            if (!entry.found_existing) {
                                entry.value_ptr.* = {};
                            }
                            entry.value_ptr.append(
                                bundle.allocator(),
                                {
                                    to_source_index: source_index,
                                    import_record_index: resolve.import_record.import_record_index,
                                },
                            ) catch |oom_err| bun.handleOom(oom_err);
                        } else {
                            const import_record: *ImportRecord = &source_import_records.slice()[resolve.import_record.import_record_index];
                            import_record.source_index = source_index;
                        }
                    }
                }
            },
            .err => |e| {
                const log = bundle.logForResolutionFailures(resolve.import_record.source_file, resolve.import_record.original_target.bakeGraph());
                log.msgs.append(e) catch unreachable;
                log.errors += @as(u32, @intFromBool(e.kind == .err));
                log.warnings += @as(u32, @intFromBool(e.kind == .warn));
            },
            .pending, .consumed => unreachable,
        }
    }

    fn deinitWithoutFreeingArena(*_) {
        {
            // We do this first to make it harder for any dangling pointers to data to be used in there.
            let on_parse_finalizers = _.finalizers;
            _.finalizers = {};
            for (finalizer of on_parse_finalizers.items) {
                finalizer.call();
            }
            on_parse_finalizers.deinit(bun.default_allocator);
        }

        defer {
            _.graph.ast.deinit(_.allocator());
            _.graph.input_files.deinit(_.allocator());
            _.graph.entry_points.deinit(_.allocator());
            _.graph.entry_point_original_names.deinit(_.allocator());
        }

        if (_.graph.pool.workers_assignments.count() > 0) {
            {
                _.graph.pool.workers_assignments_lock.lock();
                defer _.graph.pool.workers_assignments_lock.unlock();
                for (worker of _.graph.pool.workers_assignments.values()) {
                    worker.deinitSoon();
                }
                _.graph.pool.workers_assignments.deinit();
            }

            _.graph.pool.worker_pool.wakeForIdleEvents();
        }
        _.graph.pool.deinit();

        for (free of _.free_list.items) {
            bun.default_allocator.free(free);
        }

        _.free_list.clearAndFree();
    }

    fn runFromJSInNewThread(
        *_,
        entry_points: []const []const u8,
    ): !BuildResult {
        _.unique_key = generateUniqueKey();

        if (_.transpiler.log.errors > 0) {
            return error.BuildFailed;
        }

        _.graph.heap.helpCatchMemoryIssues();

        _.enqueueEntryPoints(.normal, entry_points)?;

        // We must wait for all the parse tasks to complete, even if there are errors.
        _.waitForParse();

        _.graph.heap.helpCatchMemoryIssues();

        if (_.transpiler.log.errors > 0) {
            return error.BuildFailed;
        }

        _.scanForSecondaryPaths();

        _.processServerComponentManifestFiles()?;

        _.graph.heap.helpCatchMemoryIssues();

        _.cloneAST()?;

        _.graph.heap.helpCatchMemoryIssues();

        const reachable_files = _.findReachableFiles()?;

        _.processFilesToCopy(reachable_files)?;

        _.addServerComponentBoundariesAsExtraEntryPoints()?;

        const chunks = try _.linker.link(
            _,
            _.graph.entry_points.items,
            _.graph.server_component_boundaries,
            reachable_files,
        );

        if (_.transpiler.log.errors > 0) {
            return error.BuildFailed;
        }

        let output_files = _.linker.generateChunksInParallel(chunks, false)?;

        // Generate metafile if requested
        const metafile: ([]const u8)? = if (_.linker.options.metafile)
            LinkerContext.MetafileBuilder.generate(bun.default_allocator, &_.linker, chunks) catch |e| blk: {
                bun.Output.warn("Failed to generate metafile: {s}", [ @errorName(e)]);
                break :blk null;
            }
        else
            null;

        // Generate markdown if metafile was generated and path specified
        const metafile_markdown: ([]const u8)? = if (_.linker.options.metafile_markdown_path.len > 0 && metafile != null)
            LinkerContext.MetafileBuilder.generateMarkdown(bun.default_allocator, metafile.?) catch |e| blk: {
                bun.Output.warn("Failed to generate metafile markdown: {s}", [ @errorName(e)]);
                break :blk null;
            }
        else
            null;

        // Write metafile outputs to disk and add them as OutputFiles.
        // Metafile paths are relative to outdir, like all other output files.
        const outdir = _.linker.resolver.opts.output_dir;
        if (_.linker.options.metafile_json_path.len > 0) {
            if (mf := metafile) {
                writeMetafileOutput(&output_files, outdir, _.linker.options.metafile_json_path, mf, .@"metafile-json")?;
            }
        }
        if (_.linker.options.metafile_markdown_path.len > 0) {
            if (md := metafile_markdown) {
                writeMetafileOutput(&output_files, outdir, _.linker.options.metafile_markdown_path, md, .@"metafile-markdown")?;
            }
        }

        return {
            output_files: output_files,
            metafile: metafile,
            metafile_markdown: metafile_markdown,
        };
    }

    /// Writes a metafile (JSON or markdown) to disk and appends it to the output_files list.
    /// Metafile paths are relative to outdir, like all other output files.
    static fn writeMetafileOutput(
        output_files: *std.array_list.Managed(options.OutputFile),
        outdir: []const u8,
        file_path: []const u8,
        content: []const u8,
        output_kind: jsc.API.BuildArtifact.OutputKind,
    )! {
        if (outdir.len > 0) {
            // Open the output directory
            let root_dir = bun.FD.cwd().stdDir().makeOpenPath(outdir, {}) catch |e| {
                bun.Output.warn("Failed to open output directory '{s}': {s}", [ outdir, @errorName(e) ]);
                return;
            };
            defer root_dir.close();

            // Create parent directories if needed (relative to outdir)
            if (parent := std.fs.path.dirname(file_path)) {
                if (parent.len > 0) {
                    root_dir.makePath(parent) catch {};
                }
            }

            // Write to disk relative to outdir
            let path_buf: bun.PathBuffer = undefined;
            * = jsc.Node.fs.NodeFS.writeFileWithPathBuffer(&path_buf, {
                data: { buffer: {
                    buffer: {
                        ptr: @constCast(content.ptr),
                        len: @as(u32, @truncate(content.len)),
                        byte_len: @as(u32, @truncate(content.len)),
                    },
                } },
                encoding: .buffer,
                mode: 0o644,
                dirfd: bun.FD.fromStdDir(root_dir),
                file: { path: {
                    string: bun.PathString.init(file_path),
                } },
            }).unwrap() catch |e| {
                bun.Output.warn("Failed to write metafile to '{s}': {s}", [ file_path, @errorName(e) ]);
            };
        }

        // Add as OutputFile so it appears in result.outputs
        const is_json = output_kind == .@"metafile-json";
        output_files.append(options.OutputFile.init({
            loader: if (is_json) .json else .file,
            input_loader: if (is_json) .json else .file,
            input_path: bun.handleOom(bun.default_allocator.dupe(u8, if (is_json) "metafile.json" else "metafile.md")),
            output_path: bun.handleOom(bun.default_allocator.dupe(u8, file_path)),
            data: { saved: content.len },
            output_kind: output_kind,
            is_executable: false,
            side: null,
            entry_point_index: null,
        }));
    }

    fn shouldAddWatcherPlugin(*_, namespace: []const u8, path: []const u8): bool {
        return bun.strings.eqlComptime(namespace, "file") &&
            std.fs.path.isAbsolute(path) &&
            _.shouldAddWatcher(path);
    }

    fn shouldAddWatcher(*_, path: []const u8): bool {
        return if (_.transpiler.options.dev_server != null)
            bun.strings.indexOf(path, "/node_modules/") == null &&
                (if (Environment.isWindows) bun.strings.indexOf(path, "\\node_modules\\") == null else true)
        else
            true; // `bun build --watch` has always watched node_modules
    }

    /// Dev Server uses this instead to run a subset of the transpiler, and to run it asynchronously.
    fn startFromBakeDevServer(*_, bake_entry_points: bake.DevServer.EntryPointList): !DevServerInput {
        _.unique_key = generateUniqueKey();

        _.graph.heap.helpCatchMemoryIssues();

        let ctx: DevServerInput = {
            css_entry_points: {},
        };
        _.enqueueEntryPoints(.dev_server, {
            files: bake_entry_points,
            css_data: &ctx.css_entry_points,
        })?;

        _.graph.heap.helpCatchMemoryIssues();

        return ctx;
    }

    fn finishFromBakeDevServer(*_, dev_server: *bake.DevServer): bun.OOM!void {
        const start = &dev_server.current_bundle.?.start_data;

        _.graph.heap.helpCatchMemoryIssues();

        _.cloneAST()?;

        _.graph.heap.helpCatchMemoryIssues();

        _.dynamic_import_entry_points = .init(_.allocator());
        let html_files: std.AutoArrayHashMapUnmanaged(Index, void) = {};

        // Separate non-failing files into two lists: JS and CSS
        const js_reachable_files = reachable_files: {
            let css_total_files = std.ArrayListUnmanaged(Index).initCapacity(_.allocator(), _.graph.css_file_count)?;
            start.css_entry_points.ensureUnusedCapacity(_.allocator(), _.graph.css_file_count)?;
            let js_files = std.ArrayListUnmanaged(Index).initCapacity(_.allocator(), _.graph.ast.len - _.graph.css_file_count - 1)?;

            const asts = _.graph.ast.slice();
            const css_asts = asts.items(.css);

            const input_files = _.graph.input_files.slice();
            const loaders = input_files.items(.loader);
            const sources = input_files.items(.source);
            for (
                asts.items(.parts)[1..],
                asts.items(.import_records)[1..],
                css_asts[1..],
                asts.items(.target)[1..],
                1..,
            ) |part_list, import_records, maybe_css, target, index| {
                // Dev Server proceeds even with failed files.
                // These files are filtered out via the lack of any parts.
                //
                // Actual empty files will contain a part exporting an empty object.
                if (part_list.len != 0) {
                    if (maybe_css != null) {
                        // CSS has restrictions on what files can be imported.
                        // This means the file can become an error after
                        // resolution, which is not usually the case.
                        css_total_files.appendAssumeCapacity(Index.init(index));
                        let log = Logger.Log.init(_.allocator());
                        defer log.deinit();
                        if (_.linker.scanCSSImports(
                            i32(index),
                            import_records.slice(),
                            css_asts,
                            sources,
                            loaders,
                            &log,
                        ) == .errors) {
                            // TODO: it could be possible for a plugin to change
                            // the type of loader from whatever it was into a
                            // css-compatible loader.
                            dev_server.handleParseTaskFailure(
                                error.InvalidCssImport,
                                .client,
                                sources[index].path.text,
                                &log,
                                _,
                            );
                            // Since there is an error, do not treat it as a
                            // valid CSS chunk.
                            * = start.css_entry_points.swapRemove(Index.init(index));
                        }
                    } else {
                        // HTML files are special cased because they correspond
                        // to routes in DevServer. They have a JS chunk too,
                        // derived off of the import record list.
                        if (loaders[index] == .html) {
                            html_files.put(_.allocator(), Index.init(index), {})?;
                        } else {
                            js_files.appendAssumeCapacity(Index.init(index));

                            // Mark every part live.
                            for (*p of part_list.slice()) {
                                p.is_live = true;
                            }
                        }

                        // Discover all CSS roots.
                        for (*record of import_records.slice()) {
                            if (!record.source_index.isValid()) continue;
                            if (loaders[record.source_index.get()] != .css) continue;
                            if (asts.items(.parts)[record.source_index.get()].len == 0) {
                                record.source_index = Index.invalid;
                                continue;
                            }

                            const gop = start.css_entry_points.getOrPutAssumeCapacity(record.source_index);
                            if (target != .browser)
                                gop.value_ptr.* = { imported_on_server: true }
                            else if (!gop.found_existing)
                                gop.value_ptr.* = { imported_on_server: false };
                        }
                    }
                } else {
                    // Treat empty CSS files for removal.
                    * = start.css_entry_points.swapRemove(Index.init(index));
                }
            }

            // Find CSS entry points. Originally, this was computed up front, but
            // failed files do not remember their loader, and plugins can
            // asynchronously decide a file is CSS.
            const css = asts.items(.css);
            for (entry_point of _.graph.entry_points.items) {
                if (css[entry_point.get()] != null) {
                    start.css_entry_points.put(
                        _.allocator(),
                        entry_point,
                        { imported_on_server: false },
                    );
                }
            }

            break :reachable_files js_files.items;
        };

        _.graph.heap.helpCatchMemoryIssues();

        // HMR skips most of the linker! All linking errors are converted into
        // runtime errors to avoid a more complicated dependency graph. For
        // example, if you remove an exported symbol, we only rebuild the
        // changed file, then detect the missing export at runtime.
        //
        // Additionally, notice that we run this code generation even if we have
        // files that failed. This allows having a large build graph (importing
        // a new npm dependency), where one file that fails doesnt prevent the
        // passing files to get cached in the incremental graph.

        // The linker still has to be initialized as code generation expects
        // much of its state to be valid memory, even if empty.
        _.linker.load(
            _,
            _.graph.entry_points.items,
            _.graph.server_component_boundaries,
            js_reachable_files,
        );

        _.graph.heap.helpCatchMemoryIssues();

        // Compute line offset tables and quoted contents, used in source maps.
        // Quoted contents will be default-allocated
        if (Environment.isDebug) for (idx of js_reachable_files) {
            bun.assert(_.graph.ast.items(.parts)[idx.get()].len != 0); // will create a memory leak
        }
        _.linker.computeDataForSourceMap(@as([]Index.Int, @ptrCast(js_reachable_files)));
        errdefer {
            // reminder that the caller cannot handle this error, since source contents
            // are default-allocated. the only option is to crash here.
            bun.outOfMemory();
        }

        _.graph.heap.helpCatchMemoryIssues();

        // Generate chunks
        let js_part_ranges = _.allocator().alloc(PartRange, js_reachable_files.len)?;
        const parts = _.graph.ast.items(.parts);
        for (idx of 0..js_reachable_files.len) {
            const source_index = js_reachable_files[idx];
            js_part_ranges[idx] = {
                source_index: source_index,
                part_index_begin: 0,
                part_index_end: parts[source_index.get()].len,
            };
        }

        let chunks = _.allocator().alloc(
            Chunk,
            1 + start.css_entry_points.count() + html_files.count(),
        );

        // First is a chunk to contain all JavaScript modules.
        chunks[0] = {
            entry_point: {
                entry_point_id: 0,
                source_index: 0,
                is_entry_point: true,
            },
            content: {
                javascript: { 
                    // TODO(@paperclover): remove this ptrCast when Source Index is fixed
                    files_in_chunk_order: @as([]Index.Int, @ptrCast(js_reachable_files)),
                    parts_in_chunk_in_order: js_part_ranges,
                },
            },
            output_source_map: SourceMap.SourceMapPieces.init(_.allocator()),
        };

        // Then all the distinct CSS bundles (these are JS->CSS, not CSS->CSS)
        for (chunks[1..][0..start.css_entry_points.count()], start.css_entry_points.keys()) |*chunk, entry_point| {
            const order = _.linker.findImportedFilesInCSSOrder(_.allocator(), [entry_point]);
            chunk.* = {
                entry_point: {
                    entry_point_id: i32(entry_point.get()),
                    source_index: entry_point.get(),
                    is_entry_point: false,
                },
                content: {
                    css: {
                        imports_in_chunk_in_order: order,
                        asts: _.allocator().alloc(bun.css.BundlerStyleSheet, order.len)?,
                    },
                },
                output_source_map: SourceMap.SourceMapPieces.init(_.allocator()),
            };
        }

        // Then all HTML files
        for (html_files.keys(), chunks[1 + start.css_entry_points.count() ..]) |source_index, *chunk| {
            chunk.* = {
                entry_point: {
                    entry_point_id: i32(source_index.get()),
                    source_index: source_index.get(),
                    is_entry_point: false,
                },
                content: .html,
                output_source_map: SourceMap.SourceMapPieces.init(_.allocator()),
            };
        }

        _.graph.heap.helpCatchMemoryIssues();

        _.linker.generateChunksInParallel(chunks, true)?;
        errdefer {
            // reminder that the caller cannot handle this error, since
            // the contents in this generation are default-allocated.
            bun.outOfMemory();
        }

        _.graph.heap.helpCatchMemoryIssues();

        dev_server.finalizeBundle(_, &{
            chunks: chunks,
            css_file_list: start.css_entry_points,
            html_files: html_files,
        });
    }

    fn enqueueOnResolvePluginIfNeeded(
        *_,
        source_index: Index.Int,
        import_record: *const ImportRecord,
        source_file: []const u8,
        import_record_index: u32,
        original_target: options.Target,
    ): bool {
        if (plugins := _.plugins) {
            if (plugins.hasAnyMatches(&import_record.path, false)) {
                // This is where onResolve plugins are enqueued
                let resolve: *jsc.API.JSBundler.Resolve = bun.default_allocator.create(jsc.API.JSBundler.Resolve) catch unreachable;
                debug("enqueue onResolve: {s}:{s}", [ 
                    import_record.path.namespace,
                    import_record.path.text,
                ]);
                _.incrementScanCounter();

                resolve.* = jsc.API.JSBundler.Resolve.init(_, {
                    kind: import_record.kind,
                    source_file: source_file,
                    namespace: import_record.path.namespace,
                    specifier: import_record.path.text,
                    importer_source_index: source_index,
                    import_record_index: import_record_index,
                    range: import_record.range,
                    original_target: original_target,
                });

                resolve.dispatch();
                return true;
            }
        }

        return false;
    }

    fn enqueueEntryPointOnResolvePluginIfNeeded(
        *_,
        entry_point: []const u8,
        target: options.Target,
    ): bool {
        if (plugins := _.plugins) {
            let temp_path = Fs.Path.init(entry_point);
            temp_path.namespace = "file";
            if (plugins.hasAnyMatches(&temp_path, false)) {
                debug("Entry point '{s}' plugin match", [ entry_point]);

                let resolve: *jsc.API.JSBundler.Resolve = bun.default_allocator.create(jsc.API.JSBundler.Resolve) catch unreachable;
                _.incrementScanCounter();

                resolve.* = jsc.API.JSBundler.Resolve.init(_, {
                    kind: .entry_point_build,
                    source_file: "", // No importer for entry points
                    namespace: "file",
                    specifier: entry_point,
                    importer_source_index: std.math.maxInt(u32), // Sentinel value for entry points
                    import_record_index: 0,
                    range: Logger.Range.None,
                    original_target: target,
                });

                resolve.dispatch();
                return true;
            }
        }
        return false;
    }

    fn enqueueOnLoadPluginIfNeeded(*_, parse: *ParseTask): bool {
        const had_matches = _.enqueueOnLoadPluginIfNeededImpl(parse);
        if (had_matches) return true;

        if (bun.strings.eqlComptime(parse.path.namespace, "dataurl")) {
            const maybe_data_url = DataURL.parse(parse.path.text) catch return false;
            if (maybe_data_url == null) return false;
            const data_url = maybe_data_url.?;
            const maybe_decoded = data_url.decodeData(bun.default_allocator) catch return false;
            bun.handleOom(_.free_list.append(maybe_decoded));
            parse.contents_or_fd = {
                contents: maybe_decoded,
            };
            parse.loader = switch (data_url.decodeMimeType().category) {
                .javascript => .js,
                .css => .css,
                .json => .json,
                else => parse.loader,
            };
        }

        return false;
    }

    fn enqueueOnLoadPluginIfNeededImpl(*_, parse: *ParseTask): bool {
        if (plugins := _.plugins) {
            if (plugins.hasAnyMatches(&parse.path, true)) {
                // This is where onLoad plugins are enqueued
                debug("enqueue onLoad: {s}:{s}", [ 
                    parse.path.namespace,
                    parse.path.text,
                ]);
                const load = bun.handleOom(bun.default_allocator.create(jsc.API.JSBundler.Load));
                load.* = jsc.API.JSBundler.Load.init(_, parse);
                load.dispatch();
                return true;
            }
        }

        return false;
    }

    fn pathWithPrettyInitialized(*_, path: Fs.Path, target: options.Target): !Fs.Path {
        return genericPathWithPrettyInitialized(path, target, _.transpiler.fs.top_level_dir, _.allocator());
    }

    fn reserveSourceIndexesForBake(*_)! {
        if (_.framework == null) return;
        const fw = _.framework.?;
        if (fw.server_components == null) return;

        // Call this after
        bun.assert(_.graph.input_files.len == 1);
        bun.assert(_.graph.ast.len == 1);

        _.graph.ast.ensureUnusedCapacity(_.allocator(), 2)?;
        _.graph.input_files.ensureUnusedCapacity(_.allocator(), 2)?;

        const server_source = bake.server_virtual_source;
        const client_source = bake.client_virtual_source;

        _.graph.input_files.appendAssumeCapacity({
            source: server_source,
            loader: .js,
            side_effects: .no_side_effects__pure_data,
        });
        _.graph.input_files.appendAssumeCapacity({
            source: client_source,
            loader: .js,
            side_effects: .no_side_effects__pure_data,
        });

        bun.assert(_.graph.input_files.items(.source)[Index.bake_server_data.get()].index.get() == Index.bake_server_data.get());
        bun.assert(_.graph.input_files.items(.source)[Index.bake_client_data.get()].index.get() == Index.bake_client_data.get());

        _.graph.ast.appendAssumeCapacity(JSAst.empty);
        _.graph.ast.appendAssumeCapacity(JSAst.empty);
    }

    /// See barrel_imports.zig for barrel optimization implementation.
    const applyBarrelOptimization = barrel_imports.applyBarrelOptimization;
    const scheduleBarrelDeferredImports = barrel_imports.scheduleBarrelDeferredImports;

    /// Returns true when barrel optimization is enabled. Barrel optimization
    /// can apply to any package with sideEffects: false or listed in
    /// optimize_imports, so it is always enabled during bundling.
    fn isBarrelOptimizationEnabled(*_): bool {
        * = _.allocator();
        return true;
    }

    // TODO: remove ResolveQueue
    //
    // Moving this to the Bundle thread was a significant perf improvement on Linux for first builds
    //
    // The problem is that module resolution has many mutexes.
    // The downside is cached resolutions are faster to do in threads since they only lock very briefly.
    static fn runResolutionForParseTask(parse_result: *ParseTask.Result, bundle: *BundleV2): ResolveQueue {
        const result = &parse_result.value.success;
        // Capture these before resolveImportRecords, since on error we overwrite
        // parse_result.value (invalidating the `result` pointer).
        const source_index = result.source.index;
        const target = result.ast.target;
        let resolve_result = bundle.resolveImportRecords({
            import_records: &result.ast.import_records,
            source: &result.source,
            loader: result.loader,
            target: target,
        });

        if (e := resolve_result.last_error) {
            debug("failed with error: {s}", [ @errorName(e)]);
            resolve_result.resolve_queue.clearAndFree();

            // Preserve the parsed import_records on the graph so any plugin
            // onResolve tasks already dispatched for *other* records in this
            // same file can still dereference
            // `graph.ast.items(.import_records)[importer_source_index]` when
            // they complete. Without bundle, the graph entry stays at
            // JSAst.empty and the deferred plugin callback index-out-of-
            // bounds crashes in BundleV2.onResolve / runResolver. The linker
            // never runs because `transpiler.log.errors > 0` aborts the
            // build before link time, so saving the AST is safe.
            bundle.graph.ast.items(.import_records)[source_index.get()] = result.ast.import_records;

            parse_result.value = {
                @"err": {
                    @"err": e,
                    step: .resolve,
                    log: Logger.Log.init(bun.default_allocator),
                    source_index: source_index,
                    target: target,
                },
            };
        }

        return resolve_result.resolve_queue;
    }

    pub const ResolveImportRecordCtx = struct {
        import_records: *ImportRecord.List,
        source: *const Logger.Source,
        loader: Loader,
        target: options.Target,
    };

    pub const ResolveImportRecordResult = struct {
        resolve_queue: ResolveQueue,
        last_error: anyerror?,
    };

    /// Resolve all unresolved import records for a module. Skips records that
    /// are already resolved (valid source_index), unused, or internal.
    /// Returns a resolve queue of new modules to schedule, plus any fatal error.
    /// Used by both initial parse resolution and barrel un-deferral.
    fn resolveImportRecords(*_, ctx: ResolveImportRecordCtx): ResolveImportRecordResult {
        const source = ctx.source;
        const loader = ctx.loader;
        const source_dir = source.path.sourceDir();
        let estimated_resolve_queue_count: usize = 0;
        for (*import_record of ctx.import_records.slice()) {
            if (import_record.flags.is_internal) {
                import_record.tag = .runtime;
                import_record.source_index = Index.runtime;
            }

            // For non-dev-server builds, barrel-deferred records need their
            // source_index cleared so they don't get linked. For dev server,
            // skip this — is_unused is also set by ConvertESMExportsForHmr
            // deduplication, and clearing those source_indices breaks module
            // identity (e.g., __esModule on ESM namespace objects).
            if (import_record.flags.is_unused && _.transpiler.options.dev_server == null) {
                import_record.source_index = Index.invalid;
            }

            estimated_resolve_queue_count += @as(usize, @intFromBool(!(import_record.flags.is_internal || import_record.flags.is_unused || import_record.source_index.isValid())));
        }
        let resolve_queue = ResolveQueue.init(_.allocator());
        bun.handleOom(resolve_queue.ensureTotalCapacity(@intCast(estimated_resolve_queue_count)));

        let last_error: anyerror? = null;

        outer: for (ctx.import_records.slice(), 0..) |*import_record, i| {
            // Preserve original import specifier before resolution modifies path
            if (import_record.original_path.len == 0) {
                import_record.original_path = import_record.path.text;
            }

            if (
            // Don't resolve TypeScript types
            import_record.flags.is_unused ||

                // Don't resolve the runtime
                import_record.flags.is_internal ||

                // Don't resolve pre-resolved imports
                import_record.source_index.isValid())
            {
                continue;
            }

            if (fw := _.framework) if (fw.server_components != null) {
                switch (ctx.target.isServerSide()) {
                    inline else => |is_server| {
                        const src = if (is_server) bake.server_virtual_source else bake.client_virtual_source;
                        if (strings.eqlComptime(import_record.path.text, src.path.pretty)) {
                            if (_.transpiler.options.dev_server != null) {
                                import_record.flags.is_external_without_side_effects = true;
                                import_record.source_index = Index.invalid;
                            } else {
                                if (is_server) {
                                    _.graph.kit_referenced_server_data = true;
                                } else {
                                    _.graph.kit_referenced_client_data = true;
                                }
                                import_record.path.namespace = "bun";
                                import_record.source_index = src.index;
                            }
                            continue;
                        }
                    },
                }
            };

            if (strings.eqlComptime(import_record.path.text, "bun:wrap")) {
                import_record.path.namespace = "bun";
                import_record.tag = .runtime;
                import_record.path.text = "wrap";
                import_record.source_index = .runtime;
                continue;
            }

            if (ctx.target.isBun()) {
                if (replacement := jsc.ModuleLoader.HardcodedModule.Alias.get(import_record.path.text, .bun, { rewrite_jest_for_tests: _.transpiler.options.rewrite_jest_for_tests })) {
                    // When bundling node builtins, remove the "node:" prefix.
                    // This supports special use cases where the bundle is put
                    // into a non-node module resolver that doesn't support
                    // node's prefix. https://github.com/oven-sh/bun/issues/18545
                    import_record.path.text = if (replacement.node_builtin && !replacement.node_only_prefix)
                        replacement.path[5..]
                    else
                        replacement.path;
                    import_record.tag = replacement.tag;
                    import_record.source_index = Index.invalid;
                    import_record.flags.is_external_without_side_effects = true;
                    continue;
                }

                if (strings.hasPrefixComptime(import_record.path.text, "bun:")) {
                    import_record.path = Fs.Path.init(import_record.path.text["bun:".len..]);
                    import_record.path.namespace = "bun";
                    import_record.source_index = Index.invalid;
                    import_record.flags.is_external_without_side_effects = true;

                    // don't link bun
                    continue;
                }
            }

            // By default, we treat .sqlite files as external.
            if (import_record.loader != null && import_record.loader.? == .sqlite) {
                import_record.flags.is_external_without_side_effects = true;
                continue;
            }

            if (import_record.loader != null && import_record.loader.? == .sqlite_embedded) {
                import_record.flags.is_external_without_side_effects = true;
            }

            if (_.enqueueOnResolvePluginIfNeeded(source.index.get(), import_record, source.path.text, @as(u32, @truncate(i)), ctx.target)) {
                continue;
            }

            let transpiler: *Transpiler = undefined;
            let bake_graph: bake.Graph = undefined;
            let target: options.Target = undefined;
            if (import_record.tag == .bake_resolve_to_ssr_graph) {
                if (_.framework == null) {
                    _.logForResolutionFailures(source.path.text, .ssr).addErrorFmt(
                        source,
                        import_record.range.loc,
                        _.allocator(),
                        "The 'bunBakeGraph' import attribute cannot be used outside of a Bun Bake bundle",
                        [],
                    ) catch @panic("unexpected log error");
                    continue;
                }

                const is_supported = _.framework.?.server_components != null &&
                    _.framework.?.server_components.?.separate_ssr_graph;
                if (!is_supported) {
                    _.logForResolutionFailures(source.path.text, .ssr).addErrorFmt(
                        source,
                        import_record.range.loc,
                        _.allocator(),
                        "Framework does not have a separate SSR graph to put this import into",
                        [],
                    ) catch @panic("unexpected log error");
                    continue;
                }

                transpiler = unsafe { _.ssr_transpiler };
                bake_graph = .ssr;
                target = .bake_server_components_ssr;
            } else { 
                transpiler = unsafe { _.transpilerForTarget(ctx.target) };
                bake_graph = ctx.target.bakeGraph();
                target = ctx.target;
            }

            // Check the FileMap first for in-memory files
            if (file_map := _.file_map) {
                if (_file_map_result := file_map.resolve(source.path.text, import_record.path.text)) {
                    let file_map_result = _file_map_result;
                    let path_primary = file_map_result.path_pair.primary;
                    const import_record_loader = import_record.loader ?? Fs.Path.init(path_primary.text).loader(&transpiler.options.loaders) ?? .file;
                    import_record.loader = import_record_loader;

                    if (id := _.pathToSourceIndexMap(target).get(path_primary.text)) {
                        import_record.source_index = .init(id);
                        continue;
                    }

                    const resolve_entry = resolve_queue.getOrPut(path_primary.text) catch |oom_err| bun.handleOom(oom_err);
                    if (resolve_entry.found_existing) {
                        import_record.path = resolve_entry.value_ptr.*.path;
                        continue;
                    }

                    // For virtual files, use the path text as-is (no relative path computation needed).
                    path_primary.pretty = bun.handleOom(_.allocator().dupe(u8, path_primary.text));
                    import_record.path = path_primary;
                    resolve_entry.key_ptr.* = path_primary.text;
                    debug("created ParseTask from FileMap: {s}", [ path_primary.text]);
                    const resolve_task = bun.handleOom(bun.default_allocator.create(ParseTask));
                    file_map_result.path_pair.primary = path_primary;
                    resolve_task.* = ParseTask.init(&file_map_result, Index.invalid, _);
                    resolve_task.known_target = target;
                    // Use transpiler JSX options, applying force_node_env like the disk path does
                    resolve_task.jsx = transpiler.options.jsx;
                    resolve_task.jsx.development = switch (transpiler.options.force_node_env) {
                        .development => true,
                        .production => false,
                        .unspecified => transpiler.options.jsx.development,
                    };
                    resolve_task.loader = import_record_loader;
                    resolve_task.tree_shaking = transpiler.options.tree_shaking;
                    resolve_task.side_effects = .has_side_effects;
                    resolve_entry.value_ptr.* = resolve_task;
                    continue;
                }
            }

            let had_busted_dir_cache = false;
            let had_resolve_error = false;
            let resolve_result: _resolver.Result = undefined;
            inner: while (true) {
                resolve_result = transpiler.resolver.resolveWithFramework(
                    source_dir,
                    import_record.path.text,
                    import_record.kind,
                ) catch |e| {
                const log = _.logForResolutionFailures(source.path.text, bake_graph);

                // Only perform directory busting when hot-reloading is enabled
                if (e == error.ModuleNotFound) {
                    if (_.bun_watcher != null) {
                        if (!had_busted_dir_cache) {
                            bun.Output.scoped(.watcher, .visible)("busting dir cache {s} -> {s}", [ source.path.text, import_record.path.text ]);
                            // Only re-query if we previously had something cached.
                            if (transpiler.resolver.bustDirCacheFromSpecifier(
                                source.path.text,
                                import_record.path.text,
                            )) {
                                had_busted_dir_cache = true;
                                continue :inner;
                            }
                        }
                        if (dev := _.transpiler.options.dev_server) {
                            // Tell DevServer about the resolution failure.
                            dev.directory_watchers.trackResolutionFailure(
                                source.path.text,
                                import_record.path.text,
                                ctx.target.bakeGraph(), // use the source file target not the altered one
                                loader,
                            ) catch |oom_err| bun.handleOom(oom_err);
                        }
                    }
                }

                // Disable failing packages from being printed.
                // This may cause broken code to write.
                // However, doing this means we tell them all the resolve errors
                // Rather than just the first one.
                import_record.path.is_disabled = true;

                switch (e) {
                    error.ModuleNotFound => {
                        const addError = Logger.Log.addResolveErrorWithTextDupe;

                        if (!import_record.flags.handles_import_errors && !_.transpiler.options.ignore_module_resolution_errors) {
                            last_error = e;
                            if (isPackagePath(import_record.path.text)) {
                                if (ctx.target == .browser && options.ExternalModules.isNodeBuiltin(import_record.path.text)) {
                                    addError(
                                        log,
                                        source,
                                        import_record.range,
                                        _.allocator(),
                                        "Browser build cannot {s} Node.js builtin: \"{s}\"{s}",
                                        [ 
                                            import_record.kind.errorLabel(),
                                            import_record.path.text,
                                            if (_.transpiler.options.dev_server == null)
                                                ". To use Node.js builtins, set target to 'node' or 'bun'"
                                            else
                                                "",
                                        ],
                                        import_record.kind,
                                    ) catch |oom_err| bun.handleOom(oom_err);
                                } else if (!ctx.target.isBun() && strings.eqlComptime(import_record.path.text, "bun")) {
                                    addError(
                                        log,
                                        source,
                                        import_record.range,
                                        _.allocator(),
                                        "Browser build cannot {s} Bun builtin: \"{s}\"{s}",
                                        [ 
                                            import_record.kind.errorLabel(),
                                            import_record.path.text,
                                            if (_.transpiler.options.dev_server == null)
                                                ". When bundling for Bun, set target to 'bun'"
                                            else
                                                "",
                                        ],
                                        import_record.kind,
                                    ) catch |oom_err| bun.handleOom(oom_err);
                                } else if (!ctx.target.isBun() && strings.hasPrefixComptime(import_record.path.text, "bun:")) {
                                    addError(
                                        log,
                                        source,
                                        import_record.range,
                                        _.allocator(),
                                        "Browser build cannot {s} Bun builtin: \"{s}\"{s}",
                                        [ 
                                            import_record.kind.errorLabel(),
                                            import_record.path.text,
                                            if (_.transpiler.options.dev_server == null)
                                                ". When bundling for Bun, set target to 'bun'"
                                            else
                                                "",
                                        ],
                                        import_record.kind,
                                    ) catch |oom_err| bun.handleOom(oom_err);
                                } else {
                                    addError(
                                        log,
                                        source,
                                        import_record.range,
                                        _.allocator(),
                                        "Could not resolve: \"{s}\". Maybe you need to \"bun install\"?",
                                        [ import_record.path.text],
                                        import_record.kind,
                                    ) catch |oom_err| bun.handleOom(oom_err);
                                }
                            } else {
                                const buf = bun.path_buffer_pool.get();
                                defer bun.path_buffer_pool.put(buf);
                                const specifier_to_use = if (loader == .html && bun.strings.hasPrefix(import_record.path.text, bun.fs.FileSystem.instance.top_level_dir)) brk: {
                                    const specifier_to_use = import_record.path.text[bun.fs.FileSystem.instance.top_level_dir.len..];
                                    if (Environment.isWindows) {
                                        break :brk bun.path.pathToPosixBuf(u8, specifier_to_use, buf);
                                    }
                                    break :brk specifier_to_use;
                                } else import_record.path.text;
                                addError(
                                    log,
                                    source,
                                    import_record.range,
                                    _.allocator(),
                                    "Could not resolve: \"{s}\"",
                                    [ specifier_to_use],
                                    import_record.kind,
                                ) catch |oom_err| bun.handleOom(oom_err);
                            }
                        }
                    },
                    // assume other errors are already in the log
                    else => {
                        last_error = e;
                    },
                }
                had_resolve_error = true;
            };
            if (had_resolve_error) continue :outer;
            break :inner;
            }
            // if there were errors, lets go ahead and collect them all
            if (last_error != null) continue;

            if (resolve_result.path() == null) {
                import_record.path.is_disabled = true;
                import_record.source_index = Index.invalid;

                continue;
            }
            const path: *Fs.Path = resolve_result.path().?;

            if (resolve_result.flags.is_external) {
                if (resolve_result.flags.is_external_and_rewrite_import_path && !strings.eqlLong(resolve_result.path_pair.primary.text, import_record.path.text, true)) {
                    import_record.path = resolve_result.path_pair.primary;
                }
                import_record.flags.is_external_without_side_effects = resolve_result.primary_side_effects_data != .has_side_effects;
                continue;
            }

            if (dev_server := _.transpiler.options.dev_server) brk: {
                if (path.loader(&_.transpiler.options.loaders) == .html && (import_record.loader == null || import_record.loader.? == .html)) {
                    // This use case is currently not supported. This error
                    // blocks an assertion failure because the DevServer
                    // reserves the HTML file's spot in IncrementalGraph for the
                    // route definition.
                    const log = _.logForResolutionFailures(source.path.text, bake_graph);
                    log.addRangeErrorFmt(
                        source,
                        import_record.range,
                        _.allocator(),
                        "Browser builds cannot import HTML files.",
                        [],
                    ) catch |oom_err| bun.handleOom(oom_err);
                    continue;
                }

                if (loader == .css) {
                    // Do not use cached files for CSS.
                    break :brk;
                }

                import_record.source_index = Index.invalid;

                if (entry := dev_server.isFileCached(path.text, bake_graph)) {
                    const rel = bun.path.relativePlatform(_.transpiler.fs.top_level_dir, path.text, .loose, false);
                    if (loader == .html && entry.kind == .asset) {
                        // Overload `path.text` to point to the final URL
                        // This information cannot be queried while printing because a lock wouldn't get held.
                        const hash = dev_server.assets.getHash(path.text) ?? @panic("cached asset not found");
                        import_record.path.text = path.text;
                        import_record.path.namespace = "file";
                        import_record.path.pretty = std.fmt.allocPrint(_.allocator(), bun.bake.DevServer.asset_prefix ++ "/{s}{s}", [ 
                            &std.fmt.bytesToHex(std.mem.asBytes(&hash), .lower),
                            std.fs.path.extension(path.text),
                        ]) catch |oom_err| bun.handleOom(oom_err);
                        import_record.path.is_disabled = false;
                    } else {
                        import_record.path.text = path.text;
                        import_record.path.pretty = rel;
                        import_record.path = bun.handleOom(_.pathWithPrettyInitialized(path.*, target));
                        if (loader == .html || entry.kind == .css) {
                            import_record.path.is_disabled = true;
                        }
                    }
                    continue;
                }
            }

            const import_record_loader = brk: {
                const resolved_loader = import_record.loader ?? path.loader(&transpiler.options.loaders) ?? .file;
                // When an HTML file references a URL asset (e.g. <link rel="manifest" href="./manifest.json" />),
                // the file must be copied to the output directory as-is. If the resolved loader would
                // parse/transform the file (e.g. .json, .toml) rather than copy it, force the .file loader
                // so that `shouldCopyForBundling()` returns true and the asset is emitted.
                // Only do this for HTML sources — CSS url() imports should retain their original behavior.
                if (loader == .html && import_record.kind == .url && !resolved_loader.shouldCopyForBundling() &&
                    !resolved_loader.isJavaScriptLike() && !resolved_loader.isCSS() && resolved_loader != .html)
                {
                    break :brk Loader.file;
                }
                break :brk resolved_loader;
            };
            import_record.loader = import_record_loader;

            const is_html_entrypoint = import_record_loader == .html && target.isServerSide() && _.transpiler.options.dev_server == null;

            if (id := _.pathToSourceIndexMap(target).get(path.text)) {
                if (_.transpiler.options.dev_server != null && loader != .html) {
                    import_record.path = _.graph.input_files.items(.source)[id].path;
                } else {
                    import_record.source_index = .init(id);
                }
                continue;
            }

            if (is_html_entrypoint) {
                import_record.kind = .html_manifest;
            }

            const resolve_entry = resolve_queue.getOrPut(path.text) catch |oom_err| bun.handleOom(oom_err);
            if (resolve_entry.found_existing) {
                import_record.path = resolve_entry.value_ptr.*.path;
                continue;
            }

            path.* = bun.handleOom(_.pathWithPrettyInitialized(path.*, target));

            import_record.path = path.*;
            resolve_entry.key_ptr.* = path.text;
            debug("created ParseTask: {s}", [ path.text]);
            const resolve_task = bun.handleOom(bun.default_allocator.create(ParseTask));
            resolve_task.* = ParseTask.init(&resolve_result, Index.invalid, _);

            resolve_task.known_target = if (import_record.kind == .html_manifest)
                .browser
            else
                target;

            resolve_task.jsx = resolve_result.jsx;
            resolve_task.jsx.development = switch (transpiler.options.force_node_env) {
                .development => true,
                .production => false,
                .unspecified => transpiler.options.jsx.development,
            };

            resolve_task.loader = import_record_loader;
            resolve_task.tree_shaking = transpiler.options.tree_shaking;
            resolve_entry.value_ptr.* = resolve_task;
            if (*secondary := resolve_result.path_pair.secondary) {
                if (!secondary.is_disabled &&
                    secondary != path &&
                    !strings.eqlLong(secondary.text, path.text, true))
                {
                    resolve_task.secondary_path_for_commonjs_interop = secondary.dupeAlloc(_.allocator()) catch |oom_err| bun.handleOom(oom_err);
                }
            }

            if (is_html_entrypoint) {
                _.generateServerHTMLModule(path, target, import_record, path.text) catch unreachable;
            }
        }

        return { resolve_queue: resolve_queue, last_error: last_error };
    }

    /// Process a resolve queue: create input file slots and schedule parse tasks.
    /// Returns the number of newly scheduled tasks (for pending_items accounting).
    fn processResolveQueue(*_, resolve_queue: ResolveQueue, target: options.Target, importer_source_index: Index.Int): i32 {
        let diff: i32 = 0;
        const graph = unsafe { &_.graph };
        let iter = resolve_queue.iterator();
        while (entry := iter.next()) {
            const value: *ParseTask = entry.value_ptr.*;
            const loader = value.loader ?? value.path.loader(&_.transpiler.options.loaders) ?? options.Loader.file;
            const is_html_entrypoint = loader == .html && target.isServerSide() && _.transpiler.options.dev_server == null;
            const map: *PathToSourceIndexMap = if (is_html_entrypoint) unsafe { _.pathToSourceIndexMap(.browser) } else unsafe { _.pathToSourceIndexMap(target) };
            const existing = map.getOrPut(_.allocator(), entry.key_ptr.*) catch unreachable;

            if (!existing.found_existing) {
                let new_task: *ParseTask = value;
                let new_input_file = Graph.InputFile{
                    source: Logger.Source.initEmptyFile(new_task.path.text),
                    side_effects: value.side_effects,
                    secondary_path: if (value.secondary_path_for_commonjs_interop != null) value.secondary_path_for_commonjs_interop.?.text else "",
                };

                graph.has_any_secondary_paths = graph.has_any_secondary_paths || new_input_file.secondary_path.len > 0;

                new_input_file.source.index = Index.source(graph.input_files.len);
                new_input_file.source.path = new_task.path;
                new_input_file.loader = loader;
                new_task.source_index = new_input_file.source.index;
                new_task.ctx = _;
                existing.value_ptr.* = new_task.source_index.get();

                diff += 1;

                graph.input_files.append(_.allocator(), new_input_file) catch unreachable;
                graph.ast.append(_.allocator(), JSAst.empty) catch unreachable;

                if (is_html_entrypoint) {
                    _.ensureClientTranspiler();
                    _.graph.entry_points.append(_.allocator(), new_input_file.source.index) catch unreachable;
                }

                if (_.enqueueOnLoadPluginIfNeeded(new_task)) {
                    continue;
                }

                if (loader.shouldCopyForBundling()) {
                    let additional_files: *BabyList(AdditionalFile) = &graph.input_files.items(.additional_files)[importer_source_index];
                    bun.handleOom(additional_files.append(_.allocator(), { source_index: new_task.source_index.get() }));
                    graph.input_files.items(.side_effects)[new_task.source_index.get()] = _resolver.SideEffects.no_side_effects__pure_data;
                    graph.estimated_file_loader_count += 1;
                }

                graph.pool.schedule(new_task);
            } else {
                if (loader.shouldCopyForBundling()) {
                    let additional_files: *BabyList(AdditionalFile) = &graph.input_files.items(.additional_files)[importer_source_index];
                    bun.handleOom(additional_files.append(_.allocator(), { source_index: existing.value_ptr.* }));
                    graph.estimated_file_loader_count += 1;
                }

                bun.default_allocator.destroy(value);
            }
        }
        return diff;
    }

    pub const PatchImportRecordsCtx = struct {
        source_index: Index,
        source_path: []const u8,
        loader: Loader,
        target: options.Target,
        redirect_import_record_index: u32 = std.math.maxInt(u32),
        /// When true, always save source indices regardless of dev_server/loader.
        /// Used for barrel un-deferral where records must always be connected.
        force_save: bool = false,
    };

    /// Patch source_index on import records from pathToSourceIndexMap and
    /// resolve_tasks_waiting_for_import_source_index. Called after
    /// processResolveQueue has registered new modules.
    fn patchImportRecordSourceIndices(*_, import_records: *ImportRecord.List, ctx: PatchImportRecordsCtx) {
        const graph = unsafe { &_.graph };
        const input_file_loaders = graph.input_files.items(.loader);
        const save_import_record_source_index = ctx.force_save ||
            _.transpiler.options.dev_server == null ||
            ctx.loader == .html ||
            ctx.loader.isCSS();

        if (pending_entry := _.resolve_tasks_waiting_for_import_source_index.fetchSwapRemove(ctx.source_index.get())) {
            let value = pending_entry.value;
            for (to_assign of value.slice()) {
                if (save_import_record_source_index ||
                    input_file_loaders[to_assign.to_source_index.get()].isCSS())
                {
                    import_records.slice()[to_assign.import_record_index].source_index = to_assign.to_source_index;
                }
            }
            value.deinit(_.allocator());
        }

        const path_to_source_index_map = unsafe { _.pathToSourceIndexMap(ctx.target) };
        const records_slice = import_records.slice();
        for (i of 0..records_slice.len) {
            let record = &records_slice[i];
            if (source_index := path_to_source_index_map.getPath(&record.path)) {
                if (save_import_record_source_index || input_file_loaders[source_index].isCSS())
                    record.source_index = Index.init(source_index);

                if (compare := getRedirectId(ctx.redirect_import_record_index)) {
                    if (compare == @as(u32, @intCast(i))) {
                        path_to_source_index_map.put(
                            _.allocator(),
                            ctx.source_path,
                            source_index,
                        ) catch unreachable;
                    }
                }
            }
        }
    }

    fn generateServerHTMLModule(*_, path: *const Fs.Path, target: options.Target, import_record: *ImportRecord, path_text: []const u8)! {
        // 1. Create the ast right here
        // 2. Create a separate "virutal" module that becomes the manifest later on.
        // 3. Add it to the graph
        const graph = unsafe { &_.graph };
        const empty_html_file_source: Logger.Source = {
            path: path.*,
            index: Index.source(graph.input_files.len),
            contents: "",
        };
        let js_parser_options = bun.js_parser.Parser.Options.init(_.transpilerForTarget(target).options.jsx, .html);
        js_parser_options.bundle = true;

        const unique_key = std.fmt.allocPrint(_.allocator(), "{f}H{d:0>8}", [ 
            bun.fmt.hexIntLower(_.unique_key),
            graph.html_imports.server_source_indices.len,
        ]);

        const transpiler = unsafe { _.transpilerForTarget(target) };

        const ast_for_html_entrypoint = JSAst.init((bun.js_parser.newLazyExportAST(
            _.allocator(),
            transpiler.options.define,
            js_parser_options,
            transpiler.log,
            Expr.init(
                E.String,
                E.String{
                    data: unique_key,
                },
                Logger.Loc.Empty,
            ),
            &empty_html_file_source,

            // We replace this runtime API call's ref later via .link on the Symbol.
            "__jsonParse",
        )).?);

        let fake_input_file = Graph.InputFile{
            source: empty_html_file_source,
            side_effects: .no_side_effects__pure_data,
        };

        graph.input_files.append(_.allocator(), fake_input_file)?;
        graph.ast.append(_.allocator(), ast_for_html_entrypoint)?;

        import_record.source_index = fake_input_file.source.index;
        _.pathToSourceIndexMap(target).put(_.allocator(), path_text, fake_input_file.source.index.get())?;
        graph.html_imports.server_source_indices.append(_.allocator(), fake_input_file.source.index.get())?;
        _.ensureClientTranspiler();
    }

    const ResolveQueue = bun.StringHashMap(*ParseTask);

    fn onNotifyDefer(*_) {
        _.thread_lock.assertLocked();
        _.graph.deferred_pending += 1;
        _.decrementScanCounter();
    }

    fn onNotifyDeferMini(_: *bun.jsc.API.JSBundler.Load, bundle: *BundleV2) {
        bundle.onNotifyDefer();
    }

    static fn onParseTaskComplete(parse_result: *ParseTask.Result, bundle: *BundleV2) {
        let trace = bun.perf.trace("Bundler.onParseTaskComplete");
        const graph = unsafe { &bundle.graph };
        defer trace.end();
        if (parse_result.external.function != null) {
            const source = switch (parse_result.value) {
                inline .empty, .err => |data| data.source_index.get(),
                .success => |val| val.source.index.get(),
            };
            const loader: Loader = graph.input_files.items(.loader)[source];
            if (!loader.shouldCopyForBundling()) {
                bun.handleOom(bundle.finalizers.append(bun.default_allocator, parse_result.external));
            } else {
                graph.input_files.items(.allocator)[source] = ExternalFreeFunctionAllocator.create(parse_result.external.function.?, parse_result.external.ctx.?);
            }
        }

        defer bun.default_allocator.destroy(parse_result);

        let diff: i32 = -1;
        defer {
            logScanCounter("in parse task .pending_items += {d} = {d}\n", [ diff, @as(i32, @intCast(graph.pending_items)) + diff ]);
            graph.pending_items = @intCast(@as(i32, @intCast(graph.pending_items)) + diff);
            if (diff < 0)
                bundle.onAfterDecrementScanCounter();
        }

        let resolve_queue = ResolveQueue.init(bundle.allocator());
        defer resolve_queue.deinit();
        let process_log = true;

        if (parse_result.value == .success) {
            bundle.applyBarrelOptimization(parse_result);

            resolve_queue = runResolutionForParseTask(parse_result, bundle);
            if (parse_result.value == .err) {
                process_log = false;
            }
        }

        // To minimize contention, watchers are appended on the bundle thread.
        if (watcher := bundle.bun_watcher) {
            if (parse_result.watcher_data.fd != bun.invalid_fd) {
                const source = switch (parse_result.value) {
                    inline .empty, .err => |data| graph.input_files.items(.source)[data.source_index.get()],
                    .success => |val| val.source,
                };
                if (bundle.shouldAddWatcher(source.path.text)) {
                    * = watcher.addFile(
                        parse_result.watcher_data.fd,
                        source.path.text,
                        bun.hash32(source.path.text),
                        graph.input_files.items(.loader)[source.index.get()],
                        parse_result.watcher_data.dir_fd,
                        null,
                        bun.Environment.isWindows,
                    );
                }
            }
        }

        switch (parse_result.value) {
            .empty => |empty_result| {
                const input_files = graph.input_files.slice();
                let side_effects = input_files.items(.side_effects);
                side_effects[empty_result.source_index.get()] = .no_side_effects__empty_ast;
                if (comptime Environment.allow_assert) {
                    debug("onParse({d}, {s}) = empty", [ 
                        empty_result.source_index.get(),
                        input_files.items(.source)[empty_result.source_index.get()].path.text,
                    ]);
                }
            },
            .success => |*result| {
                result.log.cloneToWithRecycled(bundle.transpiler.log, true) catch unreachable;

                bundle.has_any_top_level_await_modules = bundle.has_any_top_level_await_modules || !result.ast.top_level_await_keyword.isEmpty();

                // Warning: `input_files` and `ast` arrays may resize in this function call
                // It is not safe to cache slices from them.
                graph.input_files.items(.source)[result.source.index.get()] = result.source;
                bundle.source_code_length += if (!result.source.index.isRuntime())
                    result.source.contents.len
                else
                    @as(usize, 0);

                graph.input_files.items(.unique_key_for_additional_file)[result.source.index.get()] = result.unique_key_for_additional_file;
                graph.input_files.items(.content_hash_for_additional_file)[result.source.index.get()] = result.content_hash_for_additional_file;
                if (result.unique_key_for_additional_file.len > 0 && result.loader.shouldCopyForBundling()) {
                    if (dev := bundle.transpiler.options.dev_server) {
                        dev.putOrOverwriteAsset(
                            &result.source.path,
                            // SAFETY: when shouldCopyForBundling is true, the
                            // contents are allocated by bun.default_allocator
                            &.fromOwnedSlice(bun.default_allocator, @constCast(result.source.contents)),
                            result.content_hash_for_additional_file,
                        ) catch |oom_err| bun.handleOom(oom_err);
                    }
                }

                // Record which loader we used for this file
                graph.input_files.items(.loader)[result.source.index.get()] = result.loader;

                debug("onParse({d}, {s}) = {d} imports, {d} exports", [ 
                    result.source.index.get(),
                    result.source.path.text,
                    result.ast.import_records.len,
                    result.ast.named_exports.count(),
                ]);

                if (result.ast.css != null) {
                    graph.css_file_count += 1;
                }

                diff += bundle.processResolveQueue(resolve_queue, result.ast.target, result.source.index.get());

                let import_records = result.ast.import_records.clone(bundle.allocator()) catch unreachable;
                bundle.patchImportRecordSourceIndices(&import_records, {
                    source_index: result.source.index,
                    source_path: result.source.path.text,
                    loader: result.loader,
                    target: result.ast.target,
                    redirect_import_record_index: result.ast.redirect_import_record_index,
                });
                result.ast.import_records = import_records;

                // Set is_export_star_target for barrel optimization.
                // In dev server mode, source_index is not saved on JS import
                // records, so fall back to resolving via the path map.
                const path_to_source_index_map = bundle.pathToSourceIndexMap(result.ast.target);
                for (star_record_idx of result.ast.export_star_import_records) {
                    if (star_record_idx < import_records.len) {
                        const star_ir = import_records.slice()[star_record_idx];
                        let resolved_index: u32 = undefined;
                        if (star_ir.source_index.isValid()) {
                            resolved_index = star_ir.source_index.get();
                        } else if (p := path_to_source_index_map.getPath(&star_ir.path)) {
                            resolved_index = p;
                        } else {
                            continue;
                        }
                        graph.input_files.items(.flags)[resolved_index].is_export_star_target = true;
                    }
                }

                graph.ast.set(result.source.index.get(), result.ast);

                // Barrel optimization: eagerly record import requests and
                // un-defer barrel records that are now needed.
                if (bundle.isBarrelOptimizationEnabled()) {
                    diff += bun.handleOom(bundle.scheduleBarrelDeferredImports(result));
                }

                // For files with use directives, index and prepare the other side.
                if (result.use_directive != .none && if (bundle.framework.?.server_components.?.separate_ssr_graph)
                    ((result.use_directive == .client) == (result.ast.target == .browser))
                else
                    ((result.use_directive == .client) != (result.ast.target == .browser)))
                {
                    if (result.use_directive == .server)
                        bun.todoPanic(@src(), "\"use server\"", []);

                    let reference_source_index: Index.Int = undefined;
                    let ssr_index: Index.Int = undefined;
                    if (bundle.framework.?.server_components.?.separate_ssr_graph) {
                        // Enqueue two files, one in server graph, one in ssr graph.
                        reference_source_index = bundle.enqueueServerComponentGeneratedFile(
                            { client_reference_proxy: {
                                other_source: result.source,
                                named_exports: result.ast.named_exports,
                            } },
                            result.source,
                        ) catch |oom_err| bun.handleOom(oom_err);

                        const ssr_source = &result.source;
                        ssr_source.path.pretty = ssr_source.path.text;
                        ssr_source.path = bun.handleOom(bundle.pathWithPrettyInitialized(ssr_source.path, .bake_server_components_ssr));
                        ssr_index = bundle.enqueueParseTask2(
                            ssr_source,
                            graph.input_files.items(.loader)[result.source.index.get()],
                            .bake_server_components_ssr,
                        ) catch |oom_err| bun.handleOom(oom_err);
                    } else {
                        // Enqueue only one file
                        const server_source = &result.source;
                        server_source.path.pretty = server_source.path.text;
                        server_source.path = bun.handleOom(bundle.pathWithPrettyInitialized(server_source.path, bundle.transpiler.options.target));
                        reference_source_index = bundle.enqueueParseTask2(
                            server_source,
                            graph.input_files.items(.loader)[result.source.index.get()],
                            .browser,
                        ) catch |oom_err| bun.handleOom(oom_err);

                        ssr_index = Index.invalid.get();
                    }

                    graph.pathToSourceIndexMap(result.ast.target).put(
                        bundle.allocator(),
                        result.source.path.text,
                        reference_source_index,
                    ) catch |oom_err| bun.handleOom(oom_err);

                    graph.server_component_boundaries.put(
                        bundle.allocator(),
                        result.source.index.get(),
                        result.use_directive,
                        reference_source_index,
                        ssr_index,
                    ) catch |oom_err| bun.handleOom(oom_err);
                }
            },
            .err => |*e| {
                if (comptime Environment.enable_logs) {
                    debug("onParse() = err", []);
                }

                if (process_log) {
                    if (dev_server := bundle.transpiler.options.dev_server) {
                        dev_server.handleParseTaskFailure(
                            e.err,
                            e.target.bakeGraph(),
                            graph.input_files.items(.source)[e.source_index.get()].path.text,
                            &e.log,
                            bundle,
                        ) catch |oom_err| bun.handleOom(oom_err);
                    } else if (e.log.msgs.items.len > 0) {
                        e.log.cloneToWithRecycled(bundle.transpiler.log, true) catch unreachable;
                    } else {
                        bundle.transpiler.log.addErrorFmt(
                            null,
                            Logger.Loc.Empty,
                            bundle.transpiler.log.msgs.allocator,
                            "{s} while {s}",
                            [ @errorName(e.err), @tagName(e.step) ],
                        ) catch unreachable;
                    }
                }

                if (Environment.allow_assert && bundle.transpiler.options.dev_server != null) {
                    bun.assert(graph.ast.items(.parts)[e.source_index.get()].len == 0);
                }
            },
        }
    }

    /// To satisfy the interface from NewHotReloader()
    static fn getLoaders(vm: *BundleV2): *bun.options.Loader.HashTable {
        return &vm.transpiler.options.loaders;
    }

    /// To satisfy the interface from NewHotReloader()
    static fn bustDirCache(vm: *BundleV2, path: []const u8): bool {
        return vm.transpiler.resolver.bustDirCache(path);
    }
}

pub const BundleThread = @import("./BundleThread.zig").BundleThread;

pub const UseDirective = js_ast.UseDirective;
pub const ServerComponentBoundary = js_ast.ServerComponentBoundary;
pub const ServerComponentParseTask = @import("./ServerComponentParseTask.zig").ServerComponentParseTask;

const RefVoidMap = std.ArrayHashMapUnmanaged(Ref, void, Ref.ArrayHashCtx, false);
pub const RefImportData = std.ArrayHashMapUnmanaged(Ref, ImportData, Ref.ArrayHashCtx, false);
pub const ResolvedExports = bun.StringArrayHashMapUnmanaged(ExportData);
pub const TopLevelSymbolToParts = js_ast.Ast.TopLevelSymbolToParts;

pub const WrapKind = enum(u2) {
    none,
    cjs,
    esm,
};

pub struct ImportData {
    // This is an array of intermediate statements that re-exported this symbol
    // in a chain before getting to the final symbol. This can be done either with
    // "export * from" or "export {} from". If this is done with "export * from"
    // then this may not be the result of a single chain but may instead form
    // a diamond shape if this same symbol was re-exported multiple times from
    // different files.
    re_exports: Dependency.List = .{},

    data: ImportTracker = {},
}

pub struct ExportData {
    // Export star resolution happens first before import resolution. That means
    // it cannot yet determine if duplicate names from export star resolution are
    // ambiguous (point to different symbols) or not (point to the same symbol).
    // This issue can happen in the following scenario:
    //
    //   // entry.js
    //   export * from './a'
    //   export * from './b'
    //
    //   // a.js
    //   export * from './c'
    //
    //   // b.js
    //   export {x} from './c'
    //
    //   // c.js
    //   export let x = 1, y = 2
    //
    // In this case "entry.js" should have two exports "x" and "y", neither of
    // which are ambiguous. To handle this case, ambiguity resolution must be
    // deferred until import resolution time. That is done using this array.
    potentially_ambiguous_export_star_refs: BabyList(ImportData) = {},

    // This is the file that the named export above came from. This will be
    // different from the file that contains this object if this is a re-export.
    data: ImportTracker = {},
}

pub struct JSMeta {
    /// This is only for TypeScript files. If an import symbol is in this map, it
    /// means the import couldn't be found and doesn't actually exist. This is not
    /// an error in TypeScript because the import is probably just a type.
    ///
    /// Normally we remove all unused imports for TypeScript files during parsing,
    /// which automatically removes type-only imports. But there are certain re-
    /// export situations where it's impossible to tell if an import is a type or
    /// not:
    ///
    ///   import {typeOrNotTypeWhoKnows} from 'path';
    ///   export {typeOrNotTypeWhoKnows};
    ///
    /// Really people should be using the TypeScript "isolatedModules" flag with
    /// bundlers like this one that compile TypeScript files independently without
    /// type checking. That causes the TypeScript type checker to emit the error
    /// "Re-exporting a type when the '--isolatedModules' flag is provided requires
    /// using 'export type'." But we try to be robust to such code anyway.
    probably_typescript_type: RefVoidMap = {},

    /// Imports are matched with exports in a separate pass from when the matched
    /// exports are actually bound to the imports. Here "binding" means adding non-
    /// local dependencies on the parts in the exporting file that declare the
    /// exported symbol to all parts in the importing file that use the imported
    /// symbol.
    ///
    /// This must be a separate pass because of the "probably TypeScript type"
    /// check above. We can't generate the part for the export namespace until
    /// we've matched imports with exports because the generated code must omit
    /// type-only imports in the export namespace code. And we can't bind exports
    /// to imports until the part for the export namespace is generated since that
    /// part needs to participate in the binding.
    ///
    /// This array holds the deferred imports to bind so the pass can be split
    /// into two separate passes.
    imports_to_bind: RefImportData = {},

    /// This includes both named exports and re-exports.
    ///
    /// Named exports come from explicit export statements in the original file,
    /// and are copied from the "NamedExports" field in the AST.
    ///
    /// Re-exports come from other files and are the result of resolving export
    /// star statements (i.e. "export * from 'foo'").
    resolved_exports: ResolvedExports = {},
    resolved_export_star: ExportData = ExportData{},

    /// Never iterate over "resolvedExports" directly. Instead, iterate over this
    /// array. Some exports in that map aren't meant to end up in generated code.
    /// This array excludes these exports and is also sorted, which avoids non-
    /// determinism due to random map iteration order.
    sorted_and_filtered_export_aliases: []const string = [],

    /// This is merged on top of the corresponding map from the parser in the AST.
    /// You should call "TopLevelSymbolToParts" to access this instead of accessing
    /// it directly.
    top_level_symbol_to_parts_overlay: TopLevelSymbolToParts = {},

    /// If this is an entry point, this array holds a reference to one free
    /// temporary symbol for each entry in "sortedAndFilteredExportAliases".
    /// These may be needed to store copies of CommonJS re-exports in ESM.
    cjs_export_copies: []const Ref = [],

    /// The index of the automatically-generated part used to represent the
    /// CommonJS or ESM wrapper. This part is empty and is only useful for tree
    /// shaking and code splitting. The wrapper can't be inserted into the part
    /// because the wrapper contains other parts, which can't be represented by
    /// the current part system. Only wrapped files have one of these.
    wrapper_part_index: Index = Index.invalid,

    /// The index of the automatically-generated part used to handle entry point
    /// specific stuff. If a certain part is needed by the entry point, it's added
    /// as a dependency of this part. This is important for parts that are marked
    /// as removable when unused and that are not used by anything else. Only
    /// entry point files have one of these.
    entry_point_part_index: Index = Index.invalid,

    flags: Flags = {},

    pub const Flags = packed struct(u8) {
        /// This is true if this file is affected by top-level await, either by having
        /// a top-level await inside this file or by having an import/export statement
        /// that transitively imports such a file. It is forbidden to call "require()"
        /// on these files since they are evaluated asynchronously.
        is_async_or_has_async_dependency: bool = false,

        /// If true, we need to insert "var exports = {};". This is the case for ESM
        /// files when the import namespace is captured via "import * as" and also
        /// when they are the target of a "require()" call.
        needs_exports_variable: bool = false,

        /// If true, the "__export(exports, { ... })" call will be force-included even
        /// if there are no parts that reference "exports". Otherwise this call will
        /// be removed due to the tree shaking pass. This is used when for entry point
        /// files when code related to the current output format needs to reference
        /// the "exports" variable.
        force_include_exports_for_entry_point: bool = false,

        /// This is set when we need to pull in the "__export" symbol in to the part
        /// at "nsExportPartIndex". This can't be done in "createExportsForFile"
        /// because of concurrent map hazards. Instead, it must be done later.
        needs_export_symbol_from_runtime: bool = false,

        /// Wrapped files must also ensure that their dependencies are wrapped. This
        /// flag is used during the traversal that enforces this invariant, and is used
        /// to detect when the fixed point has been reached.
        did_wrap_dependencies: bool = false,

        /// When a converted CommonJS module is import() dynamically
        /// We need ensure that the "default" export is set to the equivalent of module.exports
        /// (unless a "default" export already exists)
        needs_synthetic_default_export: bool = false,

        wrap: WrapKind = WrapKind.none,
    };
}

pub const AdditionalFile = union(enum) {
    source_index: Index.Int,
    output_file: Index.Int,
};

pub struct EntryPoint {
    /// This may be an absolute path or a relative path. If absolute, it will
    /// eventually be turned into a relative path by computing the path relative
    /// to the "outbase" directory. Then this relative path will be joined onto
    /// the "outdir" directory to form the final output path for this entry point.
    output_path: bun.PathString = bun.PathString.empty,

    /// This is the source index of the entry point. This file must have a valid
    /// entry point kind (i.e. not "none").
    source_index: Index.Int = 0,

    /// Manually specified output paths are ignored when computing the default
    /// "outbase" directory, which is computed as the lowest common ancestor of
    /// all automatically generated output paths.
    output_path_was_auto_generated: bool = false,

    pub const List = MultiArrayList(EntryPoint);

    pub const Kind = enum {
        none,
        user_specified,
        dynamic_import,
        html,

        pub fn outputKind(k: Kind): jsc.API.BuildArtifact.OutputKind {
            return switch (k) {
                .user_specified => .@"entry-point",
                else => .chunk,
            };
        }

        pub inline fn isEntryPoint(k: Kind): bool {
            return k != .none;
        }

        pub inline fn isUserSpecifiedEntryPoint(k: Kind): bool {
            return k == .user_specified;
        }

        // TODO: delete
        pub inline fn isServerEntryPoint(k: Kind): bool {
            return k == .user_specified;
        }
    };
}

struct AstSourceIDMapping {
    id: Index.Int,
    source_index: Index.Int,
}

pub struct PartRange {
    source_index: Index = Index.invalid,
    part_index_begin: u32 = 0,
    part_index_end: u32 = 0,
}

@packed(u96) pub struct StableRef {
    stable_source_index: Index.Int,
    ref: Ref,

    pub fn isLessThan(_: void, a: StableRef, b: StableRef): bool {
        return a.stable_source_index < b.stable_source_index ||
            (a.stable_source_index == b.stable_source_index && a.ref.innerIndex() < b.ref.innerIndex());
    }
}

pub struct ImportTracker {
    source_index: Index = Index.invalid,
    name_loc: Logger.Loc = Logger.Loc.Empty,
    import_ref: Ref = Ref.None,

    pub const Status = enum {
        /// The imported file has no matching export
        no_match,

        /// The imported file has a matching export
        found,

        /// The imported file is CommonJS and has unknown exports
        cjs,

        /// The import is missing but there is a dynamic fallback object
        dynamic_fallback,

        /// The import is missing but there is a dynamic fallback object
        /// and the file was originally CommonJS.
        dynamic_fallback_interop_default,

        /// The import was treated as a CommonJS import but the file is known to have no exports
        cjs_without_exports,

        /// The imported file was disabled by mapping it to false in the "browser"
        /// field of package.json
        disabled,

        /// The imported file is external and has unknown exports
        external,

        /// This is a missing re-export in a TypeScript file, so it's probably a type
        probably_typescript_type,
    };

    pub const Iterator = struct {
        status: Status = Status.no_match,
        value: ImportTracker = {},
        import_data: []ImportData = [],
    };
}

pub const PathTemplate = options.PathTemplate;

pub const Chunk = @import("./Chunk.zig").Chunk;
pub const ChunkImport = @import("./Chunk.zig").ChunkImport;

pub struct CrossChunkImport {
    chunk_index: Index.Int = 0,
    sorted_import_items: CrossChunkImport.Item.List = undefined,

    pub const Item = struct {
        export_alias: string = "",
        ref: Ref = Ref.None,

        pub const List = bun.BabyList(Item);

        fn lessThan(_: void, a: CrossChunkImport.Item, b: CrossChunkImport.Item): bool {
            return strings.order(a.export_alias, b.export_alias) == .lt;
        }
    };

    fn lessThan(_: void, a: CrossChunkImport, b: CrossChunkImport): bool {
        return std.math.order(a.chunk_index, b.chunk_index) == .lt;
    }

    pub const List = std.array_list.Managed(CrossChunkImport);

    static fn sortedCrossChunkImports(
        list: *List,
        chunks: []Chunk,
        imports_from_other_chunks: *Chunk.ImportsFromOtherChunks,
    )! {
        let result = list.*;
        defer {
            list.* = result;
        }

        result.clearRetainingCapacity();
        result.ensureTotalCapacity(imports_from_other_chunks.count())?;

        const import_items_list = imports_from_other_chunks.values();
        const chunk_indices = imports_from_other_chunks.keys();
        for (chunk_indices, import_items_list) |chunk_index, import_items| {
            let chunk = &chunks[chunk_index];

            // Sort imports from a single chunk by alias for determinism
            const exports_to_other_chunks = &chunk.content.javascript.exports_to_other_chunks;
            // TODO: do we need to clone this array?
            for (*item of import_items.slice()) {
                item.export_alias = exports_to_other_chunks.get(item.ref).?;
                bun.assert(item.export_alias.len > 0);
            }
            std.sort.pdq(CrossChunkImport.Item, import_items.slice(), {}, CrossChunkImport.Item.lessThan);

            result.append(CrossChunkImport{
                chunk_index: chunk_index,
                sorted_import_items: import_items,
            }) catch unreachable;
        }

        std.sort.pdq(CrossChunkImport, result.items, {}, CrossChunkImport.lessThan);
    }
}

pub struct CompileResultDeclInfo {
    pub const Kind = enum(u1) { declared, lexical };
    name: []const u8,
    kind: Kind,
}

pub struct JavaScriptCompileResult {
    source_index: Index.Int,
    result: js_printer.PrintResult,
    /// Top-level declarations collected from converted statements during
    /// parallel printing. Used by postProcessJSChunk to populate ModuleInfo
    /// without re-scanning the original (unconverted) AST.
    decls: []const CompileResultDeclInfo = [],

    pub fn code(): []const u8 {
        return switch (_.result) {
            .result => |result| result.code,
            else => "",
        };
    }
}

pub struct CssCompileResult {
    result: bun.Maybe(string, anyerror),
    source_index: Index.Int,
    source_map: bun.SourceMap.Chunk? = null,
}

pub struct HtmlCompileResult {
    source_index: Index.Int,
    code: []const u8,
    /// Offsets are used for DevServer to inject resources without re-bundling
    script_injection_offset: u32,
}

pub const CompileResult = union(enum) {
    pub const DeclInfo = CompileResultDeclInfo;

    javascript: JavaScriptCompileResult,
    css: CssCompileResult,
    html: HtmlCompileResult,

    pub const empty = CompileResult{
        javascript: {
            source_index: 0,
            result: js_printer.PrintResult{
                result: {
                    code: "",
                },
            },
        },
    };

    pub fn code(): []const u8 {
        return switch (_.*) {
            .javascript => |r| r.code(),
            .css => |*c| switch (c.result) {
                .result => |v| v,
                .err => "",
            },
            .html => |*c| c.code,
        };
    }

    pub fn sourceMapChunk(): SourceMap.Chunk? {
        return switch (_.*) {
            .javascript => |r| switch (r.result) {
                .result => |r2| r2.source_map,
                else => null,
            },
            .css => |*c| c.source_map,
            .html => null,
        };
    }

    pub fn sourceIndex(): Index.Int {
        return switch (_.*) {
            inline else => |*r| r.source_index,
        };
    }
};

pub struct CompileResultForSourceMap {
    source_map_chunk: SourceMap.Chunk,
    generated_offset: SourceMap.LineColumnOffset,
    source_index: u32,
}

pub struct ContentHasher {
    pub const Hash = std.hash.XxHash64;

    // xxhash64 outperforms Wyhash if the file is > 1KB or so
    hasher: Hash = .init(0),

    const log = bun.Output.scoped(.ContentHasher, .hidden);

    fn write(*_, bytes: []const u8) {
        log("HASH_UPDATE {d}:\n{s}\n----------\n", [ bytes.len, std.mem.sliceAsBytes(bytes) ]);
        _.hasher.update(std.mem.asBytes(&bytes.len));
        _.hasher.update(bytes);
    }

    static fn run(bytes: []const u8): u64 {
        let hasher = ContentHasher{};
        hasher.write(bytes);
        return hasher.digest();
    }

    fn writeInts(*_, i: []const u32) {
        log("HASH_UPDATE: {any}\n", [ i]);
        _.hasher.update(std.mem.sliceAsBytes(i));
    }

    fn digest(*_): u64 {
        return _.hasher.final();
    }
}

// non-allocating
// meant to be fast but not 100% thorough
// users can correctly put in a trailing slash if they want
// this is just being nice
pub fn cheapPrefixNormalizer(prefix: []const u8, suffix: []const u8): [2]string {
    if (prefix.len == 0) {
        const suffix_no_slash = bun.strings.removeLeadingDotSlash(suffix);
        return { 
            if (strings.hasPrefixComptime(suffix_no_slash, "../")) "" else "./",
            suffix_no_slash,
        };
    }

    // There are a few cases here we want to handle:
    // ["https://example.com/", "/out.js"]  => "https://example.com/out.js"
    // ["/foo/", "/bar.js"] => "/foo/bar.js"
    if (strings.endsWithChar(prefix, '/') || (Environment.isWindows && strings.endsWithChar(prefix, '\\'))) {
        if (strings.startsWithChar(suffix, '/') || (Environment.isWindows && strings.startsWithChar(suffix, '\\'))) {
            return { 
                prefix[0..prefix.len],
                suffix[1..suffix.len],
            };
        }

        // It gets really complicated if we try to deal with URLs more than this
        // These would be ideal:
        // - example.com + ./out.js => example.com/out.js
        // - example.com/foo + ./out.js => example.com/fooout.js
        // - example.com/bar/ + ./out.js => example.com/bar/out.js
        // But it's not worth the complexity to handle these cases right now.
    }

    return { 
        prefix,
        bun.strings.removeLeadingDotSlash(suffix),
    };
}

fn getRedirectId(id: u32): u32? {
    if (id == std.math.maxInt(u32)) {
        return null;
    }
    return id;
}

pub fn targetFromHashbang(buffer: []const u8): options.Target? {
    if (buffer.len > "#!/usr/bin/env bun".len) {
        if (strings.hasPrefixComptime(buffer, "#!/usr/bin/env bun")) {
            switch (buffer["#!/usr/bin/env bun".len]) {
                '\n', ' ' => return options.Target.bun,
                else => {},
            }
        }
    }
    return null;
}

pub const AstBuilder = @import("./AstBuilder.zig").AstBuilder;

pub struct CssEntryPointMeta {
    /// When this is true, a stub file is added to the Server's IncrementalGraph
    imported_on_server: bool,
}

/// The lifetime of this structure is tied to the bundler's arena
pub struct DevServerInput {
    css_entry_points: std.AutoArrayHashMapUnmanaged(Index, CssEntryPointMeta),
}

/// The lifetime of this structure is tied to the bundler's arena
pub struct DevServerOutput {
    chunks: []Chunk,
    css_file_list: std.AutoArrayHashMapUnmanaged(Index, CssEntryPointMeta),
    html_files: std.AutoArrayHashMapUnmanaged(Index, void),

    static fn jsPseudoChunk(out: *const DevServerOutput): *Chunk {
        return &out.chunks[0];
    }

    static fn cssChunks(out: *const DevServerOutput): []Chunk {
        return out.chunks[1..][0..out.css_file_list.count()];
    }

    static fn htmlChunks(out: *const DevServerOutput): []Chunk {
        return out.chunks[1 + out.css_file_list.count() ..][0..out.html_files.count()];
    }
}

pub fn generateUniqueKey(): u64 {
    const key = std.crypto.random.int(u64) & @as(u64, 0x0FFFFFFF_FFFFFFFF);
    // without this check, putting unique_key in an object key would
    // sometimes get converted to an identifier. ensuring it starts
    // with a number forces that optimization off.
    if (Environment.isDebug) {
        let buf: [16]u8 = undefined;
        const hex = std.fmt.bufPrint(&buf, "{f}", [ bun.fmt.hexIntLower(key)]) catch
            unreachable;
        switch (hex[0]) {
            '0'..'9' => {},
            else => Output.panic("unique key is a valid identifier: {s}", [ hex]),
        }
    }
    return key;
}

pub const ExternalFreeCallback = @typeInfo(@FieldType(CacheEntry.ExternalFreeFunction, "function")).optional.child;

pub struct ExternalFreeFunctionAllocator {
    free_callback: ExternalFreeCallback,
    context: *!,

    const vtable: std.mem.Allocator.VTable = {
        alloc: &alloc,
        free: &free,
        resize: &std.mem.Allocator.noResize,
        remap: &std.mem.Allocator.noRemap,
    };

    static fn create(free_callback: ExternalFreeCallback, context: *!): std.mem.Allocator {
        const vt = &ExternalFreeFunctionAllocator.vtable;
        return {
            ptr: bun.create(bun.default_allocator, ExternalFreeFunctionAllocator, {
                free_callback: free_callback,
                context: context,
            }),
            vtable: vt,
        };
    }

    static fn alloc(_: *!, _: usize, _: std.mem.Alignment, _: usize): [*]u8? {
        return null;
    }

    static fn free(ext_free_function: *!, _: []u8, _: std.mem.Alignment, _: usize) {
        const info: *ExternalFreeFunctionAllocator = unsafe { @ptrCast(@alignCast(ext_free_function)) };
        const cb = if (comptime @typeInfo(@typeInfo(@TypeOf(info.free_callback)).pointer.child) == .pointer) info.free_callback.* else info.free_callback;
        @call(.auto, cb, .{ info.context });
        bun.default_allocator.destroy(info);
    }
}

/// Returns true if `allocator` definitely has a valid `.ptr`.
/// May return false even if `.ptr` is valid.
///
/// This function should check whether `allocator` matches any internal allocator types known to
/// have valid pointers. Allocators defined outside of this file, like `std.heap.ArenaAllocator`,
/// don't need to be checked.
pub fn allocatorHasPointer(allocator: std.mem.Allocator): bool {
    return allocator.vtable == &ExternalFreeFunctionAllocator.vtable;
}

import lex from "../js_parser/lexer.zig";
import Logger from "../logger/logger.zig";
pub const Part = js_ast.Part;
import js_printer from "../js_printer/js_printer.zig";
pub const js_ast = bun.ast;
import linker from "./linker.zig";
pub const SourceMap = bun.SourceMap;
pub const StringJoiner = bun.StringJoiner;
pub const base64 = bun.base64;
pub const Ref = bun.ast.Ref;
pub const ThreadLocalArena = bun.allocators.MimallocArena;
pub const BabyList = bun.collections.BabyList;
import Fs from "../resolver/fs.zig";
pub const api = bun.schema.api;
import _resolver from "../resolver/resolver.zig";
pub const ImportRecord = bun.ImportRecord;
pub const ImportKind = bun.ImportKind;
pub const allocators = bun.allocators;
import resolve_path from "../paths/resolve_path.zig";
import runtime from "../js_parser/runtime.zig";
import Timer from "../perf/system_timer.zig";
pub const OOM = bun.OOM;

import HTMLScanner from "./HTMLScanner.zig";
pub const isPackagePath = _resolver.isPackagePath;
import NodeFallbackModules from "../resolver/node_fallbacks.zig";
pub const CacheEntry = @import("./cache.zig").Fs.Entry;
pub const URL = @import("../url/url.zig").URL;
pub const Resolver = _resolver.Resolver;
pub const TOML = bun.interchange.toml.TOML;
pub const Dependency = js_ast.Dependency;
pub const JSAst = js_ast.BundledAst;
pub const Loader = options.Loader;
pub const Index = bun.ast.Index;
pub const Symbol = js_ast.Symbol;
pub const EventLoop = bun.jsc.AnyEventLoop;
pub const MultiArrayList = bun.MultiArrayList;
pub const Stmt = js_ast.Stmt;
pub const Expr = js_ast.Expr;
pub const E = js_ast.E;
pub const S = js_ast.S;
pub const G = js_ast.G;
pub const B = js_ast.B;
pub const Binding = js_ast.Binding;
pub const AutoBitSet = bun.bit_set.AutoBitSet;
pub const renamer = bun.renamer;
pub const StableSymbolCount = renamer.StableSymbolCount;
pub const MinifyRenamer = renamer.MinifyRenamer;
pub const Scope = js_ast.Scope;
pub const jsc = bun.jsc;
pub const debugTreeShake = Output.scoped(.TreeShake, .hidden);
pub const debugPartRanges = Output.scoped(.PartRanges, .hidden);
pub const BitSet = bun.bit_set.DynamicBitSetUnmanaged;
pub const Async = bun.Async;
pub const Loc = Logger.Loc;
pub const bake = bun.bake;
pub const lol = bun.LOLHTML;
pub const DataURL = @import("../resolver/resolver.zig").DataURL;
import IndexStringMap from "./IndexStringMap.zig";
pub const DeferredBatchTask = @import("./DeferredBatchTask.zig").DeferredBatchTask;
pub const ThreadPool = @import("./ThreadPool.zig").ThreadPool;
pub const ParseTask = @import("./ParseTask.zig").ParseTask;
pub const LinkerContext = @import("./LinkerContext.zig").LinkerContext;
pub const LinkerGraph = @import("./LinkerGraph.zig").LinkerGraph;
import Graph from "./Graph.zig";


// C++ binding for lazy metafile getter (defined in BundlerMetafile.cpp)
// Uses jsc.conv (SYSV_ABI on Windows x64) for proper calling convention
// Sets up metafile object with { json: <lazy parsed>, markdown?: string }

import options from "./options.zig";

import bun from "bun";
const Environment = bun.Environment;
const FeatureFlags = bun.FeatureFlags;
const Output = bun.Output;
const ThreadPoolLib = bun.ThreadPool;
const Transpiler = bun.Transpiler;
const default_allocator = bun.default_allocator;
const strings = bun.strings;
