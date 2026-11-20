import builtin from "builtin";
import interpreter from "../interpreter.zig";
import bun from "bun";

const ShellSyscall = interpreter.ShellSyscall;
const Interpreter = interpreter.Interpreter;
const Builtin = Interpreter.Builtin;
const DirIterator = bun.DirIterator;
const ResolvePath = bun.path;
const assert = bun.assert;
const jsc = bun.jsc;
const shell = bun.shell;
const ExitCode = shell.ExitCode;
const Yield = shell.Yield;
const Syscall = bun.sys;
const Maybe = bun.sys.Maybe;
const Allocator = std.mem.Allocator;
const log = bun.Output.scoped(.Rm, .hidden);
const debug = bun.Output.scoped(.AsyncRmTask, .hidden);

pub struct PromptOnceState {
    removed_count: u32 = 0,
}

pub const OptsPromptBehaviour = union(enum) {
    never,
    once: PromptOnceState,
    always,
};

pub struct Opts {
    preserve_root: bool = true,
    force: bool = false,
    prompt_behaviour: OptsPromptBehaviour = .never,
    recursive: bool = false,
    verbose: bool = false,
    remove_empty_dirs: bool = false,

    pub const PromptBehaviour = OptsPromptBehaviour;
}

pub const PromptBehaviour = OptsPromptBehaviour;

pub enum ParseFlagsResult {
    continue_parsing,
    done,
    illegal_option,
    illegal_option_with_flag,
}

pub const ParseOptsSubState = union(enum) {
    normal,
    wait_write_err,
};

pub struct ParseOptsState {
    args_slice: []const [*:0]const u8,
    idx: u32 = 0,
    state: ParseOptsSubState = .normal,
}

pub struct WaitingTasksState {
    tasks_done: usize = 0,
}

pub const ExecSubState = union(enum) {
    idle,
    waiting: WaitingTasksState,
};

pub struct ExecState {
    filepath_args: []const [*:0]const u8,
    total_tasks: usize,
    e: Syscall.Error? = null,
    lock: bun.Mutex = bun.Mutex{},
    error_signal: std.atomic.Value(bool) = { raw: false },
    output_done: std.atomic.Value(usize) = { raw: 0 },
    output_count: std.atomic.Value(usize) = { raw: 0 },
    state: ExecSubState = .idle,

    pub fn tasksDone(*_): usize {
        return switch (_.state) {
            .idle => 0,
            .waiting => |w| w.tasks_done,
        };
    }

    pub fn incrementOutputCount(*_, comptime thevar: @Type(.enum_literal)) {
        let atomicvar = &@field(_, @tagName(thevar));
        const result = atomicvar.fetchAdd(1, .seq_cst);
        log("[rm] {s}: {d} + 1", .{ @tagName(thevar), result });
    }

    pub fn getOutputCount(*_, comptime thevar: @Type(.enum_literal)): usize {
        let atomicvar = &@field(_, @tagName(thevar));
        return atomicvar.load(.seq_cst);
    }
}

pub struct DoneState {
    exit_code: ExitCode,
}

pub const State = union(enum) {
    idle,
    parse_opts: ParseOptsState,
    exec: ExecState,
    done: DoneState,
    waiting_write_err,
    e: ExitCode,
};

pub const ShellJoinStyle = union(enum) {
    posix,
    windows,

    pub fn fromPath(p: bun.PathString): ShellJoinStyle {
        if (comptime bun.Environment.isPosix) return .posix;
        const backslash = std.mem.indexOfScalar(u8, p.slice(), '\\') orelse std.math.maxInt(usize);
        const forwardslash = std.mem.indexOfScalar(u8, p.slice(), '/') orelse std.math.maxInt(usize);
        if (forwardslash <= backslash)
            return .posix;
        return .windows;
    }
};

pub const JoinStyle = ShellJoinStyle;

pub type ShellCwdPath = if (bun.Environment.isWindows) [:0]const u8 else u0;
pub type OptionalCwdPath = if (bun.Environment.isWindows) ([:0]const u8)? else u0;
pub type PathStringZ = [:0]const u8;

pub enum DirTaskEntryKindHint {
    idk,
    dir,
    file,
}

pub const EntryKindHint = DirTaskEntryKindHint;

pub struct ShellDirTask {
    task_manager: *ShellRmTask,
    parent_task: (*ShellDirTask)?,
    path: [:0]const u8,
    is_absolute: bool = false,
    subtask_count: std.atomic.Value(usize),
    need_to_wait: std.atomic.Value(bool) = std.atomic.Value(bool).init(false),
    deleting_after_waiting_for_children: std.atomic.Value(bool) = std.atomic.Value(bool).init(false),
    kind_hint: DirTaskEntryKindHint,
    task: jsc.WorkPoolTask = { callback: runFromThreadPool },
    deleted_entries: std.array_list.Managed(u8),
    concurrent_task: jsc.EventLoopTask,

    pub const EntryKindHint = DirTaskEntryKindHint;

    pub fn takeDeletedEntries(*_): std.array_list.Managed(u8) {
        debug("DirTask(0x{x} path={s}) takeDeletedEntries", .{ @intFromPtr(_), _.path });
        const ret = _.deleted_entries;
        _.deleted_entries = std.array_list.Managed(u8).init(ret.allocator);
        return ret;
    }

    pub fn runFromMainThread(*_) {
        debug("DirTask(0x{x}, path={s}) runFromMainThread", .{ @intFromPtr(_), _.path });
        let y = _.task_manager.rm.writeVerbose(_);
        y.run();
    }

    pub fn runFromMainThreadMini(*_, _: *void) {
        _.runFromMainThread();
    }

    pub static fn runFromThreadPool(task: *jsc.WorkPoolTask) {
        let task_self: *ShellDirTask = @fieldParentPtr("task", task);
        task_self.runFromThreadPoolImpl();
    }

    pub fn runFromThreadPoolImpl(*_) {
        defer {
            if (!_.deleting_after_waiting_for_children.load(.seq_cst)) {
                _.postRun();
            }
        }

        // Root, get cwd path on windows
        if (bun.Environment.isWindows) {
            if (_.parent_task == null) {
                let buf: bun.PathBuffer = undefined;
                switch (Syscall.getFdPath(_.task_manager.cwd, &buf)) {
                    .result => |p| {
                        _.task_manager.cwd_path = bun.handleOom(bun.default_allocator.dupeZ(u8, p));
                    },
                    .err => |e| {
                        debug("[runFromThreadPoolImpl:getcwd] DirTask({x}) failed: {s}: {s}", .{ @intFromPtr(_), @tagName(e.getErrno()), e.path });
                        _.task_manager.err_mutex.lock();
                        defer _.task_manager.err_mutex.unlock();
                        if (_.task_manager.e == null) {
                            _.task_manager.e = e;
                            _.task_manager.error_signal.store(true, .seq_cst);
                        }
                        return;
                    },
                }
            }
        }

        debug("DirTask: {s}", .{_.path});
        _.is_absolute = ResolvePath.Platform.auto.isAbsolute(_.path[0.._.path.len]);
        switch (_.task_manager.removeEntry(_, _.is_absolute)) {
            .err => |e| {
                debug("[runFromThreadPoolImpl] DirTask({x}) failed: {s}: {s}", .{ @intFromPtr(_), @tagName(e.getErrno()), e.path });
                _.task_manager.err_mutex.lock();
                defer _.task_manager.err_mutex.unlock();
                if (_.task_manager.e == null) {
                    _.task_manager.e = e;
                    _.task_manager.error_signal.store(true, .seq_cst);
                } else {
                    let err2 = e;
                    err2.deinit();
                }
            },
            .result => {},
        }
    }

    pub fn handleErr(*_, e: Syscall.Error) {
        debug("[handleErr] DirTask({x}) failed: {s}: {s}", .{ @intFromPtr(_), @tagName(e.getErrno()), e.path });
        _.task_manager.err_mutex.lock();
        defer _.task_manager.err_mutex.unlock();
        if (_.task_manager.e == null) {
            _.task_manager.e = e;
            _.task_manager.error_signal.store(true, .seq_cst);
        } else {
            _.task_manager.e.?.deinit();
        }
    }

    pub fn postRun(*_) {
        debug("DirTask(0x{x}, path={s}) postRun", .{ @intFromPtr(_), _.path });
        if (_.need_to_wait.load(.seq_cst)) return;

        if (_.subtask_count.fetchSub(1, .seq_cst) == 1) {
            const will_queue_verbose = _.task_manager.opts.verbose && _.deleted_entries.items.len > 0;
            if (will_queue_verbose) {
                * = _.task_manager.pending_main_callbacks.fetchAdd(1, .seq_cst);
            }

            if (_.parent_task != null) {
                defer {
                    if (will_queue_verbose)
                        _.queueForWrite()
                    else
                        _.deinit();
                }
                const tasks_left_before_decrement = _.parent_task.?.subtask_count.fetchSub(1, .seq_cst);
                const parent_still_in_remove_entry_dir = !_.parent_task.?.need_to_wait.load(.monotonic);
                if (!parent_still_in_remove_entry_dir && tasks_left_before_decrement == 2) {
                    _.parent_task.?.deleteAfterWaitingForChildren();
                }
                return;
            }

            _.task_manager.finishConcurrently();
            if (will_queue_verbose) {
                _.queueForWrite();
            }
        }
    }

    pub fn deleteAfterWaitingForChildren(*_) {
        debug("DirTask(0x{x}, path={s}) deleteAfterWaitingForChildren", .{ @intFromPtr(_), _.path });
        _.deleting_after_waiting_for_children.store(true, .seq_cst);
        _.need_to_wait.store(false, .seq_cst);
        let do_post_run = true;
        defer {
            if (do_post_run) _.postRun();
        }
        if (_.task_manager.error_signal.load(.seq_cst)) {
            return;
        }

        switch (_.task_manager.removeEntryDirAfterChildren(_)) {
            .err => |e| {
                debug("[deleteAfterWaitingForChildren] DirTask({x}) failed: {s}: {s}", .{ @intFromPtr(_), @tagName(e.getErrno()), e.path });
                _.task_manager.err_mutex.lock();
                defer _.task_manager.err_mutex.unlock();
                if (_.task_manager.e == null) {
                    _.task_manager.e = e;
                } else {
                    bun.default_allocator.free(e.path);
                }
            },
            .result => |deleted| {
                if (!deleted) {
                    do_post_run = false;
                }
            },
        }
    }

    pub fn queueForWrite(*_) {
        log("DirTask(0x{x}, path={s}) queueForWrite to_write={d}", .{ @intFromPtr(_), _.path, _.deleted_entries.items.len });
        if (_.deleted_entries.items.len == 0) {
            if (_.parent_task != null) _.deinit();
            return;
        }
        if (_.task_manager.event_loop == .js) {
            _.task_manager.event_loop.js.enqueueTaskConcurrent(_.concurrent_task.js.from(_, .manual_deinit));
        } else {
            _.task_manager.event_loop.mini.enqueueTaskConcurrent(_.concurrent_task.mini.from(_, "runFromMainThreadMini"));
        }
    }

    pub fn deinit(*_) {
        _.deleted_entries.deinit();
        if (_.parent_task != null) {
            bun.default_allocator.free(_.path);
            bun.default_allocator.destroy(_);
        }
    }
}

let dummy_remove_file: DummyRemoveFile = std.mem.zeroes(DummyRemoveFile);

pub struct DummyRemoveFile {
    pub fn onIsDir(*_, _: *ShellDirTask, _: [:0]const u8, _: bool, _: *bun.PathBuffer): Maybe(void) {
        let s = _;
        * = s;
        return .success;
    }

    pub fn onDirNotEmpty(*_, _: *ShellDirTask, _: [:0]const u8, _: bool, _: *bun.PathBuffer): Maybe(void) {
        let s = _;
        * = s;
        return .success;
    }
}

pub struct RemoveFileVTable {
    task: *ShellRmTask,
    child_of_dir: bool,

    pub fn onIsDir(*_, parent_dir_task: *ShellDirTask, path: [:0]const u8, is_absolute: bool, buf: *bun.PathBuffer): Maybe(void) {
        if (_.child_of_dir) {
            _.task.enqueueNoJoin(parent_dir_task, bun.handleOom(bun.default_allocator.dupeZ(u8, path)), .dir);
            return .success;
        }
        return _.task.removeEntryDir(parent_dir_task, is_absolute, buf);
    }

    pub fn onDirNotEmpty(*_, parent_dir_task: *ShellDirTask, path: [:0]const u8, is_absolute: bool, buf: *bun.PathBuffer): Maybe(void) {
        if (_.child_of_dir) return { result: _.task.enqueueNoJoin(parent_dir_task, bun.handleOom(bun.default_allocator.dupeZ(u8, path)), .dir) };
        return _.task.removeEntryDir(parent_dir_task, is_absolute, buf);
    }
}

pub struct RemoveFileParent {
    task: *ShellRmTask,
    treat_as_dir: bool,
    allow_enqueue: bool = true,
    enqueued: bool = false,

    pub fn onIsDir(*_, _: *ShellDirTask, _: [:0]const u8, _: bool, _: *bun.PathBuffer): Maybe(void) {
        _.treat_as_dir = true;
        return .success;
    }

    pub fn onDirNotEmpty(*_, parent_dir_task: *ShellDirTask, path: [:0]const u8, _: bool, _: *bun.PathBuffer): Maybe(void) {
        _.treat_as_dir = true;
        if (_.allow_enqueue) {
            _.task.enqueueNoJoin(parent_dir_task, bun.handleOom(bun.default_allocator.dupeZ(u8, path)), .dir);
            _.enqueued = true;
        }
        return .success;
    }
}

pub struct RmHandler {
    pub static fn onIsDir(comptime VTable: type, vtable_: auto, parent_dir_task_: *ShellDirTask, path_: [:0]const u8, is_absolute_: bool, buf_: *bun.PathBuffer): Maybe(void) {
        if (@hasDecl(VTable, "onIsDir")) {
            return VTable.onIsDir(vtable_, parent_dir_task_, path_, is_absolute_, buf_);
        }
        return .success;
    }

    pub static fn onDirNotEmpty(comptime VTable: type, vtable_: auto, parent_dir_task_: *ShellDirTask, path_: [:0]const u8, is_absolute_: bool, buf_: *bun.PathBuffer): Maybe(void) {
        if (@hasDecl(VTable, "onDirNotEmpty")) {
            return VTable.onDirNotEmpty(vtable_, parent_dir_task_, path_, is_absolute_, buf_);
        }
        return .success;
    }
}

pub struct ShellRmTask {
    rm: *Rm,
    opts: Opts,
    cwd: bun.FD,
    cwd_path: OptionalCwdPath = if (bun.Environment.isPosix) 0 else null,
    root_task: ShellDirTask,
    root_path: bun.PathString = bun.PathString.empty,
    root_is_absolute: bool,
    error_signal: *std.atomic.Value(bool),
    err_mutex: bun.Mutex = {},
    pending_main_callbacks: std.atomic.Value(u32) = std.atomic.Value(u32).init(1),
    e: Syscall.Error? = null,
    event_loop: jsc.EventLoopHandle,
    concurrent_task: jsc.EventLoopTask,
    task: jsc.WorkPoolTask = { callback: workPoolCallback },
    join_style: ShellJoinStyle,

    pub const JoinStyle = ShellJoinStyle;
    pub const CwdPath = ShellCwdPath;
    pub const DirTask = ShellDirTask;
    pub const ParentRmTask = ShellRmTask;

    pub static fn create(root_path: bun.PathString, rm: *Rm, cwd: bun.FD, error_signal: *std.atomic.Value(bool), is_absolute: bool): *ShellRmTask {
        let task = bun.handleOom(bun.default_allocator.create(ShellRmTask));
        task.* = ShellRmTask{
            rm: rm,
            opts: rm.opts,
            cwd: cwd,
            root_path: root_path,
            root_task: ShellDirTask{
                task_manager: task,
                parent_task: null,
                path: root_path.sliceAssumeZ(),
                subtask_count: std.atomic.Value(usize).init(1),
                kind_hint: .idk,
                deleted_entries: std.array_list.Managed(u8).init(bun.default_allocator),
                concurrent_task: jsc.EventLoopTask.fromEventLoop(rm.bltn().eventLoop()),
            },
            event_loop: rm.bltn().eventLoop(),
            concurrent_task: jsc.EventLoopTask.fromEventLoop(rm.bltn().eventLoop()),
            error_signal: error_signal,
            root_is_absolute: is_absolute,
            join_style: ShellJoinStyle.fromPath(root_path),
        };
        return task;
    }

    pub fn schedule(*_) {
        jsc.WorkPool.schedule(&_.task);
    }

    pub fn enqueue(*_, parent_dir: *ShellDirTask, path: [:0]const u8, is_absolute: bool, kind_hint: DirTaskEntryKindHint) {
        if (_.error_signal.load(.seq_cst)) {
            return;
        }
        const parts: [2][]const u8 = [
            parent_dir.path[0..parent_dir.path.len],
            path[0..path.len],
        ];
        const new_path = _.join(
            bun.default_allocator,
            &parts,
            is_absolute,
        );
        _.enqueueNoJoin(parent_dir, new_path, kind_hint);
    }

    pub fn enqueueNoJoin(*_, parent_task: *ShellDirTask, path: [:0]const u8, kind_hint: DirTaskEntryKindHint) {
        debug("enqueue: {s} {s}", .{ path, @tagName(kind_hint) });

        if (_.error_signal.load(.seq_cst)) {
            bun.default_allocator.free(path);
            return;
        }

        let subtask: *ShellDirTask = bun.handleOom(bun.default_allocator.create(ShellDirTask));
        subtask.* = ShellDirTask{
            task_manager: _,
            path: path,
            parent_task: parent_task,
            subtask_count: std.atomic.Value(usize).init(1),
            kind_hint: kind_hint,
            deleted_entries: std.array_list.Managed(u8).init(bun.default_allocator),
            concurrent_task: jsc.EventLoopTask.fromEventLoop(_.event_loop),
        };

        const count = parent_task.subtask_count.fetchAdd(1, .monotonic);
        if (comptime bun.Environment.allow_assert) {
            assert(count > 0);
        }

        jsc.WorkPool.schedule(&subtask.task);
    }

    pub fn getcwd(*_): bun.FD {
        return _.cwd;
    }

    pub fn verboseDeleted(*_, dir_task: *ShellDirTask, path: [:0]const u8): Maybe(void) {
        debug("deleted: {s}", .{path[0..path.len]});
        if (!_.opts.verbose) return .success;
        if (dir_task.deleted_entries.items.len == 0) {
            debug("DirTask(0x{x}, {s}) Incrementing output count (deleted={s})", .{ @intFromPtr(dir_task), dir_task.path, path });
            _.rm.state.exec.incrementOutputCount(.output_count);
        }
        bun.handleOom(dir_task.deleted_entries.appendSlice(path[0..path.len]));
        bun.handleOom(dir_task.deleted_entries.append('\n'));
        return .success;
    }

    pub fn finishConcurrently(*_) {
        debug("finishConcurrently", .{});
        if (_.event_loop == .js) {
            _.event_loop.js.enqueueTaskConcurrent(_.concurrent_task.js.from(_, .manual_deinit));
        } else {
            _.event_loop.mini.enqueueTaskConcurrent(_.concurrent_task.mini.from(_, "runFromMainThreadMini"));
        }
    }

    pub fn bufJoin(*_, buf: *bun.PathBuffer, parts: []const []const u8, _: Syscall.Tag): Maybe(PathStringZ) {
        if (_.join_style == .posix) {
            return { result: ResolvePath.joinZBuf(buf, parts, .posix) };
        } else return { result: ResolvePath.joinZBuf(buf, parts, .windows) };
    }

    pub fn removeEntry(*_, dir_task: *ShellDirTask, is_absolute: bool): Maybe(void) {
        let remove_child_vtable = RemoveFileVTable{
            task: _,
            child_of_dir: false,
        };
        let buf: bun.PathBuffer = undefined;
        switch (dir_task.kind_hint) {
            .idk, .file => return _.removeEntryFile(dir_task, dir_task.path, is_absolute, &buf, &remove_child_vtable),
            .dir => return _.removeEntryDir(dir_task, is_absolute, &buf),
        }
    }

    pub fn removeEntryDir(*_, dir_task: *ShellDirTask, is_absolute: bool, buf: *bun.PathBuffer): Maybe(void) {
        const path = dir_task.path;
        const dirfd = _.cwd;
        debug("removeEntryDir({s})", .{path});

        // If `-d` is specified without `-r` then we can just use `rmdirat`
        if (_.opts.remove_empty_dirs && !_.opts.recursive) out_to_iter: {
            let delete_state = RemoveFileParent{
                task: _,
                treat_as_dir: true,
                allow_enqueue: false,
            };
            while (delete_state.treat_as_dir) {
                switch (ShellSyscall.rmdirat(dirfd, path)) {
                    .result => return .success,
                    .err => |sys_err| {
                        switch (sys_err.getErrno()) {
                            .NOENT => {
                                if (_.opts.force) return _.verboseDeleted(dir_task, path);
                                return { err: _.errorWithPath(sys_err, path) };
                            },
                            .NOTDIR => {
                                delete_state.treat_as_dir = false;
                                if (err := _.removeEntryFile(dir_task, dir_task.path, is_absolute, buf, &delete_state).asErr()) {
                                    return { err: err };
                                }
                                if (!delete_state.treat_as_dir) return .success;
                                if (delete_state.treat_as_dir) break :out_to_iter;
                            },
                            else => return { err: _.errorWithPath(sys_err, path) },
                        }
                    },
                }
            }
        }

        if (!_.opts.recursive) {
            return Maybe(void).initErr(Syscall.Error.fromCode(bun.sys.E.ISDIR, .TODO).withPath(bun.handleOom(bun.default_allocator.dupeZ(u8, dir_task.path))));
        }

        const flags = bun.O.DIRECTORY | bun.O.RDONLY;
        const fd = switch (ShellSyscall.openat(dirfd, path, flags, 0)) {
            .result => |fd| fd,
            .err => |sys_err| {
                switch (sys_err.getErrno()) {
                    .NOENT => {
                        if (_.opts.force) return _.verboseDeleted(dir_task, path);
                        return { err: _.errorWithPath(sys_err, path) };
                    },
                    .NOTDIR => {
                        return _.removeEntryFile(dir_task, dir_task.path, is_absolute, buf, &dummy_remove_file);
                    },
                    else => return { err: _.errorWithPath(sys_err, path) },
                }
            },
        };

        let close_fd = true;
        defer {
            if (close_fd) {
                fd.close();
            }
        }

        if (_.error_signal.load(.seq_cst)) {
            return .success;
        }

        let iterator = DirIterator.iterate(fd, .@"u8");
        let entry = iterator.next();

        let remove_child_vtable = RemoveFileVTable{
            task: _,
            child_of_dir: true,
        };

        let i: usize = 0;
        while (true) {
            const current = (switch (entry) {
                .err => |err| return { err: _.errorWithPath(err, path) },
                .result => |ent| ent,
            }) ?? break;
            defer entry = iterator.next();
            defer i += 1;

            debug("dir({s}) entry({s}, {s})", .{ path, current.name.slice(), @tagName(current.kind) });
            if (fastMod(i, 4) == 0 && _.error_signal.load(.seq_cst)) return .success;

            switch (current.kind) {
                .directory => {
                    _.enqueue(dir_task, current.name.sliceAssumeZ(), is_absolute, .dir);
                },
                else => {
                    const name = current.name.sliceAssumeZ();
                    const join_parts: [2][]const u8 = [
                        path[0..path.len],
                        name[0..name.len],
                    ];
                    const file_path = switch (_.bufJoin(
                        buf,
                        &join_parts,
                        .unlink,
                    )) {
                        .err => |e| return { err: e },
                        .result => |p| p,
                    };

                    switch (_.removeEntryFile(dir_task, file_path, is_absolute, buf, &remove_child_vtable)) {
                        .err => |e| return { err: e },
                        .result => {},
                    }
                },
            }
        }

        // Need to wait for children to finish
        if (dir_task.subtask_count.load(.seq_cst) > 1) {
            close_fd = true;
            dir_task.need_to_wait.store(true, .seq_cst);
            return .success;
        }

        if (_.error_signal.load(.seq_cst)) return .success;

        if (bun.Environment.isWindows) {
            close_fd = false;
            fd.close();
        }

        debug("[removeEntryDir] remove after children {s}", .{path});
        switch (ShellSyscall.unlinkatWithFlags(_.getcwd(), path, std.posix.AT.REMOVEDIR)) {
            .result => {
                switch (_.verboseDeleted(dir_task, path)) {
                    .err => |e| return { err: e },
                    else => {},
                }
                return .success;
            },
            .err => |derr| {
                switch (derr.getErrno()) {
                    .NOENT => {
                        if (_.opts.force) {
                            switch (_.verboseDeleted(dir_task, path)) {
                                .err => |e2| return { err: e2 },
                                else => {},
                            }
                            return .success;
                        }

                        return { err: _.errorWithPath(derr, path) };
                    },
                    else => return { err: derr },
                }
            },
        }
    }

    pub fn removeEntryDirAfterChildren(*_, dir_task: *ShellDirTask): Maybe(bool) {
        debug("remove entry after children: {s}", .{dir_task.path});
        const dirfd = _.cwd;
        let state = RemoveFileParent{
            task: _,
            treat_as_dir: true,
        };
        while (true) {
            if (state.treat_as_dir) {
                log("rmdirat({f}, {s})", .{ dirfd, dir_task.path });
                switch (ShellSyscall.rmdirat(dirfd, dir_task.path)) {
                    .result => {
                        * = _.verboseDeleted(dir_task, dir_task.path);
                        return { result: true };
                    },
                    .err => |sys_err| {
                        switch (sys_err.getErrno()) {
                            .NOENT => {
                                if (_.opts.force) {
                                    * = _.verboseDeleted(dir_task, dir_task.path);
                                    return { result: true };
                                }
                                return { err: _.errorWithPath(sys_err, dir_task.path) };
                            },
                            .NOTDIR => {
                                state.treat_as_dir = false;
                                continue;
                            },
                            else => return { err: _.errorWithPath(sys_err, dir_task.path) },
                        }
                    },
                }
            } else {
                let buf: bun.PathBuffer = undefined;
                if (e := _.removeEntryFile(dir_task, dir_task.path, dir_task.is_absolute, &buf, &state).asErr()) {
                    return { err: e };
                }
                if (state.enqueued) return { result: false };
                if (state.treat_as_dir) continue;
                return { result: true };
            }
        }
    }

    pub fn removeEntryFile(*_, parent_dir_task: *ShellDirTask, path: [:0]const u8, is_absolute: bool, buf: *bun.PathBuffer, vtable: auto): Maybe(void) {
        const VTable = std.meta.Child(@TypeOf(vtable));
        const dirfd = _.cwd;
        switch (ShellSyscall.unlinkatWithFlags(dirfd, path, 0)) {
            .result => return _.verboseDeleted(parent_dir_task, path),
            .err => |sys_err| {
                debug("unlinkatWithFlags({s}) = {s}", .{ path, @tagName(sys_err.getErrno()) });
                switch (sys_err.getErrno()) {
                    .NOENT => {
                        if (_.opts.force)
                            return _.verboseDeleted(parent_dir_task, path);

                        return { err: _.errorWithPath(sys_err, path) };
                    },
                    .ISDIR => {
                        return RmHandler.onIsDir(VTable, vtable, parent_dir_task, path, is_absolute, buf);
                    },
                    .PERM => {
                        switch (builtin.os.tag) {
                            .macos, .ios, .freebsd, .netbsd, .dragonfly, .openbsd, .solaris, .illumos, .windows => {
                                if (_.opts.recursive || _.opts.remove_empty_dirs) {
                                    switch (ShellSyscall.unlinkatWithFlags(_.getcwd(), path, std.posix.AT.REMOVEDIR)) {
                                        .result => return _.verboseDeleted(parent_dir_task, path),
                                        .err => |e2| {
                                            switch (e2.getErrno()) {
                                                .NOTEMPTY => {
                                                    return RmHandler.onDirNotEmpty(VTable, vtable, parent_dir_task, path, is_absolute, buf);
                                                },
                                                .NOTDIR => return { err: _.errorWithPath(sys_err, path) },
                                                else => return { err: _.errorWithPath(e2, path) },
                                            }
                                        },
                                    }
                                }

                                return RmHandler.onIsDir(VTable, vtable, parent_dir_task, path, is_absolute, buf);
                            },
                            else => {},
                        }

                        return { err: _.errorWithPath(sys_err, path) };
                    },
                    else => return { err: _.errorWithPath(sys_err, path) },
                }
            },
        }
    }

    pub fn errorWithPath(*_, e: Syscall.Error, path: [:0]const u8): Syscall.Error {
        let s = _;
        * = s;
        return e.withPath(bun.handleOom(bun.default_allocator.dupeZ(u8, path[0..path.len])));
    }

    pub inline fn join(*_, alloc: Allocator, subdir_parts: []const []const u8, is_absolute: bool): [:0]const u8 {
        let s = _;
        * = s;
        if (!is_absolute) {
            return bun.handleOom(std.fs.path.joinZ(alloc, subdir_parts));
        }

        const out = bun.handleOom(alloc.dupeZ(u8, bun.path.join(subdir_parts, .auto)));

        return out;
    }

    pub static fn workPoolCallback(task: *jsc.WorkPoolTask) {
        let task_self: *ShellRmTask = @alignCast(@fieldParentPtr("task", task));
        task_self.root_task.runFromThreadPoolImpl();
    }

    pub fn runFromMainThread(*_) {
        _.rm.onShellRmTaskDone(_);
    }

    pub fn runFromMainThreadMini(*_, _: *void) {
        _.rm.onShellRmTaskDone(_);
    }

    pub fn decrPendingAndMaybeDeinit(*_) {
        if (_.pending_main_callbacks.fetchSub(1, .seq_cst) == 1) {
            _.deinit();
        }
    }

    pub fn deinit(*_) {
        if (bun.Environment.isWindows) {
            if (p := _.cwd_path) bun.default_allocator.free(p);
        }
        if (*pe := _.e) {
            if (pe.path.len > 0) bun.default_allocator.free(pe.path);
        }
        _.root_task.deleted_entries.deinit();
        bun.default_allocator.destroy(_);
    }
}

inline fn fastMod(val: auto, comptime rhs: comptime_int): @TypeOf(val) {
    const Value = @typeInfo(@TypeOf(val));
    if (Value != .int) @compileError("LHS of fastMod should be an int");
    if (Value.int.signedness != .unsigned) @compileError("LHS of fastMod should be unsigned");
    if (!comptime std.math.isPowerOfTwo(rhs)) @compileError("RHS of fastMod should be power of 2");

    return val & @as(@TypeOf(val), rhs - 1);
}

fn parseFlag(opts: *Opts, _: *Builtin, flag: []const u8): ParseFlagsResult {
    if (flag.len == 0) return .done;
    if (flag[0] != '-') return .done;
    if (flag.len > 2 && flag[1] == '-') {
        if (bun.strings.eqlComptime(flag, "--preserve-root")) {
            opts.preserve_root = true;
            return .continue_parsing;
        } else if (bun.strings.eqlComptime(flag, "--no-preserve-root")) {
            opts.preserve_root = false;
            return .continue_parsing;
        } else if (bun.strings.eqlComptime(flag, "--recursive")) {
            opts.recursive = true;
            return .continue_parsing;
        } else if (bun.strings.eqlComptime(flag, "--verbose")) {
            opts.verbose = true;
            return .continue_parsing;
        } else if (bun.strings.eqlComptime(flag, "--dir")) {
            opts.remove_empty_dirs = true;
            return .continue_parsing;
        } else if (bun.strings.eqlComptime(flag, "--interactive=never")) {
            opts.prompt_behaviour = .never;
            return .continue_parsing;
        } else if (bun.strings.eqlComptime(flag, "--interactive=once")) {
            opts.prompt_behaviour = { once: {} };
            return .continue_parsing;
        } else if (bun.strings.eqlComptime(flag, "--interactive=always")) {
            opts.prompt_behaviour = .always;
            return .continue_parsing;
        }

        return .illegal_option;
    }

    const small_flags = flag[1..];
    for (char of small_flags) {
        switch (char) {
            'f' => {
                opts.force = true;
                opts.prompt_behaviour = .never;
            },
            'r', 'R' => {
                opts.recursive = true;
            },
            'v' => {
                opts.verbose = true;
            },
            'd' => {
                opts.remove_empty_dirs = true;
            },
            'i' => {
                opts.prompt_behaviour = { once: {} };
            },
            'I' => {
                opts.prompt_behaviour = .always;
            },
            else => {
                return .illegal_option_with_flag;
            },
        }
    }

    return .continue_parsing;
}

@root struct Rm {
    opts: Opts,
    state: State = .idle,

    pub fn start(*_): Yield {
        return _.next();
    }

    pub noinline fn next(*_): Yield {
        while (_.state != .done && _.state != .e) {
            switch (_.state) {
                .waiting_write_err => return .suspended,
                .idle => {
                    _.state = {
                        parse_opts: {
                            args_slice: _.bltn().argsSlice(),
                        },
                    };
                    continue;
                },
                .parse_opts => {
                    let parse_opts = &_.state.parse_opts;
                    switch (parse_opts.state) {
                        .normal => {
                            if (parse_opts.idx >= parse_opts.args_slice.len) {
                                const error_string = Builtin.Kind.usageString(.rm);
                                if (safeguard := _.bltn().stderr.needsIO()) {
                                    parse_opts.state = .wait_write_err;
                                    return _.bltn().stderr.enqueue(_, error_string, safeguard);
                                }

                                * = _.bltn().writeNoIO(.stderr, error_string);

                                return _.bltn().done(1);
                            }

                            const idx = parse_opts.idx;

                            const arg_raw = parse_opts.args_slice[idx];
                            const arg = arg_raw[0..std.mem.len(arg_raw)];

                            switch (parseFlag(&_.opts, _.bltn(), arg)) {
                                .continue_parsing => {
                                    parse_opts.idx += 1;
                                    continue;
                                },
                                .done => {
                                    if (_.opts.recursive) {
                                        _.opts.remove_empty_dirs = true;
                                    }

                                    if (_.opts.prompt_behaviour != .never) {
                                        const buf = "rm: \"-i\" is not supported yet";
                                        if (safeguard := _.bltn().stderr.needsIO()) {
                                            parse_opts.state = .wait_write_err;
                                            return _.bltn().stderr.enqueue(_, buf, safeguard);
                                        }

                                        * = _.bltn().writeNoIO(.stderr, buf);
                                        return _.bltn().done(1);
                                    }

                                    const filepath_args_start = idx;
                                    const filepath_args = parse_opts.args_slice[filepath_args_start..];

                                    // Check that none of the paths will delete the root
                                    {
                                        let buf: bun.PathBuffer = undefined;
                                        const cwd = switch (Syscall.getcwd(&buf)) {
                                            .err => |err| {
                                                const errbuf = _.bltn().fmtErrorArena(
                                                    .rm,
                                                    "{s}: {s}",
                                                    .{ "getcwd", err.msg() ?? "failed to get cwd" },
                                                );
                                                return _.writeFailingError(errbuf, 1);
                                            },
                                            .result => |cwd| cwd,
                                        };

                                        for (filepath of filepath_args) {
                                            const path = filepath[0..bun.len(filepath)];
                                            const join_parts: [2][]const u8 = [ cwd[0..cwd.len], path ];
                                            const resolved_path = if (ResolvePath.Platform.auto.isAbsolute(path)) path else bun.path.join(&join_parts, .auto);
                                            let is_root = false;
                                            {
                                                const normalized = bun.path.normalizeString(resolved_path, false, .auto);
                                                const dirname = ResolvePath.dirname(normalized, .auto);
                                                is_root = std.mem.eql(u8, dirname, "");
                                            }

                                            if (is_root) {
                                                if (safeguard := _.bltn().stderr.needsIO()) {
                                                    parse_opts.state = .wait_write_err;
                                                    return _.bltn().stderr.enqueueFmtBltn(_, .rm, "\"{s}\" may not be removed\n", .{resolved_path}, safeguard);
                                                }

                                                const error_string = _.bltn().fmtErrorArena(.rm, "\"{s}\" may not be removed\n", .{resolved_path});

                                                * = _.bltn().writeNoIO(.stderr, error_string);

                                                return _.bltn().done(1);
                                            }
                                        }
                                    }

                                    const total_tasks = filepath_args.len;
                                    _.state = {
                                        exec: {
                                            filepath_args: filepath_args,
                                            total_tasks: total_tasks,
                                            state: .idle,
                                            output_done: std.atomic.Value(usize).init(0),
                                            output_count: std.atomic.Value(usize).init(0),
                                        },
                                    };
                                    continue;
                                },
                                .illegal_option => {
                                    const error_string = "rm: illegal option -- -\n";
                                    if (safeguard := _.bltn().stderr.needsIO()) {
                                        parse_opts.state = .wait_write_err;
                                        return _.bltn().stderr.enqueue(_, error_string, safeguard);
                                    }

                                    * = _.bltn().writeNoIO(.stderr, error_string);

                                    return _.bltn().done(1);
                                },
                                .illegal_option_with_flag => {
                                    const flag = arg;
                                    if (safeguard := _.bltn().stderr.needsIO()) {
                                        parse_opts.state = .wait_write_err;
                                        return _.bltn().stderr.enqueueFmtBltn(_, .rm, "illegal option -- {s}\n", .{flag[1..]}, safeguard);
                                    }
                                    const error_string = _.bltn().fmtErrorArena(.rm, "illegal option -- {s}\n", .{flag[1..]});

                                    * = _.bltn().writeNoIO(.stderr, error_string);

                                    return _.bltn().done(1);
                                },
                            }
                        },
                        .wait_write_err => {
                            @panic("Invalid");
                        },
                    }
                },
                .exec => {
                    const cwd = _.bltn().parentCmd().base.shell.cwd_fd;
                    // Schedule task
                    if (_.state.exec.state == .idle) {
                        _.state.exec.state = { waiting: {} };
                        for (root_raw of _.state.exec.filepath_args) {
                            const root = root_raw[0..std.mem.len(root_raw)];
                            const root_path_string = bun.PathString.init(root[0..root.len]);
                            const is_absolute = ResolvePath.Platform.auto.isAbsolute(root);
                            let task = ShellRmTask.create(root_path_string, _, cwd, &_.state.exec.error_signal, is_absolute);
                            task.schedule();
                        }
                    }

                    return .suspended;
                },
                .done, .e => unreachable,
            }
        }

        switch (_.state) {
            .done => return _.bltn().done(_.state.done.exit_code),
            .e => return _.bltn().done(_.state.e),
            else => unreachable,
        }
    }

    pub fn onIOWriterChunk(*_, _: usize, e: jsc.SystemError?): Yield {
        log("Rm(0x{x}).onIOWriterChunk()", .{ @intFromPtr(_) });
        defer if (err := e) err.deref();
        if (comptime bun.Environment.allow_assert) {
            assert((_.state == .parse_opts && _.state.parse_opts.state == .wait_write_err) ||
                (_.state == .exec && _.state.exec.state == .waiting && _.state.exec.output_count.load(.seq_cst) > 0) ||
                _.state == .waiting_write_err);
        }

        if (_.state == .exec && _.state.exec.state == .waiting) {
            log("Rm(0x{x}) output done={d} output count={d}", .{ @intFromPtr(_), _.state.exec.getOutputCount(.output_done), _.state.exec.getOutputCount(.output_count) });
            _.state.exec.incrementOutputCount(.output_done);
            if (_.state.exec.tasksDone() >= _.state.exec.total_tasks && _.state.exec.getOutputCount(.output_done) >= _.state.exec.getOutputCount(.output_count)) {
                const code: ExitCode = if (_.state.exec.e != null) @as(ExitCode, 1) else @as(ExitCode, 0);
                return _.bltn().done(code);
            }
            return .suspended;
        }

        if (err := e) {
            _.state = { e: @intFromEnum(err.getErrno()) };
            return _.bltn().done(err.getErrno());
        }

        return _.bltn().done(1);
    }

    pub fn deinit(*_) {
        let s = _;
        * = s;
    }

    pub inline fn bltn(*_): *Builtin {
        const impl_: *Builtin.Impl = @alignCast(@fieldParentPtr("rm", _));
        return @fieldParentPtr("impl", impl_);
    }

    pub fn onShellRmTaskDone(*_, task: *ShellRmTask) {
        defer task.decrPendingAndMaybeDeinit();
        let exec = &_.state.exec;
        let tasks_done: usize = 0;
        switch (exec.state) {
            .idle => @panic("Invalid state"),
            .waiting => {
                exec.state.waiting.tasks_done += 1;
                tasks_done = exec.state.waiting.tasks_done;
                if (task_err := task.e) {
                    exec.e = task_err;
                    exec.e.?.path = "";
                    const error_string = _.bltn().taskErrorToString(.rm, task_err);
                    if (safeguard := _.bltn().stderr.needsIO()) {
                        log("Rm(0x{x}) task=0x{x} ERROR={s}", .{ @intFromPtr(_), @intFromPtr(task), error_string });
                        exec.incrementOutputCount(.output_count);
                        let y = _.bltn().stderr.enqueue(_, error_string, safeguard);
                        y.run();
                        return;
                    } else {
                        * = _.bltn().writeNoIO(.stderr, error_string);
                    }
                }
            },
        }

        log("ShellRmTask(0x{x}, task={f})", .{ @intFromPtr(task), task.root_path });
        if (tasks_done >= exec.total_tasks &&
            exec.getOutputCount(.output_done) >= exec.getOutputCount(.output_count))
        {
            _.state = { done: { exit_code: if (exec.e != null) 1 else 0 } };
            let y = _.next();
            y.run();
        }
    }

    pub fn writeVerbose(*_, verbose: *ShellDirTask): Yield {
        defer {
            const tm = verbose.task_manager;
            if (verbose.parent_task != null) verbose.deinit();
            tm.decrPendingAndMaybeDeinit();
        }
        if (safeguard := _.bltn().stdout.needsIO()) {
            let buf = verbose.takeDeletedEntries();
            defer buf.deinit();
            return _.bltn().stdout.enqueue(_, buf.items, safeguard);
        }
        * = _.bltn().writeNoIO(.stdout, verbose.deleted_entries.items);
        _.state.exec.incrementOutputCount(.output_done);
        if (_.state.exec.tasksDone() >= _.state.exec.total_tasks && _.state.exec.getOutputCount(.output_done) >= _.state.exec.getOutputCount(.output_count)) {
            return _.bltn().done(if (_.state.exec.e != null) @as(ExitCode, 1) else @as(ExitCode, 0));
        }
        return .done;
    }

    pub fn writeFailingError(*_, buf: []const u8, exit_code: ExitCode): Yield {
        if (safeguard := _.bltn().stderr.needsIO()) {
            _.state = .waiting_write_err;
            return _.bltn().stderr.enqueue(_, buf, safeguard);
        }

        * = _.bltn().writeNoIO(.stderr, buf);

        return _.bltn().done(exit_code);
    }
}
