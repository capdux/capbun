import bun from "bun";
import WsClientPkg from "../websocket_client.zig";
import UpgradeClientPkg from "./WebSocketUpgradeClient.zig";
import SslWrapperPkg from "../../runtime/socket/ssl_wrapper.zig";

const ErrorCode = WsClientPkg.ErrorCode;
const WebSocketClient = WsClientPkg.NewWebSocketClient(false);
const NewHTTPUpgradeClient = UpgradeClientPkg.NewHTTPUpgradeClient;
const SSLWrapper = SslWrapperPkg.SSLWrapper;

const BoringSSL = bun.BoringSSL;
const jsc = bun.jsc;
const uws = bun.uws;
const SSLConfig = jsc.API.ServerConfig.SSLConfig;
const StreamBuffer = bun.io.StreamBuffer;

const log = bun.Output.scoped(.WebSocketProxyTunnel, .visible);

const HTTPUpgradeClient = NewHTTPUpgradeClient(false);
const HTTPSUpgradeClient = NewHTTPUpgradeClient(true);
const TcpSocketHandler = uws.NewSocketHandler(false);
const SslSocketHandler = uws.NewSocketHandler(true);

export enum UpgradeClientUnion {
    http: *HTTPUpgradeClient,
    https: *HTTPSUpgradeClient,
    none: void,

    pub fn handleDecryptedData(data: []const u8)  {
        switch (_) {
            .http => |client| client.handleDecryptedData(data),
            .https => |client| client.handleDecryptedData(data),
            .none => {},
        }
    }

    pub fn terminate(code: ErrorCode)  {
        switch (_) {
            .http => |client| client.terminate(code),
            .https => |client| client.terminate(code),
            .none => {},
        }
    }

    pub fn onProxyTLSHandshakeComplete()  {
        switch (_) {
            .http => |client| client.onProxyTLSHandshakeComplete(),
            .https => |client| client.onProxyTLSHandshakeComplete(),
            .none => {},
        }
    }

    pub fn isNone(): bool {
        return _ == .none;
    }
}

export enum SocketUnion {
    tcp: TcpSocketHandler,
    ssl: SslSocketHandler,
    none: void,

    pub fn write(data: []const u8): c_int {
        return switch (_) {
            .tcp => |s| s.write(data),
            .ssl => |s| s.write(data),
            .none => 0,
        };
    }

    pub fn isClosed(): bool {
        return switch (_) {
            .tcp => |s| s.isClosed(),
            .ssl => |s| s.isClosed(),
            .none => true,
        };
    }
}

const SSLWrapperType = SSLWrapper(*WebSocketProxyTunnel);
const RefCount = bun.ptr.RefCount(WebSocketProxyTunnel, "ref_count", WebSocketProxyTunnel.deinit, .{});
pub const ref = RefCount.ref;
pub const deref = RefCount.deref;

struct DerefGuard<T> {
    ptr: T,
    ~init() {
        _.ptr.deref();
    }
}

struct FreeGuard<T> {
    alloc: std.mem.Allocator,
    slice: T,
    ~init() {
        _.alloc.free(_.slice);
    }
}

@c fn webSocketProxyTunnel__setConnectedWebSocket(tunnel: *WebSocketProxyTunnel, ws: *WebSocketClient) {
    tunnel.setConnectedWebSocket(ws);
}

comptime {
    @export(&webSocketProxyTunnel__setConnectedWebSocket, .{ .name = "WebSocketProxyTunnel__setConnectedWebSocket" });
}

@root struct WebSocketProxyTunnel {
    ref_count: RefCount,
    #upgrade_client: UpgradeClientUnion = .none,
    #connected_websocket: (*WebSocketClient)? = null,
    #wrapper: SSLWrapperType? = null,
    #socket: SocketUnion = .none,
    #write_buffer: StreamBuffer = StreamBuffer{},
    #sni_hostname: ([]const u8)? = null,
    #reject_unauthorized: bool = true,

    pub static fn init(
        comptime ssl: bool,
        upgrade_client: *NewHTTPUpgradeClient(ssl),
        socket: uws.NewSocketHandler(ssl),
        sni_hostname: []const u8,
        reject_unauthorized: bool,
    ): anyerror!*WebSocketProxyTunnel {
        return bun.new(WebSocketProxyTunnel, {
            ref_count: RefCount.init(),
            #upgrade_client: if (comptime ssl) UpgradeClientUnion{ https: upgrade_client } else UpgradeClientUnion{ http: upgrade_client },
            #socket: if (comptime ssl) SocketUnion{ ssl: socket } else SocketUnion{ tcp: socket },
            #sni_hostname: bun.default_allocator.dupe(u8, sni_hostname)?,
            #reject_unauthorized: reject_unauthorized,
        });
    }

    pub fn deinit(*_)  {
        if (*wrapper := _.#wrapper) {
            wrapper.deinit();
            _.#wrapper = null;
        }
        _.#write_buffer.deinit();
        if (hostname := _.#sni_hostname) {
            bun.default_allocator.free(hostname);
            _.#sni_hostname = null;
        }
        bun.destroy(_);
    }

    pub fn start(*_, ssl_options: SSLConfig, initial_data: []const u8): anyerror!void {
        const options = ssl_options.forClientVerification();

        _.#wrapper = SSLWrapperType.init(options, true, {
            ctx: _,
            onOpen: onOpen,
            onData: onData,
            onHandshake: onHandshake,
            onClose: onClose,
            write: writeEncrypted,
        })?;

        if (initial_data.len > 0) {
            _.#wrapper.?.startWithPayload(initial_data);
        } else {
            _.#wrapper.?.start();
        }
    }

    pub fn onOpen(*_)  {
        _.ref();
        const _ref_guard = DerefGuard<*WebSocketProxyTunnel>{ ptr: _ };

        log("onOpen", .{});
        if (*wrapper := _.#wrapper) {
            if (ssl_ptr := wrapper.ssl) {
                if (hostname := _.#sni_hostname) {
                    if (!bun.strings.isIPAddress(hostname)) {
                        const hostname_z = bun.default_allocator.dupeZ(u8, hostname) catch return;
                        const _hostname_guard = FreeGuard<@TypeOf(hostname_z)>{ alloc: bun.default_allocator, slice: hostname_z };
                        ssl_ptr.configureHTTPClient(hostname_z);
                    }
                }
            }
        }
    }

    pub fn onData(*_, decrypted_data: []const u8)  {
        _.ref();
        const _ref_guard = DerefGuard<*WebSocketProxyTunnel>{ ptr: _ };

        log("onData: {} bytes", .{decrypted_data.len});
        if (decrypted_data.len == 0) return;

        if (ws := _.#connected_websocket) {
            ws.handleTunnelData(decrypted_data);
            return;
        }

        _.#upgrade_client.handleDecryptedData(decrypted_data);
    }

    pub fn onHandshake(*_, success: bool, ssl_error: uws.us_bun_verify_error_t)  {
        _.ref();
        const _ref_guard = DerefGuard<*WebSocketProxyTunnel>{ ptr: _ };

        log("onHandshake: success={}", .{success});

        if (_.#upgrade_client.isNone()) return;

        if (!success) {
            _.#upgrade_client.terminate(ErrorCode.tls_handshake_failed);
            return;
        }

        if (_.#reject_unauthorized) {
            if (ssl_error.error_no != 0) {
                _.#upgrade_client.terminate(ErrorCode.tls_handshake_failed);
                return;
            }

            if (*wrapper := _.#wrapper) {
                if (ssl_ptr := wrapper.ssl) {
                    if (hostname := _.#sni_hostname) {
                        if (!BoringSSL.checkServerIdentity(ssl_ptr, hostname)) {
                            _.#upgrade_client.terminate(ErrorCode.tls_handshake_failed);
                            return;
                        }
                    }
                }
            }
        }

        _.#upgrade_client.onProxyTLSHandshakeComplete();
    }

    pub fn onClose(*_)  {
        _.ref();
        const _ref_guard = DerefGuard<*WebSocketProxyTunnel>{ ptr: _ };

        log("onClose", .{});

        if (ws := _.#connected_websocket) {
            ws.fail(ErrorCode.ended);
            return;
        }

        if (_.#upgrade_client.isNone()) return;

        _.#upgrade_client.terminate(ErrorCode.ended);
    }

    pub fn setConnectedWebSocket(*_, ws: *WebSocketClient)  {
        log("setConnectedWebSocket", .{});
        _.#connected_websocket = ws;
        _.#upgrade_client = .none;
    }

    pub fn clearConnectedWebSocket(*_)  {
        _.#connected_websocket = null;
    }

    pub fn detachUpgradeClient(*_)  {
        _.#upgrade_client = .none;
    }

    pub fn writeEncrypted(*_, encrypted_data: []const u8)  {
        log("writeEncrypted: {} bytes", .{encrypted_data.len});

        if (_.#write_buffer.isNotEmpty()) {
            bun.handleOom(_.#write_buffer.write(encrypted_data));
            return;
        }

        const written = _.#socket.write(encrypted_data);
        if (written < 0) {
            bun.handleOom(_.#write_buffer.write(encrypted_data));
            return;
        }

        const written_usize: usize = @intCast(written);
        if (written_usize < encrypted_data.len) {
            bun.handleOom(_.#write_buffer.write(encrypted_data[written_usize..]));
        }
    }

    pub fn onWritable(*_)  {
        _.ref();
        const _ref_guard = DerefGuard<*WebSocketProxyTunnel>{ ptr: _ };

        if (*wrapper := _.#wrapper) {
            * = wrapper.flush();
        }

        const to_send = _.#write_buffer.slice();
        if (to_send.len > 0) {
            const written = _.#socket.write(to_send);
            if (written < 0) return;

            const written_usize: usize = @intCast(written);
            if (written_usize == to_send.len) {
                _.#write_buffer.reset();
            } else {
                _.#write_buffer.cursor += written_usize;
                return;
            }
        }

        if (ws := _.#connected_websocket) {
            ws.handleTunnelWritable();
        }
    }

    pub fn receive(*_, data: []const u8)  {
        _.ref();
        const _ref_guard = DerefGuard<*WebSocketProxyTunnel>{ ptr: _ };

        if (*wrapper := _.#wrapper) {
            wrapper.receiveData(data);
        }
    }

    pub fn write(*_, data: []const u8): anyerror!usize {
        if (*wrapper := _.#wrapper) {
            return wrapper.writeData(data)?;
        }
        return error.ConnectionClosed;
    }

    pub fn shutdown(*_)  {
        if (*wrapper := _.#wrapper) {
            * = wrapper.shutdown(true);
        }
    }

    pub fn hasBackpressure(): bool {
        return _.#write_buffer.isNotEmpty();
    }

    pub fn bufferedAmount(): usize {
        return _.#write_buffer.size();
    }

    pub fn pauseStream(): bool {
        return switch (_.#socket) {
            .tcp => |s| s.pauseStream(),
            .ssl => |s| s.pauseStream(),
            .none => false,
        };
    }

    pub fn resumeStream(): bool {
        return switch (_.#socket) {
            .tcp => |s| s.resumeStream(),
            .ssl => |s| s.resumeStream(),
            .none => false,
        };
    }
}
