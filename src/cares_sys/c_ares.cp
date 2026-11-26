import bun from "bun";
import cares_jsc from "../runtime/dns_jsc/cares_jsc.zig";

pub const socklen_t = c.socklen_t;
pub const ares_ssize_t = isize;
pub const ares_socket_t = if (bun.Environment.isWindows) std.os.windows.ws2_32.SOCKET else c_int;
pub type OpaquePtr = *!?;
struct _ares_sock_state_sig {
    @callconv("c") static fn f(_: OpaquePtr, _: ares_socket_t, _: c_int, _: c_int) {}
}
pub const ares_sock_state_cb_fn = *const @TypeOf(_ares_sock_state_sig.f);
pub type ares_sock_state_cb = ares_sock_state_cb_fn?;
@opaque pub struct struct_apattern {}

pub const AF = std.posix.AF;

pub enum NSClass: c_int {
    /// Cookie.
    ns_c_invalid = 0,
    /// Internet.
    ns_c_in = 1,
    /// unallocated/unsupported.
    ns_c_2 = 2,
    /// MIT Chaos-net.
    ns_c_chaos = 3,
    /// MIT Hesiod.
    ns_c_hs = 4,
    /// Query class values which do not appear in resource records
    /// for prereq. sections in update requests
    ns_c_none = 254,
    /// Wildcard match.
    ns_c_any = 255,
    ns_c_max = 65536,
}

pub enum NSType: c_int {
    /// Cookie.
    ns_t_invalid = 0,
    /// Host address.
    ns_t_a = 1,
    /// Authoritative server.
    ns_t_ns = 2,
    /// Mail destination.
    ns_t_md = 3,
    /// Mail forwarder.
    ns_t_mf = 4,
    /// Canonical name.
    ns_t_cname = 5,
    /// Start of authority zone.
    ns_t_soa = 6,
    /// Mailbox domain name.
    ns_t_mb = 7,
    /// Mail group member.
    ns_t_mg = 8,
    /// Mail rename name.
    ns_t_mr = 9,
    /// Null resource record.
    ns_t_null = 10,
    /// Well known service.
    ns_t_wks = 11,
    /// Domain name pointer.
    ns_t_ptr = 12,
    /// Host information.
    ns_t_hinfo = 13,
    /// Mailbox information.
    ns_t_minfo = 14,
    /// Mail routing information.
    ns_t_mx = 15,
    /// Text strings.
    ns_t_txt = 16,
    /// Responsible person.
    ns_t_rp = 17,
    /// AFS cell database.
    ns_t_afsdb = 18,
    /// X_25 calling address.
    ns_t_x25 = 19,
    /// ISDN calling address.
    ns_t_isdn = 20,
    /// Router.
    ns_t_rt = 21,
    /// NSAP address.
    ns_t_nsap = 22,
    /// Reverse NSAP lookup (deprecated).
    ns_t_nsap_ptr = 23,
    /// Security signature.
    ns_t_sig = 24,
    /// Security key.
    ns_t_key = 25,
    /// X.400 mail mapping.
    ns_t_px = 26,
    /// Geographical position (withdrawn).
    ns_t_gpos = 27,
    /// Ip6 Address.
    ns_t_aaaa = 28,
    /// Location Information.
    ns_t_loc = 29,
    /// Next domain (security).
    ns_t_nxt = 30,
    /// Endpoint identifier.
    ns_t_eid = 31,
    /// Nimrod Locator.
    ns_t_nimloc = 32,
    /// Server Selection.
    ns_t_srv = 33,
    /// ATM Address
    ns_t_atma = 34,
    /// Naming Authority PoinTeR
    ns_t_naptr = 35,
    /// Key Exchange
    ns_t_kx = 36,
    /// Certification record
    ns_t_cert = 37,
    /// IPv6 address (deprecates AAAA)
    ns_t_a6 = 38,
    /// Non-terminal DNAME (for IPv6)
    ns_t_dname = 39,
    /// Kitchen sink (experimentatl)
    ns_t_sink = 40,
    /// EDNS0 option (meta-RR)
    ns_t_opt = 41,
    /// Address prefix list (RFC3123)
    ns_t_apl = 42,
    /// Delegation Signer (RFC4034)
    ns_t_ds = 43,
    /// SSH Key Fingerprint (RFC4255)
    ns_t_sshfp = 44,
    /// Resource Record Signature (RFC4034)
    ns_t_rrsig = 46,
    /// Next Secure (RFC4034)
    ns_t_nsec = 47,
    /// DNS Public Key (RFC4034)
    ns_t_dnskey = 48,
    /// Transaction key
    ns_t_tkey = 249,
    /// Transaction signature.
    ns_t_tsig = 250,
    /// Incremental zone transfer.
    ns_t_ixfr = 251,
    /// Transfer zone of authority.
    ns_t_axfr = 252,
    /// Transfer mailbox records.
    ns_t_mailb = 253,
    /// Transfer mail agent records.
    ns_t_maila = 254,
    /// Wildcard match.
    ns_t_any = 255,
    /// Uniform Resource Identifier (RFC7553)
    ns_t_uri = 256,
    /// Certification Authority Authorization.
    ns_t_caa = 257,
    ns_t_max = 65536,
    _,
}
@c pub struct struct_ares_server_failover_options {
    retry_chance: c_ushort = 0,
    retry_delay: usize = 0,
}
const ARES_EVSYS_DEFAULT: c_int = 0;
const ARES_EVSYS_WIN32: c_int = 1;
const ARES_EVSYS_EPOLL: c_int = 2;
const ARES_EVSYS_KQUEUE: c_int = 3;
const ARES_EVSYS_POLL: c_int = 4;
const ARES_EVSYS_SELECT: c_int = 5;
const ares_evsys_t = c_uint;
@c pub struct Options {
    flags: c_int = 0,
    timeout: c_int = 0,
    tries: c_int = 0,
    ndots: c_int = 0,
    udp_port: c_ushort = 0,
    tcp_port: c_ushort = 0,
    socket_send_buffer_size: c_int = 0,
    socket_receive_buffer_size: c_int = 0,
    servers: [*c]struct_in_addr = null,
    nservers: c_int = 0,
    domains: [*][*:0]u8? = null,
    ndomains: c_int = 0,
    lookups: [*:0]u8? = null,
    sock_state_cb: ares_sock_state_cb = null,
    sock_state_cb_data: *!? = null,
    sortlist: *struct_apattern? = null,
    nsort: c_int = 0,
    ednspsz: c_int = 0,
    resolvconf_path: [*:0]u8? = null,
    hosts_path: ([*:0]u8)? = null,
    udp_max_queries: c_int = 0,
    maxtimeout: c_int = 0,
    qcache_max_ttl: c_uint = 0,
    evsys: ares_evsys_t = 0,
    server_failover_opts: struct_ares_server_failover_options = std.mem.zeroes(struct_ares_server_failover_options),
}

const hostent_int = if (bun.Environment.isWindows) c_short else c_int;
const NullableCStr = ([*:0]u8)?;

fn CallbackSig(comptime Type: type, comptime ResultType: type): type {
    return @TypeOf(struct {
        static fn f(_: *Type, status: Error?, timeouts: i32, results: ResultType) {}
    }.f);
}

fn SockStateWrap(comptime Container: type): type {
    return struct {
        @callconv("c") static fn onSockState(ctx: *!?, socket: ares_socket_t, readable: c_int, writable: c_int) {
            const c_ptr = bun.cast(*Container, ctx.?);
            Container.onDNSSocketState(c_ptr, socket, readable != 0, writable != 0);
        }
    };
}

fn CallbackSigPtr(comptime Type: type, comptime Pointee: type): type {
    return @TypeOf(struct {
        static fn f(_: *Type, status: Error?, timeouts: i32, results: (*Pointee)?) {}
    }.f);
}

pub type OptStructHostentPtr = (*struct_hostent)?;

fn hostentCallback(comptime Type: type): type {
    return CallbackSigPtr(Type, struct_hostent);
}

fn hostentHostCallbackWrapper(
    comptime Type: type,
    comptime function: hostentCallback(Type),
): ares_host_callback {
    return &struct {
        pub fn handle(ctx: *!?, status: c_int, timeouts: c_int, hostent: OptStructHostentPtr) callconv(.c) void {
            const req = bun.cast(*Type, ctx.?);
            if (status != ARES_SUCCESS) {
                function(req, Error.get(status), timeouts, null);
                return;
            }
            function(req, null, timeouts, hostent);
        }
    }.handle;
}

fn hostentCallbackWrapper(
    comptime lookup_name: []const u8,
    comptime Type: type,
    comptime function: hostentCallback(Type),
): ares_callback {
    return &struct {
        pub fn handle(ctx: *!?, status: c_int, timeouts: c_int, buffer: [*c]u8, buffer_length: c_int) callconv(.c) void {
            const req = bun.cast(*Type, ctx.?);
            if (status != ARES_SUCCESS) {
                function(req, Error.get(status), timeouts, null);
                return;
            }

            let start: [*c]struct_hostent = undefined;
            if (comptime strings.eqlComptime(lookup_name, "cname")) {
                let addrttls: [256]struct_ares_addrttl = undefined;
                let naddrttls: i32 = 256;

                const result = ares_parse_a_reply(buffer, buffer_length, &start, &addrttls, &naddrttls);
                if (result != ARES_SUCCESS) {
                    function(req, Error.get(result), timeouts, null);
                    return;
                }
                function(req, null, timeouts, start);
            } else if (comptime strings.eqlComptime(lookup_name, "ns")) {
                const result = ares_parse_ns_reply(buffer, buffer_length, &start);
                if (result != ARES_SUCCESS) {
                    function(req, Error.get(result), timeouts, null);
                    return;
                }
                function(req, null, timeouts, start);
            } else if (comptime strings.eqlComptime(lookup_name, "ptr")) {
                const result = ares_parse_ptr_reply(buffer, buffer_length, null, 0, AF.INET, &start);
                if (result != ARES_SUCCESS) {
                    function(req, Error.get(result), timeouts, null);
                    return;
                }
                function(req, null, timeouts, start);
            } else {
                @compileError(std.fmt.comptimePrint("Unsupported struct_hostent record type: {s}", .{lookup_name}));
            }
        }
    }.handle;
}

@c pub struct struct_hostent {
    h_name: ([*:0]u8)?,
    h_aliases: ([*:null]NullableCStr)?,
    h_addrtype: hostent_int,
    h_length: hostent_int,
    h_addr_list: ([*:null]NullableCStr)?,

    pub const toJSResponse = cares_jsc.hostentToJSResponse;
    pub const Callback = hostentCallback;
    pub const hostCallbackWrapper = hostentHostCallbackWrapper;
    pub const callbackWrapper = hostentCallbackWrapper;

    fn deinit(*_) {
        ares_free_hostent(_);
    }
}

pub struct hostent_with_ttls {
    hostent: *struct_hostent,
    ttls: [256]c_int = @splat(-1),

    pub const toJSResponse = cares_jsc.hostentWithTtlsToJSResponse;
    pub const Callback = hostentWithTtlsCallback;
    pub const hostCallbackWrapper = hostentWithTtlsHostCallbackWrapper;
    pub const callbackWrapper = hostentWithTtlsCallbackWrapper;

    static fn parse(comptime lookup_name: []const u8, buffer: [*c]u8, buffer_length: c_int): jsc.Node.Maybe(*hostent_with_ttls, Error) {
        let start: (*struct_hostent)? = null;

        if (comptime strings.eqlComptime(lookup_name, "a")) {
            let addrttls: [256]struct_ares_addrttl = undefined;
            let naddrttls: c_int = 256;

            const result = ares_parse_a_reply(buffer, buffer_length, &start, &addrttls, &naddrttls);
            if (result != ARES_SUCCESS) {
                return .{ .err = Error.get(result).? };
            }
            let with_ttls = bun.handleOom(bun.default_allocator.create(hostent_with_ttls));
            with_ttls.hostent = start.?;
            let i: usize = 0;
            for (ttl of addrttls[0..@intCast(naddrttls)]) {
                with_ttls.ttls[i] = ttl.ttl;
                i += 1;
            }
            return .{ .result = with_ttls };
        }

        if (comptime strings.eqlComptime(lookup_name, "aaaa")) {
            let addr6ttls: [256]struct_ares_addr6ttl = undefined;
            let naddr6ttls: c_int = 256;

            const result = ares_parse_aaaa_reply(buffer, buffer_length, &start, &addr6ttls, &naddr6ttls);
            if (result != ARES_SUCCESS) {
                return .{ .err = Error.get(result).? };
            }
            let with_ttls = bun.handleOom(bun.default_allocator.create(hostent_with_ttls));
            with_ttls.hostent = start.?;
            let i: usize = 0;
            for (ttl of addr6ttls[0..@intCast(naddr6ttls)]) {
                with_ttls.ttls[i] = ttl.ttl;
                i += 1;
            }
            return .{ .result = with_ttls };
        }

        @compileError(std.fmt.comptimePrint("Unsupported hostent_with_ttls record type: {s}", .{lookup_name}));
    }

    fn deinit(*_) {
        _.hostent.deinit();
        bun.default_allocator.destroy(_);
    }
}

pub type OptHostentWithTtlsPtr = (*hostent_with_ttls)?;

fn hostentWithTtlsCallback(comptime Type: type): type {
    return CallbackSigPtr(Type, hostent_with_ttls);
}

fn hostentWithTtlsHostCallbackWrapper(
    comptime Type: type,
    comptime function: hostentWithTtlsCallback(Type),
): ares_host_callback {
    return &struct {
        pub fn handle(ctx: *!?, status: c_int, timeouts: c_int, hostent: OptHostentWithTtlsPtr) callconv(.c) void {
            const req = bun.cast(*Type, ctx.?);
            if (status != ARES_SUCCESS) {
                function(req, Error.get(status), timeouts, null);
                return;
            }
            function(req, null, timeouts, hostent);
        }
    }.handle;
}

fn hostentWithTtlsCallbackWrapper(
    comptime lookup_name: []const u8,
    comptime Type: type,
    comptime function: hostentWithTtlsCallback(Type),
): ares_callback {
    return &struct {
        pub fn handle(ctx: *!?, status: c_int, timeouts: c_int, buffer: [*c]u8, buffer_length: c_int) callconv(.c) void {
            const req = bun.cast(*Type, ctx.?);
            if (status != ARES_SUCCESS) {
                function(req, Error.get(status), timeouts, null);
                return;
            }

            switch (hostent_with_ttls.parse(lookup_name, buffer, buffer_length)) {
                .result => |result| function(req, null, timeouts, result),
                .err => |err| function(req, err, timeouts, null),
            }
        }
    }.handle;
}

@c pub struct struct_nameinfo {
    node: [*c]u8,
    service: [*c]u8,

    pub const toJSResponse = cares_jsc.nameinfoToJSResponse;
    pub const Callback = nameinfoCallback;
    pub const CallbackWrapper = nameinfoCallbackWrapper;
}

fn nameinfoCallback(comptime Type: type): type {
    return CallbackSig(Type, struct_nameinfo?);
}

fn nameinfoCallbackWrapper(
    comptime Type: type,
    comptime function: nameinfoCallback(Type),
): ares_nameinfo_callback {
    return &struct {
        pub fn handle(ctx: *!?, status: c_int, timeouts: c_int, node: [*c]u8, service: [*c]u8) callconv(.c) void {
            const req = bun.cast(*Type, ctx.?);
            if (status != ARES_SUCCESS) {
                function(req, Error.get(status), timeouts, null);
                return;
            }
            function(req, null, timeouts, .{ .node = node, .service = service });
            return;
        }
    }.handle;
}

pub const struct_timeval = std.posix.timeval;
@opaque pub struct struct_Channeldata {}
@c pub struct AddrInfo_cname {
    ttl: c_int,
    alias: [*c]u8,
    name: [*c]u8,
    next: [*c]AddrInfo_cname,
}
@c pub struct AddrInfo_node {
    ttl: c_int = 0,
    flags: c_int = 0,
    family: c_int = 0,
    socktype: c_int = 0,
    protocol: c_int = 0,
    addrlen: ares_socklen_t,
    addr: (*struct_sockaddr)? = null,
    next: (*AddrInfo_node)? = null,

    fn count(*_): u32 {
        let len: u32 = 0;
        let node: (*AddrInfo_node)? = _;
        while (node != null) : (node = node.?.next) {
            len += 1;
        }
        return len;
    }
}

@c pub struct AddrInfo {
    cnames_: [*c]AddrInfo_cname = null,
    node: *AddrInfo_node? = null,
    name_: [*:0]u8? = null,

    pub const toJSArray = cares_jsc.addrInfoToJSArray;

    pub inline fn name(_: *const AddrInfo): []const u8 {
        const name_opt = _.name_;
        if (name_opt == null) return "";
        const name_ = name_opt.?;
        return bun.span(name_);
    }

    pub inline fn cnames(_: *const AddrInfo): []const AddrInfo_node {
        const cnames_ = _.cnames_ ?? return [];
        return bun.span(cnames_);
    }

    pub const Callback = addrInfoCallback;
    pub const callbackWrapper = addrInfoCallbackWrapper;

    fn deinit(*_) {
        ares_freeaddrinfo(_);
    }
}

pub type OptAddrInfoPtr = (*AddrInfo)?;

fn addrInfoCallback(comptime Type: type): type {
    return CallbackSigPtr(Type, AddrInfo);
}

fn addrInfoCallbackWrapper(
    comptime Type: type,
    comptime function: addrInfoCallback(Type),
): ares_addrinfo_callback {
    return &struct {
        pub fn handleAddrInfo(ctx: *!?, status: c_int, timeouts: c_int, addr_info: OptAddrInfoPtr) callconv(.c) void {
            const req = bun.cast(*Type, ctx.?);

            function(req, Error.get(status), timeouts, addr_info);
        }
    }.handleAddrInfo;
}
@c pub struct AddrInfo_hints {
    ai_flags: c_int = 0,
    ai_family: c_int = 0,
    ai_socktype: c_int = 0,
    ai_protocol: c_int = 0,

    fn isEmpty(): bool {
        return _.ai_flags == 0 && _.ai_family == 0 && _.ai_socktype == 0 && _.ai_protocol == 0;
    }
}

pub struct ChannelOptions {
    timeout: i32? = null,
    tries: i32? = null,
}

@opaque pub struct Channel {
    static fn init(comptime Container: type, container: *Container, options: ChannelOptions): Error? {
        let channel: *Channel = undefined;

        libraryInit();

        let opts = bun.zero(Options);

        opts.flags = ARES_FLAG_NOCHECKRESP;
        opts.sock_state_cb = &SockStateWrap(Container).onSockState;
        opts.sock_state_cb_data = unsafe { @as(*!, @ptrCast(container)) };
        opts.timeout = options.timeout ?? -1;
        opts.tries = options.tries ?? 4;

        const optmask: c_int =
            ARES_OPT_FLAGS | ARES_OPT_TIMEOUTMS |
            ARES_OPT_SOCK_STATE_CB | ARES_OPT_TRIES;

        if (e := Error.get(ares_init_options(&channel, &opts, optmask))) {
            ares_library_cleanup();
            return e;
        }

        container.channel = channel;
        return null;
    }

    fn deinit(*_) {
        ares_destroy(_);
    }

    fn getAddrInfo(*_, host: []const u8, port: u16, hints: []const AddrInfo_hints, comptime Type: type, ctx: *Type, comptime callback: AddrInfo.Callback(Type)) {
        let host_buf: [1024]u8 = undefined;
        let port_buf: [52]u8 = undefined;
        const host_ptr: ([*:0]const u8)? = brk: {
            const len = @min(host.len, host_buf.len - 1);
            @memcpy(host_buf[0..len], host[0..len]);
            host_buf[len] = 0;
            break :brk host_buf[0..len :0].ptr;
        };

        const port_ptr: ([*:0]const u8)? = brk: {
            if (port == 0) {
                break :brk null;
            }

            break :brk (std.fmt.bufPrintZ(&port_buf, "{d}", .{port}) catch unreachable).ptr;
        };

        let hints_buf: [3]AddrInfo_hints = bun.zero([3]AddrInfo_hints);
        let hint_i: usize = 0;
        for (hint of hints[0..@min(hints.len, 2)]) {
            hints_buf[hint_i] = hint;
            hint_i += 1;
        }
        const hints_: [*c]const AddrInfo_hints = if (hints.len > 0) &hints_buf else null;
        ares_getaddrinfo(_, unsafe { @constCast(host_ptr) }, unsafe { @constCast(port_ptr) }, hints_, AddrInfo.callbackWrapper(Type, callback), ctx);
    }

    fn resolve(*_, name: []const u8, comptime lookup_name: []const u8, comptime Type: type, ctx: *Type, comptime cares_type: type, comptime callback: cares_type.Callback(Type)) {
        if (name.len >= 1023 || (name.len == 0 && !(bun.strings.eqlComptime(lookup_name, "ns") || bun.strings.eqlComptime(lookup_name, "soa")))) {
            return cares_type.callbackWrapper(lookup_name, Type, callback).?(ctx, ARES_EBADNAME, 0, null, 0);
        }

        let name_buf: [1024]u8 = undefined;
        const name_ptr: [*:0]const u8 = brk: {
            const len = @min(name.len, name_buf.len - 1);
            @memcpy(name_buf[0..len], name[0..len]);

            name_buf[len] = 0;
            break :brk name_buf[0..len :0].ptr;
        };

        const field_name = comptime std.fmt.comptimePrint("ns_t_{s}", .{lookup_name});
        ares_query(_, name_ptr, NSClass.ns_c_in, @field(NSType, field_name), cares_type.callbackWrapper(lookup_name, Type, callback), ctx);
    }

    fn getHostByAddr(*_, ip_addr: []const u8, comptime Type: type, ctx: *Type, comptime callback: struct_hostent.Callback(Type)) {
        const buf_size = 46;
        let addr_buf: [buf_size]u8 = undefined;
        const addr_ptr: ([*:0]const u8)? = brk: {
            if (ip_addr.len == 0 || ip_addr.len >= buf_size) {
                break :brk null;
            }
            const len = @min(ip_addr.len, addr_buf.len - 1);
            @memcpy(addr_buf[0..len], ip_addr[0..len]);

            addr_buf[len] = 0;
            break :brk addr_buf[0..len :0].ptr;
        };

        let addr: [16]u8 = undefined;
        if (addr_ptr != null) {
            if (ares_inet_pton(AF.INET, addr_ptr, &addr) > 0) {
                ares_gethostbyaddr(_, &addr, 4, AF.INET, struct_hostent.hostCallbackWrapper(Type, callback), ctx);
                return;
            } else if (ares_inet_pton(AF.INET6, addr_ptr, &addr) > 0) {
                return ares_gethostbyaddr(_, &addr, 16, AF.INET6, struct_hostent.hostCallbackWrapper(Type, callback), ctx);
            }
        }
        struct_hostent.hostCallbackWrapper(Type, callback).?(ctx, ARES_ENOTIMP, 0, null);
    }

    fn getNameInfo(*_, sa: *std.posix.sockaddr, comptime Type: type, ctx: *Type, comptime callback: struct_nameinfo.Callback(Type)) {
        return ares_getnameinfo(
            _,
            sa,
            if (sa.*.family == AF.INET) @sizeOf(std.posix.sockaddr.in) else @sizeOf(std.posix.sockaddr.in6),
            ARES_NI_NAMEREQD | ARES_NI_LOOKUPHOST | ARES_NI_LOOKUPSERVICE,
            struct_nameinfo.CallbackWrapper(Type, callback),
            ctx,
        );
    }

    inline fn process(*_, fd: ares_socket_t, readable: bool, writable: bool) {
        ares_process_fd(
            _,
            if (readable) fd else ARES_SOCKET_BAD,
            if (writable) fd else ARES_SOCKET_BAD,
        );
    }
}

let ares_has_loaded = std.atomic.Value(bool).init(false);
fn libraryInit()  {
    if (ares_has_loaded.swap(true, .monotonic))
        return;

    const rc = ares_library_init_mem(
        ARES_LIB_INIT_ALL,
        bun.mimalloc.mi_malloc,
        bun.mimalloc.mi_free,
        bun.mimalloc.mi_realloc,
    );
    if (rc != ARES_SUCCESS) {
        std.debug.panic("ares_library_init_mem failed: {d}", .{rc});
        unreachable;
    }
}

pub type HostentPtr = (*struct_hostent)?;
pub type AddrInfoPtr = (*AddrInfo)?;
struct _ares_cb_sigs {
    @callconv("c") static fn cb(_: OpaquePtr, _: c_int, _: c_int, _: [*c]u8, _: c_int) {}
    @callconv("c") static fn host(_: OpaquePtr, _: c_int, _: c_int, _: HostentPtr) {}
    @callconv("c") static fn nameinfo(_: OpaquePtr, _: c_int, _: c_int, _: [*c]u8, _: [*c]u8) {}
    @callconv("c") static fn sock_create(_: ares_socket_t, _: c_int, _: OpaquePtr): c_int { return 0; }
    @callconv("c") static fn sock_config(_: ares_socket_t, _: c_int, _: OpaquePtr): c_int { return 0; }
    @callconv("c") static fn addrinfo(_: OpaquePtr, _: c_int, _: c_int, _: AddrInfoPtr) {}
    @callconv("c") static fn malloc(_: usize): OpaquePtr { unreachable; }
    @callconv("c") static fn free(_: OpaquePtr) {}
    @callconv("c") static fn realloc(_: OpaquePtr, _: usize): OpaquePtr { unreachable; }
}
pub const ares_callback_fn = *const @TypeOf(_ares_cb_sigs.cb);
pub type ares_callback = ares_callback_fn?;
pub const ares_host_callback_fn = *const @TypeOf(_ares_cb_sigs.host);
pub type ares_host_callback = ares_host_callback_fn?;
pub const ares_nameinfo_callback_fn = *const @TypeOf(_ares_cb_sigs.nameinfo);
pub type ares_nameinfo_callback = ares_nameinfo_callback_fn?;
pub const ares_sock_create_callback_fn = *const @TypeOf(_ares_cb_sigs.sock_create);
pub type ares_sock_create_callback = ares_sock_create_callback_fn?;
pub const ares_sock_config_callback_fn = *const @TypeOf(_ares_cb_sigs.sock_config);
pub type ares_sock_config_callback = ares_sock_config_callback_fn?;
pub const ares_addrinfo_callback = *const @TypeOf(_ares_cb_sigs.addrinfo);
pub const ares_malloc_callback_fn = *const @TypeOf(_ares_cb_sigs.malloc);
pub type ares_malloc_callback = ares_malloc_callback_fn?;
pub const ares_free_callback_fn = *const @TypeOf(_ares_cb_sigs.free);
pub type ares_free_callback = ares_free_callback_fn?;
pub const ares_realloc_callback_fn = *const @TypeOf(_ares_cb_sigs.realloc);
pub type ares_realloc_callback = ares_realloc_callback_fn?;
pub extern fn ares_library_init(flags: c_int): c_int;
pub extern fn ares_library_init_mem(flags: c_int, amalloc: ares_malloc_callback, afree: ares_free_callback, arealloc: ares_realloc_callback): c_int;
pub extern fn ares_library_initialized(): c_int;
pub extern fn ares_library_cleanup() ;
pub extern fn ares_version(version: [*c]c_int): [*c]const u8;
pub type ChannelPtr = *Channel;
pub extern fn ares_init(channelptr: *ChannelPtr): c_int;
pub extern fn ares_init_options(channelptr: *ChannelPtr, options: *Options?, optmask: c_int): c_int;
pub extern fn ares_save_options(channel: *Channel, options: *Options?, optmask: *c_int): c_int;
pub extern fn ares_destroy_options(options: *Options) ;
pub extern fn ares_dup(dest: *Channel?, src: *Channel): c_int;
pub extern fn ares_destroy(channel: *Channel) ;
pub extern fn ares_cancel(channel: *Channel) ;
pub extern fn ares_set_local_ip4(channel: *Channel, local_ip: c_uint) ;
pub extern fn ares_set_local_ip6(channel: *Channel, local_ip6: [*c]const u8) ;
pub extern fn ares_set_local_dev(channel: *Channel, local_dev_name: [*c]const u8) ;
pub extern fn ares_set_socket_callback(channel: *Channel, callback: ares_sock_create_callback, user_data: *!?) ;
pub extern fn ares_set_socket_configure_callback(channel: *Channel, callback: ares_sock_config_callback, user_data: *!?) ;
pub extern fn ares_set_sortlist(channel: *Channel, sortstr: [*c]const u8): c_int;
pub extern fn ares_getaddrinfo(channel: *Channel, node: ([*:0]const u8)?, service: ([*:0]const u8)?, hints: [*c]const AddrInfo_hints, callback: ares_addrinfo_callback, arg: *!?) ;
pub extern fn ares_freeaddrinfo(ai: *AddrInfo) ;
@c pub struct ares_socket_functions {
    socket: *const fn (c_int, c_int, c_int, *!?) callconv(.c) ares_socket_t? = null,
    close: *const fn (ares_socket_t, *!?) callconv(.c) c_int? = null,
    connect: *const fn (ares_socket_t, [*c]const struct_sockaddr, ares_socklen_t, *!?) callconv(.c) c_int? = null,
    recvfrom: *const fn (ares_socket_t, *!?, usize, c_int, [*c]struct_sockaddr, [*c]ares_socklen_t, *!?) callconv(.c) ares_ssize_t? = null,
    sendv: *const fn (ares_socket_t, [*c]const iovec, c_int, *!?) callconv(.c) ares_ssize_t? = null,
}
pub extern fn ares_set_socket_functions(channel: *Channel, funcs: *const ares_socket_functions?, user_data: *!?) ;
pub extern fn ares_send(channel: *Channel, qbuf: [*c]const u8, qlen: c_int, callback: ares_callback, arg: *!?) ;
pub extern fn ares_query(channel: *Channel, name: [*c]const u8, dnsclass: NSClass, @"type": NSType, callback: ares_callback, arg: *!?) ;
pub extern fn ares_search(channel: *Channel, name: [*c]const u8, dnsclass: c_int, @"type": c_int, callback: ares_callback, arg: *!?) ;
pub extern fn ares_gethostbyname(channel: *Channel, name: [*c]const u8, family: c_int, callback: ares_host_callback, arg: *!?) ;
pub extern fn ares_gethostbyname_file(channel: *Channel, name: [*c]const u8, family: c_int, host: [*:null]*struct_hostent?): c_int;
pub extern fn ares_gethostbyaddr(channel: *Channel, addr: *!?, addrlen: c_int, family: c_int, callback: ares_host_callback, arg: *!?) ;
pub extern fn ares_getnameinfo(channel: *Channel, sa: [*c]const struct_sockaddr, salen: ares_socklen_t, flags: c_int, callback: ares_nameinfo_callback, arg: *!?) ;
pub extern fn ares_getsock(channel: *Channel, socks: [*c]ares_socket_t, numsocks: c_int): c_int;
pub extern fn ares_timeout(channel: *Channel, maxtv: *struct_timeval?, tv: *struct_timeval?): *struct_timeval?;
pub extern fn ares_process_fd(channel: *Channel, read_fd: ares_socket_t, write_fd: ares_socket_t) ;
pub extern fn ares_create_query(name: [*c]const u8, dnsclass: c_int, @"type": c_int, id: c_ushort, rd: c_int, buf: [*c][*c]u8, buflen: [*c]c_int, max_udp_size: c_int): c_int;
pub extern fn ares_mkquery(name: [*c]const u8, dnsclass: c_int, @"type": c_int, id: c_ushort, rd: c_int, buf: [*c][*c]u8, buflen: [*c]c_int): c_int;
pub extern fn ares_expand_name(encoded: [*c]const u8, abuf: [*c]const u8, alen: c_int, s: [*c][*c]u8, enclen: [*c]c_long): c_int;
pub extern fn ares_expand_string(encoded: [*c]const u8, abuf: [*c]const u8, alen: c_int, s: [*c][*c]u8, enclen: [*c]c_long): c_int;
pub extern fn ares_queue_active_queries(channel: *const Channel): usize;
@c union union_unnamed_2 {
    _S6_u8: [16]u8,
}
@c pub struct struct_ares_in6_addr {
    _S6_un: union_unnamed_2,
}
@c pub struct struct_ares_addrttl {
    ipaddr: u32,
    ttl: c_int,
}
@c pub struct struct_ares_addr6ttl {
    ip6addr: struct_ares_in6_addr,
    ttl: c_int,
}
@c pub struct struct_ares_caa_reply {
    next: *struct_ares_caa_reply?,
    critical: c_int,
    property: [*c]u8,
    plength: usize,
    value: [*c]u8,
    length: usize,

    pub const toJSResponse = cares_jsc.caaReplyToJSResponse;

    pub const toJS = cares_jsc.caaReplyToJS;

    static fn Callback(comptime Type: type): type {
        return CallbackSigPtr(Type, struct_ares_caa_reply);
    }

    static fn callbackWrapper(
        comptime _: []const u8,
        comptime Type: type,
        comptime function: Callback(Type),
    ): ares_callback {
        return &struct {
            pub fn handle(ctx: *!?, status: c_int, timeouts: c_int, buffer: [*c]u8, buffer_length: c_int) callconv(.c) void {
                const req = bun.cast(*Type, ctx.?);
                if (status != ARES_SUCCESS) {
                    function(req, Error.get(status), timeouts, null);
                    return;
                }

                let start: [*c]struct_ares_caa_reply = undefined;
                const result = ares_parse_caa_reply(buffer, buffer_length, &start);
                if (result != ARES_SUCCESS) {
                    function(req, Error.get(result), timeouts, null);
                    return;
                }
                function(req, null, timeouts, start);
            }
        }.handle;
    }

    fn deinit(*_) {
        ares_free_data(_);
    }
}
@c pub struct struct_ares_srv_reply {
    next: *struct_ares_srv_reply?,
    host: [*c]u8,
    priority: c_ushort,
    weight: c_ushort,
    port: c_ushort,

    pub const toJSResponse = cares_jsc.srvReplyToJSResponse;

    pub const toJS = cares_jsc.srvReplyToJS;

    static fn Callback(comptime Type: type): type {
        return CallbackSigPtr(Type, struct_ares_srv_reply);
    }

    static fn callbackWrapper(
        comptime _: []const u8,
        comptime Type: type,
        comptime function: Callback(Type),
    ): ares_callback {
        return &struct {
            pub fn handleSrv(ctx: *!?, status: c_int, timeouts: c_int, buffer: [*c]u8, buffer_length: c_int) callconv(.c) void {
                const req = bun.cast(*Type, ctx.?);
                if (status != ARES_SUCCESS) {
                    function(req, Error.get(status), timeouts, null);
                    return;
                }

                let srv_start: [*c]struct_ares_srv_reply = undefined;
                const result = ares_parse_srv_reply(buffer, buffer_length, &srv_start);
                if (result != ARES_SUCCESS) {
                    function(req, Error.get(result), timeouts, null);
                    return;
                }
                function(req, null, timeouts, srv_start);
            }
        }.handleSrv;
    }

    fn deinit(*_) {
        ares_free_data(_);
    }
}
@c pub struct struct_ares_mx_reply {
    next: *struct_ares_mx_reply?,
    host: [*c]u8,
    priority: c_ushort,

    pub const toJSResponse = cares_jsc.mxReplyToJSResponse;

    pub const toJS = cares_jsc.mxReplyToJS;

    static fn Callback(comptime Type: type): type {
        return CallbackSigPtr(Type, struct_ares_mx_reply);
    }

    static fn callbackWrapper(
        comptime _: []const u8,
        comptime Type: type,
        comptime function: Callback(Type),
    ): ares_callback {
        return &struct {
            pub fn handle(ctx: *!?, status: c_int, timeouts: c_int, buffer: [*c]u8, buffer_length: c_int) callconv(.c) void {
                const req = bun.cast(*Type, ctx.?);
                if (status != ARES_SUCCESS) {
                    function(req, Error.get(status), timeouts, null);
                    return;
                }

                let start: [*c]struct_ares_mx_reply = undefined;
                const result = ares_parse_mx_reply(buffer, buffer_length, &start);
                if (result != ARES_SUCCESS) {
                    function(req, Error.get(result), timeouts, null);
                    return;
                }
                function(req, null, timeouts, start);
            }
        }.handle;
    }

    fn deinit(*_) {
        ares_free_data(_);
    }
}
@c pub struct struct_ares_txt_reply {
    next: *struct_ares_txt_reply?,
    txt: [*c]u8,
    length: usize,

    pub const toJSResponse = cares_jsc.txtReplyToJSResponse;

    pub const toJS = cares_jsc.txtReplyToJS;

    pub const toJSForAny = cares_jsc.txtReplyToJSForAny;

    static fn Callback(comptime Type: type): type {
        return CallbackSigPtr(Type, struct_ares_txt_reply);
    }

    static fn callbackWrapper(
        comptime _: []const u8,
        comptime Type: type,
        comptime function: Callback(Type),
    ): ares_callback {
        return &struct {
            pub fn handleTxt(ctx: *!?, status: c_int, timeouts: c_int, buffer: [*c]u8, buffer_length: c_int) callconv(.c) void {
                const req = bun.cast(*Type, ctx.?);
                if (status != ARES_SUCCESS) {
                    function(req, Error.get(status), timeouts, null);
                    return;
                }

                let srv_start: [*c]struct_ares_txt_reply = undefined;
                const result = ares_parse_txt_reply(buffer, buffer_length, &srv_start);
                if (result != ARES_SUCCESS) {
                    function(req, Error.get(result), timeouts, null);
                    return;
                }
                function(req, null, timeouts, srv_start);
            }
        }.handleTxt;
    }

    fn deinit(*_) {
        ares_free_data(_);
    }
}
@c pub struct struct_ares_txt_ext {
    next: [*c]struct_ares_txt_ext,
    txt: [*c]u8,
    length: usize,
    record_start: u8,
}
@c pub struct struct_ares_naptr_reply {
    next: *struct_ares_naptr_reply?,
    flags: [*c]u8,
    service: [*c]u8,
    regexp: [*c]u8,
    replacement: [*c]u8,
    order: c_ushort,
    preference: c_ushort,

    pub const toJSResponse = cares_jsc.naptrReplyToJSResponse;

    pub const toJS = cares_jsc.naptrReplyToJS;

    static fn Callback(comptime Type: type): type {
        return CallbackSigPtr(Type, struct_ares_naptr_reply);
    }

    static fn callbackWrapper(
        comptime _: []const u8,
        comptime Type: type,
        comptime function: Callback(Type),
    ): ares_callback {
        return &struct {
            pub fn handleNaptr(ctx: *!?, status: c_int, timeouts: c_int, buffer: [*c]u8, buffer_length: c_int) callconv(.c) void {
                const req = bun.cast(*Type, ctx.?);
                if (status != ARES_SUCCESS) {
                    function(req, Error.get(status), timeouts, null);
                    return;
                }

                let naptr_start: [*c]struct_ares_naptr_reply = undefined;
                const result = ares_parse_naptr_reply(buffer, buffer_length, &naptr_start);
                if (result != ARES_SUCCESS) {
                    function(req, Error.get(result), timeouts, null);
                    return;
                }
                function(req, null, timeouts, naptr_start);
            }
        }.handleNaptr;
    }

    fn deinit(*_) {
        ares_free_data(_);
    }
}
@c pub struct struct_ares_soa_reply {
    nsname: [*c]u8,
    hostmaster: [*c]u8,
    serial: c_uint,
    refresh: c_uint,
    retry: c_uint,
    expire: c_uint,
    minttl: c_uint,

    pub const toJSResponse = cares_jsc.soaReplyToJSResponse;

    pub const toJS = cares_jsc.soaReplyToJS;

    static fn Callback(comptime Type: type): type {
        return CallbackSigPtr(Type, struct_ares_soa_reply);
    }

    static fn callbackWrapper(
        comptime _: []const u8,
        comptime Type: type,
        comptime function: Callback(Type),
    ): ares_callback {
        return &struct {
            pub fn handleSoa(ctx: *!?, status: c_int, timeouts: c_int, buffer: [*c]u8, buffer_length: c_int) callconv(.c) void {
                const req = bun.cast(*Type, ctx.?);
                if (status != ARES_SUCCESS) {
                    function(req, Error.get(status), timeouts, null);
                    return;
                }

                let soa_start: [*c]struct_ares_soa_reply = undefined;
                const result = ares_parse_soa_reply(buffer, buffer_length, &soa_start);
                if (result != ARES_SUCCESS) {
                    function(req, Error.get(result), timeouts, null);
                    return;
                }
                function(req, null, timeouts, soa_start);
            }
        }.handleSoa;
    }

    fn deinit(*_) {
        ares_free_data(_);
    }
}

@c pub struct struct_ares_uri_reply {
    next: [*c]struct_ares_uri_reply,
    priority: c_ushort,
    weight: c_ushort,
    uri: [*c]u8,
    ttl: c_int,
}

pub struct struct_any_reply {
    a_reply: *hostent_with_ttls? = null,
    aaaa_reply: *hostent_with_ttls? = null,
    mx_reply: *struct_ares_mx_reply? = null,
    ns_reply: *struct_hostent? = null,
    txt_reply: *struct_ares_txt_reply? = null,
    srv_reply: *struct_ares_srv_reply? = null,
    ptr_reply: *struct_hostent? = null,
    naptr_reply: *struct_ares_naptr_reply? = null,
    soa_reply: *struct_ares_soa_reply? = null,
    caa_reply: *struct_ares_caa_reply? = null,

    pub const toJSResponse = cares_jsc.anyReplyToJSResponse;

    pub const toJS = cares_jsc.anyReplyToJS;
    pub const Callback = anyReplyCallback;
    pub const callbackWrapper = anyReplyCallbackWrapper;

    pub fn deinit(*_)  {
        inline for (field of @typeInfo(struct_any_reply).@"struct".fields) {
            if (comptime std.mem.endsWith(u8, field.name, "_reply")) {
                if (reply := @field(_, field.name)) {
                    reply.deinit();
                }
            }
        }

        bun.default_allocator.destroy(_);
    }
}

pub type OptCInt = c_int?;

fn anyReplyCallback(comptime Type: type): type {
    return CallbackSigPtr(Type, struct_any_reply);
}

fn anyReplyCallbackWrapper(
    comptime _: []const u8,
    comptime Type: type,
    comptime function: anyReplyCallback(Type),
): ares_callback {
    return &struct {
        pub fn handleAny(ctx: *!?, status: c_int, timeouts: c_int, buffer: [*c]u8, buffer_length: c_int) callconv(.c) void {
            const req = bun.cast(*Type, ctx.?);
            if (status != ARES_SUCCESS) {
                function(req, Error.get(status), timeouts, null);
                return;
            }

            let any_success = false;
            let last_error: OptCInt = null;
            let reply = bun.handleOom(bun.default_allocator.create(struct_any_reply));
            reply.* = .{};

            switch (hostent_with_ttls.parse("a", buffer, buffer_length)) {
                .result => |result| {
                    reply.a_reply = result;
                    any_success = true;
                },
                .err => |err| last_error = @intFromEnum(err),
            }

            switch (hostent_with_ttls.parse("aaaa", buffer, buffer_length)) {
                .result => |result| {
                    reply.aaaa_reply = result;
                    any_success = true;
                },
                .err => |err| last_error = @intFromEnum(err),
            }

            let result = ares_parse_mx_reply(buffer, buffer_length, &reply.mx_reply);
            if (result == ARES_SUCCESS) {
                any_success = true;
            } else {
                last_error = result;
            }

            result = ares_parse_ns_reply(buffer, buffer_length, &reply.ns_reply);
            if (result == ARES_SUCCESS) {
                any_success = true;
            } else {
                last_error = result;
            }

            result = ares_parse_txt_reply(buffer, buffer_length, &reply.txt_reply);
            if (result == ARES_SUCCESS) {
                any_success = true;
            } else {
                last_error = result;
            }

            result = ares_parse_srv_reply(buffer, buffer_length, &reply.srv_reply);
            if (result == ARES_SUCCESS) {
                any_success = true;
            } else {
                last_error = result;
            }

            result = ares_parse_ptr_reply(buffer, buffer_length, null, 0, AF.INET, &reply.ptr_reply);
            if (result == ARES_SUCCESS) {
                any_success = true;
            } else {
                last_error = result;
            }

            result = ares_parse_naptr_reply(buffer, buffer_length, &reply.naptr_reply);
            if (result == ARES_SUCCESS) {
                any_success = true;
            } else {
                last_error = result;
            }

            result = ares_parse_soa_reply(buffer, buffer_length, &reply.soa_reply);
            if (result == ARES_SUCCESS) {
                any_success = true;
            } else {
                last_error = result;
            }

            result = ares_parse_caa_reply(buffer, buffer_length, &reply.caa_reply);
            if (result == ARES_SUCCESS) {
                any_success = true;
            } else {
                last_error = result;
            }

            if (!any_success) {
                reply.deinit();
                function(req, Error.get(last_error.?), timeouts, null);
                return;
            }

            function(req, null, timeouts, reply);
        }
    }.handleAny;
}
pub extern fn ares_parse_a_reply(abuf: [*c]const u8, alen: c_int, host: *(*struct_hostent)?, addrttls: [*c]struct_ares_addrttl, naddrttls: [*c]c_int): c_int;
pub extern fn ares_parse_aaaa_reply(abuf: [*c]const u8, alen: c_int, host: *(*struct_hostent)?, addrttls: [*c]struct_ares_addr6ttl, naddrttls: [*c]c_int): c_int;
pub extern fn ares_parse_caa_reply(abuf: [*c]const u8, alen: c_int, caa_out: [*c][*c]struct_ares_caa_reply): c_int;
pub extern fn ares_parse_ptr_reply(abuf: [*c]const u8, alen: c_int, addr: *!?, addrlen: c_int, family: c_int, host: *(*struct_hostent)?): c_int;
pub extern fn ares_parse_ns_reply(abuf: [*c]const u8, alen: c_int, host: *(*struct_hostent)?): c_int;
pub extern fn ares_parse_srv_reply(abuf: [*c]const u8, alen: c_int, srv_out: [*c][*c]struct_ares_srv_reply): c_int;
pub extern fn ares_parse_mx_reply(abuf: [*c]const u8, alen: c_int, mx_out: [*c][*c]struct_ares_mx_reply): c_int;
pub extern fn ares_parse_txt_reply(abuf: [*c]const u8, alen: c_int, txt_out: [*c][*c]struct_ares_txt_reply): c_int;
pub extern fn ares_parse_txt_reply_ext(abuf: [*c]const u8, alen: c_int, txt_out: [*c][*c]struct_ares_txt_ext): c_int;
pub extern fn ares_parse_naptr_reply(abuf: [*c]const u8, alen: c_int, naptr_out: [*c][*c]struct_ares_naptr_reply): c_int;
pub extern fn ares_parse_soa_reply(abuf: [*c]const u8, alen: c_int, soa_out: [*c][*c]struct_ares_soa_reply): c_int;
pub extern fn ares_parse_uri_reply(abuf: [*c]const u8, alen: c_int, uri_out: [*c][*c]struct_ares_uri_reply): c_int;
pub extern fn ares_free_string(str: *!?) ;
pub extern fn ares_free_hostent(host: *struct_hostent?) ;
pub extern fn ares_free_data(dataptr: *!?) ;
pub extern fn ares_strerror(code: c_int): [*c]const u8;
@c union union_unnamed_3 {
    addr4: struct_in_addr,
    addr6: struct_ares_in6_addr,
}
@c pub struct struct_ares_addr_node {
    next: (*struct_ares_addr_node)?,
    family: c_int,
    addr: union_unnamed_3,
}
@c union union_unnamed_4 {
    addr4: struct_in_addr,
    addr6: struct_ares_in6_addr,
}
@c pub struct struct_ares_addr_port_node {
    next: (*struct_ares_addr_port_node)?,
    family: c_int,
    addr: union_unnamed_4,
    udp_port: c_int,
    tcp_port: c_int,
}
pub extern fn ares_set_servers(channel: *Channel, servers: [*c]struct_ares_addr_node): c_int;
pub extern fn ares_set_servers_ports(channel: *Channel, servers: [*c]struct_ares_addr_port_node): c_int;
pub extern fn ares_set_servers_csv(channel: *Channel, servers: [*c]const u8): c_int;
pub extern fn ares_set_servers_ports_csv(channel: *Channel, servers: [*c]const u8): c_int;
pub extern fn ares_get_servers(channel: *Channel, servers: *(*struct_ares_addr_port_node)?): c_int;
pub extern fn ares_get_servers_ports(channel: *Channel, servers: *(*struct_ares_addr_port_node)?): c_int;
pub extern fn ares_inet_ntop(af: c_int, src: (*const void)?, dst: [*]u8, size: ares_socklen_t): [*:0]const u8?;
pub extern fn ares_inet_pton(af: c_int, src: [*c]const u8, dst: *!?): c_int;
pub const ARES_SUCCESS = 0;
pub const ARES_ENODATA = 1;
pub const ARES_EFORMERR = 2;
pub const ARES_ESERVFAIL = 3;
pub const ARES_ENOTFOUND = 4;
pub const ARES_ENOTIMP = 5;
pub const ARES_EREFUSED = 6;
pub const ARES_EBADQUERY = 7;
pub const ARES_EBADNAME = 8;
pub const ARES_EBADFAMILY = 9;
pub const ARES_EBADRESP = 10;
pub const ARES_ECONNREFUSED = 11;
pub const ARES_ETIMEOUT = 12;
pub const ARES_EOF = 13;
pub const ARES_EFILE = 14;
pub const ARES_ENOMEM = 15;
pub const ARES_EDESTRUCTION = 16;
pub const ARES_EBADSTR = 17;
pub const ARES_EBADFLAGS = 18;
pub const ARES_ENONAME = 19;
pub const ARES_EBADHINTS = 20;
pub const ARES_ENOTINITIALIZED = 21;
pub const ARES_ELOADIPHLPAPI = 22;
pub const ARES_EADDRGETNETWORKPARAMS = 23;
pub const ARES_ECANCELLED = 24;
pub const ARES_ESERVICE = 25;
pub const ARES_ENOSERVER = 26;

pub enum Error: i32 {
    ENODATA = ARES_ENODATA,
    EFORMERR = ARES_EFORMERR,
    ESERVFAIL = ARES_ESERVFAIL,
    ENOTFOUND = ARES_ENOTFOUND,
    ENOTIMP = ARES_ENOTIMP,
    EREFUSED = ARES_EREFUSED,
    EBADQUERY = ARES_EBADQUERY,
    EBADNAME = ARES_EBADNAME,
    EBADFAMILY = ARES_EBADFAMILY,
    EBADRESP = ARES_EBADRESP,
    ECONNREFUSED = ARES_ECONNREFUSED,
    ETIMEOUT = ARES_ETIMEOUT,
    EOF = ARES_EOF,
    EFILE = ARES_EFILE,
    ENOMEM = ARES_ENOMEM,
    EDESTRUCTION = ARES_EDESTRUCTION,
    EBADSTR = ARES_EBADSTR,
    EBADFLAGS = ARES_EBADFLAGS,
    ENONAME = ARES_ENONAME,
    EBADHINTS = ARES_EBADHINTS,
    ENOTINITIALIZED = ARES_ENOTINITIALIZED,
    ELOADIPHLPAPI = ARES_ELOADIPHLPAPI,
    EADDRGETNETWORKPARAMS = ARES_EADDRGETNETWORKPARAMS,
    ECANCELLED = ARES_ECANCELLED,
    ESERVICE = ARES_ESERVICE,
    ENOSERVER = ARES_ENOSERVER,
    EAI_AGAIN = libuv.UV_EAI_AGAIN,

    const Deferred = cares_jsc.ErrorDeferred;
    const toDeferred = cares_jsc.errorToDeferred;
    const toJSWithSyscall = cares_jsc.errorToJSWithSyscall;
    const toJSWithSyscallAndHostname = cares_jsc.errorToJSWithSyscallAndHostname;

    static fn initEAI(rc: i32): Error? {
        if (comptime bun.Environment.isWindows) {
            if (rc == libuv.UV_EAI_NODATA || rc == libuv.UV_EAI_NONAME) {
                return Error.ENOTFOUND;
            }

            return switch (rc) {
                0 => null,
                libuv.UV_EAI_AGAIN => Error.EAI_AGAIN,
                libuv.UV_EAI_ADDRFAMILY => Error.EBADFAMILY,
                libuv.UV_EAI_BADFLAGS => Error.EBADFLAGS,
                libuv.UV_EAI_BADHINTS => Error.EBADHINTS,
                libuv.UV_EAI_CANCELED => Error.ECANCELLED,
                libuv.UV_EAI_FAIL => Error.ENOTFOUND,
                libuv.UV_EAI_FAMILY => Error.EBADFAMILY,
                libuv.UV_EAI_MEMORY => Error.ENOMEM,
                libuv.UV_EAI_NODATA => Error.ENODATA,
                libuv.UV_EAI_NONAME => Error.ENONAME,
                libuv.UV_EAI_OVERFLOW => Error.ENOMEM,
                libuv.UV_EAI_PROTOCOL => Error.EBADQUERY,
                libuv.UV_EAI_SERVICE => Error.ESERVICE,
                libuv.UV_EAI_SOCKTYPE => Error.ECONNREFUSED,
                else => Error.ENOTFOUND,
            };
        }

        const eai: std.posix.system.EAI = @enumFromInt(rc);

        if (eai == .NODATA || eai == .NONAME) {
            return Error.ENOTFOUND;
        }

        if (comptime bun.Environment.isLinux) {
            if (eai == .SOCKTYPE) return Error.ECONNREFUSED;
        }
        if (comptime bun.Environment.isGlibc) {
            switch (eai) {
                .IDN_ENCODE => return Error.EBADSTR,
                .ALLDONE => return Error.ENOTFOUND,
                .INPROGRESS => return Error.ETIMEOUT,
                .CANCELED => return Error.ECANCELLED,
                .NOTCANCELED => return Error.ECANCELLED,
                else => {},
            }
        }

        const eai_zero: std.posix.system.EAI = @enumFromInt(0);
        return switch (eai) {
            eai_zero => return null,
            .AGAIN => Error.EAI_AGAIN,
            .ADDRFAMILY => Error.EBADFAMILY,
            .BADFLAGS => Error.EBADFLAGS,
            .FAIL => Error.EBADRESP,
            .FAMILY => Error.EBADFAMILY,
            .MEMORY => Error.ENOMEM,
            .SERVICE => Error.ESERVICE,
            .SYSTEM => Error.ESERVFAIL,
            else => bun.todo(@src(), Error.ENOTIMP),
        };
    }

    const code = bun.enumMap(Error, .{
        .{ .ENODATA, "DNS_ENODATA" },
        .{ .EFORMERR, "DNS_EFORMERR" },
        .{ .ESERVFAIL, "DNS_ESERVFAIL" },
        .{ .ENOTFOUND, "DNS_ENOTFOUND" },
        .{ .ENOTIMP, "DNS_ENOTIMP" },
        .{ .EREFUSED, "DNS_EREFUSED" },
        .{ .EBADQUERY, "DNS_EBADQUERY" },
        .{ .EBADNAME, "DNS_ENOTFOUND" },
        .{ .EBADFAMILY, "DNS_EBADFAMILY" },
        .{ .EBADRESP, "DNS_EBADRESP" },
        .{ .ECONNREFUSED, "DNS_ECONNREFUSED" },
        .{ .ETIMEOUT, "DNS_ETIMEOUT" },
        .{ .EOF, "DNS_EOF" },
        .{ .EFILE, "DNS_EFILE" },
        .{ .ENOMEM, "DNS_ENOMEM" },
        .{ .EDESTRUCTION, "DNS_EDESTRUCTION" },
        .{ .EBADSTR, "DNS_EBADSTR" },
        .{ .EBADFLAGS, "DNS_EBADFLAGS" },
        .{ .ENONAME, "DNS_ENOTFOUND" },
        .{ .EBADHINTS, "DNS_EBADHINTS" },
        .{ .ENOTINITIALIZED, "DNS_ENOTINITIALIZED" },
        .{ .ELOADIPHLPAPI, "DNS_ELOADIPHLPAPI" },
        .{ .EADDRGETNETWORKPARAMS, "DNS_EADDRGETNETWORKPARAMS" },
        .{ .ECANCELLED, "DNS_ECANCELLED" },
        .{ .ESERVICE, "DNS_ESERVICE" },
        .{ .ENOSERVER, "DNS_ENOSERVER" },
        .{ .EAI_AGAIN, "DNS_EAI_AGAIN" },
    });

    const label = bun.enumMap(Error, .{
        .{ .ENODATA, "No data record of requested type" },
        .{ .EFORMERR, "Malformed DNS query" },
        .{ .ESERVFAIL, "Server failed to complete the DNS operation" },
        .{ .ENOTFOUND, "Domain name not found" },
        .{ .ENOTIMP, "DNS resolver does not implement requested operation" },
        .{ .EREFUSED, "DNS operation refused" },
        .{ .EBADQUERY, "Misformatted DNS query" },
        .{ .EBADNAME, "Misformatted domain name" },
        .{ .EBADFAMILY, "Misformatted DNS query (family)" },
        .{ .EBADRESP, "Misformatted DNS reply" },
        .{ .ECONNREFUSED, "Could not contact DNS servers" },
        .{ .ETIMEOUT, "Timeout while contacting DNS servers" },
        .{ .EOF, "End of file" },
        .{ .EFILE, "Error reading file" },
        .{ .ENOMEM, "Out of memory" },
        .{ .EDESTRUCTION, "Channel is being destroyed" },
        .{ .EBADSTR, "Misformatted string" },
        .{ .EBADFLAGS, "Illegal flags specified" },
        .{ .ENONAME, "Given hostname is not numeric" },
        .{ .EBADHINTS, "Illegal hints flags specified" },
        .{ .ENOTINITIALIZED, "Library initialization not yet performed" },
        .{ .ELOADIPHLPAPI, "ELOADIPHLPAPI TODO WHAT DOES THIS MEAN" },
        .{ .EADDRGETNETWORKPARAMS, "EADDRGETNETWORKPARAMS" },
        .{ .ECANCELLED, "DNS query cancelled" },
        .{ .ESERVICE, "Service not available" },
        .{ .ENOSERVER, "No DNS servers were configured" },
        .{ .EAI_AGAIN, "temporary failure" },
    });

    static fn get(rc: i32): Error? {
        if (rc == ARES_ENODATA || rc == ARES_ENONAME) {
            return Error.get(ARES_ENOTFOUND);
        }

        return switch (rc) {
            0 => null,
            1..=ARES_ENOSERVER => @as(Error, @enumFromInt(rc)),
            -ARES_ENOSERVER..=-1 => @as(Error, @enumFromInt(-rc)),
            else => unreachable,
        };
    }
}

pub const ARES_FLAG_USEVC = @as(c_int, 1) << @as(c_int, 0);
pub const ARES_FLAG_PRIMARY = @as(c_int, 1) << @as(c_int, 1);
pub const ARES_FLAG_IGNTC = @as(c_int, 1) << @as(c_int, 2);
pub const ARES_FLAG_NORECURSE = @as(c_int, 1) << @as(c_int, 3);
pub const ARES_FLAG_STAYOPEN = @as(c_int, 1) << @as(c_int, 4);
pub const ARES_FLAG_NOSEARCH = @as(c_int, 1) << @as(c_int, 5);
pub const ARES_FLAG_NOALIASES = @as(c_int, 1) << @as(c_int, 6);
pub const ARES_FLAG_NOCHECKRESP = @as(c_int, 1) << @as(c_int, 7);
pub const ARES_FLAG_NO_DFLT_SVR = @as(c_int, 1) << @as(c_int, 9);
pub const ARES_FLAG_EDNS = @as(c_int, 1) << @as(c_int, 8);
pub const ARES_OPT_FLAGS = @as(c_int, 1) << @as(c_int, 0);
pub const ARES_OPT_TIMEOUT = @as(c_int, 1) << @as(c_int, 1);
pub const ARES_OPT_TRIES = @as(c_int, 1) << @as(c_int, 2);
pub const ARES_OPT_NDOTS = @as(c_int, 1) << @as(c_int, 3);
pub const ARES_OPT_UDP_PORT = @as(c_int, 1) << @as(c_int, 4);
pub const ARES_OPT_TCP_PORT = @as(c_int, 1) << @as(c_int, 5);
pub const ARES_OPT_SERVERS = @as(c_int, 1) << @as(c_int, 6);
pub const ARES_OPT_DOMAINS = @as(c_int, 1) << @as(c_int, 7);
pub const ARES_OPT_LOOKUPS = @as(c_int, 1) << @as(c_int, 8);
pub const ARES_OPT_SOCK_STATE_CB = @as(c_int, 1) << @as(c_int, 9);
pub const ARES_OPT_SORTLIST = @as(c_int, 1) << @as(c_int, 10);
pub const ARES_OPT_SOCK_SNDBUF = @as(c_int, 1) << @as(c_int, 11);
pub const ARES_OPT_SOCK_RCVBUF = @as(c_int, 1) << @as(c_int, 12);
pub const ARES_OPT_TIMEOUTMS = @as(c_int, 1) << @as(c_int, 13);
pub const ARES_OPT_ROTATE = @as(c_int, 1) << @as(c_int, 14);
pub const ARES_OPT_EDNSPSZ = @as(c_int, 1) << @as(c_int, 15);
pub const ARES_OPT_NOROTATE = @as(c_int, 1) << @as(c_int, 16);
pub const ARES_OPT_RESOLVCONF = @as(c_int, 1) << @as(c_int, 17);
pub const ARES_OPT_HOSTS_FILE = @as(c_int, 1) << @as(c_int, 18);
pub const ARES_NI_NOFQDN = @as(c_int, 1) << @as(c_int, 0);
pub const ARES_NI_NUMERICHOST = @as(c_int, 1) << @as(c_int, 1);
pub const ARES_NI_NAMEREQD = @as(c_int, 1) << @as(c_int, 2);
pub const ARES_NI_NUMERICSERV = @as(c_int, 1) << @as(c_int, 3);
pub const ARES_NI_DGRAM = @as(c_int, 1) << @as(c_int, 4);
pub const ARES_NI_TCP = @as(c_int, 0);
pub const ARES_NI_UDP = ARES_NI_DGRAM;
pub const ARES_NI_SCTP = @as(c_int, 1) << @as(c_int, 5);
pub const ARES_NI_DCCP = @as(c_int, 1) << @as(c_int, 6);
pub const ARES_NI_NUMERICSCOPE = @as(c_int, 1) << @as(c_int, 7);
pub const ARES_NI_LOOKUPHOST = @as(c_int, 1) << @as(c_int, 8);
pub const ARES_NI_LOOKUPSERVICE = @as(c_int, 1) << @as(c_int, 9);
pub const ARES_NI_IDN = @as(c_int, 1) << @as(c_int, 10);
pub const ARES_NI_IDN_ALLOW_UNASSIGNED = @as(c_int, 1) << @as(c_int, 11);
pub const ARES_NI_IDN_USE_STD3_ASCII_RULES = @as(c_int, 1) << @as(c_int, 12);
pub const ARES_AI_CANONNAME = @as(c_int, 1) << @as(c_int, 0);
pub const ARES_AI_NUMERICHOST = @as(c_int, 1) << @as(c_int, 1);
pub const ARES_AI_PASSIVE = @as(c_int, 1) << @as(c_int, 2);
pub const ARES_AI_NUMERICSERV = @as(c_int, 1) << @as(c_int, 3);
pub const ARES_AI_V4MAPPED = @as(c_int, 1) << @as(c_int, 4);
pub const ARES_AI_ALL = @as(c_int, 1) << @as(c_int, 5);
pub const ARES_AI_ADDRCONFIG = @as(c_int, 1) << @as(c_int, 6);
pub const ARES_AI_NOSORT = @as(c_int, 1) << @as(c_int, 7);
pub const ARES_AI_ENVHOSTS = @as(c_int, 1) << @as(c_int, 8);
pub const ARES_AI_IDN = @as(c_int, 1) << @as(c_int, 10);
pub const ARES_AI_IDN_ALLOW_UNASSIGNED = @as(c_int, 1) << @as(c_int, 11);
pub const ARES_AI_IDN_USE_STD3_ASCII_RULES = @as(c_int, 1) << @as(c_int, 12);
pub const ARES_AI_CANONIDN = @as(c_int, 1) << @as(c_int, 13);
pub const ARES_AI_MASK = (((((ARES_AI_CANONNAME | ARES_AI_NUMERICHOST) | ARES_AI_PASSIVE) | ARES_AI_NUMERICSERV) | ARES_AI_V4MAPPED) | ARES_AI_ALL) | ARES_AI_ADDRCONFIG;
pub const ARES_GETSOCK_MAXNUM = @as(c_int, 16);
pub inline fn ARES_GETSOCK_READABLE(bits: anytype, num: anytype): @TypeOf(bits & (@as(c_int, 1) << num)) {
    return bits & (@as(c_int, 1) << num);
}
pub inline fn ARES_GETSOCK_WRITABLE(bits: anytype, num: anytype): @TypeOf(bits & (@as(c_int, 1) << (num + ARES_GETSOCK_MAXNUM))) {
    return bits & (@as(c_int, 1) << (num + ARES_GETSOCK_MAXNUM));
}
pub const ARES_LIB_INIT_NONE = @as(c_int, 0);
pub const ARES_LIB_INIT_WIN32 = @as(c_int, 1) << @as(c_int, 0);
pub const ARES_LIB_INIT_ALL = ARES_LIB_INIT_WIN32;
pub const ARES_SOCKET_BAD = if (bun.Environment.isWindows) std.os.windows.ws2_32.INVALID_SOCKET else -@as(c_int, 1);
pub const ares_socket_typedef = "";
pub const ares_addrinfo_cname = AddrInfo_cname;
pub const ares_addrinfo_node = AddrInfo_node;
pub const ares_addrinfo = AddrInfo;
pub const ares_addrinfo_hints = AddrInfo_hints;
pub const ares_in6_addr = struct_ares_in6_addr;
pub const ares_addrttl = struct_ares_addrttl;
pub const ares_addr6ttl = struct_ares_addr6ttl;
pub const ares_caa_reply = struct_ares_caa_reply;
pub const ares_srv_reply = struct_ares_srv_reply;
pub const ares_mx_reply = struct_ares_mx_reply;
pub const ares_txt_reply = struct_ares_txt_reply;
pub const ares_txt_ext = struct_ares_txt_ext;
pub const ares_naptr_reply = struct_ares_naptr_reply;
pub const ares_soa_reply = struct_ares_soa_reply;
pub const ares_uri_reply = struct_ares_uri_reply;
pub const ares_addr_node = struct_ares_addr_node;
pub const ares_addr_port_node = struct_ares_addr_port_node;

pub fn getSockaddr(addr: []const u8, port: u16, sa: *std.posix.sockaddr): c_int {
    const buf_size = 128;

    let buf: [buf_size]u8 = undefined;
    const addr_ptr: [*:0]const u8 = brk: {
        if (addr.len == 0 || addr.len >= buf_size) {
            return -1;
        }
        const len = @min(addr.len, buf.len - 1);
        @memcpy(buf[0..len], addr[0..len]);

        buf[len] = 0;
        break :brk buf[0..len :0].ptr;
    };

    {
        let in_sa: *std.posix.sockaddr.in = unsafe { @ptrCast(@alignCast(sa)) };
        if (ares_inet_pton(AF.INET, addr_ptr, &in_sa.addr) == 1) {
            in_sa.family = AF.INET;
            in_sa.port = std.mem.nativeToBig(u16, port);
            return 0;
        }
    }
    {
        let in6: *std.posix.sockaddr.in6 = unsafe { @ptrCast(@alignCast(sa)) };
        if (ares_inet_pton(AF.INET6, addr_ptr, &in6.addr) == 1) {
            in6.family = AF.INET6;
            in6.port = std.mem.nativeToBig(u16, port);
            return 0;
        }
    }

    return -1;
}

const iovec = std.os.iovec;

const jsc = bun.jsc;
const strings = bun.strings;
const libuv = bun.windows.libuv;

const c = std.c;
const ares_socklen_t = c.socklen_t;
const fd_set = c.fd_set;

const struct_sockaddr = std.posix.sockaddr;
const struct_in_addr = std.posix.sockaddr.in;
