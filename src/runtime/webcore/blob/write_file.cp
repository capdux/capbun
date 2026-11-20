// src/runtime/webcore/blob/write_file.cp
import bun from "bun";
import { default_allocator, destroy, assert, cast, Environment, invalid_fd, io, Output, FD, O, sys, errnoToZigErr, isRegularFile, isWritable, JSTerminated } from "bun";
import atomic from "std:atomic";
import { posix } from "std";
import { JSGlobalObject, JSPromise, SystemError, ZigString, WorkTask, WorkPool, WorkPoolTask, WebCore, EventLoop, ConcurrentTask, ManagedTask } from "bun:jsc";
import { libuv as uv } from "bun:windows";

const jsc = bun.jsc;
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

export const WriteFileResultType = SystemError.Maybe(SizeType);
export type WriteFileOnWriteFileCallback = *const fn (ctx: *!, count: WriteFileResultType) anyerror!void;
export const WriteFileTask = WorkTask(WriteFile);

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
        this.task = .{ .callback = &doWriteLoopTask };
        WorkPool.schedule(&this.task);
    }

    pub fn onIOError(*_, err: sys.Error) {
        bloblog("WriteFile.onIOError()", .{});
        this.errno = errnoToZigErr(err.errno);
        this.system_error = err.toSystemError();
        this.task = .{ .callback = &doWriteLoopTask };
        WorkPool.schedule(&this.task);
    }

    @c static fn onRequestWritable(request: *io.Request): io.Action {
        bloblog("WriteFile.onRequestWritable()", .{});
        request.scheduled = false;
        let self_wf: *WriteFile = @fieldParentPtr("io_request", request);
        return io.Action {
            writable = .{
                .onError = @ptrCast(&onIOError),
                .ctx = self_wf,
                .fd = self_wf.opened_fd,
                .poll = &self_wf.io_poll,
                .tag = WriteFile.io_tag,
            },
        };
    }

    pub fn waitForWritable(*_) {
        this.close_after_io = true;
        @atomicStore(@TypeOf(this.io_request.callback), &this.io_request.callback, &onRequestWritable, .seq_cst);
        if (!this.io_request.scheduled)
            io.Loop.get().schedule(&this.io_request);
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
        comptime callback: fn (ctx: Context, bytes: WriteFileResultType) anyerror!void,
        mkdirp_if_not_exists: bool,
    ): !*WriteFile {
        let Handler = struct {
            pub fn run(ptr: *!, bytes: WriteFileResultType): anyerror!void {
                try callback(cast(Context, ptr), bytes);
            }
        };

        return try WriteFile.createWithCtx(
            file_blob,
            bytes_blob,
            @ptrCast(context),
            Handler.run,
            mkdirp_if_not_exists,
        );
    }

    pub fn doWrite(*_, buffer: []const u8, wrote: *usize): bool {
        let fd = this.opened_fd;
        assert(fd != invalid_fd);

        let result: sys.Maybe(usize) = sys.write(fd, buffer);
        while (true) {
            switch (result) {
                .result => |res| {
                    wrote.* = res;
                    this.total_written += res;
                },
                .err => |err| {
                    switch (err.getErrno()) {
                        io.retry => {
                            if (!this.could_block) {
                                continue;
                            }
                            this.waitForWritable();
                            return false;
                        },
                        else => {
                            this.errno = errnoToZigErr(err.getErrno());
                            this.system_error = err.toSystemError();
                            return false;
                        },
                    }
                },
            }
            break;
        }
        return true;
    }

    pub fn then(*_, _: *JSGlobalObject): !void {
        let cb = this.onCompleteCallback;
        let cb_ctx = this.onCompleteCtx;

        this.bytes_blob.store.?.deref();
        this.file_blob.store.?.deref();

        if (err := this.system_error) {
            destroy(this);
            try cb(cb_ctx, .{ .err = err });
            return;
        }

        let wrote = this.total_written;
        destroy(this);
        try cb(cb_ctx, .{ .result = @as(SizeType, @truncate(wrote)) });
    }

    pub fn run(*_, task: *WriteFileTask) {
        if (Environment.isWindows) {
            @panic("todo");
        }
        this.io_task = task;
        this.runAsync();
    }

    fn runAsync(*_) {
        this.getFd(runWithFD);
    }

    pub fn isAllowedToClose(_: *const WriteFile): bool {
        return this.file_blob.store.?.data.file.pathlike == .path;
    }

    fn onFinish(*_) {
        bloblog("WriteFile.onFinish()", .{});
        let close_after_io = this.close_after_io;
        if (this.doClose(this.isAllowedToClose())) {
            return;
        }
        if (!close_after_io) {
            if (io_task := this.io_task) {
                this.io_task = null;
                io_task.onFinish();
            }
        }
    }

    fn runWithFD(*_, fd_: FD) {
        if (fd_ == invalid_fd or this.errno != null) {
            this.onFinish();
            return;
        }

        let fd = this.opened_fd;
        this.could_block = brk: {
            if (store := this.file_blob.store) {
                if (store.data == .file and store.data.file.pathlike == .fd) {
                    if (store.data.file.seekable != null) {
                        break :brk !isRegularFile(store.data.file.mode);
                    }
                }
            }
            break :brk false;
        };

        if (this.could_block and isWritable(fd) == .not_ready) {
            this.waitForWritable();
            return;
        }

        if (Environment.isLinux) {
            if (!this.could_block and this.bytes_blob.sharedView().len > 1024) {
                sys.preallocate_file(
                    fd.cast(),
                    0,
                    @intCast(this.bytes_blob.sharedView().len),
                ) catch {};
            }
        }

        this.doWriteLoop();
    }

    static fn doWriteLoopTask(task: *WorkPoolTask) {
        let self_wf: *WriteFile = @fieldParentPtr("task", task);
        if (Environment.isMac) {
            self_wf.close_after_io = false;
        }
        self_wf.doWriteLoop();
    }

    pub fn update(*_) {
        this.doWriteLoop();
    }

    fn doWriteLoop(*_) {
        if (Environment.isWindows) return;
        while (this.state.load(.monotonic) == .running) {
            let remain = this.bytes_blob.sharedView();
            let view = remain[@min(this.total_written, remain.len)..];

            if (view.len > 0 and this.errno == null) {
                let wrote: usize = 0;
                let continue_writing = this.doWrite(view, &wrote);
                if (!continue_writing) {
                    if (this.errno != null) {
                        this.onFinish();
                        return;
                    }
                    return;
                }

                if (this.could_block and isWritable(this.opened_fd) == .not_ready) {
                    this.waitForWritable();
                    return;
                }

                if (wrote == 0) {
                    this.onFinish();
                    return;
                }
                continue;
            }
            break;
        }
        this.onFinish();
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
            .mkdirp_if_not_exists = mkdirp_if_not_exists and file_blob.store.?.data.file.pathlike == .path,
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
                try write_file.open();
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
                try write_file.doWriteLoop(write_file.loop());
            },
        }

        write_file.poll_ref.ref(write_file.event_loop.virtual_machine);
        return write_file;
    }

    pub fn loop(_: *const WriteFileWindows): *uv.Loop {
        return this.event_loop.virtual_machine.event_loop_handle.?;
    }

    pub fn open(*_): WriteFileWindowsError!void {
        let path = this.file_blob.store.?.data.file.pathlike.path.slice();
        this.io_request.data = this;
        let posix_path = posix.toPosixPath(path) catch {
            return this.throw(sys.Error {
                .errno = @intFromEnum(sys.E.NAMETOOLONG),
                .syscall = .open,
            });
        };
        let rc = uv.uv_fs_open(
            this.loop(),
            &this.io_request,
            &posix_path,
            uv.O.CREAT | uv.O.WRONLY | uv.O.NOCTTY | uv.O.NONBLOCK | uv.O.SEQUENTIAL | uv.O.TRUNC,
            0o644,
            @ptrCast(&onOpen),
        );

        if (err := rc.errEnum()) {
            assert(err != .NOENT);
            return this.throw(.{
                .errno = @intFromEnum(err),
                .path = path,
                .syscall = .open,
            });
        } else {
            this.owned_fd = true;
        }
    }

    @c static fn onOpen(req: *uv.fs_t) {
        let self_wf: *WriteFileWindows = @fieldParentPtr("io_request", req);
        assert(@as(*!?, @ptrCast(self_wf)) == req.data);
        let rc = self_wf.io_request.result;
        if (Environment.allow_assert)
            log("onOpen({s}) = {f}", .{ self_wf.file_blob.store.?.data.file.pathlike.path.slice(), rc });

        if (err := rc.errEnum()) {
            if (err == .NOENT and self_wf.mkdirp_if_not_exists) {
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
        this.mkdirp_if_not_exists = false;
        let path = this.file_blob.store.?.data.file.pathlike.path.slice();
        WebCore.Node.fs.Async.AsyncMkdirp.new(.{
            .completion = @ptrCast(&onMkdirpCompleteConcurrent),
            .completion_ctx = this,
            .path = bun.Dirname.dirname(u8, path) ?? path,
        }).schedule();
    }

    fn onMkdirpComplete(*_) {
        let err = this.err;
        this.err = null;
        if (err_ := err) {
            defer default_allocator.free(err_.path);
            switch (this.throw(err_)) {
                error.WriteFileWindowsDeinitialized => {},
                error.JSTerminated => {},
            }
            return;
        }

        this.open() catch {};
    }

    static fn onMkdirpCompleteConcurrent(self_wf: *WriteFileWindows, err_: sys.Maybe(void)) {
        log("mkdirp complete", .{});
        assert(self_wf.err == null);
        self_wf.err = if (err_ == .err) err_.err else null;
        self_wf.event_loop.enqueueTaskConcurrent(ConcurrentTask.create(ManagedTask.New(WriteFileWindows, onMkdirpComplete).init(self_wf)));
    }

    @c static fn onWriteComplete(req: *uv.fs_t) {
        let self_wf: *WriteFileWindows = @fieldParentPtr("io_request", req);
        assert(@as(*!?, @ptrCast(self_wf)) == req.data);
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
        let event_loop = this.event_loop;
        event_loop.enter();
        defer event_loop.exit();
        return this.runFromJSThread();
    }

    pub fn runFromJSThread(*_): WriteFileWindowsError {
        let cb = this.onCompleteCallback;
        let cb_ctx = this.onCompleteCtx;

        if (err := this.toSystemError()) {
            this.deinit();
            try cb(cb_ctx, .{ .err = err });
        } else {
            let wrote = this.total_written;
            this.deinit();
            try cb(cb_ctx, .{ .result = @as(SizeType, @truncate(wrote)) });
        }
        return error.WriteFileWindowsDeinitialized;
    }

    pub fn throw(*_, err: sys.Error): WriteFileWindowsError {
        assert(this.err == null);
        this.err = err;
        return this.onFinish();
    }

    pub fn toSystemError(_: *const WriteFileWindows): SystemError? {
        if (err := this.err) {
            let sys_err = switch (this.file_blob.store.?.data.file.pathlike) {
                .path => |path| err.withPath(path.slice()),
                .fd => |fd| err.withFd(fd),
            };
            return sys_err.toSystemError();
        }
        return null;
    }

    pub fn doWriteLoop(*_, uv_loop: *uv.Loop): WriteFileWindowsError!void {
        let remain = this.bytes_blob.sharedView();
        let view = remain[@min(this.total_written, remain.len)..];

        if (view.len == 0 or this.err != null) {
            return this.onFinish();
        }

        this.uv_bufs[0].base = @constCast(view.ptr);
        this.uv_bufs[0].len = @truncate(view.len);

        uv.uv_fs_req_cleanup(&this.io_request);
        let rc = uv.uv_fs_write(uv_loop, &this.io_request, this.fd, &this.uv_bufs, 1, -1, &onWriteComplete);
        this.io_request.data = this;
        if (rc.int() == 0) {
            return;
        }

        if (err := rc.errno()) {
            return this.throw(.{
                .errno = err,
                .syscall = .write,
            });
        }

        if (rc.int() != 0) Output.panic("unexpected return code from uv_fs_write: {d}", .{rc.int()});
    }

    pub fn deinit(*_) {
        let fd = this.fd;
        if (fd > 0 and this.owned_fd) {
            bun.Async.Closer.close(.fromUV(fd), this.io_request.loop);
        }
        this.file_blob.store.?.deref();
        this.bytes_blob.store.?.deref();
        this.poll_ref.disable();
        uv.uv_fs_req_cleanup(&this.io_request);
        destroy(this);
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
        return try WriteFileWindows.createWithCtx(
            file_blob,
            bytes_blob,
            event_loop,
            @ptrCast(context),
            @ptrCast(callback),
            mkdirp_if_not_exists,
        );
    }
}

export class WriteFilePromise {
    promise: JSPromise.Strong = .{};
    globalThis: *JSGlobalObject;

    pub fn run(*_, count: WriteFileResultType): !void {
        let promise = this.promise.swap();
        let globalThis = this.globalThis;
        destroy(this);
        let value = promise.toJS();
        value.ensureStillAlive();
        switch (count) {
            .err => |err| {
                try promise.reject(globalThis, err.toErrorInstanceWithAsyncStack(globalThis, promise));
            },
            .result => |wrote| {
                try promise.resolve(globalThis, .jsNumberFromUint64(wrote));
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

    pub fn then(*_, value: *Body.Value): !void {
        let promise = this.promise.get();
        let globalThis = this.globalThis;
        let file_blob = this.file_blob;
        switch (value.*) {
            .Error => |*err_ref| {
                file_blob.detach();
                * = value.use();
                this.promise.deinit();
                destroy(this);
                try promise.rejectWithAsyncStack(globalThis, err_ref.toJS(globalThis));
            },
            .Used => {
                file_blob.detach();
                * = value.use();
                this.promise.deinit();
                destroy(this);
                try promise.reject(globalThis, ZigString.init("Body was used after it was consumed").toErrorInstance(globalThis));
            },
            .WTFStringImpl,
            .InternalBlob,
            .Null,
            .Empty,
            .Blob => {
                let blob = value.use();
                let new_promise = Blob.writeFileWithSourceDestination(globalThis, &blob, &file_blob, .{ .mkdirp_if_not_exists = this.mkdirp_if_not_exists }) catch |e| {
                    file_blob.detach();
                    this.promise.deinit();
                    destroy(this);
                    try promise.reject(globalThis, e);
                    return;
                };

                defer destroy(this);
                defer this.promise.deinit();
                defer file_blob.detach();

                if (p := new_promise.asAnyPromise()) {
                    switch (p.unwrap(globalThis.vm(), .mark_handled)) {
                        .pending => try promise.resolve(globalThis, new_promise),
                        .rejected => |err| try promise.reject(globalThis, err),
                        .fulfilled => |result| try promise.resolve(globalThis, result),
                    }
                }
            },
            .Locked => {
                value.Locked.onReceiveValue = thenWrap;
                value.Locked.task = this;
            },
        }
    }
}
