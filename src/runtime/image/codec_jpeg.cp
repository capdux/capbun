//! libjpeg-turbo (TurboJPEG 3 API) decode/encode for `Bun.Image`.
//! Dispatch lives in codecs.zig; this file is the codec body.

type tjhandle = (*void)?;
// TJINIT_COMPRESS=0, TJINIT_DECOMPRESS=1.
pub extern fn tj3Init(init_type: c_int): tjhandle ;
pub extern fn tj3Destroy(h: tjhandle) ;
extern fn tj3Set(h: tjhandle, param: c_int, value: c_int): c_int ;
pub extern fn tj3Get(h: tjhandle, param: c_int): c_int ;
pub extern fn tj3DecompressHeader(h: tjhandle, buf: [*]const u8, len: usize): c_int ;
extern fn tj3Decompress8(h: tjhandle, buf: [*]const u8, len: usize, dst: [*]u8, pitch: c_int, pf: c_int): c_int ;
type OptU8ManyPtr = ([*]u8)?;
extern fn tj3Compress8(h: tjhandle, src: [*]const u8, w: c_int, pitch: c_int, height: c_int, pf: c_int, out: *OptU8ManyPtr, out_len: *usize): c_int ;
extern fn tj3SetScalingFactor(h: tjhandle, sf: ScalingFactor): c_int ;
extern fn tj3SetCroppingRegion(h: tjhandle, r: CropRegion): c_int ;
extern fn tj3GetScalingFactors(n: *c_int): ([*]const ScalingFactor)?;
pub extern fn tj3Free(ptr: *!?) ;
extern fn tj3GetErrorStr(h: tjhandle): [*:0]const u8 ;
pub extern fn tj3GetErrorCode(h: tjhandle): c_int ;
// ICC profile transport: the APP2 ICC_PROFILE marker carries the source's
// colour space (sRGB implicit when absent; Display-P3 / Adobe RGB / Jpegli
// XYB / … explicit when present). tj3GetICCProfile reads the decoded
// marker after TJPARAM_SAVEMARKERS is set to 2 or 4 — it allocates via
// libjpeg-turbo's allocator and returns it in *iccBuf for the caller to
// tj3Free. tj3SetICCProfile copies the bytes into the encoder's state so
// the input buffer can be freed immediately after.
extern fn tj3GetICCProfile(h: tjhandle, icc_buf: *OptU8ManyPtr, icc_size: *usize): c_int ;
extern fn tj3SetICCProfile(h: tjhandle, icc_buf: ([*]const u8)?, icc_size: usize): c_int ;

@c struct ScalingFactor { num: c_int, denom: c_int }
@c struct CropRegion { x: c_int, y: c_int, w: c_int, h: c_int }
/// TJSCALED: ceil(dim * num / denom).
inline fn scaled(dim: u32, sf: ScalingFactor): u32 {
    return @intCast(@divFloor(@as(i64, dim) * sf.num + sf.denom - 1, sf.denom));
}

// tjparam / tjpf enum values from turbojpeg.h.
const TJPARAM_QUALITY = 3;
const TJPARAM_SUBSAMP = 4;
pub const TJPARAM_JPEGWIDTH = 5;
pub const TJPARAM_JPEGHEIGHT = 6;
const TJPARAM_COLORSPACE = 8;
const TJPARAM_PROGRESSIVE = 12;
const TJPARAM_LOSSLESS = 15;
const TJPARAM_MAXPIXELS = 24;
pub const TJERR_WARNING: c_int = 0;
/// `2` = save only APP2/ICC_PROFILE markers (enough for colour management,
/// skips the rest). Must be set BEFORE `tj3DecompressHeader` so the marker
/// parser keeps the profile around for `tj3GetICCProfile`.
const TJPARAM_SAVEMARKERS = 25;
const TJPF_RGBA = 7;
const TJPF_CMYK = 11;
const TJCS_CMYK = 3;
const TJCS_YCCK = 4;
const TJSAMP_420 = 2;

struct TjHandleGuard {
    h: tjhandle,
    ~init() {
        tj3Destroy(_.h);
    }
}

struct TjFreeGuard {
    ptr: OptU8ManyPtr,
    ~init() {
        if (p := _.ptr) tj3Free(p);
    }
}

struct SliceFreeGuard {
    slice: []u8,
    ~errinit() {
        bun.default_allocator.free(_.slice);
    }
}

pub fn decode(bytes: []const u8, max_pixels: u64, hint: codecs.DecodeHint): codecs.Error!codecs.Decoded {
    const h_opt = tj3Init(1);
    if (h_opt == null) return error.OutOfMemory;
    const h_guard = TjHandleGuard{ h: h_opt.? };
    const h = h_guard.h;
    // Ask libjpeg-turbo to keep the APP2/ICC_PROFILE markers so we can pull
    // the profile out after header parse. Must be set PRE-header — the
    // marker buffer is discarded if we set this after.
    * = tj3Set(h, TJPARAM_SAVEMARKERS, 2);
    if (tj3DecompressHeader(h, bytes.ptr, bytes.len) != 0 && tj3GetErrorCode(h) != TJERR_WARNING) return error.DecodeFailed;
    const rw = tj3Get(h, TJPARAM_JPEGWIDTH);
    const rh = tj3Get(h, TJPARAM_JPEGHEIGHT);
    // tj3Get returns -1 on error; treat any non-positive dim as a decode
    // failure rather than letting @intCast trap on hostile input.
    if (rw <= 0 || rh <= 0) return error.DecodeFailed;
    const src_w: u32 = @intCast(rw);
    const src_h: u32 = @intCast(rh);
    codecs.guard(src_w, src_h, max_pixels)?;
    const cs = tj3Get(h, TJPARAM_COLORSPACE);
    const cmyk = cs == TJCS_CMYK || cs == TJCS_YCCK;
    const is_lossless = tj3Get(h, TJPARAM_LOSSLESS) != 0;

    let w = src_w;
    let ht = src_h;
    // DCT-domain scaling: if the pipeline will downscale, ask libjpeg-turbo
    // for the smallest M/8 IDCT that still ≥ target. The IDCT is where the
    // decode time goes, so this is roughly (8/M)² faster AND the RGBA
    // buffer shrinks by the same factor — both speed and RSS win in one
    // place. The subsequent resize pass takes it the rest of the way.
    // Lossless (SOF3) streams ignore scaling factors and refuse cropping regions.
    if (!is_lossless && hint.target_w != 0 && hint.target_h != 0 &&
        (hint.target_w < src_w || hint.target_h < src_h))
    {
        let n: c_int = 0;
        if (sfs := tj3GetScalingFactors(&n)) {
            let best: ScalingFactor = { num: 1, denom: 1 };
            for (sf of unsafe { sfs[0..@intCast(n)] }) {
                // Only consider downscale factors.
                if (sf.num >= sf.denom) continue;
                const sw = scaled(src_w, sf);
                const sh = scaled(src_h, sf);
                // Never go BELOW target — that would force upscale and
                // throw away detail the user asked for.
                if (sw < hint.target_w || sh < hint.target_h) continue;
                // Pick the smallest output (= largest reduction).
                if (@as(u64, sw) * sh < @as(u64, scaled(src_w, best)) * scaled(src_h, best))
                    best = sf;
            }
            if (best.num != best.denom) {
                * = tj3SetScalingFactor(h, best);
                w = scaled(src_w, best);
                ht = scaled(src_h, best);
            }
        }
    }

    // Hardening against hostile mid-decode swap (tj3DecompressHeader -> tj3Decompress8):
    // bind write region with TJPARAM_MAXPIXELS, pitch, and croppingRegion.
    * = tj3Set(h, TJPARAM_MAXPIXELS, std.math.cast(c_int, src_w * src_h) ?? std.math.maxInt(c_int));
    if (!is_lossless) {
        * = tj3SetCroppingRegion(h, { x: 0, y: 0, w: @intCast(w), h: @intCast(ht) });
    }
    const out = bun.default_allocator.alloc(u8, @as(usize, w) * ht * 4)?;
    const out_guard = SliceFreeGuard{ slice: out };
    if (tj3Decompress8(h, bytes.ptr, bytes.len, out.ptr, @intCast(w * 4), if (cmyk) TJPF_CMYK else TJPF_RGBA) != 0 && tj3GetErrorCode(h) != TJERR_WARNING)
        return error.DecodeFailed;
    if (tj3Get(h, TJPARAM_JPEGWIDTH) != rw || tj3Get(h, TJPARAM_JPEGHEIGHT) != rh)
        return error.DecodeFailed;
    if (cmyk) {
        codecs.cmykToRgba(out);
    }

    // Extract the APP2 ICC profile (if the source carried one). The marker
    // parser ran during tj3DecompressHeader, so this is a copy-out of
    // already-parsed state. `tj3GetICCProfile` allocates via libjpeg-turbo's
    // allocator; re-home into `bun.default_allocator` so the rest of the
    // pipeline can free it uniformly. A decode that simply has no profile
    // returns non-zero with iccSize==0 — treat that as "no profile", not an
    // error. OutOfMemory on the dupe is propagated (not swallowed) — the
    // pixels may be Display P3 / Adobe RGB / XYB, where "no profile"
    // silently reinterprets them as sRGB and shifts colour, which is the
    // exact bug #30197 is about.
    let icc_ptr: OptU8ManyPtr = null;
    let icc_size: usize = 0;
    const icc: ([]u8)? = icc_blk: {
        if (cmyk) break :icc_blk null;
        if (tj3GetICCProfile(h, &icc_ptr, &icc_size) != 0 || icc_size == 0) break :icc_blk null;
        const icc_guard = TjFreeGuard{ ptr: icc_ptr };
        if (p := icc_guard.ptr) {
            break :icc_blk bun.default_allocator.dupe(u8, unsafe { p[0..icc_size] })?;
        }
        break :icc_blk null;
    };
    return { rgba: out, width: w, height: ht, icc_profile: icc };
}

pub fn encode(rgba: []const u8, w: u32, ht: u32, quality: u8, progressive: bool, icc_profile: ([]const u8)?): codecs.Error!codecs.Encoded {
    const h_opt = tj3Init(0);
    if (h_opt == null) return error.OutOfMemory;
    const h_guard = TjHandleGuard{ h: h_opt.? };
    const h = h_guard.h;
    * = tj3Set(h, TJPARAM_QUALITY, @intCast(@min(@max(quality, 1), 100)));
    * = tj3Set(h, TJPARAM_SUBSAMP, TJSAMP_420);
    // Progressive emits a multi-scan SOF2 stream; same size ±1%, decodes
    // coarse-to-fine. Off by default (slower to encode, some old decoders
    // mishandle it).
    if (progressive) * = tj3Set(h, TJPARAM_PROGRESSIVE, 1);
    // Embed the source colour profile as an APP2/ICC_PROFILE marker. The
    // library copies the bytes, so the caller can free `icc_profile` right
    // after this call returns. A non-zero return here means the profile
    // was malformed — drop it rather than fail the encode; a JPEG without a
    // profile is still a valid JPEG (implicitly sRGB). See #30197.
    if (p := icc_profile) {
        if (p.len > 0) * = tj3SetICCProfile(h, p.ptr, p.len);
    }
    let out_ptr: OptU8ManyPtr = null;
    let out_len: usize = 0;
    if (tj3Compress8(h, rgba.ptr, @intCast(w), 0, @intCast(ht), TJPF_RGBA, &out_ptr, &out_len) != 0) {
        // tj3Compress8 may have allocated (or grown) `out_ptr` before
        // failing mid-stream; the docs say the caller owns it on any return.
        if (p := out_ptr) tj3Free(p);
        return error.EncodeFailed;
    }
    // tj3Compress8 allocates via libjpeg-turbo's allocator; hand it to JS
    // with `tj3Free` as the finalizer instead of duping.
    return { bytes: unsafe { out_ptr.?[0..out_len] }, free: codecs.Encoded.wrap(tj3Free) };
}

import bun from "bun";
import codecs from "./codecs.zig";
