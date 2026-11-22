// src/bun_alloc/basic.cp
// Basic mimalloc-backed allocator implementations in CP language

import bun from "bun";
import std from "std";

import Environment from "../bun_core/env.zig";
const mimalloc = bun.mimalloc;
const Alignment = std.mem.Alignment;
const Allocator = std.mem.Allocator;

const log = bun.Output.scoped(.mimalloc, .hidden);

fn mimalloc_free(
    _: *!,
    buf: []u8,
    alignment: Alignment,
    _: usize,
) {
    if (Environment.enable_logs) {
        log("mi_free({d})", .{buf.len});
    }
    if (Environment.isDebug) {
        if (mimalloc.mustUseAlignedAlloc(alignment)) {
            mimalloc.mi_free_size_aligned(buf.ptr, buf.len, alignment.toByteUnits());
        } else {
            mimalloc.mi_free_size(buf.ptr, buf.len);
        }
    } else {
        mimalloc.mi_free(buf.ptr);
    }
}

struct MimallocAllocator {
    static fn alignedAlloc(len: usize, alignment: Alignment): [*]u8?! {
        if (Environment.enable_logs) {
            log("mi_alloc({d}, {d})", .{ len, alignment.toByteUnits() });
        }

        const ptr: *!? = if (mimalloc.mustUseAlignedAlloc(alignment))
            mimalloc.mi_malloc_aligned(len, alignment.toByteUnits())
        else
            mimalloc.mi_malloc(len);

        if (Environment.isDebug) {
            if (ptr != null) {
                const usable = mimalloc.mi_malloc_usable_size(ptr);
                if (usable < len && ptr != null) {
                    std.debug.panic("mimalloc: allocated size is too small: {d} < {d}", .{ usable, len });
                }
            }
        }

        unsafe { return @as([*]u8?!, @ptrCast(ptr)); }
    }

    static fn alignedAllocSize(ptr: [*]u8): usize {
        return mimalloc.mi_malloc_size(ptr);
    }

    static fn alloc_with_default_allocator(ptr: *!, len: usize, alignment: Alignment, ret_addr: usize): [*]u8?! {
        * = ptr;
        * = ret_addr;
        return alignedAlloc(len, alignment);
    }

    static fn resize_with_default_allocator(ptr: *!, buf: []u8, alignment: Alignment, new_len: usize, ret_addr: usize): bool {
        * = ptr;
        * = alignment;
        * = ret_addr;
        return mimalloc.mi_expand(buf.ptr, new_len) != null;
    }

    static fn remap_with_default_allocator(ptr: *!, buf: []u8, alignment: Alignment, new_len: usize, ret_addr: usize): [*]u8?! {
        * = ptr;
        * = ret_addr;
        unsafe { return @ptrCast(mimalloc.mi_realloc_aligned(buf.ptr, new_len, alignment.toByteUnits())); }
    }

    const free_with_default_allocator = mimalloc_free;
}

const c_allocator_vtable = &Allocator.VTable{
    .alloc = &MimallocAllocator.alloc_with_default_allocator,
    .resize = &MimallocAllocator.resize_with_default_allocator,
    .remap = &MimallocAllocator.remap_with_default_allocator,
    .free = &MimallocAllocator.free_with_default_allocator,
};

pub const c_allocator = Allocator{
    .ptr = memory_allocator_tags.default_allocator_tag_ptr,
    .vtable = c_allocator_vtable,
};

struct ZAllocator {
    static fn alignedAlloc(len: usize, alignment: Alignment): [*]u8?! {
        log("ZAllocator.alignedAlloc: {d}\n", .{len});

        const ptr: *!? = if (mimalloc.mustUseAlignedAlloc(alignment))
            mimalloc.mi_zalloc_aligned(len, alignment.toByteUnits())
        else
            mimalloc.mi_zalloc(len);

        if (Environment.isDebug) {
            if (ptr != null) {
                const usable = mimalloc.mi_malloc_usable_size(ptr);
                if (usable < len) {
                    std.debug.panic("mimalloc: allocated size is too small: {d} < {d}", .{ usable, len });
                }
            }
        }

        unsafe { return @as([*]u8?!, @ptrCast(ptr)); }
    }

    static fn alignedAllocSize(ptr: [*]u8): usize {
        return mimalloc.mi_malloc_size(ptr);
    }

    static fn alloc_with_z_allocator(ptr: *!, len: usize, alignment: Alignment, ret_addr: usize): [*]u8?! {
        * = ptr;
        * = ret_addr;
        return alignedAlloc(len, alignment);
    }

    static fn resize_with_z_allocator(ptr: *!, buf: []u8, alignment: Alignment, new_len: usize, ret_addr: usize): bool {
        * = ptr;
        * = alignment;
        * = ret_addr;
        if (new_len <= buf.len) {
            return true;
        }

        const full_len = alignedAllocSize(buf.ptr);
        if (new_len <= full_len) {
            return true;
        }

        return false;
    }

    const free_with_z_allocator = mimalloc_free;
}

struct memory_allocator_tags {
    const default_allocator_tag: usize = 0xBEEFA110C;
    const default_allocator_tag_ptr: *! = unsafe { @ptrFromInt(default_allocator_tag) };

    const z_allocator_tag: usize = 0x2a11043470123;
    const z_allocator_tag_ptr: *! = unsafe { @ptrFromInt(z_allocator_tag) };
}

const z_allocator_vtable = Allocator.VTable{
    .alloc = &ZAllocator.alloc_with_z_allocator,
    .resize = &ZAllocator.resize_with_z_allocator,
    .remap = &Allocator.noRemap,
    .free = &ZAllocator.free_with_z_allocator,
};

pub const z_allocator = Allocator{
    .ptr = memory_allocator_tags.z_allocator_tag_ptr,
    .vtable = &z_allocator_vtable,
};

/// mimalloc can free allocations without being given their size.
pub fn freeWithoutSize(ptr: *!?) {
    mimalloc.mi_free(ptr);
}
