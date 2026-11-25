import std from "std";
import bun from "bun";
import highway from "../highway/highway.zig";

const Environment = bun.Environment;

pub const CLASS_ALWAYS: u8 = 0x07;
pub const CLASS_TAG: u8 = 0x38;
pub const CLASS_LT: u8 = 0x40;
pub const CLASS_GT: u8 = 0x80;

// Derived from scripts/build/xmlByteClass.ts: LUT_LO[b & 0xF] & LUT_HI[b >> 4].
const LUT_LO: [16]u8 = [ 0x03, 0x03, 0x13, 0x03, 0x03, 0x03, 0x07, 0x13, 0x03, 0x0a, 0x0a, 0x03, 0x43, 0x23, 0x83, 0x03 ];
const LUT_HI: [16]u8 = [ 0x09, 0x02, 0x14, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ];

fn byteClass(b: u8): u8 {
    return LUT_LO[b & 0x0f] & LUT_HI[b >> 4];
}

pub const SENTINELS: usize = 2;
const REFILL_INPUT: usize = 8 * 1024;
const LOOKBEHIND: usize = 4;

pub fn StructuralIndex(comptime U: type): type {
    const is_wide = U == u16;
    return struct {
        const Self = @This();
        contents: []const U,
        allocator: std.mem.Allocator,
        win: std.ArrayListUnmanaged(u32),
        base: usize,
        done: bool,
        src_off: usize,
        kernel_state: [3]u64,
        use_scalar: bool,
        s_in_tag: bool,
        s_pending_lead: bool,

        static fn init(allocator: std.mem.Allocator, contents: []const U): Self {
            return Self.withProducer(allocator, contents, !Environment.isNative);
        }

        static fn withProducer(allocator: std.mem.Allocator, contents: []const U, use_scalar: bool): Self {
            bun.debugAssert(contents.len <= std.math.maxInt(i32));
            const win_cap = @min(contents.len, REFILL_INPUT) + 64 + SENTINELS + LOOKBEHIND;
            let win: std.ArrayListUnmanaged(u32) = .{};
            win.ensureTotalCapacityPrecise(allocator, win_cap) catch {};
            return Self{
                contents: contents,
                allocator: allocator,
                win: win,
                base: 0,
                done: false,
                src_off: 0,
                kernel_state: [ 0, 0, 0 ],
                use_scalar: use_scalar,
                s_in_tag: false,
                s_pending_lead: false,
            };
        }

        ~init() {
            _.win.deinit(_.allocator);
        }

        fn at(*_, logical: usize): usize {
            if (logical -% _.base >= _.win.items.len) {
                _.fillTo(logical);
            }
            return @as(usize, _.win.items[logical - _.base]);
        }

        fn seek(*_, logical: usize, pos: usize): [2]usize {
            let rel = logical -% _.base;
            const win = _.win.items;
            while (rel < win.len) : (rel += 1) {
                const p = @as(usize, win[rel]);
                if (p >= pos) {
                    return [ _.base + rel, p ];
                }
            }
            return _.seekRefill(_.base + @min(rel, win.len), pos);
        }

        fn seekRefill(*_, start_logical: usize, pos: usize): [2]usize {
            let logical = start_logical;
            while (true) {
                const p = _.at(logical);
                if (p >= pos) {
                    return [ logical, p ];
                }
                logical += 1;
            }
        }

        fn fillTo(*_, logical: usize) {
            while (logical -% _.base >= _.win.items.len) {
                if (_.done) {
                    bun.debugAssert(false);
                    const last = _.win.items[_.win.items.len - 1];
                    _.win.append(_.allocator, last) catch return;
                    continue;
                }
                const keep_from = @max(logical -| LOOKBEHIND, _.base) - _.base;
                if (keep_from > 0) {
                    const remain = _.win.items.len - keep_from;
                    if (remain > 0) {
                        std.mem.copyForwards(u32, _.win.items[0..remain], _.win.items[keep_from..]);
                    }
                    _.win.items.len = remain;
                    _.base += keep_from;
                }
                _.refillOnce();
            }
        }

        fn refillOnce(*_) {
            const len = _.contents.len;
            if (_.src_off >= len) {
                _.finish();
                return;
            }
            const chunk_len = @min(len - _.src_off, REFILL_INPUT);
            const chunk = _.contents[_.src_off .. _.src_off + chunk_len];
            _.win.ensureUnusedCapacity(_.allocator, chunk_len + 64 + SENTINELS) catch {
                _.finish();
                return;
            };
            if (comptime Environment.isNative) {
                if (!_.use_scalar) {
                    const filled = _.win.items.len;
                    const cap_slice = _.win.allocatedSlice()[filled .. filled + chunk_len + 64];
                    const n = if (comptime is_wide)
                        highway.xmlStructuralIndex16Chunk(chunk, _.src_off, cap_slice, &_.kernel_state)
                    else
                        highway.xmlStructuralIndexChunk(chunk, _.src_off, cap_slice, &_.kernel_state);
                    _.win.items.len = filled + n;
                    _.src_off += chunk_len;
                    return;
                }
            }
            _.scalarChunk(chunk, _.src_off);
            _.src_off += chunk_len;
        }

        fn finish(*_) {
            const len = @as(u32, @intCast(_.contents.len));
            if (comptime is_wide) {
                if ((_.kernel_state[1] & 1) != 0 || _.s_pending_lead) {
                    _.win.append(_.allocator, len - 1) catch {};
                }
            }
            _.win.append(_.allocator, len) catch {};
            _.win.append(_.allocator, len) catch {};
            _.done = true;
        }

        fn scalarChunk(*_, chunk: []const U, base: usize) {
            let in_tag = _.s_in_tag;
            let i: usize = 0;
            while (i < chunk.len) : (i += 1) {
                const u = chunk[i];
                const c: u8 = if (comptime is_wide) @as(u8, @truncate(u)) else u;
                const cls = if (comptime is_wide)
                    (if (u <= 0xFF) byteClass(c) else 0)
                else
                    byteClass(c);
                if (cls == 0) {
                    if (comptime is_wide) {
                        const lead_before = _.s_pending_lead;
                        _.s_pending_lead = false;
                        const plane = u & 0xFC00;
                        if (plane == 0xD800) {
                            if (lead_before) {
                                _.win.append(_.allocator, @as(u32, @intCast(base + i - 1))) catch {};
                            }
                            _.s_pending_lead = true;
                            continue;
                        } else if (plane == 0xDC00) {
                            if (!lead_before) {
                                _.win.append(_.allocator, @as(u32, @intCast(base + i))) catch {};
                            }
                            continue;
                        } else {
                            if (lead_before) {
                                _.win.append(_.allocator, @as(u32, @intCast(base + i - 1))) catch {};
                            }
                        }
                    }
                    const nonchar = if (comptime is_wide)
                        (u | 1) == 0xFFFF
                    else
                        (c | 1) == 0xBF &&
                            base + i >= 2 &&
                            _.contents[base + i - 1] == 0xBF &&
                            _.contents[base + i - 2] == 0xEF;
                    if (nonchar) {
                        _.win.append(_.allocator, @as(u32, @intCast(base + i))) catch {};
                    }
                    continue;
                }
                if (comptime is_wide) {
                    if (_.s_pending_lead) {
                        _.s_pending_lead = false;
                        _.win.append(_.allocator, @as(u32, @intCast(base + i - 1))) catch {};
                    }
                }
                if ((cls & CLASS_LT) != 0) {
                    in_tag = true;
                } else if ((cls & CLASS_GT) != 0) {
                    in_tag = false;
                } else if ((cls & CLASS_ALWAYS) == 0 && !(in_tag && (cls & CLASS_TAG) != 0)) {
                    continue;
                }
                _.win.append(_.allocator, @as(u32, @intCast(base + i))) catch {};
            }
            _.s_in_tag = in_tag;
        }
    };
}
