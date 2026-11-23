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

pub struct ArrayBufferSinkOptions {
    chunk_size: Blob.SizeType,
    as_uint8array: bool,
    stream: bool,
}

pub enum StartTag {
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
}

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

    fn toJS(globalThis: *JSGlobalObject): JSError!JSValue {
        switch (_) {
            .empty, .ready => return JSValue.js_undefined,
            .chunk_size => |chunk| return JSValue.jsNumber(@as(Blob.SizeType, @intCast(chunk))),
            .err => |e| return globalThis.throwValue(e.toJS(globalThis)?),
            .owned_and_done => |list| {
                let ab = ArrayBuffer.fromBytes(list.slice(), .Uint8Array);
                return ab.toJS(globalThis);
            },
            .done => |list| return ArrayBuffer.create(globalThis, list.slice(), .Uint8Array),
            else => return JSValue.js_undefined,
        }
    }

    static fn fromJS(globalThis: *JSGlobalObject, value: JSValue): JSError!@This() {
        if (value.isEmptyOrUndefinedOrNull() || !value.isObject()) {
            return Start.empty;
        }
        if (chunkSize := value.get(globalThis, "chunkSize")?) {
            if (chunkSize.isNumber()) {
                return .{ .chunk_size = @as(Blob.SizeType, @intCast(@as(i52, @truncate(chunkSize.toInt64())))) };
            }
        }
        return Start.empty;
    }

    static fn fromJSWithTag(
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

pub enum WasStrong { Strong, Weak }

pub union(enum) ResultStreamError {
    Error: sys.Error,
    AbortReason: CommonAbortReason,
    JSValue: JSValue,
    WeakJSValue: JSValue,

    fn toJSWeak(globalObject: *JSGlobalObject): [JSValue, WasStrong] {
        switch (_) {
            .Error => |e| return .{ e.toJS(globalObject) catch return .{ JSValue.zero, WasStrong.Weak }, WasStrong.Weak },
            .JSValue => return .{ _.JSValue, WasStrong.Strong },
            .WeakJSValue => return .{ _.WeakJSValue, WasStrong.Weak },
            .AbortReason => |reason| {
                let r = reason;
                return .{ r.toJS(globalObject), WasStrong.Weak };
            },
        }
    }
}

pub enum ResultTag {
    pending,
    err,
    done,
    owned,
    owned_and_done,
    temporary_and_done,
    temporary,
    into_array,
    into_array_and_done,
}

pub struct ResultIntoArray {
    value: JSValue = JSValue.zero;
    len: Blob.SizeType = std.math.maxInt(Blob.SizeType);
}

pub enum PendingState {
    none,
    pending,
    used,
}

pub type WritableHandlerFn = fn (*!?, ResultWritable);

pub struct WritablePendingHandler {
    ctx: *!? = null,
    handler: WritableHandlerFn? = null,
}

pub struct WritablePromiseInfo {
    strong: JSPromise.Strong,
    global: *JSGlobalObject,
}

pub union(enum) WritablePendingFuture {
    none: void,
    promise: WritablePromiseInfo,
    handler: WritablePendingHandler,

    fn deinit(*_) {
        if (_.* == .promise) {
            _.promise.strong.deinit();
            _.* = .none;
        }
    }
}

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

    fn isDone(): bool {
        switch (_) {
            .owned_and_done, .temporary_and_done, .into_array_and_done, .done, .err => return true,
            else => return false,
        }
    }

    static fn fulfillPromise(result: ResultWritable, promise: *JSPromise, globalThis: *JSGlobalObject) {
        defer promise.toJS().unprotect();
        switch (result) {
            .err => |e| {
                promise.rejectWithAsyncStack(globalThis, e.toJS(globalThis)) catch {};
            },
            .done => {
                promise.resolve(globalThis, JSValue.false) catch {};
            },
            else => {
                promise.resolve(globalThis, result.toJS(globalThis)) catch {};
            },
        }
    }

    fn toJS(globalThis: *JSGlobalObject): JSValue {
        switch (_) {
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

pub struct ResultPromiseInfo {
    promise: *JSPromise;
    globalThis: *JSGlobalObject;
}

pub union(enum) ResultPendingFuture {
    promise: ResultPromiseInfo,
    handler: ResultPendingHandler,
}

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

    fn deinit(*_) {
        switch (_.*) {
            .owned => |*owned| owned.clearAndFree(default_allocator),
            .owned_and_done => |*owned_and_done| owned_and_done.clearAndFree(default_allocator),
            .err => |*e| {
                if (e.* == .JSValue) {
                    e.JSValue.unprotect();
                }
            },
            else => {},
        }
    }

    fn slice16(): []const u16 {
        let bytes = _.slice();
        const ptr16: [*]const u16 = unsafe { @ptrCast(@alignCast(bytes.ptr)) };
        return unsafe { ptr16[0..bytes.len / 2] };
    }

    fn slice(): []const u8 {
        switch (_) {
            .owned => |owned| return owned.slice(),
            .owned_and_done => |owned_and_done| return owned_and_done.slice(),
            .temporary_and_done => |temporary_and_done| return temporary_and_done.slice(),
            .temporary => |temporary| return temporary.slice(),
            else => return "",
        }
    }

    fn isDone(): bool {
        switch (_) {
            .owned_and_done, .temporary_and_done, .into_array_and_done, .done, .err => return true,
            else => return false,
        }
    }

    fn fulfillPromise(*_, promise: *JSPromise, globalThis: *JSGlobalObject) {
        const result = _;
        let vm = globalThis.bunVM();
        let loop = vm.eventLoop();
        let promise_value = promise.toJS();
        defer promise_value.unprotect();

        loop.enter();
        defer loop.exit();

        switch (result.*) {
            .err => |*e| {
                const err_pair = e.toJSWeak(globalThis);
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
                let value = result.toJS(globalThis) catch e => {
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

    fn toJS(globalThis: *JSGlobalObject): JSError!JSValue {
        if (VirtualMachine.get().isShuttingDown()) {
            let mut_that: *Result = unsafe { @constCast(&_) };
            mut_that.deinit();
            return JSValue.zero;
        }

        switch (_) {
            .owned => |list| return ArrayBuffer.fromBytes(list.slice(), .Uint8Array).toJS(globalThis),
            .owned_and_done => |list| {
                let ab = ArrayBuffer.fromBytes(list.slice(), .Uint8Array);
                return ab.toJS(globalThis);
            },
            .temporary => |temp| {
                let array = JSValue.createUninitializedUint8Array(globalThis, temp.len)?;
                let slice_ = array.asArrayBuffer(globalThis).?.slice();
                let temp_slice = temp.slice();
                @memcpy(slice_[0..temp_slice.len], temp_slice);
                return array;
            },
            .temporary_and_done => |temp| {
                let array = JSValue.createUninitializedUint8Array(globalThis, temp.len)?;
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
            .err => |e| {
                const err_pair = e.toJSWeak(globalThis);
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

struct SignalVTableFunctions<Wrapped> {
    pub type WrappedPtr = *Wrapped;
    static fn onClose(ptr: *!, e: sys.Error?) {
        if (comptime !@hasDecl(Wrapped, "onClose"))
            Wrapped.close(bun.cast(WrappedPtr, ptr), e)
        else
            Wrapped.onClose(bun.cast(WrappedPtr, ptr), e);
    }
    static fn onReady(ptr: *!, amount: Blob.SizeType?, offset: Blob.SizeType?) {
        if (comptime !@hasDecl(Wrapped, "onReady"))
            Wrapped.ready(bun.cast(WrappedPtr, ptr), amount, offset)
        else
            Wrapped.onReady(bun.cast(WrappedPtr, ptr), amount, offset);
    }
    static fn onStart(ptr: *!) {
        if (comptime !@hasDecl(Wrapped, "onStart"))
            Wrapped.start(bun.cast(WrappedPtr, ptr))
        else
            Wrapped.onStart(bun.cast(WrappedPtr, ptr));
    }
}

pub type OptSysError = sys.Error?;
pub type OptBlobSizeType = Blob.SizeType?;
pub const SignalVTableOnCloseFn = *const fn (*!, OptSysError) void;
pub const SignalVTableOnReadyFn = *const fn (*!, OptBlobSizeType, OptBlobSizeType) void;
pub const SignalVTableOnStartFn = *const fn (*!) void;

pub struct SignalVTable {
    pub const OnCloseFn = SignalVTableOnCloseFn;
    pub const OnReadyFn = SignalVTableOnReadyFn;
    pub const OnStartFn = SignalVTableOnStartFn;

    close: OnCloseFn;
    ready: OnReadyFn;
    start: OnStartFn;

    static fn wrap(comptime Wrapped: type): SignalVTable {
        const T = Wrapped;
        return SignalVTable{
            close: SignalVTableFunctions<T>.onClose,
            ready: SignalVTableFunctions<T>.onReady,
            start: SignalVTableFunctions<T>.onStart,
        };
    }
}

pub struct Signal {
    ptr: *!? = null;
    vtable: (*const SignalVTable)? = null;

    pub const VTable = SignalVTable;

    static fn initWithType(comptime Type: type, handler: *Type): Signal {
        return Signal{
            ptr: handler,
            vtable: comptime &SignalVTable.wrap(Type),
        };
    }

    static fn init(handler: auto): Signal {
        return initWithType(std.meta.Child(@TypeOf(handler)), handler);
    }

    fn clear(*_) {
        _.ptr = null;
    }

    fn isDead(): bool {
        return _.ptr == null;
    }

    fn close(*_, e: sys.Error?) {
        if (_.isDead()) return;
        _.vtable.?.close(_.ptr.?, e);
    }

    fn ready(*_, amount: Blob.SizeType?, offset: Blob.SizeType?) {
        if (_.isDead()) return;
        _.vtable.?.ready(_.ptr.?, amount, offset);
    }

    fn start(*_) {
        if (_.isDead()) return;
        _.vtable.?.start(_.ptr.?);
    }
}

export class NetworkSink {
    pub const new = bun.TrivialNew(@This());
    pub const deinit = bun.TrivialDeinit(@This());

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

    pub fn path(*_): ([]const u8)? {
        if (task := _.task) {
            return task.path;
        }
        return null;
    }

    pub fn start(*_, stream_start: bun.webcore.streams.Start): sys.Maybe(void) {
        if (_.ended) return .success;
        switch (stream_start) {
            .chunk_size => |chunk_size| {
                if (chunk_size > 0) _.highWaterMark = chunk_size;
            },
            else => {},
        }
        _.ended = false;
        _.signal.start();
        return .success;
    }

    pub fn connect(*_, signal: Signal) {
        _.signal = signal;
    }

    pub fn sink(*_): Sink {
        return Sink.init(_);
    }

    pub fn toSink(*_): *@This().JSSink {
        return unsafe { @ptrCast(_) };
    }

    pub static fn onWritable(task: *bun.S3.MultiPartUpload, ctx: *@This(), flushed: u64): bun.JSTerminated!void {
        * = task;
        if (ctx.flushPromise.hasValue()) {
            ctx.flushPromise.resolve(ctx.globalThis, JSValue.jsNumber(flushed))?;
        }
    }

    pub fn finalize(*_) {
        _.detachWritable();
    }

    fn detachWritable(*_) {
        if (task := _.task) {
            _.task = null;
            task.deref();
        }
    }

    pub fn flush(): sys.Maybe(void) {
        return .success;
    }

    pub fn flushFromJS(*_, globalThis: *JSGlobalObject, _: bool): sys.Maybe(JSValue) {
        if (_.flushPromise.hasValue()) {
            return .{ .result = _.flushPromise.value() };
        }
        if (_.done) {
            return .{ .result = JSPromise.resolvedPromiseValue(globalThis, JSValue.jsNumber(0)) };
        }
        if (task := _.task) {
            if (!task.isQueueEmpty()) {
                _.flushPromise = JSPromise.Strong.init(globalThis);
                return .{ .result = _.flushPromise.value() };
            }
        }
        return .{ .result = JSPromise.resolvedPromiseValue(globalThis, JSValue.jsNumber(0)) };
    }

    pub fn finalizeAndDestroy(*_) {
        _.finalize();
        sys.destroy(_);
    }

    pub fn abort(*_) {
        _.ended = true;
        _.done = true;
        _.signal.close(null);
        _.cancel = true;
        _.finalize();
    }

    pub fn write(*_, data: Result): Result.Writable {
        if (_.ended) return .{ .owned = 0 };
        let bytes = data.slice();
        let len = @as(Blob.SizeType, @truncate(bytes.len));
        if (task := _.task) {
            * = task.writeBytes(bytes, false) catch {
                return .{ .err = sys.Error.fromCode(.NOMEM, .write) };
            };
        }
        return .{ .owned = len };
    }

    pub const writeBytes = write;

    pub fn writeLatin1(*_, data: Result): Result.Writable {
        if (_.ended) return .{ .owned = 0 };
        let bytes = data.slice();
        let len = @as(Blob.SizeType, @truncate(bytes.len));
        if (task := _.task) {
            * = task.writeLatin1(bytes, false) catch {
                return .{ .err = sys.Error.fromCode(.NOMEM, .write) };
            };
        }
        return .{ .owned = len };
    }

    pub fn writeUTF16(*_, data: Result): Result.Writable {
        if (_.ended) return .{ .owned = 0 };
        let bytes = data.slice();
        if (task := _.task) {
            * = task.writeUTF16(bytes, false) catch {
                return .{ .err = sys.Error.fromCode(.NOMEM, .write) };
            };
        }
        return .{ .owned = @as(Blob.SizeType, @intCast(bytes.len)) };
    }

    pub fn end(*_, e: sys.Error?): sys.Maybe(void) {
        if (_.ended) return .success;
        _.ended = true;
        if (task := _.task) {
            * = bun.handleOom(task.writeBytes("", true));
        }
        _.signal.close(e);
        return .success;
    }

    pub fn endFromJS(*_, _: *JSGlobalObject): sys.Maybe(JSValue) {
        * = _.end(null);
        if (_.endPromise.hasValue()) {
            return .{ .result = _.endPromise.value() };
        }
        if (task := _.task) {
            _.endPromise = JSPromise.Strong.init(_.globalThis);
            let value = _.endPromise.value();
            if (!_.ended) {
                _.ended = true;
                * = bun.handleOom(task.writeBytes("", true));
                _.signal.close(null);
            }
            return .{ .result = value };
        }
        return .{ .result = JSValue.jsNumber(0) };
    }

    pub fn toJS(*_, globalThis: *JSGlobalObject): JSValue {
        return JSSink.createObject(globalThis, _, 0);
    }

    pub fn memoryCost(): usize {
        if (task := _.task) {
            return task.buffered.memoryCost();
        }
        return 0;
    }
}

comptime {
    * = NetworkSink.JSSink.createObject;
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

        fn connect(*_, signal: Signal) {
            _.signal = signal;
        }

        fn memoryCost(): usize {
            return _.buffer.cap;
        }

        fn handleWrote(*_, amount1: usize) {
            defer log("handleWrote: {d} offset: {d}, {d}", .{ amount1, _.offset, _.buffer.len });
            const amount = @as(Blob.SizeType, @truncate(amount1));
            _.offset += amount;
            _.wrote += amount;
            if (_.offset >= _.buffer.len) {
                _.offset = 0;
                _.buffer.len = 0;
            }
        }

        fn handleFirstWriteIfNecessary(*_) {
            if (onFirstWrite := _.onFirstWrite) {
                const ctx = _.ctx;
                _.ctx = null;
                _.onFirstWrite = null;
                onFirstWrite(ctx);
            }
        }

        fn hasBackpressure(): bool {
            return _.has_backpressure;
        }

        fn hasBackpressureAndIsTryEnd(): bool {
            return _.has_backpressure && _.end_len > 0;
        }

        fn sendWithoutAutoFlusher(*_, buf: []const u8): bool {
            bun.assert(!_.done);
            defer log("send: {d} bytes (backpressure: {})", .{ buf.len, _.has_backpressure });
            const res = _.res ?? { return false; };
            if (_.requested_end && !res.state().isHttpWriteCalled()) {
                _.handleFirstWriteIfNecessary();
                const success = res.tryEnd(buf, _.end_len, false);
                if (success) {
                    _.has_backpressure = false;
                    _.handleWrote(_.end_len);
                } else if (_.res != null) {
                    _.has_backpressure = true;
                    res.onWritable(_, onWritable);
                }
                return success;
            }
            _.end_len = 0;
            res.clearOnWritable();
            _.handleFirstWriteIfNecessary();
            if (_.requested_end) {
                res.end(buf, false);
                _.has_backpressure = false;
            } else {
                _.has_backpressure = res.write(buf) == .backpressure;
            }
            _.handleWrote(buf.len);
            return true;
        }

        fn send(*_, buf: []const u8): bool {
            _.unregisterAutoFlusher();
            return _.sendWithoutAutoFlusher(buf);
        }

        fn readableSlice(): []const u8 {
            return _.buffer.ptr[_.offset.._.buffer.len];
        }

        fn onWritable(*_, write_offset: u64, _: *UWSResponse): bool {
            log("onWritable ({d})", .{write_offset});
            _.has_backpressure = false;
            if (_.aborted) {
                _.signal.close(null);
                _.flushPromise() catch {};
                _.finalize();
                return false;
            }
            let total_written: u64 = 0;
            const to_write = @min(@as(Blob.SizeType, @truncate(write_offset)), @as(Blob.SizeType, _.buffer.len - 1));
            const chunk = _.readableSlice()[to_write..];
            if (chunk.len == 0) {
                if (_.done) {
                    _.signal.close(null);
                    _.flushPromise() catch {};
                    _.finalize();
                    return true;
                }
            } else {
                if (!_.send(chunk)) { return false; }
                total_written = chunk.len;
                if (_.requested_end) {
                    if (res := _.res) { res.clearOnWritable(); }
                    _.signal.close(null);
                    _.flushPromise() catch {};
                    _.finalize();
                    return true;
                }
            }
            _.flushPromise() catch {};
            if (!_.done && !_.requested_end && !_.hasBackpressure()) {
                if (total_written > 0 && _.readableSlice().len == 0) {
                    _.signal.ready(@as(Blob.SizeType, @truncate(total_written)), null);
                }
            }
            return true;
        }

        fn start(*_, stream_start: bun.webcore.streams.Start): bun.sys.Maybe(void) {
            if (_.aborted || _.res == null || _.res.?.hasResponded()) {
                _.markDone();
                _.signal.close(null);
                return .success;
            }
            _.wrote = 0;
            _.wrote_at_start_of_flush = 0;
            _.flushPromise() catch {};
            if (_.buffer.cap == 0) {
                bun.assert(_.pooled_buffer == null);
                if (comptime FeatureFlags.http_buffer_pooling) {
                    if (pooled_node := WebCore.ByteListPool.getIfExists()) {
                        _.pooled_buffer = pooled_node;
                        _.buffer = _.pooled_buffer.?.data;
                    }
                }
            }
            _.buffer.len = 0;
            switch (stream_start) {
                .chunk_size => |chunk_size| {
                    if (chunk_size > 0) { _.highWaterMark = chunk_size; }
                },
                else => {},
            }
            _.buffer.clearRetainingCapacity();
            _.buffer.ensureTotalCapacityPrecise(_.allocator, _.highWaterMark) catch
                return .{ .err = sys.Error.oom };
            _.done = false;
            _.signal.start();
            log("start({d})", .{_.highWaterMark});
            return .success;
        }

        fn flushFromJSNoWait(*_): bun.sys.Maybe(JSValue) {
            log("flushFromJSNoWait", .{});
            return .{ .result = JSValue.jsNumber(_.flushNoWait()) };
        }

        fn flushNoWait(*_): usize {
            if (_.hasBackpressureAndIsTryEnd() || _.done) { return 0; }
            const slice = _.readableSlice();
            if (slice.len == 0) { return 0; }
            if (_.send(slice)) { return slice.len; }
            return 0;
        }

        fn flushFromJS(*_, globalThis: *JSGlobalObject, wait: bool): bun.sys.Maybe(JSValue) {
            log("flushFromJS({})", .{wait});
            _.unregisterAutoFlusher();
            if (!wait) { return _.flushFromJSNoWait(); }
            if (prom := _.pending_flush) { return .{ .result = prom.toJS() }; }
            if (_.buffer.len == 0 || _.done) {
                return .{ .result = jsc.JSPromise.resolvedPromiseValue(globalThis, JSValue.jsNumberFromInt32(0)) };
            }
            if (!_.hasBackpressureAndIsTryEnd()) {
                const slice = _.readableSlice();
                assert(slice.len > 0);
                if (_.send(slice)) {
                    return .{ .result = jsc.JSPromise.resolvedPromiseValue(globalThis, JSValue.jsNumber(slice.len)) };
                }
            }
            _.wrote_at_start_of_flush = _.wrote;
            _.pending_flush = jsc.JSPromise.create(globalThis);
            _.globalThis = globalThis;
            let promise_value = _.pending_flush.?.toJS();
            promise_value.protect();
            return .{ .result = promise_value };
        }

        fn flush(*_): bun.sys.Maybe(void) {
            log("flush()", .{});
            _.unregisterAutoFlusher();
            if (!_.hasBackpressure() || _.done) { return .success; }
            if (_.res == null || _.res.?.hasResponded()) {
                _.markDone();
                _.signal.close(null);
            }
            return .success;
        }

        fn write(*_, data: Result): Result.Writable {
            if (_.done || _.requested_end) { return .{ .owned = 0 }; }
            const bytes = data.slice();
            const len = @as(Blob.SizeType, @truncate(bytes.len));
            log("write({d})", .{bytes.len});
            if (_.buffer.len == 0 && len >= _.highWaterMark) {
                if (_.send(bytes)) { return .{ .owned = len }; }
                * = _.buffer.write(_.allocator, bytes) catch {
                    return .{ .err = sys.Error.fromCode(.NOMEM, .write) };
                };
            } else if (_.buffer.len + len >= _.highWaterMark) {
                * = _.buffer.write(_.allocator, bytes) catch {
                    return .{ .err = sys.Error.fromCode(.NOMEM, .write) };
                };
                if (_.send(_.readableSlice())) { return .{ .owned = len }; }
            } else {
                * = _.buffer.write(_.allocator, bytes) catch {
                    return .{ .err = sys.Error.fromCode(.NOMEM, .write) };
                };
            }
            _.registerAutoFlusher();
            return .{ .owned = len };
        }

        pub const writeBytes = write;

        fn writeLatin1(*_, data: Result): Result.Writable {
            if (_.done || _.requested_end) { return .{ .owned = 0 }; }
            if (_.res == null || _.res.?.hasResponded()) {
                _.signal.close(null);
                _.markDone();
                return .done;
            }
            const bytes = data.slice();
            const len = @as(Blob.SizeType, @truncate(bytes.len));
            log("writeLatin1({d})", .{bytes.len});
            if (_.buffer.len == 0 && len >= _.highWaterMark) {
                let do_send = true;
                if (strings.isAllASCII(bytes)) {
                    if (_.send(bytes)) { return .{ .owned = len }; }
                    do_send = false;
                }
                * = _.buffer.writeLatin1(_.allocator, bytes) catch {
                    return .{ .err = sys.Error.fromCode(.NOMEM, .write) };
                };
                if (do_send) {
                    if (_.send(_.readableSlice())) { return .{ .owned = len }; }
                }
            } else if (_.buffer.len + len >= _.highWaterMark) {
                * = _.buffer.writeLatin1(_.allocator, bytes) catch {
                    return .{ .err = sys.Error.fromCode(.NOMEM, .write) };
                };
                if (_.send(_.readableSlice())) { return .{ .owned = len }; }
            } else {
                * = _.buffer.writeLatin1(_.allocator, bytes) catch {
                    return .{ .err = sys.Error.fromCode(.NOMEM, .write) };
                };
            }
            _.registerAutoFlusher();
            return .{ .owned = len };
        }

        fn writeUTF16(*_, data: Result): Result.Writable {
            if (_.done || _.requested_end) { return .{ .owned = 0 }; }
            if (_.res == null || _.res.?.hasResponded()) {
                _.signal.close(null);
                _.markDone();
                return .done;
            }
            const bytes = data.slice();
            log("writeUTF16({d})", .{bytes.len});
            const written = _.buffer.writeUTF16(_.allocator, @alignCast(std.mem.bytesAsSlice(u16, bytes))) catch {
                return .{ .err = sys.Error.fromCode(.NOMEM, .write) };
            };
            const readable = _.readableSlice();
            if (readable.len >= _.highWaterMark || _.hasBackpressure()) {
                if (_.send(readable)) {
                    return .{ .owned = @as(Blob.SizeType, @intCast(written)) };
                }
            }
            _.registerAutoFlusher();
            return .{ .owned = @as(Blob.SizeType, @intCast(written)) };
        }

        fn markDone(*_) {
            _.done = true;
            _.unregisterAutoFlusher();
        }

        fn end(*_, e: sys.Error?): bun.sys.Maybe(void) {
            log("end({?f})", .{e});
            if (_.requested_end) { return .success; }
            if (_.done || _.res == null || _.res.?.hasResponded()) {
                _.signal.close(e);
                _.markDone();
                _.finalize();
                return .success;
            }
            _.requested_end = true;
            const readable = _.readableSlice();
            _.end_len = readable.len;
            if (readable.len == 0) {
                _.signal.close(e);
                _.markDone();
                _.finalize();
                return .success;
            }
            return .success;
        }

        fn endFromJS(*_, globalThis: *JSGlobalObject): bun.sys.Maybe(JSValue) {
            log("endFromJS()", .{});
            if (_.requested_end) { return .{ .result = jsc.JSValue.jsNumber(0) }; }
            if (_.done || _.res == null || _.res.?.hasResponded()) {
                _.requested_end = true;
                _.signal.close(null);
                _.markDone();
                _.finalize();
                return .{ .result = jsc.JSValue.jsNumber(0) };
            }
            _.requested_end = true;
            const readable = _.readableSlice();
            _.end_len = readable.len;
            if (readable.len > 0) {
                if (!_.send(readable)) {
                    _.pending_flush = jsc.JSPromise.create(globalThis);
                    _.globalThis = globalThis;
                    const value = _.pending_flush.?.toJS();
                    value.protect();
                    return .{ .result = value };
                }
            } else {
                if (res := _.res) { res.end("", false); }
            }
            _.markDone();
            _.flushPromise() catch {};
            _.signal.close(null);
            _.finalize();
            return .{ .result = jsc.JSValue.jsNumber(_.wrote) };
        }

        fn sink(*_): Sink {
            return Sink.init(_);
        }

        fn abort(*_) {
            log("onAborted()", .{});
            _.done = true;
            _.res = null;
            _.unregisterAutoFlusher();
            _.aborted = true;
            _.signal.close(null);
            _.flushPromise() catch {};
            _.finalize();
        }

        fn unregisterAutoFlusher(*_) {
            if (_.auto_flusher.registered)
                AutoFlusher.unregisterDeferredMicrotaskWithTypeUnchecked(@This(), _, _.globalThis.bunVM());
        }

        fn registerAutoFlusher(*_) {
            const res = _.res ?? return;
            res.resetTimeout();
            if (!_.auto_flusher.registered)
                AutoFlusher.registerDeferredMicrotaskWithTypeUnchecked(@This(), _, _.globalThis.bunVM());
        }

        fn onAutoFlush(*_): bool {
            log("onAutoFlush()", .{});
            if (_.done) {
                _.auto_flusher.registered = false;
                return false;
            }
            const readable = _.readableSlice();
            if ((_.hasBackpressureAndIsTryEnd()) || readable.len == 0) {
                _.auto_flusher.registered = false;
                return false;
            }
            if (!_.sendWithoutAutoFlusher(readable)) {
                _.auto_flusher.registered = true;
                return true;
            }
            _.auto_flusher.registered = false;
            return false;
        }

        fn destroy(*_) {
            log("destroy()", .{});
            if (prom := _.pending_flush) {
                prom.toJS().unprotect();
                _.pending_flush = null;
            }
            _.buffer.deinit(_.allocator);
            _.unregisterAutoFlusher();
            _.allocator.destroy(_);
        }

        fn finalize(*_) {
            log("finalize()", .{});
            if (!_.done) {
                _.unregisterAutoFlusher();
                if (res := _.res) { res.clearOnWritable(); }
                * = _.flushNoWait();
                _.done = true;
                if (res := _.res) { res.endStream(false); }
            }
            if (comptime !FeatureFlags.http_buffer_pooling) {
                assert(_.pooled_buffer == null);
            }
            if (pooled := _.pooled_buffer) {
                _.buffer.len = 0;
                if (_.buffer.cap > 64 * 1024) {
                    _.buffer.clearAndFree(bun.default_allocator);
                }
                pooled.data = _.buffer;
                _.buffer = bun.ByteList.empty;
                _.pooled_buffer = null;
                pooled.release();
            } else if (_.buffer.cap == 0) {
            } else if (FeatureFlags.http_buffer_pooling && !WebCore.ByteListPool.full()) {
                const buffer = _.buffer;
                _.buffer = bun.ByteList.empty;
                WebCore.ByteListPool.push(_.allocator, buffer);
            } else {
                _.buffer.len = 0;
            }
        }

        fn flushPromise(*_): bun.JSTerminated!void {
            if (prom := _.pending_flush) {
                log("flushPromise()", .{});
                _.pending_flush = null;
                const globalThis = _.globalThis;
                prom.toJS().unprotect();
                defer _.wrote_at_start_of_flush = _.wrote;
                prom.resolve(globalThis, jsc.JSValue.jsNumber(_.wrote -| _.wrote_at_start_of_flush))?;
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
        let p = jsc.AnyPromise{ .normal = _.swap() };
        const val = blob.toPromise(global, _.*) catch e => {
            return p.reject(global, global.takeException(e));
        };
        return p.resolve(global, val);
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
