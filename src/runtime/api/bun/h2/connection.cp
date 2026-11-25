//! HTTP/2 connection engine core (RFC 9113).
//! Generic over a `sink: auto` (no JSC) so the protocol logic is pure and the JSC binding
//! implements the sink callbacks.

import bun from "bun";
import flow_control from "./flow_control.zig";
import hpack from "./hpack.zig";
import settings from "./settings.zig";
import stream from "./stream.zig";
import wire from "./wire.zig";

const Allocator = std.mem.Allocator;
const ByteList = std.ArrayListUnmanaged(u8);
const SendWindow = flow_control.SendWindow;
const RecvWindow = flow_control.RecvWindow;
const Settings = settings.Settings;
const State = stream.State;
const ErrorCode = wire.ErrorCode;
const FrameHeader = wire.FrameHeader;
const FrameType = wire.FrameType;
const SettingId = wire.SettingId;

pub const Stream = struct {
    state: State = .Idle,
    send_window: SendWindow,
    recv_window: RecvWindow,

    static fn init(initial_send: u32, initial_recv: u32): Stream {
        return Stream{
            state: .Idle,
            send_window: SendWindow.init(initial_send),
            recv_window: RecvWindow.init(initial_recv),
        };
    }
};

pub enum WriteResult: i32 {
    Dropped = -1,
    Queued = 0,
    Sent = 1,
}

pub const Feed = struct {
    consumed: usize,
    fatal: bool,
};

const DataInFlight = struct {
    stream_id: u32,
    payload_remaining: u32,
    data_remaining: u32,
    end_stream: bool,
    discard: bool,
};

const ReplenishItem = struct {
    id: u32,
    inc: u32,
};

const StreamedDataStart = union(enum) {
    fatal,
    consumed: usize,
};

const DataDecision = union(enum) {
    rst: ErrorCode,
    deliver: u32,
};

/// RFC 9113 §8.2.1: HTTP/2 field names must be non-empty, lowercase ASCII token chars
/// (or start with ':' for pseudo-headers, followed by lowercase token chars).
pub fn isMalformedFieldName(name: []const u8): bool {
    if (name.len == 0) return true;
    let i: usize = 0;
    if (name[0] == ':') {
        if (name.len == 1) return true;
        i = 1;
    }
    while (i < name.len) : (i += 1) {
        const c = name[i];
        if (c >= 'a' && c <= 'z') continue;
        if (c >= '0' && c <= '9') continue;
        switch (c) {
            '!', '#', '$', '%', '&', '\'', '*', '+', '-', '.', '^', '_', '`', '|', '~' => continue,
            else => return true,
        }
    }
    return false;
}

/// RFC 9113 §8.2.1: field values must not contain NUL (0x00), CR (0x0d), or LF (0x0a).
pub fn isMalformedFieldValue(value: []const u8): bool {
    for (c of value) {
        if (c == 0 || c == '\r' || c == '\n') return true;
    }
    return false;
}

pub struct Connection {
    allocator: Allocator,
    is_server: bool,

    local_settings: Settings,
    remote_settings: Settings,
    local_settings_acked: bool = false,
    max_header_list_pairs: u32 = 128,
    max_invalid_frames: u32 = 1000,
    acked_local_initial_window: u32 = 65_535,
    pending_local_window_acks: std.ArrayListUnmanaged(u32) = .{},
    invalid_frame_count: u32 = 0,
    max_settings: u32 = 32,

    send_window: SendWindow,
    recv_window: RecvWindow,

    hpack_coder: hpack.Coder,
    streams: std.AutoHashMapUnmanaged(u32, Stream) = .{},

    continuation_stream: u32 = 0,
    header_block: std.ArrayListUnmanaged(u8) = .{},
    data_in_flight: DataInFlight? = null,
    header_end_stream: bool = false,
    header_flags: u8 = 0,
    header_target: u32 = 0,
    header_push_parent: u32 = 0,
    header_stream_closed: bool = false,

    enc_buf: std.ArrayListUnmanaged(u8) = .{},
    replenish_buf: std.ArrayListUnmanaged(ReplenishItem) = .{},
    evict_buf: std.ArrayListUnmanaged(u32) = .{},

    preface_received: usize = 0,
    last_stream_id: u32 = 0,
    going_away: bool = false,

    static fn init(allocator: Allocator, is_server: bool, local: Settings): Connection {
        return Connection{
            allocator: allocator,
            is_server: is_server,
            local_settings: local,
            remote_settings: Settings.initDefault(),
            send_window: SendWindow.initDefault(),
            recv_window: RecvWindow.initDefault(),
            hpack_coder: hpack.Coder.init(local.header_table_size),
        };
    }

    fn deinit(*_) {
        _.hpack_coder.deinit();
        _.streams.deinit(_.allocator);
        _.header_block.deinit(_.allocator);
        _.enc_buf.deinit(_.allocator);
        _.replenish_buf.deinit(_.allocator);
        _.evict_buf.deinit(_.allocator);
        _.pending_local_window_acks.deinit(_.allocator);
    }

    // ---- Outbound -------------------------------------------------------

    fn writeFrame(*_, sink: auto, ftype: FrameType, f: u8, stream_id: u32, payload: []const u8) {
        let hdr_buf: [wire.FRAME_HEADER_SIZE]u8 = undefined;
        const hdr = FrameHeader{
            length: @as(u32, @intCast(payload.len)),
            frame_type: @intFromEnum(ftype),
            flags: f,
            stream_id: stream_id,
        };
        hdr.write(&hdr_buf);
        * = sink.sinkWrite(&hdr_buf);
        if (payload.len > 0) {
            * = sink.sinkWrite(payload);
        }
    }

    fn sendClientPreface(*_, sink: auto) {
        * = sink.sinkWrite(wire.CONNECTION_PREFACE);
    }

    fn sendSettings(*_, sink: auto) {
        let buf: [42]u8 = undefined; // Settings.STANDARD_COUNT * 6
        const n = _.local_settings.packStandard(&buf);
        _.pending_local_window_acks.append(_.allocator, _.local_settings.initial_window_size) catch {};
        _.writeFrame(sink, .Settings, 0, 0, buf[0..n]);
    }

    fn sendSettingsAck(*_, sink: auto) {
        const empty: [0]u8 = undefined;
        _.writeFrame(sink, .Settings, wire.flags.ACK, 0, empty[0..0]);
    }

    fn sendPing(*_, sink: auto, payload: [8]u8) {
        _.writeFrame(sink, .Ping, 0, 0, &payload);
    }

    fn sendPingAck(*_, sink: auto, payload: []const u8) {
        _.writeFrame(sink, .Ping, wire.flags.ACK, 0, payload);
    }

    fn sendGoAway(*_, sink: auto, code: ErrorCode, debug: []const u8) {
        _.going_away = true;
        let payload = ByteList{};
        defer payload.deinit(_.allocator);
        let cap_ok = true;
        payload.ensureTotalCapacity(_.allocator, 8 + debug.len) catch {
            cap_ok = false;
        };
        if (cap_ok) {
            let prefix: [8]u8 = undefined;
            std.mem.writeInt(u32, prefix[0..4], _.last_stream_id, .big);
            std.mem.writeInt(u32, prefix[4..8], code.asU32(), .big);
            payload.appendSliceAssumeCapacity(&prefix);
            payload.appendSliceAssumeCapacity(debug);
            _.writeFrame(sink, .GoAway, 0, 0, payload.items);
        } else {
            let prefix: [8]u8 = undefined;
            std.mem.writeInt(u32, prefix[0..4], _.last_stream_id, .big);
            std.mem.writeInt(u32, prefix[4..8], code.asU32(), .big);
            _.writeFrame(sink, .GoAway, 0, 0, &prefix);
        }
        sink.sinkOnError(code.asU32(), _.last_stream_id, debug);
    }

    fn sendWindowUpdate(*_, sink: auto, stream_id: u32, increment: u32) {
        let buf: [4]u8 = undefined;
        std.mem.writeInt(u32, &buf, increment, .big);
        _.writeFrame(sink, .WindowUpdate, 0, stream_id, &buf);
    }

    fn sendRstStream(*_, sink: auto, stream_id: u32, code: ErrorCode) {
        let buf: [4]u8 = undefined;
        std.mem.writeInt(u32, &buf, code.asU32(), .big);
        _.writeFrame(sink, .RstStream, 0, stream_id, &buf);
    }

    // ---- Inbound --------------------------------------------------------

    fn receive(*_, sink: auto, bytes: []const u8): Feed {
        let offset: usize = 0;

        // §3.4: server validates the 24-octet client preface before any frame.
        if (_.is_server && _.preface_received < wire.CONNECTION_PREFACE.len) {
            const need = wire.CONNECTION_PREFACE.len - _.preface_received;
            const avail = @min(need, bytes.len);
            const expect = wire.CONNECTION_PREFACE[_.preface_received .. _.preface_received + avail];
            if (!std.mem.eql(u8, bytes[0..avail], expect)) {
                _.sendGoAway(sink, .ProtocolError, "invalid connection preface");
                return Feed{ consumed: avail, fatal: true };
            }
            _.preface_received += avail;
            offset += avail;
            if (_.preface_received < wire.CONNECTION_PREFACE.len) {
                return Feed{ consumed: offset, fatal: false };
            }
        }

        if (inflight_val := _.data_in_flight) {
            let inflight = inflight_val;
            _.data_in_flight = null;
            const avail = @min(bytes.len - offset, @as(usize, inflight.payload_remaining));
            const data_now = @min(avail, @as(usize, inflight.data_remaining));
            if (data_now > 0 && !inflight.discard) {
                sink.sinkOnData(inflight.stream_id, bytes[offset .. offset + data_now]);
            }
            inflight.data_remaining -= @as(u32, @intCast(data_now));
            inflight.payload_remaining -= @as(u32, @intCast(avail));
            offset += avail;
            if (inflight.payload_remaining == 0) {
                _.finishStreamedData(sink, inflight);
            } else {
                _.data_in_flight = inflight;
                _.replenishWindows(sink);
                return Feed{ consumed: offset, fatal: false };
            }
        }

        while (true) {
            const remaining = bytes[offset..];
            if (remaining.len < wire.FRAME_HEADER_SIZE) break;
            const hdr = FrameHeader.parse(remaining);
            if (hdr.length > _.local_settings.max_frame_size) {
                _.sendGoAway(sink, .FrameSizeError, "frame exceeds SETTINGS_MAX_FRAME_SIZE");
                return Feed{ consumed: offset, fatal: true };
            }
            const total = wire.FRAME_HEADER_SIZE + @as(usize, hdr.length);
            if (remaining.len < total) {
                if (hdr.typ() == .Data && _.continuation_stream == 0 && hdr.stream_id != 0 && hdr.length > 0) {
                    const padded = wire.flags.has(hdr.flags, wire.flags.PADDED);
                    const avail_payload = remaining.len - wire.FRAME_HEADER_SIZE;
                    if (!padded || avail_payload >= 1) {
                        const res = _.beginStreamedData(sink, hdr, remaining, padded);
                        switch (res) {
                            .fatal => return Feed{ consumed: offset, fatal: true },
                            .consumed => |n| offset += n,
                        }
                    }
                }
                break;
            }
            const payload = remaining[wire.FRAME_HEADER_SIZE..total];
            if (_.dispatch(sink, hdr, payload)) {
                return Feed{ consumed: offset + total, fatal: true };
            }
            offset += total;
        }

        _.replenishWindows(sink);
        return Feed{ consumed: offset, fatal: false };
    }

    fn replenishWindows(*_, sink: auto) {
        if (_.recv_window.needsUpdate()) {
            const inc = _.recv_window.takeUpdate();
            if (inc > 0) {
                _.sendWindowUpdate(sink, 0, inc);
            }
        }
        _.replenish_buf.clearRetainingCapacity();
        let it = _.streams.iterator();
        while (entry := it.next()) {
            if (entry.value_ptr.state != .Closed && entry.value_ptr.recv_window.needsUpdate()) {
                const inc = entry.value_ptr.recv_window.takeUpdate();
                if (inc > 0) {
                    _.replenish_buf.append(_.allocator, ReplenishItem{ id: entry.key_ptr.*, inc: inc }) catch {};
                }
            }
        }
        for (item of _.replenish_buf.items) {
            _.sendWindowUpdate(sink, item.id, item.inc);
        }

        _.evict_buf.clearRetainingCapacity();
        let it2 = _.streams.iterator();
        while (entry := it2.next()) {
            if (entry.value_ptr.state == .Closed) {
                _.evict_buf.append(_.allocator, entry.key_ptr.*) catch {};
            }
        }
        for (id of _.evict_buf.items) {
            * = _.streams.remove(id);
        }
    }

    fn dispatch(*_, sink: auto, hdr: FrameHeader, payload: []const u8): bool {
        if (_.continuation_stream != 0) {
            const is_continuation = hdr.typ() == .Continuation;
            if (!is_continuation || hdr.stream_id != _.continuation_stream) {
                _.sendGoAway(sink, .ProtocolError, "expected CONTINUATION frame");
                return true;
            }
        }

        const validation = wire.validateHeader(hdr, _.local_settings.max_frame_size);
        switch (validation) {
            .ok => {},
            .connection_error => |code| {
                _.sendGoAway(sink, code, "frame validation failed");
                return true;
            },
            .stream_error => |se| {
                _.sendRstStream(sink, se.id, se.code);
                return false;
            },
        }

        if (hdr.typ() == .Continuation && _.continuation_stream == 0) {
            _.sendGoAway(sink, .ProtocolError, "unexpected CONTINUATION frame");
            return true;
        }

        const ftype = hdr.typ() ?? return false;
        return switch (ftype) {
            .Settings => _.handleSettings(sink, hdr, payload),
            .Ping => _.handlePing(sink, hdr, payload),
            .GoAway => _.handleGoAway(sink, payload),
            .WindowUpdate => _.handleWindowUpdate(sink, hdr, payload),
            .Headers => _.handleHeaders(sink, hdr, payload),
            .Continuation => _.handleContinuation(sink, hdr, payload),
            .Data => _.handleData(sink, hdr, payload),
            .RstStream => _.handleRstStream(sink, hdr, payload),
            .PushPromise => _.handlePushPromise(sink, hdr, payload),
            .AltSvc => _.handleAltsvc(sink, hdr, payload),
            .Origin => _.handleOrigin(sink, hdr, payload),
            .Priority => false,
        };
    }

    fn handleSettings(*_, sink: auto, hdr: FrameHeader, payload: []const u8): bool {
        if (wire.flags.has(hdr.flags, wire.flags.ACK)) {
            if (hdr.length != 0) {
                _.sendGoAway(sink, .FrameSizeError, "SETTINGS ACK with payload");
                return true;
            }
            _.local_settings_acked = true;
            if (_.pending_local_window_acks.items.len > 0) {
                _.acked_local_initial_window = _.pending_local_window_acks.orderedRemove(0);
            } else {
                _.acked_local_initial_window = _.local_settings.initial_window_size;
            }
            const snapshot = _.local_settings;
            sink.sinkOnLocalSettings(&snapshot);
            return false;
        }
        if (@divTrunc(payload.len, 6) > _.max_settings) {
            _.sendGoAway(sink, .EnhanceYourCalm, "SETTINGS: too many settings entries");
            return true;
        }
        if (code := settings.validatePayload(payload)) {
            _.sendGoAway(sink, code, "SETTINGS value out of range");
            return true;
        }
        const old_table = _.remote_settings.header_table_size;
        const old_initial_window = _.remote_settings.initial_window_size;
        let i: usize = 0;
        while (i + 6 <= payload.len) {
            const id = std.mem.readInt(u16, payload[i..][0..2], .big);
            const value = std.mem.readInt(u32, payload[i + 2 ..][0..4], .big);
            if (sid := SettingId.fromU16(id)) {
                _.remote_settings.apply(sid, value);
            }
            i += 6;
        }
        if (_.remote_settings.header_table_size != old_table) {
            _.hpack_coder.queueEncoderCapacity(_.remote_settings.header_table_size);
        }
        if (_.remote_settings.initial_window_size != old_initial_window) {
            const delta = settings.initialWindowDelta(old_initial_window, _.remote_settings.initial_window_size);
            let it = _.streams.iterator();
            while (entry := it.next()) {
                if (entry.value_ptr.state != .Closed) {
                    entry.value_ptr.send_window.applyInitialDelta(delta);
                }
            }
        }
        // §6.5.3: ACK first. on_remote_settings flushes DATA the enlarged window
        // unblocked, and the peer enforces its old receive window until it sees the ACK.
        _.sendSettingsAck(sink);
        const snapshot = _.remote_settings;
        sink.sinkOnRemoteSettings(&snapshot);
        return false;
    }

    fn handlePing(*_, sink: auto, hdr: FrameHeader, payload: []const u8): bool {
        if (wire.flags.has(hdr.flags, wire.flags.ACK)) {
            sink.sinkOnPing(payload, true);
            return false;
        }
        let echo: [8]u8 = undefined;
        @memcpy(&echo, payload[0..8]);
        _.sendPingAck(sink, &echo);
        sink.sinkOnPing(&echo, false);
        return false;
    }

    fn handleGoAway(*_, sink: auto, payload: []const u8): bool {
        if (payload.len < 8) {
            _.sendGoAway(sink, .FrameSizeError, "GOAWAY too short");
            return true;
        }
        const last_stream_id = std.mem.readInt(u32, payload[0..4], .big) & 0x7fff_ffff;
        const code_raw = std.mem.readInt(u32, payload[4..8], .big);
        _.going_away = true;
        sink.sinkOnGoAway(code_raw, last_stream_id, payload[8..]);
        return false;
    }

    fn handleWindowUpdate(*_, sink: auto, hdr: FrameHeader, payload: []const u8): bool {
        const increment = std.mem.readInt(u32, payload[0..4], .big) & 0x7fff_ffff;
        if (increment == 0) {
            if (hdr.stream_id == 0) {
                _.sendGoAway(sink, .ProtocolError, "WINDOW_UPDATE with 0 increment");
                return true;
            }
            _.sendRstStream(sink, hdr.stream_id, .ProtocolError);
            if (s := _.streams.getPtr(hdr.stream_id)) {
                s.state = .Closed;
            }
            sink.sinkOnStreamReset(hdr.stream_id, ErrorCode.ProtocolError.asU32());
            return false;
        }
        if (hdr.stream_id == 0) {
            if (_.send_window.increase(increment) != null) {
                _.sendGoAway(sink, .FlowControlError, "connection flow-control window overflow");
                return true;
            }
        } else if (s := _.streams.getPtr(hdr.stream_id)) {
            if (s.send_window.increase(increment) != null) {
                s.state = .Closed;
                _.sendRstStream(sink, hdr.stream_id, .FlowControlError);
                sink.sinkOnStreamReset(hdr.stream_id, ErrorCode.FlowControlError.asU32());
                return false;
            }
        }
        sink.sinkOnWindowUpdate(hdr.stream_id, increment);
        return false;
    }

    // ---- Stream-level inbound ------------------------------------------

    fn handleHeaders(*_, sink: auto, hdr: FrameHeader, payload: []const u8): bool {
        let off: usize = 0;
        let end: usize = payload.len;

        if (wire.flags.has(hdr.flags, wire.flags.PADDED)) {
            if (payload.len == 0) {
                _.sendGoAway(sink, .FrameSizeError, "HEADERS padded but empty");
                return true;
            }
            const pad = @as(usize, payload[0]);
            off = 1;
            if (off + pad > end) {
                _.sendGoAway(sink, .ProtocolError, "HEADERS padding too large");
                return true;
            }
            end -= pad;
        }
        if (wire.flags.has(hdr.flags, wire.flags.PRIORITY)) {
            if (off + 5 > end) {
                _.sendGoAway(sink, .FrameSizeError, "HEADERS priority truncated");
                return true;
            }
            off += 5;
        }

        const end_stream = wire.flags.has(hdr.flags, wire.flags.END_STREAM);
        const end_headers = wire.flags.has(hdr.flags, wire.flags.END_HEADERS);

        const send_init = _.remote_settings.initial_window_size;
        const recv_init = _.local_settings.initial_window_size;
        const is_new = !_.streams.contains(hdr.stream_id);
        if (is_new && _.is_server && (hdr.stream_id % 2) == 0) {
            _.sendGoAway(sink, .ProtocolError, "invalid stream id for HEADERS");
            return true;
        }

        const gop = _.streams.getOrPut(_.allocator, hdr.stream_id) catch {
            _.sendGoAway(sink, .InternalError, "out of memory");
            return true;
        };
        if (!gop.found_existing) {
            gop.value_ptr.* = Stream.init(send_init, recv_init);
        }
        const cur_state = gop.value_ptr.state;
        const ev: stream.Event = if (end_stream) .RecvHeadersEndStream else .RecvHeaders;
        let stream_closed = false;
        let next_opt: State? = null;
        if (ns := stream.transition(cur_state, ev) catch |e| {
            if (e == error.Protocol) {
                _.sendGoAway(sink, .ProtocolError, "HEADERS in invalid stream state");
                return true;
            }
            stream_closed = true;
            null
        }) {
            next_opt = ns;
        }
        if (next := next_opt) {
            gop.value_ptr.state = next;
        }

        if (is_new) {
            if (hdr.stream_id > _.last_stream_id) {
                _.last_stream_id = hdr.stream_id;
            }
            sink.sinkOnStreamOpen(hdr.stream_id);
        }

        _.header_block.clearRetainingCapacity();
        _.header_block.appendSlice(_.allocator, payload[off..end]) catch {
            _.sendGoAway(sink, .InternalError, "out of memory");
            return true;
        };
        _.header_end_stream = end_stream;
        _.header_flags = hdr.flags;
        _.header_target = hdr.stream_id;
        _.header_push_parent = 0;
        _.header_stream_closed = stream_closed;
        if (!end_headers) {
            _.continuation_stream = hdr.stream_id;
            return false;
        }
        return _.finishHeaderBlock(sink);
    }

    fn handleContinuation(*_, sink: auto, hdr: FrameHeader, payload: []const u8): bool {
        const cap = @max(@as(usize, _.local_settings.max_header_list_size), @as(usize, 65536));
        if (_.header_block.items.len + payload.len > cap) {
            _.sendGoAway(sink, .EnhanceYourCalm, "header block too large");
            return true;
        }
        _.header_block.appendSlice(_.allocator, payload) catch {
            _.sendGoAway(sink, .InternalError, "out of memory");
            return true;
        };
        if (!wire.flags.has(hdr.flags, wire.flags.END_HEADERS)) {
            return false;
        }
        return _.finishHeaderBlock(sink);
    }

    fn finishHeaderBlock(*_, sink: auto): bool {
        _.continuation_stream = 0;
        const target = _.header_target;
        const push_parent = _.header_push_parent;
        if (push_parent != 0) {
            sink.sinkOnPushPromise(push_parent, target);
        }
        const block = _.header_block.items;
        const stream_closed = _.header_stream_closed;
        _.header_stream_closed = false;

        let off: usize = 0;
        let fatal = false;
        const max_list_size = @as(usize, _.local_settings.max_header_list_size);
        const max_pairs = @as(usize, _.max_header_list_pairs);
        let list_size: usize = 0;
        let field_count: usize = 0;
        let rejected = false;
        let malformed = false;
        let seen_regular = false;
        let seen_pseudo: u8 = 0;

        while (off < block.len) {
            const h = _.hpack_coder.decode(block[off..]) catch |e| {
                * = e;
                _.sendGoAway(sink, .CompressionError, "HPACK decode error");
                fatal = true;
                break;
            };
            off += h.next;
            list_size += h.name.len + h.value.len + 32;
            field_count += 1;
            if (rejected || list_size > max_list_size || field_count > max_pairs) {
                rejected = true;
                continue;
            }
            if (stream_closed) {
                continue;
            }
            if (!malformed) {
                const name_b = h.name;
                const value_b = h.value;
                if (isMalformedFieldName(name_b) || isMalformedFieldValue(value_b)) {
                    malformed = true;
                } else if (name_b.len > 0 && name_b[0] == ':') {
                    const rest = name_b[1..];
                    let bit: u8 = 64;
                    if (std.mem.eql(u8, rest, "method")) {
                        bit = 1;
                    } else if (std.mem.eql(u8, rest, "scheme")) {
                        bit = 2;
                    } else if (std.mem.eql(u8, rest, "authority")) {
                        bit = 4;
                    } else if (std.mem.eql(u8, rest, "path")) {
                        bit = 8;
                    } else if (std.mem.eql(u8, rest, "status")) {
                        bit = 16;
                    } else if (std.mem.eql(u8, rest, "protocol")) {
                        bit = 32;
                    }
                    const wrong_direction = _.is_server && std.mem.eql(u8, rest, "status");
                    if (seen_regular || bit == 64 || (seen_pseudo & bit) != 0 || wrong_direction) {
                        malformed = true;
                    }
                    seen_pseudo |= bit;
                } else {
                    seen_regular = true;
                    if (std.mem.eql(u8, name_b, "connection") ||
                        std.mem.eql(u8, name_b, "keep-alive") ||
                        std.mem.eql(u8, name_b, "proxy-connection") ||
                        std.mem.eql(u8, name_b, "transfer-encoding") ||
                        std.mem.eql(u8, name_b, "upgrade"))
                    {
                        malformed = true;
                    } else if (std.mem.eql(u8, name_b, "te")) {
                        if (!std.ascii.eqlIgnoreCase(value_b, "trailers")) {
                            malformed = true;
                        }
                    }
                }
            }
            if (malformed) continue;
            sink.sinkOnHeader(target, h.name, h.value, h.never_index);
        }

        _.header_block.clearRetainingCapacity();
        if (fatal) return true;

        if (stream_closed) {
            _.sendRstStream(sink, target, .StreamClosed);
            if (s := _.streams.getPtr(target)) {
                s.state = .Closed;
            }
            sink.sinkOnStreamReset(target, ErrorCode.StreamClosed.asU32());
            return false;
        }
        if (malformed && !rejected) {
            _.sendRstStream(sink, target, .ProtocolError);
            if (s := _.streams.getPtr(target)) {
                s.state = .Closed;
            }
            sink.sinkOnStreamReset(target, ErrorCode.ProtocolError.asU32());
            sink.sinkOnStreamRejected(target);
            return false;
        }
        if (rejected) {
            _.sendRstStream(sink, target, .EnhanceYourCalm);
            if (s := _.streams.getPtr(target)) {
                s.state = .Closed;
            }
            sink.sinkOnStreamReset(target, ErrorCode.EnhanceYourCalm.asU32());
            sink.sinkOnStreamRejected(target);
            return false;
        }
        const end_stream = _.header_end_stream;
        sink.sinkOnHeadersComplete(target, end_stream, _.header_flags);
        if (end_stream) {
            if (s := _.streams.getPtr(target)) {
                sink.sinkOnStreamEnd(target, @intFromEnum(s.state));
            }
        }
        return false;
    }

    fn beginStreamedData(*_, sink: auto, hdr: FrameHeader, remaining: []const u8, padded: bool): StreamedDataStart {
        const payload_avail = remaining[wire.FRAME_HEADER_SIZE..];
        let pad: usize = 0;
        let off: usize = 0;
        if (padded) {
            pad = @as(usize, payload_avail[0]);
            off = 1;
            if (1 + pad > @as(usize, hdr.length)) {
                _.sendGoAway(sink, .ProtocolError, "DATA padding too large");
                return .fatal;
            }
        }
        const data_total = @as(usize, hdr.length) - off - pad;

        _.recv_window.onData(@as(i64, hdr.length));
        if (_.recv_window.isOverflowed()) {
            _.sendGoAway(sink, .FlowControlError, "connection flow-control window exceeded");
            return .fatal;
        }

        if (!_.streams.contains(hdr.stream_id) && sink.sinkIsLocalStream(hdr.stream_id)) {
            const send_init = _.remote_settings.initial_window_size;
            const recv_init = _.local_settings.initial_window_size;
            let s = Stream.init(send_init, recv_init);
            s.state = .Open;
            _.streams.put(_.allocator, hdr.stream_id, s) catch {};
        }

        const recv_limit = @as(i64, @max(_.acked_local_initial_window, _.local_settings.initial_window_size));
        let discard = false;
        if (st := _.streams.getPtr(hdr.stream_id)) {
            if (!stream.canReceiveData(st.state)) {
                _.sendRstStream(sink, hdr.stream_id, .StreamClosed);
                st.state = .Closed;
                sink.sinkOnStreamReset(hdr.stream_id, ErrorCode.StreamClosed.asU32());
                discard = true;
            } else {
                st.recv_window.onData(@as(i64, hdr.length));
                if (st.recv_window.isOverflowedWith(recv_limit)) {
                    _.sendRstStream(sink, hdr.stream_id, .FlowControlError);
                    st.state = .Closed;
                    sink.sinkOnStreamReset(hdr.stream_id, ErrorCode.FlowControlError.asU32());
                    discard = true;
                }
            }
        } else {
            _.sendRstStream(sink, hdr.stream_id, .StreamClosed);
            sink.sinkOnStreamReset(hdr.stream_id, ErrorCode.StreamClosed.asU32());
            discard = true;
        }

        const body_avail = payload_avail[off..];
        const data_now = @min(body_avail.len, data_total);
        if (data_now > 0 && !discard) {
            sink.sinkOnData(hdr.stream_id, body_avail[0..data_now]);
        }
        const consumed_payload = off + body_avail.len;
        const inflight = DataInFlight{
            stream_id: hdr.stream_id,
            payload_remaining: @as(u32, @intCast(@as(usize, hdr.length) - consumed_payload)),
            data_remaining: @as(u32, @intCast(data_total - data_now)),
            end_stream: wire.flags.has(hdr.flags, wire.flags.END_STREAM),
            discard: discard,
        };
        std.debug.assert(inflight.payload_remaining > 0);
        _.data_in_flight = inflight;
        return StreamedDataStart{ consumed: wire.FRAME_HEADER_SIZE + consumed_payload };
    }

    fn finishStreamedData(*_, sink: auto, inflight: DataInFlight) {
        if (inflight.end_stream && !inflight.discard) {
            if (s := _.streams.getPtr(inflight.stream_id)) {
                if (next := stream.transition(s.state, .RecvEndStream) catch null) {
                    s.state = next;
                }
                sink.sinkOnStreamEnd(inflight.stream_id, @intFromEnum(s.state));
            }
        }
    }

    fn handleData(*_, sink: auto, hdr: FrameHeader, payload: []const u8): bool {
        let off: usize = 0;
        let end: usize = payload.len;
        if (wire.flags.has(hdr.flags, wire.flags.PADDED)) {
            if (payload.len == 0) {
                _.sendGoAway(sink, .FrameSizeError, "DATA padded but empty");
                return true;
            }
            const pad = @as(usize, payload[0]);
            off = 1;
            if (off + pad > end) {
                _.sendGoAway(sink, .ProtocolError, "DATA padding too large");
                return true;
            }
            end -= pad;
        }
        const consumed = @as(i64, @intCast(payload.len));

        _.recv_window.onData(consumed);
        if (_.recv_window.isOverflowed()) {
            _.sendGoAway(sink, .FlowControlError, "connection flow-control window exceeded");
            return true;
        }

        if (payload.len == 0 && !wire.flags.has(hdr.flags, wire.flags.END_STREAM)) {
            const count = _.invalid_frame_count;
            _.invalid_frame_count = count +| 1;
            if (count > _.max_invalid_frames) {
                sink.sinkOnTooManyInvalidFrames();
                return true;
            }
        }

        if (!_.streams.contains(hdr.stream_id) && sink.sinkIsLocalStream(hdr.stream_id)) {
            const send_init = _.remote_settings.initial_window_size;
            const recv_init = _.local_settings.initial_window_size;
            let s = Stream.init(send_init, recv_init);
            s.state = .Open;
            _.streams.put(_.allocator, hdr.stream_id, s) catch {};
        }

        const recv_limit = @as(i64, @max(_.acked_local_initial_window, _.local_settings.initial_window_size));
        let decision: DataDecision = DataDecision{ deliver: 0 };
        if (s := _.streams.getPtr(hdr.stream_id)) {
            if (!stream.canReceiveData(s.state)) {
                decision = DataDecision{ rst: .StreamClosed };
            } else {
                s.recv_window.onData(consumed);
                if (s.recv_window.isOverflowedWith(recv_limit)) {
                    decision = DataDecision{ rst: .FlowControlError };
                }
            }
        } else {
            decision = DataDecision{ rst: .StreamClosed };
        }

        switch (decision) {
            .rst => |code| {
                _.sendRstStream(sink, hdr.stream_id, code);
                if (s := _.streams.getPtr(hdr.stream_id)) {
                    s.state = .Closed;
                }
                sink.sinkOnStreamReset(hdr.stream_id, code.asU32());
                return false;
            },
            .deliver => {},
        }

        const end_stream = wire.flags.has(hdr.flags, wire.flags.END_STREAM);
        if (end != off) {
            sink.sinkOnData(hdr.stream_id, payload[off..end]);
        }

        if (end_stream) {
            if (s := _.streams.getPtr(hdr.stream_id)) {
                if (next := stream.transition(s.state, .RecvEndStream) catch null) {
                    s.state = next;
                }
                sink.sinkOnStreamEnd(hdr.stream_id, @intFromEnum(s.state));
            }
        }
        return false;
    }

    fn handleRstStream(*_, sink: auto, hdr: FrameHeader, payload: []const u8): bool {
        const code_raw = std.mem.readInt(u32, payload[0..4], .big);
        let on_idle = true;
        if (s := _.streams.getPtr(hdr.stream_id)) {
            if (s.state != .Idle) {
                s.state = .Closed;
                on_idle = false;
            }
        }
        if (on_idle && sink.sinkIsLocalStream(hdr.stream_id)) {
            const send_init = _.remote_settings.initial_window_size;
            const recv_init = _.local_settings.initial_window_size;
            let s = Stream.init(send_init, recv_init);
            s.state = .Closed;
            _.streams.put(_.allocator, hdr.stream_id, s) catch {};
            on_idle = false;
        }
        if (on_idle && hdr.stream_id <= _.last_stream_id) {
            return false;
        }
        if (on_idle) {
            _.sendGoAway(sink, .ProtocolError, "RST_STREAM on idle stream");
            return true;
        }
        sink.sinkOnStreamReset(hdr.stream_id, code_raw);
        return false;
    }

    fn handlePushPromise(*_, sink: auto, hdr: FrameHeader, payload: []const u8): bool {
        if (_.is_server) {
            _.sendGoAway(sink, .ProtocolError, "server received PUSH_PROMISE");
            return true;
        }
        if (_.local_settings.enable_push == 0) {
            _.sendGoAway(sink, .ProtocolError, "PUSH_PROMISE with push disabled");
            return true;
        }
        let off: usize = 0;
        let end: usize = payload.len;
        if (wire.flags.has(hdr.flags, wire.flags.PADDED)) {
            if (payload.len == 0) {
                _.sendGoAway(sink, .FrameSizeError, "PUSH_PROMISE padded but empty");
                return true;
            }
            const pad = @as(usize, payload[0]);
            off = 1;
            if (off + pad > end) {
                _.sendGoAway(sink, .ProtocolError, "PUSH_PROMISE padding too large");
                return true;
            }
            end -= pad;
        }
        if (off + 4 > end) {
            _.sendGoAway(sink, .FrameSizeError, "PUSH_PROMISE missing promised id");
            return true;
        }
        const promised = std.mem.readInt(u32, payload[off..][0..4], .big) & 0x7fff_ffff;
        off += 4;
        if (promised == 0 || (promised & 1) == 1 || _.streams.contains(promised)) {
            _.sendGoAway(sink, .ProtocolError, "PUSH_PROMISE invalid promised stream id");
            return true;
        }

        const send_init = _.remote_settings.initial_window_size;
        const recv_init = _.local_settings.initial_window_size;
        let entry = Stream.init(send_init, recv_init);
        entry.state = .ReservedRemote;
        _.streams.put(_.allocator, promised, entry) catch {
            _.sendGoAway(sink, .InternalError, "out of memory");
            return true;
        };
        if (promised > _.last_stream_id) {
            _.last_stream_id = promised;
        }

        const end_headers = wire.flags.has(hdr.flags, wire.flags.END_HEADERS);
        _.header_block.clearRetainingCapacity();
        _.header_block.appendSlice(_.allocator, payload[off..end]) catch {
            _.sendGoAway(sink, .InternalError, "out of memory");
            return true;
        };
        _.header_end_stream = false;
        _.header_flags = 0;
        _.header_target = promised;
        _.header_push_parent = hdr.stream_id;
        _.header_stream_closed = false;
        if (!end_headers) {
            _.continuation_stream = hdr.stream_id;
            return false;
        }
        return _.finishHeaderBlock(sink);
    }

    fn handleAltsvc(*_, sink: auto, hdr: FrameHeader, payload: []const u8): bool {
        if (payload.len < 2) return false;
        const origin_len = @as(usize, std.mem.readInt(u16, payload[0..2], .big));
        if (2 + origin_len > payload.len) return false;
        const origin = payload[2 .. 2 + origin_len];
        const value = payload[2 + origin_len ..];
        if (_.is_server || (hdr.stream_id == 0 && origin.len == 0) || (hdr.stream_id != 0 && origin.len != 0)) {
            return false;
        }
        sink.sinkOnAltsvc(hdr.stream_id, origin, value);
        return false;
    }

    fn handleOrigin(*_, sink: auto, hdr: FrameHeader, payload: []const u8): bool {
        if (hdr.stream_id != 0 || _.is_server) return false;
        sink.sinkOnOrigin(payload);
        return false;
    }

    // ---- Outbound stream API (called by the embedder) ------------------

    fn beginHeaderBlock(*_) {
        _.enc_buf.clearRetainingCapacity();
        let tmp: [6]u8 = undefined; // hpack.MAX_SIZE_UPDATE_BYTES
        const n = _.hpack_coder.takePendingSizeUpdate(&tmp, 0);
        if (n > 0) {
            _.enc_buf.appendSlice(_.allocator, tmp[0..n]) catch {};
        }
    }

    fn encodeHeader(*_, name: []const u8, value: []const u8, never_index: bool): bool {
        const old = _.enc_buf.items.len;
        const need = old + name.len + value.len + 16;
        _.enc_buf.resize(_.allocator, need) catch return false;
        const n = _.hpack_coder.encode(name, value, never_index, _.enc_buf.items, old) catch {
            _.enc_buf.shrinkRetainingCapacity(old);
            return false;
        };
        _.enc_buf.shrinkRetainingCapacity(old + n);
        return true;
    }

    fn sendHeaderBlock(*_, sink: auto, stream_id: u32, end_stream: bool) {
        const block = _.enc_buf.items;
        const max = @max(@as(usize, _.remote_settings.max_frame_size), @as(usize, 1));
        const total = block.len;

        const first_len = @min(total, max);
        let f: u8 = 0;
        if (first_len == total) f |= wire.flags.END_HEADERS;
        if (end_stream) f |= wire.flags.END_STREAM;
        _.writeFrame(sink, .Headers, f, stream_id, block[0..first_len]);

        let off = first_len;
        while (off < total) {
            const len = @min(total - off, max);
            const cf: u8 = if (off + len == total) wire.flags.END_HEADERS else 0;
            _.writeFrame(sink, .Continuation, cf, stream_id, block[off .. off + len]);
            off += len;
        }
        _.enc_buf.clearRetainingCapacity();

        const send_init = _.remote_settings.initial_window_size;
        const recv_init = _.local_settings.initial_window_size;
        const gop = _.streams.getOrPut(_.allocator, stream_id) catch return;
        if (!gop.found_existing) {
            gop.value_ptr.* = Stream.init(send_init, recv_init);
        }
        const ev: stream.Event = if (end_stream) .SendHeadersEndStream else .SendHeaders;
        if (next := stream.transition(gop.value_ptr.state, ev) catch null) {
            gop.value_ptr.state = next;
        }
        if (stream_id > _.last_stream_id) {
            _.last_stream_id = stream_id;
        }
    }

    fn sendData(*_, sink: auto, stream_id: u32, data: []const u8, end_stream: bool): usize {
        const conn_avail = _.send_window.available();
        let stream_avail: i64 = 0;
        if (s := _.streams.getPtr(stream_id)) {
            stream_avail = s.send_window.available();
        }
        const max_frame = @as(i64, _.remote_settings.max_frame_size);
        const allowed_raw = @min(@min(conn_avail, stream_avail), max_frame);
        const allowed = @as(usize, @intCast(@max(allowed_raw, @as(i64, 0))));
        const to_send = @min(allowed, data.len);
        const send_all = to_send == data.len;

        if (to_send == 0 && !(data.len == 0 && end_stream)) {
            return 0;
        }

        const f: u8 = if (end_stream && send_all) wire.flags.END_STREAM else 0;
        _.writeFrame(sink, .Data, f, stream_id, data[0..to_send]);
        _.send_window.consume(@as(i64, @intCast(to_send)));
        if (s := _.streams.getPtr(stream_id)) {
            s.send_window.consume(@as(i64, @intCast(to_send)));
            if (end_stream && send_all) {
                if (next := stream.transition(s.state, .SendEndStream) catch null) {
                    s.state = next;
                }
            }
        }
        return to_send;
    }

    fn closeStream(*_, stream_id: u32) {
        * = _.streams.remove(stream_id);
    }

    fn sendReset(*_, sink: auto, stream_id: u32, code: ErrorCode) {
        _.sendRstStream(sink, stream_id, code);
        if (s := _.streams.getPtr(stream_id)) {
            s.state = .Closed;
        }
    }

    fn sendPushPromise(*_, sink: auto, parent_id: u32, promised_id: u32) {
        const block = _.enc_buf.items;
        const max = @max(@as(usize, _.remote_settings.max_frame_size), @as(usize, 5));
        const first_cap = max - 4;
        const first_len = @min(block.len, first_cap);

        let first = ByteList{};
        defer first.deinit(_.allocator);
        let first_ok = true;
        first.ensureTotalCapacity(_.allocator, 4 + first_len) catch {
            first_ok = false;
        };
        if (first_ok) {
            let pid_buf: [4]u8 = undefined;
            std.mem.writeInt(u32, &pid_buf, promised_id & 0x7fff_ffff, .big);
            first.appendSliceAssumeCapacity(&pid_buf);
            first.appendSliceAssumeCapacity(block[0..first_len]);
            const f: u8 = if (first_len == block.len) wire.flags.END_HEADERS else 0;
            _.writeFrame(sink, .PushPromise, f, parent_id, first.items);
        }

        let o = first_len;
        while (o < block.len) {
            const len = @min(block.len - o, max);
            const cf: u8 = if (o + len == block.len) wire.flags.END_HEADERS else 0;
            _.writeFrame(sink, .Continuation, cf, parent_id, block[o .. o + len]);
            o += len;
        }
        _.enc_buf.clearRetainingCapacity();

        const send_init = _.remote_settings.initial_window_size;
        const recv_init = _.local_settings.initial_window_size;
        const gop = _.streams.getOrPut(_.allocator, promised_id) catch return;
        if (!gop.found_existing) {
            gop.value_ptr.* = Stream.init(send_init, recv_init);
        }
        gop.value_ptr.state = .ReservedLocal;
        if (promised_id > _.last_stream_id) {
            _.last_stream_id = promised_id;
        }
    }
}
