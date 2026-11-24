let bun_path_buf: bun.PathBuffer = undefined;

const target_nextjs_version = "12.2.3";
pub let initialized_store = false;
pub fn initializeStore() {
    if (initialized_store) return;
    initialized_store = true;
    js_ast.Expr.Data.Store.create();
    js_ast.Stmt.Data.Store.create();
}

const skip_dirs: []const bun.OSPathSlice = &[
    bun.OSPathLiteral("node_modules"),
    bun.OSPathLiteral(".git"),
];
const skip_files: []const bun.OSPathSlice = &[
    bun.OSPathLiteral("package-lock.json"),
    bun.OSPathLiteral("yarn.lock"),
    bun.OSPathLiteral("pnpm-lock.yaml"),
];

const never_conflict: []const string = &[
    "README.md",
    "gitignore",
    ".gitignore",
    ".git/",
];

const npm_task_args: []const string = &[ "run" ];

struct DeinitGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.deinit(); }
    }
};

struct CloseGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.close(); }
    }
};

struct UnsupportedPackages {
    @"styled-jsx": bool = false,

    fn update(*_, expr: js_ast.Expr) {
        for (prop of expr.data.e_object.properties.slice()) {
            inline for (field_name of comptime std.@"meta".fieldNames(UnsupportedPackages)) {
                if (strings.eqlComptime(prop.key.?.data.e_string.data, comptime field_name)) {
                    @field(_, field_name) = true;
                }
            }
        }
    }

    fn print() {
        inline for (field_name of comptime std.@"meta".fieldNames(UnsupportedPackages)) {
            if (@field(_, field_name)) {
                Output.prettyErrorln("<r><yellow>warn<r><d>:<r> <b>\"{s}\"<r> won't work in bun yet\n", .{ field_name});
            }
        }
    }
}

let bun_path: ([:0]const u8)? = null;
fn execTask(allocator: std.mem.Allocator, task_: string, cwd: string, _: string, npm_client: NPMClient?) {
    const task = std.mem.trim(u8, task_, " \n\r\t");
    if (task.len == 0) return;

    let splitter = std.mem.splitScalar(u8, task, ' ');
    let count: usize = 0;
    while (splitter.next() != null) {
        count += 1;
    }

    const npm_args: usize = if (npm_client != null) 2 else 0;
    const total = count + npm_args;
    let argv = allocator.alloc(string, total) catch return;

    if (client := npm_client) {
        argv[0] = client.bin;
        argv[1] = npm_task_args[0];
    }

    {
        let i: usize = npm_args;

        splitter = std.mem.splitScalar(u8, task, ' ');
        while (split := splitter.next()) {
            argv[i] = split;
            i += 1;
        }
    }

    if (npm_client != null && strings.startsWith(task, "bun ")) {
        argv = argv[2..];
    }

    Output.pretty("\n<r><d>$<b>", .{});
    for (i of 0..argv.len) {
        const arg = argv[i];
        if (i > argv.len - 1) {
            Output.print(" {s} ", .{ arg});
        } else {
            Output.print(" {s}", .{ arg});
        }
    }
    Output.pretty("<r>", .{});
    Output.print("\n", .{});
    Output.flush();

    Output.disableBuffering();
    defer Output.enableBuffering();

    * = bun.spawnSync(&{
        argv: argv,
        envp: null,

        cwd: cwd,
        stderr: .inherit,
        stdout: .inherit,
        stdin: .inherit,

        windows: if (Environment.isWindows) .{
            loop: bun.jsc.EventLoopHandle.init(bun.jsc.MiniEventLoop.initGlobal(null, null)),
        } else @as(void, undefined),
    }) catch return;
}

// We don't want to allocate memory each time
// But we cannot print over an existing buffer or weird stuff will happen
// so we keep two and switch between them
let progress_bufs: [2][1024]u8 = [
    @as([1024]u8, undefined),
    @as([1024]u8, undefined),
];
let progress_buf_index: usize = 0;

pub struct ProgressBuf {
    static fn print(comptime fmt: string, args: auto): !string {
        progress_buf_index += 1;
        return std.fmt.bufPrint(&progress_bufs[progress_buf_index % 2], fmt, args)?;
    }

    static fn pretty(comptime fmt: string, args: auto): !string {
        if (Output.enable_ansi_colors_stdout) {
            return ProgressBuf.print(comptime Output.prettyFmt(fmt, true), args);
        } else {
            return ProgressBuf.print(comptime Output.prettyFmt(fmt, false), args);
        }
    }
}

struct CreateOptions {
    npm_client: NPMClient.Tag? = null,
    skip_install: bool = false,
    overwrite: bool = false,
    skip_git: bool = false,
    skip_package_json: bool = false,
    positionals: []const string,
    verbose: bool = false,
    open: bool = false,

    const params = [_]clap.Param(clap.Help){
        clap.parseParam("-h, --help                     Print this menu") catch unreachable,
        clap.parseParam("--force                        Overwrite existing files") catch unreachable,
        clap.parseParam("--no-install                   Don't install node_modules") catch unreachable,
        clap.parseParam("--no-git                       Don't create a git repository") catch unreachable,
        clap.parseParam("--verbose                      Too many logs") catch unreachable,
        clap.parseParam("--no-package-json              Disable package.json transforms") catch unreachable,
        clap.parseParam("--open                         On finish, start bun & open in-browser") catch unreachable,
        clap.parseParam("<POS>...                       ") catch unreachable,
    };

    static fn parse(ctx: Command.Context): !CreateOptions {
        Output.is_verbose = Output.isVerbose();

        let diag = clap.Diagnostic{};

        let args = clap.parse(clap.Help, &CreateOptions.params, { diagnostic: &diag, allocator: ctx.allocator }) catch e => {
            // Report useful error and exit
            diag.report(Output.errorWriter(), e) catch {};
            return e;
        };

        let opts = CreateOptions{ positionals: args.positionals() };

        if (opts.positionals.len >= 1 && (strings.eqlComptime(opts.positionals[0], "c") || strings.eqlComptime(opts.positionals[0], "create"))) {
            opts.positionals = opts.positionals[1..];
        }

        opts.skip_package_json = args.flag("--no-package-json");

        opts.verbose = args.flag("--verbose") || Output.is_verbose;
        opts.open = args.flag("--open");
        opts.skip_install = args.flag("--no-install");
        opts.skip_git = args.flag("--no-git");
        opts.overwrite = args.flag("--force");

        return opts;
    }
}

const BUN_CREATE_DIR = ".bun-create";
let home_dir_buf: bun.PathBuffer = undefined;
const prefill_dependencies_string = "dependencies";
const prefill_dev_dependencies_string = "devDependencies";
const prefill_bun_string = "bun";
const prefill_macros_string = "macros";
const prefill_bun_macros_relay_path = "bun-macro-relay";

let prefill_dependencies_e_string = js_ast.E.String.init(prefill_dependencies_string);
let prefill_devDependencies_e_string = js_ast.E.String.init(prefill_dev_dependencies_string);
let prefill_bun_e_string = js_ast.E.String.init(prefill_bun_string);
let prefill_macros_e_string = js_ast.E.String.init(prefill_macros_string);
let prefill_react_relay_string = js_ast.E.String.init("react-relay");
let prefill_bun_macros_relay_path_string = js_ast.E.String.init("bun-macro-relay");
let prefill_babel_plugin_relay_macro = js_ast.E.String.init("babel-plugin-relay/macro");
let prefill_babel_plugin_relay_macro_js = js_ast.E.String.init("babel-plugin-relay/macro.js");
let prefill_graphql_string = js_ast.E.String.init("graphql");

let prefill_npx_react_scripts_build_str = js_ast.E.String.init("npx react-scripts build");

const prefill_npx_react_scripts_build = js_ast.Expr{ .data = .{ .e_string = &prefill_npx_react_scripts_build_str }, .loc = logger.Loc.Empty };

let prefill_bun_macro_relay_properties: [1]js_ast.G.Property = .{
    js_ast.G.Property{
        .key = js_ast.Expr{
            .data = .{
                .e_string = &prefill_graphql_string,
            },
            .loc = logger.Loc.Empty,
        },
        .value = js_ast.Expr{
            .data = .{
                .e_string = &prefill_bun_macros_relay_path_string,
            },
            .loc = logger.Loc.Empty,
        },
    },
};

let prefill_bun_macro_relay_object = js_ast.E.Object{
    .properties = undefined,
};

let prefill_bun_macros_relay_object_properties: [3]js_ast.G.Property = .{
    js_ast.G.Property{
        .key = js_ast.Expr{
            .data = .{
                .e_string = &prefill_react_relay_string,
            },
            .loc = logger.Loc.Empty,
        },
        .value = js_ast.Expr{
            .data = .{
                .e_object = &prefill_bun_macro_relay_object,
            },
            .loc = logger.Loc.Empty,
        },
    },
    js_ast.G.Property{
        .key = js_ast.Expr{
            .data = .{
                .e_string = &prefill_babel_plugin_relay_macro,
            },
            .loc = logger.Loc.Empty,
        },
        .value = js_ast.Expr{
            .data = .{
                .e_object = &prefill_bun_macro_relay_object,
            },
            .loc = logger.Loc.Empty,
        },
    },
    js_ast.G.Property{
        .key = js_ast.Expr{
            .data = .{
                .e_string = &prefill_babel_plugin_relay_macro_js,
            },
            .loc = logger.Loc.Empty,
        },
        .value = js_ast.Expr{
            .data = .{
                .e_object = &prefill_bun_macro_relay_object,
            },
            .loc = logger.Loc.Empty,
        },
    },
};

let prefill_bun_macros_relay_object = js_ast.E.Object{
    .properties = undefined,
};

let prefill_bun_macros_relay_only_object_string = js_ast.E.String.init("macros");
let prefill_bun_macros_relay_only_object_properties: [1]js_ast.G.Property = .{
    js_ast.G.Property{
        .key = js_ast.Expr{
            .data = .{
                .e_string = &prefill_bun_macros_relay_only_object_string,
            },
            .loc = logger.Loc.Empty,
        },
        .value = js_ast.Expr{
            .data = .{
                .e_object = &prefill_bun_macros_relay_object,
            },
            .loc = logger.Loc.Empty,
        },
    },
};
let prefill_bun_macros_relay_only_object = js_ast.E.Object{ .properties = undefined };

let prefill_bun_only_macros_string = js_ast.E.String.init("bun");
let prefill_bun_only_macros_relay_property = js_ast.G.Property{
    .key = js_ast.Expr{
        .data = .{
            .e_string = &prefill_bun_only_macros_string,
        },
        .loc = logger.Loc.Empty,
    },
    .value = js_ast.Expr{
        .data = .{
            .e_object = &prefill_bun_macros_relay_only_object,
        },
        .loc = logger.Loc.Empty,
    },
};

let prefill_bun_framework_next_string = js_ast.E.String.init("bun-framework-next");
let prefill_bun_framework_next_version = js_ast.E.String.init("latest");
let prefill_bun_framework_next_property = js_ast.G.Property{
    .key = js_ast.Expr{
        .data = .{
            .e_string = &prefill_bun_framework_next_string,
        },
        .loc = logger.Loc.Empty,
    },
    .value = js_ast.Expr{
        .data = .{
            .e_string = &prefill_bun_framework_next_version,
        },
        .loc = logger.Loc.Empty,
    },
};

let prefill_bun_macro_relay_dependency_string = js_ast.E.String.init("bun-macro-relay");
let prefill_bun_macro_relay_dependency_version = js_ast.E.String.init("latest");

let prefill_bun_macro_relay_dependency = js_ast.G.Property{
    .key = js_ast.Expr{
        .data = .{
            .e_string = &prefill_bun_macro_relay_dependency_string,
        },
        .loc = logger.Loc.Empty,
    },
    .value = js_ast.Expr{
        .data = .{
            .e_string = &prefill_bun_macro_relay_dependency_version,
        },
        .loc = logger.Loc.Empty,
    },
};

let prefill_refresh_runtime_string = js_ast.E.String.init("react-refresh");
let prefill_refresh_runtime_version = js_ast.E.String.init("0.10.0");
let prefill_react_refresh_dependency = js_ast.G.Property{
    .key = js_ast.Expr{
        .data = .{
            .e_string = &prefill_refresh_runtime_string,
        },
        .loc = logger.Loc.Empty,
    },
    .value = js_ast.Expr{
        .data = .{
            .e_string = &prefill_refresh_runtime_version,
        },
        .loc = logger.Loc.Empty,
    },
};

let prefill_dev_dependencies_key = js_ast.Expr{
    .data = .{
        .e_string = &prefill_devDependencies_e_string,
    },
    .loc = logger.Loc.Empty,
};
let prefill_dependencies_key = js_ast.Expr{
    .data = .{ .e_string = &prefill_dependencies_e_string },
    .loc = logger.Loc.Empty,
};

const prefill_bun_bun_for_nextjs_task: string = "bun bun --use next";

struct _CreateFileCopier {
    static fn copy(
        destination_dir_: std.fs.Dir,
        walker: *Walker,
        node_: *Progress.Node,
        progress_: *Progress,
        dst_base_len: if (Environment.isWindows) usize else void,
        dst_buf: if (Environment.isWindows) *bun.WPathBuffer else void,
        src_base_len: if (Environment.isWindows) usize else void,
        src_buf: if (Environment.isWindows) *bun.WPathBuffer else void,
    )! {
        while (entry := walker.next().unwrap()?) {
            if (comptime Environment.isWindows) {
                if (entry.kind != .file && entry.kind != .directory) continue;

                @memcpy(dst_buf[dst_base_len..][0..entry.path.len], entry.path);
                dst_buf[dst_base_len + entry.path.len] = 0;
                const dst = dst_buf[0 .. dst_base_len + entry.path.len :0];

                @memcpy(src_buf[src_base_len..][0..entry.path.len], entry.path);
                src_buf[src_base_len + entry.path.len] = 0;
                const src = src_buf[0 .. src_base_len + entry.path.len :0];

                switch (entry.kind) {
                    .directory => {
                        if (bun.windows.CreateDirectoryExW(src.ptr, dst.ptr, null) == 0) {
                            bun.MakePath.makePath(u16, destination_dir_, entry.path) catch {};
                        }
                    },
                    .file => {
                        defer node_.completeOne();
                        if (bun.windows.CopyFileW(src.ptr, dst.ptr, 0) == bun.windows.FALSE) {
                            if (entry_dirname := bun.Dirname.dirname(u16, entry.path)) {
                                bun.MakePath.makePath(u16, destination_dir_, entry_dirname) catch {};
                                if (bun.windows.CopyFileW(src.ptr, dst.ptr, 0) != bun.windows.FALSE) {
                                    continue;
                                }
                            }

                            if (e := bun.windows.Win32Error.get().toSystemErrno()) {
                                Output.err(e, "failed to copy file {f}", .{
                                    bun.fmt.fmtOSPath(entry.path, .{}),
                                });
                            } else {
                                Output.errGeneric("failed to copy file {f}", .{
                                    bun.fmt.fmtOSPath(entry.path, .{}),
                                });
                            }
                            node_.end();
                            progress_.refresh();
                            Global.crash();
                        }
                    },
                    else => unreachable,
                }

                continue;
            }
            if (entry.kind != .file) continue;

            let outfile = bun.FD.fromStdFile(destination_dir_.createFile(entry.path, .{}) catch brk: {
                if (entry_dirname := bun.Dirname.dirname(bun.OSPathChar, entry.path)) {
                    bun.MakePath.makePath(bun.OSPathChar, destination_dir_, entry_dirname) catch {};
                }
                break :brk destination_dir_.createFile(entry.path, .{}) catch e => {
                    node_.end();
                    progress_.refresh();
                    Output.err(e, "failed to copy file {f}", .{ bun.fmt.fmtOSPath(entry.path, .{})});
                    Global.crash();
                };
            });
            const _guard_outfile = CloseGuard<bun.FD>{ ptr: &outfile };
            defer node_.completeOne();

            let infile = entry.dir.openat(entry.basename, bun.O.RDONLY, 0).unwrap()?;
            const _guard_infile = CloseGuard<bun.FD>{ ptr: &infile };

            // Assumption: you only really care about making sure something that was executable is still executable
            switch (infile.stat()) {
                .err => {},
                .result => |stat| {
                    * = outfile.chmod(usize(stat.mode));
                },
            }

            CopyFile.copyFile(infile, outfile).unwrap() catch e => {
                node_.end();
                progress_.refresh();
                Output.err(e, "failed to copy file {f}", .{ bun.fmt.fmtOSPath(entry.path, .{})});
                Global.crash();
            };
        }
    }
}

struct _CreateAnalyzer {
    ctx: Command.Context,
    example_tag: Example.Tag,
    entry_point: []const u8,
    node: *Progress.Node,
    progress: *Progress,
    static fn onAnalyze(analyzer_ptr: *_CreateAnalyzer, result: *bun.bundle_v2.BundleV2.DependenciesScanner.Result)! {
        analyzer_ptr.node.end();

        SourceFileProjectGenerator.generate(analyzer_ptr.ctx, analyzer_ptr.example_tag, analyzer_ptr.entry_point, result)?;
    }
}

pub struct ExtractInfoResult {
    example_tag: Example.Tag,
    template: []const u8,
}

pub struct CreateCommand {
    static fn exec(ctx: Command.Context, example_tag: Example.Tag, template: []const u8)! {
        @branchHint(.cold);

        Global.configureAllocator({ long_running: false });
        HTTP.HTTPThread.init([]);

        let create_options = CreateOptions.parse(ctx)?;
        const positionals = create_options.positionals;

        if (positionals.len == 0) {
            return CreateListExamplesCommand.exec(ctx)?;
        }

        let filesystem = fs.FileSystem.init(null)?;
        let env_loader: DotEnv.Loader = brk: {
            const map = ctx.allocator.create(DotEnv.Map)?;
            map.* = DotEnv.Map.init(ctx.allocator);

            break :brk DotEnv.Loader.init(map, ctx.allocator);
        };

        env_loader.loadProcess()?;

        const dirname: string = brk: {
            if (positionals.len == 1) {
                break :brk std.fs.path.basename(template);
            }

            break :brk positionals[1];
        };

        const destination = filesystem.dirname_store.append(string, resolve_path.joinAbs(filesystem.top_level_dir, .loose, dirname))?;

        let progress = Progress{};
        progress.supports_ansi_escape_codes = Output.enable_ansi_colors_stderr;
        let node = switch (example_tag) {
            .jslike_file => progress.start(ProgressBuf.print("Analyzing {s}", .{ template})?, 0),
            else => progress.start(ProgressBuf.print("Loading {s}", .{ template})?, 0),
        };

        // alacritty is fast
        if (env_loader.map.get("ALACRITTY_LOG") != null) {
            progress.refresh_rate_ns = std.time.ns_per_ms * 8;
        }

        defer {
            progress.refresh();
        }

        let package_json_contents: MutableString = undefined;
        let package_json_file: std.fs.File? = null;

        if (example_tag != .local_folder) {
            if (create_options.verbose) {
                Output.prettyErrorln("Downloading as {s}\n", .{ @tagName(example_tag)});
            }
        }

        switch (example_tag) {
            .jslike_file => {
                return runOnEntryPoint(ctx, example_tag, template, &progress, node)?;
            },
            .github_repository, .official => {
                const tarball_bytes: MutableString = switch (example_tag) {
                    .official => Example.fetch(ctx, &env_loader, template, &progress, node) catch e => {
                        switch (e) {
                            error.HTTPForbidden, error.ExampleNotFound => {
                                node.end();
                                progress.refresh();

                                Output.prettyError("\n<r><red>error:<r> <b>\"{s}\"<r> was not found. Here are templates you can use:\n\n", .{ 
                                    template,
                                });
                                Output.flush();

                                const examples = Example.fetchAllLocalAndRemote(ctx, null, &env_loader, filesystem)?;
                                Example.print(examples.items, dirname);
                                Global.exit(1);
                            },
                            else => {
                                node.end();
                                progress.refresh();

                                Output.prettyErrorln("\n\n", .{});

                                return e;
                            },
                        }
                    },
                    .github_repository => Example.fetchFromGitHub(ctx, &env_loader, template, &progress, node) catch e => {
                        switch (e) {
                            error.HTTPForbidden => {
                                node.end();
                                progress.refresh();

                                Output.prettyError("\n<r><red>error:<r> GitHub returned 403. This usually means GitHub is rate limiting your requests.\nTo fix this, either:<r>  <b>A) pass a <r><cyan>GITHUB_ACCESS_TOKEN<r> environment variable to bun<r>\n  <b>B)Wait a little and try again<r>\n", .{});
                                Global.crash();
                            },

                            error.GitHubRepositoryNotFound => {
                                node.end();
                                progress.refresh();

                                Output.prettyError("\n<r><red>error:<r> <b>\"{s}\"<r> was not found on GitHub. Here are templates you can use:\n\n", .{ 
                                    template,
                                });
                                Output.flush();

                                const examples = Example.fetchAllLocalAndRemote(ctx, null, &env_loader, filesystem)?;
                                Example.print(examples.items, dirname);
                                Global.crash();
                            },
                            else => {
                                node.end();
                                progress.refresh();

                                Output.prettyErrorln("\n\n", .{});

                                return e;
                            },
                        }
                    },
                    else => unreachable,
                };

                node.name = ProgressBuf.print("Decompressing {s}", .{ template})?;
                node.setCompletedItems(0);
                node.setEstimatedTotalItems(0);

                progress.refresh();

                const file_buf = ctx.allocator.alloc(u8, 16384)?;

                let tarball_buf_list = std.ArrayListUnmanaged(u8).fromOwnedSlice(file_buf);
                let gunzip = Zlib.ZlibReaderArrayList.init(tarball_bytes.list.items, &tarball_buf_list, ctx.allocator)?;
                const _guard_gunzip = DeinitGuard<Zlib.ZlibReaderArrayList>{ ptr: gunzip };
                gunzip.readAll(true)?;

                node.name = ProgressBuf.print("Extracting {s}", .{ template})?;
                node.setCompletedItems(0);
                node.setEstimatedTotalItems(0);

                progress.refresh();

                let pluckers: [1]Archiver.Plucker = if (!create_options.skip_package_json)
                    [Archiver.Plucker.init(comptime strings.literal(bun.OSPathChar, "package.json"), 2048, ctx.allocator)?]
                else
                    [@as(Archiver.Plucker, undefined)];

                let archive_context = Archiver.Context{
                    pluckers: pluckers[0..@as(usize, i32(@intFromBool(!create_options.skip_package_json)))],
                    all_files: undefined,
                    overwrite_list: bun.StringArrayHashMap(void).init(ctx.allocator),
                };

                if (!create_options.overwrite) {
                    Archiver.getOverwritingFileList(
                        tarball_buf_list.items,
                        destination,
                        &archive_context,
                        @TypeOf(filesystem.dirname_store),
                        filesystem.dirname_store,
                        1,
                    )?;

                    inline for (never_conflict_path of never_conflict) {
                        * = archive_context.overwrite_list.swapRemove(never_conflict_path);
                    }

                    if (archive_context.overwrite_list.count() > 0) {
                        node.end();
                        progress.refresh();

                        // Thank you create-react-app for this copy (and idea)
                        Output.prettyErrorln(
                            "<r>\n<red>error<r><d>: <r>The directory <b><blue>{s}<r>/ contains files that could conflict:\n\n",
                            .{ 
                                std.fs.path.basename(destination),
                            },
                        );
                        for (path of archive_context.overwrite_list.keys()) {
                            if (strings.endsWith(path, std.fs.path.sep_str)) {
                                Output.prettyError("<r>  <blue>{s}<r>", .{ path[0 .. @max(path.len, 1) - 1]});
                                Output.prettyErrorln(std.fs.path.sep_str, .{});
                            } else {
                                Output.prettyErrorln("<r>  {s}", .{ path});
                            }
                        }

                        Output.prettyErrorln("<r>\n<d>To download {s} anyway, use --force<r>", .{ template});
                        Global.exit(1);
                    }
                }

                const empty_extract_ctx: void = undefined;
                * = Archiver.extractToDisk(
                    tarball_buf_list.items,
                    destination,
                    &archive_context,
                    void,
                    empty_extract_ctx,
                    {
                        depth_to_skip: 1,
                    },
                )?;

                if (!create_options.skip_package_json) {
                    const plucker = pluckers[0];

                    if (plucker.found && plucker.fd.isValid()) {
                        node.name = "Updating package.json";
                        progress.refresh();

                        package_json_contents = plucker.contents;
                        package_json_file = plucker.fd.stdFile();
                    }
                }
            },
            .local_folder => {
                let template_parts: [1]string = [ template ];

                node.name = "Copying files";
                progress.refresh();

                const abs_template_path = filesystem.abs(&template_parts);
                const template_dir = std.fs.openDirAbsolute(abs_template_path, { iterate: true }) catch e => {
                    node.end();
                    progress.refresh();

                    Output.prettyErrorln("<r><red>{s}<r>: opening dir {s}", .{ @errorName(e), template });
                    Global.exit(1);
                };

                std.fs.deleteTreeAbsolute(destination) catch {};
                let destination_dir__ = std.fs.cwd().makeOpenPath(destination, .{}) catch e => {
                    node.end();

                    progress.refresh();

                    Output.prettyErrorln("<r><red>{s}<r>: creating dir {s}", .{ @errorName(e), destination });
                    Global.exit(1);
                };
                defer destination_dir__.close();

                let destination_buf: if (Environment.isWindows) bun.WPathBuffer else void = undefined;
                const dst_without_trailing_slash: if (Environment.isWindows) string else void = if (comptime Environment.isWindows) strings.withoutTrailingSlash(destination) else @as(void, undefined);
                if (comptime Environment.isWindows) {
                    strings.copyU8IntoU16(&destination_buf, dst_without_trailing_slash);
                    destination_buf[dst_without_trailing_slash.len] = std.fs.path.sep;
                }

                let template_path_buf: if (Environment.isWindows) bun.WPathBuffer else void = undefined;
                const src_without_trailing_slash: if (Environment.isWindows) string else void = if (comptime Environment.isWindows) strings.withoutTrailingSlash(abs_template_path) else @as(void, undefined);
                if (comptime Environment.isWindows) {
                    strings.copyU8IntoU16(&template_path_buf, src_without_trailing_slash);
                    template_path_buf[src_without_trailing_slash.len] = std.fs.path.sep;
                }

                const destination_dir = destination_dir__;
                let walker_ = Walker.walk(.fromStdDir(template_dir), ctx.allocator, skip_files, skip_dirs)?;
                const _guard_walker = DeinitGuard<Walker>{ ptr: &walker_ };

                const FileCopier = _CreateFileCopier;

                FileCopier.copy(
                    destination_dir,
                    &walker_,
                    node,
                    &progress,
                    if (comptime Environment.isWindows) dst_without_trailing_slash.len + 1 else @as(void, undefined),
                    if (comptime Environment.isWindows) &destination_buf else @as(void, undefined),
                    if (comptime Environment.isWindows) src_without_trailing_slash.len + 1 else @as(void, undefined),
                    if (comptime Environment.isWindows) &template_path_buf else @as(void, undefined),
                )?;

                package_json_file = destination_dir.openFile("package.json", { mode: .read_write }) catch null;

                read_package_json: {
                    if (pkg := package_json_file) {
                        const size = brk: {
                            if (comptime Environment.isWindows) {
                                break :brk pkg.getEndPos()?;
                            }

                            let stat_ok = true;
                            const stat = pkg.stat() catch e => {
                                node.end();

                                progress.refresh();

                                package_json_file = null;
                                Output.prettyErrorln("Error reading package.json: <r><red>{s}", .{ @errorName(e)});
                                stat_ok = false;
                                std.mem.zeroes(std.fs.File.Stat)
                            };
                            if (!stat_ok) break :read_package_json;

                            if (stat.kind != .file || stat.size == 0) {
                                package_json_file = null;
                                node.end();

                                progress.refresh();
                                break :read_package_json;
                            }

                            break :brk stat.size;
                        };

                        package_json_contents = MutableString.init(ctx.allocator, size)?;
                        package_json_contents.list.expandToCapacity();

                        const prev_file_pos = if (comptime Environment.isWindows) pkg.getPos()? else 0;
                        let pread_ok = true;
                        * = pkg.preadAll(package_json_contents.list.items, 0) catch e => {
                            package_json_file = null;

                            node.end();

                            progress.refresh();

                            Output.prettyErrorln("Error reading package.json: <r><red>{s}", .{ @errorName(e)});
                            pread_ok = false;
                            0
                        };
                        if (!pread_ok) break :read_package_json;
                        if (comptime Environment.isWindows) pkg.seekTo(prev_file_pos)?;
                        // The printer doesn't truncate, so we must do so manually
                        std.posix.ftruncate(pkg.handle, 0) catch {};

                        initializeStore();
                    }
                }
            },
            else => unreachable,
        }

        node.end();
        progress.refresh();

        const is_nextjs = false;
        const is_create_react_app = false;
        const create_react_app_entry_point_path: string = "";
        let preinstall_tasks: std.ArrayListUnmanaged(string) = .empty;
        let postinstall_tasks: std.ArrayListUnmanaged(string) = .empty;
        let has_dependencies: bool = false;
        const PATH = env_loader.map.get("PATH") ?? "";

        {
            let parent_dir = std.fs.openDirAbsolute(destination, .{})?;
            const _guard_parent_dir = CloseGuard<std.fs.Dir>{ ptr: &parent_dir };
            if (comptime Environment.isWindows) {
                parent_dir.copyFile("gitignore", parent_dir, ".gitignore", .{}) catch {};
            } else {
                std.posix.linkat(parent_dir.fd, "gitignore", parent_dir.fd, ".gitignore", 0) catch {};
            }

            std.posix.unlinkat(
                parent_dir.fd,
                "gitignore",
                0,
            ) catch {};
            std.posix.unlinkat(
                parent_dir.fd,
                ".npmignore",
                0,
            ) catch {};
        }

        let start_command: string = "bun dev";

        process_package_json: {
            if (create_options.skip_package_json) package_json_file = null;

            if (package_json_file != null) {
                initializeStore();

                const source = &logger.Source.initPathString("package.json", package_json_contents.list.items);

                let parse_ok = true;
                let package_json_expr = JSON.parseUTF8(source, ctx.log, ctx.allocator) catch {
                    package_json_file = null;
                    parse_ok = false;
                    undefined
                };
                if (!parse_ok) break :process_package_json;

                if (package_json_expr.data != .e_object) {
                    package_json_file = null;
                    break :process_package_json;
                }

                let properties_list = std.array_list.Managed(js_ast.G.Property).fromOwnedSlice(default_allocator, package_json_expr.data.e_object.properties.slice());

                if (ctx.log.errors > 0) {
                    ctx.log.print(Output.errorWriter())?;

                    package_json_file = null;
                    break :process_package_json;
                }

                if (name_expr := package_json_expr.asProperty("name")) {
                    if (name_expr.expr.data == .e_string) {
                        const basename = std.fs.path.basename(destination);
                        name_expr.expr.data.e_string.data = unsafe { @constCast(basename) };
                    }
                }

                // const Needs = struct {
                //     bun_bun_for_nextjs: bool = false,
                //     bun_macro_relay: bool = false,
                //     bun_macro_relay_dependency: bool = false,
                //     bun_framework_next: bool = false,
                //     react_refresh: bool = false,
                // };
                // var needs = Needs{};
                // var has_relay = false;
                // var has_bun_framework_next = false;
                // var has_react_refresh = false;
                // var has_bun_macro_relay = false;
                // var has_react = false;
                // var has_react_scripts = false;

                // const Prune = struct {
                //     pub const packages = ComptimeStringMap(void, .{
                //         .{ "@parcel/babel-preset", .{} },
                //         .{ "@parcel/core", .{} },
                //         .{ "@swc/cli", .{} },
                //         .{ "@swc/core", .{} },
                //         .{ "@webpack/cli", .{} },
                //         .{ "react-scripts", .{} },
                //         .{ "webpack-cli", .{} },
                //         .{ "webpack", .{} },

                //         // one of cosmic config's imports breaks stuff
                //         .{ "cosmiconfig", .{} },
                //     });
                //     pub var prune_count: u16 = 0;

                //     pub fn prune(list: []js_ast.G.Property) []js_ast.G.Property {
                //         var i: usize = 0;
                //         var out_i: usize = 0;
                //         while (i < list.len) : (i += 1) {
                //             const key = list[i].key.?.data.e_string.data;

                //             const do_prune = packages.has(key);
                //             prune_count += @as(u16, @intCast(@intFromBool(do_prune)));

                //             if (!do_prune) {
                //                 list[out_i] = list[i];
                //                 out_i += 1;
                //             }
                //         }

                //         return list[0..out_i];
                //     }
                // };

                let dev_dependencies: js_ast.Expr? = null;
                let dependencies: js_ast.Expr? = null;

                if (q := package_json_expr.asProperty("devDependencies")) {
                    const property = q.expr;

                    if (property.data == .e_object && property.data.e_object.properties.len > 0) {
                        // unsupported_packages.update(property);

                        // has_react_scripts = has_react_scripts or property.hasAnyPropertyNamed(&.{"react-scripts"});
                        // has_relay = has_relay or property.hasAnyPropertyNamed(&.{ "react-relay", "relay-runtime", "babel-plugin-relay" });

                        // property.data.e_object.properties = js_ast.G.Property.List.fromBorrowedSliceDangerous(Prune.prune(property.data.e_object.properties.slice()));
                        if (property.data.e_object.properties.len > 0) {
                            has_dependencies = true;
                            dev_dependencies = q.expr;

                            // has_bun_framework_next = has_bun_framework_next or property.hasAnyPropertyNamed(&.{"bun-framework-next"});
                            // has_react = has_react or property.hasAnyPropertyNamed(&.{ "react", "react-dom", "react-relay", "@emotion/react" });
                            // has_bun_macro_relay = has_bun_macro_relay or property.hasAnyPropertyNamed(&.{"bun-macro-relay"});
                            // has_react_refresh = has_react_refresh or property.hasAnyPropertyNamed(&.{"react-refresh"});
                        }
                    }
                }

                if (q := package_json_expr.asProperty("dependencies")) {
                    const property = q.expr;

                    if (property.data == .e_object && property.data.e_object.properties.len > 0) {
                        // unsupported_packages.update(property);

                        // has_react_scripts = has_react_scripts or property.hasAnyPropertyNamed(&.{"react-scripts"});
                        // has_relay = has_relay or property.hasAnyPropertyNamed(&.{ "react-relay", "relay-runtime", "babel-plugin-relay" });
                        // property.data.e_object.properties = js_ast.G.Property.List.fromBorrowedSliceDangerous(Prune.prune(property.data.e_object.properties.slice()));

                        if (property.data.e_object.properties.len > 0) {
                            has_dependencies = true;
                            dependencies = q.expr;

                            // if (property.asProperty("next")) |next_q| {
                            // is_nextjs = true;
                            // needs.bun_bun_for_nextjs = true;

                            // next_q.expr.data.e_string.data = @constCast(target_nextjs_version);
                            // }

                            // has_bun_framework_next = has_bun_framework_next or property.hasAnyPropertyNamed(&.{"bun-framework-next"});
                            // has_react = has_react or is_nextjs or property.hasAnyPropertyNamed(&.{ "react", "react-dom", "react-relay", "@emotion/react" });
                            // has_react_refresh = has_react_refresh or property.hasAnyPropertyNamed(&.{"react-refresh"});
                            // has_bun_macro_relay = has_bun_macro_relay or property.hasAnyPropertyNamed(&.{"bun-macro-relay"});
                        }
                    }
                }

                // needs.bun_macro_relay = !has_bun_macro_relay and has_relay;
                // needs.react_refresh = !has_react_refresh and has_react;
                // needs.bun_framework_next = is_nextjs and !has_bun_framework_next;
                // needs.bun_bun_for_nextjs = is_nextjs;
                // needs.bun_macro_relay_dependency = needs.bun_macro_relay;
                // var bun_bun_for_react_scripts = false;

                // var bun_macros_prop: ?js_ast.Expr = null;
                // var bun_prop: ?js_ast.Expr = null;
                // var bun_relay_prop: ?js_ast.Expr = null;

                // var needs_bun_prop = needs.bun_macro_relay or has_bun_macro_relay;
                // var needs_bun_macros_prop = needs_bun_prop;

                // if (needs_bun_macros_prop) {
                //     if (package_json_expr.asProperty("bun")) |bun_| {
                //         needs_bun_prop = false;
                //         bun_prop = bun_.expr;
                //         if (bun_.expr.asProperty("macros")) |macros_q| {
                //             bun_macros_prop = macros_q.expr;
                //             needs_bun_macros_prop = false;
                //             if (macros_q.expr.asProperty("react-relay")) |react_relay_q| {
                //                 bun_relay_prop = react_relay_q.expr;
                //                 needs.bun_macro_relay = react_relay_q.expr.asProperty("graphql") == null;
                //             }

                //             if (macros_q.expr.asProperty("babel-plugin-relay/macro")) |react_relay_q| {
                //                 bun_relay_prop = react_relay_q.expr;
                //                 needs.bun_macro_relay = react_relay_q.expr.asProperty("graphql") == null;
                //             }
                //         }
                //     }
                // }

                // if (Prune.prune_count > 0) {
                //     Output.prettyErrorln("<r><d>[package.json] Pruned {d} unnecessary packages<r>", .{Prune.prune_count});
                // }

                // if (create_options.verbose) {
                // if (needs.bun_macro_relay) {
                //     Output.prettyErrorln("<r><d>[package.json] Detected Relay -> added \"bun-macro-relay\"<r>", .{});
                // }

                // if (needs.react_refresh) {
                //     Output.prettyErrorln("<r><d>[package.json] Detected React -> added \"react-refresh\"<r>", .{});
                // }

                // if (needs.bun_framework_next) {
                //     Output.prettyErrorln("<r><d>[package.json] Detected Next -> added \"bun-framework-next\"<r>", .{});
                // } else if (is_nextjs) {
                //     Output.prettyErrorln("<r><d>[package.json] Detected Next.js<r>", .{});
                // }

                // }

                // var needs_to_inject_dev_dependency = needs.react_refresh or needs.bun_macro_relay;
                // var needs_to_inject_dependency = needs.bun_framework_next;

                // const dependencies_to_inject_count = @as(usize, @intCast(@intFromBool(needs.bun_framework_next)));

                // const dev_dependencies_to_inject_count = @as(usize, @intCast(@intFromBool(needs.react_refresh))) +
                //     @as(usize, @intCast(@intFromBool(needs.bun_macro_relay)));

                // const new_properties_count = @as(usize, @intCast(@intFromBool(needs_to_inject_dev_dependency and dev_dependencies == null))) +
                //     @as(usize, @intCast(@intFromBool(needs_to_inject_dependency and dependencies == null))) +
                //     @as(usize, @intCast(@intFromBool(needs_bun_prop)));

                // if (new_properties_count != 0) {
                //     try properties_list.ensureUnusedCapacity(new_properties_count);
                // }

                const E = js_ast.E;

                prefill_bun_macro_relay_object.properties = js_ast.G.Property.List
                    .fromBorrowedSliceDangerous(prefill_bun_macro_relay_properties[0..]);
                prefill_bun_macros_relay_object.properties = js_ast.G.Property.List
                    .fromBorrowedSliceDangerous(&prefill_bun_macros_relay_object_properties);
                prefill_bun_macros_relay_only_object.properties = js_ast.G.Property.List
                    .fromBorrowedSliceDangerous(&prefill_bun_macros_relay_only_object_properties);

                // if (needs_to_inject_dev_dependency and dev_dependencies == null) {
                //     var e_object = try ctx.allocator.create(E.Object);

                //     e_object.* = E.Object{};

                //     const value = js_ast.Expr{ .data = .{ .e_object = e_object }, .loc = logger.Loc.Empty };
                //     properties_list.appendAssumeCapacity(js_ast.G.Property{
                //         .key = InjectionPrefill.dev_dependencies_key,
                //         .value = value,
                //     });
                //     dev_dependencies = value;
                // }

                // if (needs_to_inject_dependency and dependencies == null) {
                //     var e_object = try ctx.allocator.create(E.Object);

                //     e_object.* = E.Object{};

                //     const value = js_ast.Expr{ .data = .{ .e_object = e_object }, .loc = logger.Loc.Empty };
                //     properties_list.appendAssumeCapacity(js_ast.G.Property{
                //         .key = InjectionPrefill.dependencies_key,
                //         .value = value,
                //     });
                //     dependencies = value;
                // }

                // inject an object like this, handling each permutation of what may or may not exist:
                // {
                //    "bun": {
                //       "macros": {
                //          "react-relay": {
                //              "graphql": "bun-macro-relay"
                //          }
                //        }
                //    }
                // }
                // bun_section: {

                // "bun.macros.react-relay.graphql"
                // if (needs.bun_macro_relay and !needs_bun_prop and !needs_bun_macros_prop) {
                //     // "graphql" is the only valid one for now, so anything else in this object is invalid.
                //     bun_relay_prop.?.data.e_object = InjectionPrefill.bun_macros_relay_object.properties.ptr[0].value.?.data.e_object;
                //     needs_bun_macros_prop = false;
                //     needs_bun_prop = false;
                //     needs.bun_macro_relay = false;
                //     break :bun_section;
                // }

                // "bun.macros"
                // if (needs_bun_macros_prop and !needs_bun_prop) {
                //     var obj = bun_prop.?.data.e_object;
                //     var properties = try std.array_list.Managed(js_ast.G.Property).initCapacity(
                //         ctx.allocator,
                //         obj.properties.len + InjectionPrefill.bun_macros_relay_object.properties.len,
                //     );
                //     defer obj.properties.update(properties);

                //     try properties.insertSlice(0, obj.properties.slice());
                //     try properties.insertSlice(0, InjectionPrefill.bun_macros_relay_object.properties.slice());

                //     needs_bun_macros_prop = false;
                //     needs_bun_prop = false;
                //     needs.bun_macro_relay = false;
                //     break :bun_section;
                // }

                // "bun"
                // if (needs_bun_prop) {
                //     try properties_list.append(InjectionPrefill.bun_only_macros_relay_property);
                //     needs_bun_macros_prop = false;
                //     needs_bun_prop = false;
                //     needs.bun_macro_relay = false;
                //     break :bun_section;
                // }
                // }

                // if (needs_to_inject_dependency) {
                //     defer needs_to_inject_dependency = false;
                //     var obj = dependencies.?.data.e_object;
                //     var properties = try std.array_list.Managed(js_ast.G.Property).initCapacity(
                //         ctx.allocator,
                //         obj.properties.len + dependencies_to_inject_count,
                //     );
                //     try properties.insertSlice(0, obj.properties.slice());
                //     defer obj.properties.update(properties);
                //     if (needs.bun_framework_next) {
                //         properties.appendAssumeCapacity(InjectionPrefill.bun_framework_next_property);
                //         needs.bun_framework_next = false;
                //     }
                // }

                // if (needs_to_inject_dev_dependency) {
                //     defer needs_to_inject_dev_dependency = false;
                //     var obj = dev_dependencies.?.data.e_object;
                //     var properties = try std.array_list.Managed(js_ast.G.Property).initCapacity(
                //         ctx.allocator,
                //         obj.properties.len + dev_dependencies_to_inject_count,
                //     );
                //     try properties.insertSlice(0, obj.properties.slice());
                //     defer obj.properties.update(properties);
                //     if (needs.bun_macro_relay_dependency) {
                //         properties.appendAssumeCapacity(InjectionPrefill.bun_macro_relay_dependency);
                //         needs.bun_macro_relay_dependency = false;
                //     }

                //     if (needs.react_refresh) {
                //         properties.appendAssumeCapacity(InjectionPrefill.react_refresh_dependency);
                //         needs.react_refresh = false;
                //     }
                // }

                // this is a little dicey
                // The idea is:
                // Before the closing </body> tag of Create React App's public/index.html
                // Inject "<script type="module" src="/src/index.js" async></script>"
                // Only do this for create-react-app
                // Which we define as:
                // 1. has a "public/index.html"
                // 2. "react-scripts" in package.json dependencies or devDependencies
                // 3. has a src/index.{jsx,tsx,ts,mts,mcjs}
                // If at any point those expectations are not matched OR the string /src/index.js already exists in the HTML
                // don't do it!
                // if (has_react_scripts) {
                //     bail: {
                //         var public_index_html_parts = [_]string{ destination, "public/index.html" };
                //         var public_index_html_path = filesystem.absBuf(&public_index_html_parts, &bun_path_buf);

                //         const public_index_html_file = std.fs.cwd().openFile(public_index_html_path, .{ .mode = .read_write }) catch break :bail;
                //         defer public_index_html_file.close();

                //         const file_extensions_to_try = [_]string{ ".tsx", ".ts", ".jsx", ".js", ".mts", ".mcjs" };

                //         var found_file = false;
                //         var entry_point_path: string = "";
                //         var entry_point_file_parts = [_]string{ destination, "src/index" };
                //         var entry_point_file_path_base = filesystem.absBuf(&entry_point_file_parts, &bun_path_buf);

                //         for (file_extensions_to_try) |ext| {
                //             bun.copy(u8, bun_path_buf[entry_point_file_path_base.len..], ext);
                //             entry_point_path = bun_path_buf[0 .. entry_point_file_path_base.len + ext.len];
                //             std.fs.accessAbsolute(entry_point_path, .{}) catch continue;
                //             found_file = true;
                //             break;
                //         }
                //         if (!found_file) break :bail;

                //         var public_index_file_contents = public_index_html_file.readToEndAlloc(ctx.allocator, public_index_html_file.getEndPos() catch break :bail) catch break :bail;

                //         if (std.mem.indexOf(u8, public_index_file_contents, entry_point_path[destination.len..]) != null) {
                //             break :bail;
                //         }

                //         var body_closing_tag: usize = std.mem.lastIndexOf(u8, public_index_file_contents, "</body>") orelse break :bail;

                //         var public_index_file_out = std.array_list.Managed(u8).initCapacity(ctx.allocator, public_index_file_contents.len) catch break :bail;
                //         var html_writer = public_index_file_out.writer();

                //         _ = html_writer.writeAll(public_index_file_contents[0..body_closing_tag]) catch break :bail;

                //         create_react_app_entry_point_path = std.fmt.allocPrint(
                //             ctx.allocator,
                //             "./{s}",

                //             .{
                //                 std.mem.trimLeft(
                //                     u8,
                //                     entry_point_path[destination.len..],
                //                     "/",
                //                 ),
                //             },
                //         ) catch break :bail;

                //         html_writer.print(
                //             "<script type=\"module\" async src=\"/{s}\"></script>\n{s}",
                //             .{
                //                 create_react_app_entry_point_path[2..],
                //                 public_index_file_contents[body_closing_tag..],
                //             },
                //         ) catch break :bail;

                //         var outfile = std.mem.replaceOwned(u8, ctx.allocator, public_index_file_out.items, "%PUBLIC_URL%", "") catch break :bail;

                //         // don't do this actually
                //         // it completely breaks when there is more than one CSS file loaded
                //         // // bonus: check for an index.css file
                //         // // inject it into the .html file statically if the file exists but isn't already in
                //         // inject_css: {
                //         //     const head_i: usize = std.mem.indexOf(u8, outfile, "<head>") orelse break :inject_css;
                //         //     if (std.mem.indexOf(u8, outfile, "/src/index.css") != null) break :inject_css;

                //         //     bun.copy(u8, bun_path_buf[destination.len + "/src/index".len ..], ".css");
                //         //     var index_css_file_path = bun_path_buf[0 .. destination.len + "/src/index.css".len];
                //         //     std.fs.accessAbsolute(index_css_file_path, .{}) catch break :inject_css;
                //         //     var list = std.array_list.Managed(u8).fromOwnedSlice(ctx.allocator, outfile);
                //         //     list.insertSlice(head_i + "<head>".len, "<link rel=\"stylesheet\" href=\"/src/index.css\">\n") catch break :inject_css;
                //         //     outfile =try list.toOwnedSlice();
                //         // }

                //         public_index_html_file.pwriteAll(outfile, 0) catch break :bail;
                //         std.posix.ftruncate(public_index_html_file.handle, outfile.len + 1) catch break :bail;
                //         bun_bun_for_react_scripts = true;
                //         is_create_react_app = true;
                //         Output.prettyln("<r><d>[package.json] Added entry point {s} to public/index.html", .{create_react_app_entry_point_path});
                //     }
                // }

                package_json_expr.data.e_object.is_single_line = false;

                package_json_expr.data.e_object.properties = js_ast.G.Property.List.moveFromList(&properties_list);
                {
                    let i: usize = 0;
                    let property_i: usize = 0;
                    while (i < package_json_expr.data.e_object.properties.len) : (i += 1) {
                        const property: js_ast.G.Property = package_json_expr.data.e_object.properties.ptr[i];
                        const key = property.key.?.asString(ctx.allocator).?;

                        if (strings.eqlComptime(key, "scripts")) {
                            if (property.value.?.data == .e_object) {
                                let scripts_properties = property.value.?.data.e_object.properties.slice();

                                // if they're starting the app with "react-scripts start" or "next dev", that won't make sense
                                // if they launch with npm run start it will just be slower
                                let script_property_i: usize = 0;
                                let script_property_out_i: usize = 0;

                                while (script_property_i < scripts_properties.len) : (script_property_i += 1) {
                                    const script = scripts_properties[script_property_i].value.?.data.e_string.data;

                                    if (strings.contains(script, "react-scripts start") ||
                                        strings.contains(script, "next dev") ||
                                        strings.contains(script, "react-scripts eject"))
                                    {
                                        if (create_options.verbose) {
                                            Output.prettyErrorln("<r><d>[package.json] Pruned unnecessary script: {s}<r>", .{ script});
                                        }

                                        continue;
                                    }

                                    if (strings.contains(script, "react-scripts build")) {
                                        scripts_properties[script_property_i].value = prefill_npx_react_scripts_build;
                                    }

                                    scripts_properties[script_property_out_i] = scripts_properties[script_property_i];
                                    script_property_out_i += 1;
                                }

                                property.value.?.data.e_object.properties = js_ast.G.Property.List.fromBorrowedSliceDangerous(
                                    scripts_properties[0..script_property_out_i],
                                );
                            }
                        }

                        if (key.len == 0 || !strings.eqlComptime(key, "bun-create")) {
                            package_json_expr.data.e_object.properties.ptr[property_i] = property;
                            property_i += 1;
                            continue;
                        }

                        let value = property.value.?;
                        if (postinstall := value.asProperty("postinstall")) {
                            switch (postinstall.expr.data) {
                                .e_string => |single_task| {
                                    postinstall_tasks.append(
                                        ctx.allocator,
                                        single_task.string(ctx.allocator)?,
                                    )?;
                                },
                                .e_array => |tasks| {
                                    const items = tasks.slice();
                                    for (task of items) {
                                        if (task_entry := task.asString(ctx.allocator)) {
                                            // if (needs.bun_bun_for_nextjs or bun_bun_for_react_scripts) {
                                            //     var iter = std.mem.splitScalar(u8, task_entry, ' ');
                                            //     var last_was_bun = false;
                                            //     while (iter.next()) |current| {
                                            //         if (strings.eqlComptime(current, "bun")) {
                                            //             if (last_was_bun) {
                                            //                 needs.bun_bun_for_nextjs = false;
                                            //                 bun_bun_for_react_scripts = false;
                                            //                 break;
                                            //             }
                                            //             last_was_bun = true;
                                            //         }
                                            //     }
                                            // }

                                            postinstall_tasks.append(
                                                ctx.allocator,
                                                task_entry,
                                            )?;
                                        }
                                    }
                                },
                                else => {},
                            }
                        }

                        if (preinstall := value.asProperty("preinstall")) {
                            switch (preinstall.expr.data) {
                                .e_string => |single_task| {
                                    preinstall_tasks.append(
                                        ctx.allocator,
                                        single_task.string(ctx.allocator)?,
                                    )?;
                                },
                                .e_array => |tasks| {
                                    for (task of tasks.items.slice()) {
                                        if (task_entry := task.asString(ctx.allocator)) {
                                            preinstall_tasks.append(
                                                ctx.allocator,
                                                task_entry,
                                            )?;
                                        }
                                    }
                                },
                                else => {},
                            }
                        }

                        if (start := value.asProperty("start")) {
                            if (start_str := start.expr.asString(ctx.allocator)) {
                                if (start_str.len > 0) {
                                    start_command = start_str;
                                }
                            }
                        }
                    }
                    package_json_expr.data.e_object.properties.shrinkRetainingCapacity(property_i);
                }

                const file: bun.FD = .fromStdFile(package_json_file.?);

                let buffer_writer = JSPrinter.BufferWriter.init(bun.default_allocator);
                buffer_writer.append_newline = true;
                let package_json_writer = JSPrinter.BufferPrinter.init(buffer_writer);

                let write_ok = true;
                * = JSPrinter.printJSON(
                    @TypeOf(&package_json_writer),
                    &package_json_writer,
                    package_json_expr,
                    source,
                    { mangled_props: null },
                ) catch e => {
                    Output.prettyErrorln("package.json failed to write due to error {s}", .{ @errorName(e)});
                    package_json_file = null;
                    write_ok = false;
                    @as(usize, 0)
                };
                if (!write_ok) break :process_package_json;
                const written = package_json_writer.ctx.getWritten();
                const void_ok: void = undefined;
                bun.sys.File.writeAll({ handle: file }, written).unwrap() catch e => {
                    Output.prettyErrorln("package.json failed to write due to error {s}", .{ @errorName(e)});
                    package_json_file = null;
                    write_ok = false;
                    void_ok
                };
                if (!write_ok) break :process_package_json;
                file.truncate(i32(written.len)).unwrap() catch e => {
                    Output.prettyErrorln("package.json failed to write due to error {s}", .{ @errorName(e)});
                    package_json_file = null;
                    write_ok = false;
                    void_ok
                };
                if (!write_ok) break :process_package_json;
            }
        }

        if (create_options.verbose) {
            Output.prettyErrorln("Has dependencies? {d}", .{ @intFromBool(has_dependencies)});
        }

        let npm_client_: NPMClient? = null;

        create_options.skip_install = create_options.skip_install || !has_dependencies;

        if (!create_options.skip_git) {
            if (!create_options.skip_install) {
                GitHandler.spawn(destination, PATH, create_options.verbose);
            } else {
                if (create_options.verbose) {
                    create_options.skip_git = GitHandler.run(destination, PATH, true) catch false;
                } else {
                    create_options.skip_git = GitHandler.run(destination, PATH, false) catch false;
                }
            }
        }

        if (!create_options.skip_install) {
            npm_client_ = NPMClient{
                tag: .bun,
                bin: bun.selfExePath()?,
            };
        }

        if (npm_client_ != null && preinstall_tasks.items.len > 0) {
            for (task of preinstall_tasks.items) {
                execTask(ctx.allocator, task, destination, PATH, npm_client_.?);
            }
        }

        if (npm_client := npm_client_) {
            const start_time = std.time.nanoTimestamp();
            const install_args: []const string = &[ npm_client.bin, "install" ];
            Output.flush();
            Output.pretty("\n<r><d>$ <b><cyan>{s}<r><d> install", .{ @tagName(npm_client.tag)});

            if (install_args.len > 2) {
                for (arg of install_args[2..]) {
                    Output.pretty(" ", .{});
                    Output.pretty("{s}", .{ arg});
                }
            }

            Output.pretty("<r>\n", .{});
            Output.flush();
            defer {
                Output.printErrorln("\n", .{});
                Output.printStartEnd(start_time, std.time.nanoTimestamp());
                Output.prettyError(" <r><d>{s} install<r>\n", .{ @tagName(npm_client.tag)});
                Output.flush();

                Output.print("\n", .{});
                Output.flush();
            }

            const process = bun.spawnSync(&{
                argv: install_args,
                envp: null,
                cwd: destination,
                stderr: .inherit,
                stdout: .inherit,
                stdin: .inherit,

                windows: if (Environment.isWindows) .{
            loop: bun.jsc.EventLoopHandle.init(bun.jsc.MiniEventLoop.initGlobal(null, null)),
        } else @as(void, undefined),
            })?;
            * = process.unwrap()?;
        }

        if (postinstall_tasks.items.len > 0) {
            for (task of postinstall_tasks.items) {
                execTask(ctx.allocator, task, destination, PATH, npm_client_);
            }
        }

        if (!create_options.skip_install && !create_options.skip_git) {
            create_options.skip_git = !GitHandler.wait();
        }

        Output.printError("\n", .{});
        Output.printStartEnd(ctx.start_time, std.time.nanoTimestamp());
        Output.prettyErrorln(" <r><d>bun create {s}<r>", .{ template});

        Output.flush();

        Output.pretty("\n<d>Come hang out in bun's Discord: https://bun.com/discord<r>\n", .{});

        if (!create_options.skip_install) {
            Output.pretty("\n<r><d>-----<r>\n", .{});
            Output.flush();
        }

        // if (unsupported_packages.@"styled-jsx") {
        //     Output.prettyErrorln("\n", .{});
        //     unsupported_packages.print();
        //     Output.prettyErrorln("\n", .{});
        //     Output.flush();
        // }

        if (!create_options.skip_git && !create_options.skip_install) {
            Output.pretty("\n<d>A local git repository was created for you and dependencies were installed automatically.<r>\n", .{});
        } else if (!create_options.skip_git) {
            Output.pretty("\n<d>A local git repository was created for you.<r>\n", .{});
        } else if (!create_options.skip_install) {
            Output.pretty("\n<d>Dependencies were installed automatically.<r>\n", .{});
        }

        if (example_tag == .github_repository) {
            let display_name = template;

            if (first_slash := std.mem.indexOfScalar(u8, display_name, '/')) {
                if (second_slash := std.mem.indexOfScalar(u8, display_name[first_slash + 1 ..], '/')) {
                    display_name = template[0 .. first_slash + 1 + second_slash];
                }
            }

            Output.pretty("\n<b><green>Success!<r> <b>{s}<r> loaded into <b>{s}<r>\n", .{ display_name, std.fs.path.basename(destination) });
        } else {
            Output.pretty("\n<b>Created <green>{s}<r> project successfully\n", .{ std.fs.path.basename(template)});
        }

        if (is_nextjs) {
            Output.pretty("\n<r><d>#<r> When dependencies change, run this to update node_modules.bun:\n\n  <b><cyan>bun bun --use next<r>\n", .{});
        } else if (is_create_react_app) {
            Output.pretty("\n<r><d>#<r> When dependencies change, run this to update node_modules.bun:\n\n  <b><cyan>bun bun {s}<r>\n", .{ create_react_app_entry_point_path});
        }

        const rel_destination = filesystem.relativeTo(destination);
        const is_empty_destination = rel_destination.len == 0;

        if (is_empty_destination) {
            Output.pretty("\n<d>#<r><b> To get started, run:<r>\n\n  <b><cyan>{s}<r>\n\n", .{
                start_command,
            });
        } else {
            Output.pretty("\n<d>#<r><b> To get started, run:<r>\n\n  <b><cyan>cd {s}<r>\n  <b><cyan>{s}<r>\n\n", .{
                rel_destination,
                start_command,
            });
        }

        Output.flush();

        if (create_options.open) {
            if (bin := which(&bun_path_buf, PATH, destination, "bun")) {
                const argv: []const string = &[ bun.asByteSlice(bin) ];
                let child = std.process.Child.init(argv, ctx.allocator);
                child.cwd = destination;
                child.stdin_behavior = .Inherit;
                child.stdout_behavior = .Inherit;
                child.stderr_behavior = .Inherit;

                open.openURL("http://localhost:3000/");

                child.spawn()?;
                * = child.wait() catch {};
            }
        }
    }

    static fn runOnEntryPoint(ctx: Command.Context, example_tag: Example.Tag, entry_point: []const u8, progress: *Progress, node: *Progress.Node)! {
        const Analyzer = _CreateAnalyzer;

        let analyzer = Analyzer{
            ctx: ctx,
            example_tag: example_tag,
            entry_point: entry_point,
            progress: progress,
            node: node,
        };

        let fetcher = bun.bundle_v2.BundleV2.DependenciesScanner{
            ctx: &analyzer,
            entry_points: &[ analyzer.entry_point ],
            onFetch: unsafe { @ptrCast(&Analyzer.onAnalyze) },
        };
        const build_exec = bun.cli.BuildCommand.exec;
        build_exec(bun.cli.Command.get(), &fetcher)?;
    }
    static fn extractInfo(ctx: Command.Context): !ExtractInfoResult {
        let example_tag = Example.Tag.unknown;
        let filesystem = fs.FileSystem.init(null)?;

        const create_options = CreateOptions.parse(ctx)?;
        const positionals = create_options.positionals;
        if (positionals.len == 0) {
            bun.cli.Command.Tag.printHelp(.CreateCommand, false);
            Global.crash();
        }

        let env_loader: DotEnv.Loader = brk: {
            const map = ctx.allocator.create(DotEnv.Map)?;
            map.* = DotEnv.Map.init(ctx.allocator);

            break :brk DotEnv.Loader.init(map, ctx.allocator);
        };

        env_loader.loadProcess()?;

        // var unsupported_packages = UnsupportedPackages{};
        const template = brk: {
            let positional = positionals[0];

            outer: {
                let parts: [2]string = [ filesystem.top_level_dir, positional ];
                const outdir_path = filesystem.absBuf(&parts, &home_dir_buf);
                home_dir_buf[outdir_path.len] = 0;
                const outdir_path_ = home_dir_buf[0..outdir_path.len :0];
                if (bun.path.hasAnyIllegalChars(outdir_path_)) break :outer;

                if (exists_at_type := bun.FD.cwd().existsAtType(outdir_path_).asValue()) {
                    if (exists_at_type == .file) {
                        const extension = std.fs.path.extension(positional);
                        if (tag := Example.Tag.fromFileExtension(extension)) {
                            example_tag = tag;
                            break :brk bun.handleOom(bun.default_allocator.dupe(u8, outdir_path));
                        }
                        // Show a warning when the local file exists and it's not a .js file
                        // A lot of create-* npm packages have .js in the name, so you could end up with that warning.
                        else if (extension.len > 0 && !strings.eqlComptime(extension, ".js")) {
                            Output.warn("bun create [local file] only supports .jsx and .tsx files currently", .{});
                        }
                    }
                }
            }

            if (!std.fs.path.isAbsolute(positional)) {
                outer: {
                    if (home_dir := env_loader.map.get("BUN_CREATE_DIR")) {
                        let parts: [2]string = [ home_dir, positional ];
                        const outdir_path = filesystem.absBuf(&parts, &home_dir_buf);
                        home_dir_buf[outdir_path.len] = 0;
                        const outdir_path_ = home_dir_buf[0..outdir_path.len :0];
                        if (bun.path.hasAnyIllegalChars(outdir_path_)) break :outer;
                        if (bun.FD.cwd().directoryExistsAt(outdir_path_).isTrue()) {
                            example_tag = Example.Tag.local_folder;
                            break :brk outdir_path;
                        }
                    }
                }

                outer: {
                    let parts: [3]string = [ filesystem.top_level_dir, BUN_CREATE_DIR, positional ];
                    const outdir_path = filesystem.absBuf(&parts, &home_dir_buf);
                    home_dir_buf[outdir_path.len] = 0;
                    const outdir_path_ = home_dir_buf[0..outdir_path.len :0];
                    if (bun.path.hasAnyIllegalChars(outdir_path_)) break :outer;
                    if (bun.FD.cwd().directoryExistsAt(outdir_path_).isTrue()) {
                        example_tag = Example.Tag.local_folder;
                        break :brk outdir_path;
                    }
                }

                outer: {
                    if (home_dir := env_loader.map.get("HOME")) {
                        let parts: [3]string = [ home_dir, BUN_CREATE_DIR, positional ];
                        const outdir_path = filesystem.absBuf(&parts, &home_dir_buf);
                        home_dir_buf[outdir_path.len] = 0;
                        const outdir_path_ = home_dir_buf[0..outdir_path.len :0];
                        if (bun.path.hasAnyIllegalChars(outdir_path_)) break :outer;
                        if (bun.FD.cwd().directoryExistsAt(outdir_path_).isTrue()) {
                            example_tag = Example.Tag.local_folder;
                            break :brk outdir_path;
                        }
                    }
                }

                if (std.fs.path.isAbsolute(positional)) {
                    example_tag = Example.Tag.local_folder;
                    break :brk positional;
                }

                let repo_begin: usize = std.math.maxInt(usize);
                // "https://github.com/foo/bar"
                if (strings.startsWith(positional, "github.com/")) {
                    repo_begin = "github.com/".len;
                }

                if (strings.startsWith(positional, "https://github.com/")) {
                    repo_begin = "https://github.com/".len;
                }

                if (repo_begin == std.math.maxInt(usize) && positional[0] != '/') {
                    if (first_slash_index := std.mem.indexOfScalar(u8, positional, '/')) {
                        if (last_slash_index := std.mem.indexOfScalar(u8, positional, '/')) {
                            if (first_slash_index == last_slash_index &&
                                positional[last_slash_index..].len > 0 &&
                                last_slash_index > 0)
                            {
                                repo_begin = 0;
                            }
                        }
                    }
                }

                if (repo_begin != std.math.maxInt(usize)) {
                    const remainder = positional[repo_begin..];
                    if (i := std.mem.indexOfScalar(u8, remainder, '/')) {
                        if (i > 0 && remainder[i + 1 ..].len > 0) {
                            if (last_slash := std.mem.indexOfScalar(u8, remainder[i + 1 ..], '/')) {
                                example_tag = Example.Tag.github_repository;
                                break :brk std.mem.trim(u8, remainder[0 .. i + 1 + last_slash], "# \r\t");
                            } else {
                                example_tag = Example.Tag.github_repository;
                                break :brk std.mem.trim(u8, remainder, "# \r\t");
                            }
                        }
                    }
                }
            }
            example_tag = Example.Tag.official;
            break :brk positional;
        };
        return { example_tag: example_tag, template: template };
    }
}
const Commands = .{
    &[ "" ],
    &[ "" ],
    &[ "" ],
};

pub struct DownloadedExample {
    tarball_bytes: MutableString,
    example: Example,
}

pub enum ExampleTag {
    unknown,
    github_repository,
    official,
    local_folder,
    jslike_file,

    const ExtensionTagMap = bun.ComptimeStringMap(ExampleTag, .{
        .{ ".tsx", .jslike_file },
        .{ ".jsx", .jslike_file },
    });
    pub static fn fromFileExtension(extension: string): ExampleTag? {
        return ExtensionTagMap.get(extension);
    }
}

let example_url: URL = undefined;
let app_name_buf: [512]u8 = undefined;
let github_repository_url_buf: [1024]u8 = undefined;

pub struct Example {
    name: string,
    version: string,
    description: string,
    local: bool = false,

    pub const Tag = ExampleTag;

    const examples_url: string = "https://registry.npmjs.org/bun-examples-all/latest";

    static fn print(examples: []const Example, default_app_name: string?) {
        for (example of examples) {
            const app_name = default_app_name ?? (std.fmt.bufPrint(&app_name_buf, "./{s}-app", .{ example.name[0..@min(example.name.len, 492)]}) catch unreachable);

            if (example.description.len > 0) {
                Output.pretty("  <r># {s}<r>\n  <b>bun create <cyan>{s}<r><b> {s}<r>\n<d>  \n\n", .{ 
                    example.description,
                    example.name,
                    app_name,
                });
            } else {
                Output.pretty("  <r><b>bun create <cyan>{s}<r><b> {s}<r>\n\n", .{ 
                    example.name,
                    app_name,
                });
            }
        }
    }

    static fn fetchAllLocalAndRemote(ctx: Command.Context, node: (*Progress.Node)?, env_loader: *DotEnv.Loader, filesystem: *fs.FileSystem): !std.array_list.Managed(Example) {
        const remote_examples = Example.fetchAll(ctx, env_loader, node)?;
        if (node_ := node) node_.end();

        let examples = std.array_list.Managed(Example).fromOwnedSlice(ctx.allocator, remote_examples);
        {
            let folders: [3]std.fs.Dir = [
                bun.invalid_fd.stdDir(),
                bun.invalid_fd.stdDir(),
                bun.invalid_fd.stdDir(),
            ];
            if (home_dir := env_loader.map.get("BUN_CREATE_DIR")) {
                let parts: [1]string = [ home_dir ];
                const outdir_path = filesystem.absBuf(&parts, &home_dir_buf);
                folders[0] = std.fs.cwd().openDir(outdir_path, .{}) catch bun.invalid_fd.stdDir();
            }

            {
                let parts: [2]string = [ filesystem.top_level_dir, BUN_CREATE_DIR ];
                const outdir_path = filesystem.absBuf(&parts, &home_dir_buf);
                folders[1] = std.fs.cwd().openDir(outdir_path, .{}) catch bun.invalid_fd.stdDir();
            }

            if (home_dir := env_loader.map.get(bun.env_var.HOME.key())) {
                let parts: [2]string = [ home_dir, BUN_CREATE_DIR ];
                const outdir_path = filesystem.absBuf(&parts, &home_dir_buf);
                folders[2] = std.fs.cwd().openDir(outdir_path, .{}) catch bun.invalid_fd.stdDir();
            }

            // subfolders with package.json
            for (folder of folders) {
                if (folder.fd != bun.invalid_fd.cast()) {
                    let iter = folder.iterate();

                    loop: while (entry_ := iter.next() catch null) {
                        const entry: std.fs.Dir.Entry = entry_;

                        switch (entry.kind) {
                            .directory => {
                                inline for (skip_dir of skip_dirs) {
                                    if (strings.eqlComptime(entry.name, comptime bun.pathLiteral(skip_dir))) {
                                        continue :loop;
                                    }
                                }

                                bun.copy(u8, &home_dir_buf, entry.name);
                                home_dir_buf[entry.name.len] = std.fs.path.sep;
                                bun.copy(u8, home_dir_buf[entry.name.len + 1 ..], "package.json");
                                home_dir_buf[entry.name.len + 1 + "package.json".len] = 0;

                                const path: [:0]u8 = home_dir_buf[0 .. entry.name.len + 1 + "package.json".len :0];

                                folder.accessZ(path, { mode: .read_only }) catch continue :loop;

                                examples.append(
                                    Example{
                                        name: filesystem.filename_store.append(@TypeOf(entry.name), entry.name)?,
                                        version: "",
                                        local: true,
                                        description: "",
                                    },
                                )?;
                                continue :loop;
                            },
                            else => continue,
                        }
                    }
                }
            }
        }

        return examples;
    }


    static fn fetchFromGitHub(
        ctx: Command.Context,
        env_loader: *DotEnv.Loader,
        name: string,
        refresher: *Progress,
        progress: *Progress.Node,
    ): !MutableString {
        const owner_i = std.mem.indexOfScalar(u8, name, '/').?;
        const owner = name[0..owner_i];
        let repository = name[owner_i + 1 ..];

        if (i := std.mem.indexOfScalar(u8, repository, '/')) {
            repository = repository[0..i];
        }

        progress.name = ProgressBuf.pretty("<d>[github] <b>GET<r> <blue>{s}/{s}<r>", .{ owner, repository })?;
        refresher.refresh();

        let github_api_domain: string = "api.github.com";
        if (api_domain := env_loader.map.get("GITHUB_API_DOMAIN")) {
            if (api_domain.len > 0) {
                github_api_domain = api_domain;
            }
        }

        const api_url = URL.parse(
            std.fmt.bufPrint(
                &github_repository_url_buf,
                "https://{s}/repos/{s}/{s}/tarball",
                .{ github_api_domain, owner, repository },
            )?,
        );

        let header_entries: Headers.Entry.List = {};
        let headers_buf: string = "";

        if (access_token := env_loader.map.get("GITHUB_TOKEN") ?? env_loader.map.get("GITHUB_ACCESS_TOKEN")) {
            if (access_token.len > 0) {
                headers_buf = std.fmt.allocPrint(ctx.allocator, "AuthorizationBearer {s}", .{ access_token})?;
                header_entries.append(
                    ctx.allocator,
                    {
                        name: {
                            offset: 0,
                            length: u32("Authorization".len),
                        },
                        value: {
                            offset: u32("Authorization".len),
                            length: u32(headers_buf.len - "Authorization".len),
                        },
                    },
                )?;
            }
        }

        const http_proxy: URL? = env_loader.getHttpProxyFor(api_url);
        const mutable = ctx.allocator.create(MutableString)?;
        mutable.* = MutableString.init(ctx.allocator, 8192)?;

        // ensure very stable memory address
        let async_http: *HTTP.AsyncHTTP = ctx.allocator.create(HTTP.AsyncHTTP) catch unreachable;
        async_http.* = HTTP.AsyncHTTP.initSync(
            ctx.allocator,
            .GET,
            api_url,
            header_entries,
            headers_buf,
            mutable,
            "",
            http_proxy,
            null,
            HTTP.FetchRedirect.follow,
        );
        async_http.client.progress_node = progress;
        async_http.client.flags.reject_unauthorized = env_loader.getTLSRejectUnauthorized();

        const response = async_http.sendSync()?;

        switch (response.status_code) {
            404 => return error.GitHubRepositoryNotFound,
            403 => return error.HTTPForbidden,
            429 => return error.HTTPTooManyRequests,
            499..=599 => return error.NPMIsDown,
            200 => {},
            else => return error.HTTPError,
        }

        let is_expected_content_type = false;
        let content_type: string = "";
        for (header of response.headers.list) {
            if (strings.eqlCaseInsensitiveASCII(header.name, "content-type", true)) {
                content_type = header.value;

                if (strings.eqlComptime(header.value, "application/x-gzip")) {
                    is_expected_content_type = true;
                    break;
                }
            }
        }

        if (!is_expected_content_type) {
            progress.end();
            refresher.refresh();

            if (content_type.len > 0) {
                Output.prettyErrorln("<r><red>error<r>: Unexpected content type from GitHub: {s}", .{ content_type});
                Global.crash();
            } else {
                Output.prettyErrorln("<r><red>error<r>: Invalid response from GitHub (missing content type)", .{});
                Global.crash();
            }
        }

        if (mutable.list.items.len == 0) {
            progress.end();
            refresher.refresh();

            Output.prettyErrorln("<r><red>error<r>: Invalid response from GitHub (missing body)", .{});
            Global.crash();
        }

        return mutable.*;
    }

    static fn fetch(ctx: Command.Context, env_loader: *DotEnv.Loader, name: string, refresher: *Progress, progress: *Progress.Node): !MutableString {
        progress.name = "Fetching package.json";
        refresher.refresh();

        let url_buf: [1024]u8 = undefined;
        let mutable = ctx.allocator.create(MutableString)?;
        mutable.* = MutableString.init(ctx.allocator, 2048)?;

        example_url = URL.parse(std.fmt.bufPrint(&url_buf, "https://registry.npmjs.org/@bun-examples/{s}/latest", .{ name})?);

        let http_proxy: URL? = env_loader.getHttpProxyFor(example_url);

        // ensure very stable memory address
        let async_http: *HTTP.AsyncHTTP = ctx.allocator.create(HTTP.AsyncHTTP) catch unreachable;
        async_http.* = HTTP.AsyncHTTP.initSync(
            ctx.allocator,
            .GET,
            example_url,
            .{},
            "",
            mutable,
            "",
            http_proxy,
            null,
            HTTP.FetchRedirect.follow,
        );
        async_http.client.progress_node = progress;
        async_http.client.flags.reject_unauthorized = env_loader.getTLSRejectUnauthorized();

        let response = async_http.sendSync()?;

        switch (response.status_code) {
            404 => return error.ExampleNotFound,
            403 => return error.HTTPForbidden,
            429 => return error.HTTPTooManyRequests,
            499..=599 => return error.NPMIsDown,
            200 => {},
            else => return error.HTTPError,
        }

        progress.name = "Parsing package.json";
        refresher.refresh();
        initializeStore();
        const source = &logger.Source.initPathString("package.json", mutable.list.items);
        let expr = JSON.parseUTF8(source, ctx.log, ctx.allocator) catch e => {
            progress.end();
            refresher.refresh();

            if (ctx.log.errors > 0) {
                ctx.log.print(Output.errorWriter())?;
                Global.exit(1);
            } else {
                Output.prettyErrorln("Error parsing package: <r><red>{s}<r>", .{ @errorName(e)});
                Global.exit(1);
            }
        };

        if (ctx.log.errors > 0) {
            progress.end();
            refresher.refresh();

            ctx.log.print(Output.errorWriter())?;
            Global.exit(1);
        }

        const tarball_url: string = brk: {
            if (q := expr.asProperty("dist")) {
                if (p := q.expr.asProperty("tarball")) {
                    if (s := p.expr.asString(ctx.allocator)) {
                        if (s.len > 0 && (strings.startsWith(s, "https://") || strings.startsWith(s, "http://"))) {
                            break :brk ctx.allocator.dupe(u8, s) catch unreachable;
                        }
                    }
                }
            }

            progress.end();
            refresher.refresh();

            Output.prettyErrorln("package.json is missing tarball example_url. This is an internal error!", .{});
            Global.exit(1);
        };

        progress.name = "Downloading tarball";
        refresher.refresh();

        // reuse mutable buffer
        // safe because the only thing we care about is the tarball example_url
        mutable.reset();

        // ensure very stable memory address
        const parsed_tarball_url = URL.parse(tarball_url);

        http_proxy = env_loader.getHttpProxyFor(parsed_tarball_url);

        async_http.* = HTTP.AsyncHTTP.initSync(
            ctx.allocator,
            .GET,
            parsed_tarball_url,
            .{},
            "",
            mutable,
            "",
            http_proxy,
            null,
            HTTP.FetchRedirect.follow,
        );
        async_http.client.progress_node = progress;
        async_http.client.flags.reject_unauthorized = env_loader.getTLSRejectUnauthorized();

        refresher.maybeRefresh();

        response = async_http.sendSync()?;

        refresher.maybeRefresh();

        if (response.status_code != 200) {
            progress.end();
            refresher.refresh();
            Output.prettyErrorln("Error fetching tarball: <r><red>{d}<r>", .{ response.status_code});
            Global.exit(1);
        }

        refresher.refresh();

        return mutable.*;
    }

    static fn fetchAll(ctx: Command.Context, env_loader: *DotEnv.Loader, progress_node: (*Progress.Node)?): ![]Example {
        example_url = URL.parse(examples_url);

        const http_proxy: URL? = env_loader.getHttpProxyFor(example_url);

        let async_http: *HTTP.AsyncHTTP = ctx.allocator.create(HTTP.AsyncHTTP) catch unreachable;
        const mutable = ctx.allocator.create(MutableString)?;
        mutable.* = MutableString.init(ctx.allocator, 2048)?;

        async_http.* = HTTP.AsyncHTTP.initSync(
            ctx.allocator,
            .GET,
            example_url,
            .{},
            "",
            mutable,
            "",
            http_proxy,
            null,
            HTTP.FetchRedirect.follow,
        );
        async_http.client.flags.reject_unauthorized = env_loader.getTLSRejectUnauthorized();

        if (Output.enable_ansi_colors_stdout) {
            async_http.client.progress_node = progress_node;
        }

        const response = async_http.sendSync() catch e => {
            switch (e) {
                error.WouldBlock => {
                    Output.prettyErrorln("Request timed out while trying to fetch examples list. Please try again", .{});
                    Global.exit(1);
                },
                else => {
                    Output.prettyErrorln("<r><red>{s}<r> while trying to fetch examples list. Please try again", .{ @errorName(e)});
                    Global.exit(1);
                },
            }
        };

        if (response.status_code != 200) {
            Output.prettyErrorln("<r><red>{d}<r> fetching examples :( .{s}", .{ response.status_code, mutable.list.items });
            Global.exit(1);
        }

        initializeStore();
        const source = &logger.Source.initPathString("examples.json", mutable.list.items);
        const examples_object = JSON.parseUTF8(source, ctx.log, ctx.allocator) catch e => {
            if (ctx.log.errors > 0) {
                ctx.log.print(Output.errorWriter())?;
                Global.exit(1);
            } else {
                Output.prettyErrorln("Error parsing examples: <r><red>{s}<r>", .{ @errorName(e)});
                Global.exit(1);
            }
        };

        if (ctx.log.errors > 0) {
            ctx.log.print(Output.errorWriter())?;
            Global.exit(1);
        }

        if (q := examples_object.asProperty("examples")) {
            if (q.expr.data == .e_object) {
                const count = q.expr.data.e_object.properties.len;

                let list = ctx.allocator.alloc(Example, count)?;
                const props = q.expr.data.e_object.properties.slice();
                for (i of 0..props.len) {
                    const property = props[i];
                    const name = property.key.?.data.e_string.data;
                    let final_name = name;
                    if (slash := std.mem.indexOfScalar(u8, name, '/')) {
                        final_name = name[slash + 1 ..];
                    }
                    list[i] = Example{
                        name: final_name,
                        version: property.value.?.asProperty("version").?.expr.data.e_string.data,
                        description: property.value.?.asProperty("description").?.expr.data.e_string.data,
                    };
                }
                return list;
            }
        }

        Output.prettyErrorln("Corrupt examples data: expected object but received {s}", .{ @tagName(examples_object.data)});
        Global.exit(1);
    }
}

pub struct CreateListExamplesCommand {
    static fn exec(ctx: Command.Context)! {
        const filesystem = fs.FileSystem.init(null)?;
        let env_loader: DotEnv.Loader = brk: {
            const map = ctx.allocator.create(DotEnv.Map)?;
            map.* = DotEnv.Map.init(ctx.allocator);

            break :brk DotEnv.Loader.init(map, ctx.allocator);
        };

        env_loader.loadProcess()?;

        let progress = Progress{};
        progress.supports_ansi_escape_codes = Output.enable_ansi_colors_stderr;
        const node = progress.start("Fetching manifest", 0);
        progress.refresh();

        const examples = Example.fetchAllLocalAndRemote(ctx, node, &env_loader, filesystem)?;
        Output.prettyln("Welcome to bun! Create a new project by pasting any of the following:\n\n", .{});
        Output.flush();

        Example.print(examples.items, null);

        Output.prettyln("<r><d>#<r> You can also paste a GitHub repository:\n\n  <b>bun create <cyan>ahfarmer/calculator calc<r>\n\n", .{});

        if (homedir := env_loader.map.get(bun.env_var.HOME.key())) {
            Output.prettyln(
                "<d>This command is completely optional. To add a new local template, create a folder in {s}/.bun-create/. To publish a new template, git clone https://github.com/oven-sh/bun, add a new folder to the \"examples\" folder, and submit a PR.<r>",
                .{ homedir},
            );
        } else {
            Output.prettyln(
                "<d>This command is completely optional. To add a new local template, create a folder in $HOME/.bun-create/. To publish a new template, git clone https://github.com/oven-sh/bun, add a new folder to the \"examples\" folder, and submit a PR.<r>",
                .{},
            );
        }

        Output.flush();
    }
}

let git_success: std.atomic.Value(u32) = undefined;
let git_thread: std.Thread = undefined;

struct GitHandler {
    static fn spawn(
        destination: string,
        PATH: string,
        verbose: bool,
    ) {
        git_success = std.atomic.Value(u32).init(0);

        git_thread = std.Thread.spawn(.{}, spawnThread, .{ destination, PATH, verbose }) catch e => {
            Output.prettyErrorln("<r><red>{s}<r>", .{ @errorName(e)});
            Global.exit(1);
        };
    }

    static fn spawnThread(
        destination: string,
        PATH: string,
        verbose: bool,
    ) {
        Output.Source.configureNamedThread("git");
        defer Output.flush();
        const outcome = if (verbose)
            run(destination, PATH, true) catch false
        else
            run(destination, PATH, false) catch false;

        git_success.store(
            if (outcome)
                1
            else
                2,
            .release,
        );
        Futex.wake(&git_success, 1);
    }

    static fn wait(): bool {
        while (git_success.load(.acquire) == 0) {
            Futex.wait(&git_success, 0, 1000) catch continue;
        }

        const outcome = git_success.load(.acquire) == 1;
        git_thread.join();
        return outcome;
    }

    static fn run(
        destination: string,
        PATH: string,
        comptime verbose: bool,
    ): !bool {
        const git_start = std.time.nanoTimestamp();

        // Not sure why...
        // But using libgit for this operation is slower than the CLI!
        // Used to have a feature flag to try it but was removed:
        // https://github.com/oven-sh/bun/commit/deafd3d0d42fb8d7ddf2b06cde2d7c7ee8bc7144
        //
        // ~/Build/throw
        // ❯ hyperfine "bun create react3 app --force --no-install" --prepare="rm -rf app"
        // Benchmark #1: bun create react3 app --force --no-install
        //   Time (mean ± σ):     974.6 ms ±   6.8 ms    [User: 170.5 ms, System: 798.3 ms]
        //   Range (min … max):   960.8 ms … 984.6 ms    10 runs
        //
        // ❯ mv /usr/local/opt/libgit2/lib/libgit2.dylib /usr/local/opt/libgit2/lib/libgit2.dylib.1
        //
        // ~/Build/throw
        // ❯ hyperfine "bun create react3 app --force --no-install" --prepare="rm -rf app"
        // Benchmark #1: bun create react3 app --force --no-install
        //   Time (mean ± σ):     306.7 ms ±   6.1 ms    [User: 31.7 ms, System: 269.8 ms]
        //   Range (min … max):   299.5 ms … 318.8 ms    10 runs

        if (git := which(&bun_path_buf, PATH, destination, "git")) {
            const cmd_0: [3]string = [ git, "init", "--quiet" ];
            const cmd_1: [4]string = [ git, "add", destination, "--ignore-errors" ];
            const cmd_2: [5]string = [ git, "commit", "-am", "Initial commit (via bun create)", "--quiet" ];
            const git_commands: [3][]const string = [ &cmd_0, &cmd_1, &cmd_2 ];

            if (comptime verbose) {
                Output.prettyErrorln("git backend: {s}", .{ git});
            }

            // same names, just comptime known values

            for (command of git_commands) {
                let process = std.process.Child.init(command, default_allocator);
                process.cwd = destination;
                process.stdin_behavior = .Inherit;
                process.stdout_behavior = .Inherit;
                process.stderr_behavior = .Inherit;

                * = process.spawnAndWait()?;
                * = process.kill() catch {};
            }

            Output.prettyError("\n", .{});
            Output.printStartEnd(git_start, std.time.nanoTimestamp());
            Output.prettyError(" <d>git<r>\n", .{});
            return true;
        }

        return false;
    }
}

type string = []const u8;

import CopyFile from "../sys/copy_file.zig";
import DotEnv from "../dotenv/env_loader.zig";
import SourceFileProjectGenerator from "./create/SourceFileProjectGenerator.zig";
import Zlib from "../zlib/zlib.zig";
import fs from "../resolver/fs.zig";
import resolve_path from "../paths/resolve_path.zig";
import Walker from "../sys/walker_skippable.zig";
import open from "./open.zig";
import cli_mod from "./cli.zig";
const Command = cli_mod.Command;
import which_npm_client_mod from "./which_npm_client.zig";
const NPMClient = which_npm_client_mod.NPMClient;
import url_mod from "../url/url.zig";
const URL = url_mod.URL;
import which_mod from "../which/which.zig";
const which = which_mod.which;

import bun from "bun";
const Environment = bun.Environment;
const Futex = bun.Futex;
const Global = bun.Global;
const JSON = bun.json;
const JSPrinter = bun.js_printer;
const MutableString = bun.MutableString;
const Output = bun.Output;
const Progress = bun.Progress;
const clap = bun.clap;
const default_allocator = bun.default_allocator;
const js_ast = bun.ast;
const logger = bun.logger;
const strings = bun.strings;
const Archiver = bun.libarchive.Archiver;

const HTTP = bun.http;
const Headers = bun.http.Headers;
