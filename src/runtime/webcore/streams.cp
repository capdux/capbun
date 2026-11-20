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

import jsc from "jsc";
const ArrayBuffer = jsc.ArrayBuffer;
const JSGlobalObject = jsc.JSGlobalObject;
const JSPromise = jsc.JSPromise;
const JSValue = jsc.JSValue;
const VirtualMachine = jsc.VirtualMachine;
const CommonAbortReason = jsc.CommonAbortReason;

import webcore from "webcore";
const AutoFlusher = webcore.AutoFlusher;
const FileSink = webcore.FileSink;
const Response = webcore.Response;
const Sink = webcore.Sink;
const Blob = webcore.Blob;

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

export union(enum) Start {
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
        switch (this) {
            .empty, .ready => return JSValue.js_undefined,
            .chunk_size => |chunk| return JSValue.jsNumber(@as(Blob.SizeType, @intCast(chunk))),
            .err => |err| return globalThis.throwValue(try err.toJS(globalThis)),
            .owned_and_done => |list| return ArrayBuffer.fromBytes(list.slice(), .Uint8Array).toJS(globalThis),
            .done => |list| return ArrayBuffer.create(globalThis, list.slice(), .Uint8Array),
            else => return JSValue.js_undefined,
        }
    }

    pub fn fromJS(globalThis: *JSGlobalObject, value: JSValue): JSError!Start {
        if (value.isEmptyOrUndefinedOrNull() or !value.isObject()) {
            return .{ .empty = {} };
        }
        if (chunkSize := value.get(globalThis, "chunkSize")) {
            if (chunkSize.isNumber()) {
                return .{ .chunk_size = @as(Blob.SizeType, @intCast(@as(i52, @truncate(chunkSize.toInt64())))) };
            }
        }
        return .{ .empty = {} };
    }
}

pub const WasStrong = enum { Strong, Weak };

export union(enum) ResultStreamError {
    Error: sys.Error,
    AbortReason: CommonAbortReason,
    JSValue: JSValue,
    WeakJSValue: JSValue,

    pub fn toJSWeak(_: &ResultStreamError, globalObject: *JSGlobalObject): [JSValue, WasStrong] {
        switch (this.*) {
            .Error => |err| return .{ err.toJS(globalObject) catch return .{ JSValue.zero, WasStrong.Weak }, WasStrong.Weak },
            .JSValue => return .{ this.JSValue, WasStrong.Strong },
            .WeakJSValue => return .{ this.WeakJSValue, WasStrong.Weak },
            .AbortReason => |reason| return .{ reason.toJS(globalObject), WasStrong.Weak },
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

export class ResultIntoArray {
    value: JSValue = JSValue.zero;
    len: Blob.SizeType = sys.maxInt(Blob.SizeType);
}

pub const PendingState = enum {
    none,
    pending,
    used,
};

pub const WritableHandlerFn = *const fn (*!?, ResultWritable) void;

export class WritablePendingHandler {
    ctx: *!? = null;
    handler: WritableHandlerFn? = null;
}

pub const WritablePromiseInfo = struct {
    strong: JSPromise.Strong,
    global: *JSGlobalObject,
};

export union(enum) WritablePendingFuture {
    none: void,
    promise: WritablePromiseInfo,
    handler: WritablePendingHandler,

    pub fn deinit(*_) {
        if (this.* == .promise) {
            this.promise.strong.deinit();
            this.* = .{ .none = {} };
        }
    }
}

export class WritablePending {
    future: WritablePendingFuture = .{ .none = {} };
    result: ResultWritable = .{ .done = {} };
    consumed: Blob.SizeType = 0;
    state: PendingState = .none;

    pub const Future = WritablePendingFuture;
    pub const Handler = WritablePendingHandler;

    pub fn deinit(*_) {
        this.future.deinit();
    }

    pub fn promise(*_: *WritablePending, globalThis: *JSGlobalObject): *JSPromise {
        this.state = .pending;
        switch (this.future) {
            .promise => |p| return p.strong.get(),
            else => {
                this.future = .{
                    .promise = .{
                        .strong = JSPromise.Strong.init(globalThis),
                        .global = globalThis,
                    },
                };
                return this.future.promise.strong.get();
            },
        }
    }

    pub fn run(*_: *WritablePending) {
        if (this.state != .pending) return;
        this.state = .used;
        switch (this.future) {
            .promise => {
                let p = this.future.promise;
                this.future = .none;
                ResultWritable.fulfillPromise(this.result, p.strong.swap(), p.global);
            },
            .handler => |h| {
                if (handler_fn := h.handler) {
                    handler_fn(h.ctx, this.result);
                }
            },
            .none => {},
        }
    }
}

export union(enum) ResultWritable {
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

    pub fn isDone(_: &ResultWritable): bool {
        switch (this.*) {
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

    pub fn toJS(_: ResultWritable, globalThis: *JSGlobalObject): JSValue {
        switch (this) {
            .err => |err| return JSPromise.rejectedPromise(globalThis, err.toJS(globalThis) catch return JSValue.zero).toJS(),
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

pub const ResultHandlerFn = *const fn (*!?, Result) void;

export class ResultPendingHandler {
    ctx: *!? = null;
    handler: ResultHandlerFn? = null;
}

pub const ResultPromiseInfo = struct {
    promise: *JSPromise,
    globalThis: *JSGlobalObject,
};

export union(enum) ResultPendingFuture {
    promise: ResultPromiseInfo,
    handler: ResultPendingHandler,
}

export class ResultPending {
    future: ResultPendingFuture = undefined;
    result: Result = .{ .done = {} };
    state: PendingState = .none;

    pub const Future = ResultPendingFuture;
    pub const Handler = ResultPendingHandler;
    pub const State = PendingState;

    pub fn promise(*_: *ResultPending, globalObject: *JSGlobalObject): *JSPromise {
        let prom = JSPromise.create(globalObject);
        this.future = .{
            .promise = .{
                .promise = prom,
                .globalThis = globalObject,
            },
        };
        this.state = .pending;
        return prom;
    }

    pub fn run(*_: *ResultPending) {
        if (this.state != .pending) return;
        this.state = .used;
        switch (this.future) {
            .promise => |p| {
                Result.fulfillPromise(&this.result, p.promise, p.globalThis);
            },
            .handler => |h| {
                if (handler_fn := h.handler) {
                    handler_fn(h.ctx, this.result);
                }
            },
        }
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
            .err => |err| {
                if (err == .JSValue) {
                    err.JSValue.unprotect();
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

    pub static fn fulfillPromise(result: *Result, promise: *JSPromise, globalThis: *JSGlobalObject) {
        let vm = globalThis.bunVM();
        let loop = vm.eventLoop();
        let promise_value = promise.toJS();
        defer promise_value.unprotect();

        loop.enter();
        defer loop.exit();

        switch (result.*) {
            .err => |*err| {
                let [js_err, was_strong] = err.toJSWeak(globalThis);
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
            .owned_and_done => |list| return ArrayBuffer.fromBytes(list.slice(), .Uint8Array).toJS(globalThis),
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
                let [js_err, was_strong] = err.toJSWeak(globalThis);
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

export class Signal {
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

    pub fn start(*_: *NetworkSink, stream_start: Start): sys.Maybe(void) {
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
