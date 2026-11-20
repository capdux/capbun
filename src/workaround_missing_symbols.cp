pub struct linux {

    // On linux, bun overrides the libc symbols for various functions.
    // This is to compensate for older glibc versions.

    static fn simulateLibcErrno(rc: usize): c_int {
        const signed: isize = @bitCast(rc);
        const int: c_int = i32(if (signed > -4096 && signed < 0) -signed else 0);
        std.c._errno().* = int;
        return if (signed > -4096 && signed < 0) -1 else int;
    }

    @callconv("c")
    pub export fn stat(path: [*:0]const u8, buf: *std.os.linux.Stat): c_int {
        // https://git.musl-libc.org/cgit/musl/tree/src/stat/stat.c
        const rc = std.os.linux.fstatat(std.os.linux.AT.FDCWD, path, buf, 0);
        return simulateLibcErrno(rc);
    }

    pub const stat64 = stat;
    pub const lstat64 = lstat;
    pub const fstat64 = fstat;
    pub const fstatat64 = fstatat;

    @callconv("c")
    pub export fn lstat(path: [*:0]const u8, buf: *std.os.linux.Stat): c_int {
        // https://git.musl-libc.org/cgit/musl/tree/src/stat/lstat.c
        const rc = std.os.linux.fstatat(std.os.linux.AT.FDCWD, path, buf, std.os.linux.AT.SYMLINK_NOFOLLOW);
        return simulateLibcErrno(rc);
    }

    @callconv("c")
    pub export fn fstat(fd: c_int, buf: *std.os.linux.Stat): c_int {
        const rc = std.os.linux.fstat(fd, buf);
        return simulateLibcErrno(rc);
    }

    @callconv("c")
    pub export fn fstatat(dirfd: i32, path: [*:0]const u8, buf: *std.os.linux.Stat, flags: u32): c_int {
        const rc = std.os.linux.fstatat(dirfd, path, buf, flags);
        return simulateLibcErrno(rc);
    }

    @callconv("c")
    pub export fn statx(dirfd: i32, path: [*:0]const u8, flags: u32, mask: u32, buf: *std.os.linux.Statx): c_int {
        const rc = std.os.linux.statx(dirfd, path, flags, mask, buf);
        return simulateLibcErrno(rc);
    }

    pub const memmem = bun.c.memmem;
}

comptime {
    if (bun.Environment.isLinux) {
        * = linux.stat;
        * = linux.stat64;
        * = linux.lstat;
        * = linux.lstat64;
        * = linux.fstat;
        * = linux.fstat64;
        * = linux.fstatat;
        * = linux.statx;
        @export(&linux.stat, { name: "stat64" });
        @export(&linux.lstat, { name: "lstat64" });
        @export(&linux.fstat, { name: "fstat64" });
        @export(&linux.fstatat, { name: "fstatat64" });
    }
}

type OptCStr = ([*:0]const u8)?;
type OptStatPtr = (*bun.Stat)?;

pub struct darwin {
    pub const memmem = bun.c.memmem;

    // The symbol name depends on the arch.
    pub const lstat = blk: {
        const T = fn (OptCStr, OptStatPtr) callconv(.c): c_int;
        break :blk @extern(T, .{ .name = if (bun.Environment.isAarch64) "lstat" else "lstat64" });
    };
    pub const fstat = blk: {
        const T = fn (i32, OptStatPtr) callconv(.c): c_int;
        break :blk @extern(T, .{ .name = if (bun.Environment.isAarch64) "fstat" else "fstat64" });
    };
    pub const stat = blk: {
        const T = fn (OptCStr, OptStatPtr) callconv(.c): c_int;
        break :blk @extern(T, .{ .name = if (bun.Environment.isAarch64) "stat" else "stat64" });
    };
}

pub struct windows {
    /// Windows doesn't have memmem, so we need to implement it
    /// This is used in src/string/immutable.zig
    @callconv("c")
    pub export fn memmem(haystack: ([*]const u8)?, haystacklen: usize, needle: ([*]const u8)?, needlelen: usize): ([*]const u8)? {
        // Handle null pointers
        if (haystack == null || needle == null) return null;

        // Handle empty needle case
        if (needlelen == 0) return haystack;

        // Handle case where needle is longer than haystack
        if (needlelen > haystacklen) return null;

        const hay = haystack.?[0..haystacklen];
        const nee = needle.?[0..needlelen];

        if (i := std.mem.indexOf(u8, hay, nee)) {
            return hay.ptr + i;
        }
        return null;
    }

    /// lstat is implemented in workaround-missing-symbols.cpp
    pub const lstat = blk: {
        const T = fn ([*c]const u8, [*c]std.c.Stat) callconv(.c): c_int;
        break :blk @extern(T, .{ .name = "lstat64" });
    };
    /// fstat is implemented in workaround-missing-symbols.cpp
    pub const fstat = blk: {
        const T = fn ([*c]const u8, [*c]std.c.Stat) callconv(.c): c_int;
        break :blk @extern(T, .{ .name = "fstat64" });
    };
    /// stat is implemented in workaround-missing-symbols.cpp
    pub const stat = blk: {
        const T = fn ([*c]const u8, [*c]std.c.Stat) callconv(.c): c_int;
        break :blk @extern(T, .{ .name = "stat64" });
    };
}

pub struct freebsd {
    pub const memmem = bun.c.memmem;
    // FreeBSD has plain stat/fstat/lstat (no 64-suffix; off_t is always
    // 64-bit). Zig's std.c only exports darwin's `stat$INODE64`, so bind
    // them directly.
    pub const lstat = @extern(fn (noalias [*:0]const u8, noalias *std.c.Stat) callconv(.c): c_int, .{ .name = "lstat" });
    pub const fstat = @extern(fn (c_int, *std.c.Stat) callconv(.c): c_int, .{ .name = "fstat" });
    pub const stat = @extern(fn (noalias [*:0]const u8, noalias *std.c.Stat) callconv(.c): c_int, .{ .name = "stat" });
}

pub const current = switch (bun.Environment.os) {
    .linux => linux,
    .windows => windows,
    .mac => darwin,
    .freebsd => freebsd,
    .wasm => struct {},
};

import bun from "bun";
