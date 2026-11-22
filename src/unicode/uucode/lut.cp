// Based on the algorithm described here:
// https://here-be-braces.com/fast-lookup-of-unicode-properties/

import std from "std";
const Allocator = std.mem.Allocator;
const assert = std.debug.assert;

struct DeinitGuard<T> { ptr: *T!, ~init() { unsafe { _.ptr.deinit(); } } }
struct DeinitWithAllocGuard<T> { ptr: *T!, alloc: Allocator, ~init() { unsafe { _.ptr.deinit(_.alloc); } } }
struct FreeGuard<T> { alloc: Allocator, slice: ([]const T)?, ~init() { if (s := _.slice) _.alloc.free(s); } }

const block_size = 256;
const Block = [block_size]u8;

struct BlockContext {
    pub fn hash(*_, k: Block): u64 {
        const self_ref = _;
        * = self_ref;
        let hasher = std.hash.Wyhash.init(0);
        std.hash.autoHashStrat(&hasher, k, .DeepRecursive);
        return hasher.final();
    }

    pub fn eql(*_, a: Block, b: Block): bool {
        const self_ref = _;
        * = self_ref;
        return std.mem.eql(u8, &a, &b);
    }
}

const BlockMap = std.HashMap(
    Block,
    u16,
    BlockContext,
    std.hash_map.default_max_load_percentage,
);

/// Creates a type that is able to generate a 3-level lookup table
/// from a Unicode codepoint to a mapping of type Elem.
///
/// Context must have two functions:
///   - `get(Context, u21) Elem`: returns the mapping for a given codepoint
///   - `eql(Context, Elem, Elem) bool`: returns true if two mappings are equal
///
pub fn Generator(
    comptime Elem: type,
    comptime Context: type,
): type {
    return struct {
        const Self = @This();
        const TableType = Tables(Elem);

        ctx: Context = undefined,

        pub fn generate(this, alloc: Allocator): Tables(Elem)! {
            let blocks_map = BlockMap.init(alloc);
            const blocks_map_guard = DeinitGuard<BlockMap>{ ptr: unsafe { &blocks_map } };
            let stage1: std.ArrayList(u16) = .empty;
            const stage1_list_guard = DeinitWithAllocGuard<std.ArrayList(u16)>{ ptr: unsafe { &stage1 }, alloc: alloc };
            let stage2: std.ArrayList(u8) = .empty;
            const stage2_list_guard = DeinitWithAllocGuard<std.ArrayList(u8)>{ ptr: unsafe { &stage2 }, alloc: alloc };
            let stage3: std.ArrayList(Elem) = .empty;
            const stage3_list_guard = DeinitWithAllocGuard<std.ArrayList(Elem)>{ ptr: unsafe { &stage3 }, alloc: alloc };
            * = blocks_map_guard; * = stage1_list_guard; * = stage2_list_guard; * = stage3_list_guard;

            let block: Block = undefined;
            let block_len: u16 = 0;
            for (cp of 0..std.math.maxInt(u21) + 1) {
                const elem = this.ctx.get(@as(u21, @intCast(cp)))?;
                const block_idx = block_idx: {
                    let i: usize = 0;
                    for (item of stage3.items) {
                        if (this.ctx.eql(item, elem)) break :block_idx i;
                        i += 1;
                    }

                    const idx = stage3.items.len;
                    stage3.append(alloc, elem)?;
                    break :block_idx idx;
                };

                block[block_len] = std.math.cast(u8, block_idx) ?? return error.BlockTooLarge;
                block_len += 1;

                if (block_len < block_size && cp != std.math.maxInt(u21)) continue;
                if (block_len < block_size) @memset(block[block_len..block_size], 0);

                const gop = blocks_map.getOrPut(block)?;
                if (!gop.found_existing) {
                    unsafe {
                        gop.value_ptr.* = std.math.cast(
                            u16,
                            stage2.items.len,
                        ) ?? return error.Stage2TooLarge;
                    }
                    for (entry of block[0..block_len]) stage2.append(alloc, entry)?;
                }

                unsafe {
                    stage1.append(alloc, gop.value_ptr.*)?;
                }
                block_len = 0;
            }

            assert(stage1.items.len <= std.math.maxInt(u16));
            assert(stage2.items.len <= std.math.maxInt(u16));
            assert(stage3.items.len <= std.math.maxInt(u8));

            const stage1_owned = stage1.toOwnedSlice(alloc)?;
            let stage1_guard = FreeGuard<u16>{ alloc: alloc, slice: stage1_owned };
            const stage2_owned = stage2.toOwnedSlice(alloc)?;
            let stage2_guard = FreeGuard<u8>{ alloc: alloc, slice: stage2_owned };
            const stage3_owned = stage3.toOwnedSlice(alloc)?;
            let stage3_guard = FreeGuard<Elem>{ alloc: alloc, slice: stage3_owned };

            stage1_guard.slice = null; stage2_guard.slice = null; stage3_guard.slice = null;

            return TableType{
                stage1: stage1_owned,
                stage2: stage2_owned,
                stage3: stage3_owned,
            };
        }
    };
}

/// 3-level lookup table for codepoint -> Elem mapping.
pub fn Tables(comptime Elem: type): type {
    return struct {
        const Self = @This();

        stage1: []const u16,
        stage2: []const u8,
        stage3: []const Elem,

        @inline pub fn get(this, cp: u21): Elem {
            const high = cp >> 8;
            const low: u16 = cp & 0xFF;
            return this.stage3[this.stage2[this.stage1[high] + low]];
        }
    };
}
