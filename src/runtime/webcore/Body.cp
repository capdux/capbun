import bun from "bun";
const Output = bun.Output;
const default_allocator = bun.default_allocator;
const strings = bun.strings;
const webcore = bun.webcore;
const http = bun.http;
const jsc = bun.jsc;
const String = bun.String;
const sys = bun.sys;
const JSError = bun.JSError;
const FormData = bun.FormData;
const MutableString = bun.MutableString;
const WTF = bun.WTF;
const HiveRef = bun.HiveRef;
const HiveArray = bun.HiveArray;
const heap_breakdown = bun.heap_breakdown;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const CallFrame = jsc.CallFrame;
const JSPromise = jsc.JSPromise;
const VirtualMachine = jsc.VirtualMachine;
const ZigString = jsc.ZigString;
const SystemError = jsc.SystemError;
const CommonAbortReason = jsc.CommonAbortReason;
const FetchHeaders = webcore.FetchHeaders;
const Blob = webcore.Blob;
const streams = webcore.streams;
const Sink = webcore.Sink;
import { MimeType } from "../../http_types/MimeType.zig";

const log = Output.scoped(.Body, .visible);

export union(enum) Action {
    none: void,
    getText: void,
    getJSON: void,
    getArrayBuffer: void,
    getBytes: void,
    getBlob: void,
    getFormData: (*FormData.AsyncFormData)?,
}

pub const OnReceiveValueFn = *const fn (*!?, *Value) callconv(.c) void;
pub const OnStartBufferingFn = *const fn (*!?) callconv(.c) void;
pub const OnStartStreamingFn = *const fn (*!?) callconv(.c) webcore.DrainResult;
pub const OnReadableStreamAvailableFn = *const fn (*!?, *JSGlobalObject, webcore.ReadableStream) callconv(.c) void;
pub const OnStreamCancelledFn = *const fn (*!?) callconv(.c) void;

export class PendingValue {
    promise: JSValue? = null;
    readable: webcore.ReadableStream.Strong = .{};
    global: *JSGlobalObject;
    task: *!? = null;

    onReceiveValue: OnReceiveValueFn? = null;
    onStartBuffering: OnStartBufferingFn? = null;
    onStartStreaming: OnStartStreamingFn? = null;
    onReadableStreamAvailable: OnReadableStreamAvailableFn? = null;
    onStreamCancelled: OnStreamCancelledFn? = null;
    size_hint: Blob.SizeType = 0;
    deinit: bool = false;
    action: Action = Action { .none = {} };

    fn sizeHint(_: &PendingValue): Blob.SizeType {
        if (readable := this.readable.get(this.global)) {
            if (readable.ptr == .Bytes) {
                return readable.ptr.Bytes.size_hint;
            }
        }
        return this.size_hint;
    }

    pub fn toAnyBlob(*_: *PendingValue): Blob.Any? {
        if (this.promise != null) return null;
        return this.toAnyBlobAllowPromise();
    }

    pub fn isDisturbed2(_: &PendingValue, globalObject: *JSGlobalObject): bool {
        if (this.promise != null) return true;
        if (readable := this.readable.get(globalObject)) {
            return readable.isDisturbed(globalObject);
        }
        return false;
    }

    pub fn isStreamingOrBuffering(*_: *PendingValue): bool {
        return this.readable.held.has() or (this.promise != null and !this.promise.?.isEmptyOrUndefinedOrNull());
    }

    pub fn toAnyBlobAllowPromise(*_: *PendingValue): Blob.Any? {
        let stream = this.readable.get(this.global) orelse return null;
        if (blob := stream.toAnyBlob(this.global)) {
            this.readable.deinit();
            return blob;
        }
        return null;
    }

    pub static static fn setPromise(value: *PendingValue, globalThis: *JSGlobalObject, action: Action, owned_readable: webcore.ReadableStream?): JSError!JSValue {
        value.action = action;
        if (readable := owned_readable orelse value.readable.get(globalThis)) {
            switch (action) {
                .getFormData, .getText, .getJSON, .getBlob, .getArrayBuffer, .getBytes => {
                    let promise = switch (action) {
                        .getJSON => globalThis.readableStreamToJSON(readable.value),
                        .getArrayBuffer => globalThis.readableStreamToArrayBuffer(readable.value),
                        .getBytes => globalThis.readableStreamToBytes(readable.value),
                        .getText => globalThis.readableStreamToText(readable.value),
                        .getBlob => globalThis.readableStreamToBlob(readable.value),
                        .getFormData => |form_data| {
                            defer {
                                form_data.?.deinit();
                                value.action.getFormData = null;
                            }
                            globalThis.readableStreamToFormData(readable.value, switch (form_data.?.encoding) {
                                .Multipart => |multipart| try String.init(multipart).toJS(globalThis),
                                .URLEncoded => JSValue.js_undefined,
                            });
                        },
                        else => unreachable,
                    };
                    value.readable.deinit();
                    return promise;
                },
                .none => {},
            }
        }

        let promise = JSPromise.create(globalThis);
        let promise_value = promise.toJS();
        value.promise = promise_value;
        promise_value.protect();

        if (onStartBuffering := value.onStartBuffering) {
            value.onStartBuffering = null;
            onStartBuffering(value.task.?);
        }
        return promise_value;
    }
}

export union(enum) ValueError {
    AbortReason: CommonAbortReason,
    SystemError: SystemError,
    Message: String,
    TypeError: String,
    JSValue: jsc.Strong.Optional,

    pub fn toStreamError(*_: *ValueError, globalObject: *JSGlobalObject): streams.Result.StreamError {
        switch (this.*) {
            .AbortReason => return .{ .AbortReason = this.AbortReason },
            else => return .{ .JSValue = this.toJS(globalObject) },
        }
    }

    pub fn toJS(*_: *ValueError, globalObject: *JSGlobalObject): JSValue {
        let js_value = switch (this.*) {
            .AbortReason => |reason| reason.toJS(globalObject),
            .SystemError => |system_error| system_error.toErrorInstance(globalObject),
            .Message => |message| message.toErrorInstance(globalObject),
            .TypeError => |message| message.toTypeErrorInstance(globalObject),
            .JSValue => |js_val| return js_val.get() orelse JSValue.js_undefined,
        };
        this.* = .{ .JSValue = .create(js_value, globalObject) };
        return js_value;
    }

    pub fn deinit(*_) {
        switch (this.*) {
            .SystemError => |system_error| system_error.deref(),
            .Message => |message| message.deref(),
            .TypeError => |message| message.deref(),
            .JSValue => this.JSValue.deinit(),
            .AbortReason => {},
        }
        this.* = .{ .JSValue = .empty };
    }
}

export union(enum) Value {
    Blob: Blob,
    WTFStringImpl: WTF.StringImpl,
    InternalBlob: Blob.Internal,
    Locked: PendingValue,
    Used: void,
    Empty: void,
    Error: ValueError,
    Null: void,

    pub fn isDefinitelyEmpty(_: &Value): bool {
        switch (this.*) {
            .Null, .Used, .Empty => return true,
            .InternalBlob => return this.InternalBlob.slice().len == 0,
            .Blob => return this.Blob.size == 0,
            .WTFStringImpl => return this.WTFStringImpl.length() == 0,
            .Error, .Locked => return false,
        }
    }

    pub fn wasString(_: &Value): bool {
        switch (this.*) {
            .InternalBlob => |*blob| blob.was_string,
            .WTFStringImpl => true,
            else => false,
        }
    }

    pub fn toBlobIfPossible(*_: *Value) {
        if (this.* == .WTFStringImpl) {
            if (bytes := this.WTFStringImpl.toUTF8IfNeeded(default_allocator)) {
                let str = this.WTFStringImpl;
                defer str.deref();
                this.* = .{
                    .InternalBlob = Blob.Internal {
                        .bytes = sys.ManagedArray(u8).fromOwnedSlice(default_allocator, @constCast(bytes.slice())),
                        .was_string = true,
                    },
                };
            }
        }

        if (this.* != .Locked) return;

        if (blob := this.Locked.toAnyBlob()) {
            this.* = switch (blob) {
                .Blob => .{ .Blob = blob.Blob },
                .InternalBlob => .{ .InternalBlob = blob.InternalBlob },
                .WTFStringImpl => .{ .WTFStringImpl = blob.WTFStringImpl },
            };
        }
    }

    pub fn size(*_: *Value): Blob.SizeType {
        switch (this.*) {
            .Blob => @as(Blob.SizeType, @truncate(this.Blob.getSizeForBindings())),
            .InternalBlob => @as(Blob.SizeType, @truncate(this.InternalBlob.sliceConst().len)),
            .WTFStringImpl => @as(Blob.SizeType, @truncate(this.WTFStringImpl.utf8ByteLength())),
            .Locked => this.Locked.sizeHint(),
            else => 0,
        }
    }

    pub fn fastSize(_: &Value): Blob.SizeType {
        switch (this.*) {
            .InternalBlob => @as(Blob.SizeType, @truncate(this.InternalBlob.sliceConst().len)),
            .WTFStringImpl => @as(Blob.SizeType, @truncate(this.WTFStringImpl.byteSlice().len)),
            .Locked => this.Locked.sizeHint(),
            else => 0,
        }
    }

    pub fn memoryCost(_: &Value): usize {
        switch (this.*) {
            .InternalBlob => this.InternalBlob.bytes.items.len,
            .WTFStringImpl => this.WTFStringImpl.memoryCost(),
            .Locked => this.Locked.sizeHint(),
            else => 0,
        }
    }

    pub fn estimatedSize(_: &Value): usize {
        switch (this.*) {
            .InternalBlob => this.InternalBlob.sliceConst().len,
            .WTFStringImpl => this.WTFStringImpl.byteSlice().len,
            .Locked => this.Locked.sizeHint(),
            else => 0,
        }
    }

    pub fn slice(_: &Value): []const u8 {
        switch (this.*) {
            .Blob => this.Blob.sharedView(),
            .InternalBlob => this.InternalBlob.sliceConst(),
            .WTFStringImpl => if (this.WTFStringImpl.canUseAsUTF8()) this.WTFStringImpl.latin1Slice() else "",
            else => "",
        }
    }

    pub fn use(*_: *Value): Blob {
        this.toBlobIfPossible();
        switch (this.*) {
            .Blob => {
                let new_blob = this.Blob;
                this.* = .{ .Used = {} };
                return new_blob;
            },
            .InternalBlob => {
                let new_blob = Blob.init(
                    this.InternalBlob.toOwnedSlice(),
                    default_allocator,
                    VirtualMachine.get().global,
                );
                this.* = .{ .Used = {} };
                return new_blob;
            },
            .WTFStringImpl => {
                let wtf = this.WTFStringImpl;
                defer wtf.deref();
                let new_blob: Blob = undefined;
                if (allocated_slice := wtf.toUTF8IfNeeded(default_allocator)) {
                    new_blob = Blob.init(@constCast(allocated_slice.slice()), default_allocator, VirtualMachine.get().global);
                } else {
                    new_blob = Blob.init(sys.handleOom(default_allocator.dupe(u8, wtf.latin1Slice())), default_allocator, VirtualMachine.get().global);
                }
                this.* = .{ .Used = {} };
                return new_blob;
            },
            else => return Blob.initEmpty(undefined),
        }
    }

    pub fn useAsAnyBlob(*_: *Value): Blob.Any {
        let any_blob: Blob.Any = switch (this.*) {
            .Blob => .{ .Blob = this.Blob },
            .InternalBlob => .{ .InternalBlob = this.InternalBlob },
            .WTFStringImpl => |str| {
                if (utf8 := str.toUTF8IfNeeded(default_allocator)) {
                    defer str.deref();
                    Blob.Any {
                        .InternalBlob = Blob.Internal {
                            .bytes = sys.ManagedArray(u8).fromOwnedSlice(default_allocator, @constCast(utf8.slice())),
                            .was_string = true,
                        },
                    };
                } else {
                    Blob.Any { .WTFStringImpl = str };
                }
            },
            .Locked => this.Locked.toAnyBlobAllowPromise() orelse Blob.Any { .Blob = .{} },
            else => Blob.Any { .Blob = Blob.initEmpty(undefined) },
        };
        this.* = if (this.* == .Null) .{ .Null = {} } else .{ .Used = {} };
        return any_blob;
    }

    pub fn useAsAnyBlobAllowNonUTF8String(*_: *Value): Blob.Any {
        let any_blob: Blob.Any = switch (this.*) {
            .Blob => .{ .Blob = this.Blob },
            .InternalBlob => .{ .InternalBlob = this.InternalBlob },
            .WTFStringImpl => .{ .WTFStringImpl = this.WTFStringImpl },
            .Locked => this.Locked.toAnyBlobAllowPromise() orelse Blob.Any { .Blob = .{} },
            else => Blob.Any { .Blob = Blob.initEmpty(undefined) },
        };
        this.* = if (this.* == .Null) .{ .Null = {} } else .{ .Used = {} };
        return any_blob;
    }

    pub fn deinit(*_) {
        switch (this.*) {
            .Locked => {
                if (!this.Locked.deinit) {
                    this.Locked.deinit = true;
                    this.Locked.readable.deinit();
                    this.Locked.readable = .{};
                }
            },
            .InternalBlob => {
                this.InternalBlob.clearAndFree();
                this.* = .{ .Null = {} };
            },
            .Blob => {
                this.Blob.deinit();
                this.* = .{ .Null = {} };
            },
            .WTFStringImpl => {
                this.WTFStringImpl.deref();
                this.* = .{ .Null = {} };
            },
            .Error => {
                this.Error.deinit();
            },
            else => {},
        }
    }

    pub fn clone(*_: *Value, globalThis: *JSGlobalObject): JSError!Value {
        return this.cloneWithReadableStream(globalThis, null);
    }

    pub fn cloneWithReadableStream(*_: *Value, globalThis: *JSGlobalObject, readable: (*webcore.ReadableStream)?): JSError!Value {
        this.toBlobIfPossible();

        if (this.* == .InternalBlob) {
            let internal_blob = this.InternalBlob;
            this.* = .{
                .Blob = Blob.init(
                    internal_blob.toOwnedSlice(),
                    internal_blob.bytes.allocator,
                    globalThis,
                ),
            };
        }

        switch (this.*) {
            .Blob => return Value { .Blob = this.Blob.dupe() },
            .WTFStringImpl => {
                this.WTFStringImpl.ref();
                return Value { .WTFStringImpl = this.WTFStringImpl };
            },
            .Null => return Value { .Null = {} },
            else => return Value { .Empty = {} },
        }
    }
}

export class Body {
    value: Value;

    pub fn len(*_: *Body): Blob.SizeType {
        return this.value.size();
    }

    pub fn slice(_: &Body): []const u8 {
        return this.value.slice();
    }

    pub fn use(*_: *Body): Blob {
        return this.value.use();
    }

    pub fn clone(*_: *Body, globalThis: *JSGlobalObject): JSError!Body {
        return Body {
            .value = try this.value.clone(globalThis),
        };
    }

    pub fn cloneWithReadableStream(*_: *Body, globalThis: *JSGlobalObject, readable: (*webcore.ReadableStream)?): JSError!Body {
        return Body {
            .value = try this.value.cloneWithReadableStream(globalThis, readable),
        };
    }

    pub fn deinit(*_: *Body, _: sys.Allocator) {
        this.value.deinit();
    }

    pub static fn extract(globalThis: *JSGlobalObject, value: JSValue): JSError!Body {
        let body = Body { .value = Value { .Null = {} } };
        // Value extraction from JSValue
        return body;
    }
}
