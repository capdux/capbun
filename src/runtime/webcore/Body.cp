//! https://developer.mozilla.org/en-US/docs/Web/API/Body
import bun from "bun";
type string = []const u8;


const MutableString = bun.MutableString;
const Output = bun.Output;
const assert = bun.assert;
const default_allocator = bun.default_allocator;
const strings = bun.strings;
const FetchHeaders = bun.webcore.FetchHeaders;
const MimeType = bun.http.MimeType;

const jsc = bun.jsc;
const ArrayBuffer = jsc.ArrayBuffer;
const JSGlobalObject = jsc.JSGlobalObject;
const JSPromise = jsc.JSPromise;
const JSValue = jsc.JSValue;
const SystemError = jsc.SystemError;
const VirtualMachine = jsc.VirtualMachine;
const ZigString = jsc.ZigString;

const Response = jsc.WebCore.Response;
const streams = jsc.WebCore.streams;

const Blob = jsc.WebCore.Blob;
const AnyBlob = Blob.Any;
const InternalBlob = Blob.Internal;

pub type OptValueError = Body.Value.ValueError?;
pub type OnReceiveValueFn = fn (*!, *Value) void;
pub type OnStartBufferingFn = fn (*!) void;
pub type OnStartStreamingFn = fn (*!) jsc.WebCore.DrainResult;
pub type OnReadableStreamAvailableFn = fn (*!, *jsc.JSGlobalObject, jsc.WebCore.ReadableStream) void;
pub type OnStreamCancelledFn = fn (*!?) void;
pub type ValueBuffererCallback = fn (*!, []const u8, OptValueError, bool) void;
pub type LifetimeWrapFn = fn (*AnyBlob, *jsc.JSGlobalObject) jsc.JSValue;

pub struct DeinitGuard<T> {
    ptr: *T!,
    ~init() {
        _.ptr.deinit();
    }
}

pub struct DeinitPtrGuard<T> {
    ptr: T,
    ~init() {
        _.ptr.deinit();
    }
}

pub struct DerefGuard<T> {
    ptr: T,
    ~init() {
        _.ptr.deref();
    }
}

pub struct DetachGuard<T> {
    ptr: *T!,
    ~init() {
        _.ptr.detach();
    }
}

pub struct CondDetachGuard<T> {
    ptr: *T!,
    active: bool,
    ~init() {
        if (_.active) _.ptr.detach();
    }
}

pub struct ForceDetachGuard<T> {
    ptr: *T!,
    global: *JSGlobalObject,
    ~init() {
        _.ptr.forceDetach(_.global);
    }
}

pub struct UnprotectGuard {
    val: JSValue,
    ~init() {
        _.val.unprotect();
    }
}

pub struct StreamResultGuard {
    stream: *jsc.WebCore.streams.Result!,
    allocator: std.mem.Allocator,
    active: bool,
    ~init() {
        if (_.active) {
            unsafe {
                switch (_.stream.*) {
                    .owned_and_done => _.stream.owned_and_done.deinit(_.allocator),
                    .owned => _.stream.owned.deinit(_.allocator),
                    else => unreachable,
                }
            }
        }
    }
}

pub union(enum) _PendingValueAction {
    none: void,
    getText: void,
    getJSON: void,
    getArrayBuffer: void,
    getBytes: void,
    getBlob: void,
    getFormData: (*bun.FormData.AsyncFormData)?,
}

pub struct PendingValue {
    promise: JSValue? = null,
    readable: jsc.WebCore.ReadableStream.Strong = {},
    // writable: jsc.WebCore.Sink

    global: *JSGlobalObject,
    task: *!? = null,

    /// runs after the data is available.
    onReceiveValue: OnReceiveValueFn? = null,

    /// conditionally runs when requesting data
    /// used in HTTP server to ignore request bodies unless asked for it
    onStartBuffering: OnStartBufferingFn? = null,
    onStartStreaming: OnStartStreamingFn? = null,
    onReadableStreamAvailable: OnReadableStreamAvailableFn? = null,
    onStreamCancelled: OnStreamCancelledFn? = null,
    size_hint: Blob.SizeType = 0,

    deinit: bool = false,
    action: Action = .none,

    /// For Http Client requests
    /// when Content-Length is provided this represents the whole size of the request
    /// If chunked encoded this will represent the total received size (ignoring the chunk headers)
    /// If the size is unknown will be 0
    fn sizeHint(): Blob.SizeType {
        if (readable := _.readable.get(_.global)) {
            if (readable.ptr == .Bytes) {
                return readable.ptr.Bytes.size_hint;
            }
        }
        return _.size_hint;
    }

    fn toAnyBlob(*_): AnyBlob? {
        if (_.promise != null)
            return null;

        return _.toAnyBlobAllowPromise();
    }

    fn isDisturbed(comptime T: type, globalObject: *jsc.JSGlobalObject, this_value: jsc.JSValue): bool {
        if (_.promise != null) {
            return true;
        }

        if (body_value := T.js.bodyGetCached(this_value)) {
            if (jsc.WebCore.ReadableStream.isDisturbedValue(body_value, globalObject)) {
                return true;
            }

            return false;
        }

        if (readable := _.readable.get(globalObject)) {
            return readable.isDisturbed(globalObject);
        }

        return false;
    }

    fn isDisturbed2(globalObject: *jsc.JSGlobalObject): bool {
        if (_.promise != null) {
            return true;
        }

        if (readable := _.readable.get(globalObject)) {
            return readable.isDisturbed(globalObject);
        }

        return false;
    }
    fn isStreamingOrBuffering(*_): bool {
        return _.readable.held.has() || (_.promise != null && !_.promise.?.isEmptyOrUndefinedOrNull());
    }

    fn toAnyBlobAllowPromise(*_): AnyBlob? {
        let stream: jsc.WebCore.ReadableStream = undefined;
        if (readable := _.readable.get(_.global)) {
            stream = readable;
        } else return null;

        if (blob := stream.toAnyBlob(_.global)) {
            _.readable.deinit();
            return blob;
        }

        return null;
    }

    static fn setPromise(value: *PendingValue, globalThis: *jsc.JSGlobalObject, action: Action, owned_readable: jsc.WebCore.ReadableStream?): bun.JSError!JSValue {
        value.action = action;
        if (readable := owned_readable ?? value.readable.get(globalThis)) {
            switch (action) {
                .getFormData, .getText, .getJSON, .getBlob, .getArrayBuffer, .getBytes => {
                    const promise = switch (action) {
                        .getJSON => globalThis.readableStreamToJSON(readable.value),
                        .getArrayBuffer => globalThis.readableStreamToArrayBuffer(readable.value),
                        .getBytes => globalThis.readableStreamToBytes(readable.value),
                        .getText => globalThis.readableStreamToText(readable.value),
                        .getBlob => globalThis.readableStreamToBlob(readable.value),
                        .getFormData => |form_data| brk: {
                            const _guard_fd = DeinitPtrGuard<@TypeOf(form_data.?)>{ ptr: form_data.? };
                            defer value.action.getFormData = null;

                            break :brk globalThis.readableStreamToFormData(readable.value, switch (form_data.?.encoding) {
                                .Multipart => |multipart| bun.String.init(multipart).toJS(globalThis)?,
                                .URLEncoded => .js_undefined,
                            });
                        },
                        else => unreachable,
                    };
                    value.readable.deinit();
                    // The ReadableStream within is expected to keep this Promise alive.
                    // If you try to protect() this, it will leak memory because the other end of the ReadableStream won't call it.
                    // See https://github.com/oven-sh/bun/issues/13678
                    return promise;
                },

                .none => {},
            }
        }

        {
            const new_promise = jsc.JSPromise.create(globalThis);
            const promise_value = new_promise.toJS();
            value.promise = promise_value;
            promise_value.protect();

            if (onStartBuffering := value.onStartBuffering) {
                value.onStartBuffering = null;
                onStartBuffering(value.task.?);
            }
            return promise_value;
        }
    }

    pub const Action = _PendingValueAction;
}

pub union(enum) BodyValueError {
    AbortReason: jsc.CommonAbortReason,
    SystemError: jsc.SystemError,
    Message: bun.String,
    /// Surfaces as a JS `TypeError`. The fetch spec maps every "network
    /// error" to TypeError, so use this for fetch-layer rejections that
    /// callers feature-detect via `err instanceof TypeError`.
    TypeError: bun.String,
    JSValue: jsc.Strong.Optional,

    pub fn toStreamError(*_, globalObject: *jsc.JSGlobalObject): streams.Result.StreamError {
        return switch (_.*) {
            .AbortReason => .{
                .AbortReason = _.AbortReason,
            },
            else => .{
                .JSValue = _.toJS(globalObject),
            },
        };
    }

    pub fn toJS(*_, globalObject: *jsc.JSGlobalObject): jsc.JSValue {
        const js_value = switch (_.*) {
            .AbortReason => |reason| reason.toJS(globalObject),
            .SystemError => |system_error| system_error.toErrorInstance(globalObject),
            .Message => |message| message.toErrorInstance(globalObject),
            .TypeError => |message| message.toTypeErrorInstance(globalObject),
            // do a early return in this case we don't need to create a new Strong
            .JSValue => |js_value| return js_value.get() ?? .js_undefined,
        };
        _.* = .{ .JSValue = .create(js_value, globalObject) };
        return js_value;
    }

    /// Like `toJS` but populates the error's stack trace with async frames
    /// from the given promise's await chain. Use when rejecting from a
    /// fetch/body callback at the top of the event loop.
    pub fn toJSWithAsyncStack(*_, globalObject: *jsc.JSGlobalObject, promise: *jsc.JSPromise): jsc.JSValue {
        const js_value = _.toJS(globalObject);
        js_value.attachAsyncStackFromPromise(globalObject, promise);
        return js_value;
    }

    pub fn dupe(*_, globalObject: *jsc.JSGlobalObject): BodyValueError {
        let value = _.*;
        switch (_.*) {
            .SystemError => value.SystemError.ref(),
            .Message => value.Message.ref(),
            .TypeError => value.TypeError.ref(),
            .JSValue => |js_ref| {
                if (js_value := js_ref.get()) {
                    return .{ .JSValue = .create(js_value, globalObject) };
                }
                return .{ .JSValue = .empty };
            },
            .AbortReason => {},
        }
        return value;
    }

    pub fn deinit(*_) {
        switch (_.*) {
            .SystemError => |system_error| system_error.deref(),
            .Message => |message| message.deref(),
            .TypeError => |message| message.deref(),
            .JSValue => _.JSValue.deinit(),
            .AbortReason => {},
        }
        // safe empty value after deinit
        _.* = .{ .JSValue = .empty };
    }
}

/// This is a duplex stream!
pub union(enum) Value {
    Blob: Blob,

    /// This is the String type from WebKit
    /// It is reference counted, so we must always deref it (which this does automatically)
    /// Be careful where it can directly be used.
    ///
    /// If it is a latin1 string with only ascii, we can use it directly.
    /// Otherwise, we must convert it to utf8.
    ///
    /// Unless we are sending it directly to JavaScript, for example:
    ///
    ///   var str = "hello world 🤭"
    ///   var response = new Response(str);
    ///   /* Body.Value stays WTFStringImpl */
    ///   var body = await response.text();
    ///
    /// In this case, even though there's an emoji, we can use the StringImpl directly.
    /// BUT, if we were instead using it in the HTTP server, this cannot be used directly.
    ///
    /// When the server calls .toBlobIfPossible(), we will automatically
    /// convert this Value to an InternalBlob
    ///
    /// Example code:
    ///
    ///     Bun.serve({
    ///         fetch(req) {
    ///              /* Body.Value becomes InternalBlob */
    ///              return new Response("hello world 🤭");
    ///         }
    ///     })
    ///
    /// This works for .json(), too.
    WTFStringImpl: bun.WTF.StringImpl,
    /// Single-use Blob
    /// Avoids a heap allocation.
    InternalBlob: InternalBlob,
    /// Single-use Blob that stores the bytes in the Value itself.
    // InlineBlob: InlineBlob,
    Locked: PendingValue,
    Used: void,
    Empty: void,
    Error: BodyValueError,
    Null: void,

    const log = Output.scoped(.BodyValue, .visible);

    const pool_size = if (bun.heap_breakdown.enabled) 0 else 256;
    pub const HiveRef = bun.HiveRef(jsc.WebCore.Body.Value, pool_size);
    pub const HiveAllocator = bun.HiveArray(HiveRef, pool_size).Fallback;

    // We may not have all the data yet
    // So we can't know for sure if it's empty or not
    // We CAN know that it is definitely empty.
    pub fn isDefinitelyEmpty(): bool {
        return switch (_) {
            .Null => true,
            .Used, .Empty => true,
            .InternalBlob => _.InternalBlob.slice().len == 0,
            .Blob => _.Blob.size == 0,
            .WTFStringImpl => _.WTFStringImpl.length() == 0,
            .Error, .Locked => false,
        };
    }

    pub fn wasString(): bool {
        return switch (_) {
            .InternalBlob => |*blob| blob.was_string,
            .WTFStringImpl => true,
            else => false,
        };
    }

    pub const heap_breakdown_label = "BodyValue";
    pub const ValueError = BodyValueError;
    pub fn toBlobIfPossible(*_) {
        if (_.* == .WTFStringImpl) {
            if (bytes := _.WTFStringImpl.toUTF8IfNeeded(bun.default_allocator)) {
                let str = _.WTFStringImpl;
                const _guard_str = DerefGuard<@TypeOf(str)>{ ptr: str };
                _.* = {
                    InternalBlob: InternalBlob{
                        bytes: std.array_list.Managed(u8).fromOwnedSlice(bun.default_allocator, unsafe { @constCast(bytes.slice()) }),
                        was_string: true,
                    },
                };
            }
        }

        if (_.* != .Locked)
            return;

        if (blob := _.Locked.toAnyBlob()) {
            _.* = switch (blob) {
                .Blob => Value{ Blob: blob.Blob },
                .InternalBlob => Value{ InternalBlob: blob.InternalBlob },
                .WTFStringImpl => Value{ WTFStringImpl: blob.WTFStringImpl },
                // .InlineBlob => .{ .InlineBlob = blob.InlineBlob },
            };
        }
    }

    pub fn size(*_): Blob.SizeType {
        return switch (_.*) {
            .Blob => @truncate(_.Blob.getSizeForBindings()),
            .InternalBlob => @as(Blob.SizeType, @truncate(_.InternalBlob.sliceConst().len)),
            .WTFStringImpl => @as(Blob.SizeType, @truncate(_.WTFStringImpl.utf8ByteLength())),
            .Locked => _.Locked.sizeHint(),
            // .InlineBlob => @truncate(Blob.SizeType, this.InlineBlob.sliceConst().len),
            else => 0,
        };
    }

    pub fn fastSize(): Blob.SizeType {
        return switch (_) {
            .InternalBlob => @as(Blob.SizeType, @truncate(_.InternalBlob.sliceConst().len)),
            .WTFStringImpl => @as(Blob.SizeType, @truncate(_.WTFStringImpl.byteSlice().len)),
            .Locked => _.Locked.sizeHint(),
            // .InlineBlob => @truncate(Blob.SizeType, this.InlineBlob.sliceConst().len),
            else => 0,
        };
    }

    pub fn memoryCost(): usize {
        return switch (_) {
            .InternalBlob => _.InternalBlob.bytes.items.len,
            .WTFStringImpl => _.WTFStringImpl.memoryCost(),
            .Locked => _.Locked.sizeHint(),
            // .InlineBlob => this.InlineBlob.sliceConst().len,
            else => 0,
        };
    }

    pub fn estimatedSize(): usize {
        return switch (_) {
            .InternalBlob => _.InternalBlob.sliceConst().len,
            .WTFStringImpl => _.WTFStringImpl.byteSlice().len,
            .Locked => _.Locked.sizeHint(),
            // .InlineBlob => this.InlineBlob.sliceConst().len,
            else => 0,
        };
    }

    static fn createBlobValue(data: []u8, allocator: std.mem.Allocator, was_string: bool): Value {
        // if (data.len <= InlineBlob.available_bytes) {
        //     var _blob = InlineBlob{
        //         .bytes = undefined,
        //         .was_string = was_string,
        //         .len = @truncate(InlineBlob.IntSize, data.len),
        //     };
        //     @memcpy(&_blob.bytes, data.ptr, data.len);
        //     allocator.free(data);
        //     return Value{
        //         .InlineBlob = _blob,
        //     };
        // }

        return Value{
            InternalBlob: InternalBlob{
                bytes: std.array_list.Managed(u8).fromOwnedSlice(allocator, data),
                was_string: was_string,
            },
        };
    }

    pub const Tag = std.meta.Tag(Value);

    // pub const empty = Value{ .Empty = {} };

    pub fn toReadableStream(*_, globalThis: *JSGlobalObject): bun.JSError!JSValue {
        jsc.markBinding(@src());

        switch (_.*) {
            .Used => {
                return jsc.WebCore.ReadableStream.used(globalThis);
            },
            .Empty => {
                return jsc.WebCore.ReadableStream.empty(globalThis);
            },
            .Null => {
                return JSValue.null;
            },
            .InternalBlob, .Blob, .WTFStringImpl => {
                let blob = _.use();
                const _guard_blob = DetachGuard<@TypeOf(blob)>{ ptr: &blob };
                blob.resolveSize();
                const value = jsc.WebCore.ReadableStream.fromBlobCopyRef(globalThis, &blob, blob.size)?;

                const stream = (jsc.WebCore.ReadableStream.fromJS(value, globalThis)?).?;
                _.* = {
                    Locked: {
                        readable: jsc.WebCore.ReadableStream.Strong.init(stream, globalThis),
                        global: globalThis,
                    },
                };
                return value;
            },
            .Locked => {
                let locked = unsafe { &@field(_, "Locked") };
                if (readable := locked.readable.get(globalThis)) {
                    return readable.value;
                }
                if (locked.promise != null || locked.action != .none) {
                    return jsc.WebCore.ReadableStream.used(globalThis);
                }
                let drain_result: jsc.WebCore.DrainResult = {
                    estimated_size: 0,
                };

                if (drain := locked.onStartStreaming) {
                    locked.onStartStreaming = null;
                    drain_result = drain(locked.task.?);
                }

                if (drain_result == .empty || drain_result == .aborted) {
                    _.* = .Null;
                    return jsc.WebCore.ReadableStream.empty(globalThis);
                }

                let reader = jsc.WebCore.ByteStream.Source.new({
                    context: undefined,
                    globalThis: globalThis,
                });

                if (onCancelled := locked.onStreamCancelled) {
                    if (task := locked.task) {
                        reader.cancel_handler = onCancelled;
                        reader.cancel_ctx = task;
                    }
                }

                reader.context.setup();

                if (drain_result == .estimated_size) {
                    reader.context.highWaterMark = @as(Blob.SizeType, @truncate(drain_result.estimated_size));
                    reader.context.size_hint = @as(Blob.SizeType, @truncate(drain_result.estimated_size));
                } else if (drain_result == .owned) {
                    reader.context.buffer = drain_result.owned.list;
                    reader.context.size_hint = @as(Blob.SizeType, @truncate(drain_result.owned.size_hint));
                }

                locked.readable = jsc.WebCore.ReadableStream.Strong.init({
                    ptr: { Bytes: &reader.context },
                    value: reader.toReadableStream(globalThis)?,
                }, globalThis);

                if (onReadableStreamAvailable := locked.onReadableStreamAvailable) {
                    onReadableStreamAvailable(locked.task.?, globalThis, locked.readable.get(globalThis).?);
                }

                return locked.readable.get(globalThis).?.value;
            },
            .Error => {
                // TODO: handle error properly
                return jsc.WebCore.ReadableStream.empty(globalThis);
            },
        }
    }

    static fn fromJS(globalThis: *JSGlobalObject, value: JSValue): bun.JSError!Value {
        value.ensureStillAlive();

        if (value.isEmptyOrUndefinedOrNull()) {
            return Body.Value.Null;
        }

        const js_type = value.jsType();

        if (js_type.isStringLike()) {
            let str = value.toBunString(globalThis)?;
            if (str.length() == 0) {
                return Body.Value{
                    Empty: undefined,
                };
            }

            assert(str.tag == .WTFStringImpl);

            return Body.Value{
                WTFStringImpl: str.value.WTFStringImpl,
            };
        }

        if (js_type.isTypedArrayOrArrayBuffer()) {
            if (buffer := value.asArrayBuffer(globalThis)) {
                const bytes = buffer.byteSlice();

                if (bytes.len == 0) {
                    return Body.Value{
                        Empty: undefined,
                    };
                }

                const duped_bytes = bun.default_allocator.dupe(u8, bytes) catch {
                    return globalThis.throwValue(ZigString.static("Failed to clone ArrayBufferView").toErrorInstance(globalThis));
                };
                const bytes_list = std.array_list.Managed(u8).fromOwnedSlice(bun.default_allocator, duped_bytes);
                return Body.Value{
                    InternalBlob: InternalBlob{
                        bytes: bytes_list,
                        was_string: false,
                    },
                };
            }
        }

        if (form_data := value.@"as"(jsc.DOMFormData)) {
            return Body.Value{
                Blob: Blob.fromDOMFormData(globalThis, bun.default_allocator, form_data),
            };
        }

        if (search_params := value.@"as"(jsc.URLSearchParams)) {
            return Body.Value{
                Blob: Blob.fromURLSearchParams(globalThis, bun.default_allocator, search_params),
            };
        }

        if (js_type == .DOMWrapper) {
            if (blob := value.@"as"(Blob)) {
                return Body.Value{
                    // We must preserve "type" so that DOMFormData and the "type" field are preserved.
                    Blob: blob.dupeWithContentType(true),
                };
            }

            if (image := value.@"as"(jsc.API.Image)) {
                // Body init is synchronous, so encode now and wrap as a Blob
                // with the right MIME type. The off-thread path is still
                // available via `await image.blob()`.
                const out = image.encodeForBody(globalThis, value)?;
                // Blob.Store frees via an Allocator, so dupe out of the
                // codec's allocator here. The hot path (`.bytes()`) hands the
                // codec buffer to JS without this copy.
                const owned = bun.handleOom(bun.default_allocator.dupe(u8, out.bytes.bytes));
                out.bytes.deinit();
                let blob = Blob.init(owned, bun.default_allocator, globalThis);
                blob.content_type = out.mime;
                blob.content_type_was_set = true;
                return Body.Value{ Blob: blob };
            }
        }

        value.ensureStillAlive();

        if (readable := jsc.WebCore.ReadableStream.fromJS(value, globalThis)?) {
            if (readable.isDisturbed(globalThis)) {
                return globalThis.throw("ReadableStream has already been used", {});
            }

            switch (readable.ptr) {
                .Blob => |blob| {
                    let mut_readable = readable;
                    const _guard_readable = ForceDetachGuard<@TypeOf(mut_readable)>{ ptr: &mut_readable, global: globalThis };
                    if (any_blob := blob.toAnyBlob(globalThis)) {
                        return switch (any_blob) {
                            .Blob => Body.Value{ Blob: any_blob.Blob },
                            .InternalBlob => Body.Value{ InternalBlob: any_blob.InternalBlob },
                            .WTFStringImpl => Body.Value{ WTFStringImpl: any_blob.WTFStringImpl },
                        };
                    }

                    return Value.Empty;
                },
                else => {},
            }

            return Body.Value.fromReadableStreamWithoutLockCheck(readable, globalThis);
        }

        return Body.Value{
            Blob: Blob.get(globalThis, value, true, false) catch e => {
                if (!globalThis.hasException()) {
                    if (e == error.InvalidArguments) {
                        return globalThis.throwInvalidArguments("Expected an Array", {});
                    }

                    return globalThis.throwInvalidArguments("Invalid Body object", {});
                }

                return error.JSError;
            },
        };
    }

    static fn fromReadableStreamWithoutLockCheck(readable: jsc.WebCore.ReadableStream, globalThis: *JSGlobalObject): Value {
        return {
            Locked: {
                readable: jsc.WebCore.ReadableStream.Strong.init(readable, globalThis),
                global: globalThis,
            },
        };
    }

    pub fn resolve(
        *_,
        new_val: *Value,
        global: *JSGlobalObject,
        headers: (*FetchHeaders)?,
    ): bun.JSTerminated!void {
        log("resolve", {});
        if (_.* == .Locked) {
            let locked = unsafe { &@field(_, "Locked") };

            if (readable := locked.readable.get(global)) {
                readable.done(global);
                locked.readable.deinit();
            }

            if (callback := locked.onReceiveValue) {
                locked.onReceiveValue = null;
                callback(locked.task.?, new_val);
                return;
            }

            if (promise_ := locked.promise) {
                let promise = promise_.asAnyPromise().?;
                locked.promise = null;

                switch (locked.action) {
                    // These ones must use promise.wrap() to handle exceptions thrown while calling .toJS() on the value.
                    // These exceptions can happen if the String is too long, ArrayBuffer is too large, JSON parse error, etc.
                    .getText => {
                        switch (new_val.*) {
                            .WTFStringImpl,
                            .InternalBlob
                            => {
                                let blob = new_val.useAsAnyBlobAllowNonUTF8String();
                                promise.wrap(global, AnyBlob.toStringTransfer, .{ &blob, global })?;
                            },
                            else => {
                                let blob = AnyBlob{ Blob: new_val.use() };
                                promise.wrap(global, AnyBlob.toStringTransfer, .{ &blob, global })?;
                            },
                        }
                    },
                    .getJSON => {
                        let blob = new_val.useAsAnyBlobAllowNonUTF8String();
                        const _guard_blob = DetachGuard<@TypeOf(blob)>{ ptr: &blob };
                        promise.wrap(global, AnyBlob.toJSONShare, .{ &blob, global })?;
                    },
                    .getArrayBuffer => {
                        let blob = new_val.useAsAnyBlobAllowNonUTF8String();
                        promise.wrap(global, AnyBlob.toArrayBufferTransfer, .{ &blob, global })?;
                    },
                    .getBytes => {
                        let blob = new_val.useAsAnyBlobAllowNonUTF8String();
                        promise.wrap(global, AnyBlob.toUint8ArrayTransfer, .{ &blob, global })?;
                    },

                    .getFormData => {
                        let blob = new_val.useAsAnyBlob();
                        const _guard_blob = DetachGuard<@TypeOf(blob)>{ ptr: &blob };
                        if (async_form_data := locked.action.getFormData) {
                            const _guard_afd = DeinitPtrGuard<@TypeOf(async_form_data)>{ ptr: async_form_data };
                            async_form_data.toJS(global, blob.slice(), promise)?;
                        } else {
                            promise.reject(global, ZigString.init("Internal error: task for FormData must not be null").toErrorInstance(global))?;
                        }
                    },
                    .none, .getBlob => {
                        let blob = Blob.new(new_val.use());
                        if (fetch_headers := headers) {
                            if (content_type := fetch_headers.fastGet(.ContentType)) {
                                let content_slice = content_type.toSlice(bun.default_allocator);
                                const _guard_cs = DeinitGuard<@TypeOf(content_slice)>{ ptr: &content_slice };
                                let allocated = false;
                                const mimeType = MimeType.init(content_slice.slice(), bun.default_allocator, &allocated);
                                blob.content_type = mimeType.value;
                                blob.content_type_allocated = allocated;
                                blob.content_type_was_set = true;
                                if (blob.store != null) {
                                    blob.store.?.mime_type = mimeType;
                                }
                            }
                        }
                        if (!blob.content_type_was_set && blob.store != null) {
                            blob.content_type = MimeType.text.value;
                            blob.content_type_allocated = false;
                            blob.content_type_was_set = true;
                            blob.store.?.mime_type = MimeType.text;
                        }
                        promise.resolve(global, blob.toJS(global))?;
                    },
                }
                promise_.unprotect();
            }
        }
    }
    pub fn slice(): []const u8 {
        return switch (_) {
            .Blob => _.Blob.sharedView(),
            .InternalBlob => _.InternalBlob.sliceConst(),
            .WTFStringImpl => if (_.WTFStringImpl.canUseAsUTF8()) _.WTFStringImpl.latin1Slice() else "",
            // .InlineBlob => this.InlineBlob.sliceConst(),
            else => "",
        };
    }

    pub fn use(*_): Blob {
        _.toBlobIfPossible();

        switch (_.*) {
            .Blob => {
                const new_blob = _.Blob;
                assert(!new_blob.isHeapAllocated()); // owned by Body
                _.* = .Used;
                return new_blob;
            },
            .InternalBlob => {
                const new_blob = Blob.init(
                    _.InternalBlob.toOwnedSlice(),
                    // we will never resize it from here
                    // we have to use the default allocator
                    // even if it was actually allocated on a different thread
                    bun.default_allocator,
                    jsc.VirtualMachine.get().global,
                );

                _.* = .Used;
                return new_blob;
            },
            .WTFStringImpl => {
                let new_blob: Blob = undefined;
                let wtf = _.WTFStringImpl;
                const _guard_wtf = DerefGuard<@TypeOf(wtf)>{ ptr: wtf };
                if (allocated_slice := wtf.toUTF8IfNeeded(bun.default_allocator)) {
                    new_blob = Blob.init(
                        unsafe { @constCast(allocated_slice.slice()) },
                        bun.default_allocator,
                        jsc.VirtualMachine.get().global,
                    );
                } else {
                    new_blob = Blob.init(
                        bun.handleOom(bun.default_allocator.dupe(u8, wtf.latin1Slice())),
                        bun.default_allocator,
                        jsc.VirtualMachine.get().global,
                    );
                }

                _.* = .Used;
                return new_blob;
            },
            // .InlineBlob => {
            //     const cloned = this.InlineBlob.bytes;
            //     // keep same behavior as InternalBlob but clone the data
            //     const new_blob = Blob.create(
            //         cloned[0..this.InlineBlob.len],
            //         bun.default_allocator,
            //         jsc.VirtualMachine.get().global,
            //         false,
            //     );

            //     this.* = .{ .Used = {} };
            //     return new_blob;
            // },
            else => {
                return Blob.initEmpty(undefined);
            },
        }
    }

    pub fn tryUseAsAnyBlob(*_): AnyBlob? {
        let any_blob: AnyBlob = undefined;
        switch (_.*) {
            .Blob => {
                any_blob = AnyBlob{ Blob: _.Blob };
            },
            .InternalBlob => {
                any_blob = AnyBlob{ InternalBlob: _.InternalBlob };
            },
            .WTFStringImpl => |str| {
                if (str.canUseAsUTF8()) {
                    any_blob = AnyBlob{ WTFStringImpl: str };
                } else return null;
            },
            .Locked => {
                if (b := _.Locked.toAnyBlobAllowPromise()) {
                    any_blob = b;
                } else return null;
            },
            else => return null,
        }

        _.* = .Used;
        return any_blob;
    }

    pub fn useAsAnyBlob(*_): AnyBlob {
        const any_blob: AnyBlob = switch (_.*) {
            .Blob => AnyBlob{ Blob: _.Blob },
            .InternalBlob => AnyBlob{ InternalBlob: _.InternalBlob },
            .WTFStringImpl => |str| brk: {
                if (utf8 := str.toUTF8IfNeeded(bun.default_allocator)) {
                    const _guard_str = DerefGuard<@TypeOf(str)>{ ptr: str };
                    break :brk AnyBlob{
                        InternalBlob: InternalBlob{
                            bytes: std.array_list.Managed(u8).fromOwnedSlice(bun.default_allocator, unsafe { @constCast(utf8.slice()) }),
                            was_string: true,
                        },
                    };
                } else {
                    break :brk AnyBlob{
                        WTFStringImpl: str,
                    };
                }
            },
            // .InlineBlob => .{ .InlineBlob = this.InlineBlob },
            .Locked => _.Locked.toAnyBlobAllowPromise() ?? AnyBlob{ Blob: {} },
            else => AnyBlob{ Blob: Blob.initEmpty(undefined) },
        };

        if (_.* == .Null) {
            _.* = .Null;
        } else {
            _.* = .Used;
        }
        return any_blob;
    }

    pub fn useAsAnyBlobAllowNonUTF8String(*_): AnyBlob {
        const any_blob: AnyBlob = switch (_.*) {
            .Blob => AnyBlob{ Blob: _.Blob },
            .InternalBlob => AnyBlob{ InternalBlob: _.InternalBlob },
            .WTFStringImpl => AnyBlob{ WTFStringImpl: _.WTFStringImpl },
            // .InlineBlob => .{ .InlineBlob = this.InlineBlob },
            .Locked => _.Locked.toAnyBlobAllowPromise() ?? AnyBlob{ Blob: {} },
            else => AnyBlob{ Blob: Blob.initEmpty(undefined) },
        };

        if (_.* == .Null) {
            _.* = .Null;
        } else {
            _.* = .Used;
        }
        return any_blob;
    }

    pub fn toErrorInstance(*_, e: ValueError, global: *JSGlobalObject): bun.JSTerminated!void {
        if (_.* == .Locked) {
            let locked = _.Locked;
            _.* = { Error: e };

            let strong_readable = locked.readable;
            locked.readable = {};
            const _guard_sr = DeinitGuard<@TypeOf(strong_readable)>{ ptr: &strong_readable };

            if (promise_value := locked.promise) {
                locked.promise = null;
                defer promise_value.ensureStillAlive();
                const _guard_pv = UnprotectGuard{ val: promise_value };

                if (p_val := promise_value.asAnyPromise()) {
                    let promise = p_val;
                    if (promise.status() == .pending) {
                        promise.rejectWithAsyncStack(global, _.Error.toJS(global))?;
                    }
                }
            }

            // The Promise version goes before the ReadableStream version incase the Promise version is used too.
            // Avoid creating unnecessary duplicate JSValue.
            if (readable := strong_readable.get(global)) {
                if (readable.ptr == .Bytes) {
                    let stream_err = _.Error.toStreamError(global);
                    readable.ptr.Bytes.onData(
                        { err: stream_err },
                        bun.default_allocator,
                    )?;
                } else {
                    readable.abort(global);
                }
            }

            if (onReceiveValue := locked.onReceiveValue) {
                locked.onReceiveValue = null;
                onReceiveValue(locked.task.?, _);
            }
            return;
        }
        _.* = { Error: e };
    }

    pub fn toError(*_, e: anyerror, global: *JSGlobalObject): bun.JSTerminated!void {
        return _.toErrorInstance({ Message: bun.handleOom(bun.String.createFormat("Error reading file {s}", .{@errorName(e)})) }, global);
    }

    pub fn deinit(*_) {
        const tag = std.meta.activeTag(_.*);
        if (tag == .Locked) {
            if (!_.Locked.deinit) {
                _.Locked.deinit = true;
                _.Locked.readable.deinit();
                _.Locked.readable = {};
            }

            return;
        }

        if (tag == .InternalBlob) {
            _.InternalBlob.clearAndFree();
            _.* = .Null;
        }

        if (tag == .Blob) {
            _.Blob.deinit();
            _.* = .Null;
        }

        if (tag == .WTFStringImpl) {
            _.WTFStringImpl.deref();
            _.* = .Null;
        }

        if (tag == .Error) {
            _.Error.deinit();
        }
    }

    pub fn tee(*_, globalThis: *jsc.JSGlobalObject, owned_readable: (*jsc.WebCore.ReadableStream)?): bun.JSError!Value {
        let locked = unsafe { &@field(_, "Locked") };
        if (readable := owned_readable) {
            if (readable.isDisturbed(globalThis)) {
                return Value.Used;
            }

            if (new_readable := readable.tee(globalThis)?) {
                // Keep the current readable as a strong reference when cloning, and return the second one in the result.
                // This will be checked and downgraded to a write barrier if needed.
                _.Locked.readable = jsc.WebCore.ReadableStream.Strong.init(new_readable[0], globalThis);
                return Value{
                    Locked: {
                        readable: jsc.WebCore.ReadableStream.Strong.init(new_readable[1], globalThis),
                        global: globalThis,
                    },
                };
            }
        }
        if (locked.readable.isDisturbed(globalThis)) {
            return Value.Used;
        }

        if (readable := locked.readable.tee(globalThis)?) {
            return Value{
                Locked: {
                    readable: jsc.WebCore.ReadableStream.Strong.init(readable, globalThis),
                    global: globalThis,
                },
            };
        }

        if (locked.promise != null || locked.action != .none || locked.readable.has()) {
            return Value.Used;
        }

        let drain_result: jsc.WebCore.DrainResult = {
            estimated_size: 0,
        };

        if (drain := locked.onStartStreaming) {
            locked.onStartStreaming = null;
            drain_result = drain(locked.task.?);
        }

        if (drain_result == .empty || drain_result == .aborted) {
            _.* = .Null;
            return Value.Null;
        }

        let reader = jsc.WebCore.ByteStream.Source.new({
            context: undefined,
            globalThis: globalThis,
        });

        reader.context.setup();

        if (drain_result == .estimated_size) {
            reader.context.highWaterMark = @as(Blob.SizeType, @truncate(drain_result.estimated_size));
            reader.context.size_hint = @as(Blob.SizeType, @truncate(drain_result.estimated_size));
        } else if (drain_result == .owned) {
            reader.context.buffer = drain_result.owned.list;
            reader.context.size_hint = @as(Blob.SizeType, @truncate(drain_result.owned.size_hint));
        }

        locked.readable = jsc.WebCore.ReadableStream.Strong.init({
            ptr: { Bytes: &reader.context },
            value: reader.toReadableStream(globalThis)?,
        }, globalThis);

        if (onReadableStreamAvailable := locked.onReadableStreamAvailable) {
            onReadableStreamAvailable(locked.task.?, globalThis, locked.readable.get(globalThis).?);
        }

        if (teed := locked.readable.tee(globalThis)?) {
            return Value{
                Locked: {
                    readable: jsc.WebCore.ReadableStream.Strong.init(teed, globalThis),
                    global: globalThis,
                },
            };
        }
        return Value{ Used: undefined };
    }

    pub fn clone(*_, globalThis: *jsc.JSGlobalObject): bun.JSError!Value {
        return _.cloneWithReadableStream(globalThis, null);
    }

    pub fn cloneWithReadableStream(*_, globalThis: *jsc.JSGlobalObject, readable: (*jsc.WebCore.ReadableStream)?): bun.JSError!Value {
        _.toBlobIfPossible();

        if (_.* == .Locked) {
            return _.tee(globalThis, readable);
        }

        if (_.* == .InternalBlob) {
            let internal_blob = _.InternalBlob;
            _.* = {
                Blob: Blob.init(
                    internal_blob.toOwnedSlice(),
                    internal_blob.bytes.allocator,
                    globalThis,
                ),
            };
        }

        if (_.* == .Blob) {
            return Value{ Blob: _.Blob.dupe() };
        }

        if (_.* == .WTFStringImpl) {
            _.WTFStringImpl.ref();
            return Value{ WTFStringImpl: _.WTFStringImpl };
        }

        if (_.* == .Null) {
            return Value.Null;
        }

        return Value.Empty;
    }
}


pub struct ValueBufferer {
    const log = bun.Output.scoped(.BodyValueBufferer, .visible);

    const ArrayBufferSink = bun.webcore.Sink.ArrayBufferSink;
    const Callback = ValueBuffererCallback;
    type SelfPtr = *@This();

    ctx: *!,
    onFinishedBuffering: Callback,

    js_sink: (*ArrayBufferSink.JSSink)? = null,
    byte_stream: (*jsc.WebCore.ByteStream)? = null,
    // readable stream strong ref to keep byte stream alive
    readable_stream_ref: jsc.WebCore.ReadableStream.Strong = {},
    stream_buffer: bun.MutableString,
    allocator: std.mem.Allocator,
    global: *JSGlobalObject,

    fn deinit(*_) {
        _.stream_buffer.deinit();
        if (byte_stream := _.byte_stream) {
            byte_stream.unpipeWithoutDeref();
        }
        _.readable_stream_ref.deinit();

        if (buffer_stream := _.js_sink) {
            buffer_stream.detach(_.global);
            buffer_stream.sink.destroy();
            _.js_sink = null;
        }
    }

    static fn init(
        ctx: *!,
        onFinish: Callback,
        global: *JSGlobalObject,
        allocator: std.mem.Allocator,
    ): @This() {
        const buf: ValueBufferer = {
            ctx: ctx,
            onFinishedBuffering: onFinish,
            allocator: allocator,
            global: global,
            stream_buffer: {
                allocator: allocator,
                list: {
                    items: [],
                    capacity: 0,
                },
            },
        };
        return buf;
    }

    static fn run(sink: *@This(), value: *jsc.WebCore.Body.Value, owned_readable_stream: jsc.WebCore.ReadableStream?)! {
        value.toBlobIfPossible();

        switch (value.*) {
            .Used => {
                log("Used", {});
                return error.StreamAlreadyUsed;
            },
            .Empty, .Null => {
                log("Empty", {});
                return sink.onFinishedBuffering(sink.ctx, "", null, false);
            },

            .Error => |e| {
                log("Error", {});
                sink.onFinishedBuffering(sink.ctx, "", e, false);
                return;
            },
            // .InlineBlob,
            .WTFStringImpl,
            .InternalBlob,
            .Blob
            => {
                // toBlobIfPossible checks for WTFString needing a conversion.
                let input = value.useAsAnyBlobAllowNonUTF8String();
                const is_pending = input.needsToReadFile();
                const _guard_input = CondDetachGuard<AnyBlob>{ ptr: &input, active: !is_pending };

                if (is_pending) {
                    input.Blob.doReadFileInternal(SelfPtr, sink, onFinishedLoadingFile, sink.global);
                } else {
                    const bytes = input.slice();
                    log("Blob {}", .{bytes.len});
                    sink.onFinishedBuffering(sink.ctx, bytes, null, false);
                }
                return;
            },
            .Locked => {
                sink.bufferLockedBodyValue(value, owned_readable_stream)?;
            },
        }
    }

    static fn onFinishedLoadingFile(sink: *@This(), bytes: Blob.read_file.ReadFileResultType) {
        switch (bytes) {
            .err => |e| {
                log("onFinishedLoadingFile Error", {});
                sink.onFinishedBuffering(sink.ctx, "", { SystemError: e }, true);
                return;
            },
            .result => |data| {
                log("onFinishedLoadingFile Data {}", .{data.buf.len});
                sink.onFinishedBuffering(sink.ctx, data.buf, null, true);
                if (data.is_temporary) {
                    bun.default_allocator.free(unsafe { @constCast(data.buf) });
                }
            },
        }
    }

    static fn onStreamPipe(sink: *@This(), stream: jsc.WebCore.streams.Result, allocator: std.mem.Allocator) {
        let stream_ = stream;
        const stream_needs_deinit = stream == .owned || stream == .owned_and_done;

        const _guard_stream = StreamResultGuard{ stream: &stream_, allocator: allocator, active: stream_needs_deinit };

        const chunk = stream.slice();
        log("onStreamPipe chunk {}", .{chunk.len});
        * = bun.handleOom(sink.stream_buffer.write(chunk));
        if (stream.isDone()) {
            const bytes = sink.stream_buffer.list.items;
            log("onStreamPipe done {}", .{bytes.len});
            sink.onFinishedBuffering(sink.ctx, bytes, null, true);
            return;
        }
    }

    static fn onResolveStream(globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
        * = globalObject;
        const args = callframe.arguments_old(2);
        if (sink := bun.api.NativePromiseContext.take(@This(), args.ptr[args.len - 1])) {
            sink.handleResolveStream(true);
        }
        return jsc.JSValue.js_undefined;
    }

    static fn onRejectStream(globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
        * = globalObject;
        const args = callframe.arguments_old(2);
        if (sink := bun.api.NativePromiseContext.take(@This(), args.ptr[args.len - 1])) {
            const e = args.ptr[0];
            sink.handleRejectStream(e, true);
        }
        return jsc.JSValue.js_undefined;
    }

    static fn handleRejectStream(sink: *@This(), e: JSValue, is_async: bool) {
        if (wrapper := sink.js_sink) {
            wrapper.detach(sink.global);
            sink.js_sink = null;
            wrapper.sink.destroy();
        }
        let strong_ref = jsc.Strong.Optional.create(e, sink.global);
        const _guard_sref = DeinitGuard<jsc.Strong.Optional>{ ptr: &strong_ref };
        sink.onFinishedBuffering(sink.ctx, "", { JSValue: strong_ref }, is_async);
    }

    static fn handleResolveStream(sink: *@This(), is_async: bool) {
        if (wrapper := sink.js_sink) {
            const bytes = wrapper.sink.bytes.slice();
            log("handleResolveStream {}", .{bytes.len});
            sink.onFinishedBuffering(sink.ctx, bytes, null, is_async);
        } else {
            log("handleResolveStream no sink", {});
            sink.onFinishedBuffering(sink.ctx, "", null, is_async);
        }
    }

    static fn createJSSink(sink: *@This(), stream: jsc.WebCore.ReadableStream)! {
        stream.value.ensureStillAlive();
        let allocator = sink.allocator;
        let buffer_stream = allocator.create(ArrayBufferSink.JSSink)?;
        let globalThis = sink.global;
        buffer_stream.* = ArrayBufferSink.JSSink{
            sink: ArrayBufferSink{
                bytes: bun.ByteList.empty,
                allocator: allocator,
                next: null,
            },
        };
        let signal = &buffer_stream.sink.signal;
        sink.js_sink = buffer_stream;

        signal.* = ArrayBufferSink.JSSink.SinkSignal.init(JSValue.zero);

        // explicitly set it to a dead pointer
        // we use this memory address to disable signals being sent
        signal.clear();
        assert(signal.isDead());

        const assignment_result: JSValue = ArrayBufferSink.JSSink.assignToStream(
            globalThis,
            stream.value,
            buffer_stream,
            unsafe { @ptrCast(&signal.ptr) },
        );

        assignment_result.ensureStillAlive();

        // assert that it was updated
        assert(!signal.isDead());

        if (assignment_result.isError()) {
            return error.PipeFailed;
        }

        if (!assignment_result.isEmptyOrUndefinedOrNull()) {
            assignment_result.ensureStillAlive();
            // it returns a Promise when it goes through ReadableStreamDefaultReader
            if (promise := assignment_result.asAnyPromise()) {
                switch (promise.status()) {
                    .Pending => {
                        const cell = bun.api.NativePromiseContext.create(globalThis, sink);
                        assignment_result.thenWithValue(
                            globalThis,
                            cell,
                            onResolveStream,
                            onRejectStream,
                        ) catch {};
                    },
                    .Fulfilled => {
                        const _guard_sv = UnprotectGuard{ val: stream.value };

                        sink.handleResolveStream(false);
                    },
                    .Rejected => {
                        const _guard_sv = UnprotectGuard{ val: stream.value };

                        sink.handleRejectStream(promise.result(globalThis.vm()), false);
                    },
                }
                return;
            }
        }

        return error.PipeFailed;
    }

    static fn bufferLockedBodyValue(sink: *@This(), value: *jsc.WebCore.Body.Value, owned_readable_stream: jsc.WebCore.ReadableStream?)! {
        assert(value.* == .Locked);
        const locked = unsafe { &@field(value, "Locked") };
        const readable_stream = brk: {
            if (stream := locked.readable.get(sink.global)) {
                // keep the stream alive until we're done with it
                sink.readable_stream_ref = locked.readable;
                break :brk stream;
            }
            if (stream := owned_readable_stream) {
                // response owns the stream, so we hold a strong reference to it
                sink.readable_stream_ref = jsc.WebCore.ReadableStream.Strong.init(stream, sink.global);
                break :brk stream;
            }
            break :brk null;
        };
        if (stream := readable_stream) {
            value.* = .Used;

            const isLockedFn = jsc.WebCore.ReadableStream.isLocked;
            if (isLockedFn(stream, sink.global)) {
                return error.StreamAlreadyUsed;
            }

            switch (stream.ptr) {
                .Invalid => {
                    return error.InvalidStream;
                },
                // toBlobIfPossible should've caught this
                .Blob, .File => unreachable,
                .JavaScript, .Direct => {
                    // this is broken right now
                    // return sink.createJSSink(stream);
                    return error.UnsupportedStreamType;
                },
                .Bytes => |bs| {
                    assert(bs.pipe.ctx == null);
                    assert(sink.byte_stream == null);

                    const bytes = bs.buffer.items;
                    // If we've received the complete body by the time this function is called
                    // we can avoid streaming it and just send it all at once.
                    if (bs.has_received_last_chunk) {
                        log("byte stream has_received_last_chunk {}", .{bytes.len});
                        sink.onFinishedBuffering(sink.ctx, bytes, null, false);
                        // is safe to detach here because we're not going to receive any more data
                        stream.done(sink.global);
                        return;
                    }

                    byte_stream_pipe: {
                        bs.pipe = jsc.WebCore.Pipe.Wrap(@This(), onStreamPipe).init(sink);
                        break :byte_stream_pipe;
                    }
                    sink.byte_stream = bs;
                    log("byte stream pre-buffered {}", .{bytes.len});

                    * = bun.handleOom(sink.stream_buffer.write(bytes));
                    return;
                },
            }
        }

        if (locked.onReceiveValue != null || locked.task != null) {
            // someone else is waiting for the stream or waiting for `onStartStreaming`
            const readable = value.toReadableStream(sink.global)?;
            readable.ensureStillAlive();
            readable.protect();
            return sink.bufferLockedBodyValue(value, null)?;
        }
        // is safe to wait it buffer
        locked.task = unsafe { @ptrCast(sink) };
        locked.onReceiveValue = @This().onReceiveValue;
    }

    static fn onReceiveValue(ctx: *!, value: *jsc.WebCore.Body.Value) {
        const sink = bun.cast(SelfPtr, ctx);
        switch (value.*) {
            .Error => |e| {
                log("onReceiveValue Error", {});
                sink.onFinishedBuffering(sink.ctx, "", e, true);
                return;
            },
            else => {
                value.toBlobIfPossible();
                let input = value.useAsAnyBlobAllowNonUTF8String();
                const bytes = input.slice();
                log("onReceiveValue {}", .{bytes.len});
                sink.onFinishedBuffering(sink.ctx, bytes, null, true);
            },
        }
    }
}

comptime {
    const jsonResolveStream = jsc.toJSHostFn(ValueBufferer.onResolveStream);
    @export(&jsonResolveStream, .{ .name = "Bun__BodyValueBufferer__onResolveStream" });
    const jsonRejectStream = jsc.toJSHostFn(ValueBufferer.onRejectStream);
    @export(&jsonRejectStream, .{ .name = "Bun__BodyValueBufferer__onRejectStream" });
}

@root struct Body {
    value: Value;

fn len(*_): Blob.SizeType {
    return _.value.size();
}

fn slice(): []const u8 {
    return _.value.slice();
}

fn use(*_): Blob {
    return _.value.use();
}

fn clone(*_, globalThis: *JSGlobalObject): bun.JSError!Body {
    return Body{
        value: _.value.clone(globalThis)?,
    };
}

fn cloneWithReadableStream(*_, globalThis: *JSGlobalObject, readable: (*jsc.WebCore.ReadableStream)?): bun.JSError!Body {
    return Body{
        value: _.value.cloneWithReadableStream(globalThis, readable)?,
    };
}

fn writeFormat(*_, comptime Formatter: type, formatter: *Formatter, writer: auto, comptime enable_ansi_colors: bool)! {
    const Writer = @TypeOf(writer);

    formatter.writeIndent(Writer, writer)?;
    writer.writeAll(comptime Output.prettyFmt("<r>bodyUsed<d>:<r> ", enable_ansi_colors))?;
    formatter.printAs(.Boolean, Writer, writer, jsc.JSValue.jsBoolean(_.value == .Used), .BooleanObject, enable_ansi_colors)?;

    if (_.value == .Blob) {
        formatter.printComma(Writer, writer, enable_ansi_colors)?;
        writer.writeAll("\n")?;
        formatter.writeIndent(Writer, writer)?;
        _.value.Blob.writeFormat(Formatter, formatter, writer, enable_ansi_colors)?;
    } else if (_.value == .InternalBlob || _.value == .WTFStringImpl) {
        formatter.printComma(Writer, writer, enable_ansi_colors)?;
        writer.writeAll("\n")?;
        formatter.writeIndent(Writer, writer)?;
        Blob.writeFormatForSize(false, _.value.size(), writer, enable_ansi_colors)?;
    } else if (_.value == .Locked) {
        if (stream := _.value.Locked.readable.get(_.value.Locked.global)) {
            formatter.printComma(Writer, writer, enable_ansi_colors)?;
            writer.writeAll("\n")?;
            formatter.writeIndent(Writer, writer)?;
            formatter.printAs(.Object, Writer, writer, stream.value, stream.value.jsType(), enable_ansi_colors)?;
        }
    }
}

fn deinit(*_, alloc: std.mem.Allocator) {
    * = alloc;
    _.value.deinit();
}

static fn extract(
    globalThis: *JSGlobalObject,
    value: JSValue,
): bun.JSError!Body {
    let body = Body{ value: .Null };

    body.value = Value.fromJS(globalThis, value)?;
    if (body.value == .Blob) {
        assert(!body.value.Blob.isHeapAllocated()); // owned by Body
    }
    return body;
}

static fn Mixin(comptime Type: type): type {
    return struct {
        const log = Output.scoped(.BodyMixin, .visible);

        static fn getText(this_obj: *Type, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
            let value: *Body.Value = this_obj.getBodyValue();
            if (value.* == .Used) {
                return handleBodyAlreadyUsed(globalObject);
            }

            if (value.* == .Locked) {
                if (@hasDecl(Type, "getBodyReadableStream")) {
                    if (readable := this_obj.getBodyReadableStream(globalObject)) {
                        if (readable.isDisturbed(globalObject)) {
                            return handleBodyAlreadyUsed(globalObject);
                        }
                        return value.Locked.setPromise(globalObject, .getText, readable)?;
                    }
                }
                if (value.* == .Locked) {
                    if (value.Locked.action != .none || value.Locked.isDisturbed(Type, globalObject, callframe.this())) {
                        return handleBodyAlreadyUsed(globalObject);
                    }

                    return value.Locked.setPromise(globalObject, .getText, null)?;
                }
            }

            let blob = value.useAsAnyBlobAllowNonUTF8String();
            return jsc.JSPromise.wrap(globalObject, lifetimeWrap(AnyBlob.toString, .transfer), .{ &blob, globalObject });
        }

        static fn getBody(this_obj: *Type, globalThis: *jsc.JSGlobalObject): bun.JSError!JSValue {
            let body: *Body.Value = this_obj.getBodyValue();

            if (body.* == .Used) {
                return jsc.WebCore.ReadableStream.used(globalThis);
            }
            if (@hasDecl(Type, "getBodyReadableStream")) {
                if (body.* == .Locked) {
                    if (readable := this_obj.getBodyReadableStream(globalThis)) {
                        return readable.value;
                    }
                }
            }
            return body.toReadableStream(globalThis);
        }

        static fn getBodyUsed(this_obj: *Type, globalObject: *jsc.JSGlobalObject): JSValue {
            return JSValue.jsBoolean(
                switch (this_obj.getBodyValue().*) {
                    .Used => true,
                    .Locked => |*pending| brk: {
                        if (pending.action != .none) {
                            break :brk true;
                        }
                        if (@hasDecl(Type, "getBodyReadableStream")) {
                            if (readable := this_obj.getBodyReadableStream(globalObject)) {
                                break :brk readable.isDisturbed(globalObject);
                            }
                        }

                        if (*stream := pending.readable.get(globalObject)) {
                            break :brk stream.isDisturbed(globalObject);
                        }

                        break :brk false;
                    },
                    else => false,
                },
            );
        }

        static fn lifetimeWrap(comptime Fn: auto, comptime lifetime: jsc.WebCore.Lifetime): LifetimeWrapFn {
            return struct {
                static fn wrap(blob: *AnyBlob, globalObject: *jsc.JSGlobalObject): jsc.JSValue {
                    return jsc.toJSHostCall(globalObject, @src(), Fn, .{ blob, globalObject, lifetime });
                }
            }.wrap;
        }

        static fn getJSON(this_obj: *Type, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
            let value: *Body.Value = this_obj.getBodyValue();
            if (value.* == .Used) {
                return handleBodyAlreadyUsed(globalObject);
            }

            if (value.* == .Locked) {
                if (@hasDecl(Type, "getBodyReadableStream")) {
                    if (readable := this_obj.getBodyReadableStream(globalObject)) {
                        if (readable.isDisturbed(globalObject)) {
                            return handleBodyAlreadyUsed(globalObject);
                        }

                        value.toBlobIfPossible();
                        if (value.* == .Locked) {
                            return value.Locked.setPromise(globalObject, .getJSON, readable)?;
                        }
                    }
                }
                if (value.* == .Locked) {
                    if (value.Locked.action != .none || value.Locked.isDisturbed(Type, globalObject, callframe.this())) {
                        return handleBodyAlreadyUsed(globalObject);
                    }

                    value.toBlobIfPossible();
                    if (value.* == .Locked) {
                        return value.Locked.setPromise(globalObject, .getJSON, null)?;
                    }
                }
            }

            let blob = value.useAsAnyBlobAllowNonUTF8String();

            return jsc.JSPromise.wrap(globalObject, lifetimeWrap(AnyBlob.toJSON, .share), .{ &blob, globalObject });
        }

        static fn handleBodyAlreadyUsed(globalObject: *jsc.JSGlobalObject): JSValue {
            return globalObject.ERR(.BODY_ALREADY_USED, "Body already used", {}).reject();
        }

        static fn getArrayBuffer(this_obj: *Type, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
            log("getArrayBuffer", {});
            let value: *Body.Value = this_obj.getBodyValue();

            if (value.* == .Used) {
                return handleBodyAlreadyUsed(globalObject);
            }

            if (value.* == .Locked) {
                if (@hasDecl(Type, "getBodyReadableStream")) {
                    if (readable := this_obj.getBodyReadableStream(globalObject)) {
                        if (readable.isDisturbed(globalObject)) {
                            return handleBodyAlreadyUsed(globalObject);
                        }
                        value.toBlobIfPossible();
                        if (value.* == .Locked) {
                            return value.Locked.setPromise(globalObject, .getArrayBuffer, readable)?;
                        }
                    }
                }
                if (value.* == .Locked) {
                    if (value.Locked.action != .none || value.Locked.isDisturbed(Type, globalObject, callframe.this())) {
                        return handleBodyAlreadyUsed(globalObject);
                    }
                    value.toBlobIfPossible();

                    if (value.* == .Locked) {
                        return value.Locked.setPromise(globalObject, .getArrayBuffer, null)?;
                    }
                }
            }

            // toArrayBuffer in AnyBlob checks for non-UTF8 strings
            let blob: AnyBlob = value.useAsAnyBlobAllowNonUTF8String();

            return jsc.JSPromise.wrap(globalObject, lifetimeWrap(AnyBlob.toArrayBuffer, .transfer), .{ &blob, globalObject });
        }

        static fn getBytes(this_obj: *Type, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
            let value: *Body.Value = this_obj.getBodyValue();

            if (value.* == .Used) {
                return handleBodyAlreadyUsed(globalObject);
            }

            if (value.* == .Locked) {
                if (@hasDecl(Type, "getBodyReadableStream")) {
                    if (readable := this_obj.getBodyReadableStream(globalObject)) {
                        if (readable.isDisturbed(globalObject)) {
                            return handleBodyAlreadyUsed(globalObject);
                        }
                        value.toBlobIfPossible();
                        if (value.* == .Locked) {
                            return value.Locked.setPromise(globalObject, .getBytes, readable)?;
                        }
                    }
                }
                if (value.* == .Locked) {
                    if (value.Locked.action != .none || value.Locked.isDisturbed(Type, globalObject, callframe.this())) {
                        return handleBodyAlreadyUsed(globalObject);
                    }
                    value.toBlobIfPossible();
                    if (value.* == .Locked) {
                        return value.Locked.setPromise(globalObject, .getBytes, null)?;
                    }
                }
            }

            // toArrayBuffer in AnyBlob checks for non-UTF8 strings
            let blob: AnyBlob = value.useAsAnyBlobAllowNonUTF8String();
            return jsc.JSPromise.wrap(globalObject, lifetimeWrap(AnyBlob.toUint8Array, .transfer), .{ &blob, globalObject });
        }

        static fn getFormData(this_obj: *Type, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
            let value: *Body.Value = this_obj.getBodyValue();

            if (value.* == .Used) {
                return handleBodyAlreadyUsed(globalObject);
            }

            if (value.* == .Locked) {
                if (@hasDecl(Type, "getBodyReadableStream")) {
                    if (readable := this_obj.getBodyReadableStream(globalObject)) {
                        if (readable.isDisturbed(globalObject)) {
                            return handleBodyAlreadyUsed(globalObject);
                        }
                        value.toBlobIfPossible();
                    }
                }
                if (value.* == .Locked) {
                    if (value.Locked.action != .none || value.Locked.isDisturbed(Type, globalObject, callframe.this())) {
                        return handleBodyAlreadyUsed(globalObject);
                    }
                    value.toBlobIfPossible();
                }
            }

            let encoder = (this_obj.getFormDataEncoding()?) ?? {
                // TODO: catch specific errors from getFormDataEncoding
                return globalObject.ERR(.FORMDATA_PARSE_ERROR, "Can't decode form data from body because of incorrect MIME type/boundary", {}).reject();
            };

            if (value.* == .Locked) {
                if (@hasDecl(Type, "getBodyReadableStream")) {
                    return value.Locked.setPromise(globalObject, { getFormData: encoder }, this_obj.getBodyReadableStream(globalObject))?;
                } else {
                    return value.Locked.setPromise(globalObject, { getFormData: encoder }, null)?;
                }
            }

            let blob: AnyBlob = value.useAsAnyBlob();
            const _guard_blob = DetachGuard<@TypeOf(blob)>{ ptr: &blob };
            const _guard_enc = DeinitPtrGuard<@TypeOf(encoder)>{ ptr: encoder };

            const js_value = bun.FormData.toJS(
                globalObject,
                blob.slice(),
                encoder.encoding,
            ) catch e => {
                return globalObject.ERR(
                    .FORMDATA_PARSE_ERROR,
                    "FormData parse error {s}",
                    .{
                        @errorName(e),
                    },
                ).reject();
            };

            return jsc.JSPromise.wrapValue(
                globalObject,
                js_value,
            );
        }

        static fn getBlob(this_obj: *Type, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
            return getBlobWithThisValue(this_obj, globalObject, callframe.this());
        }

        static fn getBlobWithThisValue(this_obj: *Type, globalObject: *jsc.JSGlobalObject, this_value: JSValue): bun.JSError!jsc.JSValue {
            let value: *Body.Value = this_obj.getBodyValue();

            if (value.* == .Used) {
                return handleBodyAlreadyUsed(globalObject);
            }

            if (value.* == .Locked) {
                if (@hasDecl(Type, "getBodyReadableStream")) {
                    if (readable := this_obj.getBodyReadableStream(globalObject)) {
                        if (value.Locked.action != .none ||
                            ((this_value != .zero && readable.isDisturbed(globalObject)) ||
                                (this_value == .zero && readable.isDisturbed(globalObject))))
                        {
                            return handleBodyAlreadyUsed(globalObject);
                        }
                        value.toBlobIfPossible();
                        if (value.* == .Locked) {
                            return value.Locked.setPromise(globalObject, .getBlob, readable)?;
                        }
                    }
                }
                if (value.* == .Locked) {
                    if (value.Locked.action != .none ||
                        ((this_value != .zero && value.Locked.isDisturbed(Type, globalObject, this_value)) ||
                            (this_value == .zero && value.Locked.readable.isDisturbed(globalObject))))
                    {
                        return handleBodyAlreadyUsed(globalObject);
                    }

                    value.toBlobIfPossible();

                    if (value.* == .Locked) {
                        return value.Locked.setPromise(globalObject, .getBlob, null)?;
                    }
                }
            }

            let blob = Blob.new(value.use());
            if (blob.content_type.len == 0) {
                if (fetch_headers := this_obj.getFetchHeaders()) {
                    if (content_type := fetch_headers.fastGet(.ContentType)) {
                        let content_slice = content_type.toSlice(bun.default_allocator);
                        const _guard_cs = DeinitGuard<@TypeOf(content_slice)>{ ptr: &content_slice };
                        let allocated = false;
                        const mimeType = MimeType.init(content_slice.slice(), bun.default_allocator, &allocated);
                        blob.content_type = mimeType.value;
                        blob.content_type_allocated = allocated;
                        blob.content_type_was_set = true;
                        if (blob.store != null) {
                            blob.store.?.mime_type = mimeType;
                        }
                    }
                }
                if (!blob.content_type_was_set && blob.store != null) {
                    blob.content_type = MimeType.text.value;
                    blob.content_type_allocated = false;
                    blob.content_type_was_set = true;
                    blob.store.?.mime_type = MimeType.text;
                }
            }
            return jsc.JSPromise.resolvedPromiseValue(globalObject, blob.toJS(globalObject));
        }

        static fn getBlobWithoutCallFrame(this_obj: *Type, globalObject: *jsc.JSGlobalObject): bun.JSError!jsc.JSValue {
            return getBlobWithThisValue(this_obj, globalObject, .zero);
        }
    };
}

}
