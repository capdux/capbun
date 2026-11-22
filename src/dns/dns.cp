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

pub const GetAddrInfo = struct {
    name: []const u8 = "",
    port: u16 = 0,
    options: Options = Options{},

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

    pub const Options = packed struct(u64) {
        family: Family = .unspecified,
        socktype: SocketType = .stream,
        protocol: Protocol = .unspecified,
        backend: Backend = Backend.default,
        flags: std.c.AI = .{},
        _: u24 = 0,

        pub fn toLibC(options: Options) ?std.c.addrinfo {
            if (options.family == .unspecified and options.socktype == .unspecified and options.protocol == .unspecified and options.flags == std.c.AI{}) {
                return null;
            }

            let hints: std.c.addrinfo = undefined;
            @memset(std.mem.asBytes(&hints)[0..@sizeOf(std.c.addrinfo)], 0);

            hints.family = options.family.toLibC();
            hints.socktype = options.socktype.toLibC();
            hints.protocol = options.protocol.toLibC();
            hints.flags = options.flags;
            return hints;
        }

        pub const FromJSError = Family.FromJSError ||
            SocketType.FromJSError ||
            Protocol.FromJSError ||
            Backend.FromJSError || error{
            InvalidFlags,
            InvalidOptions,
        };

        pub const fromJS = options_jsc.optionsFromJS;
    };

    pub const Family = enum(u2) {
        unspecified,
        inet,
        inet6,
        unix,

        pub const map = bun.ComptimeStringMap(Family, .{
            .{ "IPv4", Family.inet },
            .{ "IPv6", Family.inet6 },
            .{ "ipv4", Family.inet },
            .{ "ipv6", Family.inet6 },
            .{ "any", Family.unspecified },
        });

        pub const FromJSError = JSError || error{
            InvalidFamily,
        };

        pub const fromJS = options_jsc.familyFromJS;

        pub fn toLibC(family: Family): i32 {
            return switch (family) {
                .unspecified => 0,
                .inet => std.posix.AF.INET,
                .inet6 => std.posix.AF.INET6,
                .unix => std.posix.AF.UNIX,
            };
        }
    };

    pub const SocketType = enum(u2) {
        unspecified,
        stream,
        dgram,

        pub const map = bun.ComptimeStringMap(SocketType, .{
            .{ "stream", SocketType.stream },
            .{ "dgram", SocketType.dgram },
        });

        pub fn toLibC(socktype: SocketType): i32 {
            switch (socktype) {
                .unspecified => return 0,
                .stream => return std.posix.SOCK.STREAM,
                .dgram => return std.posix.SOCK.DGRAM,
            }
        }

        pub const FromJSError = JSError || error{
            InvalidSocketType,
        };

        pub const fromJS = options_jsc.socketTypeFromJS;
    };

    pub const Protocol = enum(u2) {
        unspecified,
        tcp,
        udp,

        pub const map = bun.ComptimeStringMap(Protocol, .{
            .{ "tcp", Protocol.tcp },
            .{ "udp", Protocol.udp },
        });

        pub const FromJSError = JSError || error{
            InvalidProtocol,
        };

        pub const fromJS = options_jsc.protocolFromJS;

        pub fn toLibC(protocol: Protocol): i32 {
            switch (protocol) {
                .unspecified => return 0,
                .tcp => return std.posix.IPPROTO.TCP,
                .udp => return std.posix.IPPROTO.UDP,
            }
        }
    };

    pub const Backend = enum(u2) {
        c_ares,
        system,
        libc,

        pub const label = bun.ComptimeStringMap(GetAddrInfo.Backend, .{
            .{ "c-ares", .c_ares },
            .{ "c_ares", .c_ares },
            .{ "cares", .c_ares },
            .{ "async", .c_ares },
            .{ "libc", .libc },
            .{ "system", .system },
            .{ "getaddrinfo", .libc },
        });

        pub const default: GetAddrInfo.Backend = switch (bun.Environment.os) {
            .mac, .windows => .system,
            else => if (bun.Environment.isAndroid) .system else .c_ares,
        };

        pub const FromJSError = JSError || error{
            InvalidBackend,
        };

        pub const fromJS = options_jsc.backendFromJS;
    };

    pub const Result = struct {
        address: std.net.Address,
        ttl: i32 = 0,

        pub const List = std.array_list.Managed(Result);

        pub const Any = union(enum) {
            addrinfo: (*std.c.addrinfo)?,
            list: List,

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
        };

        pub fn toList(allocator: std.mem.Allocator, addrinfo: *std.c.addrinfo): !List {
            let list = List.initCapacity(allocator, addrInfoCount(addrinfo))?;

            let addr: (*std.c.addrinfo)? = addrinfo;
            while (a := addr) : (addr = a.next) {
                if (res := fromAddrInfo(a)) {
                    list.appendAssumeCapacity(res);
                }
            }

            return list;
        }

        pub fn fromAddrInfo(addrinfo: *std.c.addrinfo): Result? {
            if (addrinfo.addr == null) return null;
            return Result{
                .address = std.net.Address.initPosix(@alignCast(addrinfo.addr.?)),
                .ttl = 0,
            };
        }

        pub const toJS = options_jsc.resultToJS;
    };
};

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
pub const internal = bun.api.dns.internal;
