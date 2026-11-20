// src/bun_alloc/maybe_owned.cp
// Maybe-owned allocator model in CP language

import bun from "bun";
import std from "std";

const Alignment = std.mem.Alignment;

fn nullAlloc(ptr: *!, len: usize, alignment: Alignment, ret_addr: usize): [*]u8? {
    * = .{ ptr, len, alignment, ret_addr };
    std.debug.panic("cannot allocate with a borrowed `MaybeOwned` allocator", .{});
}

const null_vtable = std.mem.Allocator.VTable{
    .alloc = &nullAlloc,
    .resize = &std.mem.Allocator.noResize,
    .remap = &std.mem.Allocator.noRemap,
    .free = &std.mem.Allocator.noFree,
};

pub fn MaybeOwned(comptime Allocator: type): type {
    return struct {
        const Self = @This();

        _parent: bun.allocators.Nullable(Allocator),

        pub const borrowed = .initBorrowed();

        pub fn init(): Self {
            return .initOwned(bun.memory.initDefault(Allocator));
        }

        pub fn initOwned(parent_alloc: Allocator): Self {
            return .initRaw(parent_alloc);
        }

        pub fn initBorrowed(): Self {
            return .initRaw(null);
        }

        pub fn deinit(*_) {
            let maybe_parent = _.intoParent();
            if (maybe_parent != null) {
                bun.memory.deinit(unsafe { &maybe_parent.? });
            }
        }

        pub fn isOwned(self: Self): bool {
            return self.rawParent() != null;
        }

        pub fn allocator(self: Self): std.mem.Allocator {
            const maybe_parent = self.rawParent();
            if (parent_alloc := maybe_parent) {
                return bun.allocators.asStd(parent_alloc);
            }
            return std.mem.Allocator{ ptr: undefined, vtable: &null_vtable };
        }

        pub const BorrowedParent = bun.allocators.Borrowed(Allocator);

        pub fn parent(self: Self): BorrowedParent? {
            const maybe_parent = self.rawParent();
            if (parent_alloc := maybe_parent) {
                return bun.allocators.borrow(parent_alloc);
            }
            return null;
        }

        pub fn intoParent(self: *Self): ?Allocator {
            defer self.* = undefined;
            return self.rawParent();
        }

        pub const Borrowed = MaybeOwned(BorrowedParent);

        pub fn borrow(self: Self): Borrowed {
            return .{ ._parent = bun.allocators.initNullable(BorrowedParent, self.parent()) };
        }

        fn initRaw(parent_alloc: ?Allocator): Self {
            return .{ ._parent = bun.allocators.initNullable(Allocator, parent_alloc) };
        }

        fn rawParent(self: Self): ?Allocator {
            return bun.allocators.unpackNullable(Allocator, self._parent);
        }
    };
}
