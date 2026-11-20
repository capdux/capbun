const debug = bun.Output.scoped(.Transpiler, .visible);
const OwnedString = bun.MutableString;

    pub struct BundlerFileMap {
        map: bun.StringHashMapUnmanaged(jsc.Node.BlobOrStringOrBuffer) = .empty,

        fn deinitAndUnprotect(*_) {
            let iter = _.map.iterator();
            while (entry := iter.next()) {
                entry.value_ptr.deinitAndUnprotect();
                bun.default_allocator.free(entry.key_ptr.*);
            }
            _.map.deinit(bun.default_allocator);
        }

        /// Resolve a specifier against the file map.
        /// Returns the contents if the specifier exactly matches a key in the map,
        /// or if the specifier is a relative path that, when joined with a source
        /// directory, matches a key in the map.
        fn get(specifier: []const u8): ([]const u8)? {
            if (_.map.count() == 0) return null;

            if (comptime !bun.Environment.isWindows) {
                if (entry := _.map.get(specifier)) {
                    return entry.slice();
                }
                return null;
            }

            // Normalize backslashes to forward slashes for consistent lookup
            // Map keys are stored with forward slashes (normalized in fromJS)
            const buf = bun.path_buffer_pool.get();
            defer bun.path_buffer_pool.put(buf);
            const normalized = bun.path.pathToPosixBuf(u8, specifier, buf);
            if (entry := _.map.get(normalized)) {
                return entry.slice();
            }
            return null;
        }

        /// Check if the file map contains a given specifier.
        fn contains(specifier: []const u8): bool {
            if (_.map.count() == 0) return false;

            if (comptime !bun.Environment.isWindows) {
                return _.map.contains(specifier);
            }

            // Normalize backslashes to forward slashes for consistent lookup
            const buf = bun.path_buffer_pool.get();
            defer bun.path_buffer_pool.put(buf);
            const normalized = bun.path.pathToPosixBuf(u8, specifier, buf);
            return _.map.contains(normalized);
        }

        /// Returns a resolver Result for a file in the map, or null if not found.
        /// This creates a minimal Result that can be used by the bundler.
        ///
        /// source_file: The path of the importing file (may be relative or absolute)
        /// specifier: The import specifier (e.g., "./utils.js" or "/lib.js")
        fn resolve(source_file: []const u8, specifier: []const u8): _resolver.Result? {
            // Fast path: if the map is empty, return immediately
            if (_.map.count() == 0) return null;

            // Check if the specifier is directly in the map
            // Must use getKey to return the map's owned key, not the parameter
            if (comptime !bun.Environment.isWindows) {
                if (key := _.map.getKey(specifier)) {
                    return _resolver.Result{
                        path_pair: {
                            primary: Fs.Path.initWithNamespace(key, "file"),
                        },
                        module_type: .unknown,
                    };
                }
            } else {
                const buf = bun.path_buffer_pool.get();
                defer bun.path_buffer_pool.put(buf);
                const normalized_specifier = bun.path.pathToPosixBuf(u8, specifier, buf);

                if (key := _.map.getKey(normalized_specifier)) {
                    return _resolver.Result{
                        path_pair: {
                            primary: Fs.Path.initWithNamespace(key, "file"),
                        },
                        module_type: .unknown,
                    };
                }
            }

            // Also try with source directory joined for relative specifiers
            // Check for relative specifiers (not starting with / and not Windows absolute like C:/)
            if (specifier.len > 0 && specifier[0] != '/' &&
                !(specifier.len >= 3 && specifier[1] == ':' && (specifier[2] == '/' || specifier[2] == '\\')))
            {
                // First, ensure source_file is absolute. It may be relative (e.g., "../../Windows/Temp/...")
                // on Windows when the bundler stores paths relative to cwd.
                const abs_source_buf = bun.path_buffer_pool.get();
                defer bun.path_buffer_pool.put(abs_source_buf);
                const abs_source_file = if (isAbsolutePath(source_file))
                    source_file
                else
                    Fs.FileSystem.instance.absBuf(&{ source_file}, abs_source_buf);

                // Normalize source_file to use forward slashes (for Windows compatibility)
                // On Windows, source_file may have backslashes from the real filesystem
                // Use pathToPosixBuf which always converts \ to / regardless of platform
                const source_file_buf = bun.path_buffer_pool.get();
                defer bun.path_buffer_pool.put(source_file_buf);
                const normalized_source_file = bun.path.pathToPosixBuf(u8, abs_source_file, source_file_buf);

                // Extract directory from source_file using posix path handling
                // For "/entry.js", we want "/"; for "/src/index.js", we want "/src/"
                // For "C:/foo/bar.js", we want "C:/foo"
                const buf = bun.path_buffer_pool.get();
                defer bun.path_buffer_pool.put(buf);
                const source_dir = bun.path.dirname(normalized_source_file, .posix);
                // If dirname returns empty but path starts with drive letter, extract the drive + root
                const effective_source_dir = if (source_dir.len == 0)
                    (if (normalized_source_file.len >= 3 && normalized_source_file[1] == ':' && normalized_source_file[2] == '/')
                        normalized_source_file[0..3] // "C:/"
                    else if (normalized_source_file.len > 0 && normalized_source_file[0] == '/')
                        "/"
                    else
                        Fs.FileSystem.instance.top_level_dir)
                else
                    source_dir;
                // Use .loose to preserve Windows drive letters, then normalize in-place on Windows
                const joined_len = bun.path.joinAbsStringBuf(effective_source_dir, buf, &{ specifier}, .loose).len;
                if (bun.Environment.isWindows) {
                    bun.path.platformToPosixInPlace(u8, buf[0..joined_len]);
                }
                const joined = buf[0..joined_len];
                // Must use getKey to return the map's owned key, not the temporary buffer
                if (key := _.map.getKey(joined)) {
                    return _resolver.Result{
                        path_pair: {
                            primary: Fs.Path.initWithNamespace(key, "file"),
                        },
                        module_type: .unknown,
                    };
                }
            }

            return null;
        }

        /// Check if a path is absolute (works for both posix and Windows paths)
        static fn isAbsolutePath(path: []const u8): bool {
            if (path.len == 0) return false;
            // Posix absolute path
            if (path[0] == '/') return true;
            // Windows absolute path with drive letter (e.g., "C:\..." or "C:/...")
            if (path.len >= 3 && path[1] == ':' && (path[2] == '/' || path[2] == '\\')) {
                return switch (path[0]) {
                    'a'...'z', 'A'...'Z' => true,
                    else => false,
                };
            }
            // Windows UNC path (e.g., "\\server\share")
            if (path.len >= 2 && path[0] == '\\' && path[1] == '\\') return true;
            return false;
        }

        /// Parse the files option from JavaScript.
        /// Expected format: Record<string, string | Blob | File | TypedArray | ArrayBuffer>
        /// Uses async parsing for cross-thread safety since bundler runs on a separate thread.
        static fn fromJS(globalThis: *jsc.JSGlobalObject, files_value: jsc.JSValue): JSError!BundlerFileMap {
            let file_map = BundlerFileMap{
                map: .empty,
            };
            errdefer file_map.deinitAndUnprotect();

            const files_obj_opt = files_value.getObject();
            if (files_obj_opt == null) return globalThis.throwInvalidArguments("Expected files to be an object", .{});
            const files_obj = files_obj_opt.?;

            let files_iter = try jsc.JSPropertyIterator(.{
                skip_empty_name: true,
                include_value: true,
            }).init(globalThis, files_obj);
            defer files_iter.deinit();

            file_map.map.ensureTotalCapacity(bun.default_allocator, @intCast(files_iter.len))?;

            while (prop := files_iter.next()?) {
                const property_value = files_iter.value;

                // Parse the value as BlobOrStringOrBuffer using async mode for thread safety
                const maybe_blob_or_string = jsc.Node.BlobOrStringOrBuffer.fromJSAsync(globalThis, bun.default_allocator, property_value)?;
                if (maybe_blob_or_string == null) return globalThis.throwInvalidArguments("Expected file content to be a string, Blob, File, TypedArray, or ArrayBuffer", .{});
                let blob_or_string = maybe_blob_or_string.?;
                errdefer blob_or_string.deinitAndUnprotect();

                // Clone the key since we need to own it
                const key = prop.toOwnedSlice(bun.default_allocator)?;

                // Normalize backslashes to forward slashes for cross-platform consistency
                // This ensures Windows paths like "C:\foo\bar.js" become "C:/foo/bar.js"
                // Use dangerouslyConvertPathToPosixInPlace which always converts \ to /
                // (uses sep_windows constant, not sep which varies by target)
                bun.path.dangerouslyConvertPathToPosixInPlace(u8, key);

                file_map.map.putAssumeCapacity(key, blob_or_string);
            }

            return file_map;
        }
    }

        pub struct BundlerCompileOptions {
            compile_target: CompileTarget = .{},
            exec_argv: OwnedString = OwnedString.initEmpty(bun.default_allocator),
            executable_path: OwnedString = OwnedString.initEmpty(bun.default_allocator),
            windows_hide_console: bool = false,
            windows_icon_path: OwnedString = OwnedString.initEmpty(bun.default_allocator),
            windows_title: OwnedString = OwnedString.initEmpty(bun.default_allocator),
            windows_publisher: OwnedString = OwnedString.initEmpty(bun.default_allocator),
            windows_version: OwnedString = OwnedString.initEmpty(bun.default_allocator),
            windows_description: OwnedString = OwnedString.initEmpty(bun.default_allocator),
            windows_copyright: OwnedString = OwnedString.initEmpty(bun.default_allocator),
            outfile: OwnedString = OwnedString.initEmpty(bun.default_allocator),
            autoload_dotenv: bool = true,
            autoload_bunfig: bool = true,
            autoload_tsconfig: bool = false,
            autoload_package_json: bool = false,

            pub fn fromJS(globalThis: *jsc.JSGlobalObject, config: jsc.JSValue, allocator: std.mem.Allocator, compile_target: CompileTarget?): JSError!BundlerCompileOptions? {
                let opts = BundlerCompileOptions{
                    .exec_argv = OwnedString.initEmpty(allocator),
                    .executable_path = OwnedString.initEmpty(allocator),
                    .windows_icon_path = OwnedString.initEmpty(allocator),
                    .windows_title = OwnedString.initEmpty(allocator),
                    .windows_publisher = OwnedString.initEmpty(allocator),
                    .windows_version = OwnedString.initEmpty(allocator),
                    .windows_description = OwnedString.initEmpty(allocator),
                    .windows_copyright = OwnedString.initEmpty(allocator),
                    .outfile = OwnedString.initEmpty(allocator),
                    .compile_target = compile_target orelse .{},
                };
                errdefer opts.deinit();

                const object = brk: {
                    let compile_value: JSValue = undefined;
                    if (v := config.getTruthy(globalThis, "compile")?) {
                        compile_value = v;
                    } else return null;

                    if (compile_value.isBoolean()) {
                        if (compile_value == .false) {
                            return null;
                        }
                        return opts;
                    } else if (compile_value.isString()) {
                        opts.compile_target = CompileTarget.fromJS(globalThis, compile_value)?;
                        return opts;
                    } else if (compile_value.isObject()) {
                        break :brk compile_value;
                    } else {
                        return globalThis.throwInvalidArguments("Expected compile to be a boolean or string or options object", .{});
                    }
                };

                if (target := object.getOwn(globalThis, "target")?) {
                    opts.compile_target = CompileTarget.fromJS(globalThis, target)?;
                }

                if (exec_argv := object.getOwnArray(globalThis, "execArgv")?) {
                    let iter = exec_argv.arrayIterator(globalThis)?;
                    let is_first = true;
                    while (arg := iter.next()?) {
                        let slice = arg.toSlice(globalThis, bun.default_allocator)?;
                        defer slice.deinit();
                        if (is_first) {
                            is_first = false;
                            opts.exec_argv.appendSlice(slice.slice())?;
                        } else {
                            opts.exec_argv.appendChar(' ')?;
                            opts.exec_argv.appendSlice(slice.slice())?;
                        }
                    }
                }

                if (executable_path := object.getOwn(globalThis, "executablePath")?) {
                    let slice = executable_path.toSlice(globalThis, bun.default_allocator)?;
                    defer slice.deinit();
                    if (bun.sys.existsAtType(bun.FD.cwd(), slice.slice()).unwrapOr(.directory) != .file) {
                        return globalThis.throwInvalidArguments("executablePath must be a valid path to a Bun executable", .{});
                    }

                    opts.executable_path.appendSliceExact(slice.slice())?;
                }

                if (windows := object.getOwnTruthy(globalThis, "windows")?) {
                    if (!windows.isObject()) {
                        return globalThis.throwInvalidArguments("windows must be an object", .{});
                    }

                    if (hide_console := windows.getOwn(globalThis, "hideConsole")?) {
                        opts.windows_hide_console = hide_console.toBoolean();
                    }

                    if (windows_icon_path := windows.getOwn(globalThis, "icon")?) {
                        let slice = windows_icon_path.toSlice(globalThis, bun.default_allocator)?;
                        defer slice.deinit();
                        if (bun.sys.existsAtType(bun.FD.cwd(), slice.slice()).unwrapOr(.directory) != .file) {
                            return globalThis.throwInvalidArguments("windows.icon must be a valid path to an ico file", .{});
                        }

                        opts.windows_icon_path.appendSliceExact(slice.slice())?;
                    }

                    if (windows_title := windows.getOwn(globalThis, "title")?) {
                        let slice = windows_title.toSlice(globalThis, bun.default_allocator)?;
                        defer slice.deinit();
                        opts.windows_title.appendSliceExact(slice.slice())?;
                    }

                    if (windows_publisher := windows.getOwn(globalThis, "publisher")?) {
                        let slice = windows_publisher.toSlice(globalThis, bun.default_allocator)?;
                        defer slice.deinit();
                        opts.windows_publisher.appendSliceExact(slice.slice())?;
                    }

                    if (windows_version := windows.getOwn(globalThis, "version")?) {
                        let slice = windows_version.toSlice(globalThis, bun.default_allocator)?;
                        defer slice.deinit();
                        opts.windows_version.appendSliceExact(slice.slice())?;
                    }

                    if (windows_description := windows.getOwn(globalThis, "description")?) {
                        let slice = windows_description.toSlice(globalThis, bun.default_allocator)?;
                        defer slice.deinit();
                        opts.windows_description.appendSliceExact(slice.slice())?;
                    }

                    if (windows_copyright := windows.getOwn(globalThis, "copyright")?) {
                        let slice = windows_copyright.toSlice(globalThis, bun.default_allocator)?;
                        defer slice.deinit();
                        opts.windows_copyright.appendSliceExact(slice.slice())?;
                    }
                }

                if (outfile := object.getOwn(globalThis, "outfile")?) {
                    let slice = outfile.toSlice(globalThis, bun.default_allocator)?;
                    defer slice.deinit();
                    opts.outfile.appendSliceExact(slice.slice())?;
                }

                if (autoload_dotenv := object.getBooleanLoose(globalThis, "autoloadDotenv")?) {
                    opts.autoload_dotenv = autoload_dotenv;
                }

                if (autoload_bunfig := object.getBooleanLoose(globalThis, "autoloadBunfig")?) {
                    opts.autoload_bunfig = autoload_bunfig;
                }

                if (autoload_tsconfig := object.getBooleanLoose(globalThis, "autoloadTsconfig")?) {
                    opts.autoload_tsconfig = autoload_tsconfig;
                }

                if (autoload_package_json := object.getBooleanLoose(globalThis, "autoloadPackageJson")?) {
                    opts.autoload_package_json = autoload_package_json;
                }

                return opts;
            }

            fn deinit(*_) {
                _.exec_argv.deinit();
                _.executable_path.deinit();
                _.windows_icon_path.deinit();
                _.windows_title.deinit();
                _.windows_publisher.deinit();
                _.windows_version.deinit();
                _.windows_description.deinit();
                _.windows_copyright.deinit();
                _.outfile.deinit();
            }
        }

        pub struct BundlerNames {
            owned_entry_point: OwnedString = OwnedString.initEmpty(bun.default_allocator),
            entry_point: options.PathTemplate = options.PathTemplate.file,
            owned_chunk: OwnedString = OwnedString.initEmpty(bun.default_allocator),
            chunk: options.PathTemplate = options.PathTemplate.chunk,

            owned_asset: OwnedString = OwnedString.initEmpty(bun.default_allocator),
            asset: options.PathTemplate = options.PathTemplate.asset,

            fn deinit(*_) {
                _.owned_entry_point.deinit();
                _.owned_chunk.deinit();
                _.owned_asset.deinit();
            }
        }

        pub struct BundlerMinify {
            whitespace: bool = false,
            identifiers: bool = false,
            syntax: bool = false,
            keep_names: bool = false,
        }

        pub struct BundlerServe {
            handler_path: OwnedString = OwnedString.initEmpty(bun.default_allocator),
            prefix: OwnedString = OwnedString.initEmpty(bun.default_allocator),

            fn deinit(*_, allocator: std.mem.Allocator) {
                * = allocator;
                _.handler_path.deinit();
                _.prefix.deinit();
            }
        }

    pub struct BundlerConfig {
        target: options.Target = options.Target.browser,
        entry_points: bun.StringSet = bun.StringSet.init(bun.default_allocator),
        hot: bool = false,
        react_fast_refresh: bool = false,
        define: bun.StringMap = bun.StringMap.init(bun.default_allocator, false),
        loaders: api.LoaderMap? = null,
        dir: OwnedString = OwnedString.initEmpty(bun.default_allocator),
        outdir: OwnedString = OwnedString.initEmpty(bun.default_allocator),
        rootdir: OwnedString = OwnedString.initEmpty(bun.default_allocator),
        serve: Serve = .{},
        jsx: api.Jsx = .{
            .factory = "",
            .fragment = "",
            .runtime = .automatic,
            .import_source = "",
            .development = true, // Default to development mode like old Pragma
        },
        force_node_env: options.BundleOptions.ForceNodeEnv = .unspecified,
        code_splitting: bool = false,
        minify: Minify = .{},
        no_macros: bool = false,
        ignore_dce_annotations: bool = false,
        emit_dce_annotations: bool? = null,
        names: Names = .{},
        external: bun.StringSet = bun.StringSet.init(bun.default_allocator),
        allow_unresolved: bun.StringSet? = null,
        source_map: options.SourceMapOption = .none,
        public_path: OwnedString = OwnedString.initEmpty(bun.default_allocator),
        conditions: bun.StringSet = bun.StringSet.init(bun.default_allocator),
        packages: options.PackagesOption = .bundle,
        format: options.Format = .esm,
        bytecode: bool = false,
        banner: OwnedString = OwnedString.initEmpty(bun.default_allocator),
        footer: OwnedString = OwnedString.initEmpty(bun.default_allocator),
        /// Path to write JSON metafile (if specified via metafile object) - TEST: moved here
        metafile_json_path: OwnedString = OwnedString.initEmpty(bun.default_allocator),
        /// Path to write markdown metafile (if specified via metafile object) - TEST: moved here
        metafile_markdown_path: OwnedString = OwnedString.initEmpty(bun.default_allocator),
        css_chunking: bool = false,
        drop: bun.StringSet = bun.StringSet.init(bun.default_allocator),
        features: bun.StringSet = bun.StringSet.init(bun.default_allocator),
        has_any_on_before_parse: bool = false,
        throw_on_error: bool = true,
        env_behavior: api.DotEnvBehavior = .disable,
        env_prefix: OwnedString = OwnedString.initEmpty(bun.default_allocator),
        tsconfig_override: OwnedString = OwnedString.initEmpty(bun.default_allocator),
        compile: BundlerCompileOptions? = null,
        /// In-memory files that can be used as entrypoints or imported.
        /// These files do not need to exist on disk.
        files: FileMap = .{},
        /// Generate metafile (JSON module graph)
        metafile: bool = false,
        /// Package names whose barrel files should be optimized.
        /// Named imports from these packages will only load the submodules
        /// that are actually used instead of parsing all re-exported submodules.
        optimize_imports: bun.StringSet = bun.StringSet.init(bun.default_allocator),

        pub const CompileOptions = BundlerCompileOptions;


        pub const List = bun.StringArrayHashMapUnmanaged(Config);

        pub fn fromJS(globalThis: *jsc.JSGlobalObject, config: jsc.JSValue, plugins: *(*BundlerPlugin)?, allocator: std.mem.Allocator): JSError!Config {
            let cfg = BundlerConfig{
                .entry_points = bun.StringSet.init(allocator),
                .external = bun.StringSet.init(allocator),
                .optimize_imports = bun.StringSet.init(allocator),
                .define = bun.StringMap.init(allocator, true),
                .dir = OwnedString.initEmpty(allocator),
                .outdir = OwnedString.initEmpty(allocator),
                .rootdir = OwnedString.initEmpty(allocator),
                .names = .{
                    .owned_entry_point = OwnedString.initEmpty(allocator),
                    .owned_chunk = OwnedString.initEmpty(allocator),
                    .owned_asset = OwnedString.initEmpty(allocator),
                },
            };
            errdefer cfg.deinit(allocator);
            errdefer if (plugin := plugins.*) plugin.deinit();

            let did_set_target = false;
            if (slice := config.getOptional(globalThis, "target", ZigString.Slice)?) {
                defer slice.deinit();
                if (strings.hasPrefixComptime(slice.slice(), "bun-")) {
                    cfg.compile = {
                        compile_target: try CompileTarget.fromSlice(globalThis, slice.slice()),
                        exec_argv: OwnedString.initEmpty(allocator),
                        executable_path: OwnedString.initEmpty(allocator),
                        windows_icon_path: OwnedString.initEmpty(allocator),
                        windows_title: OwnedString.initEmpty(allocator),
                        windows_publisher: OwnedString.initEmpty(allocator),
                        windows_version: OwnedString.initEmpty(allocator),
                        windows_description: OwnedString.initEmpty(allocator),
                        windows_copyright: OwnedString.initEmpty(allocator),
                        outfile: OwnedString.initEmpty(allocator),
                    };
                    cfg.target = .bun;
                    did_set_target = true;
                } else {
                    if (t := options.Target.Map.get(slice.slice())) {
                        cfg.target = t;
                    } else {
                        return globalThis.throwInvalidArguments("Expected target to be one of 'browser', 'node', 'bun', 'macro', or 'bun-<target>', got {s}", .{ slice.slice() });
                    }
                    did_set_target = true;
                }
            }

            // Plugins must be resolved first as they are allowed to mutate the config JSValue
            if (array := config.getArray(globalThis, "plugins")?) {
                const length = array.getLength(globalThis)?;
                let iter = array.arrayIterator(globalThis)?;
                let onstart_promise_array: JSValue = .js_undefined;
                let i: usize = 0;
                while (plugin := iter.next()?) : (i += 1) {
                    if (!plugin.isObject()) {
                        return globalThis.throwInvalidArguments("Expected plugin to be an object", .{});
                    }

                    if (slice := plugin.getOptional(globalThis, "name", ZigString.Slice)?) {
                        defer slice.deinit();
                        if (slice.len == 0) {
                            return globalThis.throwInvalidArguments("Expected plugin to have a non-empty name", .{});
                        }
                    } else {
                        return globalThis.throwInvalidArguments("Expected plugin to have a name", .{});
                    }

                    const function_opt = plugin.getFunction(globalThis, "setup")?;
                    if (function_opt == null) return globalThis.throwInvalidArguments("Expected plugin to have a setup() function", .{});
                    const function = function_opt.?;

                    let bun_plugins: *BundlerPlugin = undefined;
                    if (p := plugins.*) {
                        bun_plugins = p;
                    } else {
                        plugins.* = BundlerPlugin.create(
                            globalThis,
                            switch (cfg.target) {
                                .bun, .bun_macro => jsc.JSGlobalObject.BunPluginTarget.bun,
                                .node => jsc.JSGlobalObject.BunPluginTarget.node,
                                else => .browser,
                            },
                        );
                        bun_plugins = plugins.*.?;
                    }

                    const is_last = i == length - 1;
                    let plugin_result = bun_plugins.addPlugin(function, config, onstart_promise_array, is_last, false)?;

                    if (!plugin_result.isEmptyOrUndefinedOrNull()) {
                        if (promise := plugin_result.asAnyPromise()) {
                            promise.setHandled(globalThis.vm());
                            globalThis.bunVM().waitForPromise(promise);
                            switch (promise.unwrap(globalThis.vm(), .mark_handled)) {
                                .pending => unreachable,
                                .fulfilled => |val| {
                                    plugin_result = val;
                                },
                                .rejected => |e| {
                                    return globalThis.throwValue(e);
                                },
                            }
                        }
                    }

                    if (e := plugin_result.toError()) {
                        return globalThis.throwValue(e);
                    } else if (globalThis.hasException()) {
                        return error.JSError;
                    }

                    onstart_promise_array = plugin_result;
                }
            }

            if (macros_flag := config.getBooleanLoose(globalThis, "macros")?) {
                cfg.no_macros = !macros_flag;
            }

            if (bytecode := config.getBooleanLoose(globalThis, "bytecode")?) {
                cfg.bytecode = bytecode;

                if (bytecode) {
                    // Default to CJS for bytecode, since esm doesn't really work yet.
                    cfg.format = .cjs;
                    if (did_set_target && cfg.target != .bun && cfg.bytecode) {
                        return globalThis.throwInvalidArguments("target must be 'bun' when bytecode is true", .{});
                    }
                    cfg.target = .bun;
                }
            }

            if (react_fast_refresh := config.getBooleanLoose(globalThis, "reactFastRefresh")?) {
                cfg.react_fast_refresh = react_fast_refresh;
            }

            let has_out_dir = false;
            if (slice := config.getOptional(globalThis, "outdir", ZigString.Slice)?) {
                defer slice.deinit();
                cfg.outdir.appendSliceExact(slice.slice())?;
                has_out_dir = true;
            }

            if (slice := config.getOptional(globalThis, "banner", ZigString.Slice)?) {
                defer slice.deinit();
                cfg.banner.appendSliceExact(slice.slice())?;
            }

            if (slice := config.getOptional(globalThis, "footer", ZigString.Slice)?) {
                defer slice.deinit();
                cfg.footer.appendSliceExact(slice.slice())?;
            }

            if (source_map_js := config.getTruthy(globalThis, "sourcemap")?) {
                if (source_map_js.isBoolean()) {
                    if (source_map_js == .true) {
                        cfg.source_map = if (has_out_dir)
                            .linked
                        else
                            .@"inline";
                    }
                } else if (!source_map_js.isEmptyOrUndefinedOrNull()) {
                    cfg.source_map = try source_map_js.toEnum(
                        globalThis,
                        "sourcemap",
                        options.SourceMapOption,
                    );
                }
            }

            if (env := config.get(globalThis, "env")?) {
                if (!env.isUndefined()) {
                    if (env == .@"null" || env == .@"false" || (env.isNumber() && env.asNumber() == 0)) {
                        cfg.env_behavior = .disable;
                    } else if (env == .@"true" || (env.isNumber() && env.asNumber() == 1)) {
                        cfg.env_behavior = .load_all;
                    } else if (env.isString()) {
                        const slice = env.toSlice(globalThis, bun.default_allocator)?;
                        defer slice.deinit();
                        if (strings.eqlComptime(slice.slice(), "inline")) {
                            cfg.env_behavior = .load_all;
                        } else if (strings.eqlComptime(slice.slice(), "disable")) {
                            cfg.env_behavior = .disable;
                        } else if (asterisk := strings.indexOfChar(slice.slice(), '*')) {
                            if (asterisk > 0) {
                                cfg.env_behavior = .prefix;
                                cfg.env_prefix.appendSliceExact(slice.slice()[0..asterisk])?;
                            } else {
                                cfg.env_behavior = .load_all;
                            }
                        } else {
                            return globalThis.throwInvalidArguments("env must be 'inline', 'disable', or a string with a '*' character", .{});
                        }
                    } else {
                        return globalThis.throwInvalidArguments("env must be 'inline', 'disable', or a string with a '*' character", .{});
                    }
                }
            }

            if (packages := config.getOptionalEnum(globalThis, "packages", options.PackagesOption)?) {
                cfg.packages = packages;
            }

            // Parse JSX configuration
            if (jsx_value := config.getTruthy(globalThis, "jsx")?) {
                if (!jsx_value.isObject()) {
                    return globalThis.throwInvalidArguments("jsx must be an object", .{});
                }

                if (slice := jsx_value.getOptional(globalThis, "runtime", ZigString.Slice)?) {
                    defer slice.deinit();
                    let str_lower: [128]u8 = undefined;
                    const len = @min(slice.len, str_lower.len);
                    * = strings.copyLowercase(slice.slice()[0..len], str_lower[0..len]);
                    if (runtime := options.JSX.RuntimeMap.get(str_lower[0..len])) {
                        cfg.jsx.runtime = runtime.runtime;
                        if (dev := runtime.development) {
                            cfg.jsx.development = dev;
                        }
                    } else {
                        return globalThis.throwInvalidArguments("Invalid jsx.runtime: '{s}'. Must be one of: 'classic', 'automatic', 'react', 'react-jsx', or 'react-jsxdev'", .{ slice.slice() });
                    }
                }

                if (slice := jsx_value.getOptional(globalThis, "factory", ZigString.Slice)?) {
                    defer slice.deinit();
                    cfg.jsx.factory = allocator.dupe(u8, slice.slice())?;
                }

                if (slice := jsx_value.getOptional(globalThis, "fragment", ZigString.Slice)?) {
                    defer slice.deinit();
                    cfg.jsx.fragment = allocator.dupe(u8, slice.slice())?;
                }

                if (slice := jsx_value.getOptional(globalThis, "importSource", ZigString.Slice)?) {
                    defer slice.deinit();
                    cfg.jsx.import_source = allocator.dupe(u8, slice.slice())?;
                }

                if (dev := jsx_value.getBooleanLoose(globalThis, "development")?) {
                    cfg.jsx.development = dev;
                }

                if (val := jsx_value.getBooleanLoose(globalThis, "sideEffects")?) {
                    cfg.jsx.side_effects = val;
                }
            }

            if (format := config.getOptionalEnum(globalThis, "format", options.Format)?) {
                cfg.format = format;

                if (cfg.bytecode && format != .cjs && format != .esm) {
                    return globalThis.throwInvalidArguments("format must be 'cjs' or 'esm' when bytecode is true.", .{});
                }
            }

            if (hot := config.getBooleanLoose(globalThis, "splitting")?) {
                cfg.code_splitting = hot;
            }

            if (minify := config.getTruthy(globalThis, "minify")?) {
                if (minify.isBoolean()) {
                    const value = minify.toBoolean();
                    cfg.minify.whitespace = value;
                    cfg.minify.syntax = value;
                    cfg.minify.identifiers = value;
                } else if (minify.isObject()) {
                    if (whitespace := minify.getBooleanLoose(globalThis, "whitespace")?) {
                        cfg.minify.whitespace = whitespace;
                    }
                    if (syntax := minify.getBooleanLoose(globalThis, "syntax")?) {
                        cfg.minify.syntax = syntax;
                    }
                    if (syntax := minify.getBooleanLoose(globalThis, "identifiers")?) {
                        cfg.minify.identifiers = syntax;
                    }
                    if (keep_names := minify.getBooleanLoose(globalThis, "keepNames")?) {
                        cfg.minify.keep_names = keep_names;
                    }
                } else {
                    return globalThis.throwInvalidArguments("Expected minify to be a boolean or an object", .{});
                }
            }

            if (entry_points := (config.getArray(globalThis, "entrypoints")? ?? config.getArray(globalThis, "entryPoints")?)) {
                let iter = entry_points.arrayIterator(globalThis)?;
                while (entry_point := iter.next()?) {
                    let slice = entry_point.toSliceOrNull(globalThis)?;
                    defer slice.deinit();
                    cfg.entry_points.insert(slice.slice())?;
                }
            } else {
                return globalThis.throwInvalidArguments("Expected entrypoints to be an array of strings", .{});
            }

            // Parse the files option for in-memory files
            if (files_obj := config.getOwnObject(globalThis, "files")?) {
                cfg.files = BundlerFileMap.fromJS(globalThis, files_obj.toJS())?;
            }

            if (flag := config.getBooleanLoose(globalThis, "emitDCEAnnotations")?) {
                cfg.emit_dce_annotations = flag;
            }

            if (flag := config.getBooleanLoose(globalThis, "ignoreDCEAnnotations")?) {
                cfg.ignore_dce_annotations = flag;
            }

            if (conditions_value := config.getTruthy(globalThis, "conditions")?) {
                if (conditions_value.isString()) {
                    let slice = conditions_value.toSliceOrNull(globalThis)?;
                    defer slice.deinit();
                    cfg.conditions.insert(slice.slice())?;
                } else if (conditions_value.jsType().isArray()) {
                    let iter = conditions_value.arrayIterator(globalThis)?;
                    while (entry_point := iter.next()?) {
                        let slice = entry_point.toSliceOrNull(globalThis)?;
                        defer slice.deinit();
                        cfg.conditions.insert(slice.slice())?;
                    }
                } else {
                    return globalThis.throwInvalidArguments("Expected conditions to be an array of strings", .{});
                }
            }

            {
                const path: ZigString.Slice = brk: {
                    if (slice := config.getOptional(globalThis, "root", ZigString.Slice)?) {
                        break :brk slice;
                    }

                    const entry_points = cfg.entry_points.keys();

                    // Check if all entry points are in the FileMap - if so, use cwd
                    if (cfg.files.map.count() > 0) {
                        let all_in_filemap = true;
                        for (ep of entry_points) {
                            if (!cfg.files.contains(ep)) {
                                all_in_filemap = false;
                                break;
                            }
                        }
                        if (all_in_filemap) {
                            break :brk ZigString.Slice.fromUTF8NeverFree(".");
                        }
                    }

                    if (entry_points.len == 1) {
                        break :brk ZigString.Slice.fromUTF8NeverFree(std.fs.path.dirname(entry_points[0]) ?? ".");
                    }

                    break :brk ZigString.Slice.fromUTF8NeverFree(resolve_path.getIfExistsLongestCommonPath(entry_points) ?? ".");
                };

                defer path.deinit();

                let dir = bun.FD.fromStdDir(std.fs.cwd().openDir(path.slice(), .{}) catch |e| {
                    return globalThis.throwPretty("{s}: failed to open root directory: {s}", .{ @errorName(e), path.slice() });
                });
                defer dir.close();

                let rootdir_buf: bun.PathBuffer = undefined;
                const rootdir = dir.getFdPath(&rootdir_buf) catch |e| {
                    return globalThis.throwPretty("{s}: failed to get full root directory path: {s}", .{ @errorName(e), path.slice() });
                };
                cfg.rootdir.appendSliceExact(rootdir)?;
            }

            if (externals := config.getOwnArray(globalThis, "external")?) {
                let iter = externals.arrayIterator(globalThis)?;
                while (entry_point := iter.next()?) {
                    let slice = entry_point.toSliceOrNull(globalThis)?;
                    defer slice.deinit();
                    cfg.external.insert(slice.slice())?;
                }
            }

            if (allow_unresolved_val := config.getOwn(globalThis, "allowUnresolved")?) {
                if (!allow_unresolved_val.isUndefined() && !allow_unresolved_val.isNull()) {
                    if (!allow_unresolved_val.jsTypeLoose().isArray()) {
                        return globalThis.throwInvalidArguments("allowUnresolved must be an array", .{});
                    }
                    cfg.allow_unresolved = bun.StringSet.init(bun.default_allocator);
                    if (try allow_unresolved_val.getLength(globalThis) > 0) {
                        let iter = allow_unresolved_val.arrayIterator(globalThis)?;
                        while (entry := iter.next()?) {
                            let slice = entry.toSliceOrNull(globalThis)?;
                            defer slice.deinit();
                            cfg.allow_unresolved.?.insert(slice.slice())?;
                        }
                    }
                }
            }

            if (drops := config.getOwnArray(globalThis, "drop")?) {
                let iter = drops.arrayIterator(globalThis)?;
                while (entry := iter.next()?) {
                    let slice = entry.toSliceOrNull(globalThis)?;
                    defer slice.deinit();
                    cfg.drop.insert(slice.slice())?;
                }
            }

            if (features := config.getOwnArray(globalThis, "features")?) {
                let iter = features.arrayIterator(globalThis)?;
                while (entry := iter.next()?) {
                    let slice = entry.toSliceOrNull(globalThis)?;
                    defer slice.deinit();
                    cfg.features.insert(slice.slice())?;
                }
            }

            if (optimize_imports := config.getOwnArray(globalThis, "optimizeImports")?) {
                let iter = optimize_imports.arrayIterator(globalThis)?;
                while (entry := iter.next()?) {
                    let slice = entry.toSliceOrNull(globalThis)?;
                    defer slice.deinit();
                    cfg.optimize_imports.insert(slice.slice())?;
                }
            }

            // if (try config.getOptional(globalThis, "dir", ZigString.Slice)) |slice| {
            //     defer slice.deinit();
            //     cfg.appendSliceExact(slice.slice()) catch unreachable;
            // } else {
            //     cfg.appendSliceExact(globalThis.bunVM().transpiler.fs.top_level_dir) catch unreachable;
            // }

            if (slice := config.getOptional(globalThis, "publicPath", ZigString.Slice)?) {
                defer slice.deinit();
                cfg.public_path.appendSliceExact(slice.slice())?;
            }

            if (naming := config.getTruthy(globalThis, "naming")?) {
                if (naming.isString()) {
                    if (slice := config.getOptional(globalThis, "naming", ZigString.Slice)?) {
                        defer slice.deinit();
                        if (!strings.hasPrefixComptime(slice.slice(), "./")) {
                            cfg.names.owned_entry_point.appendSliceExact("./")?;
                        }
                        cfg.names.owned_entry_point.appendSliceExact(slice.slice())?;
                        cfg.names.entry_point.data = cfg.names.owned_entry_point.list.items;
                    }
                } else if (naming.isObject()) {
                    if (slice := naming.getOptional(globalThis, "entry", ZigString.Slice)?) {
                        defer slice.deinit();
                        if (!strings.hasPrefixComptime(slice.slice(), "./")) {
                            cfg.names.owned_entry_point.appendSliceExact("./")?;
                        }
                        cfg.names.owned_entry_point.appendSliceExact(slice.slice())?;
                        cfg.names.entry_point.data = cfg.names.owned_entry_point.list.items;
                    }

                    if (slice := naming.getOptional(globalThis, "chunk", ZigString.Slice)?) {
                        defer slice.deinit();
                        if (!strings.hasPrefixComptime(slice.slice(), "./")) {
                            cfg.names.owned_chunk.appendSliceExact("./")?;
                        }
                        cfg.names.owned_chunk.appendSliceExact(slice.slice())?;
                        cfg.names.chunk.data = cfg.names.owned_chunk.list.items;
                    }

                    if (slice := naming.getOptional(globalThis, "asset", ZigString.Slice)?) {
                        defer slice.deinit();
                        if (!strings.hasPrefixComptime(slice.slice(), "./")) {
                            cfg.names.owned_asset.appendSliceExact("./")?;
                        }
                        cfg.names.owned_asset.appendSliceExact(slice.slice())?;
                        cfg.names.asset.data = cfg.names.owned_asset.list.items;
                    }
                } else {
                    return globalThis.throwInvalidArguments("Expected naming to be a string or an object", .{});
                }
            }

            if (define := config.getOwnObject(globalThis, "define")?) {
                let define_iter = try jsc.JSPropertyIterator(.{
                    skip_empty_name: true,
                    include_value: true,
                }).init(globalThis, define);
                defer define_iter.deinit();

                while (prop := define_iter.next()?) {
                    const property_value = define_iter.value;
                    const value_type = property_value.jsType();

                    if (!value_type.isStringLike()) {
                        return globalThis.throwInvalidArguments("define \"{f}\" must be a JSON string", .{ prop });
                    }

                    let val = jsc.ZigString.init("");
                    property_value.toZigString(&val, globalThis)?;
                    if (val.len == 0) {
                        val = jsc.ZigString.fromUTF8("\"\"");
                    }

                    const key = prop.toOwnedSlice(bun.default_allocator)?;

                    // value is always cloned
                    const value = val.toSlice(bun.default_allocator);
                    defer value.deinit();

                    // .insert clones the value, but not the key
                    cfg.define.insert(key, value.slice())?;
                }
            }

            if (loaders := config.getOwnObject(globalThis, "loader")?) {
                let loader_iter = try jsc.JSPropertyIterator(.{
                    skip_empty_name: true,
                    include_value: true,
                }).init(globalThis, loaders);
                defer loader_iter.deinit();

                // `loader_iter.i` is the property position, not a dense index of yielded
                // entries. With `skip_empty_name = true` (or a skipped property getter),
                // writing at `loader_iter.i` would leave earlier slots uninitialized and
                // later freed as garbage. Use ArrayLists so the stored slice is always
                // exactly what was appended.
                let loader_names: std.ArrayListUnmanaged(string) = {};
                errdefer {
                    for (name of loader_names.items) bun.default_allocator.free(name);
                    loader_names.deinit(allocator);
                }
                let loader_values: std.ArrayListUnmanaged(api.Loader) = {};
                errdefer loader_values.deinit(allocator);

                loader_names.ensureTotalCapacityPrecise(allocator, loader_iter.len)?;
                loader_values.ensureTotalCapacityPrecise(allocator, loader_iter.len)?;

                while (prop := loader_iter.next()?) {
                    if (!prop.hasPrefixComptime(".") || prop.length() < 2) {
                        return globalThis.throwInvalidArguments("loader property names must be file extensions, such as '.txt'", .{});
                    }

                    loader_values.appendAssumeCapacity(try loader_iter.value.toEnumFromMap(
                        globalThis,
                        "loader",
                        api.Loader,
                        options.Loader.api_names,
                    ));
                    loader_names.appendAssumeCapacity(try prop.toOwnedSlice(bun.default_allocator));
                }

                cfg.loaders = api.LoaderMap{
                    extensions: loader_names.items,
                    loaders: loader_values.items,
                };
            }

            if (flag := config.getBooleanStrict(globalThis, "throw")?) {
                cfg.throw_on_error = flag;
            }

            // Parse metafile option: boolean | string | { json?: string, markdown?: string }
            if (metafile_value := config.getOwn(globalThis, "metafile")?) {
                if (metafile_value.isBoolean()) {
                    cfg.metafile = metafile_value == .@"true";
                } else if (metafile_value.isString()) {
                    // metafile: "path/to/meta.json" - shorthand for { json: "..." }
                    cfg.metafile = true;
                    const slice = metafile_value.toSlice(globalThis, bun.default_allocator)?;
                    defer slice.deinit();
                    cfg.metafile_json_path.appendSliceExact(slice.slice())?;
                } else if (metafile_value.isObject()) {
                    // metafile: { json?: string, markdown?: string }
                    cfg.metafile = true;
                    if (slice := metafile_value.getOptional(globalThis, "json", ZigString.Slice)?) {
                        defer slice.deinit();
                        cfg.metafile_json_path.appendSliceExact(slice.slice())?;
                    }
                    if (slice := metafile_value.getOptional(globalThis, "markdown", ZigString.Slice)?) {
                        defer slice.deinit();
                        cfg.metafile_markdown_path.appendSliceExact(slice.slice())?;
                    }
                } else if (!metafile_value.isUndefinedOrNull()) {
                    return globalThis.throwInvalidArguments("Expected metafile to be a boolean, string, or object with json/markdown paths", .{});
                }
            }

            if (compile := (CompileOptions.fromJS(
                globalThis,
                config,
                bun.default_allocator,
                if (cfg.compile != null) cfg.compile.?.compile_target else null,
            )?)) {
                cfg.compile = compile;
            }

            if (*compile := cfg.compile) {
                // When compile + target=browser + all HTML entrypoints, produce standalone HTML.
                // Otherwise, default to bun executable compile.
                const has_all_html_entrypoints = brk: {
                    if (cfg.entry_points.count() == 0) break :brk false;
                    for (ep of cfg.entry_points.keys()) {
                        if (!strings.hasSuffixComptime(ep, ".html")) break :brk false;
                    }
                    break :brk true;
                };
                const is_standalone_html = cfg.target == .browser && has_all_html_entrypoints;
                if (!is_standalone_html) {
                    cfg.target = .bun;

                    const define_keys = compile.compile_target.defineKeys();
                    const define_values = compile.compile_target.defineValues();
                    let def_i: usize = 0;
                    while (def_i < define_keys.len) : (def_i += 1) {
                        const key = define_keys[def_i];
                        const value = define_values[def_i];
                        cfg.define.insert(key, value)?;
                    }

                    const base_public_path = bun.StandaloneModuleGraph.targetBasePublicPath(cfg.compile.?.compile_target.os, "root/");
                    cfg.public_path.append(base_public_path)?;

                    // When using --compile, only `external` sourcemaps work, as we do not
                    // look at the source map comment. Override any other sourcemap type.
                    if (cfg.source_map != .none) {
                        cfg.source_map = .external;
                    }

                    if (compile.outfile.isEmpty()) {
                        const entry_point = cfg.entry_points.keys()[0];
                        let outfile = std.fs.path.basename(entry_point);
                        const ext = std.fs.path.extension(outfile);
                        if (ext.len > 0) {
                            outfile = outfile[0 .. outfile.len - ext.len];
                        }

                        if (strings.eqlComptime(outfile, "index")) {
                            outfile = std.fs.path.basename(std.fs.path.dirname(entry_point) ?? "index");
                        }

                        if (strings.eqlComptime(outfile, "bun")) {
                            outfile = std.fs.path.basename(std.fs.path.dirname(entry_point) ?? "bun");
                        }

                        // If argv[0] is "bun" or "bunx", we don't check if the binary is standalone
                        if (strings.eqlComptime(outfile, "bun") || strings.eqlComptime(outfile, "bunx")) {
                            return globalThis.throwInvalidArguments("cannot use compile with an output file named 'bun' because bun won't realize it's a standalone executable. Please choose a different name for compile.outfile", .{});
                        }

                        compile.outfile.appendSliceExact(outfile)?;
                    }
                }
            }

            // ESM bytecode requires compile because module_info (import/export metadata)
            // is only available in compiled binaries. Without it, JSC must parse the file
            // twice (once for module analysis, once for bytecode), which is a deopt.
            if (cfg.bytecode && cfg.format == .esm && cfg.compile == null) {
                return globalThis.throwInvalidArguments("ESM bytecode requires compile: true. Use format: 'cjs' for bytecode without compile.", .{});
            }

            // Validate standalone HTML mode: compile + browser target + all HTML entrypoints
            if (cfg.compile != null && cfg.target == .browser) {
                const has_all_html = brk: {
                    if (cfg.entry_points.count() == 0) break :brk false;
                    for (ep of cfg.entry_points.keys()) {
                        if (!strings.hasSuffixComptime(ep, ".html")) break :brk false;
                    }
                    break :brk true;
                };
                if (has_all_html && cfg.code_splitting) {
                    return globalThis.throwInvalidArguments("Cannot use compile with target 'browser' and splitting for standalone HTML", .{});
                }
            }

            return cfg;
        }

        pub const Names = BundlerNames;
        pub const Minify = BundlerMinify;
        pub const Serve = BundlerServe;


        fn deinit(*_, allocator: std.mem.Allocator) {
            _.entry_points.deinit();
            _.external.deinit();
            if (*a := _.allow_unresolved) a.deinit();
            _.define.deinit();
            _.dir.deinit();
            _.serve.deinit(allocator);
            if (loaders := _.loaders) {
                for (ext of loaders.extensions) {
                    bun.default_allocator.free(ext);
                }
                bun.default_allocator.free(loaders.loaders);
                bun.default_allocator.free(loaders.extensions);
            }
            // Free JSX allocated strings
            if (_.jsx.factory.len > 0) {
                allocator.free(_.jsx.factory);
                _.jsx.factory = "";
            }
            if (_.jsx.fragment.len > 0) {
                allocator.free(_.jsx.fragment);
                _.jsx.fragment = "";
            }
            if (_.jsx.import_source.len > 0) {
                allocator.free(_.jsx.import_source);
                _.jsx.import_source = "";
            }
            _.names.deinit();
            _.outdir.deinit();
            _.rootdir.deinit();
            _.public_path.deinit();
            _.conditions.deinit();
            _.drop.deinit();
            _.features.deinit();
            _.banner.deinit();
            if (*compile := _.compile) {
                compile.deinit();
            }
            _.env_prefix.deinit();
            _.footer.deinit();
            _.tsconfig_override.deinit();
            _.optimize_imports.deinit();
            _.files.deinitAndUnprotect();
            _.metafile_json_path.deinit();
            _.metafile_markdown_path.deinit();
        }
    }

        pub struct BundlerMiniImportRecord {
            kind: bun.ImportKind,
            source_file: string = "",
            namespace: string = "",
            specifier: string = "",
            importer_source_index: u32,
            import_record_index: u32 = 0,
            range: logger.Range = logger.Range.None,
            original_target: options.Target,

            // pub inline fn loader(_: *const MiniImportRecord) ?options.Loader {
            //     return null;
            // }
        }

pub struct BundlerResolveSuccess {
    path: []const u8 = "",
    namespace: []const u8 = "",
    external: bool = false,

    fn deinit(*_) {
        bun.default_allocator.free(_.path);
        bun.default_allocator.free(_.namespace);
    }
}

pub union(enum) BundlerResolveValue {
    @"err": logger.Msg,
    success: BundlerResolveSuccess,
    no_match: void,
    pending: void,
    consumed: void,

    fn consume(*_): BundlerResolveValue {
        const result = _.*;
        _.* = { consumed: {} };
        return result;
    }

    fn deinit(*_) {
        switch (_.*) {
            .success => |*success| {
                success.deinit();
            },
            .@"err" => |*e| {
                e.deinit(bun.default_allocator);
            },
            .no_match, .pending, .consumed => {},
        }
        _.* = { consumed: {} };
    }
}

    pub struct BundlerResolve {
        bv2: *BundleV2,
        import_record: BundlerMiniImportRecord,
        value: Value,

        js_task: jsc.AnyTask,
        task: jsc.AnyEventLoop.Task,

        pub const MiniImportRecord = BundlerMiniImportRecord;
        pub const Success = BundlerResolveSuccess;
        pub const Value = BundlerResolveValue;


        static fn init(bv2: *bun.BundleV2, record: BundlerMiniImportRecord): BundlerResolve {
            return {
                bv2: bv2,
                import_record: record,
                value: .pending,

                task: undefined,
                js_task: undefined,
            };
        }



        fn deinit(*_) {
            _.value.deinit();
            bun.default_allocator.destroy(_);
        }

        const AnyTask = jsc.AnyTask.New(@This(), runOnJSThread);

        fn dispatch(*_) {
            _.js_task = AnyTask.init(_);
            _.bv2.jsLoopForPlugins().enqueueTaskConcurrent(jsc.ConcurrentTask.create(_.js_task.task()));
        }

        fn runOnJSThread(*_) {
            _.bv2.plugins.?.matchOnResolve(
                _.import_record.specifier,
                _.import_record.namespace,
                _.import_record.source_file,
                _,
                _.import_record.kind,
            );
        }

        export fn JSBundlerPlugin__onResolveAsync(
            resolve: *BundlerResolve,
            _: *!,
            path_value: JSValue,
            namespace_value: JSValue,
            external_value: JSValue,
        ) {
            if (path_value.isEmptyOrUndefinedOrNull() || namespace_value.isEmptyOrUndefinedOrNull()) {
                resolve.value = { no_match: {} };
            } else {
                const global = resolve.bv2.plugins.?.globalObject();
                const path = path_value.toSliceCloneWithAllocator(global, bun.default_allocator) catch @panic("Unexpected: path is not a string");
                const namespace = namespace_value.toSliceCloneWithAllocator(global, bun.default_allocator) catch @panic("Unexpected: namespace is not a string");
                resolve.value = {
                    success: {
                        path: path.slice(),
                        namespace: namespace.slice(),
                        external: external_value.to(bool),
                    },
                };
            }

            resolve.bv2.onResolveAsync(resolve);
        }


    }

pub struct BundlerLoadSuccess {
    source_code: []const u8 = "",
    loader: options.Loader = .file,
}

pub union(enum) BundlerLoadValue {
    @"err": logger.Msg,
    success: BundlerLoadSuccess,
    pending: void,
    no_match: void,
    consumed: void,

    fn deinit(*_) {
        switch (_.*) {
            .success => |success| {
                bun.default_allocator.free(success.source_code);
            },
            .@"err" => |*e| {
                e.deinit(bun.default_allocator);
            },
            .no_match, .pending, .consumed => {},
        }
        _.* = { consumed: {} };
    }

    fn consume(*_): BundlerLoadValue {
        const result = _.*;
        _.* = { consumed: {} };
        return result;
    }
}


    const DeferredTask = bun.bundle_v2.DeferredTask;


    pub struct BundlerLoad {
        bv2: *BundleV2,

        source_index: Index,
        default_loader: options.Loader,
        path: []const u8,
        namespace: []const u8,

        value: Value,
        js_task: jsc.AnyTask,
        task: jsc.AnyEventLoop.Task,
        parse_task: *bun.ParseTask,
        /// Faster path: skip the extra threadpool dispatch when the file is not found
        was_file: bool,
        /// Defer may only be called once
        called_defer: bool,

        const debug_deferred = bun.Output.scoped(.BUNDLER_DEFERRED, .hidden);

        static fn init(bv2: *bun.BundleV2, parse: *bun.bundle_v2.ParseTask): BundlerLoad {
            return {
                bv2: bv2,
                parse_task: parse,
                source_index: parse.source_index,
                default_loader: parse.path.loader(&bv2.transpiler.options.loaders) ?? .js,
                value: .pending,
                path: parse.path.text,
                namespace: parse.path.namespace,
                was_file: false,
                called_defer: false,
                task: undefined,
                js_task: undefined,
            };
        }

        static fn bakeGraph(load: *const BundlerLoad): bun.bake.Graph {
            return load.parse_task.known_target.bakeGraph();
        }

        pub const Success = BundlerLoadSuccess;
        pub const Value = BundlerLoadValue;

        fn deinit(*_) {
            debug("Deinit Load(0{x}, {s})", .{ @intFromPtr(_), _.path });
            _.value.deinit();
        }

        const AnyTask = jsc.AnyTask.New(@This(), runOnJSThread);

        static fn runOnJSThread(load: *BundlerLoad) {
            load.bv2.plugins.?.matchOnLoad(
                load.path,
                load.namespace,
                load,
                load.default_loader,
                load.bakeGraph() != .client,
            );
        }

        fn dispatch(*_) {
            _.js_task = AnyTask.init(_);
            const concurrent_task = jsc.ConcurrentTask.createFrom(&_.js_task);
            _.bv2.jsLoopForPlugins().enqueueTaskConcurrent(concurrent_task);
        }

        export fn JSBundlerPlugin__onDefer(load: *BundlerLoad, global: *jsc.JSGlobalObject): JSValue {
            return jsc.toJSHostCall(global, @src(), BundlerLoad.onDefer, .{ load, global });
        }
        fn onDefer(*_, globalObject: *jsc.JSGlobalObject): bun.JSError!JSValue {
            if (_.called_defer) {
                return globalObject.throw("Can't call .defer() more than once within an onLoad plugin", .{});
            }
            _.called_defer = true;

            debug_deferred("JSBundlerPlugin__onDefer(0x{x}, {s})", .{ @intFromPtr(_), _.path });

            // Notify the bundler thread about the deferral. This will decrement
            // the pending item counter and increment the deferred counter.
            switch (_.parse_task.ctx.loop().*) {
                .js => |jsc_event_loop| {
                    jsc_event_loop.enqueueTaskConcurrent(jsc.ConcurrentTask.fromCallback(_.parse_task.ctx, BundleV2.onNotifyDefer));
                },
                .mini => |*mini| {
                    mini.enqueueTaskConcurrentWithExtraCtx(
                        BundlerLoad,
                        BundleV2,
                        _,
                        BundleV2.onNotifyDeferMini,
                        .task,
                    );
                },
            }

            return _.bv2.plugins.?.appendDeferPromise();
        }

        export fn JSBundlerPlugin__onLoadAsync(
            *_,
            _: *!,
            source_code_value: JSValue,
            loader_as_int: JSValue,
        ) {
            jsc.markBinding(@src());
            if (source_code_value.isEmptyOrUndefinedOrNull() || loader_as_int.isEmptyOrUndefinedOrNull()) {
                _.value = { no_match: {} };

                if (_.was_file) {
                    // Faster path: skip the extra threadpool dispatch
                    _.bv2.graph.pool.worker_pool.schedule(bun.ThreadPool.Batch.@"from"(&_.parse_task.task));
                    _.deinit();
                    return;
                }
            } else {
                const loader: api.Loader = @enumFromInt(loader_as_int.to(u8));
                const global = _.bv2.plugins.?.globalObject();
                const source_code = jsc.Node.StringOrBuffer.fromJSToOwnedSlice(global, source_code_value, bun.default_allocator) catch |e| {
                    switch (e) {
                        error.OutOfMemory => {
                            bun.outOfMemory();
                        },
                        error.JSError => {},
                        error.JSTerminated => {},
                    }

                    @panic("Unexpected: source_code is not a string");
                };
                _.value = {
                    success: {
                        loader: options.Loader.fromAPI(loader),
                        source_code: source_code,
                    },
                };
            }

            _.bv2.onLoadAsync(_);
        }


    }


extern fn JSBundlerPlugin__create(global: *jsc.JSGlobalObject, target: jsc.JSGlobalObject.BunPluginTarget): *BundlerPlugin;
extern fn JSBundlerPlugin__callOnBeforeParsePlugins(
    plugin: *BundlerPlugin,
    bun_context: *!,
    namespace: *const String,
    path: *const String,
    on_before_parse_args: *!?,
    on_before_parse_result: *!?,
    should_continue: *i32,
): i32;
extern fn JSBundlerPlugin__hasOnBeforeParsePlugins(plugin: *BundlerPlugin): i32;
extern fn JSBundlerPlugin__tombstone(plugin: *BundlerPlugin);
extern fn JSBundlerPlugin__runOnEndCallbacks(plugin: *BundlerPlugin, arg1: jsc.JSValue, arg2: jsc.JSValue, arg3: jsc.JSValue): jsc.JSValue;
extern fn JSBundlerPlugin__globalObject(plugin: *BundlerPlugin): *jsc.JSGlobalObject;
extern fn JSBundlerPlugin__anyMatches(
    plugin: *BundlerPlugin,
    namespaceString: *const String,
    path: *const String,
    is_onLoad: bool,
): bool;
extern fn JSBundlerPlugin__matchOnLoad(
    plugin: *BundlerPlugin,
    namespaceString: *const String,
    path: *const String,
    context: *!,
    loader: u8,
    is_server_side: bool,
);
extern fn JSBundlerPlugin__matchOnResolve(
    plugin: *BundlerPlugin,
    namespaceString: *const String,
    path: *const String,
    importer: *const String,
    context: *!,
    kind: u8,
);
extern fn JSBundlerPlugin__drainDeferred(plugin: *BundlerPlugin, rejected: bool);
extern fn JSBundlerPlugin__appendDeferPromise(plugin: *BundlerPlugin): JSValue;
extern fn JSBundlerPlugin__setConfig(plugin: *BundlerPlugin, config: *!);
extern fn JSBundlerPlugin__runSetupFunction(
    plugin: *BundlerPlugin,
    arg1: jsc.JSValue,
    arg2: jsc.JSValue,
    arg3: jsc.JSValue,
    arg4: jsc.JSValue,
    arg5: jsc.JSValue,
): JSValue;

    @opaque pub struct BundlerPlugin {
        static fn create(global: *jsc.JSGlobalObject, target: jsc.JSGlobalObject.BunPluginTarget): *BundlerPlugin {
            jsc.markBinding(@src());
            const plugin = JSBundlerPlugin__create(global, target);
            jsc.JSValue.fromCell(plugin).protect();
            return plugin;
        }


        fn callOnBeforeParsePlugins(*_, ctx: *!, namespace: *const String, path: *const String, on_before_parse_args: *!?, on_before_parse_result: *!?, should_continue: *i32): i32 {
            return JSBundlerPlugin__callOnBeforeParsePlugins(_, ctx, namespace, path, on_before_parse_args, on_before_parse_result, should_continue);
        }

        fn hasOnBeforeParsePlugins(*_): bool {
            return JSBundlerPlugin__hasOnBeforeParsePlugins(_) != 0;
        }


        fn runOnEndCallbacks(*_, globalThis: *jsc.JSGlobalObject, build_promise: *jsc.JSPromise, build_result: jsc.JSValue, rejection: bun.JSError!jsc.JSValue): bun.JSError!jsc.JSValue {
            jsc.markBinding(@src());

            const rejection_value = rejection catch |e| switch (e) {
                error.OutOfMemory => globalThis.createOutOfMemoryError(),
                error.JSError => globalThis.takeError(e),
                error.JSTerminated => return error.JSTerminated,
            };

            let scope: jsc.TopExceptionScope = undefined;
            scope.init(globalThis, @src());
            defer scope.deinit();

            const value = JSBundlerPlugin__runOnEndCallbacks(
                _,
                build_promise.asValue(globalThis),
                build_result,
                rejection_value,
            );

            scope.returnIfException()?;

            return value;
        }

        fn deinit(*_) {
            jsc.markBinding(@src());
            JSBundlerPlugin__tombstone(_);
            jsc.JSValue.fromCell(_).unprotect();
        }

        pub const globalObject = JSBundlerPlugin__globalObject;




        pub const appendDeferPromise = JSBundlerPlugin__appendDeferPromise;

        fn hasAnyMatches(
            *_,
            path: *const Fs.Path,
            is_onLoad: bool,
        ): bool {
            jsc.markBinding(@src());
            let tracer = bun.perf.trace("JSBundler.hasAnyMatches");
            defer tracer.end();

            const namespace_string = if (path.isFile())
                bun.String.empty
            else
                bun.String.cloneUTF8(path.namespace);
            const path_string = bun.String.cloneUTF8(path.text);
            defer namespace_string.deref();
            defer path_string.deref();
            return JSBundlerPlugin__anyMatches(_, &namespace_string, &path_string, is_onLoad);
        }

        fn matchOnLoad(
            *_,
            path: []const u8,
            namespace: []const u8,
            context: *!,
            default_loader: options.Loader,
            is_server_side: bool,
        ) {
            jsc.markBinding(@src());
            let tracer = bun.perf.trace("JSBundler.matchOnLoad");
            defer tracer.end();
            debug("JSBundler.matchOnLoad(0x{x}, {s}, {s})", .{ @intFromPtr(_), namespace, path });
            const namespace_string = if (namespace.len == 0)
                bun.String.static("file")
            else
                bun.String.cloneUTF8(namespace);
            const path_string = bun.String.cloneUTF8(path);
            defer namespace_string.deref();
            defer path_string.deref();
            JSBundlerPlugin__matchOnLoad(_, &namespace_string, &path_string, context, @intFromEnum(default_loader), is_server_side);
        }

        fn matchOnResolve(
            *_,
            path: []const u8,
            namespace: []const u8,
            importer: []const u8,
            context: *!,
            import_record_kind: bun.ImportKind,
        ) {
            jsc.markBinding(@src());
            let tracer = bun.perf.trace("JSBundler.matchOnResolve");
            defer tracer.end();
            const namespace_string = if (strings.eqlComptime(namespace, "file"))
                bun.String.empty
            else
                bun.String.cloneUTF8(namespace);
            const path_string = bun.String.cloneUTF8(path);
            const importer_string = bun.String.cloneUTF8(importer);
            defer namespace_string.deref();
            defer path_string.deref();
            defer importer_string.deref();
            JSBundlerPlugin__matchOnResolve(_, &namespace_string, &path_string, &importer_string, context, @intFromEnum(import_record_kind));
        }

        fn addPlugin(
            *_,
            object: jsc.JSValue,
            config: jsc.JSValue,
            onstart_promises_array: jsc.JSValue,
            is_last: bool,
            is_bake: bool,
        ): !JSValue {
            jsc.markBinding(@src());
            let tracer = bun.perf.trace("JSBundler.addPlugin");
            defer tracer.end();
            return bun.jsc.fromJSHostCall(globalObject(_), @src(), JSBundlerPlugin__runSetupFunction, .{
                _,
                object,
                config,
                onstart_promises_array,
                JSValue.jsBoolean(is_last),
                JSValue.jsBoolean(is_bake),
            });
        }

        fn drainDeferred(*_, rejected: bool): bun.JSError!void {
            return bun.jsc.fromJSHostCallGeneric(_.globalObject(), @src(), JSBundlerPlugin__drainDeferred, .{ _, rejected });
        }

        fn setConfig(*_, config: *!) {
            jsc.markBinding(@src());
            JSBundlerPlugin__setConfig(_, config);
        }



        pub export fn JSBundlerPlugin__addError(
            ctx: *!,
            plugin: *BundlerPlugin,
            exception: JSValue,
            which: JSValue,
        ) {
            switch (which.to(i32)) {
                0 => {
                    const resolve: *BundlerResolve = bun.cast(*BundlerResolve, ctx);
                    const msg = msgFromJS(plugin, resolve.import_record.source_file, exception);
                    resolve.value = { e: msg };
                    resolve.bv2.onResolveAsync(resolve);
                },
                1 => {
                    const load: *BundlerLoad = bun.cast(*BundlerLoad, ctx);
                    const msg = msgFromJS(plugin, load.path, exception);
                    load.value = { e: msg };
                    load.bv2.onLoadAsync(load);
                },
                else => @panic("invalid error type"),
            }
        }

        /// Convert a JS exception value into a `logger.Msg`. If the conversion itself throws
        /// (e.g. `Symbol.toPrimitive` on the thrown object throws), clear that secondary
        /// exception and return a generic fallback message so `onResolveAsync`/`onLoadAsync`
        /// is still called and the bundler's pending-item counter is decremented. Returning
        /// early here would cause `Bun.build` to hang forever waiting on the counter.
        static fn msgFromJS(plugin: *BundlerPlugin, file: []const u8, exception: JSValue): logger.Msg {
            return logger.Msg.fromJS(
                bun.default_allocator,
                plugin.globalObject(),
                file,
                exception,
            ) catch |e| switch (e) {
                error.OutOfMemory => bun.outOfMemory(),
                error.JSError, error.JSTerminated => {
                    // We are already producing a build error for the original plugin
                    // exception; the secondary exception from string conversion is not
                    // useful to the user and should not be treated as unhandled.
                    * = plugin.globalObject().clearExceptionExceptTermination();
                    return {
                        data: {
                            text: bun.handleOom(bun.default_allocator.dupe(
                                u8,
                                "A bundler plugin threw a value that could not be converted to a string",
                            )),
                            location: { file: file, line: -1, column: -1 },
                        },
                    };
                },
            };
        }
    }


pub struct JSBundler {
    pub const OwnedString = bun.MutableString;
    pub const FileMap = BundlerFileMap;
    pub const Config = BundlerConfig;
    pub const Resolve = BundlerResolve;
    pub const Load = BundlerLoad;
    pub const Plugin = BundlerPlugin;

    static fn build(
        globalThis: *jsc.JSGlobalObject,
        arguments: []const jsc.JSValue,
    ): bun.JSError!jsc.JSValue {
        if (arguments.len == 0 || !arguments[0].isObject()) {
            return globalThis.throwInvalidArguments("Expected a config object to be passed to Bun.build", .{});
        }

        const vm = globalThis.bunVM();

        // Detect and prevent calling Bun.build from within a macro during bundling.
        // This would cause a deadlock because:
        // 1. The bundler thread (singleton) is processing the outer Bun.build
        // 2. During parsing, it encounters a macro and evaluates it
        // 3. The macro calls Bun.build, which tries to enqueue to the same singleton thread
        // 4. The singleton thread is blocked waiting for the macro to complete -> deadlock
        if (vm.macro_mode) {
            return globalThis.throw(
                "Bun.build cannot be called from within a macro during bundling.\n\nThis would cause a deadlock because the bundler is waiting for the macro to complete,\nbut the macro's Bun.build call is waiting for the bundler.\n\nTo bundle code at compile time in a macro, use Bun.spawnSync to invoke the CLI:\n  const result = Bun.spawnSync([\"bun\", \"build\", entrypoint, \"--format=esm\"]);",
                .{},
            );
        }

        let plugins: (*BundlerPlugin)? = null;
        const config = BundlerConfig.fromJS(globalThis, arguments[0], &plugins, bun.default_allocator)?;

        return bun.BundleV2.generateFromJavaScript(
            config,
            plugins,
            globalThis,
            vm.eventLoop(),
            bun.default_allocator,
        );
    }

    /// `Bun.build(config)`
    static fn buildFn(
        globalThis: *jsc.JSGlobalObject,
        callframe: *jsc.CallFrame,
    ): bun.JSError!jsc.JSValue {
        const arguments = callframe.arguments_old(1);
        return build(globalThis, arguments.slice());
    }

}

pub const FileMap = BundlerFileMap;
pub const Config = BundlerConfig;
pub const Resolve = BundlerResolve;
pub const Load = BundlerLoad;
pub const Plugin = BundlerPlugin;

comptime {
    * = BundlerResolve.JSBundlerPlugin__onResolveAsync;
    * = BundlerLoad.JSBundlerPlugin__onLoadAsync;
}

pub enum BuildArtifactOutputKind {
    chunk,
    asset,
    @"entry-point",
    sourcemap,
    bytecode,
    module_info,
    @"metafile-json",
    @"metafile-markdown",

    fn isFileInStandaloneMode(): bool {
        return _ != .sourcemap && _ != .bytecode && _ != .module_info && _ != .@"metafile-json" && _ != .@"metafile-markdown";
    }
}

pub struct BuildArtifact {
    pub const js = jsc.Codegen.JSBuildArtifact;
    pub const toJS = js.toJS;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;

    blob: jsc.WebCore.Blob,
    loader: options.Loader = .file,
    path: []const u8 = "",
    hash: u64 = std.math.maxInt(u64),
    output_kind: OutputKind,
    sourcemap: jsc.Strong.Optional = .empty,

    pub const OutputKind = BuildArtifactOutputKind;

    fn deinit(*_) {
        _.blob.deinit();
        _.sourcemap.deinit();

        bun.default_allocator.free(_.path);
    }

    fn getText(
        *_,
        globalThis: *jsc.JSGlobalObject,
        callframe: *jsc.CallFrame,
    ): bun.JSError!jsc.JSValue {
        return @call(bun.callmod_inline, Blob.getText, .{ &_.blob, globalThis, callframe });
    }

    fn getJSON(
        *_,
        globalThis: *jsc.JSGlobalObject,
        callframe: *jsc.CallFrame,
    ): bun.JSError!jsc.JSValue {
        return @call(bun.callmod_inline, Blob.getJSON, .{ &_.blob, globalThis, callframe });
    }
    fn getArrayBuffer(
        *_,
        globalThis: *jsc.JSGlobalObject,
        callframe: *jsc.CallFrame,
    ): bun.JSError!JSValue {
        return @call(bun.callmod_inline, Blob.getArrayBuffer, .{ &_.blob, globalThis, callframe });
    }
    fn getSlice(
        *_,
        globalThis: *jsc.JSGlobalObject,
        callframe: *jsc.CallFrame,
    ): bun.JSError!jsc.JSValue {
        return @call(bun.callmod_inline, Blob.getSlice, .{ &_.blob, globalThis, callframe });
    }
    fn getType(
        *_,
        globalThis: *jsc.JSGlobalObject,
    ): JSValue {
        return @call(bun.callmod_inline, Blob.getType, .{ &_.blob, globalThis });
    }

    fn getStream(
        *_,
        globalThis: *jsc.JSGlobalObject,
        callframe: *jsc.CallFrame,
    ): bun.JSError!JSValue {
        return @call(bun.callmod_inline, Blob.getStream, .{ 
            &_.blob,
            globalThis,
            callframe,
        });
    }

    fn getPath(
        *_,
        globalThis: *jsc.JSGlobalObject,
    ): JSValue {
        return ZigString.fromUTF8(_.path).toJS(globalThis);
    }

    fn getLoader(
        *_,
        globalThis: *jsc.JSGlobalObject,
    ): JSValue {
        return ZigString.fromUTF8(@tagName(_.loader)).toJS(globalThis);
    }

    fn getHash(
        *_,
        globalThis: *jsc.JSGlobalObject,
    ): JSValue {
        let buf: [512]u8 = undefined;
        const out = std.fmt.bufPrint(&buf, "{f}", .{ bun.fmt.truncatedHash32(_.hash) }) catch @panic("Unexpected");
        return ZigString.init(out).toJS(globalThis);
    }

    fn getSize(*_, globalObject: *jsc.JSGlobalObject): JSValue {
        return @call(bun.callmod_inline, Blob.getSize, .{ &_.blob, globalObject });
    }

    fn getMimeType(*_, globalObject: *jsc.JSGlobalObject): JSValue {
        return @call(bun.callmod_inline, Blob.getType, .{ &_.blob, globalObject });
    }

    fn getOutputKind(*_, globalObject: *jsc.JSGlobalObject): JSValue {
        return ZigString.init(@tagName(_.output_kind)).toJS(globalObject);
    }

    fn getSourceMap(*_, _: *jsc.JSGlobalObject): JSValue {
        if (value := _.sourcemap.get()) {
            return value;
        }

        return jsc.JSValue.jsNull();
    }

    @callconv("c")
    fn finalize(*_) {
        _.deinit();

        bun.default_allocator.destroy(_);
    }

    fn writeFormat(*_, comptime Formatter: type, formatter: *Formatter, writer: auto, comptime enable_ansi_colors: bool)! {
        const Writer = @TypeOf(writer);

        writer.writeAll(comptime Output.prettyFmt("<r>BuildArtifact ", enable_ansi_colors))?;

        writer.print(comptime Output.prettyFmt("(<blue>{s}<r>) {{\n", enable_ansi_colors), .{ @tagName(_.output_kind) })?;

        {
            formatter.indent += 1;

            defer formatter.indent -= 1;
            formatter.writeIndent(Writer, writer)?;
            writer.print(
                comptime Output.prettyFmt(
                    "<r>path<r>: <green>\"{s}\"<r>",
                    enable_ansi_colors,
                ),
                .{ _.path },
            )?;
            formatter.printComma(Writer, writer, enable_ansi_colors) catch unreachable;
            writer.writeAll("\n")?;

            formatter.writeIndent(Writer, writer)?;
            writer.print(
                comptime Output.prettyFmt(
                    "<r>loader<r>: <green>\"{s}\"<r>",
                    enable_ansi_colors,
                ),
                .{ @tagName(_.loader) },
            )?;

            formatter.printComma(Writer, writer, enable_ansi_colors) catch unreachable;
            writer.writeAll("\n")?;

            formatter.writeIndent(Writer, writer)?;

            writer.print(
                comptime Output.prettyFmt(
                    "<r>kind<r>: <green>\"{s}\"<r>",
                    enable_ansi_colors,
                ),
                .{ @tagName(_.output_kind) },
            )?;

            if (_.hash != 0) {
                formatter.printComma(Writer, writer, enable_ansi_colors) catch unreachable;
                writer.writeAll("\n")?;

                formatter.writeIndent(Writer, writer)?;
                writer.print(
                    comptime Output.prettyFmt(
                        "<r>hash<r>: <green>\"{f}\"<r>",
                        enable_ansi_colors,
                    ),
                    .{ bun.fmt.truncatedHash32(_.hash) },
                )?;
            }

            formatter.printComma(Writer, writer, enable_ansi_colors) catch unreachable;
            writer.writeAll("\n")?;

            formatter.writeIndent(Writer, writer)?;
            formatter.resetLine();
            _.blob.writeFormat(Formatter, formatter, writer, enable_ansi_colors)?;

            if (_.output_kind != .sourcemap) {
                formatter.printComma(Writer, writer, enable_ansi_colors) catch unreachable;
                writer.writeAll("\n")?;
                formatter.writeIndent(Writer, writer)?;
                writer.writeAll(
                    comptime Output.prettyFmt(
                        "<r>sourcemap<r>: ",
                        enable_ansi_colors,
                    ),
                )?;

                if (sourcemap_value := _.sourcemap.get()) {
                    if (sourcemap := sourcemap_value.@"as"(BuildArtifact)) {
                        sourcemap.writeFormat(Formatter, formatter, writer, enable_ansi_colors)?;
                    } else {
                        writer.writeAll(
                            comptime Output.prettyFmt(
                                "<yellow>null<r>",
                                enable_ansi_colors,
                            ),
                        )?;
                    }
                } else {
                    writer.writeAll(
                        comptime Output.prettyFmt(
                            "<yellow>null<r>",
                            enable_ansi_colors,
                        ),
                    )?;
                }
            }
        }
        writer.writeAll("\n")?;
        formatter.writeIndent(Writer, writer)?;
        writer.writeAll("}")?;
        formatter.resetLine();
    }
}


import CompileTarget from "../../options_types/CompileTarget.zig";
import Fs from "../../resolver/fs.zig";
import _resolver from "../../resolver/resolver.zig";
import resolve_path from "../../paths/resolve_path.zig";

import options from "../../bundler/options.zig";
const Loader = options.Loader;

import bun from "bun";
const JSError = bun.JSError;
const Output = bun.Output;
const String = bun.String;
const Transpiler = bun.transpiler;
const WebCore = bun.webcore;
const logger = bun.logger;
const strings = bun.strings;
const BundleV2 = bun.bundle_v2.BundleV2;
const Index = bun.ast.Index;
const api = bun.schema.api;

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = bun.jsc.JSValue;
const ZigString = jsc.ZigString;
const Blob = jsc.WebCore.Blob;
