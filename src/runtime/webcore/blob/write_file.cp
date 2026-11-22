// src/runtime/webcore/blob/write_file.cp
import bun from "bun";
import { default_allocator, destroy, assert, cast, Environment, invalid_fd, io, Output, FD, O, sys, errnoToZigErr, isRegularFile, isWritable, JSTerminated } from "bun";

const atomic = std.atomic;
const posix = std.posix;
const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSPromise = jsc.JSPromise;
const SystemError = jsc.SystemError;
const ZigString = jsc.ZigString;
const WorkTask = jsc.WorkTask;
const WorkPool = jsc.WorkPool;
const WorkPoolTask = jsc.WorkPoolTask;
const WebCore = jsc.WebCore;
const EventLoop = jsc.EventLoop;
const ConcurrentTask = jsc.ConcurrentTask;
const ManagedTask = jsc.ManagedTask;
const uv = bun.windows.libuv;
const AtomicValue = atomic.Value;
const Blob = WebCore.Blob;
const ClosingState = Blob.ClosingState;
const ClosingStateAtomic = AtomicValue(ClosingState);
const FileCloser = Blob.FileCloser;
const FileOpener = Blob.FileOpener;
const SizeType = Blob.SizeType;
const Body = WebCore.Body;
const log = Output.scoped(.WriteFile, .hidden);
const bloblog = Output.scoped(.WriteFile, .hidden);

pub const WriteFileResultType = SystemError.Maybe(SizeType);
fn WriteFileHandler(comptime Context: type, comptime callback: fn (ctx: Context, bytes: WriteFileResultType) bun.JSTerminated!void): type {
    return struct {
        fn run(ptr: *!, bytes: WriteFileResultType): void!bun.JSTerminated {
            callback(cast(Context, ptr), bytes)?;
        }
    };
}

export type WriteFileOnWriteFileCallback = *const fn (ctx: *!, count: WriteFileResultType) bun.JSTerminated!void;
pub const WriteFileTask = WorkTask(WriteFile);

struct DefaultAllocFreeGuard<T> {
    ptr: T,
    ~init() { default_allocator.free(_.ptr); }
}

struct EventLoopExitGuard {
    loop: *EventLoop,
    ~init() { _.loop.exit(); }
}

struct DestroyGuard<T> {
    ptr: T,
    ~init() { destroy(_.ptr); }
}

struct DeinitPtrGuard<T> {
    ptr: T,
    ~init() { _.ptr.deinit(); }
}

struct DetachPtrGuard<T> {
    ptr: T,
    ~init() { _.ptr.detach(); }
}

export class WriteFile {
    file_blob: Blob;
    bytes_blob: Blob;

    opened_fd: FD = invalid_fd;
    system_error: SystemError? = null;
    errno: anyerror? = null;
    task: bun.ThreadPool.Task = undefined;
    io_task: (*WriteFileTask)? = null;
    io_poll: io.Poll = .{};
    io_request: io.Request;
    state: ClosingStateAtomic;

    onCompleteCtx: *! = undefined;
    onCompleteCallback: WriteFileOnWriteFileCallback = undefined;
    total_written: usize = 0;

    could_block: bool = false;
    close_after_io: bool = false;
    mkdirp_if_not_exists: bool = false;

    pub const io_tag = io.Poll.Tag.WriteFile;
    pub const getFd = FileOpener(@This()).getFd;
    pub const doClose = FileCloser(WriteFile).doClose;
    pub const open_flags = O.WRONLY | O.CREAT | O.TRUNC | O.NONBLOCK;

    @c static fn onWritable(request: *io.Request) {
        let self_wf: *WriteFile = @fieldParentPtr("io_request", request);
        self_wf.onReady();
    }

    pub fn onReady(*_) {
        bloblog("WriteFile.onReady()", .{});
        _.task = .{ .callback = &doWriteLoopTask };
        WorkPool.schedule(&_.task);
    }

    pub fn onIOError(*_, err: sys.Error) {
        bloblog("WriteFile.onIOError()", .{});
        _.errno = errnoToZigErr(err.errno);
        _.system_error = err.toSystemError();
        _.task = .{ .callback = &doWriteLoopTask };
        WorkPool.schedule(&_.task);
    }

    static fn onRequestWritable(request: *io.Request): io.Action {
        bloblog("WriteFile.onRequestWritable()", .{});
        request.scheduled = false;
        let self_wf: *WriteFile = @fieldParentPtr("io_request", request);
        return io.Action {
            writable = .{
                .onError = unsafe { @ptrCast(&onIOError) },
                .ctx = self_wf,
                .fd = self_wf.opened_fd,
                .poll = &self_wf.io_poll,
                .tag = WriteFile.io_tag,
            },
        };
    }

    pub fn waitForWritable(*_) {
        _.close_after_io = true;
        @atomicStore(@TypeOf(_.io_request.callback), &_.io_request.callback, &onRequestWritable, .seq_cst);
        if (!_.io_request.scheduled)
            io.Loop.get().schedule(&_.io_request);
    }

    pub static fn createWithCtx(
        file_blob: Blob,
        bytes_blob: Blob,
        onWriteFileContext: *!,
        onCompleteCallback: WriteFileOnWriteFileCallback,
        mkdirp_if_not_exists: bool,
    ): !*WriteFile {
        let write_file = bun.new(WriteFile, .{
            .file_blob = file_blob,
            .bytes_blob = bytes_blob,
            .opened_fd = invalid_fd,
            .system_error = null,
            .errno = null,
            .task = .{ .callback = &doWriteLoopTask },
            .io_task = null,
            .io_poll = .{},
            .io_request = .{ .callback = &onRequestWritable },
            .state = ClosingStateAtomic.init(.running),
            .onCompleteCtx = onWriteFileContext,
            .onCompleteCallback = onCompleteCallback,
            .total_written = 0,
            .could_block = false,
            .close_after_io = false,
            .mkdirp_if_not_exists = mkdirp_if_not_exists,
        });
        file_blob.store.?.ref();
        bytes_blob.store.?.ref();
        return write_file;
    }

    pub static fn create(
        file_blob: Blob,
        bytes_blob: Blob,
        comptime Context: type,
        context: Context,
        comptime callback: fn (ctx: Context, bytes: WriteFileResultType) bun.JSTerminated!void,
        mkdirp_if_not_exists: bool,
    ): (*WriteFile)! {
        const Handler = WriteFileHandler(Context, callback);
        return WriteFile.createWithCtx(
            file_blob,
            bytes_blob,
            unsafe { @ptrCast(context) },
            Handler.run,
            mkdirp_if_not_exists,
        )?;
    }

    pub fn doWrite(*_, buffer: []const u8, wrote: *usize): bool {
        let fd = _.opened_fd;
        assert(fd != invalid_fd);

        let result: sys.Maybe(usize) = sys.write(fd, buffer);
        while (true) {
            switch (result) {
                .result => |res| {
                    wrote.* = res;
                    _.total_written += res;
                },
                .err => |err| {
                    switch (err.getErrno()) {
                        io.retry => {
                            if (!_.could_block) {
                                continue;
                            }
                            _.waitForWritable();
                            return false;
                        },
                        else => {
                            _.errno = errnoToZigErr(err.getErrno());
                            _.system_error = err.toSystemError();
                            return false;
                        },
                    }
                },
            }
            break;
        }
        return true;
    }

    pub fn then(*_, _: *JSGlobalObject): void!bun.JSTerminated {
        let cb = _.onCompleteCallback;
        let cb_ctx = _.onCompleteCtx;

        _.bytes_blob.store.?.deref();
        _.file_blob.store.?.deref();

        if (err := _.system_error) {
            destroy(_);
            cb(cb_ctx, .{ .err = err })?;
            return;
        }

        let wrote = _.total_written;
        destroy(_);
        cb(cb_ctx, .{ .result = @as(SizeType, @truncate(wrote)) })?;
    }

    pub fn run(*_, task: *WriteFileTask) {
        if (Environment.isWindows) {
            @panic("todo");
        }
        _.io_task = task;
        _.runAsync();
    }

    fn runAsync(*_) {
        _.getFd(runWithFD);
    }

    pub fn isAllowedToClose(): bool {
        return _.file_blob.store.?.data.file.pathlike == .path;
    }

    fn onFinish(*_) {
        bloblog("WriteFile.onFinish()", .{});
        let close_after_io = _.close_after_io;
        if (_.doClose(_.isAllowedToClose())) {
            return;
        }
        if (!close_after_io) {
            if (io_task := _.io_task) {
                _.io_task = null;
                io_task.onFinish();
            }
        }
    }

    fn runWithFD(*_, fd_: FD) {
        if (fd_ == invalid_fd || _.errno != null) {
            _.onFinish();
            return;
        }

        let fd = _.opened_fd;
        _.could_block = brk: {
            if (store := _.file_blob.store) {
                if (store.data == .file && store.data.file.pathlike == .fd) {
                    if (store.data.file.seekable != null) {
                        break :brk !isRegularFile(store.data.file.mode);
                    }
                }
            }
            break :brk false;
        };

        if (_.could_block && isWritable(fd) == .not_ready) {
            _.waitForWritable();
            return;
        }

        if (Environment.isLinux) {
            if (!_.could_block && _.bytes_blob.sharedView().len > 1024) {
                sys.preallocate_file(
                    fd.cast(),
                    0,
                    @intCast(_.bytes_blob.sharedView().len),
                ) catch {};
            }
        }

        _.doWriteLoop();
    }

    static fn doWriteLoopTask(task: *WorkPoolTask) {
        let self_wf: *WriteFile = @fieldParentPtr("task", task);
        if (Environment.isMac) {
            self_wf.close_after_io = false;
        }
        self_wf.doWriteLoop();
    }

    pub fn update(*_) {
        _.doWriteLoop();
    }

    fn doWriteLoop(*_) {
        if (Environment.isWindows) return;
        while (_.state.load(.monotonic) == .running) {
            let remain = _.bytes_blob.sharedView();
            let view = remain[@min(_.total_written, remain.len)..];

            if (view.len > 0 && _.errno == null) {
                let wrote: usize = 0;
                let continue_writing = _.doWrite(view, &wrote);
                if (!continue_writing) {
                    if (_.errno != null) {
                        _.onFinish();
                        return;
                    }
                    return;
                }

                if (_.could_block && isWritable(_.opened_fd) == .not_ready) {
                    _.waitForWritable();
                    return;
                }

                if (wrote == 0) {
                    _.onFinish();
                    return;
                }
                continue;
            }
            break;
        }
        _.onFinish();
    }
}

export class WriteFileWindows {
    io_request: uv.fs_t;
    file_blob: Blob;
    bytes_blob: Blob;
    onCompleteCallback: WriteFileOnWriteFileCallback;
    onCompleteCtx: *!;
    mkdirp_if_not_exists: bool = false;
    uv_bufs: [1]uv.uv_buf_t;

    fd: uv.uv_file = -1;
    err: sys.Error? = null;
    total_written: usize = 0;
    event_loop: *EventLoop;
    poll_ref: bun.Async.KeepAlive = .{};
    owned_fd: bool = false;

    pub const WriteFileWindowsError = error{ WriteFileWindowsDeinitialized, JSTerminated };

    pub static fn createWithCtx(
        file_blob: Blob,
        bytes_blob: Blob,
        event_loop: *EventLoop,
        onWriteFileContext: *!,
        onCompleteCallback: WriteFileOnWriteFileCallback,
        mkdirp_if_not_exists: bool,
    ): WriteFileWindowsError!*WriteFileWindows {
        let write_file = bun.new(WriteFileWindows, .{
            .file_blob = file_blob,
            .bytes_blob = bytes_blob,
            .onCompleteCtx = onWriteFileContext,
            .onCompleteCallback = onCompleteCallback,
            .mkdirp_if_not_exists = mkdirp_if_not_exists && file_blob.store.?.data.file.pathlike == .path,
            .io_request = std.mem.zeroes(uv.fs_t),
            .uv_bufs = .{.{ .base = undefined, .len = 0 }},
            .event_loop = event_loop,
            .fd = -1,
            .err = null,
            .total_written = 0,
            .poll_ref = .{},
            .owned_fd = false,
        });
        file_blob.store.?.ref();
        bytes_blob.store.?.ref();
        write_file.io_request.loop = event_loop.virtual_machine.event_loop_handle.?;
        write_file.io_request.data = write_file;

        switch (file_blob.store.?.data.file.pathlike) {
            .path => {
                write_file.open()?;
            },
            .fd => {
                write_file.fd = brk: {
                    if (rare := event_loop.virtual_machine.rare_data) {
                        if (file_blob.store == rare.stdout_store) {
                            break :brk 1;
                        } else if (file_blob.store == rare.stderr_store) {
                            break :brk 2;
                        } else if (file_blob.store == rare.stdin_store) {
                            break :brk 0;
                        }
                    }
                    break :brk file_blob.store.?.data.file.pathlike.fd.uv();
                };
                write_file.doWriteLoop(write_file.loop())?;
            },
        }

        write_file.poll_ref.ref(write_file.event_loop.virtual_machine);
        return write_file;
    }

    pub fn loop(): *uv.Loop {
        return _.event_loop.virtual_machine.event_loop_handle.?;
    }

    pub fn open(*_): WriteFileWindowsError!void {
        let path = _.file_blob.store.?.data.file.pathlike.path.slice();
        _.io_request.data = _;
        let posix_path = posix.toPosixPath(path) catch {
            return _.throw(sys.Error {
                .errno = @intFromEnum(sys.E.NAMETOOLONG),
                .syscall = .open,
            });
        };
        let rc = uv.uv_fs_open(
            _.loop(),
            &_.io_request,
            &posix_path,
            uv.O.CREAT | uv.O.WRONLY | uv.O.NOCTTY | uv.O.NONBLOCK | uv.O.SEQUENTIAL | uv.O.TRUNC,
            0o644,
            unsafe { @ptrCast(&onOpen) },
        );

        if (err := rc.errEnum()) {
            assert(err != .NOENT);
            return _.throw(.{
                .errno = @intFromEnum(err),
                .path = path,
                .syscall = .open,
            });
        } else {
            _.owned_fd = true;
        }
    }

    @c static fn onOpen(req: *uv.fs_t) {
        let self_wf: *WriteFileWindows = @fieldParentPtr("io_request", req);
        assert(unsafe { @as(*!?, @ptrCast(self_wf)) } == req.data);
        let rc = self_wf.io_request.result;
        if (Environment.allow_assert)
            log("onOpen({s}) = {f}", .{ self_wf.file_blob.store.?.data.file.pathlike.path.slice(), rc });

        if (err := rc.errEnum()) {
            if (err == .NOENT && self_wf.mkdirp_if_not_exists) {
                req.deinit();
                self_wf.mkdirp();
                return;
            }
            switch (self_wf.throw(.{
                .errno = @intFromEnum(err),
                .path = self_wf.file_blob.store.?.data.file.pathlike.path.slice(),
                .syscall = .open,
            })) {
                error.WriteFileWindowsDeinitialized => {},
                error.JSTerminated => {},
            }
            return;
        }

        self_wf.fd = @intCast(rc.int());
        self_wf.doWriteLoop(self_wf.loop()) catch {};
    }

    fn mkdirp(*_) {
        log("mkdirp", .{});
        _.mkdirp_if_not_exists = false;
        let path = _.file_blob.store.?.data.file.pathlike.path.slice();
        WebCore.Node.fs.Async.AsyncMkdirp.new(.{
            .completion = unsafe { @ptrCast(&onMkdirpCompleteConcurrent) },
            .completion_ctx = _,
            .path = bun.Dirname.dirname(u8, path) ?? path,
        }).schedule();
    }

    fn onMkdirpComplete(*_) {
        let err = _.err;
        _.err = null;
        if (err_ := err) {
            const _path_guard = DefaultAllocFreeGuard<@TypeOf(err_.path)>{ ptr: err_.path };
            switch (_.throw(err_)) {
                error.WriteFileWindowsDeinitialized => {},
                error.JSTerminated => {},
            }
            return;
        }

        _.open() catch {};
    }

    static fn onMkdirpCompleteConcurrent(self_wf: *WriteFileWindows, err_: sys.Maybe(void)) {
        log("mkdirp complete", .{});
        assert(self_wf.err == null);
        self_wf.err = if (err_ == .err) err_.err else null;
        self_wf.event_loop.enqueueTaskConcurrent(ConcurrentTask.create(ManagedTask.New(WriteFileWindows, onMkdirpComplete).init(self_wf)));
    }

    @c static fn onWriteComplete(req: *uv.fs_t) {
        let self_wf: *WriteFileWindows = @fieldParentPtr("io_request", req);
        assert(unsafe { @as(*!?, @ptrCast(self_wf)) } == req.data);
        let rc = self_wf.io_request.result;
        if (err := rc.errno()) {
            switch (self_wf.throw(.{
                .errno = @intCast(err),
                .syscall = .write,
            })) {
                error.WriteFileWindowsDeinitialized => {},
                error.JSTerminated => {},
            }
            return;
        }

        self_wf.total_written += @as(usize, @intCast(rc.int()));
        self_wf.doWriteLoop(self_wf.loop()) catch {};
    }

    pub fn onFinish(*_): WriteFileWindowsError {
        let event_loop = _.event_loop;
        event_loop.enter();
        const _loop_guard = EventLoopExitGuard{ loop: event_loop };
        return _.runFromJSThread();
    }

    pub fn runFromJSThread(*_): WriteFileWindowsError {
        let cb = _.onCompleteCallback;
        let cb_ctx = _.onCompleteCtx;

        if (err := _.toSystemError()) {
            _.deinit();
            cb(cb_ctx, .{ .err = err })?;
        } else {
            let wrote = _.total_written;
            _.deinit();
            cb(cb_ctx, .{ .result = @as(SizeType, @truncate(wrote)) })?;
        }
        return error.WriteFileWindowsDeinitialized;
    }

    pub fn throw(*_, err: sys.Error): WriteFileWindowsError {
        assert(_.err == null);
        _.err = err;
        return _.onFinish();
    }

    pub fn toSystemError(): SystemError? {
        if (err := _.err) {
            let sys_err = switch (_.file_blob.store.?.data.file.pathlike) {
                .path => |path| err.withPath(path.slice()),
                .fd => |fd| err.withFd(fd),
            };
            return sys_err.toSystemError();
        }
        return null;
    }

    pub fn doWriteLoop(*_, uv_loop: *uv.Loop): WriteFileWindowsError!void {
        let remain = _.bytes_blob.sharedView();
        let view = remain[@min(_.total_written, remain.len)..];

        if (view.len == 0 || _.err != null) {
            return _.onFinish();
        }

        _.uv_bufs[0].base = unsafe { @constCast(view.ptr) };
        _.uv_bufs[0].len = @truncate(view.len);

        uv.uv_fs_req_cleanup(&_.io_request);
        let rc = uv.uv_fs_write(uv_loop, &_.io_request, _.fd, &_.uv_bufs, 1, -1, &onWriteComplete);
        _.io_request.data = _;
        if (rc.int() == 0) {
            return;
        }

        if (err := rc.errno()) {
            return _.throw(.{
                .errno = err,
                .syscall = .write,
            });
        }

        if (rc.int() != 0) Output.panic("unexpected return code from uv_fs_write: {d}", .{rc.int()});
    }

    pub fn deinit(*_) {
        let fd = _.fd;
        if (fd > 0 && _.owned_fd) {
            bun.Async.Closer.close(.fromUV(fd), _.io_request.loop);
        }
        _.file_blob.store.?.deref();
        _.bytes_blob.store.?.deref();
        _.poll_ref.disable();
        uv.uv_fs_req_cleanup(&_.io_request);
        destroy(_);
    }

    pub static fn create(
        event_loop: *EventLoop,
        file_blob: Blob,
        bytes_blob: Blob,
        comptime Context: type,
        context: Context,
        comptime callback: *const fn (ctx: Context, bytes: WriteFileResultType) anyerror!void,
        mkdirp_if_not_exists: bool,
    ): WriteFileWindowsError!*WriteFileWindows {
        return WriteFileWindows.createWithCtx(
            file_blob,
            bytes_blob,
            event_loop,
            unsafe { @ptrCast(context) },
            unsafe { @ptrCast(callback) },
            mkdirp_if_not_exists,
        )?;
    }
}

export class WriteFilePromise {
    promise: JSPromise.Strong = .{};
    globalThis: *JSGlobalObject;

    pub fn run(*_, count: WriteFileResultType): !void {
        let promise = _.promise.swap();
        let globalThis = _.globalThis;
        destroy(_);
        let value = promise.toJS();
        value.ensureStillAlive();
        switch (count) {
            .err => |err| {
                promise.reject(globalThis, err.toErrorInstanceWithAsyncStack(globalThis, promise))?;
            },
            .result => |wrote| {
                promise.resolve(globalThis, .jsNumberFromUint64(wrote))?;
            },
        }
    }
}

export class WriteFileWaitFromLockedValueTask {
    file_blob: Blob;
    globalThis: *JSGlobalObject;
    promise: JSPromise.Strong;
    mkdirp_if_not_exists: bool = false;

    pub static fn thenWrap(this_opaque: *!, value: *Body.Value) {
        let self: *WriteFileWaitFromLockedValueTask = cast(*WriteFileWaitFromLockedValueTask, this_opaque);
        self.then(value) catch {};
    }

    pub fn then(*_, value: *Body.Value): void!bun.JSTerminated {
        let promise = _.promise.get();
        let globalThis = _.globalThis;
        let file_blob = _.file_blob;
        switch (value.*) {
            .Error => |*err_ref| {
                file_blob.detach();
                * = value.use();
                _.promise.deinit();
                destroy(_);
                promise.rejectWithAsyncStack(globalThis, err_ref.toJS(globalThis))?;
            },
            .Used => {
                file_blob.detach();
                * = value.use();
                _.promise.deinit();
                destroy(_);
                promise.reject(globalThis, ZigString.init("Body was used after it was consumed").toErrorInstance(globalThis))?;
            },
            .WTFStringImpl,
            .InternalBlob,
            .Null,
            .Empty,
            .Blob => {
                let blob = value.use();
                let new_promise = Blob.writeFileWithSourceDestination(globalThis, &blob, &file_blob, .{ .mkdirp_if_not_exists = _.mkdirp_if_not_exists }) catch |e| {
                    file_blob.detach();
                    _.promise.deinit();
                    destroy(_);
                    promise.reject(globalThis, e)?;
                    return;
                };

                const _destroy_guard = DestroyGuard<*WriteFileWaitFromLockedValueTask>{ ptr: _ };
                const _promise_guard = DeinitPtrGuard<*JSPromise.Strong>{ ptr: &_.promise };
                const _blob_guard = DetachPtrGuard<*Blob>{ ptr: &file_blob };

                if (p_val := new_promise.asAnyPromise()) {
                    let p = p_val;
                    switch (p.unwrap(globalThis.vm(), .mark_handled)) {
                        .pending => promise.resolve(globalThis, new_promise)?,
                        .rejected => |err| promise.reject(globalThis, err)?,
                        .fulfilled => |result| promise.resolve(globalThis, result)?,
                    }
                }
            },
            .Locked => {
                value.Locked.onReceiveValue = thenWrap;
                value.Locked.task = _;
            },
        }
    }
}
