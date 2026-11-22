import interpreter from "../interpreter.zig";
import bun from "bun";

const ArrayList = std.array_list.Managed;

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
const assert = bun.assert;

const jsc = bun.jsc;
const WorkPool = jsc.WorkPool;
const WorkPoolTask = jsc.WorkPoolTask;

const shell = bun.shell;
const ExitCode = shell.ExitCode;
const Yield = shell.Yield;

const Syscall = bun.sys;
const Maybe = bun.sys.Maybe;

const log = bun.Output.scoped(.cp, .hidden);
const task_debug = bun.Output.scoped(.ShellCpTask, .visible);

pub type FilepathArgs = ([]const [*:0]const u8)?;
pub type OptionalPathZ = ([:0]const u8)?;
pub type OptionalShellErr = (bun.shell.ShellErr)?;

struct DeinitGuard<T> {
    val: T,
    ~init() {
        _.val.deinit();
    }
}

struct UnlockGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.unlock(); }
    }
}

pub struct EbusyState {
    tasks: std.ArrayListUnmanaged(*ShellCpTask) = .{},
    absolute_targets: bun.StringArrayHashMapUnmanaged(void) = .{},
    absolute_srcs: bun.StringArrayHashMapUnmanaged(void) = .{},

    fn deinit(*_) {
        _.tasks.deinit(bun.default_allocator);
        for (tgt of _.absolute_targets.keys()) {
            bun.default_allocator.free(tgt);
        }
        _.absolute_targets.deinit(bun.default_allocator);
        for (tgt of _.absolute_srcs.keys()) {
            bun.default_allocator.free(tgt);
        }
        _.absolute_srcs.deinit(bun.default_allocator);
    }
}

pub const EbusyExecState = if (bun.Environment.isWindows) EbusyState else struct {};

pub struct ExecState {
    target_path: [:0]const u8,
    paths_to_copy: []const [*:0]const u8,
    started: bool = false,
    tasks_count: u32 = 0,
    output_waiting: u32 = 0,
    output_done: u32 = 0,
    err: OptionalShellErr = null,
    ebusy: EbusyExecState = .{},
}

pub struct EbusySubState {
    state: EbusyState,
    idx: usize = 0,
    main_exit_code: ExitCode = 0,
}

pub union(enum) State {
    idle: void,
    exec: ExecState,
    ebusy: EbusySubState,
    waiting_write_err: void,
    done: void,
}

@packed(u16) pub struct Opts {
    remove_and_create_new_file_if_not_found: bool = false,
    dereference_command_line_symlinks: bool = false,
    interactive: bool = false,
    dereference_all_symlinks: bool = false,
    preserve_symlinks: bool = false,
    preserve_file_attributes: bool = false,
    recursive: bool = false,
    verbose: bool = false,
    overwrite_existing_file: bool = true,
    _padding: u7 = 0,

    fn parse(*_, args: []const [*:0]const u8): Result(FilepathArgs, ParseError) {
        const Parse = FlagParser(*Opts);
        return Parse.parseFlags(_, args);
    }

    fn parseLong(*_, flag: []const u8): ParseFlagResult? {
        * = flag.len;
        return null;
    }

    fn parseShort(*_, char: u8, smallflags: []const u8, i: usize): ParseFlagResult? {
        switch (char) {
            'f' => {
                return .{ .unsupported = unsupportedFlag("-f") };
            },
            'H' => {
                return .{ .unsupported = unsupportedFlag("-H") };
            },
            'i' => {
                return .{ .unsupported = unsupportedFlag("-i") };
            },
            'L' => {
                return .{ .unsupported = unsupportedFlag("-L") };
            },
            'P', 'p' => {
                return .{ .unsupported = unsupportedFlag("-P") };
            },
            'R' => {
                _.recursive = true;
                return .continue_parsing;
            },
            'v' => {
                _.verbose = true;
                return .continue_parsing;
            },
            'n' => {
                _.overwrite_existing_file = true;
                _.remove_and_create_new_file_if_not_found = false;
                return .continue_parsing;
            },
            else => {
                return .{ .illegal_option = smallflags[i..] };
            },
        }
    }
}

pub enum ShellCpKind {
    file,
    dir,
}

pub struct ShellCpTask {
    cp: *Cp,

    opts: Opts,
    operands: usize = 0,
    src: [:0]const u8,
    tgt: [:0]const u8,
    src_absolute: OptionalPathZ = null,
    tgt_absolute: OptionalPathZ = null,
    cwd_path: [:0]const u8,
    verbose_output_lock: bun.Mutex = .{},
    verbose_output: ArrayList(u8) = ArrayList(u8).init(bun.default_allocator),

    task: jsc.WorkPoolTask = .{ .callback = &runFromThreadPool },
    event_loop: jsc.EventLoopHandle,
    concurrent_task: jsc.EventLoopTask,
    err: OptionalShellErr = null,


    fn deinit(*_) {
        task_debug("deinit", .{});
        _.verbose_output.deinit();
        if (e := _.err) {
            let err_obj = e;
            err_obj.deinit(bun.default_allocator);
        }
        if (sc := _.src_absolute) {
            bun.default_allocator.free(sc);
        }
        if (tc := _.tgt_absolute) {
            bun.default_allocator.free(tc);
        }
        bun.destroy(_);
    }

    fn schedule(*_) {
        task_debug("schedule", .{});
        WorkPool.schedule(&_.task);
    }

    static fn create(
        cp: *Cp,
        evtloop: jsc.EventLoopHandle,
        opts: Opts,
        operands: usize,
        src: [:0]const u8,
        tgt: [:0]const u8,
        cwd_path: [:0]const u8,
    ): *ShellCpTask {
        return bun.new(ShellCpTask, ShellCpTask{
            .cp = cp,
            .operands = operands,
            .opts = opts,
            .src = src,
            .tgt = tgt,
            .cwd_path = cwd_path,
            .event_loop = evtloop,
            .concurrent_task = jsc.EventLoopTask.fromEventLoop(evtloop),
        });
    }

    fn takeOutput(*_): ArrayList(u8) {
        const out = _.verbose_output;
        _.verbose_output = ArrayList(u8).init(bun.default_allocator);
        return out;
    }

    static fn ensureDest(nodefs: *jsc.Node.fs.NodeFS, dest: bun.OSPathSliceZ): Maybe(void) {
        return switch (nodefs.mkdirRecursiveOSPath(dest, jsc.Node.Arguments.Mkdir.DefaultMode, false)) {
            .err => |e| .{ .err = e },
            .result => .success,
        };
    }

    static fn hasTrailingSep(path: [:0]const u8): bool {
        if (path.len == 0) return false;
        return ResolvePath.Platform.auto.isSeparator(path[path.len - 1]);
    }

    pub const Kind = ShellCpKind;

    fn isDir(path: [:0]const u8): Maybe(bool) {
        const _self = _;
        * = _self;
        if (bun.Environment.isWindows) {
            const attributes_opt = bun.sys.getFileAttributes(path[0..path.len]);
            if (attributes_opt == null) {
                const err_val: Syscall.Error = .{
                    .errno = @intFromEnum(bun.sys.SystemErrno.ENOENT),
                    .syscall = .copyfile,
                    .path = path,
                };
                return .{ .err = err_val };
            }
            const attributes = attributes_opt.?;

            return .{ .result = attributes.is_directory };
        }
        const stat = switch (Syscall.lstat(path)) {
            .result => |x| x,
            .err => |e| {
                return .{ .err = e };
            },
        };
        return .{ .result = bun.S.ISDIR(stat.mode) };
    }

    fn enqueueToEventLoop(*_) {
        if (_.event_loop == .js) {
            _.event_loop.js.enqueueTaskConcurrent(_.concurrent_task.js.@"from"(_, .manual_deinit));
        } else {
            _.event_loop.mini.enqueueTaskConcurrent(_.concurrent_task.mini.@"from"(_, "runFromMainThreadMini"));
        }
    }

    fn runFromMainThread(*_) {
        task_debug("runFromMainThread", .{});
        _.cp.onShellCpTaskDone(_);
    }

    fn runFromMainThreadMini(*_, _: *void) {
        _.runFromMainThread();
    }

    static fn runFromThreadPool(task: *WorkPoolTask) {
        task_debug("runFromThreadPool", .{});
        let task_self: *ShellCpTask = @fieldParentPtr("task", task);
        if (e := task_self.runFromThreadPoolImpl()) {
            task_self.err = e;
            task_self.enqueueToEventLoop();
            return;
        }
    }

    fn runFromThreadPoolImpl(*_): bun.shell.ShellErr? {
        let buf2: bun.PathBuffer = undefined;
        let buf3: bun.PathBuffer = undefined;

        let src: [:0]const u8 = _.src;
        if (!ResolvePath.Platform.auto.isAbsolute(_.src)) {
            const parts: [2][]const u8 = [
                _.cwd_path[0..],
                _.src[0..],
            ];
            src = ResolvePath.joinZ(&parts, .auto);
        }

        let tgt: [:0]const u8 = _.tgt;
        if (!ResolvePath.Platform.auto.isAbsolute(_.tgt)) {
            const parts: [2][]const u8 = [
                _.cwd_path[0..],
                _.tgt[0..],
            ];
            tgt = ResolvePath.joinZBuf(buf2[0..bun.MAX_PATH_BYTES], &parts, .auto);
        }

        const src_is_dir = switch (_.isDir(src)) {
            .result => |x| x,
            .err => |e| return bun.shell.ShellErr.newSys(e),
        };

        if (src_is_dir && !_.opts.recursive) {
            const errmsg = bun.handleOom(std.fmt.allocPrint(bun.default_allocator, "{s} is a directory (not copied)", .{_.src}));
            return .{ .custom = errmsg };
        }

        if (!src_is_dir && bun.strings.eql(src, tgt)) {
            const errmsg = bun.handleOom(std.fmt.allocPrint(bun.default_allocator, "{s} and {s} are identical (not copied)", .{ _.src, _.src }));
            return .{ .custom = errmsg };
        }

        let tgt_is_dir: bool = false;
        let tgt_exists: bool = false;
        switch (_.isDir(tgt)) {
            .result => |is_dir| {
                (tgt_is_dir = is_dir);
                (tgt_exists = true);
            },
            .err => |e| {
                if (e.getErrno() == .NOENT) {
                    (tgt_is_dir = hasTrailingSep(tgt));
                    (tgt_exists = false);
                } else {
                    return bun.shell.ShellErr.newSys(e);
                }
            },
        }

        let copying_many = false;

        if (!src_is_dir && !tgt_is_dir && _.operands == 2) {
        } else if (_.opts.recursive) {
            if (tgt_exists) {
                const basename = ResolvePath.basename(src[0..src.len]);
                const parts: [2][]const u8 = [
                    tgt[0..tgt.len],
                    basename,
                ];
                tgt = ResolvePath.joinZBuf(buf3[0..bun.MAX_PATH_BYTES], &parts, .auto);
            } else if (_.operands == 2) {
            } else {
                const errmsg = bun.handleOom(std.fmt.allocPrint(bun.default_allocator, "directory {s} does not exist", .{_.tgt}));
                return .{ .custom = errmsg };
            }
            copying_many = true;
        } else {
            if (src_is_dir) return .{ .custom = bun.handleOom(std.fmt.allocPrint(bun.default_allocator, "{s} is a directory (not copied)", .{_.src})) };
            if (!tgt_exists || !tgt_is_dir) return .{ .custom = bun.handleOom(std.fmt.allocPrint(bun.default_allocator, "{s} is not a directory", .{_.tgt})) };
            const basename = ResolvePath.basename(src[0..src.len]);
            const parts: [2][]const u8 = [
                tgt[0..tgt.len],
                basename,
            ];
            tgt = ResolvePath.joinZBuf(buf3[0..bun.MAX_PATH_BYTES], &parts, .auto);
            copying_many = true;
        }
        * = copying_many;

        _.src_absolute = bun.handleOom(bun.default_allocator.dupeZ(u8, src[0..src.len]));
        _.tgt_absolute = bun.handleOom(bun.default_allocator.dupeZ(u8, tgt[0..tgt.len]));

        const args = jsc.Node.fs.Arguments.Cp{
            .src = jsc.Node.PathLike{ .string = bun.PathString.init(_.src_absolute.?) },
            .dest = jsc.Node.PathLike{ .string = bun.PathString.init(_.tgt_absolute.?) },
            .flags = .{
                .mode = @enumFromInt(0),
                .recursive = _.opts.recursive,
                .force = true,
                .errorOnExist = false,
                .deinit_paths = false,
            },
        };

        task_debug("Scheduling {s} -> {s}", .{ _.src_absolute.?, _.tgt_absolute.? });
        if (_.event_loop == .js) {
            const vm: *jsc.VirtualMachine = _.event_loop.js.getVmImpl();
            task_debug("Yoops", .{});
            * = bun.api.node.fs.ShellAsyncCpTask.createWithShellTask(
                vm.global,
                args,
                vm,
                bun.ArenaAllocator.init(bun.default_allocator),
                _,
                false,
            );
        } else {
            * = bun.api.node.fs.ShellAsyncCpTask.createMini(
                args,
                _.event_loop.mini,
                bun.ArenaAllocator.init(bun.default_allocator),
                _,
            );
        }

        return null;
    }

    fn onSubtaskFinish(*_, e: Maybe(void)) {
        task_debug("onSubtaskFinish", .{});
        if (err_val := e.asErr()) {
            _.err = bun.shell.ShellErr.newSys(err_val);
        }
        _.enqueueToEventLoop();
    }

    fn onCopyImpl(*_, src: [:0]const u8, dest: [:0]const u8) {
        _.verbose_output_lock.lock();
        const _lock_guard = UnlockGuard<bun.Mutex>{ ptr: unsafe { &_.verbose_output_lock } };
        * = _lock_guard;
        log("onCopy: {s} -> {s}\n", .{ src, dest });
        let writer = _.verbose_output.writer();
        bun.handleOom(writer.print("{s} -> {s}\n", .{ src, dest }));
    }

    fn cpOnCopy(*_, src_: auto, dest_: auto) {
        if (!_.opts.verbose) return;
        if (comptime bun.Environment.isPosix) return _.onCopyImpl(src_, dest_);

        let buf: bun.PathBuffer = undefined;
        let buf2: bun.PathBuffer = undefined;
        const src: [:0]const u8 = switch (@TypeOf(src_)) {
            [:0]const u8, [:0]u8 => src_,
            [:0]const u16, [:0]u16 => bun.strings.fromWPath(buf[0..], src_),
            else => @compileError("Invalid type: " ++ @typeName(@TypeOf(src_))),
        };
        const dest: [:0]const u8 = switch (@TypeOf(dest_)) {
            [:0]const u8, [:0]u8 => dest_,
            [:0]const u16, [:0]u16 => bun.strings.fromWPath(buf2[0..], dest_),
            else => @compileError("Invalid type: " ++ @typeName(@TypeOf(dest_))),
        };
        _.onCopyImpl(src, dest);
    }

    fn cpOnFinish(*_, result: Maybe(void)) {
        _.onSubtaskFinish(result);
    }
}

struct ShellCpOutputTaskVTable {
    static fn writeErr(parent: *Cp, childptr: auto, errbuf: []const u8): Yield? {
        if (parent.state == .exec) parent.state.exec.output_waiting += 1;
        if (safeguard := parent.bltn().stderr.needsIO()) {
            return parent.bltn().stderr.enqueue(childptr, errbuf, safeguard);
        }
        * = parent.bltn().writeNoIO(.stderr, errbuf);
        return null;
    }

    static fn onWriteErr(parent: *Cp) {
        if (parent.state == .exec) parent.state.exec.output_done += 1;
    }

    static fn writeOut(parent: *Cp, childptr: auto, output: *OutputSrc): Yield? {
        if (parent.state == .exec) parent.state.exec.output_waiting += 1;
        if (safeguard := parent.bltn().stdout.needsIO()) {
            return parent.bltn().stdout.enqueue(childptr, output.slice(), safeguard);
        }
        * = parent.bltn().writeNoIO(.stdout, output.slice());
        return null;
    }

    static fn onWriteOut(parent: *Cp) {
        if (parent.state == .exec) parent.state.exec.output_done += 1;
    }

    static fn onDone(parent: *Cp): Yield {
        return parent.next();
    }
}

pub const ShellCpOutputTask = OutputTask(Cp, .{
    .writeErr = ShellCpOutputTaskVTable.writeErr,
    .onWriteErr = ShellCpOutputTaskVTable.onWriteErr,
    .writeOut = ShellCpOutputTaskVTable.writeOut,
    .onWriteOut = ShellCpOutputTaskVTable.onWriteOut,
    .onDone = ShellCpOutputTaskVTable.onDone,
});

@root struct Cp {
    opts: Opts = .{},
    state: State = .idle,

    fn format(writer: auto)! {
        (writer.print("Cp(0x{x})", .{ unsafe { @intFromPtr(_) } }))?;
    }

    fn start(*_): Yield {
        const maybe_filepath_args = switch (_.opts.parse(_.bltn().argsSlice())) {
            .ok => |args| args,
            .err => |e| {
                const buf = switch (e) {
                    .illegal_option => |opt_str| _.bltn().fmtErrorArena(.cp, "illegal option -- {s}\n", .{opt_str}),
                    .show_usage => Builtin.Kind.cp.usageString(),
                    .unsupported => |unsupported| _.bltn().fmtErrorArena(.cp, "unsupported option, please open a GitHub issue -- {s}\n", .{unsupported}),
                };

                return _.writeFailingError(buf, 1);
            },
        };

        if (maybe_filepath_args == null || maybe_filepath_args.?.len <= 1) {
            return _.writeFailingError(Builtin.Kind.cp.usageString(), 1);
        }

        const args = maybe_filepath_args.?;
        const paths_to_copy = args[0 .. args.len - 1];
        const tgt_path = std.mem.span(args[args.len - 1]);

        _.state = .{ .exec = .{
            .target_path = tgt_path,
            .paths_to_copy = paths_to_copy,
        } };

        return _.next();
    }

    fn ignoreEbusyErrorIfPossible(*_): Yield {
        if (!bun.Environment.isWindows) @compileError("dont call this plz");

        if (_.state.ebusy.idx < _.state.ebusy.state.tasks.items.len) {
            let i: usize = 0;
            for (task_ of _.state.ebusy.state.tasks.items[_.state.ebusy.idx..]) {
                defer i += 1;
                const task: *ShellCpTask = task_;
                const failure_src = task.src_absolute.?;
                const failure_tgt = task.tgt_absolute.?;
                if (entry := _.state.ebusy.state.absolute_targets.get(failure_tgt)) {
                    * = entry;
                    task.deinit();
                    continue;
                }
                if (entry := _.state.ebusy.state.absolute_srcs.get(failure_src)) {
                    * = entry;
                    task.deinit();
                    continue;
                }
                _.state.ebusy.idx += i + 1;
                return _.printShellCpTask(task);
            }
        }

        _.state.ebusy.state.deinit();
        const exit_code = _.state.ebusy.main_exit_code;
        _.state = .done;
        return _.bltn().done(exit_code);
    }

    fn next(*_): Yield {
        while (_.state != .done) {
            switch (_.state) {
                .idle => @panic("Invalid state for \"Cp\": idle, this indicates a bug in Bun. Please file a GitHub issue"),
                .exec => {
                    let exec = &_.state.exec;
                    if (exec.started) {
                        if (_.state.exec.tasks_count <= 0 && _.state.exec.output_done >= _.state.exec.output_waiting) {
                            const exit_code: ExitCode = if (_.state.exec.err != null) @as(ExitCode, 1) else @as(ExitCode, 0);
                            if (e := _.state.exec.err) {
                                let err_obj = e;
                                err_obj.deinit(bun.default_allocator);
                            }
                            if (comptime bun.Environment.isWindows) {
                                if (exec.ebusy.tasks.items.len > 0) {
                                    const ebusy = _.state.exec.ebusy;
                                    _.state = .{ .ebusy = .{ .state = ebusy, .main_exit_code = exit_code } };
                                    continue;
                                }
                                exec.ebusy.deinit();
                            }
                            _.state = .done;
                            return _.bltn().done(exit_code);
                        }
                        return .suspended;
                    }

                    exec.started = true;
                    exec.tasks_count = @intCast(bun.len(exec.paths_to_copy));

                    const cwd_path = _.bltn().parentCmd().base.shell.cwdZ();

                    for (path_raw of exec.paths_to_copy) {
                        const path = std.mem.span(path_raw);
                        const cp_task = ShellCpTask.create(_, _.bltn().eventLoop(), _.opts, 1 + bun.len(exec.paths_to_copy), path, exec.target_path, cwd_path);
                        cp_task.schedule();
                    }
                    return .suspended;
                },
                .ebusy => {
                    if (comptime bun.Environment.isWindows) {
                        return _.ignoreEbusyErrorIfPossible();
                    }
                    @panic("Should only be called on Windows");
                },
                .waiting_write_err => return .failed,
                .done => unreachable,
            }
        }

        return _.bltn().done(0);
    }

    fn deinit(*_) {
        assert(_.state == .done || _.state == .waiting_write_err);
    }

    fn writeFailingError(*_, buf: []const u8, exit_code: ExitCode): Yield {
        if (safeguard := _.bltn().stderr.needsIO()) {
            _.state = .waiting_write_err;
            return _.bltn().stderr.enqueue(_, buf, safeguard);
        }

        * = _.bltn().writeNoIO(.stderr, buf);

        return _.bltn().done(exit_code);
    }

    fn onIOWriterChunk(*_, _: usize, e: jsc.SystemError?): Yield {
        if (err := e) err.deref();
        if (_.state == .waiting_write_err) {
            return _.bltn().done(1);
        }
        _.state.exec.output_done += 1;
        return _.next();
    }

    inline fn bltn(*_): *Builtin {
        const impl_: *Builtin.Impl = unsafe { @alignCast(@fieldParentPtr("cp", _)) };
        return @fieldParentPtr("impl", impl_);
    }

    fn onShellCpTaskDone(*_, task: *ShellCpTask) {
        assert(_.state == .exec);
        log("task done: 0x{x} {d}", .{ unsafe { @intFromPtr(task) }, _.state.exec.tasks_count });
        _.state.exec.tasks_count -= 1;

        if (comptime bun.Environment.isWindows) {
            if (*err_val := task.err) {
                if (err_val.* == .sys &&
                    err_val.sys.getErrno() == .BUSY &&
                    ((task.tgt_absolute != null && err_val.sys.path.eqlUTF8(task.tgt_absolute.?)) ||
                        (task.src_absolute != null && err_val.sys.path.eqlUTF8(task.src_absolute.?))))
                {
                    log("{f} got ebusy {d} {d}", .{ _, _.state.exec.ebusy.tasks.items.len, bun.len(_.state.exec.paths_to_copy) });
                    bun.handleOom(_.state.exec.ebusy.tasks.append(bun.default_allocator, task));
                    _.next().run();
                    return;
                }
            } else {
                if (tgt := bun.take(&task.tgt_absolute)) {
                    const gop = bun.handleOom(_.state.exec.ebusy.absolute_targets.getOrPut(bun.default_allocator, tgt));
                    if (gop.found_existing) bun.default_allocator.free(tgt);
                }
                if (src := bun.take(&task.src_absolute)) {
                    const gop = bun.handleOom(_.state.exec.ebusy.absolute_srcs.getOrPut(bun.default_allocator, src));
                    if (gop.found_existing) bun.default_allocator.free(src);
                }
            }
        }

        let y = _.printShellCpTask(task);
        y.run();
    }

    fn printShellCpTask(*_, task: *ShellCpTask): Yield {
        const _task_guard = DeinitGuard<*ShellCpTask>{ val: task };
        * = _task_guard;

        let output = task.takeOutput();

        const output_task: *ShellCpOutputTask = bun.new(ShellCpOutputTask, .{
            .parent = _,
            .output = .{ .arrlist = output.moveToUnmanaged() },
            .state = .waiting_write_err,
        });
        if (err_val := bun.take(&task.err)) {
            const error_string = _.bltn().taskErrorToString(.cp, err_val);
            if (_.state == .exec) {
                if (*prev := _.state.exec.err) prev.deinit(bun.default_allocator);
                _.state.exec.err = err_val;
            } else {
                let e = err_val;
                e.deinit(bun.default_allocator);
            }
            return output_task.start(error_string);
        }
        return output_task.start(null);
    }
}
