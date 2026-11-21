import bun from "bun";
import { Method } from "../../http_types/Method.zig";

const Output = bun.Output;
const default_allocator = bun.default_allocator;
const s3 = bun.S3;
const FetchHeaders = bun.webcore.FetchHeaders;

const http = bun.http;
const MimeType = bun.http.MimeType;

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const ZigString = jsc.ZigString;
const Request = jsc.WebCore.Request;

const Blob = jsc.WebCore.Blob;
const InternalBlob = jsc.WebCore.Blob.Internal;

const Body = jsc.WebCore.Body;
const BodyMixin = jsc.WebCore.Body.Mixin;

// C++ helper functions for AsyncLocalStorage integration
extern fn Response__getAsyncLocalStorageStore(global: *JSGlobalObject, als: JSValue): JSValue;
extern fn Response__mergeAsyncLocalStorageOptions(global: *JSGlobalObject, alsStore: JSValue, initOptions: JSValue);

pub struct DeinitGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.deinit(); }
    }
}

pub struct DerefGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.deref(); }
    }
}

pub struct OptDeinitWithAllocGuard<T> {
    ptr: (*T!)? = null,
    alloc: std.mem.Allocator = undefined,
    ~init() {
        if (p := _.ptr) {
            unsafe { p.deinit(_.alloc); }
        }
    }
}

pub struct OptDestroyGuard<T> {
    ptr: (*T!)? = null,
    ~init() {
        if (p := _.ptr) {
            bun.destroy(p);
        }
    }
}

pub struct Props {}

pub struct Init {
    headers: (*FetchHeaders)? = null,
    status_code: u16,
    status_text: bun.String = bun.String.empty,
    method: Method = Method.GET,

    fn clone(ctx: *JSGlobalObject): Init ! bun.JSError {
        let that = _;
        const headers = _.headers;
        if (head := headers) {
            that.headers = head.cloneThis(ctx)?;
        }
        that.status_text = _.status_text.clone();
        return that;
    }

    static fn init(globalThis: *JSGlobalObject, response_init: jsc.JSValue): Init? ! bun.JSError {
        let result = Init{ status_code: 200 };
        let result_guard = OptDeinitWithAllocGuard<Init>{ ptr: unsafe { &result }, alloc: bun.default_allocator };

        if (!response_init.isCell()) return null;

        const js_type = response_init.jsType();
        if (!js_type.isObject()) return null;

        if (js_type == .DOMWrapper) {
            if (req := response_init.asDirect(Request)) {
                if (headers := req.getFetchHeadersUnlessEmpty()) {
                    result.headers = headers.cloneThis(globalThis)?;
                }
                result.method = req.method;
                result_guard.ptr = null;
                return result;
            }

            if (resp := response_init.asDirect(Response)) {
                return resp.#init.clone(globalThis)?;
            }
        }

        if (globalThis.hasException()) return error.JSError;

        if (headers := response_init.fastGet(globalThis, .headers)?) {
            if (orig := headers.as(FetchHeaders)) {
                if (!orig.isEmpty()) {
                    result.headers = orig.cloneThis(globalThis)?;
                }
            } else {
                result.headers = FetchHeaders.createFromJS(globalThis, headers)?;
            }
        }

        if (globalThis.hasException()) return error.JSError;

        if (status_value := response_init.fastGet(globalThis, .status)?) {
            const number = status_value.coerceToInt64(globalThis)?;
            if ((200 <= number && number < 600) || number == 101) {
                result.status_code = @as(u16, @truncate(@as(u32, @intCast(number))));
            } else {
                if (!globalThis.hasException()) {
                    const e_val = globalThis.createRangeErrorInstance("The status provided ({d}) must be 101 or in the range of [200, 599]", .{number});
                    return globalThis.throwValue(e_val);
                }
                return error.JSError;
            }
        }

        if (globalThis.hasException()) return error.JSError;

        if (status_text := response_init.getTruthy(globalThis, "statusText")?) {
            result.status_text = bun.String.fromJS(status_text, globalThis)?;
        }

        if (method_value := response_init.getTruthy(globalThis, "method")?) {
            if (m := Method.fromJS(globalThis, method_value)?) {
                result.method = m;
            }
        }

        result_guard.ptr = null;
        return result;
    }

    fn deinit(*_, _: std.mem.Allocator) {
        if (headers := _.headers) {
            _.headers = null;
            headers.deref();
        }
        _.status_text.deref();
        _.status_text = bun.String.empty;
    }
}

@callconv(jsc.conv)
@export pub fn jsFunctionRequestOrResponseHasBodyValue(_: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): jsc.JSValue {
    const arguments = callframe.arguments_old(1);
    const this_value = unsafe { arguments.ptr[0] };
    if (this_value.isEmptyOrUndefinedOrNull()) {
        return .false;
    }

    if (response := this_value.as(Response)) {
        return jsc.JSValue.jsBoolean(!response.#body.value.isDefinitelyEmpty());
    } else if (request := this_value.as(Request)) {
        return jsc.JSValue.jsBoolean(!request.getBodyValue().isDefinitelyEmpty());
    }

    return .false;
}

@callconv(jsc.conv)
@export pub fn jsFunctionGetCompleteRequestOrResponseBodyValueAsArrayBuffer(globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): jsc.JSValue {
    const arguments = callframe.arguments_old(1);
    const this_value = unsafe { arguments.ptr[0] };
    if (this_value.isEmptyOrUndefinedOrNull()) {
        return .js_undefined;
    }

    let body_opt: (*Body.Value)? = null;
    if (response := this_value.as(Response)) {
        body_opt = &response.#body.value;
    } else if (request := this_value.as(Request)) {
        body_opt = request.getBodyValue();
    } else {
        return .js_undefined;
    }
    const body = body_opt.?;

    // Get the body if it's available synchronously.
    switch (body.*) {
        .Used, .Empty, .Null => return .js_undefined,
        .Blob => {
            const blob = &@field(body, "Blob");
            if (blob.isBunFile()) {
                return .js_undefined;
            }
            defer body.* = .{ .Used = undefined };
            return blob.toArrayBufferView(globalObject, .transfer, .ArrayBuffer) catch return .zero;
        },
        .WTFStringImpl, .InternalBlob => {
            let any_blob = body.useAsAnyBlob();
            return any_blob.toArrayBufferTransfer(globalObject) catch return .zero;
        },
        .Error, .Locked => return .js_undefined,
    }
}

@root struct Response {
    #body: Body,
    #init: Init,
    #url: bun.String = bun.String.empty,
    #redirected: bool = false,
    ref_count: u32 = 1,
    weak_ptr_data: WeakRef.Data = .empty,
    #js_ref: jsc.JSRef = .empty(),
    #reported_estimated_size: usize = 0,

    const ResponseMixin = BodyMixin(Response);
    pub const js = jsc.Codegen.JSResponse;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;

    pub const WeakRef = bun.ptr.WeakPtr(Response, "weak_ptr_data");

    pub const getText = ResponseMixin.getText;
    pub const getBody = ResponseMixin.getBody;
    pub const getBytes = ResponseMixin.getBytes;
    pub const getBodyUsed = ResponseMixin.getBodyUsed;
    pub const getJSON = ResponseMixin.getJSON;
    pub const getArrayBuffer = ResponseMixin.getArrayBuffer;
    pub const getBlob = ResponseMixin.getBlob;
    pub const getBlobWithoutCallFrame = ResponseMixin.getBlobWithoutCallFrame;
    pub const getFormData = ResponseMixin.getFormData;

    static fn init(response_init: Init, body: Body, url: bun.String, redirected: bool): Response {
        return Response{
            #init: response_init,
            #body: body,
            #url: url,
            #redirected: redirected,
        };
    }

    fn setInit(*_, method: Method, status_code: u16, status_text: bun.String) {
        _.#init.method = method;
        _.#init.status_code = status_code;
        _.#init.status_text.deref();
        _.#init.status_text = status_text;
    }

    fn setInitHeaders(*_, headers: (*FetchHeaders)?) {
        if (_headers := _.#init.headers) {
            _headers.deref();
        }
        _.#init.headers = headers;
    }

    fn getInitStatusCode(*_): u16 {
        return _.#init.status_code;
    }

    fn getInitStatusText(*_): bun.String {
        return _.#init.status_text;
    }

    fn setUrl(*_, url: bun.String) {
        _.#url.deref();
        _.#url = url;
    }

    fn getUTF8Url(*_, allocator: std.mem.Allocator): ZigString.Slice {
        return _.#url.toUTF8(allocator);
    }

    fn getUrl(*_): bun.String {
        return _.#url;
    }

    fn getInitHeaders(*_): (*FetchHeaders)? {
        return _.#init.headers;
    }

    fn swapInitHeaders(*_): (*FetchHeaders)? {
        if (headers := _.#init.headers) {
            _.#init.headers = null;
            return headers;
        }
        return null;
    }

    fn getBodyLen(*_): usize {
        return _.#body.len();
    }

    fn getMethod(*_): Method {
        return _.#init.method;
    }

    fn getFormDataEncoding(*_): (*bun.FormData.AsyncFormData)? ! bun.JSError {
        const content_type_opt = _.getContentType()?;
        if (content_type_slice := content_type_opt) {
            let ct = content_type_slice;
            const ct_guard = DeinitGuard<ZigString.Slice>{ ptr: unsafe { &ct } };
            if (encoding := bun.FormData.Encoding.get(ct.slice())) {
                return bun.handleOom(bun.FormData.AsyncFormData.init(bun.default_allocator, encoding));
            }
            return null;
        }
        return null;
    }

    @c
    fn estimatedSize(*_): usize {
        return _.#reported_estimated_size;
    }

    fn calculateEstimatedByteSize(*_) {
        _.#reported_estimated_size = _.#body.value.estimatedSize() +
            _.#url.byteSlice().len +
            _.#init.status_text.byteSlice().len +
            @sizeOf(Response);
    }

    fn checkBodyStreamRef(*_, globalObject: *JSGlobalObject) {
        if (js_value := _.#js_ref.tryGet()) {
            if (_.#body.value == .Locked) {
                if (stream := _.#body.value.Locked.readable.get(globalObject)) {
                    stream.value.ensureStillAlive();
                    js.gc.stream.set(js_value, globalObject, stream.value);
                    _.#body.value.Locked.readable.deinit();
                    _.#body.value.Locked.readable = .{};
                }
            }
        }
    }

    fn toJS(*_, globalObject: *JSGlobalObject): JSValue {
        _.calculateEstimatedByteSize();
        const js_value = js.toJSUnchecked(globalObject, _);
        _.#js_ref = .initWeak(js_value);
        _.checkBodyStreamRef(globalObject);
        return js_value;
    }

    fn getBodyValue(*_): *Body.Value {
        return &_.#body.value;
    }

    fn getBodyReadableStream(*_, globalObject: *JSGlobalObject): jsc.WebCore.ReadableStream? {
        if (js_r := _.#js_ref.tryGet()) {
            if (stream := js.gc.stream.get(js_r)) {
                return jsc.WebCore.ReadableStream.fromJS(stream, globalObject) catch |e| {
                    * = globalObject.takeException(e);
                    return null;
                };
            }
        }
        if (_.#body.value == .Locked) {
            return _.#body.value.Locked.readable.get(globalObject);
        }
        return null;
    }

    fn detachReadableStream(*_, globalObject: *jsc.JSGlobalObject) {
        if (js_r := _.#js_ref.tryGet()) {
            js.gc.stream.clear(js_r, globalObject);
        }
        if (_.#body.value == .Locked) {
            let old = _.#body.value.Locked.readable;
            old.deinit();
            _.#body.value.Locked.readable = .{};
        }
    }

    fn setSizeHint(*_, size_hint: Blob.SizeType) {
        if (_.#body.value == .Locked) {
            _.#body.value.Locked.size_hint = size_hint;
            if (readable := _.#body.value.Locked.readable.get(_.#body.value.Locked.global)) {
                if (readable.ptr == .Bytes) {
                    readable.ptr.Bytes.size_hint = size_hint;
                }
            }
        }
    }

    fn getFetchHeaders(*_): (*FetchHeaders)? {
        return _.#init.headers;
    }

    fn statusCode(): u16 {
        return _.#init.status_code;
    }

    fn redirectLocation(): ([]const u8)? {
        return _.header(.Location);
    }

    fn header(name: bun.webcore.FetchHeaders.HTTPHeaderName): ([]const u8)? {
        if (headers := _.#init.headers) {
            if (str := headers.fastGet(name)) {
                return str.slice();
            }
        }
        return null;
    }

    const RespVoid = void;
    fn writeFormat(*_, comptime Formatter: type, formatter: *Formatter, writer: auto, comptime enable_ansi_colors: bool): RespVoid ! anyerror {
        const Writer = @TypeOf(writer);
        writer.print("Response ({f}) {{\n", .{bun.fmt.size(_.#body.len(), .{})})?;

        {
            formatter.indent += 1;
            defer {
                if (formatter.indent > 0) formatter.indent -= 1;
            }

            formatter.writeIndent(Writer, writer)?;
            writer.writeAll(comptime Output.prettyFmt("<r>ok<d>:<r> ", enable_ansi_colors))?;
            formatter.printAs(.Boolean, Writer, writer, jsc.JSValue.jsBoolean(_.isOK()), .BooleanObject, enable_ansi_colors)?;
            formatter.printComma(Writer, writer, enable_ansi_colors)?;
            writer.writeAll("\n")?;

            formatter.writeIndent(Writer, writer)?;
            writer.writeAll(comptime Output.prettyFmt("<r>url<d>:<r> \"", enable_ansi_colors))?;
            writer.print(comptime Output.prettyFmt("<r><b>{f}<r>", enable_ansi_colors), .{_.#url})?;
            writer.writeAll("\"")?;
            formatter.printComma(Writer, writer, enable_ansi_colors)?;
            writer.writeAll("\n")?;

            formatter.writeIndent(Writer, writer)?;
            writer.writeAll(comptime Output.prettyFmt("<r>status<d>:<r> ", enable_ansi_colors))?;
            formatter.printAs(.Double, Writer, writer, jsc.JSValue.jsNumber(_.#init.status_code), .NumberObject, enable_ansi_colors)?;
            formatter.printComma(Writer, writer, enable_ansi_colors)?;
            writer.writeAll("\n")?;

            formatter.writeIndent(Writer, writer)?;
            writer.writeAll(comptime Output.prettyFmt("<r>statusText<d>:<r> ", enable_ansi_colors))?;
            writer.print(comptime Output.prettyFmt("<r>\"<b>{f}<r>\"", enable_ansi_colors), .{_.#init.status_text})?;
            formatter.printComma(Writer, writer, enable_ansi_colors)?;
            writer.writeAll("\n")?;

            formatter.writeIndent(Writer, writer)?;
            writer.writeAll(comptime Output.prettyFmt("<r>headers<d>:<r> ", enable_ansi_colors))?;
            formatter.printAs(.Private, Writer, writer, _.getHeaders(formatter.globalThis)?, .DOMWrapper, enable_ansi_colors)?;
            formatter.printComma(Writer, writer, enable_ansi_colors)?;
            writer.writeAll("\n")?;

            formatter.writeIndent(Writer, writer)?;
            writer.writeAll(comptime Output.prettyFmt("<r>redirected<d>:<r> ", enable_ansi_colors))?;
            formatter.printAs(.Boolean, Writer, writer, jsc.JSValue.jsBoolean(_.#redirected), .BooleanObject, enable_ansi_colors)?;
            formatter.printComma(Writer, writer, enable_ansi_colors)?;
            writer.writeAll("\n")?;

            formatter.resetLine();
            _.#body.writeFormat(Formatter, formatter, writer, enable_ansi_colors)?;
        }
        writer.writeAll("\n")?;
        formatter.writeIndent(Writer, writer)?;
        writer.writeAll("}")?;
        formatter.resetLine();
    }

    fn isOK(): bool {
        return _.#init.status_code >= 200 && _.#init.status_code <= 299;
    }

    fn getURL(*_, globalThis: *jsc.JSGlobalObject): jsc.JSValue ! bun.JSError {
        return _.#url.toJS(globalThis);
    }

    fn getResponseType(*_, globalThis: *jsc.JSGlobalObject): jsc.JSValue ! bun.JSError {
        if (_.#init.status_code < 200) {
            return globalThis.commonStrings().@"error"();
        }
        return globalThis.commonStrings().default();
    }

    fn getStatusText(*_, globalThis: *jsc.JSGlobalObject): jsc.JSValue ! bun.JSError {
        return _.#init.status_text.toJS(globalThis);
    }

    fn getRedirected(*_, _: *jsc.JSGlobalObject): jsc.JSValue {
        return JSValue.jsBoolean(_.#redirected);
    }

    fn getOK(*_, _: *jsc.JSGlobalObject): jsc.JSValue {
        return JSValue.jsBoolean(_.isOK());
    }

    fn getOrCreateHeaders(*_, globalThis: *jsc.JSGlobalObject): *FetchHeaders ! bun.JSError {
        if (_.#init.headers == null) {
            _.#init.headers = FetchHeaders.createEmpty();

            if (_.#body.value == .Blob) {
                const content_type = _.#body.value.Blob.content_type;
                if (content_type.len > 0) {
                    _.#init.headers.?.put(.ContentType, content_type, globalThis)?;
                }
            }
        }
        return _.#init.headers.?;
    }

    fn getHeaders(*_, globalThis: *jsc.JSGlobalObject): jsc.JSValue ! bun.JSError {
        const hdrs = _.getOrCreateHeaders(globalThis)?;
        return hdrs.toJS(globalThis);
    }

    fn doClone(*_, globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): JSValue ! bun.JSError {
        const this_value = callframe.this();
        const cloned = _.clone(globalThis)?;
        const js_wrapper = Response.makeMaybePooled(globalThis, cloned);

        if (js_wrapper != .zero) {
            if (cloned_stream := js.gc.stream.get(js_wrapper)) {
                js.bodySetCached(js_wrapper, globalThis, cloned_stream);
            }
        }

        if (_.#body.value == .Locked) {
            if (readable := _.#body.value.Locked.readable.get(globalThis)) {
                js.bodySetCached(this_value, globalThis, readable.value);
            }
        }

        _.checkBodyStreamRef(globalThis);
        return js_wrapper;
    }

    static fn makeMaybePooled(globalObject: *jsc.JSGlobalObject, resp_ptr: *Response): JSValue {
        return resp_ptr.toJS(globalObject);
    }

    fn cloneValue(*_, globalThis: *JSGlobalObject): Response ! bun.JSError {
        let body: Body = undefined;
        let cloned_from_stream = false;
        if (js_r := _.#js_ref.tryGet()) {
            if (stream := js.gc.stream.get(js_r)) {
                let readable = jsc.WebCore.ReadableStream.fromJS(stream, globalThis)?;
                if (readable != null) {
                    body = _.#body.cloneWithReadableStream(globalThis, &readable.?)?;
                    cloned_from_stream = true;
                }
            }
        }
        if (!cloned_from_stream) {
            body = _.#body.clone(globalThis)?;
        }
        let body_guard = OptDeinitWithAllocGuard<Body>{ ptr: unsafe { &body }, alloc: bun.default_allocator };

        let _init = _.#init.clone(globalThis)?;
        let init_guard = OptDeinitWithAllocGuard<Init>{ ptr: unsafe { &_init }, alloc: bun.default_allocator };
        body_guard.ptr = null;
        init_guard.ptr = null;
        return Response{
            #body: body,
            #init: _init,
            #url: _.#url.clone(),
            #redirected: _.#redirected,
        };
    }

    fn clone(*_, globalThis: *JSGlobalObject): *Response ! bun.JSError {
        return bun.new(Response, _.cloneValue(globalThis)?);
    }

    fn getStatus(*_, _: *jsc.JSGlobalObject): jsc.JSValue {
        return JSValue.jsNumber(_.#init.status_code);
    }

    fn destroy(*_) {
        _.#init.deinit(bun.default_allocator);
        _.#body.deinit(bun.default_allocator);
        _.#url.deref();
        _.#js_ref.deinit();

        if (_.weak_ptr_data.onFinalize()) {
            bun.destroy(_);
        }
    }

    fn ref(*_): *Response {
        _.ref_count += 1;
        return _;
    }

    fn unref(*_) {
        bun.assert(_.ref_count > 0);
        _.ref_count -= 1;
        if (_.ref_count == 0) {
            _.destroy();
        }
    }

    @c
    fn finalize(*_) {
        _.#js_ref.finalize();
        _.unref();
    }

    fn getContentType(*_): ZigString.Slice? ! bun.JSError {
        if (headers := _.#init.headers) {
            if (value := headers.fastGet(.ContentType)) {
                return value.toSlice(bun.default_allocator);
            }
        }

        if (_.#body.value == .Blob) {
            if (_.#body.value.Blob.content_type.len > 0) {
                return ZigString.Slice.fromUTF8NeverFree(_.#body.value.Blob.content_type);
            }
        }

        return null;
    }

    static fn constructJSON(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): JSValue ! bun.JSError {
        const args_list = callframe.arguments_old(2);
        let args = jsc.CallFrame.ArgumentsSlice.init(globalThis.bunVM(), unsafe { args_list.ptr[0..args_list.len] });

        let response = Response{
            #body: Body{
                value: .Empty,
            },
            #init: Init{
                status_code: 200,
            },
            #url: bun.String.empty,
        };
        let body_guard = OptDeinitWithAllocGuard<Body>{ ptr: unsafe { &response.#body }, alloc: bun.default_allocator };
        let init_guard = OptDeinitWithAllocGuard<Init>{ ptr: unsafe { &response.#init }, alloc: bun.default_allocator };
        const json_value = args.nextEat() ?? jsc.JSValue.zero;

        if (@intFromEnum(json_value) != 0) {
            if (json_value.isUndefined() || json_value.isSymbol() || json_value.jsType() == .JSFunction) {
                const e_val = globalThis.createTypeErrorInstance("Value is not JSON serializable", .{});
                return globalThis.throwValue(e_val);
            }

            if (json_value.isBigInt()) {
                const e_val = globalThis.createTypeErrorInstance("Do not know how to serialize a BigInt", .{});
                return globalThis.throwValue(e_val);
            }

            let str = bun.String.empty;
            json_value.jsonStringifyFast(globalThis, &str)?;

            if (globalThis.hasException()) {
                str.deref();
                return JSValue.zero;
            }

            if (!str.isEmpty()) {
                if (bytes := str.value.WTFStringImpl.toUTF8IfNeeded(bun.default_allocator)) {
                    const str_guard = DerefGuard<bun.String>{ ptr: unsafe { &str } };
                    response.#body.value = .{
                        .InternalBlob = InternalBlob{
                            bytes: std.array_list.Managed(u8).fromOwnedSlice(bun.default_allocator, unsafe { @constCast(bytes.slice()) }),
                            was_string: true,
                        },
                    };
                } else {
                    response.#body.value = Body.Value{
                        .WTFStringImpl = str.value.WTFStringImpl,
                    };
                }
            }
        }

        if (arg_init := args.nextEat()) {
            if (arg_init.isUndefinedOrNull()) {
            } else if (arg_init.isNumber()) {
                response.#init.status_code = @as(u16, @intCast(@min(@max(0, arg_init.toInt32()), std.math.maxInt(u16))));
            } else {
                const maybe_init = Init.init(globalThis, arg_init) catch |e| blk: {
                    if (e == error.JSError) return JSValue.zero;
                    break :blk @as(Init?, null);
                };
                if (_init := maybe_init) {
                    response.#init = _init;
                }
            }
        }

        let headers_ref = response.getOrCreateHeaders(globalThis)?;
        headers_ref.putDefault(.ContentType, MimeType.json.value, globalThis)?;
        body_guard.ptr = null;
        init_guard.ptr = null;
        return bun.new(Response, response).toJS(globalThis);
    }

    static fn validateRedirectStatusCode(globalThis: *jsc.JSGlobalObject, status_code: i32): u16 ! bun.JSError {
        switch (status_code) {
            301, 302, 303, 307, 308 => return @intCast(status_code),
            else => {
                const e_val = globalThis.createRangeErrorInstance("Failed to execute 'redirect' on 'Response': Invalid status code", .{});
                return globalThis.throwValue(e_val);
            },
        }
    }

    static fn constructRedirect(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): JSValue ! bun.JSError {
        const response = constructRedirectImpl(globalThis, callframe)?;
        const resp_ptr = bun.new(Response, response);
        const response_js = resp_ptr.toJS(globalThis);
        return response_js;
    }

    static fn constructRedirectImpl(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): Response ! bun.JSError {
        let args_list = callframe.arguments_old(4);
        let args = jsc.CallFrame.ArgumentsSlice.init(globalThis.bunVM(), unsafe { args_list.ptr[0..args_list.len] });

        let url_string_slice = ZigString.Slice.empty;
        const url_slice_guard = DeinitGuard<ZigString.Slice>{ ptr: unsafe { &url_string_slice } };

        let response = Response{
            #init: Init{
                status_code: 302,
            },
            #body: Body{
                value: .Empty,
            },
            #url: bun.String.empty,
        };

        const url_string_value = args.nextEat() ?? jsc.JSValue.zero;
        let url_string = ZigString.init("");

        if (@intFromEnum(url_string_value) != 0) {
            url_string = url_string_value.getZigString(globalThis)?;
        }
        url_string_slice = url_string.toSlice(bun.default_allocator);
        let body_guard = OptDeinitWithAllocGuard<Body>{ ptr: unsafe { &response.#body }, alloc: bun.default_allocator };
        let init_guard = OptDeinitWithAllocGuard<Init>{ ptr: unsafe { &response.#init }, alloc: bun.default_allocator };

        if (arg_init := args.nextEat()) {
            if (arg_init.isUndefinedOrNull()) {
            } else if (arg_init.isNumber()) {
                response.#init.status_code = validateRedirectStatusCode(globalThis, arg_init.toInt32())?;
            } else if (_init := Init.init(globalThis, arg_init)?) {
                response.#init = _init;

                if (_init.status_code != 200) {
                    response.#init.status_code = validateRedirectStatusCode(globalThis, _init.status_code)?;
                }
            }
        }

        if (globalThis.hasException()) {
            return error.JSError;
        }

        response.#init.headers = response.getOrCreateHeaders(globalThis)?;
        let headers_ref = response.#init.headers.?;
        headers_ref.put(.Location, url_string_slice.slice(), globalThis)?;
        body_guard.ptr = null;
        init_guard.ptr = null;
        return response;
    }

    static fn constructError(globalThis: *jsc.JSGlobalObject, _: *jsc.CallFrame): JSValue ! bun.JSError {
        const response = bun.new(
            Response,
            Response{
                #init: Init{
                    status_code: 0,
                },
                #body: Body{
                    value: .Empty,
                },
            },
        );

        const js_value = response.toJS(globalThis);
        response.#js_ref = .initWeak(js_value);
        return js_value;
    }

    static fn constructor(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame, js_this: jsc.JSValue): *Response ! bun.JSError {
        let arguments = callframe.argumentsAsArray(2);

        if (!arguments[0].isUndefinedOrNull() && arguments[0].isObject()) {
            if (blob := arguments[0].as(Blob)) {
                if (blob.isS3()) {
                    if (!arguments[1].isEmptyOrUndefinedOrNull()) {
                        return globalThis.throwInvalidArguments("new Response(s3File) do not support ResponseInit options", .{});
                    }
                    let response = Response{
                        #init: Init{
                            status_code: 302,
                        },
                        #body: Body{
                            value: .Empty,
                        },
                        #url: bun.String.empty,
                        #js_ref: .initWeak(js_this),
                    };
                    let s3_body_guard = OptDeinitWithAllocGuard<Body>{ ptr: unsafe { &response.#body }, alloc: bun.default_allocator };
                    let s3_init_guard = OptDeinitWithAllocGuard<Init>{ ptr: unsafe { &response.#init }, alloc: bun.default_allocator };

                    const credentials = blob.store.?.data.s3.getCredentials();

                    let result = credentials.signRequest(.{
                        .path = blob.store.?.data.s3.path(),
                        .method = .GET,
                    }, false, .{ .expires = 15 * 60 }) catch |sign_err| {
                        return s3.throwSignError(sign_err, globalThis);
                    };
                    const result_guard = DeinitGuard<@TypeOf(result)>{ ptr: unsafe { &result } };
                    response.#init.headers = response.getOrCreateHeaders(globalThis)?;
                    response.#redirected = true;
                    let headers_ref = response.#init.headers.?;
                    headers_ref.put(.Location, result.url, globalThis)?;
                    s3_body_guard.ptr = null;
                    s3_init_guard.ptr = null;
                    return bun.new(Response, response);
                }
            }
        }

        let _init: Init = undefined;
        if (arguments[1].isUndefinedOrNull()) {
            _init = Init{
                status_code: 200,
                headers: null,
            };
        } else if (arguments[1].isObject()) {
            if (v := Init.init(globalThis, arguments[1])?) {
                _init = v;
            } else unreachable;
        } else {
            if (!globalThis.hasException()) {
                return globalThis.throwInvalidArguments("Failed to construct 'Response': The provided body value is not of type 'ResponseInit'", .{});
            }
            return error.JSError;
        }
        let init_guard = OptDeinitWithAllocGuard<Init>{ ptr: unsafe { &_init }, alloc: bun.default_allocator };

        if (globalThis.hasException()) {
            return error.JSError;
        }

        let body: Body = undefined;
        if (arguments[0].isUndefinedOrNull()) {
            body = Body{
                value: Body.Value.Null,
            };
        } else {
            body = Body.extract(globalThis, arguments[0])?;
        }
        let body_guard = OptDeinitWithAllocGuard<Body>{ ptr: unsafe { &body }, alloc: bun.default_allocator };

        if (globalThis.hasException()) {
            return error.JSError;
        }

        let response = bun.new(Response, Response{
            #body: body,
            #init: _init,
            #js_ref: .initWeak(js_this),
        });
        let resp_guard = OptDestroyGuard<Response>{ ptr: unsafe { @ptrCast(response) } };

        if (response.#body.value == .Blob &&
            response.#init.headers != null &&
            response.#body.value.Blob.content_type.len > 0 &&
            !response.#init.headers.?.fastHas(.ContentType))
        {
            response.#init.headers.?.put(.ContentType, response.#body.value.Blob.content_type, globalThis)?;
        }

        response.calculateEstimatedByteSize();
        response.checkBodyStreamRef(globalThis);
        resp_guard.ptr = null;
        body_guard.ptr = null;
        init_guard.ptr = null;
        return response;
    }

    static fn @"404"(globalThis: *jsc.JSGlobalObject): Response {
        return emptyWithStatus(globalThis, 404);
    }

    static fn @"200"(globalThis: *jsc.JSGlobalObject): Response {
        return emptyWithStatus(globalThis, 200);
    }

    static fn emptyWithStatus(_: *jsc.JSGlobalObject, status: u16): Response {
        return bun.new(Response, .{
            .#body = Body{
                value: Body.Value{ .Null = {} },
            },
            .init = Init{
                status_code: status,
            },
        });
    }
}
