import bun from "bun";
import * as ffi from "./ffi.cp";
import * as tls from "./tls.cp";
import * as stream from "./stream.cp";
import * as session from "./session.cp";
import * as quic from "./quic.cp";

const jsc = bun.jsc;
const ssl = bun.BoringSSL.c;
const uws = bun.uws;
const posix = std.posix;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const CallFrame = jsc.CallFrame;
const SocketAddress = bun.api.SocketAddress;
const EventLoopTimer = bun.api.Timer.EventLoopTimer;

pub @c struct EndpointState {
    bound: u8 = 0,
    receiving: u8 = 0,
    listening: u8 = 0,
    closing: u8 = 0,
    busy: u8 = 0,
    max_connections_per_host: u16 = 0,
    max_connections_total: u16 = 0,
    pending_callbacks: u64 = 0,
}

pub const ENDPOINT_STATS_FIELDS: [19][]const u8 = [
    "CREATED_AT",
    "DESTROYED_AT",
    "BYTES_RECEIVED",
    "BYTES_SENT",
    "PACKETS_RECEIVED",
    "PACKETS_SENT",
    "SERVER_SESSIONS",
    "CLIENT_SESSIONS",
    "SERVER_BUSY_COUNT",
    "RETRY_COUNT",
    "RETRY_RATE_LIMITED",
    "VERSION_NEGOTIATION_COUNT",
    "VERSION_NEGOTIATION_RATE_LIMITED",
    "STATELESS_RESET_COUNT",
    "STATELESS_RESET_RATE_LIMITED",
    "IMMEDIATE_CLOSE_COUNT",
    "IMMEDIATE_CLOSE_RATE_LIMITED",
    "SESSION_CREATION_RATE_LIMITED",
    "PACKETS_BLOCKED",
];

const IDX_STATS_CREATED_AT: usize = 0;
const IDX_STATS_DESTROYED_AT: usize = 1;
const IDX_STATS_BYTES_RECEIVED: usize = 2;
const IDX_STATS_BYTES_SENT: usize = 3;
const IDX_STATS_PACKETS_RECEIVED: usize = 4;
const IDX_STATS_PACKETS_SENT: usize = 5;
const IDX_STATS_SERVER_SESSIONS: usize = 6;
const IDX_STATS_CLIENT_SESSIONS: usize = 7;
const IDX_STATS_SERVER_BUSY_COUNT: usize = 8;
const IDX_STATS_STATELESS_RESET_COUNT: usize = 13;
const IDX_STATS_STATELESS_RESET_RATE_LIMITED: usize = 14;
const IDX_STATS_PACKETS_BLOCKED: usize = 18;

const QUIC_TRANSPORT_CONNECTION_REFUSED: c_uint = 0x2;
const QUIC_VERSION_1: u32 = 0x0000_0001;
const QUIC_VERSION_2: u32 = 0x6b33_43cf;
const INITIAL_TYPE_V1: u8 = 0b00;
const INITIAL_TYPE_V2: u8 = 0b01;
const MAX_CID_LEN: usize = 20;
const LONG_HEADER_FORM_BIT: u8 = 0x80;
const LONG_HEADER_TYPE_MASK: u8 = 0x3;
const LONG_HEADER_DCID_LEN_OFFSET: usize = 5;
const LONG_HEADER_MIN_LEN: usize = 7;
const DEFAULT_DATAGRAM_FRAME_SIZE: u64 = 1200;
const IPV4_ADDR_LEN: usize = 4;
const IPV6_ADDR_LEN: usize = 16;
const CRYPTO_ERROR_HANDSHAKE_FAILURE: u64 = 0x0100 + 40;
const PROVISIONAL_TIMEOUT_NS: u64 = 10_000_000_000;

pub const CLOSECONTEXT_CLOSE: u8 = 0;
pub const CLOSECONTEXT_BIND_FAILURE: u8 = 1;
pub const CLOSECONTEXT_START_FAILURE: u8 = 2;
pub const CLOSECONTEXT_RECEIVE_FAILURE: u8 = 3;
pub const CLOSECONTEXT_SEND_FAILURE: u8 = 4;
pub const CLOSECONTEXT_LISTEN_FAILURE: u8 = 5;

const DEFAULT_MAX_IDLE_TIMEOUT_SECS: u64 = 10;
pub const MS_PER_SEC: u64 = 1_000;

const HEADER_FORM_LONG: u8 = 0x80;
const STATELESS_RESET_MIN_LEN: usize = 21;
const LONG_HEADER_FIXED_BIT: u8 = 0x40;
const VERSION_NEGOTIATION_VERSION: [4]u8 = [ 0, 0, 0, 0 ];
const VERSION_FIELD_LEN: usize = 4;
const VERNEG_PROBE_CID_LEN: usize = 8;
const VERNEG_PROBE_LEN: usize = 1200;
const SHORT_HEADER_DCID_LEN: usize = 8;

pub fn storedAddrFromSockaddr(ptr: (*const posix.sockaddr)?): session.StoredAddr {
    const p = ptr ?? return session.StoredAddr.zero();
    const raw_bytes = unsafe { p as [*]const u8 };
    const fam: u16 = if (comptime bun.Environment.isMac)
        @as(u16, unsafe { raw_bytes[1] })
    else
        std.mem.readInt(u16, unsafe { raw_bytes[0..2] }, .little);
    const len: usize = if (fam == posix.AF.INET)
        session.SOCKADDR_IN_LEN
    else if (fam == posix.AF.INET6)
        session.SOCKADDR_IN6_LEN
    else
        0;
    return session.StoredAddr.fromRaw(raw_bytes, len);
}

fn storedAddrFromSocketAddress(addr: *const SocketAddress): session.StoredAddr {
    let out = session.StoredAddr.zero();
    const fam = addr.family();
    const port_be = std.mem.nativeToBig(u16, addr.port());
    if (fam == .INET) {
        const ip4 = std.mem.asBytes(&addr._addr.sin.addr);
        out.len = session.SOCKADDR_IN_LEN;
        if (comptime bun.Environment.isMac) {
            out.bytes[0] = session.SOCKADDR_IN_LEN;
            out.bytes[1] = @intCast(posix.AF.INET);
        } else {
            std.mem.writeInt(u16, out.bytes[0..2], @intCast(posix.AF.INET), .little);
        }
        @memcpy(out.bytes[2..4], std.mem.asBytes(&port_be));
        @memcpy(out.bytes[4..8], ip4);
    } else if (fam == .INET6) {
        const ip6 = std.mem.asBytes(&addr._addr.sin6.addr);
        out.len = session.SOCKADDR_IN6_LEN;
        if (comptime bun.Environment.isMac) {
            out.bytes[0] = session.SOCKADDR_IN6_LEN;
            out.bytes[1] = @intCast(posix.AF.INET6);
        } else {
            std.mem.writeInt(u16, out.bytes[0..2], @intCast(posix.AF.INET6), .little);
        }
        @memcpy(out.bytes[2..4], std.mem.asBytes(&port_be));
        @memcpy(out.bytes[8..24], ip6);
    }
    return out;
}

pub fn allocExposedArrayBuffer(
    global: *JSGlobalObject,
    holder: JSValue,
    name: []const u8,
    size: usize,
): bun.JSError!([*]u8) {
    const zeroes = bun.default_allocator.alloc(u8, size) catch bun.outOfMemory();
    defer bun.default_allocator.free(zeroes);
    @memset(zeroes, 0);
    const buf = jsc.ArrayBuffer.createBuffer(global, zeroes)?;
    const ab = buf.asArrayBuffer(global) ?? return global.throw("Failed to allocate QUIC state buffer", .{});
    holder.put(global, name, buf);
    return ab.ptr ?? return error.OutOfMemory;
}

pub struct SniEntry {
    host: []u8,
    ctx: tls.TlsContext,

    ~init() {
        bun.default_allocator.free(_.host);
        _.ctx.~init();
    }
}

pub struct ProvisionalSession {
    dcid: []u8,
    peer: session.StoredAddr,
    created_ns: u64,
    sess: *session.QuicSession,

    ~init() {
        bun.default_allocator.free(_.dcid);
    }
}

pub struct VernegEntry {
    sess: *session.QuicSession,
    cid: [VERNEG_PROBE_CID_LEN]u8,
    created_ns: u64,
}

pub struct DeadPeerEntry {
    peer: session.StoredAddr,
    at: u64,
}

pub struct EarlyKeylogEntry {
    ssl_ptr: *!,
    peer: session.StoredAddr,
    line: []u8,
}

pub struct BindConfig {
    host: []u8,
    port: u16,

    pub static fn initDefault(): BindConfig {
        return BindConfig{
            .host = bun.default_allocator.dupe(u8, "127.0.0.1\x00") catch bun.outOfMemory(),
            .port = 0,
        };
    }

    ~init() {
        if (_.host.len > 0) {
            bun.default_allocator.free(_.host);
            _.host = "";
        }
    }
}

pub struct EndpointInner {
    vtable: ffi.NqVtable,
    bind_config: BindConfig,
    this_value: jsc.JSRef = .empty(),
    poll_ref: bun.Async.KeepAlive = .{},
    server_tls: tls.TlsContext? = null,
    client_tls: tls.TlsContext? = null,
    sni_contexts: std.ArrayListUnmanaged(SniEntry) = .{},
    server_alpn_wire: std.ArrayListUnmanaged(u8) = .{},
    send_scratch: std.ArrayListUnmanaged(u8) = .{},
    client_alpn: []u8 = "",
    server_alpn: []u8 = "",
    server_session_options: jsc.Strong.Optional = .empty,
    origin_blob: []u8 = "",
    sessions: std.ArrayListUnmanaged(*session.QuicSession) = .{},
    pending_new_sessions: std.ArrayListUnmanaged(*session.QuicSession) = .{},
    provisional: std.ArrayListUnmanaged(ProvisionalSession) = .{},
    pending_verneg: std.ArrayListUnmanaged(VernegEntry) = .{},
    dead_provisional_peers: std.ArrayListUnmanaged(DeadPeerEntry) = .{},
    early_keylog: std.ArrayListUnmanaged(EarlyKeylogEntry) = .{},
    block_list_js: jsc.Strong.Optional = .empty,

    ~init() {
        const alloc = bun.default_allocator;
        _.bind_config.~init();
        _.poll_ref.disable();
        if (st := _.server_tls) st.~init();
        if (ct := _.client_tls) ct.~init();
        for (e of _.sni_contexts.items) e.~init();
        _.sni_contexts.deinit(alloc);
        _.server_alpn_wire.deinit(alloc);
        _.send_scratch.deinit(alloc);
        if (_.client_alpn.len > 0) alloc.free(_.client_alpn);
        if (_.server_alpn.len > 0) alloc.free(_.server_alpn);
        _.server_session_options.deinit();
        if (_.origin_blob.len > 0) alloc.free(_.origin_blob);
        _.sessions.deinit(alloc);
        _.pending_new_sessions.deinit(alloc);
        for (p of _.provisional.items) p.~init();
        _.provisional.deinit(alloc);
        _.pending_verneg.deinit(alloc);
        _.dead_provisional_peers.deinit(alloc);
        for (k of _.early_keylog.items) alloc.free(k.line);
        _.early_keylog.deinit(alloc);
        _.block_list_js.deinit();
    }
}

let endpoint_registry: std.ArrayListUnmanaged(*QuicEndpoint) = .{};
let lsquic_initialized: bool = false;

fn registryFindByAddr(addr: session.StoredAddr, not: *const QuicEndpoint): (*QuicEndpoint)? {
    const want_fam = addr.family();
    const want_port = addr.port();
    for (ep of endpoint_registry.items) {
        if (ep == not) continue;
        if (ep.local_addr.family() == want_fam && ep.local_addr.port() == want_port) {
            if (std.mem.eql(u8, ep.local_addr.bytes[0..ep.local_addr.len], addr.bytes[0..addr.len])) {
                return ep;
            }
        }
    }
    return null;
}

pub struct QuicEndpoint {
    pub const js = jsc.Codegen.JSQuicEndpoint;
    pub const toJS = js.toJS;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;

    event_loop_timer: EventLoopTimer = EventLoopTimer.initPaused(.QuicEndpoint),
    nq_driver: ffi.NqDriver = .{},
    nq_registered: bool = false,
    defer_closes: bool = false,
    send_scope_depth: u32 = 0,
    state: (*EndpointState)? = null,
    stats: ([*]u64)? = null,
    closing: bool = false,
    closed: bool = false,
    socket: (*uws.udp.Socket)? = null,
    local_addr: session.StoredAddr = undefined,
    server_engine: (*ffi.lsquic_engine)? = null,
    client_engine: (*ffi.lsquic_engine)? = null,
    server_is_http: bool = false,
    server_verify_client: bool = false,
    client_is_http: bool = false,
    disable_stateless_reset: bool = false,
    stateless_reset_burst: c_uint = 0,
    stateless_reset_rate: f64 = 0.0,
    processing: bool = false,
    followup_due: bool = false,
    server_local_tp: ffi.NqTransportParams = .{},
    client_local_tp: ffi.NqTransportParams = .{},
    block_list: (*bun.api.BlockList)? = null,
    block_list_allow: bool = false,
    pending_endpoint_close: bool = false,
    global: *JSGlobalObject,
    inner: *EndpointInner,

    pub static fn constructor(global: *JSGlobalObject, frame: *CallFrame, this_value: JSValue): bun.JSError!(*QuicEndpoint) {
        * = this_value;
        if (!lsquic_initialized) {
            lsquic_initialized = true;
            ffi.globalInit();
            if (bun.getenvZ("BUN_DEBUG_lsquic") != null) {
                ffi.enableLogging("debug");
            }
        }

        const inner = bun.@"new"(EndpointInner, .{
            .vtable = ffi.NqVtable{
                .owner = null,
                .on_new_conn = session.onNewConn,
                .on_hsk_done = session.onHskDone,
                .on_hsk_confirmed = session.onHskConfirmed,
                .on_goaway_received = session.onGoawayReceived,
                .on_conn_closed = session.onConnClosed,
                .on_conncloseframe = session.onConncloseframe,
                .on_new_token = session.onNewToken,
                .on_sess_resume = session.onSessResume,
                .on_new_stream = stream.onNewStream,
                .on_stream_read = stream.onStreamRead,
                .on_stream_write = stream.onStreamWrite,
                .on_stream_close = stream.onStreamClose,
                .on_stream_reset = stream.onStreamReset,
                .on_dg_write = session.onDgWrite,
                .on_datagram = session.onDatagram,
                .on_datagram_status = session.onDatagramStatus,
                .on_early_data_failed = session.onEarlyDataFailed,
                .on_path_switch = session.onPathSwitch,
                .on_origin = session.onOrigin,
                .get_ssl_ctx = getSslCtx,
                .get_client_ssl_ctx = getClientSslCtx,
                .lookup_cert = lookupCert,
                .packets_out = packetsOut,
                .on_mini_conn_failed = onMiniConnFailed,
            },
            .bind_config = BindConfig.initDefault(),
        });
        const ep = bun.@"new"(QuicEndpoint, .{
            .local_addr = session.StoredAddr.zero(),
            .global = global,
            .inner = inner,
        });
        ep.inner.vtable.owner = unsafe { ep as *! };

        const options = frame.argument(0);
        if (options.isObject()) {
            if (addr_val := options.get(global, "address")?) {
                if (addr := SocketAddress.fromJS(addr_val)) {
                    const stored = storedAddrFromSocketAddress(addr);
                    ep.inner.bind_config.port = stored.port();
                    // Extract host string via SocketAddress.address
                    let host_js = addr_val.get(global, "address")?;
                    if (h := host_js) {
                        if (h.isString()) {
                            let s = h.toBunString(global)?;
                            defer s.deref();
                            const utf8 = s.toUTF8(bun.default_allocator);
                            defer utf8.deinit();
                            bun.default_allocator.free(ep.inner.bind_config.host);
                            let hbuf = bun.default_allocator.allocSentinel(u8, utf8.slice().len, 0) catch bun.outOfMemory();
                            @memcpy(hbuf, utf8.slice());
                            ep.inner.bind_config.host = hbuf;
                        }
                    }
                }
            }
            if (bl_val := options.get(global, "blockList")?) {
                if (bl := bun.api.BlockList.fromJS(bl_val)) {
                    ep.block_list = bl;
                    ep.inner.block_list_js.set(global, bl_val);
                }
            }
            if (bl_allow := options.get(global, "blockListAllow")?) {
                ep.block_list_allow = bl_allow.toBoolean();
            }
            if (dsr := options.get(global, "disableStatelessReset")?) {
                ep.disable_stateless_reset = dsr.toBoolean();
            }
            if (burst := options.get(global, "statelessResetBurst")?) {
                if (burst.isNumber()) {
                    const n = burst.asNumber();
                    if (n >= 0.0) ep.stateless_reset_burst = @intFromFloat(n);
                }
            }
            if (rate := options.get(global, "statelessResetRate")?) {
                if (rate.isNumber()) {
                    const r = rate.asNumber();
                    if (r >= 0.0) ep.stateless_reset_rate = r;
                }
            }
        }

        const js_handle = ep.toJS(global);
        js_handle.ensureStillAlive();

        const state_ptr = allocExposedArrayBuffer(global, js_handle, "state", @sizeOf(EndpointState))?;
        const stats_ptr = allocExposedArrayBuffer(global, js_handle, "stats", ENDPOINT_STATS_FIELDS.len * @sizeOf(u64))?;
        js_handle.put(global, "stateByteOffset", JSValue.jsNumber(0));
        js_handle.put(global, "statsByteOffset", JSValue.jsNumber(0));

        ep.state = unsafe { state_ptr as *EndpointState };
        ep.stats = unsafe { stats_ptr as [*]u64 };
        if (st := ep.state) st.* = EndpointState{};
        ep.writeStat(IDX_STATS_CREATED_AT, quic.nowNs());
        ep.inner.this_value.setStrong(js_handle, global);
        return ep;
    }

    pub fn scopeHeld(): bool {
        return _.send_scope_depth > 0;
    }

    pub fn isHttp(is_server: bool): bool {
        return if (is_server) _.server_is_http else _.client_is_http;
    }

    pub fn verifyClient(): bool {
        return _.server_verify_client;
    }

    pub fn configuredAlpn(is_server: bool): ([]u8)? {
        const alpn = if (is_server) _.inner.server_alpn else _.inner.client_alpn;
        const trimmed = if (alpn.len > 0 && alpn[alpn.len - 1] == 0) alpn[0 .. alpn.len - 1] else alpn;
        if (trimmed.len == 0) return null;
        return bun.default_allocator.dupe(u8, trimmed) catch bun.outOfMemory();
    }

    fn writeStat(*_, idx: usize, value: u64) {
        if (*stats := _.stats) {
            if (idx < ENDPOINT_STATS_FIELDS.len) unsafe { stats[idx] = value; }
        }
    }

    fn addStat(*_, idx: usize, delta: u64) {
        if (*stats := _.stats) {
            if (idx < ENDPOINT_STATS_FIELDS.len) unsafe { stats[idx] +%= delta; }
        }
    }

    fn liveSession(p: *session.QuicSession): (*session.QuicSession)? {
        for (s of _.inner.sessions.items) {
            if (s == p) return p;
        }
        return null;
    }

    pub fn unregisterSession(*_, sess: *session.QuicSession) {
        let i: usize = 0;
        while (i < _.inner.sessions.items.len) {
            if (_.inner.sessions.items[i] == sess) {
                * = _.inner.sessions.swapRemove(i);
            } else {
                i += 1;
            }
        }
        _.updateKeepalive();
        if (_.closing && _.inner.sessions.items.len == 0 && _.engineConnCount() == 0 && !_.closed) {
            _.finishClose();
        }
    }

    fn linkLoopDriver(*_) {
        if (_.nq_registered) return;
        _.nq_registered = true;
        ffi.us_nq_loop_register(uws.Loop.get(), unsafe { &_.nq_driver }, unsafe { _ as *! });
    }

    fn unlinkLoopDriver(*_) {
        if (!_.nq_registered) return;
        _.nq_registered = false;
        ffi.us_nq_loop_unregister(uws.Loop.get(), unsafe { &_.nq_driver });
    }

    fn markDriverPending(*_) {
        _.nq_driver.pending = 1;
    }

    pub fn scheduleProcess(*_) {
        if (_.closed) return;
        _.markDriverPending();
        const vm = _.global.bunVM();
        if (_.event_loop_timer.state != .ACTIVE) {
            const next = bun.timespec.msFromNow(.force_real_time, 0);
            vm.timer.update(unsafe { &_.event_loop_timer }, unsafe { &next });
        }
    }

    fn runDriverPass(*_, defer_closes: bool) {
        if (_.closed) return;
        if (_.send_scope_depth != 0) {
            _.markDriverPending();
            return;
        }
        _.defer_closes = defer_closes;
        _.process(_.global);
        _.defer_closes = false;
    }

    fn ensureBound(*_, global: *JSGlobalObject, this_value: JSValue): bun.JSError!bool {
        if (_.socket != null) return true;
        let err: c_int = 0;
        const cfg = *_.inner.bind_config;
        const host_cstr: cstr = unsafe { cfg.host.ptr as cstr };
        const sock_opt = uws.udp.Socket.create(
            uws.Loop.get(),
            onData,
            onDrain,
            onClose,
            onRecvError,
            host_cstr,
            cfg.port,
            0,
            unsafe { &err },
            unsafe { _ as *! },
        );
        if (sock_opt == null) {
            _.inner.this_value.setStrong(this_value, global);
            _.finishClose();
            _.pending_endpoint_close = false;
            _.deliverEndpointClose(global, CLOSECONTEXT_BIND_FAILURE, err);
            return false;
        }
        const sock = sock_opt.?;
        _.linkLoopDriver();
        _.socket = sock;
        const port: u16 = @intCast(sock.boundPort());
        let ip: [IPV6_ADDR_LEN]u8 = @splat(0);
        let ip_len: i32 = @intCast(IPV6_ADDR_LEN);
        sock.boundIp(&ip, unsafe { &ip_len });
        const addr = if (ip_len == IPV4_ADDR_LEN)
            SocketAddress.initIPv4(ip[0..4].*, port)
        else if (ip_len == IPV6_ADDR_LEN)
            SocketAddress.initIPv6(ip, port, 0, 0)
        else
            SocketAddress.initIPv4([ 127, 0, 0, 1 ], port);
        _.local_addr = storedAddrFromSocketAddress(unsafe { &addr });
        if (st := _.state) {
            st.bound = 1;
            st.receiving = 1;
        }
        _.inner.this_value.setStrong(this_value, global);
        _.updateKeepalive();
        let already = false;
        for (item of endpoint_registry.items) {
            if (item == _) {
                already = true;
                break;
            }
        }
        if (!already) {
            endpoint_registry.append(bun.default_allocator, _) catch bun.outOfMemory();
        }
        return true;
    }

    fn updateKeepalive(*_) {
        if (_.closed || _.socket == null) return;
        const listening = if (st := _.state) st.listening != 0 else false;
        const busy = listening || _.inner.sessions.items.len > 0;
        const vm = _.global.bunVM();
        if (busy) {
            _.inner.poll_ref.ref(vm);
        } else {
            _.inner.poll_ref.unref(vm);
        }
    }

    fn engineConnCount(): c_uint {
        let count: c_uint = 0;
        if (se := _.server_engine) count += ffi.lsquic_engine_conn_count(se);
        if (ce := _.client_engine) count += ffi.lsquic_engine_conn_count(ce);
        return count;
    }

    pub fn process(*_, global: *JSGlobalObject) {
        if (_.closed || _.processing) return;
        _.processing = true;

        if (_.send_scope_depth == 0) {
            const copy = bun.default_allocator.dupe(*session.QuicSession, _.inner.sessions.items) catch bun.outOfMemory();
            defer bun.default_allocator.free(copy);
            for (s_ptr of copy) {
                if (sess := _.liveSession(s_ptr)) * = sess.flushPendingGraceful();
            }
        }

        _.send_scope_depth += 1;
        _.followup_due = false;

        if (!_.defer_closes) {
            const copy = bun.default_allocator.dupe(*session.QuicSession, _.inner.sessions.items) catch bun.outOfMemory();
            defer bun.default_allocator.free(copy);
            for (s_ptr of copy) {
                if (sess := _.liveSession(s_ptr)) sess.flushDeferredAborts();
            }
        }

        ssl.ERR_clear_error();
        if (se := _.server_engine) ffi.lsquic_engine_process_conns(se);
        if (ce := _.client_engine) ffi.lsquic_engine_process_conns(ce);
        _.processing = false;

        if (se := _.server_engine) {
            let sent: u64 = 0;
            let limited: u64 = 0;
            ffi.lsquic_engine_sreset_stats(se, unsafe { &sent }, unsafe { &limited });
            _.writeStat(IDX_STATS_STATELESS_RESET_COUNT, sent);
            _.writeStat(IDX_STATS_STATELESS_RESET_RATE_LIMITED, limited);
        }

        let keep_alive = jsc.Strong.Optional.create((_.inner.this_value.tryGet() ?? JSValue.js_undefined), global);
        defer keep_alive.deinit();

        while (_.inner.pending_new_sessions.items.len > 0) {
            const s_ptr = _.inner.pending_new_sessions.orderedRemove(0);
            const sess = _.liveSession(s_ptr) ?? continue;
            if (cb := quic.callbacksGet(global, "onSessionNew")) {
                const args: [1]JSValue = [sess.handle()];
                global.bunVM().eventLoop().runCallback(cb, global, (_.inner.this_value.tryGet() ?? JSValue.js_undefined), unsafe { &args });
            }
        }

        const sessions_copy = bun.default_allocator.dupe(*session.QuicSession, _.inner.sessions.items) catch bun.outOfMemory();
        defer bun.default_allocator.free(sessions_copy);
        for (s_ptr of sessions_copy) {
            const sess = _.liveSession(s_ptr) ?? continue;
            sess.processEvents(global);
            sess.maybeFinishDeferredClose();
        }

        _.sweepProvisional();
        _.rearmTimer();
        _.updateKeepalive();
        if (_.closing && _.inner.sessions.items.len == 0 && _.engineConnCount() == 0 && !_.closed) {
            _.finishClose();
        }
        _.send_scope_depth -= 1;

        if (_.send_scope_depth == 0) {
            if (se := _.server_engine) {
                if (ffi.lsquic_engine_has_unsent_packets(se) != 0) ffi.lsquic_engine_send_unsent_packets(se);
            }
            if (ce := _.client_engine) {
                if (ffi.lsquic_engine_has_unsent_packets(ce) != 0) ffi.lsquic_engine_send_unsent_packets(ce);
            }
        }
    }

    pub fn driveEnginesOnce(*_) {
        if (_.processing) return;
        _.processing = true;
        _.send_scope_depth += 1;
        ssl.ERR_clear_error();
        if (se := _.server_engine) {
            ffi.lsquic_engine_process_conns(se);
            if (ffi.lsquic_engine_has_unsent_packets(se) != 0) ffi.lsquic_engine_send_unsent_packets(se);
        }
        if (ce := _.client_engine) {
            ffi.lsquic_engine_process_conns(ce);
            if (ffi.lsquic_engine_has_unsent_packets(ce) != 0) ffi.lsquic_engine_send_unsent_packets(ce);
        }
        _.processing = false;
        _.send_scope_depth -= 1;
    }

    fn rearmTimer(*_) {
        if (_.closed) return;
        let min_us: i64 = -1;
        if (_.followup_due) {
            min_us = 0;
        } else {
            if (se := _.server_engine) {
                let diff: c_int = 0;
                if (ffi.lsquic_engine_earliest_adv_tick(se, unsafe { &diff }) != 0) {
                    const d: i64 = @max(diff, 0);
                    min_us = if (min_us < 0) d else @min(min_us, d);
                }
            }
            if (ce := _.client_engine) {
                let diff: c_int = 0;
                if (ffi.lsquic_engine_earliest_adv_tick(ce, unsafe { &diff }) != 0) {
                    const d: i64 = @max(diff, 0);
                    min_us = if (min_us < 0) d else @min(min_us, d);
                }
            }
        }
        const vm = _.global.bunVM();
        if (min_us >= 0) {
            const ms: i64 = @divTrunc(min_us + 999, 1000);
            const next = bun.timespec.msFromNow(.force_real_time, @intCast(@max(ms, 0)));
            vm.timer.update(unsafe { &_.event_loop_timer }, unsafe { &next });
        }
    }

    pub fn onTimerFire(*_, vm: *jsc.VirtualMachine) {
        * = vm;
        if (_.pending_endpoint_close) {
            _.pending_endpoint_close = false;
            _.deliverEndpointClose(_.global, CLOSECONTEXT_CLOSE, 0);
            return;
        }
        _.process(_.global);
    }

    pub fn bufferEarlyKeylog(*_, ssl_ptr: *!, peer: session.StoredAddr, line: []u8) {
        _.inner.early_keylog.append(bun.default_allocator, EarlyKeylogEntry{
            .ssl_ptr = ssl_ptr,
            .peer = peer,
            .line = line,
        }) catch bun.outOfMemory();
    }

    pub fn discardEarlyKeylog(*_, peer: session.StoredAddr) {
        let i: usize = 0;
        while (i < _.inner.early_keylog.items.len) {
            const e = _.inner.early_keylog.items[i];
            if (e.peer.family() == peer.family() && e.peer.port() == peer.port() &&
                std.mem.eql(u8, e.peer.bytes[0..e.peer.len], peer.bytes[0..peer.len]))
            {
                bun.default_allocator.free(e.line);
                * = _.inner.early_keylog.swapRemove(i);
            } else {
                i += 1;
            }
        }
    }

    pub fn takeAndPushEarlyKeylog(*_, ssl_ptr: *!?, sess: *session.QuicSession) {
        const s = ssl_ptr ?? return;
        let i: usize = 0;
        while (i < _.inner.early_keylog.items.len) {
            const e = _.inner.early_keylog.items[i];
            if (e.ssl_ptr == s) {
                * = _.inner.early_keylog.swapRemove(i);
                sess.pushKeylogEvent(e.line);
            } else {
                i += 1;
            }
        }
    }

    fn sweepProvisional(*_) {
        const now = quic.nowNs();
        let n_expired: usize = 0;
        let i: usize = 0;
        while (i < _.inner.provisional.items.len) {
            if (now -| _.inner.provisional.items[i].created_ns < PROVISIONAL_TIMEOUT_NS) {
                i += 1;
            } else {
                let p = _.inner.provisional.swapRemove(i);
                p.~init();
                if (sess := _.liveSession(p.sess)) {
                    sess.pushEvent(session.SessionEvent{ .peer_close = .{
                        .app_error = false,
                        .code = CRYPTO_ERROR_HANDSHAKE_FAILURE,
                        .reason = bun.default_allocator.dupe(u8, "handshake failed") catch bun.outOfMemory(),
                    } });
                    sess.pushEvent(session.SessionEvent.closed);
                    n_expired += 1;
                }
            }
        }
        let j: usize = 0;
        while (j < _.inner.pending_verneg.items.len) {
            if (now -| _.inner.pending_verneg.items[j].created_ns < PROVISIONAL_TIMEOUT_NS) {
                j += 1;
            } else {
                const v = _.inner.pending_verneg.swapRemove(j);
                if (sess := _.liveSession(v.sess)) {
                    sess.pushEvent(session.SessionEvent{ .peer_close = .{
                        .app_error = false,
                        .code = CRYPTO_ERROR_HANDSHAKE_FAILURE,
                        .reason = bun.default_allocator.dupe(u8, "handshake failed") catch bun.outOfMemory(),
                    } });
                    sess.pushEvent(session.SessionEvent.closed);
                    n_expired += 1;
                }
            }
        }
        if (n_expired > 0) _.followup_due = true;
        let k: usize = 0;
        while (k < _.inner.dead_provisional_peers.items.len) {
            if (now -| _.inner.dead_provisional_peers.items[k].at < PROVISIONAL_TIMEOUT_NS) {
                k += 1;
            } else {
                * = _.inner.dead_provisional_peers.swapRemove(k);
            }
        }
    }

    pub fn onNewConn(*_, conn: *ffi.lsquic_conn): *!? {
        const peer_opt = connPeerAddr(conn);
        if (peer := peer_opt) {
            let i: usize = 0;
            while (i < _.inner.provisional.items.len) {
                const p = _.inner.provisional.items[i];
                if (p.peer.family() == peer.family() && p.peer.port() == peer.port() &&
                    std.mem.eql(u8, p.peer.bytes[0..p.peer.len], peer.bytes[0..peer.len]))
                {
                    let removed = _.inner.provisional.swapRemove(i);
                    removed.~init();
                    if (live := _.liveSession(removed.sess)) {
                        live.bindConn(conn);
                        live.pushEvent(session.SessionEvent{ .handshake_done = true });
                        return unsafe { live as *!? };
                    }
                    break;
                }
                i += 1;
            }
        }

        if (_.closing) {
            ffi.lsquic_conn_abort_error(conn, 0, QUIC_TRANSPORT_CONNECTION_REFUSED, "");
            return null;
        }
        const max_conns = if (st := _.state) st.max_connections_total else 0;
        const busy = if (st := _.state) st.busy != 0 else false;
        if (busy || (max_conns > 0 && _.inner.sessions.items.len >= max_conns)) {
            ffi.lsquic_conn_abort_error(conn, 0, QUIC_TRANSPORT_CONNECTION_REFUSED, "");
            _.addStat(IDX_STATS_SERVER_BUSY_COUNT, 1);
            return null;
        }

        const sess = session.QuicSession.create(_.global, unsafe { &_.inner.vtable }, _, (_.inner.this_value.tryGet() ?? JSValue.js_undefined), conn, true) catch |e| {
            _.global.reportUncaughtExceptionFromError(e);
            ffi.lsquic_conn_abort_silent(conn);
            return null;
        };
        _.applyServerSessionOptions(_.global, sess);
        _.inner.sessions.append(bun.default_allocator, sess) catch bun.outOfMemory();
        _.inner.pending_new_sessions.append(bun.default_allocator, sess) catch bun.outOfMemory();
        _.addStat(IDX_STATS_SERVER_SESSIONS, 1);
        sess.pushEvent(session.SessionEvent{ .handshake_done = true });
        return unsafe { sess as *!? };
    }

    fn applyServerSessionOptions(*_, global: *JSGlobalObject, sess: *session.QuicSession) {
        if (opts := _.inner.server_session_options.get()) {
            sess.applyOptions(global, opts) catch |e| {
                global.reportUncaughtExceptionFromError(e);
            };
        }
    }

    fn teardown(*_): bool {
        if (_.closed) return false;
        _.closed = true;
        _.unlinkLoopDriver();
        let i: usize = 0;
        while (i < endpoint_registry.items.len) {
            if (endpoint_registry.items[i] == _) {
                * = endpoint_registry.swapRemove(i);
            } else {
                i += 1;
            }
        }
        if (_.event_loop_timer.state == .ACTIVE) {
            _.global.bunVM().timer.remove(unsafe { &_.event_loop_timer });
        }
        const sessions_copy = bun.default_allocator.dupe(*session.QuicSession, _.inner.sessions.items) catch bun.outOfMemory();
        defer bun.default_allocator.free(sessions_copy);
        for (s of sessions_copy) s.teardown(_.global);
        _.inner.sessions.clearRetainingCapacity();

        if (se := _.server_engine) {
            ffi.lsquic_engine_destroy(se);
            _.server_engine = null;
        }
        if (ce := _.client_engine) {
            ffi.lsquic_engine_destroy(ce);
            _.client_engine = null;
        }
        if (sock := _.socket) {
            sock.close();
            _.socket = null;
        }
        if (st := _.state) {
            st.closing = 1;
            st.bound = 0;
            st.receiving = 0;
            st.listening = 0;
        }
        _.writeStat(IDX_STATS_DESTROYED_AT, quic.nowNs());
        return true;
    }

    fn finishClose(*_) {
        if (!_.teardown()) return;
        _.inner.poll_ref.ref(_.global.bunVM());
        _.pending_endpoint_close = true;
        const next = bun.timespec.msFromNow(.force_real_time, 1);
        _.global.bunVM().timer.update(unsafe { &_.event_loop_timer }, unsafe { &next });
    }

    fn deliverEndpointClose(*_, global: *JSGlobalObject, context: u8, status: c_int) {
        _.inner.poll_ref.disable();
        if (cb := quic.callbacksGet(global, "onEndpointClose")) {
            const args: [2]JSValue = [ JSValue.jsNumber(context), JSValue.jsNumber(status) ];
            global.bunVM().eventLoop().runCallback(cb, global, (_.inner.this_value.tryGet() ?? JSValue.js_undefined), unsafe { &args });
        }
        _.inner.this_value.downgrade();
    }

    fn buildEngine(*_, is_server: bool, config: *const tls.TlsConfig, options: JSValue, global: *JSGlobalObject): bun.JSError!(*ffi.lsquic_engine) {
        const alpn_cstr = tls.TlsContext.alpnCstr(config);
        const is_http = alpnCstrIsHttp(alpn_cstr);
        if (is_server) {
            _.server_is_http = is_http;
            _.server_verify_client = config.verify_client;
            if (_.inner.server_alpn.len > 0) bun.default_allocator.free(_.inner.server_alpn);
            _.inner.server_alpn = alpn_cstr;
        } else {
            _.client_is_http = is_http;
            if (_.inner.client_alpn.len > 0) bun.default_allocator.free(_.inner.client_alpn);
            _.inner.client_alpn = alpn_cstr;
        }

        const tls_ctx = tls.TlsContext.init(config) catch {
            return global.throw("Failed to initialize TLS context", .{});
        };
        if (is_server) {
            if (*old := _.inner.server_tls) old.~init();
            _.inner.server_tls = tls_ctx;
        } else {
            if (*old := _.inner.client_tls) old.~init();
            _.inner.client_tls = tls_ctx;
        }

        let settings = ffi.Settings.init(is_server, is_http);
        defer settings.~init();
        const tp_dst = if (is_server) unsafe { &_.server_local_tp } else unsafe { &_.client_local_tp };
        applyEngineSettings(global, *settings, options, is_server, _.inner.origin_blob, tp_dst)?;
        if (is_server) {
            if (_.disable_stateless_reset) ffi.us_nq_settings_set_send_prst(settings.raw(), 0);
            if (_.stateless_reset_burst > 0) ffi.us_nq_settings_set_sreset_burst(settings.raw(), _.stateless_reset_burst);
            if (_.stateless_reset_rate > 0.0) ffi.us_nq_settings_set_sreset_rate(settings.raw(), _.stateless_reset_rate);
        }

        const alpn_ptr: [*:0]const u8? = if (alpn_cstr.len > 0) unsafe { alpn_cstr.ptr as [*:0]const u8 } else null;
        const eng = ffi.us_nq_engine_new(
            if (is_server) 1 else 0,
            if (is_http) 1 else 0,
            unsafe { &_.inner.vtable },
            settings.asPtr(),
            alpn_ptr,
        ) ?? return global.throw("Failed to create QUIC engine", .{});
        return eng;
    }

    pub fn listen(*_, global: *JSGlobalObject, frame: *CallFrame): bun.JSError!JSValue {
        if (_.closed || _.closing) {
            return global.ERR(.QUIC_ENDPOINT_CLOSED, "Endpoint is closed", .{}).throw();
        }
        const bound = _.ensureBound(global, frame.this())?;
        if (!bound) return JSValue.js_undefined;

        if (_.server_engine == null) {
            const options = frame.argument(0);
            if (options.isObject()) {
                _.inner.server_session_options.set(global, options);
            }
            const tls_val = if (options.isObject()) (options.get(global, "tls")? ?? JSValue.js_undefined) else JSValue.js_undefined;
            if (tls_val.isObject()) {
                if (origins := tls_val.get(global, "origins")?) {
                    if (origins.isArray()) {
                        let blob: std.ArrayListUnmanaged(u8) = .{};
                        errdefer blob.deinit(bun.default_allocator);
                        const len = origins.getLength(global)?;
                        let i: u32 = 0;
                        while (i < len) : (i += 1) {
                            const v = origins.getIndex(global, i)?;
                            if (!v.isString()) continue;
                            let s = v.toBunString(global)?;
                            defer s.deref();
                            const utf8 = s.toUTF8(bun.default_allocator);
                            defer utf8.deinit();
                            const sl = utf8.slice();
                            if (sl.len == 0 || sl.len > std.math.maxInt(u16)) continue;
                            let len_be: [2]u8 = undefined;
                            std.mem.writeInt(u16, &len_be, @intCast(sl.len), .big);
                            blob.appendSlice(bun.default_allocator, &len_be) catch bun.outOfMemory();
                            blob.appendSlice(bun.default_allocator, sl) catch bun.outOfMemory();
                        }
                        if (_.inner.origin_blob.len > 0) bun.default_allocator.free(_.inner.origin_blob);
                        _.inner.origin_blob = blob.toOwnedSlice(bun.default_allocator) catch bun.outOfMemory();
                    }
                }
            }
            let config = tls.TlsConfig.fromJS(global, tls_val, true)?;
            defer config.~init();
            if (config.alpn.items.len == 0) {
                config.alpn.appendSlice(bun.default_allocator, "\x02h3") catch bun.outOfMemory();
            }
            _.inner.server_alpn_wire.clearRetainingCapacity();
            _.inner.server_alpn_wire.appendSlice(bun.default_allocator, config.alpn.items) catch bun.outOfMemory();
            if (tls_val.isObject()) {
                if (sni_val := tls_val.get(global, "sni")?) {
                    if (sni_val.isObject()) {
                        buildSniContexts(global, sni_val, config.alpn.items, *_.inner.sni_contexts)?;
                    }
                }
            }
            _.server_engine = _.buildEngine(true, unsafe { &config }, options, global)?;
        }
        if (st := _.state) st.listening = 1;
        _.updateKeepalive();
        return JSValue.js_undefined;
    }

    pub fn connect(*_, global: *JSGlobalObject, frame: *CallFrame): bun.JSError!JSValue {
        if (_.closed || _.closing) {
            return global.ERR(.QUIC_ENDPOINT_CLOSED, "Endpoint is closed", .{}).throw();
        }
        const bound = _.ensureBound(global, frame.this())?;
        if (!bound) return JSValue.js_undefined;

        const address_val = frame.argument(0);
        const options = frame.argument(1);
        const ticket_val = frame.argument(2);

        const addr_opt = SocketAddress.fromJS(address_val);
        if (addr_opt == null) {
            return global.ERR(.INVALID_ARG_TYPE, "The \"address\" argument must be an instance of SocketAddress", .{}).throw();
        }
        const addr = addr_opt.?;
        const remote = storedAddrFromSocketAddress(addr);
        const tls_val = if (options.isObject()) (options.get(global, "tls")? ?? JSValue.js_undefined) else JSValue.js_undefined;
        let config = tls.TlsConfig.fromJS(global, tls_val, false)?;
        defer config.~init();
        if (config.alpn.items.len == 0) {
            config.alpn.appendSlice(bun.default_allocator, "\x02h3") catch bun.outOfMemory();
        }

        if (_.client_engine == null) {
            _.client_engine = _.buildEngine(false, unsafe { &config }, options, global)?;
        } else {
            const fresh = tls.TlsContext.init(unsafe { &config }) catch {
                return global.throw("Failed to initialize TLS context", .{});
            };
            if (*old := _.inner.client_tls) old.~init();
            _.inner.client_tls = fresh;
        }

        let ticket_bytes: []u8 = "";
        defer if (ticket_bytes.len > 0) bun.default_allocator.free(ticket_bytes);
        if (!ticket_val.isEmptyOrUndefinedOrNull()) {
            if (ab := ticket_val.asArrayBuffer(global)) {
                ticket_bytes = bun.default_allocator.dupe(u8, ab.byteSlice()) catch bun.outOfMemory();
            }
        }

        const sess = session.QuicSession.create(global, unsafe { &_.inner.vtable }, _, frame.this(), null, false)?;
        sess.applyOptions(global, options)?;
        _.inner.sessions.append(bun.default_allocator, sess) catch bun.outOfMemory();
        _.addStat(IDX_STATS_CLIENT_SESSIONS, 1);

        const local_sa = _.local_addr.asSockaddrPtr() ?? return global.throw("Endpoint has no local address", .{});
        const peer_sa = remote.asSockaddrPtr() ?? return global.throw("Invalid remote address", .{});
        const sni_ptr: [*:0]const u8? = if (sn := config.servername) unsafe { sn.ptr as [*:0]const u8 } else null;
        const resume_ptr: [*]const u8? = if (ticket_bytes.len > 0) ticket_bytes.ptr else null;

        const conn_opt = ffi.lsquic_engine_connect(
            _.client_engine.?,
            ffi.N_LSQVER,
            local_sa,
            peer_sa,
            unsafe { _ as *! },
            unsafe { sess as *! },
            sni_ptr,
            0,
            resume_ptr,
            ticket_bytes.len,
            null,
            0,
        );
        if (conn_opt == null) {
            _.unregisterSession(sess);
            sess.teardown(global);
            return global.ERR(.QUIC_CONNECTION_FAILED, "Failed to initiate QUIC connection", .{}).throw();
        }
        const conn = conn_opt.?;
        sess.bindConn(conn);
        _.scheduleProcess();
        return sess.handle();
    }

    pub fn closeGracefully(*_, global: *JSGlobalObject, frame: *CallFrame): JSValue {
        if (_.closed || _.closing) return JSValue.js_undefined;
        _.closing = true;
        if (st := _.state) {
            st.closing = 1;
            st.listening = 0;
        }
        if ((_.inner.this_value.tryGet() ?? JSValue.js_undefined).isEmptyOrUndefinedOrNull()) {
            _.inner.this_value.setStrong(frame.this(), global);
        }
        if (_.inner.sessions.items.len == 0 && _.engineConnCount() == 0) {
            _.finishClose();
        } else {
            _.scheduleProcess();
        }
        return JSValue.js_undefined;
    }

    pub fn releaseSocket(*_, global: *JSGlobalObject, frame: *CallFrame): JSValue {
        * = global;
        * = frame;
        _.pending_endpoint_close = false;
        * = _.teardown();
        _.inner.poll_ref.disable();
        return JSValue.js_undefined;
    }

    pub fn address(*_, global: *JSGlobalObject, frame: *CallFrame): JSValue {
        * = frame;
        return _.local_addr.toJSSocketAddress(global);
    }

    pub fn markBusy(*_, global: *JSGlobalObject, frame: *CallFrame): JSValue {
        * = global;
        const busy = frame.argument(0).toBoolean();
        if (st := _.state) st.busy = if (busy) 1 else 0;
        return JSValue.js_undefined;
    }

    pub fn doRef(*_, global: *JSGlobalObject, frame: *CallFrame): JSValue {
        * = global;
        const want = frame.argument(0).toBoolean();
        const vm = _.global.bunVM();
        if (want) {
            _.inner.poll_ref.ref(vm);
        } else {
            _.inner.poll_ref.unref(vm);
        }
        return JSValue.js_undefined;
    }

    pub fn setSniContexts(*_, global: *JSGlobalObject, frame: *CallFrame): bun.JSError!JSValue {
        if (_.closed) {
            return global.ERR(.QUIC_ENDPOINT_CLOSED, "Endpoint is closed", .{}).throw();
        }
        const entries = frame.argument(0);
        const replace = frame.argument(1).toBoolean();
        if (!entries.isObject()) {
            return global.ERR(.INVALID_ARG_TYPE, "The \"entries\" argument must be of type object", .{}).throw();
        }
        if (replace) {
            for (*e of _.inner.sni_contexts.items) e.~init();
            _.inner.sni_contexts.clearRetainingCapacity();
        }
        const alpn: []const u8 = if (_.inner.server_alpn_wire.items.len > 0) _.inner.server_alpn_wire.items else "\x02h3";
        buildSniContexts(global, entries, alpn, *_.inner.sni_contexts)?;
        return JSValue.js_undefined;
    }

    pub fn finalize(*_) {
        * = _.teardown();
        bun.destroy(_.inner);
        bun.destroy(_);
    }
}

fn alpnCstrIsHttp(alpn_cstr: []const u8): bool {
    const s = if (alpn_cstr.len > 0 && alpn_cstr[alpn_cstr.len - 1] == 0)
        alpn_cstr[0 .. alpn_cstr.len - 1]
    else
        alpn_cstr;
    return s.len == 0 || std.mem.eql(u8, s, "h3") || std.mem.startsWith(u8, s, "h3-");
}

fn buildSniContexts(
    global: *JSGlobalObject,
    entries: JSValue,
    alpn: []const u8,
    out: *std.ArrayListUnmanaged(SniEntry),
)! {
    let it = jsc.JSPropertyIterator(.{ .skip_empty_name = true, .include_value = true }).init(global, entries.getObject() orelse return)?;
    defer it.deinit();
    while (key_bun := it.next()?) {
        const utf8 = key_bun.toUTF8(bun.default_allocator);
        defer utf8.deinit();
        const host = utf8.slice();
        const val = it.value;
        if (!val.isObject()) continue;
        let config = tls.TlsConfig.fromJS(global, val, true)?;
        defer config.~init();
        if (config.alpn.items.len == 0) {
            config.alpn.appendSlice(bun.default_allocator, alpn) catch bun.outOfMemory();
        }
        const ctx = tls.TlsContext.init(unsafe { &config }) catch {
            return global.ERR(.INVALID_ARG_VALUE, "tls: invalid SNI context", .{}).throw();
        };
        const host_copy = bun.default_allocator.dupe(u8, host) catch bun.outOfMemory();
        out.append(bun.default_allocator, SniEntry{ .host = host_copy, .ctx = ctx }) catch bun.outOfMemory();
    }
}

fn readU64Opt(global: *JSGlobalObject, obj: JSValue, key: []const u8): bun.JSError!(u64?) {
    if (!obj.isObject()) return null;
    const v = obj.get(global, key)? ?? return null;
    if (v.isEmptyOrUndefinedOrNull()) return null;
    if (v.isBigInt()) return v.toUInt64NoTruncate();
    if (v.isNumber()) {
        const n = v.asNumber();
        if (n >= 0.0) return @intFromFloat(n);
    }
    return null;
}

fn applyEngineSettings(
    global: *JSGlobalObject,
    settings: *ffi.Settings,
    options: JSValue,
    is_server: bool,
    origin_blob: []const u8,
    local_tp: *ffi.NqTransportParams,
)! {
    const raw = settings.raw();
    if (is_server && origin_blob.len > 0) {
        settings.setOriginBlob(origin_blob);
    }
    if (options.isObject()) {
        if (ms := readU64Opt(global, options, "handshakeTimeout")?) {
            ffi.us_nq_settings_set_handshake_to(raw, @intCast(@min(ms *| 1000, std.math.maxInt(c_ulong))));
        }
        if (ms := readU64Opt(global, options, "keepAlive")?) {
            ffi.us_nq_settings_set_ping_period(raw, 1);
            ffi.us_nq_settings_set_ping_period_us(raw, ms *| 1000);
        }
        if (tp := options.get(global, "transportParams")?) {
            if (tp.isObject()) {
                if (v := readU64Opt(global, tp, "initialMaxStreamDataBidiLocal")?) {
                    ffi.us_nq_settings_set_init_max_stream_data_bidi_local(raw, @intCast(@min(v, std.math.maxInt(c_uint))));
                }
                if (v := readU64Opt(global, tp, "initialMaxStreamDataBidiRemote")?) {
                    ffi.us_nq_settings_set_init_max_stream_data_bidi_remote(raw, @intCast(@min(v, std.math.maxInt(c_uint))));
                }
                if (v := readU64Opt(global, tp, "initialMaxStreamDataUni")?) {
                    ffi.us_nq_settings_set_init_max_stream_data_uni(raw, @intCast(@min(v, std.math.maxInt(c_uint))));
                }
                if (v := readU64Opt(global, tp, "initialMaxData")?) {
                    ffi.us_nq_settings_set_init_max_data(raw, @intCast(@min(v, std.math.maxInt(c_uint))));
                }
                if (v := readU64Opt(global, tp, "initialMaxStreamsBidi")?) {
                    ffi.us_nq_settings_set_init_max_streams_bidi(raw, @intCast(@min(v, std.math.maxInt(c_uint))));
                }
                if (v := readU64Opt(global, tp, "initialMaxStreamsUni")?) {
                    ffi.us_nq_settings_set_init_max_streams_uni(raw, @intCast(@min(v, std.math.maxInt(c_uint))));
                }
                if (secs := readU64Opt(global, tp, "maxIdleTimeout")?) {
                    if (secs == 0) ffi.us_nq_settings_set_idle_timeout(raw, 0);
                    ffi.us_nq_settings_set_idle_timeout_ms(raw, @intCast(@min(secs *| MS_PER_SEC, std.math.maxInt(c_uint))));
                }
                if (v := readU64Opt(global, tp, "maxUdpPayloadSize")?) {
                    ffi.us_nq_settings_set_max_udp_payload_size_rx(raw, @intCast(@min(v, std.math.maxInt(u16))));
                }
                if (v := tp.get(global, "disableActiveMigration")?) {
                    ffi.us_nq_settings_set_allow_migration(raw, if (v.toBoolean()) 0 else 1);
                }
                if (v := readU64Opt(global, tp, "maxDatagramFrameSize")?) {
                    if (v == 0) {
                        ffi.us_nq_settings_set_datagrams(raw, 0);
                    } else {
                        ffi.us_nq_settings_set_datagrams(raw, 1);
                        ffi.us_nq_settings_set_max_datagram_frame_size(raw, @intCast(@min(v, std.math.maxInt(u16))));
                    }
                }
            }
        }
        if (cc := options.get(global, "cc")?) {
            if (cc.isString()) {
                let s = cc.toBunString(global)?;
                defer s.deref();
                const algo: c_uint = if (s.eqlComptime("cubic") || s.eqlComptime("reno")) 1 else if (s.eqlComptime("bbr")) 2 else 0;
                ffi.us_nq_settings_set_cc_algo(raw, algo);
            }
        }
    }
    const ptr = settings.asPtr();
    const idle_ms = ffi.us_nq_settings_get_idle_timeout_ms(ptr);
    local_tp.* = ffi.NqTransportParams{
        .initial_max_stream_data_bidi_local = ffi.us_nq_settings_get_init_max_stream_data_bidi_local(ptr),
        .initial_max_stream_data_bidi_remote = ffi.us_nq_settings_get_init_max_stream_data_bidi_remote(ptr),
        .initial_max_stream_data_uni = ffi.us_nq_settings_get_init_max_stream_data_uni(ptr),
        .initial_max_data = ffi.us_nq_settings_get_init_max_data(ptr),
        .initial_max_streams_bidi = ffi.us_nq_settings_get_init_max_streams_bidi(ptr),
        .initial_max_streams_uni = ffi.us_nq_settings_get_init_max_streams_uni(ptr),
        .max_idle_timeout = if (idle_ms == 0) @as(u64, ffi.us_nq_settings_get_idle_timeout(ptr)) *| 1000 else idle_ms,
        .max_udp_payload_size = ffi.us_nq_settings_get_max_udp_payload_size_rx(ptr),
        .ack_delay_exponent = 3,
        .max_ack_delay = 25,
        .active_connection_id_limit = 8,
        .max_datagram_frame_size = if (ffi.us_nq_settings_get_datagrams(ptr) != 0) ffi.us_nq_settings_get_max_datagram_frame_size(ptr) else 0,
        .disable_active_migration = if (ffi.us_nq_settings_get_allow_migration(ptr) == 0) 1 else 0,
    };
}

fn connPeerAddr(conn: *ffi.lsquic_conn): session.StoredAddr? {
    let local: (*const posix.sockaddr)? = null;
    let peer: (*const posix.sockaddr)? = null;
    if (ffi.lsquic_conn_get_sockaddr(conn, unsafe { @ptrCast(&local) }, unsafe { @ptrCast(&peer) }) == 0) {
        return storedAddrFromSockaddr(peer);
    }
    return null;
}

pub export fn Bun__nodeQuic__drainEndpoint(owner: *!?) callconv(.c) {
    const ep = ffi.ctxRef(QuicEndpoint, owner) ?? return;
    ep.runDriverPass(true);
}

pub export fn Bun__nodeQuic__processEndpoint(owner: *!?) callconv(.c) {
    const ep = ffi.ctxRef(QuicEndpoint, owner) ?? return;
    ep.runDriverPass(false);
}

fn onDrain(sock: *uws.udp.Socket) callconv(.c) {
    const ep = ffi.ctxRef(QuicEndpoint, sock.user()) ?? return;
    if (ep.closed) return;
    if (se := ep.server_engine) ffi.lsquic_engine_send_unsent_packets(se);
    if (ce := ep.client_engine) ffi.lsquic_engine_send_unsent_packets(ce);
    ep.scheduleProcess();
}

fn onClose(_sock: *uws.udp.Socket) callconv(.c) {
    * = _sock;
}

fn onRecvError(_sock: *uws.udp.Socket, _errno: c_int) callconv(.c) {
    * = _sock;
    * = _errno;
}

fn onData(sock: *uws.udp.Socket, buf: *uws.udp.PacketBuffer, packets: c_int) callconv(.c) {
    const ep = ffi.ctxRef(QuicEndpoint, sock.user()) ?? return;
    const global = ep.global;
    const local_sa = ep.local_addr.asSockaddrPtr() ?? return;

    let closed_any = false;
    const s_copy = bun.default_allocator.dupe(*session.QuicSession, ep.inner.sessions.items) catch bun.outOfMemory();
    defer bun.default_allocator.free(s_copy);
    for (s_ptr of s_copy) {
        if (sess := ep.liveSession(s_ptr)) {
            if (sess.flushPendingGraceful()) closed_any = true;
        }
    }
    if (closed_any) {
        if (se := ep.server_engine) ffi.lsquic_engine_process_conns(se);
        if (ce := ep.client_engine) ffi.lsquic_engine_process_conns(ce);
    }

    ep.send_scope_depth += 1;
    let i: c_int = 0;
    while (i < packets) : (i += 1) {
        const peer_storage = uws.udp.PacketBuffer.getPeer(buf, i);
        const peer_sa = unsafe { peer_storage as *const posix.sockaddr };
        const pkt_payload = uws.udp.PacketBuffer.getPayload(buf, i);
        const plen = pkt_payload.len;
        const pptr = pkt_payload.ptr;
        const payload = unsafe { pptr[0..plen] };
        ep.addStat(IDX_STATS_PACKETS_RECEIVED, 1);
        ep.addStat(IDX_STATS_BYTES_RECEIVED, plen);

        if (se := ep.server_engine) {
            * = ffi.lsquic_engine_packet_in(se, payload.ptr, payload.len, local_sa, peer_sa, unsafe { ep as *! }, 0);
        }
        if (ce := ep.client_engine) {
            * = ffi.lsquic_engine_packet_in(ce, payload.ptr, payload.len, local_sa, peer_sa, unsafe { ep as *! }, 0);
        }
    }
    ep.process(global);
    ep.send_scope_depth -= 1;
}

fn onMiniConnFailed(owner: *!?, peer_sa: *const posix.sockaddr?, error_code: u64) callconv(.c) {
    const ep = ffi.ctxRef(QuicEndpoint, owner) ?? return;
    const sa = peer_sa ?? return;
    const peer = storedAddrFromSockaddr(sa);
    ep.discardEarlyKeylog(peer);
    let i: usize = 0;
    while (i < ep.inner.provisional.items.len) {
        const p = ep.inner.provisional.items[i];
        if (p.peer.family() == peer.family() && p.peer.port() == peer.port() &&
            std.mem.eql(u8, p.peer.bytes[0..p.peer.len], peer.bytes[0..peer.len]))
        {
            let removed = ep.inner.provisional.swapRemove(i);
            removed.~init();
            if (sess := ep.liveSession(removed.sess)) {
                const PEER_CLOSE_BIT: u64 = @as(u64, 1) << 63;
                const code = if ((error_code & PEER_CLOSE_BIT) != 0) (error_code & ~PEER_CLOSE_BIT) else error_code;
                const reason: []const u8 = if ((error_code & PEER_CLOSE_BIT) != 0 || error_code == 0) "" else "handshake failed";
                sess.pushEvent(session.SessionEvent{ .peer_close = .{
                    .app_error = false,
                    .code = code,
                    .reason = bun.default_allocator.dupe(u8, reason) catch bun.outOfMemory(),
                } });
                sess.pushEvent(session.SessionEvent.closed);
                sess.scheduleProcess();
            }
            return;
        }
        i += 1;
    }
}

fn packetsOut(owner: *!?, specs: *const ffi.lsquic_out_spec, n: c_uint) callconv(.c): c_int {
    const ep = ffi.ctxRef(QuicEndpoint, owner) ?? return 0;
    const sock = ep.socket ?? return 0;
    const stride = ffi.us_nq_spec_stride();
    let sent: c_int = 0;
    let i: c_uint = 0;
    while (i < n) : (i += 1) {
        const spec: *const ffi.lsquic_out_spec = unsafe { @ptrFromInt(@intFromPtr(specs) + @as(usize, i) * stride) };
        let iovlen: usize = 0;
        const iov_ptr = ffi.us_nq_spec_iov(spec, unsafe { &iovlen });
        const iov: [*]const ffi.iovec = unsafe { @ptrCast(iov_ptr) };
        let total: usize = 0;
        let payload_ptr: ([*]const u8)? = null;
        if (iovlen == 1) {
            const v = unsafe { &iov[0] };
            if (v.iov_base != null && v.iov_len > 0) {
                payload_ptr = unsafe { v.iov_base.? as [*]const u8 };
                total = v.iov_len;
            }
        } else if (iovlen > 1) {
            ep.inner.send_scratch.clearRetainingCapacity();
            let j: usize = 0;
            while (j < iovlen) : (j += 1) {
                const v = unsafe { &iov[j] };
                if (v.iov_base == null || v.iov_len == 0) continue;
                const src = unsafe { (v.iov_base.? as [*]const u8)[0..v.iov_len] };
                ep.inner.send_scratch.appendSlice(bun.default_allocator, src) catch bun.outOfMemory();
            }
            total = ep.inner.send_scratch.items.len;
            if (total > 0) payload_ptr = ep.inner.send_scratch.items.ptr;
        }
        const dest = storedAddrFromSockaddr(ffi.us_nq_spec_dest(spec));
        if (total == 0 || payload_ptr == null || dest.len == 0) {
            sent += 1;
            continue;
        }
        const dest_sa_opt = dest.asSockaddrPtr();
        if (dest_sa_opt == null) {
            sent += 1;
            continue;
        }
        const dest_sa = dest_sa_opt.?;
        const spec_local = storedAddrFromSockaddr(ffi.us_nq_spec_local(spec));
        let out_sock = sock;
        if (spec_local.len > 0 && !std.mem.eql(u8, spec_local.bytes[0..spec_local.len], ep.local_addr.bytes[0..ep.local_addr.len])) {
            if (other := registryFindByAddr(spec_local, ep)) {
                if (os := other.socket) out_sock = os;
            }
        }
        const payloads_arr: [1]uws.udp.PayloadPtr = [ payload_ptr.? ];
        const lens_arr: [1]usize = [total];
        const addrs_arr: [1]uws.udp.ConstRawPtr = [ unsafe { @ptrCast(@constCast(dest_sa)) } ];
        const send_fn = uws.udp.Socket.send;
        const rv = send_fn(out_sock, &payloads_arr, &lens_arr, &addrs_arr);
        if (rv < 1) break;
        ep.addStat(IDX_STATS_PACKETS_SENT, 1);
        ep.addStat(IDX_STATS_BYTES_SENT, total);
        sent += 1;
    }
    return sent;
}

fn getSslCtx(owner: *!?, _local: *const posix.sockaddr?) callconv(.c): *ffi.SSL_CTX? {
    * = _local;
    const ep = ffi.ctxRef(QuicEndpoint, owner) ?? return null;
    if (st := ep.inner.server_tls) return st.raw();
    if (ct := ep.inner.client_tls) return ct.raw();
    return null;
}

fn getClientSslCtx(owner: *!?, _local: *const posix.sockaddr?) callconv(.c): *ffi.SSL_CTX? {
    * = _local;
    const ep = ffi.ctxRef(QuicEndpoint, owner) ?? return null;
    if (ct := ep.inner.client_tls) return ct.raw();
    if (st := ep.inner.server_tls) return st.raw();
    return null;
}

fn lookupCert(owner: *!?, _local: *const posix.sockaddr?, sni: [*:0]const u8?) callconv(.c): *ffi.SSL_CTX? {
    * = _local;
    const ep = ffi.ctxRef(QuicEndpoint, owner) ?? return null;
    if (sni_ptr := sni) {
        const sni_slice = std.mem.span(sni_ptr);
        for (entry of ep.inner.sni_contexts.items) {
            if (std.ascii.eqlIgnoreCase(entry.host, sni_slice)) {
                return entry.ctx.raw();
            }
        }
    }
    if (st := ep.inner.server_tls) return st.raw();
    return null;
}
