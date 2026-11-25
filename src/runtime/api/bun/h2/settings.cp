//! HTTP/2 SETTINGS (RFC 9113 §6.5). Pure: value semantics, range validation, on-wire packing,
//! and the INITIAL_WINDOW_SIZE retroactive-window delta.

import wire from "./wire.zig";

const ErrorCode = wire.ErrorCode;
const SettingId = wire.SettingId;

/// Logical SETTINGS values. Defaults match Node v27 `getDefaultSettings()` exactly
/// (note `max_concurrent_streams` = 2^32-1).
pub struct Settings {
    header_table_size: u32 = 4_096,
    enable_push: u32 = 1,
    max_concurrent_streams: u32 = 4_294_967_295, // 2^32-1 (Node default)
    initial_window_size: u32 = 65_535,
    max_frame_size: u32 = 16_384,
    max_header_list_size: u32 = 65_535,
    enable_connect_protocol: u32 = 0,

    static const STANDARD_COUNT: usize = 7;

    static fn initDefault(): Settings {
        return Settings{};
    }

    fn apply(*_, id: SettingId, value: u32) {
        switch (id) {
            .HeaderTableSize => _.header_table_size = value,
            .EnablePush => _.enable_push = value,
            .MaxConcurrentStreams => _.max_concurrent_streams = value,
            .InitialWindowSize => _.initial_window_size = value,
            .MaxFrameSize => _.max_frame_size = value,
            .MaxHeaderListSize => _.max_header_list_size = value,
            .EnableConnectProtocol => _.enable_connect_protocol = value,
            .NoRfc7540Priorities => {},
        }
    }

    /// Serialize all standard parameters as 6-byte big-endian units into `out`
    /// (must be >= STANDARD_COUNT*6). Returns bytes written.
    fn packStandard(out: []u8): usize {
        const ids: [7]u16 = [
            @intFromEnum(SettingId.HeaderTableSize),
            @intFromEnum(SettingId.EnablePush),
            @intFromEnum(SettingId.MaxConcurrentStreams),
            @intFromEnum(SettingId.InitialWindowSize),
            @intFromEnum(SettingId.MaxFrameSize),
            @intFromEnum(SettingId.MaxHeaderListSize),
            @intFromEnum(SettingId.EnableConnectProtocol),
        ];
        const vals: [7]u32 = [
            _.header_table_size,
            _.enable_push,
            _.max_concurrent_streams,
            _.initial_window_size,
            _.max_frame_size,
            _.max_header_list_size,
            _.enable_connect_protocol,
        ];
        let off: usize = 0;
        let i: usize = 0;
        while (i < 7) : (i += 1) {
            std.mem.writeInt(u16, out[off..][0..2], ids[i], .big);
            std.mem.writeInt(u32, out[off + 2 ..][0..4], vals[i], .big);
            off += 6;
        }
        return off;
    }
}

/// §6.5.2 single-value range validation. `ErrorCode` = connection error to send, `null` = ok.
pub fn validateUnit(id: u16, value: u32): ErrorCode? {
    const sid = SettingId.fromU16(id) ?? return null;
    switch (sid) {
        .EnablePush, .EnableConnectProtocol, .NoRfc7540Priorities => {
            if (value > 1) return .ProtocolError;
        },
        .InitialWindowSize => {
            if (value > wire.MAX_WINDOW_SIZE) return .FlowControlError;
        },
        .MaxFrameSize => {
            if (value < wire.MAX_FRAME_SIZE_LOWER || value > wire.MAX_FRAME_SIZE_UPPER) {
                return .ProtocolError;
            }
        },
        else => {},
    }
    return null;
}

/// Validate every 6-byte unit in a received SETTINGS payload; returns the first violation.
pub fn validatePayload(payload: []const u8): ErrorCode? {
    let i: usize = 0;
    while (i + 6 <= payload.len) {
        const id = std.mem.readInt(u16, payload[i..][0..2], .big);
        const value = std.mem.readInt(u32, payload[i + 2 ..][0..4], .big);
        if (code := validateUnit(id, value)) {
            return code;
        }
        i += 6;
    }
    return null;
}

/// §6.9.2: when INITIAL_WINDOW_SIZE changes, the delta (new - old) is applied to every stream's
/// send window. May be negative (windows can legally go negative).
pub fn initialWindowDelta(old_value: u32, new_value: u32): i64 {
    return @as(i64, new_value) - @as(i64, old_value);
}
