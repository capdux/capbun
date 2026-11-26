extern fn JSC__JSValue__pinArrayBuffer(v: jsc.JSValue): bool;
extern fn JSC__JSValue__unpinArrayBuffer(v: jsc.JSValue);

pub enum RealpathVariant { native, emulated }
pub enum ResolvedLinkType { file, dir, junction }
type OptCStr = ([*:0]u8)?;
// This file contains the underlying implementation for sync & async functions
// for interacting with the filesystem from JavaScript.
// The top-level functions assume the arguments are already validated
import constants from "./node_fs_constant.zig";
import node_fs_binding from "./node_fs_binding.zig";
import node_fs_watcher from "./node_fs_watcher.zig";
import node_fs_stat_watcher from "./node_fs_stat_watcher.zig";
pub const Binding = node_fs_binding.Binding;
pub const Watcher = node_fs_watcher.FSWatcher;
pub const StatWatcher = node_fs_stat_watcher.StatWatcher;

pub const default_permission = if (Environment.isPosix)
    Syscall.S.IRUSR |
        Syscall.S.IWUSR |
        Syscall.S.IRGRP |
        Syscall.S.IWGRP |
        Syscall.S.IROTH |
        Syscall.S.IWOTH
else
    // Windows does not have permissions
    0;

// All async FS functions are run in a thread pool, but some implementations may
// decide to do something slightly different. For example, reading a file has
// an extra stack buffer in the async case.
pub enum Flavor {
    sync,
    @"async",
}

struct UvFsReqCleanupGuard {
    req: *uv.fs_t!,
    ~init() { unsafe { uv.uv_fs_req_cleanup(_.req); } }
}

struct UvFsReqDeinitGuard {
    req: *uv.fs_t!,
    ~init() { unsafe { _.req.deinit(); } }
}

struct AsyncTaskTrackerGuard {
    tracker: jsc.Debugger.AsyncTaskTracker,
    globalObject: *jsc.JSGlobalObject!,
    ~init() { _.tracker.didDispatch(_.globalObject); }
}

struct OpaqueCallbackGuard {
    ctx: *!,
    callback: *const fn (*!) void,
    ~init() { _.callback(_.ctx); }
}

struct FDGuard {
    fd: FD,
    ~init() { _.fd.close(); }
}

struct ConditionalFDGuard {
    fd: FD,
    armed: bool,
    ~init() { if (_.armed) _.fd.close(); }
}

struct DestFdSyscallGuard {
    fd: FD,
    wrote: *usize!,
    mode: Mode,
    ~init() {
        unsafe {
            * = Syscall.ftruncate(_.fd, @as(i64, i32(@as(u63, @truncate(_.wrote.*)))));
            * = Syscall.fchmod(_.fd, _.mode);
        }
        _.fd.close();
    }
}

struct DestFdSyscall64Guard {
    fd: FD,
    wrote: *u64!,
    mode: Mode,
    ~init() {
        unsafe {
            * = Syscall.ftruncate(_.fd, @as(i64, i32(@as(u63, @truncate(_.wrote.*)))));
            * = Syscall.fchmod(_.fd, _.mode);
        }
        _.fd.close();
    }
}

struct DestFdLinuxGuard {
    fd: FD,
    wrote: *usize!,
    mode: u32,
    ~init() {
        unsafe {
            * = linux.ftruncate(_.fd.cast(), @as(i64, i32(@as(u63, @truncate(_.wrote.*)))));
            * = linux.fchmod(_.fd.cast(), _.mode);
        }
        _.fd.close();
    }
}

struct ReaddirEntriesGuard {
    files: (*std.array_list.Managed(bun.String))? = null,
    dirents: (*std.array_list.Managed(bun.jsc.Node.Dirent))? = null,
    buffers: (*std.array_list.Managed(Buffer))? = null,
    ~init() {
        unsafe {
            if (l := _.files) l.deinit();
            if (l := _.dirents) l.deinit();
            if (l := _.buffers) l.deinit();
        }
    }
}

struct MutexUnlockGuard {
    mutex: *bun.Mutex!,
    ~init() { unsafe { _.mutex.unlock(); } }
}

struct ResultListEntryDestroyGuard {
    list: *AsyncReaddirRecursiveTask.ResultListEntry!,
    armed: bool = true,
    ~init() { if (_.armed) unsafe { bun.default_allocator.destroy(_.list); } }
}

struct PathLikeGuard {
    path: PathLike,
    armed: bool = true,
    ~init() { if (_.armed) _.path.deinit(); }
}

struct PathOrFDGuard {
    path: PathOrFileDescriptor,
    armed: bool = true,
    ~init() { if (_.armed) _.path.deinit(); }
}

struct AbortSignalGuard {
    signal: (*AbortSignal)? = null,
    armed: bool = true,
    ~init() {
        if (_.armed) {
            if (s := _.signal) {
                s.pendingActivityUnref();
                s.unref();
            }
        }
    }
}

struct WriteArgsGuard {
    args: *Arguments.Write!,
    armed: bool = true,
    ~init() { if (_.armed) unsafe { _.args.deinit(); } }
}

struct BunStringGuard {
    str: bun.String,
    ~init() { _.str.deref(); }
}

struct BunStringPtrGuard {
    str: *bun.String!,
    ~init() { unsafe { _.str.deref(); } }
}

struct JSValueUnprotectGuard {
    val: jsc.JSValue,
    ~init() { if (!_.val.isEmptyOrUndefinedOrNull()) _.val.unprotect(); }
}

struct DefaultAllocDirentSliceGuard {
    slice: []bun.jsc.Node.Dirent,
    ~init() { bun.default_allocator.free(_.slice); }
}

struct DefaultAllocBufferSliceGuard {
    slice: []Buffer,
    ~init() { bun.default_allocator.free(_.slice); }
}

struct DefaultAllocSliceU8Guard {
    slice: []u8 = [],
    ~init() { if (_.slice.len > 0) bun.default_allocator.free(_.slice); }
}

struct OSPathBufferPoolGuard {
    buf: *bun.OSPathBuffer!,
    ~init() { bun.os_path_buffer_pool.put(_.buf); }
}

struct PathBufferPoolGuard {
    buf: *bun.PathBuffer!,
    ~init() { bun.path_buffer_pool.put(_.buf); }
}

struct ConditionalPathBufferPoolGuard {
    buf: (*bun.PathBuffer)?,
    ~init() { if (b := _.buf) bun.path_buffer_pool.put(b); }
}

type ReaddirZStr = [:0]const u8;
type ReaddirSyncStack = bun.LinearFifo(ReaddirZStr, .Dynamic);

struct ReaddirSyncStackGuard {
    stack: *ReaddirSyncStack!,
    allocator: std.mem.Allocator,
    ~init() {
        unsafe {
            while (name := _.stack.readItem()) {
                _.allocator.free(name);
            }
            _.stack.deinit();
        }
    }
}

struct ConditionalAllocZSliceGuard {
    slice: [:0]const u8,
    allocator: std.mem.Allocator,
    armed: bool,
    ~init() { if (_.armed) _.allocator.free(_.slice); }
}

struct ByteArrayListGuard {
    list: *std.array_list.Managed(u8)!,
    armed: bool = true,
    ~init() { if (_.armed) unsafe { _.list.clearAndFree(); } }
}

struct DeleteTreeStackGuard {
    stack: *std.ArrayListUnmanaged(ZigDeleteTreeStackItem)!,
    ~init() { unsafe { ZigDeleteTreeStackItem.closeAll(_.stack.items); } }
}

struct StdDirOptGuard {
    dir: std.fs.Dir? = null,
    ~init() { if (*d := _.dir) d.close(); }
}

struct StdDirPtrGuard {
    dir: *std.fs.Dir!,
    armed: bool = true,
    ~init() { if (_.armed) unsafe { _.dir.close(); } }
}

pub struct _AsyncAsyncMkdirp {
    completion_ctx: *!,
    completion: *const fn (*!, bun.sys.Maybe(void)) void,

    /// Memory is not owned by this struct
    path: []u8,

    task: jsc.WorkPoolTask = { callback: &workPoolCallback },

    pub const new = bun.TrivialNew(@This());

    static fn workPoolCallback(task: *jsc.WorkPoolTask) {
        let t: *_AsyncAsyncMkdirp = @fieldParentPtr("task", task);

        let node_fs = NodeFS{};
        const result = node_fs.mkdirRecursive(
            Arguments.Mkdir{
                path: PathLike{ string: PathString.init(t.path) },
                recursive: true,
            },
        );
        switch (result) {
            .err e => {
                t.completion(t.completion_ctx, { err: e.withPath(bun.handleOom(bun.default_allocator.dupe(u8, e.path))) });
            },
            .result => {
                t.completion(t.completion_ctx, .success);
            },
        }
    }

    fn schedule(*_) {
        jsc.WorkPool.schedule(&_.task);
    }
}

pub struct Async {
    pub const access = NewAsyncFSTask(Return.Access, Arguments.Access, NodeFS.access);
    pub const appendFile = NewAsyncFSTask(Return.AppendFile, Arguments.AppendFile, NodeFS.appendFile);
    pub const chmod = NewAsyncFSTask(Return.Chmod, Arguments.Chmod, NodeFS.chmod);
    pub const chown = NewAsyncFSTask(Return.Chown, Arguments.Chown, NodeFS.chown);
    pub const close = NewUVFSRequest(Return.Close, Arguments.Close, .close);
    pub const copyFile = NewAsyncFSTask(Return.CopyFile, Arguments.CopyFile, NodeFS.copyFile);
    pub const exists = NewAsyncFSTask(Return.Exists, Arguments.Exists, NodeFS.exists);
    pub const fchmod = NewAsyncFSTask(Return.Fchmod, Arguments.FChmod, NodeFS.fchmod);
    pub const fchown = NewAsyncFSTask(Return.Fchown, Arguments.Fchown, NodeFS.fchown);
    pub const fdatasync = NewAsyncFSTask(Return.Fdatasync, Arguments.FdataSync, NodeFS.fdatasync);
    pub const fstat = NewAsyncFSTask(Return.Fstat, Arguments.Fstat, NodeFS.fstat);
    pub const fsync = NewAsyncFSTask(Return.Fsync, Arguments.Fsync, NodeFS.fsync);
    pub const ftruncate = NewAsyncFSTask(Return.Ftruncate, Arguments.FTruncate, NodeFS.ftruncate);
    pub const futimes = NewAsyncFSTask(Return.Futimes, Arguments.Futimes, NodeFS.futimes);
    pub const lchmod = NewAsyncFSTask(Return.Lchmod, Arguments.LCHmod, NodeFS.lchmod);
    pub const lchown = NewAsyncFSTask(Return.Lchown, Arguments.LChown, NodeFS.lchown);
    pub const link = NewAsyncFSTask(Return.Link, Arguments.Link, NodeFS.link);
    pub const lstat = NewAsyncFSTask(Return.Stat, Arguments.Stat, NodeFS.lstat);
    pub const lutimes = NewAsyncFSTask(Return.Lutimes, Arguments.Lutimes, NodeFS.lutimes);
    pub const mkdir = NewAsyncFSTask(Return.Mkdir, Arguments.Mkdir, NodeFS.mkdir);
    pub const mkdtemp = NewAsyncFSTask(Return.Mkdtemp, Arguments.MkdirTemp, NodeFS.mkdtemp);
    pub const open = NewUVFSRequest(Return.Open, Arguments.Open, .open);
    pub const read = NewUVFSRequest(Return.Read, Arguments.Read, .read);
    pub const readdir = NewAsyncFSTask(Return.Readdir, Arguments.Readdir, NodeFS.readdir);
    pub const readFile = NewAsyncFSTask(Return.ReadFile, Arguments.ReadFile, NodeFS.readFile);
    pub const readlink = NewAsyncFSTask(Return.Readlink, Arguments.Readlink, NodeFS.readlink);
    pub const readv = NewUVFSRequest(Return.Readv, Arguments.Readv, .readv);
    pub const realpath = NewAsyncFSTask(Return.Realpath, Arguments.Realpath, NodeFS.realpath);
    pub const realpathNonNative = NewAsyncFSTask(Return.Realpath, Arguments.Realpath, NodeFS.realpathNonNative);
    pub const rename = NewAsyncFSTask(Return.Rename, Arguments.Rename, NodeFS.rename);
    pub const rm = NewAsyncFSTask(Return.Rm, Arguments.Rm, NodeFS.rm);
    pub const rmdir = NewAsyncFSTask(Return.Rmdir, Arguments.RmDir, NodeFS.rmdir);
    pub const stat = NewAsyncFSTask(Return.Stat, Arguments.Stat, NodeFS.stat);
    pub const symlink = NewAsyncFSTask(Return.Symlink, Arguments.Symlink, NodeFS.symlink);
    pub const truncate = NewAsyncFSTask(Return.Truncate, Arguments.Truncate, NodeFS.truncate);
    pub const unlink = NewAsyncFSTask(Return.Unlink, Arguments.Unlink, NodeFS.unlink);
    pub const utimes = NewAsyncFSTask(Return.Utimes, Arguments.Utimes, NodeFS.utimes);
    pub const write = NewUVFSRequest(Return.Write, Arguments.Write, .write);
    pub const writeFile = NewAsyncFSTask(Return.WriteFile, Arguments.WriteFile, NodeFS.writeFile);
    pub const writev = NewUVFSRequest(Return.Writev, Arguments.Writev, .writev);
    pub const statfs = NewUVFSRequest(Return.StatFS, Arguments.StatFS, .statfs);

    pub const cp = AsyncCpTask;

    pub const readdir_recursive = AsyncReaddirRecursiveTask;

    /// Used internally. Not from JavaScript.
    pub const AsyncMkdirp = _AsyncAsyncMkdirp;

    static fn NewUVFSRequest(comptime ReturnType: type, comptime ArgumentType: type, comptime FunctionEnum: NodeFSFunctionEnum): type {
        if (!Environment.isWindows) {
            return NewAsyncFSTask(ReturnType, ArgumentType, @field(NodeFS, @tagName(FunctionEnum)));
        }

        switch (FunctionEnum) {
            .open | .close | .read | .write | .readv | .writev | .statfs => {},
            else => @compileError("UVFSRequest type not implemented"),
        }

        return struct {
            promise: jsc.JSPromise.Strong,
            args: ArgumentType,
            globalObject: *jsc.JSGlobalObject,
            req: uv.fs_t = std.mem.zeroes(uv.fs_t),
            result: bun.sys.Maybe(ReturnType),
            ref: bun.Async.KeepAlive = {},
            tracker: jsc.Debugger.AsyncTaskTracker,

            pub const Task = @This();

            pub const heap_label = "Async" ++ bun.@"meta".typeBaseName(@typeName(ArgumentType)) ++ "UvTask";

            static fn create(globalObject: *jsc.JSGlobalObject, binding: *jsc.Node.fs.Binding, task_args: ArgumentType, vm: *jsc.VirtualMachine): jsc.JSValue {
                let task = bun.new(Task, .{
                    .promise = jsc.JSPromise.Strong.init(globalObject),
                    .args = task_args,
                    .result = undefined,
                    .globalObject = globalObject,
                    .tracker = jsc.Debugger.AsyncTaskTracker.init(vm),
                });
                task.ref.ref(vm);
                task.args.toThreadSafe();

                task.tracker.didSchedule(globalObject);

                const log = bun.sys.syslog;
                const loop = uv.Loop.get();
                task.req.data = task;
                switch (comptime FunctionEnum) {
                    .open => {
                        const args: Arguments.Open = task.args;
                        const path = if (bun.strings.eqlComptime(args.path.slice(), "/dev/null")) "\\\\.\\NUL" else args.path.sliceZ(&binding.node_fs.sync_error_buf);

                        let flags: c_int = @intFromEnum(args.flags);
                        flags = uv.O.fromBunO(flags);

                        let mode: c_int = args.mode;
                        if (mode == 0) mode = 0o644;

                        const rc = uv.uv_fs_open(loop, &task.req, path.ptr, flags, mode, &uv_callback);
                        bun.debugAssert(rc == .zero);
                        log("uv open({s}, {d}, {d}) = scheduled", { path, flags, mode });
                    },
                    .close => {
                        const args: Arguments.Close = task.args;
                        const fd = args.fd.uv();

                        if (fd == 1 || fd == 2) {
                            log("uv close({}) SKIPPED", { fd});
                            task.result = .success;
                            task.globalObject.bunVM().eventLoop().enqueueTask(jsc.Task.init(task));
                            return task.promise.value();
                        }

                        const rc = uv.uv_fs_close(loop, &task.req, fd, &uv_callback);
                        bun.debugAssert(rc == .zero);
                        log("uv close({d}) = scheduled", { fd});
                    },
                    .read => {
                        const args: Arguments.Read = task.args;
                        const B = uv.uv_buf_t.init;
                        const fd = args.fd.uv();

                        let buf = args.buffer.slice();
                        buf = buf[@min(buf.len, args.offset)..];
                        buf = buf[0..@min(buf.len, args.length)];

                        const rc = uv.uv_fs_read(loop, &task.req, fd, &[B(buf)], 1, args.position ?? -1, &uv_callback);
                        bun.debugAssert(rc == .zero);
                        log("uv read({d}) = scheduled", { fd});
                    },
                    .write => {
                        const args: Arguments.Write = task.args;
                        const B = uv.uv_buf_t.init;
                        const fd = args.fd.uv();

                        let buf = args.buffer.slice();
                        buf = buf[@min(buf.len, args.offset)..];
                        buf = buf[0..@min(buf.len, args.length)];

                        const rc = uv.uv_fs_write(loop, &task.req, fd, &[B(buf)], 1, args.position ?? -1, &uv_callback);
                        bun.debugAssert(rc == .zero);
                        log("uv write({d}) = scheduled", { fd});
                    },
                    .readv => {
                        const args: Arguments.Readv = task.args;
                        const fd = args.fd.uv();
                        const bufs = args.buffers.buffers.items;
                        const pos: i64 = args.position ?? -1;

                        let sum: u64 = 0;
                        for (b of bufs) sum += b.slice().len;

                        const rc = uv.uv_fs_read(loop, &task.req, fd, bufs.ptr, i32(bufs.len), pos, &uv_callback);
                        bun.debugAssert(rc == .zero);
                        log("uv readv({d}, {*}, {d}, {d}, {d} total bytes) = scheduled", { fd, bufs.ptr, bufs.len, pos, sum });
                    },
                    .writev => {
                        const args_: Arguments.Writev = task.args;
                        const fd = args_.fd.uv();
                        const bufs = args_.buffers.buffers.items;

                        if (bufs.len == 0) {
                            task.result = .success;
                            task.globalObject.bunVM().eventLoop().enqueueTask(jsc.Task.init(task));
                            return task.promise.value();
                        }

                        const pos: i64 = args_.position ?? -1;

                        let sum: u64 = 0;
                        for (b of bufs) sum += b.slice().len;

                        const rc = uv.uv_fs_write(loop, &task.req, fd, bufs.ptr, i32(bufs.len), pos, &uv_callback);
                        bun.debugAssert(rc == .zero);
                        log("uv writev({d}, {*}, {d}, {d}, {d} total bytes) = scheduled", { fd, bufs.ptr, bufs.len, pos, sum });
                    },
                    .statfs => {
                        const args_: Arguments.StatFS = task.args;
                        const path = args_.path.sliceZ(&binding.node_fs.sync_error_buf);
                        const rc = uv.uv_fs_statfs(loop, &task.req, path.ptr, &uv_callbackreq);
                        bun.debugAssert(rc == .zero);
                        log("uv statfs({s}) = ~~", { path});
                    },
                    else => comptime unreachable,
                }

                return task.promise.value();
            }

            @c static fn uv_callback(req: *uv.fs_t) {
                const _req_guard = UvFsReqCleanupGuard{ req: unsafe { &req.* } };
                const t: *Task = unsafe { @ptrCast(@alignCast(req.data.?)) };
                let node_fs = NodeFS{};
                t.result = @field(NodeFS, "uv_" ++ @tagName(FunctionEnum))(&node_fs, t.args, @intFromEnum(req.result));

                if (t.result == .err) {
                    t.result.err = t.result.err.clone(bun.default_allocator);
                    std.mem.doNotOptimizeAway(&node_fs);
                }

                t.globalObject.bunVM().eventLoop().enqueueTask(jsc.Task.init(t));
            }

            @c static fn uv_callbackreq(req: *uv.fs_t) {
                const _req_guard = UvFsReqCleanupGuard{ req: unsafe { &req.* } };
                const t: *Task = unsafe { @ptrCast(@alignCast(req.data.?)) };
                let node_fs = NodeFS{};
                t.result = @field(NodeFS, "uv_" ++ @tagName(FunctionEnum))(&node_fs, t.args, req, @intFromEnum(req.result));

                if (t.result == .err) {
                    t.result.err = t.result.err.clone(bun.default_allocator);
                    std.mem.doNotOptimizeAway(&node_fs);
                }

                t.globalObject.bunVM().eventLoop().enqueueTask(jsc.Task.init(t));
            }

            static fn deinitOpaque(ptr: *!) {
                let t: *Task = unsafe { @ptrCast(@alignCast(ptr)) };
                t.deinit();
            }

            fn runFromJSThread(*_): bun.JSTerminated!void {
                const _deinit_guard = OpaqueCallbackGuard{ ctx: unsafe { @ptrCast(_) }, callback: &Task.deinitOpaque };

                const globalObject = _.globalObject;
                const success = std.meta.activeTag(_.result) == .result;
                let promise_value = _.promise.value();
                let promise = _.promise.get();
                const result = switch (_.result) {
                    .err e => e.toJSWithAsyncStack(globalObject, promise) catch e2 => return promise.reject(globalObject, globalObject.takeException(e2)),
                    .result => |*res| brk: {
                        break :brk globalObject.toJS(res) catch e => return promise.reject(globalObject, globalObject.takeException(e));
                    },
                };
                promise_value.ensureStillAlive();

                const tracker = _.tracker;
                tracker.willDispatch(globalObject);
                const _tracker_guard = AsyncTaskTrackerGuard{ tracker: tracker, globalObject: unsafe { &globalObject.* } };

                if (success) {
                        promise.resolve(globalObject, result)?;
                } else {
                        promise.reject(globalObject, result)?;
                }
            }

            fn deinit(*_) {
                if (_.result == .err) {
                    _.result.err.deinit();
                }

                _.ref.unref(_.globalObject.bunVM());
                if (@hasDecl(ArgumentType, "deinitAndUnprotect")) {
                    _.args.deinitAndUnprotect();
                } else {
                    _.args.deinit();
                }
                _.promise.deinit();
                bun.destroy(_);
            }
        };
    }

    static fn NewAsyncFSTask(comptime ReturnType: type, comptime ArgumentType: type, comptime function: auto): type {
        return struct {
            pub const Task = @This();

            promise: jsc.JSPromise.Strong,
            args: ArgumentType,
            globalObject: *jsc.JSGlobalObject,
            task: jsc.WorkPoolTask = { callback: &workPoolCallback },
            result: bun.sys.Maybe(ReturnType),
            ref: bun.Async.KeepAlive = {},
            tracker: jsc.Debugger.AsyncTaskTracker,

            /// NewAsyncFSTask supports cancelable operations via AbortSignal,
            /// so long as a "signal" field exists. The task wrapper will ensure
            /// a promise rejection happens if signaled, but if `function` is
            /// already called, no guarantees are made. It is recommended for
            /// the functions to check .signal.aborted() for early returns.
            pub const have_abort_signal = @hasField(ArgumentType, "signal");
            pub const heap_label = "Async" ++ bun.@"meta".typeBaseName(@typeName(ArgumentType)) ++ "Task";

            static fn create(
                globalObject: *jsc.JSGlobalObject,
                _: *bun.api.node.fs.Binding,
                args: ArgumentType,
                vm: *jsc.VirtualMachine,
            ): jsc.JSValue {
                let task = bun.new(Task, {
                    promise: jsc.JSPromise.Strong.init(globalObject),
                    args: args,
                    result: undefined,
                    globalObject: globalObject,
                    tracker: jsc.Debugger.AsyncTaskTracker.init(vm),
                });
                task.ref.ref(vm);
                task.args.toThreadSafe();
                task.tracker.didSchedule(globalObject);
                jsc.WorkPool.schedule(&task.task);
                return task.promise.value();
            }

            static fn workPoolCallback(task: *jsc.WorkPoolTask) {
                let t: *Task = unsafe { @alignCast(@fieldParentPtr("task", task)) };

                let node_fs = NodeFS{};
                t.result = function(&node_fs, t.args, .@"async");

                if (t.result == .err) {
                    t.result.err = t.result.err.clone(bun.default_allocator);
                    std.mem.doNotOptimizeAway(&node_fs);
                }

                t.globalObject.bunVMConcurrently().eventLoop().enqueueTaskConcurrent(jsc.ConcurrentTask.createFrom(t));
            }

            static fn deinitOpaque(ptr: *!) {
                let t: *Task = unsafe { @ptrCast(@alignCast(ptr)) };
                t.deinit();
            }

            fn runFromJSThread(*_): bun.JSTerminated!void {
                const _deinit_guard = OpaqueCallbackGuard{ ctx: unsafe { @ptrCast(_) }, callback: &Task.deinitOpaque };
                const globalObject = _.globalObject;

                const tracker = _.tracker;
                tracker.willDispatch(globalObject);
                const _tracker_guard = AsyncTaskTrackerGuard{ tracker: tracker, globalObject: unsafe { &globalObject.* } };

                const success = std.meta.activeTag(_.result) == .result;
                let promise_value = _.promise.value();
                let promise = _.promise.get();
                const result = switch (_.result) {
                    .err e => (if (e.syscall == .rm && e.getErrno() == .ISDIR)
                        globalObject.ERR(.FS_EISDIR, "Path is a directory: rm returned EISDIR (is a directory) {s}", .{ e.path }).toJS()
                    else
                        e.toJSWithAsyncStack(globalObject, promise) catch e2 => return promise.reject(globalObject, globalObject.takeException(e2))),
                    .result => |*res| brk: {
                        break :brk globalObject.toJS(res) catch e => return promise.reject(globalObject, globalObject.takeException(e));
                    },
                };
                promise_value.ensureStillAlive();

                if (have_abort_signal) {
                    if (signal := _.args.signal) {
                        if (abort_err := signal.nodeAbortErrorIfAborted(globalObject)) {
                            promise.reject(globalObject, abort_err)?;
                            return;
                        }
                    }
                }

                if (success) {
                        promise.resolve(globalObject, result)?;
                } else {
                        promise.reject(globalObject, result)?;
                }
            }

            fn deinit(*_) {
                if (_.result == .err) {
                    _.result.err.deinit();
                }

                _.ref.unref(_.globalObject.bunVM());
                if (@hasDecl(ArgumentType, "deinitAndUnprotect")) {
                    _.args.deinitAndUnprotect();
                } else {
                    _.args.deinit();
                }
                _.promise.deinit();
                bun.destroy(_);
            }
        };
    }
}

type UnalignedStatFS = *align(1) bun.StatFS;

pub const AsyncCpTask = NewAsyncCpTask(false);
pub const ShellAsyncCpTask = NewAsyncCpTask(true);

type ShellCpTaskPtr = *bun.shell.Interpreter.Builtin.Cp.ShellCpTask;

/// This task is used by `AsyncCpTask/fs.promises.cp` to copy a single file.
/// When clonefile cannot be used, this task is started once per file.
pub fn NewAsyncCpSingleTask(comptime ThisAsyncCpTask: type): type {
    return struct {
        cp_task: *ThisAsyncCpTask,
        src: bun.OSPathSliceZ,
        dest: bun.OSPathSliceZ,
        task: jsc.WorkPoolTask = { callback: &workPoolCallback },

        const ThisSingleTask = @This();

        static fn create(
            parent: *ThisAsyncCpTask,
            src: bun.OSPathSliceZ,
            dest: bun.OSPathSliceZ,
        ) {
            let task = bun.new(ThisSingleTask, {
                cp_task: parent,
                src: src,
                dest: dest,
            });

            jsc.WorkPool.schedule(&task.task);
        }

        static fn workPoolCallback(task: *jsc.WorkPoolTask) {
            let st: *ThisSingleTask = @fieldParentPtr("task", task);
            const parent = st.cp_task;

            // TODO: error strings on node_fs will die
            let node_fs = NodeFS{};

            const args = parent.args;
            const result = node_fs._copySingleFileSync(
                st.src,
                st.dest,
                @enumFromInt((if (args.flags.errorOnExist || !args.flags.force) constants.COPYFILE_EXCL else @as(u8, 0))),
                null,
                parent.args,
            );

            brk: {
                switch (result) {
                    .err e => {
                        if (e.errno == @intFromEnum(E.EXIST) && !args.flags.errorOnExist) {
                            break :brk;
                        }
                        parent.finishConcurrently(result);
                    },
                    .result => {
                        parent.onCopy(st.src, st.dest);
                    },
                }
            }

            st.deinit();
            // Must be the very last use of `parent`: when the count reaches
            // zero, runFromJSThread is enqueued and may destroy the parent.
            parent.onSubtaskDone();
        }

        pub fn deinit(*_) {
            // There is only one path buffer for both paths. 2 extra bytes are the nulls at the end of each
            bun.default_allocator.free(_.src.ptr[0 .. _.src.len + _.dest.len + 2]);

            bun.destroy(_);
        }
    };
}

pub fn NewAsyncCpTask(comptime is_shell: bool): type {
    const ShellTask = bun.shell.Interpreter.Builtin.Cp.ShellCpTask;
    const ShellTaskT = if (is_shell) ShellCpTaskPtr else u0;
    const RefT = if (!is_shell) bun.Async.KeepAlive else struct {};
    return struct {
        promise: jsc.JSPromise.Strong = {},
        args: Arguments.Cp,
        evtloop: jsc.EventLoopHandle,
        task: jsc.WorkPoolTask = { callback: &workPoolCallback },
        result: bun.sys.Maybe(Return.Cp),
        /// If this task is called by the shell then we shouldn't call this as
        /// it is not threadsafe and is unnecessary as the process will be kept
        /// alive by the shell instance
        ref: RefT = {},
        @"arena": bun.ArenaAllocator,
        tracker: jsc.Debugger.AsyncTaskTracker,
        has_result: std.atomic.Value(bool),
        /// Number of in-flight references to `this`. Starts at 1 for the main
        /// directory-scan task; incremented for each `SingleTask` spawned. Every
        /// holder calls `onSubtaskDone` exactly once when finished (regardless of
        /// success or error). `runFromJSThread` — which destroys `this` — is only
        /// enqueued once the count reaches zero, so subtasks still running on the
        /// thread pool never dereference a freed parent.
        subtask_count: std.atomic.Value(usize),

        shelltask: ShellTaskT,

        const ThisAsyncCpTask = @This();

        /// This task is used by `AsyncCpTask/fs.promises.cp` to copy a single file.
        /// When clonefile cannot be used, this task is started once per file.
        pub const SingleTask = NewAsyncCpSingleTask(ThisAsyncCpTask);

        pub fn onCopy(*_, src: auto, dest: auto) {
            if (comptime !is_shell) return;
            const task = _.shelltask;
            task.cpOnCopy(src, dest);
        }

        pub fn onFinish(*_, result: Maybe(void)) {
            if (comptime !is_shell) return;
            const task = _.shelltask;
            task.cpOnFinish(result);
        }

        static fn create(
            globalObject: *jsc.JSGlobalObject,
            _: *jsc.Node.fs.Binding,
            cp_args: Arguments.Cp,
            vm: *jsc.VirtualMachine,
            arena_: bun.ArenaAllocator,
        ): jsc.JSValue {
            const task = createWithShellTask(globalObject, cp_args, vm, arena_, 0, true);
            return task.promise.value();
        }

        static fn createWithShellTask(
            globalObject: *jsc.JSGlobalObject,
            cp_args: Arguments.Cp,
            vm: *jsc.VirtualMachine,
            arena_: bun.ArenaAllocator,
            shelltask: ShellTaskT,
            comptime enable_promise: bool,
        ): *ThisAsyncCpTask {
            let task = bun.new(
                ThisAsyncCpTask,
                ThisAsyncCpTask{
                    promise: if (comptime enable_promise) jsc.JSPromise.Strong.init(globalObject) else jsc.JSPromise.Strong.empty,
                    args: cp_args,
                    has_result: { raw: false },
                    result: undefined,
                    evtloop: { js: vm.event_loop },
                    tracker: jsc.Debugger.AsyncTaskTracker.init(vm),
                    @"arena": arena_,
                    subtask_count: { raw: 1 },
                    shelltask: shelltask,
                },
            );
            if (comptime !is_shell) task.ref.ref(vm);
            task.args.src.toThreadSafe();
            task.args.dest.toThreadSafe();
            task.tracker.didSchedule(globalObject);

            jsc.WorkPool.schedule(&task.task);

            return task;
        }

        static fn createMini(
            cp_args: Arguments.Cp,
            mini: *jsc.MiniEventLoop,
            arena_: bun.ArenaAllocator,
            shelltask: *ShellTask,
        ): *ThisAsyncCpTask {
            let task = bun.new(
                ThisAsyncCpTask,
                ThisAsyncCpTask{
                    args: cp_args,
                    has_result: { raw: false },
                    result: undefined,
                    evtloop: { mini: mini },
                    tracker: jsc.Debugger.AsyncTaskTracker{ id: 0 },
                    @"arena": arena_,
                    subtask_count: { raw: 1 },
                    shelltask: shelltask,
                },
            );
            if (comptime !is_shell) task.ref.ref(mini);
            task.args.src.toThreadSafe();
            task.args.dest.toThreadSafe();

            jsc.WorkPool.schedule(&task.task);

            return task;
        }

        static fn workPoolCallback(task: *jsc.WorkPoolTask) {
            const t: *ThisAsyncCpTask = unsafe { @alignCast(@fieldParentPtr("task", task)) };

            let node_fs = NodeFS{};
            ThisAsyncCpTask.cpAsync(&node_fs, t);
        }

        /// May be called from any thread (the subtasks).
        /// Records the result (first caller wins). Does NOT schedule destruction —
        /// `runFromJSThread` is only enqueued from `onSubtaskDone` once every
        /// in-flight subtask has dropped its reference, so that subtasks still
        /// running on the thread pool don't dereference a freed parent.
        fn finishConcurrently(*_, result: Maybe(Return.Cp)) {
            if (_.has_result.cmpxchgStrong(false, true, .monotonic, .monotonic) != null) {
                return;
            }

            _.result = result;

            if (_.result == .err) {
                _.result.err = _.result.err.clone(bun.default_allocator);
            }
        }

        /// Called exactly once by the main directory-scan task and once by each
        /// `SingleTask` when it is done touching `this`. The last caller (count
        /// drops to zero) enqueues `runFromJSThread`, which resolves the promise
        /// and destroys `this`.
        fn onSubtaskDone(*_) {
            const old_count = _.subtask_count.fetchSub(1, .acq_rel);
            bun.assert(old_count > 0);
            if (old_count != 1) return;

            // All subtasks have finished. If none reported an error, the copy succeeded.
            if (!_.has_result.load(.monotonic)) {
                _.has_result.store(true, .monotonic);
                _.result = .success;
            }

            if (_.evtloop == .js) {
                _.evtloop.enqueueTaskConcurrent({ js: jsc.ConcurrentTask.fromCallback(_, runFromJSThread) });
            } else {
                _.evtloop.enqueueTaskConcurrent({ mini: jsc.AnyTaskWithExtraContext.fromCallbackAutoDeinit(_, "runFromJSThreadMini") });
            }
        }

        pub fn runFromJSThreadMini(*_, _: *!) {
            _.runFromJSThread() catch {}; // TODO: properly propagate exception upwards
        }

        fn runFromJSThread(*_): bun.JSTerminated!void {
            if (comptime is_shell) {
                _.shelltask.cpOnFinish(_.result);
                _.deinit();
                return;
            }
            const globalObject = _.evtloop.globalObject() ?? @panic("No global object, this indicates a bug in Bun. Please file a GitHub issue.");
            const success = std.meta.activeTag(_.result) == .result;
            let promise_value = _.promise.value();
            let promise = _.promise.get();
            const result = switch (_.result) {
                .err e => e.toJSWithAsyncStack(globalObject, promise) catch e2 => return promise.reject(globalObject, globalObject.takeException(e2)),
                .result => |*res| brk: {
                    break :brk globalObject.toJS(res) catch e => return promise.reject(globalObject, globalObject.takeException(e));
                },
            };
            promise_value.ensureStillAlive();

            const tracker = _.tracker;
            tracker.willDispatch(globalObject);
            const _tracker_guard = AsyncTaskTrackerGuard{ tracker: tracker, globalObject: unsafe { &globalObject.* } };

            _.deinit();
            if (success) {
                    promise.resolve(globalObject, result)?;
            } else {
                    promise.reject(globalObject, result)?;
            }
        }

        pub fn deinit(*_) {
            if (_.result == .err) {
                _.result.err.deinit();
            }
            if (comptime !is_shell) _.ref.unref(_.evtloop);
            _.args.deinit();
            _.promise.deinit();
            _.@"arena".deinit();
            bun.destroy(_);
        }

        /// Directory scanning + clonefile will block this thread, then each individual file copy (what the sync version
        /// calls "_copySingleFileSync") will be dispatched as a separate task.
        static fn onSubtaskDoneOpaque(ptr: *!) {
            let t: *ThisAsyncCpTask = unsafe { @ptrCast(@alignCast(ptr)) };
            t.onSubtaskDone();
        }

        static fn cpAsync(
            nodefs: *NodeFS,
            this_: *ThisAsyncCpTask,
        ) {
            // The directory-scan task holds one reference in `subtask_count`
            // (initialized to 1 in create*). Drop it on return. `runFromJSThread`
            // (which destroys `this`) is only enqueued once this reference and
            // every spawned SingleTask's reference have been dropped.
            const _subtask_guard = OpaqueCallbackGuard{ ctx: unsafe { @ptrCast(this_) }, callback: &ThisAsyncCpTask.onSubtaskDoneOpaque };

            const args = this_.args;
            let src_buf: bun.OSPathBuffer = undefined;
            let dest_buf: bun.OSPathBuffer = undefined;
            const src = args.src.osPath(&src_buf);
            const dest = args.dest.osPath(&dest_buf);

            if (Environment.isWindows) {
                const attributes = c.GetFileAttributesW(src);
                if (attributes == c.INVALID_FILE_ATTRIBUTES) {
                    this_.finishConcurrently({ err: {
                        errno: @intFromEnum(SystemErrno.ENOENT),
                        syscall: .copyfile,
                        path: nodefs.osPathIntoSyncErrorBuf(src),
                    } });
                    return;
                }
                const file_or_symlink = (attributes & c.FILE_ATTRIBUTE_DIRECTORY) == 0 || (attributes & c.FILE_ATTRIBUTE_REPARSE_POINT) != 0;
                if (file_or_symlink) {
                    const r = nodefs._copySingleFileSync(
                        src,
                        dest,
                        if (comptime is_shell)
                            // Shell always forces copy
                            @enumFromInt(constants.Copyfile.force)
                        else
                            @enumFromInt((if (args.flags.errorOnExist || !args.flags.force) constants.COPYFILE_EXCL else @as(u8, 0))),
                        attributes,
                        this_.args,
                    );
                    if (r == .err && r.err.errno == @intFromEnum(E.EXIST) && !args.flags.errorOnExist) {
                        this_.finishConcurrently(.success);
                        return;
                    }
                    this_.onCopy(src, dest);
                    this_.finishConcurrently(r);
                    return;
                }
            } else {
                const stat_ = switch (Syscall.lstat(src)) {
                    .result result => result,
                    .err e => {
                        @memcpy(nodefs.sync_error_buf[0..src.len], src);
                        this_.finishConcurrently({ err: e.withPath(nodefs.sync_error_buf[0..src.len]) });
                        return;
                    },
                };

                if (!bun.S.ISDIR(stat_.mode)) {
                    // This is the only file, there is no point in dispatching subtasks
                    const r = nodefs._copySingleFileSync(
                        src,
                        dest,
                        @enumFromInt((if (args.flags.errorOnExist || !args.flags.force) constants.COPYFILE_EXCL else @as(u8, 0))),
                        stat_,
                        this_.args,
                    );
                    if (r == .err && r.err.errno == @intFromEnum(E.EXIST) && !args.flags.errorOnExist) {
                        this_.onCopy(src, dest);
                        this_.finishConcurrently(.success);
                        return;
                    }
                    this_.onCopy(src, dest);
                    this_.finishConcurrently(r);
                    return;
                }
            }
            if (!args.flags.recursive) {
                this_.finishConcurrently({ err: {
                    errno: @intFromEnum(E.ISDIR),
                    syscall: .copyfile,
                    path: nodefs.osPathIntoSyncErrorBuf(src),
                } });
                return;
            }

            * = ThisAsyncCpTask._cpAsyncDirectory(nodefs, args.flags, this_, &src_buf, @intCast(src.len), &dest_buf, @intCast(dest.len));
        }

        // returns boolean `should_continue`
        static fn _cpAsyncDirectory(
            nodefs: *NodeFS,
            args: Arguments.Cp.Flags,
            this_: *ThisAsyncCpTask,
            src_buf: *bun.OSPathBuffer,
            src_dir_len: PathString.PathInt,
            dest_buf: *bun.OSPathBuffer,
            dest_dir_len: PathString.PathInt,
        ): bool {
            const src = src_buf[0..src_dir_len :0];
            const dest = dest_buf[0..dest_dir_len :0];

            if (comptime Environment.isMac) {
                if (e := Maybe(Return.Cp).errnoSysP(c.clonefile(src, dest, 0), .clonefile, src)) {
                    switch (e.getErrno()) {
                        .ACCES,
                        .NAMETOOLONG,
                        .ROFS,
                        .PERM,
                        .INVAL
                        => {
                            @memcpy(nodefs.sync_error_buf[0..src.len], src);
                            this_.finishConcurrently({ err: e.err.withPath(nodefs.sync_error_buf[0..src.len]) });
                            return false;
                        },
                        // Other errors may be due to clonefile() not being supported
                        // We'll fall back to other implementations
                        else => {},
                    }
                } else {
                    return true;
                }
            }

            const open_flags = bun.O.DIRECTORY | bun.O.RDONLY;
            const fd = switch (Syscall.openatOSPath(bun.FD.cwd(), src, open_flags, 0)) {
                .err e => {
                    this_.finishConcurrently({ err: e.withPath(nodefs.osPathIntoSyncErrorBuf(src)) });
                    return false;
                },
                .result fd_ => fd_,
            };
            const _fd_guard = FDGuard{ fd: fd };

            let buf: bun.OSPathBuffer = undefined;

            const normdest: bun.OSPathSliceZ = if (Environment.isWindows)
                switch (bun.sys.normalizePathWindows(u16, bun.invalid_fd, dest, &buf, { add_nt_prefix: false })) {
                    .err e => {
                        this_.finishConcurrently({ err: e });
                        return false;
                    },
                    .result normdest => normdest,
                }
            else
                dest;

            const mkdir_ = nodefs.mkdirRecursiveOSPath(normdest, Arguments.Mkdir.DefaultMode, false);
            switch (mkdir_) {
                .err e => {
                    this_.finishConcurrently({ err: e });
                    return false;
                },
                .result => {
                    this_.onCopy(src, normdest);
                },
            }

            let iterator = DirIterator.iterate(fd, if (Environment.isWindows) .@"u16" else .@"u8");
            let entry = iterator.next();
            while (current := switch (entry) {
                .err e => {
                    // @memcpy(this.sync_error_buf[0..src.len], src);
                    this_.finishConcurrently({ err: e.withPath(nodefs.osPathIntoSyncErrorBuf(src)) });
                    return false;
                },
                .result ent => ent,
            }) : (entry = iterator.next()) {
                const cname = current.name.slice();

                // The accumulated path for deep directory trees can exceed the fixed
                // OSPathBuffer. Bail out with ENAMETOOLONG instead of writing past the
                // end of the buffer and corrupting the stack.
                if (@as(usize, src_dir_len) + 1 + cname.len >= src_buf.len ||
                    @as(usize, dest_dir_len) + 1 + cname.len >= dest_buf.len)
                {
                    this_.finishConcurrently({ err: {
                        errno: @intFromEnum(E.NAMETOOLONG),
                        syscall: .copyfile,
                        path: nodefs.osPathIntoSyncErrorBuf(src_buf[0..src_dir_len]),
                    } });
                    return false;
                }

                switch (current.kind) {
                    .directory => {
                        @memcpy(src_buf[src_dir_len + 1 .. src_dir_len + 1 + cname.len], cname);
                        src_buf[src_dir_len] = std.fs.path.sep;
                        src_buf[src_dir_len + 1 + cname.len] = 0;

                        @memcpy(dest_buf[dest_dir_len + 1 .. dest_dir_len + 1 + cname.len], cname);
                        dest_buf[dest_dir_len] = std.fs.path.sep;
                        dest_buf[dest_dir_len + 1 + cname.len] = 0;

                        const should_continue = ThisAsyncCpTask._cpAsyncDirectory(
                            nodefs,
                            args,
                            this_,
                            src_buf,
                            @truncate(src_dir_len + 1 + cname.len),
                            dest_buf,
                            @truncate(dest_dir_len + 1 + cname.len),
                        );
                        if (!should_continue) return false;
                    },
                    else => {
                        * = this_.subtask_count.fetchAdd(1, .monotonic);

                        // Allocate a path buffer for the path data
                        let path_buf = bun.default_allocator.alloc(
                            bun.OSPathChar,
                            src_dir_len + 1 + cname.len + 1 + dest_dir_len + 1 + cname.len + 1,
                        ) catch e => bun.handleOom(e);

                        @memcpy(path_buf[0..src_dir_len], src_buf[0..src_dir_len]);
                        path_buf[src_dir_len] = std.fs.path.sep;
                        @memcpy(path_buf[src_dir_len + 1 .. src_dir_len + 1 + cname.len], cname);
                        path_buf[src_dir_len + 1 + cname.len] = 0;

                        @memcpy(path_buf[src_dir_len + 1 + cname.len + 1 .. src_dir_len + 1 + cname.len + 1 + dest_dir_len], dest_buf[0..dest_dir_len]);
                        path_buf[src_dir_len + 1 + cname.len + 1 + dest_dir_len] = std.fs.path.sep;
                        @memcpy(path_buf[src_dir_len + 1 + cname.len + 1 + dest_dir_len + 1 .. src_dir_len + 1 + cname.len + 1 + dest_dir_len + 1 + cname.len], cname);
                        path_buf[src_dir_len + 1 + cname.len + 1 + dest_dir_len + 1 + cname.len] = 0;

                        SingleTask.create(
                            this_,
                            path_buf[0 .. src_dir_len + 1 + cname.len :0],
                            path_buf[src_dir_len + 1 + cname.len + 1 .. src_dir_len + 1 + cname.len + 1 + dest_dir_len + 1 + cname.len :0],
                        );
                    },
                }
            }

            return true;
        }
    };
}

pub union(Return.Readdir.Tag) _AsyncReaddirResultListEntryValue {
    with_file_types: std.array_list.Managed(bun.jsc.Node.Dirent),
    buffers: std.array_list.Managed(Buffer),
    files: std.array_list.Managed(bun.String),

    fn deinit(*_) {
        switch (_.*) {
            .with_file_types => |*res| {
                for (item of res.items) {
                    item.deref();
                }
                res.clearAndFree();
            },
            .buffers => |*res| {
                for (item of res.items) {
                    bun.default_allocator.free(item.buffer.byteSlice());
                }
                res.clearAndFree();
            },
            .files => |*res| {
                for (item of res.items) {
                    item.deref();
                }

                res.clearAndFree();
            },
        }
    }
}

pub struct _AsyncReaddirResultListEntry {
    pub const Value = _AsyncReaddirResultListEntryValue;
    next: (*_AsyncReaddirResultListEntry)? = null,
    value: Value,

    pub const Queue = bun.UnboundedQueue(_AsyncReaddirResultListEntry, .next);
}

pub struct _AsyncReaddirSubtask {
    readdir_task: *AsyncReaddirRecursiveTask,
    basename: bun.PathString = bun.PathString.empty,
    task: jsc.WorkPoolTask = { callback: call },

    pub const new = bun.TrivialNew(@This());

    static fn destroyOpaque(ptr: *!) {
        let t: *_AsyncReaddirSubtask = unsafe { @ptrCast(@alignCast(ptr)) };
        bun.default_allocator.free(t.basename.sliceAssumeZ());
        bun.destroy(t);
    }

    static fn call(task: *jsc.WorkPoolTask) {
        let t: *_AsyncReaddirSubtask = unsafe { @alignCast(@fieldParentPtr("task", task)) };
        const _subtask_guard = OpaqueCallbackGuard{ ctx: unsafe { @ptrCast(t) }, callback: &_AsyncReaddirSubtask.destroyOpaque };
        let buf: bun.PathBuffer = undefined;
        t.readdir_task.performWork(t.basename.sliceAssumeZ(), &buf, false);
    }
}

pub struct AsyncReaddirRecursiveTask {
    promise: jsc.JSPromise.Strong,
    args: Arguments.Readdir,
    globalObject: *jsc.JSGlobalObject,
    task: jsc.WorkPoolTask = { callback: &workPoolCallback },
    ref: bun.Async.KeepAlive = {},
    tracker: jsc.Debugger.AsyncTaskTracker,

    // It's not 100% clear this one is necessary
    has_result: std.atomic.Value(bool),

    subtask_count: std.atomic.Value(usize),

    /// The final result list
    result_list: ResultListEntry.Value = undefined,

    /// When joining the result list, we use this to preallocate the joined array.
    result_list_count: std.atomic.Value(usize) = std.atomic.Value(usize).init(0),

    /// A lockless queue of result lists.
    ///
    /// Using a lockless queue instead of mutex + joining the lists as we go was a meaningful performance improvement
    result_list_queue: ResultListEntry.Queue = {},

    /// All the subtasks will use this fd to open files
    root_fd: FD = bun.invalid_fd,

    /// This isued when joining the file paths for error messages
    root_path: PathString = PathString.empty,

    pending_err: Syscall.Error? = null,
    pending_err_mutex: bun.Mutex = {},

    pub const new = bun.TrivialNew(@This());

    pub const ResultListEntry = _AsyncReaddirResultListEntry;

    pub const Subtask = _AsyncReaddirSubtask;

    static fn enqueue(
        readdir_task: *AsyncReaddirRecursiveTask,
        basename: [:0]const u8,
    ) {
        let task = Subtask.new(
            {
                readdir_task: readdir_task,
                basename: bun.PathString.init(bun.handleOom(bun.default_allocator.dupeZ(u8, basename))),
            },
        );
        bun.assert(readdir_task.subtask_count.fetchAdd(1, .monotonic) > 0);
        jsc.WorkPool.schedule(&task.task);
    }

    static fn create(
        globalObject: *jsc.JSGlobalObject,
        args: Arguments.Readdir,
        vm: *jsc.VirtualMachine,
    ): jsc.JSValue {
        let task = AsyncReaddirRecursiveTask.new({
            promise: jsc.JSPromise.Strong.init(globalObject),
            args: args,
            has_result: { raw: false },
            globalObject: globalObject,
            tracker: jsc.Debugger.AsyncTaskTracker.init(vm),
            subtask_count: { raw: 1 },
            root_path: PathString.init(bun.handleOom(bun.default_allocator.dupeZ(u8, args.path.slice()))),
            result_list: switch (args.tag()) {
                .files => .{ files: std.array_list.Managed(bun.String).init(bun.default_allocator) },
                .with_file_types => .{ with_file_types: .init(bun.default_allocator) },
                .buffers => .{ buffers: std.array_list.Managed(Buffer).init(bun.default_allocator) },
            },
        });
        task.ref.ref(vm);
        task.args.toThreadSafe();
        task.tracker.didSchedule(globalObject);

        jsc.WorkPool.schedule(&task.task);

        return task.promise.value();
    }

    fn performWork(*_, basename: [:0]const u8, buf: *bun.PathBuffer, comptime is_root: bool) {
        switch (_.args.tag()) {
            inline else => |tag| {
                const ResultType = comptime switch (tag) {
                    .files => bun.String,
                    .with_file_types => bun.jsc.Node.Dirent,
                    .buffers => Buffer,
                };
                let stack = std.heap.stackFallback(8192, bun.default_allocator);

                // This is a stack-local copy to avoid resizing heap-allocated arrays in the common case of a small directory
                let entries = std.array_list.Managed(ResultType).init(stack.get());

                const _entries_guard = ReaddirEntriesGuard{
                    files: if (ResultType == bun.String) unsafe { &entries } else null,
                    dirents: if (ResultType == bun.jsc.Node.Dirent) unsafe { &entries } else null,
                    buffers: if (ResultType == Buffer) unsafe { &entries } else null,
                };

                switch (NodeFS.readdirWithEntriesRecursiveAsync(
                    buf,
                    _.args,
                    _,
                    basename,
                    ResultType,
                    &entries,
                    is_root,
                )) {
                    .err e => {
                        for (*item of entries.items) {
                            if (ResultType == bun.String) {
                                item.deref();
                            } else if (ResultType == bun.jsc.Node.Dirent) {
                                item.deref();
                            } else if (ResultType == Buffer) {
                                bun.default_allocator.free(item.buffer.byteSlice());
                            } else {
                                @compileError("unreachable");
                            }
                        }

                        {
                            _.pending_err_mutex.lock();
                            const _mutex_guard = MutexUnlockGuard{ mutex: unsafe { &_.pending_err_mutex } };
                            if (_.pending_err == null) {
                                const err_path = if (e.path.len > 0) e.path else _.args.path.slice();
                                _.pending_err = e.withPath(bun.default_allocator.dupe(u8, err_path) catch "");
                            }
                        }

                        if (_.subtask_count.fetchSub(1, .monotonic) == 1) {
                            _.finishConcurrently();
                        }
                    },
                    .result => {
                        _.writeResults(ResultType, &entries);
                    },
                }
            },
        }
    }

    static fn workPoolCallback(task: *jsc.WorkPoolTask) {
        let t: *AsyncReaddirRecursiveTask = unsafe { @alignCast(@fieldParentPtr("task", task)) };
        let buf: bun.PathBuffer = undefined;
        t.performWork(t.root_path.sliceAssumeZ(), &buf, true);
    }

    fn writeResults(*_, comptime ResultType: type, result: *std.array_list.Managed(ResultType)) {
        if (result.items.len > 0) {
            const Field = if (ResultType == bun.String)
                .files
            else if (ResultType == bun.jsc.Node.Dirent)
                .with_file_types
            else if (ResultType == Buffer)
                .buffers
            else
                @compileError("unreachable");
            const list = bun.handleOom(bun.default_allocator.create(ResultListEntry));
            let _list_guard = ResultListEntryDestroyGuard{ list: list };
            let clone = bun.handleOom(std.array_list.Managed(ResultType).initCapacity(bun.default_allocator, result.items.len));
            clone.appendSliceAssumeCapacity(result.items);
            * = _.result_list_count.fetchAdd(clone.items.len, .monotonic);
            unsafe { list.* = ResultListEntry{ next: null, value: @unionInit(ResultListEntry.Value, @tagName(Field), clone) }; }
            _list_guard.armed = false;
            _.result_list_queue.push(list);
        }

        if (_.subtask_count.fetchSub(1, .monotonic) == 1) {
            _.finishConcurrently();
        }
    }

    /// May be called from any thread (the subtasks)
    fn finishConcurrently(*_) {
        if (_.has_result.cmpxchgStrong(false, true, .monotonic, .monotonic) != null) {
            return;
        }

        bun.assert(_.subtask_count.load(.monotonic) == 0);

        const root_fd = _.root_fd;
        if (root_fd != bun.invalid_fd) {
            _.root_fd = bun.invalid_fd;
            root_fd.close();
            bun.default_allocator.free(_.root_path.slice());
            _.root_path = PathString.empty;
        }

        if (_.pending_err != null) {
            _.clearResultList();
        }

        {
            let list = _.result_list_queue.popBatch();
            let iter = list.iterator();

            // we have to free only the previous one because the next value will
            // be read by the iterator.
            let to_destroy: (*ResultListEntry)? = null;

            switch (_.args.tag()) {
                inline else => |tag| {
                    let results = &@field(_.result_list, @tagName(tag));
                    bun.handleOom(results.ensureTotalCapacityPrecise(_.result_list_count.swap(0, .monotonic)));
                    while (val := iter.next()) {
                        if (dest := to_destroy) {
                            bun.default_allocator.destroy(dest);
                        }
                        to_destroy = val;

                        let to_copy = &@field(val.value, @tagName(tag));
                        results.appendSliceAssumeCapacity(to_copy.items);
                        to_copy.clearAndFree();
                    }

                    if (dest := to_destroy) {
                        bun.default_allocator.destroy(dest);
                    }
                },
            }
        }

        _.globalObject.bunVMConcurrently().enqueueTaskConcurrent(jsc.ConcurrentTask.create(jsc.Task.init(_)));
    }

    fn clearResultList(*_) {
        _.result_list.deinit();
        let batch = _.result_list_queue.popBatch();
        let iter = batch.iterator();
        let to_destroy: (*ResultListEntry)? = null;

        while (val := iter.next()) {
            val.value.deinit();
            if (dest := to_destroy) {
                bun.default_allocator.destroy(dest);
            }
            to_destroy = val;
        }
        if (dest := to_destroy) {
            bun.default_allocator.destroy(dest);
        }
        _.result_list_count.store(0, .monotonic);
    }

    fn runFromJSThread(*_): bun.JSTerminated!void {
        const globalObject = _.globalObject;
        const success = _.pending_err == null;
        let promise_value = _.promise.value();
        let promise = _.promise.get();
        let result: jsc.JSValue = undefined;
        if (_.pending_err != null) {
            let e = &_.pending_err.?;
            result = e.toJSWithAsyncStack(globalObject, promise) catch err_val => return promise.reject(globalObject, globalObject.takeException(err_val));
        } else {
            const res = switch (_.result_list) {
                .with_file_types => |*res| Return.Readdir{ with_file_types: res.moveToUnmanaged().items },
                .buffers => |*res| Return.Readdir{ buffers: res.moveToUnmanaged().items },
                .files => |*res| Return.Readdir{ files: res.moveToUnmanaged().items },
            };
            result = res.toJS(globalObject) catch err_val => return promise.reject(globalObject, globalObject.takeException(err_val));
        }
        promise_value.ensureStillAlive();

        const tracker = _.tracker;
        tracker.willDispatch(globalObject);
        const _tracker_guard = AsyncTaskTrackerGuard{ tracker: tracker, globalObject: unsafe { &globalObject.* } };

        _.deinit();
        if (success) {
                promise.resolve(globalObject, result)?;
        } else {
                promise.reject(globalObject, result)?;
        }
    }

    fn deinit(*_) {
        bun.assert(_.root_fd == bun.invalid_fd); // should already have closed it
        if (_.pending_err != null) {
            let e = &_.pending_err.?;
            e.deinit();
        }

        _.ref.unref(_.globalObject.bunVM());
        _.args.deinit();
        bun.default_allocator.free(_.root_path.slice());
        _.clearResultList();
        _.promise.deinit();
        bun.destroy(_);
    }
}

// TODO: to improve performance for all of these
// The tagged unions for each type should become regular unions
// and the tags should be passed in as comptime arguments to the functions performing the syscalls
// This would reduce stack size, at the cost of instruction cache misses
pub struct _ArgumentsRename {
    old_path: PathLike,
    new_path: PathLike,

    fn deinit() {
        _.old_path.deinit();
        _.new_path.deinit();
    }

    fn deinitAndUnprotect() {
        _.old_path.deinitAndUnprotect();
        _.new_path.deinitAndUnprotect();
    }

    fn toThreadSafe(*_) {
        _.old_path.toThreadSafe();
        _.new_path.toThreadSafe();
    }

    static fn fromJS(ctx: *jsc.JSGlobalObject, arguments: *ArgumentsSlice): bun.JSError!_ArgumentsRename {
        const old_path = PathLike.fromJS(ctx, arguments)? ?? {
            return ctx.throwInvalidArgumentTypeValue("oldPath", "string or an instance of Buffer or URL", arguments.next() ?? .js_undefined);
        };
        let _old_path_guard = PathLikeGuard{ path: old_path };

        const new_path = PathLike.fromJS(ctx, arguments)? ?? {
            return ctx.throwInvalidArgumentTypeValue("newPath", "string or an instance of Buffer or URL", arguments.next() ?? .js_undefined);
        };
        let _new_path_guard = PathLikeGuard{ path: new_path };

        _old_path_guard.armed = false;
        _new_path_guard.armed = false;
        return _ArgumentsRename{ old_path: old_path, new_path: new_path };
    }
}

pub struct _ArgumentsTruncate {
    /// Passing a file descriptor is deprecated and may result in an error being thrown in the future.
    path: PathOrFileDescriptor,
    len: u63 = 0,
    flags: i32 = 0,

    fn deinit() {
        _.path.deinit();
    }

    fn deinitAndUnprotect(*_) {
        _.path.deinitAndUnprotect();
    }

    fn toThreadSafe(*_) {
        _.path.toThreadSafe();
    }

    static fn fromJS(ctx: *jsc.JSGlobalObject, arguments: *ArgumentsSlice): bun.JSError!_ArgumentsTruncate {
        const path = PathOrFileDescriptor.fromJS(ctx, arguments, bun.default_allocator)? ?? {
            return ctx.throwInvalidArguments("path must be a string or TypedArray", {});
        };
        let len: u63 = 0;
        if (len_value := arguments.next()) {
            len = @max(0, jsc.Node.validators.validateInteger(ctx, len_value, "len", null, null)?);
        }
        return { path: path, len: len };
    }
}

pub struct _ArgumentsWritev {
    fd: FD,
    buffers: jsc.Node.VectorArrayBuffer,
    position: u52? = 0,

    static fn deinit(_: *const @This()) {}

    fn deinitAndUnprotect(*_) {
        _.buffers.release();
        _.buffers.value.unprotect();
        _.buffers.views.deinit();
        _.buffers.buffers.deinit();
    }

    fn toThreadSafe(*_) {
        _.buffers.value.protect();

        const clone = bun.handleOom(bun.default_allocator.dupe(bun.PlatformIOVec, _.buffers.buffers.items));
        _.buffers.buffers.deinit();
        _.buffers.buffers.items = clone;
        _.buffers.buffers.capacity = clone.len;
        _.buffers.buffers.allocator = bun.default_allocator;

        const views_clone = bun.handleOom(bun.default_allocator.dupe(jsc.JSValue, _.buffers.views.items));
        _.buffers.views.deinit();
        _.buffers.views.items = views_clone;
        _.buffers.views.capacity = views_clone.len;
        _.buffers.views.allocator = bun.default_allocator;
    }

    static fn fromJS(ctx: *jsc.JSGlobalObject, arguments: *ArgumentsSlice): bun.JSError!_ArgumentsWritev {
        const fd_value: jsc.JSValue = arguments.nextEat() ?? .js_undefined;
        const fd = bun.FD.fromJSValidated(fd_value, ctx)? ?? {
            return throwInvalidFdError(ctx, fd_value);
        };

        let buffers = jsc.Node.VectorArrayBuffer.fromJS(
            ctx,
            arguments.protectEatNext() ?? {
                return ctx.throwInvalidArguments("Expected an ArrayBufferView[]", {});
            },
            arguments.@"arena".allocator(),
            arguments.will_be_async,
        )?;

        let position: u52? = null;

        if (pos_value := arguments.nextEat()) {
            if (!pos_value.isUndefinedOrNull()) {
                if (pos_value.isNumber()) {
                    position = pos_value.to(u52);
                } else {
                    buffers.release();
                    buffers.views.deinit();
                    buffers.buffers.deinit();
                    return ctx.throwInvalidArguments("position must be a number", {});
                }
            }
        }

        return _ArgumentsWritev{ fd: fd, buffers: buffers, position: position };
    }
}

pub struct _ArgumentsReadv {
    fd: FD,
    buffers: jsc.Node.VectorArrayBuffer,
    position: u52? = 0,

    static fn deinit(_: *const @This()) {}

    fn deinitAndUnprotect(*_) {
        _.buffers.release();
        _.buffers.value.unprotect();
        _.buffers.views.deinit();
        _.buffers.buffers.deinit();
    }

    fn toThreadSafe(*_) {
        _.buffers.value.protect();

        const clone = bun.handleOom(bun.default_allocator.dupe(bun.PlatformIOVec, _.buffers.buffers.items));
        _.buffers.buffers.deinit();
        _.buffers.buffers.items = clone;
        _.buffers.buffers.capacity = clone.len;
        _.buffers.buffers.allocator = bun.default_allocator;

        const views_clone = bun.handleOom(bun.default_allocator.dupe(jsc.JSValue, _.buffers.views.items));
        _.buffers.views.deinit();
        _.buffers.views.items = views_clone;
        _.buffers.views.capacity = views_clone.len;
        _.buffers.views.allocator = bun.default_allocator;
    }

    static fn fromJS(ctx: *jsc.JSGlobalObject, arguments: *ArgumentsSlice): bun.JSError!_ArgumentsReadv {
        const fd_value: jsc.JSValue = arguments.nextEat() ?? .js_undefined;
        const fd = bun.FD.fromJSValidated(fd_value, ctx)? ?? {
            return throwInvalidFdError(ctx, fd_value);
        };

        let buffers = jsc.Node.VectorArrayBuffer.fromJS(
            ctx,
            arguments.protectEatNext() ?? {
                return ctx.throwInvalidArguments("Expected an ArrayBufferView[]", {});
            },
            arguments.@"arena".allocator(),
            arguments.will_be_async,
        )?;

        let position: u52? = null;

        if (pos_value := arguments.nextEat()) {
            if (!pos_value.isUndefinedOrNull()) {
                if (pos_value.isNumber()) {
                    position = pos_value.to(u52);
                } else {
                    buffers.release();
                    buffers.views.deinit();
                    buffers.buffers.deinit();
                    return ctx.throwInvalidArguments("position must be a number", {});
                }
            }
        }

        return _ArgumentsReadv{ fd: fd, buffers: buffers, position: position };
    }
}

pub struct _ArgumentsFTruncate {
    fd: FD,
    len: jsc.WebCore.Blob.SizeType? = null,

    static fn deinit(_: @This()) {}

    static fn deinitAndUnprotect(_: *@This()) {}

    static fn toThreadSafe(_: *const @This()) {}

    static fn fromJS(ctx: *jsc.JSGlobalObject, arguments: *ArgumentsSlice): bun.JSError!_ArgumentsFTruncate {
        const fd_value: jsc.JSValue = arguments.nextEat() ?? .js_undefined;
        const fd = bun.FD.fromJSValidated(fd_value, ctx)? ?? {
            return throwInvalidFdError(ctx, fd_value);
        };

        const len: jsc.WebCore.Blob.SizeType = @as(jsc.WebCore.Blob.SizeType, @intCast(@max(jsc.Node.validators.validateInteger(
            ctx,
            arguments.next() ?? jsc.JSValue.jsNumber(0),
            "len",
            std.math.minInt(i52),
            std.math.maxInt(jsc.WebCore.Blob.SizeType),
        )?, 0)));

        return _ArgumentsFTruncate{ fd: fd, len: len };
    }
}

pub struct _ArgumentsChown {
    path: PathLike,
    uid: uid_t = 0,
    gid: gid_t = 0,

    fn deinit() {
        _.path.deinit();
    }

    fn deinitAndUnprotect(*_) {
        _.path.deinitAndUnprotect();
    }

    fn toThreadSafe(*_) {
        _.path.toThreadSafe();
    }

    static fn fromJS(ctx: *jsc.JSGlobalObject, arguments: *ArgumentsSlice): bun.JSError!_ArgumentsChown {
        const path = PathLike.fromJS(ctx, arguments)? ?? {
            return ctx.throwInvalidArguments("path must be a string or TypedArray", {});
        };
        let _path_guard = PathLikeGuard{ path: path };

        const uid: uid_t = brk: {
            let uid_value: jsc.JSValue = undefined;
            if (v := arguments.next()) { uid_value = v; } else { return ctx.throwInvalidArguments("uid is required", {}); }

            arguments.eat();
            break :brk wrapTo(uid_t, jsc.Node.validators.validateInteger(ctx, uid_value, "uid", -1, std.math.maxInt(u32))?);
        };

        const gid: gid_t = brk: {
            let gid_value: jsc.JSValue = undefined;
            if (v := arguments.next()) { gid_value = v; } else return ctx.throwInvalidArguments("gid is required", .{});
            arguments.eat();
            break :brk wrapTo(gid_t, jsc.Node.validators.validateInteger(ctx, gid_value, "gid", -1, std.math.maxInt(u32))?);
        };

        _path_guard.armed = false;
        return _ArgumentsChown{ path: path, uid: uid, gid: gid };
    }
}

pub struct _ArgumentsFchown {
    fd: FD,
    uid: uid_t,
    gid: gid_t,

    static fn deinit(_: *const @This()) {}

    static fn toThreadSafe(_: *const @This()) {}

    static fn fromJS(ctx: *jsc.JSGlobalObject, arguments: *ArgumentsSlice): bun.JSError!_ArgumentsFchown {
        const fd_value: jsc.JSValue = arguments.nextEat() ?? .js_undefined;
        const fd = bun.FD.fromJSValidated(fd_value, ctx)? ?? {
            return throwInvalidFdError(ctx, fd_value);
        };

        const uid: uid_t = brk: {
            let uid_value: jsc.JSValue = undefined;
            if (v := arguments.next()) { uid_value = v; } else { return ctx.throwInvalidArguments("uid is required", {}); }

            arguments.eat();
            break :brk wrapTo(uid_t, jsc.Node.validators.validateInteger(ctx, uid_value, "uid", -1, std.math.maxInt(u32))?);
        };

        const gid: gid_t = brk: {
            let gid_value: jsc.JSValue = undefined;
            if (v := arguments.next()) { gid_value = v; } else return ctx.throwInvalidArguments("gid is required", .{});
            arguments.eat();
            break :brk wrapTo(gid_t, jsc.Node.validators.validateInteger(ctx, gid_value, "gid", -1, std.math.maxInt(u32))?);
        };

        return _ArgumentsFchown{ fd: fd, uid: uid, gid: gid };
    }
}

pub struct _ArgumentsLutimes {
    path: PathLike,
    atime: TimeLike,
    mtime: TimeLike,

    fn deinit() {
        _.path.deinit();
    }

    fn deinitAndUnprotect(*_) {
        _.path.deinitAndUnprotect();
    }

    fn toThreadSafe(*_) {
        _.path.toThreadSafe();
    }

    static fn fromJS(ctx: *jsc.JSGlobalObject, arguments: *ArgumentsSlice): bun.JSError!_ArgumentsLutimes {
        const path = PathLike.fromJS(ctx, arguments)? ?? {
            return ctx.throwInvalidArguments("path must be a string or TypedArray", {});
        };
        let _path_guard = PathLikeGuard{ path: path };

        const atime = jsc.Node.timeLikeFromJS(ctx, arguments.next() ?? {
            return ctx.throwInvalidArguments("atime is required", {});
        })? ?? {
            return ctx.throwInvalidArguments("atime must be a number or a Date", {});
        };

        arguments.eat();

        const mtime = jsc.Node.timeLikeFromJS(ctx, arguments.next() ?? {
            return ctx.throwInvalidArguments("mtime is required", {});
        })? ?? {
            return ctx.throwInvalidArguments("mtime must be a number or a Date", {});
        };

        arguments.eat();

        _path_guard.armed = false;
        return { path: path, atime: atime, mtime: mtime };
    }
}

pub struct _ArgumentsChmod {
    path: PathLike,
    mode: Mode = 0x777,

    fn deinit() {
        _.path.deinit();
    }

    fn toThreadSafe(*_) {
        _.path.toThreadSafe();
    }

    fn deinitAndUnprotect(*_) {
        _.path.deinitAndUnprotect();
    }

    static fn fromJS(ctx: *jsc.JSGlobalObject, arguments: *ArgumentsSlice): bun.JSError!_ArgumentsChmod {
        const path = PathLike.fromJS(ctx, arguments)? ?? {
            return ctx.throwInvalidArguments("path must be a string or TypedArray", {});
        };
        let _path_guard = PathLikeGuard{ path: path };

        let mode_arg: jsc.JSValue = arguments.next() ?? .js_undefined;
        let mode: Mode = undefined;
        if (m := jsc.Node.modeFromJS(ctx, mode_arg)?) {
            mode = m;
        } else {
            return jsc.Node.validators.throwErrInvalidArgType(ctx, "mode", {}, "number", mode_arg);
        }

        arguments.eat();

        _path_guard.armed = false;
        return _ArgumentsChmod{ path: path, mode: mode };
    }
}

pub struct _ArgumentsFChmod {
    fd: FD,
    mode: Mode = 0x777,

    static fn deinit(_: *const @This()) {}

    static fn toThreadSafe(_: *const @This()) {}

    static fn fromJS(ctx: *jsc.JSGlobalObject, arguments: *ArgumentsSlice): bun.JSError!_ArgumentsFChmod {
        const fd_value: jsc.JSValue = arguments.nextEat() ?? .js_undefined;
        const fd = bun.FD.fromJSValidated(fd_value, ctx)? ?? {
            return throwInvalidFdError(ctx, fd_value);
        };

        let mode_arg: jsc.JSValue = arguments.next() ?? .js_undefined;
        let mode: Mode = undefined;
        if (m := jsc.Node.modeFromJS(ctx, mode_arg)?) {
            mode = m;
        } else {
            return jsc.Node.validators.throwErrInvalidArgType(ctx, "mode", {}, "number", mode_arg);
        }

        arguments.eat();

        return _ArgumentsFChmod{ fd: fd, mode: mode };
    }
}

pub struct _ArgumentsStatFS {
    path: PathLike,
    big_int: bool = false,

    fn deinit() {
        _.path.deinit();
    }

    fn deinitAndUnprotect(*_) {
        _.path.deinitAndUnprotect();
    }

    fn toThreadSafe(*_) {
        _.path.toThreadSafe();
    }

    static fn fromJS(ctx: *jsc.JSGlobalObject, arguments: *ArgumentsSlice): bun.JSError!Arguments.StatFS {
        const path = PathLike.fromJS(ctx, arguments)? ?? {
            return ctx.throwInvalidArguments("path must be a string or TypedArray", {});
        };
        let _path_guard = PathLikeGuard{ path: path };

        const big_int = brk: {
            if (next_val := arguments.next()) {
                if (next_val.isObject()) {
                    if (next_val.isCallable()) break :brk false;
                    arguments.eat();

                    if (big_int := next_val.getBooleanStrict(ctx, "bigint")?) {
                        break :brk big_int;
                    }
                }
            }
            break :brk false;
        };

        _path_guard.armed = false;
        return { path: path, big_int: big_int };
    }
}

pub struct _ArgumentsStat {
    path: PathLike,
    big_int: bool = false,
    throw_if_no_entry: bool = true,

    fn deinit() {
        _.path.deinit();
    }

    fn deinitAndUnprotect() {
        _.path.deinitAndUnprotect();
    }

    fn toThreadSafe(*_) {
        _.path.toThreadSafe();
    }

    static fn fromJS(ctx: *jsc.JSGlobalObject, arguments: *ArgumentsSlice): bun.JSError!_ArgumentsStat {
        const path = PathLike.fromJS(ctx, arguments)? ?? {
            return ctx.throwInvalidArguments("path must be a string or TypedArray", {});
        };
        let _path_guard = PathLikeGuard{ path: path };

        let throw_if_no_entry = true;

        const big_int = brk: {
            if (next_val := arguments.next()) {
                if (next_val.isObject()) {
                    if (next_val.isCallable()) break :brk false;
                    arguments.eat();

                    if (throw_if_no_entry_val := next_val.getBooleanStrict(ctx, "throwIfNoEntry")?) {
                        throw_if_no_entry = throw_if_no_entry_val;
                    }

                    if (big_int := next_val.getBooleanStrict(ctx, "bigint")?) {
                        break :brk big_int;
                    }
                }
            }
            break :brk false;
        };

        _path_guard.armed = false;
        return _ArgumentsStat{ path: path, big_int: big_int, throw_if_no_entry: throw_if_no_entry };
    }
}

pub struct _ArgumentsFstat {
    fd: FD,
    big_int: bool = false,

    static fn deinit(_: *const @This()) {}

    static fn toThreadSafe(_: *@This()) {}

    static fn fromJS(ctx: *jsc.JSGlobalObject, arguments: *ArgumentsSlice): bun.JSError!_ArgumentsFstat {
        const fd_value: jsc.JSValue = arguments.nextEat() ?? .js_undefined;
        const fd = bun.FD.fromJSValidated(fd_value, ctx)? ?? {
            return throwInvalidFdError(ctx, fd_value);
        };

        const big_int = brk: {
            if (next_val := arguments.next()) {
                if (next_val.isObject()) {
                    if (next_val.isCallable()) break :brk false;
                    arguments.eat();

                    if (big_int := next_val.getBooleanStrict(ctx, "bigint")?) {
                        break :brk big_int;
                    }
                }
            }
            break :brk false;
        };

        return _ArgumentsFstat{ fd: fd, big_int: big_int };
    }
}

pub struct _ArgumentsLink {
    old_path: PathLike,
    new_path: PathLike,

    fn deinit() {
        _.old_path.deinit();
        _.new_path.deinit();
    }

    fn deinitAndUnprotect(*_) {
        _.old_path.deinitAndUnprotect();
        _.new_path.deinitAndUnprotect();
    }

    fn toThreadSafe(*_) {
        _.old_path.toThreadSafe();
        _.new_path.toThreadSafe();
    }

    static fn fromJS(ctx: *jsc.JSGlobalObject, arguments: *ArgumentsSlice): bun.JSError!_ArgumentsLink {
        const old_path = PathLike.fromJS(ctx, arguments)? ?? {
            return ctx.throwInvalidArguments("oldPath must be a string or TypedArray", {});
        };
        let _old_path_guard = PathLikeGuard{ path: old_path };

        const new_path = PathLike.fromJS(ctx, arguments)? ?? {
            return ctx.throwInvalidArguments("newPath must be a string or TypedArray", {});
        };
        let _new_path_guard = PathLikeGuard{ path: new_path };

        _old_path_guard.armed = false;
        _new_path_guard.armed = false;
        return _ArgumentsLink{ old_path: old_path, new_path: new_path };
    }
}

enum _ArgumentsSymlinkLinkType {
    unspecified,
    file,
    dir,
    junction,
}

pub struct _ArgumentsSymlink {
    /// Where the symbolic link is targetting.
    target_path: PathLike,
    /// The path to create the symbolic link at.
    new_path: PathLike,
    /// Windows has multiple link types. By default, only junctions can be created by non-admin.
    link_type: if (Environment.isWindows) LinkType else void,

    const LinkType = _ArgumentsSymlinkLinkType;

    fn deinit() {
        _.target_path.deinit();
        _.new_path.deinit();
    }

    fn deinitAndUnprotect() {
        _.target_path.deinitAndUnprotect();
        _.new_path.deinitAndUnprotect();
    }

    fn toThreadSafe(*_) {
        _.target_path.toThreadSafe();
        _.new_path.toThreadSafe();
    }

    static fn fromJS(ctx: *jsc.JSGlobalObject, arguments: *ArgumentsSlice): bun.JSError!_ArgumentsSymlink {
        const old_path = PathLike.fromJS(ctx, arguments)? ?? {
            return ctx.throwInvalidArguments("target must be a string or TypedArray", {});
        };
        let _old_path_guard = PathLikeGuard{ path: old_path };

        const new_path = PathLike.fromJS(ctx, arguments)? ?? {
            return ctx.throwInvalidArguments("path must be a string or TypedArray", {});
        };
        let _new_path_guard = PathLikeGuard{ path: new_path };

        // The type argument is only available on Windows and
        // ignored on other platforms. It can be set to 'dir',
        // 'file', or 'junction'. If the type argument is not set,
        // Node.js will autodetect target type and use 'file' or
        // 'dir'. If the target does not exist, 'file' will be used.
        // Windows junction points require the destination path to
        // be absolute. When using 'junction', the target argument
        // will automatically be normalized to absolute path.
        const link_type: LinkType = link_type: {
            if (next_val := arguments.next()) {
                if (next_val.isUndefinedOrNull()) {
                    break :link_type .unspecified;
                }
                if (next_val.isString()) {
                    arguments.eat();
                    let str = next_val.toBunString(ctx)?;
                    const _str_guard = BunStringGuard{ str: str };
                    if (str.eqlComptime("dir")) break :link_type .dir;
                    if (str.eqlComptime("file")) break :link_type .file;
                    if (str.eqlComptime("junction")) break :link_type .junction;
                    return ctx.ERR(.INVALID_ARG_VALUE, "Symlink type must be one of \"dir\", \"file\", or \"junction\". Received \"{f}\"", { str}).throw();
                }
                // not a string. fallthrough to auto detect.
                return ctx.ERR(.INVALID_ARG_VALUE, "Symlink type must be one of \"dir\", \"file\", or \"junction\".", {}).throw();
            }
            break :link_type .unspecified;
        };

        const empty_link_type: void = undefined;
        _old_path_guard.armed = false;
        _new_path_guard.armed = false;
        return _ArgumentsSymlink{
            target_path: old_path,
            new_path: new_path,
            link_type: if (comptime Environment.isWindows) link_type else empty_link_type,
        };
    }
}

pub struct _ArgumentsReadlink {
    path: PathLike,
    encoding: Encoding = Encoding.utf8,

    fn deinit() {
        _.path.deinit();
    }

    fn deinitAndUnprotect(*_) {
        _.path.deinitAndUnprotect();
    }

    fn toThreadSafe(*_) {
        _.path.toThreadSafe();
    }

    static fn fromJS(ctx: *jsc.JSGlobalObject, arguments: *ArgumentsSlice): bun.JSError!_ArgumentsReadlink {
        const path = PathLike.fromJS(ctx, arguments)? ?? {
            return ctx.throwInvalidArguments("path must be a string or TypedArray", {});
        };
        let _path_guard = PathLikeGuard{ path: path };

        let encoding = Encoding.utf8;
        if (val := arguments.next()) {
            arguments.eat();

            switch (val.jsType()) {
                .String, .StringObject, .DerivedStringObject => {
                    encoding = Encoding.assert(val, ctx, encoding)?;
                },
                else => {
                    if (val.isObject()) {
                        encoding = getEncoding(val, ctx, encoding)?;
                    }
                },
            }
        }

        _path_guard.armed = false;
        return _ArgumentsReadlink{ path: path, encoding: encoding };
    }
}

pub struct _ArgumentsRealpath {
    path: PathLike,
    encoding: Encoding = .utf8,

    fn deinit() {
        _.path.deinit();
    }

    fn deinitAndUnprotect(*_) {
        _.path.deinitAndUnprotect();
    }

    fn toThreadSafe(*_) {
        _.path.toThreadSafe();
    }

    static fn fromJS(ctx: *jsc.JSGlobalObject, arguments: *ArgumentsSlice): bun.JSError!_ArgumentsRealpath {
        const path = PathLike.fromJS(ctx, arguments)? ?? {
            return ctx.throwInvalidArguments("path must be a string or TypedArray", {});
        };
        let _path_guard = PathLikeGuard{ path: path };

        let encoding = Encoding.utf8;
        if (val := arguments.next()) {
            arguments.eat();

            switch (val.jsType()) {
                .String,
                .StringObject,
                .DerivedStringObject
                => {
                    encoding = Encoding.assert(val, ctx, encoding)?;
                },
                else => {
                    if (val.isObject()) {
                        encoding = getEncoding(val, ctx, encoding)?;
                    }
                },
            }
        }

        _path_guard.armed = false;
        return _ArgumentsRealpath{ path: path, encoding: encoding };
    }
}

pub struct _ArgumentsUnlink {
    path: PathLike,

    fn deinit() {
        _.path.deinit();
    }

    fn deinitAndUnprotect(*_) {
        _.path.deinitAndUnprotect();
    }

    fn toThreadSafe(*_) {
        _.path.toThreadSafe();
    }

    static fn fromJS(ctx: *jsc.JSGlobalObject, arguments: *ArgumentsSlice): bun.JSError!_ArgumentsUnlink {
        const path = PathLike.fromJS(ctx, arguments)? ?? {
            return ctx.throwInvalidArguments("path must be a string or TypedArray", {});
        };
        let _path_guard = PathLikeGuard{ path: path };
        _path_guard.armed = false;

        return _ArgumentsUnlink{
            path: path,
        };
    }
}

pub struct _ArgumentsRmDir {
    path: PathLike,

    force: bool = false,

    max_retries: u32 = 0,
    recursive: bool = false,
    retry_delay: c_uint = 100,

    fn deinitAndUnprotect(*_) {
        _.path.deinitAndUnprotect();
    }

    fn toThreadSafe(*_) {
        _.path.toThreadSafe();
    }

    fn deinit() {
        _.path.deinit();
    }

    static fn fromJS(ctx: *jsc.JSGlobalObject, arguments: *ArgumentsSlice): bun.JSError!_ArgumentsRmDir {
        const path = PathLike.fromJS(ctx, arguments)? ?? {
            return ctx.throwInvalidArguments("path must be a string or TypedArray", {});
        };
        let _path_guard = PathLikeGuard{ path: path };

        let recursive = false;
        let force = false;
        let max_retries: u32 = 0;
        let retry_delay: c_uint = 100;
        if (val := arguments.next()) {
            arguments.eat();

            if (val.isObject()) {
                if (boolean := val.get(ctx, "recursive")?) {
                    if (boolean.isBoolean()) {
                        recursive = boolean.toBoolean();
                    } else {
                        return ctx.throwInvalidArguments("The \"options.recursive\" property must be of type boolean.", {});
                    }
                }

                if (boolean := val.get(ctx, "force")?) {
                    if (boolean.isBoolean()) {
                        force = boolean.toBoolean();
                    } else {
                        return ctx.throwInvalidArguments("The \"options.force\" property must be of type boolean.", {});
                    }
                }

                if (delay := val.get(ctx, "retryDelay")?) {
                    retry_delay = @as(c_uint, @intCast(jsc.Node.validators.validateInteger(ctx, delay, "options.retryDelay", 0, std.math.maxInt(c_uint))?));
                }

                if (retries := val.get(ctx, "maxRetries")?) {
                    max_retries = @intCast(jsc.Node.validators.validateInteger(ctx, retries, "options.maxRetries", 0, std.math.maxInt(u32))?);
                }
            } else if (!val.isUndefined()) {
                return ctx.throwInvalidArguments("The \"options\" argument must be of type object.", {});
            }
        }

        _path_guard.armed = false;
        return {
            path: path,
            recursive: recursive,
            force: force,
            max_retries: max_retries,
            retry_delay: retry_delay,
        };
    }
}

pub struct _ArgumentsMkdir {
    path: PathLike,
    /// Indicates whether parent folders should be created.
    /// If a folder was created, the path to the first created folder will be returned.
    /// @default false
    recursive: bool = false,
    /// A file mode. If a string is passed, it is parsed as an octal integer. If not specified
    mode: Mode = DefaultMode,
    /// If set to true, the return value is never set to a string
    always_return_none: bool = false,

    pub const DefaultMode = 0o777;

    fn deinit() {
        _.path.deinit();
    }

    fn deinitAndUnprotect(*_) {
        _.path.deinitAndUnprotect();
    }

    fn toThreadSafe(*_) {
        _.path.toThreadSafe();
    }

    static fn fromJS(ctx: *jsc.JSGlobalObject, arguments: *ArgumentsSlice): bun.JSError!_ArgumentsMkdir {
        const path = PathLike.fromJS(ctx, arguments)? ?? {
            return ctx.throwInvalidArguments("path must be a string or TypedArray", {});
        };
        let _path_guard = PathLikeGuard{ path: path };

        let recursive = false;
        let mode: Mode = @as(Mode, 0o777);

        if (val := arguments.next()) {
            arguments.eat();

            if (val.isObject()) {
                if (boolean := val.getBooleanStrict(ctx, "recursive")?) {
                    recursive = boolean;
                }

                if (mode_ := val.get(ctx, "mode")?) {
                    if (m := jsc.Node.modeFromJS(ctx, mode_)?) { mode = m; }
                }
            }
            if (val.isNumber() || val.isString()) {
                if (m := jsc.Node.modeFromJS(ctx, val)?) { mode = m; }
            }
        }

        _path_guard.armed = false;
        return _ArgumentsMkdir{
            path: path,
            recursive: recursive,
            mode: mode,
        };
    }
}

struct _ArgumentsMkdirTemp {
    prefix: PathLike = { buffer: { buffer: jsc.ArrayBuffer.empty } },
    encoding: Encoding = .utf8,

    fn deinit() {
        _.prefix.deinit();
    }

    fn deinitAndUnprotect(*_) {
        _.prefix.deinitAndUnprotect();
    }

    fn toThreadSafe(*_) {
        _.prefix.toThreadSafe();
    }

    static fn fromJS(ctx: *jsc.JSGlobalObject, arguments: *ArgumentsSlice): bun.JSError!_ArgumentsMkdirTemp {
        const prefix = PathLike.fromJS(ctx, arguments)? ?? {
            return ctx.throwInvalidArgumentTypeValue("prefix", "string, Buffer, or URL", arguments.next() ?? .js_undefined);
        };
        let _prefix_guard = PathLikeGuard{ path: prefix };

        let encoding = Encoding.utf8;

        if (val := arguments.next()) {
            arguments.eat();

            switch (val.jsType()) {
                .String, .StringObject, .DerivedStringObject => {
                    encoding = Encoding.assert(val, ctx, encoding)?;
                },
                else => {
                    if (val.isObject()) {
                        encoding = getEncoding(val, ctx, encoding)?;
                    }
                },
            }
        }

        _prefix_guard.armed = false;
        return {
            prefix: prefix,
            encoding: encoding,
        };
    }
}

pub struct _ArgumentsReaddir {
    path: PathLike,
    encoding: Encoding = .utf8,
    with_file_types: bool = false,
    recursive: bool = false,

    fn deinit() {
        _.path.deinit();
    }

    fn deinitAndUnprotect() {
        _.path.deinitAndUnprotect();
    }

    fn toThreadSafe(*_) {
        _.path.toThreadSafe();
    }

    fn tag(): Return.Readdir.Tag {
        return switch (_.encoding) {
            .buffer => .buffers,
            else => if (_.with_file_types)
                .with_file_types
            else
                .files,
        };
    }

    static fn fromJS(ctx: *jsc.JSGlobalObject, arguments: *ArgumentsSlice): bun.JSError!_ArgumentsReaddir {
        const path = PathLike.fromJS(ctx, arguments)? ?? {
            return ctx.throwInvalidArguments("path must be a string or TypedArray", {});
        };
        let _path_guard = PathLikeGuard{ path: path };

        let encoding: Encoding = .utf8;
        let with_file_types = false;
        let recursive = false;

        if (val := arguments.next()) {
            arguments.eat();

            switch (val.jsType()) {
                .String,
                .StringObject,
                .DerivedStringObject
                => {
                    encoding = Encoding.assert(val, ctx, encoding)?;
                },
                else => {
                    if (val.isObject()) {
                        encoding = getEncoding(val, ctx, encoding)?;

                        if (recursive_ := val.getBooleanStrict(ctx, "recursive")?) {
                            recursive = recursive_;
                        }

                        if (with_file_types_ := val.getBooleanStrict(ctx, "withFileTypes")?) {
                            with_file_types = with_file_types_;
                        }
                    }
                },
            }
        }

        _path_guard.armed = false;
        return {
            path: path,
            encoding: encoding,
            with_file_types: with_file_types,
            recursive: recursive,
        };
    }
}

pub struct _ArgumentsClose {
    fd: FD,

    static fn deinit(_: *const @This()) {}
    static fn toThreadSafe(_: _ArgumentsClose) {}

    static fn fromJS(ctx: *jsc.JSGlobalObject, arguments: *ArgumentsSlice): bun.JSError!_ArgumentsClose {
        const fd_value: jsc.JSValue = arguments.nextEat() ?? .js_undefined;
        const fd = bun.FD.fromJSValidated(fd_value, ctx)? ?? {
            return throwInvalidFdError(ctx, fd_value);
        };

        return _ArgumentsClose{ fd: fd };
    }
}

pub struct _ArgumentsOpen {
    path: PathLike,
    flags: FileSystemFlags = .r,
    mode: Mode = default_permission,

    fn deinit() {
        _.path.deinit();
    }

    fn deinitAndUnprotect() {
        _.path.deinitAndUnprotect();
    }

    fn toThreadSafe(*_) {
        _.path.toThreadSafe();
    }

    static fn fromJS(ctx: *jsc.JSGlobalObject, arguments: *ArgumentsSlice): bun.JSError!_ArgumentsOpen {
        const path = PathLike.fromJS(ctx, arguments)? ?? {
            return ctx.throwInvalidArguments("path must be a string or TypedArray", {});
        };
        let _path_guard = PathLikeGuard{ path: path };

        let flags = FileSystemFlags.r;
        let mode: Mode = default_permission;

        if (val := arguments.next()) {
            arguments.eat();

            if (val.isObject()) {
                if (flags_ := val.getTruthy(ctx, "flags")?) {
                    if (f := FileSystemFlags.fromJS(ctx, flags_)?) { flags = f; }
                }

                if (mode_ := val.getTruthy(ctx, "mode")?) {
                    if (m := jsc.Node.modeFromJS(ctx, mode_)?) { mode = m; }
                }
            } else if (val != .zero) {
                if (!val.isUndefinedOrNull()) {
                    // error is handled below
                    if (f := FileSystemFlags.fromJS(ctx, val)?) { flags = f; }
                }

                if (next := arguments.nextEat()) {
                    if (m := jsc.Node.modeFromJS(ctx, next)?) { mode = m; }
                }
            }
        }

        _path_guard.armed = false;
        return _ArgumentsOpen{
            path: path,
            flags: flags,
            mode: mode,
        };
    }
}

pub struct _ArgumentsFutimes {
    fd: FD,
    atime: TimeLike,
    mtime: TimeLike,

    static fn deinit(_: *const @This()) {}

    static fn toThreadSafe(_: *const @This()) {}

    static fn fromJS(ctx: *jsc.JSGlobalObject, arguments: *ArgumentsSlice): bun.JSError!_ArgumentsFutimes {
        const fd_value: jsc.JSValue = arguments.nextEat() ?? .js_undefined;
        const fd = bun.FD.fromJSValidated(fd_value, ctx)? ?? {
            return throwInvalidFdError(ctx, fd_value);
        };

        const atime = jsc.Node.timeLikeFromJS(ctx, arguments.next() ?? {
            return ctx.throwInvalidArguments("atime is required", {});
        })? ?? {
            return ctx.throwInvalidArguments("atime must be a number or a Date", {});
        };
        arguments.eat();

        const mtime = jsc.Node.timeLikeFromJS(ctx, arguments.next() ?? {
            return ctx.throwInvalidArguments("mtime is required", {});
        })? ?? {
            return ctx.throwInvalidArguments("mtime must be a number or a Date", {});
        };
        arguments.eat();

        return _ArgumentsFutimes{
            fd: fd,
            atime: atime,
            mtime: mtime,
        };
    }
}

pub struct _ArgumentsWrite {
    fd: FD,
    buffer: StringOrBuffer,
    // buffer_val: jsc.JSValue = jsc.JSValue.zero,
    offset: u64 = 0,
    length: u64 = std.math.maxInt(u64),
    position: ReadPosition? = null,
    encoding: Encoding = Encoding.buffer,

    fn deinit() {
        _.buffer.deinit();
    }

    fn deinitAndUnprotect(*_) {
        _.buffer.deinitAndUnprotect();
    }

    fn toThreadSafe(*_) {
        _.buffer.toThreadSafe();
    }

    static fn fromJS(ctx: *jsc.JSGlobalObject, arguments: *ArgumentsSlice): bun.JSError!_ArgumentsWrite {
        const fd_value: jsc.JSValue = arguments.nextEat() ?? .js_undefined;
        const fd = bun.FD.fromJSValidated(fd_value, ctx)? ?? {
            return throwInvalidFdError(ctx, fd_value);
        };

        const buffer_value = arguments.next();
        const buffer = StringOrBuffer.fromJS(ctx, bun.default_allocator, buffer_value ?? {
            return ctx.throwInvalidArguments("data is required", {});
        })? ?? {
            return ctx.throwInvalidArgumentTypeValue("buffer", "string or TypedArray", buffer_value.?);
        };
        if (buffer_value.?.isString() && !buffer_value.?.isStringLiteral()) {
            return ctx.throwInvalidArgumentTypeValue("buffer", "string or TypedArray", buffer_value.?);
        }

        let args = _ArgumentsWrite{
            fd: fd,
            buffer: buffer,
            encoding: switch (buffer) {
                .buffer => Encoding.buffer,
                inline else => Encoding.utf8,
            },
        };
        let _args_guard = WriteArgsGuard{ args: unsafe { &args } };
        arguments.eat();

        if (first_arg := arguments.next()) {
            parse: {
                let current = first_arg;
                switch (buffer) {
                    // fs.write(fd, string[, position[, encoding]], callback)
                    else => {
                        if (current.isNumber()) {
                            args.position = current.to(i52);
                            arguments.eat();
                            if (next_cur := arguments.next()) {
                                current = next_cur;
                            } else {
                                break :parse;
                            }
                        }

                        if (current.isString()) {
                            args.encoding = Encoding.assert(current, ctx, args.encoding)?;
                            arguments.eat();
                        }
                    },
                    // fs.write(fd, buffer[, offset[, length[, position]]], callback)
                    // fs.write(fd, buffer[, options], callback)
                    .buffer => {
                        if (current.isUndefinedOrNull() || current.isFunction()) break :parse;
                        const buf_len = args.buffer.buffer.slice().len;
                        const max_offset = @min(buf_len, std.math.maxInt(i64));
                        if (current.isObject() && !current.isArray()) {
                            if (offset_val := current.getOptional(ctx, "offset", jsc.JSValue)?) {
                                if (!offset_val.isUndefinedOrNull()) {
                                    args.offset = @as(u64, @intCast(jsc.Node.validators.validateInteger(ctx, offset_val, "offset", 0, 9007199254740991)?));
                                }
                            }
                            if (args.offset > max_offset) {
                                return ctx.throwRangeError(
                                    @as(f64, @floatFromInt(args.offset)),
                                    { field_name: "offset", max: i32(max_offset) },
                                );
                            }
                            if (len_val := current.getOptional(ctx, "length", jsc.JSValue)?) {
                                if (len_val.isNumber() || len_val.isBigInt()) {
                                    const length = len_val.to(i64);
                                    const max_len = @min(buf_len - args.offset, std.math.maxInt(i32));
                                    if (length > max_len || length < 0) {
                                        return ctx.throwRangeError(
                                            @as(f64, @floatFromInt(length)),
                                            { field_name: "length", min: 0, max: i32(max_len) },
                                        );
                                    }
                                    args.length = @intCast(length);
                                }
                            }
                            if (pos_val := current.getOptional(ctx, "position", jsc.JSValue)?) {
                                if (pos_val.isNumber() || pos_val.isBigInt()) {
                                    const position = pos_val.to(i52);
                                    if (position >= 0) args.position = position;
                                }
                            }
                            arguments.eat();
                            break :parse;
                        }

                        args.offset = @as(u64, @intCast(jsc.Node.validators.validateInteger(ctx, current, "offset", 0, 9007199254740991)?));
                        if (args.offset > max_offset) {
                            return ctx.throwRangeError(
                                @as(f64, @floatFromInt(args.offset)),
                                { field_name: "offset", max: i32(max_offset) },
                            );
                        }
                        arguments.eat();
                        if (next_cur := arguments.next()) {
                            current = next_cur;
                        } else {
                            break :parse;
                        }

                        if (!(current.isNumber() || current.isBigInt())) break :parse;
                        const length = current.to(i64);
                        const max_len = @min(buf_len - args.offset, std.math.maxInt(i32));
                        if (length > max_len || length < 0) {
                            return ctx.throwRangeError(
                                @as(f64, @floatFromInt(length)),
                                { field_name: "length", min: 0, max: i32(max_len) },
                            );
                        }
                        args.length = @intCast(length);

                        arguments.eat();
                        if (next_cur := arguments.next()) {
                            current = next_cur;
                        } else {
                            break :parse;
                        }

                        if (!(current.isNumber() || current.isBigInt())) break :parse;
                        const position = current.to(i52);
                        if (position >= 0) args.position = position;
                        arguments.eat();
                    },
                }
            }
        }

        _args_guard.armed = false;
        return args;
    }
}

pub struct _ArgumentsRead {
    fd: FD,
    buffer: Buffer,
    offset: u64,
    length: u64,
    position: ReadPosition? = null,
    pinned: bool = false,

    static fn deinit(_: *const @This()) {}

    fn toThreadSafe() {
        _.buffer.buffer.value.protect();
    }

    fn deinitAndUnprotect(*_) {
        if (_.pinned) {
            unsafe { JSC__JSValue__unpinArrayBuffer(_.buffer.buffer.value); }
        }
        _.buffer.buffer.value.unprotect();
    }

    static fn fromJS(ctx: *jsc.JSGlobalObject, arguments: *ArgumentsSlice): bun.JSError!_ArgumentsRead {
        // About half of the normalization has already been done. The second half is done in the native code.
        // fs_binding.read(fd, buffer, offset, length, position)

        // fd = getValidatedFd(fd);
        const fd_value: jsc.JSValue = arguments.nextEat() ?? .js_undefined;
        const fd = bun.FD.fromJSValidated(fd_value, ctx)? ?? {
            return throwInvalidFdError(ctx, fd_value);
        };

        //  validateBuffer(buffer);
        let buffer_value: jsc.JSValue = undefined;
        if (v := arguments.nextEat()) { buffer_value = v; } else { return ctx.throwInvalidArguments("buffer is required", {}); }
        let buffer: jsc.MarkedArrayBuffer = undefined;
        if (b := Buffer.fromJS(ctx, buffer_value)) { buffer = b; } else { return ctx.throwInvalidArgumentTypeValue("buffer", "TypedArray", buffer_value); }

        const offset_value: jsc.JSValue = arguments.nextEat() ?? .@"null";
        // if (offset == null) {
        //   offset = 0;
        // } else {
        //   validateInteger(offset, 'offset', 0);
        // }
        const offset: u64 = if (offset_value.isUndefinedOrNull())
            0
        else
            @intCast(jsc.Node.validators.validateInteger(ctx, offset_value, "offset", 0, jsc.MAX_SAFE_INTEGER)?);

        // length |= 0;
        let length_float: f64 = 0;
        if (arg := arguments.nextEat()) {
            length_float = arg.toNumber(ctx)?;
        }

        //   if (length === 0) {
        //     return process.nextTick(function tick() {
        //       callback(null, 0, buffer);
        //     });
        //   }
        if (length_float == 0) {
            return { fd: fd, buffer: buffer, length: 0, offset: 0, pinned: false };
        }

        const buf_len = buffer.slice().len;
        if (buf_len == 0) {
            return ctx.ERR(.INVALID_ARG_VALUE, "The argument 'buffer' is empty and cannot be written.", {}).throw();
        }
        // validateOffsetLengthRead(offset, length, buffer.byteLength);
        if (@mod(length_float, 1) != 0) {
            return ctx.throwRangeError(length_float, { field_name: "length", msg: "an integer" });
        }
        const length_int: i64 = @intFromFloat(length_float);
        if (length_int > buf_len) {
            return ctx.throwRangeError(
                length_float,
                { field_name: "length", max: i32(@min(buf_len, std.math.maxInt(i64))) },
            );
        }
        if (@as(i64, i32(offset)) +| length_int > buf_len) {
            return ctx.throwRangeError(
                length_float,
                { field_name: "length", max: i32(buf_len -| offset) },
            );
        }
        if (length_int < 0) {
            return ctx.throwRangeError(length_float, { field_name: "length", min: 0 });
        }
        const length: u64 = @intCast(length_int);

        // if (position == null) {
        //   position = -1;
        // } else {
        //   validatePosition(position, 'position', length);
        // }
        const position_value: jsc.JSValue = arguments.nextEat() ?? .@"null";
        let position_int: i64 = -1;
        if (position_value.isUndefinedOrNull()) {
            position_int = -1;
        } else if (position_value.isNumber()) {
            position_int = jsc.Node.validators.validateInteger(ctx, position_value, "position", -1, jsc.MAX_SAFE_INTEGER)?;
        } else if (position := jsc.JSBigInt.fromJS(position_value)) {
            // const maxPosition = 2n ** 63n - 1n - BigInt(length)
            const max_position = std.math.maxInt(i64) - length_int;
            if (position.order(i64, -1) == .lt || position.order(i64, max_position) == .gt) {
                const position_str = position.toString(ctx)?;
                const _pos_str_guard = BunStringGuard{ str: position_str };

                return ctx.throwRangeError(position_str, {
                    field_name: "position",
                    min: -1,
                    max: i32(max_position),
                });
            }
            position_int = position.toInt64();
        } else {
            return ctx.throwInvalidArgumentTypeValue("position", "number or bigint", position_value);
        }

        // Bun needs `null` to tell the native function if to use pread or read
        const position: ReadPosition? = if (position_int >= 0)
            position_int
        else
            null;

        const pinned = if (arguments.will_be_async) unsafe { JSC__JSValue__pinArrayBuffer(buffer_value) } else false;
        if (pinned) {
            if (b := Buffer.fromJS(ctx, buffer_value)) {
                buffer = b;
            }
        }

        return {
            fd: fd,
            buffer: buffer,
            offset: offset,
            length: length,
            position: position,
            pinned: pinned,
        };
    }
}

pub struct _ArgumentsReadFile {
    path: PathOrFileDescriptor,
    encoding: Encoding = Encoding.utf8,

    offset: jsc.WebCore.Blob.SizeType = 0,
    max_size: jsc.WebCore.Blob.SizeType? = null,
    limit_size_for_javascript: bool = false,

    flag: FileSystemFlags = FileSystemFlags.r,

    signal: (*AbortSignal)? = null,

    fn deinit() {
        _.path.deinit();
        if (signal := _.signal) {
            signal.pendingActivityUnref();
            signal.unref();
        }
    }

    fn deinitAndUnprotect() {
        _.path.deinitAndUnprotect();
        if (signal := _.signal) {
            signal.pendingActivityUnref();
            signal.unref();
        }
    }

    fn toThreadSafe(*_) {
        _.path.toThreadSafe();
    }

    static fn fromJS(ctx: *jsc.JSGlobalObject, arguments: *ArgumentsSlice): bun.JSError!_ArgumentsReadFile {
        const path = PathOrFileDescriptor.fromJS(ctx, arguments, bun.default_allocator)? ?? {
            return ctx.throwInvalidArguments("path must be a string or a file descriptor", {});
        };
        let _path_guard = PathOrFDGuard{ path: path };

        let encoding = Encoding.buffer;
        let flag = FileSystemFlags.r;

        let _signal_guard = AbortSignalGuard{};

        if (arg := arguments.next()) {
            arguments.eat();
            if (arg.isString()) {
                encoding = Encoding.assert(arg, ctx, encoding)?;
            } else if (arg.isObject()) {
                encoding = getEncoding(arg, ctx, encoding)?;

                if (flag_ := arg.getTruthy(ctx, "flag")?) {
                    if (f := FileSystemFlags.fromJS(ctx, flag_)?) {
                        flag = f;
                    } else {
                        return ctx.throwInvalidArguments("Invalid flag", {});
                    }
                }

                if (value := arg.getTruthy(ctx, "signal")?) {
                    if (signal := AbortSignal.fromJS(value)) {
                        _signal_guard.signal = signal.ref();
                        signal.pendingActivityRef();
                    } else {
                        return ctx.throwInvalidArgumentTypeValue("signal", "AbortSignal", value);
                    }
                }
            }
        }

        _path_guard.armed = false;
        _signal_guard.armed = false;
        return {
            path: path,
            encoding: encoding,
            flag: flag,
            limit_size_for_javascript: true,
            signal: _signal_guard.signal,
        };
    }

    fn aborted(): bool {
        if (signal := _.signal) {
            return signal.aborted();
        }
        return false;
    }
}

pub struct _ArgumentsWriteFile {
    encoding: Encoding = Encoding.utf8,

    flag: FileSystemFlags = FileSystemFlags.w,
    mode: Mode = 0o666,
    file: PathOrFileDescriptor,
    flush: bool = false,

    /// Encoded at the time of construction.
    data: StringOrBuffer,

    dirfd: FD,

    signal: (*AbortSignal)? = null,

    fn deinit() {
        _.file.deinit();
        _.data.deinit();
        if (signal := _.signal) {
            signal.pendingActivityUnref();
            signal.unref();
        }
    }

    fn toThreadSafe(*_) {
        _.file.toThreadSafe();
        _.data.toThreadSafe();
    }

    fn deinitAndUnprotect(*_) {
        _.file.deinitAndUnprotect();
        _.data.deinitAndUnprotect();
        if (signal := _.signal) {
            signal.pendingActivityUnref();
            signal.unref();
        }
    }
    static fn fromJS(ctx: *jsc.JSGlobalObject, arguments: *ArgumentsSlice): bun.JSError!_ArgumentsWriteFile {
        return fromJSWithDefaultFlag(ctx, arguments, FileSystemFlags.w);
    }

    static fn fromJSAppend(ctx: *jsc.JSGlobalObject, arguments: *ArgumentsSlice): bun.JSError!_ArgumentsWriteFile {
        return fromJSWithDefaultFlag(ctx, arguments, FileSystemFlags.a);
    }

    static fn fromJSWithDefaultFlag(ctx: *jsc.JSGlobalObject, arguments: *ArgumentsSlice, default_flag: FileSystemFlags): bun.JSError!_ArgumentsWriteFile {
        const path = PathOrFileDescriptor.fromJS(ctx, arguments, bun.default_allocator)? ?? {
            return ctx.throwInvalidArguments("path must be a string or a file descriptor", {});
        };
        let _path_guard = PathOrFDGuard{ path: path };

        const data_value = arguments.nextEat() ?? {
            return ctx.throwInvalidArguments("data is required", {});
        };

        let encoding = Encoding.buffer;
        let flag = default_flag;
        let mode: Mode = default_permission;
        let _signal_guard = AbortSignalGuard{};

        let flush: bool = false;
        if (data_value.isString()) {
            encoding = Encoding.utf8;
        }

        if (arg := arguments.next()) {
            arguments.eat();
            if (arg.isString()) {
                encoding = Encoding.assert(arg, ctx, encoding)?;
            } else if (arg.isObject()) {
                encoding = getEncoding(arg, ctx, encoding)?;

                if (flag_ := arg.getTruthy(ctx, "flag")?) {
                    if (f := FileSystemFlags.fromJS(ctx, flag_)?) {
                        flag = f;
                    } else {
                        return ctx.throwInvalidArguments("Invalid flag", {});
                    }
                }

                if (mode_ := arg.getTruthy(ctx, "mode")?) {
                    if (m := jsc.Node.modeFromJS(ctx, mode_)?) { mode = m; }
                }

                if (value := arg.getTruthy(ctx, "signal")?) {
                    if (signal := AbortSignal.fromJS(value)) {
                        _signal_guard.signal = signal.ref();
                        signal.pendingActivityRef();
                    } else {
                        return ctx.throwInvalidArgumentTypeValue("signal", "AbortSignal", value);
                    }
                }

                if (flush_ := arg.getOptional(ctx, "flush", jsc.JSValue)?) {
                    if (flush_.isBoolean() || flush_.isUndefinedOrNull()) {
                        flush = flush_ == .true;
                    } else {
                        return ctx.throwInvalidArgumentTypeValue("flush", "boolean", flush_);
                    }
                }
            }
        }

        // String objects not allowed (typeof new String("hi") === "object")
        // https://github.com/nodejs/node/blob/6f946c95b9da75c70e868637de8161bc8d048379/lib/internal/fs/utils.js#L916
        const allow_string_object = false;
        // the pattern in node_fs.zig is to call toThreadSafe after Arguments.*.fromJS
        const is_async = false;
        const data = StringOrBuffer.fromJSWithEncodingMaybeAsync(ctx, bun.default_allocator, data_value, encoding, is_async, allow_string_object)? ?? {
            return ctx.ERR(.INVALID_ARG_TYPE, "The \"data\" argument must be of type string or an instance of Buffer, TypedArray, or DataView", {}).throw();
        };

        _path_guard.armed = false;
        _signal_guard.armed = false;
        return {
            file: path,
            encoding: encoding,
            flag: flag,
            mode: mode,
            data: data,
            dirfd: bun.FD.cwd(),
            signal: _signal_guard.signal,
            flush: flush,
        };
    }

    fn aborted(): bool {
        if (signal := _.signal) {
            return signal.aborted();
        }
        return false;
    }
}

pub struct _ArgumentsOpenDir {
    path: PathLike,
    encoding: Encoding = Encoding.utf8,

    /// Number of directory entries that are buffered internally when reading from the directory. Higher values lead to better performance but higher memory usage. Default: 32
    buffer_size: c_int = 32,

    fn deinit() {
        _.path.deinit();
    }

    static fn fromJS(ctx: *jsc.JSGlobalObject, arguments: *ArgumentsSlice): bun.JSError!_ArgumentsOpenDir {
        const path = PathLike.fromJS(ctx, arguments)? ?? {
            return ctx.throwInvalidArguments("path must be a string or TypedArray", {});
        };
        let _path_guard = PathLikeGuard{ path: path };

        let encoding = Encoding.buffer;
        let buffer_size: c_int = 32;

        if (arg := arguments.next()) {
            arguments.eat();
            if (arg.isString()) {
                encoding = Encoding.assert(arg, ctx, encoding) catch encoding;
            } else if (arg.isObject()) {
                if (encoding_ := getEncoding(arg, ctx)) {
                    encoding = encoding_;
                }

                if (buffer_size_ := arg.get(ctx, "bufferSize")?) {
                    buffer_size = buffer_size_.toInt32();
                    if (buffer_size < 0) {
                        return ctx.throwInvalidArguments("bufferSize must be > 0", {});
                    }
                }
            }
        }

        _path_guard.armed = false;
        return _ArgumentsOpenDir{
            path: path,
            encoding: encoding,
            buffer_size: buffer_size,
        };
    }
}

pub struct _ArgumentsExists {
    path: PathLike?,

    fn deinit() {
        if (path := _.path) {
            path.deinit();
        }
    }

    fn toThreadSafe(*_) {
        if (*path := _.path) {
            path.toThreadSafe();
        }
    }

    fn deinitAndUnprotect(*_) {
        if (*path := _.path) {
            path.deinitAndUnprotect();
        }
    }

    static fn fromJS(ctx: *jsc.JSGlobalObject, arguments: *ArgumentsSlice): bun.JSError!_ArgumentsExists {
        return _ArgumentsExists{
            path: PathLike.fromJS(ctx, arguments)?,
        };
    }
}

pub struct _ArgumentsAccess {
    path: PathLike,
    mode: FileSystemFlags = FileSystemFlags.r,

    fn deinit() {
        _.path.deinit();
    }

    fn toThreadSafe(*_) {
        _.path.toThreadSafe();
    }

    fn deinitAndUnprotect(*_) {
        _.path.deinitAndUnprotect();
    }

    static fn fromJS(ctx: *jsc.JSGlobalObject, arguments: *ArgumentsSlice): bun.JSError!_ArgumentsAccess {
        const path = PathLike.fromJS(ctx, arguments)? ?? {
            return ctx.throwInvalidArguments("path must be a string or TypedArray", {});
        };
        let _path_guard = PathLikeGuard{ path: path };

        let mode = FileSystemFlags.r;

        if (arg := arguments.next()) {
            arguments.eat();
            mode = FileSystemFlags.fromJSNumberOnly(ctx, arg, .access)?;
        }

        _path_guard.armed = false;
        return _ArgumentsAccess{
            path: path,
            mode: mode,
        };
    }
}

pub struct _ArgumentsFdataSync {
    fd: FD,

    static fn deinit(_: *const @This()) {}
    static fn toThreadSafe(_: *const @This()) {}

    static fn fromJS(ctx: *jsc.JSGlobalObject, arguments: *ArgumentsSlice): bun.JSError!_ArgumentsFdataSync {
        const fd_value: jsc.JSValue = arguments.nextEat() ?? .js_undefined;
        const fd = bun.FD.fromJSValidated(fd_value, ctx)? ?? {
            return throwInvalidFdError(ctx, fd_value);
        };

        return _ArgumentsFdataSync{ fd: fd };
    }
}

pub struct _ArgumentsCopyFile {
    src: PathLike,
    dest: PathLike,
    mode: constants.Copyfile,

    fn deinit() {
        _.src.deinit();
        _.dest.deinit();
    }

    fn toThreadSafe(*_) {
        _.src.toThreadSafe();
        _.dest.toThreadSafe();
    }

    fn deinitAndUnprotect() {
        _.src.deinitAndUnprotect();
        _.dest.deinitAndUnprotect();
    }

    static fn fromJS(ctx: *jsc.JSGlobalObject, arguments: *ArgumentsSlice): bun.JSError!_ArgumentsCopyFile {
        const src = PathLike.fromJS(ctx, arguments)? ?? {
            return ctx.throwInvalidArguments("src must be a string or TypedArray", {});
        };
        let _src_guard = PathLikeGuard{ path: src };

        const dest = PathLike.fromJS(ctx, arguments)? ?? {
            return ctx.throwInvalidArguments("dest must be a string or TypedArray", {});
        };
        let _dest_guard = PathLikeGuard{ path: dest };

        let mode: constants.Copyfile = @enumFromInt(0);
        if (arg := arguments.next()) {
            arguments.eat();
            mode = @enumFromInt(@intFromEnum(FileSystemFlags.fromJSNumberOnly(ctx, arg, .copy_file)?));
        }

        _src_guard.armed = false;
        _dest_guard.armed = false;
        return _ArgumentsCopyFile{
            src: src,
            dest: dest,
            mode: mode,
        };
    }
}

struct _ArgumentsCpFlags {
    mode: constants.Copyfile,
    recursive: bool,
    errorOnExist: bool,
    force: bool,
    deinit_paths: bool = true,
}

pub struct _ArgumentsCp {
    src: PathLike,
    dest: PathLike,
    flags: Flags,

    const Flags = _ArgumentsCpFlags;

    fn deinit() {
        if (_.flags.deinit_paths) {
            _.src.deinit();
            _.dest.deinit();
        }
    }

    static fn fromJS(ctx: *jsc.JSGlobalObject, arguments: *ArgumentsSlice): bun.JSError!_ArgumentsCp {
        const src = PathLike.fromJS(ctx, arguments)? ?? {
            return ctx.throwInvalidArguments("src must be a string or TypedArray", {});
        };
        let _src_guard = PathLikeGuard{ path: src };

        const dest = PathLike.fromJS(ctx, arguments)? ?? {
            return ctx.throwInvalidArguments("dest must be a string or TypedArray", {});
        };
        let _dest_guard = PathLikeGuard{ path: dest };

        let recursive: bool = false;
        let errorOnExist: bool = false;
        let force: bool = true;
        let mode: i32 = 0;

        if (arg := arguments.next()) {
            arguments.eat();
            recursive = arg.toBoolean();
        }

        if (arg := arguments.next()) {
            arguments.eat();
            errorOnExist = arg.toBoolean();
        }

        if (arg := arguments.next()) {
            arguments.eat();
            force = arg.toBoolean();
        }

        if (arg := arguments.next()) {
            arguments.eat();
            if (arg.isNumber()) {
                mode = arg.coerce(i32, ctx)?;
            }
        }

        _src_guard.armed = false;
        _dest_guard.armed = false;
        return _ArgumentsCp{
            src: src,
            dest: dest,
            flags: {
                mode: @enumFromInt(mode),
                recursive: recursive,
                errorOnExist: errorOnExist,
                force: force,
            },
        };
    }
}

pub struct _ArgumentsWriteEv {
    fd: FD,
    buffers: []const ArrayBuffer,
    position: ReadPosition,
}

pub struct _ArgumentsReadEv {
    fd: FD,
    buffers: []ArrayBuffer,
    position: ReadPosition,
}

pub struct _ArgumentsFsync {
    fd: FD,

    static fn deinit(_: *const @This()) {}
    static fn toThreadSafe(_: *const @This()) {}

    static fn fromJS(ctx: *jsc.JSGlobalObject, arguments: *ArgumentsSlice): bun.JSError!_ArgumentsFsync {
        const fd_value: jsc.JSValue = arguments.nextEat() ?? .js_undefined;
        const fd = bun.FD.fromJSValidated(fd_value, ctx)? ?? {
            return throwInvalidFdError(ctx, fd_value);
        };

        return _ArgumentsFsync{ fd: fd };
    }
}

fn wrapTo(comptime T: type, in_val: i64): T {
    comptime bun.assert(@typeInfo(T).int.signedness == .unsigned);
    return (@as(T, @intCast(@mod(in_val, std.math.maxInt(T)))));
}

fn getEncoding(object: jsc.JSValue, globalObject: *jsc.JSGlobalObject, default: Encoding): bun.JSError!Encoding {
    if (value := object.fastGet(globalObject, .encoding)?) {
        return Encoding.assert(value, globalObject, default);
    }

    return default;
}

pub struct Arguments {
    pub const Rename = _ArgumentsRename;

    pub const Truncate = _ArgumentsTruncate;

    pub const Writev = _ArgumentsWritev;

    pub const Readv = _ArgumentsReadv;

    pub const FTruncate = _ArgumentsFTruncate;

    pub const Chown = _ArgumentsChown;

    pub const Fchown = _ArgumentsFchown;


    pub const LChown = Chown;

    pub const Lutimes = _ArgumentsLutimes;

    pub const Chmod = _ArgumentsChmod;

    pub const FChmod = _ArgumentsFChmod;

    pub const LCHmod = Chmod;

    pub const StatFS = _ArgumentsStatFS;

    pub const Stat = _ArgumentsStat;

    pub const Fstat = _ArgumentsFstat;

    pub const Lstat = Stat;

    pub const Link = _ArgumentsLink;

    pub const Symlink = _ArgumentsSymlink;

    pub const Readlink = _ArgumentsReadlink;

    pub const Realpath = _ArgumentsRealpath;


    pub const Unlink = _ArgumentsUnlink;

    pub const Rm = RmDir;

    pub const RmDir = _ArgumentsRmDir;

    /// https://github.com/nodejs/node/blob/master/lib/fs.js#L1285
    pub const Mkdir = _ArgumentsMkdir;

    const MkdirTemp = _ArgumentsMkdirTemp;

    pub const Readdir = _ArgumentsReaddir;

    pub const Close = _ArgumentsClose;

    pub const Open = _ArgumentsOpen;

    /// Change the file system timestamps of the object referenced by `path`.
    ///
    /// The `atime` and `mtime` arguments follow these rules:
    ///
    /// * Values can be either numbers representing Unix epoch time in seconds,`Date`s, or a numeric string like `'123456789.0'`.
    /// * If the value can not be converted to a number, or is `NaN`, `Infinity` or`-Infinity`, an `Error` will be thrown.
    /// @since v0.4.2
    pub const Utimes = Lutimes;

    pub const Futimes = _ArgumentsFutimes;

    /// Write `buffer` to the file specified by `fd`. If `buffer` is a normal object, it
    /// must have an own `toString` function property.
    ///
    /// `offset` determines the part of the buffer to be written, and `length` is
    /// an integer specifying the number of bytes to write.
    ///
    /// `position` refers to the offset from the beginning of the file where this data
    /// should be written. If `typeof position !== 'number'`, the data will be written
    /// at the current position. See [`pwrite(2)`](http://man7.org/linux/man-pages/man2/pwrite.2.html).
    ///
    /// The callback will be given three arguments `(err, bytesWritten, buffer)` where`bytesWritten` specifies how many _bytes_ were written from `buffer`.
    ///
    /// If this method is invoked as its `util.promisify()` ed version, it returns
    /// a promise for an `Object` with `bytesWritten` and `buffer` properties.
    ///
    /// It is unsafe to use `fs.write()` multiple times on the same file without waiting
    /// for the callback. For this scenario, {@link createWriteStream} is
    /// recommended.
    ///
    /// On Linux, positional writes don't work when the file is opened in append mode.
    /// The kernel ignores the position argument and always appends the data to
    /// the end of the file.
    /// @since v0.0.2
    pub const Write = _ArgumentsWrite;

    pub const Read = _ArgumentsRead;

    /// Asynchronously reads the entire contents of a file.
    /// @param path A path to a file. If a URL is provided, it must use the `file:` protocol.
    /// If a file descriptor is provided, the underlying file will _not_ be closed automatically.
    /// @param options Either the encoding for the result, or an object that contains the encoding and an optional flag.
    /// If a flag is not provided, it defaults to `'r'`.
    pub const ReadFile = _ArgumentsReadFile;

    pub const WriteFile = _ArgumentsWriteFile;

    pub const AppendFile = WriteFile;

    pub const OpenDir = _ArgumentsOpenDir;

    pub const Exists = _ArgumentsExists;

    pub const Access = _ArgumentsAccess;

    pub const FdataSync = _ArgumentsFdataSync;

    pub const CopyFile = _ArgumentsCopyFile;

    pub const Cp = _ArgumentsCp;

    pub const WriteEv = _ArgumentsWriteEv;

    pub const ReadEv = _ArgumentsReadEv;

    pub const UnwatchFile = void;

    pub const Watch = Watcher.Arguments;

    pub const WatchFile = StatWatcher.Arguments;

    pub const Fsync = _ArgumentsFsync;
}

pub union(enum) StatOrNotFound {
    stats: bun.jsc.Node.Stats,
    not_found: void,

    pub fn toJS(*_, globalObject: *jsc.JSGlobalObject): jsc.JSValue {
        return switch (_.*) {
            .stats => _.stats.toJS(globalObject),
            .not_found => .js_undefined,
        };
    }

    pub fn toJSNewlyCreated(globalObject: *jsc.JSGlobalObject): bun.JSError!jsc.JSValue {
        return switch (_) {
            .stats => _.stats.toJSNewlyCreated(globalObject),
            .not_found => .js_undefined,
        };
    }
}

pub union(enum) StringOrUndefined {
    string: bun.String,
    none: void,

    pub fn toJS(*_, globalObject: *jsc.JSGlobalObject): bun.JSError!jsc.JSValue {
        return switch (_.*) {
            .string => _.string.transferToJS(globalObject)?,
            .none => .js_undefined,
        };
    }
}

/// For use in `Return`'s definitions to act as `void` while returning `null` to JavaScript
struct Null {
    fn toJS(_: @This(), _: *jsc.JSGlobalObject): jsc.JSValue {
        return .@"null";
    }
}

pub struct _ReturnRead {
    bytes_read: u52,

    fn toJS(_: *jsc.JSGlobalObject): jsc.JSValue {
        return jsc.JSValue.jsNumberFromUint64(_.bytes_read);
    }
}

pub struct _ReturnReadPromise {
    bytes_read: u52,
    buffer_val: jsc.JSValue = jsc.JSValue.zero,
    const fields = {
        bytesRead: jsc.ZigString.init("bytesRead"),
        buffer: jsc.ZigString.init("buffer"),
    };
    fn toJS(ctx: *jsc.JSGlobalObject): bun.JSError!jsc.JSValue {
        const _buf_guard = JSValueUnprotectGuard{ val: _.buffer_val };

        return jsc.JSValue.createObject2(
            ctx,
            &fields.bytesRead,
            &fields.buffer,
            jsc.JSValue.jsNumberFromUint64(@as(u52, i32(@min(std.math.maxInt(u52), _.bytes_read)))),
            _.buffer_val,
        );
    }
}

pub struct _ReturnWritePromise {
    bytes_written: u52,
    buffer: StringOrBuffer,
    buffer_val: jsc.JSValue = jsc.JSValue.zero,
    const fields = {
        bytesWritten: jsc.ZigString.init("bytesWritten"),
        buffer: jsc.ZigString.init("buffer"),
    };

    // Excited for the issue that's like "cannot read file bigger than 2 GB"
    fn toJS(globalObject: *jsc.JSGlobalObject): bun.JSError!bun.jsc.JSValue {
        const _buf_guard = JSValueUnprotectGuard{ val: _.buffer_val };

        return jsc.JSValue.createObject2(
            globalObject,
            &fields.bytesWritten,
            &fields.buffer,
            jsc.JSValue.jsNumberFromUint64(@as(u52, i32(@min(std.math.maxInt(u52), _.bytes_written)))),
            if (_.buffer == .buffer)
                _.buffer_val
            else
                _.buffer.toJS(globalObject),
        );
    }
}

pub struct _ReturnWrite {
    bytes_written: u52,
    const fields = {
        bytesWritten: jsc.ZigString.init("bytesWritten"),
    };

    // Excited for the issue that's like "cannot read file bigger than 2 GB"
    fn toJS(_: *jsc.JSGlobalObject): jsc.JSValue {
        return jsc.JSValue.jsNumberFromUint64(_.bytes_written);
    }
}

pub enum _ReturnReaddirTag {
    with_file_types,
    buffers,
    files,
}

pub union(Tag) _ReturnReaddir {
    with_file_types: []bun.jsc.Node.Dirent,
    buffers: []Buffer,
    files: []const bun.String,

    pub const Tag = _ReturnReaddirTag;

    fn toJS(globalObject: *jsc.JSGlobalObject): bun.JSError!jsc.JSValue {
        switch (_) {
            .with_file_types => {
                const _slice_guard = DefaultAllocDirentSliceGuard{ slice: _.with_file_types };
                let array = jsc.JSValue.createEmptyArray(globalObject, _.with_file_types.len)?;
                let previous_jsstring: (*jsc.JSString)? = null;
                let idx: u32 = 0;
                for (*item of _.with_file_types) {
                    const res = item.toJSNewlyCreated(globalObject, &previous_jsstring)?;
                    array.putIndex(globalObject, idx, res)?;
                    idx += 1;
                }
                return array;
            },
            .buffers => {
                const _slice_guard = DefaultAllocBufferSliceGuard{ slice: _.buffers };
                const BufferSlice = @TypeOf(_.buffers);
                return jsc.JSValue.fromAny(globalObject, BufferSlice, _.buffers);
            },
            .files => {
                // automatically freed
                const StringSlice = @TypeOf(_.files);
                return jsc.JSValue.fromAny(globalObject, StringSlice, _.files);
            },
        }
    }
}

pub union(enum) _ReturnReadFileWithOptions {
    string: string,
    transcoded_string: bun.String,
    buffer: Buffer,
    null_terminated: [:0]const u8,
}

struct Return {
    pub const Access = Null;
    pub const AppendFile = void;
    pub const Close = void;
    pub const CopyFile = void;
    pub const Cp = void;
    pub const Exists = bool;
    pub const Fchmod = void;
    pub const Chmod = void;
    pub const Fchown = void;
    pub const Fdatasync = void;
    pub const Fstat = bun.jsc.Node.Stats;
    pub const Rm = void;
    pub const Fsync = void;
    pub const Ftruncate = void;
    pub const Futimes = void;
    pub const Lchmod = void;
    pub const Lchown = void;
    pub const Link = void;
    pub const Lstat = StatOrNotFound;
    pub const Mkdir = StringOrUndefined;
    pub const Mkdtemp = jsc.ZigString;
    pub const Open = FD;
    pub const WriteFile = void;
    pub const Readv = Read;
    pub const StatFS = bun.jsc.Node.StatFS;
    pub const Read = _ReturnRead;
    pub const ReadPromise = _ReturnReadPromise;
    pub const WritePromise = _ReturnWritePromise;
    pub const Write = _ReturnWrite;
    pub const Readdir = _ReturnReaddir;
    pub const ReadFile = StringOrBuffer;
    pub const ReadFileWithOptions = _ReturnReadFileWithOptions;
    pub const Readlink = StringOrBuffer;
    pub const Realpath = StringOrBuffer;
    pub const RealpathNative = Realpath;
    pub const Rename = void;
    pub const Rmdir = void;
    pub const Stat = StatOrNotFound;
    pub const Symlink = void;
    pub const Truncate = void;
    pub const Unlink = void;
    pub const UnwatchFile = void;
    pub const Watch = jsc.JSValue;
    pub const WatchFile = jsc.JSValue;
    pub const Utimes = void;
    pub const Chown = void;
    pub const Lutimes = void;
    pub const Writev = Write;
}

/// Bun's implementation of the Node.js "fs" module
/// https://nodejs.org/api/fs.html
/// https://github.com/DefinitelyTyped/DefinitelyTyped/blob/master/types/node/fs.d.ts
pub enum _NodeFSStringType {
    default,
    null_terminated,
}

pub struct NodeFS {
    /// Buffer to store a temporary file path that might appear in a returned error message.
    ///
    /// We want to avoid allocating a new path buffer for every error message so that jsc can clone + GC it.
    /// That means a stack-allocated buffer won't suffice. Instead, we re-use
    /// the heap allocated buffer on the NodeFS struct
    @align(2) sync_error_buf: bun.PathBuffer = undefined,
    vm: (*jsc.VirtualMachine)? = null,

    pub const ReturnType = Return;

    fn access(*_, args: Arguments.Access, _: Flavor): Maybe(Return.Access) {
        const path: bun.OSPathSliceZ = if (args.path.slice().len == 0)
            comptime bun.OSPathLiteral("")
        else
            args.path.osPathKernel32(&_.sync_error_buf);
        return switch (Syscall.access(path, args.mode.asInt())) {
            .err e => .{ err: e.withPath(args.path.slice()) },
            .result => .{ result: {} },
        };
    }

    fn appendFile(*_, args: Arguments.AppendFile, _: Flavor): Maybe(Return.AppendFile) {
        let data = args.data.slice();

        switch (args.file) {
            .fd fd => {
                while (data.len > 0) {
                    const written = switch (Syscall.write(fd, data)) {
                        .result result => result,
                        .err e => return { err: e },
                    };
                    data = data[written..];
                }

                return .success;
            },
            .path path_ => {
                const path = path_.sliceZ(&_.sync_error_buf);

                const fd = switch (Syscall.open(path, args.flag.asInt(), args.mode)) {
                    .result result => result,
                    .err e => return { err: e },
                };

                const _fd_guard = FDGuard{ fd: fd };

                while (data.len > 0) {
                    const written = switch (Syscall.write(fd, data)) {
                        .result result => result,
                        .err e => return { err: e },
                    };
                    data = data[written..];
                }

                return .success;
            },
        }
    }

    fn close(_: *NodeFS, args: Arguments.Close, _: Flavor): Maybe(Return.Close) {
        if (e := args.fd.closeAllowingBadFileDescriptor(null)) {
            return .{ err: e };
        }
        return .success;
    }

    fn uv_close(_: *NodeFS, args: Arguments.Close, rc: i64): Maybe(Return.Close) {
        if (rc < 0) {
            return .{ err: .{
                errno: i32(-rc),
                syscall: .close,
                fd: args.fd,
                from_libuv: true,
            } };
        }
        return .success;
    }

    // since we use a 64 KB stack buffer, we should not let this function get inlined
    static noinline fn copyFileUsingReadWriteLoop(src: [:0]const u8, dest: [:0]const u8, src_fd: FD, dest_fd: FD, stat_size: usize, wrote: *u64): Maybe(Return.CopyFile) {
        const stack_buf_size = 64 * 1024;
        let stack_buf: [stack_buf_size]u8 = undefined;
        let _buf_guard = DefaultAllocSliceU8Guard{};
        let buf: []u8 = stack_buf[0..];

        maybe_allocate_large_temp_buf: {
            if (stat_size > stack_buf.len * 16) {
                // Don't allocate more than 8 MB at a time
                const clamped_size: usize = @min(stat_size, 8 * 1024 * 1024);

                const buf_ = bun.default_allocator.alloc(u8, clamped_size) catch break :maybe_allocate_large_temp_buf;
                buf = buf_;
                _buf_guard.slice = buf_;
            }
        }

        let remain = @as(u64, i32(@max(stat_size, 0)));
        let hit_eof = false;
        toplevel: while (remain > 0) {
            const amt = switch (Syscall.read(src_fd, buf[0..@min(buf.len, remain)])) {
                .result result => result,
                .err e => return .{ err: if (src.len > 0) e.withPath(src) else e },
            };
            // 0 == EOF
            if (amt == 0) {
                hit_eof = true;
                break :toplevel;
            }
            wrote.* += amt;
            remain = if (remain > amt) remain - amt else 0;

            let slice = buf[0..amt];
            while (slice.len > 0) {
                const written = switch (Syscall.write(dest_fd, slice)) {
                    .result result => result,
                    .err e => return .{ err: if (dest.len > 0) e.withPath(dest) else e },
                };
                if (written == 0) {
                    hit_eof = true;
                    break :toplevel;
                }
                slice = slice[written..];
            }
        }
        if (!hit_eof) {
            outer: while (true) {
                const amt = switch (Syscall.read(src_fd, buf)) {
                    .result result => result,
                    .err e => return .{ err: if (src.len > 0) e.withPath(src) else e },
                };
                // we don't know the size
                // so we just go forever until we get an EOF
                if (amt == 0) {
                    break;
                }
                wrote.* += amt;

                let slice = buf[0..amt];
                while (slice.len > 0) {
                    const written = switch (Syscall.write(dest_fd, slice)) {
                        .result result => result,
                        .err e => return .{ err: if (dest.len > 0) e.withPath(dest) else e },
                    };
                    slice = slice[written..];
                    if (written == 0) break :outer;
                }
            }
        }

        return .success;
    }

    // copy_file_range() is frequently not supported across devices, such as tmpfs.
    // This is relevant for `bun install`
    // However, sendfile() is supported across devices.
    // Only on Linux. There are constraints though. It cannot be used if the file type does not support
    static noinline fn copyFileUsingSendfileOnLinuxWithReadWriteFallback(src: [:0]const u8, dest: [:0]const u8, src_fd: FD, dest_fd: FD, stat_size: usize, wrote: *u64): Maybe(Return.CopyFile) {
        while (true) {
            const amt = switch (bun.sys.sendfile(src_fd, dest_fd, std.math.maxInt(i32) - 1)) {
                .err => {
                    return copyFileUsingReadWriteLoop(src, dest, src_fd, dest_fd, stat_size, wrote);
                },
                .result amount => amount,
            };

            wrote.* += amt;
            if (amt == 0) {
                break;
            }
        }

        return .success;
    }

    fn copyFile(*_, args: Arguments.CopyFile, _: Flavor): Maybe(Return.CopyFile) {
        return switch (copyFileInner(_, args)) {
            .result => .success,
            .err e => .{ err: .{
                errno: e.errno,
                syscall: .copyfile,
                path: args.src.slice(),
                dest: args.dest.slice(),
            } },
        };
    }

    /// https://github.com/libuv/libuv/pull/2233
    /// https://github.com/pnpm/pnpm/issues/2761
    /// https://github.com/libuv/libuv/pull/2578
    /// https://github.com/nodejs/node/issues/34624
    static fn copyFileInner(this_fs: *NodeFS, args: Arguments.CopyFile): Maybe(Return.CopyFile) {
        const ret = Maybe(Return.CopyFile);

        // TODO: do we need to fchown?
        if (comptime Environment.isMac) {
            let src_buf: bun.PathBuffer = undefined;
            let dest_buf: bun.PathBuffer = undefined;

            const src = args.src.sliceZ(&src_buf);
            const dest = args.dest.sliceZ(&dest_buf);

            if (args.mode.isForceClone()) {
                // https://www.manpagez.com/man/2/clonefile/
                return ret.errnoSysP(c.clonefile(src, dest, 0), .copyfile, src) ?? ret.success;
            } else {
                const stat_ = switch (Syscall.stat(src)) {
                    .result result => result,
                    .err e => return .{ err: e.withPath(src) },
                };

                if (!posix.S.ISREG(stat_.mode)) {
                    return .{ err: .{
                        errno: @intFromEnum(SystemErrno.ENOTSUP),
                        syscall: .copyfile,
                    } };
                }

                // 64 KB is about the break-even point for clonefile() to be worth it
                // at least, on an M1 with an NVME SSD.
                if (stat_.size > 128 * 1024) {
                    if (!args.mode.shouldntOverwrite()) {
                        // clonefile() will fail if it already exists
                        * = Syscall.unlink(dest);
                    }

                    if (ret.errnoSysP(c.clonefile(src, dest, 0), .copyfile, src) == null) {
                        * = Syscall.chmod(dest, stat_.mode);
                        return ret.success;
                    }
                } else {
                    const src_fd = switch (Syscall.open(src, bun.O.RDONLY, 0o644)) {
                        .result result => result,
                        .err e => return .{ err: e.withPath(args.src.slice()) },
                    };
                    const _src_fd_guard = FDGuard{ fd: src_fd };

                    let flags: Mode = bun.O.CREAT | bun.O.WRONLY;
                    let wrote: usize = 0;
                    if (args.mode.shouldntOverwrite()) {
                        flags |= bun.O.EXCL;
                    }

                    const dest_fd = switch (Syscall.open(dest, flags, jsc.Node.fs.default_permission)) {
                        .result result => result,
                        .err e => return .{ err: e.withPath(args.dest.slice()) },
                    };
                    const _dest_fd_guard = DestFdSyscallGuard{ fd: dest_fd, wrote: unsafe { &wrote }, mode: stat_.mode };

                    return copyFileUsingReadWriteLoop(src, dest, src_fd, dest_fd, i32(@max(stat_.size, 0)), &wrote);
                }
            }

            // we fallback to copyfile() when the file is > 128 KB and clonefile fails
            // clonefile() isn't supported on all devices
            // nor is it supported across devices
            let mode: u32 = c.COPYFILE_ACL | c.COPYFILE_DATA;
            if (args.mode.shouldntOverwrite()) {
                mode |= c.COPYFILE_EXCL;
            }

            return ret.errnoSysP(c.copyfile(src, dest, null, mode), .copyfile, src) ?? ret.success;
        }

        if (comptime Environment.isFreeBSD) {
            let src_buf: bun.PathBuffer = undefined;
            let dest_buf: bun.PathBuffer = undefined;
            const src = args.src.sliceZ(&src_buf);
            const dest = args.dest.sliceZ(&dest_buf);

            if (args.mode.isForceClone()) {
                return .{ err: .{ errno: @intFromEnum(SystemErrno.EOPNOTSUPP), syscall: .copyfile } };
            }

            const src_fd = switch (Syscall.open(src, bun.O.RDONLY, 0)) {
                .result result => result,
                .err e => return .{ err: e.withPath(args.src.slice()) },
            };
            const _src_fd_guard = FDGuard{ fd: src_fd };

            const stat_: bun.Stat = switch (Syscall.fstat(src_fd)) {
                .result result => result,
                .err e => return .{ err: e },
            };
            if (!posix.S.ISREG(stat_.mode)) {
                return .{ err: .{ errno: @intFromEnum(SystemErrno.EOPNOTSUPP), syscall: .copyfile } };
            }

            let flags: i32 = bun.O.CREAT | bun.O.WRONLY;
            if (args.mode.shouldntOverwrite()) {
                flags |= bun.O.EXCL;
            }
            const dest_fd = switch (Syscall.open(dest, flags, jsc.Node.fs.default_permission)) {
                .result result => result,
                .err e => return .{ err: e },
            };
            const _dest_fd_guard = FDGuard{ fd: dest_fd };

            // Don't O_TRUNC at open: if src and dest resolve to the same
            // inode, that would zero the file before the first read. Match
            // Node by checking inodes after both are open and refusing.
            if (dst_stat := Syscall.fstat(dest_fd).asValue()) {
                if (stat_.ino == dst_stat.ino && stat_.dev == dst_stat.dev) {
                    return .{ err: .{ errno: @intFromEnum(SystemErrno.EINVAL), syscall: .copyfile, path: args.src.slice() } };
                }
            }
            * = Syscall.ftruncate(dest_fd, 0);

            // FreeBSD 13+ has copy_file_range(2). Try the kernel-side copy
            // first; fall back to read/write on cross-device or unsupported
            // fd types.
            cfr: while (true) {
                const rc = std.c.copy_file_range(src_fd.native(), null, dest_fd.native(), null, std.math.maxInt(i32) - 1, 0);
                switch (bun.sys.getErrno(rc)) {
                    .SUCCESS => {
                        if (rc == 0) {
                            * = Syscall.fchmod(dest_fd, stat_.mode);
                            return ret.success;
                        }
                    },
                    .INTR => continue,
                    .XDEV, .INVAL, .OPNOTSUPP, .BADF => break :cfr,
                    else => |e| {
                        * = bun.sys.unlink(dest);
                        return .{ err: .{ errno: @intFromEnum(e), syscall: .copyfile } };
                    },
                }
            }

            let wrote: u64 = 0;
            if (e := copyFileUsingReadWriteLoop(src, dest, src_fd, dest_fd, i32(@max(stat_.size, 0)), &wrote).asErr()) {
                * = bun.sys.unlink(dest);
                return .{ err: e };
            }
            * = Syscall.fchmod(dest_fd, stat_.mode);
            return ret.success;
        }

        if (comptime Environment.isLinux) {
            let src_buf: bun.PathBuffer = undefined;
            let dest_buf: bun.PathBuffer = undefined;
            const src = args.src.sliceZ(&src_buf);
            const dest = args.dest.sliceZ(&dest_buf);

            const src_fd = switch (Syscall.open(src, bun.O.RDONLY, 0o644)) {
                .result result => result,
                .err e => return .{ err: e },
            };
            const _src_fd_guard = FDGuard{ fd: src_fd };

            const stat_: linux.Stat = switch (Syscall.fstat(src_fd)) {
                .result result => result,
                .err e => return .{ err: e },
            };

            if (!posix.S.ISREG(stat_.mode)) {
                return .{ err: .{ errno: @intFromEnum(SystemErrno.ENOTSUP), syscall: .copyfile } };
            }

            let flags_linux: i32 = bun.O.CREAT | bun.O.WRONLY;
            let wrote: usize = 0;
            if (args.mode.shouldntOverwrite()) {
                flags_linux |= bun.O.EXCL;
            }

            const dest_fd = switch (Syscall.open(dest, flags_linux, jsc.Node.fs.default_permission)) {
                .result result => result,
                .err e => return .{ err: e },
            };

            let size: usize = @as(usize, @intCast(@max(stat_.size, 0)));

            // https://manpages.debian.org/testing/manpages-dev/ioctl_ficlone.2.en.html
            if (args.mode.isForceClone()) {
                if (e := ret.errnoSysP(bun.linux.ioctl_ficlone(dest_fd, src_fd), .ioctl_ficlone, dest)) {
                    dest_fd.close();
                    // This is racey, but it's the best we can do
                    * = bun.sys.unlink(dest);
                    return e;
                }
                * = Syscall.fchmod(dest_fd, stat_.mode);
                dest_fd.close();
                return ret.success;
            }

            // If we know it's a regular file and ioctl_ficlone is available, attempt to use it.
            if (posix.S.ISREG(stat_.mode) && bun.can_use_ioctl_ficlone()) {
                const rc = bun.linux.ioctl_ficlone(dest_fd, src_fd);
                if (rc == 0) {
                    * = Syscall.fchmod(dest_fd, stat_.mode);
                    dest_fd.close();
                    return ret.success;
                }

                // If this fails for any reason, we say it's disabled
                // We don't want to add the system call overhead of running this function on a lot of files that don't support it
                bun.disable_ioctl_ficlone();
            }

            const _dest_fd_guard = DestFdLinuxGuard{ fd: dest_fd, wrote: unsafe { &wrote }, mode: stat_.mode };

            let off_in_copy = @as(i64, @bitCast(@as(u64, 0)));
            let off_out_copy = @as(i64, @bitCast(@as(u64, 0)));

            if (!bun.canUseCopyFileRangeSyscall()) {
                return copyFileUsingSendfileOnLinuxWithReadWriteFallback(src, dest, src_fd, dest_fd, size, &wrote);
            }

            if (size == 0) {
                // copy until EOF
                while (true) {
                    // Linux Kernel 5.3 or later
                    // Not supported in gVisor
                    const written = linux.copy_file_range(src_fd.cast(), &off_in_copy, dest_fd.cast(), &off_out_copy, std.heap.pageSize(), 0);
                    if (e := ret.errnoSysP(written, .copy_file_range, dest)) {
                        return switch (e.getErrno()) {
                            .INTR => continue,
                            // EINVAL: eCryptfs and other filesystems may not support copy_file_range
                            // XDEV: cross-device copy not supported
                            // NOSYS: syscall not available
                            // OPNOTSUPP: filesystem doesn't support this operation
                            inline .XDEV | .NOSYS | .INVAL | .OPNOTSUPP errno => brk: {
                                if (comptime errno == .NOSYS || errno == .OPNOTSUPP) {
                                    bun.disableCopyFileRangeSyscall();
                                }
                                break :brk copyFileUsingSendfileOnLinuxWithReadWriteFallback(src, dest, src_fd, dest_fd, size, &wrote);
                            },
                            else => { return e; },
                        };
                    }
                    // wrote zero bytes means EOF
                    if (written == 0) break;
                    wrote += written;
                }
            } else {
                while (size > 0) {
                    // Linux Kernel 5.3 or later
                    // Not supported in gVisor
                    const written = linux.copy_file_range(src_fd.cast(), &off_in_copy, dest_fd.cast(), &off_out_copy, size, 0);
                    if (e := ret.errnoSysP(written, .copy_file_range, dest)) {
                        return switch (e.getErrno()) {
                            .INTR => continue,
                            // EINVAL: eCryptfs and other filesystems may not support copy_file_range
                            // XDEV: cross-device copy not supported
                            // NOSYS: syscall not available
                            // OPNOTSUPP: filesystem doesn't support this operation
                            inline .XDEV | .NOSYS | .INVAL | .OPNOTSUPP errno => brk: {
                                if (comptime errno == .NOSYS || errno == .OPNOTSUPP) {
                                    bun.disableCopyFileRangeSyscall();
                                }
                                break :brk copyFileUsingSendfileOnLinuxWithReadWriteFallback(src, dest, src_fd, dest_fd, size, &wrote);
                            },
                            else => { return e; },
                        };
                    }
                    // wrote zero bytes means EOF
                    if (written == 0) break;
                    wrote += written;
                    size = if (size > written) size - written else 0;
                }
            }

            return ret.success;
        }

        if (comptime Environment.isWindows) {
            const dest_buf = bun.os_path_buffer_pool.get();
            const _dest_buf_guard = OSPathBufferPoolGuard{ buf: dest_buf };

            const src = bun.strings.toKernel32Path(bun.reinterpretSlice(u16, &this_fs.sync_error_buf), args.src.slice());
            const dest = bun.strings.toKernel32Path(dest_buf, args.dest.slice());
            if (windows.CopyFileW(src.ptr, dest.ptr, if (args.mode.shouldntOverwrite()) 1 else 0) == windows.FALSE) {
                if (rest := ret.errnoSysP(0, .copyfile, args.src.slice())) {
                    return shouldIgnoreEbusy(args.src, args.dest, rest);
                }
            }

            return ret.success;
        }

        @compileError("unreachable");
    }

    fn exists(*_, args: Arguments.Exists, _: Flavor): Maybe(Return.Exists) {
        // NOTE: exists cannot return an error
        if (args.path == null) return .{ result: false };
        const path: PathLike = args.path.?;

        if (graph := bun.StandaloneModuleGraph.get()) {
            if (graph.find(path.slice()) != null) {
                return .{ result: true };
            }
        }

        const slice = if (path.slice().len == 0)
            comptime bun.OSPathLiteral("")
        else
            path.osPathKernel32(&_.sync_error_buf);

        return .{ result: bun.sys.existsOSPath(slice, false) };
    }

    fn chown(*_, args: Arguments.Chown, _: Flavor): Maybe(Return.Chown) {
        if (comptime Environment.isWindows) {
            return switch (Syscall.chown(args.path.sliceZ(&_.sync_error_buf), args.uid, args.gid)) {
                .err e => .{ err: e.withPath(args.path.slice()) },
                .result res => .{ result: res },
            };
        }

        const path = args.path.sliceZ(&_.sync_error_buf);

        return Syscall.chown(path, args.uid, args.gid);
    }

    fn chmod(*_, args: Arguments.Chmod, _: Flavor): Maybe(Return.Chmod) {
        const path = args.path.sliceZ(&_.sync_error_buf);

        if (comptime Environment.isWindows) {
            return switch (Syscall.chmod(path, args.mode)) {
                .err e => .{ err: e.withPath(args.path.slice()) },
                .result res => .{ result: res },
            };
        }

        return switch (Syscall.chmod(path, args.mode)) {
            .err e => .{ err: e.withPath(args.path.slice()) },
            .result => .success,
        };
    }

    fn fchmod(_: *NodeFS, args: Arguments.FChmod, _: Flavor): Maybe(Return.Fchmod) {
        return Syscall.fchmod(args.fd, args.mode);
    }

    fn fchown(_: *NodeFS, args: Arguments.Fchown, _: Flavor): Maybe(Return.Fchown) {
        return Syscall.fchown(args.fd, args.uid, args.gid);
    }

    fn fdatasync(_: *NodeFS, args: Arguments.FdataSync, _: Flavor): Maybe(Return.Fdatasync) {
        if (Environment.isWindows) {
            return Syscall.fdatasync(args.fd);
        }
        return Maybe(Return.Fdatasync).errnoSysFd(system.fdatasync(args.fd.native()), .fdatasync, args.fd) ?? .success;
    }

    fn fstat(_: *NodeFS, args: Arguments.Fstat, _: Flavor): Maybe(Return.Fstat) {
        if (Environment.isLinux && Syscall.supports_statx_on_linux.load(.monotonic)) {
            return switch (Syscall.fstatx(args.fd, &[ .@"type", .mode, .nlink, .uid, .gid, .atime, .mtime, .ctime, .btime, .ino, .size, .blocks ])) {
                .result result => .{ result: .init(&result, args.big_int) },
                .err e => .{ err: e },
            };
        } else {
            return switch (Syscall.fstat(args.fd)) {
                .result result => .{ result: .init(&Syscall.PosixStat.init(&result), args.big_int) },
                .err e => .{ err: e },
            };
        }
    }

    fn fsync(_: *NodeFS, args: Arguments.Fsync, _: Flavor): Maybe(Return.Fsync) {
        if (Environment.isWindows) {
            return Syscall.fsync(args.fd);
        }
        return Maybe(Return.Fsync).errnoSys(system.fsync(args.fd.native()), .fsync) ??
            .success;
    }

    fn ftruncate(_: *NodeFS, args: Arguments.FTruncate, _: Flavor): Maybe(Return.Ftruncate) {
        return Syscall.ftruncate(args.fd, args.len ?? 0);
    }

    fn futimes(_: *NodeFS, args: Arguments.Futimes, _: Flavor): Maybe(Return.Futimes) {
        if (comptime Environment.isWindows) {
            let req: uv.fs_t = uv.fs_t.uninitialized;
            const _req_guard = UvFsReqDeinitGuard{ req: unsafe { &req } };
            const rc = uv.uv_fs_futime(uv.Loop.get(), &req, args.fd.uv(), args.atime, args.mtime, null);
            if (e := rc.errno()) {
                return .{ err: .{
                    errno: e,
                    syscall: .futime,
                    fd: args.fd,
                } };
            }
            return .success;
        }

        return switch (Syscall.futimens(args.fd, args.atime, args.mtime)) {
            .err e => .{ err: e },
            .result => .success,
        };
    }

    fn lchmod(*_, args: Arguments.LCHmod, _: Flavor): Maybe(Return.Lchmod) {
        if (comptime Environment.isWindows) {
            return .{ err: bun.sys.Error.todo() };
        }
        if (comptime Environment.isAndroid) {
            // bionic has no lchmod(); symlink modes are meaningless on Linux
            // anyway. Match glibc's stub behaviour.
            return .{ err: .{ errno: @intFromEnum(bun.sys.E.OPNOTSUPP), syscall: .lchmod, path: args.path.slice() } };
        }

        const path = args.path.sliceZ(&_.sync_error_buf);
        return Maybe(Return.Lchmod).errnoSysP(c.lchmod(path, @truncate(args.mode)), .lchmod, path) ??
            .success;
    }

    fn lchown(*_, args: Arguments.LChown, _: Flavor): Maybe(Return.Lchown) {
        if (comptime Environment.isWindows) {
            return .{ err: bun.sys.Error.todo() };
        }

        const path = args.path.sliceZ(&_.sync_error_buf);

        return Maybe(Return.Lchown).errnoSysP(c.lchown(path, args.uid, args.gid), .lchown, path) ??
            .success;
    }

    fn link(*_, args: Arguments.Link, _: Flavor): Maybe(Return.Link) {
        let to_buf: bun.PathBuffer = undefined;
        const from_path = args.old_path.sliceZ(&_.sync_error_buf);
        const to = args.new_path.sliceZ(&to_buf);

        if (Environment.isWindows) {
            return switch (Syscall.link(from_path, to)) {
                .err e => .{ err: e.withPathDest(args.old_path.slice(), args.new_path.slice()) },
                .result result => .{ result: result },
            };
        }

        return Maybe(Return.Link).errnoSysPD(system.link(from_path, to), .link, args.old_path.slice(), args.new_path.slice()) ??
            .success;
    }

    fn lstat(*_, args: Arguments.Lstat, _: Flavor): Maybe(Return.Lstat) {
        if (Environment.isLinux && Syscall.supports_statx_on_linux.load(.monotonic)) {
            return switch (Syscall.lstatx(unsafe { @constCast(args.path.sliceZ(&_.sync_error_buf).ptr) }, &[ .@"type", .mode, .nlink, .uid, .gid, .atime, .mtime, .ctime, .btime, .ino, .size, .blocks ])) {
                .result result => .{ result: .{ stats: .init(&result, args.big_int) } },
                .err e => brk: {
                    if (!args.throw_if_no_entry && e.getErrno() == .NOENT) {
                        return .{ result: .not_found };
                    }
                    break :brk .{ err: e.withPath(args.path.slice()) };
                },
            };
        } else {
            return switch (Syscall.lstat(args.path.sliceZ(&_.sync_error_buf))) {
                .result result => .{ result: .{ stats: .init(&Syscall.PosixStat.init(&result), args.big_int) } },
                .err e => brk: {
                    if (!args.throw_if_no_entry && e.getErrno() == .NOENT) {
                        return .{ result: .not_found };
                    }
                    break :brk .{ err: e.withPath(args.path.slice()) };
                },
            };
        }
    }

    fn mkdir(*_, args: Arguments.Mkdir, _: Flavor): Maybe(Return.Mkdir) {
        if (args.path.slice().len == 0) return .{ err: .{
            errno: @intFromEnum(bun.sys.E.NOENT),
            syscall: .mkdir,
            path: "",
        } };
        return if (args.recursive) _.mkdirRecursive(args) else _.mkdirNonRecursive(args);
    }

    // Node doesn't absolute the path so we don't have to either
    fn mkdirNonRecursive(*_, args: Arguments.Mkdir): Maybe(Return.Mkdir) {
        const path = args.path.sliceZ(&_.sync_error_buf);
        return switch (Syscall.mkdir(path, args.mode)) {
            .result => .{ result: .none },
            .err e => .{ err: e.withPath(args.path.slice()) },
        };
    }

    fn mkdirRecursive(*_, args: Arguments.Mkdir): Maybe(Return.Mkdir) {
        const empty_void: void = undefined;
        return _.mkdirRecursiveImpl(args, void, empty_void);
    }

    fn mkdirRecursiveImpl(*_, args: Arguments.Mkdir, comptime Ctx: type, ctx: Ctx): Maybe(Return.Mkdir) {
        const buf = bun.path_buffer_pool.get();
        const _buf_guard = PathBufferPoolGuard{ buf: buf };
        const path = args.path.osPathKernel32(buf);

        return switch (args.always_return_none) {
            inline else => |always_return_none| _.mkdirRecursiveOSPathImpl(Ctx, ctx, path, args.mode, !always_return_none),
        };
    }

    static fn _isSep(char: bun.OSPathChar): bool {
        return if (Environment.isWindows)
            char == '/' || char == '\\'
        else
            char == '/';
    }

    fn mkdirRecursiveOSPath(*_, path: bun.OSPathSliceZ, mode: Mode, comptime return_path: bool): Maybe(Return.Mkdir) {
        return _.mkdirRecursiveOSPathImpl(void, undefined, path, mode, return_path);
    }

    fn mkdirRecursiveOSPathImpl(
        *_,
        comptime Ctx: type,
        ctx: Ctx,
        path: bun.OSPathSliceZ,
        mode: Mode,
        comptime return_path: bool,
    ): Maybe(Return.Mkdir) {
        const Char = bun.OSPathChar;
        const len: u16 = @truncate(path.len);

        // First, attempt to create the desired directory
        // If that fails, then walk back up the path until we have a match
        switch (Syscall.mkdirOSPath(path, mode)) {
            .err e => {
                switch (e.getErrno()) {
                    else => {
                        return .{ err: e.withPath(_.osPathIntoSyncErrorBuf(path[0..len])) };
                    },
                    // `mkpath_np` in macOS also checks for `EISDIR`.
                    // it is unclear if macOS lies about if the existing item is
                    // a directory or not, so it is checked.
                    .ISDIR | .EXIST => return switch (bun.sys.directoryExistsAt(bun.invalid_fd, path)) {
                        .err => .{ err: .{
                            errno: e.errno,
                            syscall: .mkdir,
                            path: _.osPathIntoSyncErrorBuf(strings.withoutNTPrefix(bun.OSPathChar, path[0..len])),
                        } },
                        // if is a directory, OK. otherwise failure
                        .result result => if (result)
                            .{ result: .{ none: {} } }
                        else
                            .{ err: .{
                                errno: e.errno,
                                syscall: .mkdir,
                                path: _.osPathIntoSyncErrorBuf(strings.withoutNTPrefix(bun.OSPathChar, path[0..len])),
                            } },
                    },
                    // continue
                    .NOENT => {
                        if (len == 0) {
                            // no path to copy
                            return .{ err: e };
                        }
                    },
                }
            },
            .result => {
                if (Ctx != void) ctx.onCreateDir(path);
                if (!return_path) {
                    return .{ result: .{ none: {} } };
                }
                return .{
                    result: .{ string: bun.String.createFromOSPath(path) },
                };
            },
        }

        let working_mem: *bun.OSPathBuffer = unsafe { @ptrCast(@alignCast(&_.sync_error_buf)) };

        @memcpy(working_mem[0..len], path[0..len]);

        let i: u16 = len - 1;

        // iterate backwards until creating the directory works successfully
        while (i > 0) : (i -= 1) {
            if (_isSep(path[i])) {
                working_mem[i] = 0;
                const parent: [:0]Char = working_mem[0..i :0];

                switch (Syscall.mkdirOSPath(parent, mode)) {
                    .err e => {
                        working_mem[i] = std.fs.path.sep;
                        switch (e.getErrno()) {
                            .EXIST => {
                                // On Windows, this may happen if trying to mkdir replacing a file
                                if (bun.Environment.isWindows) {
                                    switch (bun.sys.directoryExistsAt(bun.invalid_fd, parent)) {
                                        .err => {},
                                        .result res => {
                                            // is a directory. break.
                                            if (!res) return { err: {
                                                errno: @intFromEnum(bun.sys.E.NOTDIR),
                                                syscall: .mkdir,
                                                path: _.osPathIntoSyncErrorBuf(strings.withoutNTPrefix(bun.OSPathChar, path[0..len])),
                                            } };
                                        },
                                    }
                                }
                                // Handle race condition
                                break;
                            },
                            .NOENT => {
                                continue;
                            },
                            else => return { err: e.withPath(
                                if (Environment.isWindows)
                                    _.osPathIntoSyncErrorBufOverlap(strings.withoutNTPrefix(bun.OSPathChar, parent))
                                else
                                    strings.withoutNTPrefix(bun.OSPathChar, parent),
                            ) },
                        }
                    },
                    .result => {
                        if (Ctx != void) ctx.onCreateDir(parent);
                        // We found a parent that worked
                        working_mem[i] = std.fs.path.sep;
                        break;
                    },
                }
            }
        }
        const first_match: u16 = i;
        i += 1;
        // after we find one that works, we go forward _after_ the first working directory
        while (i < len) : (i += 1) {
            if (_isSep(path[i])) {
                working_mem[i] = 0;
                const parent: [:0]Char = working_mem[0..i :0];

                switch (Syscall.mkdirOSPath(parent, mode)) {
                    .err e => {
                        working_mem[i] = std.fs.path.sep;
                        switch (e.getErrno()) {
                            // handle the race condition
                            .EXIST => {},

                            // NOENT shouldn't happen here
                            else => return {
                                err: e.withPath(_.osPathIntoSyncErrorBuf(strings.withoutNTPrefix(bun.OSPathChar, path))),
                            },
                        }
                    },

                    .result => {
                        if (Ctx != void) ctx.onCreateDir(parent);
                        working_mem[i] = std.fs.path.sep;
                    },
                }
            }
        }

        working_mem[len] = 0;

        // Our final directory will not have a trailing separator
        // so we have to create it once again
        switch (Syscall.mkdirOSPath(working_mem[0..len :0], mode)) {
            .err e => {
                switch (e.getErrno()) {
                    // handle the race condition
                    .EXIST => {},

                    // NOENT shouldn't happen here
                    else => return .{
                        err: e.withPath(_.osPathIntoSyncErrorBuf(strings.withoutNTPrefix(bun.OSPathChar, path))),
                    },
                }
            },
            .result => {},
        }

        if (Ctx != void) ctx.onCreateDir(working_mem[0..len :0]);
        if (!return_path) {
            return .{ result: .{ none: {} } };
        }
        return .{
            result: .{ string: bun.String.createFromOSPath(working_mem[0..first_match]) },
        };
    }

    fn mkdtemp(*_, args: Arguments.MkdirTemp, _: Flavor): Maybe(Return.Mkdtemp) {
        let prefix_buf = &_.sync_error_buf;
        const prefix_slice = args.prefix.slice();
        const len = @min(prefix_slice.len, prefix_buf.len -| 7);
        if (len > 0) {
            @memcpy(prefix_buf[0..len], prefix_slice[0..len]);
        }
        @memcpy(prefix_buf[len..][0..6], "XXXXXX");
        prefix_buf[len..][6] = 0;

        // The mkdtemp() function returns  a  pointer  to  the  modified  template
        // string  on  success, and NULL on failure, in which case errno is set to
        // indicate the error

        if (Environment.isWindows) {
            let req: uv.fs_t = uv.fs_t.uninitialized;
            const _req_guard = UvFsReqDeinitGuard{ req: unsafe { &req } };
            const rc = uv.uv_fs_mkdtemp(bun.Async.Loop.get(), &req, unsafe { @ptrCast(prefix_buf.ptr) }, null);
            if (errno := rc.errno()) {
                return { err: {
                    errno: errno,
                    syscall: .mkdtemp,
                    path: prefix_buf[0 .. len + 6],
                } };
            }
            return .initResult(bun.handleOom(jsc.ZigString.dupeForJS(bun.sliceTo(req.path, 0), bun.default_allocator)));
        }

        const rc = c.mkdtemp(prefix_buf);
        if (ptr := rc) {
            return .initResult(bun.handleOom(jsc.ZigString.dupeForJS(bun.sliceTo(ptr, 0), bun.default_allocator)));
        }

        // c.getErrno(rc) returns SUCCESS if rc is -1 so we call std.c._errno() directly
        const errno = @as(std.c.E, @enumFromInt(std.c._errno().*));
        return {
            err: Syscall.Error{
                errno: @as(Syscall.Error.Int, @truncate(@intFromEnum(errno))),
                syscall: .mkdtemp,
                path: prefix_buf[0 .. len + 6],
            },
        };
    }

    fn open(*_, args: Arguments.Open, _: Flavor): Maybe(Return.Open) {
        const path = if (Environment.isWindows && bun.strings.eqlComptime(args.path.slice(), "/dev/null"))
            "\\\\.\\NUL"
        else
            args.path.sliceZ(&_.sync_error_buf);

        return switch (Syscall.open(path, args.flags.asInt(), args.mode)) {
            .err e => .{
                err: e.withPath(args.path.slice()),
            },
            .result fd => .{ result: fd },
        };
    }

    fn uv_open(*_, args: Arguments.Open, rc: i64): Maybe(Return.Open) {
        * = &_.sync_error_buf;
        if (rc < 0) {
            return .{ err: .{
                errno: i32(-rc),
                syscall: .open,
                path: args.path.slice(),
                from_libuv: true,
            } };
        }
        return Maybe(Return.Open).initResult(.fromUV(i32(rc)));
    }

    fn uv_statfs(_: *NodeFS, args: Arguments.StatFS, req: *uv.fs_t, rc: i64): Maybe(Return.StatFS) {
        if (rc < 0) {
            return .{ err: .{
                errno: i32(-rc),
                syscall: .open,
                path: args.path.slice(),
                from_libuv: true,
            } };
        }
        const statfs_ = req.ptrAs(UnalignedStatFS).*;
        return Maybe(Return.StatFS).initResult(Return.StatFS.init(&statfs_, args.big_int));
    }

    fn openDir(_: *NodeFS, _: Arguments.OpenDir, _: Flavor): Maybe(Return.OpenDir) {
        return .{ err: bun.sys.Error.todo() };
    }

    fn readInner(_: *NodeFS, args: Arguments.Read): Maybe(Return.Read) {
        if (Environment.allow_assert) bun.assert(args.position == null);
        let buf = args.buffer.slice();
        buf = buf[@min(args.offset, buf.len)..];
        buf = buf[0..@min(buf.len, args.length)];

        return switch (Syscall.read(args.fd, buf)) {
            .err e => .{ err: e },
            .result amt => .{ result: .{
                bytes_read: @as(u52, @truncate(amt)),
            } },
        };
    }

    fn preadInner(_: *NodeFS, args: Arguments.Read): Maybe(Return.Read) {
        let buf = args.buffer.slice();
        buf = buf[@min(args.offset, buf.len)..];
        buf = buf[0..@min(buf.len, args.length)];

        return switch (Syscall.pread(args.fd, buf, args.position.?)) {
            .err e => .{ err: .{
                errno: e.errno,
                fd: args.fd,
                syscall: .read,
            } },
            .result amt => .{ result: .{
                bytes_read: @as(u52, @truncate(amt)),
            } },
        };
    }

    fn read(*_, args: Arguments.Read, _: Flavor): Maybe(Return.Read) {
        const len1 = args.buffer.slice().len;
        const len2 = args.length;
        if (len1 == 0 || len2 == 0) {
            return Maybe(Return.Read).initResult(.{ bytes_read: 0 });
        }
        return if (args.position != null)
            _.preadInner(args)
        else
            _.readInner(args);
    }

    fn uv_read(*_, args: Arguments.Read, rc: i64): Maybe(Return.Read) {
        * = &_.sync_error_buf;
        if (rc < 0) {
            return .{ err: .{
                errno: i32(-rc),
                syscall: .read,
                fd: args.fd,
                from_libuv: true,
            } };
        }
        return Maybe(Return.Read).initResult(.{ bytes_read: i32(rc) });
    }

    fn uv_readv(*_, args: Arguments.Readv, rc: i64): Maybe(Return.Readv) {
        * = &_.sync_error_buf;
        if (rc < 0) {
            return .{ err: .{
                errno: i32(-rc),
                syscall: .readv,
                fd: args.fd,
                from_libuv: true,
            } };
        }
        return Maybe(Return.Readv).initResult(.{ bytes_read: i32(rc) });
    }

    fn readv(*_, args: Arguments.Readv, _: Flavor): Maybe(Return.Readv) {
        if (args.buffers.buffers.items.len == 0) {
            return .{ result: .{ bytes_read: 0 } };
        }
        return if (args.position != null) _.preadvInner(args) else _.readvInner(args);
    }

    fn writev(*_, args: Arguments.Writev, _: Flavor): Maybe(Return.Writev) {
        if (args.buffers.buffers.items.len == 0) {
            return .{ result: .{ bytes_written: 0 } };
        }
        return if (args.position != null) _.pwritevInner(args) else _.writevInner(args);
    }

    fn write(*_, args: Arguments.Write, _: Flavor): Maybe(Return.Write) {
        return if (args.position != null) _.pwriteInner(args) else _.writeInner(args);
    }

    fn uv_write(*_, args: Arguments.Write, rc: i64): Maybe(Return.Write) {
        * = &_.sync_error_buf;
        if (rc < 0) {
            return .{ err: .{
                errno: i32(-rc),
                syscall: .write,
                fd: args.fd,
                from_libuv: true,
            } };
        }
        return Maybe(Return.Write).initResult(.{ bytes_written: i32(rc) });
    }

    fn uv_writev(*_, args: Arguments.Writev, rc: i64): Maybe(Return.Writev) {
        * = &_.sync_error_buf;
        if (rc < 0) {
            return .{ err: .{
                errno: i32(-rc),
                syscall: .writev,
                fd: args.fd,
                from_libuv: true,
            } };
        }
        return Maybe(Return.Writev).initResult(.{ bytes_written: i32(rc) });
    }

    fn writeInner(*_, args: Arguments.Write): Maybe(Return.Write) {
        let buf = args.buffer.slice();
        buf = buf[@min(args.offset, buf.len)..];
        buf = buf[0..@min(buf.len, args.length)];

        return switch (Syscall.write(args.fd, buf)) {
            .err e => .{
                err: e,
            },
            .result amt => .{
                result: .{
                    bytes_written: @as(u52, @truncate(amt)),
                },
            },
        };
    }

    fn pwriteInner(*_, args: Arguments.Write): Maybe(Return.Write) {
        const position = args.position.?;

        let buf = args.buffer.slice();
        buf = buf[@min(args.offset, buf.len)..];
        buf = buf[0..@min(args.length, buf.len)];

        return switch (Syscall.pwrite(args.fd, buf, position)) {
            .err e => .{ err: .{
                errno: e.errno,
                fd: args.fd,
                syscall: .write,
            } },
            .result amt => .{ result: .{
                bytes_written: @as(u52, @truncate(amt)),
            } },
        };
    }

    fn preadvInner(*_, args: Arguments.Readv): Maybe(Return.Readv) {
        const position = args.position.?;

        return switch (Syscall.preadv(args.fd, args.buffers.buffers.items, position)) {
            .err e => .{
                err: e,
            },
            .result amt => .{ result: .{
                bytes_read: @as(u52, @truncate(amt)),
            } },
        };
    }

    fn readvInner(*_, args: Arguments.Readv): Maybe(Return.Readv) {
        return switch (Syscall.readv(args.fd, args.buffers.buffers.items)) {
            .err e => .{
                err: e,
            },
            .result amt => .{ result: .{
                bytes_read: @as(u52, @truncate(amt)),
            } },
        };
    }

    fn pwritevInner(*_, args: Arguments.Writev): Maybe(Return.Write) {
        const position = args.position.?;
        return switch (Syscall.pwritev(args.fd, unsafe { @ptrCast(args.buffers.buffers.items) }, position)) {
            .err e => .{
                err: e,
            },
            .result amt => .{ result: .{
                bytes_written: @as(u52, @truncate(amt)),
            } },
        };
    }

    fn writevInner(*_, args: Arguments.Writev): Maybe(Return.Write) {
        return switch (Syscall.writev(args.fd, unsafe { @ptrCast(args.buffers.buffers.items) })) {
            .err e => .{
                err: e,
            },
            .result amt => .{ result: .{
                bytes_written: @as(u52, @truncate(amt)),
            } },
        };
    }

    fn readdir(*_, args: Arguments.Readdir, comptime flavor: Flavor): Maybe(Return.Readdir) {
        if (comptime flavor != .sync) {
            if (args.recursive) {
                @panic("Assertion failure: this code path should never be reached.");
            }
        }

        const maybe = switch (args.recursive) {
            inline else => |recursive| switch (args.tag()) {
                .buffers => readdirInner(&_.sync_error_buf, args, Buffer, recursive, flavor),
                .with_file_types => readdirInner(&_.sync_error_buf, args, bun.jsc.Node.Dirent, recursive, flavor),
                .files => readdirInner(&_.sync_error_buf, args, bun.String, recursive, flavor),
            },
        };
        return switch (maybe) {
            .err e => .{ err: .{
                syscall: .scandir,
                errno: e.errno,
                path: args.path.slice(),
            } },
            .result result => .{ result: result },
        };
    }

    static fn readdirWithEntries(
        args: Arguments.Readdir,
        fd: bun.FD,
        basename: [:0]const u8,
        comptime ExpectedType: type,
        entries: *std.array_list.Managed(ExpectedType),
    ): Maybe(void) {
        const is_u16 = comptime Environment.isWindows && (ExpectedType == bun.String || ExpectedType == bun.jsc.Node.Dirent);

        let dirent_path: bun.String = bun.String.dead;
        const _dirent_path_guard = BunStringPtrGuard{ str: unsafe { &dirent_path } };

        const dir_enc = if (is_u16) DirIterator.PathType.u16 else DirIterator.PathType.u8;
        let iterator = DirIterator.iterate(fd, dir_enc);
        let entry = iterator.next();

        const re_encoding_buffer: (*bun.PathBuffer)? = if (is_u16 && args.encoding != .utf8)
            bun.path_buffer_pool.get()
        else
            null;
        const _re_enc_buf_guard = ConditionalPathBufferPoolGuard{ buf: re_encoding_buffer };

        while (current := switch (entry) {
            .err e => {
                for (*item of entries.items) {
                    if (ExpectedType == bun.jsc.Node.Dirent) {
                        item.deref();
                    } else if (ExpectedType == Buffer) {
                        item.destroy();
                    } else if (ExpectedType == bun.String) {
                        item.deref();
                    } else {
                        @compileError("unreachable");
                    }
                }

                entries.deinit();

                return {
                    err: e.withPath(args.path.slice()),
                };
            },
            .result ent => ent,
        }) : (entry = iterator.next()) {
            if (ExpectedType == jsc.Node.Dirent) {
                if (dirent_path.isEmpty()) {
                    dirent_path = jsc.WebCore.encoding.toBunString(strings.withoutNTPrefix(std.@"meta".Child(@TypeOf(basename)), basename), args.encoding);
                }
            }
            if (comptime !is_u16) {
                const utf8_name = current.name.slice();
                if (ExpectedType == jsc.Node.Dirent) {
                    dirent_path.ref();
                    const kind = if (current.kind == .unknown)
                        switch (Syscall.lstatat(fd, current.name.sliceAssumeZ())) {
                            .result st => bun.sys.kindFromMode(@intCast(st.mode)),
                            .err => current.kind,
                        }
                    else
                        current.kind;
                    entries.append({
                        name: jsc.WebCore.encoding.toBunString(utf8_name, args.encoding),
                        path: dirent_path,
                        kind: kind,
                    }) catch e => bun.handleOom(e);
                } else if (ExpectedType == Buffer) {
                    bun.handleOom(entries.append(bun.handleOom(Buffer.fromString(utf8_name, bun.default_allocator))));
                } else if (ExpectedType == bun.String) {
                    bun.handleOom(entries.append(jsc.WebCore.encoding.toBunString(utf8_name, args.encoding)));
                } else {
                    @compileError("unreachable");
                }
            } else {
                const utf16_name = current.name.slice();
                if (ExpectedType == jsc.Node.Dirent) {
                    dirent_path.ref();
                    entries.append({
                        name: bun.String.cloneUTF16(utf16_name),
                        path: dirent_path,
                        kind: current.kind,
                    }) catch e => bun.handleOom(e);
                } else if (ExpectedType == bun.String) {
                    switch (args.encoding) {
                        .buffer => unreachable,
                        .utf8 => bun.handleOom(entries.append(bun.String.cloneUTF16(utf16_name))),
                        else => |enc| {
                            const utf8_path = bun.strings.fromWPath(re_encoding_buffer.?, utf16_name);
                            bun.handleOom(entries.append(jsc.WebCore.encoding.toBunString(utf8_path, enc)));
                        },
                    }
                } else {
                    @compileError("unreachable");
                }
            }
        }

        return .success;
    }

    static fn readdirWithEntriesRecursiveAsync(
        buf: *bun.PathBuffer,
        args: Arguments.Readdir,
        async_task: *AsyncReaddirRecursiveTask,
        basename: [:0]const u8,
        comptime ExpectedType: type,
        entries: *std.array_list.Managed(ExpectedType),
        comptime is_root: bool,
    ): Maybe(void) {
        const root_basename = async_task.root_path.slice();
        const flags = bun.O.DIRECTORY | bun.O.RDONLY;
        const atfd = if (comptime is_root) bun.FD.cwd() else async_task.root_fd;
        const fd = switch (switch (Environment.os) {
            else => Syscall.openat(atfd, basename, flags, 0),
            // windows bun.sys.open does not pass iterable=true,
            .windows => bun.sys.openDirAtWindowsA(atfd, basename, { no_follow: true, iterable: true, read_only: true }),
        }) {
            .err e => {
                if (comptime !is_root) {
                    switch (e.getErrno()) {
                        // These things can happen and there's nothing we can do about it.
                        //
                        // This is different than what Node does, at the time of writing.
                        // Node doesn't gracefully handle errors like these. It fails the entire operation.
                        .NOENT, .NOTDIR, .PERM => {
                            return .success;
                        },
                        else => {},
                    }

                    const path_parts: [2]string = [ root_basename, basename ];
                    return {
                        err: e.withPath(bun.path.joinZBuf(buf, &path_parts, .auto)),
                    };
                }
                return {
                    err: e.withPath(args.path.slice()),
                };
            },
            .result fd_ => fd_,
        };

        if (comptime is_root) {
            async_task.root_fd = fd;
        }

        const _fd_guard = ConditionalFDGuard{ fd: fd, armed: comptime !is_root };

        let iterator = DirIterator.iterate(fd, DirIterator.PathType.u8);
        let entry = iterator.next();
        let dirent_path_prev: bun.String = bun.String.empty;
        const _dirent_prev_guard = BunStringPtrGuard{ str: unsafe { &dirent_path_prev } };

        while (current := switch (entry) {
            .err e => {
                if (comptime !is_root) {
                    const path_parts: [2]string = [ root_basename, basename ];
                    return {
                        err: e.withPath(bun.path.joinZBuf(buf, &path_parts, .auto)),
                    };
                }

                return {
                    err: e.withPath(args.path.slice()),
                };
            },
            .result ent => ent,
        }) : (entry = iterator.next()) {
            const utf8_name = current.name.slice();

            const name_to_copy: [:0]const u8 = brk: {
                if (async_task.root_path.sliceAssumeZ().ptr == basename.ptr) {
                    break :brk unsafe { @ptrCast(utf8_name) };
                }

                const path_parts: [2]string = [ basename, utf8_name ];
                break :brk bun.path.joinZBuf(buf, &path_parts, .auto);
            };

            // Track effective kind - may be resolved from .unknown via stat
            let effective_kind = current.kind;

            enqueue: {
                switch (current.kind) {
                    // a symlink might be a directory or might not be
                    // if it's not a directory, the task will fail at that point.
                    .sym_link,

                    // we know for sure it's a directory
                    .directory
                    => {
                        // if the name is too long, we can't enqueue it regardless
                        // the operating system would just return ENAMETOOLONG
                        //
                        // Technically, we could work around that due to the
                        // usage of openat, but then we risk leaving too many
                        // file descriptors open.
                        if (current.name.len + 1 + name_to_copy.len > bun.MAX_PATH_BYTES) break :enqueue;

                        async_task.enqueue(name_to_copy);
                    },
                    // Some filesystems (e.g., Docker bind mounts, FUSE, NFS) return
                    // DT_UNKNOWN for d_type. Use lstatat to determine the actual type.
                    .unknown => {
                        if (current.name.len + 1 + name_to_copy.len > bun.MAX_PATH_BYTES) break :enqueue;

                        // Lazy stat to determine the actual kind (lstatat to not follow symlinks)
                        const stat_result = bun.sys.lstatat(fd, current.name.sliceAssumeZ());
                        switch (stat_result) {
                            .result st => {
                                const real_kind = bun.sys.kindFromMode(@intCast(st.mode));
                                effective_kind = real_kind;
                                if (real_kind == .directory || real_kind == .sym_link) {
                                    async_task.enqueue(name_to_copy);
                                }
                            },
                            .err => {}, // Skip entries we can't stat
                        }
                    },
                    else => {},
                }
            }

            if (ExpectedType == bun.jsc.Node.Dirent) {
                const path_parts_dir: [2]string = [ root_basename, name_to_copy ];
                const path_u8 = bun.path.dirname(bun.path.join(&path_parts_dir, .auto), .auto);
                if (dirent_path_prev.isEmpty() || !bun.strings.eql(dirent_path_prev.byteSlice(), path_u8)) {
                    dirent_path_prev.deref();
                    dirent_path_prev = bun.String.cloneUTF8(path_u8);
                }
                dirent_path_prev.ref();

                entries.append({
                    name: bun.String.cloneUTF8(utf8_name),
                    path: dirent_path_prev,
                    kind: effective_kind,
                }) catch e => bun.handleOom(e);
            } else if (ExpectedType == Buffer) {
                bun.handleOom(entries.append(bun.handleOom(Buffer.fromString(name_to_copy, bun.default_allocator))));
            } else if (ExpectedType == bun.String) {
                bun.handleOom(entries.append(bun.String.cloneUTF8(name_to_copy)));
            } else {
                bun.outOfMemory();
            }
        }

        return .success;
    }

    static fn readdirWithEntriesRecursiveSync(
        buf: *bun.PathBuffer,
        args: Arguments.Readdir,
        root_basename: [:0]const u8,
        comptime ExpectedType: type,
        entries: *std.array_list.Managed(ExpectedType),
    ): Maybe(void) {
        let iterator_stack = std.heap.stackFallback(128, bun.default_allocator);
        let stack = bun.LinearFifo([:0]const u8, .Dynamic).init(iterator_stack.get());
        let basename_stack = std.heap.stackFallback(8192 * 2, bun.default_allocator);
        const basename_allocator = basename_stack.get();
        const _stack_guard = ReaddirSyncStackGuard{ stack: unsafe { &stack }, allocator: basename_allocator };

        stack.writeItem(root_basename) catch unreachable;
        let root_fd: bun.FD = bun.invalid_fd;
        let _root_fd_guard = ConditionalFDGuard{ fd: bun.invalid_fd, armed: false };

        while (basename := stack.readItem()) {
            const _basename_guard = ConditionalAllocZSliceGuard{ slice: basename, allocator: basename_allocator, armed: root_basename.ptr != basename.ptr };

            const flags = bun.O.DIRECTORY | bun.O.RDONLY;
            const fd = switch (Syscall.openat(if (root_fd == bun.invalid_fd) bun.FD.cwd() else root_fd, basename, flags, 0)) {
                .err e => {
                    if (root_fd == bun.invalid_fd) {
                        return {
                            err: e.withPath(args.path.slice()),
                        };
                    }

                    switch (e.getErrno()) {
                        // These things can happen and there's nothing we can do about it.
                        //
                        // This is different than what Node does, at the time of writing.
                        // Node doesn't gracefully handle errors like these. It fails the entire operation.
                        .NOENT, .NOTDIR, .PERM => continue,
                        else => {
                            // const path_parts = [_]string{ args.path.slice(), basename };
                            // TODO: propagate file path (removed previously because it leaked the path)
                            return { err: e };
                        },
                    }
                },
                .result fd_ => fd_,
            };
            if (root_fd == bun.invalid_fd) {
                root_fd = fd;
                _root_fd_guard.fd = fd;
                _root_fd_guard.armed = true;
            }

            const _fd_guard = ConditionalFDGuard{ fd: fd, armed: fd != root_fd };

            let iterator = DirIterator.iterate(fd, DirIterator.PathType.u8);
            let entry = iterator.next();
            let dirent_path_prev: bun.String = bun.String.dead;
            const _dirent_prev_guard = BunStringPtrGuard{ str: unsafe { &dirent_path_prev } };

            while (current := switch (entry) {
                .err e => {
                    return {
                        err: e.withPath(args.path.slice()),
                    };
                },
                .result ent => ent,
            }) : (entry = iterator.next()) {
                const utf8_name = current.name.slice();

                const name_to_copy = brk: {
                    if (root_basename.ptr == basename.ptr) {
                        break :brk utf8_name;
                    }

                    const path_parts: [2]string = [ basename, utf8_name ];
                    break :brk bun.path.joinZBuf(buf, &path_parts, .auto);
                };

                // Track effective kind - may be resolved from .unknown via stat
                let effective_kind = current.kind;

                enqueue: {
                    switch (current.kind) {
                        // a symlink might be a directory or might not be
                        // if it's not a directory, the task will fail at that point.
                        .sym_link,

                        // we know for sure it's a directory
                        .directory
                        => {
                            if (current.name.len + 1 + name_to_copy.len > bun.MAX_PATH_BYTES) break :enqueue;
                            stack.writeItem(basename_allocator.dupeZ(u8, name_to_copy) catch break :enqueue) catch break :enqueue;
                        },
                        // Some filesystems (e.g., Docker bind mounts, FUSE, NFS) return
                        // DT_UNKNOWN for d_type. Use lstatat to determine the actual type.
                        .unknown => {
                            if (current.name.len + 1 + name_to_copy.len > bun.MAX_PATH_BYTES) break :enqueue;

                            // Lazy stat to determine the actual kind (lstatat to not follow symlinks)
                            const stat_result = bun.sys.lstatat(fd, current.name.sliceAssumeZ());
                            switch (stat_result) {
                                .result st => {
                                    const real_kind = bun.sys.kindFromMode(@intCast(st.mode));
                                    effective_kind = real_kind;
                                    if (real_kind == .directory || real_kind == .sym_link) {
                                        stack.writeItem(basename_allocator.dupeZ(u8, name_to_copy) catch break :enqueue) catch break :enqueue;
                                    }
                                },
                                .err => {}, // Skip entries we can't stat
                            }
                        },
                        else => {},
                    }
                }

                if (ExpectedType == bun.jsc.Node.Dirent) {
                    const path_parts_sync: [2]string = [ root_basename, name_to_copy ];
                    const path_u8 = bun.path.dirname(bun.path.join(&path_parts_sync, .auto), .auto);
                    if (dirent_path_prev.isEmpty() || !bun.strings.eql(dirent_path_prev.byteSlice(), path_u8)) {
                        dirent_path_prev.deref();
                        dirent_path_prev = jsc.WebCore.encoding.toBunString(strings.withoutNTPrefix(std.@"meta".Child(@TypeOf(path_u8)), path_u8), args.encoding);
                    }
                    dirent_path_prev.ref();
                    entries.append({
                        name: jsc.WebCore.encoding.toBunString(utf8_name, args.encoding),
                        path: dirent_path_prev,
                        kind: effective_kind,
                    }) catch e => bun.handleOom(e);
                } else if (ExpectedType == Buffer) {
                    bun.handleOom(entries.append(bun.handleOom(Buffer.fromString(strings.withoutNTPrefix(std.@"meta".Child(@TypeOf(name_to_copy)), name_to_copy), bun.default_allocator))));
                } else if (ExpectedType == bun.String) {
                    bun.handleOom(entries.append(jsc.WebCore.encoding.toBunString(strings.withoutNTPrefix(std.@"meta".Child(@TypeOf(name_to_copy)), name_to_copy), args.encoding)));
                } else {
                    @compileError("unreachable");
                }
            }
        }

        return .success;
    }

    static fn shouldThrowOutOfMemoryEarlyForJavaScript(encoding: Encoding, size: usize, syscall: Syscall.Tag): Syscall.Error? {
        // Strings & typed arrays max out at 4.7 GB.
        // But, it's **string length**
        // So you can load an 8 GB hex string, for example, it should be fine.
        const adjusted_size = switch (encoding) {
            .utf16le, .ucs2, .utf8 => size / 4 -| 1,
            .hex => size / 2 -| 1,
            .base64, .base64url => size / 3 -| 1,
            .ascii, .latin1, .buffer => size,
        };

        if (
        // Typed arrays in JavaScript are limited to 4.7 GB.
        adjusted_size > jsc.VirtualMachine.synthetic_allocation_limit ||
            // If they do not have enough memory to open the file and they're on Linux, let's throw an error instead of dealing with the OOM killer.
            (Environment.isLinux && size >= bun.getTotalMemorySize()))
        {
            return Syscall.Error.fromCode(.NOMEM, syscall);
        }

        return null;
    }

    static fn readdirInner(
        buf: *bun.PathBuffer,
        args: Arguments.Readdir,
        comptime ExpectedType: type,
        comptime recursive: bool,
        comptime flavor: Flavor,
    ): Maybe(Return.Readdir) {
        const file_type = if (ExpectedType == bun.jsc.Node.Dirent)
            "with_file_types"
        else if (ExpectedType == bun.String)
            "files"
        else if (ExpectedType == Buffer)
            "buffers"
        else
            @compileError("unreachable");

        const path = args.path.sliceZ(buf);

        if (comptime recursive && flavor == .sync) {
            let buf_to_pass: bun.PathBuffer = undefined;

            let entries = std.array_list.Managed(ExpectedType).init(bun.default_allocator);
            return switch (readdirWithEntriesRecursiveSync(&buf_to_pass, args, path, ExpectedType, &entries)) {
                .err e => {
                    for (*result of entries.items) {
                        if (ExpectedType == bun.jsc.Node.Dirent) {
                            result.deref();
                        } else if (ExpectedType == Buffer) {
                            result.destroy();
                        } else if (ExpectedType == bun.String) {
                            result.deref();
                        } else {
                            @compileError("unreachable");
                        }
                    }

                    entries.deinit();

                    return {
                        err: e,
                    };
                },
                .result => .{ .result = @unionInit(Return.Readdir, file_type, entries.items) },
            };
        }

        if (comptime recursive) {
            @panic("This code path should never be reached. It should only go through readdirWithEntriesRecursiveAsync.");
        }

        const flags = bun.O.DIRECTORY | bun.O.RDONLY;
        const fd = switch (switch (Environment.os) {
            else => Syscall.open(path, flags, 0),
            // windows bun.sys.open does not pass iterable=true,
            .windows => bun.sys.openDirAtWindowsA(bun.FD.cwd(), path, { iterable: true, read_only: true }),
        }) {
            .err e => return {
                err: e.withPath(args.path.slice()),
            },
            .result fd_ => fd_,
        };

        const _fd_guard = FDGuard{ fd: fd };

        let entries = std.array_list.Managed(ExpectedType).init(bun.default_allocator);
        return switch (readdirWithEntries(args, fd, path, ExpectedType, &entries)) {
            .err e => return {
                err: e,
            },
            .result => .{ .result = @unionInit(Return.Readdir, file_type, entries.items) },
        };
    }

    pub const StringType = _NodeFSStringType;

    fn readFile(*_, args: Arguments.ReadFile, comptime flavor: Flavor): Maybe(Return.ReadFile) {
        const ret = _.readFileWithOptions(args, flavor, .default);
        return switch (ret) {
            .err => .{ .err = ret.err },
            .result result => switch (result) {
                .buffer buffer => .{
                    .result = .{ .buffer = buffer },
                },
                .transcoded_string str => {
                    if (str.tag == .Dead) {
                        return { err: Syscall.Error.fromCode(.NOMEM, .read).withPathLike(args.path) };
                    }

                    return { result: { string: { underlying: str } } };
                },
                .string => brk: {
                    const str = bun.SliceWithUnderlyingString.transcodeFromOwnedSlice(unsafe { @constCast(ret.result.string) }, args.encoding);

                    if (str.underlying.tag == .Dead && str.utf8.len == 0) {
                        return { err: Syscall.Error.fromCode(.NOMEM, .read).withPathLike(args.path) };
                    }

                    break :brk { result: { string: str } };
                },
                else => unreachable,
            },
        };
    }

    fn readFileWithOptions(*_, args: Arguments.ReadFile, comptime flavor: Flavor, comptime string_type: StringType): Maybe(Return.ReadFileWithOptions) {
        let path: [:0]const u8 = undefined;
        const fd_maybe_windows: FD = switch (args.path) {
            .path => brk: {
                path = args.path.path.sliceZ(&_.sync_error_buf);

                if (graph := bun.StandaloneModuleGraph.get()) {
                    if (file := graph.find(path)) {
                        if (args.encoding == .buffer) {
                            return {
                                result: {
                                    buffer: Buffer.fromBytes(
                                        bun.handleOom(bun.default_allocator.dupe(u8, file.contents)),
                                        bun.default_allocator,
                                        .Uint8Array,
                                    ),
                                },
                            };
                        } else if (comptime string_type == .default)
                            return {
                                result: {
                                    string: bun.handleOom(bun.default_allocator.dupe(u8, file.contents)),
                                },
                            }
                        else
                            return {
                                result: {
                                    null_terminated: bun.handleOom(bun.default_allocator.dupeZ(u8, file.contents)),
                                },
                            };
                    }
                }

                break :brk switch (bun.sys.open(
                    path,
                    args.flag.asInt() | bun.O.NOCTTY,
                    default_permission,
                )) {
                    .err e => return {
                        err: e.withPath(args.path.path.slice()),
                    },
                    .result fd => fd,
                };
            },
            .fd fd => fd,
        };
        const fd = fd_maybe_windows.makeLibUVOwned() catch {
            if (args.path == .path)
                fd_maybe_windows.close();

            return {
                e: {
                    errno: @intFromEnum(posix.E.MFILE),
                    syscall: .open,
                },
            };
        };

        const _fd_guard = ConditionalFDGuard{ fd: fd, armed: args.path == .path };

        if (args.aborted()) return Maybe(Return.ReadFileWithOptions).aborted;

        // Only used in DOMFormData
        if (args.offset > 0) {
            * = Syscall.setFileOffset(fd, args.offset);
        }

        let did_succeed = false;
        let total: usize = 0;
        const async_stack_buf_len = comptime if (flavor == .sync) 0 else 256 * 1024;
        let async_stack_buffer: [async_stack_buf_len]u8 = undefined;

        // --- Optimization: attempt to read up to 256 KB before calling stat()
        // If we manage to read the entire file, we don't need to call stat() at all.
        // This will make it slightly slower to read e.g. 512 KB files, but usually the OS won't return a full 512 KB in one read anyway.
        const temporary_read_buffer_before_stat_call = brk: {
            const temporary_read_buffer = temporary_read_buffer: {
                let temporary_read_buffer: []u8 = &async_stack_buffer;

                if (comptime flavor == .sync) {
                    if (vm := _.vm) {
                        temporary_read_buffer = vm.rareData().pipeReadBuffer();
                    }
                }

                let available = temporary_read_buffer;
                while (available.len > 0) {
                    switch (Syscall.read(fd, available)) {
                        .err e => return { err: e },
                        .result amt => {
                            if (amt == 0) {
                                did_succeed = true;
                                break;
                            }
                            total += amt;
                            available = available[amt..];
                        },
                    }
                }
                break :temporary_read_buffer temporary_read_buffer[0..total];
            };

            if (did_succeed) {
                switch (args.encoding) {
                    .buffer => {
                        if (comptime flavor == .sync && string_type == .default) {
                            if (vm := _.vm) {
                                // Attempt to create the buffer in jsc's heap.
                                // This avoids creating a WastefulTypedArray.
                                const array_buffer = jsc.ArrayBuffer.createBuffer(vm.global, temporary_read_buffer) catch .zero; // TODO: properly propagate exception upwards
                                array_buffer.ensureStillAlive();
                                const ab_opt = array_buffer.asArrayBuffer(vm.global);
                                if (ab_opt == null) return {
                                    err: Syscall.Error.fromCode(.NOMEM, .read).withPathLike(args.path),
                                };
                                const ab = ab_opt.?;
                                return {
                                    result: {
                                        buffer: jsc.MarkedArrayBuffer{
                                            buffer: ab,
                                        },
                                    },
                                };
                            }
                        }

                        return {
                            result: {
                                buffer: Buffer.fromBytes(
                                    bun.default_allocator.dupe(u8, temporary_read_buffer) catch return {
                                        err: Syscall.Error.fromCode(.NOMEM, .read).withPathLike(args.path),
                                    },
                                    bun.default_allocator,
                                    .Uint8Array,
                                ),
                            },
                        };
                    },
                    else => {
                        if (comptime string_type == .default) {
                            return {
                                result: {
                                    transcoded_string: jsc.WebCore.encoding.toBunString(temporary_read_buffer, args.encoding),
                                },
                            };
                        } else {
                            return {
                                result: {
                                    null_terminated: bun.default_allocator.dupeZ(u8, temporary_read_buffer) catch return {
                                        err: Syscall.Error.fromCode(.NOMEM, .read).withPathLike(args.path),
                                    },
                                },
                            };
                        }
                    },
                }
            }

            break :brk temporary_read_buffer;
        };
        // ----------------------------

        if (args.aborted()) return Maybe(Return.ReadFileWithOptions).aborted;

        const stat_ = switch (Syscall.fstat(fd)) {
            .err e => return {
                err: e,
            },
            .result stat_ => stat_,
        };

        // For certain files, the size might be 0 but the file might still have contents.
        // https://github.com/oven-sh/bun/issues/1220
        const max_size = args.max_size ?? std.math.maxInt(jsc.WebCore.Blob.SizeType);
        const has_max_size = args.max_size != null;

        const size = @as(
            u64,
            @max(
                @max(
                    @min(stat_.size, max_size),
                    @as(i64, i32(total))
                ),
                0
            )
        ) + @intFromBool(comptime string_type == .null_terminated);

        if (args.limit_size_for_javascript &&
            // assume that anything more than 40 bits is not trustworthy.
            (size < std.math.maxInt(u40)))
        {
            if (e := shouldThrowOutOfMemoryEarlyForJavaScript(args.encoding, size, .read)) {
                return { err: e.withPathLike(args.path) };
            }
        }

        let buf = std.array_list.Managed(u8).init(bun.default_allocator);
        let _buf_guard = ByteArrayListGuard{ list: unsafe { &buf } };
        buf.ensureTotalCapacityPrecise(
            @min(
                @max(temporary_read_buffer_before_stat_call.len, size) + 16,
                max_size,
                1024 * 1024 * 1024 * 8,
            ),
        ) catch return { err: Syscall.Error.fromCode(.NOMEM, .read).withPathLike(args.path) };
        if (temporary_read_buffer_before_stat_call.len > 0) {
            buf.appendSlice(temporary_read_buffer_before_stat_call) catch return {
                err: Syscall.Error.fromCode(.NOMEM, .read).withPathLike(args.path),
            };
        }
        buf.expandToCapacity();

        while (total < size) {
            if (args.aborted()) return Maybe(Return.ReadFileWithOptions).aborted;
            switch (Syscall.read(fd, buf.items.ptr[total..@min(buf.capacity, max_size)])) {
                .err e => return { err: e },
                .result amt => {
                    total += amt;

                    if (args.limit_size_for_javascript) {
                        if (e := shouldThrowOutOfMemoryEarlyForJavaScript(args.encoding, total, .read)) {
                            return {
                                err: e.withPathLike(args.path),
                            };
                        }
                    }

                    // There are cases where stat()'s size is wrong or out of date
                    if (total > size && amt != 0 && !has_max_size) {
                        buf.items.len = total;
                        buf.ensureUnusedCapacity(8192) catch {
                            return { err: Syscall.Error.fromCode(.NOMEM, .read).withPathLike(args.path) };
                        };
                        continue;
                    }

                    if (amt == 0) {
                        did_succeed = true;
                        break;
                    }
                },
            }
        }
        if (!did_succeed) {
            while (true) {
                if (args.aborted()) return Maybe(Return.ReadFileWithOptions).aborted;
                switch (Syscall.read(fd, buf.items.ptr[total..@min(buf.capacity, max_size)])) {
                    .err e => return { err: e },
                    .result amt => {
                        total += amt;

                        if (args.limit_size_for_javascript) {
                            if (e := shouldThrowOutOfMemoryEarlyForJavaScript(args.encoding, total, .read)) {
                                return { err: e.withPathLike(args.path) };
                            }
                        }

                        if (total > size && amt != 0 && !has_max_size) {
                            buf.items.len = total;
                            buf.ensureUnusedCapacity(8192) catch {
                                return { err: Syscall.Error.fromCode(.NOMEM, .read).withPathLike(args.path) };
                            };
                            continue;
                        }

                        if (amt == 0) {
                            did_succeed = true;
                            break;
                        }
                    },
                }
            }
        }

        buf.items.len = if (comptime string_type == .null_terminated) total + 1 else total;
        if (total == 0) {
            return switch (args.encoding) {
                .buffer => .{
                    .result = .{
                        .buffer = Buffer.empty,
                    },
                },
                else => brk: {
                    if (comptime string_type == .default) {
                        break :brk {
                            result: {
                                string: "",
                            },
                        };
                    } else {
                        break :brk {
                            result: {
                                null_terminated: "",
                            },
                        };
                    }
                },
            };
        }

        return switch (args.encoding) {
            .buffer => brk_buf: {
                _buf_guard.armed = false;
                break :brk_buf .{
                    .result = .{
                        .buffer = Buffer.fromBytes(buf.items, bun.default_allocator, .Uint8Array),
                    },
                };
            },
            else => brk: {
                if (comptime string_type == .default) {
                    _buf_guard.armed = false;
                    break :brk {
                        result: {
                            string: buf.items,
                        },
                    };
                } else {
                    const owned = buf.toOwnedSliceSentinel(0) catch return {
                        // Since we are expecting a null-terminated string, we can't just ignore the resize failure.
                        err: Syscall.Error.fromCode(.NOMEM, .read).withPathLike(args.path),
                    };
                    _buf_guard.armed = false;
                    break :brk {
                        result: {
                            null_terminated: owned,
                        },
                    };
                }
            },
        };
    }

    static fn writeFileWithPathBuffer(pathbuf: *bun.PathBuffer, args: Arguments.WriteFile): Maybe(Return.WriteFile) {
        const fd = switch (args.file) {
            .path => brk: {
                const path = args.file.path.sliceZWithForceCopy(pathbuf, true);

                const open_result = bun.sys.openat(
                    args.dirfd,
                    path,
                    args.flag.asInt(),
                    args.mode,
                );

                break :brk switch (open_result) {
                    .err e => return {
                        err: e.withPath(args.file.path.slice()),
                    },
                    .result fd => fd,
                };
            },
            .fd fd => fd,
        };

        const _fd_guard = ConditionalFDGuard{ fd: fd, armed: args.file == .path };

        if (args.aborted()) return Maybe(Return.WriteFile).aborted;

        let buf = args.data.slice();
        let written: usize = 0;

        // Attempt to pre-allocate large files
        // Worthwhile after 6 MB at least on ext4 linux
        if (bun.sys.preallocate_supported && buf.len >= bun.sys.preallocate_length) preallocate: {
            const offset: usize = if (args.file == .path)
                // on mac, it's relatively positioned
                0
            else brk: {
                // on linux, it's absolutely positione

                switch (Syscall.lseek(
                    fd,
                    @as(std.posix.off_t, i32(0)),
                    std.os.linux.SEEK.CUR,
                )) {
                    .err => break :preallocate,
                    .result pos => break :brk @as(usize, i32(pos)),
                }
            };

            bun.sys.preallocate_file(
                fd.cast(),
                @as(std.posix.off_t, i32(offset)),
                @as(std.posix.off_t, i32(buf.len)),
            ) catch {};
        }

        while (buf.len > 0) {
            switch (bun.sys.write(fd, buf)) {
                .err e => return {
                    err: e,
                },
                .result amt => {
                    buf = buf[amt..];
                    written += amt;
                    if (amt == 0) {
                        break;
                    }
                },
            }
        }

        // https://github.com/oven-sh/bun/issues/2931
        // https://github.com/oven-sh/bun/issues/10222
        // Only truncate if the open flag specifies O_TRUNC and writing to a path
        if ((args.flag.asInt() & bun.O.TRUNC) != 0 && args.file != .fd) {
            // If this errors, we silently ignore it.
            // Not all files are seekable (and thus, not all files can be truncated).
            if (Environment.isWindows) {
                * = bun.windows.SetEndOfFile(fd.cast());
            } else {
                * = Syscall.ftruncate(fd, i32(@as(u63, @truncate(written))));
            }
        }

        if (args.flush) {
            if (Environment.isWindows) {
                * = std.os.windows.kernel32.FlushFileBuffers(fd.cast());
            } else {
                * = system.fsync(fd.cast());
            }
        }

        return .success;
    }

    fn writeFile(*_, args: Arguments.WriteFile, _: Flavor): Maybe(Return.WriteFile) {
        return writeFileWithPathBuffer(&_.sync_error_buf, args);
    }

    fn readlink(*_, args: Arguments.Readlink, _: Flavor): Maybe(Return.Readlink) {
        let outbuf: bun.PathBuffer = undefined;
        const inbuf = &_.sync_error_buf;

        const path = args.path.sliceZ(inbuf);

        const link_path = switch (Syscall.readlink(path, &outbuf)) {
            .err e => return { err: e.withPath(args.path.slice()) },
            .result result => result,
        };

        return {
            result: switch (args.encoding) {
                .buffer => .{
                    .buffer = Buffer.fromString(link_path, bun.default_allocator) catch unreachable,
                },
                else => if (args.path == .slice_with_underlying_string &&
                    strings.eqlLong(args.path.slice_with_underlying_string.slice(), link_path, true))
                    .{
                        .string = args.path.slice_with_underlying_string.dupeRef(),
                    }
                else
                    .{
                        .string = .{ .utf8 = {}, .underlying = bun.String.cloneUTF8(link_path) },
                    },
            },
        };
    }

    fn realpathNonNative(*_, args: Arguments.Realpath, _: Flavor): Maybe(Return.Realpath) {
        return switch (_.realpathInner(args, .emulated)) {
            .result res => .{ .result = res },
            .err e => .{ .err = {
                errno: e.errno,
                syscall: .lstat,
                path: args.path.slice(),
            } },
        };
    }

    fn realpath(*_, args: Arguments.Realpath, _: Flavor): Maybe(Return.Realpath) {
        return switch (_.realpathInner(args, .native)) {
            .result res => .{ .result = res },
            .err e => .{
                .err = .{
                    .errno = e.errno,
                    .syscall = .realpath,
                    .path = args.path.slice(),
                },
            },
        };
    }

    // For `fs.realpath`, Node.js uses `lstat`, exposing the native system call under
    // `fs.realpath.native`. In Bun, the system call is the default, but the error
    // code must be changed to make it seem like it is using lstat (tests expect this),
    // in addition, some more subtle things depend on the variant.
    fn realpathInner(*_, args: Arguments.Realpath, variant: RealpathVariant): Maybe(Return.Realpath) {
        if (Environment.isWindows) {
            let req: uv.fs_t = uv.fs_t.uninitialized;
            const _req_guard = UvFsReqDeinitGuard{ req: unsafe { &req } };
            const rc = uv.uv_fs_realpath(bun.Async.Loop.get(), &req, args.path.sliceZ(&_.sync_error_buf).ptr, null);

            if (errno := rc.errno())
                return { err: Syscall.Error{
                    errno: errno,
                    syscall: .realpath,
                    path: args.path.slice(),
                } };

            const result_ptr: OptCStr = req.ptrAs(OptCStr);
            let buf = bun.span(result_ptr ?? return { err: Syscall.Error{
                errno: @intFromEnum(bun.sys.E.NOENT),
                syscall: .realpath,
                path: args.path.slice(),
            } });

            if (variant == .emulated) {
                // remove the trailing slash
                if (buf[buf.len - 1] == '\\') {
                    buf[buf.len - 1] = 0;
                    buf.len -= 1;
                }
            }

            return {
                result: switch (args.encoding) {
                    .buffer => .{
                        .buffer = Buffer.fromString(buf, bun.default_allocator) catch unreachable,
                    },
                    .utf8 => utf8: {
                        if (args.path == .slice_with_underlying_string) {
                            const slice = args.path.slice_with_underlying_string;
                            if (strings.eqlLong(slice.slice(), buf, true)) {
                                return { result: { string: slice.dupeRef() } };
                            }
                        }
                        break :utf8 {
                            string: { utf8: {}, underlying: bun.String.cloneUTF8(buf) },
                        };
                    },
                    else => |enc| .{
                        .string = .{ .utf8 = {}, .underlying = jsc.WebCore.encoding.toBunString(buf, enc) },
                    },
                },
            };
        }

        let outbuf: bun.PathBuffer = undefined;
        let inbuf = &_.sync_error_buf;
        if (comptime Environment.allow_assert) bun.assert(FileSystem.instance_loaded);

        const path_slice = args.path.slice();

        let parts: [2]string = [ FileSystem.instance.top_level_dir, path_slice ];
        const path_ = FileSystem.instance.absBuf(&parts, inbuf);
        inbuf[path_.len] = 0;
        const path: [:0]u8 = inbuf[0..path_.len :0];

        const flags = if (comptime Environment.isLinux)
            // O_PATH is faster
            bun.O.PATH
        else
            // O_NONBLOCK prevents blocking on a FIFO.
            // O_NOCTTY prevents acquiring a controlling terminal.
            bun.O.RDONLY | bun.O.NONBLOCK | bun.O.NOCTTY;

        const fd = switch (bun.sys.open(path, flags, 0)) {
            .err e => return { err: e.withPath(path) },
            .result fd_ => fd_,
        };

        const _fd_guard = FDGuard{ fd: fd };

        const buf = switch (Syscall.getFdPath(fd, &outbuf)) {
            .err e => return { err: e.withPath(path) },
            .result buf_ => buf_,
        };

        return {
            result: switch (args.encoding) {
                .buffer => .{
                    .buffer = Buffer.fromString(buf, bun.default_allocator) catch unreachable,
                },
                .utf8 => utf8: {
                    if (args.path == .slice_with_underlying_string) {
                        const slice = args.path.slice_with_underlying_string;
                        if (strings.eqlLong(slice.slice(), buf, true)) {
                            return { result: { string: slice.dupeRef() } };
                        }
                    }
                    break :utf8 {
                        string: { utf8: {}, underlying: bun.String.cloneUTF8(buf) },
                    };
                },
                else => |enc| .{
                    .string = .{ .utf8 = {}, .underlying = jsc.WebCore.encoding.toBunString(buf, enc) },
                },
            },
        };
    }

    pub const realpathNative = realpath;

    fn rename(*_, args: Arguments.Rename, _: Flavor): Maybe(Return.Rename) {
        const from_buf = &_.sync_error_buf;
        let to_buf: bun.PathBuffer = undefined;

        const from_path = args.old_path.sliceZ(from_buf);
        const to = args.new_path.sliceZ(&to_buf);
        return switch (Syscall.rename(from_path, to)) {
            .result result => .{ .result = result },
            .err e => .{ .err = e.withPathDest(args.old_path.slice(), args.new_path.slice()) },
        };
    }

    fn rmdir(*_, args: Arguments.RmDir, _: Flavor): Maybe(Return.Rmdir) {
        if (args.recursive) {
            zigDeleteTree(std.fs.cwd(), args.path.slice(), .directory) catch e => {
                let errno: bun.sys.E = switch (@as(anyerror, e)) {
                    error.AccessDenied => .PERM,
                    error.FileTooBig => .FBIG,
                    error.SymLinkLoop => .LOOP,
                    error.ProcessFdQuotaExceeded => .NFILE,
                    error.NameTooLong => .NAMETOOLONG,
                    error.SystemFdQuotaExceeded => .MFILE,
                    error.SystemResources => .NOMEM,
                    error.ReadOnlyFileSystem => .ROFS,
                    error.FileSystem => .IO,
                    error.FileBusy => .BUSY,
                    error.DeviceBusy => .BUSY,

                    // One of the path components was not a directory.
                    // This error is unreachable if `sub_path` does not contain a path separator.
                    error.NotDir => .NOTDIR,
                    // On Windows, file paths must be valid WTF-8.
                    error.InvalidUtf8 => .INVAL,
                    error.InvalidWtf8 => .INVAL,

                    // On Windows, file paths cannot contain these characters:
                    // '/', '*', '?', '"', '<', '>', '|'
                    error.BadPathName => .INVAL,

                    error.FileNotFound => .NOENT,
                    error.IsDir => .ISDIR,

                    else => .FAULT,
                };
                if (Environment.isWindows && errno == .NOTDIR) {
                    errno = .NOENT;
                }
                return .{
                    .err = bun.sys.Error.fromCode(errno, .rmdir),
                };
            };

            return .success;
        }

        if (comptime Environment.isWindows) {
            return switch (Syscall.rmdir(args.path.sliceZ(&_.sync_error_buf))) {
                .err e => .{ .err = e.withPath(args.path.slice()) },
                .result result => .{ .result = result },
            };
        }

        return Maybe(Return.Rmdir).errnoSysP(system.rmdir(args.path.sliceZ(&_.sync_error_buf)), .rmdir, args.path.slice()) ??
            .success;
    }

    fn rm(*_, args: Arguments.Rm, _: Flavor): Maybe(Return.Rm) {

        // We cannot use removefileat() on macOS because it does not handle write-protected files as expected.
        if (args.recursive) {
            zigDeleteTree(std.fs.cwd(), args.path.slice(), .file) catch e => {
                bun.handleErrorReturnTrace(e, @errorReturnTrace());
                const errno: E = switch (@as(anyerror, e)) {
                    // error.InvalidHandle => .BADF,
                    error.AccessDenied => .ACCES,
                    error.FileTooBig => .FBIG,
                    error.SymLinkLoop => .LOOP,
                    error.ProcessFdQuotaExceeded => .NFILE,
                    error.NameTooLong => .NAMETOOLONG,
                    error.SystemFdQuotaExceeded => .MFILE,
                    error.SystemResources => .NOMEM,
                    error.ReadOnlyFileSystem => .ROFS,
                    error.FileSystem => .IO,
                    error.FileBusy => .BUSY,
                    error.DeviceBusy => .BUSY,

                    // One of the path components was not a directory.
                    // This error is unreachable if `sub_path` does not contain a path separator.
                    error.NotDir => .NOTDIR,
                    // On Windows, file paths must be valid WTF-8.
                    error.InvalidUtf8 => .INVAL,
                    error.InvalidWtf8 => .INVAL,

                    // On Windows, file paths cannot contain these characters:
                    // '/', '*', '?', '"', '<', '>', '|'
                    error.BadPathName => .INVAL,

                    error.FileNotFound => brk: {
                        if (args.force) {
                            return .success;
                        }
                        break :brk .NOENT;
                    },
                    error.IsDir => .ISDIR,

                    else => .FAULT,
                };
                return .{
                    .err = bun.sys.Error.fromCode(errno, .rm).withPath(args.path.slice()),
                };
            };
            return .success;
        }

        const dest = args.path.sliceZ(&_.sync_error_buf);

        std.posix.unlinkZ(dest) catch err1 => {
            bun.handleErrorReturnTrace(err1, @errorReturnTrace());
            // empircally, it seems to return AccessDenied when the
            // file is actually a directory on macOS.
            if (args.recursive &&
                (err1 == error.IsDir || err1 == error.NotDir || err1 == error.AccessDenied))
            {
                std.posix.rmdirZ(dest) catch err2 => {
                    bun.handleErrorReturnTrace(err2, @errorReturnTrace());
                    const code: E = switch (err2) {
                        error.AccessDenied => .ACCES,
                        error.SymLinkLoop => .LOOP,
                        error.NameTooLong => .NAMETOOLONG,
                        error.SystemResources => .NOMEM,
                        error.ReadOnlyFileSystem => .ROFS,
                        error.FileBusy => .BUSY,
                        error.FileNotFound => brk: {
                            if (args.force) {
                                return .success;
                            }
                            break :brk .NOENT;
                        },
                        error.InvalidUtf8 => .INVAL,
                        error.InvalidWtf8 => .INVAL,
                        error.BadPathName => .INVAL,
                        else => .FAULT,
                    };

                    return {
                        err: bun.sys.Error.fromCode(code, .rm).withPath(args.path.slice()),
                    };
                };

                return .success;
            }

            {
                const code: E = switch (err1) {
                    error.IsDir => .ISDIR,
                    error.AccessDenied => .ACCES,
                    error.SymLinkLoop => .LOOP,
                    error.NameTooLong => .NAMETOOLONG,
                    error.SystemResources => .NOMEM,
                    error.ReadOnlyFileSystem => .ROFS,
                    error.FileBusy => .BUSY,
                    error.InvalidUtf8 => .INVAL,
                    error.InvalidWtf8 => .INVAL,
                    error.BadPathName => .INVAL,
                    error.FileNotFound => brk: {
                        if (args.force) {
                            return .success;
                        }
                        break :brk .NOENT;
                    },
                    else => .FAULT,
                };

                return {
                    err: bun.sys.Error.fromCode(code, .rm).withPath(args.path.slice()),
                };
            }
        };

        return .success;
    }

    fn statfs(*_, args: Arguments.StatFS, _: Flavor): Maybe(Return.StatFS) {
        const Res = Maybe(Return.StatFS);
        return switch (Syscall.statfs(args.path.sliceZ(&_.sync_error_buf))) {
            .result => |*result| Res{ result: Return.StatFS.init(result, args.big_int) },
            .err e => Res{ err: e },
        };
    }

    fn stat(*_, args: Arguments.Stat, _: Flavor): Maybe(Return.Stat) {
        const path = args.path.sliceZ(&_.sync_error_buf);
        if (graph := bun.StandaloneModuleGraph.get()) {
            if (result := graph.stat(path)) {
                return { result: { stats: .init(&Syscall.PosixStat.init(&result), args.big_int) } };
            }
        }

        if (Environment.isLinux && Syscall.supports_statx_on_linux.load(.monotonic)) {
            return switch (Syscall.statx(path, &.{ .@"type", .mode, .nlink, .uid, .gid, .atime, .mtime, .ctime, .btime, .ino, .size, .blocks })) {
                .result result => .{
                    .result = .{ .stats = .init(&result, args.big_int) },
                },
                .err e => brk: {
                    if (!args.throw_if_no_entry && e.getErrno() == .NOENT) {
                        return { result: .not_found };
                    }
                    let err_val = e.withPath(args.path.slice());
                    err_val.syscall = .stat;
                    break :brk { err: err_val };
                },
            };
        } else {
            return switch (Syscall.stat(path)) {
                .result result => .{
                    .result = .{ .stats = .init(&Syscall.PosixStat.init(&result), args.big_int) },
                },
                .err e => brk: {
                    if (!args.throw_if_no_entry && e.getErrno() == .NOENT) {
                        return { result: .not_found };
                    }
                    let err_val = e.withPath(args.path.slice());
                    err_val.syscall = .stat;
                    break :brk { err: err_val };
                },
            };
        }
    }

    fn symlink(*_, args: Arguments.Symlink, _: Flavor): Maybe(Return.Symlink) {
        let to_buf: bun.PathBuffer = undefined;

        if (Environment.isWindows) {
            const target_path = args.target_path.slice();
            const new_path = args.new_path.slice();
            // Note: to_buf and sync_error_buf hold intermediate states, but the
            // ending state is:
            //    - new_path is in &sync_error_buf
            //    - target_path is in &to_buf

            // Stat target if unspecified.
            const resolved_link_type: ResolvedLinkType = switch (args.link_type) {
                .unspecified => auto_detect: {
                    const src = bun.path.joinAbsStringBuf(
                        bun.getcwd(&to_buf) catch @panic("failed to resolve current working directory"),
                        &_.sync_error_buf,
                        &.{
                            bun.Dirname.dirname(u8, new_path) ?? new_path,
                            target_path,
                        },
                        .windows,
                    );
                    break :auto_detect switch (bun.sys.directoryExistsAt(bun.invalid_fd, src)) {
                        .err => .file,
                        .result is_dir => if (is_dir) .dir else .file,
                    };
                },
                .file => .file,
                .dir => .dir,
                .junction => .junction,
            };
            // preprocessSymlinkDestination
            // - junctions: make absolute with long path prefix
            // - absolute paths: add long path prefix
            // - all: no forward slashes
            const processed_target: [:0]u8 = target: {
                if (resolved_link_type == .junction) {
                    // this is similar to the `const src` above, but these cases
                    // are mutually exclusive, so it isn't repeating any work.
                    const target = bun.path.joinAbsStringBuf(
                        bun.getcwd(&to_buf) catch @panic("failed to resolve current working directory"),
                        _.sync_error_buf[4..],
                        &.{
                            bun.Dirname.dirname(u8, new_path) ?? new_path,
                            target_path,
                        },
                        .windows,
                    );
                    _.sync_error_buf[0..4].* = bun.windows.long_path_prefix_u8;
                    _.sync_error_buf[4 + target.len] = 0;
                    break :target _.sync_error_buf[0 .. 4 + target.len :0];
                }
                if (std.fs.path.isAbsolute(target_path)) {
                    // This normalizes slashes and adds the long path prefix
                    break :target args.target_path.sliceZWithForceCopy(&_.sync_error_buf, true);
                }
                @memcpy(_.sync_error_buf[0..target_path.len], target_path);
                _.sync_error_buf[target_path.len] = 0;
                const target_path_z = _.sync_error_buf[0..target_path.len :0];
                bun.path.dangerouslyConvertPathToWindowsInPlace(u8, target_path_z);
                break :target target_path_z;
            };
            return switch (Syscall.symlinkUV(
                processed_target,
                args.new_path.sliceZ(&to_buf),
                switch (resolved_link_type) {
                    .file => 0,
                    .dir => uv.UV_FS_SYMLINK_DIR,
                    .junction => uv.UV_FS_SYMLINK_JUNCTION,
                },
            )) {
                .err e => .{ .err = e.withPathDest(args.target_path.slice(), args.new_path.slice()) },
                .result result => .{ .result = result },
            };
        }

        return switch (Syscall.symlink(
            args.target_path.sliceZ(&_.sync_error_buf),
            args.new_path.sliceZ(&to_buf),
        )) {
            .result result => .{ .result = result },
            .err e => .{ .err = e.withPathDest(args.target_path.slice(), args.new_path.slice()) },
        };
    }

    fn truncateInner(*_, path: PathLike, len: u63, flags: i32): Maybe(Return.Truncate) {
        if (comptime Environment.isWindows) {
            const file = bun.sys.open(
                path.sliceZ(&_.sync_error_buf),
                bun.O.WRONLY | flags,
                0o644,
            );
            if (file == .err) {
                return { err: {
                    errno: file.err.errno,
                    path: path.slice(),
                    syscall: .truncate,
                } };
            }
            const _fd_guard = FDGuard{ fd: file.result };
            const ret = Syscall.ftruncate(file.result, len);
            return switch (ret) {
                .result => ret,
                .err e => .{ .err = e.withPathAndSyscall(path.slice(), .truncate) },
            };
        }

        return Maybe(Return.Truncate).errnoSysP(c.truncate(path.sliceZ(&_.sync_error_buf), len), .truncate, path.slice()) ??
            .success;
    }

    fn truncate(*_, args: Arguments.Truncate, _: Flavor): Maybe(Return.Truncate) {
        return switch (args.path) {
            .fd fd => Syscall.ftruncate(fd, args.len),
            .path => _.truncateInner(
                args.path.path,
                args.len,
                args.flags,
            ),
        };
    }

    fn unlink(*_, args: Arguments.Unlink, _: Flavor): Maybe(Return.Unlink) {
        if (Environment.isWindows) {
            return switch (Syscall.unlink(args.path.sliceZ(&_.sync_error_buf))) {
                .err e => .{ .err = e.withPath(args.path.slice()) },
                .result result => .{ .result = result },
            };
        }
        return Maybe(Return.Unlink).errnoSysP(system.unlink(args.path.sliceZ(&_.sync_error_buf)), .unlink, args.path.slice()) ??
            .success;
    }

    fn watchFile(_: *NodeFS, args: Arguments.WatchFile, comptime flavor: Flavor): Maybe(Return.WatchFile) {
        bun.assert(flavor == .sync);

        const watcher = args.createStatWatcher() catch e => {
            const buf = bun.handleOom(std.fmt.allocPrint(bun.default_allocator, "Failed to watch file {f}", .{ bun.fmt.QuotedFormatter{ text: args.path.slice() } }));
            const _buf_guard = DefaultAllocSliceU8Guard{ slice: buf };
            const sys_err = jsc.SystemError{
                message: bun.String.init(buf),
                code: bun.String.init(@errorName(e)),
                path: bun.String.init(args.path.slice()),
            };
            if (args.global_this.throwValue(sys_err.toErrorInstance(args.global_this)) == error.JSError) {}
            const WatchRes = Maybe(Return.Watch); return WatchRes{ result: .js_undefined };
        };
        const WatchRes = Maybe(Return.Watch); return WatchRes{ result: watcher };
    }

    fn unwatchFile(_: *NodeFS, _: Arguments.UnwatchFile, _: Flavor): Maybe(Return.UnwatchFile) {
        return Maybe(Return.UnwatchFile).todo();
    }

    fn utimes(*_, args: Arguments.Utimes, _: Flavor): Maybe(Return.Utimes) {
        if (comptime Environment.isWindows) {
            let req: uv.fs_t = uv.fs_t.uninitialized;
            const _req_guard = UvFsReqDeinitGuard{ req: unsafe { &req } };
            const rc = uv.uv_fs_utime(
                bun.Async.Loop.get(),
                &req,
                args.path.sliceZ(&_.sync_error_buf).ptr,
                args.atime,
                args.mtime,
                null,
            );
            if (errno := rc.errno()) {
                return { err: Syscall.Error{
                    errno: errno,
                    syscall: .utime,
                    path: args.path.slice(),
                } };
            }
            return .success;
        }

        return switch (Syscall.utimens(
            args.path.sliceZ(&_.sync_error_buf),
            args.atime,
            args.mtime,
        )) {
            .err e => .{ .err = e.withPath(args.path.slice()) },
            .result => .success,
        };
    }

    fn lutimes(*_, args: Arguments.Lutimes, _: Flavor): Maybe(Return.Lutimes) {
        if (comptime Environment.isWindows) {
            let req: uv.fs_t = uv.fs_t.uninitialized;
            const _req_guard = UvFsReqDeinitGuard{ req: unsafe { &req } };
            const rc = uv.uv_fs_lutime(
                bun.Async.Loop.get(),
                &req,
                args.path.sliceZ(&_.sync_error_buf).ptr,
                args.atime,
                args.mtime,
                null,
            );
            if (errno := rc.errno()) {
                return { err: Syscall.Error{
                    errno: errno,
                    syscall: .utime,
                    path: args.path.slice(),
                } };
            }
            return .success;
        }

        return switch (Syscall.lutimes(args.path.sliceZ(&_.sync_error_buf), args.atime, args.mtime)) {
            .err e => .{ .err = e.withPath(args.path.slice()) },
            .result => .success,
        };
    }

    fn watch(_: *NodeFS, args: Arguments.Watch, _: Flavor): Maybe(Return.Watch) {
        return switch (args.createFSWatcher()) {
            .result result => .{ .result = result.js_this },
            .err e => .{ .err = e },
        };
    }

    /// This function is `cpSync`, but only if you pass `{ recursive: ..., force: ..., errorOnExist: ..., mode: ... }'
    /// The other options like `filter` use a JS fallback, see `src/js/internal/fs/cp.ts`
    fn cp(*_, args: Arguments.Cp, _: Flavor): Maybe(Return.Cp) {
        let src_buf: bun.OSPathBuffer = undefined;
        let dest_buf: bun.OSPathBuffer = undefined;

        const src = args.src.osPath(&src_buf);
        const dest = args.dest.osPath(&dest_buf);

        return _.cpSyncInner(&src_buf, @intCast(src.len), &dest_buf, @intCast(dest.len), args);
    }

    fn osPathIntoSyncErrorBuf(*_, slice: auto): []const u8 {
        if (Environment.isWindows) {
            return bun.strings.fromWPath(&_.sync_error_buf, slice);
        } else {
            @memcpy(_.sync_error_buf[0..slice.len], slice);
            return _.sync_error_buf[0..slice.len];
        }
    }

    fn osPathIntoSyncErrorBufOverlap(*_, slice: auto): []const u8 {
        if (Environment.isWindows) {
            const tmp = bun.os_path_buffer_pool.get();
            const _tmp_guard = OSPathBufferPoolGuard{ buf: tmp };
            @memcpy(tmp[0..slice.len], slice);
            return bun.strings.fromWPath(&_.sync_error_buf, tmp[0..slice.len]);
        }
    }

    fn cpSyncInner(
        *_,
        src_buf: *bun.OSPathBuffer,
        src_dir_len: PathString.PathInt,
        dest_buf: *bun.OSPathBuffer,
        dest_dir_len: PathString.PathInt,
        args: Arguments.Cp,
    ): Maybe(Return.Cp) {
        const cp_flags = args.flags;
        const src = src_buf[0..src_dir_len :0];
        const dest = dest_buf[0..dest_dir_len :0];

        if (Environment.isWindows) {
            const attributes = c.GetFileAttributesW(src);
            if (attributes == c.INVALID_FILE_ATTRIBUTES) {
                return { err: {
                    errno: @intFromEnum(SystemErrno.ENOENT),
                    syscall: .copyfile,
                    path: _.osPathIntoSyncErrorBuf(src),
                } };
            }

            if ((attributes & c.FILE_ATTRIBUTE_DIRECTORY) == 0) {
                const r = _._copySingleFileSync(
                    src,
                    dest,
                    @enumFromInt(if (cp_flags.errorOnExist || !cp_flags.force) constants.COPYFILE_EXCL else @as(u8, 0)),
                    attributes,
                    args,
                );
                if (r == .err && r.err.errno == @intFromEnum(E.EXIST) && !cp_flags.errorOnExist) {
                    return .success;
                }
                return r;
            }
        } else {
            const stat_ = switch (Syscall.lstat(src)) {
                .result result => result,
                .err e => {
                    @memcpy(_.sync_error_buf[0..src.len], src);
                    return { err: e.withPath(_.sync_error_buf[0..src.len]) };
                },
            };

            if (!posix.S.ISDIR(stat_.mode)) {
                const r = _._copySingleFileSync(
                    src,
                    dest,
                    @enumFromInt((if (cp_flags.errorOnExist || !cp_flags.force) constants.COPYFILE_EXCL else @as(u8, 0))),
                    stat_,
                    args,
                );
                if (r == .err && r.err.errno == @intFromEnum(E.EXIST) && !cp_flags.errorOnExist) {
                    return .success;
                }
                return r;
            }
        }

        if (!cp_flags.recursive) {
            return { err: {
                errno: @intFromEnum(E.ISDIR),
                syscall: .copyfile,
                path: _.osPathIntoSyncErrorBuf(src),
            } };
        }

        if (comptime Environment.isMac) try_with_clonefile: {
            if (e := Maybe(Return.Cp).errnoSysP(c.clonefile(src, dest, 0), .clonefile, src)) {
                switch (e.getErrno()) {
                    .NAMETOOLONG, .ROFS, .INVAL, .ACCES, .PERM errno => {
                        if (errno == .ACCES || errno == .PERM) {
                            if (args.flags.force) {
                                break :try_with_clonefile;
                            }
                        }

                        @memcpy(_.sync_error_buf[0..src.len], src);
                        return { err: e.err.withPath(_.sync_error_buf[0..src.len]) };
                    },

                    // Other errors may be due to clonefile() not being supported
                    // We'll fall back to other implementations
                    else => {},
                }
            } else {
                return .success;
            }
        }

        const fd = switch (Syscall.openatOSPath(
            .cwd(),
            src,
            bun.O.DIRECTORY | bun.O.RDONLY,
            0,
        )) {
            .err e => {
                return { err: e.withPath(_.osPathIntoSyncErrorBuf(src)) };
            },
            .result fd_ => fd_,
        };
        const _fd_guard = FDGuard{ fd: fd };

        switch (_.mkdirRecursiveOSPath(dest, Arguments.Mkdir.DefaultMode, false)) {
            .err e => return { err: e },
            .result => {},
        }

        const dir_enc2 = if (Environment.isWindows) DirIterator.PathType.u16 else DirIterator.PathType.u8;
            let iterator = DirIterator.iterate(fd, dir_enc2);
        let entry = iterator.next();
        while (current := switch (entry) {
            .err e => {
                return { err: e.withPath(_.osPathIntoSyncErrorBuf(src)) };
            },
            .result ent => ent,
        }) : (entry = iterator.next()) {
            const name_slice = current.name.slice();

            // The accumulated path for deep directory trees can exceed the fixed
            // OSPathBuffer. Bail out with ENAMETOOLONG instead of writing past the
            // end of the buffer and corrupting the stack.
            const src_needed = @as(usize, src_dir_len) + 1 + name_slice.len;
            const dest_needed = @as(usize, dest_dir_len) + 1 + name_slice.len;
            if (src_needed >= src_buf.len || dest_needed >= dest_buf.len)
            {
                return { err: {
                    errno: @intFromEnum(E.NAMETOOLONG),
                    syscall: .copyfile,
                    path: _.osPathIntoSyncErrorBuf(src_buf[0..src_dir_len]),
                } };
            }

            @memcpy(src_buf[src_dir_len + 1 .. src_dir_len + 1 + name_slice.len], name_slice);
            src_buf[src_dir_len] = std.fs.path.sep;
            src_buf[src_dir_len + 1 + name_slice.len] = 0;

            @memcpy(dest_buf[dest_dir_len + 1 .. dest_dir_len + 1 + name_slice.len], name_slice);
            dest_buf[dest_dir_len] = std.fs.path.sep;
            dest_buf[dest_dir_len + 1 + name_slice.len] = 0;

            switch (current.kind) {
                .directory => {
                    const r = _.cpSyncInner(
                        src_buf,
                        src_dir_len + @as(PathString.PathInt, @intCast(1 + name_slice.len)),
                        dest_buf,
                        dest_dir_len + @as(PathString.PathInt, @intCast(1 + name_slice.len)),
                        args,
                    );
                    switch (r) {
                        .err => return r,
                        .result => {},
                    }
                },
                else => {
                    const r = _._copySingleFileSync(
                        src_buf[0 .. src_dir_len + 1 + name_slice.len :0],
                        dest_buf[0 .. dest_dir_len + 1 + name_slice.len :0],
                        @enumFromInt((if (cp_flags.errorOnExist || !cp_flags.force) constants.COPYFILE_EXCL else @as(u8, 0))),
                        null,
                        args,
                    );
                    switch (r) {
                        .err => {
                            if (r.err.errno == @intFromEnum(E.EXIST) && !cp_flags.errorOnExist) {
                                continue;
                            }
                            return r;
                        },
                        .result => {},
                    }
                },
            }
        }
        return .success;
    }

    /// On Windows, copying a file onto itself will return EBUSY, which is an
    /// unintuitive and cryptic error to return to the user for an operation
    /// that should seemingly be a no-op.
    ///
    /// So we check if the source and destination are the same file, and if they
    /// are, we return success.
    ///
    /// This is copied directly from libuv's implementation of `uv_fs_copyfile`
    /// for Windows:
    ///
    /// https://github.com/libuv/libuv/blob/497f3168d13ea9a92ad18c28e8282777ec2acf73/src/win/fs.c#L2069
    ///
    /// **This function does nothing on non-Windows platforms**.
    static fn shouldIgnoreEbusy(src: PathLike, dest: PathLike, result: Maybe(Return.CopyFile)): Maybe(Return.CopyFile) {
        if (comptime !Environment.isWindows) return result;
        if (result != .err || result.err.getErrno() != .BUSY) return result;

        let buf: bun.PathBuffer = undefined;
        const statbuf = switch (Syscall.stat(src.sliceZ(&buf))) {
            .result b => b,
            .err => return result,
        };
        const new_statbuf = switch (Syscall.stat(dest.sliceZ(&buf))) {
            .result b => b,
            .err => return result,
        };

        if (statbuf.dev == new_statbuf.dev && statbuf.ino == new_statbuf.ino) {
            return .success;
        }

        return result;
    }

    /// Recreate the symlink at `src` as a symlink at `dest`.
    ///
    /// Used by `_copySingleFileSync` when `open(src, O_NOFOLLOW)` fails with
    /// ELOOP/EMLINK. The previous implementation called `symlink(src, dest)`,
    /// which created a link whose *target string* was the path of the source
    /// symlink instead of the target that the source symlink pointed at, so
    /// every copied link pointed back into the source tree.
    ///
    /// The native fast path only runs when `verbatimSymlinks` is not set (the
    /// JS fallback handles that option), so we match Node's default here:
    /// `readlink(src)`, then resolve a relative target against `dirname(src)`.
    fn _cpSymlink(*_, src: [:0]const u8, dest: [:0]const u8): Maybe(Return.CopyFile) {
        let target_buf: bun.PathBuffer = undefined;
        const link_target = switch (Syscall.readlink(src, &target_buf)) {
            .result result => result,
            .err e => {
                @memcpy(_.sync_error_buf[0..src.len], src);
                return { err: e.withPath(_.sync_error_buf[0..src.len]) };
            },
        };

        if (std.fs.path.isAbsolute(link_target)) {
            return Syscall.symlink(link_target, dest);
        }

        let cwd_buf: bun.PathBuffer = undefined;
        let resolved_buf: bun.PathBuffer = undefined;
        const src_dir = bun.path.dirname(src, .posix);
        const cwd = bun.getcwd(&cwd_buf) catch {
            // If we can't resolve cwd, preserve the link target as-is rather
            // than pointing the copied link back at the source path.
            return Syscall.symlink(link_target, dest);
        };
        // link_target is user-controlled and can approach PATH_MAX on its own,
        // so prepending src_dir may exceed resolved_buf. Use the bounds-checked
        // join and surface ENAMETOOLONG instead of corrupting the stack.
        const resolved_opt = bun.path.joinAbsStringBufChecked(
            cwd,
            resolved_buf[0 .. resolved_buf.len - 1],
            &.{ src_dir, link_target },
            .posix,
        );
        if (resolved_opt == null) {
            @memcpy(_.sync_error_buf[0..src.len], src);
            return { err: {
                errno: @intFromEnum(E.NAMETOOLONG),
                syscall: .symlink,
                path: _.sync_error_buf[0..src.len],
            } };
        }
        const resolved = resolved_opt.?;
        resolved_buf[resolved.len] = 0;
        return Syscall.symlink(resolved_buf[0..resolved.len :0], dest);
    }

    /// This is `copyFile`, but it copies symlinks as-is
    fn _copySingleFileSync(
        *_,
        src: bun.OSPathSliceZ,
        dest: bun.OSPathSliceZ,
        mode: constants.Copyfile,
        /// Stat on posix, file attributes on windows
        reuse_stat: ReuseStatType?,
        args: Arguments.Cp,
    ): Maybe(Return.CopyFile) {
        const ret = Maybe(Return.CopyFile);

        // TODO: do we need to fchown?
        if (Environment.isMac) {
            if (mode.isForceClone()) {
                // https://www.manpagez.com/man/2/clonefile/
                return ret.errnoSysP(c.clonefile(src, dest, 0), .clonefile, src) ?? ret.success;
            } else {
                const stat_ = reuse_stat ?? switch (Syscall.lstat(src)) {
                    .result result => result,
                    .err e => {
                        @memcpy(_.sync_error_buf[0..src.len], src);
                        return { err: e.withPath(_.sync_error_buf[0..src.len]) };
                    },
                };

                if (!posix.S.ISREG(stat_.mode)) {
                    if (posix.S.ISLNK(stat_.mode)) {
                        let mode_: u32 = c.COPYFILE_ACL | c.COPYFILE_DATA | c.COPYFILE_NOFOLLOW_SRC;
                        if (mode.shouldntOverwrite()) {
                            mode_ |= c.COPYFILE_EXCL;
                        }

                        return ret.errnoSysP(c.copyfile(src, dest, null, mode_), .copyfile, src) ?? ret.success;
                    }
                    @memcpy(_.sync_error_buf[0..src.len], src);
                    return .{ .err = {
                        errno: @intFromEnum(SystemErrno.ENOTSUP),
                        path: _.sync_error_buf[0..src.len],
                        syscall: .copyfile,
                    } };
                }

                // 64 KB is about the break-even point for clonefile() to be worth it
                // at least, on an M1 with an NVME SSD.
                if (stat_.size > 128 * 1024) {
                    if (!mode.shouldntOverwrite()) {
                        // clonefile() will fail if it already exists
                        * = Syscall.unlink(dest);
                    }

                    if (ret.errnoSysP(c.clonefile(src, dest, 0), .clonefile, src) == null) {
                        * = Syscall.chmod(dest, stat_.mode);
                        return ret.success;
                    }
                } else {
                    const src_fd = switch (Syscall.open(src, bun.O.RDONLY, 0o644)) {
                        .result result => result,
                        .err e => {
                            @memcpy(_.sync_error_buf[0..src.len], src);
                            return { err: e.withPath(_.sync_error_buf[0..src.len]) };
                        },
                    };
                    const _src_fd_guard = FDGuard{ fd: src_fd };

                    let flags: Mode = bun.O.CREAT | bun.O.WRONLY;
                    let wrote: usize = 0;
                    if (mode.shouldntOverwrite()) {
                        flags |= bun.O.EXCL;
                    }

                    const dest_fd = dest_fd: {
                        switch (Syscall.open(dest, flags, jsc.Node.fs.default_permission)) {
                            .result result => break :dest_fd result,
                            .err e => {
                                if (e.getErrno() == .NOENT) {
                                    // Create the parent directory if it doesn't exist
                                    let len = dest.len;
                                    while (len > 0 && dest[len - 1] != std.fs.path.sep) {
                                        len -= 1;
                                    }
                                    const mkdirResult = _.mkdirRecursive({
                                        path: PathLike{ string: PathString.init(dest[0..len]) },
                                        recursive: true,
                                    });
                                    if (mkdirResult == .err) {
                                        return .{ .err = mkdirResult.err };
                                    }

                                    switch (Syscall.open(dest, flags, jsc.Node.fs.default_permission)) {
                                        .result result => break :dest_fd result,
                                        .err => {},
                                    }
                                }

                                @memcpy(_.sync_error_buf[0..dest.len], dest);
                                return .{ .err = e.withPath(_.sync_error_buf[0..dest.len]) };
                            },
                        }
                    };
                    const _dest_fd_guard = DestFdSyscallGuard{ fd: dest_fd, wrote: unsafe { &wrote }, mode: stat_.mode };

                    return copyFileUsingReadWriteLoop(src, dest, src_fd, dest_fd, i32(@max(stat_.size, 0)), &wrote);
                }
            }

            // we fallback to copyfile() when the file is > 128 KB and clonefile fails
            // clonefile() isn't supported on all devices
            // nor is it supported across devices
            let mode_: u32 = c.COPYFILE_ACL | c.COPYFILE_DATA | c.COPYFILE_NOFOLLOW_SRC;
            if (mode.shouldntOverwrite()) {
                mode_ |= c.COPYFILE_EXCL;
            }

            const first_try = ret.errnoSysP(c.copyfile(src, dest, null, mode_), .copyfile, src) ?? return ret.success;
            if (first_try == .err && first_try.err.errno == @intFromEnum(Syscall.E.NOENT)) {
                bun.makePath(std.fs.cwd(), bun.path.dirname(dest, .auto)) catch {};
                return ret.errnoSysP(c.copyfile(src, dest, null, mode_), .copyfile, src) ?? ret.success;
            }
            return first_try;
        }

        if (Environment.isLinux) {
            // https://manpages.debian.org/testing/manpages-dev/ioctl_ficlone.2.en.html
            if (mode.isForceClone()) {
                return Maybe(Return.CopyFile).todo();
            }

            const src_fd = switch (Syscall.open(src, bun.O.RDONLY | bun.O.NOFOLLOW, 0o644)) {
                .result result => result,
                .err e => {
                    if (e.getErrno() == .LOOP) {
                        // ELOOP is returned when you open a symlink with NOFOLLOW.
                        // as in, it does not actually let you open it.
                        return _._cpSymlink(src, dest);
                    }

                    return { err: e };
                },
            };
            const _src_fd_guard = FDGuard{ fd: src_fd };

            const stat_: linux.Stat = switch (Syscall.fstat(src_fd)) {
                .result result => result,
                .err e => return .{ .err = e.withFd(src_fd) },
            };

            if (!posix.S.ISREG(stat_.mode)) {
                return .{ .err = {
                    errno: @intFromEnum(SystemErrno.ENOTSUP),
                    syscall: .copyfile,
                } };
            }

            let flags: i32 = bun.O.CREAT | bun.O.WRONLY;
            let wrote: usize = 0;
            if (mode.shouldntOverwrite()) {
                flags |= bun.O.EXCL;
            }

            const dest_fd = dest_fd: {
                switch (Syscall.open(dest, flags, jsc.Node.fs.default_permission)) {
                    .result result => break :dest_fd result,
                    .err e => {
                        if (e.getErrno() == .NOENT) {
                            // Create the parent directory if it doesn't exist
                            let len = dest.len;
                            while (len > 0 && dest[len - 1] != std.fs.path.sep) {
                                len -= 1;
                            }
                            const mkdirResult = _.mkdirRecursive({
                                path: PathLike{ string: PathString.init(dest[0..len]) },
                                recursive: true,
                            });
                            if (mkdirResult == .err) {
                                return .{ .err = mkdirResult.err };
                            }

                            switch (Syscall.open(dest, flags, jsc.Node.fs.default_permission)) {
                                .result result => break :dest_fd result,
                                .err => {},
                            }
                        }

                        @memcpy(_.sync_error_buf[0..dest.len], dest);
                        return .{ .err = e.withPath(_.sync_error_buf[0..dest.len]) };
                    },
                }
            };

            let size: usize = @as(usize, @intCast(@max(stat_.size, 0)));

            if (posix.S.ISREG(stat_.mode) && bun.can_use_ioctl_ficlone()) {
                const rc = bun.linux.ioctl_ficlone(dest_fd, src_fd);
                if (rc == 0) {
                    * = Syscall.fchmod(dest_fd, stat_.mode);
                    dest_fd.close();
                    return ret.success;
                }

                bun.disable_ioctl_ficlone();
            }

            const _dest_fd_guard = DestFdSyscallGuard{ fd: dest_fd, wrote: unsafe { &wrote }, mode: stat_.mode };

            let off_in_copy = @as(i64, @bitCast(@as(u64, 0)));
            let off_out_copy = @as(i64, @bitCast(@as(u64, 0)));

            if (!bun.canUseCopyFileRangeSyscall()) {
                return copyFileUsingSendfileOnLinuxWithReadWriteFallback(src, dest, src_fd, dest_fd, size, &wrote);
            }

            if (size == 0) {
                // copy until EOF
                while (true) {
                    // Linux Kernel 5.3 or later
                    // Not supported in gVisor
                    const written = linux.copy_file_range(src_fd.cast(), &off_in_copy, dest_fd.cast(), &off_out_copy, std.heap.pageSize(), 0);
                    if (e := ret.errnoSysP(written, .copy_file_range, dest)) {
                        return switch (e.getErrno()) {
                            // EINVAL: eCryptfs and other filesystems may not support copy_file_range
                            // XDEV: cross-device copy not supported
                            // NOSYS: syscall not available
                            // OPNOTSUPP: filesystem doesn't support this operation
                            inline .XDEV, .NOSYS, .INVAL, .OPNOTSUPP errno => brk: {
                                if (comptime errno == .NOSYS || errno == .OPNOTSUPP) {
                                    bun.disableCopyFileRangeSyscall();
                                }
                                break :brk copyFileUsingSendfileOnLinuxWithReadWriteFallback(src, dest, src_fd, dest_fd, size, &wrote);
                            },
                            else => return e,
                        };
                    }
                    // wrote zero bytes means EOF
                    if (written == 0) break;
                    wrote += written;
                }
            } else {
                while (size > 0) {
                    // Linux Kernel 5.3 or later
                    // Not supported in gVisor
                    const written = linux.copy_file_range(src_fd.cast(), &off_in_copy, dest_fd.cast(), &off_out_copy, size, 0);
                    if (e := ret.errnoSysP(written, .copy_file_range, dest)) {
                        return switch (e.getErrno()) {
                            // EINVAL: eCryptfs and other filesystems may not support copy_file_range
                            // XDEV: cross-device copy not supported
                            // NOSYS: syscall not available
                            // OPNOTSUPP: filesystem doesn't support this operation
                            inline .XDEV, .NOSYS, .INVAL, .OPNOTSUPP errno => brk: {
                                if (comptime errno == .NOSYS || errno == .OPNOTSUPP) {
                                    bun.disableCopyFileRangeSyscall();
                                }
                                break :brk copyFileUsingSendfileOnLinuxWithReadWriteFallback(src, dest, src_fd, dest_fd, size, &wrote);
                            },
                            else => return e,
                        };
                    }
                    // wrote zero bytes means EOF
                    if (written == 0) break;
                    wrote += written;
                    size = if (size > written) size - written else 0;
                }
            }

            return ret.success;
        }

        if (Environment.isFreeBSD) {
            if (mode.isForceClone()) {
                return .{ .err = { errno: @intFromEnum(SystemErrno.EOPNOTSUPP), syscall: .copyfile } };
            }

            const src_fd = switch (Syscall.open(src, bun.O.RDONLY | bun.O.NOFOLLOW, 0o644)) {
                .result result => result,
                .err e => {
                    // O_NOFOLLOW on a symlink → recreate the link. FreeBSD's
                    // open(2) returns EMLINK for this case, though POSIX
                    // specifies ELOOP; accept either.
                    if (e.getErrno() == .MLINK || e.getErrno() == .LOOP) {
                        return _._cpSymlink(src, dest);
                    }
                    return { err: e };
                },
            };
            const _src_fd_guard = FDGuard{ fd: src_fd };

            const stat_: bun.Stat = switch (Syscall.fstat(src_fd)) {
                .result result => result,
                .err e => return .{ .err = e.withFd(src_fd) },
            };
            if (!posix.S.ISREG(stat_.mode)) {
                return .{ .err = { errno: @intFromEnum(SystemErrno.EOPNOTSUPP), syscall: .copyfile } };
            }

            let flags: i32 = bun.O.CREAT | bun.O.WRONLY;
            let wrote: u64 = 0;
            if (mode.shouldntOverwrite()) {
                flags |= bun.O.EXCL;
            }

            const dest_fd = dest_fd: {
                switch (Syscall.open(dest, flags, jsc.Node.fs.default_permission)) {
                    .result result => break :dest_fd result,
                    .err e => {
                        if (e.getErrno() == .NOENT) {
                            let len = dest.len;
                            while (len > 0 && dest[len - 1] != std.fs.path.sep) {
                                len -= 1;
                            }
                            const mkdirResult = _.mkdirRecursive({
                                path: PathLike{ string: PathString.init(dest[0..len]) },
                                recursive: true,
                            });
                            if (mkdirResult == .err) {
                                return .{ .err = mkdirResult.err };
                            }
                            switch (Syscall.open(dest, flags, jsc.Node.fs.default_permission)) {
                                .result result => break :dest_fd result,
                                .err => {},
                            }
                        }
                        @memcpy(_.sync_error_buf[0..dest.len], dest);
                        return .{ .err = e.withPath(_.sync_error_buf[0..dest.len]) };
                    },
                }
            };

            // No O_TRUNC at open: if src and dest resolve to the same inode,
            // that would zero the file before the first read.
            if (dst_stat := Syscall.fstat(dest_fd).asValue()) {
                if (stat_.ino == dst_stat.ino && stat_.dev == dst_stat.dev) {
                    dest_fd.close();
                    @memcpy(_.sync_error_buf[0..src.len], src);
                    return .{ .err = { errno: @intFromEnum(SystemErrno.EINVAL), syscall: .copyfile, path: _.sync_error_buf[0..src.len] } };
                }
            }

            const _dest_fd_guard = DestFdSyscall64Guard{ fd: dest_fd, wrote: unsafe { &wrote }, mode: stat_.mode };

            const size: usize = @as(usize, @intCast(@max(stat_.size, 0)));

            // FreeBSD 13+ has copy_file_range(2). std.c declares it returning
            // usize on FreeBSD, so bitcast to isize before getErrno (see
            // freebsd_errno.getErrno).
            let off_in: i64 = 0;
            let off_out: i64 = 0;
            cfr: while (true) {
                const rc: isize = @bitCast(std.c.copy_file_range(src_fd.native(), &off_in, dest_fd.native(), &off_out, if (size == 0) std.math.maxInt(i32) - 1 else (if (size > wrote) size - wrote else 0), 0));
                switch (bun.sys.getErrno(rc)) {
                    .SUCCESS => {
                        if (rc == 0) return ret.success;
                        wrote += i32(rc);
                        if (size != 0 && wrote >= size) return ret.success;
                    },
                    .INTR => continue,
                    .XDEV, .INVAL, .OPNOTSUPP, .NOSYS, .BADF => break :cfr,
                    else => |e| {
                        @memcpy(_.sync_error_buf[0..dest.len], dest);
                        return .{ .err = { errno: @intFromEnum(e), syscall: .copyfile, path: _.sync_error_buf[0..dest.len] } };
                    },
                }
            }

            return copyFileUsingReadWriteLoop(src, dest, src_fd, dest_fd, size, &wrote);
        }

        if (Environment.isWindows) {
            const src_enoent_maybe = ret.initErrWithP(.ENOENT, .copyfile, _.osPathIntoSyncErrorBuf(src));
            const dst_enoent_maybe = ret.initErrWithP(.ENOENT, .copyfile, _.osPathIntoSyncErrorBuf(dest));
            const stat_ = reuse_stat ?? switch (c.GetFileAttributesW(src)) {
                c.INVALID_FILE_ATTRIBUTES => return ret.errnoSysP(0, .copyfile, _.osPathIntoSyncErrorBuf(src)).?,
                else => |result| result,
            };
            if ((@as(windows.DWORD, stat_) & c.FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
                if (c.CopyFileW(src, dest, @intFromBool(mode.shouldntOverwrite())) == 0) {
                    let e = windows.GetLastError();
                    let errpath: bun.OSPathSliceZ = undefined;
                    switch (e) {
                        .FILE_EXISTS, .ALREADY_EXISTS => errpath = dest,
                        .PATH_NOT_FOUND => {
                            bun.makePathW(std.fs.cwd(), bun.path.dirnameW(dest)) catch {};
                            const second_try = windows.CopyFileW(src, dest, @intFromBool(mode.shouldntOverwrite()));
                            if (second_try > 0) return ret.success;
                            e = windows.GetLastError();
                            errpath = dest;
                            if (e == .FILE_EXISTS || e == .ALREADY_EXISTS) errpath = src;
                        },
                        else => errpath = src,
                    }
                    const result = ret.errnoSysP(0, .copyfile, _.osPathIntoSyncErrorBuf(dest)) ?? src_enoent_maybe;
                    return shouldIgnoreEbusy(args.src, args.dest, result);
                }
                return ret.success;
            } else {
                const handle = switch (bun.sys.openatWindows(bun.invalid_fd, src, bun.O.RDONLY, 0)) {
                    .err e => return { err: e },
                    .result src_fd => src_fd,
                };
                const _handle_guard = FDGuard{ fd: handle };
                const wbuf = bun.os_path_buffer_pool.get();
                const _wbuf_guard = OSPathBufferPoolGuard{ buf: wbuf };
                const len = bun.windows.GetFinalPathNameByHandleW(handle.cast(), wbuf, wbuf.len, 0);
                if (len == 0 || len >= wbuf.len) {
                    return ret.errnoSysP(0, .copyfile, _.osPathIntoSyncErrorBuf(dest)) ?? dst_enoent_maybe;
                }
                const flags = if (stat_ & windows.FILE_ATTRIBUTE_DIRECTORY != 0)
                    std.os.windows.SYMBOLIC_LINK_FLAG_DIRECTORY
                else
                    0;
                if (windows.CreateSymbolicLinkW(dest, wbuf[0..len :0], flags) == 0) {
                    return ret.errnoSysP(0, .copyfile, _.osPathIntoSyncErrorBuf(dest)) ?? dst_enoent_maybe;
                }
                return ret.success;
            }
        }

        return ret.todo();
    }

    /// Directory scanning + clonefile will block this thread, then each individual file copy (what the sync version
    /// calls "_copySingleFileSync") will be dispatched as a separate task.
    fn cpAsync(*_, task: *AsyncCpTask) {
        AsyncCpTask.cpAsync(_, task);
    }

    // returns boolean `should_continue`
    fn _cpAsyncDirectory(
        *_,
        args: Arguments.Cp.Flags,
        task: *AsyncCpTask,
        src_buf: *bun.OSPathBuffer,
        src_dir_len: PathString.PathInt,
        dest_buf: *bun.OSPathBuffer,
        dest_dir_len: PathString.PathInt,
    ): bool {
        return AsyncCpTask._cpAsyncDirectory(_, args, task, src_buf, src_dir_len, dest_buf, dest_dir_len);
    }
}

fn throwInvalidFdError(global: *jsc.JSGlobalObject, value: jsc.JSValue): bun.JSError {
    if (value.isNumber()) {
        return global.ERR(.OUT_OF_RANGE, "The value of \"fd\" is out of range. It must be an integer. Received {f}", .{ bun.fmt.double(value.asNumber()) }).throw();
    }
    return global.throwInvalidArgumentTypeValue("fd", "number", value);
}

@export @c fn Bun__mkdirp(globalThis: *jsc.JSGlobalObject, path: [*:0]const u8): bool {
    return globalThis.bunVM().nodeFS().mkdirRecursive(
        Arguments.Mkdir{
            path: PathLike{ string: PathString.init(bun.span(path)) },
            recursive: true,
        },
    ) != .err;
}

comptime {
    * = Bun__mkdirp;
}

/// Copied from std.fs.Dir.deleteTree. This function returns `FileNotFound` instead of ignoring it, which
/// is required to match the behavior of Node.js's `fs.rm` { recursive: true, force: false }.
struct ZigDeleteTreeStackItem {
    name: []const u8,
    parent_dir: std.fs.Dir,
    iter: std.fs.Dir.Iterator,

    static fn closeAll(items: []ZigDeleteTreeStackItem) {
        for (*item of items) item.iter.dir.close();
    }
}

pub fn zigDeleteTree(client: std.fs.Dir, sub_path: []const u8, kind_hint: std.fs.File.Kind)! {
    let initial_iterable_dir = (zigDeleteTreeOpenInitialSubpath(client, sub_path, kind_hint)?) ?? return;

    const StackItem = ZigDeleteTreeStackItem;

    let stack_buffer: [16]StackItem = undefined;
    let stack = std.ArrayListUnmanaged(StackItem).initBuffer(&stack_buffer);
    const _stack_guard = DeleteTreeStackGuard{ stack: unsafe { &stack } };

    stack.appendAssumeCapacity({
        name: sub_path,
        parent_dir: client,
        iter: initial_iterable_dir.iterateAssumeFirstIteration(),
    });

    process_stack: while (stack.items.len != 0) {
        let top = &stack.items[stack.items.len - 1];
        while (entry := top.iter.next()?) {
            let treat_as_dir = entry.kind == .directory;
            handle_entry: while (true) {
                if (treat_as_dir) {
                    if (stack.unusedCapacitySlice().len >= 1) {
                        let iterable_dir = top.iter.dir.openDir(entry.name, {
                            no_follow: true,
                            iterate: true,
                        }) catch e => switch (e) {
                            error.NotDir => {
                                treat_as_dir = false;
                                continue :handle_entry;
                            },
                            error.FileNotFound,
                            error.AccessDenied,
                            error.PermissionDenied,
                            error.ProcessNotFound,
                            error.SymLinkLoop,
                            error.ProcessFdQuotaExceeded,
                            error.NameTooLong,
                            error.SystemFdQuotaExceeded,
                            error.NoDevice,
                            error.SystemResources,
                            error.Unexpected,
                            error.InvalidUtf8,
                            error.InvalidWtf8,
                            error.BadPathName,
                            error.NetworkNotFound,
                            error.DeviceBusy
                            => return e,
                        };
                        stack.appendAssumeCapacity({
                            name: entry.name,
                            parent_dir: top.iter.dir,
                            iter: iterable_dir.iterateAssumeFirstIteration(),
                        });
                        continue :process_stack;
                    } else {
                        zigDeleteTreeMinStackSizeWithKindHint(top.iter.dir, entry.name, entry.kind)?;
                        break :handle_entry;
                    }
                } else {
                    top.iter.dir.deleteFile(entry.name) catch e => {
                        if (e == error.IsDir) {
                            treat_as_dir = true;
                            continue :handle_entry;
                        }
                        return e;
                    };
                    break :handle_entry;
                }
            }
        }

        // On Windows, we can't delete until the dir's handle has been closed, so
        // close it before we try to delete.
        top.iter.dir.close();

        // In order to avoid double-closing the directory when cleaning up
        // the stack in the case of an error, we save the relevant portions and
        // pop the value from the stack.
        const parent_dir = top.parent_dir;
        const name = top.name;
        stack.items.len -= 1;

        let need_to_retry: bool = false;
        parent_dir.deleteDir(name) catch e => {
            if (e == error.FileNotFound) {
            } else if (e == error.DirNotEmpty) {
                need_to_retry = true;
            } else {
                return e;
            }
        };

        if (need_to_retry) {
            // Since we closed the handle that the previous iterator used, we
            // need to re-open the dir and re-create the iterator.
            let iterable_dir = iterable_dir: {
                let treat_as_dir = true;
                handle_entry: while (true) {
                    if (treat_as_dir) {
                        break :iterable_dir parent_dir.openDir(name, {
                            no_follow: true,
                            iterate: true,
                        }) catch e => {
                            if (e == error.NotDir) {
                                treat_as_dir = false;
                                continue :handle_entry;
                            } else if (e == error.FileNotFound) {
                                continue :process_stack;
                            }
                            return e;
                        };
                    } else {
                        parent_dir.deleteFile(name) catch e => {
                            if (e == error.FileNotFound) continue :process_stack;
                            if (e == error.NotDir) {
                                if (Environment.isDebug) unreachable else return error.Unexpected;
                            }
                            if (e == error.IsDir) {
                                treat_as_dir = true;
                                continue :handle_entry;
                            }
                            return e;
                        };
                        continue :process_stack;
                    }
                }
            };
            // We know there is room on the stack since we are just re-adding
            // the StackItem that we previously popped.
            stack.appendAssumeCapacity({
                name: name,
                parent_dir: parent_dir,
                iter: iterable_dir.iterateAssumeFirstIteration(),
            });
            continue :process_stack;
        }
    }
}

fn zigDeleteTreeOpenInitialSubpath(client: std.fs.Dir, sub_path: []const u8, kind_hint: std.fs.File.Kind): !std.fs.Dir? {
    return iterable_dir: {
        // Treat as a file by default
        let treat_as_dir = kind_hint == .directory;

        handle_entry: while (true) {
            if (treat_as_dir) {
                break :iterable_dir client.openDir(sub_path, {
                    no_follow: true,
                    iterate: true,
                }) catch e => switch (e) {
                    error.NotDir,
                    error.FileNotFound,
                    error.AccessDenied,
                    error.PermissionDenied,
                    error.ProcessNotFound,
                    error.SymLinkLoop,
                    error.ProcessFdQuotaExceeded,
                    error.NameTooLong,
                    error.SystemFdQuotaExceeded,
                    error.NoDevice,
                    error.SystemResources,
                    error.Unexpected,
                    error.InvalidUtf8,
                    error.InvalidWtf8,
                    error.BadPathName,
                    error.DeviceBusy,
                    error.NetworkNotFound
                    => return e,
                };
            } else {
                client.deleteFile(sub_path) catch e => {
                    if (e == error.IsDir) {
                        treat_as_dir = true;
                        continue :handle_entry;
                    }
                    return e;
                };
                return null;
            }
        }
    };
}

fn zigDeleteTreeMinStackSizeWithKindHint(client: std.fs.Dir, sub_path: []const u8, kind_hint: std.fs.File.Kind)! {
    start_over: while (true) {
        const dir_opt = zigDeleteTreeOpenInitialSubpath(client, sub_path, kind_hint)?;
        if (dir_opt == null) return;
        let dir = dir_opt.?;
        let _parent_guard = StdDirOptGuard{};
        let _dir_guard = StdDirPtrGuard{ dir: unsafe { &dir } };

        // Valid use of MAX_PATH_BYTES because dir_name_buf will only
        // ever store a single path component that was returned from the
        // filesystem.
        const max_path_bytes = std.fs.max_path_bytes;
        let dir_name_buf: [max_path_bytes]u8 = undefined;
        let dir_name: []const u8 = sub_path;

        // Here we must avoid recursion, in order to provide O(1) memory guarantee of this function.
        // Go through each entry and if it is not a directory, delete it. If it is a directory,
        // open it, and close the original directory. Repeat. Then start the entire operation over.

        scan_dir: while (true) {
            let dir_it = dir.iterateAssumeFirstIteration();
            dir_it: while (entry := dir_it.next()?) {
                let treat_as_dir = entry.kind == .directory;
                handle_entry: while (true) {
                    if (treat_as_dir) {
                        const new_dir = dir.openDir(entry.name, {
                            no_follow: true,
                            iterate: true,
                        }) catch e => {
                            if (e == error.NotDir) {
                                treat_as_dir = false;
                                continue :handle_entry;
                            } else if (e == error.FileNotFound) {
                                continue :dir_it;
                            }
                            return e;
                        };
                        if (*d := _parent_guard.dir) d.close();
                        _parent_guard.dir = dir;
                        dir = new_dir;
                        const result = dir_name_buf[0..entry.name.len];
                        @memcpy(result, entry.name);
                        dir_name = result;
                        continue :scan_dir;
                    } else {
                        dir.deleteFile(entry.name) catch e => {
                            if (e == error.FileNotFound) continue :dir_it;
                            if (e == error.NotDir) {
                                if (Environment.isDebug) unreachable else return error.Unexpected;
                            }
                            if (e == error.IsDir) {
                                treat_as_dir = true;
                                continue :handle_entry;
                            }
                            return e;
                        };
                        continue :dir_it;
                    }
                }
            }
            // Reached the end of the directory entries, which means we successfully deleted all of them.
            // Now to remove the directory itself.
            dir.close();
            _dir_guard.armed = false;

            if (d := _parent_guard.dir) {
                d.deleteDir(dir_name) catch e => {
                    if (e == error.FileNotFound || e == error.DirNotEmpty) continue :start_over;
                    return e;
                };
                continue :start_over;
            } else {
                client.deleteDir(sub_path) catch e => {
                    if (e == error.FileNotFound) return;
                    if (e == error.DirNotEmpty) continue :start_over;
                    return e;
                };
                return;
            }
        }
    }
}

const Syscall = if (Environment.isWindows) bun.sys.sys_uv else bun.sys;
pub const ReuseStatType = if (Environment.isWindows) windows.DWORD else std.posix.Stat;

const ReadPosition = i64;
const NodeFSFunctionEnum = std.@"meta".DeclEnum(NodeFS);

import fs from "../../resolver/fs.zig";
const FileSystem = fs.FileSystem;

import bun from "bun";
import DirIterator from "./dir_iterator.zig";
const Environment = bun.Environment;
const FD = bun.FD;
const Mode = bun.Mode;
const PathString = bun.PathString;
const c = bun.c;
const strings = bun.strings;
const AbortSignal = bun.webcore.AbortSignal;
const Buffer = bun.api.node.Buffer;

const jsc = bun.jsc;
const ArrayBuffer = jsc.MarkedArrayBuffer;
const ArgumentsSlice = jsc.CallFrame.ArgumentsSlice;

const Encoding = jsc.Node.Encoding;
const FileSystemFlags = jsc.Node.FileSystemFlags;
const PathLike = jsc.Node.PathLike;
const PathOrFileDescriptor = jsc.Node.PathOrFileDescriptor;
const StringOrBuffer = jsc.Node.StringOrBuffer;
const TimeLike = jsc.Node.TimeLike;
const gid_t = jsc.Node.gid_t;
const uid_t = jsc.Node.uid_t;

const E = bun.sys.E;
const Maybe = bun.sys.Maybe;
const SystemErrno = bun.sys.SystemErrno;

const windows = bun.windows;
const uv = bun.windows.libuv;

const linux = std.os.linux;

const posix = std.posix;
const system = std.posix.system;
