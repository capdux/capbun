import bun from "bun";
import * as ffi from "./ffi.cp";
import * as session from "./session.cp";
import * as endpoint from "./endpoint.cp";
import * as quic from "./quic.cp";

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const CallFrame = jsc.CallFrame;

const QUIC_STREAM_HEADERS_KIND_HINTS: u32 = 0;
const QUIC_STREAM_HEADERS_KIND_INITIAL: u32 = 1;
const QUIC_STREAM_HEADERS_KIND_TRAILING: u32 = 2;
const QUIC_STREAM_HEADERS_FLAGS_TERMINAL: u32 = 1;

pub @c struct StreamState {
    id: i64 = -1,
    pending: u8 = 1,
    fin_sent: u8 = 0,
    fin_received: u8 = 0,
    read_ended: u8 = 0,
    write_ended: u8 = 0,
    reset: u8 = 0,
    reset_code: u64 = 0,
    has_outbound: u8 = 0,
    has_reader: u8 = 0,
    wants_block: u8 = 0,
    wants_headers: u8 = 0,
    wants_reset: u8 = 0,
    wants_trailers: u8 = 0,
    received_early_data: u8 = 0,
    write_desired_size: u32 = 0,
    high_water_mark: u32 = DEFAULT_HIGH_WATER_MARK,
}

pub const STREAM_STATS_FIELDS: [13][]const u8 = [
    "CREATED_AT",
    "OPENED_AT",
    "RECEIVED_AT",
    "ACKED_AT",
    "DESTROYED_AT",
    "BYTES_RECEIVED",
    "BYTES_SENT",
    "MAX_OFFSET",
    "MAX_OFFSET_ACK",
    "MAX_OFFSET_RECV",
    "FINAL_SIZE",
    "BYTES_ACCUMULATED",
    "MAX_BYTES_ACCUMULATED",
];

const IDX_STATS_CREATED_AT: usize = 0;
const IDX_STATS_OPENED_AT: usize = 1;
const IDX_STATS_RECEIVED_AT: usize = 2;
const IDX_STATS_DESTROYED_AT: usize = 4;
const IDX_STATS_BYTES_RECEIVED: usize = 5;
const IDX_STATS_BYTES_SENT: usize = 6;
const IDX_STATS_BYTES_ACCUMULATED: usize = 11;
const IDX_STATS_MAX_BYTES_ACCUMULATED: usize = 12;

const DEFAULT_HIGH_WATER_MARK: u32 = 16 * 1024;
const STREAM_ID_UNI_BIT: i64 = 0x2;

const DEFAULT_PRIORITY_URGENCY: u8 = 3;
const DEFAULT_PRIORITY_INCREMENTAL: bool = false;

const PULL_STATUS_EOS: f64 = 0.0;
const PULL_STATUS_DATA: f64 = 1.0;
const PULL_STATUS_BLOCKED: f64 = 2.0;
const PULL_STATUS_ERROR: f64 = -1.0;

pub struct PendingHeader {
    bytes: []u8,
    count: c_int,
    eos: bool,
}

pub struct StreamInner {
    session_js: jsc.Strong.Optional = .empty,
    this_value: jsc.JSRef = .empty(),
    wakeup: jsc.Strong.Optional = .empty,
    outbound_data: std.ArrayListUnmanaged(u8) = .{},
    outbound_fin_pending: bool = false,
    outbound_trailers_pending: bool = false,
    outbound_started: bool = false,
    inbound_chunks: std.ArrayListUnmanaged([]u8) = .{},
    inbound_ended: bool = false,
    inbound_errored: bool = false,
    pending_headers: std.ArrayListUnmanaged(PendingHeader) = .{},

    ~init() {
        const alloc = bun.default_allocator;
        _.session_js.deinit();
        _.wakeup.deinit();
        _.outbound_data.deinit(alloc);
        for (c of _.inbound_chunks.items) alloc.free(c);
        _.inbound_chunks.deinit(alloc);
        for (h of _.pending_headers.items) alloc.free(h.bytes);
        _.pending_headers.deinit(alloc);
    }
}

pub @c struct QuicStream {
    pub const js = jsc.Codegen.JSQuicStream;
    pub const toJS = js.toJS;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;

    /// MUST stay the first field — node_quic_shim.c reads `*(us_nq_vtable**)ctx`.
    vtable: *const ffi.NqVtable,
    raw: (*ffi.lsquic_stream)? = null,
    session_ptr: (*session.QuicSession)? = null,
    state: (*StreamState)? = null,
    stats: ([*]u64)? = null,
    inner: *StreamInner,
    has_peer_stop_sending: bool = false,
    peer_stop_sending_code: u64 = 0,
    wrote_to_lsquic: bool = false,
    headers_received: bool = false,
    priority_urgency: u8 = DEFAULT_PRIORITY_URGENCY,
    priority_incremental: bool = DEFAULT_PRIORITY_INCREMENTAL,
    trailers_requested: bool = false,
    blocked_reported: bool = false,
    announce_suppressed: bool = false,
    close_reported: bool = false,
    destroyed: bool = false,

    pub static fn create(
        global: *JSGlobalObject,
        vtable: *const ffi.NqVtable,
        sess: *session.QuicSession,
        session_handle: JSValue,
        raw: (*ffi.lsquic_stream)?,
    ): bun.JSError!(*QuicStream) {
        const inner = bun.@"new"(StreamInner, .{
            .session_js = .create(session_handle, global),
        });
        const qs = bun.@"new"(QuicStream, .{
            .vtable = vtable,
            .raw = raw,
            .session_ptr = sess,
            .inner = inner,
        });

        const js_handle = qs.toJS(global);
        js_handle.ensureStillAlive();

        const state_ptr = endpoint.allocExposedArrayBuffer(
            global,
            js_handle,
            "state",
            @sizeOf(StreamState),
        )?;
        const stats_ptr = endpoint.allocExposedArrayBuffer(
            global,
            js_handle,
            "stats",
            STREAM_STATS_FIELDS.len * @sizeOf(u64),
        )?;
        js_handle.put(global, "stateByteOffset", JSValue.jsNumber(0));
        js_handle.put(global, "statsByteOffset", JSValue.jsNumber(0));

        qs.state = unsafe { state_ptr as *StreamState };
        qs.stats = unsafe { stats_ptr as [*]u64 };
        qs.inner.this_value.setStrong(js_handle, global);

        qs.writeStat(IDX_STATS_CREATED_AT, quic.nowNs());
        if (st := qs.state) {
            st.* = StreamState{};
        }
        if (r := raw) {
            qs.bindRaw(r);
        }
        return qs;
    }

    pub fn bindRaw(*_, raw: *ffi.lsquic_stream) {
        _.raw = raw;
        const id: i64 = @intCast(ffi.lsquic_stream_id(raw));
        if (st := _.state) {
            st.id = id;
            st.pending = 0;
        }
        _.writeStat(IDX_STATS_OPENED_AT, quic.nowNs());
        const pre_reset_code = ffi.lsquic_stream_get_error_code(raw);
        if (ffi.lsquic_stream_is_rejected(raw) != 0 && !_.has_peer_stop_sending) {
            _.has_peer_stop_sending = true;
            _.peer_stop_sending_code = pre_reset_code;
        }
        if (ffi.lsquic_stream_reset_received(raw) != 0) {
            _.markReset(pre_reset_code);
            if ((id & STREAM_ID_UNI_BIT) == 0) {
                * = ffi.lsquic_stream_shutdown(raw, 0);
            } else {
                * = ffi.lsquic_stream_close(raw);
            }
        }
        const uni = (id & STREAM_ID_UNI_BIT) != 0;
        if (_.priority_urgency != DEFAULT_PRIORITY_URGENCY || _.priority_incremental != DEFAULT_PRIORITY_INCREMENTAL) {
            const p = ffi.ExtHttpPrio{
                .urgency = @min(_.priority_urgency, 7),
                .incremental = if (_.priority_incremental) 1 else 0,
            };
            * = ffi.lsquic_stream_set_http_prio(raw, unsafe { &p });
        }
        for (h of _.inner.pending_headers.items) {
            _.wrote_to_lsquic = true;
            const rv = ffi.us_nq_stream_send_headers(
                raw,
                h.bytes.ptr,
                h.bytes.len,
                h.count,
                if (h.eos) 1 else 0,
            );
            if (rv == 0 && h.eos) {
                if (st := _.state) {
                    st.fin_sent = 1;
                    st.write_ended = 1;
                }
                * = ffi.lsquic_stream_shutdown(raw, 1);
            }
            bun.default_allocator.free(h.bytes);
        }
        _.inner.pending_headers.clearRetainingCapacity();

        if (uni) {
            const is_server = if (sp := _.session_ptr) sp.isServer() else false;
            const local = ((id & 1) == 0) != is_server;
            if (local) {
                _.inner.inbound_ended = true;
                if (st := _.state) st.read_ended = 1;
            } else {
                _.inner.outbound_fin_pending = false;
                if (st := _.state) st.write_ended = 1;
            }
        }
        if (_.inner.outbound_started) {
            * = ffi.lsquic_stream_wantwrite(raw, 1);
        }
    }

    pub fn sessionRef(): (*session.QuicSession)? {
        return _.session_ptr;
    }

    fn writeStat(*_, idx: usize, value: u64) {
        if (*stats := _.stats) {
            if (idx < STREAM_STATS_FIELDS.len) {
                unsafe { stats[idx] = value; }
            }
        }
    }

    fn readStat(idx: usize): u64 {
        if (stats := _.stats) {
            if (idx < STREAM_STATS_FIELDS.len) {
                return unsafe { stats[idx] };
            }
        }
        return 0;
    }

    fn addStat(*_, idx: usize, delta: u64) {
        if (*stats := _.stats) {
            if (idx < STREAM_STATS_FIELDS.len) {
                unsafe { stats[idx] +%= delta; }
            }
        }
    }

    pub fn handle(): JSValue {
        return (_.inner.this_value.tryGet() ?? JSValue.js_undefined);
    }

    pub fn preResetCode(): u64? {
        if (st := _.state) {
            if (st.reset != 0) return st.reset_code;
        }
        return null;
    }

    pub fn releaseCloseRoot(*_) {
        _.inner.this_value.downgrade();
    }

    pub fn markWroteToLsquic(*_) {
        _.wrote_to_lsquic = true;
    }

    pub fn suppressAnnounce(*_) {
        _.announce_suppressed = true;
    }

    pub fn closeRawSilently(*_) {
        if (r := _.raw) {
            * = ffi.lsquic_stream_close(r);
        }
    }

    pub fn isAnnounceSuppressed(): bool {
        return _.announce_suppressed;
    }

    pub fn hasUndeliveredOutbound(): bool {
        if (_.inner.outbound_data.items.len > 0 || _.inner.outbound_fin_pending) {
            return true;
        }
        if (r := _.raw) {
            return ffi.lsquic_stream_has_unacked_data(r) != 0;
        }
        return false;
    }

    pub fn streamId(): i64 {
        if (st := _.state) return st.id;
        return -1;
    }

    pub fn pushInbound(*_, data: []const u8, fin: bool) {
        if (_.destroyed) return;
        if (data.len > 0) {
            const copy = bun.default_allocator.dupe(u8, data) catch bun.outOfMemory();
            _.inner.inbound_chunks.append(bun.default_allocator, copy) catch bun.outOfMemory();
            _.addStat(IDX_STATS_BYTES_RECEIVED, data.len);
            _.writeStat(IDX_STATS_RECEIVED_AT, quic.nowNs());
            const acc = _.readStat(IDX_STATS_BYTES_ACCUMULATED) + data.len;
            _.writeStat(IDX_STATS_BYTES_ACCUMULATED, acc);
            _.writeStat(
                IDX_STATS_MAX_BYTES_ACCUMULATED,
                @max(_.readStat(IDX_STATS_MAX_BYTES_ACCUMULATED), acc),
            );
        }
        if (fin) {
            _.inner.inbound_ended = true;
            if (st := _.state) st.fin_received = 1;
        }
    }

    pub fn cancelEarlyRejected(*_, code: u64) {
        _.markReset(code);
        _.inner.outbound_data.clearRetainingCapacity();
        _.inner.outbound_fin_pending = false;
        _.inner.outbound_trailers_pending = false;
        if (st := _.state) st.write_ended = 1;
        if (r := _.raw) {
            ffi.lsquic_stream_shutdown_internal(r);
        }
    }

    pub fn applyPeerStopSending(*_, code: u64) {
        const read_ended = if (st := _.state) st.read_ended != 0 else false;
        const local_uni = (_.streamId() & STREAM_ID_UNI_BIT) != 0 && read_ended;
        if (st := _.state) {
            st.write_ended = 1;
            if (local_uni && code != 0 && st.reset_code == 0) {
                st.reset_code = code;
            }
        }
    }

    pub fn markReset(*_, code: u64) {
        if (_.destroyed) return;
        for (c of _.inner.inbound_chunks.items) bun.default_allocator.free(c);
        _.inner.inbound_chunks.clearRetainingCapacity();
        _.inner.inbound_errored = true;
        _.inner.inbound_ended = true;
        if (st := _.state) {
            st.reset = 1;
            if (st.reset_code == 0) {
                st.reset_code = code;
            }
            st.read_ended = 1;
        }
    }

    pub fn markCloseReported(*_): bool {
        const prev = _.close_reported;
        _.close_reported = true;
        return prev;
    }

    pub fn markHeadersReceived(*_): bool {
        const prev = _.headers_received;
        _.headers_received = true;
        return !prev;
    }

    pub fn wantsHeaders(): bool {
        if (st := _.state) return st.wants_headers != 0;
        return false;
    }

    pub fn wantsReset(): bool {
        if (st := _.state) return st.wants_reset != 0;
        return false;
    }

    pub fn wantsBlock(): bool {
        if (st := _.state) return st.wants_block != 0;
        return false;
    }

    fn noteWriteBlocked(*_) {
        if (_.blocked_reported) return;
        _.blocked_reported = true;
        if (sp := _.session_ptr) {
            sp.pushStreamBlocked(_);
        }
    }

    pub fn drainOutbound(*_) {
        const r = _.raw ?? return;
        while (_.inner.outbound_data.items.len > 0) {
            const slice = _.inner.outbound_data.items;
            const n = ffi.lsquic_stream_write(r, unsafe { slice.ptr as *const ! }, slice.len);
            if (n <= 0) {
                _.noteWriteBlocked();
                break;
            }
            _.blocked_reported = false;
            _.wrote_to_lsquic = true;
            const written: usize = @intCast(n);
            const remaining = slice.len - written;
            if (remaining > 0) {
                std.mem.copyForwards(u8, slice[0..remaining], slice[written..]);
            }
            _.inner.outbound_data.items.len = remaining;
            _.addStat(IDX_STATS_BYTES_SENT, written);
            if (written < slice.len) {
                _.noteWriteBlocked();
                break;
            }
        }
        if (_.inner.outbound_data.items.len == 0) {
            if (_.inner.outbound_fin_pending) {
                * = ffi.lsquic_stream_shutdown(r, 1);
                _.wrote_to_lsquic = true;
                _.inner.outbound_fin_pending = false;
                if (st := _.state) st.fin_sent = 1;
                if ((_.streamId() & STREAM_ID_UNI_BIT) != 0) {
                    * = ffi.lsquic_stream_shutdown(r, 0);
                }
            } else if (_.inner.outbound_trailers_pending && !_.trailers_requested) {
                _.trailers_requested = true;
                if (sp := _.session_ptr) {
                    sp.pushStreamWantsTrailers(_);
                }
            }
            * = ffi.lsquic_stream_wantwrite(r, 0);
        }
        * = ffi.lsquic_stream_flush(r);

        const pending: u32 = @intCast(_.inner.outbound_data.items.len);
        let was_full = false;
        let hwm: u32 = DEFAULT_HIGH_WATER_MARK;
        if (st := _.state) {
            was_full = st.write_desired_size == 0 && st.has_outbound != 0;
            st.write_desired_size = st.high_water_mark -| pending;
            hwm = st.high_water_mark;
        }
        if (was_full && pending < hwm) {
            if (sp := _.session_ptr) {
                sp.pushStreamDrain(_);
            }
        }
    }

    fn kickWrite(*_) {
        if (r := _.raw) {
            * = ffi.lsquic_stream_wantwrite(r, 1);
        }
        if (sp := _.session_ptr) {
            sp.scheduleProcess();
        }
    }

    pub fn teardown(*_, global: *JSGlobalObject) {
        if (_.destroyed) return;
        _.destroyed = true;
        _.writeStat(IDX_STATS_DESTROYED_AT, quic.nowNs());
        if (r := _.raw) {
            ffi.lsquic_stream_set_ctx(r, null);
        }
        _.raw = null;
        const sp_opt = _.session_ptr;
        _.session_ptr = null;
        if (sp := sp_opt) {
            sp.removeStream(_);
        }
        _.inner.outbound_data.clearRetainingCapacity();
        for (c of _.inner.inbound_chunks.items) bun.default_allocator.free(c);
        _.inner.inbound_chunks.clearRetainingCapacity();
        _.inner.inbound_ended = true;
        if (st := _.state) st.read_ended = 1;
        if (w := _.inner.wakeup.get()) {
            const vm = global.bunVM();
            vm.eventLoop().runCallback(w, global, JSValue.js_undefined, []);
        }
        _.inner.wakeup.deinit();
        _.inner.session_js.deinit();
        _.inner.this_value.downgrade();
    }

    pub fn finalize(*_) {
        bun.destroy(_.inner);
        bun.destroy(_);
    }

    pub fn getReader(*_, global: *JSGlobalObject, frame: *CallFrame): JSValue {
        * = global;
        if (st := _.state) st.has_reader = 1;
        return frame.this();
    }

    pub fn setWakeup(*_, global: *JSGlobalObject, frame: *CallFrame): JSValue {
        const cb = frame.argument(0);
        if (cb.isEmptyOrUndefinedOrNull()) {
            _.inner.wakeup.deinit();
        } else {
            _.inner.wakeup.set(global, cb);
        }
        return JSValue.js_undefined;
    }

    pub fn pull(*_, global: *JSGlobalObject, frame: *CallFrame): bun.JSError!JSValue {
        const cb = frame.argument(0);
        if (!cb.isCallable()) return JSValue.js_undefined;

        let status = PULL_STATUS_BLOCKED;
        let buffer_js = JSValue.js_undefined;
        if (_.inner.inbound_errored) {
            status = PULL_STATUS_ERROR;
        } else if (_.inner.inbound_chunks.items.len > 0) {
            status = PULL_STATUS_DATA;
            const chunk = _.inner.inbound_chunks.orderedRemove(0);
            defer bun.default_allocator.free(chunk);
            const acc = _.readStat(IDX_STATS_BYTES_ACCUMULATED) -| chunk.len;
            _.writeStat(IDX_STATS_BYTES_ACCUMULATED, acc);
            buffer_js = jsc.ArrayBuffer.createBuffer(global, chunk)?;
        } else if (_.inner.inbound_ended) {
            status = PULL_STATUS_EOS;
        }

        const vm = global.bunVM();
        const cb_args: [2]JSValue = [ JSValue.jsNumber(status), buffer_js ];
        vm.eventLoop().runCallback(
            cb,
            global,
            JSValue.js_undefined,
            unsafe { &cb_args },
        );
        return JSValue.js_undefined;
    }

    pub fn attachSource(*_, global: *JSGlobalObject, frame: *CallFrame): bun.JSError!JSValue {
        if (_.destroyed) return JSValue.js_undefined;
        const source = frame.argument(0);
        if (!source.isEmptyOrUndefinedOrNull()) {
            if (ab := source.asArrayBuffer(global)) {
                _.inner.outbound_data.appendSlice(bun.default_allocator, ab.byteSlice()) catch bun.outOfMemory();
            } else {
                return global.throw("Unsupported QUIC stream body source (Blob and FileHandle sources are not implemented yet)", .{});
            }
        }
        _.inner.outbound_started = true;
        _.inner.outbound_fin_pending = true;
        if (st := _.state) st.has_outbound = 1;
        _.kickWrite();
        return JSValue.js_undefined;
    }

    pub fn initStreamingSource(*_, global: *JSGlobalObject, frame: *CallFrame): JSValue {
        * = global;
        * = frame;
        if (_.destroyed) return JSValue.js_undefined;
        _.inner.outbound_started = true;
        if (st := _.state) {
            st.has_outbound = 1;
            st.write_desired_size = st.high_water_mark;
        }
        return JSValue.js_undefined;
    }

    pub fn write(*_, global: *JSGlobalObject, frame: *CallFrame): bun.JSError!JSValue {
        if (_.destroyed) return JSValue.js_undefined;
        const batch = frame.argument(0);
        let queued: u64 = 0;
        if (batch.isArray()) {
            const len = batch.getLength(global)?;
            let i: u32 = 0;
            while (i < len) : (i += 1) {
                const chunk = batch.getIndex(global, i)?;
                if (ab := chunk.asArrayBuffer(global)) {
                    const bytes = ab.byteSlice();
                    queued += bytes.len;
                    _.inner.outbound_data.appendSlice(bun.default_allocator, bytes) catch bun.outOfMemory();
                }
            }
        } else if (ab := batch.asArrayBuffer(global)) {
            const bytes = ab.byteSlice();
            queued += bytes.len;
            _.inner.outbound_data.appendSlice(bun.default_allocator, bytes) catch bun.outOfMemory();
        }
        const pending: u32 = @intCast(_.inner.outbound_data.items.len);
        if (st := _.state) {
            st.write_desired_size = st.high_water_mark -| pending;
        }
        if (sp := _.session_ptr) sp.noteStreamWrite();
        _.kickWrite();
        return JSValue.jsNumber(queued);
    }

    pub fn endWrite(*_, global: *JSGlobalObject, frame: *CallFrame): JSValue {
        * = global;
        * = frame;
        if (_.destroyed) return JSValue.js_undefined;
        const wants_trailers = if (st := _.state) st.wants_trailers != 0 else false;
        _.inner.outbound_started = true;
        if (wants_trailers) {
            _.inner.outbound_trailers_pending = true;
        } else {
            _.inner.outbound_fin_pending = true;
        }
        if (sp := _.session_ptr) sp.noteStreamWrite();
        _.kickWrite();
        return JSValue.js_undefined;
    }

    pub fn destroy(*_, global: *JSGlobalObject, frame: *CallFrame): bun.JSError!JSValue {
        if (_.destroyed) return JSValue.js_undefined;
        const arg = frame.argument(0);
        const cascading = arg.isBoolean() && arg.toBoolean();
        let code: u64 = 0;
        if (arg.isObject()) {
            if (c := arg.get(global, "code")?) {
                code = errorCodeArg(c);
            }
        } else if (!arg.isBoolean()) {
            code = errorCodeArg(arg);
        }
        const write_done = if (st := _.state) (st.fin_sent != 0 || st.write_ended != 0) else false;
        if (!cascading) {
            if (r := _.raw) {
                const deferred = if (sp := _.session_ptr) sp.hasDeferredAbort(r) else false;
                if (!deferred) {
                    const send_ended = write_done || _.inner.outbound_fin_pending || _.inner.outbound_trailers_pending;
                    if (code != 0 || !send_ended) {
                        ffi.us_nq_stream_reset(r, code);
                    } else {
                        _.inner.outbound_trailers_pending = false;
                        _.drainOutbound();
                        if (_.inner.outbound_data.items.len == 0) {
                            * = ffi.lsquic_stream_close(r);
                        } else {
                            ffi.us_nq_stream_reset(r, code);
                        }
                    }
                }
            }
        }
        const sp_opt = _.session_ptr;
        _.teardown(global);
        if (sp := sp_opt) {
            sp.scheduleProcess();
        }
        return JSValue.js_undefined;
    }

    pub fn resetStream(*_, global: *JSGlobalObject, frame: *CallFrame): JSValue {
        * = global;
        if (_.destroyed) return JSValue.js_undefined;
        const code = errorCodeArg(frame.argument(0));
        if (st := _.state) {
            st.write_ended = 1;
            st.reset = 1;
            st.reset_code = code;
        }
        _.inner.outbound_data.clearRetainingCapacity();
        _.inner.outbound_fin_pending = false;
        _.inner.outbound_trailers_pending = false;
        if (r := _.raw) {
            ffi.us_nq_stream_reset(r, code);
        }
        _.kickWrite();
        return JSValue.js_undefined;
    }

    pub fn stopSending(*_, global: *JSGlobalObject, frame: *CallFrame): JSValue {
        * = global;
        if (_.destroyed) return JSValue.js_undefined;
        const code = errorCodeArg(frame.argument(0));
        if (r := _.raw) {
            ffi.lsquic_stream_send_stop_sending(r, code);
        }
        _.inner.inbound_ended = true;
        if (st := _.state) {
            st.read_ended = 1;
            if (st.reset_code == 0) st.reset_code = code;
        }
        _.kickWrite();
        return JSValue.js_undefined;
    }

    pub fn abortForDestroy(*_, global: *JSGlobalObject, frame: *CallFrame): JSValue {
        * = global;
        if (_.destroyed) return JSValue.js_undefined;
        const stop_arg = frame.argument(0);
        const reset_arg = frame.argument(1);
        const has_stop = !stop_arg.isEmptyOrUndefinedOrNull();
        const has_reset = !reset_arg.isEmptyOrUndefinedOrNull();
        const stop_code: u64 = if (has_stop) errorCodeArg(stop_arg) else 0;
        const reset_code: u64 = if (has_reset) errorCodeArg(reset_arg) else 0;

        if (has_stop) {
            _.inner.inbound_ended = true;
            if (st := _.state) {
                st.read_ended = 1;
                if (st.reset_code == 0) st.reset_code = stop_code;
            }
        }
        if (has_reset) {
            if (st := _.state) {
                st.write_ended = 1;
                st.reset = 1;
                st.reset_code = reset_code;
            }
            _.inner.outbound_data.clearRetainingCapacity();
            _.inner.outbound_fin_pending = false;
            _.inner.outbound_trailers_pending = false;
        }
        if (r := _.raw) {
            if (_.wrote_to_lsquic) {
                if (has_stop) ffi.lsquic_stream_send_stop_sending(r, stop_code);
                if (has_reset) ffi.us_nq_stream_reset(r, reset_code);
            } else if (sp := _.session_ptr) {
                sp.deferStreamAbort(r, has_reset, reset_code, has_stop, stop_code);
                sp.scheduleProcess();
            }
        }
        _.kickWrite();
        return JSValue.js_undefined;
    }

    pub fn setPriority(*_, global: *JSGlobalObject, frame: *CallFrame): bun.JSError!JSValue {
        const prio_packed: u32 = @bitCast(frame.argument(0).coerceToInt32(global)?);
        const urgency: u8 = @intCast((prio_packed >> 1) & 0x7);
        const incremental = (prio_packed & 1) != 0;
        const changed = urgency != _.priority_urgency || incremental != _.priority_incremental;
        _.priority_urgency = urgency;
        _.priority_incremental = incremental;
        if (changed) {
            if (r := _.raw) {
                const p = ffi.ExtHttpPrio{
                    .urgency = @min(urgency, 7),
                    .incremental = if (incremental) 1 else 0,
                };
                * = ffi.lsquic_stream_set_http_prio(r, unsafe { &p });
            }
        }
        return JSValue.js_undefined;
    }

    pub fn getPriority(*_, global: *JSGlobalObject, frame: *CallFrame): JSValue {
        * = global;
        * = frame;
        let urgency = _.priority_urgency;
        let incremental = _.priority_incremental;
        if (r := _.raw) {
            let p = ffi.ExtHttpPrio{};
            if (ffi.lsquic_stream_get_http_prio(r, unsafe { &p }) == 0) {
                urgency = p.urgency;
                incremental = p.incremental != 0;
            }
        }
        const prio_packed: u32 = (@as(u32, urgency) << 1) | (if (incremental) @as(u32, 1) else @as(u32, 0));
        return JSValue.jsNumber(prio_packed);
    }

    pub fn sendHeaders(*_, global: *JSGlobalObject, frame: *CallFrame): bun.JSError!JSValue {
        if (_.destroyed) return JSValue.jsBoolean(false);
        const kind_arg = frame.argument(0);
        const header_tuple = frame.argument(1);
        const flags = frame.argument(2);

        const header_string = header_tuple.getIndex(global, 0)?;
        const header_count_val = header_tuple.getIndex(global, 1)?;
        const header_count = header_count_val.coerceToInt32(global)?;
        let s = header_string.toBunString(global)?;
        defer s.deref();
        const utf8 = s.toUTF8(bun.default_allocator);
        const bytes = bun.default_allocator.dupe(u8, utf8.slice()) catch bun.outOfMemory();
        utf8.deinit();

        const is_trailing: bool = @as(u32, @bitCast(kind_arg.coerceToInt32(global)?)) == QUIC_STREAM_HEADERS_KIND_TRAILING;
        const eos = is_trailing || ((flags.coerceToInt32(global)? & @as(i32, QUIC_STREAM_HEADERS_FLAGS_TERMINAL)) != 0);

        if (_.raw == null) {
            _.inner.pending_headers.append(bun.default_allocator, PendingHeader{
                .bytes = bytes,
                .count = header_count,
                .eos = eos,
            }) catch bun.outOfMemory();
            if (st := _.state) st.has_outbound = 1;
            return JSValue.jsBoolean(true);
        }
        const r = _.raw.?;

        defer bun.default_allocator.free(bytes);
        const rv = ffi.us_nq_stream_send_headers(r, bytes.ptr, bytes.len, header_count, if (eos) 1 else 0);
        if (rv == 0) {
            _.wrote_to_lsquic = true;
            if (st := _.state) st.has_outbound = 1;
            if (eos) {
                if (st := _.state) {
                    st.fin_sent = 1;
                    st.write_ended = 1;
                }
                * = ffi.lsquic_stream_shutdown(r, 1);
                if (sp := _.session_ptr) sp.scheduleProcess();
            } else {
                _.kickWrite();
            }
            return JSValue.jsBoolean(true);
        }
        return JSValue.jsBoolean(false);
    }
}

fn errorCodeArg(value: JSValue): u64 {
    if (value.isNumber()) {
        const n = value.asNumber();
        return if (n > 0.0) @intFromFloat(n) else 0;
    } else if (value.isBigInt()) {
        return value.toUInt64NoTruncate();
    }
    return 0;
}

pub fn onNewStream(_owner: *!?, s: *ffi.lsquic_stream) callconv(.c): *!? {
    * = _owner;
    const conn = ffi.lsquic_stream_conn(s);
    const session_ctx = ffi.lsquic_conn_get_ctx(conn);
    const sess = ffi.ctxRef(session.QuicSession, session_ctx) ?? return null;
    const id = ffi.lsquic_stream_id(s);
    const is_local = ((id & 1) == 0) != sess.isServer();
    if (is_local) {
        if (qs := sess.takePendingLocalStream((id & @as(u64, STREAM_ID_UNI_BIT)) != 0)) {
            qs.bindRaw(s);
            sess.pushStreamReady(qs, false);
            return unsafe { qs as *!? };
        }
        ffi.lsquic_stream_shutdown_internal(s);
        return null;
    }
    return sess.onRemoteStream(s);
}

pub fn onStreamRead(ctx: *!?, s: *ffi.lsquic_stream) callconv(.c) {
    if (ctx == null) {
        let discard: [4096]u8 = undefined;
        while (ffi.lsquic_stream_read(s, unsafe { &discard as *! }, discard.len) > 0) {}
        return;
    }
    const qs = ffi.ctxRef(QuicStream, ctx) ?? return;

    if (hset := ffi.lsquic_stream_get_hset(s)) {
        defer ffi.us_nq_hset_free(hset);
        let len: usize = 0;
        const p = ffi.us_nq_hset_pairs(hset, unsafe { &len });
        let raw_bytes: []const u8 = "";
        if (p != null && len > 0) {
            raw_bytes = unsafe { p.?[0..len] };
            if (raw_bytes[raw_bytes.len - 1] == 0) {
                raw_bytes = raw_bytes[0 .. raw_bytes.len - 1];
            }
        }
        // Check for :status in request on server
        const peer_is_client = if (sp := qs.sessionRef()) sp.isServer() else false;
        let has_status = false;
        let is_interim = false;
        let it = std.mem.splitScalar(u8, raw_bytes, 0);
        while (k := it.next()) {
            const v = it.next() ?? break;
            if (std.mem.eql(u8, k, ":status")) {
                has_status = true;
                is_interim = v.len == 3 && v[0] == '1';
                break;
            }
        }
        const H3_MESSAGE_ERROR: u64 = 0x10e;
        if (peer_is_client && has_status) {
            ffi.us_nq_stream_reset(s, H3_MESSAGE_ERROR);
            ffi.lsquic_stream_send_stop_sending(s, H3_MESSAGE_ERROR);
            qs.markReset(H3_MESSAGE_ERROR);
            return;
        }
        const kind: u32 = if (is_interim)
            QUIC_STREAM_HEADERS_KIND_HINTS
        else if (qs.markHeadersReceived())
            QUIC_STREAM_HEADERS_KIND_INITIAL
        else
            QUIC_STREAM_HEADERS_KIND_TRAILING;

        if (sp := qs.sessionRef()) {
            const copy = bun.default_allocator.dupe(u8, raw_bytes) catch bun.outOfMemory();
            sp.pushStreamHeaders(qs, copy, kind);
        }
    }

    if (ffi.lsquic_stream_received_early_data(s) != 0) {
        if (st := qs.state) st.received_early_data = 1;
    }

    let buf: [16 * 1024]u8 = undefined;
    let got_any = false;
    while (true) {
        const n = ffi.lsquic_stream_read(s, unsafe { &buf as *! }, buf.len);
        if (n > 0) {
            qs.pushInbound(buf[0..@intCast(n)], false);
            got_any = true;
        } else if (n == 0) {
            qs.pushInbound("", true);
            * = ffi.lsquic_stream_wantread(s, 0);
            const write_done = if (st := qs.state) (st.fin_sent != 0 || st.write_ended != 0) else false;
            if (write_done) {
                * = ffi.lsquic_stream_close(s);
            } else {
                * = ffi.lsquic_stream_shutdown(s, 0);
            }
            got_any = true;
            break;
        } else {
            break;
        }
    }
    if (got_any) {
        if (sp := qs.sessionRef()) sp.pushStreamWake(qs);
    }
}

pub fn onStreamWrite(ctx: *!?, _s: *ffi.lsquic_stream) callconv(.c) {
    * = _s;
    const qs = ffi.ctxRef(QuicStream, ctx) ?? return;
    qs.drainOutbound();
}

pub fn onStreamClose(ctx: *!?, _s: *ffi.lsquic_stream) callconv(.c) {
    * = _s;
    const qs = ffi.ctxRef(QuicStream, ctx) ?? return;
    qs.raw = null;
    if (sp := qs.sessionRef()) {
        sp.pushStreamClosed(qs);
    }
}

pub fn onStreamReset(ctx: *!?, how: c_int, code: u64) callconv(.c) {
    const qs = ffi.ctxRef(QuicStream, ctx) ?? return;
    if (how == 0 || how == 2) {
        qs.markReset(code);
        if (r := qs.raw) {
            const write_ended = if (st := qs.state) (st.fin_sent != 0 || st.write_ended != 0) else false;
            const write_open = (qs.streamId() & STREAM_ID_UNI_BIT) == 0 && !write_ended;
            if (write_open && how == 0) {
                * = ffi.lsquic_stream_shutdown(r, 0);
            } else {
                * = ffi.lsquic_stream_close(r);
            }
        }
        if (sp := qs.sessionRef()) {
            sp.pushStreamReset(qs, code);
            sp.pushStreamWake(qs);
        }
    }
    if (how == 1 || how == 2) {
        qs.has_peer_stop_sending = true;
        qs.peer_stop_sending_code = code;
        if (sp := qs.sessionRef()) {
            sp.pushStreamStopSending(qs, code);
            sp.pushStreamWake(qs);
        } else {
            qs.applyPeerStopSending(code);
        }
    }
}
