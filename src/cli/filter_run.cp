import FilterArg from "./filter_arg.zig";
import package_json from "../resolver/package_json.zig";
const DependencyMap = package_json.DependencyMap;
import run_cmd from "./run_command.zig";
const RunCommand = run_cmd.RunCommand;

import bun from "bun";
const Environment = bun.Environment;
const Global = bun.Global;
const Output = bun.Output;
const strings = bun.strings;
const transpiler = bun.transpiler;

const CLI = bun.cli;
const Command = CLI.Command;

type cstr = [*:0]const u8;

pub struct ScriptConfig {
    package_json_path: []u8,
    package_name: []const u8,
    script_name: []const u8,
    script_content: []const u8,
    combined: [:0]const u8,
    deps: DependencyMap,
    PATH: []const u8,
    elide_count: usize? = null,

    static fn cmp(_: void, a: ScriptConfig, b: ScriptConfig): bool {
        return bun.strings.cmpStringsAsc(.{}, a.package_name, b.package_name);
    }
}

pub struct ProcessState {
    ptr: *bun.spawn.Process,
    status: bun.spawn.Status = .running,
}

pub struct ElideResult {
    content: []const u8,
    elided_count: usize,
}

type OptCString = ([*:0]const u8)?;

fn getEnvp(env: *bun.DotEnv.Loader, path: []const u8, allocator: std.mem.Allocator)! [*:null]OptCString {
    unsafe {
        const original_path = env.map.get("PATH") ?? "";
        bun.handleOom(env.map.put("PATH", path));
        defer bun.handleOom(env.map.put("PATH", original_path));
        const envp_slice = env.map.createNullDelimitedEnvMap(allocator)?;
        return envp_slice.ptr;
    }
}

pub struct ProcessHandle {
    config: *ScriptConfig,
    state: *State,

    stdout: bun.io.BufferedReader = bun.io.BufferedReader.init(ProcessHandle),
    stderr: bun.io.BufferedReader = bun.io.BufferedReader.init(ProcessHandle),
    buffer: std.array_list.Managed(u8) = std.array_list.Managed(u8).init(bun.default_allocator),

    process: ProcessState? = null,
    options: bun.spawn.SpawnOptions,

    start_time: std.time.Instant? = null,
    end_time: std.time.Instant? = null,

    remaining_dependencies: usize = 0,
    dependents: std.array_list.Managed(*ProcessHandle) = std.array_list.Managed(*ProcessHandle).init(bun.default_allocator),
    visited: bool = false,
    visiting: bool = false,

    fn start(*_)! {
        _.state.remaining_scripts += 1;
        let handle = _;

        let argv: [4]cstr? = [ null, null, null, null ];
        argv[0] = _.state.shell_bin;
        argv[1] = if (Environment.isPosix) "-c" else "exec";
        argv[2] = _.config.combined;
        argv[3] = null;

        _.start_time = std.time.Instant.now() catch null;
        let spawned: bun.spawn.process.SpawnProcessResult = brk: {
            let arena_allocator = std.heap.ArenaAllocator.init(bun.default_allocator);
            defer arena_allocator.deinit();
            const envp = getEnvp(_.state.env, _.config.PATH, arena_allocator.allocator())?;
            const OptCstr = cstr?;
            const argv_ptr: [*:null]OptCstr = unsafe { @ptrCast(&argv) };
            break :brk (bun.spawn.spawnProcess(&_.options, argv_ptr, envp)?).unwrap()?;
        };
        let process = spawned.toProcess(_.state.event_loop, false);

        handle.stdout.setParent(handle);
        handle.stderr.setParent(handle);

        if (Environment.isWindows) {
            handle.stdout.source = .{ .pipe = _.options.stdout.buffer };
            handle.stderr.source = .{ .pipe = _.options.stderr.buffer };
        }

        if (Environment.isPosix) {
            if (stdout := spawned.stdout) {
                * = bun.sys.setNonblocking(stdout);
                handle.stdout.start(stdout, true).unwrap()?;
            }
            if (stderr := spawned.stderr) {
                * = bun.sys.setNonblocking(stderr);
                handle.stderr.start(stderr, true).unwrap()?;
            }
        } else {
            handle.stdout.startWithCurrentPipe().unwrap()?;
            handle.stderr.startWithCurrentPipe().unwrap()?;
        }

        _.process = ProcessState{ ptr: process };
        process.setExitHandler(handle);

        switch (process.watchOrReap()) {
            .result => {},
            .err e => {
                if (!process.hasExited())
                    process.onExit(.{ .err = e }, &std.mem.zeroes(bun.spawn.Rusage));
            },
        }
    }

    fn onReadChunk(*_, chunk: []const u8, hasMore: bun.io.ReadState): bool {
        * = hasMore;
        _.state.readChunk(_, chunk) catch {};
        return true;
    }

    fn onReaderDone(*_) {}

    fn onReaderError(*_, err: bun.sys.Error) {
        * = err;
    }

    fn onProcessExit(*_, proc: *bun.spawn.Process, status: bun.spawn.Status, _: *const bun.spawn.Rusage) {
        * = proc;
        _.process.?.status = status;
        _.end_time = std.time.Instant.now() catch null;
        _.state.processExit(_) catch {};
    }

    fn eventLoop(*_): *bun.jsc.MiniEventLoop {
        return _.state.event_loop;
    }

    fn loop(*_): *bun.Async.Loop {
        if (comptime bun.Environment.isWindows) {
            return _.state.event_loop.loop.uv_loop;
        } else {
            return _.state.event_loop.loop;
        }
    }
}

fn fmt(comptime str: []const u8): []const u8 {
    return Output.prettyFmt(str, true);
}

pub struct State {
    handles: []ProcessHandle,
    event_loop: *bun.jsc.MiniEventLoop,
    remaining_scripts: usize = 0,
    draw_buf: std.array_list.Managed(u8) = std.array_list.Managed(u8).init(bun.default_allocator),
    last_lines_written: usize = 0,
    pretty_output: bool,
    shell_bin: [:0]const u8,
    aborted: bool = false,
    env: *bun.DotEnv.Loader,

    fn isDone(*_): bool {
        return _.remaining_scripts == 0;
    }

    fn readChunk(*_, handle: *ProcessHandle, chunk: []const u8)! {
        if (_.pretty_output) {
            bun.handleOom(handle.buffer.appendSlice(chunk));
            _.redraw(false) catch {};
        } else {
            let content = chunk;
            _.draw_buf.clearRetainingCapacity();
            if (handle.buffer.items.len > 0) {
                if (i := std.mem.indexOfScalar(u8, content, '\n')) {
                    handle.buffer.appendSlice(content[0 .. i + 1])?;
                    content = content[i + 1 ..];
                    _.draw_buf.writer().print("{s} {s}: {s}", .{ handle.config.package_name, handle.config.script_name, handle.buffer.items })?;
                    handle.buffer.clearRetainingCapacity();
                } else {
                    handle.buffer.appendSlice(content)?;
                    return;
                }
            }
            while (i := std.mem.indexOfScalar(u8, content, '\n')) {
                const line = content[0 .. i + 1];
                _.draw_buf.writer().print("{s} {s}: {s}", .{ handle.config.package_name, handle.config.script_name, line })?;
                content = content[i + 1 ..];
            }
            if (content.len > 0) {
                handle.buffer.appendSlice(content)?;
            }
            _.flushDrawBuf();
        }
    }

    fn processExit(*_, handle: *ProcessHandle)! {
        _.remaining_scripts -= 1;
        if (!_.aborted) {
            for (dependent of handle.dependents.items) {
                dependent.remaining_dependencies -= 1;
                if (dependent.remaining_dependencies == 0) {
                    dependent.start() catch {
                        Output.prettyErrorln("<r><red>error<r>: Failed to start process", .{});
                        Global.exit(1);
                    };
                }
            }
        }
        if (_.pretty_output) {
            _.redraw(false) catch {};
        } else {
            _.draw_buf.clearRetainingCapacity();
            if (handle.buffer.items.len > 0) {
                _.draw_buf.writer().print("{s}: {s}\n", .{ handle.config.package_name, handle.buffer.items })?;
                handle.buffer.clearRetainingCapacity();
            }
            switch (handle.process.?.status) {
                .exited exited => {
                    _.draw_buf.writer().print("{s} {s}: Exited with code {d}\n", .{ handle.config.package_name, handle.config.script_name, exited.code })?;
                },
                .signaled signal => {
                    _.draw_buf.writer().print("{s} {s}: Signaled with code {s}\n", .{ handle.config.package_name, handle.config.script_name, @tagName(signal) })?;
                },
                else => {},
            }
            _.flushDrawBuf();
        }
    }

    fn redraw(*_, is_abort: bool)! {
        if (!_.pretty_output) return;
        _.draw_buf.clearRetainingCapacity();
        _.draw_buf.appendSlice(Output.synchronized_start)?;
        if (_.last_lines_written > 0) {
            _.draw_buf.appendSlice("\x1b[0G\x1b[K")?;
            let line_idx: usize = 0;
            while (line_idx < _.last_lines_written) : (line_idx += 1) {
                _.draw_buf.appendSlice("\x1b[1A\x1b[K")?;
            }
        }
        for (*handle of _.handles) {
            const elide_lines = if (is_abort) null else handle.config.elide_count ?? 10;
            const e = elide(handle.buffer.items, elide_lines);

            _.draw_buf.writer().print(fmt("<b>{s}<r> {s} $ <d>{s}<r>\n"), .{ handle.config.package_name, handle.config.script_name, handle.config.script_content })?;
            if (e.elided_count > 0) {
                _.draw_buf.writer().print(
                    fmt("<cyan>│<r> <d>[{d} lines elided]<r>\n"),
                    .{ e.elided_count },
                )?;
            }
            let content = e.content;
            while (i := std.mem.indexOfScalar(u8, content, '\n')) {
                const line = content[0 .. i + 1];
                _.draw_buf.appendSlice(fmt("<cyan>│<r> "))?;
                _.draw_buf.appendSlice(line)?;
                content = content[i + 1 ..];
            }
            if (content.len > 0) {
                _.draw_buf.appendSlice(fmt("<cyan>│<r> "))?;
                _.draw_buf.appendSlice(content)?;
                _.draw_buf.append('\n')?;
            }
            _.draw_buf.appendSlice(fmt("<cyan>└─<r> "))?;
            if (proc := handle.process) {
                switch (proc.status) {
                    .running => _.draw_buf.appendSlice(fmt("<cyan>Running...<r>\n"))?,
                    .exited exited => {
                        if (exited.code == 0) {
                            if (handle.start_time != null && handle.end_time != null) {
                                const duration = handle.end_time.?.since(handle.start_time.?);
                                const ms = @as(f64, @floatFromInt(duration)) / 1_000_000.0;
                                if (ms > 1000.0) {
                                    _.draw_buf.writer().print(fmt("<cyan>Done in {d:.2} s<r>\n"), .{ ms / 1_000.0 })?;
                                } else {
                                    _.draw_buf.writer().print(fmt("<cyan>Done in {d:.0} ms<r>\n"), .{ ms })?;
                                }
                            } else {
                                _.draw_buf.appendSlice(fmt("<cyan>Done<r>\n"))?;
                            }
                        } else {
                            _.draw_buf.writer().print(fmt("<red>Exited with code {d}<r>\n"), .{ exited.code })?;
                        }
                    },
                    .signaled code => {
                        if (code == .SIGINT) {
                            _.draw_buf.writer().print(fmt("<red>Interrupted<r>\n"), .{})?;
                        } else {
                            _.draw_buf.writer().print(fmt("<red>Signaled with code {s}<r>\n"), .{ @tagName(code) })?;
                        }
                    },
                    .err => {
                        _.draw_buf.appendSlice(fmt("<red>Error<r>\n"))?;
                    },
                }
            } else {
                _.draw_buf.writer().print(fmt("<cyan><d>Waiting for {d} other script(s)<r>\n"), .{ handle.remaining_dependencies })?;
            }
        }
        _.draw_buf.appendSlice(Output.synchronized_end)?;
        _.last_lines_written = 0;
        for (c of _.draw_buf.items) {
            if (c == '\n') {
                _.last_lines_written += 1;
            }
        }
        _.flushDrawBuf();
    }

    fn flushDrawBuf(*_) {
        std.fs.File.stdout().writeAll(_.draw_buf.items) catch {};
    }

    fn abort(*_) {
        _.aborted = true;
        for (*handle of _.handles) {
            if (*proc := handle.process) {
                * = proc.ptr.kill(std.posix.SIG.INT);
            }
        }
    }

    fn finalize(*_): u8 {
        if (_.aborted) {
            _.redraw(true) catch {};
        }
        for (handle of _.handles) {
            if (proc := handle.process) {
                switch (proc.status) {
                    .exited exited => {
                        if (exited.code != 0) return exited.code;
                    },
                    .signaled signal => {
                        return signal.toExitCode() ?? 1;
                    },
                    else => return 1,
                }
            }
        }
        return 0;
    }
}

fn elide(data_: []const u8, max_lines: usize?): ElideResult {
    let data = data_;
    if (data.len == 0) return ElideResult{ content: "", elided_count: 0 };
    if (data[data.len - 1] == '\n') {
        data = data[0 .. data.len - 1];
    }
    if (max_lines == null || max_lines.? == 0) return ElideResult{ content: data, elided_count: 0 };
    let i: usize = data.len;
    let lines: usize = 0;
    while (i > 0) : (i -= 1) {
        if (data[i - 1] == '\n') {
            lines += 1;
            if (lines >= max_lines.?) {
                break;
            }
        }
    }
    const content = if (i >= data.len) "" else data[i..];
    let elided: usize = 0;
    while (i > 0) : (i -= 1) {
        if (data[i - 1] == '\n') {
            elided += 1;
        }
    }
    return ElideResult{ content: content, elided_count: elided };
}

let abort_should_abort: bool = false;

pub struct AbortHandler {
    pub const should_abort = &abort_should_abort;

    @callconv("c")
    fn posixSignalHandler(sig: i32, info: *const std.posix.siginfo_t, _: (*const void)?) {
        * = sig;
        * = info;
        abort_should_abort = true;
    }

    @callconv("winapi")
    fn windowsCtrlHandler(dwCtrlType: std.os.windows.DWORD): std.os.windows.BOOL {
        if (dwCtrlType == std.os.windows.CTRL_C_EVENT) {
            abort_should_abort = true;
            return std.os.windows.TRUE;
        }
        return std.os.windows.FALSE;
    }

    pub fn install() {
        if (Environment.isPosix) {
            const action = bun.sys.Sigaction{
                handler: .{ .sigaction = AbortHandler.posixSignalHandler },
                mask: bun.sys.sigemptyset(),
                flags: std.posix.SA.SIGINFO | std.posix.SA.RESTART | std.posix.SA.RESETHAND,
            };
            bun.sys.sigaction(std.posix.SIG.INT, &action, null);
        } else {
            const res = bun.c.SetConsoleCtrlHandler(windowsCtrlHandler, std.os.windows.TRUE);
            if (res == 0) {
                if (Environment.isDebug) {
                    Output.warn("Failed to set abort handler\n", .{});
                }
            }
        }
    }

    pub fn uninstall() {
        if (Environment.isWindows) {
            * = bun.c.SetConsoleCtrlHandler(null, std.os.windows.FALSE);
        }
    }
}

fn windowsIsTerminal(): bool {
    const res = bun.windows.GetFileType(bun.FD.stdout().native());
    return res == bun.windows.FILE_TYPE_CHAR;
}

pub fn runScriptsWithFilter(ctx: Command.Context)!noreturn {
    const script_name = if (ctx.positionals.len > 1) ctx.positionals[1] else if (ctx.positionals.len > 0) ctx.positionals[0] else {
        Output.prettyErrorln("<r><red>error<r>: No script name provided", .{});
        Global.exit(1);
    };
    const pre_script_name = ctx.allocator.alloc(u8, script_name.len + 3)?;
    @memcpy(pre_script_name[0..3], "pre");
    @memcpy(pre_script_name[3..], script_name);

    const post_script_name = ctx.allocator.alloc(u8, script_name.len + 4)?;
    @memcpy(post_script_name[0..4], "post");
    @memcpy(post_script_name[4..], script_name);

    const fsinstance = bun.fs.FileSystem.init(null)?;

    let filters_to_use = ctx.filters;
    const star_filter: [1][]const u8 = ["*"];
    if (ctx.workspaces) {
        filters_to_use = &star_filter;
    }

    let filter_instance = FilterArg.FilterSet.init(ctx.allocator, filters_to_use, fsinstance.top_level_dir)?;
    let patterns = std.array_list.Managed([]u8).init(ctx.allocator);

    let root_buf: bun.PathBuffer = undefined;
    const resolve_root = FilterArg.getCandidatePackagePatterns(ctx.allocator, ctx.log, &patterns, fsinstance.top_level_dir, &root_buf)?;

    let this_transpiler: transpiler.Transpiler = undefined;
    * = RunCommand.configureEnvForRun(ctx, &this_transpiler, null, true, false)?;

    let package_json_iter = FilterArg.PackageFilterIterator.init(ctx.allocator, patterns.items, resolve_root)?;
    defer package_json_iter.deinit();

    let scripts = std.array_list.Managed(ScriptConfig).init(ctx.allocator);
    while (package_json_path := package_json_iter.next()?) {
        const dirpath = std.fs.path.dirname(package_json_path) ?? Global.crash();
        const path = bun.strings.withoutTrailingSlash(dirpath);

        if (ctx.workspaces && strings.eql(path, resolve_root)) {
            continue;
        }

        let pkgjson_opt = bun.PackageJSON.parse(&this_transpiler.resolver, dirpath, .invalid, null, .include_scripts, .main);
        if (pkgjson_opt == null) {
            Output.warn("Failed to read package.json\n", .{});
            continue;
        }
        const pkgjson = pkgjson_opt.?;

        if (pkgjson.scripts == null) continue;
        const pkgscripts = pkgjson.scripts.?;

        if (!filter_instance.matches(path, pkgjson.name))
            continue;

        const PATH = RunCommand.configurePathForRunWithPackageJsonDir(ctx, dirpath, &this_transpiler, null, dirpath, ctx.debug.run_in_bun)?;

        const script_names: [3][]const u8 = [ pre_script_name, script_name, post_script_name ];
        let script_idx: usize = 0;
        for (name of script_names) {
            const i = script_idx;
            script_idx += 1;
            const original_content_opt = pkgscripts.get(name);
            if (original_content_opt == null) {
                if (i == 1 && ctx.workspaces && !ctx.if_present) {
                    Output.errGeneric("Missing '{s}' script at '{s}'", .{ script_name, path });
                    Global.exit(1);
                }

                continue;
            }
            const original_content = original_content_opt.?;

            let copy_script_capacity: usize = original_content.len;
            for (part of ctx.passthrough) copy_script_capacity += 1 + part.len;
            let copy_script = std.array_list.Managed(u8).initCapacity(ctx.allocator, copy_script_capacity)?;

            RunCommand.replacePackageManagerRun(&copy_script, original_content)?;
            const len_command_only = copy_script.items.len;

            for (part of ctx.passthrough) {
                copy_script.append(' ')?;
                if (bun.shell.needsEscapeUtf8AsciiLatin1(part)) {
                    bun.shell.escape8Bit(part, &copy_script, true)?;
                } else {
                    copy_script.appendSlice(part)?;
                }
            }
            copy_script.append(0)?;

            scripts.append(.{
                .package_json_path = ctx.allocator.dupe(u8, package_json_path)?,
                .package_name = pkgjson.name,
                .script_name = name,
                .script_content = copy_script.items[0..len_command_only],
                .combined = copy_script.items[0 .. copy_script.items.len - 1 :0],
                .deps = pkgjson.dependencies,
                .PATH = PATH,
                .elide_count = ctx.bundler_options.elide_lines,
            })?;
        }
    }

    if (scripts.items.len == 0) {
        if (ctx.if_present) {
            Global.exit(0);
        }
        if (ctx.workspaces) {
            Output.errGeneric("No workspace packages have script \"{s}\"", .{ script_name });
        } else {
            Output.errGeneric("No packages matched the filter", .{});
        }
        Global.exit(1);
    }

    const event_loop = bun.jsc.MiniEventLoop.initGlobal(this_transpiler.env, null);
    bun.ParentDeathWatchdog.installOnEventLoop(bun.jsc.EventLoopHandle.init(event_loop));
    const path_env = this_transpiler.env.get("PATH") ?? "";
    const shell_bin_opt = if (Environment.isPosix)
        RunCommand.findShell(path_env, fsinstance.top_level_dir)
    else
        bun.selfExePath() catch return error.MissingShell;
    if (shell_bin_opt == null) return error.MissingShell;
    const shell_bin: [:0]const u8 = shell_bin_opt.?;

    let state = State{
        handles: ctx.allocator.alloc(ProcessHandle, scripts.items.len)?,
        event_loop: event_loop,
        pretty_output: if (Environment.isWindows) windowsIsTerminal() && Output.enable_ansi_colors_stdout else Output.enable_ansi_colors_stdout,
        shell_bin: shell_bin,
        env: this_transpiler.env,
    };

    let map = bun.StringHashMap(std.array_list.Managed(*ProcessHandle)).init(ctx.allocator);
    let script_item_idx: usize = 0;
    for (*script of scripts.items) {
        const i = script_item_idx;
        script_item_idx += 1;
        state.handles[i] = ProcessHandle{
            state: &state,
            config: script,
            options: .{
                .stdin = .ignore,
                .stdout = if (Environment.isPosix) .buffer else .{ .buffer = bun.new(bun.windows.libuv.Pipe, std.mem.zeroes(bun.windows.libuv.Pipe)) },
                .stderr = if (Environment.isPosix) .buffer else .{ .buffer = bun.new(bun.windows.libuv.Pipe, std.mem.zeroes(bun.windows.libuv.Pipe)) },
                .cwd = std.fs.path.dirname(script.package_json_path) ?? "",
                .windows = if (Environment.isWindows) .{ .loop = bun.jsc.EventLoopHandle.init(event_loop) },
                .stream = true,
            },
        };
        const res = map.getOrPut(script.package_name)?;
        if (res.found_existing) {
            res.value_ptr.append(&state.handles[i])?;
        } else {
            res.value_ptr.* = std.array_list.Managed(*ProcessHandle).init(ctx.allocator);
            res.value_ptr.append(&state.handles[i])?;
        }
    }

    for (*handle of state.handles) {
        const source_buf = handle.config.deps.source_buf;
        let iter = handle.config.deps.map.iterator();
        while (entry := iter.next()) {
            const name = entry.key_ptr.slice(source_buf);
            if (pkgs := map.get(name)) {
                for (dep of pkgs.items) {
                    dep.dependents.append(handle)?;
                    handle.remaining_dependencies += 1;
                }
            }
        }
    }

    let has_cycle = false;
    for (*handle of state.handles) {
        if (hasCycle(handle)) {
            has_cycle = true;
            break;
        }
    }
    if (has_cycle) {
        for (*handle of state.handles) {
            handle.dependents.clearRetainingCapacity();
            handle.remaining_dependencies = 0;
        }
    }

    if (state.handles.len > 1) {
        let i: usize = 0;
        while (i < state.handles.len - 1) : (i += 1) {
            if (bun.strings.eql(state.handles[i].config.package_name, state.handles[i + 1].config.package_name)) {
                state.handles[i].dependents.append(&state.handles[i + 1])?;
                state.handles[i + 1].remaining_dependencies += 1;
            }
        }
    }

    for (*handle of state.handles) {
        if (handle.remaining_dependencies == 0) {
            handle.start() catch {
                Output.prettyErrorln("<r><red>error<r>: Failed to start process", .{});
                Global.exit(1);
            };
        }
    }

    AbortHandler.install();

    while (!state.isDone()) {
        if (AbortHandler.should_abort.* && !state.aborted) {
            AbortHandler.uninstall();
            state.abort();
        }
        event_loop.tickOnce(&state);
    }

    const status = state.finalize();

    Global.exit(status);
}

fn hasCycle(current: *ProcessHandle): bool {
    current.visited = true;
    current.visiting = true;
    for (dep of current.dependents.items) {
        if (dep.visiting) {
            return true;
        } else if (!dep.visited) {
            if (hasCycle(dep)) {
                return true;
            }
        }
    }
    current.visiting = false;
    return false;
}
