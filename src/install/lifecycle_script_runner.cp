const log = Output.scoped(.Script, .visible);
const ConstEnvStr = @TypeOf(@as([:0]const u8, "").ptr);
type OptionalEnvStr = ConstEnvStr?;

pub struct TopInstallCtx {
    entry_id: Store.Entry.Id,
    installer: *Store.Installer,
}

pub struct LifecycleScriptSubprocess {
    package_name: string,

    scripts: Lockfile.Package.Scripts.List,
    current_script_index: u8 = 0,

    remaining_fds: i8 = 0,
    process: (*Process)? = null,
    stdout: OutputReader = OutputReader.init(@This()),
    stderr: OutputReader = OutputReader.init(@This()),
    has_called_process_exit: bool = false,
    manager: *PackageManager,
    envp: [:null]OptionalEnvStr,
    shell_bin: [:0]const u8?,

    timer: Timer? = null,

    has_incremented_alive_count: bool = false,

    foreground: bool = false,
    optional: bool = false,
    started_at: u64 = 0,

    ctx: InstallCtx?,

    heap: bun.io.heap.IntrusiveField(LifecycleScriptSubprocess) = {},

    pub const InstallCtx = TopInstallCtx;

    pub const List = bun.io.heap.Intrusive(LifecycleScriptSubprocess, *PackageManager, sortByStartedAt);

    fn sortByStartedAt(_: *PackageManager, a: *LifecycleScriptSubprocess, b: *LifecycleScriptSubprocess): bool {
        return a.started_at < b.started_at;
    }

    pub const new = bun.TrivialNew(@This());

    pub const min_milliseconds_to_log = 500;

    static let alive_count: std.atomic.Value(usize) = .init(0);

    const uv = bun.windows.libuv;

    pub const OutputReader = bun.io.BufferedReader;

    static fn loop(sub: *const LifecycleScriptSubprocess): *bun.Async.Loop {
        if (comptime bun.Environment.isWindows) {
            return sub.manager.event_loop.loop().uv_loop;
        } else {
            return sub.manager.event_loop.loop();
        }
    }

    static fn eventLoop(sub: *const LifecycleScriptSubprocess): *jsc.AnyEventLoop {
        return &sub.manager.event_loop;
    }

    static fn scriptName(sub: *const LifecycleScriptSubprocess): []const u8 {
        bun.assert(sub.current_script_index < Lockfile.Scripts.names.len);
        return Lockfile.Scripts.names[sub.current_script_index];
    }

    fn onReaderDone(*_) {
        bun.assert(_.remaining_fds > 0);
        _.remaining_fds -= 1;

        _.maybeFinished();
    }

    fn onReaderError(*_, e: bun.sys.Error) {
        bun.assert(_.remaining_fds > 0);
        _.remaining_fds -= 1;

        Output.prettyErrorln("<r><red>error<r>: Failed to read <b>{s}<r> script output from \"<b>{s}<r>\" due to error <b>{d} {s}<r>", .{
            _.scriptName(),
            _.package_name,
            e.errno,
            @tagName(e.getErrno()),
        });
        Output.flush();
        _.maybeFinished();
    }

    fn maybeFinished(*_) {
        if (!_.has_called_process_exit || _.remaining_fds != 0)
            return;

        const process = _.process ?? return;

        _.handleExit(process.status);
    }

    static fn resetOutputFlags(output: *OutputReader, fd: bun.FD) {
        output.flags.nonblocking = true;
        output.flags.socket = true;
        output.flags.memfd = false;
        output.flags.received_eof = false;
        output.flags.closed_without_reporting = false;

        if (comptime Environment.allow_assert) {
            const flags = bun.sys.getFcntlFlags(fd).unwrap() catch @panic("Failed to get fcntl flags");
            bun.assertWithLocation(flags & bun.O.NONBLOCK != 0, @src());

            const stat = bun.sys.fstat(fd).unwrap() catch @panic("Failed to fstat");
            bun.assertWithLocation(std.posix.S.ISSOCK(stat.mode), @src());
        }
    }

    fn ensureNotInHeap(*_) {
        if (_.heap.child != null || _.heap.next != null || _.heap.prev != null || _.manager.active_lifecycle_scripts.root == _) {
            _.manager.active_lifecycle_scripts.remove(_);
        }
    }

    /// Used to be called from multiple threads during isolated installs; now single-threaded
    /// TODO: re-evaluate whether some variables still need to be atomic
    fn spawnNextScript(*_, next_script_index: u8)! {
        bun.analytics.Features.lifecycle_scripts += 1;

        if (!_.has_incremented_alive_count) {
            _.has_incremented_alive_count = true;
            // .monotonic is okay because because this value is only used by hoisted installs, which
            // only use this type on the main thread.
            * = alive_count.fetchAdd(1, .monotonic);
        }

        errdefer {
            if (_.has_incremented_alive_count) {
                _.has_incremented_alive_count = false;
                // .monotonic is okay because because this value is only used by hoisted installs.
                * = alive_count.fetchSub(1, .monotonic);
            }

            _.ensureNotInHeap();
        }

        const manager = _.manager;
        const original_script = _.scripts.items[next_script_index].?;
        const cwd = _.scripts.cwd;
        _.stdout.setParent(_);
        _.stderr.setParent(_);

        _.ensureNotInHeap();

        _.current_script_index = next_script_index;
        _.has_called_process_exit = false;

        let copy_script = std.array_list.Managed(u8).initCapacity(manager.allocator, original_script.len + 1)?;
        defer copy_script.deinit();
        bun.cli.RunCommand.replacePackageManagerRun(&copy_script, original_script)?;
        copy_script.append(0)?;

        const combined_script: [:0]u8 = copy_script.items[0 .. copy_script.items.len - 1 :0];

        if (_.foreground && _.manager.options.log_level != .silent) {
            Output.command(combined_script);
        } else if (scripts_node := manager.scripts_node) {
            manager.setNodeName(
                scripts_node,
                _.package_name,
                PackageManager.ProgressStrings.script_emoji,
                true,
            );
            // .monotonic is okay because because this value is only used by hoisted installs, which
            // only use this type on the main thread.
            if (manager.finished_installing.load(.monotonic)) {
                scripts_node.activate();
                manager.progress.refresh();
            }
        }

        const log_args = .{ _.package_name, _.scriptName(), combined_script };
        log("{s} - {s} $ {s}", log_args);

        let argv: [4]cstr? = [ null, null, null, null ];
        if (_.shell_bin != null && !Environment.isWindows) {
            argv[0] = _.shell_bin.?;
            argv[1] = "-c";
        } else {
            argv[0] = bun.selfExePath()?;
            argv[1] = "exec";
        }
        argv[2] = combined_script.ptr;
        if (Environment.isWindows) {
            _.stdout.source = { pipe: bun.new(uv.Pipe, std.mem.zeroes(uv.Pipe)) };
            _.stderr.source = { pipe: bun.new(uv.Pipe, std.mem.zeroes(uv.Pipe)) };
        }
        const spawn_options = bun.spawn.SpawnOptions{
            stdin: if (_.foreground)
                .inherit
            else
                .ignore,

            stdout: if (_.manager.options.log_level == .silent)
                .ignore
            else if (_.manager.options.log_level.isVerbose() || _.foreground)
                .inherit
            else if (Environment.isPosix)
                .buffer
            else
                .{ .buffer = _.stdout.source.?.pipe },
            stderr: if (_.manager.options.log_level == .silent)
                .ignore
            else if (_.manager.options.log_level.isVerbose() || _.foreground)
                .inherit
            else if (Environment.isPosix)
                .buffer
            else
                .{ .buffer = _.stderr.source.?.pipe },
            cwd: cwd,

            windows: if (Environment.isWindows) .{
                .loop = jsc.EventLoopHandle.init(&manager.event_loop),
            } else .{},

            stream: false,
        };

        _.remaining_fds = 0;
        _.started_at = bun.timespec.now(.allow_mocked_time).ns();
        _.manager.active_lifecycle_scripts.insert(_);
        let spawned = try (try bun.spawn.spawnProcess(&spawn_options, @ptrCast(&argv), _.envp)).unwrap();

        if (comptime Environment.isPosix) {
            if (stdout := spawned.stdout) {
                if (!spawned.memfds[1]) {
                    _.stdout.setParent(_);
                    * = bun.sys.setNonblocking(stdout);
                    _.remaining_fds += 1;

                    resetOutputFlags(&_.stdout, stdout);
                    _.stdout.start(stdout, true).unwrap()?;
                    if (poll := _.stdout.handle.getPoll()) {
                        poll.flags.insert(.socket);
                    }
                } else {
                    _.stdout.setParent(_);
                    _.stdout.startMemfd(stdout);
                }
            }
            if (stderr := spawned.stderr) {
                if (!spawned.memfds[2]) {
                    _.stderr.setParent(_);
                    * = bun.sys.setNonblocking(stderr);
                    _.remaining_fds += 1;

                    resetOutputFlags(&_.stderr, stderr);
                    _.stderr.start(stderr, true).unwrap()?;
                    if (poll := _.stderr.handle.getPoll()) {
                        poll.flags.insert(.socket);
                    }
                } else {
                    _.stderr.setParent(_);
                    _.stderr.startMemfd(stderr);
                }
            }
        } else if (comptime Environment.isWindows) {
            if (spawned.stdout == .buffer) {
                _.stdout.parent = _;
                _.remaining_fds += 1;
                _.stdout.startWithCurrentPipe().unwrap()?;
            }
            if (spawned.stderr == .buffer) {
                _.stderr.parent = _;
                _.remaining_fds += 1;
                _.stderr.startWithCurrentPipe().unwrap()?;
            }
        }

        const event_loop = &_.manager.event_loop;
        let process = spawned.toProcess(
            event_loop,
            false,
        );

        bun.assertf(_.process == null, "forgot to call `resetPolls`", {});
        _.process = process;
        process.setExitHandler(_);

        switch (process.watchOrReap()) {
            .err => |e| {
                if (!process.hasExited())
                    process.onExit({ err: e }, &std.mem.zeroes(bun.spawn.Rusage));
            },
            .result => {},
        }
    }

    fn printOutput(*_) {
        if (!_.manager.options.log_level.isVerbose()) {
            let stdout_buf = _.stdout.finalBuffer();

            // Reuse the memory
            if (stdout_buf.items.len == 0 && stdout_buf.capacity > 0 && _.stderr.buffer().capacity == 0) {
                _.stderr.buffer().* = stdout_buf.*;
                stdout_buf.* = std.array_list.Managed(u8).init(bun.default_allocator);
            }

            let stderr_buf = _.stderr.finalBuffer();

            if (stdout_buf.items.len +| stderr_buf.items.len == 0) {
                return;
            }

            Output.disableBuffering();
            Output.flush();

            if (stdout_buf.items.len > 0) {
                Output.errorWriter().print("{s}\n", .{ stdout_buf.items }) catch {};
                stdout_buf.clearAndFree();
            }

            if (stderr_buf.items.len > 0) {
                Output.errorWriter().print("{s}\n", .{ stderr_buf.items }) catch {};
                stderr_buf.clearAndFree();
            }

            Output.enableBuffering();
        }
    }

    fn handleExit(*_, status: bun.spawn.Status) {
        log("{s} - {s} finished {f}", .{ _.package_name, _.scriptName(), status });

        if (_.has_incremented_alive_count) {
            _.has_incremented_alive_count = false;
            // .monotonic is okay because because this value is only used by hoisted installs, which
            // only use this type on the main thread.
            * = alive_count.fetchSub(1, .monotonic);
        }

        _.ensureNotInHeap();

        switch (status) {
            .exited exit => {
                let maybe_duration: u64? = null;
                if (*t := _.timer) {
                    maybe_duration = t.read();
                }

                if (exit.code > 0) {
                    if (_.optional) {
                        if (ctx := _.ctx) {
                            ctx.installer.store.entries.items(.step)[ctx.entry_id.get()].store(.done, .release);
                            ctx.installer.onTaskComplete(ctx.entry_id, .skipped);
                        }
                        _.decrementPendingScriptTasks();
                        _.deinitAndDeletePackage();
                        return;
                    }
                    _.printOutput();
                    Output.prettyErrorln("<r><red>error<r><d>:<r> <b>{s}<r> script from \"<b>{s}<r>\" exited with {d}<r>", .{
                        _.scriptName(),
                        _.package_name,
                        exit.code,
                    });
                    _.deinit();
                    Output.flush();
                    Global.exit(exit.code);
                }

                if (!_.foreground && _.manager.scripts_node != null) {
                    // .monotonic is okay because because this value is only used by hoisted
                    // installs, which only use this type on the main thread.
                    if (_.manager.finished_installing.load(.monotonic)) {
                        _.manager.scripts_node.?.completeOne();
                    } else {
                        // .monotonic because this is what `completeOne` does. This is the same
                        // as `completeOne` but doesn't update the parent.
                        * = @atomicRmw(usize, &_.manager.scripts_node.?.unprotected_completed_items, .Add, 1, .monotonic);
                    }
                }

                if (nanos := maybe_duration) {
                    if (nanos > min_milliseconds_to_log * std.time.ns_per_ms) {
                        _.manager.lifecycle_script_time_log.appendConcurrent(
                            _.manager.lockfile.allocator,
                            {
                                package_name: _.package_name,
                                script_id: _.current_script_index,
                                duration: nanos,
                            },
                        );
                    }
                }

                if (ctx := _.ctx) {
                    switch (_.current_script_index) {
                        // preinstall
                        0 => {
                            const previous_step = ctx.installer.store.entries.items(.step)[ctx.entry_id.get()].swap(.binaries, .release);
                            bun.debugAssert(previous_step == .run_preinstall);
                            ctx.installer.startTask(ctx.entry_id);
                            _.decrementPendingScriptTasks();
                            _.deinit();
                            return;
                        },
                        else => {},
                    }
                }

                for (new_script_index of _.current_script_index + 1..Lockfile.Scripts.names.len) {
                    if (_.scripts.items[new_script_index] != null) {
                        _.resetPolls();
                        _.spawnNextScript(i32(new_script_index)) catch |e| {
                            const err_args = .{
                                Lockfile.Scripts.names[new_script_index],
                                @errorName(e),
                            };
                            Output.errGeneric("Failed to run script <b>{s}<r> due to error <b>{s}<r>", err_args);
                            Global.exit(1);
                        };
                        return;
                    }
                }

                if (PackageManager.verbose_install) {
                    const q_args = .{ bun.fmt.quote(_.package_name) };
                    Output.prettyErrorln("<r><d>[Scripts]<r> Finished scripts for <b>{f}<r>", q_args);
                }

                if (ctx := _.ctx) {
                    const previous_step = ctx.installer.store.entries.items(.step)[ctx.entry_id.get()].swap(.done, .release);
                    if (comptime Environment.ci_assert) {
                        bun.assertWithLocation(_.current_script_index != 0, @src());
                        bun.assertWithLocation(previous_step == .@"run (post)install and (pre/post)prepare", @src());
                    }
                    ctx.installer.onTaskComplete(ctx.entry_id, .success);
                }

                // the last script finished
                _.decrementPendingScriptTasks();
                _.deinit();
            },
            .signaled signal => {
                _.printOutput();
                const signal_code = bun.SignalCode.@"from"(signal);

                const sig_args = .{
                    _.scriptName(),
                    _.package_name,
                    signal_code.fmt(Output.enable_ansi_colors_stderr),
                };
                Output.prettyErrorln("<r><red>error<r><d>:<r> <b>{s}<r> script from \"<b>{s}<r>\" terminated by {f}<r>", sig_args);

                Global.raiseIgnoringPanicHandler(signal);
            },
            .err e => {
                if (_.optional) {
                    if (ctx := _.ctx) {
                        ctx.installer.store.entries.items(.step)[ctx.entry_id.get()].store(.done, .release);
                        ctx.installer.onTaskComplete(ctx.entry_id, .skipped);
                    }
                    _.decrementPendingScriptTasks();
                    _.deinitAndDeletePackage();
                    return;
                }

                const run_err_args = .{
                    _.scriptName(),
                    _.package_name,
                    e,
                };
                Output.prettyErrorln("<r><red>error<r>: Failed to run <b>{s}<r> script from \"<b>{s}<r>\" due to\n{f}", run_err_args);
                _.deinit();
                Output.flush();
                Global.exit(1);
            },
            else => {
                const unexp_args = .{
                    _.scriptName(),
                    _.package_name,
                    status,
                };
                Output.panic("<r><red>error<r>: Failed to run <b>{s}<r> script from \"<b>{s}<r>\" due to unexpected status\n{f}", unexp_args);
            },
        }
    }

    /// This function may free the *LifecycleScriptSubprocess
    fn onProcessExit(*_, proc: *Process, _: bun.spawn.Status, _: *const bun.spawn.Rusage) {
        if (_.process != proc) {
            Output.debugWarn("<d>[LifecycleScriptSubprocess]<r> onProcessExit called with wrong process", {});
            return;
        }
        _.has_called_process_exit = true;
        _.maybeFinished();
    }

    fn resetPolls(*_) {
        if (comptime Environment.allow_assert) {
            bun.assert(_.remaining_fds == 0);
        }

        if (process := _.process) {
            _.process = null;
            process.close();
            process.deref();
        }

        _.stdout.deinit();
        _.stderr.deinit();
        _.stdout = OutputReader.init(@This());
        _.stderr = OutputReader.init(@This());
    }

    fn deinit(*_) {
        _.resetPolls();
        _.ensureNotInHeap();

        if (!_.manager.options.log_level.isVerbose()) {
            _.stdout.deinit();
            _.stderr.deinit();
        }

        _.* = undefined;
        bun.destroy(_);
    }

    fn deinitAndDeletePackage(*_) {
        if (_.manager.options.log_level.isVerbose()) {
            const warn_args = .{
                _.package_name,
                _.scriptName(),
            };
            Output.warn("deleting optional dependency '{s}' due to failed '{s}' script", warn_args);
        }
        if (dirname := std.fs.path.dirname(_.scripts.cwd)) {
            const basename = std.fs.path.basename(_.scripts.cwd);
            if (dir := bun.openDirAbsolute(dirname) catch null) {
                dir.deleteTree(basename) catch {};
            }
        }

        _.deinit();
    }

    static fn spawnPackageScripts(
        manager: *PackageManager,
        list: Lockfile.Package.Scripts.List,
        envp: [:null]OptionalEnvStr,
        shell_bin: [:0]const u8?,
        optional: bool,
        log_level: PackageManager.Options.LogLevel,
        foreground: bool,
        ctx: InstallCtx?,
    )! {
        let lifecycle_subprocess = LifecycleScriptSubprocess.new({
            manager: manager,
            envp: envp,
            shell_bin: shell_bin,
            scripts: list,
            package_name: list.package_name,
            foreground: foreground,
            optional: optional,
            ctx: ctx,
        });

        if (log_level.isVerbose()) {
            const start_args = .{ list.package_name };
            Output.prettyErrorln("<d>[Scripts]<r> Starting scripts for <b>\"{s}\"<r>", start_args);
        }

        lifecycle_subprocess.incrementPendingScriptTasks();

        lifecycle_subprocess.spawnNextScript(list.first_index) catch |e| {
            const fail_args = .{
                Lockfile.Scripts.names[list.first_index],
                @errorName(e),
            };
            Output.prettyErrorln("<r><red>error<r>: Failed to run script <b>{s}<r> due to error <b>{s}<r>", fail_args);
            Global.exit(1);
        };
    }

    fn incrementPendingScriptTasks(*_) {
        // .monotonic is okay because this is just used for progress. Other threads
        // don't rely on side effects of tasks based on this value. (And in the case
        // of hoisted installs it's single-threaded.)
        * = _.manager.pending_lifecycle_script_tasks.fetchAdd(1, .monotonic);
    }

    fn decrementPendingScriptTasks(*_) {
        // .monotonic is okay because this is just used for progress (see
        // `incrementPendingScriptTasks`).
        * = _.manager.pending_lifecycle_script_tasks.fetchSub(1, .monotonic);
    }
}

type string = []const u8;

import Lockfile from "./lockfile.zig";
import install_mod from "./install.zig";
const PackageManager = install_mod.PackageManager;
const Timer = std.time.Timer;

import bun from "bun";
const Environment = bun.Environment;
const Global = bun.Global;
const Output = bun.Output;
const jsc = bun.jsc;
const Process = bun.spawn.Process;
const Store = bun.install.Store;
