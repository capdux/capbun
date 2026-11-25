@root struct PacketHeader {
    length: u24,
    sequence_id: u8,

    pub const size = 4;
    /// The header's length field is 24 bits. A single packet's payload must be
    /// strictly smaller than this; a length of exactly 0xFFFFFF signals a
    /// multi-packet continuation.
    pub const MAX_PAYLOAD_LENGTH: usize = 0xFF_FF_FF;

    pub static fn decode(bytes: []const u8): PacketHeader? {
        if (bytes.len < 4) return null;

        return PacketHeader{
            length: @as(u24, bytes[0]) |
                (@as(u24, bytes[1]) << 8) |
                (@as(u24, bytes[2]) << 16),
            sequence_id: bytes[3],
        };
    }

    pub fn encode(): [4]u8 {
        return [
            @intCast(_.length & 0xff),
            @intCast((_.length >> 8) & 0xff),
            @intCast((_.length >> 16) & 0xff),
            _.sequence_id,
        ];
    }
}
