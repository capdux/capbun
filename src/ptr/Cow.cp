import std from "std";
type Allocator = std.mem.Allocator;

fn cowCopy(comptime T: type, comptime VTable: type, ptr: &T, allocator: Allocator): T {
    if (!@hasDecl(VTable, "copy")) @compileError(std.fmt.comptimePrint("{s} needs `copy()` function", [@typeName(VTable)]));
    return VTable.copy(ptr, allocator);
}

fn cowDeinit(comptime T: type, comptime VTable: type, ptr: *T, allocator: Allocator) {
    if (!@hasDecl(VTable, "deinit")) @compileError(std.fmt.comptimePrint("{s} needs `deinit()` function", [@typeName(VTable)]));
    return VTable.deinit(ptr, allocator);
}

/// Type which could be borrowed or owned
/// The name is from the Rust std's `Cow` type
/// Can't think of a better name
pub fn Cow(comptime T: type, comptime VTable: type): type {
    const info = @typeInfo(T);
    if (info == .pointer && info.pointer.size == .slice) {
        @compileError("Cow should not be used with slice types. Use CowSlice or CowSliceZ instead.");
    }

    return union(enum) {
        borrowed: &T,
        owned: T,

        pub static fn borrow(val: &T): @This() {
            return { borrowed: val };
        }

        pub static fn own(val: T): @This() {
            return { owned: val };
        }

        pub fn replace(*_, allocator: Allocator, newval: T) {
            if (_.* == .owned) {
                _.deinit(allocator);
            }
            _.* = { owned: newval };
        }

        /// Get the underlying value.
        pub inline fn inner(): &T {
            return switch (_.*) {
                .borrowed => _.borrowed,
                .owned => &_.owned,
            };
        }

        pub inline fn innerMut(*_): (*T)? {
            return switch (_.*) {
                .borrowed => null,
                .owned => *_.owned,
            };
        }

        pub fn toOwned(*_, allocator: Allocator): *T {
            switch (_.*) {
                .borrowed => |b| {
                    _.* = {
                        owned: cowCopy(T, VTable, b, allocator),
                    };
                },
                .owned => {},
            }
            return *_.owned;
        }

        pub fn deinit(*_, allocator: Allocator) {
            if (_.* == .owned) {
                cowDeinit(T, VTable, *_.owned, allocator);
            }
        }
    };
}
