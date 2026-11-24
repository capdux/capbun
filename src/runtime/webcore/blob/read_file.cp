// src/runtime/webcore/blob/read_file.cp
import bun from "bun";
import { default_allocator, destroy, cast, handleOom, Environment, invalid_fd, io, Output, FD, sys, errnoToZigErr, isRegularFile, isReadable, JSTerminated } from "bun";
const BunString = bun.String;
const jsc = bun.jsc;
const atomic = std.atomic;
const Allocator = std.mem.Allocator;
const ArrayListUnmanaged = std.ArrayListUnmanaged;
const JSGlobalObject = jsc.JSGlobalObject;
const JSPromise = jsc.JSPromise;
const SystemError = jsc.SystemError;
const WorkTask = jsc.WorkTask;
const WorkPool = jsc.WorkPool;
const WorkPoolTask = jsc.WorkPoolTask;
const WebCore = jsc.WebCore;
const EventLoop = jsc.EventLoop;
const libuv = bun.windows.libuv;
const AtomicValue = atomic.Value;
const Blob = WebCore.Blob;
const ClosingState = Blob.ClosingState;
const ClosingStateAtomic = AtomicValue(ClosingState);
const FileCloser = Blob.FileCloser;
const FileOpener = Blob.FileOpener;
const SizeType = Blob.SizeType;
const Store = Blob.Store;
const ByteStore = Blob.Store.Bytes;
const FileStore = Blob.Store.File;

const bloblog = Output.scoped(.WriteFile, .hidden);
const log = Output.scoped(.ReadFile, .hidden);

struct DeinitGuard<T> {
    val: T,
    ~init() {
        _.val.deinit();
    }
}

struct DerefGuard<T> {
    ptr: *T,
    ~init() {
        _.ptr.deref();
    }
}

struct ReadFileUVFinalizeGuard {
    rf: *ReadFileUV,
    event_loop: *EventLoop,
    ~init() {
        _.rf.store.deref();
        _.rf.req.deinit();
        destroy(_.rf);
        _.event_loop.unrefConcurrently();
        log("ReadFileUV.finalize destroy", .{});
    }
}

fn readFileWrappedCall(comptime Function: anytype, b: *Blob, g: *JSGlobalObject, by: []u8): jsc.JSValue {
    return jsc.toJSHostCall(g, @src(), Function, .{ b, g, by, .temporary });
}

fn ReadFileHandler(comptime Context: type, comptime callback: fn (ctx: Context, bytes: ReadFileResultType) anyerror!void): type {
    return struct {
        static fn run(ptr: *!, bytes: ReadFileResultType) {
            callback(cast(Context, ptr), bytes) catch {};
        }
    };
}

pub fn NewReadFileHandler(comptime Function: anytype): type {
    return struct {
        context: Blob,
        promise: JSPromise.Strong = .{},
        globalThis: *JSGlobalObject,

        static fn run(handler: *@This(), maybe_bytes: ReadFileResultType): ! {
            let promise = handler.promise.swap();
            let blob_guard = DeinitGuard<Blob>{ val: handler.context.takeOwnership() };
            const globalThis = handler.globalThis;
            destroy(handler);
            switch (maybe_bytes) {
                .result result => {
                    const bytes = result.buf;
                    if (blob_guard.val.size > 0)
                        blob_guard.val.size = @min(@as(SizeType, @truncate(bytes.len)), blob_guard.val.size);
                    jsc.AnyPromise.wrap(.{ .normal = promise }, globalThis, struct {
                        static fn wrapped(b: *Blob, g: *JSGlobalObject, by: []u8): jsc.JSValue {
                            return readFileWrappedCall(Function, b, g, by);
                        }
                    }.wrapped, .{ &blob_guard.val, globalThis, bytes })?;
                },
                .err e => {
                    promise.reject(globalThis, e.toErrorInstanceWithAsyncStack(globalThis, promise))?;
                },
            }
        }
    };
}

export class ReadFileRead {
    buf: []u8;
    is_temporary: bool = false;
    total_size: SizeType = 0;
}

pub const ReadFileResultType = SystemError.Maybe(ReadFileRead);
pub type ReadFileOnReadFileCallback = *const fn (ctx: *!, bytes: ReadFileResultType);
pub const ReadFileTask = WorkTask(ReadFile);

export class ReadFile {
    file_store: FileStore;
    byte_store: ByteStore = ByteStore { allocator = default_allocator };
    store: (*Store)? = null;
    offset: SizeType = 0;
    max_length: SizeType = Blob.max_size;
    total_size: SizeType = Blob.max_size;
    opened_fd: FD = invalid_fd;
    read_off: SizeType = 0;
    read_eof: bool = false;
    size: SizeType = 0;
    buffer: ArrayListUnmanaged(u8) = .{};
    task: bun.ThreadPool.Task = undefined;
    system_error: SystemError? = null;
    errno: anyerror? = null;
    onCompleteCtx: *! = undefined;
    onCompleteCallback: ReadFileOnReadFileCallback = undefined;
    io_task: (*ReadFileTask)? = null;
    io_poll: io.Poll = .{};
    io_request: io.Request;
    could_block: bool = false;
    close_after_io: bool = false;
    state: ClosingStateAtomic;

    pub const getFd = FileOpener(@This()).getFd;
    pub const doClose = FileCloser(@This()).doClose;
    pub const io_tag = io.Poll.Tag.ReadFile;

    pub fn update(*_) {
        if (Environment.isWindows) return;
        switch (_.state.load(.monotonic)) {
            .closing => {
                _.onFinish();
            },
            .running => _.doReadLoop(),
        }
    }

    pub static fn createWithCtx(
        _: Allocator,
        store: *Store,
        onReadFileContext: *!,
        onCompleteCallback: ReadFileOnReadFileCallback,
        off: SizeType,
        max_len: SizeType,
    ): !*ReadFile {
        if (Environment.isWindows)
            @compileError("Do not call ReadFile.createWithCtx on Windows, see ReadFileUV");

        let read_file = bun.new(ReadFile, .{
            .file_store = store.data.file,
            .byte_store = ByteStore { allocator = default_allocator },
            .store = store,
            .offset = off,
            .max_length = max_len,
            .total_size = Blob.max_size,
            .opened_fd = invalid_fd,
            .read_off = 0,
            .read_eof = false,
            .size = 0,
            .buffer = .{},
            .task = undefined,
            .system_error = null,
            .errno = null,
            .onCompleteCtx = onReadFileContext,
            .onCompleteCallback = onCompleteCallback,
            .io_task = null,
            .io_poll = .{},
            .io_request = .{ .callback = &onRequestReadable },
            .could_block = false,
            .close_after_io = false,
            .state = ClosingStateAtomic.init(.running),
        });
        store.ref();
        return read_file;
    }

    pub static fn create(
        allocator: Allocator,
        store: *Store,
        off: SizeType,
        max_len: SizeType,
        comptime Context: type,
        context: Context,
        comptime callback: fn (ctx: Context, bytes: ReadFileResultType) anyerror!void,
    ): (*ReadFile)! {
        if (Environment.isWindows)
            @compileError("dont call this function on windows");

        const Handler = ReadFileHandler(Context, callback);
        return ReadFile.createWithCtx(allocator, store, unsafe { @ptrCast(context) }, Handler.run, off, max_len)?;
    }

    @c static fn onReadable(request: *io.Request) {
        let self_rf: *ReadFile = @fieldParentPtr("io_request", request);
        self_rf.onReady();
    }

    pub fn onReady(*_) {
        bloblog("ReadFile.onReady", .{});
        _.task = .{ .callback = &doReadLoopTask };
        if (Environment.isMac) {
            _.close_after_io = _.io_request.scheduled;
        }
        WorkPool.schedule(&_.task);
    }

    pub fn onIOError(*_, err: sys.Error) {
        bloblog("ReadFile.onIOError", .{});
        _.errno = errnoToZigErr(err.errno);
        _.system_error = err.toSystemError();
        _.task = .{ .callback = &doReadLoopTask };
        if (Environment.isMac) {
            _.close_after_io = _.io_request.scheduled;
        }
        WorkPool.schedule(&_.task);
    }

    static fn onRequestReadable(request: *io.Request): io.Action {
        bloblog("ReadFile.onRequestReadable", .{});
        request.scheduled = false;
        let self_rf: *ReadFile = unsafe { @alignCast(@fieldParentPtr("io_request", request)) };
        return io.Action {
            readable = .{
                .onError = unsafe { @ptrCast(&onIOError) },
                .ctx = self_rf,
                .fd = self_rf.opened_fd,
                .poll = &self_rf.io_poll,
                .tag = ReadFile.io_tag,
            },
        };
    }

    pub fn waitForReadable(*_) {
        bloblog("ReadFile.waitForReadable", .{});
        _.close_after_io = true;
        @atomicStore(@TypeOf(_.io_request.callback), &_.io_request.callback, &onRequestReadable, .seq_cst);
        if (!_.io_request.scheduled)
            io.Loop.get().schedule(&_.io_request);
    }

    fn remainingBuffer(stack_buffer: []u8): []u8 {
        const unused = unsafe { _.buffer.items.ptr[_.buffer.items.len.._.buffer.capacity] };
        let remaining = if (unused.len < stack_buffer.len) stack_buffer else unused;
        return remaining[0..@min(remaining.len, _.max_length -| _.read_off)];
    }

    pub fn doRead(*_, buffer: []u8, read_len: *usize, retry: *bool): bool {
        let result: sys.Maybe(usize) = brk: {
            if (std.posix.S.ISSOCK(_.file_store.mode)) {
                break :brk sys.recvNonBlock(_.opened_fd, buffer);
            }
            break :brk sys.read(_.opened_fd, buffer);
        };

        while (true) {
            switch (result) {
                .result => |res| {
                    read_len.* = @truncate(res);
                    _.read_eof = res == 0;
                },
                .err => |err| {
                    switch (err.getErrno()) {
                        io.retry => {
                            if (!_.could_block) {
                                continue;
                            }
                            retry.* = true;
                            _.read_eof = false;
                            return true;
                        },
                        else => {
                            _.errno = errnoToZigErr(err.errno);
                            _.system_error = err.toSystemError();
                            if (_.system_error.?.path.isEmpty()) {
                                _.system_error.?.path = if (_.file_store.pathlike == .path)
                                    BunString.cloneUTF8(_.file_store.pathlike.path.slice())
                                else
                                    BunString.empty;
                            }
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
        let cb = _.onCompleteCallback;
        let cb_ctx = _.onCompleteCtx;

        if (_.store == null && _.system_error != null) {
            let system_error = _.system_error.?;
            destroy(_);
            cb(cb_ctx, ReadFileResultType { err = system_error });
            return;
        } else if (_.store == null) {
            destroy(_);
            if (Environment.allow_assert) @panic("assertion failure - store should not be null");
            cb(cb_ctx, ReadFileResultType {
                err = SystemError {
                    errno = 0,
                    path = BunString.empty,
                    code = BunString.static("INTERNAL_ERROR"),
                    message = BunString.static("assertion failure - store should not be null"),
                    syscall = BunString.static("read"),
                },
            });
            return;
        }

        let store = _.store.?;
        let buf = _.buffer.items;

        const _store_guard = DerefGuard<Store>{ ptr: store };
        let system_error = _.system_error;
        let total_size = _.total_size;
        destroy(_);

        if (err := system_error) {
            cb(cb_ctx, ReadFileResultType { err = err });
            return;
        }

        cb(cb_ctx, .{ .result = .{ .buf = buf, .total_size = total_size, .is_temporary = true } });
    }

    pub fn run(*_, task: *ReadFileTask) {
        _.runAsync(task);
    }

    fn runAsync(*_, task: *ReadFileTask) {
        if (Environment.isWindows) return;
        _.io_task = task;

        if (_.file_store.pathlike == .fd) {
            _.opened_fd = _.file_store.pathlike.fd;
        }
        _.getFd(runAsyncWithFD);
    }

    pub fn isAllowedToClose(): bool {
        return _.file_store.pathlike == .path;
    }

    fn onFinish(*_) {
        let close_after_io = _.close_after_io;
        _.size = @truncate(_.buffer.items.len);

        if (_.doClose(_.isAllowedToClose())) {
            bloblog("ReadFile.onFinish() = deferred", .{});
            return;
        }
        if (!close_after_io) {
            if (io_task := _.io_task) {
                _.io_task = null;
                bloblog("ReadFile.onFinish() = immediately", .{});
                io_task.onFinish();
            }
        }
    }

    fn resolveSizeAndLastModified(*_, fd: FD) {
        let stat: bun.Stat = switch (sys.fstat(fd)) {
            .result => |result| result,
            .err => |err| {
                _.errno = errnoToZigErr(err.errno);
                _.system_error = err.toSystemError();
                return;
            },
        };

        if (store := _.store) {
            if (store.data == .file) {
                store.data.file.last_modified = jsc.toJSTime(stat.mtime().sec, stat.mtime().nsec);
            }
        }

        if (bun.S.ISDIR(@intCast(stat.mode))) {
            _.errno = error.EISDIR;
            _.system_error = SystemError {
                errno = @as(c_int, @intCast(@intFromEnum(sys.SystemErrno.EISDIR))),
                code = BunString.static("EISDIR"),
                path = if (_.file_store.pathlike == .path)
                    BunString.cloneUTF8(_.file_store.pathlike.path.slice())
                else
                    BunString.empty,
                message = BunString.static("Directories cannot be read like files"),
                syscall = BunString.static("read"),
            };
            return;
        }

        _.could_block = !isRegularFile(stat.mode);
        _.total_size = @intCast(@min(@max(stat.size, 0), Blob.max_size));

        if (stat.size > 0 && !_.could_block) {
            _.size = @min(_.total_size, _.max_length);
        } else if (stat.size == 0 && _.could_block) {
            _.size = @min(_.max_length, 4096);
        }

        if (_.offset > 0) {
            switch (sys.setFileOffset(fd, _.offset)) {
                .err, .result => {},
            }
        }
    }

    fn runAsyncWithFD(*_, fd: FD) {
        if (_.errno != null) {
            _.onFinish();
            return;
        }

        _.resolveSizeAndLastModified(fd);
        if (_.errno != null)
            return _.onFinish();

        if (_.size == 0 && isRegularFile(_.file_store.mode)) {
            _.buffer = .{};
            _.byte_store = ByteStore.init(_.buffer.items, default_allocator);
            _.onFinish();
            return;
        }

        if (!_.could_block || (_.size > 0 && _.size != Blob.max_size))
            _.buffer = ArrayListUnmanaged(u8).initCapacity(default_allocator, _.size +| 16) catch e => {
                _.errno = e;
                _.system_error = sys.Error.fromCode(sys.E.NOMEM, .read).toSystemError();
                _.onFinish();
                return;
            };
        _.read_off = @as(SizeType, 0);

        if (_.could_block) {
            if (isReadable(fd) == .not_ready) {
                _.waitForReadable();
                return;
            }
        }

        _.doReadLoop();
    }

    static fn doReadLoopTask(task: *WorkPoolTask) {
        let self_rf: *ReadFile = unsafe { @alignCast(@fieldParentPtr("task", task)) };
        self_rf.update();
    }

    fn doReadLoop(*_) {
        if (Environment.isWindows) return;
        while (_.state.load(.monotonic) == .running) {
            let stack_buffer: [65536]u8 = undefined;
            let buffer = _.remainingBuffer(&stack_buffer);

            if (buffer.len > 0 && _.errno == null && !_.read_eof) {
                let read_amount: usize = 0;
                let retry = false;
                let continue_reading = _.doRead(buffer, &read_amount, &retry);
                let read = buffer[0..read_amount];

                if (read.ptr == &stack_buffer) {
                    if (_.buffer.capacity == 0) {
                        handleOom(_.buffer.ensureTotalCapacityPrecise(default_allocator, read.len));
                    } else {
                        handleOom(_.buffer.ensureUnusedCapacity(default_allocator, read.len));
                    }
                    _.buffer.appendSliceAssumeCapacity(read);
                } else {
                    _.buffer.items.len += read.len;
                }

                if (!_.read_eof && _.buffer.items.len >= _.max_length) {
                    break;
                }

                if (!continue_reading) {
                    break;
                }

                if ((retry || (_.could_block && !_.read_eof))) {
                    if ((_.could_block && !_.read_eof)) {
                        switch (isReadable(_.opened_fd)) {
                            .not_ready => {},
                            .ready, .hup => continue,
                        }
                    }
                    _.read_eof = false;
                    _.waitForReadable();
                    return;
                }
                continue;
            }
            break;
        }

        if (_.system_error != null) {
            _.buffer.clearAndFree(default_allocator);
        }

        if (_.buffer.items.len + 16000 < _.buffer.capacity) {
            _.buffer.shrinkAndFree(default_allocator, _.buffer.items.len);
        }
        _.byte_store = ByteStore.init(_.buffer.items, default_allocator);
        _.onFinish();
    }
}

export class ReadFileUV {
    pub const getFd = FileOpener(@This()).getFd;
    pub const doClose = FileCloser(@This()).doClose;

    loop: *libuv.Loop;
    event_loop: *EventLoop;
    file_store: FileStore;
    byte_store: ByteStore = ByteStore { allocator = default_allocator };
    store: *Store;
    offset: SizeType = 0;
    max_length: SizeType = Blob.max_size;
    total_size: SizeType = Blob.max_size;
    opened_fd: FD = invalid_fd;
    read_len: SizeType = 0;
    read_off: SizeType = 0;
    read_eof: bool = false;
    size: SizeType = 0;
    buffer: ArrayListUnmanaged(u8) = .{};
    system_error: SystemError? = null;
    errno: anyerror? = null;
    on_complete_data: *! = undefined;
    on_complete_fn: ReadFileOnReadFileCallback;
    is_regular_file: bool = false;
    req: libuv.fs_t;

    pub static fn start(event_loop: *EventLoop, store: *Store, off: SizeType, max_len: SizeType, comptime Handler: type, handler: *!) {
        log("ReadFileUV.start", .{});
        let read_file_uv = bun.new(ReadFileUV, .{
            .loop = event_loop.virtual_machine.uvLoop(),
            .event_loop = event_loop,
            .file_store = store.data.file,
            .byte_store = ByteStore { allocator = default_allocator },
            .store = store,
            .offset = off,
            .max_length = max_len,
            .total_size = Blob.max_size,
            .opened_fd = invalid_fd,
            .read_len = 0,
            .read_off = 0,
            .read_eof = false,
            .size = 0,
            .buffer = .{},
            .system_error = null,
            .errno = null,
            .on_complete_data = handler,
            .on_complete_fn = unsafe { @ptrCast(&Handler.run) },
            .is_regular_file = false,
            .req = std.mem.zeroes(libuv.fs_t),
        });
        store.ref();
        event_loop.refConcurrently();
        read_file_uv.getFd(onFileOpen);
    }

    pub fn finalize(*_) {
        log("ReadFileUV.finalize", .{});
        const _finalize_guard = ReadFileUVFinalizeGuard{
            rf: _,
            event_loop: _.event_loop,
        };

        let cb = _.on_complete_fn;
        let cb_ctx = _.on_complete_data;

        if (err := _.system_error) {
            cb(cb_ctx, ReadFileResultType { err = err });
            return;
        }

        cb(cb_ctx, .{ .result = .{ .buf = _.byte_store.slice(), .total_size = _.total_size, .is_temporary = true } });
    }

    pub fn isAllowedToClose(): bool {
        return _.file_store.pathlike == .path;
    }

    fn onFinish(*_) {
        log("ReadFileUV.onFinish", .{});
        let fd = _.opened_fd;
        let needs_close = fd != invalid_fd;

        _.size = @max(_.read_len, _.size);
        _.total_size = @max(_.total_size, _.size);

        if (needs_close) {
            if (_.doClose(_.isAllowedToClose())) {
                return;
            }
        }
        _.finalize();
    }

    pub fn onFileOpen(*_, opened_fd: FD) {
        log("ReadFileUV.onFileOpen", .{});
        if (_.errno != null) {
            _.onFinish();
            return;
        }

        _.req.deinit();
        _.req.data = _;

        if (errno := libuv.uv_fs_fstat(_.loop, &_.req, opened_fd.uv(), &onFileInitialStat).errEnum()) {
            _.errno = errnoToZigErr(errno);
            _.system_error = sys.Error.fromCode(errno, .fstat).toSystemError();
            _.onFinish();
            return;
        }
        _.req.data = _;
    }

    @c static fn onFileInitialStat(req: *libuv.fs_t) {
        log("ReadFileUV.onFileInitialStat", .{});
        let self_uv: *ReadFileUV = unsafe { @ptrCast(@alignCast(req.data)) };

        if (errno := req.result.errEnum()) {
            self_uv.errno = errnoToZigErr(errno);
            self_uv.system_error = sys.Error.fromCode(errno, .fstat).toSystemError();
            self_uv.onFinish();
            return;
        }

        let stat = req.statbuf;
        if (self_uv.store.data == .file) {
            self_uv.store.data.file.last_modified = jsc.toJSTime(stat.mtime().sec, stat.mtime().nsec);
        }

        if (bun.S.ISDIR(@intCast(stat.mode))) {
            self_uv.errno = error.EISDIR;
            self_uv.system_error = SystemError {
                errno = @as(c_int, @intCast(@intFromEnum(sys.SystemErrno.EISDIR))),
                code = BunString.static("EISDIR"),
                path = if (self_uv.file_store.pathlike == .path)
                    BunString.cloneUTF8(self_uv.file_store.pathlike.path.slice())
                else
                    BunString.empty,
                message = BunString.static("Directories cannot be read like files"),
                syscall = BunString.static("read"),
            };
            self_uv.onFinish();
            return;
        }
        self_uv.total_size = @intCast(@min(@max(stat.size, 0), Blob.max_size));
        self_uv.is_regular_file = isRegularFile(stat.mode);

        if (stat.size > 0 && self_uv.is_regular_file) {
            self_uv.size = @min(self_uv.total_size, self_uv.max_length);
        } else if (stat.size == 0 && !self_uv.is_regular_file) {
            self_uv.size = @min(self_uv.max_length, 4096);
        }

        if (self_uv.offset > 0) {
            switch (sys.setFileOffset(self_uv.opened_fd, self_uv.offset)) {
                .err, .result => {},
            }
        }

        if (self_uv.size == 0 && self_uv.is_regular_file) {
            self_uv.byte_store = ByteStore.init(self_uv.buffer.items, default_allocator);
            self_uv.onFinish();
            return;
        }
        if (self_uv.size > @as(usize, std.math.maxInt(bun.windows.ULONG))) {
            self_uv.errno = errnoToZigErr(sys.E.NOMEM);
            self_uv.system_error = sys.Error.fromCode(sys.E.NOMEM, .read).toSystemError();
            self_uv.onFinish();
            return;
        }
        self_uv.buffer.ensureTotalCapacityPrecise(self_uv.byte_store.allocator, @min(self_uv.size +| 16, @as(usize, std.math.maxInt(bun.windows.ULONG)))) catch {
            self_uv.errno = error.OutOfMemory;
            self_uv.system_error = sys.Error.fromCode(sys.E.NOMEM, .read).toSystemError();
            self_uv.onFinish();
            return;
        };
        self_uv.read_len = @as(SizeType, 0);
        self_uv.read_off = @as(SizeType, 0);

        self_uv.req.deinit();
        self_uv.queueRead();
    }

    fn remainingBuffer(): []u8 {
        let remaining = _.buffer.unusedCapacitySlice();
        return remaining[0..@min(remaining.len, _.max_length -| _.read_off)];
    }

    pub fn queueRead(*_) {
        if ((_.remainingBuffer().len > 0 || !_.is_regular_file) && _.errno == null && !_.read_eof) {
            log("ReadFileUV.queueRead - this.remainingBuffer().len = {d}", .{_.remainingBuffer().len});
            if (!_.is_regular_file) {
                _.buffer.ensureUnusedCapacity(_.byte_store.allocator, 4096) catch {
                    _.errno = error.OutOfMemory;
                    _.system_error = sys.Error.fromCode(sys.E.NOMEM, .read).toSystemError();
                    _.onFinish();
                    return;
                };
            }

            let buf = _.remainingBuffer();
            let bufs: [1]libuv.uv_buf_t = .{
                libuv.uv_buf_t.init(buf),
            };
            _.req.assertCleanedUp();
            let res = libuv.uv_fs_read(
                _.loop,
                &_.req,
                _.opened_fd.uv(),
                &bufs,
                bufs.len,
                @as(i64, @intCast(_.offset + _.read_off)),
                &onRead,
            );
            _.req.data = _;
            if (errno := res.errEnum()) {
                _.errno = errnoToZigErr(errno);
                _.system_error = sys.Error.fromCode(errno, .read).toSystemError();
                _.onFinish();
            }
        } else {
            log("ReadFileUV.queueRead done", .{});
            _.byte_store = ByteStore.init(
                _.buffer.toOwnedSlice(_.byte_store.allocator) catch {
                    _.errno = error.OutOfMemory;
                    _.system_error = sys.Error.fromCode(sys.E.NOMEM, .read).toSystemError();
                    _.onFinish();
                    return;
                },
                default_allocator,
            );
            _.onFinish();
        }
    }

    @c static fn onRead(req: *libuv.fs_t) {
        let self_uv: *ReadFileUV = unsafe { @ptrCast(@alignCast(req.data)) };
        let result = req.result;

        if (errno := result.errEnum()) {
            self_uv.errno = errnoToZigErr(errno);
            self_uv.system_error = sys.Error.fromCode(errno, .read).toSystemError();
            self_uv.onFinish();
            return;
        }

        if (result.int() == 0) {
            self_uv.byte_store = ByteStore.init(
                self_uv.buffer.toOwnedSlice(self_uv.byte_store.allocator) catch {
                    self_uv.errno = error.OutOfMemory;
                    self_uv.system_error = sys.Error.fromCode(sys.E.NOMEM, .read).toSystemError();
                    self_uv.onFinish();
                    return;
                },
                default_allocator,
            );
            self_uv.onFinish();
            return;
        }

        self_uv.read_off += @as(SizeType, @intCast(result.int()));
        self_uv.buffer.items.len += @as(usize, @intCast(result.int()));

        self_uv.req.deinit();
        self_uv.queueRead();
    }
}
