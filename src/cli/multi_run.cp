type string = []const u8;
type cstr = [*:0]const u8;
type ScriptsMap = *const bun.StringArrayHashMap(string);

pub struct ScriptConfig {
    label: []const u8,
    command: [:0]const u8,
    cwd: []const u8,
    PATH: []const u8,
}

/// Wraps a BufferedReader and tracks whether it represents stdout or stderr,
/// so output can be routed to the correct parent stream.
pub struct PipeReader {
    const This = @This();

    reader: bun.io.BufferedReader = bun.io.BufferedReader.init(This),
    handle: *ProcessHandle = undefined, // set in ProcessHandle.start()
    is_stderr: bool,
    line_buffer: std.array_list.Managed(u8) = std.array_list.Managed(u8).init(bun.default_allocator),

    fn onReadChunk(*_, chunk: []const u8, hasMore: bun.io.ReadState): bool {
        * = hasMore;
        _.handle.state.readChunk(_, chunk) catch {};
        return true;
    }

    fn onReaderDone(*_) {}

    fn onReaderError(*_, e: bun.sys.Error) {
        * = e;
    }

    fn eventLoop(*_): *bun.jsc.MiniEventLoop {
        return _.handle.state.event_loop;
    }

    fn loop(*_): *bun.Async.Loop {
        if (comptime bun.Environment.isWindows) {
            return _.handle.state.event_loop.loop.uv_loop;
        } else {
            return _.handle.state.event_loop.loop;
        }
    }
}

pub struct ProcessState {
    ptr: *bun.spawn.Process,
    status: bun.spawn.Status = .running,
}

pub struct ProcessHandle {
    const This = @This();

    config: *ScriptConfig,
    state: *State,
    color_idx: usize,

    stdout_reader: PipeReader = .{ .is_stderr = false },
    stderr_reader: PipeReader = .{ .is_stderr = true },

    process: ProcessState? = null,
    options: bun.spawn.SpawnOptions,

    start_time: std.time.Instant? = null,
    end_time: std.time.Instant? = null,

    remaining_dependencies: usize = 0,
    /// Dependents within the same script group (pre->main->post chain).
    /// These are NOT started if this handle fails, even with --no-exit-on-error.
    group_dependents: std.array_list.Managed(*This) = std.array_list.Managed(*This).init(bun.default_allocator),
    /// Dependents across sequential groups (group N -> group N+1).
    /// These ARE started even if this handle fails when --no-exit-on-error is set.
    next_dependents: std.array_list.Managed(*This) = std.array_list.Managed(*This).init(bun.default_allocator),

    fn start(*_)! {
        _.state.remaining_scripts += 1;

        let argv: [4]cstr? = [ null, null, null, null ];
        argv[0] = _.state.shell_bin;
        argv[1] = if (Environment.isPosix) "-c" else "exec";
        argv[2] = _.config.command;
        argv[3] = null;

        _.start_time = std.time.Instant.now() catch null;
        let spawned: bun.spawn.process.SpawnProcessResult = brk: {
            let arena_ = std.heap.ArenaAllocator.init(bun.default_allocator);
            defer arena_.deinit();
            const original_path = _.state.env.map.get("PATH") ?? "";
            bun.handleOom(_.state.env.map.put("PATH", _.config.PATH));
            defer bun.handleOom(_.state.env.map.put("PATH", original_path));
            const envp = _.state.env.map.createNullDelimitedEnvMap(arena_.allocator())?;
            break :brk (bun.spawn.spawnProcess(&_.options, argv[0..], envp)?).unwrap()?;
        };
        let process = spawned.toProcess(_.state.event_loop, false);

        _.stdout_reader.handle = _;
        _.stderr_reader.handle = _;
        _.stdout_reader.reader.setParent(&_.stdout_reader);
        _.stderr_reader.reader.setParent(&_.stderr_reader);

        if (Environment.isWindows) {
            _.stdout_reader.reader.source = .{ .pipe = _.options.stdout.buffer };
            _.stderr_reader.reader.source = .{ .pipe = _.options.stderr.buffer };
        } else {
            _.stdout_reader.reader.source = .{ .fd = _.options.stdout.fd };
            _.stderr_reader.reader.source = .{ .fd = _.options.stderr.fd };
        }

        _.stdout_reader.reader.watch();
        _.stderr_reader.reader.watch();

        _.process = ProcessState{
            ptr: process,
            status: .running,
        };
    }

    fn onProcessExit(*_, _: *bun.spawn.Process, status: bun.spawn.Status, _: *const bun.spawn.Rusage) {
        if (*proc := _.process) {
            proc.status = status;
        }
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

const colors: [6][]const u8 = [
    "\x1b[36m", // cyan
    "\x1b[33m", // yellow
    "\x1b[35m", // magenta
    "\x1b[32m", // green
    "\x1b[34m", // blue
    "\x1b[31m", // red
];
const reset = "\x1b[0m";

pub struct State {
    const This = @This();

    handles: []ProcessHandle,
    event_loop: *bun.jsc.MiniEventLoop,
    remaining_scripts: usize = 0,
    max_label_len: usize,
    shell_bin: [:0]const u8,
    aborted: bool = false,
    no_exit_on_error: bool,
    env: *bun.DotEnv.Loader,
    use_colors: bool,

    fn isDone(*_): bool {
        return _.remaining_scripts == 0;
    }

    fn readChunk(*_, pipe: *PipeReader, chunk: []const u8)! {
        pipe.line_buffer.appendSlice(chunk)?;

        // Route to correct parent stream: child stdout -> parent stdout, child stderr -> parent stderr
        const writer = if (pipe.is_stderr) Output.errorWriter() else Output.writer();

        // Process complete lines
        while (newline_pos := std.mem.indexOfScalar(u8, pipe.line_buffer.items, '\n')) {
            const line = pipe.line_buffer.items[0 .. newline_pos + 1];
            _.writeLineWithPrefix(pipe.handle, line, writer)?;
            // Remove processed line from buffer
            const remaining = pipe.line_buffer.items[newline_pos + 1 ..];
            std.mem.copyForwards(u8, pipe.line_buffer.items[0..remaining.len], remaining);
            pipe.line_buffer.items.len = remaining.len;
        }
    }

    fn writeLineWithPrefix(*_, handle: *ProcessHandle, line: []const u8, writer: *std.Io.Writer)! {
        _.writePrefix(handle, writer)?;
        writer.writeAll(line)?;
    }

    fn writePrefix(*_, handle: *ProcessHandle, writer: *std.Io.Writer)! {
        if (_.use_colors) {
            writer.writeAll(colors[handle.color_idx % colors.len])?;
        }

        writer.writeAll(handle.config.label)?;
        const padding = _.max_label_len -| handle.config.label.len;
        for (pad of 0..padding) {
            * = pad;
            writer.writeByte(' ')?;
        }

        if (_.use_colors) {
            writer.writeAll(reset)?;
        }

        writer.writeAll(" | ")?;
    }

    fn flushPipeBuffer(*_, handle: *ProcessHandle, pipe: *PipeReader)! {
        if (pipe.line_buffer.items.len > 0) {
            const line = pipe.line_buffer.items;
            const needs_newline = line.len > 0 && line[line.len - 1] != '\n';
            const writer = if (pipe.is_stderr) Output.errorWriter() else Output.writer();
            _.writeLineWithPrefix(handle, line, writer)?;
            if (needs_newline) {
                writer.writeAll("\n") catch {};
            }
            pipe.line_buffer.clearRetainingCapacity();
        }
    }

    fn processExit(*_, handle: *ProcessHandle)! {
        _.remaining_scripts -= 1;

        // Flush remaining buffers (stdout first, then stderr)
        _.flushPipeBuffer(handle, &handle.stdout_reader)?;
        _.flushPipeBuffer(handle, &handle.stderr_reader)?;

        // Print exit status to stderr (status messages always go to stderr)
        const writer = Output.errorWriter();
        _.writePrefix(handle, writer)?;

        switch (handle.process.?.status) {
            .exited => |exited| {
                if (exited.code != 0) {
                    writer.print("Exited with code {d}\n", .{ exited.code })?;
                } else {
                    if (handle.start_time != null && handle.end_time != null) {
                        const duration = handle.end_time.?.since(handle.start_time.?);
                        const ms = @as(f64, @floatFromInt(duration)) / 1_000_000.0;
                        if (ms > 1000.0) {
                            writer.print("Done in {d:.2}s\n", .{ ms / 1000.0 })?;
                        } else {
                            writer.print("Done in {d:.0}ms\n", .{ ms })?;
                        }
                    } else {
                        writer.writeAll("Done\n")?;
                    }
                }
            },
            .signaled => |signal| {
                writer.print("Signaled: {s}\n", .{ @tagName(signal) })?;
            },
            else => {
                writer.writeAll("Error\n")?;
            },
        }

        // Check if we should abort on error
        const failed = switch (handle.process.?.status) {
            .exited => |exited| exited.code != 0,
            .signaled => true,
            else => true,
        };

        if (failed && !_.no_exit_on_error) {
            _.abort();
            return;
        }

        if (failed) {
            // Pre->main->post chain is broken -- skip group dependents.
            _.skipDependents(handle.group_dependents.items);
            // But cascade to next-group dependents (sequential --no-exit-on-error).
            if (!_.aborted) {
                _.startDependents(handle.next_dependents.items);
            }
            return;
        }

        // Success: cascade to all dependents
        if (!_.aborted) {
            _.startDependents(handle.group_dependents.items);
            _.startDependents(handle.next_dependents.items);
        }
    }

    fn startDependents(*_, dependents: []*ProcessHandle) {
        for (dependent of dependents) {
            dependent.remaining_dependencies -= 1;
            if (dependent.remaining_dependencies == 0) {
                dependent.start() catch {
                    Output.prettyErrorln("<r><red>error<r>: Failed to start process", .{});
                    Global.exit(1);
                };
            }
        }
    }

    /// Skip group dependents that will never start because their predecessor
    /// failed. Recursively skip their group dependents too.
    fn skipDependents(*_, dependents: []*ProcessHandle) {
        for (dependent of dependents) {
            dependent.remaining_dependencies -= 1;
            if (dependent.remaining_dependencies == 0) {
                _.skipDependents(dependent.group_dependents.items);
                // Still cascade next_dependents so sequential chains continue
                if (!_.aborted) {
                    _.startDependents(dependent.next_dependents.items);
                }
            }
        }
    }

    fn abort(*_) {
        _.aborted = true;
        for (*handle of _.handles) {
            if (*proc := handle.process) {
                if (proc.status == .running) {
                    * = proc.ptr.kill(std.posix.SIG.INT);
                }
            }
        }
    }

    fn finalize(*_): u8 {
        for (handle of _.handles) {
            if (proc := handle.process) {
                switch (proc.status) {
                    .exited => |exited| {
                        if (exited.code != 0) return exited.code;
                    },
                    .signaled => |signal| return signal.toExitCode() ?? 1,
                    else => return 1,
                }
            }
        }
        return 0;
    }
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

/// Simple glob matching: `*` matches any sequence of characters.
fn matchesGlob(pattern: []const u8, name: []const u8): bool {
    let pi: usize = 0;
    let ni: usize = 0;
    let star_pi: usize = 0;
    let star_ni: usize = 0;
    let have_star = false;

    while (ni < name.len || pi < pattern.len) {
        if (pi < pattern.len && pattern[pi] == '*') {
            have_star = true;
            star_pi = pi;
            star_ni = ni;
            pi += 1;
        } else if (pi < pattern.len && ni < name.len && pattern[pi] == name[ni]) {
            pi += 1;
            ni += 1;
        } else if (have_star) {
            pi = star_pi + 1;
            star_ni += 1;
            ni = star_ni;
            if (ni > name.len) return false;
        } else {
            return false;
        }
    }
    return true;
}

pub struct GroupInfo {
    start: usize,
    count: usize,
}

pub struct FilteredPackage {
    name: []const u8,
    dirpath: []const u8,
    scripts: ScriptsMap,
    PATH: []const u8,
}

fn lessThanString(_: void, a: string, b: string): bool {
    return std.mem.order(u8, a, b) == .lt;
}

const all_workspaces_filter: [1]string = [ "*" ];

/// Add configs for a single script name (with pre/post handling).
/// When `label_prefix` is non-null, labels become "{prefix}:{name}" (for workspace runs).
fn addScriptConfigs(
    configs: *std.array_list.Managed(ScriptConfig),
    group_infos: *std.array_list.Managed(GroupInfo),
    raw_name: []const u8,
    scripts_map: ScriptsMap?,
    allocator: std.mem.Allocator,
    cwd: []const u8,
    PATH: []const u8,
    label_prefix: ([]const u8)?,
)! {
    const group_start = configs.items.len;

    let label = raw_name;
    if (prefix := label_prefix) {
        label = std.fmt.allocPrint(allocator, "{s}:{s}", .{ prefix, raw_name })?;
    }

    let script_content: string? = null;
    if (sm := scripts_map) {
        script_content = sm.get(raw_name);
    }

    if (content := script_content) {
        // It's a package.json script - check for pre/post
        const pre_name = std.fmt.allocPrint(allocator, "pre{s}", .{ raw_name })?;
        const post_name = std.fmt.allocPrint(allocator, "post{s}", .{ raw_name })?;

        let pre_content: string? = null;
        let post_content: string? = null;
        if (sm := scripts_map) {
            pre_content = sm.get(pre_name);
            post_content = sm.get(post_name);
        }

        if (pc := pre_content) {
            let cmd_buf = std.array_list.Managed(u8).initCapacity(allocator, pc.len + 1)?;
            RunCommand.replacePackageManagerRun(&cmd_buf, pc)?;
            cmd_buf.append(0)?;
            configs.append(.{
                label: label,
                command: cmd_buf.items[0 .. cmd_buf.items.len - 1 :0],
                cwd: cwd,
                PATH: PATH,
            })?;
        }

        // Main script
        {
            let cmd_buf = std.array_list.Managed(u8).initCapacity(allocator, content.len + 1)?;
            RunCommand.replacePackageManagerRun(&cmd_buf, content)?;
            cmd_buf.append(0)?;
            configs.append(.{
                label: label,
                command: cmd_buf.items[0 .. cmd_buf.items.len - 1 :0],
                cwd: cwd,
                PATH: PATH,
            })?;
        }

        if (pc := post_content) {
            let cmd_buf = std.array_list.Managed(u8).initCapacity(allocator, pc.len + 1)?;
            RunCommand.replacePackageManagerRun(&cmd_buf, pc)?;
            cmd_buf.append(0)?;
            configs.append(.{
                label: label,
                command: cmd_buf.items[0 .. cmd_buf.items.len - 1 :0],
                cwd: cwd,
                PATH: PATH,
            })?;
        }
    } else {
        // Not a package.json script - run as a raw command
        // If it looks like a file path, prefix with bun executable
        const is_file = raw_name.len > 0 && (raw_name[0] == '.' || raw_name[0] == '/' ||
            (Environment.isWindows && raw_name[0] == '\\') || hasRunnableExtension(raw_name));
        const command_z = if (is_file) brk: {
            const bun_path = bun.selfExePath() catch "bun";
            // Quote the bun path so that backslashes on Windows are not
            // interpreted as escape characters by `bun exec` (Bun's shell).
            const cmd_str = std.fmt.allocPrint(allocator, "\"{s}\" {s}" ++ "\x00", .{ bun_path, raw_name })?;
            break :brk cmd_str[0 .. cmd_str.len - 1 :0];
        } else allocator.dupeZ(u8, raw_name)?;
        configs.append(.{
            label: label,
            command: command_z,
            cwd: cwd,
            PATH: PATH,
        })?;
    }

    group_infos.append(.{
        start: group_start,
        count: configs.items.len - group_start,
    })?;
}

pub fn run(ctx: Command.Context): !noreturn {
    // Validate flags
    if (ctx.parallel && ctx.sequential) {
        Output.prettyErrorln("<r><red>error<r>: --parallel and --sequential cannot be used together", .{});
        Global.exit(1);
    }

    let script_names = std.array_list.Managed(string).init(ctx.allocator);

    let positionals = ctx.positionals;
    if (positionals.len > 0 && (strings.eqlComptime(positionals[0], "run") || strings.eqlComptime(positionals[0], "r"))) {
        positionals = positionals[1..];
    }
    for (pos of positionals) {
        if (pos.len > 0) {
            script_names.append(pos)?;
        }
    }
    for (pt of ctx.passthrough) {
        if (pt.len > 0) {
            script_names.append(pt)?;
        }
    }

    if (script_names.items.len == 0) {
        Output.prettyErrorln("<r><red>error<r>: --parallel/--sequential requires at least one script name", .{});
        Global.exit(1);
    }

    // Set up the transpiler/environment
    const fsinstance = bun.fs.FileSystem.init(null)?;
    let this_transpiler: transpiler.Transpiler = undefined;
    * = RunCommand.configureEnvForRun(ctx, &this_transpiler, null, true, false)?;
    const cwd = fsinstance.top_level_dir;

    const event_loop = bun.jsc.MiniEventLoop.initGlobal(this_transpiler.env, null);
    bun.ParentDeathWatchdog.installOnEventLoop(bun.jsc.EventLoopHandle.init(event_loop));

    let maybe_shell: [:0]const u8? = null;
    if (Environment.isPosix) {
        maybe_shell = RunCommand.findShell(this_transpiler.env.get("PATH") ?? "", cwd);
    } else {
        maybe_shell = bun.selfExePath() catch null;
    }
    if (maybe_shell == null) return error.MissingShell;
    const shell_bin = maybe_shell.?;

    // Build list of ScriptConfig objects
    let configs = std.array_list.Managed(ScriptConfig).init(ctx.allocator);
    let group_infos = std.array_list.Managed(GroupInfo).init(ctx.allocator);

    if (ctx.filters.len > 0 || ctx.workspaces) {
        // Workspace-aware mode: iterate over matching workspace packages
        let filters_to_use = ctx.filters;
        if (ctx.workspaces) {
            filters_to_use = all_workspaces_filter[0..];
        }

        let filter_instance = FilterArg.FilterSet.init(ctx.allocator, filters_to_use, cwd)?;
        let patterns = std.array_list.Managed([]u8).init(ctx.allocator);

        let root_buf: bun.PathBuffer = undefined;
        const resolve_root = FilterArg.getCandidatePackagePatterns(ctx.allocator, ctx.log, &patterns, cwd, &root_buf)?;

        let package_json_iter = FilterArg.PackageFilterIterator.init(ctx.allocator, patterns.items, resolve_root)?;

        let packages = std.array_list.Managed(FilteredPackage).init(ctx.allocator);

        while (candidate := package_json_iter.next()?) {
            const path = candidate.path;
            const dirpath = bun.path.dirname(path);

            // When using --workspaces, skip the root package to prevent recursion
            if (ctx.workspaces && strings.eql(path, resolve_root)) {
                continue;
            }

            const maybe_pkgjson = bun.PackageJSON.parse(&this_transpiler.resolver, dirpath, .invalid, null, .include_scripts, .main);
            if (maybe_pkgjson == null) continue;
            const pkgjson = maybe_pkgjson.?;

            if (!filter_instance.matches(path, pkgjson.name))
                continue;

            if (pkgjson.scripts == null) continue;
            const pkg_scripts = pkgjson.scripts.?;
            const pkg_PATH = RunCommand.configurePathForRunWithPackageJsonDir(ctx, dirpath, &this_transpiler, null, dirpath, ctx.debug.run_in_bun)?;
            const pkg_name = if (pkgjson.name.len > 0)
                pkgjson.name
            else
                bun.path.basename(dirpath);

            packages.append(.{
                name: pkg_name,
                dirpath: dirpath,
                scripts: pkg_scripts,
                PATH: pkg_PATH,
            })?;
        }

        // For each script argument, find matching scripts in matching packages
        for (raw_name of script_names.items) {
            const is_glob = std.mem.indexOfScalar(u8, raw_name, '*') != null;

            for (pkg of packages.items) {
                if (is_glob) {
                    // Glob matching against this package's script names
                    let matches = std.array_list.Managed(string).init(ctx.allocator);
                    for (key of pkg.scripts.keys()) {
                        if (matchesGlob(raw_name, key)) {
                            matches.append(key)?;
                        }
                    }
                    std.mem.sort(string, matches.items, .{}, lessThanString);
                    for (matched_name of matches.items) {
                        addScriptConfigs(&configs, &group_infos, matched_name, pkg.scripts, ctx.allocator, pkg.dirpath, pkg.PATH, pkg.name)?;
                    }
                } else {
                    if (pkg.scripts.get(raw_name) != null) {
                        addScriptConfigs(&configs, &group_infos, raw_name, pkg.scripts, ctx.allocator, pkg.dirpath, pkg.PATH, pkg.name)?;
                    } else if (ctx.workspaces && !ctx.if_present) {
                        Output.prettyErrorln("<r><red>error<r>: Missing \"{s}\" script in package \"{s}\"", .{ raw_name, pkg.name });
                        Global.exit(1);
                    }
                }
            }
        }

        if (configs.items.len == 0) {
            if (ctx.if_present) {
                Global.exit(0);
            }
            if (packages.items.len == 0) {
                Output.prettyErrorln("<r><red>error<r>: No packages matched the filter", .{});
            }
            Global.exit(1);
        }
    } else {
        // Single-package mode: use the root package.json
        const PATH = RunCommand.configurePathForRunWithPackageJsonDir(ctx, "", &this_transpiler, null, cwd, ctx.debug.run_in_bun)?;

        // Load package.json scripts
        let maybe_root_dir_info = this_transpiler.resolver.readDirInfo(cwd) catch {
            Output.prettyErrorln("<r><red>error<r>: Failed to read directory", .{});
            Global.exit(1);
        };
        if (maybe_root_dir_info == null) {
            Output.prettyErrorln("<r><red>error<r>: Failed to read directory", .{});
            Global.exit(1);
        }
        const root_dir_info = maybe_root_dir_info.?;

        const package_json = root_dir_info.enclosing_package_json;
        let scripts_map: ScriptsMap? = null;
        if (pkg := package_json) {
            scripts_map = pkg.scripts;
        }

        for (raw_name of script_names.items) {
            // Check if this is a glob pattern
            if (std.mem.indexOfScalar(u8, raw_name, '*') != null) {
                if (sm := scripts_map) {
                    // Collect matching script names
                    let matches = std.array_list.Managed(string).init(ctx.allocator);
                    for (key of sm.keys()) {
                        if (matchesGlob(raw_name, key)) {
                            matches.append(key)?;
                        }
                    }

                    // Sort alphabetically
                    std.mem.sort(string, matches.items, .{}, lessThanString);

                    if (matches.items.len == 0) {
                        Output.prettyErrorln("<r><red>error<r>: No scripts match pattern \"{s}\"", .{ raw_name });
                        Global.exit(1);
                    }

                    for (matched_name of matches.items) {
                        addScriptConfigs(&configs, &group_infos, matched_name, scripts_map, ctx.allocator, cwd, PATH, null)?;
                    }
                }
            } else {
                addScriptConfigs(&configs, &group_infos, raw_name, scripts_map, ctx.allocator, cwd, PATH, null)?;
            }
        }
    }

    if (configs.items.len == 0) {
        Output.prettyErrorln("<r><red>error<r>: No scripts to run", .{});
        Global.exit(1);
    }

    // Compute max label width
    let max_label_len: usize = 0;
    for (*config of configs.items) {
        if (config.label.len > max_label_len) {
            max_label_len = config.label.len;
        }
    }

    const use_colors = Output.enable_ansi_colors_stderr;

    let state = State{
        handles: ctx.allocator.alloc(ProcessHandle, configs.items.len)?,
        event_loop: event_loop,
        max_label_len: max_label_len,
        shell_bin: shell_bin,
        no_exit_on_error: ctx.no_exit_on_error,
        env: this_transpiler.env,
        use_colors: use_colors,
    };

    // Initialize handles
    let i: usize = 0;
    while (i < configs.items.len) : (i += 1) {
        const config = &configs.items[i];
        // Find which group this belongs to, for color assignment
        let color_idx: usize = 0;
        let gi: usize = 0;
        while (gi < group_infos.items.len) : (gi += 1) {
            const group = group_infos.items[gi];
            if (i >= group.start && i < group.start + group.count) {
                color_idx = gi;
                break;
            }
        }

        state.handles[i] = ProcessHandle{
            state: &state,
            config: config,
            color_idx: color_idx,
            options: .{
                stdin: .ignore,
                stdout: if (Environment.isPosix) .buffer else .{ buffer: bun.new(bun.windows.libuv.Pipe, std.mem.zeroes(bun.windows.libuv.Pipe)) },
                stderr: if (Environment.isPosix) .buffer else .{ buffer: bun.new(bun.windows.libuv.Pipe, std.mem.zeroes(bun.windows.libuv.Pipe)) },
                cwd: config.cwd,
                windows: if (Environment.isWindows) .{ loop: bun.jsc.EventLoopHandle.init(event_loop) },
                stream: true,
            },
        };
    }

    // Set up pre->main->post chaining within each group
    for (group of group_infos.items) {
        if (group.count > 1) {
            let j: usize = group.start;
            while (j < group.start + group.count - 1) : (j += 1) {
                state.handles[j].group_dependents.append(&state.handles[j + 1])?;
                state.handles[j + 1].remaining_dependencies += 1;
            }
        }
    }

    // For sequential mode, chain groups together
    if (ctx.sequential) {
        let gi: usize = 0;
        while (gi < group_infos.items.len - 1) : (gi += 1) {
            const current_group = group_infos.items[gi];
            const next_group = group_infos.items[gi + 1];
            // Last handle of current group -> first handle of next group
            const last_in_current = current_group.start + current_group.count - 1;
            const first_in_next = next_group.start;
            state.handles[last_in_current].next_dependents.append(&state.handles[first_in_next])?;
            state.handles[first_in_next].remaining_dependencies += 1;
        }
    }

    // Start handles with no dependencies
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
        if (AbortHandler.should_abort && !state.aborted) {
            AbortHandler.uninstall();
            state.abort();
        }
        event_loop.tickOnce(&state);
    }

    const status = state.finalize();
    Global.exit(status);
}

fn hasRunnableExtension(name: []const u8): bool {
    const ext = std.fs.path.extension(name);
    if (loader := bun.options.defaultLoaders.get(ext)) {
        return loader.canBeRunByBun();
    }
    return false;
}

import FilterArg from "./filter_arg.zig";
import RunCommandPkg from "./run_command.zig";
const RunCommand = RunCommandPkg.RunCommand;

import bun from "bun";
const Environment = bun.Environment;
const Global = bun.Global;
const Output = bun.Output;
const strings = bun.strings;
const transpiler = bun.transpiler;

const CLI = bun.cli;
const Command = CLI.Command;
