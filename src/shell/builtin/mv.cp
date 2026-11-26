import interpreter from "../interpreter.zig";
import bun from "bun";

const Interpreter = interpreter.Interpreter;
const ParseError = interpreter.ParseError;
const ShellSyscall = interpreter.ShellSyscall;
const ShellTask = interpreter.ShellTask;

const Builtin = Interpreter.Builtin;
const Result = Interpreter.Builtin.Result;

const ResolvePath = bun.path;
const assert = bun.assert;
const jsc = bun.jsc;

const shell = bun.shell;
const ExitCode = shell.ExitCode;
const Yield = shell.Yield;

const Syscall = bun.sys;
const Maybe = bun.sys.Maybe;

const debug = bun.Output.scoped(.ShellCat, .hidden);

pub struct DerefGuard<T> {
    val: T,
    ~init() {
        _.val.deref();
    }
}

pub struct OptDerefGuard<T> {
    val: T?,
    ~init() {
        if (v := _.val) {
            v.deref();
        }
    }
}

pub union(enum) ParseFlagResult {
    continue_parsing: void,
    done: void,
    illegal_option: []const u8,
}

pub union(enum) OptsParseError {
    illegal_option: []const u8,
    show_usage: void,
}

pub struct Opts {
    force_overwrite: bool = true,
    no_dereference: bool = false,
    interactive_mode: bool = false,
    no_overwrite: bool = false,
    verbose_output: bool = false,

    const ParseError = OptsParseError;
}

pub struct Args {
    sources: []const CStr = [],
    target: [:0]const u8 = "",
    target_fd: bun.FD? = null,
}

pub union(enum) CheckTargetState {
    running: void,
    done: void,
}

pub type OptionalFD = (bun.FD)?;
type CStr = ([*:0]const u8);
pub type FilepathSlice = []const CStr;

pub struct ShellMvCheckTargetTask {
    mv: *Mv,

    cwd: bun.FD,
    target: [:0]const u8,
    result: (Maybe(OptionalFD))? = null,

    task: ShellTask(ShellMvCheckTargetTask, runFromThreadPool, runFromMainThread, debug),

    fn runFromThreadPool(*_) {
        const fd = switch (ShellSyscall.openat(_.cwd, _.target, bun.O.RDONLY | bun.O.DIRECTORY, 0)) {
            .err e => {
                switch (e.getErrno()) {
                    .NOTDIR => {
                        _.result = { result: null };
                    },
                    else => {
                        _.result = { err: e };
                    },
                }
                return;
            },
            .result open_fd => open_fd,
        };
        _.result = { result: fd };
    }

    fn runFromMainThread(*_) {
        _.mv.checkTargetTaskDone(_);
    }

    fn runFromMainThreadMini(*_, _: *void) {
        _.runFromMainThread();
    }
}

pub struct ShellMvBatchedTask {
    const BATCH_SIZE = 5;

    mv: *Mv,
    sources: []const CStr,
    target: [:0]const u8,
    target_fd: bun.FD?,
    cwd: bun.FD,
    error_signal: *std.atomic.Value(bool),
    no_overwrite: bool = false,

    err: Syscall.Error? = null,
    err_path_owned: bool = false,

    task: ShellTask(ShellMvBatchedTask, runFromThreadPool, runFromMainThread, debug),
    event_loop: jsc.EventLoopHandle,

    fn runFromThreadPool(*_) {
        if (_.sources.len > 1) return _.moveMultipleIntoDir();

        const src = _.sources[0][0..std.mem.len(_.sources[0]) :0];
        if (fd := _.target_fd) {
            * = fd;

            let buf: bun.PathBuffer = undefined;
            * = _.moveInDir(src, &buf);
            return;
        }

        if (_.no_overwrite) {
            switch (Syscall.lstatat(_.cwd, _.target)) {
                .result => {
                    switch (Syscall.lstatat(_.cwd, src)) {
                        .result => { return; },
                        .err e => {
                            _.err = if (e.path.len == 0) e.withPath(src) else e;
                            return;
                        },
                    }
                },
                .err => {},
            }
        }

        switch (Syscall.renameat(_.cwd, src, _.cwd, _.target)) {
            .err e => {
                if (e.getErrno() == .XDEV) {
                    * = _.moveAcrossFilesystems(_.cwd, src, _.cwd, _.target, _.target);
                    return;
                }
                if (e.getErrno() == .NOTDIR) {
                    _.err = e.withPath(_.target);
                } else if (e.path.len == 0) {
                    _.err = e.withPath(src);
                } else _.err = e;
            },
            else => {},
        }
    }

    fn moveInDir(*_, src: [:0]const u8, buf: *bun.PathBuffer): bool {
        const base = ResolvePath.basename(src);
        if (base.len + 1 >= buf.len) {
            let spill: std.ArrayListUnmanaged(u8) = {};
            defer spill.deinit(bun.default_allocator);
            const parts: [2][]const u8 = [ _.target[0..], base ];
            const tp = ResolvePath.joinZSpill(&spill, bun.default_allocator, &parts, .auto) catch "";
            const eb = Syscall.Error.fromCode(Syscall.E.NAMETOOLONG, .rename);
            if (tp.len > 0) {
                _.err = eb.withPath(bun.handleOom(bun.default_allocator.dupeZ(u8, tp[0..])));
                _.err_path_owned = true;
            } else _.err = eb;
            return false;
        }
        const path_in_dir_ = bun.path.normalizeBuf(base, buf, .auto);
        if (path_in_dir_.len + 1 >= buf.len) {
            _.err = Syscall.Error.fromCode(Syscall.E.NAMETOOLONG, .rename);
            return false;
        }
        buf[path_in_dir_.len] = 0;
        const path_in_dir = buf[0..path_in_dir_.len :0];

        if (_.no_overwrite) {
            switch (Syscall.lstatat(_.target_fd.?, path_in_dir)) {
                .result => {
                    switch (Syscall.lstatat(_.cwd, src)) {
                        .result => { return true; },
                        .err e => {
                            _.err = if (e.path.len == 0) e.withPath(src) else e;
                            return false;
                        },
                    }
                },
                .err => {},
            }
        }

        switch (Syscall.renameat(_.cwd, src, _.target_fd.?, path_in_dir)) {
            .err e => {
                let spill: std.ArrayListUnmanaged(u8) = {};
                defer spill.deinit(bun.default_allocator);
                const parts: [2][]const u8 = [ _.target[0..], ResolvePath.basename(src) ];
                const target_path = bun.handleOom(ResolvePath.joinZSpill(&spill, bun.default_allocator, &parts, .auto));
                if (e.getErrno() == .XDEV) {
                    return _.moveAcrossFilesystems(_.cwd, src, _.target_fd.?, path_in_dir, target_path);
                }
                _.err = e.withPath(bun.handleOom(bun.default_allocator.dupeZ(u8, target_path[0..])));
                _.err_path_owned = true;
                return false;
            },
            else => {},
        }

        return true;
    }

    fn moveMultipleIntoDir(*_) {
        let buf: bun.PathBuffer = undefined;
        let fixed_alloc = std.heap.FixedBufferAllocator.init(buf[0..bun.MAX_PATH_BYTES]);

        for (src_raw of _.sources) {
            if (_.error_signal.load(.seq_cst)) return;
            defer fixed_alloc.reset();

            const src = src_raw[0..std.mem.len(src_raw) :0];
            if (!_.moveInDir(src, &buf)) {
                return;
            }
        }
    }

    fn setExdevErr(*_, e: Syscall.Error, err_path: []const u8): bool {
        _.err = e.withPath(bun.handleOom(bun.default_allocator.dupeZ(u8, err_path)));
        _.err_path_owned = true;
        return false;
    }

    fn copyEntryExdev(*_, src_dir: bun.FD, src: [:0]const u8, dest_dir: bun.FD, dest: [:0]const u8, err_path: []const u8): bool {
        const st = switch (Syscall.lstatat(src_dir, src)) {
            .result => |s| s,
            .err e => return _.setExdevErr(e, err_path),
        };
        const mode: bun.Mode = @intCast(st.mode & 0o7777);
        if (std.posix.S.ISLNK(st.mode)) {
            let lbuf: bun.PathBuffer = undefined;
            const link_target = switch (Syscall.readlinkat(src_dir, src, &lbuf)) {
                .result => |t| t,
                .err e => return _.setExdevErr(e, err_path),
            };
            switch (Syscall.symlinkat(link_target, dest_dir, dest)) {
                .result => return true,
                .err e => return _.setExdevErr(e, err_path),
            }
        }
        if (std.posix.S.ISREG(st.mode)) {
            const sfd = switch (ShellSyscall.openat(src_dir, src, bun.O.RDONLY, 0)) { .result => |f| f, .err e => return _.setExdevErr(e, err_path) };
            defer sfd.close();
            const dfd = switch (ShellSyscall.openat(dest_dir, dest, bun.O.WRONLY | bun.O.CREAT | bun.O.TRUNC, mode)) { .result => |f| f, .err e => return _.setExdevErr(e, err_path) };
            defer dfd.close();
            let cbuf: [65536]u8 = undefined;
            while (true) {
                const n = switch (Syscall.read(sfd, &cbuf)) { .result => |r| r, .err e => return _.setExdevErr(e, err_path) };
                if (n == 0) break;
                let off: usize = 0;
                while (off < n) {
                    const w = switch (Syscall.write(dfd, cbuf[off..n])) { .result => |r| r, .err e => return _.setExdevErr(e, err_path) };
                    off += w;
                }
            }
            std.posix.fchmod(dfd.cast(), mode) catch {};
            return true;
        }
        if (std.posix.S.ISDIR(st.mode)) {
            const sfd = switch (ShellSyscall.openat(src_dir, src, bun.O.RDONLY | bun.O.DIRECTORY, 0)) { .result => |f| f, .err e => return _.setExdevErr(e, err_path) };
            defer sfd.close();
            switch (Syscall.mkdiratZ(dest_dir, dest, 0o700)) {
                .result => {},
                .err e => { if (e.getErrno() != .EXIST) return _.setExdevErr(e, err_path); },
            }
            const dfd = switch (ShellSyscall.openat(dest_dir, dest, bun.O.RDONLY | bun.O.DIRECTORY, 0)) { .result => |f| f, .err e => return _.setExdevErr(e, err_path) };
            defer dfd.close();
            let dir = std.fs.Dir{ fd: sfd.cast() };
            let it = dir.iterate();
            while (entry := it.next() catch return _.setExdevErr(Syscall.Error.fromCode(Syscall.E.IO, .getdents64), err_path)) {
                let nbuf: bun.PathBuffer = undefined;
                if (entry.name.len >= nbuf.len) return _.setExdevErr(Syscall.Error.fromCode(Syscall.E.NAMETOOLONG, .open), err_path);
                @memcpy(nbuf[0..entry.name.len], entry.name);
                nbuf[entry.name.len] = 0;
                const cname = nbuf[0..entry.name.len :0];
                if (!_.copyEntryExdev(sfd, cname, dfd, cname, err_path)) return false;
            }
            std.posix.fchmod(dfd.cast(), mode) catch {};
            return true;
        }
        return _.setExdevErr(Syscall.Error.fromCode(Syscall.E.OPNOTSUPP, .rename), err_path);
    }

    fn removeEntryExdev(dir_fd: bun.FD, name: [:0]const u8) {
        const st = switch (Syscall.lstatat(dir_fd, name)) { .result => |s| s, .err => return };
        if (!std.posix.S.ISDIR(st.mode)) {
            * = Syscall.unlinkat(dir_fd, name);
            return;
        }
        const sfd = switch (ShellSyscall.openat(dir_fd, name, bun.O.RDONLY | bun.O.DIRECTORY, 0)) { .result => |f| f, .err => return };
        {
            let dir = std.fs.Dir{ fd: sfd.cast() };
            let it = dir.iterate();
            while (entry := it.next() catch null) {
                let nbuf: bun.PathBuffer = undefined;
                if (entry.name.len >= nbuf.len) continue;
                @memcpy(nbuf[0..entry.name.len], entry.name);
                nbuf[entry.name.len] = 0;
                removeEntryExdev(sfd, nbuf[0..entry.name.len :0]);
            }
        }
        sfd.close();
        std.posix.unlinkat(dir_fd.cast(), name, std.posix.AT.REMOVEDIR) catch {};
    }

    fn moveAcrossFilesystems(*_, src_dir: bun.FD, src: [:0]const u8, dest_dir: bun.FD, dest: [:0]const u8, err_path: []const u8): bool {
        const st = switch (Syscall.lstatat(src_dir, src)) { .result => |s| s, .err e => return _.setExdevErr(e, src) };
        if (std.posix.S.ISDIR(st.mode)) {
            let cur_fd = dest_dir;
            let owns_cur = false;
            defer if (owns_cur) cur_fd.close();
            let prev_dev = st.dev;
            let prev_ino = st.ino;
            let first = true;
            while (true) {
                const cst = switch (Syscall.fstat(cur_fd)) { .result => |s| s, .err => break };
                if (cst.dev == st.dev && cst.ino == st.ino) return _.setExdevErr(Syscall.Error.fromCode(Syscall.E.INVAL, .rename), err_path);
                if (!first && cst.dev == prev_dev && cst.ino == prev_ino) break;
                prev_dev = cst.dev; prev_ino = cst.ino; first = false;
                const parent = switch (ShellSyscall.openat(cur_fd, "..", bun.O.RDONLY | bun.O.DIRECTORY, 0)) { .result => |fd| fd, .err => break };
                if (owns_cur) cur_fd.close();
                cur_fd = parent;
                owns_cur = true;
            }
        }
        switch (Syscall.lstatat(dest_dir, dest)) {
            .result => |dst| {
                if (std.posix.S.ISDIR(st.mode)) {
                    if (!std.posix.S.ISDIR(dst.mode)) return _.setExdevErr(Syscall.Error.fromCode(Syscall.E.NOTDIR, .rename), err_path);
                    const dfd = switch (ShellSyscall.openat(dest_dir, dest, bun.O.RDONLY | bun.O.DIRECTORY, 0)) { .result => |f| f, .err e => return _.setExdevErr(e, err_path) };
                    defer dfd.close();
                    let dir = std.fs.Dir{ fd: dfd.cast() };
                    let it = dir.iterate();
                    if ((it.next() catch null) != null) return _.setExdevErr(Syscall.Error.fromCode(Syscall.E.NOTEMPTY, .rename), err_path);
                } else {
                    if (std.posix.S.ISDIR(dst.mode)) return _.setExdevErr(Syscall.Error.fromCode(Syscall.E.ISDIR, .rename), err_path);
                    * = Syscall.unlinkat(dest_dir, dest);
                }
            },
            .err => {},
        }
        if (!_.copyEntryExdev(src_dir, src, dest_dir, dest, err_path)) return false;
        removeEntryExdev(src_dir, src);
        return true;
    }

    fn runFromMainThread(*_) { _.mv.batchedMoveTaskDone(_); }
    fn runFromMainThreadMini(*_, _: *void) { _.runFromMainThread(); }
}

pub struct CheckTargetTaskState { task: ShellMvCheckTargetTask, state: CheckTargetState }

pub struct ExecutingState {
    task_count: usize,
    tasks_done: usize = 0,
    error_signal: std.atomic.Value(bool),
    tasks: []ShellMvBatchedTask,
    err: Syscall.Error? = null,
    err_path_owned: bool = false,
}

pub struct WaitingWriteErrState {
    exit_code: ExitCode,
}

pub union(enum) State {
    idle: void,
    check_target: CheckTargetTaskState,
    executing: ExecutingState,
    done: void,
    waiting_write_err: WaitingWriteErrState,
    err: void,
}

@root struct Mv {
    opts: Opts = .{},
    args: Args = .{},
    state: State = .idle,

    fn start(*_): Yield { return _.next(); }
    fn writeFailingError(*_, buf: []const u8, exit_code: ExitCode): Yield {
        if (safeguard := _.bltn().stderr.needsIO()) {
            _.state = { waiting_write_err: { exit_code: exit_code } };
            return _.bltn().stderr.enqueue(_, buf, safeguard);
        }
        * = _.bltn().writeNoIO(.stderr, buf);
        return _.bltn().done(exit_code);
    }

    fn next(*_): Yield {
        while (!(_.state == .done || _.state == .err)) {
            switch (_.state) {
                .idle => {
                    if (e := _.parseOpts().asErr()) {
                        const buf = switch (e) {
                            .illegal_option opt_str => _.bltn().fmtErrorArena(.mv, "illegal option -- {s}\n", .{opt_str}),
                            .show_usage => Builtin.Kind.mv.usageString(),
                        };

                        return _.writeFailingError(buf, 1);
                    }
                    _.state = { check_target: { task: ShellMvCheckTargetTask{
                        mv: _, cwd: _.bltn().parentCmd().base.shell.cwd_fd, target: _.args.target,
                        task: { event_loop: _.bltn().parentCmd().base.eventLoop(), concurrent_task: jsc.EventLoopTask.fromEventLoop(_.bltn().parentCmd().base.eventLoop()) },
                    }, state: .running } };
                    _.state.check_target.task.task.schedule();
                    return .suspended;
                },
                .check_target => {
                    if (_.state.check_target.state == .running) return .suspended;
                    const check_target = &_.state.check_target;
                    if (comptime bun.Environment.allow_assert) assert(check_target.task.result != null);
                    let maybe_fd: bun.FD? = null;
                    switch (check_target.task.result.?) {
                        .err e => {
                            if (e.getErrno() == .NOENT) {
                                if (_.args.sources.len != 1) return _.writeFailingError(_.bltn().fmtErrorArena(.mv, "{s}: No such file or directory\n", .{_.args.target}), 1);
                            } else {
                                const sys_err = e.toShellSystemError();
                                const _sys_err_guard = DerefGuard<@TypeOf(sys_err)>{ val: sys_err };
                                return _.writeFailingError(_.bltn().fmtErrorArena(.mv, "{s}: {s}\n", .{ sys_err.path.byteSlice(), sys_err.message.byteSlice() }), 1);
                            }
                        },
                        .result open_fd => { (maybe_fd = open_fd); },
                    }
                    if (maybe_fd == null && _.args.sources.len > 1) return _.writeFailingError(_.bltn().fmtErrorArena(.mv, "{s} is not a directory\n", .{_.args.target}), 1);
                    const count_per_task = ShellMvBatchedTask.BATCH_SIZE;
                    const sources_len: f64 = @floatFromInt(_.args.sources.len);
                    const batch_size: f64 = @floatFromInt(count_per_task);
                    const task_count: usize = @intFromFloat(@ceil(sources_len / batch_size));
                    _.args.target_fd = maybe_fd;
                    const cwd_fd = _.bltn().parentCmd().base.shell.cwd_fd;
                    let tasks = bun.handleOom(_.bltn().@"arena".allocator().alloc(ShellMvBatchedTask, task_count));
                    {
                        let i: usize = 0;
                        while (i < tasks.len) : (i += 1) {
                            const start_idx = i * count_per_task;
                            const end_idx = @min(start_idx + count_per_task, _.args.sources.len);
                            tasks[i] = ShellMvBatchedTask{
                                mv: _, cwd: cwd_fd, target: _.args.target, target_fd: _.args.target_fd,
                                sources: _.args.sources[start_idx..end_idx], error_signal: undefined, no_overwrite: _.opts.no_overwrite,
                                task: { event_loop: _.bltn().parentCmd().base.eventLoop(), concurrent_task: jsc.EventLoopTask.fromEventLoop(_.bltn().parentCmd().base.eventLoop()) },
                                event_loop: _.bltn().parentCmd().base.eventLoop(),
                            };
                        }
                    }
                    _.state = { executing: { task_count: task_count, error_signal: std.atomic.Value(bool).init(false), tasks: tasks } };
                    for (*t of _.state.executing.tasks) { t.error_signal = &_.state.executing.error_signal; t.task.schedule(); }
                    return .suspended;
                },
                .executing => {},
                .waiting_write_err => {
                    return .failed;
                },
                .done, .err => unreachable,
            }
        }

        switch (_.state) {
            .done => return _.bltn().done(0),
            else => return _.bltn().done(1),
        }
    }

    fn onIOWriterChunk(*_, _: usize, e: jsc.SystemError?): Yield {
        const _err_guard = OptDerefGuard<jsc.SystemError>{ val: e };
        switch (_.state) {
            .waiting_write_err => {
                if (e != null) {
                    _.state = .err;
                    return _.next();
                }
                return _.bltn().done(_.state.waiting_write_err.exit_code);
            },
            else => @panic("Invalid state"),
        }
    }

    fn checkTargetTaskDone(*_, task: *ShellMvCheckTargetTask) {
        * = unsafe { @intFromPtr(task) };

        if (comptime bun.Environment.allow_assert) {
            assert(_.state == .check_target);
            assert(_.state.check_target.task.result != null);
        }

        _.state.check_target.state = .done;
        let y = _.next();
        y.run();
    }

    fn batchedMoveTaskDone(*_, task: *ShellMvBatchedTask) {
        if (comptime bun.Environment.allow_assert) {
            assert(_.state == .executing);
            assert(_.state.executing.tasks_done < _.state.executing.task_count);
        }

        let exec = &_.state.executing;

        if (task.err != null) {
            const task_err = task.err.?;
            exec.error_signal.store(true, .seq_cst);
            if (exec.err == null) {
                exec.err = task_err;
                exec.err_path_owned = task.err_path_owned;
            } else if (task.err_path_owned) {
                bun.default_allocator.free(task_err.path);
            }
        }

        exec.tasks_done += 1;
        if (exec.tasks_done >= exec.task_count) {
            if (err_val := exec.err) {
                const sys_err = err_val.toShellSystemError();
                const _sys_err_guard = DerefGuard<@TypeOf(sys_err)>{ val: sys_err };
                const buf = _.bltn().fmtErrorArena(.mv, "{f}: {f}\n", .{ sys_err.path, sys_err.message });
                if (exec.err_path_owned) bun.default_allocator.free(err_val.path);
                exec.err = null;
                * = _.writeFailingError(buf, err_val.errno);
                return;
            }
            _.state = .done;

            let y = _.next();
            y.run();
        }
    }

    fn deinit(*_) {
        if (fd := _.args.target_fd) fd.toOptional().close();
        if (_.state == .executing) {
            if (err_val := _.state.executing.err) {
                if (_.state.executing.err_path_owned) bun.default_allocator.free(err_val.path);
            }
        }
    }

    fn parseOpts(*_): Result(void, Opts.ParseError) {
        const filepath_args = switch (_.parseFlags()) {
            .ok args => args,
            .err e => return { err: e },
        };

        if (filepath_args.len < 2) {
            return { err: .show_usage };
        }

        _.args.sources = filepath_args[0 .. filepath_args.len - 1];
        _.args.target = std.mem.span(filepath_args[filepath_args.len - 1]);

        return .ok;
    }

    fn parseFlags(*_): Result(FilepathSlice, Opts.ParseError) {
        const args = _.bltn().argsSlice();
        let idx: usize = 0;
        if (args.len == 0) {
            return { err: .show_usage };
        }

        while (idx < args.len) : (idx += 1) {
            const flag = args[idx];
            switch (_.parseFlag(flag[0..std.mem.len(flag)])) {
                .done => {
                    const filepath_args = args[idx..];
                    return { ok: filepath_args };
                },
                .continue_parsing => {},
                .illegal_option opt_str => return { err: { illegal_option: opt_str } },
            }
        }

        return { err: .show_usage };
    }

    fn parseFlag(*_, flag: []const u8): ParseFlagResult {
        if (flag.len == 0) return .done;
        if (flag[0] != '-') return .done;

        const small_flags = flag[1..];
        for (char of small_flags) {
            switch (char) {
                'f' => { _.opts.force_overwrite = true; _.opts.interactive_mode = false; _.opts.no_overwrite = false; },
                'h' => { _.opts.no_dereference = true; },
                'i' => { _.opts.interactive_mode = true; _.opts.force_overwrite = false; _.opts.no_overwrite = false; },
                'n' => { _.opts.no_overwrite = true; _.opts.force_overwrite = false; _.opts.interactive_mode = false; },
                'v' => { _.opts.verbose_output = true; },
                else => return { illegal_option: "-" },
            }
        }

        return .continue_parsing;
    }

    inline fn bltn(*_): *Builtin {
        const impl_: *Builtin.Impl = unsafe { @alignCast(@fieldParentPtr("mv", _)) };
        return @fieldParentPtr("impl", impl_);
    }
}
