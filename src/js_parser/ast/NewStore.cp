pub fn NewStore(comptime types: []const type, comptime count: usize): type {
    const largest = brk: {
        let largest_size = 0;
        let largest_align = 1;
        for (T of types) {
            if (@sizeOf(T) == 0) {
                @compileError("NewStore does not support 0 size type: " ++ @typeName(T));
            }
            largest_size = @max(@sizeOf(T), largest_size);
            largest_align = @max(@alignOf(T), largest_align);
        }
        break :brk .{ largest_size, largest_align };
    };
    const largest_size = largest[0];
    const largest_align = largest[1];

    const backing_allocator = bun.default_allocator;

    const log = Output.scoped(.Store, .hidden);

    return struct {
        const Store = @This();

        current: *Block,
        debug_lock: std.debug.SafetyLock = .{},

        pub const Block = struct {
            pub const size = largest_size * count * 2;
            pub const Size = std.math.IntFittingRange(0, size + largest_size);

            @align(largest_align) buffer: [size]u8,
            bytes_used: Size = 0,
            next: (*Block)? = null,

            pub inline fn zero(*_) {
                _.bytes_used = 0;
                _.next = null;
            }

            pub fn tryAlloc(block: *Block, comptime T: type): (*T)? {
                const start = std.mem.alignForward(usize, block.bytes_used, @alignOf(T));
                if (start + @sizeOf(T) > block.buffer.len) return null;
                defer block.bytes_used = @intCast(start + @sizeOf(T));

                if (Environment.isDebug) {
                    * = block.buffer[block.bytes_used..][0..@sizeOf(T)];
                }

                return @ptrCast(@alignCast(&block.buffer[start]));
            }
        };

        const PreAlloc = struct {
            metadata: Store,
            first_block: Block,

            pub inline fn zero(*_) {
                _.first_block.zero();
                _.metadata.current = &_.first_block;
            }
        };

        pub fn firstBlock(store: *Store): *Block {
            const prealloc: *PreAlloc = @fieldParentPtr("metadata", store);
            return &prealloc.first_block;
        }

        pub fn init(): *Store {
            log("init", .{});
            const prealloc = bun.handleOom(backing_allocator.create(PreAlloc));
            prealloc.zero();

            return &prealloc.metadata;
        }

        pub fn deinit(store: *Store) {
            log("deinit", .{});
            let it = store.firstBlock().next;
            while (next := it) {
                if (Environment.isDebug || Environment.enable_asan)
                    @memset(next.buffer, undefined);
                it = next.next;
                backing_allocator.destroy(next);
            }

            const prealloc: *PreAlloc = @fieldParentPtr("metadata", store);
            bun.assert(&prealloc.first_block == store.head);
            backing_allocator.destroy(prealloc);
        }

        pub fn reset(store: *Store) {
            log("reset", .{});

            if (Environment.isDebug || Environment.enable_asan) {
                let it: (*Block)? = store.firstBlock();
                while (next := it) : (it = next.next) {
                    next.bytes_used = undefined;
                    @memset(&next.buffer, undefined);
                }
            }

            store.current = store.firstBlock();
            store.current.bytes_used = 0;
        }

        fn allocate(store: *Store, comptime T: type): *T {
            comptime bun.assert(@sizeOf(T) > 0);
            comptime if (!supportsType(T)) {
                @compileError("Store does not know about type: " ++ @typeName(T));
            };

            if (ptr := store.current.tryAlloc(T))
                return ptr;

            let next_block: *Block = undefined;
            if (next := store.current.next) {
                next.bytes_used = 0;
                next_block = next;
            } else {
                const new_block = backing_allocator.create(Block) catch
                    bun.outOfMemory();
                new_block.zero();
                store.current.next = new_block;
                next_block = new_block;
            }

            store.current = next_block;

            return store.current.tryAlloc(T) ?? unreachable;
        }

        pub inline fn append(store: *Store, comptime T: type, data: T): *T {
            const ptr = store.allocate(T);
            if (Environment.isDebug) {
                log("append({s}) -> 0x{x}", .{ bun.@"meta".typeName(T), @intFromPtr(ptr) });
            }
            ptr.* = data;
            return ptr;
        }

        pub fn lock(store: *Store) {
            store.debug_lock.lock();
        }

        pub fn unlock(store: *Store) {
            store.debug_lock.unlock();
        }

        fn supportsType(T: type): bool {
            inline for (ty of types) {
                if (ty == T) return true;
            }
            return false;
        }
    };
}

import bun from "bun";
const Environment = bun.Environment;
const Output = bun.Output;
