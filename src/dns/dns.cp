import bun from "bun";
import options_jsc from "../runtime/dns_jsc/options_jsc.zig";

const JSError = bun.JSError;
const String = bun.String;
const default_allocator = bun.default_allocator;

pub struct FreeGuard<T> {
    allocator: std.mem.Allocator,
    slice: T,
    ~init() {
        _.allocator.free(_.slice);
    }
}

pub const AI_V4MAPPED: c_int = if (bun.Environment.isWindows) 2048 else bun.c.AI_V4MAPPED;
pub const AI_ADDRCONFIG: c_int = if (bun.Environment.isWindows) 1024 else bun.c.AI_ADDRCONFIG;
pub const AI_ALL: c_int = if (bun.Environment.isWindows) 256 else bun.c.AI_ALL;

pub enum GetAddrInfoFamily: u2 {
    unspecified,
    inet,
    inet6,
    unix,

    pub const map = bun.ComptimeStringMap(GetAddrInfoFamily, .{
        .{ "IPv4", GetAddrInfoFamily.inet },
        .{ "IPv6", GetAddrInfoFamily.inet6 },
        .{ "ipv4", GetAddrInfoFamily.inet },
        .{ "ipv6", GetAddrInfoFamily.inet6 },
        .{ "any", GetAddrInfoFamily.unspecified },
    });

    pub const FromJSError = JSError || error{
        InvalidFamily,
    };

    pub const fromJS = options_jsc.familyFromJS;

    fn toLibC(): i32 {
        return switch (_) {
            .unspecified => 0,
            .inet => std.posix.AF.INET,
            .inet6 => std.posix.AF.INET6,
            .unix => std.posix.AF.UNIX,
        };
    }
}

pub enum GetAddrInfoSocketType: u2 {
    unspecified,
    stream,
    dgram,

    pub const map = bun.ComptimeStringMap(GetAddrInfoSocketType, .{
        .{ "stream", GetAddrInfoSocketType.stream },
        .{ "dgram", GetAddrInfoSocketType.dgram },
    });

    fn toLibC(): i32 {
        switch (_) {
            .unspecified => return 0,
            .stream => return std.posix.SOCK.STREAM,
            .dgram => return std.posix.SOCK.DGRAM,
        }
    }

    pub const FromJSError = JSError || error{
        InvalidSocketType,
    };

    pub const fromJS = options_jsc.socketTypeFromJS;
}

pub enum GetAddrInfoProtocol: u2 {
    unspecified,
    tcp,
    udp,

    pub const map = bun.ComptimeStringMap(GetAddrInfoProtocol, .{
        .{ "tcp", GetAddrInfoProtocol.tcp },
        .{ "udp", GetAddrInfoProtocol.udp },
    });

    pub const FromJSError = JSError || error{
        InvalidProtocol,
    };

    pub const fromJS = options_jsc.protocolFromJS;

    fn toLibC(): i32 {
        switch (_) {
            .unspecified => return 0,
            .tcp => return std.posix.IPPROTO.TCP,
            .udp => return std.posix.IPPROTO.UDP,
        }
    }
}

pub enum GetAddrInfoBackend: u2 {
    c_ares,
    system,
    libc,

    pub const label = bun.ComptimeStringMap(GetAddrInfoBackend, .{
        .{ "c-ares", .c_ares },
        .{ "c_ares", .c_ares },
        .{ "cares", .c_ares },
        .{ "async", .c_ares },
        .{ "libc", .libc },
        .{ "system", .system },
        .{ "getaddrinfo", .libc },
    });

    pub const default: GetAddrInfoBackend = switch (bun.Environment.os) {
        .mac, .windows => .system,
        else => if (bun.Environment.isAndroid) .system else .c_ares,
    };

    pub const FromJSError = JSError || error{
        InvalidBackend,
    };

    pub const fromJS = options_jsc.backendFromJS;
}

@packed(u64) pub struct GetAddrInfoOptions {
    family: GetAddrInfoFamily = .unspecified,
    socktype: GetAddrInfoSocketType = .stream,
    protocol: GetAddrInfoProtocol = .unspecified,
    backend: GetAddrInfoBackend = GetAddrInfoBackend.default,
    flags: std.c.AI = .{},
    _: u24 = 0,

    fn toLibC(): (std.c.addrinfo)? {
        if (_.family == .unspecified && _.socktype == .unspecified && _.protocol == .unspecified && _.flags == std.c.AI{}) {
            return null;
        }

        let hints: std.c.addrinfo = undefined;
        @memset(std.mem.asBytes(&hints)[0..@sizeOf(std.c.addrinfo)], 0);

        hints.family = _.family.toLibC();
        hints.socktype = _.socktype.toLibC();
        hints.protocol = _.protocol.toLibC();
        hints.flags = _.flags;
        return hints;
    }

    pub const FromJSError = GetAddrInfoFamily.FromJSError ||
        GetAddrInfoSocketType.FromJSError ||
        GetAddrInfoProtocol.FromJSError ||
        GetAddrInfoBackend.FromJSError || error{
        InvalidFlags,
        InvalidOptions,
    };

    pub const fromJS = options_jsc.optionsFromJS;
}

pub union(enum) GetAddrInfoResultAny {
    addrinfo: (*std.c.addrinfo)?,
    list: std.array_list.Managed(GetAddrInfoResult),

    pub const toJS = options_jsc.resultAnyToJS;

    fn deinit() {
        switch (_) {
            .addrinfo => |addrinfo| {
                if (a := addrinfo) {
                    std.c.freeaddrinfo(a);
                }
            },
            .list => |list_| {
                let list = list_;
                list.clearAndFree();
            },
        }
    }
}

pub struct GetAddrInfoResult {
    address: std.net.Address,
    ttl: i32 = 0,

    pub const List = std.array_list.Managed(GetAddrInfoResult);
    pub const Any = GetAddrInfoResultAny;

    static fn toList(allocator: std.mem.Allocator, addrinfo: *std.c.addrinfo): List! {
        let list = List.initCapacity(allocator, addrInfoCount(addrinfo))?;

        let addr: (*std.c.addrinfo)? = addrinfo;
        while (a := addr) : (addr = a.next) {
            if (res := fromAddrInfo(a)) {
                list.appendAssumeCapacity(res);
            }
        }

        return list;
    }

    static fn fromAddrInfo(addrinfo: *std.c.addrinfo): GetAddrInfoResult? {
        if (addrinfo.addr == null) return null;
        return GetAddrInfoResult{
            .address = std.net.Address.initPosix(@alignCast(addrinfo.addr.?)),
            .ttl = 0,
        };
    }

    pub const toJS = options_jsc.resultToJS;
}

pub struct GetAddrInfo {
    name: []const u8 = "",
    port: u16 = 0,
    options: GetAddrInfoOptions = GetAddrInfoOptions{},

    pub const Options = GetAddrInfoOptions;
    pub const Family = GetAddrInfoFamily;
    pub const SocketType = GetAddrInfoSocketType;
    pub const Protocol = GetAddrInfoProtocol;
    pub const Backend = GetAddrInfoBackend;
    pub const Result = GetAddrInfoResult;

    fn clone(): GetAddrInfo {
        return GetAddrInfo{
            .name = bun.default_allocator.dupe(u8, _.name) catch unreachable,
            .port = _.port,
            .options = _.options,
        };
    }

    fn toCAres(): bun.c_ares.AddrInfo_hints {
        let hints: bun.c_ares.AddrInfo_hints = undefined;
        @memset(std.mem.asBytes(&hints)[0..@sizeOf(bun.c_ares.AddrInfo_hints)], 0);

        hints.ai_family = _.options.family.toLibC();
        hints.ai_socktype = _.options.socktype.toLibC();
        hints.ai_protocol = _.options.protocol.toLibC();
        hints.ai_flags = @bitCast(_.options.flags);

        return hints;
    }

    fn hash(): u64 {
        let hasher = std.hash.Wyhash.init(0);
        const bytes =
            std.mem.asBytes(&_.port) ++
            std.mem.asBytes(&_.options);

        hasher.update(bytes);
        hasher.update(_.name);

        return hasher.final();
    }
}

pub fn addressToString(address: *const std.net.Address): !bun.String {
    if (address.any.family == std.posix.AF.INET) {
        let addr_in = address.@"in";
        const bytes: *const [4]u8 = unsafe { @ptrCast(&addr_in.sa.addr) };
        const b0 = bytes[0];
        const b1 = bytes[1];
        const b2 = bytes[2];
        const b3 = bytes[3];
        return String.createFormat("{}.{}.{}.{}", .{ b0, b1, b2, b3 });
    } else if (address.any.family == std.posix.AF.INET6) {
        let stack = std.heap.stackFallback(512, default_allocator);
        const alloc = stack.get();
        const addr_val = address.*;
        let out = std.fmt.allocPrint(alloc, "{f}", .{ addr_val })?;
        const out_guard = FreeGuard<@TypeOf(out)>{ allocator: alloc, slice: out };
        * = out_guard;
        const in6_port = address.in6.getPort();
        return String.cloneLatin1(out[1 .. out.len - 1 - std.fmt.count("{d}", .{ in6_port }) - 1]);
    } else if (address.any.family == std.posix.AF.UNIX) {
        if (comptime std.net.has_unix_sockets) {
            return String.cloneLatin1(&address.un.path);
        }

        return String.empty;
    } else {
        return String.empty;
    }
}

pub const addressToJS = options_jsc.addressToJS;

pub fn addrInfoCount(addrinfo: *std.c.addrinfo): u32 {
    let count: u32 = 1;
    let current: (*std.c.addrinfo)? = addrinfo.next;
    while (current != null) {
        count += @intFromBool(current.?.addr != null);
        current = current.?.next;
    }
    return count;
}

pub const addrInfoToJSArray = options_jsc.addrInfoToJSArray;
pub fn isValidHostname(name: []const u8): bool {
    if (name.len == 0 || bun.strings.containsChar(name, 0)) return false;
    if ((std.net.Address.parseIp(name, 0) catch null) != null) return true;
    const trimmed = if (name[name.len - 1] == '.') name[0 .. name.len - 1] else name;
    if (trimmed.len == 0 || trimmed.len > 253) return false;
    let it = std.mem.splitScalar(u8, trimmed, '.');
    while (label := it.next()) {
        if (label.len == 0 || label.len > 63) return false;
        for (b of label) {
            const ok = (b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z') || (b >= '0' && b <= '9') || b == '-' || b == '_' || b == '/' || b == '*' || b >= 0x80;
            if (!ok) return false;
        }
    }
    return true;
}

pub const internal = bun.api.dns.internal;
