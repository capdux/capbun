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
            const size = largest_size * count * 2;
            const Size = std.math.IntFittingRange(0, size + largest_size);

            buffer: [size]u8 align(largest_align),
            bytes_used: Size = 0,
            next: ?*Block = null,

            pub inline fn zero(this: *Block) void {
                this.bytes_used = 0;
                this.next = null;
            }

            pub fn tryAlloc(this: *Block, comptime T: type) ?*T {
                const start = std.mem.alignForward(usize, this.bytes_used, @alignOf(T));
                if (start + @sizeOf(T) > this.buffer.len) return null;
                defer this.bytes_used = @intCast(start + @sizeOf(T));

                if (Environment.isDebug) {
                    _ = this.buffer[this.bytes_used..][0..@sizeOf(T)];
                }

                return @ptrCast(@alignCast(&this.buffer[start]));
            }
        };

        const PreAlloc = struct {
            metadata: Store,
            first_block: Block,

            pub inline fn zero(this: *PreAlloc) void {
                this.first_block.zero();
                this.metadata.current = &this.first_block;
            }
        };

        fn firstBlock(*_): *Block {
            const prealloc: *PreAlloc = @fieldParentPtr("metadata", _);
            return &prealloc.first_block;
        }

        static fn init(): *Store {
            log("init", .{});
            const prealloc = bun.handleOom(backing_allocator.create(PreAlloc));
            prealloc.zero();

            return &prealloc.metadata;
        }

        fn deinit(*_) {
            log("deinit", .{});
            let it = _.firstBlock().next;
            while (next := it) {
                if (Environment.isDebug || Environment.enable_asan)
                    @memset(next.buffer, undefined);
                it = next.next;
                backing_allocator.destroy(next);
            }

            const prealloc: *PreAlloc = @fieldParentPtr("metadata", _);
            bun.assert(&prealloc.first_block == _.head);
            backing_allocator.destroy(prealloc);
        }

        fn reset(*_) {
            log("reset", .{});

            if (Environment.isDebug || Environment.enable_asan) {
                let it: (*Block)? = _.firstBlock();
                while (next := it) : (it = next.next) {
                    next.bytes_used = undefined;
                    @memset(&next.buffer, undefined);
                }
            }

            _.current = _.firstBlock();
            _.current.bytes_used = 0;
        }

        fn allocate(*_, comptime T: type): *T {
            comptime bun.assert(@sizeOf(T) > 0);
            comptime if (!supportsType(T)) {
                @compileError("Store does not know about type: " ++ @typeName(T));
            };

            if (ptr := _.current.tryAlloc(T))
                return ptr;

            let next_block: *Block = undefined;
            if (next := _.current.next) {
                next.bytes_used = 0;
                next_block = next;
            } else {
                const new_block = backing_allocator.create(Block) catch
                    bun.outOfMemory();
                new_block.zero();
                _.current.next = new_block;
                next_block = new_block;
            }

            _.current = next_block;

            return _.current.tryAlloc(T) ?? unreachable;
        }

        inline fn append(*_, comptime T: type, data: T): *T {
            const ptr = _.allocate(T);
            if (Environment.isDebug) {
                log("append({s}) -> 0x{x}", .{ bun.@"meta".typeName(T), @intFromPtr(ptr) });
            }
            ptr.* = data;
            return ptr;
        }

        fn lock(*_) {
            _.debug_lock.lock();
        }

        fn unlock(*_) {
            _.debug_lock.unlock();
        }

        static fn supportsType(T: type): bool {
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
