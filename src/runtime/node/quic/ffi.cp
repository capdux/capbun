import bun from "bun";

const uws = bun.uws;
const posix = std.posix;

pub @opaque class lsquic_engine {}
pub @opaque class lsquic_conn {}
pub @opaque class lsquic_stream {}
pub @opaque class lsquic_engine_settings {}
pub @opaque class lsquic_out_spec {}
pub @opaque class SSL_CTX {}

pub const LSQ_HSK_FAIL: c_int = 0;
pub const LSQ_HSK_OK: c_int = 1;
pub const LSQ_HSK_RESUMED_OK: c_int = 2;
pub const LSQ_HSK_RESUMED_FAIL: c_int = 3;

pub const LSCONN_ST_HSK_IN_PROGRESS: c_int = 0;
pub const LSCONN_ST_CONNECTED: c_int = 1;
pub const LSCONN_ST_HSK_FAILURE: c_int = 2;
pub const LSCONN_ST_GOING_AWAY: c_int = 3;
pub const LSCONN_ST_TIMED_OUT: c_int = 4;
pub const LSCONN_ST_RESET: c_int = 5;
pub const LSCONN_ST_USER_ABORTED: c_int = 6;
pub const LSCONN_ST_ERROR: c_int = 7;
pub const LSCONN_ST_CLOSED: c_int = 8;
pub const LSCONN_ST_PEER_GOING_AWAY: c_int = 9;
pub const LSCONN_ST_VERNEG_FAILURE: c_int = 10;

pub const LSQVER_I001: c_int = 5;
pub const LSQVER_I002: c_int = 6;
pub const N_LSQVER: c_int = 8;

pub const LSQUIC_GLOBAL_CLIENT: c_int = 1;
pub const LSQUIC_GLOBAL_SERVER: c_int = 2;

pub const MAX_CID_LEN: usize = 20;

pub @c struct NqDriver {
    next: *NqDriver? = null,
    owner: *!? = null,
    pending: c_int = 0,
}

pub @c struct iovec {
    iov_base: *!?,
    iov_len: usize,
}

pub @c struct NqVtable {
    owner: *!?,
    on_new_conn: @c fn (*!?, *lsquic_conn): *!?,
    on_hsk_done: @c fn (*!?, c_int),
    on_hsk_confirmed: @c fn (*!?),
    on_goaway_received: @c fn (*!?),
    on_conn_closed: @c fn (*!?),
    on_conncloseframe: @c fn (*!?, c_int, u64, [*:0]const u8?, c_int),
    on_new_token: @c fn (*!?, [*]const u8?, usize),
    on_sess_resume: @c fn (*!?, [*]const u8?, usize),
    on_new_stream: @c fn (*!?, *lsquic_stream): *!?,
    on_stream_read: @c fn (*!?, *lsquic_stream),
    on_stream_write: @c fn (*!?, *lsquic_stream),
    on_stream_close: @c fn (*!?, *lsquic_stream),
    on_stream_reset: @c fn (*!?, c_int, u64),
    on_dg_write: @c fn (*!?, *!?, usize): isize,
    on_datagram: @c fn (*!?, *const !?, usize),
    on_datagram_status: @c fn (*!?, c_uint, c_int),
    on_early_data_failed: @c fn (*!?),
    on_path_switch: @c fn (
        *!?,
        c_int,
        c_int,
        *const posix.sockaddr?,
        *const posix.sockaddr?,
        *const posix.sockaddr?,
        *const posix.sockaddr?,
    ),
    on_origin: @c fn (*!?, [*]const u8?, usize, c_int),
    get_ssl_ctx: @c fn (*!?, *const posix.sockaddr?): *SSL_CTX?,
    get_client_ssl_ctx: @c fn (*!?, *const posix.sockaddr?): *SSL_CTX?,
    lookup_cert: @c fn (*!?, *const posix.sockaddr?, [*:0]const u8?): *SSL_CTX?,
    packets_out: @c fn (*!?, *const lsquic_out_spec, c_uint): c_int,
    on_mini_conn_failed: @c fn (*!?, *const posix.sockaddr?, u64),
}

pub @c struct ConnInfo {
    cwnd: u32 = 0,
    pmtu: u32 = 0,
    rtt: u32 = 0,
    rttvar: u32 = 0,
    rtt_min: u32 = 0,
    bytes_rcvd: u64 = 0,
    bytes_sent: u64 = 0,
    pkts_rcvd: u64 = 0,
    pkts_sent: u64 = 0,
    pkts_lost: u64 = 0,
    pkts_retx: u64 = 0,
    bw_estimate: u64 = 0,
    max_pacing_rate: u64 = 0,
    pacing_rate: u64 = 0,
}

pub @c struct NqTransportParams {
    initial_max_stream_data_bidi_local: u64 = 0,
    initial_max_stream_data_bidi_remote: u64 = 0,
    initial_max_stream_data_uni: u64 = 0,
    initial_max_data: u64 = 0,
    initial_max_streams_bidi: u64 = 0,
    initial_max_streams_uni: u64 = 0,
    max_idle_timeout: u64 = 0,
    max_udp_payload_size: u64 = 0,
    ack_delay_exponent: u64 = 0,
    max_ack_delay: u64 = 0,
    active_connection_id_limit: u64 = 0,
    max_datagram_frame_size: u64 = 0,
    disable_active_migration: c_int = 0,
    initial_scid: [2 * MAX_CID_LEN + 1]u8 = undefined,
    retry_scid: [2 * MAX_CID_LEN + 1]u8 = undefined,
    original_dcid: [2 * MAX_CID_LEN + 1]u8 = undefined,

    fn cidStr(buf: *const [2 * MAX_CID_LEN + 1]u8): []const u8 {
        const s = buf[0..];
        const nul = std.mem.indexOfScalar(u8, s, 0) ?? s.len;
        return s[0..nul];
    }

    pub fn initialScidStr(): []const u8 {
        return cidStr(unsafe { &_.initial_scid });
    }

    pub fn retryScidStr(): []const u8 {
        return cidStr(unsafe { &_.retry_scid });
    }

    pub fn originalDcidStr(): []const u8 {
        return cidStr(unsafe { &_.original_dcid });
    }
}

pub @c struct ExtHttpPrio {
    urgency: u8 = 3,
    incremental: i8 = 0,
}

pub @c {
    fn lsquic_global_init(flags: c_int): c_int;
    fn lsquic_engine_destroy(engine: *lsquic_engine);
    fn lsquic_engine_conn_count(engine: *const lsquic_engine): c_uint;
    fn lsquic_engine_cid_in_use(engine: *lsquic_engine, cid: [*]const u8, cid_len: usize): c_int;
    fn lsquic_engine_packet_in(
        engine: *lsquic_engine,
        data: [*]const u8,
        size: usize,
        sa_local: *const posix.sockaddr,
        sa_peer: *const posix.sockaddr,
        peer_ctx: *!?,
        ecn: c_int,
    ): c_int;
    fn lsquic_engine_process_conns(engine: *lsquic_engine);
    fn lsquic_engine_set_idle_timeout_ms(engine: *lsquic_engine, ms: c_uint);
    fn lsquic_engine_earliest_adv_tick(engine: *lsquic_engine, diff: *c_int!): c_int;
    fn lsquic_engine_has_unsent_packets(engine: *lsquic_engine): c_int;
    fn lsquic_engine_send_unsent_packets(engine: *lsquic_engine);
    fn lsquic_engine_connect(
        engine: *lsquic_engine,
        version: c_int,
        local_sa: *const posix.sockaddr,
        peer_sa: *const posix.sockaddr,
        peer_ctx: *!?,
        conn_ctx: *!?,
        hostname: [*:0]const u8?,
        base_plpmtu: u16,
        sess_resume: [*]const u8?,
        sess_resume_len: usize,
        token: [*]const u8?,
        token_sz: usize,
    ): *lsquic_conn?;

    fn lsquic_conn_get_ctx(c: *const lsquic_conn): *!?;
    fn lsquic_conn_get_peer_ctx(c: *const lsquic_conn, local_sa: *const !?): *!?;
    fn lsquic_conn_set_ctx(c: *lsquic_conn, ctx: *!?);
    fn lsquic_conn_close(c: *lsquic_conn);
    fn lsquic_conn_going_away(c: *lsquic_conn);
    fn lsquic_conn_abort(c: *lsquic_conn);
    fn lsquic_conn_abort_silent(c: *lsquic_conn);
    fn lsquic_conn_abort_error(c: *lsquic_conn, is_app: c_int, code: c_uint, reason: [*:0]const u8);
    fn lsquic_conn_peer_h3_datagram(c: *const lsquic_conn): c_int;
    fn lsquic_conn_status(c: *lsquic_conn, errbuf: [*]u8, bufsz: usize): c_int;
    fn lsquic_conn_make_stream(c: *lsquic_conn);
    fn lsquic_conn_make_uni_stream(c: *lsquic_conn);
    fn lsquic_conn_n_avail_streams(c: *const lsquic_conn): c_uint;
    fn lsquic_conn_n_pending_streams(c: *const lsquic_conn): c_uint;
    fn lsquic_conn_get_sockaddr(
        c: *lsquic_conn,
        local: **const posix.sockaddr!,
        peer: **const posix.sockaddr!,
    ): c_int;
    fn lsquic_conn_get_sni(c: *lsquic_conn): [*:0]const u8?;
    fn lsquic_conn_want_datagram_write(c: *lsquic_conn, is_want: c_int): c_int;
    fn lsquic_conn_crypto_cipher(c: *const lsquic_conn): [*:0]const u8?;
    fn lsquic_conn_get_server_cert_chain(c: *lsquic_conn): *!?;
    fn lsquic_conn_get_ssl(c: *const lsquic_conn): *!?;
    fn lsquic_ssl_to_conn(ssl: *const !): *lsquic_conn?;
    fn lsquic_conn_get_info(c: *lsquic_conn, info: *ConnInfo!): c_int;
    fn lsquic_conn_pings_received(c: *const lsquic_conn): u64;
    fn lsquic_conn_ack_now(c: *lsquic_conn);
    fn lsquic_conn_datagram_early(c: *const lsquic_conn): c_int;
    fn lsquic_engine_sreset_stats(e: *const lsquic_engine, sent: *u64!, limited: *u64!);

    fn lsquic_stream_id(s: *const lsquic_stream): u64;
    fn lsquic_stream_conn(s: *const lsquic_stream): *lsquic_conn;
    fn lsquic_stream_get_ctx(s: *const lsquic_stream): *!?;
    fn lsquic_stream_set_ctx(s: *lsquic_stream, ctx: *!?);
    fn lsquic_stream_read(s: *lsquic_stream, buf: *!, len: usize): isize;
    fn lsquic_stream_write(s: *lsquic_stream, buf: *const !, len: usize): isize;
    fn lsquic_stream_flush(s: *lsquic_stream): c_int;
    fn lsquic_stream_shutdown(s: *lsquic_stream, how: c_int): c_int;
    fn lsquic_stream_shutdown_internal(s: *lsquic_stream);
    fn lsquic_stream_close(s: *lsquic_stream): c_int;
    fn lsquic_stream_wantread(s: *lsquic_stream, is_want: c_int): c_int;
    fn lsquic_stream_wantwrite(s: *lsquic_stream, is_want: c_int): c_int;
    fn lsquic_stream_is_pushed(s: *const lsquic_stream): c_int;
    fn lsquic_stream_get_hset(s: *lsquic_stream): *!?;
    fn lsquic_stream_get_error_code(s: *const lsquic_stream): u64;
    fn lsquic_stream_set_http_prio(s: *lsquic_stream, p: *const ExtHttpPrio): c_int;
    fn lsquic_stream_get_http_prio(s: *lsquic_stream, p: *ExtHttpPrio!): c_int;
    fn lsquic_stream_has_unacked_data(s: *lsquic_stream): c_int;
    fn lsquic_stream_reset_received(s: *const lsquic_stream): c_int;
    fn lsquic_stream_is_rejected(s: *const lsquic_stream): c_int;
    fn lsquic_stream_received_early_data(s: *lsquic_stream): c_int;
    fn lsquic_stream_send_stop_sending(s: *lsquic_stream, code: u64);

    fn us_nq_conn_transport_params(c: *const lsquic_conn, peer: c_int, out: *NqTransportParams!): c_int;
    fn us_nq_tp_size(): usize;
    fn us_nq_enable_logging(level: [*:0]const u8);
    fn us_nq_vtable_size(): usize;
    fn us_nq_settings_size(): usize;
    fn us_nq_settings_init(s: *lsquic_engine_settings, is_server: c_int, is_http: c_int);
    fn us_nq_engine_new(
        is_server: c_int,
        is_http: c_int,
        vtable: *NqVtable,
        settings: *const lsquic_engine_settings,
        alpn: [*:0]const u8?,
    ): *lsquic_engine?;
    fn us_nq_spec_dest(s: *const lsquic_out_spec): *const posix.sockaddr;
    fn us_nq_spec_local(s: *const lsquic_out_spec): *const posix.sockaddr;
    fn us_nq_spec_peer_ctx(s: *const lsquic_out_spec): *!?;
    fn us_nq_spec_iov(s: *const lsquic_out_spec, n: *usize!): *const iovec;
    fn us_nq_spec_stride(): usize;
    fn us_nq_stream_reset(s: *lsquic_stream, code: u64);
    fn us_nq_hset_pairs(hset: *!, len: *usize!): [*]const u8?;
    fn us_nq_hset_free(hset: *!);
    fn us_nq_stream_send_headers(
        s: *lsquic_stream,
        buf: [*]const u8,
        len: usize,
        expected: c_int,
        eos: c_int,
    ): c_int;

    fn us_nq_loop_register(loop: *uws.Loop, d: *NqDriver, owner: *!?);
    fn us_nq_loop_unregister(loop: *uws.Loop, d: *NqDriver);

    fn us_nq_settings_set_idle_timeout(s: *lsquic_engine_settings, v: c_uint);
    fn us_nq_settings_set_idle_timeout_ms(s: *lsquic_engine_settings, v: c_uint);
    fn us_nq_settings_set_delayed_acks(s: *lsquic_engine_settings, v: c_int);
    fn us_nq_settings_set_handshake_to(s: *lsquic_engine_settings, v: c_ulong);
    fn us_nq_settings_set_ping_period(s: *lsquic_engine_settings, v: c_uint);
    fn us_nq_settings_set_ping_period_us(s: *lsquic_engine_settings, v: u64);
    fn us_nq_settings_set_init_max_data(s: *lsquic_engine_settings, v: c_uint);
    fn us_nq_settings_set_init_max_stream_data_bidi_local(s: *lsquic_engine_settings, v: c_uint);
    fn us_nq_settings_set_init_max_stream_data_bidi_remote(s: *lsquic_engine_settings, v: c_uint);
    fn us_nq_settings_set_init_max_stream_data_uni(s: *lsquic_engine_settings, v: c_uint);
    fn us_nq_settings_set_init_max_streams_bidi(s: *lsquic_engine_settings, v: c_uint);
    fn us_nq_settings_set_init_max_streams_uni(s: *lsquic_engine_settings, v: c_uint);
    fn us_nq_settings_set_max_udp_payload_size_rx(s: *lsquic_engine_settings, v: u16);
    fn us_nq_settings_set_datagrams(s: *lsquic_engine_settings, v: c_int);
    fn us_nq_settings_set_h3_datagram(s: *lsquic_engine_settings, v: c_int);
    fn us_nq_settings_set_send_prst(s: *lsquic_engine_settings, v: c_int);
    fn us_nq_settings_set_honor_prst(s: *lsquic_engine_settings, v: c_int);
    fn us_nq_settings_set_sreset_burst(s: *lsquic_engine_settings, v: c_uint);
    fn us_nq_settings_set_sreset_rate(s: *lsquic_engine_settings, v: f64);
    fn us_nq_settings_set_h3_connect_protocol(s: *lsquic_engine_settings, v: c_int);
    fn us_nq_settings_set_preferred_address(s: *lsquic_engine_settings, addr: [*]const u8);
    fn us_nq_settings_set_max_datagram_frame_size(s: *lsquic_engine_settings, v: u16);
    fn us_nq_settings_set_max_h3_header_pairs(s: *lsquic_engine_settings, v: u16);
    fn us_nq_settings_set_max_h3_header_bytes(s: *lsquic_engine_settings, v: c_uint);
    fn us_nq_settings_set_allow_migration(s: *lsquic_engine_settings, v: c_int);
    fn us_nq_settings_set_origin_blob(s: *lsquic_engine_settings, blob: [*]const u8, len: usize);
    fn us_nq_settings_set_scid_len(s: *lsquic_engine_settings, v: c_uint);
    fn us_nq_settings_set_silent_close(s: *lsquic_engine_settings, v: c_int);
    fn us_nq_settings_set_cc_algo(s: *lsquic_engine_settings, v: c_uint);
    fn us_nq_settings_set_delay_onclose(s: *lsquic_engine_settings, v: c_int);

    fn us_nq_settings_get_init_max_data(s: *const lsquic_engine_settings): c_uint;
    fn us_nq_settings_get_init_max_stream_data_bidi_local(s: *const lsquic_engine_settings): c_uint;
    fn us_nq_settings_get_init_max_stream_data_bidi_remote(s: *const lsquic_engine_settings): c_uint;
    fn us_nq_settings_get_init_max_stream_data_uni(s: *const lsquic_engine_settings): c_uint;
    fn us_nq_settings_get_init_max_streams_bidi(s: *const lsquic_engine_settings): c_uint;
    fn us_nq_settings_get_init_max_streams_uni(s: *const lsquic_engine_settings): c_uint;
    fn us_nq_settings_get_idle_timeout(s: *const lsquic_engine_settings): c_uint;
    fn us_nq_settings_get_idle_timeout_ms(s: *const lsquic_engine_settings): c_uint;
    fn us_nq_settings_get_max_udp_payload_size_rx(s: *const lsquic_engine_settings): u16;
    fn us_nq_settings_get_allow_migration(s: *const lsquic_engine_settings): c_int;
    fn us_nq_settings_get_datagrams(s: *const lsquic_engine_settings): c_int;
    fn us_nq_settings_get_max_datagram_frame_size(s: *const lsquic_engine_settings): u16;
}

pub fn ctxRef(comptime T: type, ctx: *!?): (*T)? {
    if (ctx == null) return null;
    return unsafe { ctx.? as *T };
}

pub fn globalInit() {
    * = lsquic_global_init(LSQUIC_GLOBAL_CLIENT | LSQUIC_GLOBAL_SERVER);
}

pub fn enableLogging(level: [*:0]const u8) {
    us_nq_enable_logging(level);
}

pub struct Settings {
    bytes: []u8,

    pub static fn init(is_server: bool, is_http: bool): Settings {
        const size = us_nq_settings_size();
        const buf = bun.default_allocator.alloc(u8, size) catch bun.outOfMemory();
        @memset(buf, 0);
        us_nq_settings_init(
            unsafe { buf.ptr as *lsquic_engine_settings },
            if (is_server) 1 else 0,
            if (is_http) 1 else 0,
        );
        return Settings{ bytes: buf };
    }

    ~init() {
        if (_.bytes.len > 0) {
            bun.default_allocator.free(_.bytes);
            _.bytes = "";
        }
    }

    pub fn raw(*_): *lsquic_engine_settings {
        return unsafe { _.bytes.ptr as *lsquic_engine_settings };
    }

    pub fn asPtr(): *const lsquic_engine_settings {
        return unsafe { _.bytes.ptr as *const lsquic_engine_settings };
    }

    pub fn setPreferredAddress(*_, addr: *const [24]u8) {
        us_nq_settings_set_preferred_address(_.raw(), unsafe { addr as [*]const u8 });
    }

    pub fn setOriginBlob(*_, blob: []const u8) {
        us_nq_settings_set_origin_blob(_.raw(), blob.ptr, blob.len);
    }
}
