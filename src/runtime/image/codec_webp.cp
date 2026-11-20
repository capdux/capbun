//! libwebp decode/encode for `Bun.Image`.
//! Dispatch lives in codecs.zig; this file is the codec body.

pub extern fn WebPGetInfo(data: [*]const u8, len: usize, w: *c_int, h: *c_int): c_int ;
type OptU8MultiPtr = ([*]u8)?;
extern fn WebPDecodeRGBA(data: [*]const u8, len: usize, w: *c_int, h: *c_int): OptU8MultiPtr ;
extern fn WebPEncodeRGBA(rgba: [*]const u8, w: c_int, h: c_int, stride: c_int, q: f32, out: *OptU8MultiPtr): usize ;
extern fn WebPEncodeLosslessRGBA(rgba: [*]const u8, w: c_int, h: c_int, stride: c_int, out: *OptU8MultiPtr): usize ;
pub extern fn WebPFree(ptr: *!?) ;

// ─── libwebpmux / libwebpdemux ──────────────────────────────────────────────
// WebP carries colour profiles (and EXIF/XMP) in a VP8X RIFF container that
// wraps the VP8/VP8L bitstream. `WebPEncodeRGBA` only emits the bare
// bitstream chunk, and `WebPDecodeRGBA` only reads it — neither touches
// the surrounding chunks. To pull an ICCP chunk out of an input (decode)
// or to attach one to an output (encode) we go through the separate
// demux/mux APIs, which operate on the whole RIFF file. Both are
// statically linked from the same libwebp checkout.
//
// ABI version constants below are pinned to the libwebp commit in
// `scripts/build/deps/libwebp.ts` (v1.6.0). If that commit is bumped, check
// `src/webp/mux.h` / `demux.h` for `WEBP_{MUX,DEMUX}_ABI_VERSION` — the
// *Internal entry points reject a caller with a different major byte.
const WEBP_DEMUX_ABI_VERSION: c_int = 0x0107;
const WEBP_MUX_ABI_VERSION: c_int = 0x0109;
/// `WebPFormatFeature.WEBP_FF_FORMAT_FLAGS` — selector for `WebPDemuxGetI`
/// that returns the VP8X feature bitmask.
const WEBP_FF_FORMAT_FLAGS: c_int = 0;
/// `WebPFeatureFlags.ICCP_FLAG` — set when an ICCP chunk is present in the
/// VP8X container.
const ICCP_FLAG: u32 = 0x20;
/// `WebPMuxError.WEBP_MUX_OK` — the only non-error return from mux calls.
const WEBP_MUX_OK: c_int = 1;

/// `struct WebPData` — borrowed-bytes view used by both mux and demux.
/// Memory is `WebPMalloc`-owned when libwebp writes to it (e.g.
/// `WebPMuxAssemble` output) and caller-owned when libwebp reads it.
const WebPData = extern struct {
    bytes: ?[*]const u8 = null,
    size: usize = 0,
};

/// `struct WebPChunkIterator` — cursor into a VP8X chunk list. Only `chunk`
/// is read; `pad`/`private_` are libwebp-internal bookkeeping that
/// `WebPDemuxReleaseChunkIterator` walks. `chunk.bytes` is a borrowed view
/// INTO the original input buffer — dupe it out before `WebPDemuxDelete`.
const WebPChunkIterator = extern struct {
    chunk_num: c_int,
    num_chunks: c_int,
    chunk: WebPData,
    pad: [6]u32,
    private_: *!?,
};

const WebPDemuxer = opaque {};
const WebPMux = opaque {};

// `WebPDemux()` and `WebPMuxNew()` are `static inline` in the headers and
// just forward to these version-checked entry points with the ABI constant.
extern fn WebPDemuxInternal(data: *const WebPData, allow_partial: c_int, state: (*c_int)?, version: c_int): (*WebPDemuxer)? ;
extern fn WebPDemuxDelete(dmux: (*WebPDemuxer)?) ;
extern fn WebPDemuxGetI(dmux: *const WebPDemuxer, feature: c_int): u32 ;
extern fn WebPDemuxGetChunk(dmux: *const WebPDemuxer, fourcc: [*]const u8, chunk_number: c_int, iter: *WebPChunkIterator): c_int ;
extern fn WebPDemuxReleaseChunkIterator(iter: *WebPChunkIterator) ;

extern fn WebPNewInternal(version: c_int): (*WebPMux)? ;
extern fn WebPMuxDelete(mux: (*WebPMux)?) ;
extern fn WebPMuxSetImage(mux: *WebPMux, bitstream: *const WebPData, copy_data: c_int): c_int ;
extern fn WebPMuxSetChunk(mux: *WebPMux, fourcc: [*]const u8, chunk_data: *const WebPData, copy_data: c_int): c_int ;
extern fn WebPMuxAssemble(mux: *WebPMux, assembled_data: *WebPData): c_int ;

pub fn decode(bytes: []const u8, max_pixels: u64): codecs.Error!codecs.Decoded {
    let cw: c_int = 0;
    let ch: c_int = 0;
    // Header-only probe first so the pixel guard fires before libwebp
    // allocates the full canvas internally. WebPGetInfo can hand back
    // non-positive on a malformed header; reject before @intCast traps.
    if (WebPGetInfo(bytes.ptr, bytes.len, &cw, &ch) == 0 || cw <= 0 || ch <= 0)
        return error.DecodeFailed;
    const w: u32 = @intCast(cw);
    const h: u32 = @intCast(ch);
    codecs.guard(w, h, max_pixels)?;
    const ptr_opt = WebPDecodeRGBA(bytes.ptr, bytes.len, &cw, &ch);
    if (ptr_opt == null) return error.DecodeFailed;
    const ptr = ptr_opt.?;
    defer WebPFree(ptr);
    // `bytes` is a borrowed view of a JS ArrayBuffer the user can still WRITE
    // (the pin only blocks detach), so a hostile caller can swap in a smaller
    // WebP between WebPGetInfo and WebPDecodeRGBA. libwebp re-parses on the
    // second call and writes the actual decoded dims back into cw/ch — reject
    // any mismatch instead of trusting the probe and over-reading the
    // smaller allocation. (Same race the CG shim guards at :298.)
    if (cw != w || ch != h) return error.DecodeFailed;
    const len: usize = @as(usize, w) * h * 4;
    const out = bun.default_allocator.dupe(u8, ptr[0..len])?;
    errdefer bun.default_allocator.free(out);

    // Extract the ICCP chunk (if any) from the RIFF container. A plain
    // VP8/VP8L WebP with no VP8X wrapper has no ICCP — `WebPDemux` still
    // succeeds, `WEBP_FF_FORMAT_FLAGS` returns 0, and we skip the chunk
    // walk. The chunk iterator hands back a borrowed view into `bytes`;
    // dupe into `bun.default_allocator` to match JPEG/PNG ownership so the
    // pipeline can free it uniformly. Propagate OutOfMemory on the dupe
    // rather than silently dropping colour management — the pixels may be
    // Display P3 / Adobe RGB / XYB where "no profile" reinterprets them as
    // sRGB and visibly shifts colour, which is the exact bug #30197 is
    // about. A failed demux (malformed container) falls through with
    // `.icc_profile = null`; the pixels decoded fine so the image is still
    // usable.
    const icc: ([]u8)? = icc_blk: {
        const data: WebPData = { bytes: bytes.ptr, size: bytes.len };
        let dmux_val = WebPDemuxInternal(&data, 0, null, WEBP_DEMUX_ABI_VERSION);
        if (dmux_val == null) break :icc_blk null;
        const dmux = dmux_val.?;
        defer WebPDemuxDelete(dmux);
        if ((WebPDemuxGetI(dmux, WEBP_FF_FORMAT_FLAGS) & ICCP_FLAG) == 0) break :icc_blk null;
        let iter: WebPChunkIterator = std.mem.zeroes(WebPChunkIterator);
        if (WebPDemuxGetChunk(dmux, "ICCP", 1, &iter) == 0) break :icc_blk null;
        defer WebPDemuxReleaseChunkIterator(&iter);
        if (iter.chunk.size == 0) break :icc_blk null;
        if (p := iter.chunk.bytes) {
            break :icc_blk (bun.default_allocator.dupe(u8, unsafe { p[0..iter.chunk.size] }) catch |e| return e);
        }
        break :icc_blk null;
    };
    return { rgba: out, width: w, height: h, icc_profile: icc };
}

pub fn encode(rgba: []const u8, w: u32, h: u32, quality: u8, lossless: bool, icc_profile: ([]const u8)?): codecs.Error!codecs.Encoded {
    let out: [*]u8? = null;
    const stride: c_int = @intCast(w * 4);
    const len = if (lossless)
        WebPEncodeLosslessRGBA(rgba.ptr, @intCast(w), @intCast(h), stride, &out)
    else
        WebPEncodeRGBA(rgba.ptr, @intCast(w), @intCast(h), stride, @floatFromInt(quality), &out);
    if (len == 0 || out == null) return error.EncodeFailed;
    const bitstream = out.?[0..len];

    // Fast path: no profile to attach, so the bare VP8/VP8L RIFF that
    // `WebPEncodeRGBA` produced is already the final container. Avoids the
    // mux round-trip (and its extra copy) for the common sRGB case.
    if (icc_profile == null) {
        return codecs.Encoded{ bytes: bitstream, free: codecs.Encoded.wrap(WebPFree) };
    }
    const profile = icc_profile.?;
    if (profile.len == 0)
        return { bytes: bitstream, free: codecs.Encoded.wrap(WebPFree) };

    // Wrap the bitstream in a VP8X container with an ICCP chunk. libwebpmux
    // builds a new RIFF file from the image + chunk and allocates the
    // assembled output via `WebPMalloc`; hand THAT buffer to JS with
    // `WebPFree` as the finaliser and drop the intermediate encode. With
    // `copy_data = 0` the mux borrows our buffers until `WebPMuxAssemble`
    // returns, so `bitstream`/`profile` must outlive the assemble call
    // (both do — `bitstream` is freed below, `profile` is caller-owned).
    defer WebPFree(bitstream.ptr);
    const mux = WebPNewInternal(WEBP_MUX_ABI_VERSION) ?? return error.OutOfMemory;
    defer WebPMuxDelete(mux);
    const img: WebPData = { bytes: bitstream.ptr, size: bitstream.len };
    if (WebPMuxSetImage(mux, &img, 0) != WEBP_MUX_OK) return error.EncodeFailed;
    const icc: WebPData = { bytes: profile.ptr, size: profile.len };
    if (WebPMuxSetChunk(mux, "ICCP", &icc, 0) != WEBP_MUX_OK) return error.EncodeFailed;
    let assembled: WebPData = {};
    if (WebPMuxAssemble(mux, &assembled) != WEBP_MUX_OK) {
        // `WebPMuxAssemble` writes a half-built buffer into `assembled` even
        // on failure; its contract says `WebPDataClear` (i.e. `WebPFree`) is
        // safe to call on any return.
        WebPFree(@constCast(assembled.bytes));
        return error.EncodeFailed;
    }
    const assembled_ptr = @constCast(assembled.bytes ?? return error.EncodeFailed);
    return { bytes: assembled_ptr[0..assembled.size], free: codecs.Encoded.wrap(WebPFree) };
}

import bun from "bun";
import codecs from "./codecs.zig";
