//! https://developer.mozilla.org/en-US/docs/Web/API/Request
import Environment from "../../bun_core/env.zig";
import bun from "bun";
import { FetchCacheMode } from "../../http_types/FetchCacheMode.zig";
import { FetchRedirect } from "../../http_types/FetchRedirect.zig";
import { FetchRequestMode } from "../../http_types/FetchRequestMode.zig";
import { Method } from "../../http_types/Method.zig";

type string = []const u8;

const Output = bun.Output;
const default_allocator = bun.default_allocator;
const strings = bun.strings;
const uws = bun.uws;
const FetchHeaders = bun.webcore.FetchHeaders;
const MimeType = bun.http.MimeType;

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const ZigString = jsc.ZigString;

const AbortSignal = jsc.WebCore.AbortSignal;
const Response = jsc.WebCore.Response;

const Blob = jsc.WebCore.Blob;
const InlineBlob = jsc.WebCore.Blob.Inline;
const InternalBlob = jsc.WebCore.Blob.Internal;

const Body = jsc.WebCore.Body;
const BodyMixin = jsc.WebCore.Body.Mixin;

struct DeinitGuard<T> {
    val: T?,
    ~init() {
        if (_.val != null) {
            _.val.?.deinit();
        }
    }
}

struct DerefGuard<T> {
    val: T?,
    ~init() {
        if (_.val != null) {
            _.val.?.deref();
        }
    }
}

struct FreeGuard<T> {
    alloc: std.mem.Allocator,
    slice: T,
    ~init() {
        _.alloc.free(_.slice);
    }
}

struct DestroyGuard<T> {
    ptr: (*T!)?,
    ~init() {
        if (_.ptr != null) {
            bun.destroy(_.ptr.?);
        }
    }
}

@packed(u8)
pub struct Flags {
    redirect: FetchRedirect = .follow,
    cache: FetchCacheMode = .default,
    mode: FetchRequestMode = .cors,
    https: bool = false,
}

pub struct InternalJSEventCallback {
    function: jsc.Strong.Optional = .empty,

    pub const EventType = jsc.API.NodeHTTPResponse.AbortEvent;

    static fn init(function: jsc.JSValue, globalThis: *jsc.JSGlobalObject): InternalJSEventCallback {
        return InternalJSEventCallback{
            function: .create(function, globalThis),
        };
    }

    fn hasCallback(*_): bool {
        return _.function.has();
    }

    fn trigger(*_, eventType: EventType, globalThis: *jsc.JSGlobalObject): bool {
        if (callback := _.function.get()) {
            * = callback.call(globalThis, .js_undefined, &.{jsc.JSValue.jsNumber(
                @intFromEnum(eventType),
            )}) catch e => globalThis.reportActiveExceptionAsUnhandled(e);
            return true;
        }
        return false;
    }

    fn deinit(*_) {
        _.function.deinit();
    }
}

enum Fields {
    method,
    headers,
    body,
    mode,
    redirect,
    cache,
    signal,
    url,
}

@callconv(jsc.conv)
extern fn Bun__JSRequest__createForBake(globalObject: *jsc.JSGlobalObject, requestPtr: *Request): jsc.JSValue;

extern fn Bun__getParamsIfBunRequest(this_value: JSValue): JSValue;

@root struct Request {
url: bun.String = bun.String.empty,

#headers: (*FetchHeaders)? = null,
signal: (*AbortSignal)? = null,
#body: *Body.Value.HiveRef,
#js_ref: jsc.JSRef = .empty(),
method: Method = Method.GET,
flags: Flags = {},
request_context: jsc.API.AnyRequestContext = jsc.API.AnyRequestContext.Null,
weak_ptr_data: WeakRef.Data = .empty,
// We must report a consistent value for this
reported_estimated_size: usize = 0,
internal_event_callback: InternalJSEventCallback = {},

pub const js = jsc.Codegen.JSRequest;
// NOTE: toJS is overridden
pub const fromJS = js.fromJS;
pub const fromJSDirect = js.fromJSDirect;

pub const new = bun.TrivialNew(@This());

const RequestMixin = BodyMixin(@This());
pub const getText = RequestMixin.getText;
pub const getBytes = RequestMixin.getBytes;
pub const getBody = RequestMixin.getBody;
pub const getBodyUsed = RequestMixin.getBodyUsed;
pub const getJSON = RequestMixin.getJSON;
pub const getArrayBuffer = RequestMixin.getArrayBuffer;
pub const getBlob = RequestMixin.getBlob;
pub const getFormData = RequestMixin.getFormData;
pub const getBlobWithoutCallFrame = RequestMixin.getBlobWithoutCallFrame;
pub const WeakRef = bun.ptr.WeakPtr(Request, "weak_ptr_data");

pub static fn memoryCost(client: *const Request): usize {
    return @sizeOf(Request) + client.request_context.memoryCost() + client.url.byteSlice().len + client.#body.value.memoryCost();
}

@export @c static fn Request__setCookiesOnRequestContext(client: *Request, cookieMap: (*jsc.WebCore.CookieMap)?) {
    client.request_context.setCookies(cookieMap);
}

@export @c static fn Request__getUWSRequest(
    client: *Request,
): (*uws.Request)? {
    return client.request_context.getRequest();
}

@export @c static fn Request__setInternalEventCallback(
    client: *Request,
    callback: jsc.JSValue,
    globalThis: *jsc.JSGlobalObject,
) {
    client.internal_event_callback = InternalJSEventCallback.init(callback, globalThis);
    // we always have the abort event but we need to enable the timeout event as well in case of `node:http`.Server.setTimeout is set
    client.request_context.enableTimeoutEvents();
}

@export @c static fn Request__setTimeout(client: *Request, seconds: jsc.JSValue, globalThis: *jsc.JSGlobalObject) {
    if (!seconds.isNumber()) {
        if (globalThis.throw("Failed to set timeout: The provided value is not of type 'number'.", {}) == error.JSError) {}
        return;
    }

    client.setTimeout(seconds.to(c_uint));
}

@export @c static fn Request__clone(client: *Request, globalThis: *jsc.JSGlobalObject): (*Request)? {
    return client.clone(bun.default_allocator, globalThis) catch return null;
}

const _keep_exports = .{
    Request__clone,
    Request__getUWSRequest,
    Request__setInternalEventCallback,
    Request__setTimeout,
};

pub static fn init(
    method: Method,
    request_context: jsc.API.AnyRequestContext,
    https: bool,
    signal: (*AbortSignal)?,
    body: *Body.Value.HiveRef,
): Request {
    return Request{
        request_context: request_context,
        method: method,
        flags: { https: https },
        signal: signal,
        .#body = body,
    };
}

/// TODO: do we need this?
pub static fn init2(
    url: bun.String,
    headers: (*FetchHeaders)?,
    body: *Body.Value.HiveRef,
    method: Method,
): Request {
    return Request{
        url: url,
        .#headers = headers,
        .#body = body,
        method: method,
    };
}

pub fn getContentType(
    *_,
): bun.JSError!ZigString.Slice? {
    if (req := _.request_context.getRequest()) {
        if (value := req.header("content-type")) {
            return ZigString.Slice.fromUTF8NeverFree(value);
        }
    }

    if (headers := _.#headers) {
        if (value := headers.fastGet(.ContentType)) {
            return value.toSlice(bun.default_allocator);
        }
    }

    if (_.#body.value == .Blob) {
        if (_.#body.value.Blob.content_type.len > 0)
            return ZigString.Slice.fromUTF8NeverFree(_.#body.value.Blob.content_type);
    }

    return null;
}

pub static fn getFormDataEncoding(client: *Request): bun.JSError!(*bun.FormData.AsyncFormData)? {
    const content_type_opt = client.getContentType()?;
    let content_type_slice: ZigString.Slice = undefined;
    if (v := content_type_opt) {
        content_type_slice = v;
    } else {
        return null;
    }
    const _content_type_guard = DeinitGuard<ZigString.Slice>{ val: content_type_slice };
    let encoding: bun.FormData.Encoding = undefined;
    if (enc := bun.FormData.Encoding.get(content_type_slice.slice())) {
        encoding = enc;
    } else {
        return null;
    }
    return bun.FormData.AsyncFormData.init(bun.default_allocator, encoding);
}

@callconv(bun.jsc.conv)
pub static fn estimatedSize(client: *Request): usize {
    return client.reported_estimated_size;
}

pub static fn getRemoteSocketInfo(client: *Request, globalObject: *jsc.JSGlobalObject): jsc.JSValue? {
    if (info := client.request_context.getRemoteSocketInfo()) {
        return jsc.JSSocketAddress.create(globalObject, info.ip, info.port, info.is_ipv6);
    }

    return null;
}

pub static fn calculateEstimatedByteSize(client: *Request) {
    client.reported_estimated_size = client.#body.value.estimatedSize() + client.sizeOfURL() + @sizeOf(Request);
}

@export @c static fn Bun__JSRequest__calculateEstimatedByteSize(client: *Request) {
    client.calculateEstimatedByteSize();
}

pub inline fn getBodyReadableStream(
    *_,
    globalObject: *JSGlobalObject,
): jsc.WebCore.ReadableStream? {
    if (js_ref := _.#js_ref.tryGet()) {
        if (stream := js.gc.stream.get(js_ref)) {
            // JS is always source of truth for the stream
            return jsc.WebCore.ReadableStream.fromJS(stream, globalObject) catch e => {
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
pub static inline fn detachReadableStream(client: *Request, globalObject: *jsc.JSGlobalObject) {
    if (js_ref := client.#js_ref.tryGet()) {
        js.gc.stream.clear(js_ref, globalObject);
    }
    if (client.#body.value == .Locked) {
        let old = client.#body.value.Locked.readable;
        old.deinit();
        client.#body.value.Locked.readable = {};
    }
}

pub static fn toJS(client: *Request, globalObject: *JSGlobalObject): JSValue {
    client.calculateEstimatedByteSize();
    const js_value = js.toJSUnchecked(globalObject, client);
    client.#js_ref = .initWeak(js_value);

    client.checkBodyStreamRef(globalObject);
    return js_value;
}

pub static fn toJSForBake(client: *Request, globalObject: *JSGlobalObject): bun.JSError!JSValue {
    return bun.jsc.fromJSHostCall(
        globalObject,
        @src(),
        Bun__JSRequest__createForBake,
        .{ globalObject, client },
    );
}

pub static fn writeFormat(client: *Request, this_value: JSValue, comptime Formatter: type, formatter: *Formatter, writer: auto, comptime enable_ansi_colors: bool): anyerror!void {
    const Writer = @TypeOf(writer);

    const params_object = Bun__getParamsIfBunRequest(this_value);

    const class_label = switch (params_object) {
        .zero => "Request",
        else => "BunRequest",
    };
    writer.print("{s} ({f}) {{\n", .{ class_label, bun.fmt.size(client.#body.value.size(), {}) })?;
    {
        formatter.indent += 1;
        defer {
            formatter.indent = formatter.indent -| 1;
        }

        formatter.writeIndent(Writer, writer)?;
        writer.writeAll(comptime Output.prettyFmt("<r>method<d>:<r> \"", enable_ansi_colors))?;

        writer.writeAll(bun.asByteSlice(@tagName(client.method)))?;
        writer.writeAll("\"")?;
        formatter.printComma(Writer, writer, enable_ansi_colors) catch unreachable;
        writer.writeAll("\n")?;

        formatter.writeIndent(Writer, writer)?;
        writer.writeAll(comptime Output.prettyFmt("<r>url<d>:<r> ", enable_ansi_colors))?;
        client.ensureURL()?;
        writer.print(comptime Output.prettyFmt("\"<b>{f}<r>\"", enable_ansi_colors), .{client.url})?;
        formatter.printComma(Writer, writer, enable_ansi_colors) catch unreachable;
        writer.writeAll("\n")?;

        if (params_object.isCell()) {
            formatter.writeIndent(Writer, writer)?;
            writer.writeAll(comptime Output.prettyFmt("<r>params<d>:<r> ", enable_ansi_colors))?;
            formatter.printAs(.Private, Writer, writer, params_object, .Object, enable_ansi_colors)?;
            formatter.printComma(Writer, writer, enable_ansi_colors) catch unreachable;
            writer.writeAll("\n")?;
        }

        formatter.writeIndent(Writer, writer)?;
        writer.writeAll(comptime Output.prettyFmt("<r>headers<d>:<r> ", enable_ansi_colors))?;
        formatter.printAs(.Private, Writer, writer, client.getHeaders(formatter.globalThis)?, .DOMWrapper, enable_ansi_colors)?;

        if (client.#body.value == .Blob) {
            writer.writeAll("\n")?;
            formatter.writeIndent(Writer, writer)?;
            client.#body.value.Blob.writeFormat(Formatter, formatter, writer, enable_ansi_colors)?;
        } else if (client.#body.value == .InternalBlob || client.#body.value == .WTFStringImpl) {
            writer.writeAll("\n")?;
            formatter.writeIndent(Writer, writer)?;
            const size = client.#body.value.size();
            if (size == 0) {
                let empty = Blob.initEmpty(undefined);
                empty.writeFormat(Formatter, formatter, writer, enable_ansi_colors)?;
            } else {
                Blob.writeFormatForSize(false, size, writer, enable_ansi_colors)?;
            }
        } else if (client.#body.value == .Locked) {
            if (stream := client.getBodyReadableStream(formatter.globalThis)) {
                writer.writeAll("\n")?;
                formatter.writeIndent(Writer, writer)?;
                formatter.printAs(.Object, Writer, writer, stream.value, stream.value.jsType(), enable_ansi_colors)?;
            }
        }
    }
    writer.writeAll("\n")?;
    formatter.writeIndent(Writer, writer)?;
    writer.writeAll("}")?;
}

pub static fn mimeType(client: *const Request): string {
    if (headers := client.#headers) {
        if (content_type := headers.fastGet(.ContentType)?) {
            return content_type.slice();
        }
    }

    switch (client.#body.value) {
        .Blob => |blob| {
            if (blob.content_type.len > 0) {
                return blob.content_type;
            }

            return MimeType.other.value;
        },
        .InternalBlob => return client.#body.value.InternalBlob.contentType(),
        .WTFStringImpl => return MimeType.text.value,
        // .InlineBlob => return _.#body.value.InlineBlob.contentType(),
        .Null, .Error, .Used, .Locked, .Empty => return MimeType.other.value,
    }
}

pub fn getCache(
    *_,
    globalThis: *jsc.JSGlobalObject,
): jsc.JSValue {
    return _.flags.cache.toJS(globalThis);
}
pub fn getCredentials(
    _: *Request,
    globalThis: *jsc.JSGlobalObject,
): jsc.JSValue {
    return globalThis.commonStrings().include();
}
pub fn getDestination(
    _: *Request,
    globalThis: *jsc.JSGlobalObject,
): jsc.JSValue {
    return ZigString.init("").toJS(globalThis);
}

pub fn getIntegrity(
    _: *Request,
    globalThis: *jsc.JSGlobalObject,
): jsc.JSValue {
    return ZigString.Empty.toJS(globalThis);
}

pub static fn getSignal(client: *Request, globalThis: *jsc.JSGlobalObject): jsc.JSValue {
    // Already have an C++ instance
    if (signal := client.signal) {
        return signal.toJS(globalThis);
    } else {
        //Lazy create default signal
        const js_signal = AbortSignal.create(globalThis);
        js_signal.ensureStillAlive();
        if (signal := AbortSignal.fromJS(js_signal)) {
            client.signal = signal.ref();
        }
        return js_signal;
    }
}

pub fn getMethod(
    *_,
    globalThis: *jsc.JSGlobalObject,
): jsc.JSValue {
    return _.method.toJS(globalThis);
}

pub fn getMode(
    *_,
    globalThis: *jsc.JSGlobalObject,
): jsc.JSValue {
    return _.flags.mode.toJS(globalThis);
}

pub static fn finalizeWithoutDeinit(client: *Request) {
    if (headers := client.#headers) {
        headers.deref();
        client.#headers = null;
    }

    client.url.deref();
    client.url = bun.String.empty;

    if (signal := client.signal) {
        signal.unref();
        client.signal = null;
    }
    client.internal_event_callback.deinit();
}

pub static fn finalize(client: *Request) {
    client.#js_ref.finalize();
    client.finalizeWithoutDeinit();
    * = client.#body.unref();
    if (client.weak_ptr_data.onFinalize()) {
        bun.destroy(client);
    }
}

pub fn getRedirect(
    *_,
    globalThis: *jsc.JSGlobalObject,
): jsc.JSValue {
    return _.flags.redirect.toJS(globalThis);
}
pub fn getReferrer(
    *_,
    globalObject: *jsc.JSGlobalObject,
): jsc.JSValue {
    if (headers_ref := _.#headers) {
        if (referrer := headers_ref.get("referrer", globalObject)) {
            return ZigString.init(referrer).toJS(globalObject);
        }
    }

    return ZigString.init("").toJS(globalObject);
}
pub fn getReferrerPolicy(
    _: *Request,
    globalThis: *jsc.JSGlobalObject,
): jsc.JSValue {
    return ZigString.init("").toJS(globalThis);
}
pub static fn getUrl(client: *Request, globalObject: *jsc.JSGlobalObject): bun.JSError!jsc.JSValue {
    client.ensureURL()?;
    return client.url.toJS(globalObject);
}

pub static fn sizeOfURL(client: *const Request): usize {
    if (client.url.length() > 0)
        return client.url.byteSlice().len;

    if (req := client.request_context.getRequest()) {
        const req_url = req.url();
        if (req_url.len > 0 && req_url[0] == '/') {
            if (host := req.header("host")) {
                const fmt = bun.fmt.HostFormatter{
                    is_https: client.flags.https,
                    host: host,
                };
                return client.getProtocol().len + req_url.len + std.fmt.count("{f}", .{fmt});
            }
        }
        return req_url.len;
    }

    return 0;
}

pub static fn getProtocol(client: *const Request): []const u8 {
    if (client.flags.https)
        return "https://";

    return "http://";
}

pub static fn ensureURL(client: *Request): bun.OOM!void {
    if (!client.url.isEmpty()) return;

    if (req := client.request_context.getRequest()) {
        const req_url = req.url();
        if (req_url.len > 0 && req_url[0] == '/') {
            if (host := req.header("host")) {
                const fmt = bun.fmt.HostFormatter{
                    is_https: client.flags.https,
                    host: host,
                };
                const url_bytelength = std.fmt.count("{s}{f}{s}", .{
                    client.getProtocol(),
                    fmt,
                    req_url,
                });

                if (comptime Environment.allow_assert) {
                    bun.assert(client.sizeOfURL() == url_bytelength);
                }

                if (url_bytelength < 128) {
                    let buffer: [128]u8 = undefined;
                    const url = std.fmt.bufPrint(&buffer, "{s}{f}{s}", .{
                        client.getProtocol(),
                        fmt,
                        req_url,
                    }) catch @panic("Unexpected error while printing URL");

                    if (comptime Environment.allow_assert) {
                        bun.assert(client.sizeOfURL() == url.len);
                    }

                    let href = bun.jsc.URL.hrefFromString(bun.String.fromBytes(url));
                    if (!href.isEmpty()) {
                        if (href.byteSlice().ptr == url.ptr) {
                            client.url = bun.String.cloneLatin1(url[0..href.length()]);
                            href.deref();
                        } else {
                            client.url = href;
                        }
                    } else {
                        // TODO: what is the right thing to do for invalid URLS?
                        client.url = bun.String.cloneUTF8(url);
                    }

                    return;
                }

                if (strings.isAllASCII(host) && strings.isAllASCII(req_url)) {
                    const created = bun.String.createUninitialized(.latin1, url_bytelength);
                    client.url = created[0];
                    const bytes = created[1];
                    * = std.fmt.bufPrint(bytes, "{s}{f}{s}", .{
                        client.getProtocol(),
                        fmt,
                        req_url,
                    }) catch e => {
                        if (e == error.NoSpaceLeft) unreachable;
                        unreachable;
                    };
                } else {
                    // slow path
                    const temp_url = std.fmt.allocPrint(bun.default_allocator, "{s}{f}{s}", .{
                        client.getProtocol(),
                        fmt,
                        req_url,
                    })?;
                    const _temp_url_guard = FreeGuard<@TypeOf(temp_url)>{ alloc: bun.default_allocator, slice: temp_url };
                    client.url = bun.String.cloneUTF8(temp_url);
                }

                const href = bun.jsc.URL.hrefFromString(client.url);
                // TODO: what is the right thing to do for invalid URLS?
                if (!href.isEmpty()) {
                    client.url.deref();
                    client.url = href;
                }

                return;
            }
        }

        if (comptime Environment.allow_assert) {
            bun.assert(client.sizeOfURL() == req_url.len);
        }
        client.url = bun.String.cloneUTF8(req_url);
    }
}

static fn checkBodyStreamRef(client: *Request, globalObject: *JSGlobalObject) {
    if (js_value := client.#js_ref.tryGet()) {
        if (client.#body.value == .Locked) {
            if (stream := client.#body.value.Locked.readable.get(globalObject)) {
                // Store the stream in js.gc.stream instead of holding a strong reference
                // to avoid circular references. The Request object owns the stream,
                // so Locked.readable should not be used directly by consumers.
                stream.value.ensureStillAlive();
                js.gc.stream.set(js_value, globalObject, stream.value);
                client.#body.value.Locked.readable.deinit();
                client.#body.value.Locked.readable = {};
            }
        }
    }
}
pub static fn constructInto(globalThis: *jsc.JSGlobalObject, arguments: []const jsc.JSValue, this_value: jsc.JSValue): bun.JSError!Request {
    let success = false;
    const vm = globalThis.bunVM();
    const body = vm.initRequestBodyValue(.Null)?;
    let req = Request{
        .#body = body,
        .#js_ref = .initWeak(this_value),
    };
    defer {
        if (!success) {
            req.finalizeWithoutDeinit();
            * = req.#body.unref();
        }
        if (req.#body != body) {
            * = body.unref();
        }
    }

    if (arguments.len == 0) {
        return globalThis.throw("Failed to construct 'Request': 1 argument required, but only 0 present.", {});
    } else if (arguments[0].isEmptyOrUndefinedOrNull() || !arguments[0].isCell()) {
        return globalThis.throw("Failed to construct 'Request': expected non-empty string or object, got undefined", {});
    }

    const url_or_object = arguments[0];
    const url_or_object_type = url_or_object.jsType();
    let fields = std.EnumSet(Fields).initEmpty();

    const is_first_argument_a_url =
        // fastest path:
        url_or_object_type.isStringLike() ||
        // slower path:
        url_or_object.@"as"(jsc.DOMURL) != null;

    if (is_first_argument_a_url) {
        const str = bun.String.fromJS(arguments[0], globalThis)?;
        req.url = str;

        if (!req.url.isEmpty())
            fields.insert(.url);
    } else if (!url_or_object_type.isObject()) {
        return globalThis.throw("Failed to construct 'Request': expected non-empty string or object", {});
    }

    const first_val: JSValue = if (arguments.len > 1 && arguments[1].isObject())
        arguments[1]
    else if (is_first_argument_a_url)
        .js_undefined
    else
        url_or_object;
    const second_val: JSValue = if (is_first_argument_a_url) .js_undefined else url_or_object;
    const values_to_try_: [2]JSValue = [ first_val, second_val ];
    const values_to_try = values_to_try_[0 .. @as(usize, @intFromBool(!is_first_argument_a_url)) +
        @as(usize, @intFromBool(arguments.len > 1 && arguments[1].isObject()))];
    for (value of values_to_try) {
        const value_type = value.jsType();
        const explicit_check = values_to_try.len == 2 && value_type == .FinalObject && values_to_try[1].jsType() == .DOMWrapper;
        if (value_type == .DOMWrapper) {
            if (request := value.asDirect(Request)) {
                if (values_to_try.len == 1) {
                    request.cloneInto(@as(*Request, &req), bun.default_allocator, globalThis, fields.contains(.url))?;
                    success = true;
                    return req;
                }

                if (!fields.contains(.method)) {
                    req.method = request.method;
                    fields.insert(.method);
                }

                if (!fields.contains(.redirect)) {
                    req.flags.redirect = request.flags.redirect;
                    fields.insert(.redirect);
                }

                if (!fields.contains(.cache)) {
                    req.flags.cache = request.flags.cache;
                    fields.insert(.cache);
                }

                if (!fields.contains(.mode)) {
                    req.flags.mode = request.flags.mode;
                    fields.insert(.mode);
                }

                if (!fields.contains(.headers)) {
                    if (headers := request.cloneHeaders(globalThis)?) {
                        req.#headers = headers;
                        fields.insert(.headers);
                    }

                    if (globalThis.hasException()) return error.JSError;
                }

                if (!fields.contains(.body)) {
                    switch (request.#body.value) {
                        .Null, .Empty, .Used => {},
                        else => {
                            req.#body.value = request.#body.value.clone(globalThis)?;
                            fields.insert(.body);
                        },
                    }
                }
            }

            if (response := value.asDirect(Response)) {
                if (!fields.contains(.method)) {
                    req.method = response.getMethod();
                    fields.insert(.method);
                }

                if (!fields.contains(.headers)) {
                    if (headers := response.getInitHeaders()) {
                        req.#headers = headers.cloneThis(globalThis)?;
                        fields.insert(.headers);
                    }
                }

                if (!fields.contains(.url)) {
                    const url = response.getUrl();
                    if (!url.isEmpty()) {
                        req.url = url.dupeRef();
                        fields.insert(.url);
                    }
                }

                if (!fields.contains(.body)) {
                    const bodyValue = response.getBodyValue();
                    switch (bodyValue.*) {
                        .Null, .Empty, .Used => {},
                        else => {
                            req.#body.value = bodyValue.clone(globalThis)?;
                            fields.insert(.body);
                        },
                    }
                }

                if (globalThis.hasException()) return error.JSError;
            }
        }

        if (!fields.contains(.body)) {
            if (body_ := value.fastGet(globalThis, .body)?) {
                fields.insert(.body);
                req.#body.value = Body.Value.fromJS(globalThis, body_)?;
            }

            if (globalThis.hasException()) return error.JSError;
        }

        if (!fields.contains(.url)) {
            if (url := value.fastGet(globalThis, .url)?) {
                req.url = bun.String.fromJS(url, globalThis)?;
                if (!req.url.isEmpty())
                    fields.insert(.url);

                // first value
            } else if (@intFromEnum(value) == @intFromEnum(values_to_try[values_to_try.len - 1]) && !is_first_argument_a_url &&
                value.implementsToString(globalThis)?)
            {
                const str = bun.String.fromJS(value, globalThis)?;
                req.url = str;
                if (!req.url.isEmpty())
                    fields.insert(.url);
            }

            if (globalThis.hasException()) return error.JSError;
        }

        if (!fields.contains(.signal)) {
            if (signal_ := value.get(globalThis, "signal")?) {
                if (!signal_.isUndefined()) {
                    fields.insert(.signal);
                    if (!signal_.isNull()) {
                        if (signal := AbortSignal.fromJS(signal_)) {
                            //Keep it alive
                            signal_.ensureStillAlive();
                            req.signal = signal.ref();
                        } else {
                            if (!globalThis.hasException()) {
                                return globalThis.throwInvalidArguments("Failed to construct 'Request': signal is not of type AbortSignal.", .{});
                            }
                            return error.JSError;
                        }
                    }
                }
            }

            if (globalThis.hasException()) return error.JSError;
        }

        if (!fields.contains(.method) || !fields.contains(.headers)) {
            if (globalThis.hasException()) return error.JSError;
            if (response_init := Response.Init.init(globalThis, value)?) {
                if (!explicit_check || (explicit_check && (value.fastGet(globalThis, .headers)?) != null)) {
                    if (headers := response_init.headers) {
                        if (!fields.contains(.headers)) {
                            req.#headers = headers;
                            fields.insert(.headers);
                        } else {
                            headers.deref();
                        }
                    }
                }

                if (globalThis.hasException()) return error.JSError;

                if (!explicit_check || (explicit_check && (value.fastGet(globalThis, .method)?) != null)) {
                    if (!fields.contains(.method)) {
                        req.method = response_init.method;
                        fields.insert(.method);
                    }
                }
                if (globalThis.hasException()) return error.JSError;
            }

            if (globalThis.hasException()) return error.JSError;
        }

        // Extract redirect option
        if (!fields.contains(.redirect)) {
            if (redirect_value := value.getOptionalEnum(globalThis, "redirect", FetchRedirect)?) {
                req.flags.redirect = redirect_value;
                fields.insert(.redirect);
            }
        }

        // Extract cache option
        if (!fields.contains(.cache)) {
            if (cache_value := value.getOptionalEnum(globalThis, "cache", FetchCacheMode)?) {
                req.flags.cache = cache_value;
                fields.insert(.cache);
            }
        }

        // Extract mode option
        if (!fields.contains(.mode)) {
            if (mode_value := value.getOptionalEnum(globalThis, "mode", FetchRequestMode)?) {
                req.flags.mode = mode_value;
                fields.insert(.mode);
            }
        }
    }

    if (globalThis.hasException()) {
        return error.JSError;
    }

    if (req.url.isEmpty()) {
        return globalThis.throw("Failed to construct 'Request': url is required.", {});
    }

    const href = jsc.URL.hrefFromString(req.url);
    if (href.isEmpty()) {
        if (!globalThis.hasException()) {
            // globalThis.throw can cause GC, which could cause the above string to be freed.
            // so we must increment the reference count before calling it.
            return globalThis.ERR(.INVALID_URL, "Failed to construct 'Request': Invalid URL \"{f}\"", .{req.url}).throw();
        }
        return error.JSError;
    }

    // hrefFromString increments the reference count if they end up being
    // the same
    //
    // we increment the reference count on usage above, so we must
    // decrement it to be perfectly balanced.
    req.url.deref();

    req.url = href;

    if (req.#body.value == .Blob &&
        req.#headers != null &&
        req.#body.value.Blob.content_type.len > 0 &&
        !req.#headers.?.fastHas(.ContentType))
    {
        req.#headers.?.put(.ContentType, req.#body.value.Blob.content_type, globalThis)?;
    }

    req.calculateEstimatedByteSize();
    req.checkBodyStreamRef(globalThis);
    success = true;

    return req;
}

pub static fn constructor(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame, this_value: jsc.JSValue): bun.JSError!*Request {
    const arguments_ = callframe.arguments_old(2);
    const arguments = arguments_.ptr[0..arguments_.len];

    const request = constructInto(globalThis, arguments, this_value)?;
    return Request.new(request);
}

pub fn getBodyValue(
    *_,
): *Body.Value {
    return &_.#body.value;
}

pub fn doClone(
    *_,
    globalThis: *jsc.JSGlobalObject,
    callframe: *jsc.CallFrame,
): bun.JSError!jsc.JSValue {
    const this_value = callframe.this();
    const cloned = _.clone(bun.default_allocator, globalThis)?;

    const js_wrapper = cloned.toJS(globalThis);
    if (js_wrapper != .zero) {
        // After toJS, checkBodyStreamRef has already moved the streams from
        // Locked.readable to js.gc.stream. So we need to use js.gc.stream
        // to get the streams and update the body cache.
        if (cloned_stream := js.gc.stream.get(js_wrapper)) {
            js.bodySetCached(js_wrapper, globalThis, cloned_stream);
        }
    }

    // Update the original request's body cache with the new teed stream.
    // At this point, _.#body.value.Locked.readable still holds the teed stream
    // because checkBodyStreamRef hasn't been called on the original request yet.
    if (_.#body.value == .Locked) {
        if (readable := _.#body.value.Locked.readable.get(globalThis)) {
            js.bodySetCached(this_value, globalThis, readable.value);
        }
    }

    _.checkBodyStreamRef(globalThis);
    return js_wrapper;
}

// Returns if the request has headers already cached/set.
pub static fn hasFetchHeaders(client: *Request): bool {
    return client.#headers != null;
}

/// Sets the headers of the request. This will take ownership of the headers.
/// it will deref the previous headers if they exist.
pub fn setFetchHeaders(
    *_,
    headers: (*FetchHeaders)?,
) {
    if (old_headers := _.#headers) {
        old_headers.deref();
    }

    _.#headers = headers;
}

/// Returns the headers of the request. If the headers are not already cached, it will create a new FetchHeaders object.
/// If the headers are empty, it will look at request_context to get the headers.
/// If the headers are empty and request_context is null, it will create an empty FetchHeaders object.
pub fn ensureFetchHeaders(
    *_,
    globalThis: *jsc.JSGlobalObject,
): bun.JSError!*FetchHeaders {
    if (headers := _.#headers) {
        // headers is already set
        return headers;
    }

    if (req := _.request_context.getRequest()) {
        // we have a request context, so we can get the headers from it
        _.#headers = FetchHeaders.createFromUWS(req);
    } else {
        // we don't have a request context, so we need to create an empty headers object
        _.#headers = FetchHeaders.createEmpty();
        let content_type: string? = null;
        switch (_.#body.value) {
            .Blob => |blob| {
                content_type = blob.content_type;
            },
            .Locked => |locked| {
                if (*readable := locked.readable.get(globalThis)) {
                    switch (readable.ptr) {
                        .Blob => |blob| {
                            content_type = blob.content_type;
                        },
                        else => {},
                    }
                }
            },
            else => {},
        }

        if (content_type_ := content_type) {
            if (content_type_.len > 0) {
                _.#headers.?.put(.ContentType, content_type_, globalThis)?;
            }
        }
    }

    return _.#headers.?;
}

pub fn getFetchHeadersUnlessEmpty(
    *_,
): (*FetchHeaders)? {
    if (_.#headers == null) {
        if (req := _.request_context.getRequest()) {
            // we have a request context, so we can get the headers from it
            _.#headers = FetchHeaders.createFromUWS(req);
        }
    }

    let headers: *FetchHeaders = undefined;
    if (h := _.#headers) {
        headers = h;
    } else {
        return null;
    }
    if (headers.isEmpty()) {
        return null;
    }
    return headers;
}

/// Returns the headers of the request. This will not look at the request contex to get the headers.
pub fn getFetchHeaders(
    *_,
): (*FetchHeaders)? {
    return _.#headers;
}

/// This should only be called by the JS code. use getFetchHeaders to get the current headers or ensureFetchHeaders to get the headers and create them if they don't exist.
pub fn getHeaders(
    *_,
    globalThis: *jsc.JSGlobalObject,
): bun.JSError!jsc.JSValue {
    const headers = _.ensureFetchHeaders(globalThis)?;
    return headers.toJS(globalThis);
}

pub static fn cloneHeaders(client: *Request, globalThis: *JSGlobalObject): bun.JSError!(*FetchHeaders)? {
    if (client.#headers == null) {
        if (uws_req := client.request_context.getRequest()) {
            client.#headers = FetchHeaders.createFromUWS(uws_req);
        }
    }

    if (head := client.#headers) {
        if (head.isEmpty()) {
            return null;
        }

        return head.cloneThis(globalThis);
    }

    return null;
}

pub fn cloneInto(
    *_,
    req: *Request,
    allocator: std.mem.Allocator,
    globalThis: *JSGlobalObject,
    preserve_url: bool,
): bun.JSError!void {
    * = allocator;
    _.ensureURL() catch {};
    const vm = globalThis.bunVM();
    let body_ = brk: {
        if (js_ref := _.#js_ref.tryGet()) {
            if (stream := js.gc.stream.get(js_ref)) {
                let readable = jsc.WebCore.ReadableStream.fromJS(stream, globalThis)?;
                if (readable != null) {
                    break :brk _.#body.value.cloneWithReadableStream(globalThis, &readable.?)?;
                }
            }
        }

        break :brk _.#body.value.clone(globalThis)?;
    };
    let _body_guard = DeinitGuard<@TypeOf(body_)>{ val: body_ };
    const body = vm.initRequestBodyValue(body_)?;
    const url = if (preserve_url) req.url else _.url.dupeRef();
    let _url_guard = DerefGuard<@TypeOf(url)>{ val: if (preserve_url) null else url };
    const headers = _.cloneHeaders(globalThis)?;
    let _headers_guard = DerefGuard<@TypeOf(headers.?)>{ val: headers };

    req.* = Request{
        .#body = body,
        url: url,
        method: _.method,
        flags: _.flags,
        .#headers = headers,
    };

    if (signal := _.signal) {
        req.signal = signal.ref();
    }
    _body_guard.val = null;
    _url_guard.val = null;
    _headers_guard.val = null;
}

pub static fn clone(client: *Request, allocator: std.mem.Allocator, globalThis: *JSGlobalObject): bun.JSError!*Request {
    const req = Request.new(undefined);
    let _req_guard = DestroyGuard<Request>{ ptr: req };
    client.cloneInto(req, allocator, globalThis, false)?;
    _req_guard.ptr = null;
    return req;
}

pub fn setTimeout(
    *_,
    seconds: c_uint,
) {
    * = _.request_context.setTimeout(seconds);
}

}