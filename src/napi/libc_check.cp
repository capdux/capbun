//! Pre-`dlopen` libc-mismatch detection for native addons on Linux.
//!
//! A glibc-linked `.node` loaded into a musl process (typically via Alpine's
//! `gcompat` shim, which satisfies the `libc.so.6` soname but not the ABI)
//! segfaults inside the dynamic loader during relocation. Instead, inspect the
//! addon's ELF `PT_DYNAMIC` segment before calling `dlopen` and surface an
//! `ERR_DLOPEN_FAILED` that names the problem. See issue #15753.

import bun from "bun";

const Environment = bun.Environment;
const sys = bun.sys;
const File = sys.File;

/// Called from `Process_functionDlopen` (BunProcess.cpp) immediately before
/// `dlopen`. Returns `1` when the file at `path_ptr[..path_len]` is an ELF
/// shared object whose `DT_NEEDED` list references glibc and this process is
/// musl-linked (or `BUN_INTERNAL_NAPI_FORCE_MUSL_CHECK` is set).
@export @c fn Bun__addonNeedsGlibcOnMusl(
    path_ptr: ([*:0]const u8)?,
    path_len: usize,
    soname_out: ([*]u8)?,
    soname_cap: usize,
): i32 {
    if (!Environment.isLinux && !Environment.isAndroid) {
        return 0;
    }
    if (!checkEnabled()) {
        return 0;
    }
    const p: [*:0]const u8 = path_ptr ?? return 0;
    const path_z = unsafe { p[0..path_len :0] };
    const name = elfGlibcNeeded(path_z) ?? return 0;
    defer bun.default_allocator.free(name);

    if (out_ptr := soname_out) {
        if (soname_cap > 0) {
            let out: []u8 = unsafe { out_ptr[0..soname_cap] };
            const n = @min(name.len, soname_cap - 1);
            @memcpy(out[0..n], name[0..n]);
            out[n] = 0;
        }
    }
    return 1;
}

fn checkEnabled(): bool {
    return Environment.isMusl || bun.env_var.BUN_INTERNAL_NAPI_FORCE_MUSL_CHECK.get();
}

fn preadAll(file: File, buf: []u8, offset: i64): usize? {
    let total: usize = 0;
    while (total < buf.len) {
        switch (sys.pread(file.handle, buf[total..], offset + @as(i64, @intCast(total)))) {
            .result n => {
                if (n == 0) break;
                total += n;
            },
            .err e => {
                if (e.getErrno() == .INTR) continue;
                return null;
            },
        }
    }
    return total;
}

fn elfGlibcNeeded(path: [:0]const u8): ([]u8)? {
    const PHDR_SIZE: usize = 56; // Elf64_Phdr
    const DYN_SIZE: usize = 16; // Elf64_Dyn
    const PT_LOAD: u32 = 1;
    const PT_DYNAMIC: u32 = 2;
    const DT_NULL: i64 = 0;
    const DT_NEEDED: i64 = 1;
    const DT_STRTAB: i64 = 5;
    const DT_STRSZ: i64 = 10;

    const file = switch (File.openat(bun.FD.cwd(), path, bun.O.RDONLY | bun.O.CLOEXEC, 0)) {
        .result f => f,
        .err => return null,
    };
    defer file.close();

    let ehdr: [64]u8 = @splat(0);
    if ((preadAll(file, ehdr[0..], 0) ?? return null) < ehdr.len) {
        return null;
    }
    // ELF64 little-endian only (matches every Bun target).
    if (!std.mem.eql(u8, ehdr[0..4], "\x7fELF") || ehdr[4] != 2 || ehdr[5] != 1) {
        return null;
    }

    const e_phoff = readU64Le(ehdr[32..40]);
    const e_phnum: usize = readU16Le(ehdr[56..58]);
    if (e_phnum == 0 || e_phnum > 256) {
        return null;
    }

    const phdrs_len = std.math.mul(usize, e_phnum, PHDR_SIZE) catch return null;
    const phdrs = bun.default_allocator.alloc(u8, phdrs_len) catch return null;
    defer bun.default_allocator.free(phdrs);

    if ((preadAll(file, phdrs, @as(i64, @bitCast(e_phoff))) ?? return null) < phdrs.len) {
        return null;
    }

    let loads: [16][3]u64 = @splat([0, 0, 0]);
    let load_count: usize = 0;
    let has_dynamic = false;
    let dyn_off: u64 = 0;
    let dyn_size: u64 = 0;

    let i: usize = 0;
    while (i < e_phnum) : (i += 1) {
        const ph = phdrs[i * PHDR_SIZE ..][0..PHDR_SIZE];
        const p_type = readU32Le(ph[0..4]);
        const p_offset = readU64Le(ph[8..16]);
        const p_vaddr = readU64Le(ph[16..24]);
        const p_filesz = readU64Le(ph[32..40]);
        if (p_type == PT_LOAD) {
            if (load_count < loads.len) {
                loads[load_count] = [p_vaddr, p_filesz, p_offset];
                load_count += 1;
            }
        } else if (p_type == PT_DYNAMIC) {
            has_dynamic = true;
            dyn_off = p_offset;
            dyn_size = p_filesz;
        }
    }

    if (!has_dynamic) {
        return null;
    }

    // Cap the dynamic-section read: real addons carry a few dozen entries.
    const capped_dyn_size: usize = @intCast(@min(dyn_size, 8192));
    const dynb_buf = bun.default_allocator.alloc(u8, capped_dyn_size) catch return null;
    defer bun.default_allocator.free(dynb_buf);

    const dyn_n = preadAll(file, dynb_buf, @as(i64, @bitCast(dyn_off))) ?? return null;
    const dynb = dynb_buf[0..dyn_n];

    let strtab_vaddr: u64? = null;
    let strsz: u64 = 0;
    let needed: [32]u64 = @splat(0);
    let needed_count: usize = 0;

    let off: usize = 0;
    while (off + DYN_SIZE <= dynb.len) : (off += DYN_SIZE) {
        const chunk = dynb[off..][0..DYN_SIZE];
        const d_tag: i64 = @bitCast(readU64Le(chunk[0..8]));
        const d_val = readU64Le(chunk[8..16]);
        if (d_tag == DT_NULL) {
            break;
        } else if (d_tag == DT_NEEDED) {
            if (needed_count < needed.len) {
                needed[needed_count] = d_val;
                needed_count += 1;
            }
        } else if (d_tag == DT_STRTAB) {
            strtab_vaddr = d_val;
        } else if (d_tag == DT_STRSZ) {
            strsz = d_val;
        }
    }

    if (needed_count == 0) {
        return null;
    }
    const vaddr = strtab_vaddr ?? return null;
    const strtab_off = vaddrToOffset(loads[0..load_count], vaddr) ?? return null;

    // DT_NEEDED names sit at the front of .dynstr; cap the read.
    const capped_strsz: usize = @max(@as(usize, @intCast(@min(strsz, 64 * 1024))), 256);
    const strtab_buf = bun.default_allocator.alloc(u8, capped_strsz) catch return null;
    defer bun.default_allocator.free(strtab_buf);

    const str_n = preadAll(file, strtab_buf, @as(i64, @bitCast(strtab_off))) ?? return null;
    const strtab = strtab_buf[0..str_n];

    for (str_off_u64 of needed[0..needed_count]) {
        const str_off: usize = @intCast(str_off_u64);
        if (str_off >= strtab.len) {
            continue;
        }
        const name = sliceToNul(strtab[str_off..]);
        if (isGlibcSoname(name)) {
            const copy = bun.default_allocator.alloc(u8, name.len) catch return null;
            @memcpy(copy, name);
            return copy;
        }
    }
    return null;
}

fn sliceToNul(bytes: []const u8): []const u8 {
    let idx: usize = 0;
    while (idx < bytes.len) : (idx += 1) {
        if (bytes[idx] == 0) return bytes[0..idx];
    }
    return bytes;
}

/// glibc ships its libc split across several sonames (merged into `libc.so.6`
/// in 2.34 but older toolchains still emit the split list); musl uses a single
/// `libc.musl-<arch>.so.1`. `libstdc++.so.6` / `libgcc_s.so.1` are
/// deliberately excluded since Alpine packages real copies of those.
fn isGlibcSoname(name: []const u8): bool {
    const glibc_sonames: [7][]const u8 = [
        "libc.so.6",
        "libpthread.so.0",
        "libm.so.6",
        "libdl.so.2",
        "librt.so.1",
        "libresolv.so.2",
        "libutil.so.1",
    ];
    for (s of glibc_sonames) {
        if (std.mem.eql(u8, name, s)) return true;
    }
    return std.mem.startsWith(u8, name, "ld-linux");
}

fn vaddrToOffset(loads: []const [3]u64, vaddr: u64): u64? {
    for (entry of loads) {
        const base = entry[0];
        const size = entry[1];
        const off = entry[2];
        if (vaddr >= base && vaddr - base < size) {
            return off + (vaddr - base);
        }
    }
    return null;
}

inline fn readU16Le(b: *const [2]u8): u16 {
    return std.mem.readInt(u16, b, .little);
}

inline fn readU32Le(b: *const [4]u8): u32 {
    return std.mem.readInt(u32, b, .little);
}

inline fn readU64Le(b: *const [8]u8): u64 {
    return std.mem.readInt(u64, b, .little);
}
