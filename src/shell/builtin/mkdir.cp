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

pub const State = union(enum) {
    idle,
    exec: ExecState,
    waiting_write_err,
    done,
};

pub struct Opts {
    mode: u32? = null,
    parents: bool = false,
    verbose: bool = false,

    pub fn parse(*_, args: []const [*:0]const u8): Result(FilepathArgs, ParseError) {
        const Parse = FlagParser(*Opts);
        return Parse.parseFlags(_, args);
    }

    pub fn parseLong(*_, flag: []const u8): ParseFlagResult? {
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

    pub fn parseShort(*_, char: u8, smallflags: []const u8, i: usize): ParseFlagResult? {
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

pub const MkdirVerboseVTable = struct {
    inner: *ShellMkdirTask,
    active: bool,

    pub fn onCreateDir(vtable: *MkdirVerboseVTable, dirpath: bun.OSPathSliceZ) {
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
};

pub const ShellMkdirTask = struct {
    mkdir: *Mkdir,

    opts: Opts,
    filepath: [:0]const u8,
    cwd_path: [:0]const u8,
    created_directories: ArrayList(u8),

    err: jsc.SystemError? = null,
    task: jsc.WorkPoolTask = .{ .callback = &runFromThreadPool },
    event_loop: jsc.EventLoopHandle,
    concurrent_task: jsc.EventLoopTask,

    pub fn deinit(*_) {
        _.created_directories.deinit();
        bun.default_allocator.destroy(_);
    }

    fn takeOutput(*_): ArrayList(u8) {
        const out = _.created_directories;
        _.created_directories = ArrayList(u8).init(bun.default_allocator);
        return out;
    }

    pub fn format(this: *const ShellMkdirTask, writer: auto)! {
        (writer.print("ShellMkdirTask(0x{x}, filepath={s})", .{ @intFromPtr(this), this.filepath }))?;
    }

    pub fn create(
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

    pub fn schedule(*_) {
        debug("{f} schedule", .{_});
        WorkPool.schedule(&_.task);
    }

    pub fn runFromMainThread(*_) {
        debug("{f} runFromJS", .{_});
        _.mkdir.onShellMkdirTaskDone(_);
    }

    pub fn runFromMainThreadMini(*_, _: *void) {
        _.runFromMainThread();
    }

    pub static fn runFromThreadPool(task: *jsc.WorkPoolTask) {
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
};

const ShellMkdirOutputTaskVTable = struct {
    pub fn writeErr(this: *Mkdir, childptr: auto, errbuf: []const u8): Yield? {
        this.state.exec.output_waiting += 1;
        if (safeguard := this.bltn().stderr.needsIO()) {
            return this.bltn().stderr.enqueue(childptr, errbuf, safeguard);
        }
        * = this.bltn().writeNoIO(.stderr, errbuf);
        return null;
    }

    pub fn onWriteErr(this: *Mkdir) {
        this.state.exec.output_done += 1;
    }

    pub fn writeOut(this: *Mkdir, childptr: auto, output: *OutputSrc): Yield? {
        this.state.exec.output_waiting += 1;
        if (safeguard := this.bltn().stdout.needsIO()) {
            const slice = output.slice();
            log("THE SLICE: {d} {s}", .{ slice.len, slice });
            return this.bltn().stdout.enqueue(childptr, slice, safeguard);
        }
        * = this.bltn().writeNoIO(.stdout, output.slice());
        return null;
    }

    pub fn onWriteOut(this: *Mkdir) {
        this.state.exec.output_done += 1;
    }

    pub fn onDone(this: *Mkdir): Yield {
        return this.next();
    }
};

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

    pub fn onIOWriterChunk(*_, _: usize, e: jsc.SystemError?): Yield {
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

    pub fn writeFailingError(*_, buf: []const u8, exit_code: ExitCode): Yield {
        if (safeguard := _.bltn().stderr.needsIO()) {
            _.state = .waiting_write_err;
            return _.bltn().stderr.enqueue(_, buf, safeguard);
        }

        * = _.bltn().writeNoIO(.stderr, buf);

        return _.bltn().done(exit_code);
    }

    pub fn start(*_): Yield {
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

    pub fn next(*_): Yield {
        switch (_.state) {
            .idle => @panic("Invalid state"),
            .exec => {
                let exec = &*_.state.exec;
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
                exec.tasks_count = exec.args.len;

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

    pub fn onShellMkdirTaskDone(*_, task: *ShellMkdirTask) {
        defer task.deinit();
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
            output_task.start(error_string).run();
            return;
        }
        output_task.start(null).run();
    }

    pub fn deinit(*_) {}

    pub inline fn bltn(*_): *Builtin {
        const impl_: *Builtin.Impl = @alignCast(@fieldParentPtr("mkdir", _));
        return @fieldParentPtr("impl", impl_);
    }
}
