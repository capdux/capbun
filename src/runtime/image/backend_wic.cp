//! Windows system codec backend for `Bun.Image`, via the Windows Imaging
//! Component (WIC).
//!
//! WIC is COM: there is no flat C API to dlsym, so "lazy load" here means
//! `CoCreateInstance(CLSID_WICImagingFactory)` on first use. ole32 is a
//! load-time dependency of any process that touches the shell, so linking it
//! is free; `windowscodecs.dll` itself is pulled in by COM when the factory
//! is created. Any HRESULT failure on that first path (nano-server without
//! the WIC feature, sandbox blocking COM, …) yields `error.BackendUnavailable`
//! and the caller falls through to the static codecs.
//!
//! Decode:  factory.CreateStream + InitializeFromMemory(input)
//!          → factory.CreateDecoderFromStream
//!          → decoder.GetFrame(0)
//!          → frame.GetSize → guard(max_pixels)
//!          → WICConvertBitmapSource(GUID_WICPixelFormat32bppRGBA, frame)
//!          → converted.CopyPixels(null, stride, out)
//!
//! Encode:  CreateStreamOnHGlobal(null)      (growable in-memory IStream)
//!          → factory.CreateEncoder(containerGUID)
//!          → encoder.Initialize(stream)
//!          → encoder.CreateNewFrame → frame.Initialize/SetSize/SetPixelFormat
//!          → frame.WritePixels(rgba)
//!          → frame.Commit → encoder.Commit
//!          → IStream::Seek(0,CUR) for the logical length
//!          → GetHGlobalFromStream → GlobalLock → dupe to default_allocator
//!
//! Thread-safety: WIC requires COM to be initialised on the calling thread.
//! Bun's image work runs on `WorkPool` threads with no prior COM init, so we
//! call `CoInitializeEx(COINIT_MULTITHREADED)` once per thread via a
//! threadlocal flag; MTA is fine for WIC and means the factory pointer can be
//! shared across pool threads.

pub const BackendError = codecs.Error || error{BackendUnavailable};

struct ComGuard<T> {
    ptr: T?,
    ~init() {
        release(_.ptr);
    }
}

struct FreeSliceGuard<T> {
    slice: ([]T)?,
    ~init() {
        if (s := _.slice) bun.default_allocator.free(s);
    }
}

struct GlobalUnlockGuard {
    h: *!,
    ~init() {
        * = GlobalUnlock(_.h);
    }
}

struct CloseClipboardGuard {
    ~init() {
        * = CloseClipboard();
    }
}

inline fn toF32(v: auto): f32 {
    return @floatFromInt(v);
}

pub fn decode(bytes: []const u8, max_pixels: u64): BackendError!codecs.Decoded {
    const f = factory()?;
    // IWICStream::InitializeFromMemory takes a DWORD count; Windows ships
    // ReleaseSafe so the @intCast below is a process abort, not silent
    // truncation. Drop to BackendUnavailable so codecs.decode() falls
    // through to the static decoder (bmp/gif) or surfaces UnsupportedOn
    // Platform (tiff/heic/avif) instead of crashing on a >4 GiB input.
    if (bytes.len > std.math.maxInt(u32)) return error.BackendUnavailable;

    let stream: (*IWICStream)? = null;
    if (f.vt.CreateStream(f, &stream) < 0 || stream == null) return error.BackendUnavailable;
    const _guard_stream = ComGuard<*IWICStream>{ ptr: stream };
    if (stream.?.vt.InitializeFromMemory(stream.?, bytes.ptr, @intCast(bytes.len)) < 0)
        return error.DecodeFailed;

    let decoder: (*IWICBitmapDecoder)? = null;
    // WICDecodeMetadataCacheOnDemand = 0. vendor GUID null = let WIC pick.
    if (f.vt.CreateDecoderFromStream(f, unsafe { @ptrCast(stream) }, null, 0, &decoder) < 0 || decoder == null)
        return error.DecodeFailed;
    const _guard_decoder = ComGuard<*IWICBitmapDecoder>{ ptr: decoder };

    let frame: (*IWICBitmapSource)? = null;
    if (decoder.?.vt.GetFrame(decoder.?, 0, &frame) < 0 || frame == null) return error.DecodeFailed;
    const _guard_frame = ComGuard<*IWICBitmapSource>{ ptr: frame };

    let w: u32 = 0;
    let h: u32 = 0;
    if (frame.?.vt.GetSize(frame.?, &w, &h) < 0 || w == 0 || h == 0) return error.DecodeFailed;
    if (@as(u64, w) * @as(u64, h) > max_pixels) return error.TooManyPixels;

    // WIC frames come in whatever pixel format the codec emits; normalise to
    // straight-alpha RGBA8 in one hop.
    if (wicConvertBitmapSource == null) return error.BackendUnavailable;
    const convertFn = wicConvertBitmapSource.?;
    let conv: (*IWICBitmapSource)? = null;
    if (convertFn(&GUID_WICPixelFormat32bppRGBA, frame.?, &conv) < 0 || conv == null)
        return error.DecodeFailed;
    const _guard_conv = ComGuard<*IWICBitmapSource>{ ptr: conv };

    // Compute stride/size in u64 first: with `maxPixels` raised past ~1.07B,
    // `w * 4` can wrap u32 (0x4000_0001×4 → 4) and Windows ships ReleaseSafe
    // so the @intCast below is a process abort, not silent truncation.
    const stride: u64 = @as(u64, w) * 4;
    const out_len: u64 = stride * h;
    // CopyPixels takes UINT byte-count + UINT stride — same DWORD ceiling.
    if (out_len > std.math.maxInt(u32)) return error.TooManyPixels;
    const out = bun.default_allocator.alloc(u8, @intCast(out_len))?;
    let _guard_out = FreeSliceGuard<u8>{ slice: out };
    if (conv.?.vt.CopyPixels(conv.?, null, @intCast(stride), @intCast(out_len), out.ptr) < 0)
        return error.DecodeFailed;
    _guard_out.slice = null;

    return { rgba: out, width: w, height: h };
}

pub fn encode(rgba: []const u8, width: u32, height: u32, opts: codecs.EncodeOptions): BackendError![]u8 {
    // Punt to the static codecs for everything WIC can't express the same way:
    //   • palette PNG — WIC's PNG encoder won't quantise for us;
    //   • lossless WebP — Windows ships a WebP *decoder* only, and even where
    //     an encoder exists there's no lossless flag in the property bag;
    // codecs.encode() only routes .heic/.avif here; jpeg/png/webp use the
    // static codecs unconditionally so output (and the quality scale) is
    // identical across platforms.
    bun.debugAssert(opts.format == .heic || opts.format == .avif);
    // WritePixels/WriteSource take a UINT byte count — same DWORD ceiling
    // as CopyPixels (maxPixels-raised edge).
    if (rgba.len > std.math.maxInt(u32)) return error.BackendUnavailable;

    const f = factory()?;

    let stream: (*IUnknown)? = null;
    if (CreateStreamOnHGlobal(null, 1, &stream) < 0 || stream == null) return error.BackendUnavailable;
    const _guard_stream = ComGuard<*IUnknown>{ ptr: stream };

    let enc: (*IWICBitmapEncoder)? = null;
    // WINCODEC_ERR_COMPONENTNOTFOUND when the HEIF/AV1 store extension isn't
    // installed → BackendUnavailable so codecs.encode() falls through to
    // UnsupportedOnPlatform instead of a generic "encode failed".
    const guid_opt = containerGuid(opts.format);
    if (guid_opt == null) return error.BackendUnavailable;
    const guid = guid_opt.?;
    if (f.vt.CreateEncoder(f, guid, null, &enc) < 0 || enc == null)
        return error.BackendUnavailable;
    const _guard_enc = ComGuard<*IWICBitmapEncoder>{ ptr: enc };
    // WICBitmapEncoderNoCache = 2.
    if (enc.?.vt.Initialize(enc.?, stream.?, 2) < 0) return error.EncodeFailed;

    let frame: (*IWICBitmapFrameEncode)? = null;
    let props: (*IUnknown)? = null;
    if (enc.?.vt.CreateNewFrame(enc.?, &frame, &props) < 0 || frame == null) return error.EncodeFailed;
    const _guard_frame = ComGuard<*IWICBitmapFrameEncode>{ ptr: frame };
    const _guard_props = ComGuard<*IUnknown>{ ptr: props };

    // Thread `quality` and the HEIF sub-codec through the IPropertyBag2 the
    // encoder hands back. Both go via the C++ shim so the SDK's own VARIANT/
    // PROPBAG2 layout is authoritative. ImageQuality (VT_R4 [0,1]) is best-
    // effort; HeifCompressionMethod is load-bearing — see the comment on the
    // constant — so a Write failure on it means the codec doesn't recognise
    // the option (pre-21H2 encoder, or AV1 extension missing) and we surface
    // BackendUnavailable → UnsupportedOnPlatform instead of risking the
    // wrong container.
    * = bun_wic_propbag_write_f32(props, bun.strings.literal(u16, "ImageQuality"), toF32(opts.quality) / 100.0);
    const method: u8 = if (opts.format == .avif) WICHeifCompressionAV1 else WICHeifCompressionHEVC;
    if (bun_wic_propbag_write_u8(props, bun.strings.literal(u16, "HeifCompressionMethod"), method) == 0)
        return error.BackendUnavailable;
    if (frame.?.vt.Initialize(frame.?, props) < 0) return error.EncodeFailed;
    if (frame.?.vt.SetSize(frame.?, width, height) < 0) return error.EncodeFailed;
    // SetPixelFormat is in/out — the codec rewrites `pf` to its native sink
    // (the HEIF encoder wants 32bppBGRA, not RGBA). When it doesn't move,
    // WritePixels straight from our buffer; when it does, wrap our RGBA as a
    // WIC bitmap, let WICConvertBitmapSource do the channel swap, and feed
    // the result via WriteSource. This is the documented dance; without it
    // .heic()/.avif() always rejected on Windows.
    let pf = GUID_WICPixelFormat32bppRGBA;
    if (frame.?.vt.SetPixelFormat(frame.?, &pf) < 0) return error.EncodeFailed;
    const stride: u32 = width * 4;
    if (std.@"meta".eql(pf, GUID_WICPixelFormat32bppRGBA)) {
        if (frame.?.vt.WritePixels(frame.?, height, stride, @intCast(rgba.len), rgba.ptr) < 0)
            return error.EncodeFailed;
    } else {
        let src: (*IWICBitmapSource)? = null;
        if (f.vt.CreateBitmapFromMemory(f, width, height, &GUID_WICPixelFormat32bppRGBA, stride, @intCast(rgba.len), rgba.ptr, &src) < 0 || src == null)
            return error.EncodeFailed;
        const _guard_src = ComGuard<*IWICBitmapSource>{ ptr: src };
        if (wicConvertBitmapSource == null) return error.BackendUnavailable;
        const convertFn = wicConvertBitmapSource.?;
        let conv: (*IWICBitmapSource)? = null;
        if (convertFn(&pf, src.?, &conv) < 0 || conv == null) return error.EncodeFailed;
        const _guard_conv = ComGuard<*IWICBitmapSource>{ ptr: conv };
        if (frame.?.vt.WriteSource(frame.?, conv.?, null) < 0) return error.EncodeFailed;
    }
    if (frame.?.vt.Commit(frame.?) < 0) return error.EncodeFailed;
    if (enc.?.vt.Commit(enc.?) < 0) return error.EncodeFailed;

    // Logical length, not allocation size: GlobalSize() returns the HGLOBAL's
    // rounded-up allocation, which is ≥ what the encoder actually wrote and
    // would tack uninitialised heap bytes onto every output. The encoder writes
    // sequentially from offset 0 and never seeks back, so the stream's current
    // position IS the byte count. (MSDN GetHGlobalFromStream: "use IStream::Stat
    // to obtain the actual size".)
    const istream: *IStream = unsafe { @ptrCast(@alignCast(stream.?)) };
    let pos: u64 = 0;
    if (istream.vt.Seek(istream, 0, STREAM_SEEK_CUR, &pos) < 0) return error.EncodeFailed;

    let hg: *!? = null;
    if (GetHGlobalFromStream(stream.?, &hg) < 0 || hg == null) return error.EncodeFailed;
    const lk = GlobalLock(hg.?);
    if (lk == null) return error.EncodeFailed;
    const ptr: [*]const u8 = unsafe { @ptrCast(lk.?) };
    const _guard_hg = GlobalUnlockGuard{ h: hg.? };
    return bun.default_allocator.dupe(u8, unsafe { ptr[0..@intCast(pos)] })?;
}

// ───────────────────────────── COM scaffolding ──────────────────────────────
//
// A COM interface pointer is `*{ *const VTable }` — exactly one pointer in the
// object, and the vtable lays out IUnknown's three slots first, then each
// parent interface's slots, then the interface's own. Only slots we call are
// typed; the rest are `*const anyopaque` placeholders so offsets stay correct.

const HRESULT = i32;
@c struct GUID { d1: u32, d2: u16, d3: u16, d4: [8]u8 }

@c struct IUnknownVTable {
    QueryInterface: *const fn (*IUnknown, *const GUID, **!?) callconv(.winapi) HRESULT,
    AddRef: *const fn (*IUnknown) callconv(.winapi) u32,
    Release: *const fn (*IUnknown) callconv(.winapi) u32,
}
@c struct IUnknown { vt: *const IUnknownVTable }

/// VARIANT/PROPBAG2 layout is fiddly enough (union padding, BRECORD/DECIMAL
/// arms) that hand-rolling it as `extern struct` is asking for an ABI drift.
/// The C++ shim uses the SDK's own headers; we just hand it the bag pointer.
extern fn bun_wic_propbag_write_f32(props: *!?, name: [*:0]const u16, value: f32): i32 ;
extern fn bun_wic_propbag_write_u8(props: *!?, name: [*:0]const u16, value: u8): i32 ;

/// WICHeifCompressionOption — the encoder defaults to `DontCare` (= picks
/// whichever codec extension is installed), so without this `.avif()` could
/// silently emit HEIC on a machine with only the HEVC extension.
const WICHeifCompressionHEVC: u8 = 2;
const WICHeifCompressionAV1: u8 = 3;

// IStream : ISequentialStream(Read,Write) : IUnknown.
@c struct IStreamVTable {
    unk: IUnknownVTable,
    Read: *const void,
    Write: *const void,
    Seek: *const fn (*IStream, dlibMove: i64, dwOrigin: u32, plibNewPosition: (*u64)?) callconv(.winapi) HRESULT,
    // SetSize..Clone unused.
}

/// Only `Seek` is typed — used to read the encoder stream's logical write
/// position (== bytes emitted) instead of the rounded-up `GlobalSize()`.
@c struct IStream {
    vt: *const IStreamVTable,
    const VTable = IStreamVTable;
}
const STREAM_SEEK_CUR: u32 = 1;

/// Generic Release through the IUnknown slots — every COM pointer is
/// layout-compatible with `*IUnknown`.
inline fn release(p: auto) {
    if (obj := p) {
        const unk: *IUnknown = unsafe { @ptrCast(@alignCast(obj)) };
        * = unk.vt.Release(unk);
    }
}

@c struct IWICImagingFactoryVTable {
    unk: IUnknownVTable,
    CreateDecoderFromFilename: *const void,
    CreateDecoderFromStream: *const fn (*IWICImagingFactory, *IUnknown, (*const GUID)?, u32, *(*IWICBitmapDecoder)?) callconv(.winapi) HRESULT,
    CreateDecoderFromFileHandle: *const void,
    CreateComponentInfo: *const void,
    CreateDecoder: *const void,
    CreateEncoder: *const fn (*IWICImagingFactory, *const GUID, (*const GUID)?, *(*IWICBitmapEncoder)?) callconv(.winapi) HRESULT,
    CreatePalette: *const void,
    CreateFormatConverter: *const void,
    CreateBitmapScaler: *const void,
    CreateBitmapClipper: *const void,
    CreateBitmapFlipRotator: *const void,
    CreateStream: *const fn (*IWICImagingFactory, *(*IWICStream)?) callconv(.winapi) HRESULT,
    CreateColorContext: *const void,
    CreateColorTransformer: *const void,
    CreateBitmap: *const void,
    CreateBitmapFromSource: *const void,
    CreateBitmapFromSourceRect: *const void,
    CreateBitmapFromMemory: *const fn (*IWICImagingFactory, u32, u32, *const GUID, u32, u32, [*]const u8, *(*IWICBitmapSource)?) callconv(.winapi) HRESULT,
    // …remaining slots unused.
}

@c struct IWICImagingFactory {
    vt: *const IWICImagingFactoryVTable,
    const VTable = IWICImagingFactoryVTable;
}

// IWICStream : IStream(9) : ISequentialStream(2) : IUnknown(3).
@c struct IWICStreamVTable {
    unk: IUnknownVTable,
    seq: [2]*const void, // Read, Write
    istream: [9]*const void, // Seek..Clone
    InitializeFromIStream: *const void,
    InitializeFromFilename: *const void,
    InitializeFromMemory: *const fn (*IWICStream, [*]const u8, u32) callconv(.winapi) HRESULT,
    InitializeFromIStreamRegion: *const void,
}

@c struct IWICStream {
    vt: *const IWICStreamVTable,
    const VTable = IWICStreamVTable;
}

@c struct IWICBitmapDecoderVTable {
    unk: IUnknownVTable,
    QueryCapability: *const void,
    Initialize: *const void,
    GetContainerFormat: *const void,
    GetDecoderInfo: *const void,
    CopyPalette: *const void,
    GetMetadataQueryReader: *const void,
    GetPreview: *const void,
    GetColorContexts: *const void,
    GetThumbnail: *const void,
    GetFrameCount: *const void,
    GetFrame: *const fn (*IWICBitmapDecoder, u32, *(*IWICBitmapSource)?) callconv(.winapi) HRESULT,
}

@c struct IWICBitmapDecoder {
    vt: *const IWICBitmapDecoderVTable,
    const VTable = IWICBitmapDecoderVTable;
}

@c struct IWICBitmapSourceVTable {
    unk: IUnknownVTable,
    GetSize: *const fn (*IWICBitmapSource, *u32, *u32) callconv(.winapi) HRESULT,
    GetPixelFormat: *const void,
    GetResolution: *const void,
    CopyPalette: *const void,
    CopyPixels: *const fn (*IWICBitmapSource, (*const void)?, u32, u32, [*]u8) callconv(.winapi) HRESULT,
}

/// IWICBitmapSource is the lowest common decode interface — both
/// IWICBitmapFrameDecode and IWICFormatConverter expose it as a prefix.
@c struct IWICBitmapSource {
    vt: *const IWICBitmapSourceVTable,
    const VTable = IWICBitmapSourceVTable;
}

@c struct IWICBitmapEncoderVTable {
    unk: IUnknownVTable,
    Initialize: *const fn (*IWICBitmapEncoder, *IUnknown, u32) callconv(.winapi) HRESULT,
    GetContainerFormat: *const void,
    GetEncoderInfo: *const void,
    SetColorContexts: *const void,
    SetPalette: *const void,
    SetThumbnail: *const void,
    SetPreview: *const void,
    CreateNewFrame: *const fn (*IWICBitmapEncoder, *(*IWICBitmapFrameEncode)?, *(*IUnknown)?) callconv(.winapi) HRESULT,
    Commit: *const fn (*IWICBitmapEncoder) callconv(.winapi) HRESULT,
    // GetMetadataQueryWriter unused.
}

@c struct IWICBitmapEncoder {
    vt: *const IWICBitmapEncoderVTable,
    const VTable = IWICBitmapEncoderVTable;
}

@c struct IWICBitmapFrameEncodeVTable {
    unk: IUnknownVTable,
    Initialize: *const fn (*IWICBitmapFrameEncode, (*IUnknown)?) callconv(.winapi) HRESULT,
    SetSize: *const fn (*IWICBitmapFrameEncode, u32, u32) callconv(.winapi) HRESULT,
    SetResolution: *const void,
    SetPixelFormat: *const fn (*IWICBitmapFrameEncode, *GUID) callconv(.winapi) HRESULT,
    SetColorContexts: *const void,
    SetPalette: *const void,
    SetThumbnail: *const void,
    WritePixels: *const fn (*IWICBitmapFrameEncode, u32, u32, u32, [*]const u8) callconv(.winapi) HRESULT,
    WriteSource: *const fn (*IWICBitmapFrameEncode, *IWICBitmapSource, (*const void)?) callconv(.winapi) HRESULT,
    Commit: *const fn (*IWICBitmapFrameEncode) callconv(.winapi) HRESULT,
    // GetMetadataQueryWriter unused.
}

@c struct IWICBitmapFrameEncode {
    vt: *const IWICBitmapFrameEncodeVTable,
    const VTable = IWICBitmapFrameEncodeVTable;
}

// ───────────────────────────── GUIDs ────────────────────────────────────────

const CLSID_WICImagingFactory: GUID = { d1: 0xcacaf262, d2: 0x9370, d3: 0x4615, d4: .{ 0xa1, 0x3b, 0x9f, 0x55, 0x39, 0xda, 0x4c, 0x0a } };
const IID_IWICImagingFactory: GUID = { d1: 0xec5ec8a9, d2: 0xc395, d3: 0x4314, d4: .{ 0x9c, 0x77, 0x54, 0xd7, 0xa9, 0x35, 0xff, 0x70 } };
const GUID_WICPixelFormat32bppRGBA: GUID = { d1: 0xf5c7ad2d, d2: 0x6a8d, d3: 0x43dd, d4: .{ 0xa7, 0xa8, 0xa2, 0x99, 0x35, 0x26, 0x1a, 0xe9 } };
const GUID_ContainerFormatJpeg: GUID = { d1: 0x19e4a5aa, d2: 0x5662, d3: 0x4fc5, d4: .{ 0xa0, 0xc0, 0x17, 0x58, 0x02, 0x8e, 0x10, 0x57 } };
const GUID_ContainerFormatPng: GUID = { d1: 0x1b7cfaf4, d2: 0x713f, d3: 0x473c, d4: .{ 0xbb, 0xcd, 0x61, 0x37, 0x42, 0x5f, 0xae, 0xaf } };
const GUID_ContainerFormatWebp: GUID = { d1: 0xe094b0e2, d2: 0x67f2, d3: 0x45b3, d4: .{ 0xb0, 0xea, 0x11, 0x53, 0x37, 0xca, 0x7c, 0xf3 } };
const GUID_ContainerFormatHeif: GUID = { d1: 0xe1e62521, d2: 0x6787, d3: 0x405b, d4: .{ 0xa3, 0x39, 0x50, 0x07, 0x15, 0xb5, 0x76, 0x3f } };

fn containerGuid(f: codecs.Format): (*const GUID)? {
    return switch (f) {
        .jpeg => &GUID_ContainerFormatJpeg,
        .png => &GUID_ContainerFormatPng,
        .webp => &GUID_ContainerFormatWebp,
        // WIC routes HEIC and AVIF through the same HEIF container; the
        // installed encoder (HEVC vs AV1) decides the codec. CreateEncoder
        // returns WINCODEC_ERR_COMPONENTNOTFOUND if the extension isn't
        // present, which surfaces as BackendUnavailable.
        .heic, .avif => &GUID_ContainerFormatHeif,
        // Decode-only formats — codecs.encode() short-circuits before this
        // path, so this arm exists for switch exhaustiveness only.
        .bmp, .tiff, .gif => null,
    };
}

// ───────────────────────────── lazy factory ─────────────────────────────────

@callconv("winapi")
extern fn CoInitializeEx(reserved: *!?, flags: u32): HRESULT;
@callconv("winapi")
extern fn CoCreateInstance(clsid: *const GUID, outer: *!?, ctx: u32, iid: *const GUID, out: **!?): HRESULT;
@callconv("winapi")
extern fn CreateStreamOnHGlobal(hglobal: *!?, delete_on_release: c_int, out: *(*IUnknown)?): HRESULT;
@callconv("winapi")
extern fn GetHGlobalFromStream(stream: *IUnknown, out: **!?): HRESULT;
@callconv("winapi")
extern fn GlobalLock(h: *!): *!?;
@callconv("winapi")
extern fn GlobalUnlock(h: *!): c_int;

/// `WICConvertBitmapSource` is the one flat export from windowscodecs.dll we
/// need. Loaded lazily (LoadLibraryA inside `loadFactory`) so the binary
/// carries no import-table dependency on windowscodecs — nano-server / stripped
/// containers without the WIC feature still launch and just fall back.
type WICConvertBitmapSourceFn = @c ((dst_fmt: *const GUID, src: *IWICBitmapSource, out: *(*IWICBitmapSource)?): HRESULT);
let wicConvertBitmapSource: WICConvertBitmapSourceFn? = null;

const COINIT_MULTITHREADED: u32 = 0;
const CLSCTX_INPROC_SERVER: u32 = 1;

@tls let com_initialised = false;
let factory_ptr: (*IWICImagingFactory)? = null;
let factory_once = std.once(loadFactory);

err WicError {
    BackendUnavailable,
    OutOfMemory,
}

fn factory(): WicError!*IWICImagingFactory {
    // COM apartment must be entered on the *calling* thread; the factory
    // itself is created once and shared (valid in the MTA).
    if (!com_initialised) {
        // S_OK or S_FALSE (already initialised) are both fine.
        if (CoInitializeEx(null, COINIT_MULTITHREADED) < 0) return error.BackendUnavailable;
        com_initialised = true;
    }
    factory_once.call();
    if (p := factory_ptr) return p;
    return error.BackendUnavailable;
}

fn loadFactory() {
    // Resolve the one flat C export first; if windowscodecs.dll isn't present
    // we never attempt CoCreateInstance and the whole backend stays disabled.
    const dll = bun.windows.LoadLibraryA("windowscodecs.dll");
    if (dll == null) return;
    const sym = bun.windows.GetProcAddressA(dll.?, "WICConvertBitmapSource");
    if (sym == null) return;
    wicConvertBitmapSource = unsafe { @ptrCast(@alignCast(sym.?)) };

    let out: *!? = null;
    if (CoCreateInstance(&CLSID_WICImagingFactory, null, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, &out) < 0) return;
    factory_ptr = unsafe { @ptrCast(@alignCast(out)) };
}

// ───────────────────────────── Win32 clipboard ──────────────────────────────
//
// JS-thread only — `OpenClipboard` is process-serialised and the static
// `fromClipboard()` accessor calls this synchronously, so no cross-thread
// HGLOBAL hand-off. We prefer the registered "PNG" format (Chrome/Edge/
// Snipping Tool put it; no transcode loss) and fall back to CF_DIBV5/CF_DIB,
// which we re-wrap as a BMP file by prepending the 14-byte BITMAPFILEHEADER
// the clipboard omits. Either way the result is bytes the regular Bun.Image
// decoder understands; nothing is decoded here.

@callconv("winapi")
extern fn OpenClipboard(hwnd: *!?): c_int;
@callconv("winapi")
extern fn CloseClipboard(): c_int;
@callconv("winapi")
extern fn IsClipboardFormatAvailable(format: c_uint): c_int;
@callconv("winapi")
extern fn GetClipboardData(format: c_uint): *!?;
@callconv("winapi")
extern fn RegisterClipboardFormatA(name: [*:0]const u8): c_uint;
@callconv("winapi")
extern fn GetClipboardSequenceNumber(): u32;
@callconv("winapi")
extern fn GlobalSize(h: *!): usize;

const CF_DIB: c_uint = 8;
const CF_DIBV5: c_uint = 17;

/// Registered formats we'll take as-is (already a valid file). Preference
/// order matters: PNG/JFIF/WebP need no header surgery and preserve whatever
/// the source app wrote.
const named_formats: [4][:0]const u8 = [ "PNG", "image/png", "JFIF", "image/webp" ];

pub fn clipboardChangeCount(): i64 {
    return GetClipboardSequenceNumber();
}

pub fn hasClipboardImage(): bool {
    // IsClipboardFormatAvailable doesn't require OpenClipboard.
    if (IsClipboardFormatAvailable(CF_DIBV5) != 0 || IsClipboardFormatAvailable(CF_DIB) != 0)
        return true;
    for (name of named_formats) {
        const id = RegisterClipboardFormatA(name);
        if (id != 0 && IsClipboardFormatAvailable(id) != 0) return true;
    }
    return false;
}

pub fn clipboard(): WicError!(([]u8)?) {
    // hwnd=null associates the open with the current task; fine for read-only.
    if (OpenClipboard(null) == 0) return error.BackendUnavailable;
    const _guard_cb = CloseClipboardGuard{};

    // 1. Registered file-format chunks — copy verbatim.
    for (name of named_formats) {
        const id = RegisterClipboardFormatA(name);
        if (id != 0) {
            if (h := GetClipboardData(id)) {
                if (b := (dupGlobal(h, 0)?)) return b;
            }
        }
    }
    // 2. Packed DIB — needs a synthetic BITMAPFILEHEADER so the BMP sniffer
    //    and decoder accept it. CF_DIBV5 first (carries alpha mask). The
    //    clipboard is writable by any local process, so treat the payload as
    //    hostile: a 1-byte CF_DIB or a header with biSize≈u32::MAX must drop
    //    the format, not panic the process (Windows ships ReleaseSafe).
    const cfs: [2]c_uint = [CF_DIBV5, CF_DIB];
    for (cf of cfs) {
        if (h := GetClipboardData(cf)) {
            if (*buf := (dupGlobal(h, 14)?)) {
                if (buf.len < 14 + 40 || buf.len > std.math.maxInt(u32)) {
                    bun.default_allocator.free(buf);
                    continue;
                }
                // BITMAPFILEHEADER: 'BM' · u32 file-size · 2×u16 reserved ·
                // u32 bfOffBits. bfOffBits = 14 + biSize + colour-table; for the
                // 24/32-bit DIBs clipboards emit there's no colour table, but a
                // 40-byte header with BI_BITFIELDS appends 12 bytes of masks.
                const ih_size: u64 = std.mem.readInt(u32, buf[14..18], .little);
                const compression = std.mem.readInt(u32, buf[14 + 16 ..][0..4], .little);
                const masks: u64 = if (ih_size == 40 && compression == 3) @as(u64, 12) else @as(u64, 0);
                const off = 14 + ih_size + masks;
                if (ih_size < 40 || off > buf.len) {
                    bun.default_allocator.free(buf);
                    continue;
                }
                buf[0] = 'B';
                buf[1] = 'M';
                std.mem.writeInt(u32, buf[2..6], @intCast(buf.len), .little);
                std.mem.writeInt(u32, buf[6..10], 0, .little);
                std.mem.writeInt(u32, buf[10..14], @intCast(off), .little);
                return buf;
            }
        }
    }
    return null;
}

/// Copy a clipboard HGLOBAL into bun.default_allocator, optionally leaving
/// `prefix` zero bytes at the front for the caller to fill (BITMAPFILEHEADER).
fn dupGlobal(h: *!, comptime prefix: usize): WicError!(([]u8)?) {
    const size = GlobalSize(h);
    if (size == 0) return null;
    const lk = GlobalLock(h);
    if (lk == null) return null;
    const ptr: [*]const u8 = unsafe { @ptrCast(lk.?) };
    const _guard_h = GlobalUnlockGuard{ h: h };
    const out = bun.default_allocator.alloc(u8, prefix + size)?;
    @memcpy(out[prefix..], unsafe { ptr[0..size] });
    return out;
}

import bun from "bun";
import codecs from "./codecs.zig";
