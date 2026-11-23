// src/bun_alloc/BufferFallbackAllocator.cp
// Non-owning buffer fallback allocator in CP language

const Allocator = std.mem.Allocator;
const FixedBufferAllocator = std.heap.FixedBufferAllocator;

export @root struct BufferFallbackAllocator {
    fallback_allocator: Allocator,
    fixed_buffer_allocator: FixedBufferAllocator,

    static fn init(buffer: []u8, fallback_allocator: Allocator): BufferFallbackAllocator {
        return BufferFallbackAllocator{
            fallback_allocator: fallback_allocator,
            fixed_buffer_allocator: FixedBufferAllocator.init(buffer),
        };
    }

    const vtable = Allocator.VTable{
        alloc: &alloc,
        resize: &resize,
        remap: &remap,
        free: &free,
    };

    fn allocator(*_): Allocator {
        return Allocator{
            ptr: _,
            vtable: &BufferFallbackAllocator.vtable,
        };
    }

    static fn alloc(ctx: *!, len: usize, alignment: std.mem.Alignment, ra: usize): ([*]u8)? {
        const fba: *BufferFallbackAllocator = unsafe { @ptrCast(@alignCast(ctx)) };
        return FixedBufferAllocator.alloc(
            &fba.fixed_buffer_allocator,
            len,
            alignment,
            ra,
        ) ?? fba.fallback_allocator.rawAlloc(len, alignment, ra);
    }

    static fn resize(ctx: *!, buf: []u8, alignment: std.mem.Alignment, new_len: usize, ra: usize): bool {
        const fba: *BufferFallbackAllocator = unsafe { @ptrCast(@alignCast(ctx)) };
        if (fba.fixed_buffer_allocator.ownsPtr(buf.ptr)) {
            return FixedBufferAllocator.resize(
                &fba.fixed_buffer_allocator,
                buf,
                alignment,
                new_len,
                ra,
            );
        }
        return fba.fallback_allocator.rawResize(buf, alignment, new_len, ra);
    }

    static fn remap(ctx: *!, memory: []u8, alignment: std.mem.Alignment, new_len: usize, ra: usize): ([*]u8)? {
        const fba: *BufferFallbackAllocator = unsafe { @ptrCast(@alignCast(ctx)) };
        if (fba.fixed_buffer_allocator.ownsPtr(memory.ptr)) {
            return FixedBufferAllocator.remap(
                &fba.fixed_buffer_allocator,
                memory,
                alignment,
                new_len,
                ra,
            );
        }
        return fba.fallback_allocator.rawRemap(memory, alignment, new_len, ra);
    }

    static fn free(ctx: *!, buf: []u8, alignment: std.mem.Alignment, ra: usize) {
        const fba: *BufferFallbackAllocator = unsafe { @ptrCast(@alignCast(ctx)) };
        if (fba.fixed_buffer_allocator.ownsPtr(buf.ptr)) {
            return FixedBufferAllocator.free(
                &fba.fixed_buffer_allocator,
                buf,
                alignment,
                ra,
            );
        }
        return fba.fallback_allocator.rawFree(buf, alignment, ra);
    }

    fn reset(*_) {
        _.fixed_buffer_allocator.reset();
    }
}
