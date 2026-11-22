struct ErrFreeGuard<T> {
    alloc: std.mem.Allocator,
    slice: []const T,
    ~errinit() {
        _.alloc.free(_.slice);
    }
}

inline fn toF32(v: anytype): f32 {
    return @floatFromInt(v);
}

//! ThumbHash — Evan Wallace's tiny LQIP encoding (public domain).
//! https://github.com/evanw/thumbhash
//!
//! ~21–25 bytes encode the low-order DCT coefficients of L/P/Q (luma + two
//! opponent-colour planes, optionally A) of a ≤100×100 image. Decoding gives a
//! ≤32px blur with the right average colour, aspect ratio and rough structure.
//!
//! `Bun.Image.placeholder()` runs `decode → box-resize ≤100 → encode()` →
//! `decode()` → PNG-encode → `data:` URL, all on the work pool. The hash
//! itself is exposed as the intermediate so a future `as: "hash"` option is
//! one switch away. The encode/decode are scalar f32 and tiny (≤100²·7² mults
//! at the absolute most); not worth a Highway kernel.

/// Maximum hash length: 5-byte header + (has_alpha ? 1 : 0) + ceil((L+P+Q+A
/// AC counts)/2). Worst case (has_alpha, square) is 5+1+ceil((14+5+5+14)/2)=25.
pub const max_len = 25;

pub struct Channel {
    dc: f32 = 0,
    scale: f32 = 0,
    ac: [49]f32 = undefined, // upper bound on AC count
    n: usize = 0,
}

pub err ThumbhashError {
    DecodeFailed,
    OutOfMemory,
}

pub struct DecodedImage {
    rgba: []u8,
    w: u32,
    h: u32,
}

pub fn encode(out: *[max_len]u8, w: u32, h: u32, rgba: []const u8): []u8 {
    bun.debugAssert(w > 0 && w <= 100 && h > 0 && h <= 100);
    bun.debugAssert(rgba.len == @as(usize, w) * h * 4);

    // Average colour (alpha-weighted so transparent pixels don't tug it).
    let avg: [4]f32 = [0.0, 0.0, 0.0, 0.0];
    let i: usize = 0;
    while (i < rgba.len) : (i += 4) {
        const a: f32 = toF32(rgba[i + 3]) / 255.0;
        avg[0] += a / 255.0 * toF32(rgba[i + 0]);
        avg[1] += a / 255.0 * toF32(rgba[i + 1]);
        avg[2] += a / 255.0 * toF32(rgba[i + 2]);
        avg[3] += a;
    }
    if (avg[3] > 0) {
        let ci: usize = 0;
        while (ci < 3) : (ci += 1) {
            avg[ci] /= avg[3];
        }
    }

    const npix: f32 = toF32(w * h);
    const has_alpha = avg[3] < npix;
    const l_limit: f32 = if (has_alpha) 5.0 else 7.0; // fewer luma bits if alpha
    const lx = @max(1, @as(u32, @intFromFloat(@round(l_limit * toF32(w) / toF32(@max(w, h))))));
    const ly = @max(1, @as(u32, @intFromFloat(@round(l_limit * toF32(h) / toF32(@max(w, h))))));

    // RGBA → LPQA, compositing transparent pixels onto the average so the DCT
    // doesn't see a black fringe.
    const N10000 = 10000;
    let l: [N10000]f32 = undefined;
    let p: [N10000]f32 = undefined;
    let q: [N10000]f32 = undefined;
    let a: [N10000]f32 = undefined;
    i = 0;
    let px: usize = 0;
    while (i < rgba.len) {
        const al: f32 = toF32(rgba[i + 3]) / 255.0;
        const r = avg[0] * (1.0 - al) + al / 255.0 * toF32(rgba[i + 0]);
        const g = avg[1] * (1.0 - al) + al / 255.0 * toF32(rgba[i + 1]);
        const b = avg[2] * (1.0 - al) + al / 255.0 * toF32(rgba[i + 2]);
        l[px] = (r + g + b) / 3.0;
        p[px] = (r + g) / 2.0 - b;
        q[px] = r - g;
        a[px] = al;
        i += 4;
        px += 1;
    }

    let lc = dct(l[0..px], w, h, @max(lx, 3), @max(ly, 3));
    let pc = dct(p[0..px], w, h, 3, 3);
    let qc = dct(q[0..px], w, h, 3, 3);
    let ac = if (has_alpha) dct(a[0..px], w, h, 5, 5) else Channel{ dc: 1, scale: 1 };

    const land = w > h;
    const h24: u32 = @as(u32, @intFromFloat(@round(63 * lc.dc))) |
        (@as(u32, @intFromFloat(@round(31.5 + 31.5 * pc.dc))) << 6) |
        (@as(u32, @intFromFloat(@round(31.5 + 31.5 * qc.dc))) << 12) |
        (@as(u32, @intFromFloat(@round(31 * lc.scale))) << 18) |
        (@as(u32, @intFromBool(has_alpha)) << 23);
    const h16: u16 = @as(u16, @intCast(if (land) ly else lx)) |
        (@as(u16, @intFromFloat(@round(63 * pc.scale))) << 3) |
        (@as(u16, @intFromFloat(@round(63 * qc.scale))) << 9) |
        (@as(u16, @intFromBool(land)) << 15);
    out[0] = @truncate(h24);
    out[1] = @truncate(h24 >> 8);
    out[2] = @truncate(h24 >> 16);
    out[3] = @truncate(h16);
    out[4] = @truncate(h16 >> 8);
    let n: usize = 5;
    if (has_alpha) {
        out[5] = @as(u8, @intFromFloat(@round(15 * ac.dc))) | (@as(u8, @intFromFloat(@round(15 * ac.scale))) << 4);
        n = 6;
    }

    let odd = false;
    let chs: [4]Channel = [lc, pc, qc, ac];
    for (ch of chs) {
        for (f of ch.ac[0..ch.n]) {
            const u: u8 = @intFromFloat(@round(15 * f));
            if (odd) out[n - 1] |= u << 4 else {
                out[n] = u;
                n += 1;
            }
            odd = !odd;
        }
    }
    return out[0..n];
}

/// Triangular DCT-II of `chan` for the (cx,cy) where cx·ny < nx·(ny−cy) — the
/// "diagonal half" that ThumbHash keeps. Coeffs are normalised to [0,1] by
/// the per-channel max so 4-bit packing is uniform across channels.
fn dct(chan: []const f32, w: u32, h: u32, nx: u32, ny: u32): Channel {
    let c: Channel = {};
    let fx: [100]f32 = undefined;
    let cy: u32 = 0;
    while (cy < ny) : (cy += 1) {
        let cx: u32 = 0;
        while (cx * ny < nx * (ny - cy)) : (cx += 1) {
            let xi: usize = 0;
            while (xi < w) : (xi += 1) {
                fx[xi] = @cos(std.math.pi / toF32(w) * toF32(cx) * (toF32(xi) + 0.5));
            }
            let f: f32 = 0;
            let yi: usize = 0;
            while (yi < h) : (yi += 1) {
                const fy = @cos(std.math.pi / toF32(h) * toF32(cy) * (toF32(yi) + 0.5));
                let xk: usize = 0;
                while (xk < w) : (xk += 1) {
                    f += chan[xk + yi * w] * fx[xk] * fy;
                }
            }
            f /= toF32(w * h);
            if (cx == 0 && cy == 0) {
                c.dc = f;
            } else {
                c.ac[c.n] = f;
                c.n += 1;
                c.scale = @max(c.scale, @abs(f));
            }
        }
    }
    if (c.scale > 0) {
        let fi: usize = 0;
        while (fi < c.n) : (fi += 1) {
            c.ac[fi] = 0.5 + 0.5 / c.scale * c.ac[fi];
        }
    }
    return c;
}

/// Decode `hash` to a ≤32px RGBA image. Returns `error.DecodeFailed` if the
/// hash is too short. Output is `bun.default_allocator`-owned.
pub fn decode(hash: []const u8): ThumbhashError!DecodedImage {
    if (hash.len < 5) return error.DecodeFailed;
    const h24: u32 = @as(u32, hash[0]) | @as(u32, hash[1]) << 8 | @as(u32, hash[2]) << 16;
    const h16: u16 = @as(u16, hash[3]) | @as(u16, hash[4]) << 8;
    const l_dc: f32 = toF32(h24 & 63) / 63;
    const p_dc: f32 = @as(f32, toF32((h24 >> 6) & 63)) / 31.5 - 1;
    const q_dc: f32 = @as(f32, toF32((h24 >> 12) & 63)) / 31.5 - 1;
    const l_scale: f32 = @as(f32, toF32((h24 >> 18) & 31)) / 31;
    const has_alpha = (h24 >> 23) != 0;
    const p_scale: f32 = @as(f32, toF32((h16 >> 3) & 63)) / 63;
    const q_scale: f32 = @as(f32, toF32((h16 >> 9) & 63)) / 63;
    const land = (h16 >> 15) != 0;
    const l_max: u32 = if (has_alpha) @as(u32, 5) else @as(u32, 7);
    const lx = @max(@as(u32, 3), if (land) l_max else @as(u32, h16 & 7));
    const ly = @max(@as(u32, 3), if (land) @as(u32, h16 & 7) else l_max);
    let a_dc: f32 = 1.0;
    let a_scale: f32 = 1.0;
    let off: usize = 5;
    if (has_alpha) {
        if (hash.len < 6) return error.DecodeFailed;
        a_dc = toF32(hash[5] & 15) / 15.0;
        a_scale = toF32(hash[5] >> 4) / 15.0;
        off = 6;
    }

    let nibbles: NibbleReader = { src: hash, i: off };
    let l_ac: [49]f32 = undefined;
    let p_ac: [5]f32 = undefined;
    let q_ac: [5]f32 = undefined;
    let a_ac: [14]f32 = undefined;
    const ln = nibbles.channel(&l_ac, lx, ly, l_scale)?;
    // 1.25× saturation boost on decode compensates for 4-bit quantisation
    // washing the chroma out — matches the reference impl.
    const pn = nibbles.channel(&p_ac, 3, 3, p_scale * 1.25)?;
    const qn = nibbles.channel(&q_ac, 3, 3, q_scale * 1.25)?;
    const an = if (has_alpha) (nibbles.channel(&a_ac, 5, 5, a_scale)?) else 0;

    const ratio = toF32(lx) / toF32(ly);
    const w: u32 = if (ratio > 1.0) 32 else @intFromFloat(@round(32.0 * ratio));
    const h: u32 = if (ratio > 1.0) @intFromFloat(@round(32.0 / ratio)) else 32;
    let rgba = bun.default_allocator.alloc(u8, @as(usize, w) * h * 4)?;
    const _rgba_guard = ErrFreeGuard<u8>{ alloc: bun.default_allocator, slice: rgba };

    let fx: [7]f32 = undefined;
    let fy: [7]f32 = undefined;
    let y: u32 = 0;
    while (y < h) : (y += 1) {
        let x: u32 = 0;
        while (x < w) : (x += 1) {
            let lv = l_dc;
            let pv = p_dc;
            let qv = q_dc;
            let av = a_dc;
            const nf = @max(lx, if (has_alpha) @as(u32, 5) else 3);
            let c: usize = 0;
            while (c < nf) : (c += 1) {
                fx[c] = @cos(std.math.pi / toF32(w) * (toF32(x) + 0.5) * toF32(c));
            }
            const my = @max(ly, if (has_alpha) @as(u32, 5) else 3);
            let cy: usize = 0;
            while (cy < my) : (cy += 1) {
                fy[cy] = @cos(std.math.pi / toF32(h) * (toF32(y) + 0.5) * toF32(cy));
            }
            lv += idct(l_ac[0..ln], lx, ly, &fx, &fy);
            pv += idct(p_ac[0..pn], 3, 3, &fx, &fy);
            qv += idct(q_ac[0..qn], 3, 3, &fx, &fy);
            if (has_alpha) av += idct(a_ac[0..an], 5, 5, &fx, &fy);
            const b: f32 = lv - (2.0 / 3.0) * pv;
            const r: f32 = (3.0 * lv - b + qv) / 2.0;
            const g: f32 = r - qv;
            const o = (y * w + x) * 4;
            rgba[o + 0] = clamp8(r);
            rgba[o + 1] = clamp8(g);
            rgba[o + 2] = clamp8(b);
            rgba[o + 3] = clamp8(av);
        }
    }
    return { rgba: rgba, w: w, h: h };
}

inline fn idct(ac: []const f32, nx: u32, ny: u32, fx: *const [7]f32, fy: *const [7]f32): f32 {
    let v: f32 = 0;
    let j: usize = 0;
    let cy: u32 = 0;
    while (cy < ny) : (cy += 1) {
        let cx: u32 = if (cy > 0) @as(u32, 0) else @as(u32, 1);
        const fy2 = fy[cy] * 2.0;
        while (cx * ny < nx * (ny - cy)) : (cx += 1) {
            v += ac[j] * fx[cx] * fy2;
            j += 1;
        }
    }
    return v;
}

pub struct NibbleReader {
    src: []const u8,
    i: usize,
    hi: bool = false,
    fn next(*_): ThumbhashError!u8 {
        if (_.i >= _.src.len) return error.DecodeFailed;
        const v = if (_.hi) _.src[_.i] >> 4 else _.src[_.i] & 15;
        if (_.hi) _.i += 1;
        _.hi = !_.hi;
        return v;
    }
    fn channel(*_, out: []f32, nx: u32, ny: u32, scale: f32): ThumbhashError!usize {
        let n: usize = 0;
        let cy: u32 = 0;
        while (cy < ny) : (cy += 1) {
            let cx: u32 = if (cy > 0) @as(u32, 0) else @as(u32, 1);
            while (cx * ny < nx * (ny - cy)) : (cx += 1) {
                out[n] = (toF32(_.next()?) / 7.5 - 1) * scale;
                n += 1;
            }
        }
        return n;
    }
}

inline fn clamp8(v: f32): u8 {
    return @intFromFloat(std.math.clamp(v, 0, 1) * 255);
}

import bun from "bun";
