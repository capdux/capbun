struct CloseGuard<T> {
    val: T,
    ~init() {
        _.val.close();
    }
};

struct FreeGuard<T> {
    allocator: std.mem.Allocator,
    slice: T,
    ~init() {
        _.allocator.free(_.slice);
    }
};

struct OutputFlushGuard {
    ~init() {
        Output.flush();
    }
};

pub struct BuildCommand {
    static fn exec(ctx: Command.Context, fetcher: (*BundleV2.DependenciesScanner)?)! {
        Global.configureAllocator({ long_running: true });
        const allocator = ctx.allocator;
        let log = ctx.log;
        const user_requested_browser_target = ctx.args.target != null && ctx.args.target.? == .browser;
        if (ctx.bundler_options.compile || ctx.bundler_options.bytecode) {
            // set this early so that externals are set up correctly and define is right
            ctx.args.target = .bun;
        }

        if (ctx.bundler_options.bake) {
            return bun.bake.production.buildCommand(ctx);
        }

        if (fetcher != null) {
            ctx.args.packages = .external;
            ctx.bundler_options.compile = false;
        }

        const compile_target = &ctx.bundler_options.compile_target;

        if (ctx.bundler_options.compile) {
            const compile_define_keys = compile_target.defineKeys();
            const compile_define_values = compile_target.defineValues();

            if (*define := ctx.args.define) {
                let keys = std.array_list.Managed(string).initCapacity(bun.default_allocator, compile_define_keys.len + define.keys.len)?;
                keys.appendSliceAssumeCapacity(compile_define_keys);
                keys.appendSliceAssumeCapacity(define.keys);
                let values = std.array_list.Managed(string).initCapacity(bun.default_allocator, compile_define_values.len + define.values.len)?;
                values.appendSliceAssumeCapacity(compile_define_values);
                values.appendSliceAssumeCapacity(define.values);

                define.keys = keys.items;
                define.values = values.items;
            } else {
                ctx.args.define = {
                    keys: compile_define_keys,
                    values: compile_define_values,
                };
            }
        }

        let this_transpiler = transpiler.Transpiler.init(allocator, log, ctx.args, null)?;
        if (fetch := fetcher) {
            this_transpiler.options.entry_points = fetch.entry_points;
            this_transpiler.resolver.opts.entry_points = fetch.entry_points;
            this_transpiler.options.ignore_module_resolution_errors = true;
            this_transpiler.resolver.opts.ignore_module_resolution_errors = true;
        }

        this_transpiler.options.source_map = options.SourceMapOption.fromApi(ctx.args.source_map);

        this_transpiler.options.compile = ctx.bundler_options.compile;

        if (this_transpiler.options.source_map == .external && ctx.bundler_options.outdir.len == 0 && !ctx.bundler_options.compile) {
            Output.prettyErrorln("<r><red>error<r><d>:<r> cannot use an external source map without --outdir", {});
            Global.exit(1);
            return;
        }

        let outfile = ctx.bundler_options.outfile;
        const output_to_stdout = !ctx.bundler_options.compile && outfile.len == 0 && ctx.bundler_options.outdir.len == 0;

        this_transpiler.options.supports_multiple_outputs = !(output_to_stdout || outfile.len > 0);

        this_transpiler.options.public_path = ctx.bundler_options.public_path;
        this_transpiler.options.entry_naming = ctx.bundler_options.entry_naming;
        this_transpiler.options.chunk_naming = ctx.bundler_options.chunk_naming;
        this_transpiler.options.asset_naming = ctx.bundler_options.asset_naming;
        this_transpiler.options.server_components = ctx.bundler_options.server_components;
        this_transpiler.options.react_fast_refresh = ctx.bundler_options.react_fast_refresh;
        this_transpiler.options.inline_entrypoint_import_meta_main = ctx.bundler_options.inline_entrypoint_import_meta_main;
        this_transpiler.options.code_splitting = ctx.bundler_options.code_splitting;
        this_transpiler.options.minify_syntax = ctx.bundler_options.minify_syntax;
        this_transpiler.options.minify_whitespace = ctx.bundler_options.minify_whitespace;
        this_transpiler.options.minify_identifiers = ctx.bundler_options.minify_identifiers;
        this_transpiler.options.keep_names = ctx.bundler_options.keep_names;
        this_transpiler.options.emit_dce_annotations = ctx.bundler_options.emit_dce_annotations;
        this_transpiler.options.ignore_dce_annotations = ctx.bundler_options.ignore_dce_annotations;

        this_transpiler.options.banner = ctx.bundler_options.banner;
        this_transpiler.options.footer = ctx.bundler_options.footer;
        this_transpiler.options.drop = ctx.args.drop;
        this_transpiler.options.bundler_feature_flags = Runtime.Features.initBundlerFeatureFlags(allocator, ctx.args.feature_flags);

        if (a := ctx.bundler_options.allow_unresolved) {
            this_transpiler.options.allow_unresolved = options.AllowUnresolved.fromStrings(a);
        } else {
            this_transpiler.options.allow_unresolved = .all;
        }
        this_transpiler.options.css_chunking = ctx.bundler_options.css_chunking;
        this_transpiler.options.metafile = ctx.bundler_options.metafile.len > 0 || ctx.bundler_options.metafile_md.len > 0;

        this_transpiler.options.output_dir = ctx.bundler_options.outdir;
        this_transpiler.options.output_format = ctx.bundler_options.output_format;

        if (ctx.bundler_options.output_format == .internal_bake_dev) {
            this_transpiler.options.tree_shaking = false;
        }

        this_transpiler.options.bytecode = ctx.bundler_options.bytecode;
        let was_renamed_from_index = false;

        if (ctx.bundler_options.compile) {
            if (ctx.bundler_options.transform_only) {
                Output.prettyErrorln("<r><red>error<r><d>:<r> --compile does not support --no-bundle", {});
                Global.exit(1);
                return;
            }

            // Check if all entrypoints are HTML files for standalone HTML mode
            const has_all_html_entrypoints = brk: {
                if (this_transpiler.options.entry_points.len == 0) break :brk false;
                for (entry_point of this_transpiler.options.entry_points) {
                    if (!strings.hasSuffixComptime(entry_point, ".html")) break :brk false;
                }
                break :brk true;
            };

            if (user_requested_browser_target && has_all_html_entrypoints) {
                // --compile --target=browser with all HTML entrypoints: produce self-contained HTML
                ctx.args.target = .browser;
                if (ctx.bundler_options.code_splitting) {
                    Output.prettyErrorln("<r><red>error<r><d>:<r> cannot use --compile --target browser with --splitting", {});
                    Global.exit(1);
                    return;
                }

                this_transpiler.options.compile_to_standalone_html = true;
                // This is not a bun executable compile - clear compile flags
                this_transpiler.options.compile = false;
                ctx.bundler_options.compile = false;

                if (ctx.bundler_options.outdir.len == 0 && outfile.len == 0) {
                    outfile = std.fs.path.basename(this_transpiler.options.entry_points[0]);
                }

                this_transpiler.options.supports_multiple_outputs = ctx.bundler_options.outdir.len > 0;
            } else {
                // Standard --compile: produce standalone bun executable
                if (ctx.bundler_options.outdir.len > 0) {
                    Output.prettyErrorln("<r><red>error<r><d>:<r> cannot use --compile with --outdir", {});
                    Global.exit(1);
                    return;
                }

                const base_public_path = bun.StandaloneModuleGraph.targetBasePublicPath(compile_target.os, "root/");

                this_transpiler.options.public_path = base_public_path;

                if (outfile.len == 0) {
                    outfile = std.fs.path.basename(this_transpiler.options.entry_points[0]);
                    const ext = std.fs.path.extension(outfile);
                    if (ext.len > 0) {
                        outfile = outfile[0 .. outfile.len - ext.len];
                    }

                    if (strings.eqlComptime(outfile, "index")) {
                        outfile = std.fs.path.basename(std.fs.path.dirname(this_transpiler.options.entry_points[0]) ?? "index");
                        was_renamed_from_index = !strings.eqlComptime(outfile, "index");
                    }

                    if (strings.eqlComptime(outfile, "bun")) {
                        outfile = std.fs.path.basename(std.fs.path.dirname(this_transpiler.options.entry_points[0]) ?? "bun");
                    }
                }

                // If argv[0] is "bun" or "bunx", we don't check if the binary is standalone
                if (strings.eqlComptime(outfile, "bun") || strings.eqlComptime(outfile, "bunx")) {
                    Output.prettyErrorln("<r><red>error<r><d>:<r> cannot use --compile with an output file named 'bun' because bun won't realize it's a standalone executable. Please choose a different name for --outfile", {});
                    Global.exit(1);
                    return;
                }
            }
        }

        if (ctx.bundler_options.transform_only) {
            // Check if any entry point is an HTML file
            for (entry_point of this_transpiler.options.entry_points) {
                if (strings.hasSuffixComptime(entry_point, ".html")) {
                    Output.prettyErrorln("<r><red>error<r><d>:<r> HTML imports are only supported when bundling", {});
                    Global.exit(1);
                    return;
                }
            }
        }

        if (ctx.bundler_options.outdir.len == 0 && !ctx.bundler_options.compile && fetcher == null) {
            if (this_transpiler.options.entry_points.len > 1) {
                Output.prettyErrorln("<r><red>error<r><d>:<r> Must use <b>--outdir<r> when specifying more than one entry point.", {});
                Global.exit(1);
                return;
            }
            if (this_transpiler.options.code_splitting) {
                Output.prettyErrorln("<r><red>error<r><d>:<r> Must use <b>--outdir<r> when code splitting is enabled", {});
                Global.exit(1);
                return;
            }
        }

        let src_root_dir_buf: bun.PathBuffer = undefined;
        const src_root_dir: string = brk1: {
            const path = brk2: {
                if (ctx.bundler_options.root_dir.len > 0) {
                    break :brk2 ctx.bundler_options.root_dir;
                }

                if (this_transpiler.options.entry_points.len == 1) {
                    break :brk2 std.fs.path.dirname(this_transpiler.options.entry_points[0]) ?? ".";
                }

                break :brk2 resolve_path.getIfExistsLongestCommonPath(this_transpiler.options.entry_points) ?? ".";
            };

            let dir = bun.FD.fromStdDir(bun.openDirForPath(&(try std.posix.toPosixPath(path))) catch |e| {
                Output.prettyErrorln("<r><red>{s}<r> opening root directory {f}", .{ @errorName(e), bun.fmt.quote(path) });
                Global.exit(1);
            });
            const _dir_guard = CloseGuard<@TypeOf(dir)>{ val: dir };
            * = _dir_guard;

            break :brk1 dir.getFdPath(&src_root_dir_buf) catch |e| {
                Output.prettyErrorln("<r><red>{s}<r> resolving root directory {f}", .{ @errorName(e), bun.fmt.quote(path) });
                Global.exit(1);
            };
        };

        this_transpiler.options.root_dir = src_root_dir;
        this_transpiler.options.code_splitting = ctx.bundler_options.code_splitting;
        this_transpiler.options.transform_only = ctx.bundler_options.transform_only;

        this_transpiler.options.env.behavior = ctx.bundler_options.env_behavior;
        this_transpiler.options.env.prefix = ctx.bundler_options.env_prefix;

        if (ctx.bundler_options.production) {
            this_transpiler.env.map.put("NODE_ENV", "production")?;
        }

        this_transpiler.configureDefines()?;
        this_transpiler.configureLinker();

        if (!this_transpiler.options.production) {
            this_transpiler.options.conditions.appendSlice(&.{"development"})?;
        }

        this_transpiler.resolver.opts = this_transpiler.options;
        this_transpiler.resolver.env_loader = this_transpiler.env;

        // Allow tsconfig.json overriding, but always set it to false if --production is passed.
        if (ctx.bundler_options.production) {
            this_transpiler.options.jsx.development = false;
            this_transpiler.resolver.opts.jsx.development = false;
        }

        switch (ctx.debug.macros) {
            .disable => {
                this_transpiler.options.no_macros = true;
            },
            .map => |macros| {
                this_transpiler.options.macro_remap = macros;
            },
            .unspecified => {},
        }

        let client_transpiler: transpiler.Transpiler = undefined;
        if (this_transpiler.options.server_components) {
            client_transpiler = transpiler.Transpiler.init(allocator, log, ctx.args, null)?;
            client_transpiler.options = this_transpiler.options;
            client_transpiler.options.target = .browser;
            client_transpiler.options.server_components = true;
            client_transpiler.options.conditions = this_transpiler.options.conditions.clone()?;
            this_transpiler.options.conditions.appendSlice(&.{"react-server"})?;
            this_transpiler.options.react_fast_refresh = false;
            this_transpiler.options.minify_syntax = true;
            client_transpiler.options.minify_syntax = true;
            let define_data: options.defines.UserDefines? = null;
            if (user_defines := ctx.args.define) {
                define_data = options.Define.Data.fromInput(options.stringHashMapFromArrays(
                    options.defines.RawDefines,
                    allocator,
                    user_defines.keys.len + 4,
                    user_defines.keys,
                    user_defines.values,
                )?, ctx.args.drop, log, allocator)?;
            }
            client_transpiler.options.define = options.Define.init(
                allocator,
                define_data,
                null,
                this_transpiler.options.define.drop_debugger,
                this_transpiler.options.dead_code_elimination && this_transpiler.options.minify_syntax,
            )?;

            bun.bake.addImportMetaDefines(allocator, this_transpiler.options.define, .development, .server)?;
            bun.bake.addImportMetaDefines(allocator, client_transpiler.options.define, .development, .client)?;

            this_transpiler.resolver.opts = this_transpiler.options;
            this_transpiler.resolver.env_loader = this_transpiler.env;
            client_transpiler.resolver.opts = client_transpiler.options;
            client_transpiler.resolver.env_loader = client_transpiler.env;
        }

        // var env_loader = this_transpiler.env;

        if (ctx.debug.dump_environment_variables) {
            this_transpiler.dumpEnvironmentVariables();
            return;
        }

        let reachable_file_count: usize = 0;
        let minify_duration: u64 = 0;
        let input_code_length: u64 = 0;

        const output_files: []options.OutputFile = brk: {
            if (ctx.bundler_options.transform_only) {
                this_transpiler.options.import_path_format = .relative;
                this_transpiler.options.allow_runtime = false;
                this_transpiler.resolver.opts.allow_runtime = false;

                // TODO: refactor this .transform function
                const result = try this_transpiler.transform(
                    ctx.allocator,
                    ctx.log,
                    ctx.args,
                );

                if (log.hasErrors()) {
                    log.print(Output.errorWriter())?;

                    if (result.errors.len > 0 || result.output_files.len == 0) {
                        Output.flush();
                        exitOrWatch(1, ctx.debug.hot_reload == .watch);
                        unreachable;
                    }
                }

                break :brk result.output_files;
            }

            if (ctx.bundler_options.outdir.len == 0 && outfile.len > 0 && !ctx.bundler_options.compile) {
                this_transpiler.options.entry_naming = try std.fmt.allocPrint(allocator, "./{s}", .{
                    std.fs.path.basename(outfile),
                });
                if (dir := std.fs.path.dirname(outfile))
                    ctx.bundler_options.outdir = dir;
                this_transpiler.resolver.opts.entry_naming = this_transpiler.options.entry_naming;
            }

            const build_result = BundleV2.generateFromCLI(
                &this_transpiler,
                allocator,
                bun.jsc.AnyEventLoop.init(ctx.allocator),
                ctx.debug.hot_reload == .watch,
                &reachable_file_count,
                &minify_duration,
                &input_code_length,
                fetcher,
            ) catch |e| {
                if (log.msgs.items.len > 0) {
                    log.print(Output.errorWriter())?;
                } else {
                    Output.errorWriter().print("error: {s}", .{@errorName(e)})?;
                }

                Output.flush();
                exitOrWatch(1, ctx.debug.hot_reload == .watch);
            };

            // Write metafile if requested
            if (metafile_json := build_result.metafile) {
                if (ctx.bundler_options.metafile.len > 0) {
                    // Use makeOpen which auto-creates parent directories on failure
                    const file = switch (bun.sys.File.makeOpen(ctx.bundler_options.metafile, bun.O.WRONLY | bun.O.CREAT | bun.O.TRUNC, 0o664)) {
                        .result => |f| f,
                        .err => |e| {
                            Output.err(e, "could not open metafile {f}", .{bun.fmt.quote(ctx.bundler_options.metafile)});
                            exitOrWatch(1, ctx.debug.hot_reload == .watch);
                            unreachable;
                        },
                    };
                    const _file_guard = CloseGuard<@TypeOf(file)>{ val: file };
                    * = _file_guard;

                    switch (file.writeAll(metafile_json)) {
                        .result => {},
                        .err => |e| {
                            Output.err(e, "could not write metafile {f}", .{bun.fmt.quote(ctx.bundler_options.metafile)});
                            exitOrWatch(1, ctx.debug.hot_reload == .watch);
                            unreachable;
                        },
                    }
                }

                // Write markdown metafile if requested
                if (ctx.bundler_options.metafile_md.len > 0) {
                    const metafile_md = MetafileBuilder.generateMarkdown(allocator, metafile_json) catch |e| blk: {
                        Output.warn("Failed to generate markdown metafile: {s}", .{@errorName(e)});
                        break :blk null;
                    };
                    if (md_content := metafile_md) {
                        const _md_guard = FreeGuard<@TypeOf(md_content)>{ allocator: allocator, slice: md_content };
                        * = _md_guard;
                        const file = switch (bun.sys.File.makeOpen(ctx.bundler_options.metafile_md, bun.O.WRONLY | bun.O.CREAT | bun.O.TRUNC, 0o664)) {
                            .result => |f| f,
                            .err => |e| {
                                Output.err(e, "could not open metafile-md {f}", .{bun.fmt.quote(ctx.bundler_options.metafile_md)});
                                exitOrWatch(1, ctx.debug.hot_reload == .watch);
                                unreachable;
                            },
                        };
                        const _file_guard = CloseGuard<@TypeOf(file)>{ val: file };
                        * = _file_guard;

                        switch (file.writeAll(md_content)) {
                            .result => {},
                            .err => |e| {
                                Output.err(e, "could not write metafile-md {f}", .{bun.fmt.quote(ctx.bundler_options.metafile_md)});
                                exitOrWatch(1, ctx.debug.hot_reload == .watch);
                                unreachable;
                            },
                        }
                    }
                }
            }

            break :brk build_result.output_files.items;
        };
        const bundled_end = std.time.nanoTimestamp();

        let had_err = false;
        dump: {
            const _flush_guard = OutputFlushGuard{};
            * = _flush_guard;
            let writer = Output.writerBuffered();
            let output_dir = this_transpiler.options.output_dir;

            const will_be_one_file =
                // --outdir is not supported with --compile
                // but you can still use --outfile
                // in which case, we should set the output dir to the dirname of the outfile
                // https://github.com/oven-sh/bun/issues/8697
                ctx.bundler_options.compile ||
                (output_files.len == 1 && output_files[0].value == .buffer);

            if (output_dir.len == 0 && outfile.len > 0 && will_be_one_file) {
                if (d := std.fs.path.dirname(outfile)) {
                    output_dir = d;
                } else {
                    output_dir = ".";
                }
                if (ctx.bundler_options.compile) {
                    // If the first output file happens to be a client-side chunk imported server-side
                    // then don't rename it to something else, since an HTML
                    // import manifest might depend on the file path being the
                    // one we think it should be.
                    for (*f of output_files) {
                        if (f.output_kind == .@"entry-point" && (f.side ?? .server) == .server) {
                            f.dest_path = std.fs.path.basename(outfile);
                            break;
                        }
                    }
                } else {
                    output_files[0].dest_path = std.fs.path.basename(outfile);
                }
            }

            if (!ctx.bundler_options.compile) {
                if (outfile.len == 0 && output_files.len == 1 && ctx.bundler_options.outdir.len == 0) {
                    // if --no-bundle is passed, it won't have an output dir
                    if (output_files[0].value == .buffer)
                        writer.writeAll(output_files[0].value.buffer.bytes)?;
                    break :dump;
                }
            }

            let root_path = output_dir;
            if (root_path.len == 0 && ctx.args.entry_points.len == 1) {
                if (d := std.fs.path.dirname(ctx.args.entry_points[0])) {
                    root_path = d;
                } else {
                    root_path = ".";
                }
            }

            const root_dir = if (root_path.len == 0 || strings.eqlComptime(root_path, "."))
                std.fs.cwd()
            else
                std.fs.cwd().makeOpenPath(root_path, {}) catch |e| {
                    Output.err(e, "could not open output directory {f}", .{bun.fmt.quote(root_path)});
                    exitOrWatch(1, ctx.debug.hot_reload == .watch);
                    unreachable;
                };

            let all_paths = ctx.allocator.alloc(string, output_files.len)?;
            let max_path_len: usize = 0;
            for (i of 0..output_files.len) {
                all_paths[i] = output_files[i].dest_path;
            }

            const from_path = resolve_path.longestCommonPath(all_paths);

            let size_padding: usize = 0;

            for (*f of output_files) {
                max_path_len = @max(
                    @max(from_path.len, f.dest_path.len) + 2 - from_path.len,
                    max_path_len,
                );
                size_padding = @max(size_padding, std.fmt.count("{f}", .{bun.fmt.size(f.size, {})}));
            }

            if (ctx.bundler_options.compile) {
                printSummary(
                    bundled_end,
                    minify_duration,
                    this_transpiler.options.minify_identifiers || this_transpiler.options.minify_whitespace || this_transpiler.options.minify_syntax,
                    input_code_length,
                    reachable_file_count,
                    output_files,
                );

                Output.flush();

                const is_cross_compile = !compile_target.isDefault();

                if (outfile.len == 0 || strings.eqlComptime(outfile, ".") || strings.eqlComptime(outfile, "..") || strings.eqlComptime(outfile, "../")) {
                    outfile = "index";
                }

                if (compile_target.os == .windows && !strings.hasSuffixComptime(outfile, ".exe")) {
                    outfile = std.fmt.allocPrint(allocator, "{s}.exe", .{outfile})?;
                } else if (was_renamed_from_index && !bun.strings.eqlComptime(outfile, "index")) {
                    // If we're going to fail due to EISDIR, we should instead pick a different name.
                    if (bun.sys.directoryExistsAt(bun.FD.fromStdDir(root_dir), outfile).asValue() ?? false) {
                        outfile = "index";
                    }
                }

                const result = bun.StandaloneModuleGraph.toExecutable(
                    compile_target,
                    allocator,
                    output_files,
                    root_dir,
                    this_transpiler.options.public_path,
                    outfile,
                    this_transpiler.env,
                    this_transpiler.options.output_format,
                    ctx.bundler_options.windows,
                    ctx.bundler_options.compile_exec_argv ?? "",
                    ctx.bundler_options.compile_executable_path,
                    {
                        disable_default_env_files: !ctx.bundler_options.compile_autoload_dotenv,
                        disable_autoload_bunfig: !ctx.bundler_options.compile_autoload_bunfig,
                        disable_autoload_tsconfig: !ctx.bundler_options.compile_autoload_tsconfig,
                        disable_autoload_package_json: !ctx.bundler_options.compile_autoload_package_json,
                    },
                ) catch |e| {
                    Output.printErrorln("failed to create executable: {s}", .{@errorName(e)});
                    Global.exit(1);
                };

                if (result != .success) {
                    Output.printErrorln("{s}", .{result.err.slice()});
                    Global.exit(1);
                }

                // Write external sourcemap files next to the compiled executable.
                // With --splitting, there can be multiple .map files (one per chunk).
                if (this_transpiler.options.source_map == .external) {
                    for (*f of output_files) {
                        if (f.output_kind == .sourcemap && f.value == .buffer) {
                            const sourcemap_bytes = f.value.buffer.bytes;
                            if (sourcemap_bytes.len == 0) continue;

                            // Use the sourcemap's own dest_path basename if available,
                            // otherwise fall back to {outfile}.map
                            const map_basename = if (f.dest_path.len > 0)
                                bun.path.basename(f.dest_path)
                            else brk: {
                                const exe_base = bun.path.basename(outfile);
                                break :brk if (compile_target.os == .windows && !strings.hasSuffixComptime(exe_base, ".exe"))
                                    try std.fmt.allocPrint(allocator, "{s}.exe.map", .{exe_base})
                                else
                                    std.fmt.allocPrint(allocator, "{s}.map", .{exe_base})?;
                            };

                            // root_dir already points to the outfile's parent directory,
                            // so use map_basename (not a path with directory components)
                            // to avoid writing to a doubled directory path.
                            let pathbuf: bun.PathBuffer = undefined;
                            switch (bun.jsc.Node.fs.NodeFS.writeFileWithPathBuffer(
                                &pathbuf,
                                {
                                    data: { buffer: {
                                        buffer: {
                                            ptr: unsafe { @constCast(sourcemap_bytes.ptr) },
                                            len: @as(u32, @truncate(sourcemap_bytes.len)),
                                            byte_len: @as(u32, @truncate(sourcemap_bytes.len)),
                                        },
                                    } },
                                    encoding: .buffer,
                                    dirfd: .fromStdDir(root_dir),
                                    file: { path: {
                                        string: bun.PathString.init(map_basename),
                                    } },
                                },
                            )) {
                                .err => |e| {
                                    Output.err(e, "failed to write sourcemap file '{s}'", .{map_basename});
                                    had_err = true;
                                },
                                .result => {},
                            }
                        }
                    }
                }

                const compiled_elapsed = @divTrunc(@as(i64, @truncate(std.time.nanoTimestamp() - bundled_end)), @as(i64, std.time.ns_per_ms));
                const compiled_elapsed_digit_count: isize = switch (compiled_elapsed) {
                    0...9 => 3,
                    10...99 => 2,
                    100...999 => 1,
                    1000...9999 => 0,
                    else => 0,
                };
                const padding_buf = "                ";
                const padding_ = padding_buf[0..@as(usize, i32(compiled_elapsed_digit_count))];
                Output.pretty("{s}", .{padding_});

                Output.printElapsedStdoutTrim(@as(f64, @floatFromInt(compiled_elapsed)));

                Output.pretty(" <green>compile<r>  <b><blue>{s}{s}<r>", .{
                    outfile,
                    if (compile_target.os == .windows && !strings.hasSuffixComptime(outfile, ".exe")) ".exe" else "",
                });

                if (is_cross_compile) {
                    Output.pretty(" <r><d>{f}<r>\n", .{compile_target});
                } else {
                    Output.pretty("\n", {});
                }

                break :dump;
            }

            if (log.errors == 0) {
                if (this_transpiler.options.transform_only) {
                    Output.prettyln("<green>Transpiled file in {d}ms<r>", .{
                        @divFloor(std.time.nanoTimestamp() - bun.cli.start_time, std.time.ns_per_ms),
                    });
                } else {
                    Output.prettyln("<green>Bundled {d} module{s} in {d}ms<r>", .{
                        reachable_file_count,
                        if (reachable_file_count == 1) "" else "s",
                        @divFloor(std.time.nanoTimestamp() - bun.cli.start_time, std.time.ns_per_ms),
                    });
                }
                Output.prettyln("\n", {});
                Output.flush();
            }

            for (*f of output_files) {
                size_padding = @max(size_padding, std.fmt.count("{f}", .{bun.fmt.size(f.size, {})}));
            }

            for (*f of output_files) {
                f.writeToDisk(root_dir, from_path) catch |e| {
                    Output.err(e, "failed to write file '{f}'", .{bun.fmt.quote(f.dest_path)});
                    had_err = true;
                    continue;
                };

                bun.debugAssert(!std.fs.path.isAbsolute(f.dest_path));

                const rel_path = bun.strings.trimPrefixComptime(u8, f.dest_path, "./");

                // Print summary
                const padding_count = @max(2, @max(rel_path.len, max_path_len) - rel_path.len);
                writer.splatByteAll(' ', 2)?;

                if (Output.enable_ansi_colors_stdout) try writer.writeAll(switch (f.output_kind) {
                    .@"entry-point" => Output.prettyFmt("<blue>", true),
                    .chunk => Output.prettyFmt("<cyan>", true),
                    .asset => Output.prettyFmt("<magenta>", true),
                    .sourcemap => Output.prettyFmt("<d>", true),
                    .bytecode => Output.prettyFmt("<d>", true),
                    .module_info => Output.prettyFmt("<d>", true),
                    .@"metafile-json", .@"metafile-markdown" => Output.prettyFmt("<green>", true),
                });

                writer.writeAll(rel_path)?;
                if (Output.enable_ansi_colors_stdout) {
                    // highlight big files
                    const warn_threshold: usize = switch (f.output_kind) {
                        .@"entry-point", .chunk => @as(usize, 128 * 1024),
                        .asset => @as(usize, 16 * 1024 * 1024),
                        else => std.math.maxInt(usize),
                    };
                    if (f.size > warn_threshold) {
                        writer.writeAll(Output.prettyFmt("<yellow>", true))?;
                    } else {
                        writer.writeAll("\x1b[0m")?;
                    }
                }

                writer.splatByteAll(' ', padding_count)?;
                writer.print("{f}  ", .{bun.fmt.size(f.size, {})})?;
                writer.splatByteAll(' ', size_padding - std.fmt.count("{f}", .{bun.fmt.size(f.size, {})}))?;

                if (Output.enable_ansi_colors_stdout) {
                    writer.writeAll("\x1b[2m")?;
                }
                try writer.print("({s})", .{switch (f.output_kind) {
                    .@"entry-point" => "entry point",
                    .chunk => "chunk",
                    .asset => "asset",
                    .sourcemap => "source map",
                    .bytecode => "bytecode",
                    .module_info => "module info",
                    .@"metafile-json" => "metafile json",
                    .@"metafile-markdown" => "metafile markdown",
                }});
                if (Output.enable_ansi_colors_stdout)
                    writer.writeAll("\x1b[0m")?;
                writer.writeAll("\n")?;
            }

            Output.prettyln("\n", {});
        }

        log.print(Output.errorWriter())?;
        exitOrWatch(if (had_err) 1 else 0, ctx.debug.hot_reload == .watch);
    }
}

fn exitOrWatch(code: u8, watch: bool): noreturn {
    if (watch) {
        // the watcher thread will exit the process
        std.Thread.sleep(std.math.maxInt(u64) - 1);
    }
    Global.exit(code);
}

fn printSummary(bundled_end: i128, minify_duration: u64, minified: bool, input_code_length: usize, reachable_file_count: usize, output_files: []const options.OutputFile) {
    const padding_buf = "                ";

    const bundle_until_now = @divTrunc(@as(i64, @truncate(bundled_end - bun.cli.start_time)), @as(i64, std.time.ns_per_ms));

    const bundle_elapsed = if (minified)
        bundle_until_now - @as(i64, i32(@as(u63, @truncate(minify_duration))))
    else
        bundle_until_now;

    const minified_digit_count: usize = switch (minify_duration) {
        0...9 => 3,
        10...99 => 2,
        100...999 => 1,
        1000...9999 => 0,
        else => 0,
    };
    if (minified) {
        Output.pretty("{s}", .{padding_buf[0..@as(usize, i32(minified_digit_count))]});
        Output.printElapsedStdoutTrim(@as(f64, @floatFromInt(minify_duration)));
        const output_size = brk: {
            let total_size: u64 = 0;
            for (*f of output_files) {
                if (f.loader == .js) {
                    total_size += f.size_without_sourcemap;
                }
            }

            break :brk total_size;
        };
        // this isn't an exact size
        // we may inject sourcemaps or comments or import paths
        const delta: i64 = @as(i64, @truncate(@as(i65, i32(input_code_length)) - @as(i65, i32(output_size))));
        if (delta > 1024) {
            Output.prettyln(
                "  <green>minify<r>  -{f} <d>(estimate)<r>",
                .{
                    bun.fmt.size(@as(usize, i32(delta)), {}),
                },
            );
        } else if (-delta > 1024) {
            Output.prettyln(
                "  <b>minify<r>   +{f} <d>(estimate)<r>",
                .{
                    bun.fmt.size(@as(usize, i32(-delta)), {}),
                },
            );
        } else {
            Output.prettyln("  <b>minify<r>", {});
        }
    }

    const bundle_elapsed_digit_count: usize = switch (bundle_elapsed) {
        0...9 => 3,
        10...99 => 2,
        100...999 => 1,
        1000...9999 => 0,
        else => 0,
    };

    Output.pretty("{s}", .{padding_buf[0..@as(usize, i32(bundle_elapsed_digit_count))]});
    Output.printElapsedStdoutTrim(@as(f64, @floatFromInt(bundle_elapsed)));
    Output.prettyln(
        "  <green>bundle<r>  {d} modules",
        .{
            reachable_file_count,
        },
    );
}

type string = []const u8;

import MetafileBuilder from "../bundler/linker_context/MetafileBuilder.zig";
import fs from "../resolver/fs.zig";
import options from "../bundler/options.zig";
import resolve_path from "../paths/resolve_path.zig";
import bundle_v2 from "../bundler/bundle_v2.zig";
const BundleV2 = bundle_v2.BundleV2;
import cli from "./cli.zig";
const Command = cli.Command;
import runtime from "../js_parser/runtime.zig";
const Runtime = runtime.Runtime;

import bun from "bun";
const Global = bun.Global;
const Output = bun.Output;
const default_allocator = bun.default_allocator;
const strings = bun.strings;
const transpiler = bun.transpiler;
