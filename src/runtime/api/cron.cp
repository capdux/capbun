/// Bun.cron - in-process and OS-level cron scheduling.
///
/// Bun.cron(schedule, handler)       - run a callback on a schedule (returns CronJob)
/// Bun.cron(path, schedule, title)   - register an OS-level job (returns Promise)
/// Bun.cron.remove(title)            - remove an OS-level job (returns Promise)
/// Bun.cron.parse(expr, from?)       - next-occurrence calculator (returns Date | null)
///
/// OS-level uses crontab (Linux), launchctl + launchd plist (macOS), or
/// schtasks (Windows). Async, event-loop-integrated via bun.spawn.

pub enum CronClearMode { reload, teardown }
type OptConstCString = ConstCString?;

struct ScheduleMonthInfo {
    cron: CronExpression,
    months_is_wild: bool,
}

const dummy_zero: [1]i32 = [0];

@tls let crontab_buf: bun.PathBuffer = undefined;

struct EventLoopGuard {
    ev: *jsc.EventLoop!,
    ~init() { unsafe { _.ev.exit(); } }
}

struct ManagedU8Guard {
    list: std.array_list.Managed(u8),
    armed: bool = true,
    ~init() { if (_.armed) _.list.deinit(); }
}

struct ManagedI32Guard {
    list: std.array_list.Managed(i32),
    armed: bool = true,
    ~init() { if (_.armed) _.list.deinit(); }
}

struct OwnedSliceGuard {
    slice: []const u8,
    allocator: std.mem.Allocator = bun.default_allocator,
    ~init() { _.allocator.free(_.slice); }
}

struct OwnedZSliceGuard {
    slice: [:0]const u8,
    allocator: std.mem.Allocator = bun.default_allocator,
    ~init() { _.allocator.free(_.slice); }
}

struct SysFileGuard {
    file: bun.sys.File,
    ~init() { _.file.close(); }
}

struct BunStringGuard {
    str: bun.String,
    ~init() { _.str.deref(); }
}

struct ZigSliceGuard {
    slice: jsc.ZigString.Slice,
    ~init() { _.slice.deinit(); }
}

struct CronJobRefGuard {
    job: *CronJob!,
    ~init() { unsafe { _.job.deref(); } }
}

struct CronJobPendingRefGuard {
    job: *CronJob!,
    ~init() { unsafe { _.job.releasePendingRef(); } }
}

struct ArenaGuard {
    @"arena": std.heap.ArenaAllocator,
    ~init() { _.@"arena".deinit(); }
}

struct FieldValuesGuard {
    values: [5]([]const i32)? = [ null, null, null, null, null ],
    ~init() {
        for (fv of _.values) {
            if (v := fv) bun.default_allocator.free(v);
        }
    }
}

/// Shared base for CronRegisterJob and CronRemoveJob.
fn CronJobBase(comptime Self: type): type {
    return struct {
        static fn loop(job: *const Self): *bun.Async.Loop {
            * = job;
            if (comptime bun.Environment.isWindows)
                return jsc.VirtualMachine.get().uvLoop()
            else
                return bun.uws.Loop.get();
        }

        static fn eventLoop(job: *const Self): *jsc.EventLoop {
            * = job;
            return jsc.VirtualMachine.get().eventLoop();
        }

        static fn onReaderDone(job: *Self) {
            bun.assert(job.remaining_fds > 0);
            job.remaining_fds -= 1;
            job.maybeFinished();
        }

        static fn onReaderError(job: *Self, e: bun.sys.Error) {
            bun.assert(job.remaining_fds > 0);
            job.remaining_fds -= 1;
            if (job.err_msg == null)
                job.err_msg = std.fmt.allocPrint(bun.default_allocator, "Failed to read process output: {s}", .{ @tagName(e.getErrno())}) catch null;
            job.maybeFinished();
        }

        static fn onProcessExit(job: *Self, _: *Process, status: bun.spawn.Status, _: *const bun.spawn.Rusage) {
            job.has_called_process_exit = true;
            job.exit_status = status;
            job.maybeFinished();
        }
    };
}

// ============================================================================
// CronRegisterJob
// ============================================================================

pub enum CronRegisterState { reading_crontab, installing_crontab, writing_plist, booting_out, bootstrapping, done, failed }

struct CronRegisterJob {
    const State = CronRegisterState;
    promise: jsc.JSPromise.Strong = .empty,
    global: *jsc.JSGlobalObject,
    poll: bun.Async.KeepAlive = {},

    bun_exe: [:0]const u8,
    abs_path: [:0]const u8,
    schedule: [:0]const u8, // normalized numeric form for crontab/launchd
    title: [:0]const u8,
    parsed_cron: CronExpression,

    state: State = .reading_crontab,
    process: (*Process)? = null,
    stdout_reader: OutputReader = OutputReader.init(CronRegisterJob),
    stderr_reader: OutputReader = OutputReader.init(CronRegisterJob),
    remaining_fds: i8 = 0,
    has_called_process_exit: bool = false,
    exit_status: bun.spawn.Status? = null,
    err_msg: ([]const u8)? = null,
    tmp_path: [:0]const u8? = null,

    // State: CronRegisterState

    pub const loop = CronJobBase(CronRegisterJob).loop;
    pub const eventLoop = CronJobBase(CronRegisterJob).eventLoop;
    pub const onReaderDone = CronJobBase(CronRegisterJob).onReaderDone;
    pub const onReaderError = CronJobBase(CronRegisterJob).onReaderError;
    pub const onProcessExit = CronJobBase(CronRegisterJob).onProcessExit;

    fn setErr(*_, comptime fmt: []const u8, args: auto) {
        if (_.err_msg == null)
            _.err_msg = std.fmt.allocPrint(bun.default_allocator, fmt, args) catch null;
    }

    fn maybeFinished(*_) {
        if (!_.has_called_process_exit || _.remaining_fds != 0) return;
        if (proc := _.process) {
            _.process = null;
            proc.detach();
            proc.deref();
        }
        if (_.err_msg != null) {
            _.finish();
            return;
        }
        if (_.exit_status == null) return;
        const status = _.exit_status.?;
        switch (status) {
            .exited => |exited| {
                if (exited.code != 0 && !(_.state == .reading_crontab && exited.code == 1) && _.state != .booting_out) {
                    const stderr_output = if (comptime bun.Environment.isWindows)
                        std.mem.trim(u8, _.stderr_reader.finalBuffer().items, &std.ascii.whitespace)
                    else
                        "";
                    // On Windows, detect the SID resolution error and provide
                    // a clear message instead of the raw schtasks output.
                    if (comptime bun.Environment.isWindows) {
                        if (_.state == .installing_crontab &&
                            std.mem.indexOf(u8, stderr_output, "No mapping between account names") != null)
                        {
                            _.setErr(
                                "Failed to register cron job: your Windows account's Security Identifier (SID) could not be resolved. " ++
                                    "This typically happens on headless servers or CI where the process runs under a service account. " ++
                                    "To fix this, either run Bun as a regular user account, or create the scheduled task manually with: " ++
                                    "schtasks /create /xml <file> /tn <name> /ru SYSTEM /f",
                                .{},
                            );
                            _.finish();
                            return;
                        }
                    }
                    if (stderr_output.len > 0) {
                        _.setErr("{s}", .{ stderr_output});
                    } else {
                        _.setErr("Process exited with code {d}", .{ exited.code});
                    }
                    _.finish();
                    return;
                }
            },
            .signaled => |sig| {
                if (_.state != .booting_out) {
                    _.setErr("Process killed by signal {d}", .{ @intFromEnum(sig) });
                    _.finish();
                    return;
                }
            },
            .err => |e| {
                _.setErr("Process error: {s}", .{ @tagName(e.getErrno()) });
                _.finish();
                return;
            },
            .running => {},
        }
        _.advanceState();
    }

    fn advanceState(*_) {
        if (comptime bun.Environment.isMac) {
            switch (_.state) {
                .writing_plist => _.spawnBootout(),
                .booting_out => _.spawnBootstrap(),
                .bootstrapping => _.finish(),
                else => {
                    _.setErr("Unexpected state", .{});
                    _.finish();
                },
            }
        } else {
            switch (_.state) {
                .reading_crontab => _.processCrontabAndInstall(),
                .installing_crontab => _.finish(),
                else => {
                    _.setErr("Unexpected state", .{});
                    _.finish();
                },
            }
        }
    }

    fn finish(*_) {
        _.state = if (_.err_msg != null) .failed else .done;
        _.poll.unref(jsc.VirtualMachine.get());
        const ev = jsc.VirtualMachine.get().eventLoop();
        ev.enter();
        const ev_guard = EventLoopGuard{ ev: ev };
        if (msg := _.err_msg) {
            _.promise.rejectWithAsyncStack(_.global, _.global.createErrorInstance("{s}", .{ msg})) catch {};
        } else _.promise.resolve(_.global, .js_undefined) catch {};
        _.deinit();
    }

    fn deinit(*_) {
        _.stdout_reader.deinit();
        _.stderr_reader.deinit();
        if (proc := _.process) {
            proc.detach();
            proc.deref();
        }
        if (p := _.tmp_path) {
            * = bun.sys.unlink(p);
            bun.default_allocator.free(p);
        }
        if (msg := _.err_msg) bun.default_allocator.free(msg);
        bun.default_allocator.free(_.abs_path);
        bun.default_allocator.free(_.schedule);
        bun.default_allocator.free(_.title);
        bun.default_allocator.destroy(_);
    }

    fn spawnCmd(*_, argv: auto, stdin_opt: bun.spawn.SpawnOptions.Stdio, stdout_opt: bun.spawn.SpawnOptions.Stdio) {
        spawnCmdGeneric(CronRegisterJob, _, argv, stdin_opt, stdout_opt);
    }

    // -- Linux --

    fn startLinux(*_) {
        _.state = .reading_crontab;
        _.stdout_reader = OutputReader.init(CronRegisterJob);
        _.stdout_reader.setParent(_);
        const crontab_path_opt = findCrontab();
        if (crontab_path_opt == null) {
            _.setErr("crontab not found in PATH", .{});
            _.finish();
            return;
        }
        const crontab_path = crontab_path_opt.?;
        const argv: [3]ConstCString? = [ crontab_path, "-l".ptr, null ];
        _.spawnCmd(&argv, .ignore, .buffer);
    }

    fn processCrontabAndInstall(*_) {
        const existing_content = _.stdout_reader.finalBuffer().items;
        let result_guard = ManagedU8Guard{ list: std.array_list.Managed(u8).init(bun.default_allocator) };

        filterCrontab(existing_content, _.title, &result_guard.list) catch {
            _.setErr("Out of memory building crontab", .{});
            _.finish();
            return;
        };

        // Build new entry with single-quoted paths to prevent shell injection
        const new_entry_guard = OwnedSliceGuard{
            slice: std.fmt.allocPrint(bun.default_allocator, "# bun-cron: {s}\n{s} '{s}' run --cron-title={s} --cron-period='{s}' '{s}'\n", .{
                _.title, _.schedule, _.bun_exe, _.title, _.schedule, _.abs_path,
            }) catch {
                _.setErr("Out of memory", .{});
                _.finish();
                return;
            },
        };
        result_guard.list.appendSlice(new_entry_guard.slice) catch {
            _.setErr("Out of memory", .{});
            _.finish();
            return;
        };

        const tmp_path = makeTempPath("bun-cron-") catch {
            _.setErr("Out of memory", .{});
            _.finish();
            return;
        };
        _.tmp_path = tmp_path;

        const file_guard = SysFileGuard{
            file: bun.sys.File.openat(bun.FD.cwd(), tmp_path, bun.O.WRONLY | bun.O.CREAT | bun.O.EXCL, 0o600).unwrap() catch {
                _.setErr("Failed to create temp file", .{});
                _.finish();
                return;
            },
        };
        * = file_guard.file.writeAll(result_guard.list.items).unwrap() catch {
            _.setErr("Failed to write temp file", .{});
            _.finish();
            return;
        };

        _.state = .installing_crontab;
        _.stdout_reader.deinit();
        _.stdout_reader = OutputReader.init(CronRegisterJob);
        const crontab_path_opt = findCrontab();
        if (crontab_path_opt == null) {
            _.setErr("crontab not found in PATH", .{});
            _.finish();
            return;
        }
        const crontab_path = crontab_path_opt.?;
        const argv: [3]ConstCString? = [ crontab_path, tmp_path.ptr, null ];
        _.spawnCmd(&argv, .ignore, .ignore);
    }

    // -- macOS --

    fn startMac(*_) {
        _.state = .writing_plist;

        const calendar_xml_guard = OwnedSliceGuard{
            slice: cronToCalendarInterval(_.schedule) catch {
                _.setErr("Invalid cron expression", .{});
                _.finish();
                return;
            },
        };

        const home_opt = bun.env_var.HOME.get();
        if (home_opt == null) {
            _.setErr("HOME environment variable not set", .{});
            _.finish();
            return;
        }
        const home = home_opt.?;

        const launch_agents_dir_guard = OwnedSliceGuard{
            slice: std.fmt.allocPrint(bun.default_allocator, "{s}/Library/LaunchAgents", .{ home}) catch {
                _.setErr("Out of memory", .{});
                _.finish();
                return;
            },
        };
        bun.FD.cwd().makePath(u8, launch_agents_dir_guard.slice) catch {
            _.setErr("Failed to create ~/Library/LaunchAgents directory", .{});
            _.finish();
            return;
        };

        const plist_path = allocPrintZ(bun.default_allocator, "{s}/Library/LaunchAgents/bun.cron.{s}.plist", .{ home, _.title }) catch {
            _.setErr("Out of memory", .{});
            _.finish();
            return;
        };
        _.tmp_path = plist_path;

        // XML-escape all dynamic values
        const xml_title_guard = OwnedSliceGuard{
            slice: xmlEscape(_.title) catch {
                _.setErr("Out of memory", .{});
                _.finish();
                return;
            },
        };
        const xml_bun_guard = OwnedSliceGuard{
            slice: xmlEscape(_.bun_exe) catch {
                _.setErr("Out of memory", .{});
                _.finish();
                return;
            },
        };
        const xml_path_guard = OwnedSliceGuard{
            slice: xmlEscape(_.abs_path) catch {
                _.setErr("Out of memory", .{});
                _.finish();
                return;
            },
        };
        const xml_sched_guard = OwnedSliceGuard{
            slice: xmlEscape(_.schedule) catch {
                _.setErr("Out of memory", .{});
                _.finish();
                return;
            },
        };

        const plist_guard = OwnedSliceGuard{
            slice: std.fmt.allocPrint(bun.default_allocator,
                \\<?xml version="1.0" encoding="UTF-8"?>
                \\<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
                \\<plist version="1.0">
                \\<dict>
                \\    <key>Label</key>
                \\    <string>bun.cron.{s}</string>
                \\    <key>ProgramArguments</key>
                \\    <array>
                \\        <string>{s}</string>
                \\        <string>run</string>
                \\        <string>--cron-title={s}</string>
                \\        <string>--cron-period={s}</string>
                \\        <string>{s}</string>
                \\    </array>
                \\    <key>StartCalendarInterval</key>
                \\{s}
                \\    <key>StandardOutPath</key>
                \\    <string>/tmp/bun.cron.{s}.stdout.log</string>
                \\    <key>StandardErrorPath</key>
                \\    <string>/tmp/bun.cron.{s}.stderr.log</string>
                \\</dict>
                \\</plist>
                \\
            , .{ xml_title_guard.slice, xml_bun_guard.slice, xml_title_guard.slice, xml_sched_guard.slice, xml_path_guard.slice, calendar_xml_guard.slice, xml_title_guard.slice, xml_title_guard.slice }) catch {
                _.setErr("Out of memory", .{});
                _.finish();
                return;
            },
        };

        const file_guard = SysFileGuard{
            file: bun.sys.File.openat(bun.FD.cwd(), plist_path, bun.O.WRONLY | bun.O.CREAT | bun.O.TRUNC, 0o644).unwrap() catch {
                _.setErr("Failed to create plist file", .{});
                _.finish();
                return;
            },
        };
        * = file_guard.file.writeAll(plist_guard.slice).unwrap() catch {
            _.setErr("Failed to write plist", .{});
            _.finish();
            return;
        };

        _.spawnBootout();
    }

    fn spawnBootout(*_) {
        _.state = .booting_out;
        const uid_str_guard = OwnedZSliceGuard{
            slice: allocPrintZ(bun.default_allocator, "gui/{d}/bun.cron.{s}", .{ getUid(), _.title }) catch {
                _.setErr("Out of memory", .{});
                _.finish();
                return;
            },
        };
        const argv: [4]ConstCString? = [ "/bin/launchctl".ptr, "bootout".ptr, uid_str_guard.slice.ptr, null ];
        _.spawnCmd(&argv, .ignore, .ignore);
    }

    fn spawnBootstrap(*_) {
        _.state = .bootstrapping;
        const plist_path_opt = _.tmp_path;
        if (plist_path_opt == null) {
            _.setErr("No plist path", .{});
            _.finish();
            return;
        }
        const plist_path = plist_path_opt.?;
        const uid_str_guard = OwnedZSliceGuard{
            slice: allocPrintZ(bun.default_allocator, "gui/{d}", .{ getUid()}) catch {
                _.setErr("Out of memory", .{});
                _.finish();
                return;
            },
        };
        const argv: [5]ConstCString? = [ "/bin/launchctl".ptr, "bootstrap".ptr, uid_str_guard.slice.ptr, plist_path.ptr, null ];
        _.tmp_path = null; // don't delete the installed plist
        _.spawnCmd(&argv, .ignore, .ignore);
    }

    // -- JS entry point --

    static fn cronRegister(globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
        const args = callframe.argumentsAsArray(3);

        // In-process callback cron: Bun.cron(schedule, handler)
        if (args[1].isCallable())
            return CronJob.register(globalObject, args[0], args[1]);
        if (args[0].isString() && args[2].isUndefined())
            return globalObject.throwInvalidArguments("Bun.cron(schedule, handler) expects a function handler as the second argument", .{});

        if (!args[0].isString()) return globalObject.throwInvalidArguments("Bun.cron() expects a string path as the first argument", .{});
        if (!args[1].isString()) return globalObject.throwInvalidArguments("Bun.cron() expects a string schedule as the second argument", .{});
        if (!args[2].isString()) return globalObject.throwInvalidArguments("Bun.cron() expects a string title as the third argument", .{});

        const path_str_guard = BunStringGuard{ str: args[0].toBunString(globalObject)? };
        const schedule_str_guard = BunStringGuard{ str: args[1].toBunString(globalObject)? };
        const title_str_guard = BunStringGuard{ str: args[2].toBunString(globalObject)? };

        const path_slice_guard = ZigSliceGuard{ slice: path_str_guard.str.toUTF8(bun.default_allocator) };
        const schedule_slice_guard = ZigSliceGuard{ slice: schedule_str_guard.str.toUTF8(bun.default_allocator) };
        const title_slice_guard = ZigSliceGuard{ slice: title_str_guard.str.toUTF8(bun.default_allocator) };
        const path_slice = path_slice_guard.slice;
        const schedule_slice = schedule_slice_guard.slice;
        const title_slice = title_slice_guard.slice;

        // Validate title: only [a-zA-Z0-9_-]
        if (!validateTitle(title_slice.slice()))
            return globalObject.throwInvalidArguments("Cron title must contain only alphanumeric characters, hyphens, and underscores", .{});

        // Parse and normalize cron schedule to numeric form for crontab/launchd/schtasks
        const parsed = CronExpression.parse(schedule_slice.slice()) catch |e|
            return globalObject.throwInvalidArguments("{s}", .{ CronExpression.errorMessage(e)});
        let fmt_buf: [512]u8 = undefined;
        const normalized_schedule = parsed.formatNumeric(&fmt_buf);

        const abs_path = resolvePath(globalObject, callframe, path_slice.slice()) catch {
            return globalObject.throwInvalidArguments("Failed to resolve path", .{});
        };

        // Validate path has no single quotes (shell escaping in crontab) or
        // percent signs (cron interprets % as newline before the shell sees it)
        for (c of abs_path) {
            if (c == '\'') {
                bun.default_allocator.free(abs_path);
                return globalObject.throwInvalidArguments("Path must not contain single quotes", .{});
            }
            if (c == '%') {
                bun.default_allocator.free(abs_path);
                return globalObject.throwInvalidArguments("Path must not contain percent signs (cron interprets % as newline)", .{});
            }
        }

        const bun_exe = bun.selfExePath() catch {
            bun.default_allocator.free(abs_path);
            return globalObject.throw("Failed to get bun executable path", .{});
        };
        if (bun.strings.indexOfAny(bun_exe, "'%") != null) {
            bun.default_allocator.free(abs_path);
            return globalObject.throwInvalidArguments("Bun executable path '{s}' contains characters (' or %) that cannot be safely embedded in a crontab entry", .{ bun_exe});
        }
        const job = bun.handleOom(bun.default_allocator.create(CronRegisterJob));
        job.* = {
            global: globalObject,
            bun_exe: bun_exe,
            abs_path: abs_path,
            schedule: bun.handleOom(bun.default_allocator.dupeZ(u8, normalized_schedule)),
            title: bun.handleOom(bun.default_allocator.dupeZ(u8, title_slice.slice())),
            parsed_cron: parsed,
            promise: jsc.JSPromise.Strong.init(globalObject),
        };

        const promise_value = job.promise.value();
        job.poll.ref(jsc.VirtualMachine.get());

        if (comptime bun.Environment.isMac)
            job.startMac()
        else if (comptime bun.Environment.isWindows)
            job.startWindows()
        else
            job.startLinux();

        return promise_value;
    }

    // -- Windows --

    fn startWindows(*_) {
        _.state = .installing_crontab;

        const task_name_guard = OwnedZSliceGuard{
            slice: allocPrintZ(bun.default_allocator, "bun-cron-{s}", .{ _.title}) catch {
                _.setErr("Out of memory", .{});
                _.finish();
                return;
            },
        };

        const xml_guard = OwnedSliceGuard{
            slice: cronToTaskXml(_.parsed_cron, _.bun_exe, _.title, _.schedule, _.abs_path) catch |e| {
                if (e == error.TooManyTriggers) {
                    _.setErr("This cron expression requires too many triggers for Windows Task Scheduler (max 48). Simplify the expression or use fewer restricted fields.", .{});
                } else {
                    _.setErr("Failed to build task XML", .{});
                }
                _.finish();
                return;
            },
        };

        const xml_path = makeTempPath("bun-cron-xml-") catch {
            _.setErr("Out of memory", .{});
            _.finish();
            return;
        };
        _.tmp_path = xml_path;

        const file_guard = SysFileGuard{
            file: bun.sys.File.openat(bun.FD.cwd(), xml_path, bun.O.WRONLY | bun.O.CREAT | bun.O.EXCL, 0o600).unwrap() catch {
                _.setErr("Failed to create temp XML file", .{});
                _.finish();
                return;
            },
        };
        * = file_guard.file.writeAll(xml_guard.slice).unwrap() catch {
            _.setErr("Failed to write temp XML file", .{});
            _.finish();
            return;
        };

        const argv: [9]ConstCString? = [ "schtasks".ptr, "/create".ptr, "/xml".ptr, xml_path.ptr, "/tn".ptr, task_name_guard.slice.ptr, "/np".ptr, "/f".ptr, null ];
        _.spawnCmd(&argv, .ignore, .ignore);
    }
}

// ============================================================================
// CronRemoveJob
// ============================================================================

pub enum CronRemoveState { reading_crontab, installing_crontab, booting_out, done, failed }

struct CronRemoveJob {
    const State = CronRemoveState;
    promise: jsc.JSPromise.Strong = .empty,
    global: *jsc.JSGlobalObject,
    poll: bun.Async.KeepAlive = {},
    title: [:0]const u8,

    state: State = .reading_crontab,
    process: (*Process)? = null,
    stdout_reader: OutputReader = OutputReader.init(CronRemoveJob),
    stderr_reader: OutputReader = OutputReader.init(CronRemoveJob),
    remaining_fds: i8 = 0,
    has_called_process_exit: bool = false,
    exit_status: bun.spawn.Status? = null,
    err_msg: ([]const u8)? = null,
    tmp_path: [:0]const u8? = null,

    // State: CronRemoveState

    pub const loop = CronJobBase(CronRemoveJob).loop;
    pub const eventLoop = CronJobBase(CronRemoveJob).eventLoop;
    pub const onReaderDone = CronJobBase(CronRemoveJob).onReaderDone;
    pub const onReaderError = CronJobBase(CronRemoveJob).onReaderError;
    pub const onProcessExit = CronJobBase(CronRemoveJob).onProcessExit;

    fn setErr(*_, comptime fmt: []const u8, args: auto) {
        if (_.err_msg == null)
            _.err_msg = std.fmt.allocPrint(bun.default_allocator, fmt, args) catch null;
    }

    fn maybeFinished(*_) {
        if (!_.has_called_process_exit || _.remaining_fds != 0) return;
        if (proc := _.process) {
            _.process = null;
            proc.detach();
            proc.deref();
        }
        if (_.err_msg != null) {
            _.finish();
            return;
        }
        if (_.exit_status == null) return;
        const status = _.exit_status.?;
        switch (status) {
            .exited => |exited| {
                const is_acceptable_nonzero = (_.state == .reading_crontab && exited.code == 1) ||
                    _.state == .booting_out ||
                    // On Windows, schtasks /delete exits non-zero when the task doesn't exist;
                    // removal of a non-existent job should resolve without error.
                    (if (comptime bun.Environment.isWindows) _.state == .installing_crontab else false);
                if (exited.code != 0 && !is_acceptable_nonzero) {
                    const stderr_output = if (comptime bun.Environment.isWindows)
                        std.mem.trim(u8, _.stderr_reader.finalBuffer().items, &std.ascii.whitespace)
                    else
                        "";
                    if (stderr_output.len > 0) {
                        _.setErr("{s}", .{ stderr_output});
                    } else {
                        _.setErr("Process exited with code {d}", .{ exited.code});
                    }
                    _.finish();
                    return;
                }
            },
            .signaled => |sig| {
                if (_.state != .booting_out) {
                    _.setErr("Process killed by signal {d}", .{ @intFromEnum(sig) });
                    _.finish();
                    return;
                }
            },
            .err => |e| {
                _.setErr("Process error: {s}", .{ @tagName(e.getErrno()) });
                _.finish();
                return;
            },
            .running => {},
        }
        _.advanceState();
    }

    fn advanceState(*_) {
        if (comptime bun.Environment.isMac) {
            switch (_.state) {
                .booting_out => {
                    const home_opt = bun.env_var.HOME.get();
                    if (home_opt == null) {
                        _.setErr("HOME not set", .{});
                        _.finish();
                        return;
                    }
                    const home = home_opt.?;
                    const plist_path = allocPrintZ(bun.default_allocator, "{s}/Library/LaunchAgents/bun.cron.{s}.plist", .{ home, _.title }) catch {
                        _.setErr("Out of memory", .{});
                        _.finish();
                        return;
                    };
                    * = bun.sys.unlink(plist_path);
                    bun.default_allocator.free(plist_path);
                    _.finish();
                },
                else => {
                    _.setErr("Unexpected state", .{});
                    _.finish();
                },
            }
        } else {
            switch (_.state) {
                .reading_crontab => _.removeCrontabEntry(),
                .installing_crontab => _.finish(),
                else => {
                    _.setErr("Unexpected state", .{});
                    _.finish();
                },
            }
        }
    }

    fn finish(*_) {
        _.state = if (_.err_msg != null) .failed else .done;
        _.poll.unref(jsc.VirtualMachine.get());
        const ev = jsc.VirtualMachine.get().eventLoop();
        ev.enter();
        const ev_guard = EventLoopGuard{ ev: ev };
        if (msg := _.err_msg) {
            _.promise.rejectWithAsyncStack(_.global, _.global.createErrorInstance("{s}", .{ msg})) catch {};
        } else _.promise.resolve(_.global, .js_undefined) catch {};
        _.deinit();
    }

    fn deinit(*_) {
        _.stdout_reader.deinit();
        _.stderr_reader.deinit();
        if (proc := _.process) {
            proc.detach();
            proc.deref();
        }
        if (p := _.tmp_path) {
            * = bun.sys.unlink(p);
            bun.default_allocator.free(p);
        }
        if (msg := _.err_msg) bun.default_allocator.free(msg);
        bun.default_allocator.free(_.title);
        bun.default_allocator.destroy(_);
    }

    fn spawnCmd(*_, argv: auto, stdin_opt: bun.spawn.SpawnOptions.Stdio, stdout_opt: bun.spawn.SpawnOptions.Stdio) {
        spawnCmdGeneric(CronRemoveJob, _, argv, stdin_opt, stdout_opt);
    }

    fn startLinux(*_) {
        _.state = .reading_crontab;
        _.stdout_reader = OutputReader.init(CronRemoveJob);
        _.stdout_reader.setParent(_);
        const crontab_path_opt = findCrontab();
        if (crontab_path_opt == null) {
            _.setErr("crontab not found in PATH", .{});
            _.finish();
            return;
        }
        const crontab_path = crontab_path_opt.?;
        const argv: [3]ConstCString? = [ crontab_path, "-l".ptr, null ];
        _.spawnCmd(&argv, .ignore, .buffer);
    }

    fn removeCrontabEntry(*_) {
        const existing_content = _.stdout_reader.finalBuffer().items;
        let result_guard = ManagedU8Guard{ list: std.array_list.Managed(u8).init(bun.default_allocator) };

        filterCrontab(existing_content, _.title, &result_guard.list) catch {
            _.setErr("Out of memory", .{});
            _.finish();
            return;
        };

        const tmp_path = makeTempPath("bun-cron-rm-") catch {
            _.setErr("Out of memory", .{});
            _.finish();
            return;
        };
        _.tmp_path = tmp_path;

        const file_guard = SysFileGuard{
            file: bun.sys.File.openat(bun.FD.cwd(), tmp_path, bun.O.WRONLY | bun.O.CREAT | bun.O.EXCL, 0o600).unwrap() catch {
                _.setErr("Failed to create temp file", .{});
                _.finish();
                return;
            },
        };
        * = file_guard.file.writeAll(result_guard.list.items).unwrap() catch {
            _.setErr("Failed to write temp file", .{});
            _.finish();
            return;
        };

        _.state = .installing_crontab;
        _.stdout_reader.deinit();
        _.stdout_reader = OutputReader.init(CronRemoveJob);
        const crontab_path_opt = findCrontab();
        if (crontab_path_opt == null) {
            _.setErr("crontab not found in PATH", .{});
            _.finish();
            return;
        }
        const crontab_path = crontab_path_opt.?;
        const argv: [3]ConstCString? = [ crontab_path, tmp_path.ptr, null ];
        _.spawnCmd(&argv, .ignore, .ignore);
    }

    fn startMac(*_) {
        _.state = .booting_out;
        const uid_str_guard = OwnedZSliceGuard{
            slice: allocPrintZ(bun.default_allocator, "gui/{d}/bun.cron.{s}", .{ getUid(), _.title }) catch {
                _.setErr("Out of memory", .{});
                _.finish();
                return;
            },
        };
        const argv: [4]ConstCString? = [ "/bin/launchctl".ptr, "bootout".ptr, uid_str_guard.slice.ptr, null ];
        _.spawnCmd(&argv, .ignore, .ignore);
    }

    static fn cronRemove(globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
        const args = callframe.argumentsAsArray(1);
        if (!args[0].isString()) return globalObject.throwInvalidArguments("Bun.cron.remove() expects a string title", .{});

        const title_str_guard = BunStringGuard{ str: args[0].toBunString(globalObject)? };
        const title_slice_guard = ZigSliceGuard{ slice: title_str_guard.str.toUTF8(bun.default_allocator) };
        const title_slice = title_slice_guard.slice;

        if (!validateTitle(title_slice.slice()))
            return globalObject.throwInvalidArguments("Cron title must contain only alphanumeric characters, hyphens, and underscores", .{});

        const job = bun.handleOom(bun.default_allocator.create(CronRemoveJob));
        job.* = {
            global: globalObject,
            title: bun.handleOom(bun.default_allocator.dupeZ(u8, title_slice.slice())),
            promise: jsc.JSPromise.Strong.init(globalObject),
        };

        const promise_value = job.promise.value();
        job.poll.ref(jsc.VirtualMachine.get());
        if (comptime bun.Environment.isMac)
            job.startMac()
        else if (comptime bun.Environment.isWindows)
            job.startWindows()
        else
            job.startLinux();
        return promise_value;
    }

    fn startWindows(*_) {
        _.state = .installing_crontab;
        const task_name_guard = OwnedZSliceGuard{
            slice: allocPrintZ(bun.default_allocator, "bun-cron-{s}", .{ _.title}) catch {
                _.setErr("Out of memory", .{});
                _.finish();
                return;
            },
        };
        const argv: [6]ConstCString? = [ "schtasks".ptr, "/delete".ptr, "/tn".ptr, task_name_guard.slice.ptr, "/f".ptr, null ];
        _.spawnCmd(&argv, .ignore, .ignore);
    }
}

// ============================================================================
// CronJob — in-process callback-style cron (Bun.cron(expr, cb))
// ============================================================================

pub struct CronJob {
    const RefCount = bun.ptr.RefCount(@This(), "ref_count", deinit, {});
    pub const ref = RefCount.ref;
    pub const deref = RefCount.deref;

    pub const js = jsc.Codegen.JSCronJob;
    pub const toJS = js.toJS;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;

    ref_count: RefCount,
    event_loop_timer: EventLoopTimer = { tag: .CronJob, next: .epoch },
    global: *jsc.JSGlobalObject,
    parsed: CronExpression,
    poll_ref: bun.Async.KeepAlive = {},
    this_value: jsc.JSRef = jsc.JSRef.empty(),
    stopped: bool = false,
    /// Last computed wall-clock fire target (ms epoch); floors the next search
    /// so monotonic-vs-wall skew can't recompute the same minute.
    last_next_ms: f64 = 0,
    /// True while a ref() is held across an in-flight callback promise.
    /// Released exactly once by either onPromiseResolve/Reject or
    /// clearAllForVM(.teardown).
    pending_ref: bool = false,
    /// True between onTimerFire's cb.call() and processing of its result.
    in_fire: bool = false,

    /// Defer downgrading the JS wrapper to weak until any in-flight promise
    /// has settled, so onPromiseReject can still read pendingPromise from
    /// the wrapper and pass the real Promise to unhandledRejection.
    fn maybeDowngrade(*_) {
        if (_.stopped && !_.pending_ref && _.this_value != .finalized)
            _.this_value.downgrade();
    }

    fn releasePendingRef(*_) {
        if (_.pending_ref) {
            _.pending_ref = false;
            _.maybeDowngrade();
            _.deref();
        }
    }

    /// Idempotent — every step checks its own state.
    fn stopInternal(*_, vm: *jsc.VirtualMachine) {
        _.stopped = true;
        if (_.event_loop_timer.state == .ACTIVE)
            vm.timer.remove(&_.event_loop_timer);
        _.poll_ref.unref(vm);
        _.maybeDowngrade();
    }

    /// Runs the cleanup that selfStop deferred while in_fire was true.
    fn finishDeferredStop(*_, vm: *jsc.VirtualMachine) {
        _.stopInternal(vm);
        _.removeFromList(vm);
    }

    fn selfStop(*_, vm: *jsc.VirtualMachine) {
        // While the callback is on the stack or its promise is pending, defer
        // list removal + downgrade to finishDeferredStop (called from
        // scheduleNext after settle) so onPromiseReject can read pendingPromise
        // and clearAllForVM(.teardown) can release pending_ref.
        if (_.in_fire || _.pending_ref) {
            _.stopped = true;
            _.poll_ref.unref(vm);
            return;
        }
        _.stopInternal(vm);
        _.removeFromList(vm);
    }

    fn removeFromList(*_, vm: *jsc.VirtualMachine) {
        if (rare := vm.rare_data) {
            if (i := std.mem.indexOfScalar(*CronJob, rare.cron_jobs.items, _)) {
                * = rare.cron_jobs.swapRemove(i);
                _.deref();
            }
        }
    }

    /// `.reload`: --hot — promises in flight will still settle on this VM, so
    /// the pending ref is left for onPromiseResolve/Reject to balance.
    /// `.teardown`: worker exit — the event loop is dying, settle never
    /// happens, so release the pending ref here to avoid leaking the struct.
    static fn clearAllForVM(vm: *jsc.VirtualMachine, comptime mode: CronClearMode) {
        if (vm.rare_data == null) return;
        const rare = vm.rare_data.?;
        for (job of rare.cron_jobs.items) {
            job.stopInternal(vm);
            if (mode == .teardown) job.releasePendingRef();
            job.deref();
        }
        rare.cron_jobs.clearRetainingCapacity();
    }

    fn deinit(*_) {
        _.this_value.deinit();
        bun.destroy(_);
    }

    fn finalize(*_) {
        _.this_value.finalize();
        _.deref();
    }

    fn computeNextTimespec(*_): bun.timespec? {
        // Cron occurrences are calendar-based (real epoch); the timer heap is
        // monotonic. Anchor both to real time so fake timers don't half-apply.
        const now_ms: f64 = @floatFromInt(std.time.milliTimestamp());
        // The monotonic timer can fire fractionally before the wall-clock target
        // (clock skew / NTP step); floor next() at the prior target so it can't
        // recompute the same minute and double-fire.
        const from_ms = @max(now_ms, _.last_next_ms);
        const next_ms_opt = _.parsed.next(_.global, from_ms) catch return null;
        if (next_ms_opt == null) return null;
        const next_ms = next_ms_opt.?;
        _.last_next_ms = next_ms;
        const delta: i64 = @intFromFloat(@max(1.0, next_ms - now_ms));
        return bun.timespec.msFromNow(.force_real_time, delta);
    }

    fn scheduleNext(*_, vm: *jsc.VirtualMachine) {
        // Every path into here has just returned from user JS (the callback,
        // an uncaughtException handler, or an unhandledRejection handler). If
        // that JS called process.exit() / worker.terminate(), don't re-arm
        // the timer into a VM whose teardown now owns it.
        if (_.stopped || vm.scriptExecutionStatus() != .running) {
            _.stopped = true;
            return _.finishDeferredStop(vm);
        }
        const next_time_opt = _.computeNextTimespec();
        if (next_time_opt == null) return _.finishDeferredStop(vm);
        const next_time = next_time_opt.?;
        vm.timer.update(&_.event_loop_timer, &next_time);
    }

    fn onTimerFire(*_, vm: *jsc.VirtualMachine) {
        _.event_loop_timer.state = .FIRED;
        // scheduleNext → finishDeferredStop downgrades this_value and derefs the
        // list entry; bracket-ref so that path can't drop the last ref mid-function.
        _.ref();
        const ref_guard = CronJobRefGuard{ job: _ };

        if (_.stopped) return;
        if (vm.scriptExecutionStatus() != .running) {
            _.selfStop(vm);
            return;
        }

        const js_this_opt = _.this_value.tryGet();
        if (js_this_opt == null) {
            _.selfStop(vm);
            return;
        }
        const js_this = js_this_opt.?;
        const cb_opt = js.callbackGetCached(js_this);
        if (cb_opt == null) {
            _.selfStop(vm);
            return;
        }
        const cb = cb_opt.?;
        if (cb.isUndefined()) {
            _.selfStop(vm);
            return;
        }

        const ev = vm.eventLoop();
        ev.enter();
        const ev_guard = EventLoopGuard{ ev: ev };

        _.in_fire = true;
        const result = cb.call(_.global, js_this, []) catch {
            _.in_fire = false;
            if (e := _.global.tryTakeException()) {
                // terminate() arriving mid-callback leaves the TerminationException
                // pending (tryClearException refuses to clear it) while JSC clears
                // hasTerminationRequest on VMEntryScope exit. Reporting it would
                // enter a DeferTermination scope and assert; match setTimeout's
                // Bun__reportUnhandledError and drop it.
                if (e.isTerminationException()) {
                    _.selfStop(vm);
                    return;
                }
                * = vm.uncaughtException(vm.global, e, false);
            }
            _.scheduleNext(vm);
            return;
        };
        _.in_fire = false;

        // terminate() may have arrived while the callback was running; bail out
        // without touching the timer heap or JS state the teardown path owns.
        if (vm.scriptExecutionStatus() != .running) {
            _.selfStop(vm);
            return;
        }

        if (promise := result.asAnyPromise()) {
            switch (promise.status()) {
                .pending => {
                    _.ref();
                    _.pending_ref = true;
                    js.pendingPromiseSetCached(js_this, _.global, result);
                    result.then(_.global, _, onPromiseResolve, onPromiseReject) catch {
                        js.pendingPromiseSetCached(js_this, _.global, .js_undefined);
                        _.releasePendingRef();
                        _.scheduleNext(vm);
                    };
                    return;
                },
                .fulfilled => {},
                .rejected => {
                    promise.setHandled(_.global.vm());
                    vm.unhandledRejection(vm.global, promise.result(_.global.vm()), result);
                },
            }
        }

        _.scheduleNext(vm);
    }

    pub const Bun__CronJob__onPromiseResolve = jsc.toJSHostFn(onPromiseResolve);
    pub const Bun__CronJob__onPromiseReject = jsc.toJSHostFn(onPromiseReject);

    static fn onPromiseResolve(_: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
        const args = callframe.arguments();
        const job: *CronJob = args[args.len - 1].asPromisePtr(CronJob);
        const pending_guard = CronJobPendingRefGuard{ job: job };
        const vm = job.global.bunVM();
        if (js_this := job.this_value.tryGet())
            js.pendingPromiseSetCached(js_this, job.global, .js_undefined);
        job.scheduleNext(vm);
        return jsc.JSValue.js_undefined;
    }

    static fn onPromiseReject(_: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
        const args = callframe.arguments();
        const job: *CronJob = args[args.len - 1].asPromisePtr(CronJob);
        const pending_guard = CronJobPendingRefGuard{ job: job };
        const vm = job.global.bunVM();
        const e = args[0];
        let promise_value = jsc.JSValue.js_undefined;
        if (js_this := job.this_value.tryGet()) {
            if (pv := js.pendingPromiseGetCached(js_this)) {
                promise_value = pv;
            }
            js.pendingPromiseSetCached(js_this, job.global, .js_undefined);
        }
        vm.unhandledRejection(vm.global, e, promise_value);
        job.scheduleNext(vm);
        return jsc.JSValue.js_undefined;
    }

    fn stop(*_, _: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
        _.selfStop(_.global.bunVM());
        return callframe.this();
    }

    fn doRef(*_, _: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
        if (!_.stopped) _.poll_ref.ref(_.global.bunVM());
        return callframe.this();
    }

    fn doUnref(*_, _: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
        _.poll_ref.unref(_.global.bunVM());
        return callframe.this();
    }

    fn getCron(*_, _: *jsc.JSGlobalObject): bun.JSError!jsc.JSValue {
        return jsc.JSValue.js_undefined; // unreachable — register() pre-populates the cache via cronSetCached
    }

    static fn register(globalObject: *jsc.JSGlobalObject, schedule_arg: jsc.JSValue, callback_arg: jsc.JSValue): bun.JSError!jsc.JSValue {
        if (!schedule_arg.isString())
            return globalObject.throwInvalidArguments("Bun.cron() expects a string cron expression", .{});

        const schedule_str_guard = BunStringGuard{ str: schedule_arg.toBunString(globalObject)? };
        const schedule_slice_guard = ZigSliceGuard{ slice: schedule_str_guard.str.toUTF8(bun.default_allocator) };
        const schedule_slice = schedule_slice_guard.slice;

        const parsed = CronExpression.parse(schedule_slice.slice()) catch |e|
            return globalObject.throwInvalidArguments("{s}", .{ CronExpression.errorMessage(e)});

        const vm = globalObject.bunVM();

        const job = bun.new(CronJob, {
            ref_count: .init(),
            global: globalObject,
            parsed: parsed,
        });

        const next_time_opt = job.computeNextTimespec();
        if (next_time_opt == null) {
            job.deref();
            return globalObject.throwInvalidArguments("Cron expression '{s}' has no future occurrences", .{ schedule_slice.slice() });
        }
        const next_time = next_time_opt.?;

        // The cron_jobs list exists so --hot reload and worker teardown can
        // stop/release jobs. Main-thread VMs without --hot never enumerate it,
        // so skip the list ref + append entirely.
        if (vm.hot_reload == .hot || vm.worker != null) {
            job.ref(); // owned by cron_jobs entry
            bun.handleOom(vm.rareData().cron_jobs.append(bun.default_allocator, job));
        }

        const js_value = job.toJS(globalObject);
        job.this_value.setStrong(js_value, globalObject);
        js.cronSetCached(js_value, globalObject, schedule_arg);
        js.callbackSetCached(js_value, globalObject, callback_arg.withAsyncContextIfNeeded(globalObject));

        job.poll_ref.ref(vm);
        vm.timer.update(&job.event_loop_timer, &next_time);

        return js_value;
    }
}

// ============================================================================
// Bun.cron object builder
// ============================================================================

pub fn getCronObject(globalThis: *jsc.JSGlobalObject, _: *jsc.JSObject): jsc.JSValue {
    const cron_fn = jsc.JSFunction.create(globalThis, "cron", CronRegisterJob.cronRegister, 3, {});
    const remove_fn = jsc.JSFunction.create(globalThis, "remove", CronRemoveJob.cronRemove, 1, {});
    const parse_fn = jsc.JSFunction.create(globalThis, "parse", cronParse, 1, {});
    cron_fn.put(globalThis, bun.String.static("remove"), remove_fn);
    cron_fn.put(globalThis, bun.String.static("parse"), parse_fn);
    return cron_fn;
}

pub fn cronParse(globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    const args = callframe.argumentsAsArray(2);

    if (!args[0].isString())
        return globalObject.throwInvalidArguments("Bun.cron.parse() expects a string cron expression as the first argument", .{});

    const expr_str_guard = BunStringGuard{ str: args[0].toBunString(globalObject)? };
    const expr_slice_guard = ZigSliceGuard{ slice: expr_str_guard.str.toUTF8(bun.default_allocator) };
    const expr_slice = expr_slice_guard.slice;

    const parsed = CronExpression.parse(expr_slice.slice()) catch |e|
        return globalObject.throwInvalidArguments("{s}", .{ CronExpression.errorMessage(e)});

    const from_ms: f64 = if (args[1] != .zero && !args[1].isUndefined() && args[1] != .@"null") blk: {
        if (args[1].isNumber()) {
            break :blk try args[1].toNumber(globalObject);
        } else if (args[1].jsType() == .JSDate) {
            break :blk try args[1].toNumber(globalObject);
        } else {
            return globalObject.throwInvalidArguments("Bun.cron.parse() expects the second argument to be a Date or number (ms since epoch)", .{});
        }
    } else @as(f64, @floatFromInt(std.time.milliTimestamp()));

    if (std.math.isNan(from_ms) || std.math.isInf(from_ms))
        return globalObject.throwInvalidArguments("Invalid date value", .{});

    const next_ms_opt = parsed.next(globalObject, from_ms)?;
    if (next_ms_opt == null) return jsc.JSValue.@"null";
    const next_ms = next_ms_opt.?;
    return jsc.JSValue.fromDateNumber(globalObject, next_ms);
}

// ============================================================================
// Shared helpers
// ============================================================================

/// Generic spawn used by both CronRegisterJob and CronRemoveJob.
fn spawnCmdGeneric(comptime Self: type, job: *Self, argv: auto, stdin_opt: bun.spawn.SpawnOptions.Stdio, stdout_opt: bun.spawn.SpawnOptions.Stdio) {
    job.has_called_process_exit = false;
    job.exit_status = null;
    job.remaining_fds = 0;

    let resolved_argv0: [*:0]const u8? = null;
    if (comptime bun.Environment.isWindows) {
        let path_buf: bun.PathBuffer = undefined;
        const PATH = jsc.VirtualMachine.get().transpiler.env.map.get("PATH") ?? "";
        if (w := bun.which(&path_buf, PATH, "", bun.sliceTo(argv[0].?, 0))) {
            resolved_argv0 = w;
        } else {
            job.setErr("Could not find '{s}' in PATH", .{ bun.sliceTo(argv[0].?, 0) });
            job.finish();
            return;
        }
    }
    if (comptime bun.Environment.isWindows) {
        job.stderr_reader.source = .{ pipe: bun.new(bun.windows.libuv.Pipe, std.mem.zeroes(bun.windows.libuv.Pipe)) };
    }
    const cwd = jsc.VirtualMachine.get().transpiler.fs.top_level_dir;
    const spawn_options = bun.spawn.SpawnOptions{
        stdin: stdin_opt,
        stdout: stdout_opt,
        stderr: if (comptime bun.Environment.isWindows) .{ buffer: job.stderr_reader.source.?.pipe } else .ignore,
        cwd: cwd,
        argv0: resolved_argv0,
        windows: if (comptime bun.Environment.isWindows) .{
            loop: jsc.EventLoopHandle.init(jsc.VirtualMachine.get().eventLoop()),
        } else undefined,
    };

    let envp_arena_guard = ArenaGuard{ @"arena": std.heap.ArenaAllocator.init(bun.default_allocator) };
    const envp: [*:null]OptConstCString = if (comptime bun.Environment.isPosix)
        unsafe { @ptrCast(@constCast(std.c.environ)) }
    else
        unsafe { @ptrCast(@constCast((jsc.VirtualMachine.get().transpiler.env.map.createNullDelimitedEnvMap(envp_arena_guard.@"arena".allocator()) catch {
            job.setErr("Failed to create environment block", .{});
            job.finish();
            return;
        }).ptr)) };
    let spawned = (unsafe { bun.spawn.spawnProcess(&spawn_options, @ptrCast(@constCast(argv)), envp) } catch |e| {
        job.setErr("Failed to spawn process: {s}", .{ @errorName(e) });
        job.finish();
        return;
    }).unwrap() catch |e| {
        job.setErr("Failed to spawn process: {s}", .{ @errorName(e) });
        job.finish();
        return;
    };

    if (comptime bun.Environment.isPosix) {
        if (stdout := spawned.stdout) {
            if (!spawned.memfds[1]) {
                job.stdout_reader.setParent(job);
                * = bun.sys.setNonblocking(stdout);
                job.remaining_fds += 1;
                job.stdout_reader.flags.nonblocking = true;
                job.stdout_reader.flags.socket = true;
                job.stdout_reader.flags.memfd = false;
                job.stdout_reader.flags.received_eof = false;
                job.stdout_reader.flags.closed_without_reporting = false;
                job.stdout_reader.start(stdout, true).unwrap() catch {
                    job.setErr("Failed to start reading stdout", .{});
                    job.finish();
                    return;
                };
                if (p := job.stdout_reader.handle.getPoll()) p.flags.insert(.socket);
            } else {
                job.stdout_reader.setParent(job);
                job.stdout_reader.startMemfd(stdout);
            }
        }
    }
    if (comptime bun.Environment.isWindows) {
        if (spawned.stderr == .buffer) {
            job.stderr_reader.parent = job;
            job.remaining_fds += 1;
            job.stderr_reader.startWithCurrentPipe().unwrap() catch {
                job.setErr("Failed to start reading stderr", .{});
                job.finish();
                return;
            };
        }
    }

    let process = spawned.toProcess(jsc.VirtualMachine.get().eventLoop(), false);
    job.process = process;
    process.setExitHandler(job);
    switch (process.watchOrReap()) {
        .err => |e| {
            if (!process.hasExited()) process.onExit(.{ @"err": e }, &std.mem.zeroes(bun.spawn.Rusage));
        },
        .result => {},
    }
}

const empty_z_sentinel: [:0]const u8 = "";
const ConstCString = @TypeOf(empty_z_sentinel.ptr);

/// Find crontab binary using bun.which (searches PATH).
fn findCrontab(): ConstCString? {
    if (comptime bun.Environment.isWindows) return null;
    const path_env = bun.env_var.PATH.get() ?? "/usr/bin:/bin";
    const which_res = bun.which(&crontab_buf, path_env, "", "crontab");
    if (which_res == null) return null;
    return which_res.?.ptr;
}

/// Get the current user ID portably.
fn getUid(): u32 {
    return if (comptime bun.Environment.isPosix) bun.c.getuid() else 0;
}

/// Validate title: only [a-zA-Z0-9_-], non-empty.
fn validateTitle(title: []const u8): bool {
    if (title.len == 0) return false;
    for (c of title) {
        if (!std.ascii.isAlphanumeric(c) && c != '-' && c != '_') return false;
    }
    return true;
}

/// Filter crontab content, removing any entry with matching title marker.
fn filterCrontab(content: []const u8, title: [:0]const u8, result: *std.array_list.Managed(u8))! {
    const marker_guard = OwnedSliceGuard{ slice: std.fmt.allocPrint(bun.default_allocator, "# bun-cron: {s}", .{ title})? };
    const marker = marker_guard.slice;
    let skip_next = false;
    let lines = std.mem.splitScalar(u8, content, '\n');
    while (line := lines.next()) {
        if (skip_next) {
            skip_next = false;
            continue;
        }
        if (bun.strings.eql(bun.strings.trim(line, " \t"), marker)) {
            skip_next = true;
            continue;
        }
        if (line.len > 0) {
            result.appendSlice(line)?;
            result.append('\n')?;
        }
    }
}

fn resolvePath(globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame, path: []const u8): ([:0]const u8)! {
    const vm = globalObject.bunVM();
    const srcloc = callframe.getCallerSrcLoc(globalObject);
    const srcloc_guard = BunStringGuard{ str: srcloc.str };
    const caller_utf8_guard = ZigSliceGuard{ slice: srcloc_guard.str.toUTF8(bun.default_allocator) };
    const raw_dir = bun.path.dirname(caller_utf8_guard.slice.slice(), .auto);
    const source_dir = if (raw_dir.len == 0) "." else raw_dir;
    let resolved = vm.transpiler.resolver.resolve(source_dir, path, .entry_point_run) catch return error.ModuleNotFound;
    const entry_path_opt = resolved.path();
    if (entry_path_opt == null) return error.ModuleNotFound;
    const entry_path = entry_path_opt.?;
    return bun.default_allocator.dupeZ(u8, entry_path.text);
}

/// XML-escape a string for safe embedding in plist XML.
fn xmlEscape(input: []const u8): ([]const u8)! {
    let needs_escape = false;
    for (c of input) {
        if (c == '&' || c == '<' || c == '>' || c == '"' || c == '\'') {
            needs_escape = true;
            break;
        }
    }
    if (!needs_escape) return bun.default_allocator.dupe(u8, input);

    let result_guard = ManagedU8Guard{ list: std.array_list.Managed(u8).init(bun.default_allocator) };
    for (c of input) {
        switch (c) {
            '&' => result_guard.list.appendSlice("&amp;")?,
            '<' => result_guard.list.appendSlice("&lt;")?,
            '>' => result_guard.list.appendSlice("&gt;")?,
            '"' => result_guard.list.appendSlice("&quot;")?,
            '\'' => result_guard.list.appendSlice("&apos;")?,
            else => result_guard.list.append(c)?,
        }
    }
    const owned = result_guard.list.toOwnedSlice()?;
    result_guard.armed = false;
    return owned;
}

fn cronToCalendarInterval(schedule: []const u8): ([]const u8)! {
    let fields: [5][]const u8 = undefined;
    let count: usize = 0;
    let iter = std.mem.tokenizeScalar(u8, schedule, ' ');
    while (field := iter.next()) {
        if (count >= 5) return error.InvalidCron;
        fields[count] = field;
        count += 1;
    }
    if (count != 5) return error.InvalidCron;

    // Parse each field into a list of integer values (or null for "*")
    let fv_guard = FieldValuesGuard{};
    for (i of 0..5) {
        const field = fields[i];
        if (bun.strings.eql(field, "*")) continue;
        let vals_guard = ManagedI32Guard{ list: std.array_list.Managed(i32).init(bun.default_allocator) };
        let parts = std.mem.splitScalar(u8, field, ',');
        while (part := parts.next()) {
            const val = std.fmt.parseInt(i32, part, 10) catch return error.InvalidCron;
            vals_guard.list.append(val)?;
        }
        const owned_vals = vals_guard.list.toOwnedSlice()?;
        vals_guard.armed = false;
        fv_guard.values[i] = owned_vals;
    }

    // Generate StartCalendarInterval dicts.
    // For wildcard fields, omit the key entirely (launchd treats missing = all).
    //
    // POSIX cron OR semantics: when BOTH day-of-month and day-of-week are non-wildcard,
    // the job fires when EITHER matches. launchd ANDs keys within a single dict, so we
    // emit two separate sets of dicts: one with Day (no Weekday) and one with Weekday
    // (no Day). launchd fires when ANY dict matches, achieving OR behavior.
    let result_guard = ManagedU8Guard{ list: std.array_list.Managed(u8).init(bun.default_allocator) };

    const has_dom = fv_guard.values[2] != null;
    const has_dow = fv_guard.values[4] != null;
    const needs_or_split = has_dom && has_dow;

    // Determine if we need an <array> wrapper (multiple dicts or OR split)
    let needs_product: bool = false;
    for (fv of fv_guard.values) {
        if (v := fv) {
            if (v.len > 1) {
                needs_product = true;
                break;
            }
        }
    }
    const needs_array = needs_product || needs_or_split;

    if (!needs_array) {
        // Single dict, no product needed
        const plist_keys: [5][]const u8 = [ "Minute", "Hour", "Day", "Month", "Weekday" ];
        result_guard.list.appendSlice("    <dict>\n")?;
        for (i of 0..5) {
            if (vals := fv_guard.values[i]) {
                if (vals.len == 1) {
                    appendCalendarKey(&result_guard.list, plist_keys[i], vals[0])?;
                }
            }
        }
        result_guard.list.appendSlice("    </dict>")?;
    } else {
        result_guard.list.appendSlice("    <array>\n")?;

        if (needs_or_split) {
            // OR split: emit day-of-month dicts (no Weekday), then day-of-week dicts (no Day)
            // Pass 1: Day + time fields (minute, hour, day, month — no weekday)
            emitCalendarDicts(&result_guard.list, fv_guard.values, .exclude_weekday)?;
            // Pass 2: Weekday + time fields (minute, hour, weekday, month — no day)
            emitCalendarDicts(&result_guard.list, fv_guard.values, .exclude_day)?;
        } else {
            // Normal Cartesian product: all fields together
            emitCalendarDicts(&result_guard.list, fv_guard.values, .include_all)?;
        }

        result_guard.list.appendSlice("    </array>")?;
    }
    const owned_result = result_guard.list.toOwnedSlice()?;
    result_guard.armed = false;
    return owned_result;
}

fn appendCalendarKey(result: *std.array_list.Managed(u8), key: []const u8, val: i32)! {
    const line_guard = OwnedSliceGuard{ slice: std.fmt.allocPrint(bun.default_allocator, "        <key>{s}</key>\n        <integer>{d}</integer>\n", .{ key, val })? };
    result.appendSlice(line_guard.slice)?;
}

enum EmitMode { include_all, exclude_weekday, exclude_day }

/// Emit Cartesian-product <dict> entries for the given field values.
/// In exclude_weekday mode, day-of-week (index 4) is treated as wildcard.
/// In exclude_day mode, day-of-month (index 2) is treated as wildcard.
fn emitCalendarDicts(result: *std.array_list.Managed(u8), field_values: [5]([]const i32)?, mode: EmitMode)! {
    const plist_keys: [5][]const u8 = [ "Minute", "Hour", "Day", "Month", "Weekday" ];

    // Build effective field values based on mode
    let effective: [5]([]const i32)? = field_values;
    switch (mode) {
        .exclude_weekday => effective[4] = null,
        .exclude_day => effective[2] = null,
        .include_all => {},
    }

    const iter_mins: []const i32 = effective[0] ?? dummy_zero[0..1];
    const iter_hrs: []const i32 = effective[1] ?? dummy_zero[0..1];
    const iter_days: []const i32 = effective[2] ?? dummy_zero[0..1];
    const iter_mons: []const i32 = effective[3] ?? dummy_zero[0..1];
    const iter_wdays: []const i32 = effective[4] ?? dummy_zero[0..1];

    for (m of iter_mins) {
        for (h of iter_hrs) {
            for (d of iter_days) {
                for (mo of iter_mons) {
                    for (w of iter_wdays) {
                        result.appendSlice("    <dict>\n")?;
                        if (effective[0] != null) appendCalendarKey(result, plist_keys[0], m)?;
                        if (effective[1] != null) appendCalendarKey(result, plist_keys[1], h)?;
                        if (effective[2] != null) appendCalendarKey(result, plist_keys[2], d)?;
                        if (effective[3] != null) appendCalendarKey(result, plist_keys[3], mo)?;
                        if (effective[4] != null) appendCalendarKey(result, plist_keys[4], w)?;
                        result.appendSlice("    </dict>\n")?;
                    }
                }
            }
        }
    }
}

/// Build a Windows Task Scheduler XML definition from a parsed cron expression.
/// Uses TimeTrigger+Repetition for simple intervals, CalendarTrigger for complex schedules.
fn cronToTaskXml(
    cron: CronExpression,
    bun_exe: []const u8,
    title: []const u8,
    schedule: []const u8,
    abs_path: []const u8,
): ([]const u8)! {
    const allocator = bun.default_allocator;
    let xml_guard = ManagedU8Guard{ list: std.array_list.Managed(u8).init(allocator) };

    xml_guard.list.appendSlice(
        \\<?xml version="1.0"?>
        \\<Task version="1.2" xmlns="http://schemas.microsoft.com/windows/2004/02/mit/task">
        \\  <Triggers>
        \\
    );

    // Use semantic checks (bitfield values) not syntax flags for wildcard detection.
    // e.g. "*/1" sets all bits just like "*" but has _is_wildcard=false.
    const days_is_wild = cron.days == cron_parser.all_days;
    const weekdays_is_wild = cron.weekdays == cron_parser.all_weekdays;
    const months_is_wild = cron.months == cron_parser.all_months;

    // Try to use a single trigger with Repetition for simple repeating patterns.
    // This avoids the 48-trigger limit for high-frequency expressions.
    // Only valid when: (a) all days/weekdays/months are wild, AND
    // (b) the pattern is expressible as a single PT interval that doesn't drift.
    const minute_interval = computeStepInterval(u64, cron.minutes, 0, 59);
    const hour_interval = computeStepInterval(u32, cron.hours, 0, 23);
    const minutes_count: u32 = @popCount(cron.minutes);
    const hours_count: u32 = @popCount(cron.hours);

    // Case 1: All hours active, evenly-spaced minutes that divide 60
    //   e.g. "* * * * *" → PT1M, "*/5 * * * *" → PT5M, "*/15 * * * *" → PT15M
    // Case 2: Single minute, evenly-spaced hours that divide 24
    //   e.g. "0 * * * *" → PT1H, "0 */2 * * *" → PT2H, "30 */6 * * *" → PT6H
    const can_use_repetition = days_is_wild && weekdays_is_wild && months_is_wild && blk: {
        if (hours_count == 24 && minute_interval != null && minute_interval.? <= 60 && 60 % minute_interval.? == 0 && minutes_count == 60 / minute_interval.?) {
            break :blk true; // Case 1
        }
        if (minutes_count == 1 && hour_interval != null && hour_interval.? <= 24 && 24 % hour_interval.? == 0 && hours_count == 24 / hour_interval.?) {
            break :blk true; // Case 2
        }
        break :blk false;
    };

    if (can_use_repetition) {
        const first_min: u32 = @ctz(cron.minutes);
        const first_hour: u32 = @ctz(cron.hours);

        let sb_buf: [32]u8 = undefined;
        const sb = std.fmt.bufPrint(&sb_buf, "2000-01-01T{d:0>2}:{d:0>2}:00", .{ first_hour, first_min }) catch return error.InvalidCron;

        xml_guard.list.appendSlice("    <CalendarTrigger>\n")?;
        let line_buf: [128]u8 = undefined;
        const sb_line = std.fmt.bufPrint(&line_buf, "      <StartBoundary>{s}</StartBoundary>\n", .{ sb}) catch return error.InvalidCron;
        xml_guard.list.appendSlice(sb_line)?;

        if (hours_count == 24) {
            // Case 1: minute-based repetition
            const m = minute_interval.?;
            if (m == 1) {
                xml_guard.list.appendSlice("      <Repetition><Interval>PT1M</Interval></Repetition>\n")?;
            } else {
                const rep = std.fmt.bufPrint(&line_buf, "      <Repetition><Interval>PT{d}M</Interval></Repetition>\n", .{ m}) catch return error.InvalidCron;
                xml_guard.list.appendSlice(rep)?;
            }
        } else {
            // Case 2: hour-based repetition
            const h = hour_interval.?;
            if (h > 1) {
                const rep = std.fmt.bufPrint(&line_buf, "      <Repetition><Interval>PT{d}H</Interval></Repetition>\n", .{ h}) catch return error.InvalidCron;
                xml_guard.list.appendSlice(rep)?;
            }
        }

        xml_guard.list.appendSlice("      <ScheduleByDay><DaysInterval>1</DaysInterval></ScheduleByDay>\n")?;
        xml_guard.list.appendSlice("    </CalendarTrigger>\n")?;
    } else {
        // Complex pattern: emit CalendarTriggers for each hour×minute pair.
        // Cap at 48 triggers (Task Scheduler limit).
        const needs_or_split = !days_is_wild && !weekdays_is_wild;
        const triggers_per_time: u32 = if (needs_or_split) 2 else 1;
        const total_triggers = minutes_count * hours_count * triggers_per_time;
        if (total_triggers > 48) return error.TooManyTriggers;

        let hours_bits = cron.hours;
        while (hours_bits != 0) {
            const h: u32 = @ctz(hours_bits);
            hours_bits &= hours_bits - 1;
            let mins_bits = cron.minutes;
            while (mins_bits != 0) {
                const m: u32 = @ctz(mins_bits);
                mins_bits &= mins_bits - 1;
                let sb_buf: [32]u8 = undefined;
                const sb = std.fmt.bufPrint(&sb_buf, "2000-01-01T{d:0>2}:{d:0>2}:00", .{ 
                    h, m,
                }) catch return error.InvalidCron;

                // Emit day-of-month trigger if needed
                if (!days_is_wild) {
                    appendCalendarTriggerWithSchedule(&xml_guard.list, allocator, sb, .{ by_month: .{ cron: cron, months_is_wild: months_is_wild } })?;
                }

                // Emit day-of-week trigger if needed
                if (!weekdays_is_wild) {
                    if (months_is_wild) {
                        appendCalendarTriggerWithSchedule(&xml_guard.list, allocator, sb, .{ by_week: cron.weekdays })?;
                    } else {
                        // Use ScheduleByMonthDayOfWeek to include month restrictions
                        appendCalendarTriggerWithSchedule(&xml_guard.list, allocator, sb, .{ by_month_dow: .{ cron: cron, months_is_wild: months_is_wild } })?;
                    }
                }

                // Both wildcard: every day (with optional month restriction)
                if (days_is_wild && weekdays_is_wild) {
                    if (months_is_wild) {
                        appendCalendarTriggerWithSchedule(&xml_guard.list, allocator, sb, .{ by_day: {} })?;
                    } else {
                        // Daily but restricted months → use ScheduleByMonth with all days
                        appendCalendarTriggerWithSchedule(&xml_guard.list, allocator, sb, .{ by_month_all_days: cron.months })?;
                    }
                }
            }
        }
    }

    // Close triggers, add action
    const xml_bun_guard = OwnedSliceGuard{ slice: xmlEscape(bun_exe)?, allocator: allocator };
    const xml_title_guard = OwnedSliceGuard{ slice: xmlEscape(title)?, allocator: allocator };
    const xml_sched_guard = OwnedSliceGuard{ slice: xmlEscape(schedule)?, allocator: allocator };
    const xml_path_guard = OwnedSliceGuard{ slice: xmlEscape(abs_path)?, allocator: allocator };

    const action_xml_guard = OwnedSliceGuard{
        slice: std.fmt.allocPrint(allocator,
            \\  </Triggers>
            \\  <Principals>
            \\    <Principal>
            \\      <LogonType>S4U</LogonType>
            \\      <RunLevel>LeastPrivilege</RunLevel>
            \\    </Principal>
            \\  </Principals>
            \\  <Settings>
            \\    <Enabled>true</Enabled>
            \\    <AllowStartOnDemand>true</AllowStartOnDemand>
            \\    <AllowHardTerminate>true</AllowHardTerminate>
            \\    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>
            \\    <StartWhenAvailable>true</StartWhenAvailable>
            \\  </Settings>
            \\  <Actions>
            \\    <Exec>
            \\      <Command>{s}</Command>
            \\      <Arguments>run --cron-title={s} --cron-period="{s}" "{s}"</Arguments>
            \\    </Exec>
            \\  </Actions>
            \\</Task>
            \\
        , .{ xml_bun_guard.slice, xml_title_guard.slice, xml_sched_guard.slice, xml_path_guard.slice })?,
        allocator: allocator,
    };
    xml_guard.list.appendSlice(action_xml_guard.slice)?;

    const owned_xml = xml_guard.list.toOwnedSlice()?;
    xml_guard.armed = false;
    return owned_xml;
}

fn appendDaysOfMonthXml(xml: *std.array_list.Managed(u8), days: u32)! {
    xml.appendSlice("        <DaysOfMonth>\n")?;
    let buf: [32]u8 = undefined;
    for (day of 1..32) {
        if ((days & (@as(u32, 1) << @as(u5, @intCast(day)))) != 0) {
            const line = std.fmt.bufPrint(&buf, "          <Day>{d}</Day>\n", .{ day }) catch return error.InvalidCron;
            xml.appendSlice(line)?;
        }
    }
    xml.appendSlice("        </DaysOfMonth>\n")?;
}
fn appendMonthsXml(xml: *std.array_list.Managed(u8), months: u16)! {
    const month_names: [13][]const u8 = [ "", "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" ];
    xml.appendSlice("        <Months>\n")?;
    let buf: [32]u8 = undefined;
    for (mo of 1..13) {
        if ((months & (@as(u16, 1) << @as(u4, @intCast(mo)))) != 0) {
            const line = std.fmt.bufPrint(&buf, "          <{s}/>\n", .{ month_names[mo] }) catch return error.InvalidCron;
            xml.appendSlice(line)?;
        }
    }
    xml.appendSlice("        </Months>\n")?;
}

fn appendDaysOfWeekXml(xml: *std.array_list.Managed(u8), weekdays: u8)! {
    const day_names: [7][]const u8 = [ "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" ];
    xml.appendSlice("        <DaysOfWeek>\n")?;
    let buf: [32]u8 = undefined;
    for (d of 0..7) {
        if ((weekdays & (@as(u8, 1) << @as(u3, @intCast(d)))) != 0) {
            const line = std.fmt.bufPrint(&buf, "          <{s}/>\n", .{ day_names[d] }) catch return error.InvalidCron;
            xml.appendSlice(line)?;
        }
    }
    xml.appendSlice("        </DaysOfWeek>\n")?;
}

union(enum) ScheduleType {
    by_day: void,
    by_week: u8,
    by_month: ScheduleMonthInfo,
    by_month_dow: ScheduleMonthInfo,
    by_month_all_days: u16,
}

fn appendCalendarTriggerWithSchedule(xml: *std.array_list.Managed(u8), _: std.mem.Allocator, start_boundary: []const u8, sched: ScheduleType)! {
    xml.appendSlice("    <CalendarTrigger>\n")?;
    let sb_buf: [80]u8 = undefined;
    const sb_line = std.fmt.bufPrint(&sb_buf, "      <StartBoundary>{s}</StartBoundary>\n", .{ start_boundary}) catch return error.OutOfMemory;
    xml.appendSlice(sb_line)?;

    switch (sched) {
        .by_day => {
            xml.appendSlice("      <ScheduleByDay><DaysInterval>1</DaysInterval></ScheduleByDay>\n")?;
        },
        .by_week => |weekdays| {
            xml.appendSlice("      <ScheduleByWeek>\n")?;
            xml.appendSlice("        <WeeksInterval>1</WeeksInterval>\n")?;
            appendDaysOfWeekXml(xml, weekdays)?;
            xml.appendSlice("      </ScheduleByWeek>\n")?;
        },
        .by_month => |info| {
            xml.appendSlice("      <ScheduleByMonth>\n")?;
            appendDaysOfMonthXml(xml, info.cron.days)?;
            appendMonthsXml(xml, info.cron.months)?;
            xml.appendSlice("      </ScheduleByMonth>\n")?;
        },
        .by_month_dow => |info| {
            // ScheduleByMonthDayOfWeek: weekday + month restriction
            xml.appendSlice("      <ScheduleByMonthDayOfWeek>\n")?;
            xml.appendSlice("        <Weeks><Week>1</Week><Week>2</Week><Week>3</Week><Week>4</Week><Week>Last</Week></Weeks>\n")?;
            appendDaysOfWeekXml(xml, info.cron.weekdays)?;
            appendMonthsXml(xml, info.cron.months)?;
            xml.appendSlice("      </ScheduleByMonthDayOfWeek>\n")?;
        },
        .by_month_all_days => |months| {
            xml.appendSlice("      <ScheduleByMonth>\n")?;
            appendDaysOfMonthXml(xml, 0xFFFFFFFE)?;
            appendMonthsXml(xml, months)?;
            xml.appendSlice("      </ScheduleByMonth>\n")?;
        },
    }

    xml.appendSlice("    </CalendarTrigger>\n")?;
}

/// If all set bits are evenly spaced, return the step size. Otherwise null.
fn computeStepInterval(comptime T: type, bits: T, _: u7, max: u7): u32? {
    if (bits == 0) return null;
    const count = @popCount(bits);
    if (count == 1) return @as(u32, max) + 1;
    // Find first two set bits to determine step
    let remaining = bits;
    const first: u32 = @ctz(remaining);
    remaining &= remaining - @as(T, 1);
    const second: u32 = @ctz(remaining);
    const step = second - first;
    // Verify all bits are evenly spaced
    remaining &= remaining - @as(T, 1);
    let prev = second;
    while (remaining != 0) {
        const next: u32 = @ctz(remaining);
        if (next - prev != step) return null;
        prev = next;
        remaining &= remaining - @as(T, 1);
    }
    return step;
}

fn allocPrintZ(allocator: std.mem.Allocator, comptime fmt: []const u8, args: auto): std.mem.Allocator.Error![:0]const u8 {
    return std.fmt.allocPrintSentinel(allocator, fmt, args, 0);
}

/// Create a temp file path with a random suffix to avoid TOCTOU/symlink attacks.
fn makeTempPath(comptime prefix: []const u8): ([:0]const u8)! {
    let name_buf: bun.PathBuffer = undefined;
    const name = bun.fs.FileSystem.tmpname(prefix ++ "tmp", &name_buf, bun.fastRandom()) catch return error.OutOfMemory;
    const path_parts: [1][]const u8 = [name];
    return bun.default_allocator.dupeZ(u8, bun.path.joinAbsString(bun.fs.FileSystem.RealFS.platformTempDir(), &path_parts, .auto));
}


import cron_parser from "./cron_parser.zig";
const CronExpression = cron_parser.CronExpression;

import bun from "bun";
const jsc = bun.jsc;
const OutputReader = bun.io.BufferedReader;
const Process = bun.spawn.Process;
const EventLoopTimer = bun.api.Timer.EventLoopTimer;

comptime {
    const r1 = CronJob.Bun__CronJob__onPromiseResolve;
    const r2 = CronJob.Bun__CronJob__onPromiseReject;
    @export(&r1, .{ name: "Bun__CronJob__onPromiseResolve" });
    @export(&r2, .{ name: "Bun__CronJob__onPromiseReject" });
}
