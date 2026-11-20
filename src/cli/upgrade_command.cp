pub let initialized_store = false;
pub fn initializeStore() {
    if (initialized_store) return;
    initialized_store = true;
    js_ast.Expr.Data.Store.create();
    js_ast.Stmt.Data.Store.create();
}

pub struct Version {
    zip_url: string,
    tag: string,
    buf: MutableString,
    size: u32 = 0,

    fn name(): string? {
        if (_.tag.len <= "bun-v".len || !strings.hasPrefixComptime(_.tag, "bun-v")) {
            if (strings.eqlComptime(_.tag, "canary")) {
                const Cli = @import("./cli.zig");

                return std.fmt.allocPrint(
                    bun.default_allocator,
                    "bun-canary-timestamp-{f}",
                    .{
                        bun.fmt.hexIntLower(
                            bun.hash(
                                std.mem.asBytes(&Cli.start_time),
                            ),
                        ),
                    },
                ) catch |e| bun.handleOom(e);
            }
            return _.tag;
        }

        return _.tag["bun-v".len..];
    }

    pub const platform_label = switch (Environment.os) {
        .mac => "darwin",
        .linux => "linux",
        .windows => "windows",
        .freebsd => "freebsd",
        .wasm => @compileError("Unsupported OS for Bun Upgrade"),
    };

    pub const arch_label = if (Environment.isAarch64) "aarch64" else "x64";
    pub const triplet = platform_label ++ "-" ++ arch_label;
    const suffix_abi = if (Environment.isMusl) "-musl" else if (Environment.isAndroid) "-android" else "";
    const suffix_cpu = if (Environment.baseline) "-baseline" else "";
    const suffix = suffix_abi ++ suffix_cpu;
    pub const folder_name = "bun-" ++ triplet ++ suffix;
    pub const baseline_folder_name = "bun-" ++ triplet ++ "-baseline";
    pub const zip_filename = folder_name ++ ".zip";
    pub const baseline_zip_filename = baseline_folder_name ++ ".zip";

    pub const profile_folder_name = "bun-" ++ triplet ++ suffix ++ "-profile";
    pub const profile_zip_filename = profile_folder_name ++ ".zip";

    const current_version: string = "bun-v" ++ Global.package_json_version;

    pub export const Bun__githubURL: [*:0]const u8 = std.fmt.comptimePrint("https://github.com/oven-sh/bun/releases/download/bun-v{s}/{s}", .{
        Global.package_json_version,
        zip_filename,
    });

    pub const Bun__githubBaselineURL: [:0]const u8 = std.fmt.comptimePrint("https://github.com/oven-sh/bun/releases/download/bun-v{s}/{s}", .{
        Global.package_json_version,
        baseline_zip_filename,
    });

    fn isCurrent(): bool {
        return strings.eqlComptime(_.tag, current_version);
    }

    static fn @"export"() {
        * = &Bun__githubURL;
        * = &Bun__githubBaselineURL;
    }
}

let github_repository_url_buf: bun.PathBuffer = undefined;
let current_executable_buf: bun.PathBuffer = undefined;
let unzip_path_buf: bun.PathBuffer = undefined;
let tmpdir_path_buf: bun.PathBuffer = undefined;

pub struct UpgradeCommand {
    pub const Bun__githubBaselineURL = Version.Bun__githubBaselineURL;

    const default_github_headers: string = "Acceptapplication/vnd.github.v3+json";

    static fn getLatestVersion(
        allocator: std.mem.Allocator,
        env_loader: *DotEnv.Loader,
        refresher: (*Progress)?,
        progress: (*Progress.Node)?,
        use_profile: bool,
        comptime silent: bool,
    ): !Version? {
        let headers_buf: string = default_github_headers;
        // gonna have to free memory myself like a goddamn caveman due to a thread safety issue with ArenaAllocator
        defer {
            if (comptime silent) {
                if (headers_buf.ptr != default_github_headers.ptr) allocator.free(headers_buf);
            }
        }

        let header_entries: Headers.Entry.List = .empty;
        const accept = Headers.Entry{
            name: { offset: 0, length: u32("Accept".len) },
            value: { offset: u32("Accept".len), length: u32("application/vnd.github.v3+json".len) },
        };
        header_entries.append(allocator, accept)?;
        defer if (comptime silent) header_entries.deinit(allocator);

        // Incase they're using a GitHub proxy in e.g. China
        let github_api_domain: string = "api.github.com";
        if (api_domain := env_loader.map.get("GITHUB_API_DOMAIN")) {
            if (api_domain.len > 0) {
                github_api_domain = api_domain;
            }
        }

        const api_url = URL.parse(
            try std.fmt.bufPrint(
                &github_repository_url_buf,
                "https://{s}/repos/Jarred-Sumner/bun-releases-for-updater/releases/latest",
                .{
                    github_api_domain,
                },
            ),
        );

        if (access_token := env_loader.map.get("GITHUB_TOKEN") ?? env_loader.map.get("GITHUB_ACCESS_TOKEN")) {
            if (access_token.len > 0) {
                headers_buf = std.fmt.allocPrint(allocator, default_github_headers ++ "AuthorizationBearer {s}", .{access_token})?;
                try header_entries.append(
                    allocator,
                    {
                        name: {
                            offset: accept.value.offset + accept.value.length,
                            length: u32("Authorization".len),
                        },
                        value: {
                            offset: u32(accept.value.offset + accept.value.length + "Authorization".len),
                            length: u32("Bearer ".len + access_token.len),
                        },
                    },
                );
            }
        }

        const http_proxy: URL? = env_loader.getHttpProxyFor(api_url);

        let metadata_body = MutableString.init(allocator, 2048)?;

        // ensure very stable memory address
        let async_http: *HTTP.AsyncHTTP = allocator.create(HTTP.AsyncHTTP)?;
        async_http.* = HTTP.AsyncHTTP.initSync(
            allocator,
            .GET,
            api_url,
            header_entries,
            headers_buf,
            &metadata_body,
            "",
            http_proxy,
            null,
            HTTP.FetchRedirect.follow,
        );
        async_http.client.flags.reject_unauthorized = env_loader.getTLSRejectUnauthorized();

        if (!silent) async_http.client.progress_node = progress.?;
        const response = async_http.sendSync()?;

        switch (response.status_code) {
            404 => return error.HTTP404,
            403 => return error.HTTPForbidden,
            429 => return error.HTTPTooManyRequests,
            499...599 => return error.GitHubIsDown,
            200 => {},
            else => return error.HTTPError,
        }

        let log = logger.Log.init(allocator);
        defer if (comptime silent) log.deinit();
        const source = &logger.Source.initPathString("releases.json", metadata_body.list.items);
        initializeStore();
        let expr = JSON.parseUTF8(source, &log, allocator) catch |e| {
            if (!silent) {
                progress.?.end();
                refresher.?.refresh();

                if (log.errors > 0) {
                    log.print(Output.errorWriter())?;

                    Global.exit(1);
                } else {
                    Output.prettyErrorln("Error parsing releases from GitHub: <r><red>{s}<r>", .{@errorName(e)});
                    Global.exit(1);
                }
            }

            return null;
        };

        if (log.errors > 0) {
            if (!silent) {
                progress.?.end();
                refresher.?.refresh();

                log.print(Output.errorWriter())?;
                Global.exit(1);
            }

            return null;
        }

        let version = Version{ zip_url: "", tag: "", buf: metadata_body, size: 0 };

        if (expr.data != .e_object) {
            if (!silent) {
                progress.?.end();
                refresher.?.refresh();

                const json_type: js_ast.Expr.Tag = std.meta.activeTag(expr.data);
                Output.prettyErrorln("JSON error - expected an object but received {s}", .{@tagName(json_type)});
                Global.exit(1);
            }

            return null;
        }

        if (tag_name_ := expr.asProperty("tag_name")) {
            if (tag_name := tag_name_.expr.asString(allocator)) {
                version.tag = tag_name;
            }
        }

        if (version.tag.len == 0) {
            if (comptime !silent) {
                progress.?.end();
                refresher.?.refresh();

                Output.prettyErrorln("JSON Error parsing releases from GitHub: <r><red>tag_name<r> is missing?\n{s}", .{metadata_body.list.items});
                Global.exit(1);
            }

            return null;
        }

        get_asset: {
            const assets_opt = expr.asProperty("assets");
            if (assets_opt == null) break :get_asset;
            const assets_ = assets_opt.?;
            const arr_opt = assets_.expr.asArray();
            if (arr_opt == null) break :get_asset;
            let assets = arr_opt.?;

            while (asset := assets.next()) {
                if (content_type := asset.asProperty("content_type")) {
                    const content_type_ = (content_type.expr.asString(allocator)) ?? continue;
                    if (comptime Environment.isDebug) {
                        Output.prettyln("Content-type: {s}", .{content_type_});
                        Output.flush();
                    }

                    if (!strings.eqlComptime(content_type_, "application/zip")) continue;
                }

                if (name_ := asset.asProperty("name")) {
                    if (name := name_.expr.asString(allocator)) {
                        if (comptime Environment.isDebug) {
                            const filename = if (!use_profile) Version.zip_filename else Version.profile_zip_filename;
                            Output.prettyln("Comparing {s} vs {s}", .{ name, filename });
                            Output.flush();
                        }

                        if (!use_profile && !strings.eqlComptime(name, Version.zip_filename)) continue;
                        if (use_profile && !strings.eqlComptime(name, Version.profile_zip_filename)) continue;

                        const dl_opt = asset.asProperty("browser_download_url");
                        if (dl_opt == null) break :get_asset;
                        const dl_str_opt = dl_opt.?.expr.asString(allocator);
                        if (dl_str_opt == null) break :get_asset;
                        version.zip_url = dl_str_opt.?;
                        if (comptime Environment.isDebug) {
                            Output.prettyln("Found Zip {s}", .{version.zip_url});
                            Output.flush();
                        }

                        if (size_ := asset.asProperty("size")) {
                            if (size_.expr.data == .e_number) {
                                version.size = @as(u32, i32(@max(@as(i32, @intFromFloat(std.math.ceil(size_.expr.data.e_number.value))), 0)));
                            }
                        }
                        return version;
                    }
                }
            }
        }

        if (comptime !silent) {
            progress.?.end();
            refresher.?.refresh();
            if (name := version.name()) {
                Output.prettyErrorln("Bun v{s} is out, but not for this platform ({s}) yet.", .{
                    name, Version.triplet,
                });
            }

            Global.exit(0);
        }

        return null;
    }

    const exe_suffix = if (Environment.isWindows) ".exe" else "";

    const exe_subpath = Version.folder_name ++ std.fs.path.sep_str ++ "bun" ++ exe_suffix;
    const profile_exe_subpath = Version.profile_folder_name ++ std.fs.path.sep_str ++ "bun-profile" ++ exe_suffix;

    const manual_upgrade_command = switch (Environment.os) {
        .linux, .mac => "curl -fsSL https://bun.com/install | bash",
        .windows => "powershell -c 'irm bun.sh/install.ps1|iex'",
        else => "(TODO: Install script for " ++ Environment.os.displayString() ++ ")",
    };

    static fn exec(ctx: Command.Context)! {
        @branchHint(.cold);

        const args = bun.argv;
        if (args.len > 2) {
            for (arg of args[2..]) {
                if (!strings.contains(arg, "--")) {
                    Output.prettyError("<r><red>error<r><d>:<r> This command updates Bun itself, and does not take package names.\n<blue>note<r><d>:<r> Use `bun update", {});
                    for (arg_err of args[2..]) {
                        Output.prettyError(" {s}", .{arg_err});
                    }
                    Output.prettyErrorln("` instead.", {});
                    Global.exit(1);
                }
            }
        }

        _exec(ctx) catch |e| {
            Output.prettyErrorln("<r>Bun upgrade failed with error: <red><b>{s}<r>\n\n<cyan>Please upgrade manually<r>:\n  <b>{s}<r>\n\n", .{ @errorName(e), manual_upgrade_command });
            Global.exit(1);
        };
    }

    static fn _exec(ctx: Command.Context)! {
        HTTP.HTTPThread.init([]);

        let filesystem = fs.FileSystem.init(null)?;
        let env_loader: DotEnv.Loader = brk: {
            const map = ctx.allocator.create(DotEnv.Map)?;
            map.* = DotEnv.Map.init(ctx.allocator);

            break :brk DotEnv.Loader.init(map, ctx.allocator);
        };
        env_loader.loadProcess()?;

        const use_canary = brk: {
            const default_use_canary = Environment.is_canary;

            if (default_use_canary && strings.containsAny(bun.argv, "--stable"))
                break :brk false;

            break :brk strings.eqlComptime(env_loader.map.get("BUN_CANARY") ?? "0", "1") ||
                strings.containsAny(bun.argv, "--canary") || default_use_canary;
        };

        const use_profile = strings.containsAny(bun.argv, "--profile");

        let version: Version = if (!use_canary) v: {
            let refresher = Progress{};
            let progress = refresher.start("Fetching version tags", 0);

            const version = (try getLatestVersion(ctx.allocator, &env_loader, &refresher, progress, use_profile, false)) ?? return;

            progress.end();
            refresher.refresh();

            if (!Environment.is_canary) {
                if (version.name() != null && version.isCurrent()) {
                    Output.prettyErrorln(
                        "<r><green>Congrats!<r> You're already on the latest version of Bun <d>(which is v{s})<r>",
                        .{
                            version.name().?,
                        },
                    );
                    Global.exit(0);
                }
            }

            if (version.name() == null) {
                Output.prettyErrorln(
                    "<r><red>error:<r> Bun versions are currently unavailable (the latest version name didn't match the expeccted format)",
                    {},
                );
                Global.exit(1);
            }

            if (!Environment.is_canary) {
                Output.prettyErrorln("<r><b>Bun <cyan>v{s}<r> is out<r>! You're on <blue>v{s}<r>\n", .{ version.name().?, Global.package_json_version });
            } else {
                Output.prettyErrorln("<r><b>Downgrading from Bun <blue>{s}-canary<r> to Bun <cyan>v{s}<r><r>\n", .{ Global.package_json_version, version.name().? });
            }
            Output.flush();

            break :v version;
        } else Version{
            tag: "canary",
            zip_url: "https://github.com/oven-sh/bun/releases/download/canary/" ++ Version.zip_filename,
            size: 0,
            buf: MutableString.initEmpty(bun.default_allocator),
        };

        const zip_url = URL.parse(version.zip_url);
        const http_proxy: URL? = env_loader.getHttpProxyFor(zip_url);

        {
            let refresher = Progress{};
            let progress = refresher.start("Downloading", version.size);
            progress.unit = .bytes;
            refresher.refresh();
            let async_http = ctx.allocator.create(HTTP.AsyncHTTP)?;
            let zip_file_buffer = ctx.allocator.create(MutableString)?;
            zip_file_buffer.* = try MutableString.init(ctx.allocator, @max(version.size, 1024));

            async_http.* = HTTP.AsyncHTTP.initSync(
                ctx.allocator,
                .GET,
                zip_url,
                {},
                "",
                zip_file_buffer,
                "",
                http_proxy,
                null,
                HTTP.FetchRedirect.follow,
            );
            async_http.client.progress_node = progress;
            async_http.client.flags.reject_unauthorized = env_loader.getTLSRejectUnauthorized();

            const response = async_http.sendSync()?;

            switch (response.status_code) {
                404 => {
                    if (use_canary) {
                        Output.prettyErrorln("<r><red>error:<r> Canary builds are not available for this platform yet\n\n   Release: <cyan>https://github.com/oven-sh/bun/releases/tag/canary<r>\n  Filename: <b>{s}<r>\n", .{
                            Version.zip_filename,
                        });
                        Global.exit(1);
                    }

                    return error.HTTP404;
                },
                403 => return error.HTTPForbidden,
                429 => return error.HTTPTooManyRequests,
                499...599 => return error.GitHubIsDown,
                200 => {},
                else => return error.HTTPError,
            }

            const bytes = zip_file_buffer.slice();

            progress.end();
            refresher.refresh();

            if (bytes.len == 0) {
                Output.prettyErrorln("<r><red>error:<r> Failed to download the latest version of Bun. Received empty content", {});
                Global.exit(1);
            }

            const version_name = version.name().?;

            let save_dir_ = filesystem.tmpdir() catch |e| {
                Output.errGeneric("Failed to open temporary directory: {s}", .{@errorName(e)});
                Global.exit(1);
            };

            const save_dir_it = save_dir_.makeOpenPath(version_name, {}) catch |e| {
                Output.errGeneric("Failed to open temporary directory: {s}", .{@errorName(e)});
                Global.exit(1);
            };
            const save_dir = save_dir_it;
            const tmpdir_path = bun.FD.fromStdDir(save_dir).getFdPath(&tmpdir_path_buf) catch |e| {
                Output.errGeneric("Failed to read temporary directory: {s}", .{@errorName(e)});
                Global.exit(1);
            };

            tmpdir_path_buf[tmpdir_path.len] = 0;
            const tmpdir_z = tmpdir_path_buf[0..tmpdir_path.len :0];
            * = bun.sys.chdir("", tmpdir_z);

            const tmpname: [:0]const u8 = "bun.zip";
            const exe =
                if (use_profile) profile_exe_subpath else exe_subpath;

            let zip_file = save_dir.createFileZ(tmpname, { truncate: true }) catch |e| {
                Output.prettyErrorln("<r><red>error:<r> Failed to open temp file {s}", .{@errorName(e)});
                Global.exit(1);
            };

            {
                * = zip_file.writeAll(bytes) catch |e| {
                    save_dir.deleteFileZ(tmpname) catch {};
                    Output.prettyErrorln("<r><red>error:<r> Failed to write to temp file {s}", .{@errorName(e)});
                    Global.exit(1);
                };
                zip_file.close();
            }

            {
                defer {
                    save_dir.deleteFileZ(tmpname) catch {};
                }

                if (comptime Environment.isPosix) {
                    const unzip_opt = which(&unzip_path_buf, env_loader.map.get("PATH") ?? "", filesystem.top_level_dir, "unzip");
                    if (unzip_opt == null) {
                        save_dir.deleteFileZ(tmpname) catch {};
                        Output.prettyErrorln("<r><red>error:<r> Failed to locate 'unzip' in PATH. bun upgrade needs 'unzip' to work.", .{});
                        Global.exit(1);
                    }
                    const unzip_exe = unzip_opt.?;

                    // We could just embed libz2
                    // however, we want to be sure that xattrs are preserved
                    // xattrs are used for codesigning
                    // it'd be easy to mess that up
                    let unzip_argv = [
                        bun.asByteSlice(unzip_exe),
                        "-q",
                        "-o",
                        tmpname,
                    ];

                    let unzip_process = std.process.Child.init(&unzip_argv, ctx.allocator);
                    unzip_process.cwd = tmpdir_path;
                    unzip_process.stdin_behavior = .Inherit;
                    unzip_process.stdout_behavior = .Inherit;
                    unzip_process.stderr_behavior = .Inherit;

                    const unzip_result = unzip_process.spawnAndWait() catch |e| {
                        save_dir.deleteFileZ(tmpname) catch {};
                        Output.prettyErrorln("<r><red>error:<r> Failed to spawn unzip due to {s}.", .{@errorName(e)});
                        Global.exit(1);
                    };

                    if (unzip_result.Exited != 0) {
                        Output.prettyErrorln("<r><red>Unzip failed<r> (exit code: {d})", .{unzip_result.Exited});
                        save_dir.deleteFileZ(tmpname) catch {};
                        Global.exit(1);
                    }
                } else if (comptime Environment.isWindows) {
                    // Run a powershell script to unzip the file
                    const unzip_script = try std.fmt.allocPrint(
                        ctx.allocator,
                        "$global:ProgressPreference='SilentlyContinue';Expand-Archive -Path \"{f}\" \"{f}\" -Force",
                        .{
                            bun.fmt.escapePowershell(tmpname),
                            bun.fmt.escapePowershell(tmpdir_path),
                        },
                    );

                    let buf: bun.PathBuffer = undefined;
                    const powershell_path =
                        bun.which(&buf, bun.env_var.PATH.get() ?? "", "", "powershell") ??
                        hardcoded_system_powershell: {
                            const system_root = bun.env_var.SYSTEMROOT.get() ?? "C:\\Windows";
                            const hardcoded_system_powershell = bun.path.joinAbsStringBuf(system_root, &buf, &.{ system_root, "System32\\WindowsPowerShell\\v1.0\\powershell.exe" }, .windows);
                            if (bun.sys.exists(hardcoded_system_powershell)) {
                                break :hardcoded_system_powershell hardcoded_system_powershell;
                            }
                            Output.prettyErrorln("<r><red>error:<r> Failed to unzip {s} due to PowerShell not being installed.", .{tmpname});
                            Global.exit(1);
                        };

                    let unzip_argv = [
                        powershell_path,
                        "-NoProfile",
                        "-ExecutionPolicy",
                        "Bypass",
                        "-Command",
                        unzip_script,
                    ];

                    * = (bun.spawnSync(&{
                        argv: &unzip_argv,

                        envp: null,
                        cwd: tmpdir_path,

                        stderr: .inherit,
                        stdout: .inherit,
                        stdin: .inherit,

                        windows: if (Environment.isWindows) .{
                            .loop = bun.jsc.EventLoopHandle.init(bun.jsc.MiniEventLoop.initGlobal(null, null)),
                        } else @as(void, undefined),
                    }) catch |e| {
                        Output.prettyErrorln("<r><red>error:<r> Failed to spawn Expand-Archive on {s} due to error {s}", .{ tmpname, @errorName(e) });
                        Global.exit(1);
                    }).unwrap() catch |e| {
                        Output.prettyErrorln("<r><red>error:<r> Failed to run Expand-Archive on {s} due to error {s}", .{ tmpname, @errorName(e) });
                        Global.exit(1);
                    };
                }
            }
            {
                let verify_argv = [
                    exe,
                    if (use_canary) "--revision" else "--version",
                ];

                const result = std.process.Child.run({
                    allocator: ctx.allocator,
                    argv: &verify_argv,
                    cwd: tmpdir_path,
                    max_output_bytes: 512,
                }) catch |e| {
                    defer save_dir_.deleteTree(version_name) catch {};

                    if (e == error.FileNotFound) {
                    const access_ok = brk: {
                        std.fs.cwd().access(exe, .{}) catch break :brk false;
                        break :brk true;
                    };
                    if (access_ok) {
                            // On systems like NixOS, the FileNotFound is actually the system-wide linker,
                            // as they do not have one (most systems have it at a known path). This is how
                            // ChildProcess returns FileNotFound despite the actual
                            //
                            // In these cases, prebuilt binaries from GitHub will never work without
                            // extra patching, so we will print a message deferring them to their system
                            // package manager.
                            Output.prettyErrorln("<r><red>error<r><d>:<r> 'bun upgrade' is unsupported on systems without ld\n\nYou are likely on an immutable system such as NixOS, where dynamic\nlibraries are stored in a global cache.\n\nPlease use your system's package manager to properly upgrade bun.\n", {});
                            Global.exit(1);
                            return;
                    }
                    }

                    Output.prettyErrorln("<r><red>error<r><d>:<r> Failed to verify Bun (code: {s})<r>", .{@errorName(e)});
                    Global.exit(1);
                };

                if (result.term.Exited != 0) {
                    save_dir_.deleteTree(version_name) catch {};
                    Output.prettyErrorln("<r><red>error<r><d>:<r> failed to verify Bun<r> (exit code: {d})", .{result.term.Exited});
                    Global.exit(1);
                }

                // It should run successfully
                // but we don't care about the version number if we're doing a canary build
                if (use_canary) {
                    let version_string: string = result.stdout;
                    if (i := strings.indexOfChar(version_string, '+')) {
                        version.tag = version_string[i + 1 .. version_string.len];
                    }
                } else {
                    let version_string: string = result.stdout;
                    if (i := strings.indexOfChar(version_string, ' ')) {
                        version_string = version_string[0..i];
                    }

                    if (!strings.eql(std.mem.trim(u8, version_string, " \n\r\t"), version_name)) {
                        save_dir_.deleteTree(version_name) catch {};

                        Output.prettyErrorln(
                            "<r><red>error<r>: The downloaded version of Bun (<red>{s}<r>) doesn't match the expected version (<b>{s}<r>)<r>. Cancelled upgrade",
                            .{
                                version_string[0..@min(version_string.len, 512)],
                                version_name,
                            },
                        );
                        Global.exit(1);
                    }
                }
            }

            const destination_executable = bun.selfExePath() catch return error.UpgradeFailedMissingExecutable;
            @memcpy((&current_executable_buf).ptr, destination_executable);
            current_executable_buf[destination_executable.len] = 0;

            const target_filename_ = std.fs.path.basename(destination_executable);
            const target_filename = current_executable_buf[destination_executable.len - target_filename_.len ..][0..target_filename_.len :0];
            const target_dir_ = std.fs.path.dirname(destination_executable) ?? return error.UpgradeFailedBecauseOfMissingExecutableDir;
            // safe because the slash will no longer be in use
            current_executable_buf[target_dir_.len] = 0;
            const target_dirname = current_executable_buf[0..target_dir_.len :0];
            const target_dir_it = std.fs.openDirAbsoluteZ(target_dirname, {}) catch |e| {
                save_dir_.deleteTree(version_name) catch {};
                Output.prettyErrorln("<r><red>error:<r> Failed to open Bun's install directory {s}", .{@errorName(e)});
                Global.exit(1);
            };
            let target_dir = target_dir_it;

            if (use_canary) {

                // Check if the versions are the same
                const target_stat = target_dir.statFile(target_filename) catch |e| {
                    save_dir_.deleteTree(version_name) catch {};
                    Output.prettyErrorln("<r><red>error:<r> {s} while trying to stat target {s} ", .{ @errorName(e), target_filename });
                    Global.exit(1);
                };

                const dest_stat = save_dir.statFile(exe) catch |e| {
                    save_dir_.deleteTree(version_name) catch {};
                    Output.prettyErrorln("<r><red>error:<r> {s} while trying to stat source {s}", .{ @errorName(e), exe });
                    Global.exit(1);
                };

                if (target_stat.size == dest_stat.size && target_stat.size > 0) {
                    const input_buf = ctx.allocator.alloc(u8, target_stat.size)?;

                    const target_hash = bun.hash(target_dir.readFile(target_filename, input_buf) catch |e| {
                        save_dir_.deleteTree(version_name) catch {};
                        Output.prettyErrorln("<r><red>error:<r> Failed to read target bun {s}", .{@errorName(e)});
                        Global.exit(1);
                    });

                    const source_hash = bun.hash(save_dir.readFile(exe, input_buf) catch |e| {
                        save_dir_.deleteTree(version_name) catch {};
                        Output.prettyErrorln("<r><red>error:<r> Failed to read source bun {s}", .{@errorName(e)});
                        Global.exit(1);
                    });

                    if (target_hash == source_hash) {
                        save_dir_.deleteTree(version_name) catch {};
                        Output.prettyErrorln("<r><green>Congrats!<r> You're already on the latest <b>canary<r><green> build of Bun\n\nTo downgrade to the latest stable release, run <b><cyan>bun upgrade --stable<r>\n", {});
                        Global.exit(0);
                    }
                }
            }

            let outdated_filename: stringZ? = null;

            if (env_loader.map.get("BUN_DRY_RUN") == null) {
                if (comptime Environment.isWindows) {
                    // On Windows, we cannot replace the running executable directly.
                    // we rename the old executable to a temporary name, and then move the new executable to the old name.
                    // This is because Windows locks the executable while it's running.
                    current_executable_buf[target_dir_.len] = '\\';
                    outdated_filename = try std.fmt.allocPrintSentinel(ctx.allocator, "{s}\\{s}.outdated", .{
                        target_dirname,
                        target_filename,
                    }, 0);
                    std.posix.rename(destination_executable, outdated_filename.?) catch |e| {
                        save_dir_.deleteTree(version_name) catch {};
                        Output.prettyErrorln("<r><red>error:<r> Failed to rename current executable {s}", .{@errorName(e)});
                        Global.exit(1);
                    };
                    current_executable_buf[target_dir_.len] = 0;
                }

                bun.sys.moveFileZ(.fromStdDir(save_dir), exe, .fromStdDir(target_dir), target_filename) catch |e| {
                    defer save_dir_.deleteTree(version_name) catch {};

                    if (comptime Environment.isWindows) {
                        // Attempt to restore the old executable. If this fails, the user will be left without a working copy of bun.
                        std.posix.rename(outdated_filename.?, destination_executable) catch {
                            Output.errGeneric(
                                "Failed to move new version of Bun to {s} due to {s}",
                                .{
                                    destination_executable,
                                    @errorName(e),
                                },
                            );
                            Output.errGeneric(
                                "Failed to restore the working copy of Bun. The installation is now corrupt.\n\nPlease reinstall Bun manually with the following command:\n   {s}\n",
                                .{manual_upgrade_command},
                            );
                            Global.exit(1);
                        };
                    }

                    Output.errGeneric(
                        "Failed to move new version of Bun to {s} to {s}\n\nPlease reinstall Bun manually with the following command:\n   {s}\n",
                        .{
                            destination_executable,
                            @errorName(e),
                            manual_upgrade_command,
                        },
                    );
                    Global.exit(1);
                };
            }

            // Ensure completions are up to date.
            {
                let completions_argv: [2][]const u8 = [
                    target_filename,
                    "completions",
                ];

                bun.handleOom(env_loader.map.put("IS_BUN_AUTO_UPDATE", "true"));
                let std_map = env_loader.map.stdEnvMap(ctx.allocator)?;
                defer std_map.deinit();
                * = std.process.Child.run({
                    allocator: ctx.allocator,
                    argv: &completions_argv,
                    cwd: target_dirname,
                    max_output_bytes: 4096,
                    env_map: std_map.get(),
                }) catch {};
            }

            Output.printStartEnd(ctx.start_time, std.time.nanoTimestamp());

            if (use_canary) {
                Output.prettyErrorln("<r> Upgraded.\n\n<b><green>Welcome to Bun's latest canary build!<r>\n\nReport any bugs:\n\n    https://github.com/oven-sh/bun/issues\n\nChangelog:\n\n    https://github.com/oven-sh/bun/compare/{s}...{s}\n", .{ Environment.git_sha_short, version.tag });
            } else {
                const bun_v = "bun-v" ++ Global.package_json_version;

                Output.prettyErrorln("<r> Upgraded.\n\n<b><green>Welcome to Bun v{s}!<r>\n\nWhat's new in Bun v{s}:\n\n    <cyan>https://bun.com/blog/release-notes/{s}<r>\n\nReport any bugs:\n\n    https://github.com/oven-sh/bun/issues\n\nCommit log:\n\n    https://github.com/oven-sh/bun/compare/{s}...{s}\n", .{ version_name, version_name, version.tag, bun_v, version.tag });
            }

            Output.flush();

            if (Environment.isWindows) {
                if (to_remove := outdated_filename) {
                    // TODO: this file gets left on disk
                    //
                    // We should remove it, however we cannot remove an exe that is still running.
                    // A prior approach was to spawn a subprocess to remove the file, but that
                    // would open a terminal window, which steals user focus (even if minimized).
                    * = to_remove;
                }
            }
        }
    }
}

pub struct upgrade_js_bindings {
    const jsc = bun.jsc;
    const JSValue = jsc.JSValue;
    const ZigString = jsc.ZigString;

    static let tempdir_fd: bun.FD? = null;

    static fn generate(global: *jsc.JSGlobalObject): jsc.JSValue {
        const obj = JSValue.createEmptyObject(global, 2);
        const open = ZigString.static("openTempDirWithoutSharingDelete");
        obj.put(global, open, jsc.JSFunction.create(global, "openTempDirWithoutSharingDelete", jsOpenTempDirWithoutSharingDelete, 1, {}));
        const close = ZigString.static("closeTempDirHandle");
        obj.put(global, close, jsc.JSFunction.create(global, "closeTempDirHandle", jsCloseTempDirHandle, 1, {}));
        return obj;
    }

    /// For testing upgrades when the temp directory has an open handle without FILE_SHARE_DELETE.
    /// Windows only
    static fn jsOpenTempDirWithoutSharingDelete(_: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!bun.jsc.JSValue {
        if (comptime !Environment.isWindows) return jsc.JSValue.js_undefined;
        const w = std.os.windows;

        let buf: bun.WPathBuffer = undefined;
        const tmpdir_path = fs.FileSystem.RealFS.getDefaultTempDir();
        const path = switch (bun.sys.normalizePathWindows(u8, bun.invalid_fd, tmpdir_path, &buf, {})) {
            .err => return jsc.JSValue.js_undefined,
            .result => |norm| norm,
        };

        const path_len_bytes: u16 = @truncate(path.len * 2);
        let nt_name = std.os.windows.UNICODE_STRING{
            Length: path_len_bytes,
            MaximumLength: path_len_bytes,
            Buffer: @constCast(path.ptr),
        };

        let attr = std.os.windows.OBJECT_ATTRIBUTES{
            Length: @sizeOf(std.os.windows.OBJECT_ATTRIBUTES),
            RootDirectory: null,
            Attributes: 0,
            ObjectName: &nt_name,
            SecurityDescriptor: null,
            SecurityQualityOfService: null,
        };

        const flags: u32 = w.STANDARD_RIGHTS_READ | w.FILE_READ_ATTRIBUTES | w.FILE_READ_EA | w.SYNCHRONIZE | w.FILE_TRAVERSE;

        let fd: std.os.windows.HANDLE = std.os.windows.INVALID_HANDLE_VALUE;
        let io: std.os.windows.IO_STATUS_BLOCK = undefined;

        const rc = std.os.windows.ntdll.NtCreateFile(
            &fd,
            flags,
            &attr,
            &io,
            null,
            0,
            w.FILE_SHARE_READ | w.FILE_SHARE_WRITE,
            w.FILE_OPEN,
            w.FILE_DIRECTORY_FILE | w.FILE_SYNCHRONOUS_IO_NONALERT | w.FILE_OPEN_FOR_BACKUP_INTENT,
            null,
            0,
        );

        switch (bun.windows.Win32Error.fromNTStatus(rc)) {
            .SUCCESS => tempdir_fd = .fromNative(fd),
            else => {},
        }

        return jsc.JSValue.js_undefined;
    }

    static fn jsCloseTempDirHandle(_: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!JSValue {
        if (comptime !Environment.isWindows) return jsc.JSValue.js_undefined;

        if (fd := tempdir_fd) {
            fd.close();
        }

        return jsc.JSValue.js_undefined;
    }
}

pub fn @"export"() {
    * = &upgrade_js_bindings;
    Version.@"export"();
}

type string = []const u8;
type stringZ = [:0]const u8;

import DotEnv from "../dotenv/env_loader.zig";
import fs from "../resolver/fs.zig";
import linker from "../bundler/linker.zig";
const Archive = @import("../libarchive/libarchive.zig").Archive;
const Command = @import("./cli.zig").Command;
const URL = @import("../url/url.zig").URL;
const which = @import("../which/which.zig").which;

import bun from "bun";
const Environment = bun.Environment;
const Global = bun.Global;
const JSON = bun.json;
const MutableString = bun.MutableString;
const Output = bun.Output;
const Progress = bun.Progress;
const default_allocator = bun.default_allocator;
const js_ast = bun.ast;
const logger = bun.logger;
const strings = bun.strings;

const HTTP = bun.http;
const Headers = bun.http.Headers;
