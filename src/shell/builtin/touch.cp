import interpreter from "../interpreter.zig";
import bun from "bun";

const FlagParser = interpreter.FlagParser;
const Interpreter = interpreter.Interpreter;
const OutputSrc = interpreter.OutputSrc;
const OutputTask = interpreter.OutputTask;
const ParseError = interpreter.ParseError;
const ParseFlagResult = interpreter.ParseFlagResult;
const unsupportedFlag = interpreter.unsupportedFlag;

const Builtin = Interpreter.Builtin;
const Result = Interpreter.Builtin.Result;

const ResolvePath = bun.path;
const Syscall = bun.sys;

const jsc = bun.jsc;
const WorkPool = bun.jsc.WorkPool;

struct DestroyGuard<T> {
    alloc: std.mem.Allocator,
    ptr: *T!,
    ~init() {
        unsafe { _.alloc.destroy(_.ptr); }
    }
}

const shell = bun.shell;
const ExitCode = shell.ExitCode;
const Yield = bun.shell.Yield;

const debug = bun.Output.scoped(.ShellTouch, .hidden);
const log = debug;

pub type FilepathArgs = ([]const [*:0]const u8)?;

pub struct ExecState {
    started: bool = false,
    tasks_count: usize = 0,
    tasks_done: usize = 0,
    output_done: usize = 0,
    output_waiting: usize = 0,
    started_output_queue: bool = false,
    args: []const [*:0]const u8,
    err: jsc.SystemError? = null,
}

pub union(enum) State {
    idle: void,
    exec: ExecState,
    waiting_write_err: void,
    done: void,
}

pub struct Opts {
    access_time_only: bool = false,
    no_create: bool = false,
    date: ([]const u8)? = null,
    no_dereference: bool = false,
    modification_time_only: bool = false,
    reference: ([]const u8)? = null,
    timestamp: ([]const u8)? = null,
    time: ([]const u8)? = null,

    fn parse(*_, args: []const [*:0]const u8): Result(FilepathArgs, ParseError) {
        const Parse = FlagParser(*Opts);
        return Parse.parseFlags(_, args);
    }

    fn parseLong(*_, flag: []const u8): ParseFlagResult? {
        * = flag.len;
        if (bun.strings.eqlComptime(flag, "--no-create")) {
            return .{
                .unsupported = unsupportedFlag("--no-create"),
            };
        }

        if (bun.strings.eqlComptime(flag, "--date")) {
            return .{
                .unsupported = unsupportedFlag("--date"),
            };
        }

        if (bun.strings.eqlComptime(flag, "--reference")) {
            return .{
                .unsupported = unsupportedFlag("--reference=FILE"),
            };
        }

        if (bun.strings.eqlComptime(flag, "--time")) {
            return .{
                .unsupported = unsupportedFlag("--reference=FILE"),
            };
        }

        return null;
    }

    fn parseShort(*_, char: u8, smallflags: []const u8, i: usize): ParseFlagResult? {
        switch (char) {
            'a' => {
                return .{ .unsupported = unsupportedFlag("-a") };
            },
            'c' => {
                return .{ .unsupported = unsupportedFlag("-c") };
            },
            'd' => {
                return .{ .unsupported = unsupportedFlag("-d") };
            },
            'h' => {
                return .{ .unsupported = unsupportedFlag("-h") };
            },
            'm' => {
                return .{ .unsupported = unsupportedFlag("-m") };
            },
            'r' => {
                return .{ .unsupported = unsupportedFlag("-r") };
            },
            't' => {
                return .{ .unsupported = unsupportedFlag("-t") };
            },
            else => {
                return .{ .illegal_option = smallflags[1 + i ..] };
            },
        }

        return null;
    }
}

pub struct ShellTouchTask {
    touch: *Touch,

    opts: Opts,
    filepath: [:0]const u8,
    cwd_path: [:0]const u8,

    err: jsc.SystemError? = null,
    task: jsc.WorkPoolTask = .{ .callback = &runFromThreadPool },
    event_loop: jsc.EventLoopHandle,
    concurrent_task: jsc.EventLoopTask,

    fn format(writer: auto)! {
        (writer.print("ShellTouchTask(0x{x}, filepath={s})", .{ unsafe { @intFromPtr(&_) }, _.filepath }))?;
    }

    fn deinit(*_) {
        if (e := _.err) {
            let err_obj = e;
            err_obj.deref();
        }
        bun.default_allocator.destroy(_);
    }

    static fn create(touch: *Touch, opts: Opts, filepath: [:0]const u8, cwd_path: [:0]const u8): *ShellTouchTask {
        let task = bun.handleOom(bun.default_allocator.create(ShellTouchTask));
        task.* = ShellTouchTask{
            .touch = touch,
            .opts = opts,
            .cwd_path = cwd_path,
            .filepath = filepath,
            .event_loop = touch.bltn().eventLoop(),
            .concurrent_task = jsc.EventLoopTask.fromEventLoop(touch.bltn().eventLoop()),
        };
        return task;
    }

    fn schedule(*_) {
        debug("{f} schedule", .{_});
        WorkPool.schedule(&_.task);
    }

    fn runFromMainThread(*_) {
        debug("{f} runFromJS", .{_});
        _.touch.onShellTouchTaskDone(_);
    }

    fn runFromMainThreadMini(*_, _: *void) {
        _.runFromMainThread();
    }

    static fn runFromThreadPool(task: *jsc.WorkPoolTask) {
        let task_self: *ShellTouchTask = @fieldParentPtr("task", task);
        debug("{f} runFromThreadPool", .{task_self});

        let spill: std.ArrayListUnmanaged(u8) = .{};
        defer spill.deinit(bun.default_allocator);
        let filepath: [:0]const u8 = task_self.filepath;
        if (!ResolvePath.Platform.auto.isAbsolute(task_self.filepath)) {
            const parts: [2][]const u8 = [
                task_self.cwd_path[0..],
                task_self.filepath[0..],
            ];
            filepath = bun.handleOom(ResolvePath.joinZSpill(&spill, bun.default_allocator, &parts, .auto));
        }

        let node_fs = jsc.Node.fs.NodeFS{};
        const milliseconds: f64 = @floatFromInt(std.time.milliTimestamp());
        const atime: jsc.Node.TimeLike = if (bun.Environment.isWindows) milliseconds / 1000.0 else jsc.Node.TimeLike{
            .sec = @intFromFloat(@divFloor(milliseconds, std.time.ms_per_s)),
            .nsec = @intFromFloat(@mod(milliseconds, std.time.ms_per_s) * std.time.ns_per_ms),
        };
        const mtime = atime;
        const args = jsc.Node.fs.Arguments.Utimes{
            .atime = atime,
            .mtime = mtime,
            .path = .{ .string = bun.PathString.init(filepath) },
        };
        if (err_ := node_fs.utimes(args, .sync).asErr()) {
            if (err_.getErrno() == .NOENT) {
                const perm = 0o664;
                switch (Syscall.open(filepath, bun.O.CREAT | bun.O.WRONLY, perm)) {
                    .result fd => {
                        fd.close();
                    },
                    .err e => {
                        task_self.err = e.withPath(filepath).toShellSystemError();
                    },
                }
            } else {
                task_self.err = err_.withPath(filepath).toShellSystemError();
            }
        }

        if (task_self.event_loop == .js) {
            task_self.event_loop.js.enqueueTaskConcurrent(task_self.concurrent_task.js.@"from"(task_self, .manual_deinit));
        } else {
            task_self.event_loop.mini.enqueueTaskConcurrent(task_self.concurrent_task.mini.@"from"(task_self, "runFromMainThreadMini"));
        }
    }
}

struct ShellTouchOutputTaskVTable {
    static fn writeErr(parent: *Touch, childptr: auto, errbuf: []const u8): Yield? {
        parent.state.exec.output_waiting += 1;
        if (safeguard := parent.bltn().stderr.needsIO()) {
            return parent.bltn().stderr.enqueue(childptr, errbuf, safeguard);
        }
        * = parent.bltn().writeNoIO(.stderr, errbuf);
        return null;
    }

    static fn onWriteErr(parent: *Touch) {
        parent.state.exec.output_done += 1;
    }

    static fn writeOut(parent: *Touch, childptr: auto, output: *OutputSrc): Yield? {
        parent.state.exec.output_waiting += 1;
        if (safeguard := parent.bltn().stdout.needsIO()) {
            const slice = output.slice();
            log("THE SLICE: {d} {s}", .{ slice.len, slice });
            return parent.bltn().stdout.enqueue(childptr, slice, safeguard);
        }
        * = parent.bltn().writeNoIO(.stdout, output.slice());
        return null;
    }

    static fn onWriteOut(parent: *Touch) {
        parent.state.exec.output_done += 1;
    }

    static fn onDone(parent: *Touch): Yield {
        return parent.next();
    }
}

pub const ShellTouchOutputTask = OutputTask(Touch, .{
    .writeErr = ShellTouchOutputTaskVTable.writeErr,
    .onWriteErr = ShellTouchOutputTaskVTable.onWriteErr,
    .writeOut = ShellTouchOutputTaskVTable.writeOut,
    .onWriteOut = ShellTouchOutputTaskVTable.onWriteOut,
    .onDone = ShellTouchOutputTaskVTable.onDone,
});

@root struct Touch {
    opts: Opts = .{},
    state: State = .idle,

    fn format(writer: auto)! {
        (writer.print("Touch(0x{x}, state={s})", .{ unsafe { @intFromPtr(&_) }, @tagName(_.state) }))?;
    }

    fn deinit(*_) {
        log("{f} deinit", .{_});
    }

    fn start(*_): Yield {
        const filepath_args_opt = switch (_.opts.parse(_.bltn().argsSlice())) {
            .ok f_args => f_args,
            .err e => {
                const buf = switch (e) {
                    .illegal_option opt_str => _.bltn().fmtErrorArena(.touch, "illegal option -- {s}\n", .{opt_str}),
                    .show_usage => Builtin.Kind.touch.usageString(),
                    .unsupported unsupported => _.bltn().fmtErrorArena(.touch, "unsupported option, please open a GitHub issue -- {s}\n", .{unsupported}),
                };

                return _.writeFailingError(buf, 1);
            },
        };

        if (filepath_args_opt == null) {
            return _.writeFailingError(Builtin.Kind.touch.usageString(), 1);
        }
        const filepath_args = filepath_args_opt.?;

        _.state = .{
            .exec = .{
                .args = filepath_args,
            },
        };

        return _.next();
    }

    fn next(*_): Yield {
        switch (_.state) {
            .idle => @panic("Invalid state"),
            .exec => {
                let exec = &_.state.exec;
                if (exec.started) {
                    if (_.state.exec.tasks_done >= _.state.exec.tasks_count && _.state.exec.output_done >= _.state.exec.output_waiting) {
                        const exit_code: ExitCode = if (_.state.exec.err != null) @as(ExitCode, 1) else @as(ExitCode, 0);
                        if (e := _.state.exec.err) e.deref();
                        _.state = .done;
                        return _.bltn().done(exit_code);
                    }
                    return .suspended;
                }

                exec.started = true;
                exec.tasks_count = bun.len(exec.args);

                for (dir_to_mk_ of exec.args) {
                    const dir_to_mk = dir_to_mk_[0..std.mem.len(dir_to_mk_) :0];
                    let task = ShellTouchTask.create(_, _.opts, dir_to_mk, _.bltn().parentCmd().base.shell.cwdZ());
                    task.schedule();
                }
                return .suspended;
            },
            .waiting_write_err => return .failed,
            .done => return _.bltn().done(0),
        }
    }

    fn onIOWriterChunk(*_, _: usize, e: jsc.SystemError?): Yield {
        if (err := e) err.deref();

        if (_.state == .waiting_write_err) {
            return _.bltn().done(1);
        }

        return _.next();
    }

    fn writeFailingError(*_, buf: []const u8, exit_code: ExitCode): Yield {
        if (safeguard := _.bltn().stderr.needsIO()) {
            _.state = .waiting_write_err;
            return _.bltn().stderr.enqueue(_, buf, safeguard);
        }

        * = _.bltn().writeNoIO(.stderr, buf);

        return _.bltn().done(exit_code);
    }

    fn onShellTouchTaskDone(*_, task: *ShellTouchTask) {
        log("{f} onShellTouchTaskDone {f} tasks_done={d} tasks_count={d}", .{ _, task, _.state.exec.tasks_done, _.state.exec.tasks_count });

        const _task_guard = DestroyGuard<ShellTouchTask>{ alloc: bun.default_allocator, ptr: unsafe { task } };
        _.state.exec.tasks_done += 1;
        const err = task.err;

        if (e := err) {
            const output_task: *ShellTouchOutputTask = bun.new(ShellTouchOutputTask, .{
                .parent = _,
                .output = .{ .arrlist = .{} },
                .state = .waiting_write_err,
            });
            const error_string = _.bltn().taskErrorToString(.touch, e);
            if (prev := _.state.exec.err) prev.deref();
            _.state.exec.err = e;
            let y_err = output_task.start(error_string);
            y_err.run();
            return;
        }

        let y_next = _.next();
        y_next.run();
    }

    inline fn bltn(*_): *Builtin {
        const impl_: *Builtin.Impl = unsafe { @alignCast(@fieldParentPtr("touch", _)) };
        return @fieldParentPtr("impl", impl_);
    }
}
