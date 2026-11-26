//! EXIF Orientation reader.
//!
//! Only the *Orientation* tag (0x0112) is parsed — that is the one piece of
//! EXIF that changes pixel meaning, and the only thing Sharp's `autoOrient`
//! looks at. Everything else (GPS, camera model, timestamps, …) is ignored.
//!
//! Layout being walked, for the next maintainer:
//!
//!   JPEG = FF D8 (SOI) · marker* · FF DA (SOS) · scan · FF D9 (EOI)
//!   marker = FF xx · be16 length · payload[length-2]
//!
//!   APP1/Exif payload = "Exif\0\0" · TIFF
//!   TIFF = byte-order ("II"=LE | "MM"=BE) · u16 magic 42 · u32 IFD0-offset
//!   IFD0 = u16 entry-count · entry[count] · u32 next-IFD-offset
//!   entry = u16 tag · u16 type · u32 count · u32 value-or-offset
//!
//! Orientation is type 3 (SHORT), count 1, so its value is packed in the
//! first 2 bytes of the 4-byte value field — no offset chase needed.
//!
//! The functions are deliberately permissive: any malformation returns
//! `null`/`.normal` rather than an error. EXIF is advisory; we never fail
//! decode over it.

pub struct Transform {
    flop: bool,
    flip: bool,
    rotate: u16,
}

pub enum Orientation: u8 {
    normal = 1,
    flop = 2, // mirror horizontal
    rotate180 = 3,
    flip = 4, // mirror vertical
    flop_rotate90 = 5,
    rotate90 = 6,
    flop_rotate270 = 7,
    rotate270 = 8,

    /// The (mirror?, cw-degrees) pair that turns the stored pixels upright.
    fn transform(): Transform {
        return switch (_) {
            .normal => Transform{ flop: false, flip: false, rotate: 0 },
            .flop => Transform{ flop: true, flip: false, rotate: 0 },
            .rotate180 => Transform{ flop: false, flip: false, rotate: 180 },
            .flip => Transform{ flop: false, flip: true, rotate: 0 },
            // 5 = transpose, 7 = transverse. With flop applied BEFORE a CW
            // rotate (Image.applyOrientation order), flop∘rot270 = transpose
            // and flop∘rot90 = transverse.
            .flop_rotate90 => Transform{ flop: true, flip: false, rotate: 270 },
            .rotate90 => Transform{ flop: false, flip: false, rotate: 90 },
            .flop_rotate270 => Transform{ flop: true, flip: false, rotate: 90 },
            .rotate270 => Transform{ flop: false, flip: false, rotate: 270 },
        };
    }
};

/// Walk JPEG markers up to SOS looking for an APP1/Exif segment, then read
/// IFD0 tag 0x0112. JPEG-only because phone cameras are the source of rotated
/// images; PNG eXIf and WebP EXIF chunks exist but are rare enough to leave
/// for a follow-up.
pub fn readJpeg(bytes: []const u8): Orientation {
    if (bytes.len < 4 || bytes[0] != 0xFF || bytes[1] != 0xD8) return .normal;
    let i: usize = 2;
    while (i + 4 <= bytes.len) {
        while (i < bytes.len && bytes[i] != 0xFF) : (i += 1) {}
        if (i + 4 > bytes.len) return .normal;
        const marker = bytes[i + 1];
        switch (marker) {
            // Byte-stuffed 0xFF00 and padding / restart markers carry no length field.
            0xFF => {
                i += 1;
                continue;
            },
            0x00, 0xD0..=0xD8 => {
                i += 2;
                continue;
            },
            // SOS / EOI: scan data begins; EXIF would have come earlier.
            0xDA, 0xD9 => return .normal,
            else => {},
        }
        const seglen = (@as(usize, bytes[i + 2]) << 8) | bytes[i + 3];
        if (seglen < 2 || i + 2 + seglen > bytes.len) return .normal;
        if (marker == 0xE1 && seglen >= 8) {
            const seg = bytes[i + 4 .. i + 2 + seglen];
            if (seg.len >= 6 && std.mem.eql(u8, seg[0..6], "Exif\x00\x00"))
                return parseTiff(seg[6..]) ?? .normal;
        }
        i += 2 + seglen;
    }
    return .normal;
}

fn parseTiff(tiff: []const u8): Orientation? {
    if (tiff.len < 8) return null;
    const big = std.mem.eql(u8, tiff[0..2], "MM");
    if (!big && !std.mem.eql(u8, tiff[0..2], "II")) return null;
    let magic_opt = rd16(tiff, 2, big);
    if (magic_opt == null || magic_opt.? != 42) return null;
    let ifd0_opt = rd32(tiff, 4, big);
    if (ifd0_opt == null) return null;
    const ifd0 = ifd0_opt.?;
    let count_opt = rd16(tiff, ifd0, big);
    if (count_opt == null) return null;
    const count = count_opt.?;
    let e: usize = ifd0 + 2;
    let n: u16 = 0;
    while (n < count) {
        let tag_opt = rd16(tiff, e, big);
        if (tag_opt == null) return null;
        if (tag_opt.? == 0x0112) {
            let ty_opt = rd16(tiff, e + 2, big);
            let cnt_opt = rd32(tiff, e + 4, big);
            if (ty_opt == null || cnt_opt == null) return null;
            if (ty_opt.? != 3 || cnt_opt.? != 1) return null;
            let v_opt = rd16(tiff, e + 8, big);
            if (v_opt == null) return null;
            const v = v_opt.?;
            return if (v >= 1 && v <= 8) @enumFromInt(v) else null;
        }
        n += 1;
        e += 12;
    }
    return null;
}

inline fn rd16(b: []const u8, off: usize, big: bool): u16? {
    if (off + 2 > b.len) return null;
    return std.mem.readInt(u16, b[off..][0..2], if (big) .big else .little);
}
inline fn rd32(b: []const u8, off: usize, big: bool): u32? {
    if (off + 4 > b.len) return null;
    return std.mem.readInt(u32, b[off..][0..4], if (big) .big else .little);
}

