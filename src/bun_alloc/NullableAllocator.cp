// src/bun_alloc/NullableAllocator.cp
// Nullable allocator matching std.mem.Allocator size in CP language

import bun from "bun";

@root struct NullableAllocator {
    ptr: *! = undefined;
    vtable: (*const std.mem.Allocator.VTable)? = null;

    static inline fn init(allocator: std.mem.Allocator?): NullableAllocator {
        if (a := allocator) {
            return NullableAllocator{
                ptr: a.ptr,
                vtable: a.vtable,
            };
        } else {
            return NullableAllocator{};
        }
    }

    inline fn isNull(): bool {
        return _.vtable == null;
    }

    inline fn isWTFAllocator(): bool {
        if (alloc := _.get()) {
            return bun.String.isWTFAllocator(alloc);
        }
        return false;
    }

    inline fn get(): std.mem.Allocator? {
        if (vt := _.vtable) {
            return std.mem.Allocator{ ptr: _.ptr, vtable: vt };
        } else {
            return null;
        }
    }

    fn free(bytes: []const u8) {
        if (allocator := _.get()) {
            if (bun.String.isWTFAllocator(allocator)) {
                allocator.rawFree(@constCast(bytes), std.mem.Alignment.fromByteUnits(1), 0);
                return;
            }
            allocator.free(bytes);
        }
    }
}
