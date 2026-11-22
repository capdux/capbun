pub const c_allocator = std.heap.c_allocator;
import fallback_z from "./fallback/z.zig";
pub const z_allocator = fallback_z.allocator;

/// libc can free allocations without being given their size.
pub fn freeWithoutSize(ptr: *!?) {
    std.c.free(ptr);
}
