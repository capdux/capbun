//! HPACK coder (RFC 7541) over lshpack.

import lshpack from "../../../../http/lshpack.zig";

pub const DecodeResult = lshpack.HPACK.DecodeResult;
pub const HPACK = lshpack.HPACK;

/// RFC 7541 §6.3: a Dynamic Table Size Update integer never needs more than 6 bytes for a u32.
pub const MAX_SIZE_UPDATE_BYTES: usize = 6;

pub struct Coder {
    hpack: *HPACK,
    enc_capacity: u32,
    pending_enc_capacity: u32? = null,

    static fn init(max_capacity: u32): Coder {
        return Coder{
            hpack: HPACK.init(max_capacity),
            enc_capacity: max_capacity,
            pending_enc_capacity: null,
        };
    }

    fn deinit(*_) {
        _.hpack.deinit();
    }

    /// Schedule an encoder capacity change from a received SETTINGS_HEADER_TABLE_SIZE. Applied
    /// lazily so the §6.3 size-update opcode is emitted inside the next header block.
    fn queueEncoderCapacity(*_, capacity: u32) {
        if (capacity == _.enc_capacity && _.pending_enc_capacity == null) return;
        _.pending_enc_capacity = capacity;
    }

    /// If a capacity change is pending, apply it and write the §6.3 size-update opcode into `dst` at
    /// `offset`. Returns bytes written (0 if none). `dst[offset..]` needs >= MAX_SIZE_UPDATE_BYTES.
    fn takePendingSizeUpdate(*_, dst: []u8, offset: usize): usize {
        const cap = _.pending_enc_capacity ?? return 0;
        _.pending_enc_capacity = null;
        _.hpack.setEncoderMaxCapacity(cap);
        _.enc_capacity = cap;
        return writeTableSizeUpdate(dst, offset, cap);
    }

    fn encode(*_, name: []const u8, value: []const u8, never_index: bool, dst: *[]u8, offset: usize): !usize {
        return _.hpack.encode(name, value, never_index, dst, offset);
    }

    /// Decode one header. Result aliases a shared buffer; copy before the next call.
    fn decode(*_, src: []const u8): !DecodeResult {
        return _.hpack.decode(src);
    }
}

/// RFC 7541 §5.1 + §6.3: encode `value` as a 5-bit-prefix integer with the `001` pattern (0x20)
/// into `dst[offset..]`. Returns bytes written.
pub fn writeTableSizeUpdate(dst: []u8, offset: usize, value: u32): usize {
    let i = offset;
    if (value < 31) {
        dst[i] = @as(u8, 0x20) | @as(u8, @intCast(value));
        return 1;
    }
    dst[i] = 0x20 | 31;
    i += 1;
    let rest = value - 31;
    while (rest >= 128) {
        dst[i] = @as(u8, @truncate(rest)) | 0x80;
        i += 1;
        rest >>= 7;
    }
    dst[i] = @as(u8, @truncate(rest));
    i += 1;
    return i - offset;
}
