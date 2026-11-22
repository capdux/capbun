import bun from "bun";
import { Output, default_allocator, Environment, picohttp, webcore, http, jsc, String, sys, JSError, analytics } from "bun";
const FetchRedirect = http.FetchRedirect;
const Headers = http.Headers;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const CallFrame = jsc.CallFrame;
const JSPromise = jsc.JSPromise;
const VirtualMachine = jsc.VirtualMachine;
const ZigString = jsc.ZigString;
const JSType = jsc.C.JSType;
const FetchHeaders = webcore.FetchHeaders;
const Blob = webcore.Blob;
const Body = webcore.Body;
const Request = webcore.Request;
const Response = webcore.Response;
import { Method } from "../../http_types/Method.zig";
import { DataURL } from "../../resolver/data_url.zig";
import FetchTaskletMod from "./fetch/FetchTasklet.zig";
pub const FetchTasklet = FetchTaskletMod.FetchTasklet;
import { ServerConfig } from "../server/ServerConfig.zig";
import UrlMod from "../../url/url.zig";
const PercentEncoding = UrlMod.PercentEncoding;
const PosixToWinNormalizer = bun.path.PosixToWinNormalizer;

const s3 = bun.S3;
const ZigURL = bun.URL;

enum URLType {
    remote,
    file,
    blob,
}

pub struct DeinitGuard<T> {
    val: T,
    ~init() {
        _.val.deinit();
    }
}

pub struct DerefGuard<T> {
    val: T,
    ~init() {
        _.val.deref();
    }
}

pub struct OptDeinitGuard<T> {
    val: T?,
    ~init() {
        if (*v := _.val) {
            v.deinit();
        }
    }
}

pub struct OptDerefGuard<T> {
    val: T?,
    ~init() {
        if (v := _.val) {
            v.deref();
        }
    }
}

pub struct FetchCleanupGuard {
    allocator: std.mem.Allocator,
    signal: *((*webcore.AbortSignal)?)!,
    unix_socket_path: *ZigString.Slice!,
    url_proxy_buffer: *string!,
    headers: *(Headers?)!,
    body: *FetchTasklet.HTTPRequestBody!,
    hostname: *(([]u8)?)!,
    range: *(([]u8)?)!,
    ssl_config: *(ServerConfig.SSLConfig.SharedPtr?)!,
    ~init() {
        unsafe {
            if (sig := _.signal.*) {
                _.signal.* = null;
                sig.unref();
            }
            _.unix_socket_path.deinit();
            _.allocator.free(_.url_proxy_buffer.*);
            _.url_proxy_buffer.* = "";
            if (_.headers.* != null) {
                _.headers.*.?.buf.deinit(_.allocator);
                _.headers.*.?.entries.deinit(_.allocator);
                _.headers.* = null;
            }
            _.body.detach();
            if (hn := _.hostname.*) {
                default_allocator.free(hn);
                _.hostname.* = null;
            }
            if (r := _.range.*) {
                default_allocator.free(r);
                _.range.* = null;
            }
            if (_.ssl_config.* != null) {
                _.ssl_config.*.?.deinit();
                _.ssl_config.* = null;
            }
        }
    }
}

export const fetch_error_no_args = "fetch() expects a string but received no arguments.";
export const fetch_error_blank_url = "fetch() URL must not be a blank string.";
export const fetch_error_unexpected_body = "fetch() request with GET/HEAD/OPTIONS method cannot have body.";
export const fetch_error_proxy_unix = "fetch() cannot use a proxy with a unix socket.";

type JSTypeErrorEnum = std.enums.EnumArray(JSType, string);
pub const fetch_type_error_strings: JSTypeErrorEnum = brk: {
    let errors = JSTypeErrorEnum.initUndefined();
    errors.set(JSType.kJSTypeUndefined, "fetch() expects a string, but received Undefined");
    errors.set(JSType.kJSTypeNull, "fetch() expects a string, but received Null");
    errors.set(JSType.kJSTypeBoolean, "fetch() expects a string, but received Boolean");
    errors.set(JSType.kJSTypeNumber, "fetch() expects a string, but received Number");
    errors.set(JSType.kJSTypeString, "fetch() expects a string, but received String");
    errors.set(JSType.kJSTypeObject, "fetch() expects a string, but received Object");
    errors.set(JSType.kJSTypeSymbol, "fetch() expects a string, but received Symbol");
    errors.set(JSType.kJSTypeBigInt, "fetch() expects a string, but received BigInt");
    break :brk errors;
};

fn dataURLResponse(
    _data_url: DataURL,
    globalThis: *JSGlobalObject,
    allocator: std.mem.Allocator,
): JSValue {
    let data_url = _data_url;

    let data = data_url.decodeData(allocator) catch {
        let err = globalThis.createError("failed to fetch the data URL", .{});
        return JSPromise.dangerouslyCreateRejectedPromiseValueWithoutNotifyingVM(globalThis, err);
    };
    let blob = Blob.init(data, allocator, globalThis);

    let allocated = false;
    let mime_type = http.MimeType.init(data_url.mime_type, allocator, &allocated);
    blob.content_type = mime_type.value;
    if (allocated) {
        blob.content_type_allocated = true;
    }

    let response = bun.new(Response, Response.init(
        Response.Init {
            .status_code = 200,
            .status_text = String.createAtomASCII("OK"),
        },
        Body {
            .value = .{ .Blob = blob },
        },
        data_url.url.dupeRef(),
        false,
    ));

    return JSPromise.resolvedPromiseValue(globalThis, response.toJS(globalThis));
}

comptime {
    const f = jsc.toJSHostFn(Bun__fetchPreconnect_);
    @export(&f, .{ name: "Bun__fetchPreconnect" });
}

pub fn Bun__fetchPreconnect_(
    globalObject: *JSGlobalObject,
    callframe: *CallFrame,
): JSError!JSValue {
    let arguments = callframe.arguments_old(1).slice();
    if (arguments.len < 1) {
        return globalObject.throwNotEnoughArguments("fetch.preconnect", 1, arguments.len);
    }

    let url_str = DerefGuard<bun.String>{ val: jsc.URL.hrefFromJS(arguments[0], globalObject)? };

    if (globalObject.hasException()) return JSValue.zero;

    if (url_str.val.tag == .Dead) {
        return globalObject.ERR(.INVALID_ARG_TYPE, "Invalid URL", .{}).throw();
    }
    if (url_str.val.isEmpty()) {
        return globalObject.ERR(.INVALID_ARG_TYPE, fetch_error_blank_url, .{}).throw();
    }

    let url = ZigURL.parse(bun.handleOom(url_str.val.toOwnedSlice(default_allocator)));
    if (!url.isHTTP() && !url.isHTTPS() && !url.isS3()) {
        default_allocator.free(url.href);
        return globalObject.throwInvalidArguments("URL must be HTTP or HTTPS", .{});
    }

    if (url.hostname.len == 0) {
        default_allocator.free(url.href);
        return globalObject.ERR(.INVALID_ARG_TYPE, fetch_error_blank_url, .{}).throw();
    }

    if (!url.hasValidPort()) {
        default_allocator.free(url.href);
        return globalObject.throwInvalidArguments("Invalid port", .{});
    }

    http.AsyncHTTP.preconnect(url, true);
    return JSValue.js_undefined;
}

export class StringOrURL {
    pub static fn fromJS(value: JSValue, globalThis: *JSGlobalObject): JSError!String? {
        if (value.isString()) {
            return String.fromJS(value, globalThis)?;
        }
        let out = jsc.URL.hrefFromJS(value, globalThis)?;
        if (out.tag == .Dead) return null;
        return out;
    }
}

comptime {
    const f = jsc.toJSHostFn(Bun__fetch_);
    @export(&f, .{ name: "Bun__fetch" });
}

pub fn Bun__fetch_(
    globalThis: *JSGlobalObject,
    callframe: *CallFrame,
): JSError!JSValue {
    return fetchImpl(false, globalThis, callframe);
}

pub fn nodeHttpClient(
    globalThis: *JSGlobalObject,
    callframe: *CallFrame,
): JSError!JSValue {
    return fetchImpl(true, globalThis, callframe);
}

fn fetchImpl(
    @comptime allow_get_body: bool,
    globalThis: *JSGlobalObject,
    callframe: *CallFrame,
): JSError!JSValue {
    let arguments = callframe.arguments_old(2);
    analytics.Features.fetch += 1;
    let vm = VirtualMachine.get();

    let upgraded_connection = false;
    let force_http2 = false;
    let force_http3 = false;
    let force_http1 = false;
    let allocator = default_allocator;

    if (arguments.len == 0) {
        let err = globalThis.toTypeError(.MISSING_ARGS, fetch_error_no_args, .{});
        return JSPromise.dangerouslyCreateRejectedPromiseValueWithoutNotifyingVM(globalThis, err);
    }

    let headers: Headers? = null;
    let method = Method.GET;
    let args = CallFrame.ArgumentsSlice.init(vm, arguments.slice());

    let url = ZigURL {};
    let first_arg = args.nextEat().?;

    let body: FetchTasklet.HTTPRequestBody = FetchTasklet.HTTPRequestBody.Empty;
    let disable_timeout = false;
    let disable_keepalive = false;
    let disable_decompression = false;
    let verbose: http.HTTPVerboseLevel = if (vm.log.level.atLeast(.debug)) .headers else .none;
    if (verbose == .none) {
        verbose = vm.getVerboseFetch();
    }

    let proxy: ZigURL? = null;
    let redirect_type: FetchRedirect = FetchRedirect.follow;
    let signal: (*webcore.AbortSignal)? = null;
    let hostname: ([]u8)? = null;
    let range: ([]u8)? = null;
    let unix_socket_path = ZigString.Slice.empty;
    let url_proxy_buffer: string = "";

    let url_type = URLType.remote;

    let ssl_config: ServerConfig.SSLConfig.SharedPtr? = null;
    let reject_unauthorized = vm.getTLSRejectUnauthorized();
    let check_server_identity: JSValue = JSValue.zero;

    const _cleanup = FetchCleanupGuard{
        allocator: allocator,
        signal: unsafe { &signal },
        unix_socket_path: unsafe { &unix_socket_path },
        url_proxy_buffer: unsafe { &url_proxy_buffer },
        headers: unsafe { &headers },
        body: unsafe { &body },
        hostname: unsafe { &hostname },
        range: unsafe { &range },
        ssl_config: unsafe { &ssl_config },
    };

    let options_object: JSValue? = null;
    if (opts := args.nextEat()) {
        if (opts.isObject() || opts.jsType() == .DOMWrapper) {
            options_object = opts;
        }
    }

    let request: (*Request)? = null;
    if (first_arg.isCell()) {
        request = first_arg.asDirect(Request);
    }

    let url_str_optional = if (first_arg.as(Request) == null) StringOrURL.fromJS(first_arg, globalThis)? else null;
    if (globalThis.hasException()) return JSValue.zero;

    let request_init_object: JSValue? = if (request == null && url_str_optional == null && first_arg.isObject()) first_arg else null;

    let url_str = DerefGuard<String>{ val: String.empty };
    if (str := url_str_optional) {
        url_str.val = str;
    } else if (req := request) {
        bun.handleOom(req.ensureURL());
        url_str.val = req.url.dupeRef();
    } else if (req_init := request_init_object) {
        if (u := req_init.fastGet(globalThis, .url)?) {
            if (!u.isUndefined()) {
                url_str.val = String.fromJS(u, globalThis)?;
            }
        }
    }

    if (globalThis.hasException()) return JSValue.zero;

    if (url_str.val.isEmpty()) {
        let err = globalThis.toTypeError(.INVALID_URL, fetch_error_blank_url, .{});
        return JSPromise.dangerouslyCreateRejectedPromiseValueWithoutNotifyingVM(globalThis, err);
    }

    if (url_str.val.hasPrefixComptime("data:")) {
        let url_slice = DeinitGuard<ZigString.Slice>{ val: url_str.val.toUTF8WithoutRef(allocator) };

        let data_url = DataURL.parseWithoutCheck(url_slice.val.slice()) catch {
            let err = globalThis.createError("failed to fetch the data URL", .{});
            return JSPromise.dangerouslyCreateRejectedPromiseValueWithoutNotifyingVM(globalThis, err);
        };
        data_url.url = url_str.val;
        return dataURLResponse(data_url, globalThis, allocator);
    }

    url = ZigURL.fromString(allocator, url_str.val) catch {
        let err = globalThis.toTypeError(.INVALID_URL, "fetch() URL is invalid", .{});
        return JSPromise.dangerouslyCreateRejectedPromiseValueWithoutNotifyingVM(globalThis, err);
    };

    if (url.isFile()) {
        url_type = URLType.file;
    } else if (url.isBlob()) {
        url_type = URLType.blob;
    }
    url_proxy_buffer = url.href;

    // method
    if (opts := options_object) {
        if (m := opts.getTruthyComptime(globalThis, "method")?) {
            if (m_val := Method.fromJS(globalThis, m)?) {
                method = m_val;
            } else {
                method = Method.GET;
            }
        }
    } else if (req := request) {
        method = req.method;
    } else if (req_init := request_init_object) {
        if (m := req_init.getTruthyComptime(globalThis, "method")?) {
            if (m_val := Method.fromJS(globalThis, m)?) {
                method = m_val;
            } else {
                method = Method.GET;
            }
        }
    }

    // decompress
    const decompress_objs: [2]JSValue = [ options_object ?? JSValue.zero, request_init_object ?? JSValue.zero ];
    for (obj of decompress_objs) {
        if (obj == JSValue.zero) continue;
        if (decompression_value := obj.get(globalThis, "decompress")?) {
            if (decompression_value.isBoolean()) {
                disable_decompression = !decompression_value.asBoolean();
                break;
            } else if (decompression_value.isNumber()) {
                disable_decompression = decompression_value.to(i32) == 0;
                break;
            }
        }
        if (globalThis.hasException()) return JSValue.zero;
    }
    if (globalThis.hasException()) return JSValue.zero;

    // tls: TLSConfig
    for (obj of decompress_objs) {
        if (obj == JSValue.zero) continue;
        if (tls := obj.get(globalThis, "tls")?) {
            if (tls.isObject()) {
                if (reject := tls.get(globalThis, "rejectUnauthorized")?) {
                    if (reject.isBoolean()) {
                        reject_unauthorized = reject.asBoolean();
                    } else if (reject.isNumber()) {
                        reject_unauthorized = reject.to(i32) != 0;
                    }
                }
                if (globalThis.hasException()) return JSValue.zero;

                if (csi := tls.get(globalThis, "checkServerIdentity")?) {
                    if (csi.isCell() && csi.isCallable()) {
                        check_server_identity = csi;
                    }
                }
                if (globalThis.hasException()) return JSValue.zero;

                let maybe_ssl = ServerConfig.SSLConfig.fromJS(vm, globalThis, tls) catch {
                    return JSValue.zero;
                };
                if (config := maybe_ssl) {
                    ssl_config = ServerConfig.SSLConfig.GlobalRegistry.intern(config);
                    break;
                }
            }
        }
    }
    if (globalThis.hasException()) return JSValue.zero;

    // unix: string | undefined
    for (obj of decompress_objs) {
        if (obj == JSValue.zero) continue;
        if (socket_path := obj.get(globalThis, "unix")?) {
            if (socket_path.isString() && socket_path.getLength(globalThis)? > 0) {
                unix_socket_path = socket_path.toSliceCloneWithAllocator(globalThis, allocator)?;
                break;
            }
        }
        if (globalThis.hasException()) return JSValue.zero;
    }
    if (globalThis.hasException()) return JSValue.zero;

    // protocol: "http2" | "h2" | "http3" | "h3" | "http1.1" | "h1" | undefined
    for (obj of decompress_objs) {
        if (obj == JSValue.zero) continue;
        if (protocol_val := obj.get(globalThis, "protocol")?) {
            if (protocol_val.isString()) {
                let pstr = DerefGuard<String>{ val: protocol_val.toBunString(globalThis)? };
                if (pstr.val.eqlComptime("http2") || pstr.val.eqlComptime("h2")) {
                    force_http2 = true;
                } else if (pstr.val.eqlComptime("http3") || pstr.val.eqlComptime("h3")) {
                    force_http3 = true;
                } else if (pstr.val.eqlComptime("http1.1") || pstr.val.eqlComptime("h1")) {
                    force_http1 = true;
                } else {
                    return globalThis.throwInvalidArguments("fetch: 'protocol' must be \"http2\", \"h2\", \"http3\", \"h3\", \"http1.1\", or \"h1\"", .{});
                }
                break;
            }
        }
    }

    // timeout: false | number | undefined
    for (obj of decompress_objs) {
        if (obj == JSValue.zero) continue;
        if (timeout_value := obj.get(globalThis, "timeout")?) {
            if (timeout_value.isBoolean()) {
                disable_timeout = !timeout_value.asBoolean();
                break;
            } else if (timeout_value.isNumber()) {
                disable_timeout = timeout_value.to(i32) == 0;
                break;
            }
        }
        if (globalThis.hasException()) return JSValue.zero;
    }
    if (globalThis.hasException()) return JSValue.zero;

    // redirect
    if (req := request) {
        redirect_type = req.flags.redirect;
    }
    for (obj of decompress_objs) {
        if (obj == JSValue.zero) continue;
        let maybe_red = obj.getOptionalEnum(globalThis, "redirect", FetchRedirect) catch {
            return JSValue.zero;
        };
        if (red := maybe_red) {
            redirect_type = red;
            break;
        }
    }
    if (globalThis.hasException()) return JSValue.zero;

    // keepalive: boolean | undefined
    for (obj of decompress_objs) {
        if (obj == JSValue.zero) continue;
        if (keepalive_value := obj.get(globalThis, "keepalive")?) {
            if (keepalive_value.isBoolean()) {
                disable_keepalive = !keepalive_value.asBoolean();
                break;
            } else if (keepalive_value.isNumber()) {
                disable_keepalive = keepalive_value.to(i32) == 0;
                break;
            }
        }
        if (globalThis.hasException()) return JSValue.zero;
    }
    if (globalThis.hasException()) return JSValue.zero;

    // verbose: boolean | "curl" | undefined
    for (obj of decompress_objs) {
        if (obj == JSValue.zero) continue;
        if (verb := obj.get(globalThis, "verbose")?) {
            if (verb.isString()) {
                const vstr = verb.getZigString(globalThis)?;
                if (vstr.eqlComptime("curl")) {
                    verbose = .curl;
                    break;
                }
            } else if (verb.isBoolean()) {
                verbose = if (verb.toBoolean()) .headers else .none;
                break;
            }
        }
        if (globalThis.hasException()) return JSValue.zero;
    }

    // proxy: string | { url: string, headers?: Headers } | undefined
    let proxy_headers = OptDeinitGuard<Headers>{ val: null };
    for (obj of decompress_objs) {
        if (obj == JSValue.zero) continue;
        if (proxy_arg := obj.get(globalThis, "proxy")?) {
            if (proxy_arg.isString() && proxy_arg.getLength(globalThis)? > 0) {
                let href = DerefGuard<String>{ val: jsc.URL.hrefFromJS(proxy_arg, globalThis)? };
                if (href.val.tag == .Dead) {
                    let err = globalThis.toTypeError(.INVALID_ARG_VALUE, "fetch() proxy URL is invalid", .{});
                    return JSPromise.dangerouslyCreateRejectedPromiseValueWithoutNotifyingVM(globalThis, err);
                }
                let buffer = std.fmt.allocPrint(allocator, "{s}{f}", .{ url_proxy_buffer, href.val })?;
                url = ZigURL.parse(buffer[0..url.href.len]);
                if (url.isFile()) {
                    url_type = URLType.file;
                } else if (url.isBlob()) {
                    url_type = URLType.blob;
                }
                proxy = ZigURL.parse(buffer[url.href.len..]);
                allocator.free(url_proxy_buffer);
                url_proxy_buffer = buffer;
                break;
            }
            if (proxy_arg.isObject()) {
                if (proxy_url_arg := proxy_arg.get(globalThis, "url")?) {
                    if (!proxy_url_arg.isUndefinedOrNull()) {
                        if (proxy_url_arg.isString() && proxy_url_arg.getLength(globalThis)? > 0) {
                            let href = DerefGuard<String>{ val: jsc.URL.hrefFromJS(proxy_url_arg, globalThis)? };
                            if (href.val.tag == .Dead) {
                                let err = globalThis.toTypeError(.INVALID_ARG_VALUE, "fetch() proxy URL is invalid", .{});
                                return JSPromise.dangerouslyCreateRejectedPromiseValueWithoutNotifyingVM(globalThis, err);
                            }
                            let buffer = std.fmt.allocPrint(allocator, "{s}{f}", .{ url_proxy_buffer, href.val })?;
                            url = ZigURL.parse(buffer[0..url.href.len]);
                            if (url.isFile()) {
                                url_type = URLType.file;
                            } else if (url.isBlob()) {
                                url_type = URLType.blob;
                            }
                            proxy = ZigURL.parse(buffer[url.href.len..]);
                            allocator.free(url_proxy_buffer);
                            url_proxy_buffer = buffer;

                            if (headers_value := proxy_arg.get(globalThis, "headers")?) {
                                if (!headers_value.isUndefinedOrNull()) {
                                    if (fetch_hdrs := headers_value.as(FetchHeaders)) {
                                        proxy_headers.val = Headers.from(fetch_hdrs, allocator, .{}) catch |e| bun.handleOom(e);
                                    } else if (fetch_hdrs := FetchHeaders.createFromJS(globalThis, headers_value)?) {
                                        const _fh_guard = DerefGuard<*FetchHeaders>{ val: fetch_hdrs };
                                        proxy_headers.val = Headers.from(fetch_hdrs, allocator, .{}) catch |e| bun.handleOom(e);
                                    }
                                }
                            }
                            break;
                        } else {
                            let err = globalThis.toTypeError(.INVALID_ARG_VALUE, "fetch() proxy.url must be a non-empty string", .{});
                            return JSPromise.dangerouslyCreateRejectedPromiseValueWithoutNotifyingVM(globalThis, err);
                        }
                    }
                }
            }
        }
        if (globalThis.hasException()) return JSValue.zero;
    }
    if (globalThis.hasException()) return JSValue.zero;

    // signal
    if (opts := options_object) {
        if (sig := opts.get(globalThis, "signal")?) {
            if (!sig.isUndefined()) {
                if (s := sig.as(webcore.AbortSignal)) {
                    signal = s.ref();
                }
            }
        }
        if (globalThis.hasException()) return JSValue.zero;
    }
    if (signal == null) {
        if (req := request) {
            if (s := req.signal) {
                signal = s.ref();
            }
        } else if (req_init := request_init_object) {
            if (sig := req_init.get(globalThis, "signal")?) {
                if (!sig.isUndefined()) {
                    if (s := sig.as(webcore.AbortSignal)) {
                        signal = s.ref();
                    }
                }
            }
        }
    }
    if (globalThis.hasException()) return JSValue.zero;

    // body
    let has_body = false;
    if (opts := options_object) {
        if (b := opts.fastGet(globalThis, .body)?) {
            if (!b.isUndefined()) {
                body = FetchTasklet.HTTPRequestBody.fromJS(globalThis, b)?;
                has_body = true;
            }
        }
        if (globalThis.hasException()) return JSValue.zero;
    }
    if (!has_body) {
        if (req := request) {
            let bodyValue = req.getBodyValue();
            if (bodyValue.* == .Used || (bodyValue.* == .Locked && (bodyValue.Locked.action != .none || bodyValue.Locked.isDisturbed(Request, globalThis, first_arg)))) {
                return globalThis.ERR(.BODY_ALREADY_USED, "Request body already used", .{}).throw();
            }
            if (bodyValue.* == .Locked) {
                if (readable := req.getBodyReadableStream(globalThis)) {
                    body = { ReadableStream: jsc.WebCore.ReadableStream.Strong.init(readable, globalThis) };
                    has_body = true;
                } else if (bodyValue.Locked.readable.has()) {
                    body = { ReadableStream: jsc.WebCore.ReadableStream.Strong.init(bodyValue.Locked.readable.get(globalThis).?, globalThis) };
                    has_body = true;
                } else {
                    const readable = bodyValue.toReadableStream(globalThis)?;
                    if (!readable.isEmptyOrUndefinedOrNull() && bodyValue.* == .Locked && bodyValue.Locked.readable.has()) {
                        body = { ReadableStream: jsc.WebCore.ReadableStream.Strong.init(bodyValue.Locked.readable.get(globalThis).?, globalThis) };
                        has_body = true;
                    }
                }
            }
            if (!has_body) {
                body = { AnyBlob: bodyValue.useAsAnyBlob() };
                has_body = true;
            }
        } else if (req_init := request_init_object) {
            if (b := req_init.fastGet(globalThis, .body)?) {
                if (!b.isUndefined()) {
                    body = FetchTasklet.HTTPRequestBody.fromJS(globalThis, b)?;
                }
            }
        }
    }
    if (globalThis.hasException()) return JSValue.zero;

    // headers: Headers | undefined
    let fetch_headers_to_deref = OptDerefGuard<*FetchHeaders>{ val: null };
    let fetch_headers: (*FetchHeaders)? = null;
    let headers_resolved = false;

    if (opts := options_object) {
        if (h_val := opts.fastGet(globalThis, .headers)?) {
            if (!h_val.isUndefined()) {
                headers_resolved = true;
                if (fh := h_val.as(FetchHeaders)) {
                    if (!fh.isEmpty()) {
                        fetch_headers = fh;
                    }
                } else if (fh := FetchHeaders.createFromJS(globalThis, h_val)?) {
                    fetch_headers_to_deref.val = fh;
                    fetch_headers = fh;
                }
            }
        }
        if (globalThis.hasException()) return JSValue.zero;
    }
    if (!headers_resolved) {
        if (req := request) {
            headers_resolved = true;
            if (head := req.getFetchHeadersUnlessEmpty()) {
                fetch_headers = head;
            }
        } else if (req_init := request_init_object) {
            if (h_val := req_init.fastGet(globalThis, .headers)?) {
                if (!h_val.isUndefined()) {
                    if (fh := h_val.as(FetchHeaders)) {
                        if (!fh.isEmpty()) {
                            fetch_headers = fh;
                        }
                    } else if (fh := FetchHeaders.createFromJS(globalThis, h_val)?) {
                        fetch_headers_to_deref.val = fh;
                        fetch_headers = fh;
                    }
                }
            }
        }
    }
    if (globalThis.hasException()) return JSValue.zero;

    if (fh := fetch_headers) {
        if (_hostname := fh.fastGet(FetchHeaders.HTTPHeaderName.Host)) {
            if (host := hostname) {
                hostname = null;
                allocator.free(host);
            }
            hostname = bun.handleOom(_hostname.toOwnedSliceZ(allocator));
        }
        if (url.isS3()) {
            if (_range := fh.fastGet(FetchHeaders.HTTPHeaderName.Range)) {
                if (r := range) {
                    range = null;
                    allocator.free(r);
                }
                range = bun.handleOom(_range.toOwnedSliceZ(allocator));
            }
        }
        if (_upgrade := fh.fastGet(FetchHeaders.HTTPHeaderName.Upgrade)) {
            let upgrade = DeinitGuard<ZigString.Slice>{ val: _upgrade.toSlice(default_allocator) };
            let slice = upgrade.val.slice();
            if (!bun.strings.eqlComptime(slice, "h2") && !bun.strings.eqlComptime(slice, "h2c")) {
                upgraded_connection = true;
            }
        }
        headers = Headers.from(fh, allocator, .{ .body = body.getAnyBlob() }) catch |e| bun.handleOom(e);
    }
    if (globalThis.hasException()) return JSValue.zero;

    if (url_type != .remote) {
        let path_buf: bun.PathBuffer = undefined;
        let path_buf2: bun.PathBuffer = undefined;
        let stream = std.io.fixedBufferStream(&path_buf2);
        let url_path_decoded: []const u8 = path_buf2[0 .. PercentEncoding.decode(
            @TypeOf(&stream.writer()),
            &stream.writer(),
            switch (url_type) {
                .file => url.path,
                .blob => url.href["blob:".len..],
                .remote => unreachable,
            },
        ) catch |e| {
            return globalThis.throwError(e, "Failed to decode file url");
        }];
        let url_string = DerefGuard<bun.String>{ val: bun.String.empty };
        const blob_to_use = blob: {
            if (url_type == URLType.blob) {
                if (resolved := jsc.WebCore.ObjectURLRegistry.singleton().resolveAndDupe(url_path_decoded)) {
                    url_string.val = bun.String.createFormat("blob:{s}", .{url_path_decoded}) catch |e| bun.handleOom(e);
                    break :blob resolved;
                } else {
                    const err = globalThis.toTypeError(.INVALID_ARG_VALUE, "Failed to resolve blob:{s}", .{
                        url_path_decoded,
                    });
                    return JSPromise.dangerouslyCreateRejectedPromiseValueWithoutNotifyingVM(globalThis, err);
                }
            }

            const temp_file_path = brk: {
                if (std.fs.path.isAbsolute(url_path_decoded)) {
                    if (Environment.isWindows) {
                        if (url_path_decoded[0] == '/') {
                            url_path_decoded = url_path_decoded[1..];
                        }
                        break :brk PosixToWinNormalizer.resolveCWDWithExternalBufZ(&path_buf, url_path_decoded) catch |e| {
                            return globalThis.throwError(e, "Failed to resolve file url");
                        };
                    }
                    break :brk url_path_decoded;
                }

                let cwd_buf: bun.PathBuffer = undefined;
                const cwd = if (Environment.isWindows) (bun.getcwd(&cwd_buf) catch |e| {
                    return globalThis.throwError(e, "Failed to resolve file url");
                }) else globalThis.bunVM().transpiler.fs.top_level_dir;

                let parts: [3]string = undefined;
                parts[0] = globalThis.bunVM().main;
                parts[1] = "../";
                parts[2] = url_path_decoded;
                const fullpath = bun.path.joinAbsStringBuf(
                    cwd,
                    &path_buf,
                    &parts,
                    .auto,
                );
                if (Environment.isWindows) {
                    break :brk PosixToWinNormalizer.resolveCWDWithExternalBufZ(&path_buf2, fullpath) catch |e| {
                        return globalThis.throwError(e, "Failed to resolve file url");
                    };
                }
                break :brk fullpath;
            };

            url_string.val = jsc.URL.fileURLFromString(bun.String.borrowUTF8(temp_file_path));

            let pathlike = jsc.Node.PathOrFileDescriptor{
                .path = .{
                    .encoded_slice = ZigString.Slice.init(bun.default_allocator, bun.default_allocator.dupe(u8, temp_file_path)?),
                },
            };

            break :blob Blob.findOrCreateFileFromPath(
                &pathlike,
                globalThis,
                true,
            );
        };

        const response = bun.new(Response, Response.init(
            Response.Init{
                status_code: 200,
            },
            Body{
                value: .{ Blob: blob_to_use },
            },
            url_string.val.clone(),
            false,
        ));

        return JSPromise.resolvedPromiseValue(globalThis, response.toJS(globalThis));
    }

    if (proxy != null && unix_socket_path.length() > 0) {
        let err = globalThis.toTypeError(.INVALID_ARG_VALUE, fetch_error_proxy_unix, .{});
        return JSPromise.dangerouslyCreateRejectedPromiseValueWithoutNotifyingVM(globalThis, err);
    }
    if (globalThis.hasException()) return JSValue.zero;

    if (!allow_get_body && !method.hasRequestBody() && body.hasBody() && !upgraded_connection) {
        let err = globalThis.toTypeError(.INVALID_ARG_VALUE, fetch_error_unexpected_body, .{});
        return JSPromise.dangerouslyCreateRejectedPromiseValueWithoutNotifyingVM(globalThis, err);
    }

    if (headers == null && body.hasBody() && body.hasContentTypeFromUser()) {
        headers = Headers.from(
            null,
            allocator,
            { body: body.getAnyBlob() },
        ) catch |e| bun.handleOom(e);
    }

    let promise = JSPromise.Strong.init(globalThis);
    let promise_val = promise.value();

    * = FetchTasklet.queue(
        allocator,
        globalThis,
        &.{
            .method = method,
            .url = url,
            .headers = headers ?? Headers { .allocator = allocator },
            .body = body,
            .disable_keepalive = disable_keepalive,
            .disable_timeout = disable_timeout,
            .disable_decompression = disable_decompression,
            .reject_unauthorized = reject_unauthorized,
            .redirect_type = redirect_type,
            .verbose = verbose,
            .proxy = proxy,
            .proxy_headers = proxy_headers.val,
            .url_proxy_buffer = url_proxy_buffer,
            .signal = signal,
            .globalThis = globalThis,
            .ssl_config = ssl_config,
            .hostname = hostname,
            .upgraded_connection = upgraded_connection,
            .force_http2 = force_http2,
            .force_http3 = force_http3,
            .force_http1 = force_http1,
            .check_server_identity = if (check_server_identity.isEmptyOrUndefinedOrNull()) .empty else .create(check_server_identity, globalThis),
            .unix_socket_path = unix_socket_path,
        },
        promise,
    ) catch |e| bun.handleOom(e);

    url = .{};
    headers = null;
    proxy_headers.val = null;
    body = FetchTasklet.HTTPRequestBody.Empty;
    proxy = null;
    url_proxy_buffer = "";
    signal = null;
    ssl_config = null;
    hostname = null;
    range = null;
    unix_socket_path = ZigString.Slice.empty;

    return promise_val;
}

fn setHeaders(headers: (*Headers)?, new_headers: []const picohttp.Header, allocator: std.mem.Allocator) {
    let old = headers.*;
    headers.* = bun.handleOom(Headers.fromPicoHttpHeaders(new_headers, allocator));
    if (*h := old) {
        h.deinit();
    }
}
