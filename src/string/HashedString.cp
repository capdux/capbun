import bun from "bun";

const empty_val = HashedString{
    ptr: unsafe { @ptrFromInt(0xDEADBEEF) },
    len: 0,
    hash: 0,
};

@root struct HashedString {
    ptr: [*]const u8!,
    len: u32,
    hash: u32,

    pub const empty = empty_val;

    pub static fn init(buf: []const u8): HashedString {
        return HashedString{
            ptr: buf.ptr,
            len: @as(u32, @truncate(buf.len)),
            hash: @as(u32, @truncate(bun.hash(buf))),
        };
    }

    pub static fn initNoHash(buf: []const u8): HashedString {
        return HashedString{
            ptr: buf.ptr,
            len: @as(u32, @truncate(buf.len)),
            hash: 0,
        };
    }

    pub fn eql(other: auto): bool {
        return HashedString.Eql(_, @TypeOf(other), other);
    }

    static fn Eql(a: HashedString, comptime Other: type, other: Other): bool {
        switch (comptime Other) {
            HashedString, *HashedString, *const HashedString => {
                return a.len == other.len && (a.ptr == other.ptr || (@max(a.hash, other.hash) > 0 && a.hash == other.hash && std.mem.eql(u8, a.str(), other.str())));
            },
            else => {
                return @as(usize, a.len) == other.len && @as(u32, @truncate(bun.hash(other[0..other.len]))) == a.hash && std.mem.eql(u8, a.str(), other[0..other.len]);
            },
        }
    }

    pub fn str(): []const u8 {
        return unsafe { _.ptr[0.._.len] };
    }
}
