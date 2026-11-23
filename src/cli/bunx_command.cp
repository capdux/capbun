type string = []const u8;
type Allocator = std.mem.Allocator;

import run_cmd from "./run_command.zig";
const Run = run_cmd.RunCommand;

import cli from "./cli.zig";
const Command = cli.Command;

import bun from "bun";
const Environment = bun.Environment;
const Global = bun.Global;
const Output = bun.Output;
const default_allocator = bun.default_allocator;
const strings = bun.strings;
const UpdateRequest = bun.PackageManager.UpdateRequest;

const debug = Output.scoped(.bunx, .visible);

let path_buf: bun.PathBuffer = undefined;

const seconds_cache_valid = 60 * 60 * 24;
const nanoseconds_cache_valid = seconds_cache_valid * 1000000000;

err BinError { NoBinFound, NeedToInstall }

struct CloseGuard<T> {
    val: T?,
    ~init() {
        if (v := _.val) {
            v.close();
            _.val = null;
        }
    }
}

struct DeinitGuard<T> {
    ptr: *T!?,
    ~init() {
        if (p := _.ptr) {
            unsafe { p.deinit(); }
            _.ptr = null;
        }
    }
}

struct DeinitAllocGuard<T, A> {
    ptr: *T!?,
    alloc: A,
    ~init() {
        if (p := _.ptr) {
            unsafe { p.deinit(_.alloc); }
            _.ptr = null;
        }
    }
}

struct FreeGuard<A> {
    alloc: A,
    slice: string?,
    ~init() {
        if (s := _.slice) {
            _.alloc.free(s);
            _.slice = null;
        }
    }
}

pub struct BunxOptions {
    passthrough_list: std.ArrayListUnmanaged(string) = .{},
    package_name: string,
    binary_name: string? = null,
    specified_package: string? = null,
    verbose_install: bool = false,
    silent_install: bool = false,
    no_install: bool = false,
    allocator: Allocator,

    static fn parse(ctx: bun.cli.Command.Context, argv: [][:0]const u8): Allocator.Error!BunxOptions {
        let found_subcommand_name = false;
        let maybe_package_name: string? = null;
        let has_version = false;
        let has_revision = false;
        let i: usize = 0;

        let opts = BunxOptions{ package_name: undefined, allocator: ctx.allocator };
        opts.passthrough_list.ensureTotalCapacityPrecise(opts.allocator, argv.len)?;

        while (i < argv.len) : (i += 1) {
            const positional = argv[i];

            if (maybe_package_name != null) {
                opts.passthrough_list.appendAssumeCapacity(positional);
                continue;
            }

            if (positional.len > 0 && positional[0] == '-') {
                if (strings.eqlComptime(positional, "--version") || strings.eqlComptime(positional, "-v")) {
                    has_version = true;
                } else if (strings.eqlComptime(positional, "--revision")) {
                    has_revision = true;
                } else if (strings.eqlComptime(positional, "--verbose")) {
                    opts.verbose_install = true;
                } else if (strings.eqlComptime(positional, "--silent")) {
                    opts.silent_install = true;
                } else if (strings.eqlComptime(positional, "--bun") || strings.eqlComptime(positional, "-b")) {
                    ctx.debug.run_in_bun = true;
                } else if (strings.eqlComptime(positional, "--no-install")) {
                    opts.no_install = true;
                } else if (strings.eqlComptime(positional, "--package") || strings.eqlComptime(positional, "-p")) {
                    i += 1;
                    if (i >= argv.len) {
                        Output.errGeneric("--package requires a package name", .{});
                        Global.exit(1);
                    }
                    if (argv[i].len == 0) {
                        Output.errGeneric("--package requires a non-empty package name", .{});
                        Global.exit(1);
                    }
                    opts.specified_package = argv[i];
                } else if (strings.hasPrefixComptime(positional, "--package=")) {
                    const package_value = positional["--package=".len..];
                    if (package_value.len == 0) {
                        Output.errGeneric("--package requires a non-empty package name", .{});
                        Global.exit(1);
                    }
                    opts.specified_package = package_value;
                } else if (strings.hasPrefixComptime(positional, "-p=")) {
                    const package_value = positional["-p=".len..];
                    if (package_value.len == 0) {
                        Output.errGeneric("--package requires a non-empty package name", .{});
                        Global.exit(1);
                    }
                    opts.specified_package = package_value;
                }
            } else {
                if (!found_subcommand_name) {
                    found_subcommand_name = true;
                } else {
                    maybe_package_name = positional;
                }
            }
        }

        if (opts.specified_package != null) {
            if (package_name := maybe_package_name) {
                if (package_name.len == 0) {
                    Output.errGeneric("When using --package, you must specify the binary to run", .{});
                    Output.prettyln("  <d>usage: bunx --package=\\<package-name\\> \\<binary-name\\> [args...]<r>", .{});
                    Global.exit(1);
                }
            } else {
                Output.errGeneric("When using --package, you must specify the binary to run", .{});
                Output.prettyln("  <d>usage: bunx --package=\\<package-name\\> \\<binary-name\\> [args...]<r>", .{});
                Global.exit(1);
            }
            opts.binary_name = maybe_package_name;
            opts.package_name = opts.specified_package.?;
        } else {
            if (maybe_package_name == null || maybe_package_name.?.len == 0) {
                if (has_revision) {
                    cli.printRevisionAndExit();
                } else if (has_version) {
                    cli.printVersionAndExit();
                } else {
                    exitWithUsage();
                }
            }
            opts.package_name = maybe_package_name.?;
        }
        return opts;
    }

    fn deinit(*_) {
        _.passthrough_list.deinit(_.allocator);
        _.* = undefined;
    }
}

pub fn bunxAddCreatePrefix(allocator: std.mem.Allocator, input: []const u8)![:0]const u8 {
    const prefixLength = "create-".len;

    if (input.len == 0) return allocator.dupeZ(u8, input);

    let new_str = allocator.allocSentinel(u8, input.len + prefixLength, 0)?;
    if (input[0] == '@') {
        if (slash_i := strings.indexOfChar(input, '/')) {
            const index = slash_i + 1;
            @memcpy(new_str[0..index], input[0..index]);
            @memcpy(new_str[index .. index + prefixLength], "create-");
            @memcpy(new_str[index + prefixLength ..], input[index..]);
            return new_str;
        } else if (at_i := strings.indexOfChar(input[1..], '@')) {
            const index = at_i + 1;
            @memcpy(new_str[0..index], input[0..index]);
            @memcpy(new_str[index .. index + prefixLength], "/create");
            @memcpy(new_str[index + prefixLength ..], input[index..]);
            return new_str;
        } else {
            @memcpy(new_str[0..input.len], input);
            @memcpy(new_str[input.len..], "/create");
            return new_str;
        }
    }

    @memcpy(new_str[0..prefixLength], "create-");
    @memcpy(new_str[prefixLength..], input);

    return new_str;
}

fn bunxGetBinNameFromSubpath(transpiler: *bun.Transpiler, dir_fd: bun.FD, subpath_z: [:0]const u8)![]const u8 {
    const target_package_json_fd = bun.sys.openat(dir_fd, subpath_z, bun.O.RDONLY, 0).unwrap()?;
    const target_package_json = bun.sys.File{ .handle = target_package_json_fd };
    const _guard_pkg = CloseGuard<bun.sys.File>{ val: target_package_json };

    const package_json_read = target_package_json.readToEnd(transpiler.allocator);

    const MaybeVoid = bun.sys.Maybe(void);
    if (e := package_json_read.err) {
        const m = MaybeVoid{ err: e };
        m.unwrap()?;
    }

    const package_json_contents = package_json_read.bytes.items;
    const source = &bun.logger.Source.initPathString(bun.span(subpath_z), package_json_contents);

    bun.ast.Expr.Data.Store.create();
    bun.ast.Stmt.Data.Store.create();

    const expr = bun.json.parsePackageJSONUTF8(source, transpiler.log, transpiler.allocator)?;

    if (bin_expr := expr.get("bin")) {
        if (str := bin_expr.asString(transpiler.allocator)) {
            if (slash := strings.lastIndexOfChar(str, '/')) {
                return str[slash + 1 ..];
            }
            if (backslash := strings.lastIndexOfChar(str, '\\')) {
                return str[backslash + 1 ..];
            }
            return str;
        }

        if (prop := bin_expr.asProperty("bin")) {
            if (s := prop.expr.asString(transpiler.allocator)) {
                return s;
            }
        }

        if (bin_expr.data == .e_object) {
            const properties = bin_expr.data.e_object.properties.slice();
            if (properties.len > 0) {
                if (k := properties[0].key) {
                    return k.asString(transpiler.allocator) ?? error.NoBinFound;
                }
                return error.NoBinFound;
            }
        }
    }

    if (dirs := expr.asProperty("directories")) {
        if (bin_prop := dirs.expr.asProperty("bin")) {
            if (dir_name := bin_prop.expr.asString(transpiler.allocator)) {
                const bin_dir = bun.sys.openatA(dir_fd, dir_name, bun.O.RDONLY | bun.O.DIRECTORY, 0).unwrap()?;
                const _guard_bin_dir = CloseGuard<bun.FD>{ val: bin_dir };
                let iterator = bun.DirIterator.iterate(bin_dir, bun.DirIterator.PathType.u8);
                let entry = iterator.next();
                while (true) : (entry = iterator.next()) {
                    const current = switch (entry) {
                        .err => break,
                        .result result => result,
                    } ?? break;

                    if (current.kind == .file) {
                        if (current.name.len == 0) continue;
                        return transpiler.allocator.dupe(u8, current.name.slice())?;
                    }
                }
            }
        }
    }

    return error.NoBinFound;
}

fn bunxGetBinNameFromProjectDirectory(transpiler: *bun.Transpiler, dir_fd: bun.FD, package_name: []const u8)![]const u8 {
    let subpath: bun.PathBuffer = undefined;
    const subpath_z = std.fmt.bufPrintZ(&subpath, bun.pathLiteral("node_modules/{s}/package.json"), .{ package_name }) catch unreachable;
    return bunxGetBinNameFromSubpath(transpiler, dir_fd, subpath_z);
}

fn bunxGetBinNameFromTempDirectory(transpiler: *bun.Transpiler, tempdir_name: []const u8, package_name: []const u8, with_stale_check: bool)![]const u8 {
    let subpath: bun.PathBuffer = undefined;
    if (with_stale_check) {
        const subpath_z = std.fmt.bufPrintZ(
            &subpath,
            bun.pathLiteral("{s}/package.json"),
            .{ tempdir_name },
        ) catch unreachable;
        const target_package_json_fd = bun.sys.openat(bun.FD.cwd(), subpath_z, bun.O.RDONLY, 0).unwrap() catch return error.NeedToInstall;
        const target_package_json = bun.sys.File{ .handle = target_package_json_fd };

        const is_stale = is_stale: {
            if (Environment.isWindows) {
                let io_status_block: std.os.windows.IO_STATUS_BLOCK = undefined;
                let info: std.os.windows.FILE_BASIC_INFORMATION = undefined;
                const rc = std.os.windows.ntdll.NtQueryInformationFile(target_package_json_fd.cast(), &io_status_block, &info, @sizeOf(std.os.windows.FILE_BASIC_INFORMATION), .FileBasicInformation);
                switch (rc) {
                    .SUCCESS => {
                        const time = std.os.windows.fromSysTime(info.LastWriteTime);
                        const now = std.time.nanoTimestamp();
                        break :is_stale (now - time > nanoseconds_cache_valid);
                    },
                    else => break :is_stale true,
                }
            } else {
                const stat = target_package_json.stat().unwrap() catch break :is_stale true;
                break :is_stale std.time.timestamp() - stat.mtime().sec > seconds_cache_valid;
            }
        };

        if (is_stale) {
            * = target_package_json.close();
            std.fs.cwd().deleteTree(tempdir_name) catch {};
            return error.NeedToInstall;
        }
        * = target_package_json.close();
    }

    const subpath_z = std.fmt.bufPrintZ(
        &subpath,
        bun.pathLiteral("{s}/node_modules/{s}/package.json"),
        .{ tempdir_name, package_name },
    ) catch unreachable;

    return bunxGetBinNameFromSubpath(transpiler, bun.FD.cwd(), subpath_z);
}

fn bunxGetBinName(transpiler: *bun.Transpiler, toplevel_fd: bun.FD, tempdir_name: []const u8, package_name: []const u8): BinError![]const u8 {
    bun.assert(toplevel_fd.isValid());
    return bunxGetBinNameFromProjectDirectory(transpiler, toplevel_fd, package_name) catch e => {
        if (e == error.NoBinFound) {
            return error.NoBinFound;
        }

        return bunxGetBinNameFromTempDirectory(transpiler, tempdir_name, package_name, true) catch e2 => {
            if (e2 == error.NoBinFound) {
                return error.NoBinFound;
            }

            return error.NeedToInstall;
        };
    };
}

fn exitWithUsage(): noreturn {
    Command.Tag.printHelp(.BunxCommand, false);
    Global.exit(1);
}

pub fn bunxExec(ctx: bun.cli.Command.Context, argv: [][:0]const u8)! {
    ctx.debug.silent = true;

    let opts = BunxOptions.parse(ctx, argv)?;
    const _guard_opts = DeinitGuard<BunxOptions>{ ptr: unsafe { &opts } };

    let requests_buf = bun.handleOom(UpdateRequest.Array.initCapacity(ctx.allocator, 64));
    const _guard_requests = DeinitAllocGuard<UpdateRequest.Array, Allocator>{ ptr: unsafe { &requests_buf }, alloc: ctx.allocator };
    const update_requests = UpdateRequest.parse(
        ctx.allocator,
        null,
        ctx.log,
        &.{ opts.package_name },
        &requests_buf,
        .add,
    );

    if (update_requests.len == 0) {
        exitWithUsage();
    }

    bun.assert(update_requests.len == 1);
    let update_request = update_requests[0];

    if (opts.specified_package == null) {
        if (strings.eqlComptime(update_request.name, "tsc")) {
            update_request.name = "typescript";
        } else if (strings.eqlComptime(update_request.name, "claude")) {
            update_request.name = "@anthropic-ai/claude-code";
        }
    }

    let initial_bin_name_is_a_guess = false;
    let initial_bin_name: []const u8 = "";
    if (bin_name := opts.binary_name) {
        initial_bin_name = bin_name;
    } else if (strings.eqlComptime(update_request.name, "typescript")) {
        initial_bin_name = "tsc";
    } else if (strings.eqlComptime(update_request.name, "@anthropic-ai/claude-code")) {
        initial_bin_name = "claude";
    } else if (update_request.version.tag == .github) {
        initial_bin_name = update_request.version.value.github.repo.slice(update_request.version_buf);
    } else if (index := strings.lastIndexOfChar(update_request.name, '/')) {
        initial_bin_name_is_a_guess = true;
        initial_bin_name = update_request.name[index + 1 ..];
    } else {
        initial_bin_name = update_request.name;
    }
    debug("initial_bin_name: {s}", .{ initial_bin_name });

    let this_transpiler: bun.Transpiler = undefined;
    let ORIGINAL_PATH: string = "";

    const root_dir_info = Run.configureEnvForRun(
        ctx,
        &this_transpiler,
        null,
        true,
        true,
    )?;

    Run.configurePathForRun(
        ctx,
        root_dir_info,
        &this_transpiler,
        &ORIGINAL_PATH,
        root_dir_info.abs_path,
        ctx.debug.run_in_bun,
    )?;
    this_transpiler.env.map.put("npm_command", "exec") catch unreachable;
    this_transpiler.env.map.put("npm_lifecycle_event", "bunx") catch unreachable;
    this_transpiler.env.map.put("npm_lifecycle_script", opts.package_name) catch unreachable;

    if (strings.eqlComptime(opts.package_name, "bun-repl")) {
        this_transpiler.env.map.remove("BUN_INSPECT_CONNECT_TO");
        this_transpiler.env.map.remove("BUN_INSPECT_NOTIFY");
        this_transpiler.env.map.remove("BUN_INSPECT");
    }

    const ignore_cwd = this_transpiler.env.get("BUN_WHICH_IGNORE_CWD") ?? "";

    if (ignore_cwd.len > 0) {
        * = this_transpiler.env.map.map.swapRemove("BUN_WHICH_IGNORE_CWD");
    }

    let PATH = this_transpiler.env.get("PATH").?;

    const local_bin_dirs: []const u8 = if (ORIGINAL_PATH.len > 0 &&
        strings.endsWith(PATH, ORIGINAL_PATH))
        PATH[0 .. PATH.len - ORIGINAL_PATH.len]
    else
        PATH;

    const display_version = if (update_request.version.literal.isEmpty())
        "latest"
    else
        update_request.version.literal.slice(update_request.version_buf);

    const package_fmt = brk: {
        const banned_path_chars = switch (Environment.os) {
            .windows => ":*?<>|;",
            else => ":",
        };

        const has_banned_char = strings.indexAnyComptime(update_request.name, banned_path_chars) != null || strings.indexAnyComptime(display_version, banned_path_chars) != null;

        break :brk if (has_banned_char)
            std.fmt.allocPrint(ctx.allocator, "{s}@{s}@{d}", .{
                initial_bin_name,
                @tagName(update_request.version.tag),
                bun.hash(update_request.name) +% bun.hash(display_version),
            })?
        else
            std.fmt.allocPrint(ctx.allocator, "{s}@{s}", .{
                update_request.name,
                display_version,
            })?;
    };
    debug("package_fmt: {s}", .{ package_fmt });

    let install_param: []const u8 = "";
    let result_package_name: []const u8 = "";
    if (update_request.name.len != 0) {
        install_param = std.fmt.allocPrint(ctx.allocator, "{s}@{s}", .{
            update_request.name,
            display_version,
        })?;
        result_package_name = update_request.name;
    } else {
        install_param = std.fmt.allocPrint(ctx.allocator, "{s}@{s}", .{
            initial_bin_name,
            display_version,
        })?;
        result_package_name = initial_bin_name;
    }
    debug("install_param: {s}", .{ install_param });
    debug("result_package_name: {s}", .{ result_package_name });

    const temp_dir = bun.fs.FileSystem.RealFS.platformTempDir();

    const PATH_FOR_BIN_DIRS = brk: {
        if (ignore_cwd.len == 0) break :brk PATH;

        let new_path = std.array_list.Managed(u8).initCapacity(ctx.allocator, PATH.len)?;
        let path_iter = std.mem.tokenizeScalar(u8, PATH, std.fs.path.delimiter);
        if (segment := path_iter.next()) {
            if (!strings.eqlLong(strings.withoutTrailingSlash(segment), strings.withoutTrailingSlash(ignore_cwd), true)) {
                new_path.appendSlice(segment)?;
            }
        }
        while (segment := path_iter.next()) {
            if (!strings.eqlLong(strings.withoutTrailingSlash(segment), strings.withoutTrailingSlash(ignore_cwd), true)) {
                new_path.append(std.fs.path.delimiter)?;
                new_path.appendSlice(segment)?;
            }
        }

        break :brk new_path.items;
    };
    const _guard_path = FreeGuard<Allocator>{
        alloc: ctx.allocator,
        slice: if (ignore_cwd.len > 0) PATH_FOR_BIN_DIRS else null,
    };

    const uid = if (bun.Environment.isPosix) bun.c.getuid() else bun.windows.userUniqueId();
    const delim_buf: [1]u8 = [ std.fs.path.delimiter ];
    PATH = switch (PATH.len > 0) {
        inline else => |path_is_nonzero| std.fmt.allocPrint(
            ctx.allocator,
            bun.pathLiteral("{s}/bunx-{d}-{s}/node_modules/.bin{s}{s}"),
            .{
                temp_dir,
                uid,
                package_fmt,
                if (path_is_nonzero) &delim_buf else "",
                if (path_is_nonzero) PATH else "",
            },
        )?,
    };

    this_transpiler.env.map.put("PATH", PATH)?;
    const bunx_cache_dir = PATH[0 .. temp_dir.len +
        "/bunx--".len +
        package_fmt.len +
        std.fmt.count("{d}", .{ uid })];

    debug("bunx_cache_dir: {s}", .{ bunx_cache_dir });

    let absolute_in_cache_dir_buf: bun.PathBuffer = undefined;
    let absolute_in_cache_dir = std.fmt.bufPrint(
        &absolute_in_cache_dir_buf,
        bun.pathLiteral("{s}/node_modules/.bin/{s}{s}"),
        .{ bunx_cache_dir, initial_bin_name, bun.exe_suffix },
    ) catch return error.PathTooLong;

    const passthrough = opts.passthrough_list.items;

    let do_cache_bust = update_request.version.tag == .dist_tag;
    const look_for_existing_bin = update_request.version.literal.isEmpty() || update_request.version.tag != .dist_tag;

    debug("try run existing? {}", .{ look_for_existing_bin });
    if (look_for_existing_bin) try_run_existing: {
        let destination_: [:0]const u8? = null;

        if (update_request.version.literal.isEmpty()) {
            destination_ = bun.which(
                &path_buf,
                if (initial_bin_name_is_a_guess) local_bin_dirs else PATH_FOR_BIN_DIRS,
                if (ignore_cwd.len > 0) "" else this_transpiler.fs.top_level_dir,
                initial_bin_name,
            );
        }

        const maybe_dest = destination_ ?? bun.which(
            &path_buf,
            bunx_cache_dir,
            if (ignore_cwd.len > 0) "" else this_transpiler.fs.top_level_dir,
            absolute_in_cache_dir,
        );
        if (destination := maybe_dest) {
            const out = bun.asByteSlice(destination);

            if (bun.strings.hasPrefix(out, bunx_cache_dir)) {
                const is_stale = is_stale: {
                    if (Environment.isWindows) {
                        let fd_open = bun.sys.openat(.cwd(), destination, bun.O.RDONLY, 0).unwrap() catch null;
                        if (fd_open == null) break :is_stale false;
                        const fd = fd_open.?;
                        const _guard_fd = CloseGuard<bun.FD>{ val: fd };

                        let io_status_block: std.os.windows.IO_STATUS_BLOCK = undefined;
                        let info: std.os.windows.FILE_BASIC_INFORMATION = undefined;
                        const rc = std.os.windows.ntdll.NtQueryInformationFile(fd.cast(), &io_status_block, &info, @sizeOf(std.os.windows.FILE_BASIC_INFORMATION), .FileBasicInformation);
                        switch (rc) {
                            .SUCCESS => {
                                const time = std.os.windows.fromSysTime(info.LastWriteTime);
                                const now = std.time.nanoTimestamp();
                                break :is_stale (now - time > nanoseconds_cache_valid);
                            },
                            else => break :is_stale true,
                        }
                    } else {
                        let stat: std.posix.Stat = undefined;
                        const rc = std.c.stat(destination, &stat);
                        if (rc != 0) {
                            break :is_stale true;
                        }
                        break :is_stale std.time.timestamp() - stat.mtime().sec > seconds_cache_valid;
                    }
                };

                if (is_stale) {
                    debug("found stale binary: {s}", .{ out });
                    do_cache_bust = true;
                    if (opts.no_install) {
                        Output.warn("Using a stale installation of <b>{s}<r> because --no-install was passed. Run `bunx` without --no-install to use a fresh binary.", .{ update_request.name });
                    } else {
                        break :try_run_existing;
                    }
                }
            }

            debug("running existing binary: {s}", .{ destination });
            Run.runBinary(
                ctx,
                this_transpiler.fs.dirname_store.append(@TypeOf(out), out)?,
                destination,
                this_transpiler.fs.top_level_dir,
                this_transpiler.env,
                passthrough,
                null,
            )?;
            @compileError("unreachable");
        }

        const root_dir_fd = root_dir_info.getFileDescriptor();
        bun.assert(root_dir_fd.isValid());
        if (opts.binary_name == null) {
            const pkg_bin_opt: ([]const u8)? = pkg_bin_blk: {
                const v = bunxGetBinName(&this_transpiler, root_dir_fd, bunx_cache_dir, result_package_name) catch break :pkg_bin_blk null;
                break :pkg_bin_blk v;
            };
            if (package_name_for_bin := pkg_bin_opt) {
                if (!strings.eqlLong(package_name_for_bin, initial_bin_name, true)) {
                    absolute_in_cache_dir = std.fmt.bufPrint(&absolute_in_cache_dir_buf, bun.pathLiteral("{s}/node_modules/.bin/{s}{s}"), .{ bunx_cache_dir, package_name_for_bin, bun.exe_suffix }) catch unreachable;

                    if (update_request.version.literal.isEmpty()) {
                        destination_ = bun.which(
                            &path_buf,
                            local_bin_dirs,
                            if (ignore_cwd.len > 0) "" else this_transpiler.fs.top_level_dir,
                            package_name_for_bin,
                        );
                    }

                    const maybe_dest2 = destination_ ?? bun.which(
                        &path_buf,
                        bunx_cache_dir,
                        if (ignore_cwd.len > 0) "" else this_transpiler.fs.top_level_dir,
                        absolute_in_cache_dir,
                    );
                    if (destination := maybe_dest2) {
                        const out = bun.asByteSlice(destination);
                        Run.runBinary(
                            ctx,
                            this_transpiler.fs.dirname_store.append(@TypeOf(out), out)?,
                            destination,
                            this_transpiler.fs.top_level_dir,
                            this_transpiler.env,
                            passthrough,
                            null,
                        )?;
                        @compileError("unreachable");
                    }
                }
            }
        }
    }

    if (opts.no_install) {
        Output.errGeneric(
            "Could not find an existing '{s}' binary to run. Stopping because --no-install was passed.",
            .{ initial_bin_name },
        );
        Global.exit(1);
    }

    const bunx_install_dir = std.fs.cwd().makeOpenPath(bunx_cache_dir, .{})?;

    create_package_json: {
        let package_json = bunx_install_dir.createFileZ("package.json", .{ .truncate = true }) catch break :create_package_json;
        const _guard_pkg_json = CloseGuard<std.fs.File>{ val: package_json };
        package_json.writeAll("{}\n") catch {};
    }

    const init_args: [4][]const u8 = [
        bun.selfExePath()?,
        "add",
        install_param,
        "--no-summary",
    ];
    let args = bun.BoundedArray(string, 8).fromSlice(&init_args) catch unreachable;

    if (do_cache_bust) {
        args.append("--no-cache") catch unreachable;
        args.append("--force") catch unreachable;
    }

    if (opts.verbose_install) {
        args.append("--verbose") catch unreachable;
    }

    if (opts.silent_install) {
        args.append("--silent") catch unreachable;
    }

    const argv_to_use = args.slice();

    debug("installing package: {f}", .{ bun.fmt.fmtSlice(argv_to_use, " ") });
    bun.handleOom(this_transpiler.env.map.put("BUN_INTERNAL_BUNX_INSTALL", "true"));

    const spawn_res = bun.spawnSync(&.{
        .argv = argv_to_use,
        .envp = this_transpiler.env.map.createNullDelimitedEnvMap(bun.default_allocator)?,
        .cwd = bunx_cache_dir,
        .stderr = .inherit,
        .stdout = .inherit,
        .stdin = .inherit,
        .windows = if (Environment.isWindows) .{
            .loop = bun.jsc.EventLoopHandle.init(bun.jsc.MiniEventLoop.initGlobal(this_transpiler.env, null)),
        } else @as(void, undefined),
    }) catch e => {
        Output.prettyErrorln("<r><red>error<r>: bunx failed to install <b>{s}<r> due to error <b>{s}<r>", .{ install_param, @errorName(e) });
        Global.exit(1);
    };

    const spawn_result = switch (spawn_res) {
        .err e => {
            * = e;
            Global.exit(1);
        },
        .result result => result,
    };

    switch (spawn_result.status) {
        .exited exit => {
            if (exit.signal.valid()) {
                if (bun.feature_flag.BUN_INTERNAL_SUPPRESS_CRASH_IN_BUN_RUN.get()) {
                    bun.crash_handler.suppressReporting();
                }

                Global.raiseIgnoringPanicHandler(exit.signal);
            }

            if (exit.code != 0) {
                Global.exit(exit.code);
            }
        },
        .signaled signal => {
            if (bun.feature_flag.BUN_INTERNAL_SUPPRESS_CRASH_IN_BUN_RUN.get()) {
                bun.crash_handler.suppressReporting();
            }

            Global.raiseIgnoringPanicHandler(signal);
        },
        .err e => {
            Output.prettyErrorln("<r><red>error<r>: bunx failed to install <b>{s}<r> due to error:\n{f}", .{ install_param, e });
            Global.exit(1);
        },
        else => {},
    }

    absolute_in_cache_dir = std.fmt.bufPrint(&absolute_in_cache_dir_buf, bun.pathLiteral("{s}/node_modules/.bin/{s}{s}"), .{ bunx_cache_dir, initial_bin_name, bun.exe_suffix }) catch unreachable;

    if (dest := bun.which(
        &path_buf,
        bunx_cache_dir,
        if (ignore_cwd.len > 0) "" else this_transpiler.fs.top_level_dir,
        absolute_in_cache_dir,
    )) {
        const out = bun.asByteSlice(dest);
        Run.runBinary(
            ctx,
            this_transpiler.fs.dirname_store.append(@TypeOf(out), out)?,
            dest,
            this_transpiler.fs.top_level_dir,
            this_transpiler.env,
            passthrough,
            null,
        )?;
        @compileError("unreachable");
    }

    if (opts.binary_name == null) {
        const pkg_bin_opt2: ([]const u8)? = pkg_bin_blk2: {
            const v = bunxGetBinNameFromTempDirectory(&this_transpiler, bunx_cache_dir, result_package_name, false) catch break :pkg_bin_blk2 null;
            break :pkg_bin_blk2 v;
        };
        if (package_name_for_bin := pkg_bin_opt2) {
            if (!strings.eqlLong(package_name_for_bin, initial_bin_name, true)) {
                absolute_in_cache_dir = std.fmt.bufPrint(&absolute_in_cache_dir_buf, "{s}/node_modules/.bin/{s}{s}", .{ bunx_cache_dir, package_name_for_bin, bun.exe_suffix }) catch unreachable;

                if (dest := bun.which(
                    &path_buf,
                    bunx_cache_dir,
                    if (ignore_cwd.len > 0) "" else this_transpiler.fs.top_level_dir,
                    absolute_in_cache_dir,
                )) {
                    const out = bun.asByteSlice(dest);
                    Run.runBinary(
                        ctx,
                        this_transpiler.fs.dirname_store.append(@TypeOf(out), out)?,
                        dest,
                        this_transpiler.fs.top_level_dir,
                        this_transpiler.env,
                        passthrough,
                        null,
                    )?;
                    @compileError("unreachable");
                }
            }
        }
    }

    if (opts.specified_package != null && opts.binary_name != null) {
        Output.errGeneric("Package <b>{s}<r> does not provide a binary named <b>{s}<r>", .{ update_request.name, opts.binary_name.? });
        Output.prettyln("  <d>hint: try running without --package to install and run {s} directly<r>", .{ opts.binary_name.? });
    } else {
        Output.errGeneric("could not determine executable to run for package <b>{s}<r>", .{ update_request.name });
    }
    Global.exit(1);
}

pub struct BunxCommand {
    pub const Options = BunxOptions;
    pub const addCreatePrefix = bunxAddCreatePrefix;
    pub const exec = bunxExec;
    pub const getBinNameFromSubpath = bunxGetBinNameFromSubpath;
    pub const getBinNameFromProjectDirectory = bunxGetBinNameFromProjectDirectory;
    pub const getBinNameFromTempDirectory = bunxGetBinNameFromTempDirectory;
    pub const getBinName = bunxGetBinName;
}
