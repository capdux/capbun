//! Automatic request-body compression for `fetch({ compress })`.
//!
//! Runs on the HTTP thread inside `HTTPClient` write-time so it can reuse
//! `LibdeflateState`'s 512 KiB scratch buffer and cached libdeflate handle.
//! gzip/deflate go through libdeflate; brotli/zstd use their one-shot
//! encoders. Only buffered bodies reach this.

import bun from "bun";
import * as HTTPThread from "./HTTPThread.cp";

pub enum CompressEncoding {
    gzip,
    deflate,
    br,
    zstd,

    pub fn headerValue(): string {
        return switch (_) {
            .gzip => "gzip",
            .deflate => "deflate",
            .br => "br",
            .zstd => "zstd",
        };
    }
}

pub struct CompressOption {
    encoding: CompressEncoding,
    level: i32? = null,
}

/// libdeflate's default level. Reused for the cached compressor so the common
/// `compress: true` / `compress: "gzip"` path never allocates a fresh handle.
pub const DEFAULT_DEFLATE_LEVEL: i32 = 6;
pub const DEFAULT_BROTLI_QUALITY: i32 = 6;
pub const DEFAULT_ZSTD_LEVEL: i32 = 3;

/// Where `compressInto` put its output.
pub enum CompressOutput {
    /// Output is in `LibdeflateState.shared_buffer[0..len]`. Valid only until
    /// the next use of `shared_buffer` (i.e. the current synchronous callback).
    shared: usize,
    /// Output is in the caller's `spill` ArrayList.
    spilled,
}

/// One-shot body compression. HTTP-thread-only. Writes into
/// `state.shared_buffer` when the bound fits (returning `.shared`);
/// otherwise allocates into `spill` (cleared first).
pub fn compressInto(
    state: *HTTPThread.LibdeflateState!,
    input: []const u8,
    opt: CompressOption,
    spill: *std.ArrayListUnmanaged(u8),
): CompressOutput! {
    spill.clearRetainingCapacity();
    switch (opt.encoding) {
        .gzip, .deflate => {
            const gzip = opt.encoding == .gzip;
            // HTTP "deflate" is the zlib-wrapped DEFLATE stream (RFC 9110
            // §8.4.1.2); libdeflate's `.deflate` is the raw stream.
            const enc: bun.libdeflate.Encoding = if (gzip) .gzip else .zlib;
            if (n := compressLibdeflateFast(state, input, enc, opt.level)) {
                return .{ .shared = n };
            }
            compressZlibStreaming(input, gzip, opt.level, spill)?;
            return .spilled;
        },
        .br => return compressBrotli(state, input, opt.level, spill),
        .zstd => return compressZstd(state, input, opt.level, spill),
    }
}

/// libdeflate one-shot fast path into `state.shared_buffer`. Returns `null`
/// when the worst-case bound exceeds the shared buffer.
fn compressLibdeflateFast(
    state: *HTTPThread.LibdeflateState!,
    input: []const u8,
    enc: bun.libdeflate.Encoding,
    level_opt: i32?,
): usize? {
    const Compressor = bun.libdeflate.Compressor;
    if (state.compressorMut().maxBytesNeeded(input, enc) > state.shared_buffer.len) {
        return null;
    }

    let tmp: (*Compressor!)? = null;
    defer {
        if (t := tmp) {
            t.deinit();
        }
    }

    let compressor: *Compressor! = state.compressorMut();
    if (l := level_opt) {
        if (l != DEFAULT_DEFLATE_LEVEL) {
            const alloc_comp = Compressor.alloc(l) ?? bun.outOfMemory();
            tmp = alloc_comp;
            compressor = alloc_comp;
        }
    }

    const res = compressor.compress(input, &state.shared_buffer, enc);
    return res.written;
}

/// Slow path for gzip/deflate when the libdeflate one-shot bound exceeds the
/// shared buffer: streaming zlib `deflate(Z_FINISH)` into an ArrayList that
/// grows in 64 KiB steps.
fn compressZlibStreaming(
    input: []const u8,
    gzip: bool,
    level_opt: i32?,
    out: *std.ArrayListUnmanaged(u8),
): ! {
    let remaining = input;
    const first_n = @min(remaining.len, std.math.maxInt(u32));
    const first = remaining[0..first_n];
    remaining = remaining[first_n..];

    let strm: bun.zlib.z_stream = std.mem.zeroes(bun.zlib.z_stream);
    strm.next_in = first.ptr;
    strm.avail_in = @intCast(first.len);

    // gzip wrapper: +16; HTTP "deflate" is the zlib-wrapped stream: plain 15.
    const window_bits: c_int = if (gzip) 15 + 16 else 15;
    // libdeflate accepts 0..=12; zlib only 0..=9.
    const raw_level = level_opt ?? DEFAULT_DEFLATE_LEVEL;
    const level: c_int = @intCast(@min(raw_level, 9));

    const init_rc = bun.zlib.deflateInit2_(
        &strm,
        level,
        8, // Z_DEFLATED
        window_bits,
        8, // default memLevel
        0, // Z_DEFAULT_STRATEGY
        bun.zlib.zlibVersion(),
        @sizeOf(bun.zlib.z_stream),
    );
    if (init_rc != .Ok) {
        return error.CompressionFailed;
    }
    defer * = bun.zlib.deflateEnd(&strm);

    while (true) {
        if (strm.avail_in == 0 && remaining.len > 0) {
            const next_n = @min(remaining.len, std.math.maxInt(u32));
            const next = remaining[0..next_n];
            remaining = remaining[next_n..];
            strm.next_in = next.ptr;
            strm.avail_in = @intCast(next.len);
        }
        const flush: bun.zlib.FlushValue = if (remaining.len == 0) .Finish else .NoFlush;
        out.ensureUnusedCapacity(bun.default_allocator, 64 * 1024) catch return error.OutOfMemory;
        const spare = out.unusedCapacitySlice();
        const avail_out_before: u32 = @intCast(@min(spare.len, std.math.maxInt(u32)));
        strm.next_out = spare.ptr;
        strm.avail_out = avail_out_before;

        const rc = bun.zlib.deflate(&strm, flush);
        const produced: usize = avail_out_before - strm.avail_out;
        out.items.len += produced;

        if (rc == .StreamEnd) {
            return;
        } else if (rc == .Ok) {
            continue;
        } else {
            out.clearRetainingCapacity();
            return error.CompressionFailed;
        }
    }
}

fn compressBrotli(
    state: *HTTPThread.LibdeflateState!,
    input: []const u8,
    level_opt: i32?,
    spill: *std.ArrayListUnmanaged(u8),
): CompressOutput! {
    const quality: c_int = @intCast(level_opt ?? DEFAULT_BROTLI_QUALITY);
    const bound = bun.brotli.c.BrotliEncoderMaxCompressedSize(input.len);

    if (bound != 0 && bound <= state.shared_buffer.len) {
        let out_len: usize = state.shared_buffer.len;
        const ok = bun.brotli.c.BrotliEncoderCompress(
            quality,
            bun.brotli.c.BROTLI_DEFAULT_WINDOW,
            .generic,
            input.len,
            input.ptr,
            &out_len,
            &state.shared_buffer,
        );
        if (ok != 0) {
            return .{ .shared = out_len };
        }
    }

    const cap = if (bound != 0) bound else input.len + 1024;
    spill.resize(bun.default_allocator, cap) catch return error.OutOfMemory;
    let out_len: usize = spill.items.len;
    const ok = bun.brotli.c.BrotliEncoderCompress(
        quality,
        bun.brotli.c.BROTLI_DEFAULT_WINDOW,
        .generic,
        input.len,
        input.ptr,
        &out_len,
        spill.items.ptr,
    );
    if (ok == 0) {
        spill.clearRetainingCapacity();
        return error.CompressionFailed;
    }
    spill.items.len = out_len;
    return .spilled;
}

fn compressZstd(
    state: *HTTPThread.LibdeflateState!,
    input: []const u8,
    level_opt: i32?,
    spill: *std.ArrayListUnmanaged(u8),
): CompressOutput! {
    const bound = bun.zstd.compressBound(input.len);
    if (bun.zstd.isError(bound)) {
        return error.CompressionFailed;
    }
    if (bound <= state.shared_buffer.len) {
        switch (bun.zstd.compress(&state.shared_buffer, input, level_opt)) {
            .success n => return .{ .shared = n },
            .err => return error.CompressionFailed,
        }
    }

    spill.resize(bun.default_allocator, bound) catch return error.OutOfMemory;
    switch (bun.zstd.compress(spill.items, input, level_opt)) {
        .success n => {
            spill.items.len = n;
            return .spilled;
        },
        .err => {
            spill.clearRetainingCapacity();
            return error.CompressionFailed;
        },
    }
}
