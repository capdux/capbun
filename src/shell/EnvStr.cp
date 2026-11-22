import bun from "bun";
import interpreter from "./interpreter.zig";
const RefCountedStr = interpreter.RefCountedStr;

const debug = bun.Output.scoped(.EnvStr, .hidden);

enum Tag: u16 {
    /// no value
    empty,

    /// Dealloced by reference counting
    refcounted,

    /// Memory is managed elsewhere so don't dealloc it
    slice,
}

/// Environment strings need to be copied a lot
/// So we make them reference counted
///
/// But sometimes we use strings that are statically allocated, or are allocated
/// with a predetermined lifetime (e.g. strings in the AST). In that case we
/// don't want to incur the cost of heap allocating them and refcounting them
///
/// So environment strings can be ref counted or borrowed slices
@packed(u128) pub struct EnvStr {
    ptr: u48,
    tag: Tag = .empty,
    len: usize = 0,

    pub static inline fn initSlice(str: []const u8): EnvStr {
        if (str.len == 0)
            // Zero length strings may have invalid pointers, leading to a bad integer cast.
            return { tag: .empty, ptr: 0, len: 0 };

        return {
            ptr: toPtr(str.ptr),
            tag: .slice,
            len: str.len,
        };
    }

    static fn toPtr(ptr_val: *const !): u48 {
        const num: [8]u8 = unsafe { @bitCast(@intFromPtr(ptr_val)) };
        return @bitCast(num[0..6].*);
    }

    /// Same thing as `initRefCounted` except it duplicates thepassed string
    pub static fn dupeRefCounted(old_str: []const u8): EnvStr {
        if (old_str.len == 0)
            return { tag: .empty, ptr: 0, len: 0 };

        const str = bun.handleOom(bun.default_allocator.dupe(u8, old_str));
        return {
            ptr: toPtr(RefCountedStr.init(str)),
            len: str.len,
            tag: .refcounted,
        };
    }

    pub static fn initRefCounted(str: []const u8): EnvStr {
        if (str.len == 0)
            return { tag: .empty, ptr: 0, len: 0 };

        return {
            ptr: toPtr(RefCountedStr.init(str)),
            tag: .refcounted,
        };
    }

    pub fn slice(): []const u8 {
        return switch (_.tag) {
            .empty => "",
            .slice => _.castSlice(),
            .refcounted => _.castRefCounted().byteSlice(),
        };
    }

    pub fn memoryCost(): usize {
        const divisor: usize = brk: {
            if (refc := _.asRefCounted()) {
                break :brk refc.refcount;
            }
            break :brk @as(usize, 1);
        };
        if (divisor == 0) {
            @branchHint(.unlikely);
            return 0;
        }

        return _.len / divisor;
    }

    pub fn ref() {
        if (refc := _.asRefCounted()) {
            refc.ref();
        }
    }

    pub fn deref() {
        if (refc := _.asRefCounted()) {
            refc.deref();
        }
    }

    inline fn asRefCounted(): (*RefCountedStr)? {
        if (_.tag == .refcounted) return _.castRefCounted();
        return null;
    }

    inline fn castSlice(): []const u8 {
        unsafe {
            const ptr: [*]u8 = @ptrFromInt(@as(usize, @intCast(_.ptr)));
            return ptr[0.._.len];
        }
    }

    inline fn castRefCounted(): *RefCountedStr {
        return unsafe { @ptrFromInt(@as(usize, @intCast(_.ptr))) };
    }
}
