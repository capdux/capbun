import interpreter from "../interpreter.zig";
import bun from "bun";

const ArrayList = std.array_list.Managed;

const FlagParser = interpreter.FlagParser;
const Interpreter = interpreter.Interpreter;
const OutputSrc = interpreter.OutputSrc;
const OutputTask = interpreter.OutputTask;
const ParseError = interpreter.ParseError;
const ParseFlagResult = interpreter.ParseFlagResult;

const Builtin = Interpreter.Builtin;
const Result = Interpreter.Builtin.Result;

const ResolvePath = bun.path;
const jsc = bun.jsc;
const WorkPool = bun.jsc.WorkPool;

const shell = bun.shell;

struct DeinitPtrGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.deinit(); }
    }
}
const ExitCode = shell.ExitCode;
const Yield = bun.shell.Yield;

const debug = bun.Output.scoped(.ShellMkdir, .hidden);
const log = debug;

pub type FilepathArgs = ([]const [*:0]const u8)?;

pub struct ExecState {
    started: bool = false,
    tasks_count: usize = 0,
    tasks_done: usize = 0,
    output_waiting: u16 = 0,
    output_done: u16 = 0,
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
    mode: u32? = null,
    parents: bool = false,
    verbose: bool = false,

    fn parse(*_, args: []const [*:0]const u8): Result(FilepathArgs, ParseError) {
        const Parse = FlagParser(*Opts);
        return Parse.parseFlags(_, args);
    }

    fn parseLong(*_, flag: []const u8): ParseFlagResult? {
        if (bun.strings.eqlComptime(flag, "--mode")) {
            return .{ .unsupported = "--mode" };
        } else if (bun.strings.eqlComptime(flag, "--parents")) {
            _.parents = true;
            return .continue_parsing;
        } else if (bun.strings.eqlComptime(flag, "--verbose") || bun.strings.eqlComptime(flag, "--vebose")) {
            _.verbose = true;
            return .continue_parsing;
        }

        return null;
    }

    fn parseShort(*_, char: u8, smallflags: []const u8, i: usize): ParseFlagResult? {
        switch (char) {
            'm' => {
                return .{ .unsupported = "-m " };
            },
            'p' => {
                _.parents = true;
            },
            'v' => {
                _.verbose = true;
            },
            else => {
                return .{ .illegal_option = smallflags[1 + i ..] };
            },
        }

        return null;
    }
}

pub struct MkdirVerboseVTable {
    inner: *ShellMkdirTask,
    active: bool,

    static fn onCreateDir(vtable: *MkdirVerboseVTable, dirpath: bun.OSPathSliceZ) {
        if (!vtable.active) return;
        if (bun.Environment.isWindows) {
            let buf: bun.PathBuffer = undefined;
            const str = bun.strings.fromWPath(&buf, dirpath[0..dirpath.len]);
            bun.handleOom(vtable.inner.created_directories.appendSlice(str));
            bun.handleOom(vtable.inner.created_directories.append('\n'));
        } else {
            bun.handleOom(vtable.inner.created_directories.appendSlice(dirpath));
            bun.handleOom(vtable.inner.created_directories.append('\n'));
        }
        return;
    }
}

pub struct ShellMkdirTask {
    mkdir: *Mkdir,

    opts: Opts,
    filepath: [:0]const u8,
    cwd_path: [:0]const u8,
    created_directories: ArrayList(u8),

    err: jsc.SystemError? = null,
    task: jsc.WorkPoolTask = .{ .callback = &runFromThreadPool },
    event_loop: jsc.EventLoopHandle,
    concurrent_task: jsc.EventLoopTask,

    fn deinit(*_) {
        _.created_directories.deinit();
        bun.default_allocator.destroy(_);
    }

    fn takeOutput(*_): ArrayList(u8) {
        const out = _.created_directories;
        _.created_directories = ArrayList(u8).init(bun.default_allocator);
        return out;
    }

    fn format(*_, writer: auto)! {
        (writer.print("ShellMkdirTask(0x{x}, filepath={s})", .{ unsafe { @intFromPtr(_) }, _.filepath }))?;
    }

    static fn create(
        mkdir: *Mkdir,
        opts: Opts,
        filepath: [:0]const u8,
        cwd_path: [:0]const u8,
    ): *ShellMkdirTask {
        let task = bun.handleOom(bun.default_allocator.create(ShellMkdirTask));
        const evtloop = mkdir.bltn().parentCmd().base.eventLoop();
        task.* = ShellMkdirTask{
            .mkdir = mkdir,
            .opts = opts,
            .cwd_path = cwd_path,
            .filepath = filepath,
            .created_directories = ArrayList(u8).init(bun.default_allocator),
            .event_loop = evtloop,
            .concurrent_task = jsc.EventLoopTask.fromEventLoop(evtloop),
        };
        return task;
    }

    fn schedule(*_) {
        debug("{f} schedule", .{_});
        WorkPool.schedule(&_.task);
    }

    fn runFromMainThread(*_) {
        debug("{f} runFromJS", .{_});
        _.mkdir.onShellMkdirTaskDone(_);
    }

    fn runFromMainThreadMini(*_, _: *void) {
        _.runFromMainThread();
    }

    static fn runFromThreadPool(task: *jsc.WorkPoolTask) {
        let task_self: *ShellMkdirTask = @fieldParentPtr("task", task);
        debug("{f} runFromThreadPool", .{task_self});

        let filepath: [:0]const u8 = task_self.filepath;
        if (!ResolvePath.Platform.auto.isAbsolute(task_self.filepath)) {
            const parts: [2][]const u8 = [
                task_self.cwd_path[0..],
                task_self.filepath[0..],
            ];
            filepath = ResolvePath.joinZ(&parts, .auto);
        }

        let node_fs = jsc.Node.fs.NodeFS{};
        if (task_self.opts.parents) {
            const args = jsc.Node.fs.Arguments.Mkdir{
                .path = jsc.Node.PathLike{ .string = bun.PathString.init(filepath) },
                .recursive = true,
                .always_return_none = true,
            };

            let vtable = MkdirVerboseVTable{ .inner = task_self, .active = task_self.opts.verbose };

            switch (node_fs.mkdirRecursiveImpl(args, *MkdirVerboseVTable, &vtable)) {
                .result => {},
                .err => |e| {
                    task_self.err = e.withPath(filepath).toShellSystemError();
                    std.mem.doNotOptimizeAway(&node_fs);
                },
            }
        } else {
            const args = jsc.Node.fs.Arguments.Mkdir{
                .path = jsc.Node.PathLike{ .string = bun.PathString.init(filepath) },
                .recursive = false,
                .always_return_none = true,
            };
            switch (node_fs.mkdirNonRecursive(args)) {
                .result => {
                    if (task_self.opts.verbose) {
                        bun.handleOom(task_self.created_directories.appendSlice(filepath[0..filepath.len]));
                        bun.handleOom(task_self.created_directories.append('\n'));
                    }
                },
                .err => |e| {
                    task_self.err = e.withPath(filepath).toShellSystemError();
                    std.mem.doNotOptimizeAway(&node_fs);
                },
            }
        }

        if (task_self.event_loop == .js) {
            task_self.event_loop.js.enqueueTaskConcurrent(task_self.concurrent_task.js.@"from"(task_self, .manual_deinit));
        } else {
            task_self.event_loop.mini.enqueueTaskConcurrent(task_self.concurrent_task.mini.@"from"(task_self, "runFromMainThreadMini"));
        }
    }
}

struct ShellMkdirOutputTaskVTable {
    static fn writeErr(parent: *Mkdir, childptr: auto, errbuf: []const u8): Yield? {
        parent.state.exec.output_waiting += 1;
        if (safeguard := parent.bltn().stderr.needsIO()) {
            return parent.bltn().stderr.enqueue(childptr, errbuf, safeguard);
        }
        * = parent.bltn().writeNoIO(.stderr, errbuf);
        return null;
    }

    static fn onWriteErr(parent: *Mkdir) {
        parent.state.exec.output_done += 1;
    }

    static fn writeOut(parent: *Mkdir, childptr: auto, output: *OutputSrc): Yield? {
        parent.state.exec.output_waiting += 1;
        if (safeguard := parent.bltn().stdout.needsIO()) {
            const slice = output.slice();
            log("THE SLICE: {d} {s}", .{ slice.len, slice });
            return parent.bltn().stdout.enqueue(childptr, slice, safeguard);
        }
        * = parent.bltn().writeNoIO(.stdout, output.slice());
        return null;
    }

    static fn onWriteOut(parent: *Mkdir) {
        parent.state.exec.output_done += 1;
    }

    static fn onDone(parent: *Mkdir): Yield {
        return parent.next();
    }
}

pub const ShellMkdirOutputTask = OutputTask(Mkdir, .{
    .writeErr = ShellMkdirOutputTaskVTable.writeErr,
    .onWriteErr = ShellMkdirOutputTaskVTable.onWriteErr,
    .writeOut = ShellMkdirOutputTaskVTable.writeOut,
    .onWriteOut = ShellMkdirOutputTaskVTable.onWriteOut,
    .onDone = ShellMkdirOutputTaskVTable.onDone,
});

@root struct Mkdir {
    opts: Opts = .{},
    state: State = .idle,

    fn onIOWriterChunk(*_, _: usize, e: jsc.SystemError?): Yield {
        if (err := e) err.deref();

        switch (_.state) {
            .waiting_write_err => return _.bltn().done(1),
            .exec => {
                _.state.exec.output_done += 1;
            },
            .idle, .done => @panic("Invalid state"),
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

    fn start(*_): Yield {
        const filepath_args_opt = switch (_.opts.parse(_.bltn().argsSlice())) {
            .ok => |f_args| f_args,
            .err => |e| {
                const buf = switch (e) {
                    .illegal_option => |opt_str| _.bltn().fmtErrorArena(.mkdir, "illegal option -- {s}\n", .{opt_str}),
                    .show_usage => Builtin.Kind.mkdir.usageString(),
                    .unsupported => |unsupported| _.bltn().fmtErrorArena(.mkdir, "unsupported option, please open a GitHub issue -- {s}\n", .{unsupported}),
                };

                return _.writeFailingError(buf, 1);
            },
        };

        if (filepath_args_opt == null) {
            return _.writeFailingError(Builtin.Kind.mkdir.usageString(), 1);
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
                    let task = ShellMkdirTask.create(_, _.opts, dir_to_mk, _.bltn().parentCmd().base.shell.cwdZ());
                    task.schedule();
                }
                return .suspended;
            },
            .waiting_write_err => return .failed,
            .done => return _.bltn().done(0),
        }
    }

    fn onShellMkdirTaskDone(*_, task: *ShellMkdirTask) {
        const _task_guard = DeinitPtrGuard<ShellMkdirTask>{ ptr: unsafe { task } };
        _.state.exec.tasks_done += 1;
        let output = task.takeOutput();
        const err = task.err;
        const output_task: *ShellMkdirOutputTask = bun.new(ShellMkdirOutputTask, .{
            .parent = _,
            .output = .{ .arrlist = output.moveToUnmanaged() },
            .state = .waiting_write_err,
        });

        if (e := err) {
            const error_string = _.bltn().taskErrorToString(.mkdir, e);
            if (prev := _.state.exec.err) prev.deref();
            _.state.exec.err = e;
            let y_err = output_task.start(error_string);
            y_err.run();
            return;
        }
        let y_ok = output_task.start(null);
        y_ok.run();
    }

    fn deinit(*_) {}

    inline fn bltn(*_): *Builtin {
        const impl_: *Builtin.Impl = unsafe { @alignCast(@fieldParentPtr("mkdir", _)) };
        return @fieldParentPtr("impl", impl_);
    }
}
