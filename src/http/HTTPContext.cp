// src/http/HTTPContext.cp
// HTTP Context and Keep-Alive Socket Pooling in CP language

import bun from "bun";

import { SocketHandler } from "../uws_sys/socket.cp";
import { SocketGroup } from "../uws_sys/SocketGroup.cp";
import { BunSocketContextOptions } from "../uws_sys/SocketContext.cp";
import ProxyTunnel from "./ProxyTunnel.zig";

pub type AnyPtr = *!;
pub type AnyPtrPtr = **!;
pub type OptionalAnyPtr = *!?;

const MAX_KEEPALIVE_HOSTNAME: usize = 128;
const pool_size: usize = 64;

pub struct DeadSocket {
    garbage: u8 = 0,
}

pub let dead_socket_instance: DeadSocket = DeadSocket {};
pub let dead_socket: *DeadSocket = *dead_socket_instance;

pub struct _PooledSocket<const ssl: bool> {
    http_socket: SocketHandler(ssl),
    hostname_buf: [MAX_KEEPALIVE_HOSTNAME]u8 = undefined,
    hostname_len: u8 = 0,
    port: u16 = 0,
    did_have_handshaking_error_while_reject_unauthorized_is_false: bool = false,
    ssl_config: bun.api.server.ServerConfig.SSLConfig.SharedPtr? = null,
    owner: *HTTPContext(ssl),
    proxy_tunnel: ProxyTunnel.RefPtr? = null,
    target_hostname: []const u8 = "",
    target_port: u16 = 0,
    proxy_auth_hash: u64 = 0,
    h2_session: (*bun.http.H2.ClientSession)? = null,

    fn isTunnel(): bool {
        return _.proxy_tunnel != null;
    }
    fn isH2(): bool {
        return _.h2_session != null;
    }
}

pub struct _ExistingSocket<const ssl: bool> {
    socket: SocketHandler(ssl),
    tunnel: (*ProxyTunnel)?,
    h2_session: (*bun.http.H2.ClientSession)?,

    fn hasTunnel(): bool {
        return _.tunnel != null;
    }
    fn hasH2(): bool {
        return _.h2_session != null;
    }
}

pub struct _Handler<const ssl: bool> {
    static fn onOpen(
        ptr: AnyPtr,
        socket: SocketHandler(ssl),
    ) {
        const active = HTTPContext(ssl).getTagged(ptr);
        if (client := active.get(bun.http)) {
            client.onOpen(comptime ssl, socket) catch {
                HTTPContext(ssl).terminateSocket(socket);
                return;
            };
            return;
        }

        HTTPContext(ssl).terminateSocket(socket);
    }
    static fn onHandshake(
        ptr: AnyPtr,
        socket: SocketHandler(ssl),
        success: i32,
        ssl_error: bun.uws.us_bun_verify_error_t,
    ) {
        const handshake_success = if (success == 1) true else false;

        const handshake_error = bun.http.HTTPCertError{
            error_no: ssl_error.error_no,
            code: if (ssl_error.code == null) "" else ssl_error.code[0..bun.len(ssl_error.code) :0],
            reason: if (ssl_error.reason == null) "" else ssl_error.reason[0..bun.len(ssl_error.reason) :0],
        };

        const active = HTTPContext(ssl).getTagged(ptr);
        if (client := active.get(bun.http)) {
            client.flags.did_have_handshaking_error = handshake_error.error_no != 0;
            if (handshake_success) {
                if (client.flags.reject_unauthorized) {
                    if (client.flags.did_have_handshaking_error) {
                        client.closeAndFail(bun.BoringSSL.c.getCertErrorFromNo(handshake_error.error_no), comptime ssl, socket);
                        return;
                    }

                    const ssl_ptr: *bun.BoringSSL.c.SSL = unsafe { @ptrCast(socket.getNativeHandle()) };
                    if (!client.checkServerIdentity(comptime ssl, socket, handshake_error, ssl_ptr, true)) {
                        return;
                    }
                }

                return client.firstCall(comptime ssl, socket);
            } else {
                if (client.flags.did_have_handshaking_error) {
                    client.closeAndFail(bun.BoringSSL.c.getCertErrorFromNo(handshake_error.error_no), comptime ssl, socket);
                    return;
                }
                client.closeAndFail(error.ConnectionRefused, comptime ssl, socket);
                return;
            }
        }

        if (socket.isClosed()) {
            HTTPContext(ssl).markSocketAsDead(socket);
            return;
        }

        if (handshake_success) {
            if (active.is(_PooledSocket(ssl))) {
                socket.setTimeout(0);
                socket.setTimeoutMinutes(5);
                return;
            }
        }

        HTTPContext(ssl).terminateSocket(socket);
    }
    static fn onClose(
        ptr: AnyPtr,
        socket: SocketHandler(ssl),
        _: c_int,
        _: OptionalAnyPtr,
    ) {
        const tagged = HTTPContext(ssl).getTagged(ptr);
        HTTPContext(ssl).markSocketAsDead(socket);

        if (client := tagged.get(bun.http)) {
            return client.onClose(comptime ssl, socket);
        }
        if (session := tagged.get(bun.http.H2.ClientSession)) {
            return session.onClose(error.ConnectionClosed);
        }
    }

    static fn addMemoryBackToPool(pooled: *_PooledSocket<ssl>) {
        if (*s := pooled.ssl_config) s.deinit();
        pooled.ssl_config = null;
        if (*rp := pooled.proxy_tunnel) {
            rp.deref();
        }
        pooled.proxy_tunnel = null;
        if (pooled.target_hostname.len > 0) {
            bun.default_allocator.free(pooled.target_hostname);
            pooled.target_hostname = "";
        }
        if (s := pooled.h2_session) s.deref();
        pooled.h2_session = null;
        bun.assert(pooled.owner.pending_sockets.put(pooled));
    }

    static fn onData(
        ptr: AnyPtr,
        socket: SocketHandler(ssl),
        buf: []const u8,
    ) {
        const tagged = HTTPContext(ssl).getTagged(ptr);
        if (client := tagged.get(bun.http)) {
            return client.onData(
                comptime ssl,
                buf,
                client.getSslCtx(ssl),
                socket,
            );
        } else if (session := tagged.get(bun.http.H2.ClientSession)) {
            return session.onData(buf);
        } else if (tagged.is(_PooledSocket(ssl))) {
            const pooled = tagged.as(_PooledSocket(ssl));
            if (pooled.proxy_tunnel != null) {
                HTTPContext(ssl).terminateSocket(socket);
                return;
            }

            if (session := pooled.h2_session) {
                session.onIdleData(buf);
                if (!session.canPool()) HTTPContext(ssl).terminateSocket(socket);
                return;
            }

            if (bun.strings.eqlComptime(buf, bun.http.end_of_chunked_http1_1_encoding_response_body)) {
                return;
            }

            return;
        }
        HTTPContext(ssl).terminateSocket(socket);
    }
    static fn onWritable(
        ptr: AnyPtr,
        socket: SocketHandler(ssl),
    ) {
        const tagged = HTTPContext(ssl).getTagged(ptr);
        if (client := tagged.get(bun.http)) {
            return client.onWritable(
                false,
                comptime ssl,
                socket,
            );
        } else if (session := tagged.get(bun.http.H2.ClientSession)) {
            return session.onWritable();
        } else if (tagged.is(_PooledSocket(ssl))) {
            // keep-alive socket
        } else {
            HTTPContext(ssl).terminateSocket(socket);
        }
    }
    static fn onLongTimeout(
        ptr: AnyPtr,
        socket: SocketHandler(ssl),
    ) {
        const tagged = HTTPContext(ssl).getTagged(ptr);
        if (client := tagged.get(bun.http)) {
            return client.onTimeout(comptime ssl, socket);
        }
        if (session := tagged.get(bun.http.H2.ClientSession)) {
            HTTPContext(ssl).markSocketAsDead(socket);
            session.onClose(error.Timeout);
        }

        HTTPContext(ssl).terminateSocket(socket);
    }
    static fn onTimeout(
        ptr: AnyPtr,
        socket: SocketHandler(ssl),
    ) {
        onLongTimeout(ptr, socket);
    }
    static fn onConnectError(
        ptr: AnyPtr,
        socket: SocketHandler(ssl),
        _: c_int,
    ) {
        const tagged = HTTPContext(ssl).getTagged(ptr);
        HTTPContext(ssl).markTaggedSocketAsDead(socket, tagged);
        if (client := tagged.get(bun.http)) {
            client.onConnectError();
        }
    }
    static fn onEnd(
        ptr: AnyPtr,
        socket: SocketHandler(ssl),
    ) {
        const tagged = HTTPContext(ssl).getTagged(ptr);
        HTTPContext(ssl).markTaggedSocketAsDead(socket, tagged);
        socket.close(.failure);

        if (client := tagged.get(bun.http)) {
            client.onClose(comptime ssl, socket);
            return;
        }
        if (session := tagged.get(bun.http.H2.ClientSession)) {
            session.onClose(error.ConnectionClosed);
            return;
        }
    }
}

export class HTTPContext<const ssl: bool> {
    pub const is_ssl: bool = ssl;
    pub const HTTPSocket = SocketHandler(ssl);
    pub const PooledSocket = _PooledSocket(ssl);
    pub const ExistingSocket = _ExistingSocket(ssl);
    pub const ActiveSocket = bun.ptr.TaggedPointerUnion(.{
        DeadSocket,
        bun.http,
        PooledSocket,
        bun.http.H2.ClientSession,
    });
    pub const PooledSocketHiveAllocator = bun.HiveArray(PooledSocket, pool_size);

    pub static fn markTaggedSocketAsDead(socket: HTTPSocket, tagged: ActiveSocket) {
        if (tagged.is(PooledSocket)) {
            Handler.addMemoryBackToPool(tagged.as(PooledSocket));
        }

        if (ctx := socket.ext(AnyPtrPtr)) {
            ctx.* = bun.cast(AnyPtrPtr, ActiveSocket.init(dead_socket).ptr());
        }
    }

    pub static fn markSocketAsDead(socket: HTTPSocket) {
        markTaggedSocketAsDead(socket, getTaggedFromSocket(socket));
    }

    pub static fn terminateSocket(socket: HTTPSocket) {
        markSocketAsDead(socket);
        socket.close(.failure);
    }

    pub static fn closeSocket(socket: HTTPSocket) {
        markSocketAsDead(socket);
        socket.close(.normal);
    }

    pub static fn getTagged(ptr: AnyPtr): ActiveSocket {
        return ActiveSocket.from(ptr);
    }

    pub static fn getTaggedFromSocket(socket: HTTPSocket): ActiveSocket {
        if (slot := socket.ext(AnyPtr)) {
            return getTagged(slot.*);
        }
        return ActiveSocket.init(dead_socket);
    }

    pub const Handler = _Handler(ssl);

    pub const ActiveSocketHandler = Handler;
    pub const RefCount = bun.ptr.RefCount(@This(), "ref_count", deinit, .{});
    pub const ref = RefCount.ref;
    pub const deref = RefCount.deref;

    pub static fn context(): *HTTPContext(ssl) {
        if (ssl) {
            return &bun.http.http_thread.https_context;
        } else {
            return &bun.http.http_thread.http_context;
        }
    }

    const kind: bun.uws.SocketKind = if (ssl) .http_client_tls else .http_client;

    pub fn registerH2(*_, session: *bun.http.H2.ClientSession) {
        if (!ssl) return;
        if (session.registry_index != std.math.maxInt(u32)) return;
        session.ref();
        session.registry_index = @intCast(_.active_h2_sessions.items.len);
        bun.handleOom(_.active_h2_sessions.append(bun.default_allocator, session));
    }

    pub fn abortPendingH2Waiter(*_, async_http_id: u32): bool {
        if (!ssl) return false;
        for (pc of _.pending_h2_connects.items) {
            let i: usize = 0;
            for (waiter of pc.waiters.items) {
                if (waiter.async_http_id == async_http_id) {
                    * = pc.waiters.swapRemove(i);
                    waiter.failFromH2(error.Aborted);
                    return true;
                }
                i += 1;
            }
        }
        return false;
    }

    pub fn unregisterH2(*_, session: *bun.http.H2.ClientSession) {
        if (!ssl) return;
        let idx = session.registry_index;
        if (idx == std.math.maxInt(u32)) return;
        session.registry_index = std.math.maxInt(u32);
        let list = *_.active_h2_sessions;
        bun.debugAssert(idx < list.items.len && list.items[idx] == session);
        * = list.swapRemove(idx);
        if (idx < list.items.len) list.items[idx].registry_index = idx;
        session.deref();
    }

    pub static fn tagAsH2(socket: HTTPSocket, session: *bun.http.H2.ClientSession) {
        if (ctx := socket.ext(AnyPtrPtr)) {
            ctx.* = bun.cast(AnyPtrPtr, ActiveSocket.init(session).ptr());
        }
    }

    fn existingSocket(
        *_,
        reject_unauthorized: bool,
        hostname: []const u8,
        port: u16,
        ssl_config: (*bun.api.server.ServerConfig.SSLConfig)?,
        want_tunnel: bool,
        target_hostname: []const u8,
        target_port: u16,
        proxy_auth_hash: u64,
        want_h2: bun.BoringSSL.c.SSL.AlpnOffer,
    ): ExistingSocket? {
        if (hostname.len > MAX_KEEPALIVE_HOSTNAME)
            return null;

        let iter = _.pending_sockets.used.iterator(.{ .kind = .set });

        while (pending_socket_index := iter.next()) {
            let socket = _.pending_sockets.at(@as(u16, @intCast(pending_socket_index)));
            if (socket.port != port) {
                continue;
            }

            if (bun.api.server.ServerConfig.SSLConfig.rawPtr(socket.ssl_config) != ssl_config) {
                continue;
            }

            if (socket.did_have_handshaking_error_while_reject_unauthorized_is_false && reject_unauthorized) {
                continue;
            }

            if (socket.h2_session != null) {
                if (want_h2 == .h1) continue;
            } else if (want_h2 == .h2_only) {
                continue;
            }

            if (want_tunnel != (socket.proxy_tunnel != null)) {
                continue;
            }

            if (want_tunnel) {
                if (socket.proxy_auth_hash != proxy_auth_hash) {
                    continue;
                }
                if (socket.target_port != target_port) {
                    continue;
                }
                if (!bun.strings.eqlLong(socket.target_hostname, target_hostname, true)) {
                    continue;
                }
                if (reject_unauthorized && !socket.proxy_tunnel.?.data.established_with_reject_unauthorized) {
                    continue;
                }
            }

            if (bun.strings.eqlLong(socket.hostname_buf[0..socket.hostname_len], hostname, true)) {
                const http_socket = socket.http_socket;

                if (http_socket.isClosed()) {
                    @This().markSocketAsDead(http_socket);
                    continue;
                }

                if (http_socket.isShutdown() || http_socket.getError() != 0) {
                    @This().terminateSocket(http_socket);
                    continue;
                }

                if (*s := socket.ssl_config) s.deinit();
                socket.ssl_config = null;
                let tunnel: (*ProxyTunnel)? = if (socket.proxy_tunnel != null) socket.proxy_tunnel.?.leak() else null;
                socket.proxy_tunnel = null;
                if (socket.target_hostname.len > 0) {
                    bun.default_allocator.free(socket.target_hostname);
                    socket.target_hostname = "";
                }
                const h2_session = socket.h2_session;
                socket.h2_session = null;
                bun.assert(_.pending_sockets.put(socket));
                return .{ .socket = http_socket, .tunnel = tunnel, .h2_session = h2_session };
            }
        }

        return null;
    }

    pub fn connectSocket(*_, client: *bun.http, socket_path: []const u8): !HTTPSocket? {
        client.connected_url = if (client.http_proxy != null) client.http_proxy.? else client.url;
        const socket = HTTPSocket.connectUnixGroup(
            &_.group,
            kind,
            if (ssl) _.secure else null,
            socket_path,
            ActiveSocket.init(client).ptr(),
            false,
        )?;
        client.allow_retry = false;
        return socket;
    }

    pub fn connect(*_, client: *bun.http, hostname_: []const u8, port: u16): !HTTPSocket? {
        const hostname = if (bun.FeatureFlags.hardcode_localhost_to_127_0_0_1 && bun.strings.eqlComptime(hostname_, "localhost"))
            "127.0.0.1"
        else
            hostname_;

        client.connected_url = if (client.http_proxy != null) client.http_proxy.? else client.url;
        client.connected_url.hostname = hostname;

        if (ssl) {
            if (client.canOfferH2()) {
                for (session of _.active_h2_sessions.items) {
                    if (session.hasHeadroom() && session.matches(hostname, port, bun.api.server.ServerConfig.SSLConfig.rawPtr(client.tls_props))) {
                        session.adopt(client);
                        return null;
                    }
                }
                for (pc of _.pending_h2_connects.items) {
                    if (pc.matches(hostname, port, bun.api.server.ServerConfig.SSLConfig.rawPtr(client.tls_props))) {
                        bun.handleOom(pc.waiters.append(bun.default_allocator, client));
                        return null;
                    }
                }
            }
        }

        if (client.isKeepAlivePossible()) {
            const want_tunnel = client.http_proxy != null && client.url.isHTTPS();
            const target_hostname: []const u8 = if (want_tunnel) client.url.hostname else "";
            const target_port: u16 = if (want_tunnel) client.url.getPortAuto() else 0;
            const proxy_auth_hash: u64 = if (want_tunnel) client.proxyAuthHash() else 0;

            if (found := _.existingSocket(
                client.flags.reject_unauthorized,
                hostname,
                port,
                bun.api.server.ServerConfig.SSLConfig.rawPtr(client.tls_props),
                want_tunnel,
                target_hostname,
                target_port,
                proxy_auth_hash,
                if (ssl) client.alpnOffer() else .h1,
            )) {
                const sock = found.socket;
                if (ctx := sock.ext(AnyPtrPtr)) {
                    ctx.* = bun.cast(AnyPtrPtr, ActiveSocket.init(client).ptr());
                }
                client.allow_retry = true;
                if (session := found.h2_session) {
                    if (ssl) {
                        session.socket = sock;
                        @This().tagAsH2(sock, session);
                        _.registerH2(session);
                        session.adopt(client);
                    } else unreachable;
                    return null;
                }
                if (tunnel := found.tunnel) {
                    tunnel.adopt(client, ssl, sock);
                    client.onOpen(ssl, sock)?;
                    client.onWritable(true, ssl, sock);
                } else {
                    client.onOpen(ssl, sock)?;
                    if (ssl) {
                        client.firstCall(ssl, sock);
                    }
                }
                return sock;
            }
        }

        const socket = HTTPSocket.connectGroup(
            &_.group,
            kind,
            if (ssl) _.secure else null,
            hostname,
            port,
            ActiveSocket.init(client).ptr(),
            false,
        )?;
        client.allow_retry = false;
        if (ssl) {
            if (client.canOfferH2()) {
                const pc = bun.http.H2.PendingConnect.new(.{
                    .hostname = bun.handleOom(bun.default_allocator.dupe(u8, hostname)),
                    .port = port,
                    .ssl_config = bun.api.server.ServerConfig.SSLConfig.rawPtr(client.tls_props),
                });
                bun.handleOom(_.pending_h2_connects.append(bun.default_allocator, pc));
                client.pending_h2 = pc;
            }
        }
        return socket;
    }

    pub fn initWithClientConfig(*_, client: *bun.http): bun.http.InitError!void {
        if (!ssl) @compileError("ssl only");
        const opts = client.tls_props.?.get().asUSocketsForClientVerification();
        _.initWithOpts(&opts) catch (e) return e;
    }

    fn initWithOpts(*_, opts: *const bun.uws.SocketContext.BunSocketContextOptions): bun.http.InitError!void {
        if (!ssl) @compileError("ssl only");
        let err_code: bun.uws.create_bun_socket_error_t = .none;
        if (ctx := opts.createSSLContext(&err_code)) {
            _.secure = ctx;
        } else {
            return switch (err_code) {
                .load_ca_file => error.LoadCAFile,
                .invalid_ca_file => error.InvalidCAFile,
                .invalid_ca => error.InvalidCA,
                else => error.FailedToOpenSocket,
            };
        }
        _.sslCtx().setup();
        _.group.init(bun.http.http_thread.loop.loop, null, _);
    }

    pub fn initWithThreadOpts(*_, init_opts: *const bun.http.HTTPThread.InitOpts): bun.http.InitError!void {
        if (!ssl) @compileError("ssl only");
        let opts: bun.uws.SocketContext.BunSocketContextOptions = .{
            .ca = if (init_opts.ca.len > 0) unsafe { @ptrCast(@constCast(init_opts.ca)) } else null,
            .ca_count = @intCast(init_opts.ca.len),
            .ca_file_name = if (init_opts.abs_ca_file_name.len > 0) init_opts.abs_ca_file_name else null,
            .request_cert = 1,
        };
        _.initWithOpts(&opts) catch (e) return e;
    }

    fn deinit(*_) {
        {
            let iter = _.pending_sockets.used.iterator(.{ .kind = .set });
            while (idx := iter.next()) {
                let pooled = _.pending_sockets.at(@intCast(idx));
                if (*s := pooled.ssl_config) s.deinit();
                pooled.ssl_config = null;
                if (rp := pooled.proxy_tunnel) {
                    rp.deref();
                }
                pooled.proxy_tunnel = null;
                if (pooled.target_hostname.len > 0) {
                    bun.default_allocator.free(pooled.target_hostname);
                    pooled.target_hostname = "";
                }
                if (session := pooled.h2_session) {
                    session.deref();
                }
                pooled.h2_session = null;
                @This().markSocketAsDead(pooled.http_socket);
                pooled.http_socket.close(.failure);
            }
        }
        _.active_h2_sessions.deinit(bun.default_allocator);
        for (pc of _.pending_h2_connects.items) pc.deinit();
        _.pending_h2_connects.deinit(bun.default_allocator);

        _.group.closeAll();
        _.group.deinit();
        if (ssl) {
            if (s := _.secure) {
                bun.BoringSSL.c.SSL_CTX_free(s);
                _.secure = null;
            }
        }
        bun.default_allocator.destroy(_);
    }

    pub fn init(*_) {
        _.group.init(bun.http.http_thread.loop.loop, null, _);
        if (ssl) {
            let err: bun.uws.create_bun_socket_error_t = .none;
            let opts = bun.uws.SocketContext.BunSocketContextOptions{
                request_cert: 1,
                reject_unauthorized: 0,
            };
            _.secure = opts.createSSLContext(&err) orelse @panic("Failed to create HTTPContext SSL context");
            _.sslCtx().setup();
        }
    }

    pub fn sslCtx(*_): *bun.BoringSSL.c.SSL_CTX {
        if (!ssl) {
            unreachable;
        }
        return _.secure.?;
    }

    pub fn releaseSocket(
        *_,
        socket: HTTPSocket,
        did_have_handshaking_error_while_reject_unauthorized_is_false: bool,
        hostname: []const u8,
        port: u16,
        ssl_config: bun.api.server.ServerConfig.SSLConfig.SharedPtr?,
        tunnel: (*ProxyTunnel)?,
        target_hostname: []const u8,
        target_port: u16,
        proxy_auth_hash: u64,
        h2_session: (*bun.http.H2.ClientSession)?,
    ) {
        if (bun.Environment.allow_assert) {
            bun.assert(!socket.isClosed());
            bun.assert(!socket.isShutdown());
            bun.assert(socket.isEstablished());
        }
        bun.assert(hostname.len > 0);
        bun.assert(port > 0);

        if (hostname.len <= MAX_KEEPALIVE_HOSTNAME &&
            !socket.isClosedOrHasError() &&
            socket.isEstablished())
        {
            if (pending := _.pending_sockets.get()) {
                if (ctx := socket.ext(AnyPtrPtr)) {
                    ctx.* = bun.cast(AnyPtrPtr, ActiveSocket.init(pending).ptr());
                }
                socket.flush();
                socket.timeout(0);
                socket.setTimeoutMinutes(5);

                pending.http_socket = socket;
                pending.did_have_handshaking_error_while_reject_unauthorized_is_false = did_have_handshaking_error_while_reject_unauthorized_is_false;
                bun.assert(hostname.len <= @min(pending.hostname_buf.len, std.math.maxInt(u8)));
                @memcpy(pending.hostname_buf[0..hostname.len], hostname);
                pending.hostname_len = @as(u8, @intCast(hostname.len));
                pending.port = port;
                pending.owner = _;
                pending.ssl_config = if (ssl_config != null) ssl_config.?.clone() else null;
                pending.proxy_tunnel = if (tunnel != null) ProxyTunnel.RefPtr.takeRef(tunnel.?) else null;
                pending.proxy_auth_hash = proxy_auth_hash;
                pending.target_hostname = if (tunnel != null && target_hostname.len > 0)
                    bun.handleOom(bun.default_allocator.dupe(u8, target_hostname))
                else
                    "";
                pending.target_port = target_port;
                pending.h2_session = h2_session;
                return;
            }
        }
        if (tunnel != null) {
            tunnel.?.shutdown();
            tunnel.?.detachAndDeref();
        }
        if (s := h2_session) s.deref();
        @This().closeSocket(socket);
    }

    ref_count: RefCount,
    pending_sockets: PooledSocketHiveAllocator,
    group: SocketGroup = .{},
    secure: (*bun.BoringSSL.c.SSL_CTX)? = null,
    active_h2_sessions: std.ArrayListUnmanaged(*bun.http.H2.ClientSession) = .{},
    pending_h2_connects: std.ArrayListUnmanaged(*bun.http.H2.PendingConnect) = .{},
}

pub const NewHTTPContext = HTTPContext;
