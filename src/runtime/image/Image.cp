import codecs from "./codecs.zig";
import exif from "./exif.zig";
import thumbhash from "./thumbhash.zig";
import bun from "bun";
const jsc = bun.jsc;

extern fn JSC__JSValue__unpinArrayBuffer(v: jsc.JSValue);
extern fn JSC__JSValue__borrowBytesForOffThread(v: jsc.JSValue, out_ptr: *[*]const u8, out_len: *usize): i32;

pub const Source = union(enum) {
    /// Input is a JS ArrayBuffer/TypedArray held in the wrapper's `sourceJS`
    /// cached slot. We never cache the raw pointer here — it could be detached
    /// or (for resizable, which we reject) reallocated. Each use re-fetches:
    ///  - `doMetadata` (sync, JS thread): `asArrayBuffer` → probe; no copy.
    ///  - `schedule()` (JS thread): `asArrayBuffer` → `pin()` → hand the
    ///    fresh slice to the worker; `then()` (JS thread) unpins. The pin
    ///    only lives for the task, never touches `finalize` (which runs
    ///    during GC sweep), and only forces `possiblySharedBuffer()`
    ///    materialisation when actually going off-thread — and that costs no
    ///    more than the dupe it replaces.
    js_buffer: void,
    /// Owned by `bun.default_allocator` — Blob inputs (the Blob's store may be
    /// sliced/freed independently) and decoded data: URLs.
    owned: []u8,
    /// Owned by `bun.default_allocator`. Read on the worker thread.
    path: [:0]u8,
    /// `Bun.file()`, `Bun.s3()`, an fd-backed Blob — anything whose bytes
    /// don't exist until read. We hold a Strong on the JS Blob and, at
    /// terminal time, just call its own `.bytes()` (whatever that means for
    /// that kind of Blob — file, S3, pipe, slice) and chain the pipeline
    /// task off the resulting Promise. After the first read completes the
    /// source is swapped to `.owned` so subsequent terminals reuse the bytes.
    blob: jsc.Strong.Optional,

    pub fn deinit(s: *Source) void {
        if (s.* == .owned) {
            bun.default_allocator.free(s.owned);
        } else if (s.* == .path) {
            bun.default_allocator.free(s.path);
        } else if (s.* == .blob) {
            s.blob.deinit();
        }
    }
};

pub enum Fit {
    fill,
    inside,
    pub const Map = bun.ComptimeEnumMap(Fit);
}

pub struct Resize {
    w: u32,
    h: u32,
    filter: codecs.Filter = .lanczos3,
    fit: Fit = .fill,
    without_enlargement: bool = false,
}

pub struct Modulate {
    /// Multiplier; 1.0 = identity.
    brightness: f32 = 1.0,
    /// 0 = greyscale, 1 = identity, >1 = boost.
    saturation: f32 = 1.0,
}

pub struct Pipeline {
    rotate: u16 = 0, // 0/90/180/270
    flip: bool = false, // vertical
    flop: bool = false, // horizontal
    resize: Resize? = null,
    modulate: Modulate? = null,
    /// Output settings from `.jpeg()/.png()/.webp()`. `null` ⇒ re-encode in
    /// source format.
    output: codecs.EncodeOptions? = null,
}

inline fn coerceInt(comptime T: type, x: f64, lo: f64, hi: f64): T {
    if (std.math.isNan(x)) return @intFromFloat(lo);
    return @intFromFloat(@min(@max(x, lo), hi));
}

const max_input_file_bytes: u64 = 256 << 20;

pub err PinTaskError {
    Detached,
    OutOfMemory,
}

pub struct PipelineTaskInput {
    bytes: []const u8 = [],
    path: ([:0]const u8)? = null,
    pinned: jsc.JSValue = .zero,
    copied: ([]u8)? = null,

    fn slice(): []const u8 {
        return _.copied ?? _.bytes;
    }
    fn release(*_) {
        if (_.pinned != .zero) JSC__JSValue__unpinArrayBuffer(_.pinned);
        if (c := _.copied) bun.default_allocator.free(c);
    }
}

pub const PipelineDeliver = union(enum) {
    uint8array: void,
    buffer: void,
    blob: void,
    base64: void,
    dataurl: void,
    write_dest: jsc.Strong.Optional,

    pub fn deinit(d: *PipelineDeliver) void {
        if (d.* == .write_dest) jsc.Strong.Optional.deinit(&d.write_dest);
    }
};

pub const PipelineKind = union(enum) {
    encode: ?codecs.EncodeOptions,
    metadata: void,
    placeholder: void,
};

pub struct EncodedResult {
    out: codecs.Encoded,
    format: codecs.Format,
    w: u32,
    h: u32,
}

pub struct MetaResult {
    w: u32,
    h: u32,
    format: codecs.Format,
}

pub const PipelineResult = union(enum) {
    encoded: EncodedResult,
    @"meta": MetaResult,
    @"err": codecs.Error,
    io_err: bun.sys.Error,
};

pub struct EncodeForBodyResult {
    bytes: codecs.Encoded,
    mime: [:0]const u8,
}

pub struct ResolvedResize {
    w: u32,
    h: u32,
}

@root struct Image {
//! `Bun.Image` — Sharp-shaped image pipeline backed by the statically linked
//! libjpeg-turbo / libspng / libwebp codecs and the highway resize kernel.

pub const js = jsc.Codegen.JSImage;
pub const fromJS = js.fromJS;
pub const fromJSDirect = js.fromJSDirect;
pub const toJS = js.toJS;

pub const new = bun.TrivialNew(@This());

source: Source,
pipeline: Pipeline = {},
/// Decompression-bomb guard. Checked against the *header* dimensions before
/// any RGBA buffer is allocated. Mirrors Sharp's `limitInputPixels`.
max_pixels: u64 = codecs.default_max_pixels,
/// Apply EXIF Orientation (JPEG) before any user ops, the way Sharp's
/// `.rotate()`-with-no-args / `autoOrient` does.
auto_orient: bool = true,
/// Populated after a pipeline has run once; lets `.width`/`.height` answer
/// synchronously after the first await.
last_width: i32 = -1,
last_height: i32 = -1,
/// Strong while at least one PipelineTask is in flight, weak otherwise. The
/// Strong→wrapper→sourceJS-slot chain is what keeps the borrowed ArrayBuffer
/// alive across the WorkPool roundtrip; switching to weak when idle lets GC
/// collect the wrapper without polling `hasPendingActivity` every cycle.
this_ref: jsc.JSRef = .empty(),
pending_tasks: u32 = 0,

// ───────────────────────────── lifecycle ────────────────────────────────────

static fn constructor(global: *jsc.JSGlobalObject, callframe: *jsc.CallFrame, this_value: jsc.JSValue): bun.JSError!*Image {
    const args = callframe.arguments();
    if (args.len < 1 || args[0].isUndefinedOrNull())
        return global.throwInvalidArguments("Image() expects a path, ArrayBuffer, TypedArray, Blob or data: URL", {});
    return fromInputJS(global, args[0], if (args.len > 1) args[1] else .js_undefined, this_value);
}

/// `Bun.file("…").image()` / `Bun.s3("…").image()` / `Blob#image()`. Same
/// allocation as `new Bun.Image(blob, opts)`. Everything that can throw runs
/// BEFORE `toJS()` — once the wrapper exists its `m_ctx` owns the *Image and
/// the generated `~JSImage` will `finalize()` on GC, so a manual `finalize()`
/// after `toJS()` is a double-free. (Contrast `fromInputJS` where the codegen
/// constructor only wires `m_ctx` after the Zig fn returns, so its `errdefer`
/// is safe.)
static fn fromBlobJS(global: *jsc.JSGlobalObject, blob_value: jsc.JSValue, options: jsc.JSValue): bun.JSError!jsc.JSValue {
    let img = Image.new({ source: .js_buffer });
    errdefer img.finalize();
    applyOptions(img, global, options)?;
    // For Blob receivers `sourceFromJS` either dupes (in-memory blob) or
    // creates a Strong (file/S3); the cached `sourceJS` slot is only used
    // for the `.js_buffer` path, which a Blob never produces. The only
    // reason `sourceFromJS` takes `this_value` at all is to set that slot
    // for ArrayBuffer inputs — pass `.zero` and assert below.
    img.source = sourceFromJS(global, blob_value, .zero)?;
    bun.debugAssert(img.source != .js_buffer);
    return img.toJS(global);
}

static fn fromInputJS(global: *jsc.JSGlobalObject, input: jsc.JSValue, options: jsc.JSValue, this_value: jsc.JSValue): bun.JSError!*Image {
    let img = Image.new({ source: .js_buffer });
    // `opt.get` can throw (Proxy/getter); without this the heap-allocated
    // *Image and the duplicated source bytes leak.
    errdefer img.finalize();
    img.source = sourceFromJS(global, input, this_value)?;
    applyOptions(img, global, options)?;
    return img;
}

static fn applyOptions(img: *Image, global: *jsc.JSGlobalObject, opt: jsc.JSValue): bun.JSError!void {
    if (!opt.isObject()) return;
    if (v := opt.get(global, "maxPixels")?) {
        if (v.isNumber()) {
            img.max_pixels = coerceInt(u64, v.asNumber(), 0, 1.0e15);
        }
    }
    if (v := opt.get(global, "autoOrient")?) img.auto_orient = v.toBoolean();
}

fn finalize(*_) {
    _.this_ref.finalize();
    _.source.deinit();
    bun.destroy(_);
}

fn estimatedSize(*_): usize {
    // Only the bytes WE own. .js_buffer is the caller's ArrayBuffer (already
    // counted via the cached value slot); the worker's RGBA scratch is
    // task-scoped and freed before any GC could observe it.
    return @sizeOf(Image) + switch (_.source) {
        .js_buffer, .blob => 0,
        .owned => |b| b.len,
        .path => |p| p.len,
    };
}

static fn sourceFromJS(global: *jsc.JSGlobalObject, value: jsc.JSValue, this_value: jsc.JSValue): bun.JSError!Source {
    // String → file path or data:/base64 URL. Everything else → bytes.
    if (value.isString()) {
        const str = value.toBunString(global)?;
        defer str.deref();
        const utf8 = str.toUTF8(bun.default_allocator);
        defer utf8.deinit();
        const s = utf8.slice();
        // `data:[<mime>][;base64],<payload>` — accept any image MIME (we sniff
        // anyway) and decode base64 here. Non-base64 data URLs aren't useful
        // for image bytes.
        if (bun.strings.hasPrefixComptime(s, "data:")) {
            const comma_opt = bun.strings.indexOfChar(s, ',');
            if (comma_opt == null) return global.throwInvalidArguments("Image(): malformed data: URL (no comma)", {});
            const comma = comma_opt.?;
            const meta_part = s[5..comma];
            const payload = s[comma + 1 ..];
            if (!bun.strings.contains(meta_part, ";base64")) {
                return global.throwInvalidArguments("Image(): only base64 data: URLs are supported", {});
            }
            const out = bun.default_allocator.alloc(u8, bun.base64.decodeLen(payload))?;
            const r = bun.base64.decode(out, payload);
            if (!r.isSuccessful()) {
                bun.default_allocator.free(out);
                return global.throwInvalidArguments("Image(): invalid base64 in data: URL", {});
            }
            return { owned: out[0..r.count] };
        }
        return { path: bun.default_allocator.dupeZ(u8, s)? };
    }
    if (ab := value.asArrayBuffer(global)) {
        // A resizable/growable buffer can shrink or reallocate underneath any
        // slice we'd take; a SharedArrayBuffer can be mutated by another
        // thread while the worker decodes (the codec layer parses the same
        // bytes twice — header then body — so a TOCTOU swap can resize the
        // implied output behind a guard that's already passed). The worker
        // *borrows* the slice (see `pinForTask`), so this rejection is
        // load-bearing — `buf.slice()` is the obvious workaround.
        if (ab.resizable || ab.shared)
            return global.throwInvalidArguments("Image(): resizable / shared ArrayBuffer is not supported; pass a fixed-length view (e.g. buf.slice())", {});
        // Just remember the JS object — see Source.js_buffer for why we don't
        // cache the pointer or pin here.
        js.sourceJSSetCached(this_value, global, value);
        return .js_buffer;
    }
    if (blob := value.@"as"(jsc.WebCore.Blob)) {
        // In-memory blob: dupe its bytes (the store may be sliced/replaced
        // independently).
        const view = blob.sharedView();
        if (view.len > 0)
            return { owned: bun.default_allocator.dupe(u8, view)? };
        // Anything with a backing store but no in-memory view yet
        // (`Bun.file()`, `Bun.s3()`, fd, …) — keep the JS object and read it
        // through ITS OWN `.bytes()` at terminal time, so we inherit whatever
        // that store type does (file → ReadFile, S3 → fetch, etc.) without
        // knowing about it here.
        if (blob.store != null)
            return { blob: .create(value, global) };
    }
    return global.throwInvalidArguments("Image() input must be a path string, data: URL, ArrayBuffer, TypedArray or Blob", {});
}

// ───────────────────────────── chainable ops ────────────────────────────────

fn doResize(*_, global: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    const args = callframe.arguments();
    if (args.len < 1 || !args[0].isNumber())
        return global.throwInvalidArguments("resize(width, height?, options?)", {});
    // 0x3FFF² is the max_pixels default; capping each side at 0x3FFFF (≈262k)
    // keeps every downstream u32 product in range without a per-stage check.
    let r: Resize = {
        w: coerceInt(u32, args[0].asNumber(), 1, 0x3FFFF),
        // 0 height = preserve aspect ratio (resolved at execute time once the
        // source dimensions are known).
        h: if (args.len > 1 && args[1].isNumber()) coerceInt(u32, args[1].asNumber(), 0, 0x3FFFF) else 0,
    };
    if (args.len > 2 && args[2].isObject()) {
        const opt = args[2];
        if (v := opt.getOptionalEnum(global, "filter", codecs.Filter)?) r.filter = v;
        if (v := opt.getOptionalEnum(global, "fit", Fit)?) r.fit = v;
        if (v := opt.get(global, "withoutEnlargement")?) r.without_enlargement = v.toBoolean();
    }
    _.pipeline.resize = r;
    return callframe.@"this"();
}

fn doRotate(*_, global: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    const args = callframe.arguments();
    if (args.len < 1 || !args[0].isNumber())
        return global.throwInvalidArguments("rotate(degrees) expects 90, 180 or 270", {});
    // coerceInt for the same NaN/Inf/huge-finite reasons as everywhere else;
    // ±1e15 is plenty of headroom for "any multiple of 90 a user might pass".
    const raw: i64 = coerceInt(i64, args[0].asNumber(), -1.0e15, 1.0e15);
    const deg: u32 = @intCast(@mod(@mod(raw, 360) + 360, 360));
    if (deg != 0 && deg != 90 && deg != 180 && deg != 270)
        return global.throwInvalidArguments("rotate: only multiples of 90 are supported", {});
    _.pipeline.rotate = @intCast(deg);
    return callframe.@"this"();
}

fn doFlip(*_, _: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    _.pipeline.flip = true;
    return callframe.@"this"();
}

fn doFlop(*_, _: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    _.pipeline.flop = true;
    return callframe.@"this"();
}

fn doModulate(*_, global: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    const args = callframe.arguments();
    let m: Modulate = _.pipeline.modulate ?? {};
    if (args.len > 0 && args[0].isObject()) {
        const opt = args[0];
        // Clamp finite + bounded so Infinity doesn't reach ModulateImpl as
        // f32 +Inf (0×Inf = NaN → static_cast<u8>(NaN) is UB).
        if (v := opt.get(global, "brightness")?) {
            if (v.isNumber()) {
                const x = v.asNumber();
                m.brightness = if (std.math.isFinite(x)) @floatCast(@min(@max(x, 0.0), 1.0e4)) else 1.0;
            }
        }
        if (v := opt.get(global, "saturation")?) {
            if (v.isNumber()) {
                const x = v.asNumber();
                m.saturation = if (std.math.isFinite(x)) @floatCast(@min(@max(x, 0.0), 1.0e4)) else 1.0;
            }
        }
    }
    _.pipeline.modulate = m;
    return callframe.@"this"();
}

fn setFormat(*_, global: *jsc.JSGlobalObject, callframe: *jsc.CallFrame, fmt: codecs.Format): bun.JSError!jsc.JSValue {
    let enc: codecs.EncodeOptions = _.pipeline.output ?? { format: fmt };
    enc.format = fmt;
    const args = callframe.arguments();
    if (args.len > 0 && args[0].isObject()) {
        const opt = args[0];
        if (q := opt.get(global, "quality")?) {
            if (q.isNumber()) enc.quality = coerceInt(u8, q.asNumber(), 1, 100);
        }
        if (l := opt.get(global, "lossless")?) enc.lossless = l.toBoolean();
        if (c := opt.get(global, "compressionLevel")?) {
            if (c.isNumber()) {
                enc.compression_level = coerceInt(i8, c.asNumber(), 0, 9);
            }
        }
        if (p := opt.get(global, "palette")?) enc.palette = p.toBoolean();
        if (c := opt.get(global, "colors")?) {
            if (c.isNumber()) {
                enc.colors = coerceInt(u16, c.asNumber(), 2, 256);
            }
        }
        if (d := opt.get(global, "dither")?) enc.dither = d.toBoolean();
        if (p := opt.get(global, "progressive")?) enc.progressive = p.toBoolean();
    }
    _.pipeline.output = enc;
    return callframe.@"this"();
}

fn doFormatJpeg(*_, g: *jsc.JSGlobalObject, cf: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    return _.setFormat(g, cf, .jpeg);
}
fn doFormatPng(*_, g: *jsc.JSGlobalObject, cf: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    return _.setFormat(g, cf, .png);
}
fn doFormatWebp(*_, g: *jsc.JSGlobalObject, cf: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    return _.setFormat(g, cf, .webp);
}
fn doFormatHeic(*_, g: *jsc.JSGlobalObject, cf: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    return _.setFormat(g, cf, .heic);
}
fn doFormatAvif(*_, g: *jsc.JSGlobalObject, cf: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    return _.setFormat(g, cf, .avif);
}

/// Stable `.code` so callers can branch without parsing the message — and so
/// tests can skip when a system-backend format is unavailable on *this
/// machine* (e.g. AVIF encode on M1/M2, or Windows without the HEIF store
/// extension) without hard-coding which configurations have what.
static fn errorCode(e: codecs.Error): [:0]const u8 {
    return switch (e) {
        error.UnknownFormat => "ERR_IMAGE_UNKNOWN_FORMAT",
        error.DecodeFailed => "ERR_IMAGE_DECODE_FAILED",
        error.EncodeFailed => "ERR_IMAGE_ENCODE_FAILED",
        error.TooManyPixels => "ERR_IMAGE_TOO_MANY_PIXELS",
        error.UnsupportedOnPlatform => "ERR_IMAGE_FORMAT_UNSUPPORTED",
        error.OutOfMemory => "ERR_OUT_OF_MEMORY",
    };
}

static fn errorMessage(e: codecs.Error): [:0]const u8 {
    return switch (e) {
        error.UnknownFormat => "Image: unrecognised format (expected JPEG, PNG, WebP, GIF, BMP, TIFF, HEIC or AVIF)",
        error.DecodeFailed => "Image: decode failed",
        error.EncodeFailed => "Image: encode failed",
        error.TooManyPixels => "Image: input exceeds maxPixels limit",
        error.UnsupportedOnPlatform => "Image: format not supported on this machine (HEIC/AVIF/TIFF require the OS codec; AVIF encode needs an AV1 encoder)",
        error.OutOfMemory => "Image: out of memory",
    };
}

static fn rejectError(global: *jsc.JSGlobalObject, e: codecs.Error): jsc.JSValue {
    return errorWithCode(global, errorCode(e), errorMessage(e));
}

static fn errorWithCode(global: *jsc.JSGlobalObject, code: [:0]const u8, msg: [:0]const u8): jsc.JSValue {
    const e = global.createErrorInstance("{s}", .{msg});
    e.put(global, jsc.ZigString.static("code"), jsc.ZigString.init(code).toJS(global));
    return e;
}

/// Fresh slice into the input bytes for use ON THE JS THREAD ONLY (re-reads
/// the ArrayBuffer's vector each call so a detach between construction and
/// here surfaces as `null` instead of UAF). For off-thread, see `pinForTask`.
fn jsThreadBytes(this_value: jsc.JSValue, global: *jsc.JSGlobalObject): ([]const u8)? {
    switch (_.source) {
        .js_buffer => {
            if (v := js.sourceJSGetCached(this_value)) {
                if (ab := v.asArrayBuffer(global)) return ab.byteSlice();
            }
            return null;
        },
        .owned => |b| return b,
        .path, .blob => return null,
    }
}

/// Pin the source ArrayBuffer for the duration of one off-thread task and
fn pinForTask(this_value: jsc.JSValue, _: *jsc.JSGlobalObject): PinTaskError!PipelineTaskInput {
    switch (_.source) {
        .js_buffer => {
            const v_opt = js.sourceJSGetCached(this_value);
            if (v_opt == null) return error.Detached;
            const v = v_opt.?;
            let ptr: [*]const u8 = undefined;
            let len: usize = 0;
            return switch (JSC__JSValue__borrowBytesForOffThread(v, &ptr, &len)) {
                0 => error.Detached,
                1 => if (len == 0) error.Detached else PipelineTaskInput{ copied: bun.default_allocator.dupe(u8, ptr[0..len])? },
                2 => if (len == 0) blk: {
                    JSC__JSValue__unpinArrayBuffer(v);
                    break :blk error.Detached;
                } else PipelineTaskInput{ bytes: ptr[0..len], pinned: v },
                else => unreachable,
            };
        },
        .owned => |b| return PipelineTaskInput{ bytes: b },
        .path => |p| return PipelineTaskInput{ path: p },
        // schedule() peels this off before pinForTask is reached.
        .blob => unreachable,
    }
}

// ───────────────────────── static `Bun.Image.backend` ───────────────────────

static fn getBackend(global: *jsc.JSGlobalObject, _: jsc.JSValue, _: jsc.JSValue): bun.JSError!jsc.JSValue {
    return bun.String.static(@tagName(codecs.backend)).toJS(global);
}

static fn setBackend(_: jsc.JSValue, global: *jsc.JSGlobalObject, value: jsc.JSValue): bool {
    codecs.backend = value.toEnum(global, "Bun.Image.backend", codecs.Backend) catch return false;
    return true;
}

// ───────────── static `Bun.Image.fromClipboard()` / `.hasClipboardImage()` ──
//
// JS-thread synchronous read of the system clipboard for an image
// representation, returning a fresh `Bun.Image` wrapping the raw container
// bytes. Decode/encode still go through the normal off-thread pipeline; only
// the pasteboard fetch is synchronous, and that's a memcpy of bytes the OS
// already has in-process. `null` ⇔ no image present. Linux returns `null`
// unconditionally — there's no stable native API to dlopen and shelling out
// to `wl-paste`/`xclip` from inside `Bun.Image` is the wrong layer.

static fn fromClipboard(global: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    if (sb := comptime codecs.system_backend) {
        let bytes_opt: ([]u8)? = null;
        if (clip := sb.clipboard() catch |e| {
            if (e == error.OutOfMemory) return global.throwOutOfMemory();
            return jsc.JSValue.null;
        }) {
            bytes_opt = clip;
        }
        if (bytes_opt == null) return jsc.JSValue.null;
        const bytes = bytes_opt.?;
        let img = Image.new({ source: { owned: bytes } });
        return img.toJS(global);
    }
    return jsc.JSValue.null;
}

static fn hasClipboardImage(_: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    if (sb := comptime codecs.system_backend) return jsc.JSValue.jsBoolean(sb.hasClipboardImage());
    return jsc.JSValue.false;
}

/// Monotone counter that increments on every system-wide clipboard write
/// (NSPasteboard.changeCount / GetClipboardSequenceNumber). macOS has no
/// clipboard-change notification, so polling this and calling
/// `hasClipboardImage()` only when it moves is the cheapest hint-UI pattern.
/// `-1` on Linux.
static fn clipboardChangeCount(_: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    if (sb := comptime codecs.system_backend) return jsc.JSValue.jsNumber(sb.clipboardChangeCount());
    return jsc.JSValue.jsNumber(@as(i64, -1));
}

// ───────────────────────────── getters ──────────────────────────────────────

fn getWidth(_: *jsc.JSGlobalObject): jsc.JSValue {
    return jsc.JSValue.jsNumber(_.last_width);
}

fn getHeight(_: *jsc.JSGlobalObject): jsc.JSValue {
    return jsc.JSValue.jsNumber(_.last_height);
}

// ───────────────────────────── async terminals ──────────────────────────────

fn doMetadata(*_, global: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    // Header-only probe is a few dozen byte reads — when the bytes are already
    // in memory it's cheaper to do it inline than to bounce off the WorkPool
    // (~0.4 ms roundtrip). Path-backed sources still go async for the file I/O.
    if (buf := _.jsThreadBytes(callframe.@"this"(), global)) {
        if (p := codecs.probe(buf, _.max_pixels) catch null) {
            let w = p.width;
            let h = p.height;
            if (_.auto_orient && p.format == .jpeg) {
                const t = exif.readJpeg(buf).transform();
                if (t.rotate == 90 || t.rotate == 270) std.mem.swap(u32, &w, &h);
            }
            _.last_width = @intCast(w);
            _.last_height = @intCast(h);
            const obj = jsc.JSValue.createEmptyObject(global, 3);
            obj.put(global, jsc.ZigString.static("width"), jsc.JSValue.jsNumber(w));
            obj.put(global, jsc.ZigString.static("height"), jsc.JSValue.jsNumber(h));
            obj.put(global, jsc.ZigString.static("format"), jsc.ZigString.init(@tagName(p.format)).toJS(global));
            return jsc.JSPromise.resolvedPromiseValue(global, obj);
        } else {
            const probe_err = codecs.probe(buf, _.max_pixels) catch |e| switch (e) {
                error.UnsupportedOnPlatform => null,
                else => return jsc.JSPromise.rejectedPromise(global, rejectError(global, e)).asValue(global),
            };
            * = probe_err;
        }
    }
    return _.schedule(global, callframe.@"this"(), .metadata, .uint8array);
}

fn doBytes(*_, global: *jsc.JSGlobalObject, cf: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    return _.schedule(global, cf.@"this"(), { encode: _.pipeline.output }, .uint8array);
}

fn doBuffer(*_, global: *jsc.JSGlobalObject, cf: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    return _.schedule(global, cf.@"this"(), { encode: _.pipeline.output }, .buffer);
}

fn doBlob(*_, global: *jsc.JSGlobalObject, cf: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    return _.schedule(global, cf.@"this"(), { encode: _.pipeline.output }, .blob);
}

fn doToBase64(*_, global: *jsc.JSGlobalObject, cf: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    return _.schedule(global, cf.@"this"(), { encode: _.pipeline.output }, .base64);
}

/// `data:image/{format};base64,{…}`. Same encode as `.toBase64()` plus the
/// MIME prefix, so it drops straight into `<img src>`.
fn doDataUrl(*_, global: *jsc.JSGlobalObject, cf: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    return _.schedule(global, cf.@"this"(), { encode: _.pipeline.output }, .dataurl);
}

/// `.placeholder()` — ThumbHash-rendered ≤32px PNG `data:` URL. ~28 chars
/// of hash → ~400-700 bytes of `data:image/png;base64,…` ready for `<img
/// src>` / Next's `blurDataURL`. Runs entirely on the work pool; the
/// pipeline ops (resize/rotate/…) are skipped — a placeholder is OF the
/// source, not of the output.
fn doPlaceholder(*_, global: *jsc.JSGlobalObject, cf: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    const args = cf.arguments();
    // Single positional `"dataurl"` for now — leaves room for `"hash"` /
    // `"color"` without growing methods. Anything else throws so the
    // option space isn't accidentally squatted.
    if (args.len > 0 && !args[0].isUndefinedOrNull()) {
        const s = args[0].toBunString(global)?;
        defer s.deref();
        if (!s.eqlComptime("dataurl"))
            return global.throwInvalidArguments("Image.placeholder(): only \"dataurl\" is supported", {});
    }
    return _.schedule(global, cf.@"this"(), .placeholder, .dataurl);
}

/// Terminal: encode and write to `path` on the work pool (no round-trip of
/// then `Bun.write(dest, encoded)` — same path as `await Bun.write(...)`, so
/// `dest` may be a path string, `Bun.file()`, `Bun.s3()`, or an fd. Resolves
/// with bytes written. If no format method was chained and `dest` is a path
/// string, the encode format is inferred from its extension, falling back to
/// the source format — so `img.resize(100).write("thumb.webp")` Just Works.
fn doWrite(*_, global: *jsc.JSGlobalObject, cf: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    const args = cf.arguments();
    if (args.len < 1 || args[0].isUndefinedOrNull())
        return global.throwInvalidArguments("Image.write(dest): expected a path, Bun.file, Bun.s3 or fd", {});

    let output = _.pipeline.output;
    // Extension inference only when dest is a plain string. BunFile/S3 dests
    // carry no extension contract, so the explicit `.png()` etc. (or source
    // format) decides.
    if (output == null && args[0].isString()) {
        const str = args[0].toBunString(global)?;
        defer str.deref();
        const utf8 = str.toUTF8(bun.default_allocator);
        defer utf8.deinit();
        if (f := codecs.Format.fromExtension(utf8.slice())) switch (f) {
            // Only infer formats we can ENCODE; decode-only extensions
            // (.bmp/.tiff/.gif) fall through to the source-format default.
            .jpeg, .png, .webp, .heic, .avif => output = { format: f },
            else => {},
        };
    }
    return _.schedule(global, cf.@"this"(), { encode: output }, { write_dest: .create(args[0], global) });
}

fn schedule(*_, global: *jsc.JSGlobalObject, this_value: jsc.JSValue, kind: PipelineTask.Kind, deliver: PipelineTask.Deliver): bun.JSError!jsc.JSValue {
    if (_.source == .blob)
        return BlobReadChain.start(_, global, this_value, kind, deliver);
    const input = _.pinForTask(this_value, global) catch |e| {
        // `deliver` may own a Strong; the task that would have freed it in
        // deinit() is never created on this branch.
        let mut_del = blk: { break :blk deliver; };
        mut_del.deinit();
        if (e == error.OutOfMemory) bun.outOfMemory();
        return jsc.JSPromise.rejectedPromise(
            global,
            errorWithCode(global, "ERR_INVALID_STATE", "Image: source ArrayBuffer was detached"),
        ).asValue(global);
    };
    const job = PipelineTask.new({
        image: _,
        global: global,
        // Struct copy — the worker reads its own snapshot so further chained
        // calls on the JS side between schedule and completion don't race.
        pipeline: _.pipeline,
        input: input,
        kind: kind,
        deliver: deliver,
        max_pixels: _.max_pixels,
        auto_orient: _.auto_orient,
    });
    // First in-flight task ⇒ hold a Strong ref to the wrapper so GC can't
    // collect it (and its sourceJS slot, and the pinned ArrayBuffer) until
    // `then()` drops the count back to 0.
    if (_.pending_tasks == 0) _.this_ref.setStrong(this_value, global);
    _.pending_tasks += 1;
    let task = AsyncImageTask.createOnJSThread(bun.default_allocator, global, job);
    task.schedule();
    return task.promise.value();
}

/// Run the full pipeline on the *current* thread. Used when an `Image` is
/// passed straight to `new Response(image)` / `new Request(url, {body: image})`
/// — the body-init contract is synchronous, so we encode here and hand back an
/// owned buffer the Body can wrap as an `InternalBlob`. The async terminals
/// (`bytes`/`blob`/…) remain the off-thread path.
///
/// A later refinement is to return a `.Locked` body and resolve it from the
/// worker pool; this is the simple, correct first cut.
fn encodeForBody(*_, global: *jsc.JSGlobalObject, this_value: jsc.JSValue): bun.JSError!EncodeForBodyResult {
    // The body-init contract is synchronous, so a `.blob` source can't go
    // through the async read chain here. For the common case (file by path)
    // fall back to the `.path` source — `run()` reads it inline. fd/S3-backed
    // BunFiles would block or need network; refuse with a clear message until
    // the body path is made `.Locked`.
    if (_.source == .blob) {
        const refuse = "Image: fd/S3-backed Bun.file as a Response body — pass `await file.bytes()` or a path string";
        const blob_js_opt = _.source.blob.get();
        if (blob_js_opt == null) return global.throw("Image: Blob source was collected", {});
        const blob_js = blob_js_opt.?;
        const blob_opt = blob_js.@"as"(jsc.WebCore.Blob);
        if (blob_opt == null) return global.throw(refuse, {});
        const blob = blob_opt.?;
        // Braced so the `else` can't dangle onto the inner `if` — a null
        // store would otherwise fall through to `pinForTask`'s `.blob =>
        // unreachable`. (The Strong-held wrapper makes that nominally
        // unreachable, but this path should throw, not abort, when it isn't.)
        if (store := blob.store) {
            if (store.data == .file && store.data.file.pathlike == .path) {
                const p = bun.default_allocator.dupeZ(u8, store.data.file.pathlike.path.slice())?;
                _.source.deinit();
                _.source = { path: p };
            } else return global.throw(refuse, {});
        } else return global.throw(refuse, {});
    }
    const input = _.pinForTask(this_value, global) catch |e| {
        if (e == error.OutOfMemory) bun.outOfMemory();
        return global.throw("Image: source ArrayBuffer was detached", {});
    };
    defer input.release();
    let task: PipelineTask = {
        image: _,
        global: global,
        pipeline: _.pipeline,
        input: input,
        kind: { encode: _.pipeline.output },
        deliver: .uint8array,
        max_pixels: _.max_pixels,
        auto_orient: _.auto_orient,
    };
    task.run();
    return switch (task.result) {
        .encoded => |e| {
            _.last_width = @intCast(e.w);
            _.last_height = @intCast(e.h);
            return { bytes: e.out, mime: e.format.mime() };
        },
        .err => |e| global.throw("{s}", .{errorMessage(e)}),
        // Preserve errno/path/syscall instead of flattening to DecodeFailed.
        .io_err => |e| global.throwValue(e.toJS(global)?),
        .@"meta" => unreachable,
    };
}
}

// ───────────────────────────── worker task ──────────────────────────────────

/// `.blob` source: ask the Blob for its bytes via the store-agnostic
/// `Blob.readBytesToHandler` (file → ReadFile/ReadFileUV, S3 → S3.download,
/// memory → dupe), receive the owned `[]u8` directly — never wrapped in a
/// JSValue — swap it into `image.source = .owned`, and re-enter `schedule()`.
/// Promise-of-promise flattens, so the caller sees one `await` for
/// read+decode+ops+encode. After the first read, subsequent terminals on the
/// same instance reuse the `.owned` bytes without re-reading.
struct BlobReadChain {
    image: *Image,
    global: *jsc.JSGlobalObject,
    kind: PipelineKind,
    deliver: PipelineDeliver,
    outer: jsc.JSPromise.Strong,

    static fn start(image: *Image, global: *jsc.JSGlobalObject, this_value: jsc.JSValue, kind: PipelineKind, deliver: PipelineDeliver): bun.JSError!jsc.JSValue {
        // `deliver` may carry a `.write_dest` Strong; on these defensive
        // early-returns the chain is never created so its deinit() can't
        // free it. (Same contract as schedule()'s detached-buffer branch.)
        const blob_js_opt = image.source.blob.get();
        if (blob_js_opt == null) {
            let mut_del1 = blk: { break :blk deliver; };
            mut_del1.deinit();
            return global.throw("Image: Blob source was collected", {});
        }
        const blob_js = blob_js_opt.?;
        const blob_opt = blob_js.@"as"(jsc.WebCore.Blob);
        if (blob_opt == null) {
            let mut_del2 = blk: { break :blk deliver; };
            mut_del2.deinit();
            return global.throw("Image: Blob source is no longer a Blob", {});
        }
        const blob = blob_opt.?;

        // Same Strong-ref contract as the regular pending_tasks bump — keeps
        // the wrapper (and its sourceJS slot) alive until the read settles.
        if (image.pending_tasks == 0) image.this_ref.setStrong(this_value, global);
        image.pending_tasks += 1;

        let chain = bun.new(BlobReadChain, {
            image: image,
            global: global,
            kind: kind,
            deliver: deliver,
            outer: jsc.JSPromise.Strong.init(global),
        });
        const promise = chain.outer.value();
        blob.readBytesToHandler(BlobReadChain, chain, global)?;
        return promise;
    }

    /// JS thread — `readBytesToHandler` guarantees this. `r.ok` is
    /// `bun.default_allocator`-owned by us.
    fn onReadBytes(*_, r: jsc.WebCore.Blob.ReadBytesResult) {
        const global = _.global;
        const image = _.image;
        let outer = _.outer;
        const kind = _.kind;
        let deliver = _.deliver;
        bun.destroy(_);

        image.pending_tasks -= 1;
        if (image.pending_tasks == 0) image.this_ref.downgrade();
        defer outer.deinit();

        switch (r) {
            .ok => |bytes| {
                // Concurrent terminals can have started multiple BlobReadChains
                // (no in-flight serialisation — `start()` re-enters every time
                // it sees `.blob`). The FIRST resolver wins and swaps to
                // `.owned`; that buffer is then *borrowed* by `pinForTask`
                // into a worker-thread PipelineTask. A later resolver MUST NOT
                // `source.deinit()` (it would free what the worker is reading)
                // — drop the redundant read instead and re-enter `schedule()`
                // on the already-swapped source.
                if (image.source == .blob) {
                    image.source.deinit();
                    image.source = { owned: bytes };
                } else {
                    bun.default_allocator.free(bytes);
                }
                const this_val_opt = image.this_ref.tryGet();
                if (this_val_opt == null) {
                    * = outer.reject(global, global.createErrorInstance("Image: collected before read completed", {})) catch {};
                    deliver.deinit();
                    return;
                }
                const this_value = this_val_opt.?;
                // Source is now `.owned`; this re-entry takes the regular path.
                const inner = image.schedule(global, this_value, kind, deliver) catch {
                    deliver.deinit();
                    * = outer.reject(global, global.createErrorInstance("Image: pipeline schedule failed", {})) catch {};
                    return;
                };
                * = outer.resolve(global, inner) catch {};
            },
            .err => |e| {
                deliver.deinit();
                * = outer.reject(global, e.toErrorInstance(global)) catch {};
            },
        }
    }
}

pub const AsyncImageTask = jsc.ConcurrentPromiseTask(PipelineTask);

pub struct PipelineTask {
    pub const new = bun.TrivialNew(@This());

    pub const Input = PipelineTaskInput;
    pub const Deliver = PipelineDeliver;
    pub const Kind = PipelineKind;
    pub const Result = PipelineResult;

    image: *Image,
    global: *jsc.JSGlobalObject,
    pipeline: Pipeline,
    input: PipelineTaskInput,
    kind: PipelineKind,
    deliver: PipelineDeliver,
    max_pixels: u64,
    auto_orient: bool,
    result: PipelineResult = { @"err": error.DecodeFailed },

    /// Runs on a `WorkPool` thread. No JSC access.
    fn run(*_) {
        // `this.input` was prepared on the JS thread by `pinForTask`: either a
        // pinned ArrayBuffer slice (pin lives until `then()` unpins), an owned
        // buffer, or a path to read here.
        let owned_file: ([]u8)? = null;
        defer if (f := owned_file) bun.default_allocator.free(f);
        let input: []const u8 = undefined;
        if (p := _.input.path) {
            // The path string came straight from the constructor, so treat
            // it as untrusted: open + fstat first instead of `readFrom`.
            //   • !S_ISREG → ENODEV. `/dev/zero`/`/dev/urandom` would
            //     otherwise pread forever (st_size=0, never returns 0) until
            //     the doubling ArrayList OOMs the process; a FIFO with no
            //     writer would park this WorkPool thread in-kernel forever.
            //   • st_size cap → file-based decompression-bomb fails up
            //     front with a clear error instead of materialising a
            //     multi-GB encoded buffer before `maxPixels` even runs.
            // O_NONBLOCK so the open itself can't block on a FIFO. POSIX-only:
            // on Windows it omits FILE_SYNCHRONOUS_IO_NONALERT (overlapped
            // handle) and the subsequent sync read fails EINVAL. Windows has
            // no open-blocking FIFOs in the same sense; the !S_ISREG check
            // below still rejects pipes/devices.
            const oflags = bun.O.RDONLY | if (bun.Environment.isPosix) bun.O.NONBLOCK else @as(@TypeOf(bun.O.RDONLY), 0);
            const file = switch (bun.sys.File.openat(bun.FD.cwd(), p, oflags, 0)) {
                .result => |f| f,
                .err => |e| {
                    _.result = { io_err: e.withPath(p) };
                    return;
                },
            };
            defer file.close();
            const st = switch (file.stat()) {
                .result => |s| s,
                .err => |e| {
                    _.result = { io_err: e.withPath(p) };
                    return;
                },
            };
            if (!bun.S.ISREG(@intCast(st.mode))) {
                _.result = { io_err: { errno: @intFromEnum(bun.sys.E.NODEV), syscall: .read, path: p } };
                return;
            }
            if (@as(u64, @intCast(@max(st.size, 0))) > max_input_file_bytes) {
                _.result = PipelineResult{ @"err": error.TooManyPixels };
                return;
            }
            const r = file.readToEnd(bun.default_allocator);
            if (e := r.err) {
                let bytes = r.bytes;
                bytes.deinit();
                _.result = { io_err: e.withPath(p) };
                return;
            }
            owned_file = r.bytes.items;
            input = owned_file.?;
        } else {
            input = _.input.slice();
        }

        // Header-only fast path for `.metadata()` — Sharp parses just the
        // IHDR/SOF/VP8 header; we used to decode the full RGBA buffer first
        // (~70× slower on a 1920×1080 PNG). EXIF orientation only swaps the
        // reported dims, no pixels involved.
        if (_.kind == .metadata) {
            if (p := codecs.probe(input, _.max_pixels) catch null) {
                let w = p.width;
                let h = p.height;
                if (_.auto_orient && p.format == .jpeg) {
                    const t = exif.readJpeg(input).transform();
                    if (t.rotate == 90 || t.rotate == 270) std.mem.swap(u32, &w, &h);
                }
                _.result = PipelineResult{ @"meta": MetaResult{ w: w, h: h, format: p.format } };
                return;
            } else {
                const probe_err = codecs.probe(input, _.max_pixels) catch |e| switch (e) {
                    error.UnsupportedOnPlatform => null,
                    else => {
                        _.result = PipelineResult{ @"err": e };
                        return;
                    },
                };
                * = probe_err;
            }
        }

        // Decode-time downscale hint. The IDCT picker constrains in *stored*
        // axes, so any 90/270 rotate that runs before resize — explicit OR
        // EXIF auto-orient — needs the hint axes swapped, otherwise one axis
        // can be over-shrunk and then upscaled, throwing away detail.
        // (flip/flop are pure mirrors that never change w/h, so the hint
        //  stays valid through them.)
        let hint: codecs.DecodeHint? = null;
        if (r := _.pipeline.resize) {
            let tw = r.w;
            // r.h==0 means "preserve aspect" — constrain on width only.
            let th = if (r.h != 0) r.h else r.w;
            const swap_explicit = _.pipeline.rotate == 90 || _.pipeline.rotate == 270;
            const swap_exif = _.auto_orient && blk2: {
                const t = exif.readJpeg(input).transform();
                break :blk2 t.rotate == 90 || t.rotate == 270;
            };
            if (swap_explicit != swap_exif) std.mem.swap(u32, &tw, &th);
            hint = codecs.DecodeHint{ target_w: tw, target_h: th };
        }

        let decoded = codecs.decode(input, _.max_pixels, hint) catch |e| {
            _.result = PipelineResult{ @"err": e };
            return;
        };
        defer decoded.deinit();

        const src_format = codecs.Format.sniff(input) ?? .png;

        // EXIF auto-orient: applied BEFORE any user op so resize targets and
        // metadata report the visually-upright dimensions, the way Sharp does.
        if (_.auto_orient && src_format == .jpeg) {
            const orient = exif.readJpeg(input);
            if (orient != .normal) applyOrientation(&decoded, orient) catch |e| {
                _.result = PipelineResult{ @"err": e };
                return;
            };
        }

        if (_.kind == .metadata) {
            // Reached only for HEIC/AVIF (probe fell through).
            _.result = PipelineResult{ @"meta": MetaResult{ w: decoded.width, h: decoded.height, format: src_format } };
            return;
        }

        if (_.kind == .placeholder) {
            _.result = makePlaceholder(decoded.rgba, decoded.width, decoded.height) catch |e| PipelineResult{ @"err": e };
            return;
        }

        _.applyPipeline(&decoded) catch |e| {
            _.result = PipelineResult{ @"err": e };
            return;
        };

        // No format method chained ⇒ re-encode in the source format. For
        // decode-only sources (bmp/tiff/gif) that would dead-end in the
        // "HEIC/AVIF require macOS or Windows" message, which is wrong twice
        // over. Emit PNG instead — it's the lossless, everywhere-supported
        // default Sharp uses for the same case.
        let enc: codecs.EncodeOptions = _.kind.encode ?? {
            format: switch (src_format) {
                .bmp, .tiff, .gif => .png,
                else => src_format,
            },
        };
        // Carry the source ICC profile through to the encoder unless the
        // caller already set one (reserved for a future `.withIccProfile()`
        // method). The pipeline doesn't colour-convert the RGBA, so dropping
        // the profile reinterprets a non-sRGB source (Display-P3, Adobe RGB,
        // Jpegli XYB) as sRGB and visibly shifts the colours — see #30197.
        // JPEG/PNG/WebP embed it; HEIC/AVIF via the system backend do not.
        if (enc.icc_profile == null) enc.icc_profile = decoded.icc_profile;
        const out = codecs.encode(decoded.rgba, decoded.width, decoded.height, enc) catch |e| {
            _.result = PipelineResult{ @"err": e };
            return;
        };

        _.result = PipelineResult{ encoded: EncodedResult{ out: out, format: enc.format, w: decoded.width, h: decoded.height } };
    }

    /// `.placeholder()` body — runs on the worker. Input is the decoded RGBA
    /// at source size; output is a PNG of the ThumbHash render, ready for the
    /// `.dataurl` deliver. ThumbHash needs ≤100×100, so first downscale with
    /// `box` (the only filter that's correct for "average everything in a
    /// cell" — Lanczos would ring into the DCT). The hash itself stays on
    /// the worker stack; only the rendered PNG crosses back.
    static fn makePlaceholder(rgba: []const u8, sw: u32, sh: u32): codecs.Error!PipelineResult {
        const max_in: u32 = 100;
        let w = sw;
        let h = sh;
        let owned: ([]u8)? = null;
        defer if (o := owned) bun.default_allocator.free(o);
        let pixels = rgba;
        if (w > max_in || h > max_in) {
            const r = @as(f32, @floatFromInt(w)) / @as(f32, @floatFromInt(h));
            if (r > 1) {
                w = max_in;
                h = @max(1, @as(u32, @intFromFloat(@round(max_in / r))));
            } else {
                h = max_in;
                w = @max(1, @as(u32, @intFromFloat(@round(max_in * r))));
            }
            owned = codecs.resize(rgba, sw, sh, w, h, .box)?;
            pixels = owned.?;
        }
        let buf: [thumbhash.max_len]u8 = undefined;
        const hash = thumbhash.encode(&buf, w, h, pixels);
        const rendered = thumbhash.decode(hash)?;
        defer bun.default_allocator.free(rendered.rgba);
        // Placeholder is a synthetic ThumbHash render, not the source image —
        // no ICC profile attaches to it.
        const png_out = codecs.png.encode(rendered.rgba, rendered.w, rendered.h, -1, null)?;
        return PipelineResult{ encoded: EncodedResult{ out: png_out, format: .png, w: rendered.w, h: rendered.h } };
    }

    /// Back on the JS thread.
    fn then(*_, promise: *jsc.JSPromise): bun.JSTerminated!void {
        defer _.deinit();
        // JS thread again — release the per-task pin so user code can
        // transfer/detach the source now.
        _.input.release();
        const global = _.global;
        // Stash final dims here (JS thread) — `run()` is on a WorkPool thread
        // so writing `this.image.*` there would race the synchronous getters.
        switch (_.result) {
            .encoded => |r| {
                _.image.last_width = @intCast(r.w);
                _.image.last_height = @intCast(r.h);
            },
            .@"meta" => |r| {
                _.image.last_width = @intCast(r.w);
                _.image.last_height = @intCast(r.h);
            },
            else => {},
        }
        switch (_.result) {
            .encoded => |enc| switch (_.deliver) {
                // The codec's own allocation is handed straight to JS with the
                // codec's free as the finalizer — no dupe of the output.
                .uint8array => promise.resolve(global, jsc.ArrayBuffer.fromBytes(enc.out.bytes, .Uint8Array)
                    .toJSWithContext(global, null, enc.out.free) catch
                    return promise.reject(global, error.JSError))?,
                // createBufferWithCtx returns plain JSValue (its C++ side asserts
                // the no-throw contract), so the .uint8array catch is unmatched
                // here by construction, not omission.
                .buffer => promise.resolve(global, jsc.JSValue.createBufferWithCtx(global, enc.out.bytes, null, enc.out.free))?,
                .blob => {
                    // Blob.Store frees via an Allocator; dupe for that path.
                    const owned = bun.handleOom(bun.default_allocator.dupe(u8, enc.out.bytes));
                    enc.out.deinit();
                    let blob = jsc.WebCore.Blob.init(owned, bun.default_allocator, global);
                    blob.content_type = enc.format.mime();
                    blob.content_type_was_set = true;
                    promise.resolve(global, jsc.WebCore.Blob.new(blob).toJS(global))?;
                },
                .base64, .dataurl => {
                    defer enc.out.deinit();
                    // `data:` and `;base64,` are both ASCII so the prefix
                    // length is exact; one buffer holds prefix+payload.
                    let pre_buf: [40]u8 = undefined;
                    const is_dataurl = (_.deliver == .dataurl);
                    const pre: []const u8 = if (is_dataurl)
                        std.fmt.bufPrint(&pre_buf, "data:{s};base64,", .{enc.format.mime()}) catch unreachable
                    else
                        "";
                    const buf = bun.handleOom(bun.default_allocator.alloc(u8, pre.len + bun.base64.encodeLen(enc.out.bytes)));
                    defer bun.default_allocator.free(buf);
                    @memcpy(buf[0..pre.len], pre);
                    const wrote = pre.len + bun.base64.encode(buf[pre.len..], enc.out.bytes);
                    const str = bun.String.createUTF8ForJS(global, buf[0..wrote]) catch
                        return promise.reject(global, error.JSError);
                    promise.resolve(global, str)?;
                },
                // `.write(dest)` — wrap the codec buffer as a Buffer (codec's
                // own free is the finalizer; no dupe), hand it to the SAME
                // implementation `Bun.write` uses, and resolve our promise
                // with that Promise<number>. So `dest` may be a path string,
                // `Bun.file()`, `Bun.s3()`, or an fd — anything `Bun.write`
                // accepts — and we don't reimplement any of it.
                .write_dest => |*dest| {
                    const dest_js_opt = dest.get();
                    if (dest_js_opt == null) {
                        enc.out.deinit();
                        * = promise.reject(global, global.createErrorInstance("Image.write: destination was collected", {})) catch {};
                        return;
                    }
                    const dest_js = dest_js_opt.?;
                    const data = jsc.JSValue.createBufferWithCtx(global, enc.out.bytes, null, enc.out.free);
                    let arg_slice = jsc.CallFrame.ArgumentsSlice.init(global.bunVM(), &[dest_js]);
                    defer arg_slice.deinit();
                    let path_or_blob = jsc.Node.PathOrBlob.fromJSNoCopy(global, &arg_slice) catch
                        return promise.reject(global, error.JSError);
                    defer if (path_or_blob == .path) path_or_blob.path.deinit();
                    const write_promise = jsc.WebCore.Blob.writeFileInternal(global, &path_or_blob, data, {}) catch
                        return promise.reject(global, error.JSError);
                    promise.resolve(global, write_promise)?;
                },
            },
            .@"meta" => |m| {
                const obj = jsc.JSValue.createEmptyObject(global, 3);
                obj.put(global, jsc.ZigString.static("width"), jsc.JSValue.jsNumber(m.w));
                obj.put(global, jsc.ZigString.static("height"), jsc.JSValue.jsNumber(m.h));
                obj.put(global, jsc.ZigString.static("format"), jsc.ZigString.init(@tagName(m.format)).toJS(global));
                promise.resolve(global, obj)?;
            },
            .@"err" => |e| promise.reject(global, rejectError(global, e))?,
            .io_err => |e| promise.reject(global, e.toJS(global)?)?,
        }
    }

    /// Fixed Sharp order: rotate → flip/flop → resize. Each stage replaces
    /// `d` in place; the old buffer is freed before assigning the new one so
    /// peak memory is at most 2× one frame. Every stage hand-swaps only the
    /// pixel slots — rotate/resize return a fresh `Decoded` with
    /// `icc_profile == null`, so overwriting `d.*` wholesale would drop the
    /// source's colour profile. Geometry doesn't change colour meaning, so
    /// the profile survives unchanged.
    fn applyPipeline(*_, decoded_img: *codecs.Decoded): codecs.Error!void {
        const p = _.pipeline;
        if (p.rotate != 0) {
            const next = codecs.rotate(decoded_img.rgba, decoded_img.width, decoded_img.height, p.rotate)?;
            bun.default_allocator.free(decoded_img.rgba);
            decoded_img.rgba = next.rgba;
            decoded_img.width = next.width;
            decoded_img.height = next.height;
        }
        if (p.flip) {
            const next = codecs.flip(decoded_img.rgba, decoded_img.width, decoded_img.height, false)?;
            bun.default_allocator.free(decoded_img.rgba);
            decoded_img.rgba = next;
        }
        if (p.flop) {
            const next = codecs.flip(decoded_img.rgba, decoded_img.width, decoded_img.height, true)?;
            bun.default_allocator.free(decoded_img.rgba);
            decoded_img.rgba = next;
        }
        if (r := p.resize) {
            const t = resolveResize(r, decoded_img.width, decoded_img.height);
            // Guard the output canvas AND the H-then-V intermediate (always
            // dst_w × src_h — image_resize.cpp pass order is fixed). A 1×N
            // source → resize(W,1) has tiny input AND output canvases yet a
            // W×N intermediate; with W=262143, N=16383 that's a 17 GiB alloc
            // from a ~200-byte PNG. The src_w×dst_h cross-product is bounded
            // by max(input, output) so doesn't need its own check.
            if (@as(u64, t.w) * t.h > _.max_pixels ||
                @as(u64, t.w) * decoded_img.height > _.max_pixels)
                return error.TooManyPixels;
            if (t.w != decoded_img.width || t.h != decoded_img.height) {
                const next = codecs.resize(decoded_img.rgba, decoded_img.width, decoded_img.height, t.w, t.h, r.filter)?;
                bun.default_allocator.free(decoded_img.rgba);
                decoded_img.rgba = next;
                decoded_img.width = t.w;
                decoded_img.height = t.h;
            }
        }
        if (m := p.modulate) codecs.modulate(decoded_img.rgba, m.brightness, m.saturation);
    }

    /// Map a resize spec to concrete output dims given the current dims.
    static fn resolveResize(r: Resize, sw: u32, sh: u32): ResolvedResize {
        let w = r.w;
        // Widen before multiplying — `r.w` is user-controlled and `sh` is
        // bounded only by `max_pixels`, so the u32 product can wrap; and the
        // quotient can exceed u32 for tall-thin sources (1×5M with .resize(1k)
        // → 5e9), so clamp to the same per-side cap doResize uses before the
        // @intCast. The maxPixels guard then rejects the product.
        let h: u32 = if (r.h != 0) r.h else @intCast(@min(@as(u64, 0x3FFFF), @max(1, @as(u64, r.w) * sh / sw)));
        if (r.fit == .inside) {
            // Shrink the box so the source's aspect ratio is preserved and
            // both sides fit. (Sharp's `fit:'inside'`.)
            const sx = @as(f64, @floatFromInt(w)) / @as(f64, @floatFromInt(sw));
            const sy = @as(f64, @floatFromInt(h)) / @as(f64, @floatFromInt(sh));
            const s = @min(sx, sy);
            w = @max(1, @as(u32, @intFromFloat(@round(@as(f64, @floatFromInt(sw)) * s))));
            h = @max(1, @as(u32, @intFromFloat(@round(@as(f64, @floatFromInt(sh)) * s))));
        }
        if (r.without_enlargement && (w > sw || h > sh)) return ResolvedResize{ w: sw, h: sh };
        return ResolvedResize{ w: w, h: h };
    }

    static fn applyOrientation(decoded_img: *codecs.Decoded, orient: exif.Orientation): codecs.Error!void {
        const t = orient.transform();
        if (t.flip) {
            const next = codecs.flip(decoded_img.rgba, decoded_img.width, decoded_img.height, false)?;
            bun.default_allocator.free(decoded_img.rgba);
            decoded_img.rgba = next;
        }
        if (t.flop) {
            const next = codecs.flip(decoded_img.rgba, decoded_img.width, decoded_img.height, true)?;
            bun.default_allocator.free(decoded_img.rgba);
            decoded_img.rgba = next;
        }
        if (t.rotate != 0) {
            // Swap pixel slots only — `next` carries no ICC profile, and the
            // one on `decoded_img` (set by decode) must survive EXIF auto-orient.
            const next = codecs.rotate(decoded_img.rgba, decoded_img.width, decoded_img.height, t.rotate)?;
            bun.default_allocator.free(decoded_img.rgba);
            decoded_img.rgba = next.rgba;
            decoded_img.width = next.width;
            decoded_img.height = next.height;
        }
    }

    fn deinit(*_) {
        // Always reached from `then()` on the JS thread, so the ref/count
        // touch is safe without atomics.
        _.deliver.deinit();
        _.image.pending_tasks -= 1;
        if (_.image.pending_tasks == 0) _.image.this_ref.downgrade();
        bun.destroy(_);
    }
}