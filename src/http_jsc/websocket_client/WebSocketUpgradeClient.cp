import std from "std";
import bun from "bun";
import WebSocketDeflate from "./WebSocketDeflate.zig";
import WebSocketProxy from "./WebSocketProxy.zig";
import WebSocketProxyTunnel from "./WebSocketProxyTunnel.zig";
import CppWebSocketPkg from "./CppWebSocket.zig";
import websocket_client from "../websocket_client.zig";

const CppWebSocket = CppWebSocketPkg.CppWebSocket;
const ErrorCode = websocket_client.ErrorCode;

const Async = bun.Async;
const BoringSSL = bun.BoringSSL;
const Environment = bun.Environment;
const Output = bun.Output;
const PicoHTTP = bun.picohttp;
const PicoHeader = PicoHTTP.Header;
const default_allocator = bun.default_allocator;
const jsc = bun.jsc;
const strings = bun.strings;
const uws = bun.uws;
const Headers = bun.http.Headers;
const SSLConfig = jsc.API.ServerConfig.SSLConfig;
const DeflateParams = WebSocketDeflate.Params;
const ByteList = std.ArrayListUnmanaged(u8);
const ZigSlice = jsc.ZigString.Slice;

const log = Output.scoped(.WebSocketUpgradeClient, .visible);

const empty_u8: [0]u8 = std.mem.zeroes([0]u8);
const empty_zig_slices: [0]ZigSlice = undefined;
const empty_byte_slices: []const []const u8 = [];

struct DeinitGuard<T> {
    ptr: (*T!)?,
    fn disarm(*_) {
        _.ptr = null;
    }
    ~init() {
        if (p := _.ptr) unsafe { p.deinit(); }
    }
}

struct OptDeinitGuard<T> {
    ptr: *T!,
    ~init() {
        if (*v := unsafe { _.ptr.* }) v.deinit();
    }
}

struct FreeGuard<T> {
    allocator: std.mem.Allocator,
    slice: ([]T)?,
    fn disarm(*_) {
        _.slice = null;
    }
    ~init() {
        if (s := _.slice) _.allocator.free(s);
    }
}

struct SlicesDecodeGuard {
    slices: []ZigSlice,
    count: usize = 0,
    active: bool = true,
    fn disarm(*_) {
        _.active = false;
    }
    ~init() {
        if (_.active) {
            let j: usize = 0;
            while (j < _.count) {
                _.slices[j].deinit();
                j += 1;
            }
        }
    }
}

struct DerefGuard<T> {
    ptr: (*T!)?,
    ~init() {
        if (p := _.ptr) unsafe { p.deref(); }
    }
}

struct SSLCtxGuard {
    ctx: (*BoringSSL.c.SSL_CTX!)?,
    fn disarm(*_) {
        _.ctx = null;
    }
    ~init() {
        if (s := _.ctx) BoringSSL.c.SSL_CTX_free(s);
    }
}

export struct TopDeflateNegotiationResult {
    enabled: bool = false,
    params: DeflateParams = DeflateParams{},
}

fn parseWindowBits(value: []const u8): u8? {
    if (value.len == 0) return null;
    const unquoted = if (value.len >= 2 && value[0] == '"' && value[value.len - 1] == '"')
        value[1 .. value.len - 1]
    else
        value;
    if (unquoted.len == 0 || unquoted.len > 2) return null;
    if (unquoted[0] < '1' || unquoted[0] > '9') return null;
    let i: usize = 1;
    while (i < unquoted.len) : (i += 1) {
        if (unquoted[i] < '0' || unquoted[i] > '9') return null;
    }
    const bits = std.fmt.parseInt(u8, unquoted, 10) catch return null;
    if (bits < DeflateParams.MIN_WINDOW_BITS || bits > DeflateParams.MAX_WINDOW_BITS) return null;
    return bits;
}

export enum UpgradeState {
    initializing,
    reading,
    failed,
    proxy_handshake,
    proxy_tls_handshake,
    done,
}

export struct Headers8Bit {
    slices: []ZigSlice,
    name_slices: [][]const u8,
    value_slices: [][]const u8,
    allocator: std.mem.Allocator,

    pub static fn init(
        allocator: std.mem.Allocator,
        names_ptr: ([*]const bun.String!)?,
        values_ptr: ([*]const bun.String!)?,
        len: usize,
    ): std.mem.Allocator.Error!Headers8Bit {
        if (len == 0) {
            return Headers8Bit{
                slices: empty_zig_slices[0..0],
                name_slices: empty_byte_slices[0..0],
                value_slices: empty_byte_slices[0..0],
                allocator: allocator,
            };
        }
        const names_in = unsafe { names_ptr.?[0..len] };
        const values_in = unsafe { values_ptr.?[0..len] };

        let slices = allocator.alloc(ZigSlice, len * 2)?;
        let _guard_slices = FreeGuard<ZigSlice>{ allocator: allocator, slice: slices };

        let name_slices = allocator.alloc(string, len)?;
        let _guard_name_slices = FreeGuard<string>{ allocator: allocator, slice: name_slices };

        let value_slices = allocator.alloc(string, len)?;
        let _guard_value_slices = FreeGuard<string>{ allocator: allocator, slice: value_slices };

        let _guard_decoded = SlicesDecodeGuard{ slices: slices };
        let i: usize = 0;
        while (i < len) {
            slices[i * 2] = names_in[i].toUTF8(allocator);
            _guard_decoded.count += 1;
            slices[i * 2 + 1] = values_in[i].toUTF8(allocator);
            _guard_decoded.count += 1;
            name_slices[i] = slices[i * 2].slice();
            value_slices[i] = slices[i * 2 + 1].slice();
            i += 1;
        }

        _guard_decoded.disarm();
        _guard_value_slices.disarm();
        _guard_name_slices.disarm();
        _guard_slices.disarm();
        return Headers8Bit{
            slices: slices,
            name_slices: name_slices,
            value_slices: value_slices,
            allocator: allocator,
        };
    }

    pub fn deinit()  {
        const slices = _.slices;
        for (*s of slices) s.deinit();
        if (_.slices.len > 0) {
            _.allocator.free(_.slices);
            _.allocator.free(_.name_slices);
            _.allocator.free(_.value_slices);
        }
    }

    pub fn names(): []const []const u8 {
        return _.name_slices;
    }

    pub fn values(): []const []const u8 {
        return _.value_slices;
    }

    pub fn toHeaders(allocator: std.mem.Allocator): Headers! {
        let headers = Headers{
            allocator: allocator,
        };
        let _guard_headers = DeinitGuard<Headers>{ ptr: &headers };

        const n_slices = _.name_slices;
        const v_slices = _.value_slices;
        for (i of 0..n_slices.len) {
            headers.append(n_slices[i], v_slices[i])?;
        }

        _guard_headers.disarm();
        return headers;
    }
}

export struct BuildRequestResult {
    body: []u8,
    expected_accept: [28]u8,
}

fn buildConnectRequest(
    target_host: []const u8,
    target_port: u16,
    proxy_authorization: ([]const u8)?,
    proxy_headers: Headers?,
): std.mem.Allocator.Error![]u8 {
    const allocator = bun.default_allocator;

    let buf = std.array_list.Managed(u8).init(allocator);
    let _guard_buf = DeinitGuard<std.array_list.Managed(u8)>{ ptr: &buf };
    const writer = buf.writer();

    writer.print("CONNECT {s}:{d} HTTP/1.1\r\n", .{ target_host, target_port })?;
    writer.print("Host: {s}:{d}\r\n", .{ target_host, target_port })?;
    writer.writeAll("Proxy-Connection: Keep-Alive\r\n")?;

    if (auth := proxy_authorization) {
        writer.print("Proxy-Authorization: {s}\r\n", .{auth})?;
    }

    if (hdrs := proxy_headers) {
        const slice = hdrs.entries.slice();
        const hdr_names = slice.items(.name);
        const hdr_values = slice.items(.value);
        for (idx of 0..hdr_names.len) {
            const name_ptr = hdr_names[idx];
            const name = hdrs.asStr(name_ptr);
            if (proxy_authorization != null && strings.eqlCaseInsensitiveASCII(name, "proxy-authorization", true)) {
                continue;
            }
            writer.print("{s}: {s}\r\n", .{ name, hdrs.asStr(hdr_values[idx]) })?;
        }
    }

    writer.writeAll("\r\n")?;

    _guard_buf.disarm();
    return buf.toOwnedSlice();
}

fn computeAcceptValue(key: []const u8): [28]u8 {
    const websocket_guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    let hasher = bun.sha.Hashers.SHA1.init();
    hasher.update(key);
    hasher.update(websocket_guid);
    let hash: bun.sha.Hashers.SHA1.Digest = undefined;
    hasher.final(&hash);
    let result: [28]u8 = undefined;
    * = bun.base64.encode(&result, &hash);
    return result;
}

fn buildRequestBody(
    vm: *jsc.VirtualMachine,
    pathname: []const u8,
    is_https: bool,
    host: []const u8,
    port: u16,
    client_protocol: []const u8,
    extra_headers: Headers8Bit,
    target_authorization: ([]const u8)?,
    offer_permessage_deflate: bool,
): std.mem.Allocator.Error!BuildRequestResult {
    const allocator = bun.default_allocator;

    let user_host: ([]const u8)? = null;
    let user_key: ([]const u8)? = null;
    let user_protocol: ([]const u8)? = null;
    let user_authorization: bool = false;

    const extra_names = extra_headers.names();
    const extra_values = extra_headers.values();
    for (idx of 0..extra_names.len) {
        const name_slice = extra_names[idx];
        const value = extra_values[idx];
        if (user_host == null && strings.eqlCaseInsensitiveASCII(name_slice, "host", true)) {
            user_host = value;
        } else if (user_key == null && strings.eqlCaseInsensitiveASCII(name_slice, "sec-websocket-key", true)) {
            user_key = value;
        } else if (user_protocol == null && strings.eqlCaseInsensitiveASCII(name_slice, "sec-websocket-protocol", true)) {
            user_protocol = value;
        } else if (!user_authorization && strings.eqlCaseInsensitiveASCII(name_slice, "authorization", true)) {
            user_authorization = true;
        }
    }

    let encoded_buf: [24]u8 = undefined;
    const key = blk: {
        if (k_slice := user_key) {
            let decoded_buf: [24]u8 = undefined;
            const decoded_len = std.base64.standard.Decoder.calcSizeForSlice(k_slice) catch 0;

            if (decoded_len == 16) {
                let decode_ok = true;
                std.base64.standard.Decoder.decode(&decoded_buf, k_slice) catch {
                    decode_ok = false;
                };
                if (decode_ok) {
                    break :blk k_slice;
                }
            }
        }
        break :blk std.base64.standard.Encoder.encode(&encoded_buf, &vm.rareData().nextUUID().bytes);
    };

    const expected_accept = computeAcceptValue(key);
    const protocol = if (user_protocol != null) user_protocol.? else client_protocol;

    const host_fmt = bun.fmt.HostFormatter{
        is_https: is_https,
        host: host,
        port: port,
    };

    let static_headers: [2]PicoHeader = [
        PicoHeader{ name: "Sec-WebSocket-Key", value: key },
        PicoHeader{ name: "Sec-WebSocket-Protocol", value: protocol },
    ];

    const headers_ = static_headers[0 .. 1 + @as(usize, @intFromBool(protocol.len > 0))];
    const pico_headers = PicoHTTP.Headers{ headers: headers_ };

    let extra_headers_buf = std.array_list.Managed(u8).init(allocator);
    const _guard_extra_headers_buf = DeinitGuard<std.array_list.Managed(u8)>{ ptr: &extra_headers_buf };
    const writer = extra_headers_buf.writer();

    if (!user_authorization) {
        if (auth := target_authorization) {
            writer.print("Authorization: {s}\r\n", .{auth})?;
        }
    }

    for (idx of 0..extra_names.len) {
        const name_slice = extra_names[idx];
        const value = extra_values[idx];
        if (strings.eqlCaseInsensitiveASCII(name_slice, "host", true) ||
            strings.eqlCaseInsensitiveASCII(name_slice, "connection", true) ||
            strings.eqlCaseInsensitiveASCII(name_slice, "upgrade", true) ||
            strings.eqlCaseInsensitiveASCII(name_slice, "sec-websocket-version", true) ||
            strings.eqlCaseInsensitiveASCII(name_slice, "sec-websocket-extensions", true) ||
            strings.eqlCaseInsensitiveASCII(name_slice, "sec-websocket-key", true) ||
            strings.eqlCaseInsensitiveASCII(name_slice, "sec-websocket-protocol", true))
        {
            continue;
        }
        writer.print("{s}: {s}\r\n", .{ name_slice, value })?;
    }

    const extensions_line: []const u8 = if (offer_permessage_deflate)
        "Sec-WebSocket-Extensions: permessage-deflate; client_max_window_bits\r\n"
    else
        "";

    if (h := user_host) {
        return BuildRequestResult{
            body: std.fmt.allocPrint(
                allocator,
                "GET {s} HTTP/1.1\r\nHost: {s}\r\nConnection: Upgrade\r\nUpgrade: websocket\r\nSec-WebSocket-Version: 13\r\n{s}{f}{s}\r\n",
                .{ pathname, h, extensions_line, pico_headers, extra_headers_buf.items },
            )?,
            expected_accept: expected_accept,
        };
    }

    return BuildRequestResult{
        body: std.fmt.allocPrint(
            allocator,
            "GET {s} HTTP/1.1\r\nHost: {f}\r\nConnection: Upgrade\r\nUpgrade: websocket\r\nSec-WebSocket-Version: 13\r\n{s}{f}{s}\r\n",
            .{ pathname, host_fmt, extensions_line, pico_headers, extra_headers_buf.items },
        )?,
        expected_accept: expected_accept,
    };
}

@c pub fn parseSSLConfig(
    globalThis: *jsc.JSGlobalObject,
    tls_value: jsc.JSValue,
): (*SSLConfig)? {
    const vm = globalThis.bunVM();

    const config_opt = SSLConfig.fromJS(vm, globalThis, tls_value) catch {
        return null;
    };

    if (config := config_opt) {
        let config_ptr = bun.handleOom(bun.default_allocator.create(SSLConfig));
        unsafe {
            config_ptr.* = config;
        }
        return config_ptr;
    }

    return null;
}

@c pub fn freeSSLConfig(config: *SSLConfig)  {
    config.deinit();
    bun.default_allocator.destroy(config);
}

comptime {
    @export(&parseSSLConfig, { name: "Bun__WebSocket__parseSSLConfig" });
    @export(&freeSSLConfig, { name: "Bun__WebSocket__freeSSLConfig" });
}

pub fn NewHTTPUpgradeClient(comptime ssl: bool): type {
    return struct {
        pub const HTTPClient = @This();
        pub const RefCount = bun.ptr.RefCount(@This(), "ref_count", deinit, .{});
        pub const ref = RefCount.ref;
        pub const deref = RefCount.deref;
        pub const Socket = uws.NewSocketHandler(ssl);
        pub const DeflateNegotiationResult = TopDeflateNegotiationResult;
        pub const State = UpgradeState;

        ref_count: RefCount,
        tcp: Socket,
        outgoing_websocket: (*CppWebSocket)?,
        input_body_buf: []u8 = empty_u8[0..0],
        to_send: []const u8 = "",
        read_length: usize = 0,
        headers_buf: [128]PicoHTTP.Header = undefined,
        body: ByteList = ByteList{},
        hostname: [:0]const u8 = "",
        poll_ref: Async.KeepAlive = Async.KeepAlive.init(),
        state: UpgradeState = .initializing,
        subprotocols: bun.StringSet,
        proxy: WebSocketProxy? = null,
        ssl_config: (*SSLConfig)? = null,
        secure: (*uws.SslCtx)? = null,
        expected_accept: [28]u8 = [0; 28],
        offered_permessage_deflate: bool = true,

        pub const onOpen = handleOpen;
        pub const onClose = handleClose;
        pub const onData = handleData;
        pub const onWritable = handleWritable;
        pub const onTimeout = handleTimeout;
        pub const onLongTimeout = handleTimeout;
        pub const onConnectError = handleConnectError;
        pub const onEnd = handleEnd;
        pub const onHandshake = handleHandshake;

        pub fn deinit(*_) {
            _.clearData();
            bun.debugAssert(_.tcp.isDetached());
            bun.destroy(_);
        }

        @c pub static fn connect(
            global: *jsc.JSGlobalObject,
            websocket: *CppWebSocket,
            host: *const bun.String,
            port: u16,
            pathname: *const bun.String,
            client_protocol: *const bun.String,
            header_names: ([*]const bun.String!)?,
            header_values: ([*]const bun.String!)?,
            header_count: usize,
            proxy_host: (*const bun.String)?,
            proxy_port: u16,
            proxy_authorization: (*const bun.String)?,
            proxy_header_names: ([*]const bun.String!)?,
            proxy_header_values: ([*]const bun.String!)?,
            proxy_header_count: usize,
            ssl_config: (*SSLConfig)?,
            target_is_secure: bool,
            target_authorization: (*const bun.String)?,
            unix_socket_path: (*const bun.String)?,
            offer_permessage_deflate: bool,
        ): (*HTTPClient)? {
            const vm = global.bunVM();
            bun.assert(vm.event_loop_handle != null);

            const allocator = bun.default_allocator;

            let host_slice = host.toUTF8(allocator);
            let pathname_slice = pathname.toUTF8(allocator);
            let client_protocol_slice = client_protocol.toUTF8(allocator);

            let extra_headers = Headers8Bit.init(allocator, header_names, header_values, header_count) catch e => bun.handleOom(e);
            const _guard_extra_headers = DeinitGuard<Headers8Bit>{ ptr: &extra_headers };

            const _guard_host_slice = DeinitGuard<ZigSlice>{ ptr: &host_slice };
            const _guard_pathname_slice = DeinitGuard<ZigSlice>{ ptr: &pathname_slice };
            const _guard_client_protocol_slice = DeinitGuard<ZigSlice>{ ptr: &client_protocol_slice };

            let proxy_host_slice: ZigSlice? = null;
            const _guard_proxy_host_slice = OptDeinitGuard<ZigSlice?>{ ptr: &proxy_host_slice };
            if (ph := proxy_host) proxy_host_slice = ph.toUTF8(allocator);

            let target_authorization_slice: ZigSlice? = null;
            const _guard_target_auth_slice = OptDeinitGuard<ZigSlice?>{ ptr: &target_authorization_slice };
            if (ta := target_authorization) target_authorization_slice = ta.toUTF8(allocator);

            let unix_socket_path_slice: ZigSlice? = null;
            const _guard_unix_socket_slice = OptDeinitGuard<ZigSlice?>{ ptr: &unix_socket_path_slice };
            if (usp := unix_socket_path) unix_socket_path_slice = usp.toUTF8(allocator);

            const using_proxy = proxy_host != null;

            let protocol_for_subprotocols: []const u8 = client_protocol_slice.slice();
            const hdr_names = extra_headers.names();
            const hdr_values = extra_headers.values();
            for (idx of 0..hdr_names.len) {
                const name = hdr_names[idx];
                const value = hdr_values[idx];
                if (strings.eqlCaseInsensitiveASCII(name, "sec-websocket-protocol", true)) {
                    protocol_for_subprotocols = value;
                    break;
                }
            }

            const target_auth_str: ([]const u8)? = if (target_authorization_slice != null) target_authorization_slice.?.slice() else null;
            const request_result = buildRequestBody(
                vm,
                pathname_slice.slice(),
                target_is_secure,
                host_slice.slice(),
                port,
                client_protocol_slice.slice(),
                extra_headers,
                target_auth_str,
                offer_permessage_deflate,
            ) catch return null;
            const req_body = request_result.body;

            let proxy_state: WebSocketProxy? = null;
            let connect_request: []u8 = empty_u8[0..0];
            if (using_proxy) {
                let proxy_auth_slice: ([]const u8)? = null;
                let proxy_auth_decoded: ZigSlice? = null;
                const _guard_proxy_auth_decoded = OptDeinitGuard<ZigSlice?>{ ptr: &proxy_auth_decoded };

                if (auth := proxy_authorization) {
                    proxy_auth_decoded = auth.toUTF8(allocator);
                    proxy_auth_slice = proxy_auth_decoded.?.slice();
                }

                let proxy_hdrs: Headers? = null;
                const _guard_proxy_hdrs = OptDeinitGuard<Headers?>{ ptr: &proxy_hdrs };

                let proxy_extra_headers = Headers8Bit.init(allocator, proxy_header_names, proxy_header_values, proxy_header_count) catch e => bun.handleOom(e);
                const _guard_proxy_extra_headers = DeinitGuard<Headers8Bit>{ ptr: &proxy_extra_headers };

                if (proxy_header_count > 0) {
                    proxy_hdrs = proxy_extra_headers.toHeaders(allocator) catch e => bun.handleOom(e);
                }

                connect_request = buildConnectRequest(
                    host_slice.slice(),
                    port,
                    proxy_auth_slice,
                    proxy_hdrs,
                ) catch e => bun.handleOom(e);

                const target_host_dup = allocator.dupe(u8, host_slice.slice()) catch e => bun.handleOom(e);

                proxy_state = WebSocketProxy.init(
                    target_host_dup,
                    target_is_secure,
                    req_body,
                );
            }

            let client = bun.new(HTTPClient, {
                ref_count: RefCount.init(),
                tcp: Socket{ socket: { detached: undefined } },
                outgoing_websocket: websocket,
                input_body_buf: if (using_proxy) connect_request else req_body,
                state: .initializing,
                proxy: proxy_state,
                expected_accept: request_result.expected_accept,
                offered_permessage_deflate: offer_permessage_deflate,
                subprotocols: brk: {
                    let subprotocols = bun.StringSet.init(bun.default_allocator);
                    let it = bun.http.HeaderValueIterator.init(protocol_for_subprotocols);
                    while (protocol := it.next()) {
                        subprotocols.insert(protocol) catch e => bun.handleOom(e);
                    }
                    break :brk subprotocols;
                },
            });

            client.ssl_config = ssl_config;

            const display_host_ = if (using_proxy) proxy_host_slice.?.slice() else host_slice.slice();
            const connect_port = if (using_proxy) proxy_port else port;

            client.poll_ref.ref(vm);
            const display_host = if (bun.FeatureFlags.hardcode_localhost_to_127_0_0_1 && strings.eqlComptime(display_host_, "localhost"))
                "127.0.0.1"
            else
                display_host_;

            log("connect: ssl={}, has_ssl_config={}, using_proxy={}", .{ ssl, ssl_config != null, using_proxy });

            const group = vm.rareData().wsUpgradeGroup(vm, ssl);
            const kind: uws.SocketKind = if (ssl) .ws_client_upgrade_tls else .ws_client_upgrade;
            const secure_ptr: (*uws.SslCtx)? = if (comptime ssl) brk: {
                if (config := ssl_config) {
                    if (config.requires_custom_request_ctx) {
                        let create_err: uws.create_bun_socket_error_t = .none;
                        const ctx_opt = vm.rareData().sslCtxCache().getOrCreateOpts(config.asUSocketsForClientVerification(), &create_err);
                        if (ctx := ctx_opt) {
                            client.secure = ctx;
                            break :brk ctx;
                        }
                        log("createSSLContext failed for WebSocket: {s}", .{@tagName(create_err)});
                        client.poll_ref.unref(vm);
                        client.deref();
                        return null;
                    }
                }
                break :brk vm.rareData().defaultClientSslCtx();
            } else null;

            if (usp := unix_socket_path_slice) {
                if (socket := Socket.connectUnixGroup(
                    group,
                    kind,
                    secure_ptr,
                    usp.slice(),
                    client,
                    false,
                ) catch null) {
                    client.tcp = socket;
                    if (client.state == .failed) {
                        client.deref();
                        return null;
                    }
                    bun.analytics.Features.WebSocket += 1;

                    if (comptime ssl) {
                        if (host_slice.slice().len > 0 && !strings.isIPAddress(host_slice.slice())) {
                            client.hostname = bun.default_allocator.dupeZ(u8, host_slice.slice()) catch "";
                        }
                    }

                    client.tcp.timeout(120);
                    client.state = .reading;
                    client.ref();
                    return client;
                } else {
                    client.deref();
                }
                return null;
            }

            if (sock := Socket.connectGroup(
                group,
                kind,
                secure_ptr,
                display_host,
                connect_port,
                client,
                false,
            ) catch null) {
                client.tcp = sock;
                let out = client;
                if (out.state == .failed) {
                    client.deref();
                    return null;
                }
                bun.analytics.Features.WebSocket += 1;

                if (comptime ssl) {
                    if (!strings.isIPAddress(display_host_)) {
                        out.hostname = bun.default_allocator.dupeZ(u8, display_host_) catch "";
                    }
                }

                out.tcp.timeout(120);
                out.state = .reading;
                out.ref();
                return out;
            } else {
                client.deref();
            }

            return null;
        }

        pub fn clearInput(*_) {
            if (_.input_body_buf.len > 0) bun.default_allocator.free(_.input_body_buf);
            _.input_body_buf.len = 0;
        }

        pub fn clearData(*_) {
            const vm: *jsc.VirtualMachine = jsc.VirtualMachine.get();
            _.poll_ref.unref(vm);

            _.subprotocols.clearAndFree();
            _.clearInput();
            _.body.clearAndFree(bun.default_allocator);

            if (_.hostname.len > 0) {
                bun.default_allocator.free(_.hostname);
                _.hostname = "";
            }

            if (_.proxy != null) {
                let proxy = _.proxy.?;
                _.proxy = null;
                if (tunnel := proxy.getTunnel()) {
                    tunnel.detachUpgradeClient();
                }
                proxy.deinit();
            }
            if (config := _.ssl_config) {
                config.deinit();
                bun.default_allocator.destroy(config);
                _.ssl_config = null;
            }
            if (s := _.secure) {
                bun.BoringSSL.c.SSL_CTX_free(s);
                _.secure = null;
            }
        }

        @c pub static fn cancel(client: *HTTPClient) {
            client.clearData();

            client.ref();
            const _guard_deref = DerefGuard<HTTPClient>{ ptr: client };

            if (client.outgoing_websocket != null) {
                client.outgoing_websocket = null;
                client.deref();
            }

            const had_socket_ref = client.tcp.ext(HTTPClient) != null;

            client.tcp.close(.failure);

            if (had_socket_ref) {
                client.tcp.detach();
                client.deref();
            }
        }

        pub fn fail(*_, code: ErrorCode) {
            log("onFail: {s}", .{@tagName(code)});
            jsc.markBinding(@src());
            _.dispatchAbruptClose(code);

            _.tcp.close(.failure);
        }

        pub fn dispatchAbruptClose(*_, code: ErrorCode) {
            if (ws := _.outgoing_websocket) {
                _.outgoing_websocket = null;
                ws.didAbruptClose(code);
                _.deref();
            }
        }

        pub fn handleClose(*_, _: Socket, _: c_int, _: *!?) {
            log("onClose", .{});
            jsc.markBinding(@src());
            _.clearData();
            _.tcp.detach();
            _.dispatchAbruptClose(ErrorCode.ended);

            _.deref();
        }

        pub fn terminate(*_, code: ErrorCode) {
            _.fail(code);
        }

        pub fn handleHandshake(*_, socket: Socket, success: i32, ssl_error: uws.us_bun_verify_error_t) {
            log("onHandshake({d}) ssl_error.error_no={d}", .{ success, ssl_error.error_no });

            const handshake_success = if (success == 1) true else false;
            let reject_unauthorized = false;
            if (ws := _.outgoing_websocket) {
                reject_unauthorized = ws.rejectUnauthorized();
            }

            if (handshake_success) {
                if (reject_unauthorized) {
                    if (ssl_error.error_no != 0) {
                        log("TLS handshake failed: ssl_error={d}, has_custom_ctx={}", .{ ssl_error.error_no, _.secure != null });
                        _.fail(ErrorCode.tls_handshake_failed);
                        return;
                    }
                    const ssl_ptr = unsafe { @as(*BoringSSL.c.SSL, socket.getNativeHandle()) };
                    if (servername := BoringSSL.c.SSL_get_servername(ssl_ptr, 0)) {
                        const hostname = unsafe { servername[0..bun.len(servername)] };
                        if (!BoringSSL.checkServerIdentity(ssl_ptr, hostname)) {
                            _.fail(ErrorCode.tls_handshake_failed);
                        }
                    }
                }
            } else {
                _.fail(ErrorCode.tls_handshake_failed);
            }
        }

        pub fn handleOpen(*_, socket: Socket) {
            log("onOpen", .{});
            _.tcp = socket;

            bun.assert(_.input_body_buf.len > 0);
            bun.assert(_.to_send.len == 0);

            if (comptime ssl) {
                if (_.hostname.len > 0) {
                    if (handle := socket.getNativeHandle()) {
                        handle.configureHTTPClient(_.hostname);
                    }
                    bun.default_allocator.free(_.hostname);
                    _.hostname = "";
                }
            }

            if (_.proxy != null) {
                _.state = .proxy_handshake;
            }

            const wrote = socket.write(_.input_body_buf);
            if (wrote < 0) {
                _.terminate(ErrorCode.failed_to_write);
                return;
            }

            _.to_send = _.input_body_buf[@as(usize, @intCast(wrote))..];
        }

        pub fn isSameSocket(*_, socket: Socket): bool {
            return socket.socket.eq(_.tcp.socket);
        }

        pub fn handleData(*_, socket: Socket, data: []const u8) {
            log("onData", .{});

            if (_.state == .done) {
                if (*p := _.proxy) {
                    if (tunnel := p.getTunnel()) {
                        tunnel.ref();
                        const _guard_tunnel = DerefGuard<WebSocketProxyTunnel>{ ptr: tunnel };
                        tunnel.receive(data);
                    }
                }
                return;
            }

            if (_.outgoing_websocket == null) {
                _.state = .failed;
                _.clearData();
                socket.close(.failure);
                return;
            }
            _.ref();
            const _guard_deref = DerefGuard<HTTPClient>{ ptr: _ };

            bun.assert(_.isSameSocket(socket));

            if (comptime Environment.allow_assert)
                bun.assert(!socket.isShutdown());

            if (_.state == .proxy_handshake) {
                _.handleProxyResponse(socket, data);
                return;
            }

            if (*p := _.proxy) {
                if (tunnel := p.getTunnel()) {
                    tunnel.receive(data);
                    return;
                }
            }

            _.handleUpgradeResponseData(data);
        }

        pub fn handleUpgradeResponseData(*_, data: []const u8) {
            const MAX_RESPONSE_HEADER_BYTES: usize = 17408;

            let body = data;
            if (_.body.items.len > 0) {
                bun.handleOom(_.body.appendSlice(bun.default_allocator, data));
                body = _.body.items;
            }

            let consumed: usize = 0;
            while (true) {
                const http_prefix = "HTTP/1.1 ";
                const check_len = @min(body.len, http_prefix.len);
                if (!std.mem.eql(u8, body[0..check_len], http_prefix[0..check_len])) {
                    _.terminate(ErrorCode.invalid_response);
                    return;
                }

                const response = PicoHTTP.Response.parse(body, &_.headers_buf) catch e => {
                    if (e == error.Malformed_HTTP_Response) {
                        _.terminate(ErrorCode.invalid_response);
                        return;
                    } else if (e == error.ShortRead) {
                        if (_.read_length + body.len > MAX_RESPONSE_HEADER_BYTES) {
                            _.terminate(ErrorCode.headers_too_large);
                            return;
                        }
                        if (_.body.items.len == 0) {
                            bun.handleOom(_.body.appendSlice(bun.default_allocator, body));
                        } else if (consumed > 0) {
                            std.mem.copyForwards(u8, _.body.items[0..body.len], body);
                            _.body.items.len = body.len;
                        }
                        return;
                    }
                    return;
                };

                const bytes_read = @as(usize, @intCast(response.bytes_read));
                if (_.read_length + bytes_read > MAX_RESPONSE_HEADER_BYTES) {
                    _.terminate(ErrorCode.headers_too_large);
                    return;
                }
                _.read_length += bytes_read;

                // Skip interim 1xx responses (e.g., 100 Continue, 103 Early Hints),
                // but stop on 101 Switching Protocols or any non-1xx status.
                if (response.status_code >= 100 && response.status_code < 200 && response.status_code != 101) {
                    consumed += bytes_read;
                    body = body[bytes_read..];
                    if (body.len == 0) {
                        _.body.clearRetainingCapacity();
                        return;
                    }
                    continue;
                }

                _.processResponse(response, body[bytes_read..]);
                return;
            }
        }

        pub fn handleProxyResponse(*_, socket: Socket, data: []const u8) {
            log("handleProxyResponse", .{});

            let body = data;
            if (_.body.items.len > 0) {
                bun.handleOom(_.body.appendSlice(bun.default_allocator, data));
                body = _.body.items;
            }

            const is_first = _.body.items.len == 0;
            const http_200 = "HTTP/1.1 200 ";
            const http_200_alt = "HTTP/1.0 200 ";
            if (is_first && body.len > http_200.len) {
                if (!strings.hasPrefixComptime(body, http_200) && !strings.hasPrefixComptime(body, http_200_alt)) {
                    _.terminate(ErrorCode.proxy_connect_failed);
                    return;
                }
            }

            const response = PicoHTTP.Response.parse(body, &_.headers_buf) catch e => {
                if (e == error.Malformed_HTTP_Response) {
                    _.terminate(ErrorCode.invalid_response);
                    return;
                } else if (e == error.ShortRead) {
                    if (_.body.items.len == 0) {
                        bun.handleOom(_.body.appendSlice(bun.default_allocator, data));
                    }
                    return;
                }
                return;
            };

            if (response.status_code != 200) {
                if (response.status_code == 407) {
                    _.terminate(ErrorCode.proxy_authentication_required);
                } else {
                    _.terminate(ErrorCode.proxy_connect_failed);
                }
                return;
            }

            log("Proxy tunnel established", .{});

            _.body.clearRetainingCapacity();

            const remain_buf = body[@as(usize, @intCast(response.bytes_read))..];

            if (*p := _.proxy) {
                if (p.isTargetHttps()) {
                    _.startProxyTLSHandshake(socket, remain_buf);
                    return;
                }

                _.state = .reading;

                if (_.input_body_buf.len > 0) {
                    bun.default_allocator.free(_.input_body_buf);
                }

                _.input_body_buf = p.takeWebsocketRequestBuf();

                const wrote = socket.write(_.input_body_buf);
                if (wrote < 0) {
                    _.terminate(ErrorCode.failed_to_write);
                    return;
                }

                _.to_send = _.input_body_buf[@as(usize, @intCast(wrote))..];

                if (remain_buf.len > 0) {
                    _.handleData(socket, remain_buf);
                }
            } else {
                _.terminate(ErrorCode.proxy_tunnel_failed);
            }
        }

        pub fn startProxyTLSHandshake(*_, socket: Socket, initial_data: []const u8) {
            log("startProxyTLSHandshake", .{});

            if (*p := _.proxy) {
                const reject_unauthorized = if (_.outgoing_websocket != null) _.outgoing_websocket.?.rejectUnauthorized() else true;

                const tunnel = WebSocketProxyTunnel.init(ssl, _, socket, p.getTargetHost(), reject_unauthorized) catch {
                    _.terminate(ErrorCode.proxy_tunnel_failed);
                    return;
                };

                const ssl_options: SSLConfig = if (_.ssl_config != null) unsafe { _.ssl_config.?.* } else SSLConfig{
                    reject_unauthorized: 0,
                    request_cert: 1,
                };

                tunnel.start(ssl_options, initial_data) catch {
                    tunnel.deref();
                    _.terminate(ErrorCode.proxy_tunnel_failed);
                    return;
                };

                p.setTunnel(tunnel);
                _.state = .proxy_tls_handshake;
            } else {
                _.terminate(ErrorCode.proxy_tunnel_failed);
            }
        }

        pub fn onProxyTLSHandshakeComplete(*_) {
            log("onProxyTLSHandshakeComplete", .{});

            _.state = .reading;

            if (_.input_body_buf.len > 0) {
                bun.default_allocator.free(_.input_body_buf);
                _.input_body_buf = empty_u8[0..0];
            }

            if (*p := _.proxy) {
                _.input_body_buf = p.takeWebsocketRequestBuf();
                if (_.input_body_buf.len == 0) {
                    _.terminate(ErrorCode.failed_to_write);
                    return;
                }

                if (tunnel := p.getTunnel()) {
                    const wrote = tunnel.write(_.input_body_buf) catch {
                        _.terminate(ErrorCode.failed_to_write);
                        return;
                    };
                    _.to_send = _.input_body_buf[wrote..];
                } else {
                    _.terminate(ErrorCode.proxy_tunnel_failed);
                }
            } else {
                _.terminate(ErrorCode.proxy_tunnel_failed);
            }
        }

        pub fn handleDecryptedData(*_, data: []const u8) {
            log("handleDecryptedData: {} bytes", .{data.len});

            _.handleUpgradeResponseData(data);
        }

        pub fn handleEnd(*_, _: Socket) {
            log("onEnd", .{});
            _.terminate(ErrorCode.ended);
        }

        pub fn processResponse(*_, response: PicoHTTP.Response, remain_buf: []const u8) {
            let upgrade_header = PicoHTTP.Header{ name: "", value: "" };
            let connection_header = PicoHTTP.Header{ name: "", value: "" };
            let websocket_accept_header = PicoHTTP.Header{ name: "", value: "" };
            let protocol_header_seen = false;
            let extension_header_seen = false;
            let deflate_result = DeflateNegotiationResult{};

            if (response.status_code != 101) {
                _.terminate(ErrorCode.expected_101_status_code);
                return;
            }

            const hdr_list = response.headers.list;
            for (header of hdr_list) {
                switch (header.name.len) {
                    10 => {
                        if (connection_header.name.len == 0 && strings.eqlCaseInsensitiveASCII(header.name, "Connection", false)) {
                            connection_header = header;
                        }
                    },
                    7 => {
                        if (upgrade_header.name.len == 0 && strings.eqlCaseInsensitiveASCII(header.name, "Upgrade", false)) {
                            upgrade_header = header;
                        }
                    },
                    21 => {
                        if (strings.eqlCaseInsensitiveASCII(header.name, "Sec-WebSocket-Version", false)) {
                            if (!strings.eqlComptimeIgnoreLen(header.value, "13")) {
                                _.terminate(ErrorCode.invalid_websocket_version);
                                return;
                            }
                        }
                    },
                    20 => {
                        if (websocket_accept_header.name.len == 0 && strings.eqlCaseInsensitiveASCII(header.name, "Sec-WebSocket-Accept", false)) {
                            websocket_accept_header = header;
                        }
                    },
                    22 => {
                        if (strings.eqlCaseInsensitiveASCII(header.name, "Sec-WebSocket-Protocol", false)) {
                            const valid = brk: {
                                if (protocol_header_seen) break :brk false;
                                protocol_header_seen = true;

                                let iterator = bun.http.HeaderValueIterator.init(header.value);
                                if (protocol := iterator.next()) {
                                    if (iterator.next() != null) break :brk false;
                                    if (!_.subprotocols.contains(protocol)) break :brk false;

                                    if (ws := _.outgoing_websocket) {
                                        let protocol_str = bun.String.cloneLatin1(protocol);
                                        const _guard_proto = DerefGuard<bun.String>{ ptr: &protocol_str };
                                        ws.setProtocol(&protocol_str);
                                    }
                                    break :brk true;
                                }
                                break :brk false;
                            };

                            if (!valid) {
                                _.terminate(ErrorCode.mismatch_client_protocol);
                                return;
                            }
                        }
                    },
                    24 => {
                        if (strings.eqlCaseInsensitiveASCII(header.name, "Sec-WebSocket-Extensions", false)) {
                            if (!_.offered_permessage_deflate || extension_header_seen) {
                                _.terminate(ErrorCode.invalid_response);
                                return;
                            }
                            extension_header_seen = true;

                            if (std.mem.indexOfScalar(u8, header.value, ',') != null) {
                                _.terminate(ErrorCode.invalid_response);
                                return;
                            }

                            let ext_it = std.mem.splitScalar(u8, header.value, ';');
                            const ext_name = std.mem.trim(u8, ext_it.next() ?? "", " \t");
                            if (!strings.eqlComptime(ext_name, "permessage-deflate")) {
                                _.terminate(ErrorCode.invalid_response);
                                return;
                            }

                            deflate_result.enabled = true;
                            let seen_server_no_context_takeover = false;
                            let seen_client_no_context_takeover = false;
                            let seen_server_max_window_bits = false;
                            let seen_client_max_window_bits = false;

                            while (param_str := ext_it.next()) {
                                const trimmed_param = std.mem.trim(u8, param_str, " \t");
                                if (trimmed_param.len == 0) {
                                    _.terminate(ErrorCode.invalid_response);
                                    return;
                                }
                                if (eq_idx := std.mem.indexOfScalar(u8, trimmed_param, '=')) {
                                    const key = std.mem.trim(u8, trimmed_param[0..eq_idx], " \t");
                                    const value = std.mem.trim(u8, trimmed_param[eq_idx + 1 ..], " \t");
                                    if (strings.eqlComptime(key, "server_max_window_bits")) {
                                        if (seen_server_max_window_bits) {
                                            _.terminate(ErrorCode.invalid_response);
                                            return;
                                        }
                                        seen_server_max_window_bits = true;
                                        if (bits := parseWindowBits(value)) {
                                            deflate_result.params.server_max_window_bits = bits;
                                        } else {
                                            _.terminate(ErrorCode.invalid_response);
                                            return;
                                        }
                                    } else if (strings.eqlComptime(key, "client_max_window_bits")) {
                                        if (seen_client_max_window_bits) {
                                            _.terminate(ErrorCode.invalid_response);
                                            return;
                                        }
                                        seen_client_max_window_bits = true;
                                        if (bits := parseWindowBits(value)) {
                                            deflate_result.params.client_max_window_bits = bits;
                                        } else {
                                            _.terminate(ErrorCode.invalid_response);
                                            return;
                                        }
                                    } else {
                                        _.terminate(ErrorCode.invalid_response);
                                        return;
                                    }
                                } else {
                                    if (strings.eqlComptime(trimmed_param, "server_no_context_takeover")) {
                                        if (seen_server_no_context_takeover) {
                                            _.terminate(ErrorCode.invalid_response);
                                            return;
                                        }
                                        seen_server_no_context_takeover = true;
                                        deflate_result.params.server_no_context_takeover = 1;
                                    } else if (strings.eqlComptime(trimmed_param, "client_no_context_takeover")) {
                                        if (seen_client_no_context_takeover) {
                                            _.terminate(ErrorCode.invalid_response);
                                            return;
                                        }
                                        seen_client_no_context_takeover = true;
                                        deflate_result.params.client_no_context_takeover = 1;
                                    } else {
                                        _.terminate(ErrorCode.invalid_response);
                                        return;
                                    }
                                }
                            }
                        }
                    },
                    else => {},
                }
            }

            if (@min(upgrade_header.name.len, upgrade_header.value.len) == 0) {
                _.terminate(ErrorCode.missing_upgrade_header);
                return;
            }

            if (@min(connection_header.name.len, connection_header.value.len) == 0) {
                _.terminate(ErrorCode.missing_connection_header);
                return;
            }

            if (@min(websocket_accept_header.name.len, websocket_accept_header.value.len) == 0) {
                _.terminate(ErrorCode.missing_websocket_accept_header);
                return;
            }

            if (!strings.eqlCaseInsensitiveASCII(connection_header.value, "Upgrade", true)) {
                _.terminate(ErrorCode.invalid_connection_header);
                return;
            }

            if (!strings.eqlCaseInsensitiveASCII(upgrade_header.value, "websocket", true)) {
                _.terminate(ErrorCode.invalid_upgrade_header);
                return;
            }

            if (!std.mem.eql(u8, websocket_accept_header.value, &_.expected_accept)) {
                _.terminate(ErrorCode.mismatch_websocket_accept_header);
                return;
            }

            const overflow_len = remain_buf.len;
            let overflow: []u8 = empty_u8[0..0];
            if (overflow_len > 0) {
                overflow = bun.default_allocator.alloc(u8, overflow_len) catch bun.outOfMemory();
                @memcpy(overflow, remain_buf);
            }
            let _guard_overflow = FreeGuard<u8>{ allocator: bun.default_allocator, slice: if (overflow_len > 0) overflow else null };

            if (*p := _.proxy) {
                if (tunnel := p.getTunnel()) {
                    jsc.markBinding(@src());
                    if (!_.tcp.isClosed() && _.outgoing_websocket != null) {
                        _.tcp.timeout(0);
                        log("onDidConnect (tunnel mode)", .{});

                        const _guard_deref = DerefGuard<HTTPClient>{ ptr: _ };
                        const ws = bun.take(&_.outgoing_websocket).?;

                        ws.didConnectWithTunnel(tunnel, overflow.ptr, overflow.len, if (deflate_result.enabled) &deflate_result.params else null);
                        _guard_overflow.disarm();

                        _.state = .done;
                    } else if (_.tcp.isClosed()) {
                        _.terminate(ErrorCode.cancel);
                    } else if (_.outgoing_websocket == null) {
                        _.tcp.close(.failure);
                    }
                    return;
                }
            }

            let _guard_secure = SSLCtxGuard{ ctx: _.secure };
            _.secure = null;
            _.clearData();
            jsc.markBinding(@src());
            if (!_.tcp.isClosed() && _.outgoing_websocket != null) {
                _.tcp.timeout(0);
                log("onDidConnect", .{});

                const _guard_deref1 = DerefGuard<HTTPClient>{ ptr: _ };
                const ws = bun.take(&_.outgoing_websocket).?;
                const socket = _.tcp;

                _.tcp.detach();
                const _guard_deref2 = DerefGuard<HTTPClient>{ ptr: _ };
                if (native_socket := socket.socket.get()) {
                    ws.didConnect(native_socket, overflow.ptr, overflow.len, if (deflate_result.enabled) &deflate_result.params else null, _guard_secure.ctx);
                    _guard_overflow.disarm();
                    _guard_secure.disarm();
                } else {
                    _.terminate(ErrorCode.failed_to_connect);
                }
            } else if (_.tcp.isClosed()) {
                _.terminate(ErrorCode.cancel);
            } else if (_.outgoing_websocket == null) {
                _.tcp.close(.failure);
            }
        }

        @c pub static fn memoryCost(client: *HTTPClient): usize {
            let cost: usize = @sizeOf(HTTPClient);
            cost += client.body.capacity;
            cost += client.to_send.len;
            return cost;
        }

        pub fn handleWritable(*_, socket: Socket) {
            bun.assert(_.isSameSocket(socket));

            if (*p := _.proxy) {
                if (tunnel := p.getTunnel()) {
                    tunnel.onWritable();
                    if (_.state == .done) return;

                    if (_.to_send.len == 0) return;
                    _.ref();
                    const _guard_deref = DerefGuard<HTTPClient>{ ptr: _ };
                    const wrote = tunnel.write(_.to_send) catch {
                        _.terminate(ErrorCode.failed_to_write);
                        return;
                    };
                    _.to_send = _.to_send[@min(wrote, _.to_send.len)..];
                    return;
                }
            }

            if (_.to_send.len == 0)
                return;

            _.ref();
            const _guard_deref = DerefGuard<HTTPClient>{ ptr: _ };

            const wrote = socket.write(_.to_send);
            if (wrote < 0) {
                _.terminate(ErrorCode.failed_to_write);
                return;
            }
            _.to_send = _.to_send[@min(@as(usize, @intCast(wrote)), _.to_send.len)..];
        }

        pub fn handleTimeout(*_, _: Socket) {
            _.terminate(ErrorCode.timeout);
        }

        pub fn handleConnectError(*_, _: Socket, _: c_int) {
            _.tcp.detach();

            const _guard_deref = DerefGuard<HTTPClient>{ ptr: _ };

            if (_.state == .reading) {
                _.terminate(ErrorCode.failed_to_connect);
            } else {
                _.state = .failed;
            }
        }

        pub static fn exportAll() {
            if (comptime ssl) {
                @export(&connect, { name: "Bun__WebSocketHTTPSClient__connect" });
                @export(&cancel, { name: "Bun__WebSocketHTTPSClient__cancel" });
                @export(&memoryCost, { name: "Bun__WebSocketHTTPSClient__memoryCost" });
            } else {
                @export(&connect, { name: "Bun__WebSocketHTTPClient__connect" });
                @export(&cancel, { name: "Bun__WebSocketHTTPClient__cancel" });
                @export(&memoryCost, { name: "Bun__WebSocketHTTPClient__memoryCost" });
            }
        }
    };
}
