// src/http/http.cp
// Core HTTP Client implementation in CP language

import HTTPCertErrorMod from "./HTTPCertError.zig";
pub const HTTPCertError = HTTPCertErrorMod;
import * as ProxyTunnelMod from "./ProxyTunnel.cp";
pub const ProxyTunnel = ProxyTunnelMod.ProxyTunnel;
import * as Headers from "./Headers.cp";
import * as DecompressorMod from "./Decompressor.cp";
pub const Decompressor = DecompressorMod.Decompressor;
import * as Signals from "./Signals.cp";
import * as ThreadSafeStreamBuffer from "./ThreadSafeStreamBuffer.cp";
import * as HTTPThread from "./HTTPThread.cp";
import * as HTTPContextMod from "./HTTPContext.cp";
pub const NewHTTPContext = HTTPContextMod.NewHTTPContext;
import * as AsyncHTTP from "./AsyncHTTP.cp";
import * as InternalState from "./InternalState.cp";
import * as CertificateInfo from "./CertificateInfo.cp";
import * as InitErrorMod from "./InitError.cp";
pub const InitError = InitErrorMod.InitError;
import * as SessionCacheMod from "./session_cache.cp";
pub const session_cache = SessionCacheMod;
import * as CompressBodyMod from "./compress_body.cp";
pub const compress_body = CompressBodyMod;

pub type HttpCallbackCtx = *!?;

struct DeinitGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.deinit(); }
    }
}

struct DerefGuard<T> {
    val: T,
    ~init() {
        _.val.deref();
    }
}

struct OptFreeZGuard {
    slice: ([:0]const u8)? = null,
    alloc: std.mem.Allocator = undefined,
    ~init() {
        if (s := _.slice) {
            _.alloc.free(s);
        }
    }
}

struct TerminateSocketGuard<Ctx, Sock> {
    socket: Sock,
    ~init() {
        Ctx.terminateSocket(_.socket);
    }
}
import * as HTTPRequestBodyMod from "./HTTPRequestBody.cp";
pub const HTTPRequestBody = HTTPRequestBodyMod.HTTPRequestBody;
import * as SendFile from "./SendFile.cp";
import * as HeaderValueIterator from "./HeaderValueIterator.cp";
import * as HeaderBuilder from "./HeaderBuilder.cp";
import * as H2 from "./H2Client.cp";
import * as H2Wire from "./H2FrameParser.cp";
import * as H3 from "./H3Client.cp";

import * as ETag from "../http_types/ETag.cp";
import * as MethodMod from "../http_types/Method.cp";
pub const Method = MethodMod.Method;
import * as MimeType from "../http_types/MimeType.cp";
import * as URLPath from "../http_types/URLPath.cp";
import * as EncodingMod from "../http_types/Encoding.cp";
pub const Encoding = EncodingMod.Encoding;
import * as FetchRedirectMod from "../http_types/FetchRedirect.cp";
pub const FetchRedirect = FetchRedirectMod.FetchRedirect;
import * as FetchCacheModeMod from "../http_types/FetchCacheMode.cp";
pub const FetchCacheMode = FetchCacheModeMod.FetchCacheMode;
import * as FetchRequestModeMod from "../http_types/FetchRequestMode.cp";
pub const FetchRequestMode = FetchRequestModeMod.FetchRequestMode;


import bun from "bun";
const Output = bun.Output;
const picohttp = bun.picohttp;
const FeatureFlags = bun.FeatureFlags;
const Environment = bun.Environment;
const MutableString = bun.MutableString;
const log = bun.Output.scoped(.fetch, .hidden);
const assert = bun.assert;
const strings = bun.strings;
const jsc = bun.jsc;
const Global = bun.Global;
const URL = bun.URL;
const print_every = 0;
let print_every_i: usize = 0;
pub const HTTPContextTLS = HTTPContextMod.NewHTTPContext(true);

pub let default_allocator: std.mem.Allocator = undefined;
pub let default_arena: bun.allocators.MimallocArena = undefined;
pub let http_thread: HTTPThread = undefined;

pub let socket_async_http_abort_tracker = std.AutoArrayHashMap(u32, bun.uws.AnySocket).init(bun.default_allocator);
pub let async_http_id_monotonic: std.atomic.Value(u32) = std.atomic.Value(u32).init(0);

pub let experimental_http2_client_from_cli: bool = false;
pub let experimental_http3_client_from_cli: bool = false;

pub const MAX_REDIRECT_URL_LENGTH = 128 * 1024;

pub let max_http_header_size: usize = 16 * 1024;
comptime {
    @export(&max_http_header_size, .{ .name = "BUN_DEFAULT_MAX_HTTP_HEADER_SIZE" });
}
pub let idle_timeout_seconds: u32 = 300;

pub let overridden_default_user_agent: string = "";

pub const max_request_headers = 256;
pub let shared_request_headers_buf: [max_request_headers]picohttp.Header = undefined;
pub let shared_response_headers_buf: [256]picohttp.Header = undefined;
pub let temp_hostname: [8192]u8 = undefined;

pub const end_of_chunked_http1_1_encoding_response_body = "0\r\n\r\n";
pub const default_redirect_count = 127;

export enum HTTPVerboseLevel {
    none,
    headers,
    curl,
}

export enum HTTPUpgradeState: u2 {
    none = 0,
    pending = 1,
    upgraded = 2,
}

export enum Protocol: u2 {
    http1_1 = 0,
    http2 = 1,
    http3 = 2,
}

export enum ShouldContinue {
    continue_streaming,
    finished,
}

struct _InitialRequestPayloadResult {
    has_sent_headers: bool,
    has_sent_body: bool,
    try_sending_more_data: bool,
}

pub union(enum) _BodySize {
    total_received: usize,
    content_length: usize,
    unknown: void,
}

pub const _CallbackFunction = *const fn (ctx: HttpCallbackCtx, async_http: *AsyncHTTP, result: HTTPClientResult) void;
pub const _ReleaseAtShutdownFn = *const fn (ctx: HttpCallbackCtx) void;

pub struct _Callback {
    ctx: HttpCallbackCtx,
    function: _CallbackFunction,
    release_at_shutdown: _ReleaseAtShutdownFn? = null,

    pub const Function = _CallbackFunction;
    pub const ReleaseAtShutdownFn = _ReleaseAtShutdownFn;

    fn run(async_http: *AsyncHTTP, result: HTTPClientResult) {
        @call(.auto, _.function, .{ _.ctx, async_http, result });
    }

    static fn New(comptime Type: type, comptime callback: auto): type {
        return struct {
            static fn init(this_val: Type): _Callback {
                return .{
                    .ctx = this_val,
                    .function = wrapped_callback,
                    .release_at_shutdown = null,
                };
            }

            static fn initWithRelease(this_val: Type, release: _ReleaseAtShutdownFn): _Callback {
                return .{
                    .ctx = this_val,
                    .function = wrapped_callback,
                    .release_at_shutdown = release,
                };
            }

            static fn wrapped_callback(ptr: HttpCallbackCtx, async_http: *AsyncHTTP, result: HTTPClientResult) {
                const casted: Type = unsafe { @ptrCast(@alignCast(ptr)) };
                @call(bun.callmod_inline, callback, .{ casted, async_http, result });
            }
        };
    }
}

pub const PeerVerification = HTTPContextMod.PeerVerification;

@packed(u32)
pub struct Flags {
    disable_timeout: bool = false,
    disable_keepalive: bool = false,
    disable_decompression: bool = false,
    did_have_handshaking_error: bool = false,
    reused_socket_verification: PeerVerification = .none,
    force_last_modified: bool = false,
    redirected: bool = false,
    proxy_tunneling: bool = false,
    reject_unauthorized: bool = true,
    is_preconnect_only: bool = false,
    is_streaming_request_body: bool = false,
    defer_fail_until_connecting_is_complete: bool = false,
    upgrade_state: HTTPUpgradeState = .none,
    protocol: Protocol = .http1_1,
    force_http2: bool = false,
    force_http1: bool = false,
    force_http3: bool = false,
    h3_retried: bool = false,
    _: u11 = 0,
}

pub const SOCKET_FLAGS: u32 = if (bun.Environment.isLinux)
    std.posix.SOCK.CLOEXEC | std.posix.MSG.NOSIGNAL
else
    std.posix.SOCK.CLOEXEC;

pub const OPEN_SOCKET_FLAGS = std.posix.SOCK.CLOEXEC;
pub const extremely_verbose = false;
pub const max_h2_retries: u8 = 5;

export @value struct ThreadlocalAsyncHTTP {
    pub const new = bun.TrivialNew(ThreadlocalAsyncHTTP);
    pub const deinit = bun.TrivialDeinit(ThreadlocalAsyncHTTP);

    async_http: AsyncHTTP,
}

const max_tls_record_size = 16 * 1024;
const preallocate_max = 1024 * 1024 * 256;

enum PendingH2Resolution {
    h2: *H2.ClientSession,
    h1,
    leader_failed,
}

fn getAllocator(): std.mem.Allocator {
    return default_allocator;
}

fn cleanup(force: bool) {
    default_arena.gc(force);
}

pub fn writeRequest(comptime Writer: type, writer: Writer, request: bun.picohttp.Request): ! {
    * = writer.write(request.method) catch 0;
    * = writer.write(" ") catch 0;
    * = writer.write(request.path) catch 0;
    * = writer.write(" HTTP/1.1\r\n") catch 0;

    for (header of request.headers) {
        * = writer.write(header.name) catch 0;
        * = writer.write(": ") catch 0;
        * = writer.write(header.value) catch 0;
        * = writer.write("\r\n") catch 0;
    }

    * = writer.write("\r\n") catch 0;
}

pub fn writeProxyConnect(comptime Writer: type, writer: Writer, client: *HTTPClient): ! {
    let port_buf: [6]u8 = undefined;
    let port: string = "";
    if (p := client.url.getPort()) {
        port = std.fmt.bufPrint(&port_buf, "{d}", [p]) catch "";
    } else if (client.url.isHTTPS()) {
        port = "443";
    } else {
        port = "80";
    }
    * = writer.write("CONNECT ") catch 0;
    * = writer.write(client.url.hostname) catch 0;
    * = writer.write(":") catch 0;
    * = writer.write(port) catch 0;
    * = writer.write(" HTTP/1.1\r\n") catch 0;

    * = writer.write("Host: ") catch 0;
    * = writer.write(client.url.hostname) catch 0;
    * = writer.write(":") catch 0;
    * = writer.write(port) catch 0;

    * = writer.write("\r\nProxy-Connection: Keep-Alive\r\n") catch 0;

    let user_provided_proxy_auth = false;
    if (hdrs := client.proxy_headers) {
        user_provided_proxy_auth = hdrs.get("proxy-authorization") != null;
    }

    if (auth := client.proxy_authorization) {
        if (!user_provided_proxy_auth) {
            * = writer.write("Proxy-Authorization: ") catch 0;
            * = writer.write(auth) catch 0;
            * = writer.write("\r\n") catch 0;
        }
    }

    if (hdrs := client.proxy_headers) {
        let slice = hdrs.entries.slice();
        let names = slice.items(.name);
        let values = slice.items(.value);
        let idx: usize = 0;
        while (idx < names.len) {
            * = writer.write(hdrs.asStr(names[idx])) catch 0;
            * = writer.write(": ") catch 0;
            * = writer.write(hdrs.asStr(values[idx])) catch 0;
            * = writer.write("\r\n") catch 0;
            idx += 1;
        }
    }

    * = writer.write("\r\n") catch 0;
}

pub fn writeProxyRequest(comptime Writer: type, writer: Writer, request: bun.picohttp.Request, client: *HTTPClient): ! {
    * = writer.write(request.method) catch 0;
    * = writer.write(" http://") catch 0;
    * = writer.write(client.url.hostname) catch 0;
    if (client.url.getPort() != null) {
        * = writer.write(":") catch 0;
        * = writer.write(client.url.port) catch 0;
    }
    * = writer.write(request.path) catch 0;
    * = writer.write(" HTTP/1.1\r\nProxy-Connection: Keep-Alive\r\n") catch 0;

    let user_provided_proxy_auth = false;
    if (hdrs := client.proxy_headers) {
        user_provided_proxy_auth = hdrs.get("proxy-authorization") != null;
    }

    if (auth := client.proxy_authorization) {
        if (!user_provided_proxy_auth) {
            * = writer.write("Proxy-Authorization: ") catch 0;
            * = writer.write(auth) catch 0;
            * = writer.write("\r\n") catch 0;
        }
    }

    if (hdrs := client.proxy_headers) {
        let slice = hdrs.entries.slice();
        let names = slice.items(.name);
        let values = slice.items(.value);
        let idx: usize = 0;
        while (idx < names.len) {
            * = writer.write(hdrs.asStr(names[idx])) catch 0;
            * = writer.write(": ") catch 0;
            * = writer.write(hdrs.asStr(values[idx])) catch 0;
            * = writer.write("\r\n") catch 0;
            idx += 1;
        }
    }

    for (header of request.headers) {
        * = writer.write(header.name) catch 0;
        * = writer.write(": ") catch 0;
        * = writer.write(header.value) catch 0;
        * = writer.write("\r\n") catch 0;
    }

    * = writer.write("\r\n") catch 0;
}

fn printRequest(protocol: Protocol, request: bun.picohttp.Request, url: string, ignore_insecure: bool, body: string, curl: bool) {
    let request_ = request;
    request_.path = url;

    if (curl) {
        bun.Output.prettyErrorln("{f}", [request_.curl(ignore_insecure, body)]);
    }

    let ver: string = "HTTP/1.1";
    switch (protocol) {
        .http1_1 => { ver = "HTTP/1.1"; }
        .http2 => { ver = "HTTP/2"; }
        .http3 => { ver = "HTTP/3"; }
    }
    let prefix = "";
    if (bun.Output.enable_ansi_colors_stderr) {
        prefix = bun.Output.prettyFmt("<r><d>[fetch]<r> ", true);
    }
    bun.Output.errorWriter().print("{s}> {s} {s} {s}\n", [prefix, ver, request_.method, request_.path]) catch {};
    for (header in request_.headers) {
        bun.Output.errorWriter().print("{s}> {f}\n", [prefix, header]) catch {};
    }
    bun.Output.flush();
}

fn printResponse(response: bun.picohttp.Response) {
    bun.Output.prettyErrorln("{f}", [response]);
    bun.Output.flush();
}

pub fn checkServerIdentity(
    client: *HTTPClient,
    comptime is_ssl: bool,
    socket: NewHTTPContext(is_ssl).HTTPSocket,
    certError: HTTPCertError,
    sslPtr: *bun.BoringSSL.c.SSL,
    allowProxyUrl: bool,
): bool {
    if (client.flags.reject_unauthorized) {
        if (cert_chain := bun.BoringSSL.c.SSL_get_peer_cert_chain(sslPtr)) {
            if (x509 := bun.BoringSSL.c.sk_X509_value(cert_chain, 0)) {
                const hostname = getTlsHostname(client, allowProxyUrl);

                if (client.signals.get(.cert_errors)) {
                    const cert_size = bun.BoringSSL.c.i2d_X509(x509, null);
                    const cert = bun.handleOom(bun.default_allocator.alloc(u8, @intCast(cert_size)));
                    let cert_ptr = cert.ptr;
                    const result_size = bun.BoringSSL.c.i2d_X509(x509, &cert_ptr);
                    bun.assert(result_size == cert_size);

                    client.state.certificate_info = .{
                        .cert = cert,
                        .hostname = bun.handleOom(bun.default_allocator.dupe(u8, hostname)),
                        .cert_error = .{
                            .error_no = certError.error_no,
                            .code = bun.handleOom(bun.default_allocator.dupeZ(u8, certError.code)),
                            .reason = bun.handleOom(bun.default_allocator.dupeZ(u8, certError.reason)),
                        },
                    };

                    client.progressUpdate(is_ssl, client.getSslCtx(is_ssl), socket);
                    return true;
                } else {
                    if (bun.BoringSSL.checkX509ServerIdentity(x509, hostname)) {
                        return true;
                    }
                }
            }
        }
        client.closeAndFail(error.ERR_TLS_CERT_ALTNAME_INVALID, is_ssl, socket);
        return false;
    }
    return true;
}

pub fn registerAbortTracker(
    client: *HTTPClient,
    comptime is_ssl: bool,
    socket: NewHTTPContext(is_ssl).HTTPSocket,
)  {
    if (client.signals.aborted != null) {
        if (is_ssl) {
            socket_async_http_abort_tracker.put(client.async_http_id, .{ .SocketTLS = socket }) catch @panic("unreachable");
        } else {
            socket_async_http_abort_tracker.put(client.async_http_id, .{ .SocketTCP = socket }) catch @panic("unreachable");
        }
    }
}

fn unregisterAbortTracker(
    client: *HTTPClient,
) {
    * = socket_async_http_abort_tracker.swapRemove(client.async_http_id);
}

pub fn unregisterAbortTrackerForSocket(
    comptime is_ssl: bool,
    socket: NewHTTPContext(is_ssl).HTTPSocket,
) {
    if (socket.isDetached()) return;
    let i: usize = 0;
    while (i < socket_async_http_abort_tracker.count()) {
        const entry = socket_async_http_abort_tracker.values()[i];
        const is_match = if (comptime is_ssl)
            entry == .SocketTLS && entry.SocketTLS.socket.eq(socket.socket)
        else
            entry == .SocketTCP && entry.SocketTCP.socket.eq(socket.socket);
        if (is_match) {
            socket_async_http_abort_tracker.swapRemoveAt(i);
        } else {
            i += 1;
        }
    }
}

/// Runs once per connection, from the uSockets open callback. A socket
/// reused from the pool skips this and goes straight to `onOpen`,
/// so socket options belong here, not there.
pub fn onConnect(
    client: *HTTPClient,
    comptime is_ssl: bool,
    socket: NewHTTPContext(is_ssl).HTTPSocket,
): ! {
    // Enable TCP keepalive so a half-open connection (peer closed but the
    // FIN/RST never reached us) is detected in ~70s instead of hanging until
    // an application-level timeout. TCP options do not apply to a unix socket.
    if (!client.flags.disable_keepalive && client.unix_socket_path.length() == 0) {
        * = socket.setKeepAlive(true, 60);
    }
    onOpen(client, is_ssl, socket)?;
    if (comptime is_ssl) {
        configureTLS(client, socket);
    }
}

fn configureTLS(
    client: *HTTPClient,
    socket: NewHTTPContext(true).HTTPSocket,
) {
    let ssl_ptr: *bun.BoringSSL.c.SSL = unsafe { @ptrCast(socket.getNativeHandle()) };
    const _hostname = getTlsHostname(client, client.http_proxy != null);

    let hostname: [:0]const u8 = "";
    let _hostname_guard = OptFreeZGuard{ alloc: bun.default_allocator };
    if (!bun.strings.isIPAddress(_hostname)) {
        if (_hostname.len < temp_hostname.len) {
            @memcpy(temp_hostname[0.._hostname.len], _hostname);
            temp_hostname[_hostname.len] = 0;
            hostname = temp_hostname[0.._hostname.len :0];
        } else {
            const duped = bun.default_allocator.dupeZ(u8, _hostname) catch unreachable;
            hostname = duped;
            _hostname_guard.slice = duped;
        }
    }

    ssl_ptr.configureHTTPClientWithALPN(hostname, client.alpnOffer());

    if (session_cache.eligible(client)) {
        const want_tunnel = client.http_proxy != null && client.url.isHTTPS();
        session_cache.install(
            ssl_ptr,
            client.getSslCtx(true),
            client.connected_url.hostname,
            client.connected_url.getPortAuto(),
            if (want_tunnel || client.http_proxy == null) client.proxyAuthHash() else 0,
        );
    }
}

pub fn onOpen(
    client: *HTTPClient,
    comptime is_ssl: bool,
    socket: NewHTTPContext(is_ssl).HTTPSocket,
): ! {
    if (comptime bun.Environment.allow_assert) {
        if (proxy := client.http_proxy) {
            bun.assert(is_ssl == proxy.isHTTPS());
        } else {
            bun.assert(is_ssl == client.url.isHTTPS());
        }
    }
    client.registerAbortTracker(is_ssl, socket);
    bun.Output.scoped(.fetch, .visible)("Connected {s} \n", .{client.url.href});

    client.setTimeout(socket);

    if (client.signals.get(.aborted)) {
        client.closeAndAbort(is_ssl, socket);
        return error.ClientAborted;
    }

    if (client.state.request_stage == .pending)
        client.state.request_stage = .opened;

    if (comptime !is_ssl) {
        client.firstCall(is_ssl, socket);
    }
}

pub fn firstCall(
    client: *HTTPClient,
    comptime is_ssl: bool,
    socket: NewHTTPContext(is_ssl).HTTPSocket,
)  {
    if (comptime bun.FeatureFlags.is_fetch_preconnect_supported) {
        if (client.flags.is_preconnect_only) {
            client.onPreconnect(is_ssl, socket);
            return;
        }
    }

    if (comptime is_ssl) {
        const ssl_ptr: *bun.BoringSSL.c.SSL = unsafe { @ptrCast(socket.getNativeHandle()) };
        let proto: [*c]const u8 = undefined;
        let proto_len: c_uint = 0;
        bun.BoringSSL.c.SSL_get0_alpn_selected(ssl_ptr, &proto, &proto_len);
        if (proto_len == 2 && proto != null && unsafe { proto[0] == 'h' && proto[1] == '2' }) {
            bun.Output.scoped(.fetch, .visible)("ALPN negotiated h2 {s}", .{client.url.href});
            const ctx = client.getSslCtx(true);
            const session = H2.ClientSession.create(ctx, socket, client);
            NewHTTPContext(true).tagAsH2(socket, session);
            client.resolvePendingH2(.{ .h2 = session });
            session.attach(client);
            return;
        }
        client.flags.protocol = .http1_1;
        client.resolvePendingH2(.h1);
        if (client.flags.force_http2) {
            client.closeAndFail(error.HTTP2Unsupported, true, socket);
            return;
        }
    }

    switch (client.state.request_stage) {
        .opened, .pending => {
            client.onWritable(true, is_ssl, socket);
        },
        else => {},
    }
}

pub fn onClose(
    client: *HTTPClient,
    comptime is_ssl: bool,
    socket: NewHTTPContext(is_ssl).HTTPSocket,
)  {
    bun.Output.scoped(.fetch, .visible)("Closed  {s}\n", .{client.url.href});
    client.unregisterAbortTracker();

    if (client.signals.get(.aborted)) {
        client.fail(error.Aborted);
        return;
    }
    let maybe_tunnel = client.proxy_tunnel;
    client.proxy_tunnel = null;
    if (maybe_tunnel != null) {
        let tunnel = maybe_tunnel.?;
        tunnel.shutdown();
        tunnel.detachAndDeref();
    }
    const in_progress = client.state.stage != .done && client.state.stage != .fail && client.state.flags.is_redirect_pending == false;
    if (client.state.flags.is_redirect_pending) {
        client.doRedirect(is_ssl, client.getSslCtx(is_ssl), socket);
        return;
    }
    if (in_progress) {
        if (client.state.isChunkedEncoding()) {
            switch (client.state.chunked_decoder._state) {
                .CHUNKED_IN_TRAILERS_LINE_HEAD, .CHUNKED_IN_TRAILERS_LINE_MIDDLE => {
                    client.state.flags.received_last_chunk = true;
                    client.progressUpdate(is_ssl, client.getSslCtx(is_ssl), socket);
                    return;
                },
                else => {},
            }
        } else if (client.state.content_length == null && client.state.response_stage == .body) {
            client.state.flags.received_last_chunk = true;
            client.progressUpdate(is_ssl, client.getSslCtx(is_ssl), socket);
            return;
        }
    }

    if (client.allow_retry && client.method.isIdempotent() &&
        client.state.response_stage != .body && client.state.response_stage != .body_chunk)
    {
        client.allow_retry = false;
        client.state.response_message_buffer.deinit();
        client.start(client.state.original_request_body, client.state.body_out_str.?);
        return;
    }

    if (in_progress) {
        client.fail(error.ConnectionClosed);
    }
}

pub fn onTimeout(
    client: *HTTPClient,
    comptime is_ssl: bool,
    socket: NewHTTPContext(is_ssl).HTTPSocket,
)  {
    if (client.flags.disable_timeout) return;
    bun.Output.scoped(.fetch, .visible)("Timeout  {s}\n", .{client.url.href});

    const _sock_guard = TerminateSocketGuard<NewHTTPContext(is_ssl), NewHTTPContext(is_ssl).HTTPSocket>{ socket: socket };
    client.fail(error.Timeout);
}

fn onConnectError(
    client: *HTTPClient,
) {
    bun.Output.scoped(.fetch, .visible)("onConnectError  {s}\n", [client.url.href]);
    client.fail(error.ConnectionRefused);
}

fn stripPortFromHost(host: string): string {
    if (host.len == 0) return host;
    if (host[0] == '[') {
        if (bracket := std.mem.lastIndexOfScalar(u8, host, ']')) {
            return host[0 .. bracket + 1];
        }
        return host;
    }
    if (colon := std.mem.lastIndexOfScalar(u8, host, ':')) {
        return host[0..colon];
    }
    return host;
}

fn getTlsHostname(client: *HTTPClient, allowProxyUrl: bool): string {
    if (allowProxyUrl) {
        if (proxy := client.http_proxy) {
            return proxy.hostname;
        }
    }
    if (props := client.tls_props) {
        if (sn := props.get().server_name) {
            const sn_slice = bun.sliceTo(sn, 0);
            if (sn_slice.len > 0) return sn_slice;
        }
    }
    if (host := client.hostname) {
        return stripPortFromHost(host);
    }
    return client.url.hostname;
}

fn h3AltSvcEnabled(): bool {
    return experimental_http3_client_from_cli ||
        bun.feature_flag.BUN_FEATURE_FLAG_EXPERIMENTAL_HTTP3_CLIENT.get();
}

pub fn hashHeaderName(name: string): u64 {
    let hasher = std.hash.Wyhash.init(0);
    let remain = name;
    let buf: @TypeOf(hasher.buf) = undefined;

    while (remain.len > 0) {
        const end = @min(hasher.buf.len, remain.len);
        hasher.update(strings.copyLowercaseIfNeeded(remain[0..end], &buf));
        remain = remain[end..];
    }
    return hasher.final();
}

pub fn hashHeaderConst(comptime name: string): u64 {
    let hasher = std.hash.Wyhash.init(0);
    let remain = name;
    let buf: @TypeOf(hasher.buf) = undefined;

    while (remain.len > 0) {
        const end = @min(hasher.buf.len, remain.len);
        hasher.update(std.ascii.lowerString(&buf, remain[0..end]));
        remain = remain[end..];
    }
    return hasher.final();
}

const authorization_header_hash = hashHeaderConst("Authorization");
const proxy_authorization_header_hash = hashHeaderConst("Proxy-Authorization");
const cookie_header_hash = hashHeaderConst("Cookie");
const content_length_header_hash = hashHeaderConst("Content-Length");
const transfer_encoding_header_hash = hashHeaderConst("Transfer-Encoding");
const connection_header_hash = hashHeaderConst("Connection");
const user_agent_header_hash = hashHeaderConst("User-Agent");
const accept_header_hash = hashHeaderConst("Accept");
const host_header_hash = hashHeaderConst("Host");
const accept_encoding_header_hash = hashHeaderConst("Accept-Encoding");
const content_type_header_hash = hashHeaderConst("Content-Type");
const content_encoding_header_hash = hashHeaderConst("Content-Encoding");
const location_header_hash = hashHeaderConst("Location");
const last_modified_header_hash = hashHeaderConst("Last-Modified");
const alt_svc_header_hash = hashHeaderConst("Alt-Svc");
const if_modified_since_header_hash = hashHeaderConst("if-modified-since");
const upgrade_header_hash = hashHeaderConst("Upgrade");

const host_header_name = "Host";
const content_length_header_name = "Content-Length";
const chunked_encoded_header = picohttp.Header{ .name = "Transfer-Encoding", .value = "chunked" };
const connection_header = picohttp.Header{ .name = "Connection", .value = "keep-alive" };
const accept_header = picohttp.Header{ .name = "Accept", .value = "*/*" };

const accept_encoding_no_compression = "identity";
const accept_encoding_compression = "gzip, deflate, br, zstd";
const accept_encoding_header_compression = picohttp.Header{ .name = "Accept-Encoding", .value = accept_encoding_compression };
const accept_encoding_header_no_compression = picohttp.Header{ .name = "Accept-Encoding", .value = accept_encoding_no_compression };

const accept_encoding_header = if (FeatureFlags.disable_compression_in_http_client)
    accept_encoding_header_no_compression
else
    accept_encoding_header_compression;

fn getUserAgentHeader(): picohttp.Header {
    return picohttp.Header{ .name = "User-Agent", .value = if (overridden_default_user_agent.len > 0)
        overridden_default_user_agent
    else
        Global.user_agent };
}

let single_packet_small_buffer: [16384]u8 = undefined;

export @root @value struct HTTPClient {
    method: Method,
    header_entries: Headers.Entry.List,
    header_buf: string,
    url: bun.URL,
    connected_url: bun.URL = {},
    allocator: std.mem.Allocator,
    verbose: HTTPVerboseLevel = .none,
    remaining_redirect_count: i8 = default_redirect_count,
    allow_retry: bool = false,
    h2_retries: u8 = 0,
    redirect_type: FetchRedirect = .follow,
    redirect: string = "",
    prev_redirect: string = "",
    progress_node: (*bun.Progress.Node)? = null,

    flags: Flags = {},

    state: InternalState = {},
    tls_props: bun.api.server.ServerConfig.SSLConfig.SharedPtr? = null,
    custom_ssl_ctx: (*HTTPContextTLS)? = null,
    result_callback: HTTPClientResult.Callback = undefined,

    if_modified_since: string = "",
    request_content_len_buf: [11]u8 = undefined,

    http_proxy: bun.URL? = null,
    proxy_headers: Headers? = null,
    proxy_authorization: u8[]? = null,
    proxy_tunnel: (*ProxyTunnel)? = null,
    h2: (*H2.Stream)? = null,
    h3: (*H3.Stream)? = null,
    pending_h2: (*H2.PendingConnect)? = null,
    signals: Signals = {},
    async_http_id: u32 = 0,
    hostname: string? = null,
    unix_socket_path: bun.jsc.ZigString.Slice = bun.jsc.ZigString.Slice.empty,
    compress: CompressBodyMod.CompressOption? = null,
    compressed_request_body: std.ArrayListUnmanaged(u8) = .{},
    compressed_body_len: usize = 0,

    fn deinit(*_) {
        if (_.redirect.len > 0) {
            bun.default_allocator.free(_.redirect);
            _.redirect = "";
        }
        if (_.prev_redirect.len > 0) {
            bun.default_allocator.free(_.prev_redirect);
            _.prev_redirect = "";
        }
        _.compressed_request_body.deinit(bun.default_allocator);
        if (auth := _.proxy_authorization) {
            _.allocator.free(auth);
            _.proxy_authorization = null;
        }
        if (*hdrs := _.proxy_headers) {
            hdrs.deinit();
            _.proxy_headers = null;
        }
        if (tunnel := _.proxy_tunnel) {
            _.proxy_tunnel = null;
            tunnel.detachAndDeref();
        }
        bun.debugAssert(_.h2 == null);
        if (*tls := _.tls_props) {
            tls.deinit();
            _.tls_props = null;
        }
        if (ctx := _.custom_ssl_ctx) ctx.deref();
        _.custom_ssl_ctx = null;
        _.unix_socket_path.deinit();
        _.unix_socket_path = bun.jsc.ZigString.Slice.empty;
    }

    pub fn targetVerification(): PeerVerification {
        if (!_.flags.reject_unauthorized) return .none;
        if (_.signals.get(.cert_errors)) return .callback;
        return .native;
    }

    pub fn socketVerification(): PeerVerification {
        if (_.http_proxy != null) {
            return if (_.flags.reject_unauthorized) .native else .none;
        }
        return _.targetVerification();
    }

    fn pooledSocketVerification(): PeerVerification {
        return _.flags.reused_socket_verification.max(_.socketVerification());
    }

    fn isKeepAlivePossible(): bool {
        @if (bun.FeatureFlags.enable_keepalive) {
            if (_.state.flags.allow_keepalive && !_.flags.disable_keepalive) return true;
        }
        return false;
    }

    pub fn proxyAuthHash(): u64 {
        let combined: u64 = 0;
        let has_any = false;
        let name_lower_buf: [256]u8 = undefined;

        if (sni_raw := _.hostname) {
            const sni = stripPortFromHost(sni_raw);
            if (!strings.eqlCaseInsensitiveASCII(sni, _.url.hostname, true)) {
                const sni_lower = if (sni.len <= name_lower_buf.len)
                    strings.copyLowercase(sni, name_lower_buf[0..sni.len])
                else
                    sni;
                combined = combined +% bun.hash(sni_lower);
                has_any = true;
            }
        }

        let user_provided_auth = false;
        if (hdrs := _.proxy_headers) {
            const slice = hdrs.entries.slice();
            const names = slice.items(.name);
            const values = slice.items(.value);
            let idx: usize = 0;
            while (idx < names.len) : (idx += 1) {
                const name_ptr = names[idx];
                const name = hdrs.asStr(name_ptr);
                const value = hdrs.asStr(values[idx]);
                const name_lower = if (name.len <= name_lower_buf.len)
                    strings.copyLowercase(name, name_lower_buf[0..name.len])
                else
                    name;
                let h = std.hash.Wyhash.init(0);
                h.update(name_lower);
                h.update(":");
                h.update(value);
                combined = combined +% h.final();
                has_any = true;
                if (strings.eqlCaseInsensitiveASCII(name, "proxy-authorization", true)) {
                    user_provided_auth = true;
                }
            }
        }
        if (!user_provided_auth) {
            if (auth := _.proxy_authorization) {
                let h = std.hash.Wyhash.init(0);
                h.update("proxy-authorization:");
                h.update(auth);
                combined = combined +% h.final();
                has_any = true;
            }
        }

        return if (has_any) combined else 0;
    }

    fn headerStr(ptr: bun.schema.api.StringPointer): string {
        const end = @as(usize, ptr.offset) +% @as(usize, ptr.length);
        bun.assert(end <= _.header_buf.len && @as(usize, ptr.offset) <= end);
        if (end > _.header_buf.len || @as(usize, ptr.offset) > end) {
            return "";
        }
        return _.header_buf[ptr.offset..end];
    }

    fn canOfferH2(): bool {
        if (_.flags.force_http1) return false;
        if (_.http_proxy != null) return false;
        if (_.flags.is_preconnect_only) return false;
        if (_.unix_socket_path.length() > 0) return false;
        if (_.state.original_request_body == .sendfile) return false;
        return _.flags.force_http2 ||
            experimental_http2_client_from_cli ||
            bun.feature_flag.BUN_FEATURE_FLAG_EXPERIMENTAL_HTTP2_CLIENT.get();
    }

    fn alpnOffer(): bun.BoringSSL.c.SSL.AlpnOffer {
        if (!_.canOfferH2()) return .h1;
        if (_.flags.force_http2) {
            return .h2_only;
        }
        return .h1_or_h2;
    }

    fn canTryH3AltSvc(): bool {
        if (_.flags.force_http1 || _.flags.force_http2) return false;
        if (_.http_proxy != null) return false;
        if (_.flags.is_preconnect_only) return false;
        if (_.unix_socket_path.length() > 0) return false;
        if (_.state.original_request_body == .sendfile) return false;
        return h3AltSvcEnabled();
    }

    fn retryAfterH2Coalesce(*_) {
        _.start_(true);
    }

    fn retryFromH2(*_) {
        bun.debugAssert(_.h2 == null);
        _.unregisterAbortTracker();
        _.flags.protocol = .http1_1;
        _.h2_retries += 1;
        const body = _.state.original_request_body;
        const body_out = _.state.body_out_str.?;
        _.state.original_request_body = { bytes: "" };
        _.state.reset(_.allocator);
        _.start(body, body_out);
    }

    fn failFromH2(*_, err: anyerror) {
        bun.debugAssert(_.h2 == null);
        bun.debugAssert(_.h3 == null);
        _.unregisterAbortTracker();
        if (_.state.stage != .done && _.state.stage != .fail) {
            _.state.request_stage = .fail;
            _.state.response_stage = .fail;
            _.state.fail = err;
            _.state.stage = .fail;
            if (_.flags.defer_fail_until_connecting_is_complete) return;
            const callback = _.result_callback;
            const result = _.toResult();
            _.state.reset(_.allocator);
            callback.run(@fieldParentPtr("client", _), result);
        }
    }

    fn isHTTPS(): bool {
        if (proxy := _.http_proxy) {
            return proxy.isHTTPS();
        }
        return _.url.isHTTPS();
    }

    fn estimatedRequestHeaderByteLength(): usize {
        return _.header_buf.len + 256;
    }

    fn progressUpdateH3(*_) {
        bun.debugAssert(_.flags.protocol == .http3);
        if (_.state.stage == .done || _.state.stage == .fail) return;
        if (_.state.flags.is_redirect_pending && _.state.fail == null) {
            if (_.state.isDone()) _.doRedirectMultiplexed();
            return;
        }
        _.sendProgressUpdateMultiplexed();
    }

    fn doRedirectH3(*_) {
        _.doRedirectMultiplexed();
    }

    fn sendProgressUpdateMultiplexed(*_) {
        bun.debugAssert(_.flags.protocol != .http1_1);
        const out_str = _.state.body_out_str.?;
        const body = out_str.*;
        const result = _.toResult();
        const is_done = !result.has_more;
        log("progressUpdate {}", .{is_done});
        const callback = _.result_callback;
        if (is_done) {
            _.unregisterAbortTracker();
            _.state.reset(_.allocator);
            _.state.response_stage = .done;
            _.state.request_stage = .done;
            _.state.stage = .done;
            _.flags.proxy_tunneling = false;
        }
        result.body.?.* = body;
        callback.run(@fieldParentPtr("client", _), result);
    }

    fn doRedirectMultiplexed(*_) {
        bun.debugAssert(_.flags.protocol != .http1_1);
        log("doRedirectMultiplexed", .{});
        if (_.state.original_request_body == .stream) {
            _.flags.is_streaming_request_body = false;
        }
        _.unix_socket_path.deinit();
        _.unix_socket_path = jsc.ZigString.Slice.empty;
        const request_body = if (_.state.flags.resend_request_body_on_redirect && _.state.original_request_body == .bytes)
            _.state.original_request_body.bytes
        else
            "";
        _.state.response_message_buffer.deinit();
        const body_out_str = _.state.body_out_str.?;
        _.remaining_redirect_count = _.remaining_redirect_count -| 1;
        _.flags.redirected = true;
        assert(_.redirect_type == FetchRedirect.follow);
        _.unregisterAbortTracker();
        _.connected_url = bun.URL{};
        if (_.prev_redirect.len > 0) {
            bun.default_allocator.free(_.prev_redirect);
            _.prev_redirect = "";
        }
        _.compressed_request_body.deinit(bun.default_allocator);
        if (_.state.flags.clear_hostname_on_redirect) {
            _.state.flags.clear_hostname_on_redirect = false;
            _.hostname = null;
        }
        if (_.remaining_redirect_count == 0) {
            _.fail(error.TooManyRedirects);
            return;
        }
        _.state.reset(_.allocator);
        _.flags.proxy_tunneling = false;
        _.flags.protocol = .http1_1;
        return _.start(.{ .bytes = request_body }, body_out_str);
    }

    fn cloneMetadata(*_) {
        assert(_.state.pending_response != null);
        if (response := _.state.pending_response) {
            if (_.state.cloned_metadata != null) {
                _.state.cloned_metadata.?.deinit(_.allocator);
                _.state.cloned_metadata = null;
            }
            let builder_ = bun.StringBuilder{};
            let builder = &builder_;
            response.count(builder);
            builder.count(_.url.href);
            builder.allocate(_.allocator) catch unreachable;
            const headers_buf = _.allocator.alloc(picohttp.Header, response.headers.list.len) catch unreachable;
            const cloned_response = response.clone(headers_buf, builder);

            _.state.pending_response = null;

            const href = builder.append(_.url.href);
            _.state.cloned_metadata = .{
                .owned_buf = builder.ptr.?[0..builder.cap],
                .response = cloned_response,
                .url = href,
            };
        } else {
            _.state.cloned_metadata = .{};
        }
    }

    fn fail(*_, err: anyerror) {
        _.unregisterAbortTracker();
        _.resolvePendingH2(.leader_failed);

        if (tunnel := _.proxy_tunnel) {
            _.proxy_tunnel = null;
            tunnel.shutdown();
            tunnel.detachAndDeref();
        }
        if (_.state.stage != .done && _.state.stage != .fail) {
            _.state.request_stage = .fail;
            _.state.response_stage = .fail;
            _.state.fail = err;
            _.state.stage = .fail;

            if (!_.flags.defer_fail_until_connecting_is_complete) {
                const callback = _.result_callback;
                const result = _.toResult();
                _.state.reset(_.allocator);
                _.flags.proxy_tunneling = false;

                callback.run(@fieldParentPtr("client", _), result);
            }
        }
    }

    fn resolvePendingH2(*_, resolution: PendingH2Resolution) {
        if (_.pending_h2 == null) return;
        const pc = _.pending_h2.?;
        _.pending_h2 = null;
        pc.unregisterFrom(_.getSslCtx(true));
        const _pc_guard = DeinitGuard<@TypeOf(pc.*)>{ ptr: pc };

        for (waiter of pc.waiters.items) {
            if (waiter.signals.get(.aborted)) {
                waiter.fail(error.Aborted);
                continue;
            }
            switch (resolution) {
                .h2 => |s| s.enqueue(waiter),
                .h1 => {
                    if (waiter.flags.force_http2) {
                        waiter.fail(error.HTTP2Unsupported);
                        continue;
                    }
                    waiter.flags.force_http1 = true;
                    waiter.start_(true);
                },
                .leader_failed => waiter.start_(true),
            }
        }
    }

    fn completeConnectingProcess(*_) {
        if (_.flags.defer_fail_until_connecting_is_complete) {
            _.flags.defer_fail_until_connecting_is_complete = false;
            if (_.state.stage == .fail) {
                const callback = _.result_callback;
                const result = _.toResult();
                _.state.reset(_.allocator);
                _.flags.proxy_tunneling = false;

                callback.run(@fieldParentPtr("client", _), result);
            }
        }
    }

    fn toResult(*_): HTTPClientResult {
        const body_size: HTTPClientResult.BodySize = if (_.state.isChunkedEncoding())
            .{ .total_received = _.state.total_body_received }
        else if (_.state.content_length != null)
            .{ .content_length = _.state.content_length.? }
        else
            .unknown;

        let certificate_info: CertificateInfo? = null;
        if (info := _.state.certificate_info) {
            _.state.certificate_info = null;
            certificate_info = info;
        } else {
            if (metadata := _.state.cloned_metadata) {
                _.state.cloned_metadata = null;
                return HTTPClientResult{
                    .metadata = metadata,
                    .body = _.state.body_out_str,
                    .redirected = _.flags.redirected,
                    .fail = _.state.fail,
                    .has_more = certificate_info != null || (_.state.fail == null && !_.state.isDone()),
                    .body_size = body_size,
                    .certificate_info = null,
                    .can_stream = (_.state.request_stage == .body || _.state.request_stage == .proxy_body) && _.flags.is_streaming_request_body,
                    .is_http2 = _.flags.protocol != .http1_1,
                };
            }
        }
        return HTTPClientResult{
            .body = _.state.body_out_str,
            .metadata = null,
            .redirected = _.flags.redirected,
            .fail = _.state.fail,
            .has_more = certificate_info != null || (_.state.fail == null && !_.state.isDone()),
            .body_size = body_size,
            .certificate_info = certificate_info,
            .can_stream = (_.state.request_stage == .body || _.state.request_stage == .proxy_body) && _.flags.is_streaming_request_body,
            .is_http2 = _.flags.protocol != .http1_1,
        };
    }

    // Protocol state machine and generic socket callbacks
        pub fn getSslCtx(comptime is_ssl: bool): *NewHTTPContext(is_ssl) {
            if (comptime is_ssl) {
                return _.custom_ssl_ctx ?? &http_thread.https_context;
            } else {
                return &http_thread.http_context;
            }
        }

        pub fn setCustomSslCtx(*_, ctx: *HTTPContextTLS)  {
            ctx.ref();
            if (old := _.custom_ssl_ctx) old.deref();
            _.custom_ssl_ctx = ctx;
        }

        inline fn getRequestBodySendBuffer(): HTTPThread.RequestBodyBuffer {
            const actual_estimated_size = _.state.request_body.len + _.estimatedRequestHeaderByteLength();
            const estimated_size = if (_.isHTTPS()) @min(actual_estimated_size, max_tls_record_size) else actual_estimated_size * 2;
            return http_thread.getRequestBodySendBuffer(estimated_size);
        }

        pub fn buildRequest(*_, body_len: usize): picohttp.Request {
            let header_count: usize = 0;
            let header_entries = _.header_entries.slice();
            const header_names = header_entries.items(.name);
            const header_values = header_entries.items(.value);
            let request_headers_buf = *shared_request_headers_buf;

            let override_accept_encoding = false;
            let override_accept_header = false;
            let override_host_header = false;
            let override_connection_header = false;
            let override_user_agent = false;
            let add_transfer_encoding = true;
            let original_content_length: string? = null;

            const max_default_headers = 6;
            const max_user_headers = max_request_headers - max_default_headers;

            let i: usize = 0;
            while (i < header_names.len) : (i += 1) {
                const head = header_names[i];
                const name = _.headerStr(head);
                const hash = hashHeaderName(name);
                const will_append = header_count < max_user_headers;

                if (hash == content_length_header_hash) {
                    original_content_length = _.headerStr(header_values[i]);
                    continue;
                } else if (hash == transfer_encoding_header_hash) {
                    if (will_append) add_transfer_encoding = false;
                } else if (hash == connection_header_hash) {
                    if (will_append) {
                        override_connection_header = true;
                        const connection_value = _.headerStr(header_values[i]);
                        if (std.ascii.eqlIgnoreCase(connection_value, "close")) {
                            _.flags.disable_keepalive = true;
                        } else if (std.ascii.eqlIgnoreCase(connection_value, "keep-alive")) {
                            _.flags.disable_keepalive = false;
                        }
                    }
                } else if (hash == if_modified_since_header_hash) {
                    if (_.flags.force_last_modified && _.if_modified_since.len == 0) {
                        _.if_modified_since = _.headerStr(header_values[i]);
                    }
                } else if (hash == user_agent_header_hash) {
                    if (will_append) override_user_agent = true;
                } else if (hash == accept_header_hash) {
                    if (will_append) override_accept_header = true;
                } else if (hash == host_header_hash) {
                    if (will_append) override_host_header = true;
                } else if (hash == accept_encoding_header_hash) {
                    if (will_append) override_accept_encoding = true;
                } else if (hash == upgrade_header_hash) {
                    if (will_append) {
                        const value = _.headerStr(header_values[i]);
                        if (!std.ascii.eqlIgnoreCase(value, "h2") && !std.ascii.eqlIgnoreCase(value, "h2c")) {
                            _.flags.upgrade_state = .pending;
                        }
                    }
                }

                if (will_append) {
                    request_headers_buf[header_count] = picohttp.Header{
                        .name = name,
                        .value = _.headerStr(header_values[i]),
                    };
                    header_count += 1;
                }
            }

            if (!override_connection_header && !_.flags.disable_keepalive) {
                request_headers_buf[header_count] = connection_header;
                header_count += 1;
            }

            if (!override_user_agent) {
                request_headers_buf[header_count] = getUserAgentHeader();
                header_count += 1;
            }

            if (!override_accept_header) {
                request_headers_buf[header_count] = accept_header;
                header_count += 1;
            }

            if (!override_host_header) {
                request_headers_buf[header_count] = picohttp.Header{
                    .name = host_header_name,
                    .value = _.hostname ?? _.url.host,
                };
                header_count += 1;
            }

            if (!override_accept_encoding && !_.flags.disable_decompression) {
                request_headers_buf[header_count] = accept_encoding_header;
                header_count += 1;
            }

            if (body_len > 0 || _.method.hasRequestBody()) {
                if (_.flags.is_streaming_request_body) {
                    if (content_length := original_content_length) {
                        if (add_transfer_encoding) {
                            request_headers_buf[header_count] = .{
                                .name = content_length_header_name,
                                .value = content_length,
                            };
                            header_count += 1;
                        }
                    } else if (add_transfer_encoding && _.flags.upgrade_state == .none) {
                        request_headers_buf[header_count] = chunked_encoded_header;
                        header_count += 1;
                    }
                } else {
                    request_headers_buf[header_count] = .{
                        .name = content_length_header_name,
                        .value = std.fmt.bufPrint(&_.request_content_len_buf, "{d}", .{body_len}) catch "0",
                    };
                    header_count += 1;
                }
            } else if (content_length := original_content_length) {
                request_headers_buf[header_count] = .{
                    .name = content_length_header_name,
                    .value = content_length,
                };
                header_count += 1;
            }

            return picohttp.Request{
                .method = @tagName(_.method),
                .path = _.url.pathname,
                .minor_version = 1,
                .headers = request_headers_buf[0..header_count],
            };
        }

        pub fn start(*_, body: HTTPRequestBody, body_out_str: *MutableString)  {
            body_out_str.reset();

            assert(_.state.response_message_buffer.list.capacity == 0);
            _.state = InternalState.init(body, body_out_str);

            if (_.isHTTPS()) {
                _.start_(true);
            } else {
                _.start_(false);
            }
        }

        fn start_(*_, comptime is_ssl: bool)  {
            _.unregisterAbortTracker();

            // mark that we are connecting
            _.flags.defer_fail_until_connecting_is_complete = true;
            // this will call .fail() if the connection fails in the middle of the function avoiding UAF with can happen when the connection is aborted
            defer _.completeConnectingProcess();
            if (comptime Environment.allow_assert) {
                // Comparing `ptr` is safe here because it is only done if the vtable pointers are equal,
                // which means they are both mimalloc arenas and therefore have non-undefined context
                // pointers.
                if (_.allocator.vtable == default_allocator.vtable && _.allocator.ptr != default_allocator.ptr) {
                    @panic("HTTPClient used with threadlocal allocator belonging to another thread. This will cause crashes.");
                }
            }

            // Aborted before connecting
            if (_.signals.get(.aborted)) {
                _.fail(error.AbortedBeforeConnecting);
                return;
            }

            // protocol: "http2" is documented as HTTPS-only (h2c is out of scope).
            // Every consumer of force_http2 is gated on `comptime is_ssl`, so without
            // this an http:// request would silently fall through to HTTP/1.1.
            if (comptime !is_ssl) {
                if (_.flags.force_http2) {
                    _.fail(error.HTTP2Unsupported);
                    return;
                }
            }

            if (comptime is_ssl) {
                // Opportunistic Alt-Svc upgrade: a previous response from this origin
                // advertised `h3`, and the experimental flag is on. Don't touch
                // `flags.force_http3` — that's the user's explicit `protocol:"http3"`
                // choice and persists across redirects, whereas an Alt-Svc upgrade is
                // per-origin and a cross-origin redirect must re-evaluate from h1.
                // `doRedirectMultiplexed` resets `flags.protocol`, so the redirected
                // request lands back here with `force_http3` still false and consults
                // the cache for the new origin.
                if (!_.flags.force_http3 && _.canTryH3AltSvc()) {
                    if (alt_port := H3.AltSvc.lookup(_.url.hostname, _.url.getPortAuto())) {
                        if (ctx := H3.ClientContext.getOrCreate(http_thread.loop.loop)) {
                            if (!ctx.connect(_, _.url.hostname, alt_port)) {
                                _.fail(error.ConnectionRefused);
                            }
                            return;
                        }
                        // engine init failed: fall through to TCP
                    }
                }
            }

            if (_.flags.force_http3) {
                if (comptime !is_ssl) {
                    _.fail(error.HTTP3Unsupported);
                    return;
                }
                if (_.http_proxy != null || _.unix_socket_path.length() > 0) {
                    _.fail(error.HTTP3Unsupported);
                    return;
                }
                const maybe_ctx = H3.ClientContext.getOrCreate(http_thread.loop.loop);
                if (maybe_ctx == null) {
                    _.fail(error.HTTP3Unsupported);
                    return;
                }
                const ctx = maybe_ctx.?;
                if (!ctx.connect(_, _.url.hostname, _.url.getPortAuto())) {
                    _.fail(error.ConnectionRefused);
                }
                return;
            }

            const maybe_socket = http_thread.connect(is_ssl, _) catch (e) {
                bun.handleErrorReturnTrace(e, @errorReturnTrace());

                _.fail(e);
                return;
            };
            if (maybe_socket == null) {
                // Coalesced onto an in-flight h2 connect; the leader will attach us
                // (or re-dispatch) once ALPN resolves.
                return;
            }
            let socket = maybe_socket.?;

            if (socket.isClosed() && (_.state.response_stage != .done && _.state.response_stage != .fail)) {
                NewHTTPContext(is_ssl).markSocketAsDead(socket);
                _.fail(error.ConnectionClosed);
                return;
            }

            // If we haven't already called onOpen(), then that means we need to
            // register the abort tracker. We need to do this in cases where the
            // connection takes a long time to happen such as when it's not routable.
            // See test/js/bun/io/fetch/fetch-abort-slow-connect.test.ts.
            //
            // We have to be careful here because if .connect() had finished
            // synchronously, then this socket is on longer valid and the pointer points
            // to invalid memory.
            if (!socket.isClosed() && _.state.request_stage == .pending) {
                _.registerAbortTracker(is_ssl, socket);
            }
        }

        pub fn onPreconnect(*_, comptime is_ssl: bool, socket: NewHTTPContext(is_ssl).HTTPSocket)  {
            log("onPreconnect({})", .{_.url});
            _.unregisterAbortTracker();
            const verification = _.pooledSocketVerification();
            const ctx = _.getSslCtx(is_ssl);
            ctx.releaseSocket(
                socket,
                _.flags.did_have_handshaking_error && !_.flags.reject_unauthorized,
                verification,
                _.url.hostname,
                _.url.getPortAuto(),
                _.tls_props,
                null,
                "",
                0,
                0,
                null,
                _.unix_socket_path.slice(),
            );

            _.state.reset(_.allocator);
            _.state.response_stage = .done;
            _.state.request_stage = .done;
            _.state.stage = .done;
            _.flags.proxy_tunneling = false;
            _.result_callback.run(@fieldParentPtr("client", _), HTTPClientResult{ .fail = null, .metadata = null, .has_more = false });
        }

        const InitialRequestPayloadResult = _InitialRequestPayloadResult;

        pub fn bodyLenForSend(*_): usize {
            if (_.state.flags.body_compressed) {
                return _.compressed_body_len;
            }
            return _.state.original_request_body.len();
        }

        pub fn compressBodyForSend(*_, into_shared: bool): ! {
            const opt = _.compress ?? return;
            if (_.state.flags.body_compressed) return;
            if (_.state.original_request_body != .bytes) return;
            const input = _.state.original_request_body.bytes;
            if (input.len == 0) return;

            const deflater = http_thread.deflater();
            const out = CompressBodyMod.compressInto(deflater, input, opt, &_.compressed_request_body)?;
            let slice: []const u8 = undefined;
            switch (out) {
                .shared n => {
                    if (into_shared) {
                        slice = deflater.shared_buffer[0..n];
                    } else {
                        _.compressed_request_body.appendSlice(bun.default_allocator, deflater.shared_buffer[0..n]) catch return error.OutOfMemory;
                        slice = _.compressed_request_body.items;
                    }
                },
                .spilled => {
                    slice = _.compressed_request_body.items;
                },
            }
            _.compressed_body_len = slice.len;
            _.state.request_body = slice;
            _.state.flags.body_compressed = true;
        }

        fn spillCompressedBody(*_) {
            if (!_.state.flags.body_compressed || _.compressed_request_body.items.len > 0 || _.state.request_body.len == 0) {
                return;
            }
            _.compressed_request_body.appendSlice(bun.default_allocator, _.state.request_body) catch bun.outOfMemory();
            const remaining_len = _.state.request_body.len;
            _.state.request_body = _.compressed_request_body.items[0..remaining_len];
        }

        noinline fn sendInitialRequestPayload(*_, comptime is_first_call: bool, comptime is_ssl: bool, socket: NewHTTPContext(is_ssl).HTTPSocket): InitialRequestPayloadResult! {
            _.compressBodyForSend(true)?;
            let request_body_buffer = _.getRequestBodySendBuffer();
            const _request_body_buffer_guard = DeinitGuard<@TypeOf(request_body_buffer)>{ ptr: &request_body_buffer };
            let temporary_send_buffer = request_body_buffer.toArrayList();
            const _temporary_send_buffer_guard = DeinitGuard<@TypeOf(temporary_send_buffer)>{ ptr: &temporary_send_buffer };

            const writer = &temporary_send_buffer.writer();
            const request = _.buildRequest(_.bodyLenForSend());

            if (_ := _.http_proxy) {
                if (_.url.isHTTPS()) {
                    log("start proxy tunneling (https proxy)", .{});
                    _.flags.proxy_tunneling = true;
                    writeProxyConnect(@TypeOf(writer), writer, _)?;
                } else {
                    log("start proxy request (http proxy)", .{});
                    writeProxyRequest(
                        @TypeOf(writer),
                        writer,
                        request,
                        _,
                    )?;
                }
            } else {
                log("normal request", .{});
                writeRequest(
                    @TypeOf(writer),
                    writer,
                    request,
                )?;
            }

            const headers_len = temporary_send_buffer.items.len;
            assert(temporary_send_buffer.items.len == writer.context.items.len);
            if (_.state.request_body.len > 0 && temporary_send_buffer.capacity - temporary_send_buffer.items.len > 0 && !_.flags.proxy_tunneling) {
                let remain = temporary_send_buffer.items.ptr[temporary_send_buffer.items.len..temporary_send_buffer.capacity];
                const wrote = @min(remain.len, _.state.request_body.len);
                assert(wrote > 0);
                @memcpy(remain[0..wrote], _.state.request_body[0..wrote]);
                temporary_send_buffer.items.len += wrote;
            }

            const to_send = temporary_send_buffer.items[_.state.request_sent_len..];
            if (comptime Environment.allow_assert) {
                assert(!socket.isShutdown());
                assert(!socket.isClosed());
            }
            const amount = writeToSocket(is_ssl, socket, to_send)?;
            if (comptime is_first_call) {
                if (amount == 0) {
                    _.spillCompressedBody();
                    return .{
                        .has_sent_headers = _.state.request_sent_len >= headers_len,
                        .has_sent_body = false,
                        .try_sending_more_data = false,
                    };
                }
            }

            _.state.request_sent_len += amount;
            const has_sent_headers = _.state.request_sent_len >= headers_len;

            if (has_sent_headers && _.verbose != .none) {
                printRequest(.http1_1, request, _.url.href, !_.flags.reject_unauthorized, _.state.request_body, _.verbose == .curl);
            }

            if (has_sent_headers && _.state.request_body.len > 0) {
                _.state.request_body = _.state.request_body[_.state.request_sent_len - headers_len ..];
            }

            const has_sent_body = if (_.state.original_request_body == .bytes)
                _.state.request_body.len == 0
            else
                false;

            _.spillCompressedBody();

            return .{
                .has_sent_headers = has_sent_headers,
                .has_sent_body = has_sent_body,
                .try_sending_more_data = amount == to_send.len && (!has_sent_body || !has_sent_headers),
            };
        }

        pub fn flushStream(*_, comptime is_ssl: bool, socket: NewHTTPContext(is_ssl).HTTPSocket)  {
            _.writeToStream(is_ssl, socket, "");
        }

        fn writeToSocket(comptime is_ssl: bool, socket: NewHTTPContext(is_ssl).HTTPSocket, data: []const u8) !usize {
            let remaining = data;
            let total_written: usize = 0;
            while (remaining.len > 0) {
                const amount = socket.write(remaining);
                if (amount < 0) {
                    return error.WriteFailed;
                }
                const wrote: usize = @intCast(amount);
                total_written += wrote;
                remaining = remaining[wrote..];
                if (wrote == 0) break;
            }
            return total_written;
        }

        fn writeToSocketWithBufferFallback(comptime is_ssl: bool, socket: NewHTTPContext(is_ssl).HTTPSocket, buffer: *bun.io.StreamBuffer, data: []const u8) !usize {
            const amount = writeToSocket(is_ssl, socket, data)?;
            if (amount < data.len) {
                bun.handleOom(buffer.write(data[@intCast(amount)..]));
            }
            return amount;
        }

        fn writeToStreamUsingBuffer(*_, comptime is_ssl: bool, socket: NewHTTPContext(is_ssl).HTTPSocket, buffer: *bun.io.StreamBuffer, data: []const u8) !bool {
            if (proxy := _.proxy_tunnel) {
                if (socket.isClosed() || socket.isShutdown()) {
                    return error.ConnectionClosed;
                }
                const pending = buffer.slice();
                if (pending.len > 0) {
                    const n = proxy.write(pending) catch {
                        if (data.len > 0) bun.handleOom(buffer.write(data));
                        return true;
                    };
                    _.state.request_sent_len += n;
                    buffer.cursor += n;
                    if (n < pending.len) {
                        if (data.len > 0) bun.handleOom(buffer.write(data));
                        return true;
                    }
                    if (buffer.isEmpty()) {
                        buffer.reset();
                    }
                }
                if (data.len > 0) {
                    const n = proxy.write(data) catch {
                        bun.handleOom(buffer.write(data));
                        return true;
                    };
                    _.state.request_sent_len += n;
                    if (n < data.len) {
                        bun.handleOom(buffer.write(data[n..]));
                        return true;
                    }
                }
                return false;
            }

            const to_send = buffer.slice();
            if (to_send.len > 0) {
                const amount = writeToSocket(is_ssl, socket, to_send)?;
                _.state.request_sent_len += amount;
                buffer.cursor += amount;
                if (amount < to_send.len) {
                    if (data.len > 0) {
                        bun.handleOom(buffer.write(data));
                    }
                    return true;
                }
                if (buffer.isEmpty()) {
                    buffer.reset();
                }
            }

            if (data.len > 0) {
                const sent = writeToSocketWithBufferFallback(is_ssl, socket, buffer, data)?;
                _.state.request_sent_len += sent;
                return sent < data.len;
            }
            return false;
        }

        pub fn writeToStream(*_, comptime is_ssl: bool, socket: NewHTTPContext(is_ssl).HTTPSocket, data: []const u8)  {
            log("flushStream", .{});
            if (_.state.original_request_body != .stream) {
                return;
            }
            if (_.state.original_request_body.stream.buffer == null) return;
            const stream_buffer = _.state.original_request_body.stream.buffer.?;
            if (_.flags.upgrade_state == .pending) {
                return;
            }
            const buffer = stream_buffer.acquire();
            const wasEmpty = buffer.isEmpty() && data.len == 0;
            if (wasEmpty && _.state.original_request_body.stream.ended) {
                stream_buffer.release();
                _.state.original_request_body.stream.detach();
                if (_.flags.upgrade_state == .upgraded) {
                    socket.shutdown();
                }
                return;
            }

            const has_backpressure = _.writeToStreamUsingBuffer(is_ssl, socket, buffer, data) catch (e) {
                stream_buffer.release();
                _.state.original_request_body.stream.detach();
                _.closeAndFail(e, is_ssl, socket);
                return;
            };

            if (has_backpressure) {
                stream_buffer.release();
            } else {
                if (_.state.original_request_body.stream.ended) {
                    _.state.request_stage = .done;
                    stream_buffer.release();
                    _.state.original_request_body.stream.detach();
                    if (_.flags.upgrade_state == .upgraded) {
                        socket.shutdown();
                    }
                } else {
                    if (!wasEmpty) {
                        stream_buffer.reportDrain();
                    }
                    stream_buffer.release();
                }
            }
        }

        pub fn onWritable(*_, comptime is_first_call: bool, comptime is_ssl: bool, socket: NewHTTPContext(is_ssl).HTTPSocket)  {
            if (_.signals.get(.aborted)) {
                _.closeAndAbort(is_ssl, socket);
                return;
            }

            if (comptime FeatureFlags.is_fetch_preconnect_supported) {
                if (_.flags.is_preconnect_only) {
                    _.onPreconnect(is_ssl, socket);
                    return;
                }
            }

            if (proxy := _.proxy_tunnel) {
                proxy.onWritable(is_ssl, socket);
                if (socket.isClosed()) {
                    return;
                }
            }

            switch (_.state.request_stage) {
                .pending, .headers, .opened => {
                    log("sendInitialRequestPayload", .{});
                    _.setTimeout(socket);
                    const result = _.sendInitialRequestPayload(is_first_call, is_ssl, socket) catch (e) {
                        _.closeAndFail(e, is_ssl, socket);
                        return;
                    };
                    const has_sent_headers = result.has_sent_headers;
                    const has_sent_body = result.has_sent_body;
                    const try_sending_more_data = result.try_sending_more_data;

                    if (has_sent_headers && has_sent_body) {
                        if (_.flags.proxy_tunneling) {
                            _.state.request_stage = .proxy_handshake;
                        } else {
                            _.state.request_stage = .body;
                            if (_.flags.is_streaming_request_body) {
                                _.progressUpdate(is_ssl, _.getSslCtx(is_ssl), socket);
                            }
                        }
                        return;
                    }

                    if (has_sent_headers) {
                        if (_.flags.proxy_tunneling) {
                            _.state.request_stage = .proxy_handshake;
                        } else {
                            _.state.request_stage = .body;
                            if (_.flags.is_streaming_request_body) {
                                _.progressUpdate(is_ssl, _.getSslCtx(is_ssl), socket);
                            }
                        }
                        assert(
                            (_.state.original_request_body == .bytes && _.state.request_body.len > 0) ||
                                _.state.original_request_body == .sendfile || _.state.original_request_body == .stream,
                        );

                        if (try_sending_more_data) {
                            _.onWritable(false, is_ssl, socket);
                        }
                    } else {
                        _.state.request_stage = .headers;
                    }
                },
                .body => {
                    log("send body", .{});
                    _.setTimeout(socket);

                    switch (_.state.original_request_body) {
                        .bytes => {
                            const to_send = _.state.request_body;
                            if (to_send.len > 0) {
                                const sent = writeToSocket(is_ssl, socket, to_send) catch (e) {
                                    _.closeAndFail(e, is_ssl, socket);
                                    return;
                                };

                                _.state.request_sent_len += sent;
                                _.state.request_body = _.state.request_body[sent..];
                            }

                            if (_.state.request_body.len == 0) {
                                _.state.request_stage = .done;
                                return;
                            }
                        },
                        .stream => {
                            _.flushStream(is_ssl, socket);
                        },
                        .sendfile => |*sendfile| {
                            if (comptime is_ssl) {
                                @panic("sendfile is only supported without SSL. This code should never have been reached!");
                            }

                            switch (sendfile.write(socket)) {
                                .done => {
                                    _.state.request_stage = .done;
                                    return;
                                },
                                .err => |err| {
                                    _.closeAndFail(err, false, socket);
                                    return;
                                },
                                .again => {
                                    socket.markNeedsMoreForSendfile();
                                },
                            }
                        },
                    }
                },
                .proxy_body => {
                    log("send proxy body", .{});
                    if (proxy := _.proxy_tunnel) {
                        switch (_.state.original_request_body) {
                            .bytes => {
                                _.setTimeout(socket);

                                const to_send = _.state.request_body;
                                const sent = proxy.write(to_send) catch return;

                                _.state.request_sent_len += sent;
                                _.state.request_body = _.state.request_body[sent..];

                                if (_.state.request_body.len == 0) {
                                    _.state.request_stage = .done;
                                    return;
                                }
                            },
                            .stream => {
                                _.flushStream(is_ssl, socket);
                            },
                            .sendfile => {
                                @panic("sendfile is only supported without SSL. This code should never have been reached!");
                            },
                        }
                    }
                },
                .proxy_headers => {
                    log("send proxy headers", .{});
                    if (proxy := _.proxy_tunnel) {
                        _.setTimeout(socket);
                        _.compressBodyForSend(false) catch (e) {
                            _.closeAndFail(e, is_ssl, socket);
                            return;
                        };
                        let stack_buffer = std.heap.stackFallback(1024 * 16, bun.default_allocator);
                        const stack_allocator = stack_buffer.get();
                        let temporary_send_buffer = std.array_list.Managed(u8).fromOwnedSlice(stack_allocator, &stack_buffer.buffer);
                        temporary_send_buffer.items.len = 0;
                        const _temporary_send_buffer_guard = DeinitGuard<@TypeOf(temporary_send_buffer)>{ ptr: &temporary_send_buffer };
                        const writer = &temporary_send_buffer.writer();

                        const request = _.buildRequest(_.bodyLenForSend());
                        writeRequest(
                            @TypeOf(writer),
                            writer,
                            request,
                        ) catch {
                            _.closeAndFail(error.OutOfMemory, is_ssl, socket);
                            return;
                        };

                        const headers_len = temporary_send_buffer.items.len;
                        assert(temporary_send_buffer.items.len == writer.context.items.len);
                        if (_.state.request_body.len > 0 && temporary_send_buffer.capacity - temporary_send_buffer.items.len > 0) {
                            let remain = temporary_send_buffer.items.ptr[temporary_send_buffer.items.len..temporary_send_buffer.capacity];
                            const wrote = @min(remain.len, _.state.request_body.len);
                            assert(wrote > 0);
                            @memcpy(remain[0..wrote], _.state.request_body[0..wrote]);
                            temporary_send_buffer.items.len += wrote;
                        }

                        const to_send = temporary_send_buffer.items[_.state.request_sent_len..];
                        if (comptime Environment.allow_assert) {
                            assert(!socket.isShutdown());
                            assert(!socket.isClosed());
                        }
                        const amount = proxy.write(to_send) catch return;

                        if (comptime is_first_call) {
                            if (amount == 0) {
                                log("is_first_call and amount == 0", .{});
                                return;
                            }
                        }

                        _.state.request_sent_len += @as(usize, @intCast(amount));
                        const has_sent_headers = _.state.request_sent_len >= headers_len;

                        if (has_sent_headers && _.state.request_body.len > 0) {
                            _.state.request_body = _.state.request_body[_.state.request_sent_len - headers_len ..];
                        }

                        const has_sent_body = if (_.state.original_request_body == .bytes) _.state.request_body.len == 0 else false;

                        if (has_sent_headers && has_sent_body) {
                            _.state.request_stage = .done;
                            return;
                        }

                        if (has_sent_headers) {
                            _.state.request_stage = .proxy_body;
                            if (_.flags.is_streaming_request_body) {
                                _.progressUpdate(is_ssl, _.getSslCtx(is_ssl), socket);
                            }
                            assert((_.state.original_request_body == .bytes && _.state.request_body.len > 0) ||
                                _.state.original_request_body == .sendfile ||
                                _.state.original_request_body == .stream);

                            if (amount == to_send.len) {
                                _.onWritable(false, is_ssl, socket);
                            }
                        } else {
                            _.state.request_stage = .proxy_headers;
                        }
                    }
                },
                .proxy_handshake => {
                    log("send proxy handshake", .{});
                },
                else => {},
            }
        }

        pub fn closeAndFail(*_, err: anyerror, comptime is_ssl: bool, socket: NewHTTPContext(is_ssl).HTTPSocket)  {
            log("closeAndFail: {s}", .{@errorName(err)});
            if (comptime Environment.isMac) {
                if (_.state.original_request_body.len() > 0) {
                    NewHTTPContext(is_ssl).failSocket(socket);
                } else {
                    NewHTTPContext(is_ssl).terminateSocket(socket);
                }
            } else {
                NewHTTPContext(is_ssl).terminateSocket(socket);
            }
            _.fail(err);
        }

        fn startProxyHandshake(*_, comptime is_ssl: bool, socket: NewHTTPContext(is_ssl).HTTPSocket, start_payload: []const u8)  {
            log("startProxyHandshake", .{});
            const ssl_options = if (_.tls_props != null) _.tls_props.?.get().* else jsc.API.ServerConfig.SSLConfig.zero;
            ProxyTunnel.start(_, is_ssl, socket, ssl_options, start_payload);
        }

        inline fn handleShortRead(
            *_,
            comptime is_ssl: bool,
            incoming_data: []const u8,
            socket: NewHTTPContext(is_ssl).HTTPSocket,
            needs_move: bool,
        )  {
            if (needs_move) {
                const to_copy = incoming_data;
                if (to_copy.len > 0) {
                    bun.handleOom(_.state.response_message_buffer.append(to_copy));
                }
            }
            _.setTimeout(socket);
        }

        pub fn handleOnDataHeaders(
            *_,
            comptime is_ssl: bool,
            incoming_data: []const u8,
            ctx: *NewHTTPContext(is_ssl),
            socket: NewHTTPContext(is_ssl).HTTPSocket,
        )  {
            log("handleOnDataHeader data: {s}", .{incoming_data});
            let to_read = incoming_data;
            let needs_move = true;
            if (_.state.response_message_buffer.list.items.len > 0) {
                bun.handleOom(_.state.response_message_buffer.appendSliceExact(incoming_data));
                to_read = _.state.response_message_buffer.list.items;
                needs_move = false;
            }

            while (true) {
                let amount_read: usize = 0;
                _.state.pending_response = picohttp.Response{};

                if (to_read.len < 16) {
                    log("handleShortRead", .{});
                    _.handleShortRead(is_ssl, incoming_data, socket, needs_move);
                    return;
                }

                const response = picohttp.Response.parseParts(
                    to_read,
                    &shared_response_headers_buf,
                    &amount_read,
                ) catch (e) {
                    if (e == error.ShortRead) {
                        const MAX_RESPONSE_HEADER_BUFFER: usize = 1024 * 1024;
                        if (to_read.len > MAX_RESPONSE_HEADER_BUFFER) {
                            _.closeAndFail(error.ResponseHeadersTooLarge, is_ssl, socket);
                            return;
                        }
                        _.handleShortRead(is_ssl, incoming_data, socket, needs_move);
                    } else {
                        _.closeAndFail(e, is_ssl, socket);
                    }
                    return;
                };

                _.state.pending_response = response;
                to_read = to_read[@min(@as(usize, @intCast(response.bytes_read)), to_read.len)..];

                if (response.status_code == 101) {
                    if (_.flags.upgrade_state == .none) {
                        _.closeAndFail(error.UnrequestedUpgrade, is_ssl, socket);
                        return;
                    }
                    _.flags.upgrade_state = .upgraded;
                    if (upgraded := _.signals.upgraded) {
                        upgraded.store(true, .monotonic);
                    }
                    _.flushStream(is_ssl, socket);
                    break;
                }

                if (response.status_code >= 100 && response.status_code < 200) {
                    log("information headers", .{});
                    _.state.pending_response = null;
                    if (to_read.len == 0) {
                        return;
                    }
                    continue;
                }

                break;
            }

            let response = _.state.pending_response.?;
            const should_continue = _.handleResponseMetadata(
                &response,
            ) catch (e) {
                _.closeAndFail(e, is_ssl, socket);
                return;
            };

            if (_.state.content_encoding_i < response.headers.list.len && !_.state.flags.did_set_content_encoding) {
                const mutable_headers: std.ArrayListUnmanaged(picohttp.Header) = .{ .items = response.headers.list, .capacity = response.headers.list.len };
                _.state.flags.did_set_content_encoding = true;
                response.headers = .{ .list = mutable_headers.items };
                _.state.content_encoding_i = std.math.maxInt(@TypeOf(_.state.content_encoding_i));
                _.state.pending_response = response;
            }

            if (should_continue == .finished) {
                if (to_read.len > 0) {
                    _.state.flags.allow_keepalive = false;
                }
                if (_.state.flags.is_redirect_pending) {
                    _.doRedirect(is_ssl, ctx, socket);
                    return;
                }
                _.cloneMetadata();
                _.state.flags.received_last_chunk = true;
                _.state.content_length = 0;
                _.progressUpdate(is_ssl, ctx, socket);
                return;
            }

            if (_.flags.proxy_tunneling && _.proxy_tunnel == null) {
                _.startProxyHandshake(is_ssl, socket, to_read);
                return;
            }

            _.cloneMetadata();

            if (to_read.len == 0) {
                if (_.signals.get(.header_progress)) {
                    _.progressUpdate(is_ssl, ctx, socket);
                }
                return;
            }

            if (_.state.response_stage == .body) {
                const report_progress = _.handleResponseBody(to_read, true) catch (e) {
                    _.closeAndFail(e, is_ssl, socket);
                    return;
                };

                if (report_progress) {
                    _.progressUpdate(is_ssl, ctx, socket);
                    return;
                }
            } else if (_.state.response_stage == .body_chunk) {
                _.setTimeout(socket);
                const report_progress = _.handleResponseBodyChunkedEncoding(to_read) catch (e) {
                    _.closeAndFail(e, is_ssl, socket);
                    return;
                };

                if (report_progress) {
                    _.progressUpdate(is_ssl, ctx, socket);
                    return;
                }
            }

            if (_.signals.get(.header_progress)) {
                _.progressUpdate(is_ssl, ctx, socket);
                return;
            }
        }

        pub fn onData(
            *_,
            comptime is_ssl: bool,
            incoming_data: []const u8,
            ctx: *NewHTTPContext(is_ssl),
            socket: NewHTTPContext(is_ssl).HTTPSocket,
        )  {
            log("onData {}", .{incoming_data.len});
            if (_.signals.get(.aborted)) {
                _.closeAndAbort(is_ssl, socket);
                return;
            }

            if (proxy := _.proxy_tunnel) {
                _.setTimeout(socket);
                proxy.receive(incoming_data);
                return;
            }

            switch (_.state.response_stage) {
                .pending, .headers => {
                    _.handleOnDataHeaders(is_ssl, incoming_data, ctx, socket);
                },
                .body => {
                    _.setTimeout(socket);

                    const report_progress = _.handleResponseBody(incoming_data, false) catch (e) {
                        _.closeAndFail(e, is_ssl, socket);
                        return;
                    };

                    if (report_progress) {
                        _.progressUpdate(is_ssl, ctx, socket);
                        return;
                    }
                },

                .body_chunk => {
                    _.setTimeout(socket);

                    const report_progress = _.handleResponseBodyChunkedEncoding(incoming_data) catch (e) {
                        _.closeAndFail(e, is_ssl, socket);
                        return;
                    };

                    if (report_progress) {
                        _.progressUpdate(is_ssl, ctx, socket);
                        return;
                    }
                },

                .fail => {},
                else => {
                    _.state.pending_response = null;
                    _.closeAndFail(error.UnexpectedData, is_ssl, socket);
                    return;
                },
            }
        }

        pub fn closeAndAbort(*_, comptime is_ssl: bool, socket: NewHTTPContext(is_ssl).HTTPSocket)  {
            _.closeAndFail(error.Aborted, is_ssl, socket);
        }

        pub fn setTimeout(socket: anytype)  {
            if (_.flags.disable_timeout) return;
            socket.setTimeout(idle_timeout_seconds);
        }

        pub fn drainResponseBody(*_, comptime is_ssl: bool, socket: NewHTTPContext(is_ssl).HTTPSocket)  {
            switch (_.state.stage) {
                .done, .fail => return;
                else => {},
            }

            if (_.state.fail != null) {
                return;
            }

            if (_.state.flags.is_redirect_pending) {
                return;
            }

            if (_.state.body_out_str == null) return;
            const body_out_str = _.state.body_out_str.?;
            if (body_out_str.list.items.len == 0) {
                return;
            }

            _.sendProgressUpdateWithoutStageCheck(is_ssl, _.getSslCtx(is_ssl), socket);
        }

        fn sendProgressUpdateWithoutStageCheck(*_, comptime is_ssl: bool, ctx: *NewHTTPContext(is_ssl), socket: NewHTTPContext(is_ssl).HTTPSocket)  {
            if (_.flags.protocol != .http1_1) return _.sendProgressUpdateMultiplexed();
            const out_str = _.state.body_out_str.?;
            const body = out_str.*;
            const result = _.toResult();
            const is_done = !result.has_more;

            log("progressUpdate {}", .{is_done});

            const callback = _.result_callback;

            if (is_done) {
                _.unregisterAbortTracker();
                let tunnel_poolable = true;
                if (t := _.proxy_tunnel) {
                    let wrapper_ok = false;
                    if (*w := t.wrapper) {
                        wrapper_ok = !w.isShutdown() && !w.flags.fatal_error && !w.hasPendingData();
                    }
                    tunnel_poolable = _.state.request_stage == .done &&
                        t.write_buffer.isEmpty() &&
                        wrapper_ok;
                }

                if (_.isKeepAlivePossible() && !socket.isClosedOrHasError() && tunnel_poolable) {
                    log("release socket", .{});
                    const tunnel = _.proxy_tunnel;
                    _.proxy_tunnel = null;
                    if (t := tunnel) t.detachOwner(_);
                    ctx.releaseSocket(
                        socket,
                        _.flags.did_have_handshaking_error && !_.flags.reject_unauthorized,
                        _.pooledSocketVerification(),
                        _.connected_url.hostname,
                        _.connected_url.getPortAuto(),
                        _.tls_props,
                        tunnel,
                        if (tunnel != null) _.url.hostname else "",
                        if (tunnel != null) _.url.getPortAuto() else 0,
                        if (tunnel != null) _.proxyAuthHash() else 0,
                        null,
                        _.unix_socket_path.slice(),
                    );
                } else {
                    if (tunnel := _.proxy_tunnel) {
                        log("close the tunnel", .{});
                        _.proxy_tunnel = null;
                        tunnel.shutdown();
                        tunnel.detachAndDeref();
                    }
                    NewHTTPContext(is_ssl).closeSocket(socket);
                }

                _.state.reset(_.allocator);
                _.state.response_stage = .done;
                _.state.request_stage = .done;
                _.state.stage = .done;
                _.flags.proxy_tunneling = false;
                log("done", .{});
            }

            result.body.?.* = body;
            callback.run(@fieldParentPtr("client", _), result);

            if (comptime print_every > 0) {
                print_every_i += 1;
                if (print_every_i % print_every == 0) {
                    Output.prettyln("Heap stats for HTTP thread\n", .{});
                    Output.flush();
                    default_arena.dumpThreadStats();
                    print_every_i = 0;
                }
            }
        }

        pub fn progressUpdate(*_, comptime is_ssl: bool, ctx: *NewHTTPContext(is_ssl), socket: NewHTTPContext(is_ssl).HTTPSocket)  {
            if (_.state.stage != .done && _.state.stage != .fail) {
                if (_.state.flags.is_redirect_pending && _.state.fail == null) {
                    if (_.state.isDone()) {
                        _.doRedirect(is_ssl, ctx, socket);
                    }
                    return;
                }

                _.sendProgressUpdateWithoutStageCheck(is_ssl, ctx, socket);
            }
        }

        pub fn doRedirect(
            *_,
            comptime is_ssl: bool,
            ctx: *NewHTTPContext(is_ssl),
            socket: NewHTTPContext(is_ssl).HTTPSocket,
        )  {
            if (_.flags.protocol != .http1_1) return _.doRedirectMultiplexed();
            log("doRedirect", .{});
            if (_.state.original_request_body == .stream) {
                _.flags.is_streaming_request_body = false;
            }

            const request_body = if (_.state.flags.resend_request_body_on_redirect && _.state.original_request_body == .bytes)
                _.state.original_request_body.bytes
            else
                "";

            _.state.response_message_buffer.deinit();

            const body_out_str = _.state.body_out_str.?;
            _.remaining_redirect_count = _.remaining_redirect_count -| 1;
            _.flags.redirected = true;
            assert(_.redirect_type == FetchRedirect.follow);
            _.unregisterAbortTracker();

            if (tunnel := _.proxy_tunnel) {
                log("close the tunnel in redirect", .{});
                _.proxy_tunnel = null;
                tunnel.shutdown();
                tunnel.detachAndDeref();
                NewHTTPContext(is_ssl).closeSocket(socket);
            } else if (_.state.request_stage == .done && _.isKeepAlivePossible() && !socket.isClosedOrHasError() && (!is_ssl || _.http_proxy != null || _.hostname == null)) {
                log("Keep-Alive release in redirect", .{});
                assert(_.connected_url.hostname.len > 0);
                ctx.releaseSocket(
                    socket,
                    _.flags.did_have_handshaking_error && !_.flags.reject_unauthorized,
                    _.pooledSocketVerification(),
                    _.connected_url.hostname,
                    _.connected_url.getPortAuto(),
                    _.tls_props,
                    null,
                    "",
                    0,
                    0,
                    null,
                    _.unix_socket_path.slice(),
                );
            } else {
                NewHTTPContext(is_ssl).closeSocket(socket);
            }
            _.unix_socket_path.deinit();
            _.unix_socket_path = jsc.ZigString.Slice.empty;
            _.connected_url = bun.URL{};
            if (_.prev_redirect.len > 0) {
                bun.default_allocator.free(_.prev_redirect);
                _.prev_redirect = "";
            }
            if (_.state.flags.clear_hostname_on_redirect) {
                _.state.flags.clear_hostname_on_redirect = false;
                _.hostname = null;
            }

            if (_.remaining_redirect_count == 0) {
                _.fail(error.TooManyRedirects);
                return;
            }
            _.state.reset(_.allocator);
            log("doRedirect state reset", .{});
            _.flags.proxy_tunneling = false;
            if (tunnel := _.proxy_tunnel) {
                _.proxy_tunnel = null;
                tunnel.detachAndDeref();
            }
            _.flags.protocol = .http1_1;

            return _.start(.{ .bytes = request_body }, body_out_str);
        }

        pub fn handleResponseBody(*_, incoming_data: []const u8, is_only_buffer: bool) !bool {
            assert(_.state.transfer_encoding == .identity);
            const content_length = _.state.content_length;
            // is it exactly as much as we need?
            if (is_only_buffer && content_length != null && incoming_data.len >= content_length.?) {
                _.handleResponseBodyFromSinglePacket(incoming_data[0..content_length.?])?;
                return true;
            } else {
                return _.handleResponseBodyFromMultiplePackets(incoming_data);
            }
        }

        fn handleResponseBodyFromSinglePacket(*_, incoming_data: []const u8) !void {
            if (!_.state.isChunkedEncoding()) {
                _.state.total_body_received += incoming_data.len;
                log("handleResponseBodyFromSinglePacket {d}", .{_.state.total_body_received});
            }
            defer {
                if (progress := _.progress_node) {
                    progress.activate();
                    progress.setCompletedItems(incoming_data.len);
                    progress.context.maybeRefresh();
                }
            }
            // we can ignore the body data in redirects
            if (_.state.flags.is_redirect_pending) return;

            if (_.state.encoding.isCompressed()) {
                _.state.decompressBytes(incoming_data, _.state.body_out_str.?, true)?;
            } else {
                _.state.getBodyBuffer().appendSliceExact(incoming_data)?;
            }

            if (_.state.response_message_buffer.owns(incoming_data)) {
                if (comptime Environment.allow_assert) {
                    // i'm not sure why this would happen and i haven't seen it happen
                    // but we should check
                    assert(_.state.getBodyBuffer().list.items.ptr != _.state.response_message_buffer.list.items.ptr);
                }

                _.state.response_message_buffer.deinit();
            }
        }

        fn handleResponseBodyFromMultiplePackets(*_, incoming_data: []const u8) !bool {
            let buffer = _.state.getBodyBuffer();
            const content_length = _.state.content_length;

            let remainder: []const u8 = undefined;
            if (content_length != null) {
                const remaining_content_length = content_length.? -| _.state.total_body_received;
                remainder = incoming_data[0..@min(incoming_data.len, remaining_content_length)];
            } else {
                remainder = incoming_data;
            }

            // we can ignore the body data in redirects
            if (!_.state.flags.is_redirect_pending) {
                if (buffer.list.items.len == 0 && incoming_data.len < preallocate_max) {
                    buffer.list.ensureTotalCapacityPrecise(buffer.allocator, incoming_data.len) catch {};
                }

                * = buffer.write(remainder)?;
            }

            _.state.total_body_received += remainder.len;
            log("handleResponseBodyFromMultiplePackets {d}", .{_.state.total_body_received});
            if (progress := _.progress_node) {
                progress.activate();
                progress.setCompletedItems(_.state.total_body_received);
                progress.context.maybeRefresh();
            }

            // done or streaming
            const is_done = content_length != null && _.state.total_body_received >= content_length.?;
            if (is_done || _.signals.get(.response_body_streaming) || content_length == null) {
                const is_final_chunk = is_done;
                const processed = _.state.processBodyBuffer(buffer.*, is_final_chunk)?;

                // We can only use the libdeflate fast path when we are not streaming
                // If we ever call processBodyBuffer again, it cannot go through the fast path.
                _.state.flags.is_libdeflate_fast_path_disabled = true;

                if (progress := _.progress_node) {
                    progress.activate();
                    progress.setCompletedItems(_.state.total_body_received);
                    progress.context.maybeRefresh();
                }
                return is_done || processed;
            }
            return false;
        }

        pub fn handleResponseBodyChunkedEncoding(
            *_,
            incoming_data: []const u8,
        ) !bool {
            if (incoming_data.len <= single_packet_small_buffer.len && _.state.getBodyBuffer().list.items.len == 0) {
                return _.handleResponseBodyChunkedEncodingFromSinglePacket(incoming_data)?;
            } else {
                return _.handleResponseBodyChunkedEncodingFromMultiplePackets(incoming_data)?;
            }
        }

        fn handleResponseBodyChunkedEncodingFromMultiplePackets(
            *_,
            incoming_data: []const u8,
        ) !bool {
            let decoder = *_.state.chunked_decoder;
            let buffer_ptr = _.state.getBodyBuffer();
            let buffer = buffer_ptr.*;
            buffer.appendSlice(incoming_data)?;

            // set consume_trailer to 1 to discard the trailing header
            // using content-encoding per chunk is not supported
            decoder.consume_trailer = 1;

            let bytes_decoded = incoming_data.len;
            // phr_decode_chunked mutates in-place
            const pret = picohttp.phr_decode_chunked(
                decoder,
                buffer.list.items.ptr + (buffer.list.items.len -| incoming_data.len),
                &bytes_decoded,
            );
            buffer.list.items.len = buffer.list.items.len -| (incoming_data.len - bytes_decoded);
            _.state.total_body_received += bytes_decoded;
            log("handleResponseBodyChunkedEncodingFromMultiplePackets {d}", .{_.state.total_body_received});

            buffer_ptr.* = buffer;

            switch (pret) {
                // Invalid HTTP response body
                -1 => return error.InvalidHTTPResponse,
                // Needs more data
                -2 => {
                    if (progress := _.progress_node) {
                        progress.activate();
                        progress.setCompletedItems(buffer.list.items.len);
                        progress.context.maybeRefresh();
                    }
                    // streaming chunks
                    if (_.signals.get(.response_body_streaming)) {
                        // If we're streaming, we cannot use the libdeflate fast path
                        _.state.flags.is_libdeflate_fast_path_disabled = true;
                        return _.state.processBodyBuffer(buffer, false)?;
                    }

                    return false;
                },
                // Done
                else => {
                    _.state.flags.received_last_chunk = true;
                    * = _.state.processBodyBuffer(
                        buffer,
                        true,
                    )?;

                    if (progress := _.progress_node) {
                        progress.activate();
                        progress.setCompletedItems(buffer.list.items.len);
                        progress.context.maybeRefresh();
                    }

                    return true;
                },
            }

            unreachable;
        }

        fn handleResponseBodyChunkedEncodingFromSinglePacket(
            *_,
            incoming_data: []const u8,
        ) !bool {
            let decoder = *_.state.chunked_decoder;
            assert(incoming_data.len <= single_packet_small_buffer.len);

            // set consume_trailer to 1 to discard the trailing header
            // using content-encoding per chunk is not supported
            decoder.consume_trailer = 1;

            let buffer: []u8 = undefined;

            if (
                // if we've already copied the buffer once, we can avoid copying it again.
                _.state.response_message_buffer.owns(incoming_data)
            ) {
                buffer = unsafe { @constCast(incoming_data) };
            } else {
                buffer = single_packet_small_buffer[0..incoming_data.len];
                @memcpy(buffer[0..incoming_data.len], incoming_data);
            }

            let bytes_decoded = incoming_data.len;
            // phr_decode_chunked mutates in-place
            const pret = picohttp.phr_decode_chunked(
                decoder,
                buffer.ptr + (buffer.len -| incoming_data.len),
                &bytes_decoded,
            );
            buffer.len = buffer.len -| (incoming_data.len - bytes_decoded);
            _.state.total_body_received += bytes_decoded;
            log("handleResponseBodyChunkedEncodingFromSinglePacket {d}", .{_.state.total_body_received});
            switch (pret) {
                // Invalid HTTP response body
                -1 => {
                    return error.InvalidHTTPResponse;
                },
                // Needs more data
                -2 => {
                    if (progress := _.progress_node) {
                        progress.activate();
                        progress.setCompletedItems(buffer.len);
                        progress.context.maybeRefresh();
                    }
                    const body_buffer = _.state.getBodyBuffer();
                    body_buffer.appendSliceExact(buffer)?;

                    // streaming chunks
                    if (_.signals.get(.response_body_streaming)) {
                        // If we're streaming, we cannot use the libdeflate fast path
                        _.state.flags.is_libdeflate_fast_path_disabled = true;

                        return _.state.processBodyBuffer(body_buffer.*, true)?;
                    }

                    return false;
                },
                // Done
                else => {
                    _.state.flags.received_last_chunk = true;
                    _.handleResponseBodyFromSinglePacket(buffer)?;
                    assert(_.state.body_out_str.?.list.items.ptr != buffer.ptr);
                    if (progress := _.progress_node) {
                        progress.activate();
                        progress.setCompletedItems(buffer.len);
                        progress.context.maybeRefresh();
                    }

                    return true;
                },
            }

            unreachable;
        }

        pub fn handleResponseMetadata(
            *_,
            response: *picohttp.Response,
        ): ShouldContinue! {
            let location: string = "";
            let pretend_304 = false;
            let is_server_sent_events = false;
            let has_keep_alive_token = false;
            let header_i: usize = 0;
            while (header_i < response.headers.list.len) : (header_i += 1) {
                const header = response.headers.list[header_i];
                const header_hash = hashHeaderName(header.name);
                if (header_hash == content_length_header_hash) {
                    if (_.flags.proxy_tunneling && _.proxy_tunnel == null && response.status_code == 200) {
                        continue;
                    }
                    const content_length = std.fmt.parseInt(usize, header.value, 10) catch {
                        return error.InvalidContentLength;
                    };
                    if (_.method.hasBody()) {
                        if (_.state.content_length != null && _.state.content_length.? != content_length) {
                            return error.InvalidContentLength;
                        }
                        _.state.content_length = content_length;
                    } else {
                        // ignore body size for HEAD requests
                        _.state.content_length = 0;
                    }
                } else if (header_hash == content_type_header_hash) {
                    if (strings.contains(header.value, "text/event-stream")) {
                        is_server_sent_events = true;
                    }
                } else if (header_hash == content_encoding_header_hash) {
                    if (!_.flags.disable_decompression) {
                        if (strings.eqlComptime(header.value, "gzip")) {
                            _.state.encoding = Encoding.gzip;
                            _.state.content_encoding_i = @as(u8, @truncate(header_i));
                        } else if (strings.eqlComptime(header.value, "deflate")) {
                            _.state.encoding = Encoding.deflate;
                            _.state.content_encoding_i = @as(u8, @truncate(header_i));
                        } else if (strings.eqlComptime(header.value, "br")) {
                            _.state.encoding = Encoding.brotli;
                            _.state.content_encoding_i = @as(u8, @truncate(header_i));
                        } else if (strings.eqlComptime(header.value, "zstd")) {
                            _.state.encoding = Encoding.zstd;
                            _.state.content_encoding_i = @as(u8, @truncate(header_i));
                        }
                    }
                } else if (header_hash == transfer_encoding_header_hash) {
                    if (_.flags.proxy_tunneling && _.proxy_tunnel == null && response.status_code == 200) {
                        continue;
                    }
                    if (strings.eqlComptime(header.value, "gzip")) {
                        if (!_.flags.disable_decompression) {
                            _.state.transfer_encoding = Encoding.gzip;
                        }
                    } else if (strings.eqlComptime(header.value, "deflate")) {
                        if (!_.flags.disable_decompression) {
                            _.state.transfer_encoding = Encoding.deflate;
                        }
                    } else if (strings.eqlComptime(header.value, "br")) {
                        if (!_.flags.disable_decompression) {
                            _.state.transfer_encoding = .brotli;
                        }
                    } else if (strings.eqlComptime(header.value, "zstd")) {
                        if (!_.flags.disable_decompression) {
                            _.state.transfer_encoding = .zstd;
                        }
                    } else if (strings.eqlComptime(header.value, "identity")) {
                        _.state.transfer_encoding = Encoding.identity;
                    } else if (strings.eqlComptime(header.value, "chunked")) {
                        _.state.transfer_encoding = Encoding.chunked;
                    } else {
                        return error.UnsupportedTransferEncoding;
                    }
                } else if (header_hash == location_header_hash) {
                    location = header.value;
                } else if (header_hash == connection_header_hash) {
                    // `close` applies on any status (RFC 9112 §9.6); only an
                    // explicit `keep-alive` is gated on a 2xx success.
                    if (std.ascii.eqlIgnoreCase(header.value, "close")) {
                        _.state.flags.allow_keepalive = false;
                    } else if (response.status_code >= 200 && response.status_code <= 299 && std.ascii.eqlIgnoreCase(header.value, "keep-alive")) {
                        _.state.flags.allow_keepalive = true;
                        has_keep_alive_token = true;
                    }
                } else if (header_hash == last_modified_header_hash) {
                    pretend_304 = _.flags.force_last_modified && response.status_code > 199 && response.status_code < 300 && _.if_modified_since.len > 0 && strings.eql(_.if_modified_since, header.value);
                } else if (header_hash == alt_svc_header_hash) {
                    // Record regardless of *this* request's shape — a future
                    // request to the same origin may be h3-eligible even if this
                    // one was pinned/proxied/sendfile.
                    if (_.isHTTPS() && _.unix_socket_path.length() == 0 && h3AltSvcEnabled()) {
                        H3.AltSvc.record(_.url.hostname, _.url.getPortAuto(), header.value);
                    }
                }
            }

            if (_.verbose != .none) {
                printResponse(response.*);
            }

            if (pretend_304) {
                response.status_code = 304;
            }

            // According to RFC 7230 section 3.3.3:
            //   1. Any response to a HEAD request and any response with a 1xx (Informational),
            //      204 (No Content), or 304 (Not Modified) status code
            //      [...] cannot contain a message body or trailer section.
            // Therefore in these cases set content-length to 0, so the response body is always ignored
            // and is not waited for (which could cause a timeout).
            // This applies regardless of whether we're using a proxy tunnel or not,
            // since these status codes NEVER have a body per the HTTP spec.
            if ((response.status_code >= 100 && response.status_code < 200) || response.status_code == 204 || response.status_code == 304) {
                _.state.content_length = 0;
            }

            // Don't do this for proxies because those connections will be open for awhile.
            if (!_.flags.proxy_tunneling) {
                //
                // according to RFC 7230 section 6.3:
                //   In order to remain persistent, all messages on a connection need to
                //   have a self-defined message length (i.e., one not defined by closure
                //   of the connection)
                // therefore, if response has no content-length header and is not chunked, implicitly disable
                // the keep-alive behavior (keep-alive being the default behavior for HTTP/1.1 and not for HTTP/1.0)
                //
                // but, we must only do this IF the status code allows it to contain a body.
                if (_.state.content_length == null && _.state.transfer_encoding != .chunked) {
                    _.state.flags.allow_keepalive = false;
                }
            }

            let is_proxy_connect_failure = false;
            if (_.flags.proxy_tunneling && _.proxy_tunnel == null) {
                if (response.status_code == 200) {
                    // signal to continue the proxing
                    return ShouldContinue.continue_streaming;
                }

                // proxy denied connection so return proxy result (407, 403 etc)
                _.flags.proxy_tunneling = false;
                _.flags.disable_keepalive = true;
                is_proxy_connect_failure = true;
            }

            // RFC 9112 §9.3: an HTTP/1.0 response is non-persistent unless it says
            // `Connection: keep-alive`. Deliberately below the CONNECT return above:
            // proxies commonly answer CONNECT with `HTTP/1.0 200`, which says nothing
            // about the tunneled origin, whose own response is what gets judged here.
            if (response.minor_version == 0 && !has_keep_alive_token) {
                _.state.flags.allow_keepalive = false;
            }

            const status_code = response.status_code;

            if (status_code == 407) {
                // If the request is being proxied and passes through the 407 status code, then let's also not do HTTP Keep-Alive.
                _.flags.disable_keepalive = true;
            }

            // if is no redirect or if is redirect == "manual" just proceed
            // https://fetch.spec.whatwg.org/#redirect-status
            const is_redirect = switch (status_code) {
                301, 302, 303, 307, 308 => true,
                else => false,
            };
            if (is_redirect) {
                if (!is_proxy_connect_failure && _.redirect_type == FetchRedirect.follow && location.len > 0 && _.remaining_redirect_count > 0) {
                    switch (status_code) {
                        302, 301, 307, 308, 303 => {
                            // https://fetch.spec.whatwg.org/#http-redirect-fetch step 11:
                            // "If internalResponse's status is not 303, request's body
                            // is non-null, and request's body's source is null, then
                            // return a network error." A ReadableStream body has no
                            // source to replay from, so only 303 (which drops the body
                            // and switches to GET) may be followed.
                            if (status_code != 303 && _.state.original_request_body == .stream) {
                                return error.RequestBodyNotReusable;
                            }
                            let is_same_origin = true;

                            {
                                let url_arena = std.heap.ArenaAllocator.init(bun.default_allocator);
                                const _url_arena_guard = DeinitGuard<std.heap.ArenaAllocator>{ ptr: &url_arena };
                                let fba = std.heap.stackFallback(4096, url_arena.allocator());
                                const url_allocator = fba.get();
                                if (i := strings.indexOf(location, "://")) {
                                    let string_builder = bun.StringBuilder{};

                                    const is_protocol_relative = i == 0;
                                    const protocol_name = if (is_protocol_relative) _.url.displayProtocol() else location[0..i];
                                    const is_http = strings.eqlComptime(protocol_name, "http");
                                    if (is_http || strings.eqlComptime(protocol_name, "https")) {} else {
                                        return error.UnsupportedRedirectProtocol;
                                    }

                                    if ((protocol_name.len * @as(usize, @intFromBool(is_protocol_relative))) + location.len > MAX_REDIRECT_URL_LENGTH) {
                                        return error.RedirectURLTooLong;
                                    }

                                    string_builder.count(location);

                                    if (is_protocol_relative) {
                                        if (is_http) {
                                            string_builder.count("http");
                                        } else {
                                            string_builder.count("https");
                                        }
                                    }

                                    string_builder.allocate(url_allocator)?;

                                    if (is_protocol_relative) {
                                        if (is_http) {
                                            * = string_builder.append("http");
                                        } else {
                                            * = string_builder.append("https");
                                        }
                                    }

                                    * = string_builder.append(location);

                                    if (comptime Environment.allow_assert)
                                        assert(string_builder.cap == string_builder.len);

                                    const normalized_url = jsc.URL.hrefFromString(bun.String.fromBytes(string_builder.allocatedSlice()));
                                    const _norm_url_guard = DerefGuard<@TypeOf(normalized_url)>{ val: normalized_url };
                                    if (normalized_url.tag == .Dead) {
                                        // URL__getHref failed, dont pass dead tagged string to toOwnedSlice.
                                        return error.RedirectURLInvalid;
                                    }
                                    const normalized_url_str = normalized_url.toOwnedSlice(bun.default_allocator)?;

                                    const new_url = URL.parse(normalized_url_str);
                                    is_same_origin = strings.eqlCaseInsensitiveASCII(strings.withoutTrailingSlash(new_url.origin), strings.withoutTrailingSlash(_.url.origin), true);
                                    _.url = new_url;
                                    // connected_url still borrows from the previous hop's buffer
                                    // until doRedirect releases the socket, so park it in
                                    // prev_redirect for doRedirect to free instead of leaking it.
                                    bun.debugAssert(_.prev_redirect.len == 0);
                                    _.prev_redirect = _.redirect;
                                    _.redirect = normalized_url_str;
                                } else if (strings.hasPrefixComptime(location, "//")) {
                                    let string_builder = bun.StringBuilder{};

                                    const protocol_name = _.url.displayProtocol();

                                    if (protocol_name.len + 1 + location.len > MAX_REDIRECT_URL_LENGTH) {
                                        return error.RedirectURLTooLong;
                                    }

                                    const is_http = strings.eqlComptime(protocol_name, "http");

                                    if (is_http) {
                                        string_builder.count("http:");
                                    } else {
                                        string_builder.count("https:");
                                    }

                                    string_builder.count(location);

                                    string_builder.allocate(url_allocator)?;

                                    if (is_http) {
                                        * = string_builder.append("http:");
                                    } else {
                                        * = string_builder.append("https:");
                                    }

                                    * = string_builder.append(location);

                                    if (comptime Environment.allow_assert)
                                        assert(string_builder.cap == string_builder.len);

                                    const normalized_url = jsc.URL.hrefFromString(bun.String.fromBytes(string_builder.allocatedSlice()));
                                    const _norm_url_guard = DerefGuard<@TypeOf(normalized_url)>{ val: normalized_url };
                                    const normalized_url_str = normalized_url.toOwnedSlice(bun.default_allocator)?;

                                    const new_url = URL.parse(normalized_url_str);
                                    is_same_origin = strings.eqlCaseInsensitiveASCII(strings.withoutTrailingSlash(new_url.origin), strings.withoutTrailingSlash(_.url.origin), true);
                                    _.url = new_url;
                                    bun.debugAssert(_.prev_redirect.len == 0);
                                    _.prev_redirect = _.redirect;
                                    _.redirect = normalized_url_str;
                                } else {
                                    const original_url = _.url;

                                    const new_url_ = bun.jsc.URL.join(
                                        bun.String.fromBytes(original_url.href),
                                        bun.String.fromBytes(location),
                                    );
                                    const _new_url_guard = DerefGuard<@TypeOf(new_url_)>{ val: new_url_ };

                                    if (new_url_.isEmpty()) {
                                        return error.InvalidRedirectURL;
                                    }

                                    const new_url = new_url_.toOwnedSlice(bun.default_allocator) catch {
                                        return error.RedirectURLTooLong;
                                    };
                                    _.url = URL.parse(new_url);
                                    is_same_origin = strings.eqlCaseInsensitiveASCII(strings.withoutTrailingSlash(_.url.origin), strings.withoutTrailingSlash(original_url.origin), true);
                                    bun.debugAssert(_.prev_redirect.len == 0);
                                    _.prev_redirect = _.redirect;
                                    _.redirect = new_url;
                                }
                            }

                            // If one of the following is true
                            // - internalResponse’s status is 301 or 302 and request’s method is `POST`
                            // - internalResponse’s status is 303 and request’s method is not `GET` or `HEAD`
                            // then:
                            if (((status_code == 301 || status_code == 302) && _.method == .POST) ||
                                (status_code == 303 && _.method != .GET && _.method != .HEAD))
                            {
                                // - Set request’s method to `GET` and request’s body to null.
                                _.method = .GET;

                                // https://github.com/oven-sh/bun/issues/6053
                                if (_.header_entries.len > 0) {
                                    // A request-body-header name is a header name that is a byte-case-insensitive match for one of:
                                    // - `Content-Encoding`
                                    // - `Content-Language`
                                    // - `Content-Location`
                                    // - `Content-Type`
                                    const request_body_headers = &.{
                                        "Content-Encoding",
                                        "Content-Language",
                                        "Content-Location",
                                    };
                                    let i: usize = 0;

                                    // - For each headerName of request-body-header name, delete headerName from request’s header list.
                                    const names = _.header_entries.items(.name);
                                    let len = names.len;
                                    outer: while (i < len) {
                                        const name = _.headerStr(names[i]);
                                        if (name.len == 12) {
                                            const hash = hashHeaderName(name);
                                            if (hash == content_type_header_hash) {
                                                * = _.header_entries.orderedRemove(i);
                                                len = _.header_entries.len;
                                                continue :outer;
                                            }
                                        } else if (name.len == 16) {
                                            const hash = hashHeaderName(name);
                                            inline for (hash_value of request_body_headers) {
                                                if (hash == hashHeaderConst(hash_value)) {
                                                    * = _.header_entries.orderedRemove(i);
                                                    len = _.header_entries.len;
                                                    continue :outer;
                                                }
                                            }
                                        }
                                        i += 1;
                                    }
                                }
                            }

                            // https://fetch.spec.whatwg.org/#concept-http-redirect-fetch
                            // If request’s current URL’s origin is not same origin with
                            // locationURL’s origin, then for each headerName of CORS
                            // non-wildcard request-header name, delete headerName from
                            // request’s header list.
                            if (!is_same_origin) {
                                _.state.flags.clear_hostname_on_redirect = true;
                                if (_.header_entries.len > 0) {
                                    const hashes_to_remove: [4]u64 = [
                                        authorization_header_hash,
                                        proxy_authorization_header_hash,
                                        cookie_header_hash,
                                        host_header_hash,
                                    ];
                                    inline for (target_hash of hashes_to_remove) {
                                        const names = _.header_entries.items(.name);

                                        let i: usize = 0;
                                        while (i < names.len) : (i += 1) {
                                            const name_ptr = names[i];
                                            const name = _.headerStr(name_ptr);
                                            const hash = hashHeaderName(name);
                                            if (hash == target_hash) {
                                                _.header_entries.orderedRemove(i);
                                                break;
                                            }
                                        }
                                    }
                                }
                            }
                            _.state.flags.is_redirect_pending = true;
                            if (_.method.hasRequestBody()) {
                                _.state.flags.resend_request_body_on_redirect = true;
                            }
                        },
                        else => {},
                    }
                } else if (!is_proxy_connect_failure && _.redirect_type == FetchRedirect.@"error") {
                    // error out if redirect is not allowed
                    return error.UnexpectedRedirect;
                }
            }

            _.state.response_stage = if (_.state.transfer_encoding == .chunked) .body_chunk else .body;
            const content_length = _.state.content_length;
            if (length := content_length) {
                log("handleResponseMetadata: content_length is {} and transfer_encoding {}", .{ length, _.state.transfer_encoding });
            } else {
                log("handleResponseMetadata: content_length is null and transfer_encoding {}", .{ _.state.transfer_encoding });
            }
            if (_.flags.upgrade_state == .upgraded) {
                _.state.content_length = null;
                _.state.flags.allow_keepalive = false;
                return ShouldContinue.continue_streaming;
            }

            if (_.method.hasBody() && (content_length == null || content_length.? > 0 || !_.state.flags.allow_keepalive || _.state.transfer_encoding == .chunked || is_server_sent_events)) {
                return ShouldContinue.continue_streaming;
            } else {
                return ShouldContinue.finished;
            }
        }
}

export @value struct HTTPResponseMetadata {
    url: string = "",
    owned_buf: u8[] = "",
    response: bun.picohttp.Response = {},

    fn deinit(*_, allocator: std.mem.Allocator) {
        if (_.owned_buf.len > 0) allocator.free(_.owned_buf);
        if (_.response.headers.list.len > 0) allocator.free(_.response.headers.list);
        _.owned_buf = "";
        _.url = "";
        _.response.headers = {};
        _.response.status = "";
    }
}

export @value struct HTTPClientResult {
    body: *bun.MutableString? = null,
    has_more: bool = false,
    redirected: bool = false,
    can_stream: bool = false,
    is_http2: bool = false,

    fail: anyerror? = null,

    metadata: HTTPResponseMetadata? = null,
    body_size: BodySize = .unknown,
    certificate_info: CertificateInfo? = null,

    fn abortReason(): bun.jsc.CommonAbortReason? {
        if (_.isTimeout()) {
            return .Timeout;
        }

        if (_.isAbort()) {
            return .UserAbort;
        }

        return null;
    }

    fn isSuccess(): bool {
        return _.fail == null;
    }

    fn isTimeout(): bool {
        if (e := _.fail) {
            return e == error.Timeout;
        }
        return false;
    }

    fn isAbort(): bool {
        if (e := _.fail) {
            return e == error.Aborted || e == error.AbortedBeforeConnecting;
        }
        return false;
    }

    pub const BodySize = _BodySize;

    pub const Callback = _Callback;
}
