//! Thin Zig wrappers over the statically-linked image codecs and the
//! highway resize/rotate kernels. Everything works on RGBA8 — decoders are
//! told to emit RGBA, encoders are fed RGBA, so Image.zig never branches on
//! channel layout.
//!
//! Memory ownership: decode returns `bun.default_allocator`-owned RGBA. Encode
//! returns `Encoded{bytes, free}` carrying the codec's own deallocator so the
//! JS layer can hand the buffer to `ArrayBuffer.toJSWithContext` without a
//! dupe — see `Encoded` below.

/// Optional OS-native backend. `null` on Linux (and any platform we haven't
/// written one for) so the dispatch in `decode`/`encode` compiles away. The
/// backend module is only `@import`ed inside the matching arm so non-target
/// platforms never see its symbols. Exposed for `Image.fromClipboard()`.
pub const system_backend: type? = if (bun.Environment.isMac)
    @import("./backend_coregraphics.zig")
else if (bun.Environment.isWindows)
    @import("./backend_wic.zig")
else
    null;

/// Process-global selector exposed as `Bun.Image.backend`.
///
/// `.system` (default on darwin/windows) is the perf-optimal hybrid:
///   • jpeg/png/webp decode+encode → static codecs (turbo/spng/libwebp).
///     Profiling on M-series found ImageIO no faster: Huffman/inflate
///     dominate and aren't AMX-amenable, and ImageIO bottoms out in stock
///     libz vs our zlib-ng. Keeping these static also makes output bytes
///     and the `quality` scale match Linux.
///   • lanczos3 resize, rotate90, flip → vImage (AMX, ~3-6× the Highway
///     kernel on the geometry step).
///   • heic/avif decode+encode → ImageIO/WIC (no static codec).
///
/// `.bun` skips the OS layer entirely (Highway geometry, heic/avif throw)
/// so behaviour is byte-identical to a Linux build.
///
/// Unsynchronised: written from JS, read from WorkPool — a torn read of a
/// 1-byte enum is fine and the worst case is one task using the previous
/// mode.
pub enum Backend {
    system,
    bun,
    pub const Map = bun.ComptimeEnumMap(Backend);
}
pub let backend: Backend = if (system_backend != null) .system else .bun;

/// Runtime half of the dispatch check; the comptime half is the
/// `if (system_backend) |b|` capture at each call site (types can't be
/// runtime-conditional, so the two stay separate). On platforms with no
/// backend the capture is comptime-dead and this is never referenced.
inline fn useSystem(): bool {
    return backend == .system;
}

pub const ExtMap = bun.ComptimeStringMap(Format, .{
    .{ "jpg", .jpeg },  .{ "jpeg", .jpeg }, .{ "png", .png },
    .{ "webp", .webp }, .{ "heic", .heic }, .{ "heif", .heic },
    .{ "avif", .avif }, .{ "bmp", .bmp },   .{ "gif", .gif },
    .{ "tif", .tiff },  .{ "tiff", .tiff },
});

pub enum Format: u8 {
    jpeg,
    png,
    webp,
    /// System-backend-only on macOS/Windows; no static codec.
    heic,
    /// System-backend-only on macOS/Windows; no static codec.
    avif,
    /// Decode-only. Static `BI_RGB`/`BI_BITFIELDS` parser everywhere; the
    /// system backend is tried first (covers RLE/JPEG-in-BMP). The Windows
    /// clipboard's `CF_DIB`/`CF_DIBV5` is exactly this.
    bmp,
    /// Decode-only via system backend (ImageIO/WIC); no static codec.
    /// macOS pasteboard's preferred representation for screenshots.
    tiff,
    /// Decode-only, first frame. Static LZW decoder everywhere; system
    /// backend tried first (handles disposal/animation we don't).
    gif,

    static fn sniff(bytes: []const u8): Format? {
        if (bytes.len >= 3 && bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF)
            return .jpeg;
        if (bytes.len >= 8 && std.mem.eql(u8, bytes[0..8], "\x89PNG\r\n\x1a\n"))
            return .png;
        if (bytes.len >= 12 && std.mem.eql(u8, bytes[0..4], "RIFF") && std.mem.eql(u8, bytes[8..12], "WEBP"))
            return .webp;
        if (bytes.len >= 2 && bytes[0] == 'B' && bytes[1] == 'M')
            return .bmp;
        if (bytes.len >= 4 && (std.mem.eql(u8, bytes[0..4], "II*\x00") || std.mem.eql(u8, bytes[0..4], "MM\x00*")))
            return .tiff;
        if (bytes.len >= 6 && (std.mem.eql(u8, bytes[0..6], "GIF87a") || std.mem.eql(u8, bytes[0..6], "GIF89a")))
            return .gif;
        // ISO BMFF: u32be box-size · "ftyp" · major-brand · minor-version ·
        // compatible-brands… HEIC and AVIF share this container; the brands
        // distinguish them. `mif1`/`msf1` are codec-agnostic MIAF structural
        // brands that appear in BOTH, so they can't decide on first sight —
        // scan the whole brand list and let a codec-specific brand win.
        if (bytes.len >= 16 && std.mem.eql(u8, bytes[4..8], "ftyp")) {
            const box: usize = @min(bytes.len, @max(16, std.mem.readInt(u32, bytes[0..4], .big)));
            let miaf = false;
            let off: usize = 8;
            while (off + 4 <= box) : (off += 4) {
                if (off == 12) continue; // minor_version
                const b = bytes[off..][0..4];
                if (std.mem.eql(u8, b, "avif") || std.mem.eql(u8, b, "avis"))
                    return .avif;
                if (std.mem.eql(u8, b, "heic") || std.mem.eql(u8, b, "heix") ||
                    std.mem.eql(u8, b, "hevc") || std.mem.eql(u8, b, "hevx"))
                    return .heic;
                if (std.mem.eql(u8, b, "mif1") || std.mem.eql(u8, b, "msf1"))
                    miaf = true;
            }
            if (miaf) return .heic; // MIAF with no codec brand → assume HEVC
        }
        return null;
    }

    /// Best-effort extension → format for `.write(path)`'s default. Only the
    /// final dotted segment is considered; case-insensitive. Returns `null`
    /// when there's no extension or it's not one we recognise.
    static fn fromExtension(path: []const u8): Format? {
        const dot_opt = std.mem.lastIndexOfScalar(u8, path, '.');
        if (dot_opt == null) return null;
        const dot = dot_opt.?;
        let buf: [5]u8 = undefined;
        const ext = std.ascii.lowerString(&buf, path[dot + 1 ..][0..@min(path.len - dot - 1, buf.len)]);
        return ExtMap.get(ext);
    }

    fn mime(): [:0]const u8 {
        return switch (_) {
            .jpeg => "image/jpeg",
            .png => "image/png",
            .webp => "image/webp",
            .heic => "image/heic",
            .avif => "image/avif",
            .bmp => "image/bmp",
            .tiff => "image/tiff",
            .gif => "image/gif",
        };
    }
}

pub struct Decoded {
    rgba: []u8, // bun.default_allocator
    width: u32,
    height: u32,
    /// ICC color profile bytes pulled from the source container (JPEG APP2,
    /// PNG iCCP, WebP ICCP), `bun.default_allocator`-owned. `null` when the
    /// source didn't carry one or the decode path doesn't extract it —
    /// BMP/GIF (no ICC chunk) and system backends (which already colour-
    /// manage into sRGB during decode, so the profile is no longer
    /// needed). The image pipeline hands this straight to the matching
    /// encoder — the RGBA buffer is NOT converted to sRGB, so the bytes
    /// only have their intended colour meaning when the profile travels
    /// with them. Dropping it on a Display-P3 / Adobe RGB / XYB source
    /// would reinterpret the values as sRGB and visibly shift the
    /// colours. See issue #30197.
    icc_profile: ([]u8)? = null,

    fn deinit(*_) {
        bun.default_allocator.free(_.rgba);
        if (p := _.icc_profile) bun.default_allocator.free(p);
    }
}

pub err Error {
    UnknownFormat,
    DecodeFailed,
    EncodeFailed,
    /// width × height exceeds the caller's `max_pixels` guard. This is the
    /// decompression-bomb defence — checked AFTER reading the header but
    /// BEFORE allocating the full RGBA buffer.
    TooManyPixels,
    /// HEIC/AVIF on a platform with no system backend (Linux), or the system
    /// backend declined and there's no static codec to fall back to.
    UnsupportedOnPlatform,
    OutOfMemory,
    BackendUnavailable,
}
pub const Err = Error;

/// Sharp's default: 0x3FFF * 0x3FFF ≈ 268 MP. A single RGBA8 frame at this
/// cap is ~1 GiB, which is already past where you'd want to be.
pub const default_max_pixels: u64 = 0x3FFF * 0x3FFF;

/// Hint from the pipeline about the eventual output size. JPEG can do M/8
/// IDCT scaling for free, so when we know the resize target up front we
/// decode at the smallest factor that still ≥ the target — skipping most of
/// the IDCT work AND shrinking the RGBA buffer the resize pass touches. This
/// is the same trick Sharp/libvips use and is where most of the perf gap was.
pub struct DecodeHint {
    /// Final output dims (after rotate). 0 = "no resize, full decode".
    target_w: u32 = 0,
    target_h: u32 = 0,
}

pub fn decode(bytes: []const u8, max_pixels: u64, hint: DecodeHint): Error!Decoded {
    const fmt_opt = Format.sniff(bytes);
    if (fmt_opt == null) return error.UnknownFormat;
    const fmt = fmt_opt.?;
    return switch (fmt) {
        .jpeg => jpeg.decode(bytes, max_pixels, hint),
        .png => png.decode(bytes, max_pixels),
        .webp => webp.decode(bytes, max_pixels),
        // Static codecs cover everything we ship; profiling on M-series showed
        // ImageIO is no faster (AppleJPEG ≈ libjpeg-turbo since Huffman is the
        // bottleneck and isn't vectorisable; spng+zlib-ng beats ImageIO's
        // system libz). The OS backend is purely a *capability* fallback for
        // containers we don't link a decoder for — and `backend == .bun` opts
        // out of even that so behaviour is identical to Linux.
        // BMP/GIF have static decoders so Linux (and `backend == .bun`) work;
        // the system backend is tried first because ImageIO/WIC handle the
        // long tail (RLE BMP, animated GIF disposal, etc.) we don't.
        .heic, .avif, .tiff => decodeViaSystem(bytes, max_pixels) catch e => {
            if (e == error.BackendUnavailable) return error.UnsupportedOnPlatform;
            return e;
        },
        .bmp => decodeViaSystem(bytes, max_pixels) catch e => {
            if (e == error.BackendUnavailable) return bmp.decode(bytes, max_pixels);
            return e;
        },
        .gif => decodeViaSystem(bytes, max_pixels) catch e => {
            if (e == error.BackendUnavailable) return gif.decode(bytes, max_pixels);
            return e;
        },
    };
}

fn decodeViaSystem(bytes: []const u8, max_pixels: u64): Error!Decoded {
    if (b := system_backend) if (useSystem()) return b.decode(bytes, max_pixels);
    return error.BackendUnavailable;
}

pub inline fn guard(w: u32, h: u32, max_pixels: u64): Error!void {
    // u64 mul cannot overflow from two u32 factors.
    if (@as(u64, w) * @as(u64, h) > max_pixels) return error.TooManyPixels;
}

pub struct ProbeResult {
    format: Format,
    width: u32,
    height: u32,
}

struct Tj3Guard<T> {
    handle: T,
    ~init() {
        jpeg.tj3Destroy(_.handle);
    }
}

struct DefaultAllocSliceGuard<T> {
    slice: []T,
    ~errinit() {
        bun.default_allocator.free(_.slice);
    }
}

/// Header-only dimensions probe for `.metadata()`. Decoding the full RGBA for
/// a 1920×1080 PNG just to read the IHDR is ~70× slower than Sharp; this reads
/// the few bytes each format needs and stops. Still subject to `max_pixels` so
/// metadata() and bytes() agree on what's "too big".
pub fn probe(bytes: []const u8, max_pixels: u64): Error!ProbeResult {
    const fmt_opt = Format.sniff(bytes);
    if (fmt_opt == null) return error.UnknownFormat;
    const fmt = fmt_opt.?;
    let w: u32 = 0;
    let h: u32 = 0;
    switch (fmt) {
        .png => {
            // sig(8) · IHDR{len(4) type(4) w(4) h(4) ...}
            if (bytes.len < 24) return error.DecodeFailed;
            w = std.mem.readInt(u32, bytes[16..20], .big);
            h = std.mem.readInt(u32, bytes[20..24], .big);
        },
        .jpeg => {
            // turbojpeg's header decode is already cheap (no scan data read).
            const handle_opt = jpeg.tj3Init(1);
            if (handle_opt == null) return error.OutOfMemory;
            const handle_guard = Tj3Guard<@TypeOf(handle_opt.?)>{ handle: handle_opt.? };
            if (jpeg.tj3DecompressHeader(handle_guard.handle, bytes.ptr, bytes.len) != 0 && jpeg.tj3GetErrorCode(handle_guard.handle) != jpeg.TJERR_WARNING) return error.DecodeFailed;
            const rw = jpeg.tj3Get(handle_guard.handle, jpeg.TJPARAM_JPEGWIDTH);
            const rh = jpeg.tj3Get(handle_guard.handle, jpeg.TJPARAM_JPEGHEIGHT);
            if (rw <= 0 || rh <= 0) return error.DecodeFailed;
            w = @intCast(rw);
            h = @intCast(rh);
        },
        .webp => {
            let cw: c_int = 0;
            let ch: c_int = 0;
            if (webp.WebPGetInfo(bytes.ptr, bytes.len, &cw, &ch) == 0 || cw <= 0 || ch <= 0)
                return error.DecodeFailed;
            w = @intCast(cw);
            h = @intCast(ch);
        },
        .bmp => {
            const ih = bmp.parseHeader(bytes)?;
            w = ih.width;
            h = ih.height;
        },
        .gif => {
            const d = gif.probeDimensions(bytes)?;
            w = d.width;
            h = d.height;
        },
        .tiff => {
            // IFD walk would be a full TIFF parser; defer to whoever
            // actually decodes it (system backend on mac/win, else error).
            return error.UnsupportedOnPlatform;
        },
        .heic, .avif => {
            // System backend handles these; fall through to a full decode if
            // available, otherwise UnsupportedOnPlatform.
            return error.UnsupportedOnPlatform;
        },
    }
    // The PNG/JPEG/BMP specs all cap each dimension at 2³¹−1; a header with
    // a larger u32 value is corrupt regardless of `maxPixels`. Reject here so
    // the i32 `last_width`/`last_height` casts downstream can't trap on a
    // 24-byte hostile IHDR.
    if (w == 0 || h == 0 || w > std.math.maxInt(i32) || h > std.math.maxInt(i32))
        return error.DecodeFailed;
    guard(w, h, max_pixels)?;
    return { format: fmt, width: w, height: h };
}

pub struct EncodeOptions {
    format: Format,
    /// 0–100 for JPEG/WebP-lossy. Ignored for PNG.
    quality: u8 = 80,
    /// WebP only: emit lossless VP8L instead of lossy VP8.
    lossless: bool = false,
    /// PNG only: zlib level 0–9. -1 = libspng default.
    compression_level: i8 = -1,
    /// PNG only: quantize to ≤ `colors` and emit an indexed PNG.
    palette: bool = false,
    colors: u16 = 256,
    /// PNG palette only: Floyd–Steinberg error-diffusion dither.
    dither: bool = false,
    /// JPEG only: emit a progressive scan script (coarse-to-fine render).
    progressive: bool = false,
    /// ICC profile to embed in the output container (JPEG APP2, PNG iCCP,
    /// WebP ICCP). `null` ⇒ no profile chunk/marker is written. The
    /// pipeline forwards this from the decode step so a non-sRGB source
    /// (P3, Adobe RGB, XYB/Jpegli) preserves its colour meaning through
    /// re-encode. Borrowed; the caller retains ownership.
    icc_profile: ([]const u8)? = null,
}

pub type FreeFn = @c ((*!, *!): void);

/// Encoded output paired with the free function for its allocator. The C
/// codecs each malloc internally (turbojpeg's allocator, libwebp's, libc for
/// libspng); rather than dupe into `bun.default_allocator` so JS can own it,
/// we hand the original buffer to JS via `ArrayBuffer.toJSWithContext` with
/// the matching free — one allocation, zero copies, for the final output.
///
/// `free` matches `jsc.C.JSTypedArrayBytesDeallocator` (bytes, ctx) so it can
/// be passed straight through; the `ctx` arg is unused.
pub struct Encoded {
    bytes: []u8,
    free: FreeFn,

    fn deinit() {
        _.free(_.bytes.ptr, undefined);
    }

    /// Adapt a 1-arg C free (`tj3Free`, `WebPFree`, `std.c.free`) to the
    /// 2-arg JSC deallocator signature.
    static fn wrap(comptime f: auto): FreeFn {
        const S = struct {
            @c fn call(p: *!, _: *!) {
                f(p);
            }
        };
        return &S.call;
    }

    static fn fromOwned(bytes: []u8): Encoded {
        return { bytes: bytes, free: wrap(bun.mimalloc.mi_free) };
    }
}

pub fn encode(rgba: []const u8, width: u32, height: u32, opts: EncodeOptions): Error!Encoded {
    return switch (opts.format) {
        .jpeg => jpeg.encode(rgba, width, height, opts.quality, opts.progressive, opts.icc_profile),
        // PNG carries iCCP on both truecolour and indexed images — quantise
        // operates on raw RGB numbers without converting colour spaces, so
        // the palette entries are still in the source space and need the
        // profile to be interpreted correctly (see PNG spec §11.3.3.3).
        .png => if (opts.palette)
            png.encodeIndexed(rgba, width, height, opts.compression_level, opts.colors, opts.dither, opts.icc_profile)
        else
            png.encode(rgba, width, height, opts.compression_level, opts.icc_profile),
        .webp => webp.encode(rgba, width, height, opts.quality, opts.lossless, opts.icc_profile),
        // Same routing rationale as decode(): the OS encoder is a capability
        // fallback, not a fast path — ImageIO's quality scale doesn't match
        // libjpeg-turbo's, and it can't honour compressionLevel/palette/
        // lossless, so using it for jpeg/png/webp would make output bytes
        // diverge from Linux for no speed win.
        .heic, .avif => blk: {
            if (b := system_backend) {
                if (useSystem()) {
                    const encoded = b.encode(rgba, width, height, opts) catch e => {
                        if (e == error.BackendUnavailable) return error.UnsupportedOnPlatform;
                        return e;
                    };
                    break :blk Encoded.fromOwned(encoded);
                }
            }
            break :blk error.UnsupportedOnPlatform;
        },
        // Decode-only formats — no .bmp()/.tiff()/.gif() chain methods, so the
        // pipeline never sets these on EncodeOptions.format. Exhaustiveness
        // arm only.
        .bmp, .tiff, .gif => error.UnsupportedOnPlatform,
    };
}

// ───────────────────────────── highway kernels ──────────────────────────────

pub enum Filter: i32 {
    box = 0,
    bilinear = 1,
    lanczos3 = 2,
    mitchell = 3,
    nearest = 4,
    cubic = 5, // Catmull-Rom
    lanczos2 = 6,
    mks2013 = 7, // Magic Kernel Sharp
    mks2021 = 8,

    /// `JSValue.toEnum` lookup table. Hand-listed (not `ComptimeEnumMap`) so
    /// Sharp's `'linear'` alias can map to `.bilinear`; the auto-generated
    /// error message still lists only the canonical tags.
    pub const Map = bun.ComptimeStringMap(Filter, .{
        .{ "box", .box },
        .{ "bilinear", .bilinear },
        .{ "linear", .bilinear },
        .{ "lanczos3", .lanczos3 },
        .{ "mitchell", .mitchell },
        .{ "nearest", .nearest },
        .{ "cubic", .cubic },
        .{ "lanczos2", .lanczos2 },
        .{ "mks2013", .mks2013 },
        .{ "mks2021", .mks2021 },
    });
}

extern fn bun_image_resize_scratch_size(src_w: i32, src_h: i32, dst_w: i32, dst_h: i32, filter: i32): usize ;
extern fn bun_image_resize_rgba8(
    src: [*]const u8,
    src_w: i32,
    src_h: i32,
    dst: [*]u8,
    dst_w: i32,
    dst_h: i32,
    filter: i32,
    scratch: [*]u8,
): c_int ;
extern fn bun_image_rotate_rgba8(src: [*]const u8, w: i32, h: i32, dst: [*]u8, deg: i32) ;
extern fn bun_image_flip_rgba8(src: [*]const u8, w: i32, h: i32, dst: [*]u8, horiz: i32) ;
extern fn bun_image_modulate_rgba8(buf: [*]u8, len: usize, brightness: f32, saturation: f32) ;
extern fn bun_image_cmyk_to_rgba8(buf: [*]u8, len: usize) ;

pub fn cmykToRgba(px: []u8) {
    bun_image_cmyk_to_rgba8(px.ptr, px.len);
}

/// In-place brightness/saturation. brightness multiplies V (so 1.0 is
/// identity); saturation linearly interpolates each channel toward the pixel's
/// luma (0 = greyscale, 1 = identity, >1 = boost).
pub fn modulate(rgba: []u8, brightness: f32, saturation: f32) {
    bun_image_modulate_rgba8(rgba.ptr, rgba.len, brightness, saturation);
}

pub fn resize(src: []const u8, sw: u32, sh: u32, dw: u32, dh: u32, f: Filter): Error![]u8 {
    if (b := system_backend) if (@hasDecl(b, "scale")) if (useSystem()) {
        if (out := b.scale(src, sw, sh, dw, dh, f) catch e => switch (e) {
            error.BackendUnavailable => null,
            else => return e,
        }) return out;
    }
    // ONE allocation for output + the kernel's scratch arena (intermediate
    // dst_w×src_h×4 row buffer + spans/weights tables). Zero mallocs in the
    // C++; mimalloc here is faster than libc, and the over-allocation rounds
    // into the same size class as the row buffer alone.
    const out_sz: usize = @as(usize, dw) * dh * 4;
    const scratch_sz = bun_image_resize_scratch_size(@intCast(sw), @intCast(sh), @intCast(dw), @intCast(dh), @intFromEnum(f));
    const block = bun.default_allocator.alloc(u8, out_sz + scratch_sz)?;
    const block_guard = DefaultAllocSliceGuard<u8>{ slice: block };
    if (bun_image_resize_rgba8(src.ptr, @intCast(sw), @intCast(sh), block.ptr, @intCast(dw), @intCast(dh), @intFromEnum(f), unsafe { block.ptr + out_sz }) != 0)
        return error.OutOfMemory;
    // Drop the scratch tail; mimalloc's shrink is in-place when the new size
    // fits the same block, so this is free.
    return bun.handleOom(bun.default_allocator.realloc(block, out_sz));
}

pub fn rotate(src: []const u8, w: u32, h: u32, degrees: u32): Error!Decoded {
    const dw: u32 = if (degrees == 90 || degrees == 270) h else w;
    const dh: u32 = if (degrees == 90 || degrees == 270) w else h;
    if (b := system_backend) if (@hasDecl(b, "rotate")) if (useSystem()) {
        if (out := b.rotate(src, w, h, degrees / 90) catch e => switch (e) {
            error.BackendUnavailable => null,
            else => return e,
        }) return { rgba: out, width: dw, height: dh };
    }
    const out = bun.default_allocator.alloc(u8, @as(usize, dw) * dh * 4)?;
    bun_image_rotate_rgba8(src.ptr, @intCast(w), @intCast(h), out.ptr, @intCast(degrees));
    return { rgba: out, width: dw, height: dh };
}

pub fn flip(src: []const u8, w: u32, h: u32, horizontal: bool): Error![]u8 {
    if (b := system_backend) if (@hasDecl(b, "flip")) if (useSystem()) {
        if (out := b.flip(src, w, h, horizontal) catch e => switch (e) {
            error.BackendUnavailable => null,
            else => return e,
        }) return out;
    }
    const out = bun.default_allocator.alloc(u8, @as(usize, w) * h * 4)?;
    bun_image_flip_rgba8(src.ptr, @intCast(w), @intCast(h), out.ptr, @intFromBool(horizontal));
    return out;
}

// ───────────────────────────── format codecs ────────────────────────────────
// Per-format implementations live in their own files; codecs.zig is the
// dispatch surface only.

import jpeg from "./codec_jpeg.zig";
import png from "./codec_png.zig";
import webp from "./codec_webp.zig";
import bmp from "./codec_bmp.zig";
import gif from "./codec_gif.zig";

import bun from "bun";
