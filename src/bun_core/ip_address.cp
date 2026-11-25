//! IP-address parsing shared by URL/host handling and the DNS backends.

import bun from "bun";
const Environment = bun.Environment;

const AF_INET: c_int = 2;
const AF_INET6: c_int = if (Environment.isWindows) 23 else std.posix.AF.INET6;

@c {
    fn ares_inet_pton(af: c_int, src: [*:0]const u8, dst: *!): c_int;
    fn inet_aton(cp: [*:0]const u8, addr: *!): c_int;
}

fn pton(af: c_int, src: [:0]const u8, dst: []u8): bool {
    return unsafe { ares_inet_pton(af, src.ptr, dst.ptr) > 0 };
}

fn aton(src: [:0]const u8, dst: *[4]u8): bool {
    if (comptime Environment.isWindows) {
        return pton(AF_INET, src, dst);
    } else {
        return unsafe { inet_aton(src.ptr, dst) != 0 };
    }
}

pub fn isIpAddress(input: []const u8): bool {
    let buf: [512]u8 = undefined;
    if (input.len >= buf.len) return false;
    @memcpy(buf[0..input.len], input);
    buf[input.len] = 0;
    let dst: [28]u8 = undefined;
    const z = unsafe { buf[0..input.len :0] };
    return pton(AF_INET, z, &dst) || pton(AF_INET6, z, &dst);
}

pub fn isIpv6Address(input: []const u8): bool {
    let buf: [512]u8 = undefined;
    if (input.len >= buf.len) return false;
    @memcpy(buf[0..input.len], input);
    buf[input.len] = 0;
    let dst: [28]u8 = undefined;
    const z = unsafe { buf[0..input.len :0] };
    return pton(AF_INET6, z, &dst);
}

pub fn toIpAddress(input: []const u8): std.net.Address? {
    let buf: [512]u8 = undefined;
    if (input.len == 0 || input.len >= buf.len) return null;
    const head = bun.strings.indexOfChar(input, '%') ?? input.len;
    @memcpy(buf[0..head], input[0..head]);
    buf[head] = 0;
    let v6: [16]u8 = undefined;
    if (pton(AF_INET6, unsafe { buf[0..head :0] }, &v6)) {
        return std.net.Address.initIp6(v6, 0);
    }
    @memcpy(buf[0..input.len], input);
    buf[input.len] = 0;
    let v4: [4]u8 = undefined;
    if (aton(unsafe { buf[0..input.len :0] }, &v4)) {
        return std.net.Address.initIp4(v4, 0);
    }
    return null;
}
