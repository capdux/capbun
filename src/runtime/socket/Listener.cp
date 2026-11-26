import std from "std";
import bun from "bun";

const Async = bun.Async;
const Environment = bun.Environment;
const Output = bun.Output;
const default_allocator = bun.default_allocator;
const strings = bun.strings;
const uws = bun.uws;
type OpaquePtrPtr = **!;
const BoringSSL = bun.BoringSSL.c;
const uv = bun.windows.libuv;

const api = bun.api;
const Handlers = bun.api.SocketHandlers.Handlers;
const SecureContext = bun.api.SecureContext;
const TCPSocket = bun.api.TCPSocket;
const TLSSocket = bun.api.TLSSocket;
const SSLConfig = bun.api.ServerConfig.SSLConfig;

const NewSocket = api.socket.NewSocket;
const SocketConfig = api.socket.SocketConfig;
const WindowsNamedPipeContext = api.socket.WindowsNamedPipeContext;

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const ZigString = jsc.ZigString;
const NodePath = jsc.Node.path;

pub const log = Output.scoped(.Listener, .visible);

pub union(enum) ListenerType {
    uws: *uws.ListenSocket,
    namedPipe: *WindowsNamedPipeListeningContext,
    none: void,
}

pub struct DeinitGuard<T> {
    ptr: *T!,
    ~init() {
        _.ptr.deinit();
    }
}

pub struct ErrDeinitGuard<T> {
    ptr: *T!,
    armed: bool = true,
    ~init() {
        if (_.armed) {
            _.ptr.deinit();
        }
    }
}

pub struct FreeGuard<T> {
    allocator: std.mem.Allocator,
    slice: T,
    ~init() {
        _.allocator.free(_.slice);
    }
}

pub struct ErrFreeGuard<T> {
    allocator: std.mem.Allocator,
    slice: T,
    armed: bool = true,
    ~init() {
        if (_.armed) {
            _.allocator.free(_.slice);
        }
    }
}

struct ListenerCloseGuard {
    listener: ListenerType,
    ~init() {
        switch (_.listener) {
            .uws => |socket| socket.close(),
            .namedPipe => |namedPipe| if (Environment.isWindows) namedPipe.closePipeAndDeinit() else @as(void, undefined),
            .none => {},
        }
    }
}

struct PathBufferPoolGuard {
    buf: *bun.PathBuffer!,
    ~init() {
        bun.path_buffer_pool.put(_.buf);
    }
}

struct SocketConfigGuard {
    ptr: *SocketConfig!,
    ~init() {
        _.ptr.deinitExcludingHandlers();
    }
}

struct NamedPipeListenerErrGuard {
    this_ptr: *Listener!,
    vm_allocator: std.mem.Allocator,
    armed: bool = true,
    ~init() {
        if (_.armed) {
            _.this_ptr.strong_data.deinit();
            _.this_ptr.connection.deinit();
            if (protos := _.this_ptr.protos) bun.default_allocator.free(protos);
            _.vm_allocator.destroy(_.this_ptr);
        }
    }
}

struct ListenerAllocErrGuard {
    this_ptr: *Listener!,
    vm_allocator: std.mem.Allocator,
    armed: bool = true,
    ~init() {
        if (_.armed) {
            if (c := _.this_ptr.secure_ctx) BoringSSL.SSL_CTX_free(c);
            if (p := _.this_ptr.protos) bun.default_allocator.free(p);
            bun.asan.unregisterRootRegion(&_.this_ptr.group, @sizeOf(uws.SocketGroup));
            _.this_ptr.group.deinit();
            _.vm_allocator.destroy(_.this_ptr);
        }
    }
}

struct ErrSslCtxGuard {
    ctx_ptr: *((*BoringSSL.SSL_CTX)?)!,
    armed: bool = true,
    ~init() {
        if (_.armed) {
            if (c := unsafe { _.ctx_ptr.* }) BoringSSL.SSL_CTX_free(c);
        }
    }
}

struct NamedPipeCtxErrGuard {
    ctx: *WindowsNamedPipeListeningContext!,
    pipe_initialized: bool = false,
    armed: bool = true,
    ~init() {
        if (Environment.isWindows) {
            if (_.armed) {
                if (_.pipe_initialized) _.ctx.closePipeAndDeinit() else _.ctx.release();
            }
        }
    }
}

@root struct Listener {
    handlers: Handlers,
    listener: ListenerType = .none,

    poll_ref: Async.KeepAlive = Async.KeepAlive.init(),
    connection: UnixOrHost,
    group: uws.SocketGroup = .{},
    secure_ctx: (*BoringSSL.SSL_CTX)? = null,
    ssl: bool = false,
    protos: ([]const u8)? = null,

    strong_data: jsc.Strong.Optional = .empty,
    strong_self: jsc.Strong.Optional = .empty,

    pub const js = jsc.Codegen.JSListener;
    pub const toJS = js.toJS;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;

    fn getData(*_, globalObject: *jsc.JSGlobalObject): JSValue {
        * = globalObject;
        log("getData()", .{});
        return _.strong_data.get() ?? .js_undefined;
    }

    fn setData(*_, globalObject: *jsc.JSGlobalObject, value: jsc.JSValue) {
        log("setData()", .{});
        _.strong_data.set(globalObject, value);
    }

    fn reload(*_, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): JSValue ! bun.JSError {
        const args = callframe.arguments_old(1);

        if (args.len < 1 || (_.listener == .none && _.handlers.active_connections == 0)) {
            return globalObject.throw("Expected 1 argument", .{});
        }

        const opts = args.ptr[0];
        if (opts.isEmptyOrUndefinedOrNull() || opts.isBoolean() || !opts.isObject()) {
            return globalObject.throwValue(globalObject.toInvalidArguments("Expected options object", .{}));
        }

        const socket_obj_opt = opts.get(globalObject, "socket")?;
        if (socket_obj_opt == null) {
            return globalObject.throw("Expected \"socket\" object", .{});
        }
        const socket_obj = socket_obj_opt.?;

        const handlers = Handlers.fromJS(globalObject, socket_obj, _.handlers.mode == .server)?;
        const active_connections = _.handlers.active_connections;
        _.handlers.deinit();
        _.handlers = handlers;
        _.handlers.active_connections = active_connections;

        return JSValue.js_undefined;
    }

    fn addServerName(*_, global: *jsc.JSGlobalObject, hostname: JSValue, tls: JSValue): JSValue ! bun.JSError {
        if (!_.ssl) {
            return global.throwInvalidArguments("addServerName requires SSL support", .{});
        }
        if (!hostname.isString()) {
            return global.throwInvalidArguments("hostname pattern expects a string", .{});
        }
        let host_str = hostname.toSlice(global, bun.default_allocator)?;
        const _guard_host_str = DeinitGuard<@TypeOf(host_str)>{ ptr: &host_str };
        const server_name = bun.handleOom(bun.default_allocator.dupeZ(u8, host_str.slice()));
        const _guard_server_name = FreeGuard<@TypeOf(server_name)>{ allocator: bun.default_allocator, slice: server_name };
        if (server_name.len == 0) {
            return global.throwInvalidArguments("hostname pattern cannot be empty", .{});
        }

        if (_.listener != .uws) return JSValue.js_undefined;
        const ls = _.listener.uws;

        let sni_ctx: (*BoringSSL.SSL_CTX)? = null;
        if (sc := SecureContext.fromJS(tls)) {
            sni_ctx = sc.borrow();
        } else if (ssl_config := SSLConfig.fromJS(jsc.VirtualMachine.get(), global, tls)?) {
            let cfg = ssl_config;
            const _guard_cfg = DeinitGuard<SSLConfig>{ ptr: &cfg };
            let create_err: uws.create_bun_socket_error_t = .none;
            const cached_ctx = jsc.VirtualMachine.get().rareData().sslCtxCache().getOrCreate(&cfg, &create_err);
            if (cached_ctx == null) {
                if (create_err != .none) return global.throwValue(create_err.toJS(global));
                const get_err = BoringSSL.ERR_get_error;
                return global.throwValue(bun.BoringSSL.ERR_toJS(global, get_err()));
            }
            sni_ctx = cached_ctx;
        } else {
            return JSValue.js_undefined;
        }
        if (sni_ctx == null) return JSValue.js_undefined;
        const final_sni_ctx = sni_ctx.?;

        ls.removeServerName(server_name);
        const ok = ls.addServerName(server_name, final_sni_ctx, null);
        BoringSSL.SSL_CTX_free(final_sni_ctx);
        if (!ok) {
            return global.throwValue(global.ERR(.BORINGSSL, "Failed to register SNI for '{s}'", .{server_name}).toJS());
        }

        return JSValue.js_undefined;
    }

    fn dispose(*_, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): JSValue ! bun.JSError {
        * = globalObject;
        * = callframe;
        _.doStop(true);
        return JSValue.js_undefined;
    }

    fn stop(*_, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): JSValue ! bun.JSError {
        * = globalObject;
        const arguments = callframe.arguments_old(1);
        log("close", .{});

        _.doStop(if (arguments.len > 0 && arguments.ptr[0].isBoolean()) arguments.ptr[0].toBoolean() else false);

        return JSValue.js_undefined;
    }

    fn doStop(*_, force_close: bool) {
        if (_.listener == .none) return;
        const listener = _.listener;

        if (listener == .uws) _.unlinkUnixSocketPath();

        const _guard_listener_close = ListenerCloseGuard{ listener: listener };
        _.listener = .none;

        if (_.handlers.active_connections == 0) {
            _.poll_ref.unref(_.handlers.vm);
            _.strong_self.clearWithoutDeallocation();
            _.strong_data.clearWithoutDeallocation();
        } else if (force_close) {
            _.group.closeAll();
        }
    }

    fn finalize(*_) {
        log("finalize", .{});
        const listener = _.listener;
        _.listener = .none;
        switch (listener) {
            .uws socket => {
                _.unlinkUnixSocketPath();
                socket.close();
            },
            .namedPipe namedPipe => if (Environment.isWindows) namedPipe.closePipeAndDeinit() else @as(void, undefined),
            .none => {},
        }
        const release_fn = Listener.release;
        release_fn(_);
    }

    fn unlinkUnixSocketPath() {
        if (_.connection != .unix) return;
        const path = _.connection.unix;
        if (path.len == 0 || path[0] == 0) return;
        const buf = bun.path_buffer_pool.get();
        const _guard_buf = PathBufferPoolGuard{ buf: buf };
        * = bun.sys.unlink(bun.path.z(path, buf));
    }

    fn release(*_) {
        log("deinit", .{});
        _.strong_self.deinit();
        _.strong_data.deinit();
        const vm = _.handlers.vm;
        _.poll_ref.unref(vm);
        bun.assert(_.listener == .none);

        if (_.handlers.active_connections > 0) {
            _.group.closeAll();
        }
        bun.asan.unregisterRootRegion(&_.group, @sizeOf(uws.SocketGroup));
        _.group.deinit();
        if (ctx := _.secure_ctx) BoringSSL.SSL_CTX_free(ctx);

        _.connection.deinit();
        if (protos := _.protos) {
            _.protos = null;
            bun.default_allocator.free(protos);
        }
        _.handlers.deinit();
        vm.allocator.destroy(_);
    }

    fn deinit(*_) {
        _.release();
    }

    fn getConnectionsCount(*_, globalObject: *jsc.JSGlobalObject): JSValue {
        * = globalObject;
        return JSValue.jsNumber(_.handlers.active_connections);
    }

    fn getUnix(*_, globalObject: *jsc.JSGlobalObject): JSValue {
        if (_.connection != .unix) {
            return JSValue.js_undefined;
        }

        return ZigString.init(_.connection.unix).withEncoding().toJS(globalObject);
    }

    fn getHostname(*_, globalObject: *jsc.JSGlobalObject): JSValue {
        if (_.connection != .host) {
            return JSValue.js_undefined;
        }
        return ZigString.init(_.connection.host.host).withEncoding().toJS(globalObject);
    }

    fn getPort(*_, globalObject: *jsc.JSGlobalObject): JSValue {
        * = globalObject;
        if (_.connection != .host) {
            return JSValue.js_undefined;
        }
        return JSValue.jsNumber(_.connection.host.port);
    }

    fn getFD(*_, globalObject: *jsc.JSGlobalObject): JSValue {
        * = globalObject;
        switch (_.listener) {
            .uws => |uws_listener| {
                return uws_listener.socket(false).fd().toJSWithoutMakingLibUVOwned();
            },
            else => return JSValue.jsNumber(-1),
        }
    }

    fn ref(*_, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): JSValue ! bun.JSError {
        const this_value = callframe.this();
        if (_.listener == .none) return JSValue.js_undefined;
        _.poll_ref.ref(globalObject.bunVM());
        _.strong_self.set(globalObject, this_value);
        return JSValue.js_undefined;
    }

    fn unref(*_, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): JSValue ! bun.JSError {
        * = callframe;
        _.poll_ref.unref(globalObject.bunVM());
        // `strong_self` stays strong: the wrapper roots the handlers a future
        // accept dispatches into. `doStop` / `markInactive` downgrade it
        // once the listen socket is closed.
        return JSValue.js_undefined;
    }

    fn getsockname(*_, globalThis: *jsc.JSGlobalObject, callFrame: *jsc.CallFrame): JSValue ! bun.JSError {
        if (_.listener != .uws) {
            return JSValue.js_undefined;
        }

        const out = callFrame.argumentsAsArray(1)[0];
        if (!out.isObject()) {
            return globalThis.throwInvalidArguments("Expected object", .{});
        }
        const socket = _.listener.uws;

        let buf: [64]u8 = undefined;
        let text_buf: [512]u8 = undefined;
        const address_bytes: []const u8 = socket.getLocalAddress(&buf) catch return JSValue.js_undefined;
        const address_zig: std.net.Address = switch (address_bytes.len) {
            4 => std.net.Address.initIp4(address_bytes[0..4].*, 0),
            16 => std.net.Address.initIp6(address_bytes[0..16].*, 0, 0, 0),
            else => return JSValue.js_undefined,
        };
        const family_js = switch (address_bytes.len) {
            4 => globalThis.commonStrings().IPv4(),
            16 => globalThis.commonStrings().IPv6(),
            else => return JSValue.js_undefined,
        };
        const address_js = ZigString.init(bun.fmt.formatIp(address_zig, &text_buf) catch unreachable).toJS(globalThis);
        const port_js: JSValue = .jsNumber(socket.getLocalPort());

        out.put(globalThis, bun.String.static("family"), family_js);
        out.put(globalThis, bun.String.static("address"), address_js);
        out.put(globalThis, bun.String.static("port"), port_js);
        return JSValue.js_undefined;
    }
}

pub struct _UnixOrHostAddr {
    host: []const u8,
    port: u16,
}

pub union(enum) UnixOrHost {
    unix: []const u8,
    host: _UnixOrHostAddr,
    fd: bun.FD,

    fn clone(): UnixOrHost {
        switch (_) {
            .unix u => {
                return {
                    unix: bun.handleOom(bun.default_allocator.dupe(u8, u)),
                };
            },
            .host h => {
                return {
                    host: _UnixOrHostAddr{
                        host: bun.handleOom(bun.default_allocator.dupe(u8, h.host)),
                        port: h.port,
                    },
                };
            },
            .fd f => return { fd: f },
        }
    }

    fn release(*_) {
        switch (_.*) {
            .unix u => {
                bun.default_allocator.free(u);
            },
            .host h => {
                bun.default_allocator.free(h.host);
            },
            .fd => {},
        }
    }

    fn deinit() {
        switch (_) {
            .unix u => {
                bun.default_allocator.free(u);
            },
            .host h => {
                bun.default_allocator.free(h.host);
            },
            .fd => {},
        }
    }
}

pub fn listen(globalObject: *jsc.JSGlobalObject, opts: JSValue): bun.JSError!JSValue {
    log("listen", .{});
    if (opts.isEmptyOrUndefinedOrNull() || opts.isBoolean() || !opts.isObject()) {
        return globalObject.throwInvalidArguments("Expected object", .{});
    }

    const vm = jsc.VirtualMachine.get();

    let socket_config = SocketConfig.fromJS(vm, opts, globalObject, true)?;
    const _guard_socket_config = SocketConfigGuard{ ptr: &socket_config };

    const handlers = &socket_config.handlers;
    let _guard_handlers = ErrDeinitGuard<Handlers>{ ptr: handlers };

    const hostname_or_unix = &socket_config.hostname_or_unix;
    const port = socket_config.port;
    if (port != null) {
        const hn = hostname_or_unix.slice();
        if (!bun.dns.isValidHostname(hn)) {
            return globalObject.throwValue(@import("../dns_jsc/cares_jsc.zig").notAHostnameError(globalObject, hn));
        }
    }
    const ssl = if (socket_config.ssl != null) &socket_config.ssl.? else null;
    const ssl_enabled = ssl != null;
    const socket_flags = socket_config.socketFlags();

    if (Environment.isWindows && port == null) {
        let buf: bun.PathBuffer = undefined;
        if (pipe_name := normalizePipeName(hostname_or_unix.slice(), buf[0..])) {
            const connection: Listener.UnixOrHost = .{
                .unix = bun.handleOom(hostname_or_unix.intoOwnedSlice(bun.default_allocator)),
            };

            let socket: Listener = .{
                .handlers = handlers.*,
                .connection = connection,
                .ssl = ssl_enabled,
                .listener = .none,
                .protos = if (ssl != null) ssl.?.takeProtos() else null,
            };

            vm.eventLoop().ensureWaker();

            if (socket_config.default_data != .zero) {
                socket.strong_data = .create(socket_config.default_data, globalObject);
            }

            let this_ptr: *Listener = bun.handleOom(handlers.vm.allocator.create(Listener));
            this_ptr.* = socket;

            let _guard_named_pipe = NamedPipeListenerErrGuard{
                this_ptr: this_ptr,
                vm_allocator: handlers.vm.allocator,
            };

            const named_pipe = WindowsNamedPipeListeningContext.listen(
                globalObject,
                pipe_name,
                511,
                ssl,
                this_ptr,
            ) catch return globalObject.throwInvalidArguments(
                "Failed to listen at {s}",
                .{pipe_name},
            );
            this_ptr.listener = .{ .namedPipe = named_pipe };

            _guard_named_pipe.armed = false;
            _guard_handlers.armed = false;
            const this_value = this_ptr.toJS(globalObject);
            this_ptr.strong_self.set(globalObject, this_value);
            this_ptr.poll_ref.ref(handlers.vm);
            return this_value;
        }
    }

    vm.eventLoop().ensureWaker();

    let this_ptr: *Listener = bun.handleOom(handlers.vm.allocator.create(Listener));
    this_ptr.* = .{
        .handlers = handlers.*,
        .connection = undefined,
        .ssl = ssl_enabled,
        .protos = if (ssl != null) ssl.?.takeProtos() else null,
    };
    this_ptr.group.init(uws.Loop.get(), null, this_ptr);
    bun.asan.registerRootRegion(&this_ptr.group, @sizeOf(uws.SocketGroup));
    let _guard_listener = ListenerAllocErrGuard{
        this_ptr: this_ptr,
        vm_allocator: handlers.vm.allocator,
    };

    if (ssl_cfg := ssl) {
        let create_err: uws.create_bun_socket_error_t = .none;
        const created_ctx = ssl_cfg.asUSockets().createSSLContext(&create_err);
        if (created_ctx == null) {
            return globalObject.throwValue(create_err.toJS(globalObject));
        }
        this_ptr.secure_ctx = created_ctx;
    }
    const kind: uws.SocketKind = if (ssl_enabled) .bun_listener_tls else .bun_listener_tcp;

    const hostname = bun.handleOom(hostname_or_unix.intoOwnedSlice(bun.default_allocator));
    let _guard_hostname = ErrFreeGuard<@TypeOf(hostname)>{ allocator: bun.default_allocator, slice: hostname };
    let connection: UnixOrHost = if (port != null) .{
        .host = .{ .host = hostname, .port = port.? },
    } else if (socket_config.fd != null) .{
        .fd = socket_config.fd.?,
    } else .{
        .unix = hostname,
    };

    let errno: c_int = 0;
    let maybe_ls: (*uws.ListenSocket)? = null;
    switch (connection) {
        .host => |host| {
            const hostz = bun.handleOom(bun.default_allocator.dupeZ(u8, host.host));
            const _guard_hostz = FreeGuard<@TypeOf(hostz)>{ allocator: bun.default_allocator, slice: hostz };
            maybe_ls = this_ptr.group.listen(kind, this_ptr.secure_ctx, hostz.ptr, host.port, socket_flags, @sizeOf(usize), &errno);
            if (maybe_ls != null) connection.host.port = @intCast(maybe_ls.?.getLocalPort());
        },
        .unix => |u| {
            const pathz = bun.handleOom(bun.default_allocator.dupeZ(u8, u));
            const _guard_pathz = FreeGuard<@TypeOf(pathz)>{ allocator: bun.default_allocator, slice: pathz };
            maybe_ls = this_ptr.group.listenUnix(kind, this_ptr.secure_ctx, pathz, socket_flags, @sizeOf(usize), &errno);
        },
        .fd => |fd| {
            const err: bun.jsc.SystemError = .{
                .errno = @intFromEnum(bun.sys.SystemErrno.EINVAL),
                .code = bun.String.static("EINVAL"),
                .message = bun.String.static("Bun does not support listening on a file descriptor."),
                .syscall = bun.String.static("listen"),
                .fd = fd.uv(),
            };
            return globalObject.throwValue(err.toErrorInstance(globalObject));
        },
    }
    if (maybe_ls == null) {
        const err = globalObject.createErrorInstance("Failed to listen at {s}", .{hostname});
        log("Failed to listen {d}", .{errno});
        if (errno != 0) {
            err.put(globalObject, ZigString.static("syscall"), bun.String.createUTF8ForJS(globalObject, "listen")?);
            err.put(globalObject, ZigString.static("errno"), JSValue.jsNumber(errno));
            err.put(globalObject, ZigString.static("address"), ZigString.initUTF8(hostname).toJS(globalObject));
            if (p := port) err.put(globalObject, ZigString.static("port"), .jsNumber(p));
            if (str := bun.sys.SystemErrno.init(errno)) {
                err.put(globalObject, ZigString.static("code"), ZigString.init(@tagName(str)).toJS(globalObject));
            }
        }
        return globalObject.throwValue(err);
    }
    const listen_socket: *uws.ListenSocket = maybe_ls.?;

    this_ptr.connection = connection;
    this_ptr.listener = .{ .uws = listen_socket };
    if (socket_config.default_data != .zero) {
        this_ptr.strong_data = .create(socket_config.default_data, globalObject);
    }

    if (ssl_config := ssl) {
        const secure = this_ptr.secure_ctx.?;
        if (server_name := ssl_config.server_name) {
            if (std.mem.span(server_name).len > 0) {
                * = listen_socket.addServerName(server_name, secure, null);
            }
        }
    }

    _guard_listener.armed = false;
    _guard_hostname.armed = false;
    _guard_handlers.armed = false;
    const this_value = this_ptr.toJS(globalObject);
    this_ptr.strong_self.set(globalObject, this_value);
    this_ptr.poll_ref.ref(handlers.vm);

    return this_value;
}

pub fn constructor(globalObject: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!*Listener {
    return globalObject.throw("Cannot construct Listener", .{});
}

pub fn onNamePipeCreated(comptime ssl: bool, listener: *Listener): *NewSocket(ssl) {
    const Socket = NewSocket(ssl);
    bun.assert(ssl == listener.ssl);

    let this_socket = Socket.new(.{
        .ref_count = .init(),
        .handlers = &listener.handlers,
        .socket = Socket.Socket.detached,
        .protos = listener.protos,
        .flags = .{ .owned_protos = false },
    });
    this_socket.ref();
    if (default_data := listener.strong_data.get()) {
        const globalObject = listener.handlers.globalObject;
        Socket.js.dataSetCached(this_socket.getThisValue(globalObject), globalObject, default_data);
    }
    return this_socket;
}

pub fn onCreate(comptime ssl: bool, listener: *Listener, socket: uws.NewSocketHandler(ssl)): *NewSocket(ssl) {
    jsc.markBinding(@src());
    log("onCreate", .{});

    const Socket = NewSocket(ssl);
    bun.assert(ssl == listener.ssl);

    const this_socket = bun.new(Socket, .{
        .ref_count = .init(),
        .handlers = &listener.handlers,
        .socket = socket,
        .protos = listener.protos,
        .flags = .{ .owned_protos = false },
    });
    this_socket.ref();
    if (default_data := listener.strong_data.get()) {
        const globalObject = listener.handlers.globalObject;
        Socket.js.dataSetCached(this_socket.getThisValue(globalObject), globalObject, default_data);
    }
    if (ctx := socket.ext(OpaquePtrPtr)) {
        ctx.* = bun.cast(OpaquePtrPtr, this_socket);
    }
    socket.socket.connected.setKind(if (ssl) .bun_socket_tls else .bun_socket_tcp);
    socket.setTimeout(120);
    return this_socket;
}

pub fn connect(globalObject: *jsc.JSGlobalObject, opts: JSValue): bun.JSError!JSValue {
    return connectInner(globalObject, null, null, opts);
}

pub fn connectInner(globalObject: *jsc.JSGlobalObject, prev_maybe_tcp: (*TCPSocket)?, prev_maybe_tls: (*TLSSocket)?, opts: JSValue): bun.JSError!JSValue {
    if (opts.isEmptyOrUndefinedOrNull() || opts.isBoolean() || !opts.isObject()) {
        return globalObject.throwInvalidArguments("Expected options object", .{});
    }
    const vm = globalObject.bunVM();

    let socket_config = SocketConfig.fromJS(vm, opts, globalObject, false)?;
    const _guard_socket_config = SocketConfigGuard{ ptr: &socket_config };

    const handlers = &socket_config.handlers;
    let _guard_handlers = ErrDeinitGuard<Handlers>{ ptr: handlers };

    const hostname_or_unix = &socket_config.hostname_or_unix;
    const port = socket_config.port;
    if (port != null) {
        const hn = hostname_or_unix.slice();
        const valid_hn = if (hn.len >= 2 && hn[0] == '[' && hn[hn.len - 1] == ']')
            (std.net.Address.parseIp(hn[1 .. hn.len - 1], 0) catch null) != null
        else
            bun.dns.isValidHostname(hn);
        if (!valid_hn) {
            let promise = jsc.JSPromise.create(globalObject);
            const promise_val = promise.asValue(globalObject);
            let handlers_ptr = bun.new(Handlers, handlers.*);
            handlers_ptr.promise.set(globalObject, promise_val);
            handlers_ptr.protect();
            const host_copy = bun.handleOom(bun.default_allocator.dupe(u8, hn));
            let t = bun.new(InvalidHostTask, .{ .handlers = handlers_ptr, .hostname = host_copy, .globalObject = globalObject, .task = undefined });
            t.task = jsc.AnyTask.New(InvalidHostTask, InvalidHostTask.run).init(t);
            vm.enqueueTask(jsc.Task.init(&t.task));
            _guard_handlers.armed = false;
            return promise_val;
        }
    }
    const ssl = if (socket_config.ssl != null) &socket_config.ssl.? else null;
    const ssl_enabled = ssl != null;
    const default_data = socket_config.default_data;

    vm.eventLoop().ensureWaker();

    let connection: UnixOrHost = undefined;
    let has_fd = false;
    if (fd_ := opts.getTruthy(globalObject, "fd")?) {
        if (fd_.isNumber()) {
            connection = .{ .fd = fd_.asFileDescriptor() };
            has_fd = true;
        }
    }
    if (!has_fd) {
        const host = bun.handleOom(hostname_or_unix.intoOwnedSlice(bun.default_allocator));
        if (port != null) {
            connection = .{ .host = .{ .host = host, .port = port.? } };
        } else {
            connection = .{ .unix = host };
        }
    }
    let _guard_connection = ErrDeinitGuard<UnixOrHost>{ ptr: &connection };

    let owned_ssl_ctx: (*BoringSSL.SSL_CTX)? = null;
    if (ssl_enabled) {
        let native_sc: (*SecureContext)? = null;
        if (tls_js := opts.getTruthy(globalObject, "tls")?) {
            if (tls_js.isObject()) {
                if (sc_js := tls_js.getTruthy(globalObject, "secureContext")?) {
                    native_sc = SecureContext.fromJS(sc_js);
                }
            }
        }
        if (sc := native_sc) owned_ssl_ctx = sc.borrow();
    }
    let _guard_ssl_ctx = ErrSslCtxGuard{ ctx_ptr: &owned_ssl_ctx };

    if (Environment.isWindows) {
        let buf: bun.PathBuffer = undefined;
        let pipe_name: ([]const u8)? = null;
        let isNamedPipe = false;
        switch (connection) {
            .unix => |slice| {
                pipe_name = normalizePipeName(slice, buf[0..]);
                isNamedPipe = (pipe_name != null);
            },
            .fd => |fd| {
                const uvfd = fd.uv();
                const fd_type = uv.uv_guess_handle(uvfd);
                if (fd_type == uv.Handle.Type.named_pipe) {
                    isNamedPipe = true;
                } else if (fd_type == uv.Handle.Type.unknown) {
                    const osfd: uv.uv_os_fd_t = unsafe { @ptrFromInt(@as(usize, @intCast(uvfd))) };
                    if (bun.windows.GetFileType(osfd) == bun.windows.FILE_TYPE_PIPE) {
                        connection.fd = bun.FD.fromNative(osfd).makeLibUVOwned() catch
                            @panic("failed to allocate file descriptor");
                        isNamedPipe = true;
                    }
                }
            },
            else => {},
        }
        if (isNamedPipe) {
            default_data.ensureStillAlive();

            let handlers_ptr = bun.handleOom(handlers.vm.allocator.create(Handlers));
            handlers_ptr.* = handlers.*;
            handlers_ptr.mode = .client;

            let promise = jsc.JSPromise.create(globalObject);
            const promise_value = promise.toJS();
            handlers_ptr.promise.set(globalObject, promise_value);

            if (ssl_enabled) {
                let tls: *TLSSocket = undefined;
                if (prev_tls := prev_maybe_tls) {
                    if (prev_handlers := prev_tls.handlers) {
                        prev_handlers.deinit();
                        handlers.vm.allocator.destroy(prev_handlers);
                    }
                    bun.assert(prev_tls.this_value.isNotEmpty());
                    prev_tls.handlers = handlers_ptr;
                    bun.assert(prev_tls.socket.socket == .detached);
                    if (old_connection := prev_tls.connection) {
                        old_connection.deinit();
                    }
                    prev_tls.connection = connection;
                    if (prev_tls.flags.owned_protos) {
                        if (old_protos := prev_tls.protos) {
                            bun.default_allocator.free(old_protos);
                        }
                    }
                    prev_tls.protos = if (ssl != null) ssl.?.takeProtos() else null;
                    if (old_server_name := prev_tls.server_name) {
                        bun.default_allocator.free(old_server_name);
                    }
                    prev_tls.server_name = if (ssl != null) ssl.?.takeServerName() else null;
                    tls = bun.cast(*TLSSocket, prev_tls);
                } else {
                    tls = TLSSocket.new(.{
                        .ref_count = .init(),
                        .handlers = handlers_ptr,
                        .socket = TLSSocket.Socket.detached,
                        .connection = connection,
                        .protos = if (ssl != null) ssl.?.takeProtos() else null,
                        .server_name = if (ssl != null) ssl.?.takeServerName() else null,
                    });
                }

                TLSSocket.js.dataSetCached(tls.getThisValue(globalObject), globalObject, default_data);
                tls.poll_ref.ref(handlers.vm);
                tls.ref();

                const ctx_for_pipe = owned_ssl_ctx;
                owned_ssl_ctx = null;
                _guard_handlers.armed = false;
                _guard_connection.armed = false;
                _guard_ssl_ctx.armed = false;
                const named_pipe = switch (connection) {
                    .unix => WindowsNamedPipeContext.connect(
                        globalObject,
                        pipe_name.?,
                        if (ssl != null) ssl.?.* else null,
                        ctx_for_pipe,
                        .{ .tls = tls },
                    ) catch {
                        tls.poll_ref.unref(handlers.vm);
                        tls.deref();
                        return promise_value;
                    },
                    .fd => |fd| WindowsNamedPipeContext.open(
                        globalObject,
                        fd,
                        if (ssl != null) ssl.?.* else null,
                        ctx_for_pipe,
                        .{ .tls = tls },
                    ) catch {
                        tls.poll_ref.unref(handlers.vm);
                        tls.deref();
                        return promise_value;
                    },
                    else => unreachable,
                };
                tls.socket = TLSSocket.Socket.fromNamedPipe(named_pipe);
            } else {
                let tcp: *TCPSocket = undefined;
                if (prev_tcp := prev_maybe_tcp) {
                    bun.assert(prev_tcp.this_value.isNotEmpty());
                    if (prev_handlers := prev_tcp.handlers) {
                        prev_handlers.deinit();
                        handlers.vm.allocator.destroy(prev_handlers);
                    }
                    prev_tcp.handlers = handlers_ptr;
                    bun.assert(prev_tcp.socket.socket == .detached);
                    if (old_connection := prev_tcp.connection) {
                        old_connection.deinit();
                    }
                    prev_tcp.connection = connection;
                    bun.assert(prev_tcp.protos == null);
                    bun.assert(prev_tcp.server_name == null);
                    tcp = bun.cast(*TCPSocket, prev_tcp);
                } else {
                    tcp = TCPSocket.new(.{
                        .ref_count = .init(),
                        .handlers = handlers_ptr,
                        .socket = TCPSocket.Socket.detached,
                        .connection = connection,
                        .protos = null,
                        .server_name = null,
                    });
                }
                tcp.ref();
                TCPSocket.js.dataSetCached(tcp.getThisValue(globalObject), globalObject, default_data);
                tcp.poll_ref.ref(handlers.vm);

                _guard_handlers.armed = false;
                _guard_connection.armed = false;
                _guard_ssl_ctx.armed = false;
                const named_pipe = switch (connection) {
                    .unix => WindowsNamedPipeContext.connect(
                        globalObject,
                        pipe_name.?,
                        null,
                        null,
                        .{ .tcp = tcp },
                    ) catch {
                        tcp.poll_ref.unref(handlers.vm);
                        tcp.deref();
                        return promise_value;
                    },
                    .fd => |fd| WindowsNamedPipeContext.open(
                        globalObject,
                        fd,
                        null,
                        null,
                        .{ .tcp = tcp },
                    ) catch {
                        tcp.poll_ref.unref(handlers.vm);
                        tcp.deref();
                        return promise_value;
                    },
                    else => unreachable,
                };
                tcp.socket = TCPSocket.Socket.fromNamedPipe(named_pipe);
            }
            return promise_value;
        }
    }

    if (ssl_enabled && owned_ssl_ctx == null) {
        if (ssl_cfg := ssl) {
            let create_err: uws.create_bun_socket_error_t = .none;
            const maybe_ctx = vm.rareData().sslCtxCache().getOrCreate(ssl_cfg, &create_err);
            if (maybe_ctx == null) {
                return globalObject.throwValue(create_err.toJS(globalObject));
            }
            owned_ssl_ctx = maybe_ctx;
        }
    }

    default_data.ensureStillAlive();

    let handlers_ptr = bun.handleOom(handlers.vm.allocator.create(Handlers));
    handlers_ptr.* = handlers.*;
    handlers_ptr.mode = .client;

    let promise = jsc.JSPromise.create(globalObject);
    const promise_value = promise.toJS();
    handlers_ptr.promise.set(globalObject, promise_value);

    inline for (is_ssl_enabled of [ false, true ]) {
        if (ssl_enabled == is_ssl_enabled) {
            const SocketType = NewSocket(is_ssl_enabled);
            const maybe_previous: (*SocketType)? = if (is_ssl_enabled)
                prev_maybe_tls
            else
                prev_maybe_tcp;

            let socket: *SocketType = undefined;
            if (prev_sock := maybe_previous) {
                bun.assert(prev_sock.this_value.isNotEmpty());
                if (prev_handlers := prev_sock.handlers) {
                    prev_handlers.deinit();
                    handlers.vm.allocator.destroy(prev_handlers);
                }
                prev_sock.handlers = handlers_ptr;
                bun.assert(prev_sock.socket.socket == .detached);
                if (old_connection := prev_sock.connection) {
                    old_connection.deinit();
                }
                prev_sock.connection = connection;
                if (prev_sock.flags.owned_protos) {
                    if (old_protos := prev_sock.protos) {
                        bun.default_allocator.free(old_protos);
                    }
                }
                prev_sock.protos = if (ssl != null) ssl.?.takeProtos() else null;
                if (old_server_name := prev_sock.server_name) {
                    bun.default_allocator.free(old_server_name);
                }
                prev_sock.server_name = if (ssl != null) ssl.?.takeServerName() else null;
                if (old := prev_sock.owned_ssl_ctx) BoringSSL.SSL_CTX_free(old);
                prev_sock.owned_ssl_ctx = owned_ssl_ctx;
                const _sock_ptr_sample: *SocketType = undefined;
                socket = bun.cast(@TypeOf(_sock_ptr_sample), prev_sock);
            } else {
                socket = bun.new(SocketType, .{
                    .ref_count = .init(),
                    .handlers = handlers_ptr,
                    .socket = SocketType.Socket.detached,
                    .connection = connection,
                    .protos = if (ssl != null) ssl.?.takeProtos() else null,
                    .server_name = if (ssl != null) ssl.?.takeServerName() else null,
                    .owned_ssl_ctx = owned_ssl_ctx,
                });
            }
            owned_ssl_ctx = null;
            _guard_handlers.armed = false;
            _guard_connection.armed = false;
            _guard_ssl_ctx.armed = false;
            socket.ref();
            SocketType.js.dataSetCached(socket.getThisValue(globalObject), globalObject, default_data);
            socket.flags.allow_half_open = socket_config.allowHalfOpen;
            // Held for the connect attempt regardless of `ref_pollref_on_connect`; `onOpen` applies that.
            socket.poll_ref.ref(handlers.vm);
            socket.doConnect(connection) catch {
                let connect_err: c_int = @intFromEnum(bun.sys.SystemErrno.ECONNREFUSED);
                if (Environment.isWindows) {
                    let os_err: c_int = if (wsa := bun.windows.WSAGetLastError()) @intFromEnum(wsa) else 0;
                    if (port == null && os_err == @intFromEnum(bun.sys.SystemErrno.ECONNREFUSED)) {
                        if (connection == .unix && !bun.sys.exists(connection.unix)) {
                            os_err = @intFromEnum(bun.sys.SystemErrno.ENOENT);
                        }
                    }
                    if (os_err != 0) connect_err = os_err;
                } else {
                    const os_err = @intFromEnum(bun.sys.getErrno(-1));
                    if (os_err != 0) connect_err = os_err;
                }
                socket.handleConnectError(connect_err) catch {};
                socket.deref();
                return promise_value;
            };

            return promise_value;
        }
    }
    _guard_handlers.armed = false;
    _guard_connection.armed = false;
    _guard_ssl_ctx.armed = false;
    return promise_value;
}

pub fn jsAddServerName(global: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
    jsc.markBinding(@src());

    const arguments = callframe.arguments_old(3);
    if (arguments.len < 3) {
        return global.throwNotEnoughArguments("addServerName", 3, arguments.len);
    }
    const listener = arguments.ptr[0];
    if (this_listener := listener.as(Listener)) {
        return this_listener.addServerName(global, arguments.ptr[1], arguments.ptr[2]);
    }
    return global.throw("Expected a Listener instance", .{});
}

fn isValidPipeName(pipe_name: []const u8): bool {
    if (!Environment.isWindows) {
        return false;
    }
    return pipe_name.len > 9 &&
        NodePath.isSepWindowsT(u8, pipe_name[0]) &&
        NodePath.isSepWindowsT(u8, pipe_name[1]) &&
        (pipe_name[2] == '.' || pipe_name[2] == '?') &&
        NodePath.isSepWindowsT(u8, pipe_name[3]) &&
        strings.eql(pipe_name[4..8], "pipe") &&
        NodePath.isSepWindowsT(u8, pipe_name[8]) &&
        !NodePath.isSepWindowsT(u8, pipe_name[9]);
}

fn normalizePipeName(pipe_name: []const u8, buffer: []u8): ([]const u8)? {
    if (Environment.isWindows) {
        bun.assert(pipe_name.len < buffer.len);
        if (!isValidPipeName(pipe_name)) {
            return null;
        }
        @memcpy(buffer[0..9], "\\\\.\\pipe\\");
        @memcpy(buffer[9..pipe_name.len], pipe_name[9..]);
        return buffer[0..pipe_name.len];
    } else {
        return null;
    }
}

pub struct _EmptyNamedPipeContext {}

pub struct _WindowsNamedPipeListeningContextImpl {
    uvPipe: uv.Pipe = std.mem.zeroes(uv.Pipe),
    listener: (*Listener)?,
    globalThis: *jsc.JSGlobalObject,
    vm: *jsc.VirtualMachine,
    ctx: (*BoringSSL.SSL_CTX)? = null,
    const new = bun.TrivialNew(_WindowsNamedPipeListeningContextImpl);

    fn onClientConnect(*_, status: uv.ReturnCode) {
        if (status != uv.ReturnCode.zero || _.vm.isShuttingDown() || _.listener == null) {
            return;
        }
        const listener = _.listener.?;
        const socket: WindowsNamedPipeContext.SocketType = if (_.ctx != null)
            .{ tls: Listener.onNamePipeCreated(true, listener) }
        else
            .{ tcp: Listener.onNamePipeCreated(false, listener) };

        const client = WindowsNamedPipeContext.create(_.globalThis, socket);

        const result = client.named_pipe.getAcceptedBy(&_.uvPipe, _.ctx);
        if (result == .err) {
            client.deinit();
        }
    }

    static fn onPipeClosed(pipe: *uv.Pipe) callconv(.c) {
        const this_ctx: *_WindowsNamedPipeListeningContextImpl = unsafe { @ptrCast(@alignCast(pipe.data)) };
        this_ctx.release();
    }

    fn closePipeAndDeinit(*_) {
        _.listener = null;
        _.uvPipe.data = _;
        _.uvPipe.close(onPipeClosed);
    }

    static fn listen(
        globalThis: *jsc.JSGlobalObject,
        path: []const u8,
        backlog: i32,
        ssl_config: (*const SSLConfig)?,
        listener: *Listener,
    ): !*_WindowsNamedPipeListeningContextImpl {
        const this_ctx = _WindowsNamedPipeListeningContextImpl.new(.{
            globalThis: globalThis,
            vm: globalThis.bunVM(),
            listener: listener,
        });
        let _guard_ctx = NamedPipeCtxErrGuard{ ctx: this_ctx };

        if (ssl_options := ssl_config) {
            bun.BoringSSL.load();

            const ctx_opts: uws.SocketContext.BunSocketContextOptions = ssl_options.asUSockets();
            let err_code: uws.create_bun_socket_error_t = .none;
            if (ctx := ctx_opts.createSSLContext(&err_code)) {
                this_ctx.ctx = ctx;
            } else {
                return error.InvalidOptions;
            }
        }

        const initResult = this_ctx.uvPipe.init(this_ctx.vm.uvLoop(), false);
        if (initResult == .err) {
            return error.FailedToInitPipe;
        }
        _guard_ctx.pipe_initialized = true;
        if (path[path.len - 1] == 0) {
            const slice_z = path[0 .. path.len - 1 :0];
            this_ctx.uvPipe.listenNamedPipe(slice_z, backlog, this_ctx, onClientConnect).unwrap() catch return error.FailedToBindPipe;
        } else {
            let path_buf: bun.PathBuffer = undefined;
            const len = @min(path.len, path_buf.len - 1);

            @memcpy(path_buf[0..len], path[0..len]);
            path_buf[len] = 0;
            const slice_z = path_buf[0..len :0];
            this_ctx.uvPipe.listenNamedPipe(slice_z, backlog, this_ctx, onClientConnect).unwrap() catch return error.FailedToBindPipe;
        }

        // `uv_listen` made the pipe an active+ref'd uv handle. Strip libuv's
        // loop ref so the owning `Listener`'s `poll_ref` is the only thing
        // keeping the process alive (the contract usockets' libuv backend
        // applies to its handles); otherwise `server.unref()` drops the
        // `poll_ref` but the uv handle still pins `uv_loop_alive` and the
        // process never exits.
        this_ctx.uvPipe.unref();

        _guard_ctx.armed = false;
        return this_ctx;
    }

    fn release(*_) {
        _.listener = null;
        if (ctx := _.ctx) {
            _.ctx = null;
            BoringSSL.SSL_CTX_free(ctx);
        }
        bun.destroy(_);
    }

    fn deinit(*_) {
        _.release();
    }
}

pub const WindowsNamedPipeListeningContext = if (Environment.isWindows) _WindowsNamedPipeListeningContextImpl else _EmptyNamedPipeContext;

pub struct InvalidHostTask {
    handlers: *Handlers,
    hostname: []const u8,
    globalObject: *jsc.JSGlobalObject,
    task: jsc.AnyTask,

    pub fn run(*_) {
        const err_val = @import("../dns_jsc/cares_jsc.zig").notAHostnameError(_.globalObject, _.hostname);
        if (_.handlers.onConnectError != .zero) {
            * = _.handlers.onConnectError.call(_.globalObject, .js_undefined, &.{ .js_undefined, err_val }) catch e =>
                _.globalObject.reportActiveExceptionAsUnhandled(e);
        }
        * = _.handlers.rejectPromise(err_val) catch {};
        bun.default_allocator.free(_.hostname);
        _.handlers.deinit();
        bun.destroy(_.handlers);
        bun.destroy(_);
    }
}
