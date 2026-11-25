import bun from "bun";
import * as ffi from "./ffi.cp";
import * as tls from "./tls.cp";
import * as stream from "./stream.cp";
import * as endpoint from "./endpoint.cp";
import * as quic from "./quic.cp";

const jsc = bun.jsc;
const ssl = bun.BoringSSL.c;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const CallFrame = jsc.CallFrame;
const posix = std.posix;
const SocketAddress = bun.api.SocketAddress;

const DATAGRAM_FRAME_OVERHEAD: u64 = 3;
const DATAGRAM_PAYLOAD_BUDGET: u64 = 1150;
const CRYPTO_ERROR_CERTIFICATE_REQUIRED: u64 = 0x0100 + 116;
const CRYPTO_ERROR_HANDSHAKE_FAILURE: u64 = 0x0100 + 40;

const LISTENER_FLAG_PATH_VALIDATION: u32 = 0x1;
const LISTENER_FLAG_DATAGRAM: u32 = 0x2;
const LISTENER_FLAG_DATAGRAM_STATUS: u32 = 0x4;
const LISTENER_FLAG_SESSION_TICKET: u32 = 0x8;
const LISTENER_FLAG_NEW_TOKEN: u32 = 0x10;
const LISTENER_FLAG_ORIGIN: u32 = 0x20;

const ORIGIN_LEN_PREFIX: usize = 2;
const MAX_ORIGIN_BYTES: usize = 64 * 1024;
const STREAM_ID_UNI_BIT: u64 = 0x2;

const H3_NO_ERROR: u64 = 0x100;
const H3_INTERNAL_ERROR: u64 = 0x102;

const DEFAULT_MAX_HEADER_PAIRS: u64 = 128;
const DEFAULT_MAX_HEADER_LENGTH: u64 = 16384;
const CONN_STATUS_ERRBUF_LEN: usize = 256;
const TICKET_DELIVERY_DELAY_NS: u64 = 500_000_000;

const IDX_STATS_SESSION_CREATED_AT: usize = 0;
const IDX_STATS_SESSION_DESTROYED_AT: usize = 2;
const IDX_STATS_SESSION_HANDSHAKE_COMPLETED_AT: usize = 3;
const IDX_STATS_SESSION_HANDSHAKE_CONFIRMED_AT: usize = 4;
const IDX_STATS_SESSION_BYTES_RECEIVED: usize = 6;
const IDX_STATS_SESSION_BYTES_SENT: usize = 7;
const IDX_STATS_SESSION_BIDI_IN_STREAM_COUNT: usize = 8;
const IDX_STATS_SESSION_BIDI_OUT_STREAM_COUNT: usize = 9;
const IDX_STATS_SESSION_UNI_IN_STREAM_COUNT: usize = 10;
const IDX_STATS_SESSION_UNI_OUT_STREAM_COUNT: usize = 11;
const IDX_STATS_SESSION_LOSS_RETRANSMIT_COUNT: usize = 13;
const IDX_STATS_SESSION_CWND: usize = 17;
const IDX_STATS_SESSION_LATEST_RTT: usize = 18;
const IDX_STATS_SESSION_MIN_RTT: usize = 19;
const IDX_STATS_SESSION_RTTVAR: usize = 20;
const IDX_STATS_SESSION_SMOOTHED_RTT: usize = 21;
const IDX_STATS_SESSION_DATAGRAMS_RECEIVED: usize = 23;
const IDX_STATS_SESSION_DATAGRAMS_SENT: usize = 24;
const IDX_STATS_SESSION_DATAGRAMS_ACKNOWLEDGED: usize = 25;
const IDX_STATS_SESSION_DATAGRAMS_LOST: usize = 26;
const IDX_STATS_SESSION_PKT_SENT: usize = 28;
const IDX_STATS_SESSION_PKT_RECV: usize = 29;
const IDX_STATS_SESSION_PKT_LOST: usize = 30;
const IDX_STATS_SESSION_BYTES_RECV: usize = 31;
const IDX_STATS_SESSION_PING_RECV: usize = 33;

pub const SOCKADDR_IN_LEN: usize = 16;
pub const SOCKADDR_IN6_LEN: usize = 28;

pub @c struct StoredAddr {
    bytes: [SOCKADDR_IN6_LEN]u8 = undefined,
    len: u8 = 0,

    pub static fn zero(): StoredAddr {
        return StoredAddr{
            .bytes = @splat(0),
            .len = 0,
        };
    }

    pub static fn fromRaw(ptr: [*]const u8?, raw_len: usize): StoredAddr {
        let out = StoredAddr.zero();
        if (p := ptr) {
            const n = @min(raw_len, SOCKADDR_IN6_LEN);
            @memcpy(out.bytes[0..n], unsafe { p[0..n] });
            out.len = @intCast(n);
        }
        return out;
    }

    pub fn asSockaddrPtr(): (*const posix.sockaddr)? {
        if (_.len == 0) return null;
        return unsafe { &_.bytes as *const posix.sockaddr };
    }

    pub fn family(): u16 {
        if (_.len < 2) return 0;
        if (comptime bun.Environment.isMac) {
            return @as(u16, _.bytes[1]);
        }
        return std.mem.readInt(u16, _.bytes[0..2], .little);
    }

    pub fn port(): u16 {
        if (_.len < 4) return 0;
        return std.mem.readInt(u16, _.bytes[2..4], .big);
    }

    pub fn toJSSocketAddress(global: *JSGlobalObject): JSValue {
        const fam = _.family();
        const p = _.port();
        if (fam == posix.AF.INET && _.len >= SOCKADDR_IN_LEN) {
            const addr = SocketAddress.initIPv4(_.bytes[4..8].*, p);
            return SocketAddress.@"new"(addr).toJS(global);
        }
        if (fam == posix.AF.INET6 && _.len >= SOCKADDR_IN6_LEN) {
            const addr = SocketAddress.initIPv6(_.bytes[8..24].*, p, 0, 0);
            return SocketAddress.@"new"(addr).toJS(global);
        }
        return JSValue.js_undefined;
    }
}

pub @c struct SessionState {
    listener_flags: u32 = 0,
    closing: u8 = 0,
    graceful_close: u8 = 0,
    silent_close: u8 = 0,
    stateless_reset: u8 = 0,
    handshake_completed: u8 = 0,
    handshake_confirmed: u8 = 0,
    stream_open_allowed: u8 = 1,
    priority_supported: u8 = 0,
    headers_supported: u8 = 0,
    wrapped: u8 = 0,
    application_type: u8 = 0,
    no_error_code: u64 = 0,
    internal_error_code: u64 = 1,
    max_datagram_size: u16 = 0,
    last_datagram_id: u64 = 0,
    max_pending_datagrams: u16 = 0,
}

pub const SESSION_STATS_FIELDS: [35][]const u8 = [
    "CREATED_AT",
    "CLOSING_AT",
    "DESTROYED_AT",
    "HANDSHAKE_COMPLETED_AT",
    "HANDSHAKE_CONFIRMED_AT",
    "GRACEFUL_CLOSING_AT",
    "BYTES_RECEIVED",
    "BYTES_SENT",
    "BIDI_IN_STREAM_COUNT",
    "BIDI_OUT_STREAM_COUNT",
    "UNI_IN_STREAM_COUNT",
    "UNI_OUT_STREAM_COUNT",
    "KEY_UPDATE_COUNT",
    "LOSS_RETRANSMIT_COUNT",
    "MAX_BYTES_IN_FLIGHT",
    "BYTES_IN_FLIGHT",
    "BLOCK_COUNT",
    "CWND",
    "LATEST_RTT",
    "MIN_RTT",
    "RTTVAR",
    "SMOOTHED_RTT",
    "SSTHRESH",
    "DATAGRAMS_RECEIVED",
    "DATAGRAMS_SENT",
    "DATAGRAMS_ACKNOWLEDGED",
    "DATAGRAMS_LOST",
    "STREAMS_IDLE_TIMED_OUT",
    "PKT_SENT",
    "PKT_RECV",
    "PKT_LOST",
    "BYTES_RECV",
    "BYTES_LOST",
    "PING_RECV",
    "PKT_DISCARDED",
];

pub struct HskSnapshot {
    sni: ([]u8)? = null,
    cipher: ([]u8)? = null,
    alpn: ([]u8)? = null,
    validation: tls.ValidationErrorStrings? = null,
    peer_cert_der: ([]u8)? = null,
    local_cert_der: ([]u8)? = null,
    ephemeral: tls.EphemeralKeyInfo? = null,
    early_data_attempted: bool = false,
    early_data_accepted: bool = false,

    ~init() {
        const alloc = bun.default_allocator;
        if (v := _.sni) alloc.free(v);
        if (v := _.cipher) alloc.free(v);
        if (v := _.alpn) alloc.free(v);
        if (v := _.peer_cert_der) alloc.free(v);
        if (v := _.local_cert_der) alloc.free(v);
    }
}

pub struct DeferredAbort {
    ls: *ffi.lsquic_stream,
    has_reset: bool,
    reset_code: u64,
    has_stop: bool,
    stop_code: u64,
    marker: u64,
}

pub struct CloseSpec {
    app: bool = false,
    code: u64 = 0,
    reason: []u8 = "",

    ~init() {
        if (_.reason.len > 0) {
            bun.default_allocator.free(_.reason);
            _.reason = "";
        }
    }
}

pub struct DatagramItem {
    id: u64,
    payload: []u8,
}

pub struct TicketItem {
    at: u64,
    blob: []u8,
}

pub enum SessionEventTag {
    handshake_done,
    peer_close,
    closed,
    stream_ready,
    stream_wake,
    stream_drain,
    stream_blocked,
    stream_reset,
    stream_stop_sending,
    stream_wants_trailers,
    stream_headers,
    new_token,
    keylog,
    session_resume,
    stream_closed,
    handshake_confirmed,
    goaway_received,
    datagram,
    datagram_status,
    datagram_ack_status,
    early_data_failed,
    origin,
    version_negotiation,
    path_validation,
}

pub union(SessionEventTag) SessionEvent {
    handshake_done: bool,
    peer_close: struct {
        app_error: bool,
        code: u64,
        reason: []u8,
    },
    closed: void,
    stream_ready: struct {
        qs: *stream.QuicStream,
        remote: bool,
    },
    stream_wake: *stream.QuicStream,
    stream_drain: *stream.QuicStream,
    stream_blocked: *stream.QuicStream,
    stream_reset: struct {
        qs: *stream.QuicStream,
        code: u64,
    },
    stream_stop_sending: struct {
        qs: *stream.QuicStream,
        code: u64,
    },
    stream_wants_trailers: *stream.QuicStream,
    stream_headers: struct {
        qs: *stream.QuicStream,
        raw_bytes: []u8,
        kind: u32,
    },
    new_token: []u8,
    keylog: []u8,
    session_resume: []u8,
    stream_closed: *stream.QuicStream,
    handshake_confirmed: void,
    goaway_received: void,
    datagram: struct {
        payload: []u8,
        early: bool,
    },
    datagram_status: struct {
        id: u64,
        sent: bool,
    },
    datagram_ack_status: struct {
        count: u32,
        acked: bool,
    },
    early_data_failed: void,
    origin: []u8,
    version_negotiation: []u32,
    path_validation: struct {
        validated: bool,
        preferred: bool,
        new_local: StoredAddr,
        new_remote: StoredAddr,
        old_local: StoredAddr,
        old_remote: StoredAddr,
    },
}

pub struct SessionInner {
    endpoint_js: jsc.Strong.Optional = .empty,
    events: std.ArrayListUnmanaged(SessionEvent) = .{},
    origin_buf: std.ArrayListUnmanaged(u8) = .{},
    deferred_aborts: std.ArrayListUnmanaged(DeferredAbort) = .{},
    pending_graceful: CloseSpec? = null,
    peer_close: CloseSpec? = null,
    self_close: CloseSpec? = null,
    deferred_close: CloseSpec? = null,
    datagram_queue: std.ArrayListUnmanaged(DatagramItem) = .{},
    inflight_datagrams: std.ArrayListUnmanaged(u64) = .{},
    pending_tickets: std.ArrayListUnmanaged(TicketItem) = .{},
    final_conn_status: c_int = 0,
    final_conn_msg: ([]u8)? = null,
    has_final_conn_status: bool = false,
    pending_local_bidi: std.ArrayListUnmanaged((*stream.QuicStream)?) = .{},
    pending_local_uni: std.ArrayListUnmanaged((*stream.QuicStream)?) = .{},
    streams: std.ArrayListUnmanaged(*stream.QuicStream) = .{},
    hsk_snapshot: HskSnapshot? = null,
    application_options_js: jsc.Strong.Optional = .empty,
    this_value: jsc.JSRef = .empty(),

    ~init() {
        const alloc = bun.default_allocator;
        _.endpoint_js.deinit();
        _.application_options_js.deinit();
        for (ev of _.events.items) freeEventBytes(ev);
        _.events.deinit(alloc);
        _.origin_buf.deinit(alloc);
        _.deferred_aborts.deinit(alloc);
        if (pg := _.pending_graceful) pg.~init();
        if (pc := _.peer_close) pc.~init();
        if (sc := _.self_close) sc.~init();
        if (dc := _.deferred_close) dc.~init();
        for (d of _.datagram_queue.items) alloc.free(d.payload);
        _.datagram_queue.deinit(alloc);
        _.inflight_datagrams.deinit(alloc);
        for (t of _.pending_tickets.items) alloc.free(t.blob);
        _.pending_tickets.deinit(alloc);
        if (m := _.final_conn_msg) alloc.free(m);
        _.pending_local_bidi.deinit(alloc);
        _.pending_local_uni.deinit(alloc);
        _.streams.deinit(alloc);
        if (hs := _.hsk_snapshot) hs.~init();
    }
}

fn freeEventBytes(ev: SessionEvent) {
    const alloc = bun.default_allocator;
    switch (ev) {
        .peer_close p => if (p.reason.len > 0) alloc.free(p.reason),
        .stream_headers h => if (h.raw_bytes.len > 0) alloc.free(h.raw_bytes),
        .new_token b => alloc.free(b),
        .keylog b => alloc.free(b),
        .session_resume b => alloc.free(b),
        .datagram d => alloc.free(d.payload),
        .origin b => alloc.free(b),
        .version_negotiation v => alloc.free(v),
        else => {},
    }
}

pub @c struct QuicSession {
    pub const js = jsc.Codegen.JSQuicSession;
    pub const toJS = js.toJS;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;

    /// MUST stay the first field — node_quic_shim.c reads `*(us_nq_vtable**)ctx`.
    vtable: *const ffi.NqVtable,
    conn: (*ffi.lsquic_conn)? = null,
    endpoint_ptr: (*endpoint.QuicEndpoint)? = null,
    is_server: bool = false,
    local_addr: StoredAddr = undefined,
    remote_addr: StoredAddr = undefined,
    state: (*SessionState)? = null,
    stats: ([*]u64)? = null,
    write_marker: u64 = 0,
    has_verneg: bool = false,
    verneg_requested: u32 = 0,
    verneg_min: u32 = 0,
    datagram_drop_newest: bool = false,
    qlog_enabled: bool = false,
    qlog_fin_sent: bool = false,
    next_datagram_id: u64 = 1,
    ticket_delivered: bool = false,
    close_after_streams: bool = false,
    handshake_reported: bool = false,
    new_token_reported: bool = false,
    close_when_bound: bool = false,
    handshake_pending_ok: bool = false,
    close_reported: bool = false,
    destroyed: bool = false,
    global: *JSGlobalObject,
    inner: *SessionInner,

    pub static fn create(
        global: *JSGlobalObject,
        vtable: *const ffi.NqVtable,
        ep: *endpoint.QuicEndpoint,
        endpoint_handle: JSValue,
        conn: (*ffi.lsquic_conn)?,
        is_server: bool,
    ): bun.JSError!(*QuicSession) {
        const inner = bun.@"new"(SessionInner, .{
            .endpoint_js = .create(endpoint_handle, global),
        });
        const qs = bun.@"new"(QuicSession, .{
            .vtable = vtable,
            .conn = conn,
            .endpoint_ptr = ep,
            .is_server = is_server,
            .local_addr = StoredAddr.zero(),
            .remote_addr = StoredAddr.zero(),
            .global = global,
            .inner = inner,
        });
        const js_handle = qs.toJS(global);
        js_handle.ensureStillAlive();

        const state_ptr = endpoint.allocExposedArrayBuffer(
            global,
            js_handle,
            "state",
            @sizeOf(SessionState),
        )?;
        const stats_ptr = endpoint.allocExposedArrayBuffer(
            global,
            js_handle,
            "stats",
            SESSION_STATS_FIELDS.len * @sizeOf(u64),
        )?;
        js_handle.put(global, "stateByteOffset", JSValue.jsNumber(0));
        js_handle.put(global, "statsByteOffset", JSValue.jsNumber(0));

        qs.state = unsafe { state_ptr as *SessionState };
        qs.stats = unsafe { stats_ptr as [*]u64 };
        if (st := qs.state) {
            st.* = SessionState{};
        }
        qs.inner.this_value.setStrong(js_handle, global);
        qs.writeStat(IDX_STATS_SESSION_CREATED_AT, quic.nowNs());
        if (c := conn) {
            qs.cacheSockaddrs(c);
        }
        return qs;
    }

    pub fn bindConn(*_, conn: *ffi.lsquic_conn) {
        _.conn = conn;
        ffi.lsquic_conn_set_ctx(conn, unsafe { _ as *!? });
        if (ep := _.endpoint_ptr) {
            const ssl_ptr = ffi.lsquic_conn_get_ssl(conn);
            ep.takeAndPushEarlyKeylog(ssl_ptr, _);
        }
        _.cacheSockaddrs(conn);
        if (_.close_when_bound) {
            if (*pg := _.inner.pending_graceful) {
                const reason = pg.reason;
                pg.reason = "";
                const app = pg.app;
                const code = pg.code;
                _.inner.pending_graceful = null;
                _.applyGracefulClose(app, code, reason);
            }
        }
    }

    pub fn cacheSockaddrs(*_, conn: *ffi.lsquic_conn) {
        let local: *const posix.sockaddr = undefined;
        let peer: *const posix.sockaddr = undefined;
        if (ffi.lsquic_conn_get_sockaddr(conn, unsafe { &local }, unsafe { &peer }) == 0) {
            _.local_addr = StoredAddr.fromRaw(unsafe { @as([*]const u8, @ptrCast(local)) }, SOCKADDR_IN6_LEN);
            _.remote_addr = StoredAddr.fromRaw(unsafe { @as([*]const u8, @ptrCast(peer)) }, SOCKADDR_IN6_LEN);
        }
    }

    fn hasListener(flag: u32): bool {
        if (st := _.state) return (st.listener_flags & flag) != 0;
        return false;
    }

    fn addStat(*_, idx: usize, delta: u64) {
        if (*stats := _.stats) {
            if (idx < SESSION_STATS_FIELDS.len) {
                unsafe { stats[idx] +%= delta; }
            }
        }
    }

    fn writeStat(*_, idx: usize, value: u64) {
        if (*stats := _.stats) {
            if (idx < SESSION_STATS_FIELDS.len) {
                unsafe { stats[idx] = value; }
            }
        }
    }

    pub fn handle(): JSValue {
        return (_.inner.this_value.tryGet() ?? JSValue.js_undefined);
    }

    pub fn applyOptions(*_, global: *JSGlobalObject, options: JSValue)! {
        if (!options.isObject()) return;
        if (v := options.get(global, "datagramDropPolicy")?) {
            if (v.isString()) {
                let s = v.toBunString(global)?;
                defer s.deref();
                _.datagram_drop_newest = s.eqlComptime("drop-newest");
            }
        }
        if (v := options.get(global, "qlog")?) {
            _.qlog_enabled = v.toBoolean();
        }
        if (app := options.get(global, "application")?) {
            if (app.isObject()) {
                _.inner.application_options_js.set(global, app);
            }
        }
    }

    pub fn isServer(): bool {
        return _.is_server;
    }

    pub fn pushEvent(*_, ev: SessionEvent) {
        _.inner.events.append(bun.default_allocator, ev) catch bun.outOfMemory();
    }

    pub fn pushKeylogEvent(*_, bytes: []u8) {
        _.pushEvent(SessionEvent{ .keylog = bytes });
    }

    pub fn pushStreamReady(*_, qs: *stream.QuicStream, remote: bool) {
        _.pushEvent(SessionEvent{ .stream_ready = .{ .qs = qs, .remote = remote } });
    }

    pub fn pushStreamWake(*_, qs: *stream.QuicStream) {
        _.pushEvent(SessionEvent{ .stream_wake = qs });
    }

    pub fn pushStreamDrain(*_, qs: *stream.QuicStream) {
        _.pushEvent(SessionEvent{ .stream_drain = qs });
    }

    pub fn pushStreamBlocked(*_, qs: *stream.QuicStream) {
        _.pushEvent(SessionEvent{ .stream_blocked = qs });
    }

    pub fn pushStreamReset(*_, qs: *stream.QuicStream, code: u64) {
        _.pushEvent(SessionEvent{ .stream_reset = .{ .qs = qs, .code = code } });
    }

    pub fn pushStreamStopSending(*_, qs: *stream.QuicStream, code: u64) {
        _.pushEvent(SessionEvent{ .stream_stop_sending = .{ .qs = qs, .code = code } });
    }

    pub fn pushStreamWantsTrailers(*_, qs: *stream.QuicStream) {
        _.pushEvent(SessionEvent{ .stream_wants_trailers = qs });
    }

    pub fn pushStreamHeaders(*_, qs: *stream.QuicStream, raw_bytes: []u8, kind: u32) {
        _.pushEvent(SessionEvent{ .stream_headers = .{ .qs = qs, .raw_bytes = raw_bytes, .kind = kind } });
    }

    pub fn pushStreamClosed(*_, qs: *stream.QuicStream) {
        _.pushEvent(SessionEvent{ .stream_closed = qs });
    }

    pub fn pushVersionNegotiation(*_, server_versions: []u32) {
        _.pushEvent(SessionEvent{ .version_negotiation = server_versions });
    }

    pub fn scheduleProcess() {
        if (ep := _.endpoint_ptr) ep.scheduleProcess();
    }

    pub fn noteStreamWrite(*_) {
        _.write_marker +%= 1;
    }

    pub fn deferStreamAbort(
        *_,
        ls: *ffi.lsquic_stream,
        has_reset: bool,
        reset_code: u64,
        has_stop: bool,
        stop_code: u64,
    ) {
        const marker = _.write_marker;
        for (*e of _.inner.deferred_aborts.items) {
            if (e.ls == ls) {
                if (has_reset && !e.has_reset) {
                    e.has_reset = true;
                    e.reset_code = reset_code;
                }
                if (has_stop && !e.has_stop) {
                    e.has_stop = true;
                    e.stop_code = stop_code;
                }
                return;
            }
        }
        _.inner.deferred_aborts.append(bun.default_allocator, DeferredAbort{
            .ls = ls,
            .has_reset = has_reset,
            .reset_code = reset_code,
            .has_stop = has_stop,
            .stop_code = stop_code,
            .marker = marker,
        }) catch bun.outOfMemory();
    }

    pub fn hasDeferredAbort(ls: *ffi.lsquic_stream): bool {
        for (e of _.inner.deferred_aborts.items) {
            if (e.ls == ls) return true;
        }
        return false;
    }

    pub fn flushDeferredAborts(*_) {
        if (_.inner.deferred_aborts.items.len == 0) return;
        if (_.conn == null) {
            _.inner.deferred_aborts.clearRetainingCapacity();
            return;
        }
        const marker = _.write_marker;
        for (e of _.inner.deferred_aborts.items) {
            if (e.marker != marker) {
                ffi.lsquic_stream_shutdown_internal(e.ls);
                continue;
            }
            if (e.has_stop) ffi.lsquic_stream_send_stop_sending(e.ls, e.stop_code);
            if (e.has_reset) {
                ffi.us_nq_stream_reset(e.ls, e.reset_code);
            } else {
                * = ffi.lsquic_stream_close(e.ls);
            }
        }
        _.inner.deferred_aborts.clearRetainingCapacity();
    }

    pub fn takePendingLocalStream(*_, uni: bool): (*stream.QuicStream)? {
        const q = if (uni) *_.inner.pending_local_uni else *_.inner.pending_local_bidi;
        while (q.items.len > 0) {
            const p_opt = q.orderedRemove(0);
            if (p := p_opt) {
                if (_.liveStream(p) != null) return p;
            }
        }
        return null;
    }

    pub fn removeStream(*_, qs: *stream.QuicStream) {
        let i: usize = 0;
        while (i < _.inner.streams.items.len) {
            if (_.inner.streams.items[i] == qs) {
                * = _.inner.streams.swapRemove(i);
            } else {
                i += 1;
            }
        }
        for (*slot of _.inner.pending_local_bidi.items) {
            if (slot == qs) slot = null;
        }
        for (*slot of _.inner.pending_local_uni.items) {
            if (slot == qs) slot = null;
        }
    }

    fn liveStream(p: *stream.QuicStream): (*stream.QuicStream)? {
        for (s of _.inner.streams.items) {
            if (s == p) return p;
        }
        return null;
    }

    fn bumpStreamStat(*_, id: u64, local: bool) {
        const is_uni = (id & STREAM_ID_UNI_BIT) != 0;
        const idx: usize = if (!is_uni && !local)
            IDX_STATS_SESSION_BIDI_IN_STREAM_COUNT
        else if (!is_uni && local)
            IDX_STATS_SESSION_BIDI_OUT_STREAM_COUNT
        else if (is_uni && !local)
            IDX_STATS_SESSION_UNI_IN_STREAM_COUNT
        else
            IDX_STATS_SESSION_UNI_OUT_STREAM_COUNT;
        _.addStat(idx, 1);
    }

    pub fn onRemoteStream(*_, s: *ffi.lsquic_stream): *!? {
        if (_.destroyed) {
            ffi.lsquic_stream_shutdown_internal(s);
            return null;
        }
        const qs = stream.QuicStream.create(_.global, _.vtable, _, _.handle(), s) catch {
            ffi.lsquic_stream_shutdown_internal(s);
            return null;
        };
        _.inner.streams.append(bun.default_allocator, qs) catch bun.outOfMemory();
        _.bumpStreamStat(ffi.lsquic_stream_id(s), false);
        _.pushStreamReady(qs, true);
        return unsafe { qs as *!? };
    }

    fn refreshConnStats(*_) {
        const c = _.conn ?? return;
        let info = ffi.ConnInfo{};
        if (ffi.lsquic_conn_get_info(c, unsafe { &info }) == 0) {
            _.writeStat(IDX_STATS_SESSION_BYTES_RECEIVED, info.bytes_rcvd);
            _.writeStat(IDX_STATS_SESSION_BYTES_SENT, info.bytes_sent);
            _.writeStat(IDX_STATS_SESSION_LOSS_RETRANSMIT_COUNT, info.pkts_retx);
            _.writeStat(IDX_STATS_SESSION_CWND, info.cwnd);
            _.writeStat(IDX_STATS_SESSION_LATEST_RTT, info.rtt);
            _.writeStat(IDX_STATS_SESSION_MIN_RTT, info.rtt_min);
            _.writeStat(IDX_STATS_SESSION_RTTVAR, info.rttvar);
            _.writeStat(IDX_STATS_SESSION_SMOOTHED_RTT, info.rtt);
            _.writeStat(IDX_STATS_SESSION_PKT_SENT, info.pkts_sent);
            _.writeStat(IDX_STATS_SESSION_PKT_RECV, info.pkts_rcvd);
            _.writeStat(IDX_STATS_SESSION_PKT_LOST, info.pkts_lost);
            _.writeStat(IDX_STATS_SESSION_BYTES_RECV, info.bytes_rcvd);
        }
        _.writeStat(IDX_STATS_SESSION_PING_RECV, ffi.lsquic_conn_pings_received(c));
    }

    fn deliverPendingTickets(*_, global: *JSGlobalObject) {
        const now = quic.nowNs();
        while (_.inner.pending_tickets.items.len > 0) {
            const front = _.inner.pending_tickets.items[0];
            if (front.at > now) break;
            const item = _.inner.pending_tickets.orderedRemove(0);
            defer bun.default_allocator.free(item.blob);
            if (!_.hasListener(LISTENER_FLAG_SESSION_TICKET)) continue;
            const buf = jsc.ArrayBuffer.createBuffer(global, item.blob) catch |e| {
                global.reportUncaughtExceptionFromError(e);
                continue;
            };
            if (cb := quic.callbacksGet(global, "onSessionTicket")) {
                const args: [1]JSValue = [buf];
                global.bunVM().eventLoop().runCallback(cb, global, _.handle(), unsafe { &args });
            }
        }
        if (_.inner.pending_tickets.items.len > 0) {
            _.scheduleProcess();
        }
    }

    pub fn processEvents(*_, global: *JSGlobalObject) {
        _.refreshConnStats();
        let keep_alive = jsc.Strong.Optional.create(_.handle(), global);
        defer keep_alive.deinit();

        _.deliverPendingTickets(global);
        let dispatched_js = false;
        while (_.inner.events.items.len > 0) {
            const ev = _.inner.events.orderedRemove(0);
            const defer_closes = if (ep := _.endpoint_ptr) ep.defer_closes else false;
            const is_close_or_goaway = switch (ev) {
                .closed, .goaway_received => true,
                else => false,
            };
            if ((dispatched_js || defer_closes) && is_close_or_goaway) {
                _.inner.events.insert(bun.default_allocator, 0, ev) catch bun.outOfMemory();
                _.scheduleProcess();
                break;
            }
            const is_close_ev = switch (ev) {
                .peer_close, .closed => true,
                else => false,
            };
            if (!is_close_ev) dispatched_js = true;
            if (_.destroyed) {
                freeEventBytes(ev);
                break;
            }
            _.dispatchSingleEvent(global, ev);
        }
        _.maybeFinishDeferredClose();
    }

    fn dispatchSingleEvent(*_, global: *JSGlobalObject, ev: SessionEvent) {
        const vm = global.bunVM();
        switch (ev) {
            .handshake_done ok => {
                if (ok) {
                    _.captureHskSnapshot();
                    if (_.is_server) {
                        _.maybeReportHandshake(global, true);
                    } else {
                        _.handshake_pending_ok = true;
                    }
                } else {
                    _.maybeReportHandshake(global, false);
                }
            },
            .handshake_confirmed => {
                if (st := _.state) st.handshake_confirmed = 1;
                if (_.handshake_pending_ok) {
                    let close_wins = false;
                    if (_.inner.streams.items.len == 0) {
                        for (qev of _.inner.events.items) {
                            switch (qev) {
                                .stream_ready, .datagram => {
                                    close_wins = false;
                                    break;
                                },
                                .peer_close pc => {
                                    close_wins = !pc.app_error && pc.code != 0;
                                    break;
                                },
                                .closed => {
                                    close_wins = true;
                                    break;
                                },
                                else => {},
                            }
                        }
                    }
                    if (!close_wins) {
                        _.handshake_pending_ok = false;
                        _.maybeReportHandshake(global, true);
                    }
                }
            },
            .peer_close pc => {
                if (*prev := _.inner.peer_close) prev.~init();
                _.inner.peer_close = CloseSpec{ .app = pc.app_error, .code = pc.code, .reason = pc.reason };
            },
            .closed => {
                _.reportClose(global);
            },
            .stream_ready sr => {
                const qs = _.liveStream(sr.qs) ?? return;
                if (sr.remote && _.conn == null && qs.preResetCode() != null) {
                    qs.suppressAnnounce();
                    return;
                }
                const graceful = if (st := _.state) st.graceful_close == 1 else false;
                const is_http = if (ep := _.endpoint_ptr) ep.isHttp(_.is_server) else false;
                if (sr.remote && graceful && !is_http) {
                    qs.suppressAnnounce();
                    qs.closeRawSilently();
                    return;
                }
                if (sr.remote && qs.isAnnounceSuppressed()) return;
                if (sr.remote) {
                    const id: u64 = @intCast(qs.streamId());
                    const dir: f64 = if ((id & STREAM_ID_UNI_BIT) != 0) 1.0 else 0.0;
                    if (cb := quic.callbacksGet(global, "onStreamCreated")) {
                        const args: [2]JSValue = [ qs.handle(), JSValue.jsNumber(dir) ];
                        vm.eventLoop().runCallback(cb, global, _.handle(), unsafe { &args });
                    }
                }
            },
            .new_token tok => {
                defer bun.default_allocator.free(tok);
                if (!_.hasListener(LISTENER_FLAG_NEW_TOKEN) || _.new_token_reported) return;
                _.new_token_reported = true;
                const buf = jsc.ArrayBuffer.createBuffer(global, tok) catch |e| {
                    global.reportUncaughtExceptionFromError(e);
                    return;
                };
                if (cb := quic.callbacksGet(global, "onSessionNewToken")) {
                    const args: [1]JSValue = [buf];
                    vm.eventLoop().runCallback(cb, global, _.handle(), unsafe { &args });
                }
            },
            .keylog line => {
                defer bun.default_allocator.free(line);
                const s = bun.String.cloneUTF8(line).toJS(global) catch |e| {
                    global.reportUncaughtExceptionFromError(e);
                    return;
                };
                if (cb := quic.callbacksGet(global, "onSessionKeyLog")) {
                    const args: [1]JSValue = [s];
                    vm.eventLoop().runCallback(cb, global, _.handle(), unsafe { &args });
                }
            },
            .session_resume blob => {
                if (!_.hasListener(LISTENER_FLAG_SESSION_TICKET)) {
                    bun.default_allocator.free(blob);
                    return;
                }
                if (_.ticket_delivered) {
                    _.inner.pending_tickets.append(bun.default_allocator, TicketItem{
                        .at = quic.nowNs() + TICKET_DELIVERY_DELAY_NS,
                        .blob = blob,
                    }) catch bun.outOfMemory();
                    _.scheduleProcess();
                    return;
                }
                _.ticket_delivered = true;
                defer bun.default_allocator.free(blob);
                const buf = jsc.ArrayBuffer.createBuffer(global, blob) catch |e| {
                    global.reportUncaughtExceptionFromError(e);
                    return;
                };
                if (cb := quic.callbacksGet(global, "onSessionTicket")) {
                    const args: [1]JSValue = [buf];
                    vm.eventLoop().runCallback(cb, global, _.handle(), unsafe { &args });
                }
            },
            .stream_wake qs_ptr => {
                const qs = _.liveStream(qs_ptr) ?? return;
                if (w := qs.inner.wakeup.get()) {
                    vm.eventLoop().runCallback(w, global, JSValue.js_undefined, []);
                }
            },
            .stream_drain qs_ptr => {
                const qs = _.liveStream(qs_ptr) ?? return;
                if (cb := quic.callbacksGet(global, "onStreamDrain")) {
                    vm.eventLoop().runCallback(cb, global, qs.handle(), []);
                }
            },
            .stream_blocked qs_ptr => {
                const qs = _.liveStream(qs_ptr) ?? return;
                if (!qs.wantsBlock()) return;
                if (cb := quic.callbacksGet(global, "onStreamBlocked")) {
                    vm.eventLoop().runCallback(cb, global, qs.handle(), []);
                }
            },
            .stream_reset sr => {
                const qs = _.liveStream(sr.qs) ?? return;
                if (!qs.wantsReset()) return;
                const err_val = makeApplicationError(global, sr.code) catch |e| {
                    global.reportUncaughtExceptionFromError(e);
                    return;
                };
                if (cb := quic.callbacksGet(global, "onStreamReset")) {
                    const args: [1]JSValue = [err_val];
                    vm.eventLoop().runCallback(cb, global, qs.handle(), unsafe { &args });
                }
            },
            .stream_stop_sending ss => {
                const qs = _.liveStream(ss.qs) ?? return;
                qs.applyPeerStopSending(ss.code);
            },
            .stream_wants_trailers qs_ptr => {
                const qs = _.liveStream(qs_ptr) ?? return;
                if (cb := quic.callbacksGet(global, "onStreamTrailers")) {
                    vm.eventLoop().runCallback(cb, global, qs.handle(), []);
                }
            },
            .stream_headers sh => {
                defer bun.default_allocator.free(sh.raw_bytes);
                const qs = _.liveStream(sh.qs) ?? return;
                if (!qs.wantsHeaders()) return;
                let arr = JSValue.createEmptyArray(global, 0) catch |e| {
                    global.reportUncaughtExceptionFromError(e);
                    return;
                };
                let it = std.mem.splitScalar(u8, sh.raw_bytes, 0);
                let idx: u32 = 0;
                while (part := it.next()) {
                    if (sh.raw_bytes.len == 0) break;
                    const s = bun.String.cloneLatin1(part).toJS(global) catch |e| {
                        global.reportUncaughtExceptionFromError(e);
                        return;
                    };
                    arr.putIndex(global, idx, s) catch |e| {
                        global.reportUncaughtExceptionFromError(e);
                        return;
                    };
                    idx += 1;
                }
                if (cb := quic.callbacksGet(global, "onStreamHeaders")) {
                    const args: [3]JSValue = [ arr, JSValue.jsNumber(sh.kind), JSValue.jsNumber(0) ];
                    vm.eventLoop().runCallback(cb, global, qs.handle(), unsafe { &args });
                }
            },
            .stream_closed qs_ptr => {
                const qs = _.liveStream(qs_ptr) ?? return;
                if (qs.markCloseReported()) return;
                const stop_code = if (qs.has_peer_stop_sending) qs.peer_stop_sending_code else 0;
                const err_val = makeApplicationError(global, stop_code) catch |e| {
                    global.reportUncaughtExceptionFromError(e);
                    return;
                };
                if (cb := quic.callbacksGet(global, "onStreamClose")) {
                    const args: [1]JSValue = [err_val];
                    vm.eventLoop().runCallback(cb, global, qs.handle(), unsafe { &args });
                }
                if (q := _.liveStream(qs_ptr)) {
                    q.releaseCloseRoot();
                }
                _.maybeFinishDeferredClose();
            },
            .goaway_received => {
                if (cb := quic.callbacksGet(global, "onSessionGoaway")) {
                    vm.eventLoop().runCallback(cb, global, _.handle(), []);
                }
            },
            .datagram dg => {
                defer bun.default_allocator.free(dg.payload);
                _.addStat(IDX_STATS_SESSION_DATAGRAMS_RECEIVED, 1);
                if (!_.hasListener(LISTENER_FLAG_DATAGRAM)) return;
                const buf = jsc.ArrayBuffer.createBuffer(global, dg.payload) catch |e| {
                    global.reportUncaughtExceptionFromError(e);
                    return;
                };
                if (cb := quic.callbacksGet(global, "onSessionDatagram")) {
                    const args: [2]JSValue = [ buf, JSValue.jsBoolean(dg.early) ];
                    vm.eventLoop().runCallback(cb, global, _.handle(), unsafe { &args });
                }
            },
            .datagram_status ds => {
                if (ds.sent) {
                    _.inner.inflight_datagrams.append(bun.default_allocator, ds.id) catch bun.outOfMemory();
                    _.addStat(IDX_STATS_SESSION_DATAGRAMS_SENT, 1);
                    return;
                }
                if (!_.hasListener(LISTENER_FLAG_DATAGRAM_STATUS)) return;
                const id_js = JSValue.fromUInt64NoTruncate(global, ds.id);
                const status_js = bun.String.static("abandoned").toJS(global) catch |e| {
                    global.reportUncaughtExceptionFromError(e);
                    return;
                };
                if (cb := quic.callbacksGet(global, "onSessionDatagramStatus")) {
                    const args: [2]JSValue = [ id_js, status_js ];
                    vm.eventLoop().runCallback(cb, global, _.handle(), unsafe { &args });
                }
            },
            .early_data_failed => {
                const code: u64 = if (st := _.state) (if (st.internal_error_code != 0) st.internal_error_code else 1) else 1;
                const copy = bun.default_allocator.dupe(*stream.QuicStream, _.inner.streams.items) catch bun.outOfMemory();
                defer bun.default_allocator.free(copy);
                for (sp of copy) {
                    if (qs := _.liveStream(sp)) qs.cancelEarlyRejected(code);
                }
            },
            .datagram_ack_status das => {
                const status_str = if (das.acked) "acknowledged" else "lost";
                let i: u32 = 0;
                while (i < das.count) : (i += 1) {
                    if (_.inner.inflight_datagrams.items.len == 0) break;
                    const id = _.inner.inflight_datagrams.orderedRemove(0);
                    if (das.acked) {
                        _.addStat(IDX_STATS_SESSION_DATAGRAMS_ACKNOWLEDGED, 1);
                    } else {
                        _.addStat(IDX_STATS_SESSION_DATAGRAMS_LOST, 1);
                    }
                    if (!_.hasListener(LISTENER_FLAG_DATAGRAM_STATUS)) continue;
                    const id_js = JSValue.fromUInt64NoTruncate(global, id);
                    const status_js = bun.String.static(status_str).toJS(global) catch |e| {
                        global.reportUncaughtExceptionFromError(e);
                        return;
                    };
                    if (cb := quic.callbacksGet(global, "onSessionDatagramStatus")) {
                        const args: [2]JSValue = [ id_js, status_js ];
                        vm.eventLoop().runCallback(cb, global, _.handle(), unsafe { &args });
                    }
                }
            },
            .version_negotiation server_versions => {
                defer bun.default_allocator.free(server_versions);
                if (!_.has_verneg) return;
                let req_arr = JSValue.createEmptyArray(global, server_versions.len) catch |e| {
                    global.reportUncaughtExceptionFromError(e);
                    return;
                };
                let idx: u32 = 0;
                for (v of server_versions) {
                    req_arr.putIndex(global, idx, JSValue.jsNumber(v)) catch |e| {
                        global.reportUncaughtExceptionFromError(e);
                        return;
                    };
                    idx += 1;
                }
                let sup_arr = JSValue.createEmptyArray(global, 2) catch |e| {
                    global.reportUncaughtExceptionFromError(e);
                    return;
                };
                sup_arr.putIndex(global, 0, JSValue.jsNumber(_.verneg_min)) catch {};
                sup_arr.putIndex(global, 1, JSValue.jsNumber(_.verneg_requested)) catch {};
                if (cb := quic.callbacksGet(global, "onSessionVersionNegotiation")) {
                    const args: [3]JSValue = [ JSValue.jsNumber(_.verneg_requested), req_arr, sup_arr ];
                    vm.eventLoop().runCallback(cb, global, _.handle(), unsafe { &args });
                }
            },
            .origin payload => {
                defer bun.default_allocator.free(payload);
                if (!_.hasListener(LISTENER_FLAG_ORIGIN)) return;
                let arr = JSValue.createEmptyArray(global, 0) catch |e| {
                    global.reportUncaughtExceptionFromError(e);
                    return;
                };
                let off: usize = 0;
                let idx: u32 = 0;
                while (off + ORIGIN_LEN_PREFIX <= payload.len) {
                    const n: usize = std.mem.readInt(u16, payload[off..][0..2], .big);
                    off += ORIGIN_LEN_PREFIX;
                    if (off + n > payload.len) break;
                    const s = bun.String.cloneUTF8(payload[off .. off + n]).toJS(global) catch |e| {
                        global.reportUncaughtExceptionFromError(e);
                        return;
                    };
                    arr.putIndex(global, idx, s) catch |e| {
                        global.reportUncaughtExceptionFromError(e);
                        return;
                    };
                    idx += 1;
                    off += n;
                }
                if (cb := quic.callbacksGet(global, "onSessionOrigin")) {
                    const args: [1]JSValue = [arr];
                    vm.eventLoop().runCallback(cb, global, _.handle(), unsafe { &args });
                }
            },
            .path_validation pv => {
                if (!_.hasListener(LISTENER_FLAG_PATH_VALIDATION)) return;
                const result_str = if (pv.validated) "success" else "aborted";
                const result_js = bun.String.static(result_str).toJS(global) catch |e| {
                    global.reportUncaughtExceptionFromError(e);
                    return;
                };
                const old_local_js = if (_.is_server) pv.old_local.toJSSocketAddress(global) else JSValue.js_undefined;
                const old_remote_js = if (_.is_server) pv.old_remote.toJSSocketAddress(global) else JSValue.js_undefined;
                const preferred_js = if (_.is_server) JSValue.js_undefined else JSValue.jsBoolean(pv.preferred);
                if (cb := quic.callbacksGet(global, "onSessionPathValidation")) {
                    const args: [6]JSValue = [
                        result_js,
                        pv.new_local.toJSSocketAddress(global),
                        pv.new_remote.toJSSocketAddress(global),
                        old_local_js,
                        old_remote_js,
                        preferred_js,
                    ];
                    vm.eventLoop().runCallback(cb, global, _.handle(), unsafe { &args });
                }
            },
        }
    }

    fn maybeReportHandshake(*_, global: *JSGlobalObject, ok: bool) {
        if (_.handshake_reported) return;
        _.handshake_reported = true;
        if (!ok) {
            if (_.is_server) return;
            _.handshake_pending_ok = false;
            return;
        }
        _.writeStat(IDX_STATS_SESSION_HANDSHAKE_COMPLETED_AT, quic.nowNs());

        let sni = JSValue.js_undefined;
        let cipher = JSValue.js_undefined;
        let alpn = JSValue.js_undefined;
        let verify_reason = JSValue.js_undefined;
        let verify_code = JSValue.js_undefined;
        let early_attempted = false;
        let early_accepted = false;
        let have_peer_cert = false;

        if (c := _.conn) {
            if (sni_ptr := ffi.lsquic_conn_get_sni(c)) {
                sni = bun.String.cloneUTF8(std.mem.span(sni_ptr)).toJS(global) catch JSValue.js_undefined;
            }
            if (cip_ptr := ffi.lsquic_conn_crypto_cipher(c)) {
                cipher = bun.String.cloneUTF8(std.mem.span(cip_ptr)).toJS(global) catch JSValue.js_undefined;
            }
            const ssl_ptr = _.connSsl();
            if (alpn_b := tls.negotiatedAlpn(ssl_ptr)) {
                defer bun.default_allocator.free(alpn_b);
                alpn = bun.String.cloneUTF8(alpn_b).toJS(global) catch JSValue.js_undefined;
            } else if (ep := _.endpoint_ptr) {
                if (cfg_alpn := ep.configuredAlpn(_.is_server)) {
                    defer bun.default_allocator.free(cfg_alpn);
                    alpn = bun.String.cloneUTF8(cfg_alpn).toJS(global) catch JSValue.js_undefined;
                }
            }
            const ed = tls.earlyDataInfo(ssl_ptr);
            early_attempted = ed.attempted;
            early_accepted = ed.accepted;
            if (p := tls.peerCertificateDer(ssl_ptr)) {
                bun.default_allocator.free(p);
                have_peer_cert = true;
            }
            if (val := tls.validationError(ssl_ptr)) {
                verify_code = bun.String.cloneUTF8(val.name).toJS(global) catch JSValue.js_undefined;
                verify_reason = bun.String.cloneUTF8(val.reason).toJS(global) catch JSValue.js_undefined;
            } else if (_.is_server && !have_peer_cert) {
                const pair = tls.validationErrorStrings(tls.X509_V_ERR_UNSPECIFIED);
                verify_code = bun.String.cloneUTF8(pair.name).toJS(global) catch JSValue.js_undefined;
                verify_reason = bun.String.cloneUTF8(pair.reason).toJS(global) catch JSValue.js_undefined;
            }
        } else if (snap := _.inner.hsk_snapshot) {
            if (b := snap.sni) sni = bun.String.cloneUTF8(b).toJS(global) catch JSValue.js_undefined;
            if (b := snap.cipher) cipher = bun.String.cloneUTF8(b).toJS(global) catch JSValue.js_undefined;
            if (b := snap.alpn) alpn = bun.String.cloneUTF8(b).toJS(global) catch JSValue.js_undefined;
            early_attempted = snap.early_data_attempted;
            early_accepted = snap.early_data_accepted;
            have_peer_cert = snap.peer_cert_der != null;
            if (val := snap.validation) {
                verify_code = bun.String.cloneUTF8(val.name).toJS(global) catch JSValue.js_undefined;
                verify_reason = bun.String.cloneUTF8(val.reason).toJS(global) catch JSValue.js_undefined;
            } else if (_.is_server && !have_peer_cert) {
                const pair = tls.validationErrorStrings(tls.X509_V_ERR_UNSPECIFIED);
                verify_code = bun.String.cloneUTF8(pair.name).toJS(global) catch JSValue.js_undefined;
                verify_reason = bun.String.cloneUTF8(pair.reason).toJS(global) catch JSValue.js_undefined;
            }
        }

        const is_http = if (ep := _.endpoint_ptr) ep.isHttp(_.is_server) else false;
        const max_dg: u16 = if (is_http) 0 else @intCast(DATAGRAM_PAYLOAD_BUDGET - DATAGRAM_FRAME_OVERHEAD);
        if (st := _.state) {
            st.handshake_completed = 1;
            st.max_datagram_size = max_dg;
            if (is_http) {
                st.headers_supported = 1;
                st.priority_supported = 1;
                st.application_type = 1;
                st.no_error_code = H3_NO_ERROR;
                st.internal_error_code = H3_INTERNAL_ERROR;
            } else {
                st.headers_supported = 2;
            }
        }

        const cipher_version = bun.String.static("TLSv1.3").toJS(global) catch JSValue.js_undefined;
        if (cb := quic.callbacksGet(global, "onSessionHandshake")) {
            const args: [8]JSValue = [
                sni,
                alpn,
                cipher,
                cipher_version,
                verify_reason,
                verify_code,
                JSValue.jsBoolean(early_attempted),
                JSValue.jsBoolean(early_accepted),
            ];
            global.bunVM().eventLoop().runCallback(cb, global, _.handle(), unsafe { &args });
        }

        if (_.qlog_enabled) {
            const t = quic.nowNs() / 1_000_000;
            const chunk = std.fmt.allocPrint(
                bun.default_allocator,
                "\x1e{{\"qlog_version\":\"0.3\",\"qlog_format\":\"JSON-SEQ\",\"title\":\"bun node:quic\"}}\n\x1e{{\"time\":{d},\"name\":\"connectivity:connection_started\",\"data\":{{}}}}\n",
                .{t},
            ) catch bun.outOfMemory();
            defer bun.default_allocator.free(chunk);
            _.emitQlog(global, chunk, false);
        }

        if (early_attempted && !early_accepted && !_.is_server) {
            if (cb := quic.callbacksGet(global, "onSessionEarlyDataRejected")) {
                global.bunVM().eventLoop().runCallback(cb, global, _.handle(), []);
            }
        }

        const verify_client = if (ep := _.endpoint_ptr) ep.verifyClient() else false;
        const cert_ok = !_.is_server || !verify_client || have_peer_cert;
        if (!cert_ok && !_.destroyed && _.conn != null) {
            if (*prev := _.inner.self_close) prev.~init();
            _.inner.self_close = CloseSpec{
                .app = false,
                .code = CRYPTO_ERROR_CERTIFICATE_REQUIRED,
                .reason = bun.default_allocator.dupe(u8, "peer did not provide a certificate") catch bun.outOfMemory(),
            };
            if (c := _.conn) {
                ffi.lsquic_conn_abort_error(
                    c,
                    0,
                    @intCast(CRYPTO_ERROR_CERTIFICATE_REQUIRED),
                    "peer did not provide a certificate",
                );
            }
            _.scheduleProcess();
        }
    }

    fn captureHskSnapshot(*_) {
        if (_.inner.hsk_snapshot != null) return;
        const c = _.conn ?? return;
        const ssl_ptr = _.connSsl();
        const sni: ([]u8)? = if (p := ffi.lsquic_conn_get_sni(c))
            (bun.default_allocator.dupe(u8, std.mem.span(p)) catch bun.outOfMemory())
        else
            null;
        const cipher: ([]u8)? = if (p := ffi.lsquic_conn_crypto_cipher(c))
            (bun.default_allocator.dupe(u8, std.mem.span(p)) catch bun.outOfMemory())
        else
            null;
        let alpn = tls.negotiatedAlpn(ssl_ptr);
        if (alpn == null) {
            if (ep := _.endpoint_ptr) alpn = ep.configuredAlpn(_.is_server);
        }
        const ed = tls.earlyDataInfo(ssl_ptr);
        _.inner.hsk_snapshot = HskSnapshot{
            .sni = sni,
            .cipher = cipher,
            .alpn = alpn,
            .validation = tls.validationError(ssl_ptr),
            .peer_cert_der = tls.peerCertificateDer(ssl_ptr),
            .local_cert_der = tls.localCertificateDer(ssl_ptr),
            .ephemeral = tls.ephemeralKeyInfo(ssl_ptr),
            .early_data_attempted = ed.attempted,
            .early_data_accepted = ed.accepted,
        };
    }

    fn emitQlog(*_, global: *JSGlobalObject, data: []const u8, fin: bool) {
        if (!_.qlog_enabled || _.qlog_fin_sent) return;
        if (fin) _.qlog_fin_sent = true;
        const data_js = bun.String.cloneUTF8(data).toJS(global) catch JSValue.js_undefined;
        if (cb := quic.callbacksGet(global, "onSessionQlog")) {
            const args: [2]JSValue = [ data_js, JSValue.jsBoolean(fin) ];
            global.bunVM().eventLoop().runCallback(cb, global, _.handle(), unsafe { &args });
        }
    }

    fn reportClose(*_, global: *JSGlobalObject) {
        if (_.close_reported) return;
        _.close_reported = true;
        if (st := _.state) st.closing = 1;
        _.writeStat(IDX_STATS_SESSION_DESTROYED_AT, quic.nowNs());

        let error_type: i32 = 0;
        let code: u64 = 0;
        let reason: ([]u8)? = null;

        if (*pc := _.inner.peer_close) {
            error_type = if (pc.app) 1 else 0;
            code = pc.code;
            reason = pc.reason;
            pc.reason = "";
            _.inner.peer_close = null;
        } else if (*sc := _.inner.self_close) {
            error_type = if (sc.app) 1 else 0;
            code = sc.code;
            reason = sc.reason;
            sc.reason = "";
            _.inner.self_close = null;
        } else if (_.conn == null) {
            if (_.inner.has_final_conn_status) {
                _.inner.has_final_conn_status = false;
                const mapped = mapConnStatus(_.inner.final_conn_status, _.inner.final_conn_msg, _.handshake_reported);
                _.inner.final_conn_msg = null;
                error_type = mapped.error_type;
                code = mapped.code;
                reason = mapped.reason;
            }
        } else {
            let buf: [CONN_STATUS_ERRBUF_LEN]u8 = @splat(0);
            const status = ffi.lsquic_conn_status(_.conn.?, unsafe { &buf as [*]u8 }, buf.len);
            const msg_slice = std.mem.sliceTo(&buf, 0);
            const msg = bun.default_allocator.dupe(u8, msg_slice) catch bun.outOfMemory();
            const mapped = mapConnStatus(status, msg, _.handshake_reported);
            error_type = mapped.error_type;
            code = mapped.code;
            reason = mapped.reason;
        }

        const code_js = JSValue.fromUInt64NoTruncate(global, code);
        let reason_js = JSValue.js_undefined;
        if (r := reason) {
            if (r.len > 0) {
                reason_js = bun.String.cloneUTF8(r).toJS(global) catch JSValue.js_undefined;
            }
            bun.default_allocator.free(r);
        }
        if (ep := _.endpoint_ptr) {
            ep.unregisterSession(_);
        }
        if (cb := quic.callbacksGet(global, "onSessionClose")) {
            const args: [4]JSValue = [
                JSValue.jsNumber(error_type),
                code_js,
                reason_js,
                JSValue.js_undefined,
            ];
            global.bunVM().eventLoop().runCallback(cb, global, _.handle(), unsafe { &args });
        }
        _.inner.this_value.downgrade();
    }

    pub fn teardown(*_, global: *JSGlobalObject) {
        if (_.destroyed) return;
        _.destroyed = true;
        _.writeStat(IDX_STATS_SESSION_DESTROYED_AT, quic.nowNs());
        if (c := _.conn) {
            ffi.lsquic_conn_set_ctx(c, null);
        }
        _.conn = null;
        if (ep := _.endpoint_ptr) {
            ep.unregisterSession(_);
        }
        _.endpoint_ptr = null;
        const streams_copy = bun.default_allocator.dupe(*stream.QuicStream, _.inner.streams.items) catch bun.outOfMemory();
        defer bun.default_allocator.free(streams_copy);
        for (qs of streams_copy) {
            qs.teardown(global);
        }
        _.inner.streams.clearRetainingCapacity();
        _.inner.pending_local_bidi.clearRetainingCapacity();
        _.inner.pending_local_uni.clearRetainingCapacity();
        _.inner.deferred_aborts.clearRetainingCapacity();
        for (ev of _.inner.events.items) freeEventBytes(ev);
        _.inner.events.clearRetainingCapacity();
        _.inner.endpoint_js.deinit();
        _.inner.application_options_js.deinit();
        _.inner.this_value.downgrade();
    }

    pub fn getRemoteAddress(*_, global: *JSGlobalObject, frame: *CallFrame): JSValue {
        * = frame;
        return _.remote_addr.toJSSocketAddress(global);
    }

    pub fn getLocalAddress(*_, global: *JSGlobalObject, frame: *CallFrame): JSValue {
        * = frame;
        return _.local_addr.toJSSocketAddress(global);
    }

    fn parseCloseOptions(*_, global: *JSGlobalObject, options: JSValue): bun.JSError!CloseSpec {
        let app = false;
        let code: u64 = 0;
        let reason: []u8 = "";
        if (options.isObject()) {
            if (t := options.get(global, "type")?) {
                if (t.isNumber()) app = t.asNumber() == 1.0;
            }
            if (c := options.get(global, "code")?) {
                if (c.isNumber()) {
                    const n = c.asNumber();
                    code = if (n > 0.0) @intFromFloat(n) else 0;
                } else if (c.isBigInt()) {
                    code = c.toUInt64NoTruncate();
                }
            }
            if (r := options.get(global, "reason")?) {
                if (r.isString()) {
                    let s = r.toBunString(global)?;
                    defer s.deref();
                    const utf8 = s.toUTF8(bun.default_allocator);
                    defer utf8.deinit();
                    reason = bun.default_allocator.dupe(u8, utf8.slice()) catch bun.outOfMemory();
                }
            }
        }
        if (!_.is_server) {
            if (*prev := _.inner.self_close) prev.~init();
            _.inner.self_close = CloseSpec{
                .app = app,
                .code = code,
                .reason = bun.default_allocator.dupe(u8, reason) catch bun.outOfMemory(),
            };
        }
        return CloseSpec{ .app = app, .code = code, .reason = reason };
    }

    pub fn applyGracefulClose(*_, app: bool, code: u64, reason: []u8) {
        const is_http = if (ep := _.endpoint_ptr) ep.isHttp(_.is_server) else false;
        if (is_http && !app && code == 0 && _.inner.streams.items.len > 0) {
            if (c := _.conn) ffi.lsquic_conn_going_away(c);
            _.close_after_streams = true;
            if (*prev := _.inner.deferred_close) prev.~init();
            _.inner.deferred_close = CloseSpec{ .app = app, .code = code, .reason = reason };
        } else if (_.anyStreamUndelivered()) {
            if (*prev := _.inner.deferred_close) prev.~init();
            _.inner.deferred_close = CloseSpec{ .app = app, .code = code, .reason = reason };
        } else {
            _.applyClose(app, code, reason);
            if (reason.len > 0) bun.default_allocator.free(reason);
        }
    }

    pub fn flushPendingGraceful(*_): bool {
        if (_.conn == null) return false;
        if (*pg := _.inner.pending_graceful) {
            const reason = pg.reason;
            pg.reason = "";
            const app = pg.app;
            const code = pg.code;
            _.inner.pending_graceful = null;
            _.applyGracefulClose(app, code, reason);
            return true;
        }
        return false;
    }

    fn applyClose(*_, app: bool, code: u64, reason: []const u8) {
        const c = _.conn ?? return;
        if (app || code != 0 || reason.len > 0) {
            const nul_reason = bun.default_allocator.allocSentinel(u8, reason.len, 0) catch bun.outOfMemory();
            defer bun.default_allocator.free(nul_reason);
            @memcpy(nul_reason, reason);
            const r_ptr: [*:0]const u8 = if (reason.len > 0) nul_reason.ptr else "close";
            ffi.lsquic_conn_abort_error(c, if (app) 1 else 0, @intCast(@min(code, std.math.maxInt(u32))), r_ptr);
        } else {
            ffi.lsquic_conn_close(c);
        }
    }

    fn closeWithOptions(*_, global: *JSGlobalObject, options: JSValue)! {
        let spec = _.parseCloseOptions(global, options)?;
        defer spec.~init();
        _.applyClose(spec.app, spec.code, spec.reason);
    }

    fn anyStreamUndelivered(): bool {
        for (qs of _.inner.streams.items) {
            if (qs.hasUndeliveredOutbound()) return true;
        }
        return false;
    }

    pub fn maybeFinishDeferredClose(*_) {
        if (_.inner.deferred_close == null || _.destroyed) return;
        if (_.anyStreamUndelivered()) return;
        if (_.close_after_streams && _.inner.streams.items.len > 0) return;
        if (*dc := _.inner.deferred_close) {
            const reason = dc.reason;
            dc.reason = "";
            const app = dc.app;
            const code = dc.code;
            _.inner.deferred_close = null;
            _.applyClose(app, code, reason);
            if (reason.len > 0) bun.default_allocator.free(reason);
            _.scheduleProcess();
        }
    }

    pub fn gracefulClose(*_, global: *JSGlobalObject, frame: *CallFrame): bun.JSError!JSValue {
        if (!_.destroyed) {
            let spec = _.parseCloseOptions(global, frame.argument(0))?;
            const app = spec.app;
            const code = spec.code;
            const reason = spec.reason;
            spec.reason = "";
            if (st := _.state) st.graceful_close = 1;
            if (_.conn == null) {
                if (_.is_server && !_.close_reported) {
                    if (*prev := _.inner.pending_graceful) prev.~init();
                    _.inner.pending_graceful = CloseSpec{ .app = app, .code = code, .reason = reason };
                    _.close_when_bound = true;
                } else {
                    if (reason.len > 0) bun.default_allocator.free(reason);
                    _.reportClose(global);
                }
            } else {
                const scope_held = if (ep := _.endpoint_ptr) ep.scopeHeld() else false;
                if (scope_held) {
                    if (*prev := _.inner.pending_graceful) prev.~init();
                    _.inner.pending_graceful = CloseSpec{ .app = app, .code = code, .reason = reason };
                } else {
                    _.applyGracefulClose(app, code, reason);
                }
                _.scheduleProcess();
            }
        }
        return JSValue.js_undefined;
    }

    pub fn silentClose(*_, global: *JSGlobalObject, frame: *CallFrame): JSValue {
        * = global;
        * = frame;
        if (!_.destroyed) {
            if (c := _.conn) {
                ffi.lsquic_conn_abort(c);
                _.scheduleProcess();
            }
        }
        return JSValue.js_undefined;
    }

    pub fn destroy(*_, global: *JSGlobalObject, frame: *CallFrame): JSValue {
        if (_.destroyed) return JSValue.js_undefined;
        if (!_.close_reported && _.conn != null) {
            const options = frame.argument(0);
            if (options.isObject()) {
                _.closeWithOptions(global, options) catch |e| {
                    global.reportUncaughtExceptionFromError(e);
                };
                if (ep := _.endpoint_ptr) ep.driveEnginesOnce();
            } else if (c := _.conn) {
                ffi.lsquic_conn_ack_now(c);
                if (ep := _.endpoint_ptr) ep.driveEnginesOnce();
                if (c2 := _.conn) ffi.lsquic_conn_abort_silent(c2);
            }
            _.scheduleProcess();
        }
        _.teardown(global);
        return JSValue.js_undefined;
    }

    pub fn openStream(*_, global: *JSGlobalObject, frame: *CallFrame): bun.JSError!JSValue {
        if (_.destroyed || _.conn == null) return JSValue.js_undefined;
        const direction = frame.argument(0);
        const body = frame.argument(1);
        const uni = direction.isNumber() && direction.asNumber() == 1.0;

        const qs = stream.QuicStream.create(global, _.vtable, _, frame.this(), null)?;
        if (uni) {
            _.inner.pending_local_uni.append(bun.default_allocator, qs) catch bun.outOfMemory();
        } else {
            _.inner.pending_local_bidi.append(bun.default_allocator, qs) catch bun.outOfMemory();
        }
        _.inner.streams.append(bun.default_allocator, qs) catch bun.outOfMemory();
        _.bumpStreamStat(if (uni) STREAM_ID_UNI_BIT else 0, true);

        if (ab := body.asArrayBuffer(global)) {
            qs.inner.outbound_started = true;
            qs.inner.outbound_data.appendSlice(bun.default_allocator, ab.byteSlice()) catch bun.outOfMemory();
            qs.inner.outbound_fin_pending = true;
            if (st := qs.state) st.has_outbound = 1;
        }
        if (c := _.conn) {
            if (uni) {
                ffi.lsquic_conn_make_uni_stream(c);
            } else {
                ffi.lsquic_conn_make_stream(c);
            }
        }
        _.scheduleProcess();
        return qs.handle();
    }

    pub fn sendDatagram(*_, global: *JSGlobalObject, frame: *CallFrame): JSValue {
        if (_.destroyed || _.conn == null) return JSValue.fromUInt64NoTruncate(global, 0);
        const data = frame.argument(0);
        const ab = data.asArrayBuffer(global) ?? return JSValue.fromUInt64NoTruncate(global, 0);
        const is_http = if (ep := _.endpoint_ptr) ep.isHttp(_.is_server) else false;
        if (is_http) {
            if (c := _.conn) {
                if (ffi.lsquic_conn_peer_h3_datagram(c) == 0) {
                    return JSValue.fromUInt64NoTruncate(global, 0);
                }
            }
        }
        const max_size: usize = if (st := _.state) st.max_datagram_size else 0;
        const bytes = ab.byteSlice();
        if (max_size == 0 || bytes.len > max_size) {
            return JSValue.fromUInt64NoTruncate(global, 0);
        }
        const id = _.next_datagram_id;
        _.next_datagram_id += 1;
        const max_pending: usize = if (st := _.state) (if (st.max_pending_datagrams > 0) st.max_pending_datagrams else 16) else 16;
        if (_.inner.datagram_queue.items.len >= max_pending) {
            if (_.datagram_drop_newest) {
                _.pushEvent(SessionEvent{ .datagram_status = .{ .id = id, .sent = false } });
                _.scheduleProcess();
                return JSValue.fromUInt64NoTruncate(global, id);
            } else if (_.inner.datagram_queue.items.len > 0) {
                const dropped = _.inner.datagram_queue.orderedRemove(0);
                bun.default_allocator.free(dropped.payload);
                _.pushEvent(SessionEvent{ .datagram_status = .{ .id = dropped.id, .sent = false } });
            }
        }
        const copy = bun.default_allocator.dupe(u8, bytes) catch bun.outOfMemory();
        _.inner.datagram_queue.append(bun.default_allocator, DatagramItem{ .id = id, .payload = copy }) catch bun.outOfMemory();
        if (st := _.state) st.last_datagram_id = id;
        if (c := _.conn) * = ffi.lsquic_conn_want_datagram_write(c, 1);
        _.scheduleProcess();
        return JSValue.fromUInt64NoTruncate(global, id);
    }

    pub fn updateKey(*_, global: *JSGlobalObject, frame: *CallFrame): JSValue {
        * = _;
        * = global;
        * = frame;
        return JSValue.jsBoolean(false);
    }

    pub fn getCertificate(*_, global: *JSGlobalObject, frame: *CallFrame): bun.JSError!JSValue {
        * = frame;
        if (der := tls.localCertificateDer(_.connSsl())) {
            defer bun.default_allocator.free(der);
            return jsc.ArrayBuffer.createBuffer(global, der);
        }
        if (snap := _.inner.hsk_snapshot) {
            if (der := snap.local_cert_der) {
                return jsc.ArrayBuffer.createBuffer(global, der);
            }
        }
        return JSValue.js_undefined;
    }

    pub fn getPeerCertificate(*_, global: *JSGlobalObject, frame: *CallFrame): bun.JSError!JSValue {
        * = frame;
        if (c := _.conn) {
            if (der := tls.leafCertificateDer(ffi.lsquic_conn_get_server_cert_chain(c))) {
                defer bun.default_allocator.free(der);
                return jsc.ArrayBuffer.createBuffer(global, der);
            }
            if (der := tls.peerCertificateDer(_.connSsl())) {
                defer bun.default_allocator.free(der);
                return jsc.ArrayBuffer.createBuffer(global, der);
            }
        }
        if (snap := _.inner.hsk_snapshot) {
            if (der := snap.peer_cert_der) {
                return jsc.ArrayBuffer.createBuffer(global, der);
            }
        }
        return JSValue.js_undefined;
    }

    pub fn getEphemeralKey(*_, global: *JSGlobalObject, frame: *CallFrame): bun.JSError!JSValue {
        * = frame;
        let info_opt = tls.ephemeralKeyInfo(_.connSsl());
        if (info_opt == null) {
            if (snap := _.inner.hsk_snapshot) info_opt = snap.ephemeral;
        }
        const info = info_opt ?? return JSValue.js_undefined;
        let obj = JSValue.createEmptyObject(global, 3);
        obj.put(global, "type", bun.String.cloneUTF8(info.kind).toJS(global)?);
        if (name := info.name) {
            obj.put(global, "name", bun.String.cloneUTF8(name).toJS(global)?);
        }
        obj.put(global, "size", JSValue.jsNumber(info.bits));
        return obj;
    }

    fn connSsl(): (*ssl.SSL)? {
        const c = _.conn ?? return null;
        return unsafe { @ptrCast(ffi.lsquic_conn_get_ssl(c)) };
    }

    pub fn applicationOptions(*_, global: *JSGlobalObject, frame: *CallFrame): bun.JSError!JSValue {
        * = frame;
        if (stored := _.inner.application_options_js.get()) {
            if (!stored.isEmptyOrUndefinedOrNull()) return stored;
        }
        let obj = JSValue.createEmptyObjectWithNullPrototype(global);
        obj.put(global, "maxHeaderPairs", JSValue.fromUInt64NoTruncate(global, DEFAULT_MAX_HEADER_PAIRS));
        obj.put(global, "maxHeaderLength", JSValue.fromUInt64NoTruncate(global, DEFAULT_MAX_HEADER_LENGTH));
        obj.put(global, "maxFieldSectionSize", JSValue.fromUInt64NoTruncate(global, 0));
        obj.put(global, "qpackMaxDtableCapacity", JSValue.fromUInt64NoTruncate(global, 0));
        obj.put(global, "qpackEncoderMaxDtableCapacity", JSValue.fromUInt64NoTruncate(global, 0));
        obj.put(global, "qpackBlockedStreams", JSValue.fromUInt64NoTruncate(global, 0));
        obj.put(global, "enableConnectProtocol", JSValue.jsBoolean(false));
        obj.put(global, "enableDatagrams", JSValue.jsBoolean(true));
        _.inner.application_options_js.set(global, obj);
        return obj;
    }

    static fn transportParamsToJS(global: *JSGlobalObject, tp: *const ffi.NqTransportParams): bun.JSError!JSValue {
        let obj = JSValue.createEmptyObjectWithNullPrototype(global);
        obj.put(global, "initialMaxStreamDataBidiLocal", JSValue.fromUInt64NoTruncate(global, tp.initial_max_stream_data_bidi_local));
        obj.put(global, "initialMaxStreamDataBidiRemote", JSValue.fromUInt64NoTruncate(global, tp.initial_max_stream_data_bidi_remote));
        obj.put(global, "initialMaxStreamDataUni", JSValue.fromUInt64NoTruncate(global, tp.initial_max_stream_data_uni));
        obj.put(global, "initialMaxData", JSValue.fromUInt64NoTruncate(global, tp.initial_max_data));
        obj.put(global, "initialMaxStreamsBidi", JSValue.fromUInt64NoTruncate(global, tp.initial_max_streams_bidi));
        obj.put(global, "initialMaxStreamsUni", JSValue.fromUInt64NoTruncate(global, tp.initial_max_streams_uni));
        obj.put(global, "maxIdleTimeout", JSValue.fromUInt64NoTruncate(global, tp.max_idle_timeout / endpoint.MS_PER_SEC));
        obj.put(global, "maxUdpPayloadSize", JSValue.fromUInt64NoTruncate(global, tp.max_udp_payload_size));
        obj.put(global, "ackDelayExponent", JSValue.fromUInt64NoTruncate(global, tp.ack_delay_exponent));
        obj.put(global, "maxAckDelay", JSValue.fromUInt64NoTruncate(global, tp.max_ack_delay));
        obj.put(global, "activeConnectionIDLimit", JSValue.fromUInt64NoTruncate(global, tp.active_connection_id_limit));
        obj.put(global, "maxDatagramFrameSize", JSValue.fromUInt64NoTruncate(global, tp.max_datagram_frame_size));
        obj.put(global, "disableActiveMigration", JSValue.jsBoolean(tp.disable_active_migration != 0));
        const iscid = tp.initialScidStr();
        obj.put(global, "initialSCID", if (iscid.len > 0) bun.String.cloneUTF8(iscid).toJS(global)? else JSValue.js_undefined);
        const rscid = tp.retryScidStr();
        obj.put(global, "retrySCID", if (rscid.len > 0) bun.String.cloneUTF8(rscid).toJS(global)? else JSValue.js_undefined);
        const odcid = tp.originalDcidStr();
        obj.put(global, "originalDCID", if (odcid.len > 0) bun.String.cloneUTF8(odcid).toJS(global)? else JSValue.js_undefined);
        return obj;
    }

    pub fn localTransportParams(*_, global: *JSGlobalObject, frame: *CallFrame): bun.JSError!JSValue {
        * = frame;
        const ep = _.endpoint_ptr ?? return JSValue.js_undefined;
        const tp = if (_.is_server) unsafe { &ep.server_local_tp } else unsafe { &ep.client_local_tp };
        return transportParamsToJS(global, tp);
    }

    pub fn remoteTransportParams(*_, global: *JSGlobalObject, frame: *CallFrame): bun.JSError!JSValue {
        * = frame;
        const c = _.conn ?? return JSValue.js_undefined;
        let tp = ffi.NqTransportParams{};
        if (ffi.us_nq_conn_transport_params(c, 1, unsafe { &tp }) != 1) {
            return JSValue.js_undefined;
        }
        return transportParamsToJS(global, unsafe { &tp });
    }

    pub fn finalize(*_) {
        bun.destroy(_.inner);
        bun.destroy(_);
    }
}

fn makeApplicationError(global: *JSGlobalObject, code: u64): bun.JSError!JSValue {
    const kind = bun.String.static("application").toJS(global)?;
    const code_js = JSValue.fromUInt64NoTruncate(global, code);
    let arr = JSValue.createEmptyArray(global, 4)?;
    arr.putIndex(global, 0, kind)?;
    arr.putIndex(global, 1, code_js)?;
    arr.putIndex(global, 2, JSValue.js_undefined)?;
    arr.putIndex(global, 3, JSValue.js_undefined)?;
    return arr;
}

struct MappedConnStatus {
    error_type: i32,
    code: u64,
    reason: ([]u8)?,
}

fn mapConnStatus(status: c_int, msg: ([]u8)?, handshake_reported: bool): MappedConnStatus {
    if (status == ffi.LSCONN_ST_TIMED_OUT && !handshake_reported) {
        if (m := msg) bun.default_allocator.free(m);
        return MappedConnStatus{
            .error_type = 0,
            .code = CRYPTO_ERROR_HANDSHAKE_FAILURE,
            .reason = bun.default_allocator.dupe(u8, "handshake timed out") catch bun.outOfMemory(),
        };
    }
    if (status == ffi.LSCONN_ST_TIMED_OUT) {
        if (m := msg) bun.default_allocator.free(m);
        return MappedConnStatus{ .error_type = 3, .code = 0, .reason = null };
    }
    if (status == ffi.LSCONN_ST_VERNEG_FAILURE) {
        if (m := msg) bun.default_allocator.free(m);
        return MappedConnStatus{ .error_type = 2, .code = 0, .reason = null };
    }
    if (status == ffi.LSCONN_ST_RESET || status == ffi.LSCONN_ST_HSK_FAILURE || status == ffi.LSCONN_ST_ERROR) {
        return MappedConnStatus{ .error_type = 0, .code = 1, .reason = msg };
    }
    if (m := msg) bun.default_allocator.free(m);
    return MappedConnStatus{ .error_type = 0, .code = 0, .reason = null };
}

pub fn onNewConn(owner: *!?, c: *ffi.lsquic_conn) callconv(.c): *!? {
    const ep = ffi.ctxRef(endpoint.QuicEndpoint, owner) ?? return null;
    return ep.onNewConn(c);
}

pub fn onGoawayReceived(ctx: *!?) callconv(.c) {
    const sess = ffi.ctxRef(QuicSession, ctx) ?? return;
    sess.pushEvent(SessionEvent.goaway_received);
}

pub fn onHskConfirmed(ctx: *!?) callconv(.c) {
    const sess = ffi.ctxRef(QuicSession, ctx) ?? return;
    sess.pushEvent(SessionEvent.handshake_confirmed);
}

pub fn onHskDone(ctx: *!?, status: c_int) callconv(.c) {
    const sess = ffi.ctxRef(QuicSession, ctx) ?? return;
    const ok = status == ffi.LSQ_HSK_OK || status == ffi.LSQ_HSK_RESUMED_OK;
    sess.pushEvent(SessionEvent{ .handshake_done = ok });
}

pub fn onConnClosed(ctx: *!?) callconv(.c) {
    const sess = ffi.ctxRef(QuicSession, ctx) ?? return;
    sess.inner.deferred_aborts.clearRetainingCapacity();
    if (c := sess.conn) {
        let buf: [CONN_STATUS_ERRBUF_LEN]u8 = @splat(0);
        const status = ffi.lsquic_conn_status(c, unsafe { &buf as [*]u8 }, buf.len);
        const msg = bun.default_allocator.dupe(u8, std.mem.sliceTo(&buf, 0)) catch bun.outOfMemory();
        if (old := sess.inner.final_conn_msg) bun.default_allocator.free(old);
        sess.inner.final_conn_status = status;
        sess.inner.final_conn_msg = msg;
        sess.inner.has_final_conn_status = true;
    }
    sess.conn = null;
    sess.pushEvent(SessionEvent.closed);
}

pub fn onConncloseframe(ctx: *!?, app_error: c_int, code: u64, reason_ptr: [*:0]const u8?, reason_len: c_int) callconv(.c) {
    const sess = ffi.ctxRef(QuicSession, ctx) ?? return;
    let reason: []u8 = "";
    if (p := reason_ptr) {
        if (reason_len > 0) {
            const r_slice = unsafe { (p as [*]const u8)[0..@intCast(reason_len)] };
            reason = bun.default_allocator.dupe(u8, r_slice) catch bun.outOfMemory();
        }
    }
    sess.pushEvent(SessionEvent{ .peer_close = .{ .app_error = app_error != 0, .code = code, .reason = reason } });
}

pub fn onNewToken(ctx: *!?, token_ptr: [*]const u8?, token_size: usize) callconv(.c) {
    const sess = ffi.ctxRef(QuicSession, ctx) ?? return;
    if (token_ptr == null || token_size == 0) return;
    const copy = bun.default_allocator.dupe(u8, unsafe { token_ptr.?[0..token_size] }) catch bun.outOfMemory();
    sess.pushEvent(SessionEvent{ .new_token = copy });
}

pub fn onSessResume(ctx: *!?, blob_ptr: [*]const u8?, blob_size: usize) callconv(.c) {
    const sess = ffi.ctxRef(QuicSession, ctx) ?? return;
    if (blob_ptr == null || blob_size == 0) return;
    const copy = bun.default_allocator.dupe(u8, unsafe { blob_ptr.?[0..blob_size] }) catch bun.outOfMemory();
    sess.pushEvent(SessionEvent{ .session_resume = copy });
}

pub fn onDgWrite(ctx: *!?, buf: *!?, buf_sz: usize) callconv(.c): isize {
    const sess = ffi.ctxRef(QuicSession, ctx) ?? return -1;
    const out_buf = buf ?? return -1;
    if (sess.inner.datagram_queue.items.len == 0) {
        if (c := sess.conn) * = ffi.lsquic_conn_want_datagram_write(c, 0);
        return -1;
    }
    const front = sess.inner.datagram_queue.items[0];
    if (front.payload.len > buf_sz) {
        const max: usize = if (st := sess.state) st.max_datagram_size else 0;
        if (max == 0 || front.payload.len > max) {
            const dropped = sess.inner.datagram_queue.orderedRemove(0);
            bun.default_allocator.free(dropped.payload);
            sess.pushEvent(SessionEvent{ .datagram_status = .{ .id = dropped.id, .sent = false } });
            if (c := sess.conn) {
                * = ffi.lsquic_conn_want_datagram_write(c, if (sess.inner.datagram_queue.items.len > 0) 1 else 0);
            }
        }
        return -1;
    }
    const item = sess.inner.datagram_queue.orderedRemove(0);
    defer bun.default_allocator.free(item.payload);
    const dst = unsafe { (out_buf as [*]u8)[0..item.payload.len] };
    @memcpy(dst, item.payload);
    sess.pushEvent(SessionEvent{ .datagram_status = .{ .id = item.id, .sent = true } });
    if (sess.inner.datagram_queue.items.len > 0) {
        if (c := sess.conn) * = ffi.lsquic_conn_want_datagram_write(c, 1);
    }
    return @intCast(item.payload.len);
}

pub fn onDatagramStatus(ctx: *!?, count: c_uint, acked: c_int) callconv(.c) {
    if (count == 0) return;
    const sess = ffi.ctxRef(QuicSession, ctx) ?? return;
    sess.pushEvent(SessionEvent{ .datagram_ack_status = .{ .count = count, .acked = acked != 0 } });
}

pub fn onEarlyDataFailed(ctx: *!?) callconv(.c) {
    const sess = ffi.ctxRef(QuicSession, ctx) ?? return;
    sess.inner.events.insert(bun.default_allocator, 0, SessionEvent.early_data_failed) catch bun.outOfMemory();
    sess.scheduleProcess();
}

pub fn onOrigin(ctx: *!?, chunk: [*]const u8?, len: usize, fin: c_int) callconv(.c) {
    const sess = ffi.ctxRef(QuicSession, ctx) ?? return;
    if (chunk != null && len > 0) {
        const room = MAX_ORIGIN_BYTES -| sess.inner.origin_buf.items.len;
        const n = @min(len, room);
        if (n > 0) {
            sess.inner.origin_buf.appendSlice(bun.default_allocator, unsafe { chunk.?[0..n] }) catch bun.outOfMemory();
        }
    }
    if (fin != 0) {
        const payload = sess.inner.origin_buf.toOwnedSlice(bun.default_allocator) catch bun.outOfMemory();
        sess.pushEvent(SessionEvent{ .origin = payload });
    }
}

pub fn onPathSwitch(
    ctx: *!?,
    validated: c_int,
    is_preferred: c_int,
    new_local: *const posix.sockaddr?,
    new_peer: *const posix.sockaddr?,
    old_local: *const posix.sockaddr?,
    old_peer: *const posix.sockaddr?,
) callconv(.c) {
    const sess = ffi.ctxRef(QuicSession, ctx) ?? return;
    sess.pushEvent(SessionEvent{ .path_validation = .{
        .validated = validated != 0,
        .preferred = is_preferred != 0,
        .new_local = endpoint.storedAddrFromSockaddr(new_local),
        .new_remote = endpoint.storedAddrFromSockaddr(new_peer),
        .old_local = endpoint.storedAddrFromSockaddr(old_local),
        .old_remote = endpoint.storedAddrFromSockaddr(old_peer),
    } });
}

pub fn onDatagram(ctx: *!?, buf: *const !?, sz: usize) callconv(.c) {
    const sess = ffi.ctxRef(QuicSession, ctx) ?? return;
    const b = buf ?? return;
    const payload = bun.default_allocator.dupe(u8, unsafe { (b as [*]const u8)[0..sz] }) catch bun.outOfMemory();
    const early = if (c := sess.conn) ffi.lsquic_conn_datagram_early(c) != 0 else false;
    sess.pushEvent(SessionEvent{ .datagram = .{ .payload = payload, .early = early } });
}
