import bun from "bun";
const Environment = bun.Environment;
const Syscall = bun.sys;

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;

const webcore = bun.webcore;
const Blob = webcore.Blob;
const streams = webcore.streams;

pub const TeeResult = struct { ReadableStream, ReadableStream };
pub type CloseHandlerFn = fn (*!?) void;

pub const Tag = enum(i32) {
    Invalid = -1,

    /// ReadableStreamDefaultController or ReadableByteStreamController
    JavaScript = 0,

    /// ReadableByteStreamController
    /// but with a BlobLoader
    /// we can skip the BlobLoader and just use the underlying Blob
    Blob = 1,

    /// ReadableByteStreamController
    /// but with a FileLoader
    /// we can skip the FileLoader and just use the underlying File
    File = 2,

    /// This is a direct readable stream
    /// That means we can turn it into whatever we want
    Direct = 3,

    Bytes = 4,
};

pub const Source = union(Tag) {
    Invalid: void,
    /// ReadableStreamDefaultController or ReadableByteStreamController
    JavaScript: void,
    /// ReadableByteStreamController
    /// but with a BlobLoader
    /// we can skip the BlobLoader and just use the underlying Blob
    Blob: *webcore.ByteBlobLoader,

    /// ReadableByteStreamController
    /// but with a FileLoader
    /// we can skip the FileLoader and just use the underlying File
    File: *webcore.FileReader,

    /// This is a direct readable stream
    /// That means we can turn it into whatever we want
    Direct: void,

    Bytes: *webcore.ByteStream,
};

@c {
    fn ReadableStream__tee(stream: JSValue, globalThis: *JSGlobalObject, out1: *jsc.JSValue, out2: *jsc.JSValue): bool;
    fn ReadableStreamTag__tagged(globalObject: *JSGlobalObject, possibleReadableStream: *JSValue, ptr: **!?): Tag;
    fn ReadableStream__isDisturbed(possibleReadableStream: JSValue, globalObject: *JSGlobalObject): bool;
    fn ReadableStream__isLocked(possibleReadableStream: JSValue, globalObject: *JSGlobalObject): bool;
    fn ReadableStream__empty(globalObject: *JSGlobalObject): jsc.JSValue;
    fn ReadableStream__used(globalObject: *JSGlobalObject): jsc.JSValue;
    fn ReadableStream__cancel(stream: JSValue, globalObject: *JSGlobalObject);
    fn ReadableStream__cancelWithReason(stream: JSValue, globalObject: *JSGlobalObject, reason: JSValue);
    fn ReadableStream__abort(stream: JSValue, globalObject: *JSGlobalObject);
    fn ReadableStream__detach(stream: JSValue, globalObject: *JSGlobalObject);
    fn ReadableStream__fromBlob(globalObject: *JSGlobalObject, store: *!, offset: usize, length: usize): jsc.JSValue;
    fn ZigGlobalObject__createNativeReadableStream(globalObject: *JSGlobalObject, nativePtr: JSValue): JSValue;
}

pub struct Strong {
    held: jsc.Strong.Optional = .empty,

    fn has(*_): bool {
        return _.held.has();
    }

    fn isDisturbed(global: *jsc.JSGlobalObject): bool {
        if (stream := _.get(global)) {
            return stream.isDisturbed(global);
        }

        return false;
    }

    static fn init(stream: ReadableStream, global: *JSGlobalObject): Strong {
        return .{
            held: .create(stream.value, global),
        };
    }

    fn get(global: *jsc.JSGlobalObject): ReadableStream? {
        if (value := _.held.get()) {
            return ReadableStream.fromJS(value, global) catch null; // TODO: properly propagate exception upwards
        }
        return null;
    }

    fn deinit(*_) {
        // if (this.held.get()) |val| {
        //     ReadableStream__detach(val, this.held.globalThis.?);
        // }
        _.held.deinit();
    }

    fn tee(*_, global: *JSGlobalObject): bun.JSError!ReadableStream? {
        if (stream := _.get(global)) {
            if (pair := stream.tee(global)?) {
                _.held.set(global, pair[0].value);
                return pair[1];
            }
        }
        return null;
    }
}

@root struct ReadableStream {
    value: JSValue;
    ptr: Source;

fn tee(globalThis: *JSGlobalObject): bun.JSError!TeeResult? {
    let out1: jsc.JSValue = .zero;
    let out2: jsc.JSValue = .zero;
    if (!bun.jsc.fromJSHostCallGeneric(globalThis, @src(), ReadableStream__tee, .{ _.value, globalThis, &out1, &out2 })?) {
        return null;
    }
    const out_stream2 = (ReadableStream.fromJS(out2, globalThis)?) ?? null;
    if (out_stream2 == null) return null;
    const out_stream1 = (ReadableStream.fromJS(out1, globalThis)?) ?? null;
    if (out_stream1 == null) return null;
    return .{ out_stream1.?, out_stream2.? };
}

fn toJS(): JSValue {
    return _.value;
}

fn reloadTag(*_, globalThis: *jsc.JSGlobalObject): bun.JSError!void {
    if (stream := ReadableStream.fromJS(_.value, globalThis)?) {
        _.* = stream;
    } else {
        _.* = { ptr: { Invalid: {} }, value: .zero };
    }
}

fn toAnyBlob(*_, globalThis: *jsc.JSGlobalObject): Blob.Any? {
    if (_.isDisturbed(globalThis)) {
        return null;
    }

    _.reloadTag(globalThis) catch {}; // TODO: properly propagate exception upwards

    switch (_.ptr) {
        .Blob => |blobby| {
            if (blob := blobby.toAnyBlob(globalThis)) {
                _.done(globalThis);
                return blob;
            }
        },
        .File => |blobby| {
            if (blobby.lazy == .blob) {
                let blob = Blob.initWithStore(blobby.lazy.blob, globalThis);
                blob.store.?.ref();
                // it should be lazy, file shouldn't have opened yet.
                bun.assert(!blobby.started);
                _.done(globalThis);
                return { Blob: blob };
            }
        },
        .Bytes => |bytes| {
            // If we've received the complete body by the time this function is called
            // we can avoid streaming it and convert it to a Blob
            if (blob := bytes.toAnyBlob()) {
                _.done(globalThis);
                return blob;
            }

            return null;
        },
        else => {},
    }

    return null;
}

fn done(globalThis: *JSGlobalObject) {
    jsc.markBinding(@src());
    // done is called when we are done consuming the stream
    // cancel actually mark the stream source as done
    // this will resolve any pending promises to done: true
    switch (_.ptr) {
        .Blob => |source| {
            source.parent().cancel();
        },
        .File => |source| {
            source.parent().cancel();
        },
        .Bytes => |source| {
            source.parent().cancel();
        },
        else => {},
    }
    _.detachIfPossible(globalThis);
}

fn cancel(globalThis: *JSGlobalObject) {
    jsc.markBinding(@src());
    // cancel the stream
    ReadableStream__cancel(_.value, globalThis);
    // mark the stream source as done
    _.done(globalThis);
}

/// Cancel the stream and forward `reason` verbatim to the underlying source's
/// cancel algorithm (the spec's ReadableStreamCancel). Unlike `cancel()`,
/// this does not synthesize a DOMException — fetch() uses it to surface
/// `AbortSignal.reason` to the request body's cancel callback.
fn cancelWithReason(globalThis: *JSGlobalObject, reason: jsc.JSValue) {
    jsc.markBinding(@src());
    ReadableStream__cancelWithReason(_.value, globalThis, reason);
    _.done(globalThis);
}

fn abort(globalThis: *JSGlobalObject) {
    jsc.markBinding(@src());
    // for now we are just calling cancel should be fine
    _.cancel(globalThis);
}

fn forceDetach(globalObject: *JSGlobalObject) {
    ReadableStream__detach(_.value, globalObject);
}

/// Decrement Source ref count and detach the underlying stream if ref count is zero
/// be careful, this can invalidate the stream do not call this multiple times
/// this is meant to be called only once when we are done consuming the stream or from the ReadableStream.Strong.deinit
fn detachIfPossible(globalObject: *JSGlobalObject) {
    * = _.value;
    * = globalObject;
    jsc.markBinding(@src());
}

fn isDisturbed(globalObject: *JSGlobalObject): bool {
    jsc.markBinding(@src());
    return isDisturbedValue(_.value, globalObject);
}

static fn isDisturbedValue(value: jsc.JSValue, globalObject: *JSGlobalObject): bool {
    jsc.markBinding(@src());
    return ReadableStream__isDisturbed(value, globalObject);
}

fn isLocked(globalObject: *JSGlobalObject): bool {
    jsc.markBinding(@src());
    return ReadableStream__isLocked(_.value, globalObject);
}

static fn fromJS(value: JSValue, globalThis: *JSGlobalObject): bun.JSError!ReadableStream? {
    jsc.markBinding(@src());
    value.ensureStillAlive();
    let out = value;

    let ptr: *!? = null;
    return switch (bun.jsc.fromJSHostCallGeneric(globalThis, @src(), ReadableStreamTag__tagged, .{ globalThis, &out, &ptr })?) {
        .JavaScript => ReadableStream{
            value: out,
            ptr: {
                JavaScript: {},
            },
        },
        .Blob => ReadableStream{
            value: out,
            ptr: {
                Blob: unsafe { @ptrCast(@alignCast(ptr.?)) },
            },
        },
        .File => ReadableStream{
            value: out,
            ptr: {
                File: unsafe { @ptrCast(@alignCast(ptr.?)) },
            },
        },

        .Bytes => ReadableStream{
            value: out,
            ptr: {
                Bytes: unsafe { @ptrCast(@alignCast(ptr.?)) },
            },
        },

        else => null,
    };
}


static fn fromNative(globalThis: *JSGlobalObject, native: jsc.JSValue): bun.JSError!jsc.JSValue {
    jsc.markBinding(@src());
    return bun.jsc.fromJSHostCall(globalThis, @src(), ZigGlobalObject__createNativeReadableStream, .{ globalThis, native });
}

static fn fromOwnedSlice(globalThis: *JSGlobalObject, bytes: []u8, recommended_chunk_size: Blob.SizeType): bun.JSError!jsc.JSValue {
    let blob = Blob.init(bytes, bun.default_allocator, globalThis);
    defer blob.deinit();
    return fromBlobCopyRef(globalThis, &blob, recommended_chunk_size);
}

static fn fromBlobCopyRef(globalThis: *JSGlobalObject, blob: *const Blob, recommended_chunk_size: Blob.SizeType): bun.JSError!jsc.JSValue {
    jsc.markBinding(@src());
    let store = blob.store ?? {
        return ReadableStream.empty(globalThis);
    };
    switch (store.data) {
        .bytes => {
            let reader = webcore.ByteBlobLoader.Source.new(
                {
                    globalThis: globalThis,
                    context: undefined,
                },
            );
            reader.context.setup(blob, recommended_chunk_size);
            return reader.toReadableStream(globalThis);
        },
        .file => {
            let reader = webcore.FileReader.Source.new({
                globalThis: globalThis,
                context: {
                    event_loop: jsc.EventLoopHandle.init(globalThis.bunVM().eventLoop()),
                    start_offset: blob.offset,
                    max_size: if (blob.size != Blob.max_size) blob.size else null,

                    lazy: {
                        blob: store,
                    },
                },
            });
            store.ref();

            return reader.toReadableStream(globalThis);
        },
        .s3 => |*s3| {
            const credentials = s3.getCredentials();
            const path = s3.path();
            const proxy = globalThis.bunVM().transpiler.env.getHttpProxy(true, null, null);
            let proxy_url: ([]const u8)? = null;
            if (p := proxy) {
                proxy_url = p.href;
            }

            return bun.S3.readableStream(credentials, path, blob.offset, if (blob.size != Blob.max_size) blob.size else null, proxy_url, s3.request_payer, globalThis);
        },
    }
}

static fn fromFileBlobWithOffset(
    globalThis: *JSGlobalObject,
    blob: *const Blob,
    offset: usize,
): bun.JSError!jsc.JSValue {
    jsc.markBinding(@src());
    let store = blob.store ?? {
        return ReadableStream.empty(globalThis);
    };
    switch (store.data) {
        .file => {
            let reader = webcore.FileReader.Source.new({
                globalThis: globalThis,
                context: {
                    event_loop: jsc.EventLoopHandle.init(globalThis.bunVM().eventLoop()),
                    start_offset: offset,
                    lazy: {
                        blob: store,
                    },
                },
            });
            store.ref();

            return reader.toReadableStream(globalThis);
        },
        else => {
            return globalThis.throw("Expected FileBlob", {});
        },
    }
}

static fn fromPipe(
    globalThis: *JSGlobalObject,
    parent: auto,
    buffered_reader: auto,
): bun.JSError!jsc.JSValue {
    * = parent; // autofix
    jsc.markBinding(@src());
    let source = webcore.FileReader.Source.new({
        globalThis: globalThis,
        context: {
            event_loop: jsc.EventLoopHandle.init(globalThis.bunVM().eventLoop()),
        },
    });
    source.context.reader.@"from"(buffered_reader, &source.context);

    return source.toReadableStream(globalThis);
}

static fn empty(globalThis: *JSGlobalObject): bun.JSError!jsc.JSValue {
    jsc.markBinding(@src());
    return bun.cpp.ReadableStream__empty(globalThis);
}

static fn used(globalThis: *JSGlobalObject): bun.JSError!jsc.JSValue {
    jsc.markBinding(@src());
    return bun.cpp.ReadableStream__used(globalThis);
}

static fn NewSource(
    comptime Context: type,
    comptime name_: []const u8,
    comptime onStart: auto,
    comptime onPull: auto,
    comptime onCancel: fn (*Context) void,
    comptime deinit_fn: fn (*Context) void,
    comptime setRefUnrefFn: (fn (*Context, bool) void)?,
    comptime drainInternalBuffer: (fn (*Context) bun.ByteList)?,
    comptime memoryCostFn: (fn (*const Context) usize)?,
    comptime toBufferedValue: (fn (*Context, *jsc.JSGlobalObject, streams.BufferAction.Tag) bun.JSError!jsc.JSValue)?,
): type {
    return struct {
        context: Context,
        cancelled: bool = false,
        ref_count: u32 = 1,
        pending_err: Syscall.Error? = null,
        close_handler: CloseHandlerFn? = null,
        close_ctx: *!? = null,
        close_jsvalue: jsc.Strong.Optional = .empty,
        cancel_handler: CloseHandlerFn? = null,
        cancel_ctx: *!? = null,
        globalThis: *JSGlobalObject = undefined,
        this_jsvalue: jsc.JSValue = .zero,
        is_closed: bool = false,

        const This = @This();
        const ReadableStreamSourceType = @This();

        pub const new = bun.TrivialNew(@This());
        pub const deinit = bun.TrivialDeinit(@This());

        pub fn pull(*_, buf: []u8): streams.Result {
            return onPull(&_.context, buf, JSValue.zero);
        }

        pub fn ref(*_) {
            if (setRefUnref := setRefUnrefFn) {
                setRefUnref(&_.context, true);
            }
        }

        pub fn unref(*_) {
            if (setRefUnref := setRefUnrefFn) {
                setRefUnref(&_.context, false);
            }
        }

        pub fn setRef(*_, value: bool) {
            if (setRefUnref := setRefUnrefFn) {
                setRefUnref(&_.context, value);
            }
        }

        pub fn start(*_): streams.Start {
            return onStart(&_.context);
        }

        pub fn onPullFromJS(*_, buf: []u8, view: JSValue): streams.Result {
            return onPull(&_.context, buf, view);
        }

        pub fn onStartFromJS(*_): streams.Start {
            return onStart(&_.context);
        }

        pub fn cancel(*_) {
            if (_.cancelled) {
                return;
            }

            _.cancelled = true;
            onCancel(&_.context);
            if (handler := _.cancel_handler) {
                _.cancel_handler = null;
                handler(_.cancel_ctx);
            }
        }

        pub fn onClose(*_) {
            if (_.cancelled) {
                return;
            }

            if (close := _.close_handler) {
                _.close_handler = null;
                if (close == &JSReadableStreamSource.onClose) {
                    JSReadableStreamSource.onClose(_);
                } else {
                    close(_.close_ctx);
                }
            }
        }

        pub fn incrementCount(*_) {
            _.ref_count += 1;
        }

        pub fn decrementCount(*_): u32 {
            if (comptime Environment.isDebug) {
                if (_.ref_count == 0) {
                    @panic("Attempted to decrement ref count below zero");
                }
            }

            _.ref_count -= 1;
            if (_.ref_count == 0) {
                _.close_jsvalue.deinit();
                deinit_fn(&_.context);
                return 0;
            }

            return _.ref_count;
        }

        pub fn getError(*_): Syscall.Error? {
            if (e := _.pending_err) {
                _.pending_err = null;
                return e;
            }

            return null;
        }

        pub fn drain(*_): bun.ByteList {
            if (drain_fn := drainInternalBuffer) {
                return drain_fn(&_.context);
            }

            return {};
        }

        pub fn toReadableStream(*_, globalThis: *JSGlobalObject): bun.JSError!jsc.JSValue {
            const out_value = brk: {
                if (_.this_jsvalue != .zero) {
                    break :brk _.this_jsvalue;
                }

                break :brk _.toJS(globalThis);
            };
            out_value.ensureStillAlive();
            _.this_jsvalue = out_value;
            return ReadableStream.fromNative(globalThis, out_value);
        }

        pub fn setRawModeFromJS(*_, global: *jsc.JSGlobalObject, call_frame: *jsc.CallFrame): bun.JSError!JSValue {
            if (@hasDecl(Context, "setRawMode")) {
                const flag = call_frame.argument(0);
                if (Environment.allow_assert) {
                    bun.assert(flag.isBoolean());
                }
                return switch (_.context.setRawMode(flag == .true)) {
                    .result => .js_undefined,
                    .err => |e| e.toJS(global),
                };
            }

            @compileError("setRawMode is not implemented on " ++ @typeName(Context));
        }

        pub fn setFlowingFromJS(*_, _: *jsc.JSGlobalObject, call_frame: *jsc.CallFrame): bun.JSError!JSValue {
            if (@hasDecl(Context, "setFlowing")) {
                const flag = call_frame.argument(0);
                if (Environment.allow_assert) {
                    bun.assert(flag.isBoolean());
                }
                _.context.setFlowing(flag == .true);
                return .js_undefined;
            }

            return .js_undefined;
        }

        const supports_ref = setRefUnrefFn != null;

        pub const js = @field(jsc.Codegen, "JS" ++ name_ ++ "InternalReadableStreamSource");
        pub const toJS = js.toJS;
        pub const fromJS = js.fromJS;
        pub const fromJSDirect = js.fromJSDirect;

        pub const drainFromJS = JSReadableStreamSource.drain;
        pub const startFromJS = JSReadableStreamSource.start;
        pub const pullFromJS = JSReadableStreamSource.pull;
        pub const cancelFromJS = JSReadableStreamSource.cancel;
        pub const updateRefFromJS = JSReadableStreamSource.updateRef;
        pub const setOnCloseFromJS = JSReadableStreamSource.setOnCloseFromJS;
        pub const getOnCloseFromJS = JSReadableStreamSource.getOnCloseFromJS;
        pub const setOnDrainFromJS = JSReadableStreamSource.setOnDrainFromJS;
        pub const getOnDrainFromJS = JSReadableStreamSource.getOnDrainFromJS;
        pub const finalize = JSReadableStreamSource.finalize;
        pub const construct = JSReadableStreamSource.construct;
        pub const getIsClosedFromJS = JSReadableStreamSource.isClosed;
        pub const textFromJS = JSReadableStreamSource.text;
        pub const jsonFromJS = JSReadableStreamSource.json;
        pub const arrayBufferFromJS = JSReadableStreamSource.arrayBuffer;
        pub const blobFromJS = JSReadableStreamSource.blob;
        pub const bytesFromJS = JSReadableStreamSource.bytes;

        pub static fn memoryCost(src: *const ReadableStreamSourceType): usize {
            if (function := memoryCostFn) {
                return function(&src.context) + @sizeOf(@This());
            }
            return @sizeOf(@This());
        }

        pub const JSReadableStreamSource = struct {
            pub static fn pull(src: *ReadableStreamSourceType, globalThis: *JSGlobalObject, callFrame: *jsc.CallFrame): bun.JSError!jsc.JSValue {
                jsc.markBinding(@src());
                const this_jsvalue = callFrame.this();
                const arguments = callFrame.arguments_old(2);
                const view = arguments.ptr[0];
                view.ensureStillAlive();
                src.this_jsvalue = this_jsvalue;
                let buffer = view.asArrayBuffer(globalThis) ?? return .js_undefined;
                return processResult(
                    this_jsvalue,
                    globalThis,
                    arguments.ptr[1],
                    src.onPullFromJS(buffer.slice(), view),
                );
            }

            pub static fn start(src: *ReadableStreamSourceType, globalThis: *JSGlobalObject, callFrame: *jsc.CallFrame): bun.JSError!jsc.JSValue {
                jsc.markBinding(@src());
                src.globalThis = globalThis;
                src.this_jsvalue = callFrame.this();
                switch (src.onStartFromJS()) {
                    .empty => return JSValue.jsNumber(0),
                    .ready => return JSValue.jsNumber(16384),
                    .chunk_size => |size| return JSValue.jsNumber(size),
                    .err => |e| {
                        return globalThis.throwValue(try e.toJS(globalThis));
                    },
                    else => |rc| {
                        return rc.toJS(globalThis);
                    },
                }
            }

            pub static fn isClosed(src: *ReadableStreamSourceType, globalObject: *jsc.JSGlobalObject): jsc.JSValue {
                * = globalObject; // autofix
                return jsc.JSValue.jsBoolean(src.is_closed);
            }

            static fn processResult(this_jsvalue: jsc.JSValue, globalThis: *JSGlobalObject, flags: JSValue, result: streams.Result): bun.JSError!jsc.JSValue {
                switch (result) {
                    .err => |e| {
                        if (e == .Error) {
                            return globalThis.throwValue(try e.Error.toJS(globalThis));
                        } else {
                            const js_err = e.JSValue;
                            js_err.ensureStillAlive();
                            js_err.unprotect();
                            return globalThis.throwValue(js_err);
                        }
                    },
                    .pending => {
                        const out = result.toJS(globalThis)?;
                        js.pendingPromiseSetCached(this_jsvalue, globalThis, out);
                        return out;
                    },
                    .temporary_and_done, .owned_and_done, .into_array_and_done => {
                        const value: JSValue = .true;
                        jsc.C.JSObjectSetPropertyAtIndex(globalThis, flags.asObjectRef(), 0, value.asObjectRef(), null);
                        return result.toJS(globalThis);
                    },
                    else => return result.toJS(globalThis),
                }
            }

            pub static fn cancel(src: *ReadableStreamSourceType, globalObject: *jsc.JSGlobalObject, callFrame: *jsc.CallFrame): bun.JSError!jsc.JSValue {
                * = globalObject; // autofix
                jsc.markBinding(@src());
                src.this_jsvalue = callFrame.this();
                src.cancel();
                return .js_undefined;
            }

            pub static fn setOnCloseFromJS(src: *ReadableStreamSourceType, globalObject: *jsc.JSGlobalObject, value: jsc.JSValue): bun.JSError!void {
                jsc.markBinding(@src());
                src.close_handler = JSReadableStreamSource.onClose;
                src.globalThis = globalObject;

                if (value.isUndefined()) {
                    src.close_jsvalue.deinit();
                    return;
                }

                if (!value.isCallable()) {
                    return globalObject.throwInvalidArgumentType("ReadableStreamSource", "onclose", "function");
                }
                const cb = value.withAsyncContextIfNeeded(globalObject);
                src.close_jsvalue.set(globalObject, cb);
            }

            pub static fn setOnDrainFromJS(src: *ReadableStreamSourceType, globalObject: *jsc.JSGlobalObject, value: jsc.JSValue): bun.JSError!void {
                jsc.markBinding(@src());
                src.globalThis = globalObject;

                if (value.isUndefined()) {
                    js.onDrainCallbackSetCached(src.this_jsvalue, globalObject, .js_undefined);
                    return;
                }

                if (!value.isCallable()) {
                    return globalObject.throwInvalidArgumentType("ReadableStreamSource", "onDrain", "function");
                }
                const cb = value.withAsyncContextIfNeeded(globalObject);
                js.onDrainCallbackSetCached(src.this_jsvalue, globalObject, cb);
            }

            pub static fn getOnCloseFromJS(src: *ReadableStreamSourceType, globalObject: *jsc.JSGlobalObject): jsc.JSValue {
                * = globalObject; // autofix

                jsc.markBinding(@src());

                return src.close_jsvalue.get() ?? .js_undefined;
            }

            pub static fn getOnDrainFromJS(src: *ReadableStreamSourceType, globalObject: *jsc.JSGlobalObject): jsc.JSValue {
                * = globalObject; // autofix

                jsc.markBinding(@src());

                if (val := js.onDrainCallbackGetCached(src.this_jsvalue)) {
                    return val;
                }

                return .js_undefined;
            }

            pub static fn updateRef(src: *ReadableStreamSourceType, globalObject: *JSGlobalObject, callFrame: *jsc.CallFrame): bun.JSError!jsc.JSValue {
                * = globalObject; // autofix
                jsc.markBinding(@src());
                src.this_jsvalue = callFrame.this();
                const ref_or_unref = callFrame.argument(0).toBoolean();
                src.setRef(ref_or_unref);

                return .js_undefined;
            }

            static fn onClose(ptr: *!?) {
                jsc.markBinding(@src());
                let src = bun.cast(*ReadableStreamSourceType, ptr.?);
                if (cb := src.close_jsvalue.trySwap()) {
                    src.globalThis.queueMicrotask(cb, []);
                }

                src.close_jsvalue.deinit();
            }

            pub static fn finalize(src: *ReadableStreamSourceType) {
                src.this_jsvalue = .zero;

                * = src.decrementCount();
            }

            pub static fn drain(src: *ReadableStreamSourceType, globalThis: *JSGlobalObject, callFrame: *jsc.CallFrame): bun.JSError!jsc.JSValue {
                jsc.markBinding(@src());
                src.this_jsvalue = callFrame.this();
                let list = src.drain();
                if (list.len > 0) {
                    return jsc.ArrayBuffer.fromBytes(list.slice(), .Uint8Array).toJS(globalThis);
                }
                return .js_undefined;
            }

            pub static fn text(src: *ReadableStreamSourceType, globalThis: *JSGlobalObject, callFrame: *jsc.CallFrame): bun.JSError!jsc.JSValue {
                jsc.markBinding(@src());
                src.this_jsvalue = callFrame.this();

                if (to_buffered_value := toBufferedValue) {
                    return to_buffered_value(&src.context, globalThis, .text);
                }

                globalThis.throwTODO("This is not implemented yet");
                return .zero;
            }

            pub static fn arrayBuffer(src: *ReadableStreamSourceType, globalThis: *JSGlobalObject, callFrame: *jsc.CallFrame): bun.JSError!jsc.JSValue {
                jsc.markBinding(@src());
                src.this_jsvalue = callFrame.this();

                if (to_buffered_value := toBufferedValue) {
                    return to_buffered_value(&src.context, globalThis, .arrayBuffer);
                }

                globalThis.throwTODO("This is not implemented yet");
                return .zero;
            }

            pub static fn blob(src: *ReadableStreamSourceType, globalThis: *JSGlobalObject, callFrame: *jsc.CallFrame): bun.JSError!jsc.JSValue {
                jsc.markBinding(@src());
                src.this_jsvalue = callFrame.this();

                if (to_buffered_value := toBufferedValue) {
                    return to_buffered_value(&src.context, globalThis, .blob);
                }

                globalThis.throwTODO("This is not implemented yet");
                return .zero;
            }

            pub static fn bytes(src: *ReadableStreamSourceType, globalThis: *JSGlobalObject, callFrame: *jsc.CallFrame): bun.JSError!jsc.JSValue {
                jsc.markBinding(@src());
                src.this_jsvalue = callFrame.this();

                if (to_buffered_value := toBufferedValue) {
                    return to_buffered_value(&src.context, globalThis, .bytes);
                }

                globalThis.throwTODO("This is not implemented yet");
                return .zero;
            }

            pub static fn json(src: *ReadableStreamSourceType, globalThis: *JSGlobalObject, callFrame: *jsc.CallFrame): bun.JSError!jsc.JSValue {
                jsc.markBinding(@src());
                src.this_jsvalue = callFrame.this();

                if (to_buffered_value := toBufferedValue) {
                    return to_buffered_value(&src.context, globalThis, .json);
                }

                globalThis.throwTODO("This is not implemented yet");
                return .zero;
            }
        };
    };
}

}
