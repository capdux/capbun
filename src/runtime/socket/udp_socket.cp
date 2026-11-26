import std from "std";
import bun from "bun";

const Async = bun.Async;
const Output = bun.Output;
const default_allocator = bun.default_allocator;
const jsc = bun.jsc;
const CallFrame = jsc.CallFrame;
const JSGlobalObject = jsc.JSGlobalObject;
const JSRef = jsc.JSRef;
const JSValue = jsc.JSValue;
const SocketAddress = jsc.API.SocketAddress;
import uws from "../../uws/uws.zig";

const log = Output.scoped(.UdpSocket, .visible);

const INET6_ADDRSTRLEN = if (bun.Environment.isWindows) 65 else 46;

struct DeinitGuard<T> {
    ptr: *T!,
    ~init() {
        _.ptr.deinit();
    }
}

struct DerefGuard<T> {
    val: T,
    ~init() {
        _.val.deref();
    }
}

struct FreeGuard<T> {
    slice: T,
    ~init() {
        bun.default_allocator.free(_.slice);
    }
}

struct EventLoopExitGuard<T> {
    loop: T,
    ~init() {
        _.loop.exit();
    }
}

struct EnsureAliveGuard {
    val: JSValue,
    ~init() {
        _.val.ensureStillAlive();
    }
}

struct ErrReleaseGuard<T> {
    ptr: *T!,
    armed: bool = true,
    ~init() {
        if (_.armed) _.ptr.release();
    }
}

struct UDPSocketInitGuard<T> {
    ptr: T,
    armed: bool = true,
    ~init() {
        if (_.armed) {
            _.ptr.closed = true;
            if (socket := _.ptr.socket) {
                _.ptr.socket = null;
                socket.close();
            }
            _.ptr.this_value.downgrade();
        }
    }
}

struct AddrInfoGuard {
    addrinfo: *std.c.addrinfo,
    ~init() {
        std.c.freeaddrinfo(_.addrinfo);
    }
}

fn parsePort(globalThis: *JSGlobalObject, value: JSValue, comptime min_port: u16, comptime name: []const u8): u16 ! bun.JSError {
    const num = value.toNumber(globalThis)?;
    if (!std.math.isFinite(num) || @trunc(num) != num || num < @as(f64, @floatFromInt(min_port)) || num > 65535.0) {
        return globalThis.throwInvalidArguments("Expected \"{s}\" to be an integer between {d} and 65535", .{ name, min_port });
    }
    return @intFromFloat(num);
}

fn throwGetaddrinfoNotFound(globalThis: *JSGlobalObject, hostname: []const u8): JSValue ! bun.JSError {
    const sys_err = jsc.SystemError{
        .errno = @intFromEnum(bun.c_ares.Error.ENOTFOUND),
        .code = bun.String.static("ENOTFOUND"),
        .message = bun.handleOom(bun.String.createFormat("getaddrinfo ENOTFOUND {s}", .{hostname})),
        .syscall = bun.String.static("getaddrinfo"),
        .hostname = bun.String.cloneUTF8(hostname),
    };
    return globalThis.throwValue(sys_err.toErrorInstance(globalThis));
}

extern fn ntohs(nshort: u16): u16;
extern fn htonl(hlong: u32): u32;
extern fn htons(hshort: u16): u16;
type ConstOpaquePtr = *!?;
type SockaddrInPtr = *std.posix.sockaddr.in;
type SockaddrIn6Ptr = *std.posix.sockaddr.in6;
extern fn inet_ntop(af: c_int, src: ConstOpaquePtr, dst: [*c]u8, size: c_int): ([*:0]const u8)?;
extern fn inet_pton(af: c_int, src: [*c]const u8, dst: *!?): c_int;

@c
fn onClose(socket: *uws.udp.Socket) {
    jsc.markBinding(@src());

    let this_socket: *UDPSocket = bun.cast(*UDPSocket, socket.user().?);
    this_socket.closed = true;
    this_socket.poll_ref.disable();
    this_socket.this_value.downgrade();
    this_socket.socket = null;
}

@c
fn onRecvError(socket: *uws.udp.Socket, errno: c_int) {
    jsc.markBinding(@src());

    let this_socket: *UDPSocket = bun.cast(*UDPSocket, socket.user().?);
    const sys_err = bun.sys.Error.fromCodeInt(errno, .recv);
    const globalThis = this_socket.globalThis;
    const err_value = sys_err.toJS(globalThis) catch return;
    this_socket.callErrorHandler(.zero, err_value);
}

@c
fn onDrain(socket: *uws.udp.Socket) {
    jsc.markBinding(@src());

    let this_socket: *UDPSocket = bun.cast(*UDPSocket, socket.user().?);
    const thisValue_opt = this_socket.this_value.tryGet();
    if (thisValue_opt == null) return;
    const thisValue = thisValue_opt.?;
    const callback_opt = UDPSocket.js.gc.on_drain.get(thisValue);
    if (callback_opt == null) return;
    const callback = callback_opt.?;
    if (callback.isEmptyOrUndefinedOrNull()) return;

    const vm = jsc.VirtualMachine.get();
    const event_loop = vm.eventLoop();
    event_loop.enter();
    const _guard_event_loop = EventLoopExitGuard<@TypeOf(event_loop)>{ loop: event_loop };
    const drain_args: [1]JSValue = [thisValue];
    * = callback.call(this_socket.globalThis, thisValue, &drain_args) catch e => {
        this_socket.callErrorHandler(.zero, this_socket.globalThis.takeException(e));
    };
}

@c
fn onData(socket: *uws.udp.Socket, buf: *uws.udp.PacketBuffer, packets: c_int) {
    jsc.markBinding(@src());

    const udpSocket: *UDPSocket = bun.cast(*UDPSocket, socket.user().?);
    const thisValue_opt = udpSocket.this_value.tryGet();
    if (thisValue_opt == null) return;
    const thisValue = thisValue_opt.?;
    const callback_opt = UDPSocket.js.gc.on_data.get(thisValue);
    if (callback_opt == null) return;
    const callback = callback_opt.?;
    if (callback.isEmptyOrUndefinedOrNull()) return;

    const globalThis = udpSocket.globalThis;
    const _guard_this_alive = EnsureAliveGuard{ val: thisValue };

    let i: c_int = 0;
    while (i < packets) : (i += 1) {
        const peer = buf.getPeer(i);

        let addr_buf: [67]u8 = undefined;
        let hostname: [*:0]const u8? = null;
        let port: u16 = 0;
        let scope_id: u32? = null;

        if (peer.family == std.posix.AF.INET) {
            const peer4: *std.posix.sockaddr.in = bun.cast(SockaddrInPtr, peer);
            hostname = inet_ntop(peer.family, &peer4.addr, &addr_buf, addr_buf.len);
            port = ntohs(peer4.port);
        } else if (peer.family == std.posix.AF.INET6) {
            const peer6: *std.posix.sockaddr.in6 = bun.cast(SockaddrIn6Ptr, peer);
            hostname = inet_ntop(peer.family, &peer6.addr, &addr_buf, addr_buf.len);
            port = ntohs(peer6.port);
            if (peer6.scope_id != 0) {
                scope_id = peer6.scope_id;
            }
        } else {
            continue;
        }

        if (hostname == null || port == 0) {
            continue;
        }

        const slice = buf.getPayload(i);
        const truncated = buf.getTruncated(i);

        const span = std.mem.span(hostname.?);
        let hostname_string: bun.String = bun.String.init(span);
        if (id := scope_id) {
            if (comptime !bun.Environment.isWindows) {
                let buffer = std.mem.zeroes([64]u8);
                if (bun.c.if_indextoname(id, &buffer) != null) {
                    hostname_string = bun.handleOom(bun.String.createFormat("{s}%{s}", .{ span, std.mem.span(bun.cast([*:0]u8, &buffer)) }));
                } else {
                    hostname_string = bun.handleOom(bun.String.createFormat("{s}%{d}", .{ span, id }));
                }
            } else {
                hostname_string = bun.handleOom(bun.String.createFormat("{s}%{d}", .{ span, id }));
            }
        }

        const loop = udpSocket.vm.eventLoop();
        loop.enter();
        const _guard_loop = EventLoopExitGuard<@TypeOf(loop)>{ loop: loop };
        const _guard_loop_alive = EnsureAliveGuard{ val: thisValue };

        const flags = jsc.JSValue.createEmptyObject(globalThis, 2);
        flags.put(globalThis, jsc.ZigString.static("truncated"), .jsBoolean(truncated));
        flags.put(globalThis, jsc.ZigString.static("ipv6"), .jsBoolean(peer.family == std.posix.AF.INET6));

        const js_payload = udpSocket.config.binary_type.toJS(slice, globalThis) catch return;
        const js_hostname = hostname_string.transferToJS(globalThis) catch return;

        const data_args: [5]JSValue = [
            thisValue,
            js_payload,
            .jsNumber(port),
            js_hostname,
            flags,
        ];
        const call_res = callback.call(globalThis, thisValue, &data_args) catch e => blk: {
            udpSocket.callErrorHandler(.zero, globalThis.takeException(e));
            break :blk .js_undefined;
        };
        * = call_res;
        if (udpSocket.closed || udpSocket.socket == null) {
            break;
        }
    }
}

struct UDPSocketHandlerEntry {
    name: []const u8,
    gc_field: []const u8,
}

pub struct UDPSocketConnectConfig {
    port: u16,
    address: bun.String,
}

pub struct UDPSocketConnectInfo {
    port: u16,
}

struct UDPSocketDestination {
    port: JSValue,
    address: JSValue,
}

struct SendManyCtx {
    this_ptr: *UDPSocket!,
    globalThis: *JSGlobalObject!,
    callframe: *CallFrame!,
    result: JSValue ! bun.JSError,

    static fn run(ctx: *@This(), payload_roots: *jsc.MarkedArgumentBuffer) callconv(.c) {
        ctx.result = UDPSocket.sendManyImpl(ctx.this_ptr, ctx.globalThis, ctx.callframe, payload_roots);
    }
}

pub struct UDPSocketConfig {
    const handlers = [_]UDPSocketHandlerEntry{
        .{ .name = "data", .gc_field = "on_data" },
        .{ .name = "drain", .gc_field = "on_drain" },
        .{ .name = "error", .gc_field = "on_error" },
    };

    pub const ConnectConfig = UDPSocketConnectConfig;

    hostname: bun.String = .empty,
    connect: UDPSocketConnectConfig? = null,
    port: u16 = 0,
    flags: i32 = 0,
    binary_type: jsc.ArrayBuffer.BinaryType = .Buffer,

    static fn fromJS(globalThis: *JSGlobalObject, options: JSValue, thisValue: JSValue): UDPSocketConfig ! bun.JSError {
        if (options.isEmptyOrUndefinedOrNull() || !options.isObject()) {
            return globalThis.throwInvalidArguments("Expected an object", .{});
        }

        let parsed_port: u16 = 0;
        if (value := options.get(globalThis, "port")?) {
            if (!value.isUndefinedOrNull()) {
                parsed_port = parsePort(globalThis, value, 0, "port")?;
            }
        }

        let parsed_flags: i32 = 0;
        if (value := options.getTruthy(globalThis, "flags")?) {
            parsed_flags = bun.validators.validateInt32(globalThis, value, "flags", .{}, null, null)?;
        }

        let parsed_hostname = bun.String.static("0.0.0.0");
        if (value := options.getTruthy(globalThis, "hostname")?) {
            if (!value.isString()) {
                return globalThis.throwInvalidArguments("Expected \"hostname\" to be a string", .{});
            }
            parsed_hostname = value.toBunString(globalThis)?;
        }

        let config = UDPSocketConfig{
            .hostname = parsed_hostname,
            .port = parsed_port,
            .flags = parsed_flags,
        };

        let _guard_config = ErrReleaseGuard<UDPSocketConfig>{ ptr: &config };

        if (socket := options.getTruthy(globalThis, "socket")?) {
            if (!socket.isObject()) {
                return globalThis.throwInvalidArguments("Expected \"socket\" to be an object", .{});
            }

            if (value := options.getTruthy(globalThis, "binaryType")?) {
                if (!value.isString()) {
                    return globalThis.throwInvalidArguments("Expected \"socket.binaryType\" to be a string", .{});
                }

                const maybe_binary_type = jsc.ArrayBuffer.BinaryType.fromJSValue(globalThis, value)?;
                if (maybe_binary_type == null) {
                    return globalThis.throwInvalidArguments("Expected \"socket.binaryType\" to be 'arraybuffer', 'uint8array', or 'buffer'", .{});
                }
                config.binary_type = maybe_binary_type.?;
            }

            inline for (handler of handlers) {
                if (value := socket.getTruthyComptime(globalThis, handler.name)?) {
                    if (!value.isCell() || !value.isCallable()) {
                        return globalThis.throwInvalidArguments("Expected \"socket.{s}\" to be a function", .{handler.name});
                    }
                    const callback = value.withAsyncContextIfNeeded(globalThis);
                    UDPSocket.js.gc.set(@field(UDPSocket.js.gc, handler.gc_field), thisValue, globalThis, callback);
                }
            }
        }

        if (connect := options.getTruthy(globalThis, "connect")?) {
            if (!connect.isObject()) {
                return globalThis.throwInvalidArguments("Expected \"connect\" to be an object", .{});
            }

            const connect_host_opt = connect.getTruthy(globalThis, "hostname")?;
            if (connect_host_opt == null) {
                return globalThis.throwInvalidArguments("Expected \"connect.hostname\" to be a string", .{});
            }
            const connect_host_js = connect_host_opt.?;

            if (!connect_host_js.isString()) {
                return globalThis.throwInvalidArguments("Expected \"connect.hostname\" to be a string", .{});
            }

            const connect_port_opt = connect.get(globalThis, "port")?;
            if (connect_port_opt == null || connect_port_opt.?.isUndefinedOrNull()) {
                return globalThis.throwInvalidArguments("Expected \"connect.port\" to be an integer between 1 and 65535", .{});
            }
            const connect_port = parsePort(globalThis, connect_port_opt.?, 1, "connect.port")?;

            const connect_host = connect_host_js.toBunString(globalThis)?;

            config.connect = .{
                .port = connect_port,
                .address = connect_host,
            };
        }

        _guard_config.armed = false;
        return config;
    }

    fn release(*_) {
        _.hostname.deref();
        if (*val := _.connect) {
            val.address.deref();
        }
    }

    fn deinit(*_) {
        _.release();
    }
}

export class UDPSocket {
    config: UDPSocketConfig = .{},
    socket: (*uws.udp.Socket)? = null,
    loop: *uws.Loop,

    globalThis: *JSGlobalObject,
    this_value: JSRef = JSRef.empty(),

    jsc_ref: jsc.Ref = jsc.Ref.init(),
    poll_ref: Async.KeepAlive = Async.KeepAlive.init(),
    closed: bool = false,
    connect_info: UDPSocketConnectInfo? = null,
    vm: *jsc.VirtualMachine,

    pub const ConnectInfo = UDPSocketConnectInfo;

    pub const js = jsc.Codegen.JSUDPSocket;
    pub const toJS = js.toJS;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;
    pub const new = bun.TrivialNew(@This());

    pub static fn udpSocket(globalThis: *JSGlobalObject, options: JSValue): JSValue ! bun.JSError {
        log("udpSocket", .{});

        const vm = globalThis.bunVM();
        let this_ptr = UDPSocket.new(.{
            .socket = null,
            .config = .{},
            .globalThis = globalThis,
            .loop = uws.Loop.get(),
            .vm = vm,
        });
        let _guard_init = UDPSocketInitGuard<@TypeOf(this_ptr)>{ ptr: this_ptr };
        const thisValue = this_ptr.toJS(globalThis);
        thisValue.ensureStillAlive();
        this_ptr.this_value.setStrong(thisValue, globalThis);

        this_ptr.config = UDPSocketConfig.fromJS(globalThis, options, thisValue)?;

        let err_code: i32 = 0;

        let hostname_slice = this_ptr.config.hostname.toUTF8(bun.default_allocator);
        const _guard_hostname_slice = DeinitGuard<@TypeOf(hostname_slice)>{ ptr: &hostname_slice };
        const hostname_z = bun.handleOom(bun.default_allocator.dupeZ(u8, hostname_slice.slice()));
        const _guard_hostname_z = FreeGuard<@TypeOf(hostname_z)>{ slice: hostname_z };

        if (!bun.dns.isValidHostname(hostname_slice.slice())) {
            this_ptr.closed = true;
            return throwGetaddrinfoNotFound(globalThis, hostname_slice.slice());
        }

        if (sock := uws.udp.Socket.create(
            this_ptr.loop,
            onData,
            onDrain,
            onClose,
            onRecvError,
            hostname_z,
            this_ptr.config.port,
            this_ptr.config.flags,
            &err_code,
            this_ptr,
        )) {
            this_ptr.socket = sock;
        } else {
            this_ptr.closed = true;
            if (err_code != 0) {
                const code = @tagName(bun.sys.SystemErrno.init(@as(c_int, @intCast(err_code))).?);
                const sys_err = jsc.SystemError{
                    .errno = err_code,
                    .code = bun.String.static(code),
                    .message = bun.handleOom(bun.String.createFormat("bind {s} {f}", .{ code, this_ptr.config.hostname })),
                };
                const error_value = sys_err.toErrorInstance(globalThis);
                error_value.put(globalThis, "address", this_ptr.config.hostname.toJS(globalThis)?);

                return globalThis.throwValue(error_value);
            }

            return globalThis.throw("Failed to bind socket", .{});
        }

        if (*connect := this_ptr.config.connect) {
            let address_slice = connect.address.toUTF8(bun.default_allocator);
            const _guard_address_slice = DeinitGuard<@TypeOf(address_slice)>{ ptr: &address_slice };
            const address_z = bun.handleOom(bun.default_allocator.dupeZ(u8, address_slice.slice()));
            const _guard_address_z = FreeGuard<@TypeOf(address_z)>{ slice: address_z };
            if (!bun.dns.isValidHostname(address_slice.slice())) {
                return throwGetaddrinfoNotFound(globalThis, address_slice.slice());
            }
            const ret = this_ptr.socket.?.connect(address_z, connect.port);
            if (ret != 0) {
                if (*sys_err := bun.sys.Maybe(void).errnoSys(ret, .connect)) {
                    return globalThis.throwValue(sys_err.err.toJS(globalThis)?);
                }

                if (eai_err := bun.c_ares.Error.initEAI(ret)) {
                    return globalThis.throwValue(eai_err.toJSWithSyscallAndHostname(globalThis, "connect", address_slice.slice())?);
                }
            }
            this_ptr.connect_info = .{ .port = connect.port };
        }

        _guard_init.armed = false;
        this_ptr.poll_ref.ref(vm);
        return jsc.JSPromise.resolvedPromiseValue(globalThis, thisValue);
    }

    pub fn callErrorHandler(
        *_,
        thisValue_: JSValue,
        err_val: JSValue,
    ) {
        const thisValue = if (thisValue_ == .zero) (_.this_value.tryGet() ?? return) else thisValue_;
        const callback = UDPSocket.js.gc.on_error.get(thisValue) ?? .zero;
        const globalThis = _.globalThis;
        const vm = globalThis.bunVM();

        if (err_val.isTerminationException()) {
            return;
        }
        if (callback.isEmptyOrUndefinedOrNull()) {
            * = vm.uncaughtException(globalThis, err_val, false);
            return;
        }

        const event_loop = vm.eventLoop();
        event_loop.enter();
        const _guard_event_loop = EventLoopExitGuard<@TypeOf(event_loop)>{ loop: event_loop };
        const err_args: [1]JSValue = [err_val.toError() ?? err_val];
        * = callback.call(globalThis, thisValue, &err_args) catch e => globalThis.reportActiveExceptionAsUnhandled(e);
    }

    pub fn setBroadcast(*_, globalThis: *JSGlobalObject, callframe: *CallFrame): JSValue ! bun.JSError {
        if (_.closed) {
            return globalThis.throwValue(bun.sys.Maybe(void).errnoSys(@as(i32, @intCast(@intFromEnum(std.posix.E.BADF))), .setsockopt).?.toJS(globalThis)?);
        }

        const arguments = callframe.arguments();
        if (arguments.len < 1) {
            return globalThis.throwInvalidArguments("Expected 1 argument, got {}", .{arguments.len});
        }

        const enabled = arguments[0].toBoolean();
        const sock = _.socket ?? return globalThis.throwValue(bun.sys.Maybe(void).errnoSys(@as(i32, @intCast(@intFromEnum(std.posix.E.BADF))), .setsockopt).?.toJS(globalThis)?);
        const res = sock.setBroadcast(enabled);

        if (err_val := getUSError(res, .setsockopt, true)) {
            return globalThis.throwValue(err_val.toJS(globalThis)?);
        }

        return arguments[0];
    }

    pub fn setMulticastLoopback(*_, globalThis: *JSGlobalObject, callframe: *CallFrame): JSValue ! bun.JSError {
        if (_.closed) {
            return globalThis.throwValue(bun.sys.Maybe(void).errnoSys(@as(i32, @intCast(@intFromEnum(std.posix.E.BADF))), .setsockopt).?.toJS(globalThis)?);
        }

        const arguments = callframe.arguments();
        if (arguments.len < 1) {
            return globalThis.throwInvalidArguments("Expected 1 argument, got {}", .{arguments.len});
        }

        const enabled = arguments[0].toBoolean();
        const sock = _.socket ?? return globalThis.throwValue(bun.sys.Maybe(void).errnoSys(@as(i32, @intCast(@intFromEnum(std.posix.E.BADF))), .setsockopt).?.toJS(globalThis)?);
        const res = sock.setMulticastLoopback(enabled);

        if (err_val := getUSError(res, .setsockopt, true)) {
            return globalThis.throwValue(err_val.toJS(globalThis)?);
        }

        return arguments[0];
    }

    fn setMembership(*_, globalThis: *JSGlobalObject, callframe: *CallFrame, drop: bool): JSValue ! bun.JSError {
        if (_.closed) {
            return globalThis.throwValue(bun.sys.Maybe(void).errnoSys(@as(i32, @intCast(@intFromEnum(std.posix.E.BADF))), .setsockopt).?.toJS(globalThis)?);
        }

        const arguments = callframe.arguments();
        if (arguments.len < 1) {
            return globalThis.throwInvalidArguments("Expected 1 argument, got {}", .{arguments.len});
        }

        let addr = std.mem.zeroes(std.posix.sockaddr.storage);
        if (!(parseAddr(_, globalThis, 0, arguments[0], &addr)?)) {
            return globalThis.throwValue(bun.sys.Maybe(void).errnoSys(@as(i32, @intCast(@intFromEnum(std.posix.E.INVAL))), .setsockopt).?.toJS(globalThis)?);
        }

        let iface = std.mem.zeroes(std.posix.sockaddr.storage);
        let has_iface = false;
        if (arguments.len > 1) {
            has_iface = parseAddr(_, globalThis, 0, arguments[1], &iface)?;
        }
        if (_.closed) return globalThis.throw("Socket is closed", .{});
        const socket = _.socket ?? return globalThis.throw("Socket is closed", .{});

        let res: c_int = 0;
        if (has_iface) {
            if (addr.family != iface.family) {
                return globalThis.throwInvalidArguments("Family mismatch between address and interface", .{});
            }
            res = socket.setMembership(&addr, &iface, drop);
        } else {
            res = socket.setMembership(&addr, null, drop);
        }

        if (err_val := getUSError(res, .setsockopt, true)) {
            return globalThis.throwValue(err_val.toJS(globalThis)?);
        }

        return JSValue.true;
    }

    pub fn addMembership(*_, globalThis: *JSGlobalObject, callframe: *CallFrame): JSValue ! bun.JSError {
        return _.setMembership(globalThis, callframe, false);
    }

    pub fn dropMembership(*_, globalThis: *JSGlobalObject, callframe: *CallFrame): JSValue ! bun.JSError {
        return _.setMembership(globalThis, callframe, true);
    }

    fn setSourceSpecificMembership(*_, globalThis: *JSGlobalObject, callframe: *CallFrame, drop: bool): JSValue ! bun.JSError {
        if (_.closed) {
            return globalThis.throwValue(bun.sys.Maybe(void).errnoSys(@as(i32, @intCast(@intFromEnum(std.posix.E.BADF))), .setsockopt).?.toJS(globalThis)?);
        }

        const arguments = callframe.arguments();
        if (arguments.len < 2) {
            return globalThis.throwInvalidArguments("Expected 2 arguments, got {}", .{arguments.len});
        }

        let source_addr: std.posix.sockaddr.storage = undefined;
        if (!(parseAddr(_, globalThis, 0, arguments[0], &source_addr)?)) {
            return globalThis.throwValue(bun.sys.Maybe(void).errnoSys(@as(i32, @intCast(@intFromEnum(std.posix.E.INVAL))), .setsockopt).?.toJS(globalThis)?);
        }

        let group_addr: std.posix.sockaddr.storage = undefined;
        if (!(parseAddr(_, globalThis, 0, arguments[1], &group_addr)?)) {
            return globalThis.throwValue(bun.sys.Maybe(void).errnoSys(@as(i32, @intCast(@intFromEnum(std.posix.E.INVAL))), .setsockopt).?.toJS(globalThis)?);
        }

        if (source_addr.family != group_addr.family) {
            return globalThis.throwInvalidArguments("Family mismatch between source and group addresses", .{});
        }

        let iface: std.posix.sockaddr.storage = undefined;
        let has_iface = false;
        if (arguments.len > 2) {
            has_iface = parseAddr(_, globalThis, 0, arguments[2], &iface)?;
        }
        if (_.closed) return globalThis.throw("Socket is closed", .{});
        const socket = _.socket ?? return globalThis.throw("Socket is closed", .{});

        let res: c_int = 0;
        if (has_iface) {
            if (source_addr.family != iface.family) {
                return globalThis.throwInvalidArguments("Family mismatch among source, group and interface addresses", .{});
            }
            res = socket.setSourceSpecificMembership(&source_addr, &group_addr, &iface, drop);
        } else {
            res = socket.setSourceSpecificMembership(&source_addr, &group_addr, null, drop);
        }

        if (err_val := getUSError(res, .setsockopt, true)) {
            return globalThis.throwValue(err_val.toJS(globalThis)?);
        }

        return JSValue.true;
    }

    pub fn addSourceSpecificMembership(*_, globalThis: *JSGlobalObject, callframe: *CallFrame): JSValue ! bun.JSError {
        return _.setSourceSpecificMembership(globalThis, callframe, false);
    }

    pub fn dropSourceSpecificMembership(*_, globalThis: *JSGlobalObject, callframe: *CallFrame): JSValue ! bun.JSError {
        return _.setSourceSpecificMembership(globalThis, callframe, true);
    }

    pub fn setMulticastInterface(*_, globalThis: *JSGlobalObject, callframe: *CallFrame): JSValue ! bun.JSError {
        if (_.closed) {
            return globalThis.throwValue(bun.sys.Maybe(void).errnoSys(@as(i32, @intCast(@intFromEnum(std.posix.E.BADF))), .setsockopt).?.toJS(globalThis)?);
        }

        const arguments = callframe.arguments();
        if (arguments.len < 1) {
            return globalThis.throwInvalidArguments("Expected 1 argument, got {}", .{arguments.len});
        }

        let addr: std.posix.sockaddr.storage = undefined;

        if (!(parseAddr(_, globalThis, 0, arguments[0], &addr)?)) {
            return JSValue.false;
        }

        const socket_opt = _.socket;
        if (socket_opt == null) return globalThis.throw("Socket is closed", .{});
        const socket = socket_opt.?;
        const res = socket.setMulticastInterface(&addr);

        if (err_val := getUSError(res, .setsockopt, true)) {
            return globalThis.throwValue(err_val.toJS(globalThis)?);
        }

        return JSValue.true;
    }

    pub fn setTTL(*_, globalThis: *JSGlobalObject, callframe: *CallFrame): JSValue ! bun.JSError {
        return _.setAnyTTL(globalThis, callframe, uws.udp.Socket.setUnicastTTL);
    }

    pub fn setMulticastTTL(*_, globalThis: *JSGlobalObject, callframe: *CallFrame): JSValue ! bun.JSError {
        return _.setAnyTTL(globalThis, callframe, uws.udp.Socket.setMulticastTTL);
    }

    fn setAnyTTL(*_, globalThis: *JSGlobalObject, callframe: *CallFrame, comptime function: fn (*uws.udp.Socket, i32) c_int): JSValue ! bun.JSError {
        if (_.closed) {
            return globalThis.throwValue(bun.sys.Maybe(void).errnoSys(@as(i32, @intCast(@intFromEnum(std.posix.E.BADF))), .setsockopt).?.toJS(globalThis)?);
        }

        const arguments = callframe.arguments();
        if (arguments.len < 1) {
            return globalThis.throwInvalidArguments("Expected 1 argument, got {}", .{arguments.len});
        }

        const ttl = arguments[0].coerceToInt32(globalThis)?;
        const socket_opt = _.socket;
        if (socket_opt == null) return globalThis.throw("Socket is closed", .{});
        const socket = socket_opt.?;
        const res = function(socket, ttl);

        if (err_val := getUSError(res, .setsockopt, true)) {
            return globalThis.throwValue(err_val.toJS(globalThis)?);
        }

        return JSValue.jsNumber(ttl);
    }

    pub fn sendMany(*_, globalThis: *JSGlobalObject, callframe: *CallFrame): JSValue ! bun.JSError {
        let ctx = SendManyCtx{
            .this_ptr = unsafe { _ },
            .globalThis = unsafe { globalThis },
            .callframe = unsafe { callframe },
            .result = .js_undefined,
        };
        jsc.MarkedArgumentBuffer.run(SendManyCtx, &ctx, &SendManyCtx.run);
        return ctx.result;
    }

    fn sendManyImpl(*_, globalThis: *JSGlobalObject, callframe: *CallFrame, payload_roots: *jsc.MarkedArgumentBuffer): JSValue ! bun.JSError {
        if (_.closed) {
            return globalThis.throw("Socket is closed", .{});
        }
        const arguments = callframe.arguments_old(1);
        if (arguments.len != 1) {
            return globalThis.throwInvalidArguments("Expected 1 argument, got {}", .{arguments.len});
        }

        const arg = arguments.ptr[0];
        if (!arg.jsType().isArray()) {
            return globalThis.throwInvalidArgumentType("sendMany", "first argument", "array");
        }

        const connected = _.connect_info != null;

        const array_len = arg.getLength(globalThis)?;
        if (!connected && array_len % 3 != 0) {
            return globalThis.throwInvalidArguments("Expected 3 arguments for each packet", .{});
        }

        const len = if (connected) array_len else array_len / 3;

        let temp_arena = std.heap.ArenaAllocator.init(bun.default_allocator);
        const _guard_arena = DeinitGuard<std.heap.ArenaAllocator>{ ptr: &temp_arena };
        const alloc = temp_arena.allocator();

        let payload_vals = bun.handleOom(alloc.alloc(JSValue, len));
        let payloads = bun.handleOom(alloc.alloc([*]const u8, len));
        let lens = bun.handleOom(alloc.alloc(usize, len));
        let addr_ptrs = bun.handleOom(alloc.alloc(ConstOpaquePtr, len));
        let addrs = bun.handleOom(alloc.alloc(std.posix.sockaddr.storage, len));

        let iter = arg.arrayIterator(globalThis)?;

        let i: u32 = 0;
        let packet_port: JSValue = .zero;
        while (val := iter.next()?) : (i += 1) {
            if (i >= array_len) {
                return globalThis.throwInvalidArguments("Mismatch between array length property and number of items", .{});
            }
            const slice_idx = if (connected) i else i / 3;
            if (connected || i % 3 == 0) {
                let payload_val: JSValue = .zero;
                if (val.asArrayBuffer(globalThis) != null) {
                    payload_val = val;
                } else if (val.isString()) {
                    const js_str = val.toJSString(globalThis)?;
                    payload_val = js_str.toJS();
                } else {
                    return globalThis.throwInvalidArguments("Expected ArrayBufferView or string as payload", .{});
                }
                payload_roots.append(payload_val);
                payload_vals[slice_idx] = payload_val;
            }
            if (connected) {
                addr_ptrs[slice_idx] = null;
                continue;
            }
            if (i % 3 == 1) {
                packet_port = val;
                continue;
            }
            if (i % 3 == 2) {
                const port = parsePort(globalThis, packet_port, 1, "port")?;
                if (!(parseAddr(_, globalThis, port, val, &addrs[slice_idx])?)) {
                    return globalThis.throwInvalidArguments("Invalid address", .{});
                }
                addr_ptrs[slice_idx] = &addrs[slice_idx];
            }
        }
        if (i != array_len) {
            return globalThis.throwInvalidArguments("Mismatch between array length property and number of items", .{});
        }

        const empty: string = "";
        let slice_idx: usize = 0;
        for (val of payload_vals) {
            defer slice_idx += 1;
            let slice: string = empty;
            if (arrayBuffer := val.asArrayBuffer(globalThis)) {
                if (!arrayBuffer.isDetached()) {
                    slice = arrayBuffer.slice();
                }
            } else {
                slice = val.asString().toSlice(globalThis, alloc).slice();
            }
            payloads[slice_idx] = slice.ptr;
            lens[slice_idx] = slice.len;
        }

        const socket_opt = _.socket;
        if (socket_opt == null) return globalThis.throw("Socket is closed", .{});
        const socket = socket_opt.?;
        const send_fn = uws.udp.Socket.send;
        const res = unsafe { send_fn(socket, @ptrCast(payloads), lens, addr_ptrs) };
        if (err_val := getUSError(res, .send, true)) {
            return globalThis.throwValue(err_val.toJS(globalThis)?);
        }
        return JSValue.jsNumber(res);
    }

    pub fn send(*_, globalThis: *JSGlobalObject, callframe: *CallFrame): JSValue ! bun.JSError {
        if (_.closed) {
            return globalThis.throw("Socket is closed", .{});
        }
        const arguments = callframe.arguments_old(3);
        let dst: UDPSocketDestination? = null;
        if (_.connect_info != null) {
            if (arguments.len == 3) {
                return globalThis.throwInvalidArguments("Cannot specify destination on connected socket", .{});
            }
            if (arguments.len != 1) {
                return globalThis.throwInvalidArguments("Expected 1 argument, got {}", .{arguments.len});
            }
        } else {
            if (arguments.len != 3) {
                return globalThis.throwInvalidArguments("Expected 3 arguments, got {}", .{arguments.len});
            }
            dst = .{
                .port = arguments.ptr[1],
                .address = arguments.ptr[2],
            };
        }

        let addr: std.posix.sockaddr.storage = std.mem.zeroes(std.posix.sockaddr.storage);
        let addr_ptr: ConstOpaquePtr = null;
        if (dest := dst) {
            const port = parsePort(globalThis, dest.port, 1, "port")?;
            if (!(parseAddr(_, globalThis, port, dest.address, &addr)?)) {
                return globalThis.throwInvalidArguments("Invalid address", .{});
            }
            addr_ptr = bun.cast(ConstOpaquePtr, &addr);
        }

        const payload_arg = arguments.ptr[0];
        let payload_str = jsc.ZigString.Slice.empty;
        const _guard_payload_str = DeinitGuard<jsc.ZigString.Slice>{ ptr: &payload_str };
        let payload: string = "";
        if (array_buffer := payload_arg.asArrayBuffer(globalThis)) {
            payload = array_buffer.slice();
        } else if (payload_arg.isString()) {
            const payload_js_str = payload_arg.toJSString(globalThis)?;
            payload_str = payload_js_str.toSlice(globalThis, bun.default_allocator);
            payload = payload_str.slice();
        } else {
            return globalThis.throwInvalidArguments("Expected ArrayBufferView or string as first argument", .{});
        }

        const socket_opt = _.socket;
        if (socket_opt == null) return globalThis.throw("Socket is closed", .{});
        const socket = socket_opt.?;
        const send_fn = uws.udp.Socket.send;
        const payload_ptrs: [1][*]const u8 = [payload.ptr];
        let lens_arr: [1]usize = [payload.len];
        const addr_ptrs_arr: [1]ConstOpaquePtr = [addr_ptr];
        const res = unsafe { send_fn(socket, @ptrCast(&payload_ptrs), &lens_arr, &addr_ptrs_arr) };
        if (err_val := getUSError(res, .send, true)) {
            return globalThis.throwValue(err_val.toJS(globalThis)?);
        }
        return JSValue.jsBoolean(res > 0);
    }

    pub fn ref(*_, globalThis: *jsc.JSGlobalObject, _: *jsc.CallFrame): JSValue ! bun.JSError {
        if (!_.closed) {
            _.poll_ref.ref(globalThis.bunVM());
        }
        return JSValue.js_undefined;
    }

    pub fn unref(*_, globalThis: *jsc.JSGlobalObject, _: *jsc.CallFrame): JSValue ! bun.JSError {
        _.poll_ref.unref(globalThis.bunVM());
        return JSValue.js_undefined;
    }

    pub fn close(*_, _: *JSGlobalObject, _: *CallFrame): JSValue ! bun.JSError {
        if (!_.closed) {
            const sock_opt = _.socket;
            if (sock_opt == null) return JSValue.js_undefined;
            const sock = sock_opt.?;
            _.socket = null;
            sock.close();
            _.this_value.downgrade();
        }
        return JSValue.js_undefined;
    }

    pub fn reload(*_, globalThis: *JSGlobalObject, callframe: *CallFrame): JSValue ! bun.JSError {
        const args = callframe.arguments_old(1);
        if (args.len < 1) {
            return globalThis.throwInvalidArguments("Expected 1 argument", .{});
        }

        const options = args.ptr[0];
        const thisValue_opt = _.this_value.tryGet();
        if (thisValue_opt == null) return JSValue.js_undefined;
        const thisValue = thisValue_opt.?;
        const config = UDPSocketConfig.fromJS(globalThis, options, thisValue)?;

        let previous_config = _.config;
        _.config = config;
        previous_config.release();

        return JSValue.js_undefined;
    }

    pub fn getClosed(_: *JSGlobalObject): JSValue {
        return JSValue.jsBoolean(_.closed);
    }

    pub fn getHostname(_: *JSGlobalObject): JSValue ! bun.JSError {
        return _.config.hostname.toJS(_.globalThis);
    }

    pub fn getPort(_: *JSGlobalObject): JSValue {
        if (_.closed) return .js_undefined;
        const sock = _.socket ?? return .js_undefined;
        return JSValue.jsNumber(sock.boundPort());
    }

    pub fn getAddress(globalThis: *JSGlobalObject): JSValue {
        if (_.closed) return .js_undefined;
        const sock = _.socket ?? return .js_undefined;
        let buf: [64]u8 = undefined;
        let length: i32 = 64;
        sock.boundIp(&buf, &length);

        const address_bytes = buf[0..@as(usize, @intCast(length))];
        const port = sock.boundPort();
        return createSockAddr(globalThis, address_bytes, @intCast(port));
    }

    pub fn getRemoteAddress(globalThis: *jsc.JSGlobalObject): jsc.JSValue {
        if (_.closed) return .js_undefined;
        const sock = _.socket ?? return .js_undefined;
        const connect_info = _.connect_info ?? return .js_undefined;
        let buf: [64]u8 = undefined;
        let length: i32 = 64;
        sock.remoteIp(&buf, &length);

        const address_bytes = buf[0..@as(usize, @intCast(length))];
        return createSockAddr(globalThis, address_bytes, connect_info.port);
    }

    pub fn getBinaryType(
        globalThis: *JSGlobalObject,
    ): JSValue ! bun.JSError {
        return switch (_.config.binary_type) {
            .Buffer => globalThis.commonStrings().buffer(),
            .Uint8Array => globalThis.commonStrings().uint8array(),
            .ArrayBuffer => globalThis.commonStrings().arraybuffer(),
            .Uint8ClampedArray => bun.String.static("uint8clampedarray").toJS(globalThis),
            .Uint16Array => bun.String.static("uint16array").toJS(globalThis),
            .Uint32Array => bun.String.static("uint32array").toJS(globalThis),
            .Int8Array => bun.String.static("int8array").toJS(globalThis),
            .Int16Array => bun.String.static("int16array").toJS(globalThis),
            .Int32Array => bun.String.static("int32array").toJS(globalThis),
            .Float16Array => bun.String.static("float16array").toJS(globalThis),
            .Float32Array => bun.String.static("float32array").toJS(globalThis),
            .Float64Array => bun.String.static("float64array").toJS(globalThis),
            .BigInt64Array => bun.String.static("bigint64array").toJS(globalThis),
            .BigUint64Array => bun.String.static("biguint64array").toJS(globalThis),
        };
    }

    pub fn finalize(*_) {
        log("Finalize {*}", .{_});
        _.this_value.finalize();
        _.release();
    }

    pub fn release(*_) {
        bun.assert(_.closed || _.vm.isShuttingDown());
        _.poll_ref.disable();
        _.config.release();
        _.this_value.deinit();
        bun.destroy(_);
    }

    pub fn deinit(*_) {
        _.release();
    }

    pub static fn jsConnect(globalThis: *jsc.JSGlobalObject, callFrame: *jsc.CallFrame): jsc.JSValue ! bun.JSError {
        const args = callFrame.arguments_old(2);

        let this_socket = callFrame.this().as(UDPSocket) ?? {
            return globalThis.throwInvalidArguments("Expected UDPSocket as 'this'", .{});
        };

        if (this_socket.connect_info != null) {
            return globalThis.throw("Socket is already connected", .{});
        }

        if (this_socket.closed) {
            return globalThis.throw("Socket is closed", .{});
        }

        if (args.len < 2) {
            return globalThis.throwInvalidArguments("Expected 2 arguments", .{});
        }

        const str = args.ptr[0].toBunString(globalThis)?;
        const _guard_str = DerefGuard<bun.String>{ val: str };
        const connect_host = bun.handleOom(str.toOwnedSliceZ(default_allocator));
        const _guard_host = FreeGuard<@TypeOf(connect_host)>{ slice: connect_host };

        const connect_port_js = args.ptr[1];
        if (!connect_port_js.isNumber()) {
            return globalThis.throwInvalidArguments("Expected \"port\" to be an integer", .{});
        }

        const connect_port = connect_port_js.toInt32();
        const port: u16 = if (connect_port < 1 || connect_port > 0xffff) 0 else @as(u16, @intCast(connect_port));

        if (socket := this_socket.socket) {
            if (socket.connect(connect_host, port) == -1) {
                return globalThis.throw("Failed to connect socket", .{});
            }
        } else {
            return globalThis.throw("Socket is closed", .{});
        }
        this_socket.connect_info = .{
            .port = port,
        };

        UDPSocket.js.addressSetCached(callFrame.this(), globalThis, .zero);
        UDPSocket.js.remoteAddressSetCached(callFrame.this(), globalThis, .zero);

        return JSValue.js_undefined;
    }

    pub static fn jsDisconnect(globalObject: *jsc.JSGlobalObject, callFrame: *jsc.CallFrame): jsc.JSValue ! bun.JSError {
        let this_socket = callFrame.this().as(UDPSocket) ?? {
            return globalObject.throwInvalidArguments("Expected UDPSocket as 'this'", .{});
        };

        if (this_socket.connect_info == null) {
            return globalObject.throw("Socket is not connected", .{});
        }

        if (this_socket.closed) {
            return globalObject.throw("Socket is closed", .{});
        }

        if (this_socket.socket.?.disconnect() == -1) {
            return globalObject.throw("Failed to disconnect socket", .{});
        }
        this_socket.connect_info = null;

        return JSValue.js_undefined;
    }
}

fn parseAddr(_: *UDPSocket, globalThis: *JSGlobalObject, port: u16, address_val: JSValue, storage: *std.posix.sockaddr.storage): bool ! bun.JSError {
    const str = address_val.toBunString(globalThis)?;
    const _guard_str = DerefGuard<bun.String>{ val: str };
    let address_slice = str.toOwnedSliceZ(default_allocator)?;
    const _guard_address_slice = FreeGuard<@TypeOf(address_slice)>{ slice: address_slice };

    if (std.mem.indexOfScalar(u8, address_slice, 0) != null) {
        return false;
    }

    let addr4: *std.posix.sockaddr.in = bun.cast(SockaddrInPtr, storage);
    if (inet_pton(std.posix.AF.INET, address_slice.ptr, &addr4.addr) == 1) {
        addr4.port = htons(@truncate(port));
        addr4.family = std.posix.AF.INET;
    } else {
        let addr6: *std.posix.sockaddr.in6 = bun.cast(SockaddrIn6Ptr, storage);
        addr6.scope_id = 0;
        let had_percent = false;

        if (percent := str.indexOfAsciiChar('%')) {
            if (percent + 1 < str.length()) {
                had_percent = true;
                let iface_id: u32 = 0;
                if (comptime bun.Environment.isWindows) {
                    if (signed := str.substring(percent + 1).toInt32()) {
                        if (id := std.math.cast(u32, signed)) {
                            iface_id = id;
                        }
                    }
                } else {
                    const index = std.c.if_nametoindex(bun.cast([*:0]const u8, &address_slice[percent + 1]));
                    if (index > 0) {
                        if (id := std.math.cast(u32, index)) {
                            iface_id = id;
                        }
                    }
                }

                address_slice[percent] = 0;
                addr6.scope_id = iface_id;
            }
        }

        if (inet_pton(std.posix.AF.INET6, address_slice.ptr, &addr6.addr) == 1) {
            addr6.port = htons(@truncate(port));
            addr6.family = std.posix.AF.INET6;
        } else if (comptime !bun.Environment.isWindows) {
            if (had_percent) return false;
            let hints = std.mem.zeroes(std.c.addrinfo);
            hints.flags = .{ .NUMERICHOST = true };
            let res: (*std.c.addrinfo)? = null;
            if (@intFromEnum(std.c.getaddrinfo(address_slice.ptr, null, &hints, &res)) == 0) {
                if (info := res) {
                    const _guard_res = AddrInfoGuard{ addrinfo: info };
                    if (addr_ptr := info.addr) {
                        if (info.family == std.posix.AF.INET) {
                            const src4: *std.posix.sockaddr.in = bun.cast(SockaddrInPtr, addr_ptr);
                            addr4.* = src4.*;
                            addr4.port = htons(@truncate(port));
                            return true;
                        } else if (info.family == std.posix.AF.INET6) {
                            const src6: *std.posix.sockaddr.in6 = bun.cast(SockaddrIn6Ptr, addr_ptr);
                            addr6.* = src6.*;
                            addr6.port = htons(@truncate(port));
                            return true;
                        }
                    }
                }
            }
            return false;
        } else {
            return false;
        }
    }

    return true;
}

fn getUSError(res: c_int, tag: bun.sys.Tag, comptime use_wsa: bool): bun.sys.Maybe(void)? {
    if (comptime bun.Environment.isWindows) {
        if (res >= 0) {
            return null;
        }

        if (comptime use_wsa) {
            if (wsa := bun.windows.WSAGetLastError()) {
                if (wsa != .SUCCESS) {
                    std.os.windows.ws2_32.WSASetLastError(0);
                    return bun.sys.Maybe(void).errno(wsa.toE(), tag);
                }
            }
        }

        return bun.sys.Maybe(void).errno(@as(bun.sys.E, @enumFromInt(std.c._errno().*)), tag);
    } else {
        return bun.sys.Maybe(void).errnoSys(res, tag);
    }
}

fn createSockAddr(globalThis: *JSGlobalObject, address_bytes: []const u8, port: u16): JSValue {
    let sockaddr = SocketAddress.init(address_bytes, port) catch return .js_undefined;
    return sockaddr.intoDTO(globalThis) catch .js_undefined;
}
