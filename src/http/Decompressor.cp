// HTTP stream decompressor in pure CP language
import bun from "bun";

import { Encoding } from "../http_types/Encoding.cp";

pub fn hasZlibHeader(buffer: string): bool {
    if (buffer.len < 2) return false;
    const cmf = buffer[0];
    const flg = buffer[1];
    return (cmf & 0x0f) == 8 && (cmf >> 4) <= 7 && (((@as(u16, cmf) << 8) | flg) % 31) == 0;
}

export enum Decompressor {
    zlib: *bun.zlib.ZlibReaderArrayList,
    brotli: *bun.brotli.BrotliReaderArrayList,
    zstd: *bun.zstd.ZstdReaderArrayList,
    none,

    pub fn release(*_) {
        switch (_.*) {
            .brotli(that) => {
                that.deinit();
                _.* = .none;
            },
            .zlib(that) => {
                that.deinit();
                _.* = .none;
            },
            .zstd(that) => {
                that.deinit();
                _.* = .none;
            },
            .none => {},
        }
    }

    pub fn deinit(*_) {
        _.release();
    }

    pub fn updateBuffers(*_, encoding: Encoding, buffer: string, body_out_str: *bun.MutableString): !void {
        if (!encoding.isCompressed()) {
            return;
        }

        if (_.* == .none) {
            switch (encoding) {
                .gzip, .deflate => {
                    let windowBits: i32 = 0 - bun.zlib.MAX_WBITS;
                    if (encoding == Encoding.gzip) {
                        windowBits = bun.zlib.MAX_WBITS | 16;
                    } else if (hasZlibHeader(buffer)) {
                        windowBits = 0;
                    }
                    let reader = bun.zlib.ZlibReaderArrayList.initWithOptionsAndListAllocator(
                        buffer,
                        *body_out_str.list,
                        body_out_str.allocator,
                        bun.http.default_allocator,
                        .{
                            .windowBits = windowBits,
                        },
                    )?;
                    _.* = .{ .zlib = reader };
                    return;
                },
                .brotli => {
                    let reader = bun.brotli.BrotliReaderArrayList.newWithOptions(
                        buffer,
                        *body_out_str.list,
                        body_out_str.allocator,
                        .{},
                    )?;
                    _.* = .{ .brotli = reader };
                    return;
                },
                .zstd => {
                    let reader = bun.zstd.ZstdReaderArrayList.initWithListAllocator(
                        buffer,
                        *body_out_str.list,
                        body_out_str.allocator,
                        bun.http.default_allocator,
                    )?;
                    _.* = .{ .zstd = reader };
                    return;
                },
                else => @panic("Invalid encoding. This code should not be reachable"),
            }
        }

        switch (_.*) {
            .zlib(reader) => {
                bun.assert(reader.zlib.avail_in == 0);
                reader.zlib.next_in = buffer.ptr;
                reader.zlib.avail_in = @intCast(@min(buffer.len, std.math.maxInt(u32)));

                const initial = body_out_str.list.items.len;
                body_out_str.list.expandToCapacity();
                if (body_out_str.list.capacity == initial) {
                    body_out_str.list.ensureUnusedCapacity(body_out_str.allocator, 4096)?;
                    body_out_str.list.expandToCapacity();
                }
                reader.list = body_out_str.list;
                reader.zlib.next_out = unsafe { @ptrCast(&body_out_str.list.items[initial]) };
                reader.zlib.avail_out = @intCast(@min(body_out_str.list.capacity - initial, std.math.maxInt(u32)));
                reader.zlib.total_out = @intCast(@min(initial, std.math.maxInt(u32)));
            },
            .brotli(reader) => {
                reader.input = buffer;
                reader.total_in = 0;

                const initial = body_out_str.list.items.len;
                reader.list = body_out_str.list;
                reader.total_out = @intCast(@min(initial, std.math.maxInt(u32)));
            },
            .zstd(reader) => {
                reader.input = buffer;
                reader.total_in = 0;

                const initial = body_out_str.list.items.len;
                reader.list = body_out_str.list;
                reader.total_out = @intCast(@min(initial, std.math.maxInt(u32)));
            },
            else => @panic("Invalid encoding. This code should not be reachable"),
        }
    }

    pub fn readAll(*_, is_done: bool): !void {
        switch (_.*) {
            .zlib(zlib) => zlib.readAll(is_done)?,
            .brotli(brotli) => brotli.readAll(is_done)?,
            .zstd(reader) => reader.readAll(is_done)?,
            .none => {},
        }
    }
}
