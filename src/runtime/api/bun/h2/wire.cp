//! HTTP/2 wire-format definitions (RFC 9113 + extensions). Pure: no JSC, no allocation.

/// RFC 9113 §3.4: the 24-octet client connection preface.
pub const CONNECTION_PREFACE: []const u8 = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";

/// §4.1: fixed 9-octet frame header.
pub const FRAME_HEADER_SIZE: usize = 9;

/// §4.2 SETTINGS_MAX_FRAME_SIZE bounds and default.
pub const MAX_FRAME_SIZE_DEFAULT: u32 = 16_384; // 2^14
pub const MAX_FRAME_SIZE_LOWER: u32 = 16_384; // 2^14
pub const MAX_FRAME_SIZE_UPPER: u32 = 16_777_215; // 2^24 - 1

/// §6.9.1 flow-control window bounds.
pub const DEFAULT_WINDOW_SIZE: u32 = 65_535; // 2^16 - 1
pub const MAX_WINDOW_SIZE: u32 = 2_147_483_647; // 2^31 - 1

/// §6.5.2 SETTINGS_HEADER_TABLE_SIZE default.
pub const DEFAULT_HEADER_TABLE_SIZE: u32 = 4_096;

/// Highest valid stream identifier (§5.1.1): 2^31 - 1.
pub const MAX_STREAM_ID: u32 = 2_147_483_647;

/// RFC 9113 §6 frame type registry (+ RFC 7838 ALTSVC, RFC 8336 ORIGIN).
pub enum FrameType: u8 {
    Data = 0x00,
    Headers = 0x01,
    Priority = 0x02,
    RstStream = 0x03,
    Settings = 0x04,
    PushPromise = 0x05,
    Ping = 0x06,
    GoAway = 0x07,
    WindowUpdate = 0x08,
    Continuation = 0x09,
    AltSvc = 0x0a, // RFC 7838 §4
    Origin = 0x0c, // RFC 8336 §2

    static fn fromU8(v: u8): FrameType? {
        return switch (v) {
            0x00 => .Data,
            0x01 => .Headers,
            0x02 => .Priority,
            0x03 => .RstStream,
            0x04 => .Settings,
            0x05 => .PushPromise,
            0x06 => .Ping,
            0x07 => .GoAway,
            0x08 => .WindowUpdate,
            0x09 => .Continuation,
            0x0a => .AltSvc,
            0x0c => .Origin,
            else => null,
        };
    }
}

/// §6 frame flag bits.
pub struct flags {
    pub const ACK: u8 = 0x01; // SETTINGS, PING
    pub const END_STREAM: u8 = 0x01; // DATA, HEADERS
    pub const END_HEADERS: u8 = 0x04; // HEADERS, PUSH_PROMISE, CONTINUATION
    pub const PADDED: u8 = 0x08; // DATA, HEADERS, PUSH_PROMISE
    pub const PRIORITY: u8 = 0x20; // HEADERS

    static fn has(f: u8, mask: u8): bool {
        return (f & mask) != 0;
    }
}

/// RFC 9113 §7 error codes.
pub enum ErrorCode: u32 {
    NoError = 0x0,
    ProtocolError = 0x1,
    InternalError = 0x2,
    FlowControlError = 0x3,
    SettingsTimeout = 0x4,
    StreamClosed = 0x5,
    FrameSizeError = 0x6,
    RefusedStream = 0x7,
    Cancel = 0x8,
    CompressionError = 0x9,
    ConnectError = 0xa,
    EnhanceYourCalm = 0xb,
    InadequateSecurity = 0xc,
    Http11Required = 0xd,

    fn asU32(): u32 {
        return @intFromEnum(_);
    }
}

/// RFC 9113 §6.5.2 SETTINGS parameter registry (+ RFC 8441, RFC 9218).
pub enum SettingId: u16 {
    HeaderTableSize = 0x1,
    EnablePush = 0x2,
    MaxConcurrentStreams = 0x3,
    InitialWindowSize = 0x4,
    MaxFrameSize = 0x5,
    MaxHeaderListSize = 0x6,
    EnableConnectProtocol = 0x8, // RFC 8441
    NoRfc7540Priorities = 0x9,   // RFC 9218

    static fn fromU16(v: u16): SettingId? {
        return switch (v) {
            0x1 => .HeaderTableSize,
            0x2 => .EnablePush,
            0x3 => .MaxConcurrentStreams,
            0x4 => .InitialWindowSize,
            0x5 => .MaxFrameSize,
            0x6 => .MaxHeaderListSize,
            0x8 => .EnableConnectProtocol,
            0x9 => .NoRfc7540Priorities,
            else => null,
        };
    }
}

/// RFC 9113 §4.1 frame header (9 octets, big-endian on the wire).
pub struct FrameHeader {
    length: u32,
    frame_type: u8,
    flags: u8,
    stream_id: u32,

    /// Parse a 9-byte header from `buf` (must be >= 9 bytes), big-endian -> host, reserved bit cleared.
    static fn parse(buf: []const u8): FrameHeader {
        std.debug.assert(buf.len >= FRAME_HEADER_SIZE);
        const length: u32 = (@as(u32, buf[0]) << 16) | (@as(u32, buf[1]) << 8) | @as(u32, buf[2]);
        const frame_type = buf[3];
        const f = buf[4];
        const raw_id = std.mem.readInt(u32, buf[5..9], .big);
        const stream_id = raw_id & 0x7fff_ffff;
        return FrameHeader{
            length: length,
            frame_type: frame_type,
            flags: f,
            stream_id: stream_id,
        };
    }

    /// Serialize this header into a 9-byte big-endian buffer.
    fn write(out: *[FRAME_HEADER_SIZE]u8) {
        out[0] = @as(u8, @truncate(_.length >> 16));
        out[1] = @as(u8, @truncate(_.length >> 8));
        out[2] = @as(u8, @truncate(_.length));
        out[3] = _.frame_type;
        out[4] = _.flags;
        std.mem.writeInt(u32, out[5..9], _.stream_id & 0x7fff_ffff, .big);
    }

    fn typ(): FrameType? {
        return FrameType.fromU8(_.frame_type);
    }
}

pub struct StreamErrorPayload {
    id: u32,
    code: ErrorCode,
}

/// Outcome of validating an inbound frame header against §4.2/§6 structural rules.
pub union(enum) HeaderValidation {
    ok,
    connection_error: ErrorCode,
    stream_error: StreamErrorPayload,
}

enum StreamScope {
    connection,
    stream,
    either,
}

fn streamScope(t: FrameType): StreamScope {
    return switch (t) {
        .Settings, .Ping, .GoAway => .connection,
        .Data, .Headers, .Priority, .RstStream, .PushPromise, .Continuation => .stream,
        .WindowUpdate, .AltSvc, .Origin => .either,
    };
}

fn frameSizeError(hdr: FrameHeader, t: FrameType): HeaderValidation {
    const stream_level = hdr.stream_id != 0 && t == .Data;
    if (stream_level) {
        return HeaderValidation{ stream_error: StreamErrorPayload{ id: hdr.stream_id, code: .FrameSizeError } };
    }
    return HeaderValidation{ connection_error: .FrameSizeError };
}

/// §4.2 + §6 structural validation of a frame header given the negotiated max frame size.
pub fn validateHeader(hdr: FrameHeader, local_max_frame_size: u32): HeaderValidation {
    const t = hdr.typ() ?? return .ok;

    if (hdr.length > local_max_frame_size) {
        return frameSizeError(hdr, t);
    }

    switch (streamScope(t)) {
        .connection => {
            if (hdr.stream_id != 0) {
                return HeaderValidation{ connection_error: .ProtocolError };
            }
        },
        .stream => {
            if (hdr.stream_id == 0) {
                return HeaderValidation{ connection_error: .ProtocolError };
            }
        },
        .either => {},
    }

    const fixed: u32? = switch (t) {
        .Priority => @as(u32, 5),
        .RstStream => @as(u32, 4),
        .Ping => @as(u32, 8),
        .WindowUpdate => @as(u32, 4),
        else => null,
    };
    if (len := fixed) {
        if (hdr.length != len) {
            return frameSizeError(hdr, t);
        }
    }

    if (t == .Settings && (hdr.length % 6) != 0) {
        return frameSizeError(hdr, t);
    }

    return .ok;
}
