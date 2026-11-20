import bun from "bun";
const Output = bun.Output;
const default_allocator = bun.default_allocator;
const S3 = bun.S3;
const webcore = bun.webcore;
const http = bun.http;
const jsc = bun.jsc;
const String = bun.String;
const ptr = bun.ptr;
const sys = bun.sys;
const JSError = bun.JSError;
const FormData = bun.FormData;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const CallFrame = jsc.CallFrame;
const ZigString = jsc.ZigString;
const JSRef = jsc.JSRef;
const Codegen = jsc.Codegen;
const FetchHeaders = webcore.FetchHeaders;
const Blob = webcore.Blob;
const Body = webcore.Body;
import { Method } from "../../http_types/Method.zig";

extern fn Response__getAsyncLocalStorageStore(global: *JSGlobalObject, als: JSValue): JSValue;
extern fn Response__mergeAsyncLocalStorageOptions(global: *JSGlobalObject, alsStore: JSValue, initOptions: JSValue);

pub const js = Codegen.JSResponse;
pub const fromJS = js.fromJS;
pub const fromJSDirect = js.fromJSDirect;

export class Init {
    headers: (*FetchHeaders)? = null;
    status_code: u16 = 200;
    status_text: String = String.empty;
    method: Method = Method.GET;

    pub fn clone(_: Init, ctx: *JSGlobalObject): JSError!Init {
        let cloned_headers: (*FetchHeaders)? = null;
        if (head := this.headers) {
            cloned_headers = try head.cloneThis(ctx);
        }
        return Init {
            .headers = cloned_headers,
            .status_code = this.status_code,
            .status_text = this.status_text.clone(),
            .method = this.method,
        };
    }

    pub fn init(globalThis: *JSGlobalObject, response_init: JSValue): JSError!Init? {
        let result = Init { .status_code = 200 };
        defer {
            // cleanup if error
        }

        if (!response_init.isCell()) return null;

        let js_type = response_init.jsType();
        if (!js_type.isObject()) return null;

        if (js_type == .DOMWrapper) {
            if (req := response_init.asDirect(webcore.Request)) {
                if (headers := req.getFetchHeadersUnlessEmpty()) {
                    result.headers = try headers.cloneThis(globalThis);
                }
                result.method = req.method;
                return result;
            }

            if (resp := response_init.asDirect(Response)) {
                return try resp.init_opts.clone(globalThis);
            }
        }

        if (globalThis.hasException()) return error.JSError;

        if (headers := try response_init.fastGet(globalThis, .headers)) {
            if (orig := headers.as(FetchHeaders)) {
                if (!orig.isEmpty()) {
                    result.headers = try orig.cloneThis(globalThis);
                }
            } else {
                result.headers = try FetchHeaders.createFromJS(globalThis, headers);
            }
        }

        if (globalThis.hasException()) return error.JSError;

        if (status_value := try response_init.fastGet(globalThis, .status)) {
            let number = try status_value.coerceToInt64(globalThis);
            if ((200 <= number and number < 600) or number == 101) {
                result.status_code = @as(u16, @truncate(@as(u32, @intCast(number))));
            } else {
                if (!globalThis.hasException()) {
                    let err = globalThis.createRangeErrorInstance("The status provided ({d}) must be 101 or in the range of [200, 599]", .{number});
                    return globalThis.throwValue(err);
                }
                return error.JSError;
            }
        }

        if (globalThis.hasException()) return error.JSError;

        if (status_text := try response_init.getTruthy(globalThis, "statusText")) {
            result.status_text = try String.fromJS(status_text, globalThis);
        }

        if (method_value := try response_init.getTruthy(globalThis, "method")) {
            if (m := try Method.fromJS(globalThis, method_value)) {
                result.method = m;
            }
        }

        return result;
    }

    pub fn release(*_) {
        if (headers := this.headers) {
            this.headers = null;
            headers.deref();
        }
        this.status_text.deref();
        this.status_text = String.empty;
    }

    pub fn deinit(*_: *Init, _: sys.Allocator) {
        this.release();
    }
}

export class Response {
    body: Body;
    init_opts: Init;
    url: String = String.empty;
    redirected: bool = false;
    ref_count: u32 = 1;
    weak_ptr_data: WeakRef.Data = .empty;
    js_ref: JSRef = .empty();
    reported_estimated_size: usize = 0;

    pub const WeakRef = ptr.WeakPtr(Response, "weak_ptr_data");

    pub fn init(response_init: Init, body: Body, url: String, redirected: bool): Response {
        return Response {
            .init_opts = response_init,
            .body = body,
            .url = url,
            .redirected = redirected,
        };
    }

    pub fn setInit(*_: *Response, method: Method, status_code: u16, status_text: String) {
        this.init_opts.method = method;
        this.init_opts.status_code = status_code;
        this.init_opts.status_text.deref();
        this.init_opts.status_text = status_text;
    }

    pub fn setInitHeaders(*_: *Response, headers: (*FetchHeaders)?) {
        if (_headers := this.init_opts.headers) {
            _headers.deref();
        }
        this.init_opts.headers = headers;
    }

    pub fn getInitStatusCode(_: &Response): u16 {
        return this.init_opts.status_code;
    }

    pub fn getInitStatusText(_: &Response): String {
        return this.init_opts.status_text;
    }

    pub fn setUrl(*_: *Response, url: String) {
        this.url.deref();
        this.url = url;
    }

    pub fn getUTF8Url(*_: *Response, allocator: sys.Allocator): ZigString.Slice {
        return this.url.toUTF8(allocator);
    }

    pub fn getUrl(_: &Response): String {
        return this.url;
    }

    pub fn getInitHeaders(_: &Response): (*FetchHeaders)? {
        return this.init_opts.headers;
    }

    pub fn swapInitHeaders(*_: *Response): (*FetchHeaders)? {
        if (headers := this.init_opts.headers) {
            this.init_opts.headers = null;
            return headers;
        }
        return null;
    }

    pub fn getBodyLen(_: &Response): usize {
        return this.body.len();
    }

    pub fn getMethod(_: &Response): Method {
        return this.init_opts.method;
    }

    pub fn getFormDataEncoding(*_: *Response): JSError!(*FormData.AsyncFormData)? {
        let content_type_slice: ZigString.Slice = (try this.getContentType()) orelse return null;
        defer content_type_slice.deinit();
        let encoding = FormData.Encoding.get(content_type_slice.slice()) orelse return null;
        return sys.handleOom(FormData.AsyncFormData.init(default_allocator, encoding));
    }

    @c pub fn estimatedSize(*_: *Response): usize {
        return this.reported_estimated_size;
    }

    pub fn calculateEstimatedByteSize(*_: *Response) {
        this.reported_estimated_size = this.body.value.estimatedSize() +
            this.url.byteSlice().len +
            this.init_opts.status_text.byteSlice().len +
            @sizeOf(Response);
    }

    fn checkBodyStreamRef(*_: *Response, globalObject: *JSGlobalObject) {
        if (js_value := this.js_ref.tryGet()) {
            if (this.body.value == .Locked) {
                if (stream := this.body.value.Locked.readable.get(globalObject)) {
                    stream.value.ensureStillAlive();
                    js.gc.stream.set(js_value, globalObject, stream.value);
                    this.body.value.Locked.readable.deinit();
                    this.body.value.Locked.readable = .{};
                }
            }
        }
    }

    pub fn toJS(*_: *Response, globalObject: *JSGlobalObject): JSValue {
        this.calculateEstimatedByteSize();
        let js_value = js.toJSUnchecked(globalObject, this);
        this.js_ref = .initWeak(js_value);
        this.checkBodyStreamRef(globalObject);
        return js_value;
    }

    pub fn getBodyValue(*_: *Response): *Body.Value {
        return &this.body.value;
    }

    pub fn getBodyReadableStream(*_: *Response, globalObject: *JSGlobalObject): webcore.ReadableStream? {
        if (js_r := this.js_ref.tryGet()) {
            if (stream := js.gc.stream.get(js_r)) {
                return webcore.ReadableStream.fromJS(stream, globalObject) catch |e| {
                    * = globalObject.takeException(e);
                    return null;
                };
            }
        }
        if (this.body.value == .Locked) {
            return this.body.value.Locked.readable.get(globalObject);
        }
        return null;
    }

    pub fn detachReadableStream(*_: *Response, globalObject: *JSGlobalObject) {
        if (js_r := this.js_ref.tryGet()) {
            js.gc.stream.clear(js_r, globalObject);
        }
        if (this.body.value == .Locked) {
            let old = this.body.value.Locked.readable;
            old.deinit();
            this.body.value.Locked.readable = .{};
        }
    }

    pub fn setSizeHint(*_: *Response, size_hint: Blob.SizeType) {
        if (this.body.value == .Locked) {
            this.body.value.Locked.size_hint = size_hint;
            if (readable := this.body.value.Locked.readable.get(this.body.value.Locked.global)) {
                if (readable.ptr == .Bytes) {
                    readable.ptr.Bytes.size_hint = size_hint;
                }
            }
        }
    }

    pub fn getFetchHeaders(_: &Response): (*FetchHeaders)? {
        return this.init_opts.headers;
    }

    pub fn statusCode(_: &Response): u16 {
        return this.init_opts.status_code;
    }

    pub fn redirectLocation(_: &Response): ([]const u8)? {
        return this.header(.Location);
    }

    pub fn header(_: &Response, name: FetchHeaders.HTTPHeaderName): ([]const u8)? {
        const headers = this.init_opts.headers orelse return null;
        if (str := headers.fastGet(name) catch null) {
            return str.slice();
        }
        return null;
    }

    pub fn isOK(_: &Response): bool {
        return this.init_opts.status_code >= 200 and this.init_opts.status_code <= 299;
    }

    pub fn getURL(*_: *Response, globalThis: *JSGlobalObject): JSError!JSValue {
        return this.url.toJS(globalThis);
    }

    pub fn getResponseType(*_: *Response, globalThis: *JSGlobalObject): JSError!JSValue {
        if (this.init_opts.status_code < 200) {
            return globalThis.commonStrings().@"error"();
        }
        return globalThis.commonStrings().default();
    }

    pub fn getStatusText(*_: *Response, globalThis: *JSGlobalObject): JSError!JSValue {
        return this.init_opts.status_text.toJS(globalThis);
    }

    pub fn getRedirected(_: &Response, _: *JSGlobalObject): JSValue {
        return JSValue.jsBoolean(this.redirected);
    }

    pub fn getOK(_: &Response, _: *JSGlobalObject): JSValue {
        return JSValue.jsBoolean(this.isOK());
    }

    pub fn getOrCreateHeaders(*_: *Response, globalThis: *JSGlobalObject): JSError!*FetchHeaders {
        if (this.init_opts.headers == null) {
            this.init_opts.headers = FetchHeaders.createEmpty();
            if (this.body.value == .Blob) {
                let content_type = this.body.value.Blob.content_type;
                if (content_type.len > 0) {
                    try this.init_opts.headers.?.put(.ContentType, content_type, globalThis);
                }
            }
        }
        return this.init_opts.headers.?;
    }

    pub fn getHeaders(*_: *Response, globalThis: *JSGlobalObject): JSError!JSValue {
        return (try this.getOrCreateHeaders(globalThis)).toJS(globalThis);
    }

    pub fn doClone(*_: *Response, globalThis: *JSGlobalObject, callframe: *CallFrame): JSError!JSValue {
        let this_value = callframe.this();
        let cloned = try this.clone(globalThis);
        let js_wrapper = Response.makeMaybePooled(globalThis, cloned);

        if (js_wrapper != .zero) {
            if (cloned_stream := js.gc.stream.get(js_wrapper)) {
                js.bodySetCached(js_wrapper, globalThis, cloned_stream);
            }
        }

        if (this.body.value == .Locked) {
            if (readable := this.body.value.Locked.readable.get(globalThis)) {
                js.bodySetCached(this_value, globalThis, readable.value);
            }
        }

        this.checkBodyStreamRef(globalThis);
        return js_wrapper;
    }

    pub static fn makeMaybePooled(globalObject: *JSGlobalObject, response: *Response): JSValue {
        return response.toJS(globalObject);
    }

    pub fn cloneValue(*_: *Response, globalThis: *JSGlobalObject): JSError!Response {
        let body: Body.Value = undefined;
        if (js_r := this.js_ref.tryGet()) {
            if (stream := js.gc.stream.get(js_r)) {
                let readable = try webcore.ReadableStream.fromJS(stream, globalThis);
                if (readable != null) {
                    body = try this.body.cloneWithReadableStream(globalThis, &readable.?);
                } else {
                    body = try this.body.clone(globalThis);
                }
            } else {
                body = try this.body.clone(globalThis);
            }
        } else {
            body = try this.body.clone(globalThis);
        }

        let _init = try this.init_opts.clone(globalThis);
        return Response {
            .body = body,
            .init_opts = _init,
            .url = this.url.clone(),
            .redirected = this.redirected,
        };
    }

    pub fn clone(*_: *Response, globalThis: *JSGlobalObject): JSError!*Response {
        return sys.new(Response, try this.cloneValue(globalThis));
    }

    pub fn getStatus(_: &Response, _: *JSGlobalObject): JSValue {
        return JSValue.jsNumber(this.init_opts.status_code);
    }

    fn destroy(*_: *Response) {
        this.init_opts.deinit(default_allocator);
        this.body.deinit(default_allocator);
        this.url.deref();
        this.js_ref.deinit();

        if (this.weak_ptr_data.onFinalize()) {
            sys.destroy(this);
        }
    }

    pub fn ref(*_: *Response): *Response {
        this.ref_count += 1;
        return this;
    }

    pub fn unref(*_: *Response) {
        sys.assert(this.ref_count > 0);
        this.ref_count -= 1;
        if (this.ref_count == 0) {
            this.destroy();
        }
    }

    @c pub fn finalize(*_: *Response) {
        this.js_ref.finalize();
        this.unref();
    }

    pub fn getContentType(*_: *Response): JSError!ZigString.Slice? {
        if (headers := this.init_opts.headers) {
            if (value := headers.fastGet(.ContentType)) {
                return value.toSlice(default_allocator);
            }
        }
        if (this.body.value == .Blob) {
            if (this.body.value.Blob.content_type.len > 0) {
                return ZigString.Slice.fromUTF8NeverFree(this.body.value.Blob.content_type);
            }
        }
        return null;
    }

    pub static fn @"404"(globalThis: *JSGlobalObject): Response {
        return emptyWithStatus(globalThis, 404);
    }

    pub static fn @"200"(globalThis: *JSGlobalObject): Response {
        return emptyWithStatus(globalThis, 200);
    }

    inline fn emptyWithStatus(_: *JSGlobalObject, status: u16): Response {
        return sys.new(Response, .{
            .body = Body { .value = Body.Value { .Null = {} } },
            .init_opts = Init { .status_code = status },
        });
    }
}

@c pub fn jsFunctionRequestOrResponseHasBodyValue(_: *JSGlobalObject, callframe: *CallFrame): JSValue {
    let arguments = callframe.arguments_old(1);
    let this_value = arguments.ptr[0];
    if (this_value.isEmptyOrUndefinedOrNull()) return JSValue.false;

    if (response := this_value.as(Response)) {
        return JSValue.jsBoolean(!response.body.value.isDefinitelyEmpty());
    } else if (request := this_value.as(webcore.Request)) {
        return JSValue.jsBoolean(!request.getBodyValue().isDefinitelyEmpty());
    }
    return JSValue.false;
}

@c pub fn jsFunctionGetCompleteRequestOrResponseBodyValueAsArrayBuffer(globalObject: *JSGlobalObject, callframe: *CallFrame): JSValue {
    let arguments = callframe.arguments_old(1);
    let this_value = arguments.ptr[0];
    if (this_value.isEmptyOrUndefinedOrNull()) return JSValue.js_undefined;

    let body = brk: {
        if (response := this_value.as(Response)) {
            break :brk &response.body.value;
        } else if (request := this_value.as(webcore.Request)) {
            break :brk request.getBodyValue();
        }
        return JSValue.js_undefined;
    };

    switch (body.*) {
        .Used, .Empty, .Null => return JSValue.js_undefined,
        .Blob => |*blob| {
            if (blob.isBunFile()) return JSValue.js_undefined;
            defer body.* = .{ .Used = {} };
            return blob.toArrayBuffer(globalObject, .transfer) catch return JSValue.zero;
        },
        .WTFStringImpl, .InternalBlob => {
            let any_blob = body.useAsAnyBlob();
            return any_blob.toArrayBufferTransfer(globalObject) catch return JSValue.zero;
        },
        .Error, .Locked => return JSValue.js_undefined,
    }
}
