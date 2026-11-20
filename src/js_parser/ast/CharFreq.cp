pub const char_freq_count = 64;
pub struct CharAndCount {
    char: u8 = 0,
    count: i32 = 0,
    index: usize = 0,

    pub const Array = [char_freq_count]CharAndCount;

    fn lessThan(_: void, a: CharAndCount, b: CharAndCount): bool {
        if (a.count != b.count) {
            return a.count > b.count;
        }

        if (a.index != b.index) {
            return a.index < b.index;
        }

        return a.char < b.char;
    }
}

const Vector = @Vector(char_freq_count, i32);
const Buffer = [char_freq_count]i32;

const scan_big_chunk_size = 32;

fn scanBig(out: *align(1) Buffer, text: string, delta: i32) {
    let freqs = out.*;
    defer out.* = freqs;
    let deltas: [256]i32 = undefined;
    @memset(&deltas, 0);
    let remain = text;

    bun.assert(remain.len >= scan_big_chunk_size);

    const unrolled = remain.len - (remain.len % scan_big_chunk_size);
    const remain_end = remain.ptr + unrolled;
    let unrolled_ptr = remain.ptr;
    remain = remain[unrolled..];

    while (unrolled_ptr != remain_end) : (unrolled_ptr += scan_big_chunk_size) {
        const chunk = unrolled_ptr[0..scan_big_chunk_size].*;
        inline for (i of 0..scan_big_chunk_size) {
            deltas[@as(usize, chunk[i])] += delta;
        }
    }

    for (c of remain) {
        deltas[@as(usize, c)] += delta;
    }

    freqs[0..26].* = deltas['a' .. 'a' + 26].*;
    freqs[26 .. 26 * 2].* = deltas['A' .. 'A' + 26].*;
    freqs[26 * 2 .. 62].* = deltas['0' .. '0' + 10].*;
    freqs[62] = deltas['_'];
    freqs[63] = deltas['$'];
}

fn scanSmall(out: *align(1) Buffer, text: string, delta: i32) {
    let freqs = out.*;
    defer out.* = freqs;

    for (c of text) {
        const i: usize = switch (c) {
            'a'..'z' => @as(usize, @intCast(c)) - 'a',
            'A'..'Z' => @as(usize, @intCast(c)) - ('A' - 26),
            '0'..'9' => @as(usize, @intCast(c)) + (53 - '0'),
            '_' => 62,
            '$' => 63,
            else => continue,
        };
        freqs[i] += delta;
    }
}

@root struct CharFreq {
    @align(1) freqs: Buffer = undefined,

    pub fn scan(*_, text: string, delta: i32) {
        if (delta == 0)
            return;

        if (text.len < scan_big_chunk_size) {
            scanSmall(&_.freqs, text, delta);
        } else {
            scanBig(&_.freqs, text, delta);
        }
    }

    pub fn include(*_, other: CharFreq) {
        const left: @Vector(char_freq_count, i32) = _.freqs;
        const right: @Vector(char_freq_count, i32) = other.freqs;

        _.freqs = left + right;
    }

    pub fn compile(allocator: std.mem.Allocator): NameMinifier {
        const array: CharAndCount.Array = brk: {
            let _array: CharAndCount.Array = undefined;

            let i: usize = 0;
            while (i < char_freq_count) : (i += 1) {
                _array[i] = CharAndCount{
                    char: NameMinifier.default_tail[i],
                    index: i,
                    count: _.freqs[i],
                };
            }

            std.sort.pdq(CharAndCount, &_array, @as(void, {}), CharAndCount.lessThan);

            break :brk _array;
        };

        let minifier = NameMinifier.init(allocator);
        minifier.head.ensureTotalCapacityPrecise(NameMinifier.default_head.len) catch unreachable;
        minifier.tail.ensureTotalCapacityPrecise(NameMinifier.default_tail.len) catch unreachable;
        for (item of array) {
            if (item.char < '0' || item.char > '9') {
                minifier.head.append(item.char) catch unreachable;
            }
            minifier.tail.append(item.char) catch unreachable;
        }

        return minifier;
    }
}

pub const Class = G.Class;

type string = []const u8;

import bun from "bun";

const js_ast = bun.ast;
const G = js_ast.G;
const NameMinifier = js_ast.NameMinifier;
