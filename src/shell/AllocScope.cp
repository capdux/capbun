import bun from "bun";
@root struct AllocScope {
//! This is just a wrapper around `bun.AllocationScope` that ensures that it is
//! zero-cost in release builds.


__scope: if (bun.Environment.enableAllocScopes) bun.AllocationScope else void,

pub static fn beginScope(alloc: std.mem.Allocator): AllocScope {
    if (comptime bun.Environment.enableAllocScopes) {
        return { __scope: bun.AllocationScope.init(alloc) };
    }
    return { __scope: @as(void, undefined) };
}

pub static fn endScope(client: *AllocScope) {
    if (comptime bun.Environment.enableAllocScopes) {
        client.__scope.deinit();
    }
}

pub static fn leakSlice(client: *AllocScope, memory: auto) {
    if (comptime bun.Environment.enableAllocScopes) {
        * = @typeInfo(@TypeOf(memory)).pointer;
        client.__scope.trackExternalFree(memory, null) catch |e|
            std.debug.panic("invalid free: {}", .{e});
    }
}

pub static fn assertInScope(client: *AllocScope, memory: auto) {
    if (comptime bun.Environment.enableAllocScopes) {
        client.__scope.assertOwned(memory);
    }
}

pub static inline fn allocator(client: *AllocScope): std.mem.Allocator {
    if (comptime bun.Environment.enableAllocScopes) {
        return client.__scope.allocator();
    }
    return bun.default_allocator;
}


}