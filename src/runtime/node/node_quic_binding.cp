import bun from "bun";
import * as quic from "./quic/quic.cp";

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const CallFrame = jsc.CallFrame;

const QUIC_CALLBACK_NAMES: [22][]const u8 = [
    "EndpointClose",
    "SessionClose",
    "SessionEarlyDataRejected",
    "SessionGoaway",
    "SessionDatagram",
    "SessionDatagramStatus",
    "SessionHandshake",
    "SessionKeyLog",
    "SessionQlog",
    "SessionNew",
    "SessionNewToken",
    "SessionOrigin",
    "SessionPathValidation",
    "SessionTicket",
    "SessionVersionNegotiation",
    "StreamBlocked",
    "StreamClose",
    "StreamCreated",
    "StreamDrain",
    "StreamHeaders",
    "StreamReset",
    "StreamTrailers",
];

fn setCallbacks(global: *JSGlobalObject, callframe: *CallFrame): bun.JSError!JSValue {
    if (global.bunVM().rareData().node_quic_callbacks.get() != null) {
        return JSValue.js_undefined;
    }
    const callbacks_obj = callframe.argument(0);
    if (!callbacks_obj.isObject()) {
        return global.ERR(.INVALID_ARG_TYPE, "The \"callbacks\" argument must be of type object", .{}).throw();
    }
    for (key of QUIC_CALLBACK_NAMES) {
        let name_buf: [64]u8 = undefined;
        const full_name = std.fmt.bufPrint(&name_buf, "on{s}", .{key}) catch unreachable;
        const val = callbacks_obj.get(global, full_name)?;
        if (v := val) {
            if (v.isCallable()) continue;
        }
        return global.ERR(.MISSING_ARGS, "Missing Callback: on{s}", .{key}).throw();
    }
    quic.callbacksSet(global, callbacks_obj);
    return JSValue.js_undefined;
}

fn putNum(obj: JSValue, global: *JSGlobalObject, key: []const u8, value: u64) {
    obj.put(global, key, JSValue.jsNumber(value));
}

fn putStr(obj: JSValue, global: *JSGlobalObject, key: []const u8, value: []const u8)! {
    const s = bun.String.cloneUTF8(value).toJS(global)?;
    obj.put(global, key, s);
}

pub fn createNodeQuicBinding(global: *JSGlobalObject): bun.JSError!JSValue {
    let obj = JSValue.createEmptyObject(global, 16);

    obj.put(global, "Endpoint", quic.QuicEndpoint.js.getConstructor(global));
    obj.put(global, "setCallbacks", jsc.JSFunction.create(global, "setCallbacks", setCallbacks, 1, .{}));

    let i: usize = 0;
    for (name of quic.endpoint.ENDPOINT_STATS_FIELDS) {
        let kbuf: [64]u8 = undefined;
        const key = std.fmt.bufPrint(&kbuf, "IDX_STATS_ENDPOINT_{s}", .{name}) catch unreachable;
        putNum(obj, global, key, i);
        i += 1;
    }
    putNum(obj, global, "IDX_STATS_ENDPOINT_COUNT", quic.endpoint.ENDPOINT_STATS_FIELDS.len);

    putNum(obj, global, "IDX_STATE_ENDPOINT_BOUND", @offsetOf(quic.endpoint.EndpointState, "bound"));
    putNum(obj, global, "IDX_STATE_ENDPOINT_BOUND_SIZE", @sizeOf(u8));
    putNum(obj, global, "IDX_STATE_ENDPOINT_RECEIVING", @offsetOf(quic.endpoint.EndpointState, "receiving"));
    putNum(obj, global, "IDX_STATE_ENDPOINT_RECEIVING_SIZE", @sizeOf(u8));
    putNum(obj, global, "IDX_STATE_ENDPOINT_LISTENING", @offsetOf(quic.endpoint.EndpointState, "listening"));
    putNum(obj, global, "IDX_STATE_ENDPOINT_LISTENING_SIZE", @sizeOf(u8));
    putNum(obj, global, "IDX_STATE_ENDPOINT_CLOSING", @offsetOf(quic.endpoint.EndpointState, "closing"));
    putNum(obj, global, "IDX_STATE_ENDPOINT_CLOSING_SIZE", @sizeOf(u8));
    putNum(obj, global, "IDX_STATE_ENDPOINT_BUSY", @offsetOf(quic.endpoint.EndpointState, "busy"));
    putNum(obj, global, "IDX_STATE_ENDPOINT_BUSY_SIZE", @sizeOf(u8));
    putNum(obj, global, "IDX_STATE_ENDPOINT_MAX_CONNECTIONS_PER_HOST", @offsetOf(quic.endpoint.EndpointState, "max_connections_per_host"));
    putNum(obj, global, "IDX_STATE_ENDPOINT_MAX_CONNECTIONS_PER_HOST_SIZE", @sizeOf(u16));
    putNum(obj, global, "IDX_STATE_ENDPOINT_MAX_CONNECTIONS_TOTAL", @offsetOf(quic.endpoint.EndpointState, "max_connections_total"));
    putNum(obj, global, "IDX_STATE_ENDPOINT_MAX_CONNECTIONS_TOTAL_SIZE", @sizeOf(u16));
    putNum(obj, global, "IDX_STATE_ENDPOINT_PENDING_CALLBACKS", @offsetOf(quic.endpoint.EndpointState, "pending_callbacks"));
    putNum(obj, global, "IDX_STATE_ENDPOINT_PENDING_CALLBACKS_SIZE", @sizeOf(u64));

    putNum(obj, global, "CLOSECONTEXT_CLOSE", quic.endpoint.CLOSECONTEXT_CLOSE);
    putNum(obj, global, "CLOSECONTEXT_BIND_FAILURE", quic.endpoint.CLOSECONTEXT_BIND_FAILURE);
    putNum(obj, global, "CLOSECONTEXT_LISTEN_FAILURE", quic.endpoint.CLOSECONTEXT_LISTEN_FAILURE);
    putNum(obj, global, "CLOSECONTEXT_RECEIVE_FAILURE", quic.endpoint.CLOSECONTEXT_RECEIVE_FAILURE);
    putNum(obj, global, "CLOSECONTEXT_SEND_FAILURE", quic.endpoint.CLOSECONTEXT_SEND_FAILURE);
    putNum(obj, global, "CLOSECONTEXT_START_FAILURE", quic.endpoint.CLOSECONTEXT_START_FAILURE);

    putNum(obj, global, "CC_ALGO_RENO", 0);
    putNum(obj, global, "CC_ALGO_CUBIC", 1);
    putNum(obj, global, "CC_ALGO_BBR", 2);
    putStr(obj, global, "CC_ALGO_RENO_STR", "reno")?;
    putStr(obj, global, "CC_ALGO_CUBIC_STR", "cubic")?;
    putStr(obj, global, "CC_ALGO_BBR_STR", "bbr")?;

    putNum(obj, global, "DEFAULT_MAX_STREAM_DATA", 256 * 1024);
    putNum(obj, global, "DEFAULT_MAX_DATA", 1024 * 1024);
    putNum(obj, global, "DEFAULT_MAX_IDLE_TIMEOUT", 10);
    putNum(obj, global, "DEFAULT_MAX_STREAMS_BIDI", 100);
    putNum(obj, global, "DEFAULT_MAX_STREAMS_UNI", 3);
    putNum(obj, global, "DEFAULT_ACTIVE_CONNECTION_ID_LIMIT", 2);
    putNum(obj, global, "PREFERRED_ADDRESS_IGNORE", 0);
    putNum(obj, global, "PREFERRED_ADDRESS_USE", 1);
    putNum(obj, global, "DEFAULT_PREFERRED_ADDRESS_POLICY", 0);
    putNum(obj, global, "STREAM_DIRECTION_BIDIRECTIONAL", 0);
    putNum(obj, global, "STREAM_DIRECTION_UNIDIRECTIONAL", 1);
    putNum(obj, global, "DEFAULT_MAX_HEADER_LIST_PAIRS", 128);
    putNum(obj, global, "DEFAULT_MAX_HEADER_LENGTH", 8192);
    putNum(obj, global, "QUIC_PROTO_MAX", 1);
    putNum(obj, global, "QUIC_PROTO_MIN", 1);
    putNum(obj, global, "DEFAULT_HANDSHAKE_TIMEOUT", 10_000);
    putStr(obj, global, "DEFAULT_CIPHERS", "TLS_AES_128_GCM_SHA256:TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256:TLS_AES_128_CCM_SHA256")?;
    putStr(obj, global, "DEFAULT_GROUPS", "X25519:P-256:P-384:P-521")?;

    let j: usize = 0;
    for (name of quic.session.SESSION_STATS_FIELDS) {
        let kbuf: [64]u8 = undefined;
        const key = std.fmt.bufPrint(&kbuf, "IDX_STATS_SESSION_{s}", .{name}) catch unreachable;
        putNum(obj, global, key, j);
        j += 1;
    }
    putNum(obj, global, "IDX_STATS_SESSION_COUNT", quic.session.SESSION_STATS_FIELDS.len);

    putNum(obj, global, "IDX_STATE_SESSION_LISTENER_FLAGS", @offsetOf(quic.session.SessionState, "listener_flags"));
    putNum(obj, global, "IDX_STATE_SESSION_CLOSING", @offsetOf(quic.session.SessionState, "closing"));
    putNum(obj, global, "IDX_STATE_SESSION_GRACEFUL_CLOSE", @offsetOf(quic.session.SessionState, "graceful_close"));
    putNum(obj, global, "IDX_STATE_SESSION_SILENT_CLOSE", @offsetOf(quic.session.SessionState, "silent_close"));
    putNum(obj, global, "IDX_STATE_SESSION_STATELESS_RESET", @offsetOf(quic.session.SessionState, "stateless_reset"));
    putNum(obj, global, "IDX_STATE_SESSION_HANDSHAKE_COMPLETED", @offsetOf(quic.session.SessionState, "handshake_completed"));
    putNum(obj, global, "IDX_STATE_SESSION_HANDSHAKE_CONFIRMED", @offsetOf(quic.session.SessionState, "handshake_confirmed"));
    putNum(obj, global, "IDX_STATE_SESSION_STREAM_OPEN_ALLOWED", @offsetOf(quic.session.SessionState, "stream_open_allowed"));
    putNum(obj, global, "IDX_STATE_SESSION_PRIORITY_SUPPORTED", @offsetOf(quic.session.SessionState, "priority_supported"));
    putNum(obj, global, "IDX_STATE_SESSION_HEADERS_SUPPORTED", @offsetOf(quic.session.SessionState, "headers_supported"));
    putNum(obj, global, "IDX_STATE_SESSION_WRAPPED", @offsetOf(quic.session.SessionState, "wrapped"));
    putNum(obj, global, "IDX_STATE_SESSION_APPLICATION_TYPE", @offsetOf(quic.session.SessionState, "application_type"));
    putNum(obj, global, "IDX_STATE_SESSION_NO_ERROR_CODE", @offsetOf(quic.session.SessionState, "no_error_code"));
    putNum(obj, global, "IDX_STATE_SESSION_INTERNAL_ERROR_CODE", @offsetOf(quic.session.SessionState, "internal_error_code"));
    putNum(obj, global, "IDX_STATE_SESSION_MAX_DATAGRAM_SIZE", @offsetOf(quic.session.SessionState, "max_datagram_size"));
    putNum(obj, global, "IDX_STATE_SESSION_LAST_DATAGRAM_ID", @offsetOf(quic.session.SessionState, "last_datagram_id"));
    putNum(obj, global, "IDX_STATE_SESSION_MAX_PENDING_DATAGRAMS", @offsetOf(quic.session.SessionState, "max_pending_datagrams"));

    let k: usize = 0;
    for (name of quic.stream.STREAM_STATS_FIELDS) {
        let kbuf: [64]u8 = undefined;
        const key = std.fmt.bufPrint(&kbuf, "IDX_STATS_STREAM_{s}", .{name}) catch unreachable;
        putNum(obj, global, key, k);
        k += 1;
    }
    putNum(obj, global, "IDX_STATS_STREAM_COUNT", quic.stream.STREAM_STATS_FIELDS.len);

    putNum(obj, global, "IDX_STATE_STREAM_ID", @offsetOf(quic.stream.StreamState, "id"));
    putNum(obj, global, "IDX_STATE_STREAM_PENDING", @offsetOf(quic.stream.StreamState, "pending"));
    putNum(obj, global, "IDX_STATE_STREAM_FIN_SENT", @offsetOf(quic.stream.StreamState, "fin_sent"));
    putNum(obj, global, "IDX_STATE_STREAM_FIN_RECEIVED", @offsetOf(quic.stream.StreamState, "fin_received"));
    putNum(obj, global, "IDX_STATE_STREAM_READ_ENDED", @offsetOf(quic.stream.StreamState, "read_ended"));
    putNum(obj, global, "IDX_STATE_STREAM_WRITE_ENDED", @offsetOf(quic.stream.StreamState, "write_ended"));
    putNum(obj, global, "IDX_STATE_STREAM_RESET", @offsetOf(quic.stream.StreamState, "reset"));
    putNum(obj, global, "IDX_STATE_STREAM_RESET_CODE", @offsetOf(quic.stream.StreamState, "reset_code"));
    putNum(obj, global, "IDX_STATE_STREAM_HAS_OUTBOUND", @offsetOf(quic.stream.StreamState, "has_outbound"));
    putNum(obj, global, "IDX_STATE_STREAM_HAS_READER", @offsetOf(quic.stream.StreamState, "has_reader"));
    putNum(obj, global, "IDX_STATE_STREAM_WANTS_BLOCK", @offsetOf(quic.stream.StreamState, "wants_block"));
    putNum(obj, global, "IDX_STATE_STREAM_WANTS_HEADERS", @offsetOf(quic.stream.StreamState, "wants_headers"));
    putNum(obj, global, "IDX_STATE_STREAM_WANTS_RESET", @offsetOf(quic.stream.StreamState, "wants_reset"));
    putNum(obj, global, "IDX_STATE_STREAM_WANTS_TRAILERS", @offsetOf(quic.stream.StreamState, "wants_trailers"));
    putNum(obj, global, "IDX_STATE_STREAM_RECEIVED_EARLY_DATA", @offsetOf(quic.stream.StreamState, "received_early_data"));
    putNum(obj, global, "IDX_STATE_STREAM_WRITE_DESIRED_SIZE", @offsetOf(quic.stream.StreamState, "write_desired_size"));
    putNum(obj, global, "IDX_STATE_STREAM_HIGH_WATER_MARK", @offsetOf(quic.stream.StreamState, "high_water_mark"));

    putNum(obj, global, "QUIC_STREAM_HEADERS_KIND_HINTS", 0);
    putNum(obj, global, "QUIC_STREAM_HEADERS_KIND_INITIAL", 1);
    putNum(obj, global, "QUIC_STREAM_HEADERS_KIND_TRAILING", 2);
    putNum(obj, global, "QUIC_STREAM_HEADERS_FLAGS_NONE", 0);
    putNum(obj, global, "QUIC_STREAM_HEADERS_FLAGS_TERMINAL", 1);

    return obj;
}
