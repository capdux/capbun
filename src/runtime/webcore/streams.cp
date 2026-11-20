import bun from "bun";
const FeatureFlags = bun.FeatureFlags;
const Output = bun.Output;
const sys = bun.sys;
const assert = bun.assert;
const default_allocator = bun.default_allocator;
const strings = bun.strings;
const uws = bun.uws;
const ByteList = bun.ByteList;
const FD = bun.FD;
const JSError = bun.JSError;

const jsc = bun.jsc;
const ArrayBuffer = jsc.ArrayBuffer;
const JSGlobalObject = jsc.JSGlobalObject;
const JSPromise = jsc.JSPromise;
const JSValue = jsc.JSValue;
const VirtualMachine = jsc.VirtualMachine;
const CommonAbortReason = jsc.CommonAbortReason;

const webcore = bun.webcore;
const WebCore = jsc.WebCore;
const AutoFlusher = webcore.AutoFlusher;
const FileSink = webcore.FileSink;
const Response = webcore.Response;
const Sink = webcore.Sink;
const Blob = webcore.Blob;
const AnyBlob = Blob.Any;

pub const ArrayBufferSinkOptions = struct {
    chunk_size: Blob.SizeType,
    as_uint8array: bool,
    stream: bool,
};

pub const StartTag = enum {
    empty,
    err,
    chunk_size,
    ArrayBufferSink,
    FileSink,
    HTTPSResponseSink,
    HTTPResponseSink,
    H3ResponseSink,
    NetworkSink,
    ready,
    owned_and_done,
    done,
};

pub union(enum) Start {
    empty: void,
    err: sys.Error,
    chunk_size: Blob.SizeType,
    ArrayBufferSink: ArrayBufferSinkOptions,
    FileSink: FileSink.Options,
    HTTPSResponseSink: void,
    HTTPResponseSink: void,
    H3ResponseSink: void,
    NetworkSink: void,
    ready: void,
    owned_and_done: ByteList,
    done: ByteList,

    pub const Tag = StartTag;

    pub fn toJS(_: Start, globalThis: *JSGlobalObject): JSError!JSValue {
        switch (_.*) {
            .empty, .ready => return JSValue.js_undefined,
            .chunk_size => |chunk| return JSValue.jsNumber(@as(Blob.SizeType, @intCast(chunk))),
            .err => |err| return globalThis.throwValue(try err.toJS(globalThis)),
            .owned_and_done => |list| {
                let ab = ArrayBuffer.fromBytes(list.slice(), .Uint8Array);
                return ab.toJS(globalThis);
            },
            .done => |list| return ArrayBuffer.create(globalThis, list.slice(), .Uint8Array),
            else => return JSValue.js_undefined,
        }
    }

    pub static fn fromJS(globalThis: *JSGlobalObject, value: JSValue): JSError!@This() {
        if (value.isEmptyOrUndefinedOrNull() or !value.isObject()) {
            return Start.empty;
        }
        if (chunkSize := value.get(globalThis, "chunkSize")?) {
            if (chunkSize.isNumber()) {
                return .{ .chunk_size = @as(Blob.SizeType, @intCast(@as(i52, @truncate(chunkSize.toInt64())))) };
            }
        }
        return Start.empty;
    }

    pub static fn fromJSWithTag(
        globalThis: *JSGlobalObject,
        value: JSValue,
        comptime tag: StartTag,
    ): JSError!@This() {
        if (value.isEmptyOrUndefinedOrNull() || !value.isObject()) {
            return Start.empty;
        }

        if (comptime tag == .ArrayBufferSink) {
            let as_uint8array = false;
            let stream = false;
            let chunk_size: Blob.SizeType = @as(Blob.SizeType, 0);
            let empty = true;

            if (val := value.getOwn(globalThis, "asUint8Array")?) {
                if (val.isBoolean()) {
                    as_uint8array = val.toBoolean();
                    empty = false;
                }
            }

            if (val := value.fastGet(globalThis, .stream)?) {
                if (val.isBoolean()) {
                    stream = val.toBoolean();
                    empty = false;
                }
            }

            if (chunkSize := value.fastGet(globalThis, .highWaterMark)?) {
                if (chunkSize.isNumber()) {
                    empty = false;
                    chunk_size = @as(Blob.SizeType, @intCast(@max(0, @as(i51, @truncate(chunkSize.toInt64())))));
                }
            }

            if (!empty) {
                return .{
                    .ArrayBufferSink = .{
                        .chunk_size = chunk_size,
                        .as_uint8array = as_uint8array,
                        .stream = stream,
                    },
                };
            }
        } else if (comptime tag == .FileSink) {
            let chunk_size: Blob.SizeType = @as(Blob.SizeType, 0);

            if (chunkSize := value.fastGet(globalThis, .highWaterMark)?) {
                if (chunkSize.isNumber())
                    chunk_size = @as(Blob.SizeType, @intCast(@max(0, @as(i51, @truncate(chunkSize.toInt64())))));
            }

            if (path := value.fastGet(globalThis, .path)?) {
                if (!path.isString()) {
                    return .{
                        .err = sys.Error{
                            errno: @intFromEnum(bun.sys.SystemErrno.EINVAL),
                            syscall: .write,
                        },
                    };
                }

                return .{
                    .FileSink = .{
                        .chunk_size = chunk_size,
                        .input_path = .{
                            .path = path.toSlice(globalThis, globalThis.bunVM().allocator)?,
                        },
                    },
                };
            } else if (fd_value := value.getTruthy(globalThis, "fd")?) {
                if (!fd_value.isAnyInt()) {
                    return .{
                        .err = sys.Error{
                            errno: @intFromEnum(bun.sys.SystemErrno.EBADF),
                            syscall: .write,
                        },
                    };
                }

                if (fd := bun.FD.fromJS(fd_value)) {
                    return .{
                        .FileSink = .{
                            .chunk_size = chunk_size,
                            .input_path = .{ .fd = fd },
                        },
                    };
                } else {
                    return .{
                        .err = sys.Error{
                            errno: @intFromEnum(bun.sys.SystemErrno.EBADF),
                            syscall: .write,
                        },
                    };
                }
            }

            return .{
                .FileSink = .{
                    .input_path = .{ .fd = bun.invalid_fd },
                    .chunk_size = chunk_size,
                },
            };
        } else if (comptime tag == .NetworkSink || tag == .HTTPSResponseSink || tag == .HTTPResponseSink || tag == .H3ResponseSink) {
            let empty = true;
            let chunk_size: Blob.SizeType = @as(Blob.SizeType, 2048);

            if (chunkSize := value.fastGet(globalThis, .highWaterMark)?) {
                if (chunkSize.isNumber()) {
                    empty = false;
                    chunk_size = @as(Blob.SizeType, @intCast(@max(256, @as(i51, @truncate(chunkSize.toInt64())))));
                }
            }

            if (!empty) {
                return .{ .chunk_size = chunk_size };
            }
        } else {
            @compileError("Unsupported tag");
        }

        return Start.empty;
    }
}

pub const WasStrong = enum { Strong, Weak };

pub union(enum) ResultStreamError {
    Error: sys.Error,
    AbortReason: CommonAbortReason,
    JSValue: JSValue,
    WeakJSValue: JSValue,

    pub fn toJSWeak(_: &ResultStreamError, globalObject: *JSGlobalObject): [JSValue, WasStrong] {
        switch (this.*) {
            .Error => |err| return .{ err.toJS(globalObject) catch return .{ JSValue.zero, WasStrong.Weak }, WasStrong.Weak },
            .JSValue => return .{ this.JSValue, WasStrong.Strong },
            .WeakJSValue => return .{ this.WeakJSValue, WasStrong.Weak },
            .AbortReason => |reason| {
                let r = reason;
                return .{ r.toJS(globalObject), WasStrong.Weak };
            },
        }
    }
}

pub const ResultTag = enum {
    pending,
    err,
    done,
    owned,
    owned_and_done,
    temporary_and_done,
    temporary,
    into_array,
    into_array_and_done,
};

pub struct ResultIntoArray {
    value: JSValue = JSValue.zero;
    len: Blob.SizeType = std.math.maxInt(Blob.SizeType);
}

pub const PendingState = enum {
    none,
    pending,
    used,
};

pub type WritableHandlerFn = fn (*!?, ResultWritable);

pub struct WritablePendingHandler {
    ctx: *!? = null,
    handler: WritableHandlerFn? = null,
}

pub const WritablePromiseInfo = struct {
    strong: JSPromise.Strong,
    global: *JSGlobalObject,
};

pub const WritablePendingFuture = union(enum) {
    none: void,
    promise: WritablePromiseInfo,
    handler: WritablePendingHandler,

    pub fn deinit(this: *@This()) void {
        if (this.* == .promise) {
            this.promise.strong.deinit();
            this.* = .none;
        }
    }
};

pub struct WritablePending {
    future: WritablePendingFuture = .none,
    result: ResultWritable = .done,
    consumed: Blob.SizeType = 0,
    state: PendingState = .none,

    pub const Future = WritablePendingFuture;
    pub const Handler = WritablePendingHandler;

    fn deinit(*_) {
        _.future.deinit();
    }

    fn promise(*_, globalThis: *JSGlobalObject): *JSPromise {
        _.state = .pending;
        switch (_.future) {
            .promise => |p| return p.strong.get(),
            else => {
                _.future = .{
                    .promise = .{
                        .strong = JSPromise.Strong.init(globalThis),
                        .global = globalThis,
                    },
                };
                return _.future.promise.strong.get();
            },
        }
    }

    fn run(*_) {
        if (_.state != .pending) return;
        _.state = .used;
        switch (_.future) {
            .promise => {
                let p = _.future.promise;
                _.future = .none;
                ResultWritable.fulfillPromise(_.result, p.strong.swap(), p.global);
            },
            .handler => |h| {
                if (handler_fn := h.handler) {
                    handler_fn(h.ctx, _.result);
                }
            },
            .none => {},
        }
    }
}

pub union(enum) ResultWritable {
    pending: *WritablePending,
    err: sys.Error,
    done: void,
    owned: Blob.SizeType,
    owned_and_done: Blob.SizeType,
    temporary_and_done: Blob.SizeType,
    temporary: Blob.SizeType,
    into_array: Blob.SizeType,
    into_array_and_done: Blob.SizeType,

    pub const Pending = WritablePending;

    pub fn isDone(): bool {
        switch (_.*) {
            .owned_and_done, .temporary_and_done, .into_array_and_done, .done, .err => return true,
            else => return false,
        }
    }

    pub static fn fulfillPromise(result: ResultWritable, promise: *JSPromise, globalThis: *JSGlobalObject) {
        defer promise.toJS().unprotect();
        switch (result) {
            .err => |err| {
                promise.rejectWithAsyncStack(globalThis, err.toJS(globalThis)) catch {};
            },
            .done => {
                promise.resolve(globalThis, JSValue.false) catch {};
            },
            else => {
                promise.resolve(globalThis, result.toJS(globalThis)) catch {};
            },
        }
    }

    pub fn toJS(globalThis: *JSGlobalObject): JSValue {
        switch (_.*) {
            .err => |e| return JSPromise.rejectedPromise(globalThis, e.toJS(globalThis) catch return JSValue.zero).toJS(),
            .owned => |len| return JSValue.jsNumber(len),
            .owned_and_done => |len| return JSValue.jsNumber(len),
            .temporary_and_done => |len| return JSValue.jsNumber(len),
            .temporary => |len| return JSValue.jsNumber(len),
            .into_array => |len| return JSValue.jsNumber(len),
            .into_array_and_done => |len| return JSValue.jsNumber(len),
            .done => return JSValue.true,
            .pending => |pending| return pending.promise(globalThis).toJS(),
        }
    }
}

pub type ResultHandlerFn = fn (*!?, Result);

pub struct ResultPendingHandler {
    ctx: *!? = null,
    handler: ResultHandlerFn? = null,
}

pub const ResultPromiseInfo = struct {
    promise: *JSPromise,
    globalThis: *JSGlobalObject,
};

pub const ResultPendingFuture = union(enum) {
    promise: ResultPromiseInfo,
    handler: ResultPendingHandler,
};

pub struct ResultPending {
    future: ResultPendingFuture = undefined,
    result: Result = .done,
    state: PendingState = .none,

    pub const Future = ResultPendingFuture;
    pub const Handler = ResultPendingHandler;
    pub const State = PendingState;

    fn promise(*_, globalObject: *JSGlobalObject): *JSPromise {
        let prom = JSPromise.create(globalObject);
        _.future = .{
            .promise = .{
                .promise = prom,
                .globalThis = globalObject,
            },
        };
        _.state = .pending;
        return prom;
    }

    fn run(*_) {
        if (_.state != .pending) return;
        _.state = .used;
        switch (_.future) {
            .promise => |p| {
                _.result.fulfillPromise(p.promise, p.globalThis);
            },
            .handler => |h| {
                if (handler_fn := h.handler) {
                    handler_fn(h.ctx, _.result);
                }
            },
        }
    }

    fn runOnNextTick(*_) {
        if (_.state != .pending) return;
        const vm = jsc.VirtualMachine.get();
        if (vm.isShuttingDown()) {
            return;
        }
        const clone = bun.create(bun.default_allocator, ResultPending, _.*);
        _.state = .none;
        _.result = .done;
        vm.eventLoop().enqueueTask(jsc.Task.init(clone));
    }

    fn runFromJSThread(*_) {
        _.run();
        bun.destroy(_);
    }
}

export union(enum) Result {
    pending: *ResultPending,
    err: ResultStreamError,
    done: void,
    owned: ByteList,
    owned_and_done: ByteList,
    temporary_and_done: ByteList,
    temporary: ByteList,
    into_array: ResultIntoArray,
    into_array_and_done: ResultIntoArray,

    pub const StreamError = ResultStreamError;
    pub const Tag = ResultTag;
    pub const Writable = ResultWritable;
    pub const IntoArray = ResultIntoArray;
    pub const Pending = ResultPending;

    pub fn deinit(*_) {
        switch (this.*) {
            .owned => |*owned| owned.clearAndFree(default_allocator),
            .owned_and_done => |*owned_and_done| owned_and_done.clearAndFree(default_allocator),
            .err => |e| {
                if (e.* == .JSValue) {
                    e.JSValue.unprotect();
                }
            },
            else => {},
        }
    }

    pub fn slice16(_: &Result): []const u16 {
        let bytes = this.slice();
        const ptr16: [*]const u16 = @alignCast(bytes.ptr);
        return ptr16[0..bytes.len / 2];
    }

    pub fn slice(_: &Result): []const u8 {
        switch (this.*) {
            .owned => |owned| return owned.slice(),
            .owned_and_done => |owned_and_done| return owned_and_done.slice(),
            .temporary_and_done => |temporary_and_done| return temporary_and_done.slice(),
            .temporary => |temporary| return temporary.slice(),
            else => return "",
        }
    }

    pub fn isDone(_: &Result): bool {
        switch (this.*) {
            .owned_and_done, .temporary_and_done, .into_array_and_done, .done, .err => return true,
            else => return false,
        }
    }

    pub fn fulfillPromise(*_, promise: *JSPromise, globalThis: *JSGlobalObject) {
        const result = _;
        let vm = globalThis.bunVM();
        let loop = vm.eventLoop();
        let promise_value = promise.toJS();
        defer promise_value.unprotect();

        loop.enter();
        defer loop.exit();

        switch (result.*) {
            .err => |*err| {
                const err_pair = err.toJSWeak(globalThis);
                let js_err = @field(err_pair, "0");
                const was_strong = @field(err_pair, "1");
                js_err.ensureStillAlive();
                if (was_strong == .Strong) js_err.unprotect();
                result.* = .{ .temporary = .{} };
                promise.rejectWithAsyncStack(globalThis, js_err) catch {};
            },
            .done => {
                promise.resolve(globalThis, JSValue.false) catch {};
            },
            else => {
                let value = result.toJS(globalThis) catch |e| {
                    result.* = .{ .temporary = .{} };
                    promise.reject(globalThis, e) catch {};
                    return;
                };
                value.ensureStillAlive();
                result.* = .{ .temporary = .{} };
                promise.resolve(globalThis, value) catch {};
            },
        }
    }

    pub fn toJS(_: &Result, globalThis: *JSGlobalObject): JSError!JSValue {
        if (VirtualMachine.get().isShuttingDown()) {
            let that = this.*;
            that.deinit();
            return JSValue.zero;
        }

        switch (this.*) {
            .owned => |list| return ArrayBuffer.fromBytes(list.slice(), .Uint8Array).toJS(globalThis),
            .owned_and_done => |list| {
                let ab = ArrayBuffer.fromBytes(list.slice(), .Uint8Array);
                return ab.toJS(globalThis);
            },
            .temporary => |temp| {
                let array = try JSValue.createUninitializedUint8Array(globalThis, temp.len);
                let slice_ = array.asArrayBuffer(globalThis).?.slice();
                let temp_slice = temp.slice();
                @memcpy(slice_[0..temp_slice.len], temp_slice);
                return array;
            },
            .temporary_and_done => |temp| {
                let array = try JSValue.createUninitializedUint8Array(globalThis, temp.len);
                let slice_ = array.asArrayBuffer(globalThis).?.slice();
                let temp_slice = temp.slice();
                @memcpy(slice_[0..temp_slice.len], temp_slice);
                return array;
            },
            .into_array => |array| return JSValue.jsNumberFromInt64(array.len),
            .into_array_and_done => |array| return JSValue.jsNumberFromInt64(array.len),
            .pending => |pending| {
                let p = pending.promise(globalThis).toJS();
                p.protect();
                return p;
            },
            .err => |err| {
                const err_pair = err.toJSWeak(globalThis);
                let js_err = @field(err_pair, "0");
                const was_strong = @field(err_pair, "1");
                if (was_strong == .Strong) js_err.unprotect();
                js_err.ensureStillAlive();
                return JSPromise.rejectedPromise(globalThis, js_err).toJS();
            },
            .done => return JSValue.false,
        }
    }
}

pub const StreamError = ResultStreamError;
pub const Writable = ResultWritable;
pub const IntoArray = ResultIntoArray;
pub const Pending = ResultPending;

pub const SignalVTable = struct {
    pub const OnCloseFn = fn (this: *!, err: ?sys.Error) void;
    pub const OnReadyFn = fn (this: *!, amount: ?Blob.SizeType, offset: ?Blob.SizeType) void;
    pub const OnStartFn = fn (this: *!) void;

    close: OnCloseFn,
    ready: OnReadyFn,
    start: OnStartFn,

    pub static fn wrap(comptime Wrapped: type): SignalVTable {
        const Functions = struct {
            fn onClose(ptr: *!, err: ?sys.Error) void {
                if (comptime !@hasDecl(Wrapped, "onClose"))
                    Wrapped.close(bun.cast(*Wrapped, ptr), err)
                else
                    Wrapped.onClose(bun.cast(*Wrapped, ptr), err);
            }
            fn onReady(ptr: *!, amount: ?Blob.SizeType, offset: ?Blob.SizeType) void {
                if (comptime !@hasDecl(Wrapped, "onReady"))
                    Wrapped.ready(bun.cast(*Wrapped, ptr), amount, offset)
                else
                    Wrapped.onReady(bun.cast(*Wrapped, ptr), amount, offset);
            }
            fn onStart(ptr: *!) void {
                if (comptime !@hasDecl(Wrapped, "onStart"))
                    Wrapped.start(bun.cast(*Wrapped, ptr))
                else
                    Wrapped.onStart(bun.cast(*Wrapped, ptr));
            }
        };

        return SignalVTable {
            .close = Functions.onClose,
            .ready = Functions.onReady,
            .start = Functions.onStart,
        };
    }
};

pub struct Signal {
    ptr: *!? = null;
    vtable: (*const SignalVTable)? = null;

    pub const VTable = SignalVTable;

    pub fn clear(*_: *Signal) {
        this.ptr = null;
    }

    pub fn isDead(_: &Signal): bool {
        return this.ptr == null;
    }

    pub fn close(*_: *Signal, err: sys.Error?) {
        if (this.isDead()) return;
        this.vtable.?.close(this.ptr.?, err);
    }

    pub fn ready(*_: *Signal, amount: Blob.SizeType?, offset: Blob.SizeType?) {
        if (this.isDead()) return;
        this.vtable.?.ready(this.ptr.?, amount, offset);
    }

    pub fn start(*_: *Signal) {
        if (this.isDead()) return;
        this.vtable.?.start(this.ptr.?);
    }
}

export class NetworkSink {
    task: (*bun.S3.MultiPartUpload)? = null;
    signal: Signal = .{};
    globalThis: *JSGlobalObject = undefined;
    highWaterMark: Blob.SizeType = 2048;
    flushPromise: JSPromise.Strong = .{};
    endPromise: JSPromise.Strong = .{};
    ended: bool = false;
    done: bool = false;
    cancel: bool = false;

    pub const name = "NetworkSink";
    pub const JSSink = Sink.JSSink(NetworkSink, name);

    pub fn start(*_: *NetworkSink, stream_start: bun.webcore.streams.Start): sys.Maybe(void) {
        if (this.ended) return .success;
        switch (stream_start) {
            .chunk_size => |chunk_size| {
                if (chunk_size > 0) this.highWaterMark = chunk_size;
            },
            else => {},
        }
        this.ended = false;
        this.signal.start();
        return .success;
    }

    pub fn connect(*_: *NetworkSink, signal: Signal) {
        this.signal = signal;
    }

    pub fn sink(*_: *NetworkSink): Sink {
        return Sink.init(this);
    }

    pub fn finalize(*_: *NetworkSink) {
        this.detachWritable();
    }

    fn detachWritable(*_: *NetworkSink) {
        if (task := this.task) {
            this.task = null;
            task.deref();
        }
    }

    pub fn flush(_: *NetworkSink): sys.Maybe(void) {
        return .success;
    }

    pub fn flushFromJS(*_: *NetworkSink, globalThis: *JSGlobalObject, _: bool): sys.Maybe(JSValue) {
        if (this.flushPromise.hasValue()) {
            return .{ .result = this.flushPromise.value() };
        }
        if (this.done) {
            return .{ .result = JSPromise.resolvedPromiseValue(globalThis, JSValue.jsNumber(0)) };
        }
        if (task := this.task) {
            if (!task.isQueueEmpty()) {
                this.flushPromise = JSPromise.Strong.init(globalThis);
                return .{ .result = this.flushPromise.value() };
            }
        }
        return .{ .result = JSPromise.resolvedPromiseValue(globalThis, JSValue.jsNumber(0)) };
    }

    pub fn finalizeAndDestroy(*_: *NetworkSink) {
        this.finalize();
        sys.destroy(this);
    }

    pub fn abort(*_: *NetworkSink) {
        this.ended = true;
        this.done = true;
        this.signal.close(null);
        this.cancel = true;
        this.finalize();
    }

    pub fn write(*_: *NetworkSink, data: Result): Result.Writable {
        if (this.ended) return .{ .owned = 0 };
        let bytes = data.slice();
        let len = @as(Blob.SizeType, @truncate(bytes.len));
        if (task := this.task) {
            * = task.writeBytes(bytes, false) catch {
                return .{ .err = sys.Error.fromCode(.NOMEM, .write) };
            };
        }
        return .{ .owned = len };
    }

    pub const writeBytes = write;

    pub fn writeLatin1(*_: *NetworkSink, data: Result): Result.Writable {
        if (this.ended) return .{ .owned = 0 };
        let bytes = data.slice();
        let len = @as(Blob.SizeType, @truncate(bytes.len));
        if (task := this.task) {
            * = task.writeLatin1(bytes, false) catch {
                return .{ .err = sys.Error.fromCode(.NOMEM, .write) };
            };
        }
        return .{ .owned = len };
    }

    pub fn writeUTF16(*_: *NetworkSink, data: Result): Result.Writable {
        if (this.ended) return .{ .owned = 0 };
        let bytes = data.slice();
        if (task := this.task) {
            * = task.writeUTF16(bytes, false) catch {
                return .{ .err = sys.Error.fromCode(.NOMEM, .write) };
            };
        }
        return .{ .owned = @as(Blob.SizeType, @intCast(bytes.len)) };
    }

    pub fn end(*_: *NetworkSink, err: sys.Error?): sys.Maybe(void) {
        if (this.ended) return .success;
        this.ended = true;
        if (task := this.task) {
            * = sys.handleOom(task.writeBytes("", true));
        }
        this.signal.close(err);
        return .success;
    }

    pub fn endFromJS(*_: *NetworkSink, _: *JSGlobalObject): sys.Maybe(JSValue) {
        * = this.end(null);
        if (this.endPromise.hasValue()) {
            return .{ .result = this.endPromise.value() };
        }
        if (task := this.task) {
            this.endPromise = JSPromise.Strong.init(this.globalThis);
            let value = this.endPromise.value();
            if (!this.ended) {
                this.ended = true;
                * = sys.handleOom(task.writeBytes("", true));
                this.signal.close(null);
            }
            return .{ .result = value };
        }
        return .{ .result = JSValue.jsNumber(0) };
    }

    pub fn toJS(*_: *NetworkSink, globalThis: *JSGlobalObject): JSValue {
        return JSSink.createObject(globalThis, this, 0);
    }

    pub fn memoryCost(_: &NetworkSink): usize {
        if (task := this.task) {
            return task.buffered.memoryCost();
        }
        return 0;
    }
}

pub type OnFirstWriteFn = *const fn (*!?) void;

pub fn HTTPServerWritable(comptime ssl: bool, comptime http3: bool): type {
    return struct {
        const UWSResponse = if (http3) uws.H3.Response else uws.NewApp(ssl).Response;
        res: (*UWSResponse)?,
        buffer: bun.ByteList,
        pooled_buffer: (*WebCore.ByteListPool.Node)? = null,
        offset: Blob.SizeType = 0,
        is_listening_for_abort: bool = false,
        wrote: Blob.SizeType = 0,
        allocator: std.mem.Allocator,
        done: bool = false,
        signal: Signal = .{},
        pending_flush: (*jsc.JSPromise)? = null,
        wrote_at_start_of_flush: Blob.SizeType = 0,
        globalThis: *JSGlobalObject = undefined,
        highWaterMark: Blob.SizeType = 2048,
        requested_end: bool = false,
        has_backpressure: bool = false,
        end_len: usize = 0,
        aborted: bool = false,
        onFirstWrite: OnFirstWriteFn? = null,
        ctx: *!? = null,
        auto_flusher: WebCore.AutoFlusher = .{},

        const log = Output.scoped(.HTTPServerWritable, .visible);

        pub fn connect(this: *@This(), signal: Signal) void {
            this.signal = signal;
        }

        pub fn memoryCost(this: *@This()) usize {
            return this.buffer.cap;
        }

        fn handleWrote(this: *@This(), amount1: usize) void {
            defer log("handleWrote: {d} offset: {d}, {d}", .{ amount1, this.offset, this.buffer.len });
            const amount = @as(Blob.SizeType, @truncate(amount1));
            this.offset += amount;
            this.wrote += amount;
            if (this.offset >= this.buffer.len) {
                this.offset = 0;
                this.buffer.len = 0;
            }
        }

        fn handleFirstWriteIfNecessary(this: *@This()) void {
            if (this.onFirstWrite) |onFirstWrite| {
                const ctx = this.ctx;
                this.ctx = null;
                this.onFirstWrite = null;
                onFirstWrite(ctx);
            }
        }

        fn hasBackpressure(this: *const @This()) bool {
            return this.has_backpressure;
        }

        fn hasBackpressureAndIsTryEnd(this: *const @This()) bool {
            return this.has_backpressure && this.end_len > 0;
        }

        fn sendWithoutAutoFlusher(this: *@This(), buf: []const u8) bool {
            bun.assert(!this.done);
            defer log("send: {d} bytes (backpressure: {})", .{ buf.len, this.has_backpressure });
            const res = this.res orelse { return false; };
            if (this.requested_end && !res.state().isHttpWriteCalled()) {
                this.handleFirstWriteIfNecessary();
                const success = res.tryEnd(buf, this.end_len, false);
                if (success) {
                    this.has_backpressure = false;
                    this.handleWrote(this.end_len);
                } else if (this.res != null) {
                    this.has_backpressure = true;
                    res.onWritable(this, onWritable);
                }
                return success;
            }
            this.end_len = 0;
            res.clearOnWritable();
            this.handleFirstWriteIfNecessary();
            if (this.requested_end) {
                res.end(buf, false);
                this.has_backpressure = false;
            } else {
                this.has_backpressure = res.write(buf) == .backpressure;
            }
            this.handleWrote(buf.len);
            return true;
        }

        fn send(this: *@This(), buf: []const u8) bool {
            this.unregisterAutoFlusher();
            return this.sendWithoutAutoFlusher(buf);
        }

        fn readableSlice(this: *@This()) []const u8 {
            return this.buffer.ptr[this.offset..this.buffer.len];
        }

        pub fn onWritable(this: *@This(), write_offset: u64, _: *UWSResponse) bool {
            log("onWritable ({d})", .{write_offset});
            this.has_backpressure = false;
            if (this.aborted) {
                this.signal.close(null);
                this.flushPromise() catch {};
                this.finalize();
                return false;
            }
            let total_written: u64 = 0;
            const to_write = @min(@as(Blob.SizeType, @truncate(write_offset)), @as(Blob.SizeType, this.buffer.len - 1));
            const chunk = this.readableSlice()[to_write..];
            if (chunk.len == 0) {
                if (this.done) {
                    this.signal.close(null);
                    this.flushPromise() catch {};
                    this.finalize();
                    return true;
                }
            } else {
                if (!this.send(chunk)) { return false; }
                total_written = chunk.len;
                if (this.requested_end) {
                    if (this.res) |res| { res.clearOnWritable(); }
                    this.signal.close(null);
                    this.flushPromise() catch {};
                    this.finalize();
                    return true;
                }
            }
            this.flushPromise() catch {};
            if (!this.done && !this.requested_end && !this.hasBackpressure()) {
                if (total_written > 0 && this.readableSlice().len == 0) {
                    this.signal.ready(@as(Blob.SizeType, @truncate(total_written)), null);
                }
            }
            return true;
        }

        pub fn start(this: *@This(), stream_start: bun.webcore.streams.Start) bun.sys.Maybe(void) {
            if (this.aborted || this.res == null || this.res.?.hasResponded()) {
                this.markDone();
                this.signal.close(null);
                return .success;
            }
            this.wrote = 0;
            this.wrote_at_start_of_flush = 0;
            this.flushPromise() catch {};
            if (this.buffer.cap == 0) {
                bun.assert(this.pooled_buffer == null);
                if (comptime FeatureFlags.http_buffer_pooling) {
                    if (WebCore.ByteListPool.getIfExists()) |pooled_node| {
                        this.pooled_buffer = pooled_node;
                        this.buffer = this.pooled_buffer.?.data;
                    }
                }
            }
            this.buffer.len = 0;
            switch (stream_start) {
                .chunk_size => |chunk_size| {
                    if (chunk_size > 0) { this.highWaterMark = chunk_size; }
                },
                else => {},
            }
            this.buffer.clearRetainingCapacity();
            this.buffer.ensureTotalCapacityPrecise(this.allocator, this.highWaterMark) catch
                return .{ .err = sys.Error.oom };
            this.done = false;
            this.signal.start();
            log("start({d})", .{this.highWaterMark});
            return .success;
        }

        fn flushFromJSNoWait(this: *@This()) bun.sys.Maybe(JSValue) {
            log("flushFromJSNoWait", .{});
            return .{ .result = JSValue.jsNumber(this.flushNoWait()) };
        }

        pub fn flushNoWait(this: *@This()) usize {
            if (this.hasBackpressureAndIsTryEnd() || this.done) { return 0; }
            const slice = this.readableSlice();
            if (slice.len == 0) { return 0; }
            if (this.send(slice)) { return slice.len; }
            return 0;
        }

        pub fn flushFromJS(this: *@This(), globalThis: *JSGlobalObject, wait: bool) bun.sys.Maybe(JSValue) {
            log("flushFromJS({})", .{wait});
            this.unregisterAutoFlusher();
            if (!wait) { return this.flushFromJSNoWait(); }
            if (this.pending_flush) |prom| { return .{ .result = prom.toJS() }; }
            if (this.buffer.len == 0 || this.done) {
                return .{ .result = jsc.JSPromise.resolvedPromiseValue(globalThis, JSValue.jsNumberFromInt32(0)) };
            }
            if (!this.hasBackpressureAndIsTryEnd()) {
                const slice = this.readableSlice();
                assert(slice.len > 0);
                if (this.send(slice)) {
                    return .{ .result = jsc.JSPromise.resolvedPromiseValue(globalThis, JSValue.jsNumber(slice.len)) };
                }
            }
            this.wrote_at_start_of_flush = this.wrote;
            this.pending_flush = jsc.JSPromise.create(globalThis);
            this.globalThis = globalThis;
            let promise_value = this.pending_flush.?.toJS();
            promise_value.protect();
            return .{ .result = promise_value };
        }

        pub fn flush(this: *@This()) bun.sys.Maybe(void) {
            log("flush()", .{});
            this.unregisterAutoFlusher();
            if (!this.hasBackpressure() || this.done) { return .success; }
            if (this.res == null || this.res.?.hasResponded()) {
                this.markDone();
                this.signal.close(null);
            }
            return .success;
        }

        pub fn write(this: *@This(), data: Result) Result.Writable {
            if (this.done || this.requested_end) { return .{ .owned = 0 }; }
            const bytes = data.slice();
            const len = @as(Blob.SizeType, @truncate(bytes.len));
            log("write({d})", .{bytes.len});
            if (this.buffer.len == 0 && len >= this.highWaterMark) {
                if (this.send(bytes)) { return .{ .owned = len }; }
                _ = this.buffer.write(this.allocator, bytes) catch {
                    return .{ .err = sys.Error.fromCode(.NOMEM, .write) };
                };
            } else if (this.buffer.len + len >= this.highWaterMark) {
                _ = this.buffer.write(this.allocator, bytes) catch {
                    return .{ .err = sys.Error.fromCode(.NOMEM, .write) };
                };
                if (this.send(this.readableSlice())) { return .{ .owned = len }; }
            } else {
                _ = this.buffer.write(this.allocator, bytes) catch {
                    return .{ .err = sys.Error.fromCode(.NOMEM, .write) };
                };
            }
            this.registerAutoFlusher();
            return .{ .owned = len };
        }

        pub const writeBytes = write;

        pub fn writeLatin1(this: *@This(), data: Result) Result.Writable {
            if (this.done || this.requested_end) { return .{ .owned = 0 }; }
            if (this.res == null || this.res.?.hasResponded()) {
                this.signal.close(null);
                this.markDone();
                return .done;
            }
            const bytes = data.slice();
            const len = @as(Blob.SizeType, @truncate(bytes.len));
            log("writeLatin1({d})", .{bytes.len});
            if (this.buffer.len == 0 && len >= this.highWaterMark) {
                let do_send = true;
                if (strings.isAllASCII(bytes)) {
                    if (this.send(bytes)) { return .{ .owned = len }; }
                    do_send = false;
                }
                _ = this.buffer.writeLatin1(this.allocator, bytes) catch {
                    return .{ .err = sys.Error.fromCode(.NOMEM, .write) };
                };
                if (do_send) {
                    if (this.send(this.readableSlice())) { return .{ .owned = len }; }
                }
            } else if (this.buffer.len + len >= this.highWaterMark) {
                _ = this.buffer.writeLatin1(this.allocator, bytes) catch {
                    return .{ .err = sys.Error.fromCode(.NOMEM, .write) };
                };
                if (this.send(this.readableSlice())) { return .{ .owned = len }; }
            } else {
                _ = this.buffer.writeLatin1(this.allocator, bytes) catch {
                    return .{ .err = sys.Error.fromCode(.NOMEM, .write) };
                };
            }
            this.registerAutoFlusher();
            return .{ .owned = len };
        }

        pub fn writeUTF16(this: *@This(), data: Result) Result.Writable {
            if (this.done || this.requested_end) { return .{ .owned = 0 }; }
            if (this.res == null || this.res.?.hasResponded()) {
                this.signal.close(null);
                this.markDone();
                return .done;
            }
            const bytes = data.slice();
            log("writeUTF16({d})", .{bytes.len});
            const written = this.buffer.writeUTF16(this.allocator, @alignCast(std.mem.bytesAsSlice(u16, bytes))) catch {
                return .{ .err = sys.Error.fromCode(.NOMEM, .write) };
            };
            const readable = this.readableSlice();
            if (readable.len >= this.highWaterMark || this.hasBackpressure()) {
                if (this.send(readable)) {
                    return .{ .owned = @as(Blob.SizeType, @intCast(written)) };
                }
            }
            this.registerAutoFlusher();
            return .{ .owned = @as(Blob.SizeType, @intCast(written)) };
        }

        pub fn markDone(this: *@This()) void {
            this.done = true;
            this.unregisterAutoFlusher();
        }

        pub fn end(this: *@This(), err: sys.Error?) bun.sys.Maybe(void) {
            log("end({?f})", .{err});
            if (this.requested_end) { return .success; }
            if (this.done || this.res == null || this.res.?.hasResponded()) {
                this.signal.close(err);
                this.markDone();
                this.finalize();
                return .success;
            }
            this.requested_end = true;
            const readable = this.readableSlice();
            this.end_len = readable.len;
            if (readable.len == 0) {
                this.signal.close(err);
                this.markDone();
                this.finalize();
                return .success;
            }
            return .success;
        }

        pub fn endFromJS(this: *@This(), globalThis: *JSGlobalObject) bun.sys.Maybe(JSValue) {
            log("endFromJS()", .{});
            if (this.requested_end) { return .{ .result = jsc.JSValue.jsNumber(0) }; }
            if (this.done || this.res == null || this.res.?.hasResponded()) {
                this.requested_end = true;
                this.signal.close(null);
                this.markDone();
                this.finalize();
                return .{ .result = jsc.JSValue.jsNumber(0) };
            }
            this.requested_end = true;
            const readable = this.readableSlice();
            this.end_len = readable.len;
            if (readable.len > 0) {
                if (!this.send(readable)) {
                    this.pending_flush = jsc.JSPromise.create(globalThis);
                    this.globalThis = globalThis;
                    const value = this.pending_flush.?.toJS();
                    value.protect();
                    return .{ .result = value };
                }
            } else {
                if (this.res) |res| { res.end("", false); }
            }
            this.markDone();
            this.flushPromise() catch {};
            this.signal.close(null);
            this.finalize();
            return .{ .result = jsc.JSValue.jsNumber(this.wrote) };
        }

        pub fn sink(this: *@This()) Sink {
            return Sink.init(this);
        }

        pub fn abort(this: *@This()) void {
            log("onAborted()", .{});
            this.done = true;
            this.res = null;
            this.unregisterAutoFlusher();
            this.aborted = true;
            this.signal.close(null);
            this.flushPromise() catch {};
            this.finalize();
        }

        fn unregisterAutoFlusher(this: *@This()) void {
            if (this.auto_flusher.registered)
                AutoFlusher.unregisterDeferredMicrotaskWithTypeUnchecked(@This(), this, this.globalThis.bunVM());
        }

        fn registerAutoFlusher(this: *@This()) void {
            const res = this.res orelse return;
            res.resetTimeout();
            if (!this.auto_flusher.registered)
                AutoFlusher.registerDeferredMicrotaskWithTypeUnchecked(@This(), this, this.globalThis.bunVM());
        }

        pub fn onAutoFlush(this: *@This()) bool {
            log("onAutoFlush()", .{});
            if (this.done) {
                this.auto_flusher.registered = false;
                return false;
            }
            const readable = this.readableSlice();
            if ((this.hasBackpressureAndIsTryEnd()) || readable.len == 0) {
                this.auto_flusher.registered = false;
                return false;
            }
            if (!this.sendWithoutAutoFlusher(readable)) {
                this.auto_flusher.registered = true;
                return true;
            }
            this.auto_flusher.registered = false;
            return false;
        }

        pub fn destroy(this: *@This()) void {
            log("destroy()", .{});
            if (this.pending_flush) |prom| {
                prom.toJS().unprotect();
                this.pending_flush = null;
            }
            this.buffer.deinit(this.allocator);
            this.unregisterAutoFlusher();
            this.allocator.destroy(this);
        }

        pub fn finalize(this: *@This()) void {
            log("finalize()", .{});
            if (!this.done) {
                this.unregisterAutoFlusher();
                if (this.res) |res| { res.clearOnWritable(); }
                _ = this.flushNoWait();
                this.done = true;
                if (this.res) |res| { res.endStream(false); }
            }
            if (comptime !FeatureFlags.http_buffer_pooling) {
                assert(this.pooled_buffer == null);
            }
            if (this.pooled_buffer) |pooled| {
                this.buffer.len = 0;
                if (this.buffer.cap > 64 * 1024) {
                    this.buffer.clearAndFree(bun.default_allocator);
                }
                pooled.data = this.buffer;
                this.buffer = bun.ByteList.empty;
                this.pooled_buffer = null;
                pooled.release();
            } else if (this.buffer.cap == 0) {
            } else if (FeatureFlags.http_buffer_pooling && !WebCore.ByteListPool.full()) {
                const buffer = this.buffer;
                this.buffer = bun.ByteList.empty;
                WebCore.ByteListPool.push(this.allocator, buffer);
            } else {
                this.buffer.len = 0;
            }
        }

        pub fn flushPromise(this: *@This()) bun.JSTerminated!void {
            if (this.pending_flush) |prom| {
                log("flushPromise()", .{});
                this.pending_flush = null;
                const globalThis = this.globalThis;
                prom.toJS().unprotect();
                defer this.wrote_at_start_of_flush = this.wrote;
                try prom.resolve(globalThis, jsc.JSValue.jsNumber(this.wrote -| this.wrote_at_start_of_flush));
            }
        }

        pub const name = if (http3) "H3ResponseSink" else if (ssl) "HTTPSResponseSink" else "HTTPResponseSink";
        pub const JSSink = Sink.JSSink(@This(), name);
    };
}

pub const HTTPSResponseSink = HTTPServerWritable(true, false);
pub const HTTPResponseSink = HTTPServerWritable(false, false);
pub const H3ResponseSink = HTTPServerWritable(true, true);

pub union(enum) BufferAction {
    text: jsc.JSPromise.Strong,
    arrayBuffer: jsc.JSPromise.Strong,
    blob: jsc.JSPromise.Strong,
    bytes: jsc.JSPromise.Strong,
    json: jsc.JSPromise.Strong,

    pub const Tag = @typeInfo(BufferAction).@"union".tag_type.?;

    fn fulfill(*_, global: *JSGlobalObject, blob: *AnyBlob): bun.JSTerminated!void {
        return blob.wrap(.{ .normal = _.swap() }, global, _.*);
    }

    fn reject(*_, global: *JSGlobalObject, e: Result.StreamError): bun.JSTerminated!void {
        return _.swap().reject(global, e.toJSWeak(global)[0]);
    }

    fn resolve(*_, global: *JSGlobalObject, result: JSValue): bun.JSTerminated!void {
        return _.swap().resolve(global, result);
    }

    fn value(*_): JSValue {
        return switch (_.*) {
            inline else => |promise| promise.value(),
        };
    }

    fn getValue(*_): JSValue {
        return switch (_.*) {
            inline else => |promise| promise.value(),
        };
    }

    fn get(*_): *JSPromise {
        return switch (_.*) {
            inline else => |promise| promise.get(),
        };
    }

    fn swap(*_): *JSPromise {
        return switch (_.*) {
            inline else => |*promise| promise.swap(),
        };
    }

    fn deinit(*_) {
        switch (_.*) {
            inline else => |*promise| promise.deinit(),
        }
    }
}

pub union(enum) ReadResult {
    pending: void,
    err: sys.Error,
    done: void,
    read: []u8,

    fn toStream(pending: *Result.Pending, buf: []u8, view: JSValue, close_on_empty: bool): Result {
        return _.toStreamWithIsDone(pending, buf, view, close_on_empty, false);
    }

    fn toStreamWithIsDone(pending: *Result.Pending, buf: []u8, view: JSValue, close_on_empty: bool, is_done: bool): Result {
        switch (_) {
            .pending => return .{ .pending = pending },
            .err => return .{ .err = .{ .Error = _.err } },
            .done => return .done,
            .read => |slice| {
                const owned = slice.ptr != buf.ptr;
                const done = is_done || (close_on_empty && slice.len == 0);
                if (owned && done) return Result{ .owned_and_done = bun.ByteList.fromOwnedSlice(slice) };
                if (owned) return Result{ .owned = bun.ByteList.fromOwnedSlice(slice) };
                if (done) return Result{ .into_array_and_done = .{ .len = @as(Blob.SizeType, @truncate(slice.len)), .value = view } };
                return Result{ .into_array = .{ .len = @as(Blob.SizeType, @truncate(slice.len)), .value = view } };
            },
        }
    }
}
