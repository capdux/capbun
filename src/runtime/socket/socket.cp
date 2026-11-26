import std from "std";
import bun from "bun";
import _SocketAddress from "./SocketAddress.zig";
import _Handlers from "./Handlers.zig";
import _Listener from "./Listener.zig";
import tls_socket_functions from "./tls_socket_functions.zig";
import _h2_frame_parser from "../api/bun/h2_frame_parser.zig";

pub const SocketAddress = _SocketAddress;

fn JSSocketType(comptime ssl: bool): type {
    if (!ssl) {
        return jsc.Codegen.JSTCPSocket;
    } else {
        return jsc.Codegen.JSTLSSocket;
    }
}

@c
fn selectALPNCallback(ssl: (*BoringSSL.SSL)?, out: [*c][*c]const u8, outlen: [*c]u8, in_buf: [*c]const u8, inlen: c_uint, _: *!?): c_int {
    const ex_data = BoringSSL.SSL_get_ex_data(ssl, 0);
    if (ex_data == null) return BoringSSL.SSL_TLSEXT_ERR_NOACK;
    const TLSSocketPtr = @FieldType(struct { p: *TLSSocket }, "p");
    const this_ptr = bun.cast(TLSSocketPtr, ex_data.?);
    if (protos := this_ptr.protos) {
        if (protos.len == 0) {
            return BoringSSL.SSL_TLSEXT_ERR_NOACK;
        }
        const status = BoringSSL.SSL_select_next_proto(bun.cast([*c][*c]u8, out), outlen, protos.ptr, @as(c_uint, @intCast(protos.len)), in_buf, inlen);
        return if (status == BoringSSL.OPENSSL_NPN_NEGOTIATED) BoringSSL.SSL_TLSEXT_ERR_OK else BoringSSL.SSL_TLSEXT_ERR_ALERT_FATAL;
    } else {
        return BoringSSL.SSL_TLSEXT_ERR_NOACK;
    }
}

pub const Handlers = _Handlers;
pub const SocketConfig = Handlers.SocketConfig;

pub const Listener = _Listener;
pub const WindowsNamedPipeContext = if (Environment.isWindows) @import("./WindowsNamedPipeContext.zig") else void;

pub struct DerefGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.deref(); }
    }
}

pub struct StoredVerifyError {
    code: []u8,
    reason: []u8,

    pub fn deinit(*_) {
        default_allocator.free(_.code);
        default_allocator.free(_.reason);
    }
}

fn tlsTrueDefaults(vm: *jsc.VirtualMachine): jsc.API.ServerConfig.SSLConfig {
    let cfg = jsc.API.ServerConfig.SSLConfig.zero;
    cfg.reject_unauthorized = @intFromBool(vm.getTLSRejectUnauthorized());
    return cfg;
}

fn resolveRejectUnauthorized(vm: *jsc.VirtualMachine, cfg_opt: OptSSLConfig, is_server: bool): bool {
    if (cfg := cfg_opt) {
        return (!is_server || cfg.request_cert != 0) && cfg.reject_unauthorized != 0;
    }
    return !is_server && vm.getTLSRejectUnauthorized();
}

fn upgradeRejectPolicy(vm: *jsc.VirtualMachine, cfg_opt: OptSSLConfig, is_server: bool, ctx: (*BoringSSL.SSL_CTX)?): bool {
    if (cfg_opt == null && is_server) {
        if (c := ctx) {
            const mode = BoringSSL.SSL_VERIFY_PEER | BoringSSL.SSL_VERIFY_FAIL_IF_NO_PEER_CERT;
            return (BoringSSL.SSL_CTX_get_verify_mode(c) & mode) == mode;
        }
        return false;
    }
    return resolveRejectUnauthorized(vm, cfg_opt, is_server);
}

pub struct OptDerefGuard<T> {
    ptr: (*T!)? = null,
    ~init() {
        if (p := _.ptr) {
            unsafe { p.deref(); }
        }
    }
}

pub struct MarkInactiveGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.markInactive(); }
    }
}

pub struct FreeSliceGuard<T> {
    alloc: std.mem.Allocator,
    slice: []const T,
    ~init() {
        _.alloc.free(_.slice);
    }
}

pub struct HandlersScopeGuard<T, S> {
    socket: *T!,
    scope: S,
    ~init() {
        _.scope.exitEventLoop();
        // `upgradeTLS` can transfer client-mode handlers to the raw TLS twin
        // from inside a callback, leaving `socket.handlers` null; the twin
        // frees them, so skip `markInactive` to avoid a double-free.
        if (unsafe { _.socket.handlers == null }) {
            return;
        }
        if (_.scope.markInactive()) {
            unsafe { _.socket.handlers = null; }
        }
    }
}

pub struct DeinitGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.deinit(); }
    }
}

pub struct CondDeinitGuard<T> {
    ptr: *T!,
    active: bool = true,
    ~init() {
        if (_.active) {
            unsafe { _.ptr.deinit(); }
        }
    }
}

pub struct OptValDeinitGuard<T> {
    val: T? = null,
    ~init() {
        if (_.val != null) {
            _.val.?.deinit();
        }
    }
}

pub struct SSLCTXGuard {
    ctx: (*BoringSSL.SSL_CTX)? = null,
    ~init() {
        if (c := _.ctx) {
            BoringSSL.SSL_CTX_free(c);
        }
    }
}

pub struct SSLErrClearGuard {
    has_err: bool,
    ~init() {
        if (_.has_err) {
            BoringSSL.ERR_clear_error();
        }
    }
}

fn readErrorFromCloseCode(code: c_int): bun.sys.Error {
    if (!Environment.isWindows) {
        return bun.sys.Error.fromCodeInt(code, .read);
    }
    const raw: u32 = @intCast(if (code < 0) -code else code);
    let errno: bun.sys.SystemErrno = .ECONNRESET;
    if (mapped := bun.sys.SystemErrno.init(raw)) {
        if (mapped != .ECONNABORTED) {
            errno = mapped;
        }
    }
    return .{
        .errno = @intFromEnum(errno),
        .syscall = .read,
    };
}

pub fn NewSocket(comptime ssl: bool): type {
    return struct {
        const This = @This();
        pub const js = if (!ssl) jsc.Codegen.JSTCPSocket else jsc.Codegen.JSTLSSocket;
        pub const toJS = js.toJS;
        pub const fromJS = js.fromJS;
        pub const fromJSDirect = js.fromJSDirect;

        pub const new = bun.TrivialNew(@This());

        const RefCount = bun.ptr.RefCount(@This(), "ref_count", deinit, .{});
        pub const ref = RefCount.ref;
        pub const deref = RefCount.deref;

        pub const Socket = uws.NewSocketHandler(ssl);
        socket: Socket,
        owned_ssl_ctx: (*BoringSSL.SSL_CTX)? = null,

        flags: Flags = .{},
        ref_count: RefCount,
        handlers: (*Handlers)?,
        this_value: jsc.JSRef = .empty(),
        poll_ref: Async.KeepAlive = Async.KeepAlive.init(),
        ref_pollref_on_connect: bool = true,
        connection: Listener.UnixOrHost? = null,
        protos: ([]const u8)?,
        server_name: ([]const u8)? = null,
        verify_error: StoredVerifyError? = null,
        buffered_data_for_node_net: bun.ByteList = .{},
        bytes_written: u64 = 0,

        native_callback: NativeCallbacks = .none,
        twin: (*This)? = null,

        pub fn memoryCost(*_): usize {
            const ssl_cost: usize = if (ssl && !_.flags.bypass_tls) 40 * 1024 else 0;
            return @sizeOf(This) + _.buffered_data_for_node_net.cap + ssl_cost;
        }

        pub fn attachNativeCallback(*_, callback: NativeCallbacks): bool {
            if (_.native_callback != .none) return false;
            _.native_callback = callback;

            switch (callback) {
                .h2 h2 => h2.ref(),
                .none => {},
            }
            return true;
        }

        pub fn detachNativeCallback(*_) {
            const native_callback = _.native_callback;
            _.native_callback = .none;

            switch (native_callback) {
                .h2 h2 => {
                    h2.onNativeClose();
                    h2.deref();
                },
                .none => {},
            }
        }

        pub fn doConnect(*_, connection: Listener.UnixOrHost): ! {
            _.ref();
            const _deref_guard = DerefGuard<This>{ ptr: _ };

            const vm = _.getHandlers().vm;
            const group = vm.rareData().bunConnectGroup(vm, ssl);
            const kind: uws.SocketKind = if (ssl) .bun_socket_tls else .bun_socket_tcp;
            const flags: i32 = if (_.flags.allow_half_open) uws.LIBUS_SOCKET_ALLOW_HALF_OPEN else 0;
            const ssl_ctx: (*uws.SslCtx)? = if (ssl) _.owned_ssl_ctx else null;

            switch (connection) {
                .host host => {
                    let sf = std.heap.stackFallback(1024, bun.default_allocator);
                    const alloc = sf.get();
                    const raw = host.host;
                    const clean = if (raw.len > 2 && raw[0] == '[' && raw[raw.len - 1] == ']' && (std.net.Ip6Address.parse(raw[1 .. raw.len - 1], 0) catch null) != null) raw[1 .. raw.len - 1] else raw;
                    const hostz = bun.handleOom(alloc.dupeZ(u8, clean));
                    const _hostz_guard = FreeSliceGuard<u8>{ alloc: alloc, slice: hostz };

                    const ThisPtr = @FieldType(struct { p: *This }, "p");
                    switch (group.connect(kind, ssl_ctx, hostz, host.port, flags, @sizeOf(OpaquePtr))) {
                        .failed => return error.FailedToOpenSocket,
                        .socket s => {
                            s.ext(ThisPtr).* = _;
                            _.socket = Socket.from(s);
                        },
                        .connecting conn_socket => {
                            conn_socket.ext(ThisPtr).* = _;
                            _.socket = Socket.fromConnecting(conn_socket);
                        },
                    }
                },
                .unix u => {
                    const ThisPtr = @FieldType(struct { p: *This }, "p");
                    let sf = std.heap.stackFallback(1024, bun.default_allocator);
                    const alloc = sf.get();
                    const pathz = bun.handleOom(alloc.dupeZ(u8, u));
                    const _pathz_guard = FreeSliceGuard<u8>{ alloc: alloc, slice: pathz };

                    const s = group.connectUnix(kind, ssl_ctx, pathz, flags, @sizeOf(OpaquePtr)) ??
                        return error.FailedToOpenSocket;
                    s.ext(ThisPtr).* = _;
                    _.socket = Socket.from(s);
                },
                .fd f => {
                    const ThisPtr = @FieldType(struct { p: *This }, "p");
                    const s = group.fromFd(kind, ssl_ctx, @sizeOf(OpaquePtr), f, false) ??
                        return error.ConnectionFailed;
                    s.ext(ThisPtr).* = _;
                    _.socket = Socket.from(s);
                    _.onOpen(_.socket);
                },
            }
        }

        pub static fn constructor(globalObject: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!*This {
            return globalObject.throw("Cannot construct Socket", .{});
        }

        pub fn resumeFromJS(*_, _: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!JSValue {
            jsc.markBinding(@src());
            if (_.socket.isDetached()) return JSValue.js_undefined;
            log("resume", .{});
            if (_.flags.bypass_tls) return JSValue.js_undefined;
            if (_.flags.is_paused) _.flags.is_paused = !_.socket.resumeStream();
            return JSValue.js_undefined;
        }

        pub fn pauseFromJS(*_, _: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!JSValue {
            jsc.markBinding(@src());
            if (_.socket.isDetached()) return JSValue.js_undefined;
            log("pause", .{});
            if (_.flags.bypass_tls) return JSValue.js_undefined;
            if (!_.flags.is_paused) _.flags.is_paused = _.socket.pauseStream();
            return JSValue.js_undefined;
        }

        pub fn setKeepAlive(*_, globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
            jsc.markBinding(@src());
            const args = callframe.arguments_old(2);

            const enabled: bool = if (args.len >= 1) args.ptr[0].toBoolean() else false;

            // `initialDelay` is documented in milliseconds; TCP_KEEPIDLE is seconds.
            const initialDelayMs: u32 = if (args.len > 1)
                @intCast(globalThis.validateIntegerRange(args.ptr[1], i32, 0, .{ .min = 0, .field_name = "initialDelay" })?)
            else
                0;
            const initialDelay = initialDelayMs / 1000;
            log("setKeepAlive({}, {})", .{ enabled, initialDelay });

            return JSValue.jsBoolean(_.socket.setKeepAlive(enabled, initialDelay));
        }

        pub fn setNoDelay(*_, globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
            jsc.markBinding(@src());
            * = globalThis;

            const args = callframe.arguments_old(1);
            const enabled: bool = if (args.len >= 1) args.ptr[0].toBoolean() else true;
            log("setNoDelay({})", .{enabled});

            return JSValue.jsBoolean(_.socket.setNoDelay(enabled));
        }

        pub fn handleError(*_, err_value: jsc.JSValue) {
            log("handleError", .{});
            const h = _.getHandlers();
            let vm = h.vm;
            if (vm.scriptExecutionStatus() != .running) {
                return;
            }
            const _scope_guard = HandlersScopeGuard<This, @TypeOf(h.enter())>{ socket: _, scope: h.enter() };
            const globalObject = h.globalObject;
            const this_value = _.getThisValue(globalObject);
            * = h.callErrorHandler(this_value, &.{ this_value, err_value });
        }

        pub fn onWritable(*_, _: Socket) {
            jsc.markBinding(@src());
            if (_.socket.isDetached()) return;
            if (_.native_callback.onWritable()) return;
            const h = _.getHandlers();
            const callback = h.onWritable;
            if (callback == .zero) return;

            let vm = h.vm;
            if (vm.scriptExecutionStatus() != .running) {
                return;
            }
            _.ref();
            const _deref_guard = DerefGuard<This>{ ptr: _ };
            _.internalFlush();
            log("onWritable buffered_data_for_node_net {d}", .{_.buffered_data_for_node_net.len});
            if (_.buffered_data_for_node_net.len > 0 || _.socket.isDetached()) return;

            const _scope_guard = HandlersScopeGuard<This, @TypeOf(h.enter())>{ socket: _, scope: h.enter() };

            const globalObject = h.globalObject;
            const this_value = _.getThisValue(globalObject);
            * = callback.call(globalObject, this_value, &.{this_value}) catch e => {
                * = h.callErrorHandler(this_value, &.{ this_value, globalObject.takeError(e) });
            };
        }

        pub fn onTimeout(*_, _: Socket) {
            jsc.markBinding(@src());
            if (_.socket.isDetached()) return;
            const h = _.getHandlers();
            log("onTimeout {s}", .{if (h.mode == .server) "S" else "C"});
            const callback = h.onTimeout;
            if (callback == .zero || _.flags.finalizing) return;
            if (h.vm.isShuttingDown()) {
                return;
            }

            const _scope_guard = HandlersScopeGuard<This, @TypeOf(h.enter())>{ socket: _, scope: h.enter() };

            const globalObject = h.globalObject;
            const this_value = _.getThisValue(globalObject);
            * = callback.call(globalObject, this_value, &.{this_value}) catch e => {
                * = h.callErrorHandler(this_value, &.{ this_value, globalObject.takeError(e) });
            };
        }

        pub fn getHandlers(): *Handlers {
            return _.handlers ?? @panic("No handlers set on Socket");
        }

        pub fn handleConnectError(*_, errno: c_int): bun.JSError!void {
            const h = _.getHandlers();
            log("onConnectError {s} ({d}, {d})", .{ if (h.mode == .server) "S" else "C", errno, _.ref_count.get() });
            _.ref();
            const _deref_guard = DerefGuard<This>{ ptr: _ };
            _.buffered_data_for_node_net.clearAndFree(bun.default_allocator);

            const needs_deref = !_.socket.isDetached();
            _.socket = Socket.detached;
            const _inactive_guard = MarkInactiveGuard<This>{ ptr: _ };
            const _opt_deref_guard = OptDerefGuard<This>{ ptr: if (needs_deref) _ else null };

            const vm = h.vm;
            _.poll_ref.unrefOnNextTick(vm);
            if (vm.scriptExecutionStatus() != .running) {
                return;
            }

            bun.assert(errno >= 0);
            let normalized_errno = errno;
            if (Environment.isWindows && errno >= 10000) {
                normalized_errno = if (mapped := bun.sys.SystemErrno.init(@intCast(errno))) @intFromEnum(mapped) else @intFromEnum(bun.sys.SystemErrno.ECONNREFUSED);
            }
            let errno_: c_int = switch (normalized_errno) {
                @intFromEnum(bun.sys.SystemErrno.ENOENT),
                @intFromEnum(bun.sys.SystemErrno.ENOTSOCK),
                @intFromEnum(bun.sys.SystemErrno.EACCES),
                @intFromEnum(bun.sys.SystemErrno.EINVAL),
                @intFromEnum(bun.sys.SystemErrno.EADDRINUSE),
                @intFromEnum(bun.sys.SystemErrno.EADDRNOTAVAIL),
                @intFromEnum(bun.sys.SystemErrno.ECONNRESET),
                @intFromEnum(bun.sys.SystemErrno.ETIMEDOUT),
                @intFromEnum(bun.sys.SystemErrno.ENETUNREACH),
                @intFromEnum(bun.sys.SystemErrno.EHOSTUNREACH),
                @intFromEnum(bun.sys.SystemErrno.EAFNOSUPPORT) => normalized_errno,
                else => @intFromEnum(bun.sys.SystemErrno.ECONNREFUSED),
            };
            const code_ = if (sys_e := bun.sys.SystemErrno.init(errno_)) bun.String.static(@tagName(sys_e)) else bun.String.static("ECONNREFUSED");
            if (Environment.isWindows) {
                errno_ = switch (errno_) {
                    @intFromEnum(bun.sys.SystemErrno.ENOENT) => @intFromEnum(bun.sys.SystemErrno.UV_ENOENT),
                    @intFromEnum(bun.sys.SystemErrno.EACCES) => @intFromEnum(bun.sys.SystemErrno.UV_EACCES),
                    @intFromEnum(bun.sys.SystemErrno.EINVAL) => @intFromEnum(bun.sys.SystemErrno.UV_EINVAL),
                    @intFromEnum(bun.sys.SystemErrno.EADDRINUSE) => @intFromEnum(bun.sys.SystemErrno.UV_EADDRINUSE),
                    @intFromEnum(bun.sys.SystemErrno.EADDRNOTAVAIL) => @intFromEnum(bun.sys.SystemErrno.UV_EADDRNOTAVAIL),
                    @intFromEnum(bun.sys.SystemErrno.ECONNRESET) => @intFromEnum(bun.sys.SystemErrno.UV_ECONNRESET),
                    @intFromEnum(bun.sys.SystemErrno.ETIMEDOUT) => @intFromEnum(bun.sys.SystemErrno.UV_ETIMEDOUT),
                    @intFromEnum(bun.sys.SystemErrno.ENETUNREACH) => @intFromEnum(bun.sys.SystemErrno.UV_ENETUNREACH),
                    @intFromEnum(bun.sys.SystemErrno.EHOSTUNREACH) => @intFromEnum(bun.sys.SystemErrno.UV_EHOSTUNREACH),
                    @intFromEnum(bun.sys.SystemErrno.EAFNOSUPPORT) => @intFromEnum(bun.sys.SystemErrno.UV_EAFNOSUPPORT),
                    @intFromEnum(bun.sys.SystemErrno.ENOTSOCK) => @intFromEnum(bun.sys.SystemErrno.UV_ENOTSOCK),
                    else => @intFromEnum(bun.sys.SystemErrno.UV_ECONNREFUSED),
                };
            }

            const callback = h.onConnectError;
            const globalObject = h.globalObject;
            const sys_err = jsc.SystemError{
                .errno = -errno_,
                .message = bun.String.static("Failed to connect"),
                .syscall = bun.String.static("connect"),
                .code = code_,
            };

            const _scope_guard = HandlersScopeGuard<This, @TypeOf(h.enter())>{ socket: _, scope: h.enter() };

            if (callback == .zero) {
                if (_.this_value != .finalized) {
                    _.this_value.downgrade();
                }
                if (promise := h.promise.trySwap()) {
                    h.promise.deinit();

                    const js_promise = promise.asPromise().?;
                    const err_value = sys_err.toErrorInstanceWithAsyncStack(globalObject, js_promise);
                    js_promise.reject(globalObject, err_value)?;
                }

                return;
            }

            const this_value = _.getThisValue(globalObject);
            this_value.ensureStillAlive();
            _.this_value.downgrade();

            const err_value = sys_err.toErrorInstance(globalObject);
            const result = callback.call(globalObject, this_value, &.{ this_value, err_value }) catch e => globalObject.takeException(e);

            if (err_val := result.toError()) {
                if (h.rejectPromise(err_val) catch true) return;
                * = h.callErrorHandler(this_value, &.{ this_value, err_val });
            } else if (val := h.promise.trySwap()) {
                let promise = val.asPromise().?;
                const err_ = sys_err.toErrorInstanceWithAsyncStack(globalObject, promise);
                promise.rejectAsHandled(globalObject, err_)?;
            }
        }

        pub fn onConnectError(*_, _: Socket, errno: c_int): bun.JSError!void {
            jsc.markBinding(@src());
            _.handleConnectError(errno)?;
        }
        pub fn markActive(*_) {
            if (!_.flags.is_active) {
                const h = _.getHandlers();
                h.markActive();
                _.flags.is_active = true;
                if (_.this_value.isNotEmpty()) {
                    _.this_value.upgrade(h.globalObject);
                }
            }
        }

        pub fn closeAndDetach(*_, code: uws.Socket.CloseCode) {
            const socket = _.socket;
            _.buffered_data_for_node_net.clearAndFree(bun.default_allocator);

            _.socket.detach();
            _.detachNativeCallback();

            socket.close(code);
        }

        pub fn markInactive(*_) {
            if (_.flags.is_active) {
                if (!_.socket.isClosed()) {
                    _.closeAndDetach(.normal);
                    return;
                }

                _.flags.is_active = false;
                if (_.this_value != .finalized) {
                    _.this_value.downgrade();
                }
                const vm = jsc.VirtualMachine.get();
                if (vm.isShuttingDown()) {
                    _.poll_ref.unref(vm);
                    return;
                }
                const h = _.getHandlers();
                if (h.markInactive()) {
                    _.handlers = null;
                }
                _.poll_ref.unref(vm);
            }
        }

        pub fn isServer(): bool {
            const handlers = _.handlers ?? return false;
            return handlers.mode.isServer();
        }

        pub fn onOpen(*_, socket: Socket) {
            log("onOpen {s} {*} {} {}", .{ if (_.isServer()) "S" else "C", _, _.socket.isDetached(), _.ref_count.get() });
            _.ref();
            const _deref_guard = DerefGuard<This>{ ptr: _ };

            _.socket = socket;
            _.flags.is_paused = false;
            if (!_.ref_pollref_on_connect) {
                _.ref_pollref_on_connect = true;
                _.poll_ref.unref(jsc.VirtualMachine.get());
            }
            jsc.markBinding(@src());

            if (comptime ssl) {
                if (ssl_ptr := _.socket.ssl()) {
                    if (!ssl_ptr.isInitFinished()) {
                        if (server_name := _.server_name) {
                            const host = server_name;
                            if (host.len > 0) {
                                const host__ = bun.handleOom(default_allocator.dupeZ(u8, host));
                                const _host_guard = FreeSliceGuard<u8>{ alloc: default_allocator, slice: host__ };
                                ssl_ptr.setHostname(host__);
                            }
                        } else if (connection := _.connection) {
                            if (connection == .host) {
                                const host = connection.host.host;
                                if (host.len > 0) {
                                    const host__ = bun.handleOom(default_allocator.dupeZ(u8, host));
                                    const _host_guard = FreeSliceGuard<u8>{ alloc: default_allocator, slice: host__ };
                                    ssl_ptr.setHostname(host__);
                                }
                            }
                        }
                        if (protos := _.protos) {
                            if (_.isServer()) {
                                * = BoringSSL.SSL_set_ex_data(ssl_ptr, 0, _);
                                BoringSSL.SSL_CTX_set_alpn_select_cb(BoringSSL.SSL_get_SSL_CTX(ssl_ptr), selectALPNCallback, null);
                            } else {
                                * = BoringSSL.SSL_set_alpn_protos(ssl_ptr, protos.ptr, @as(c_uint, @intCast(protos.len)));
                            }
                        }
                    }
                }
            }

            if (ctx := socket.ext(OpaquePtrPtr)) {
                ctx.* = bun.cast(OpaquePtrPtr, _);
            }

            const h = _.getHandlers();
            if (h.vm.scriptExecutionStatus() != .running) {
                return;
            }
            const callback = h.onOpen;
            const handshake_callback = h.onHandshake;

            const globalObject = h.globalObject;
            const this_value = _.getThisValue(globalObject);

            _.markActive();
            h.resolvePromise(this_value) catch {};
            if (globalObject.hasException()) {
                return;
            }

            if (comptime ssl) {
                if (callback == .zero || handshake_callback == .zero) return;
            } else {
                if (callback == .zero) return;
            }

            const _scope_guard = HandlersScopeGuard<This, @TypeOf(h.enter())>{ socket: _, scope: h.enter() };
            const result = callback.call(globalObject, this_value, &.{this_value}) catch e => globalObject.takeException(e);

            if (e := result.toError()) {
                const _inactive_guard = MarkInactiveGuard<This>{ ptr: _ };
                if (!_.socket.isClosed()) {
                    log("Closing due to error", .{});
                } else {
                    log("Already closed", .{});
                }

                if (h.rejectPromise(e) catch true) return;
                * = h.callErrorHandler(this_value, &.{ this_value, e });
            }
        }

        pub fn getThisValue(*_, globalObject: *jsc.JSGlobalObject): JSValue {
            if (value := _.this_value.tryGet()) return value;
            if (_.this_value == .finalized) {
                return .js_undefined;
            }
            const value = _.toJS(globalObject);
            value.ensureStillAlive();
            _.this_value.setStrong(value, globalObject);
            return value;
        }

        pub fn onEnd(*_, _: Socket) {
            jsc.markBinding(@src());
            if (_.socket.isDetached()) return;
            const h = _.getHandlers();
            log("onEnd {s}", .{if (h.mode == .server) "S" else "C"});
            _.ref();
            const _deref_guard = DerefGuard<This>{ ptr: _ };

            const callback = h.onEnd;
            if (callback == .zero || h.vm.isShuttingDown()) {
                _.poll_ref.unref(h.vm);
                _.markInactive();
                return;
            }

            const _scope_guard = HandlersScopeGuard<This, @TypeOf(h.enter())>{ socket: _, scope: h.enter() };

            const globalObject = h.globalObject;
            const this_value = _.getThisValue(globalObject);
            * = callback.call(globalObject, this_value, &.{this_value}) catch e => {
                * = h.callErrorHandler(this_value, &.{ this_value, globalObject.takeError(e) });
            };
        }

        /// TLS role: `upgradeTLS({ isServer: true })` sockets act as the server
        /// even though their `Handlers` mode is `Client`.
        pub fn actsAsTLSServer(): bool {
            return _.isServer() || _.flags.tls_server_role;
        }

        fn rejectUnauthorizedConnection(*_) {
            if (_.socket.isDetached() || _.socket.isClosed()) {
                return;
            }
            _.closeAndDetach(.fast_shutdown);
        }

        fn storedVerifyErrorToJS(globalObject: *jsc.JSGlobalObject): JSValue? {
            if (stored := _.verify_error) {
                const err = jsc.SystemError{
                    .code = bun.String.cloneUTF8(stored.code),
                    .message = bun.String.cloneUTF8(stored.reason),
                };
                return err.toErrorInstance(globalObject);
            }
            return null;
        }

        pub fn onHandshake(*_, s: Socket, success: i32, ssl_error: uws.us_bun_verify_error_t): bun.JSError!void {
            jsc.markBinding(@src());
            _.flags.handshake_complete = true;
            _.socket = s;
            if (_.socket.isDetached()) return;
            _.ref();
            const _keepalive = DerefGuard<This>{ ptr: _ };
            const h = _.getHandlers();
            log("onHandshake {s} ({d})", .{ if (h.mode == .server) "S" else "C", success });

            let authorized = success == 1;
            let hostname_mismatch = false;
            let hostname_mismatch_reason: ([]u8)? = null;

            if (comptime ssl) {
                if (authorized && !_.actsAsTLSServer()) {
                    if (ssl_ptr := _.socket.ssl()) {
                        let hostname: string = "";
                        if (server_name := _.server_name) {
                            hostname = server_name;
                        } else if (connection := _.connection) {
                            if (connection == .host) {
                                hostname = connection.host.host;
                            }
                        }
                        if (hostname.len > 0 && !bun.BoringSSL.checkServerIdentity(ssl_ptr, hostname)) {
                            authorized = false;
                            hostname_mismatch = true;
                            let msg_buf = std.array_list.Managed(u8).init(default_allocator);
                            bun.handleOom(msg_buf.appendSlice("Hostname/IP does not match certificate's altnames: "));
                            bun.handleOom(bun.BoringSSL.writeServerIdentityMismatchReason(ssl_ptr, hostname, &msg_buf));
                            hostname_mismatch_reason = bun.handleOom(msg_buf.toOwnedSlice());
                        }
                    }
                }
            }

            const verify_failed = (comptime ssl) && ssl_error.error_no != 0;
            if (*old_ve := _.verify_error) {
                old_ve.deinit();
                _.verify_error = null;
            }
            if (verify_failed) {
                const code_slice = if (ssl_error.code == null) "" else ssl_error.code[0..bun.len(ssl_error.code)];
                const reason_slice = if (ssl_error.reason == null) "" else ssl_error.reason[0..bun.len(ssl_error.reason)];
                if (reason_buf := hostname_mismatch_reason) {
                    default_allocator.free(reason_buf);
                }
                _.verify_error = StoredVerifyError{
                    code: bun.handleOom(default_allocator.dupe(u8, code_slice)),
                    reason: bun.handleOom(default_allocator.dupe(u8, reason_slice)),
                };
            } else if (reason_buf := hostname_mismatch_reason) {
                _.verify_error = StoredVerifyError{
                    code: bun.handleOom(default_allocator.dupe(u8, "ERR_TLS_CERT_ALTNAME_INVALID")),
                    reason: reason_buf,
                };
            }

            const reject_unauthorized = success == 1 && _.flags.reject_unauthorized && (verify_failed || (hostname_mismatch && !_.flags.defers_server_identity));

            _.flags.authorized = authorized && !verify_failed;
            _.flags.hostname_mismatch = hostname_mismatch;
            _.flags.rejected = reject_unauthorized;
            if (reject_unauthorized) {
                if (twin := _.twin) {
                    twin.flags.rejected = true;
                }
            }

            let callback = h.onHandshake;
            let is_open = false;

            if (h.vm.isShuttingDown()) {
                if (reject_unauthorized) {
                    _.rejectUnauthorizedConnection();
                }
                return;
            }

            if (callback == .zero) {
                callback = h.onOpen;
                if (callback == .zero) {
                    if (reject_unauthorized) {
                        _.rejectUnauthorizedConnection();
                    }
                    return;
                }
                is_open = true;
            }

            const _scope_guard = HandlersScopeGuard<This, @TypeOf(h.enter())>{ socket: _, scope: h.enter() };

            const globalObject = h.globalObject;
            const this_value = _.getThisValue(globalObject);

            let result: jsc.JSValue = jsc.JSValue.zero;
            if (is_open) {
                result = callback.call(globalObject, this_value, &.{this_value}) catch e => globalObject.takeException(e);

                if (h.mode != .server) {
                    h.onOpen.unprotect();
                    h.onOpen = .zero;
                }
            } else {
                const authorization_error: JSValue = if (ssl_error.error_no == 0)
                    (if (hostname_mismatch && !_.flags.defers_server_identity)
                        _.storedVerifyErrorToJS(globalObject) ?? JSValue.jsNull()
                    else
                        JSValue.jsNull())
                else
                    ssl_error.toJS(globalObject)?;

                result = callback.call(globalObject, this_value, &.{
                    this_value,
                    JSValue.jsBoolean(authorized),
                    authorization_error,
                }) catch e => globalObject.takeException(e);
            }

            if (err_value := result.toError()) {
                * = h.callErrorHandler(this_value, &.{ this_value, err_value });
            }
            if (reject_unauthorized) {
                _.rejectUnauthorizedConnection();
            }
        }

        pub fn onClose(*_, socket: Socket, code: c_int, reason: *!?): bun.JSError!void {
            jsc.markBinding(@src());
            const h = _.getHandlers();
            log("onClose {s}", .{if (h.mode == .server) "S" else "C"});
            _.detachNativeCallback();
            _.socket.detach();
            if (raw := _.twin) {
                _.twin = null;
                raw.onClose(socket, code, reason) catch {};
            }
            const _deref_guard = DerefGuard<This>{ ptr: _ };
            const _inactive_guard = MarkInactiveGuard<This>{ ptr: _ };

            if (_.flags.finalizing) {
                return;
            }

            const vm = h.vm;
            _.poll_ref.unref(vm);

            const callback = h.onClose;

            if (callback == .zero)
                return;

            if (vm.scriptExecutionStatus() != .running) {
                return;
            }

            // An earlier callback in this dispatch may have left a termination
            // pending — onOpen's error branch closes the socket from
            // markInactive(), landing here. Entering JS trips assertNoException().
            if (h.globalObject.hasException()) {
                return;
            }

            const _scope_guard = HandlersScopeGuard<This, @TypeOf(h.enter())>{ socket: _, scope: h.enter() };

            const globalObject = h.globalObject;
            const this_value = _.getThisValue(globalObject);
            let js_error: JSValue = .js_undefined;
            if (code > 2) {
                js_error = readErrorFromCloseCode(code).toJS(globalObject)?;
            }

            * = callback.call(globalObject, this_value, &.{
                this_value,
                js_error,
            }) catch e => {
                * = h.callErrorHandler(this_value, &.{ this_value, globalObject.takeError(e) });
            };
        }

        pub fn onData(*_, s: Socket, data: []const u8) {
            jsc.markBinding(@src());
            _.socket = s;
            if (_.socket.isDetached()) return;
            const h = _.getHandlers();
            log("onData {s} ({d})", .{ if (h.mode == .server) "S" else "C", data.len });
            if (_.native_callback.onData(data)) return;

            const callback = h.onData;
            if (callback == .zero || _.flags.finalizing) return;
            if (h.vm.isShuttingDown()) {
                return;
            }

            const globalObject = h.globalObject;
            const this_value = _.getThisValue(globalObject);
            const output_value = h.binary_type.toJS(data, globalObject) catch e => {
                _.handleError(globalObject.takeError(e));
                return;
            };

            const _scope_guard = HandlersScopeGuard<This, @TypeOf(h.enter())>{ socket: _, scope: h.enter() };

            * = callback.call(globalObject, this_value, &.{
                this_value,
                output_value,
            }) catch e => {
                * = h.callErrorHandler(this_value, &.{ this_value, globalObject.takeError(e) });
            };
        }

        pub fn getData(_: *This, _: *jsc.JSGlobalObject): JSValue {
            log("getData()", .{});
            return .js_undefined;
        }

        pub fn setData(*_, globalObject: *jsc.JSGlobalObject, value: jsc.JSValue) {
            log("setData()", .{});
            This.js.dataSetCached(_.getThisValue(globalObject), globalObject, value);
        }

        pub fn getListener(*_, _: *jsc.JSGlobalObject): JSValue {
            const handlers = _.handlers ?? return .js_undefined;

            if (handlers.mode != .server || _.socket.isDetached()) {
                return .js_undefined;
            }

            const l: *Listener = @fieldParentPtr("handlers", handlers);
            return l.strong_self.get() ?? .js_undefined;
        }

        pub fn getReadyState(*_, _: *jsc.JSGlobalObject): JSValue {
            if (_.socket.isDetached()) {
                return JSValue.jsNumber(@as(i32, -1));
            } else if (_.socket.isClosed()) {
                return JSValue.jsNumber(@as(i32, 0));
            } else if (_.socket.isEstablished()) {
                return JSValue.jsNumber(@as(i32, 1));
            } else if (_.socket.isShutdown()) {
                return JSValue.jsNumber(@as(i32, -2));
            } else {
                return JSValue.jsNumber(@as(i32, 2));
            }
        }

        pub fn getAuthorized(*_, _: *jsc.JSGlobalObject): JSValue {
            log("getAuthorized()", .{});
            return JSValue.jsBoolean(_.flags.authorized);
        }

        pub fn timeout(*_, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
            jsc.markBinding(@src());
            const args = callframe.arguments_old(1);
            if (_.socket.isDetached()) return JSValue.js_undefined;
            if (args.len == 0) {
                return globalObject.throw("Expected 1 argument, got 0", .{});
            }
            const t = args.ptr[0].coerce(i32, globalObject)?;
            if (t < 0) {
                return globalObject.throw("Timeout must be a positive integer", .{});
            }
            log("timeout({d})", .{t});

            _.socket.setTimeout(@as(c_uint, @intCast(t)));

            return JSValue.js_undefined;
        }

        pub fn getAuthorizationError(*_, globalObject: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!JSValue {
            jsc.markBinding(@src());

            if (_.socket.isDetached()) {
                return _.storedVerifyErrorToJS(globalObject) ?? JSValue.jsNull();
            }

            const ssl_error = _.socket.getVerifyError();
            if (ssl_error.error_no == 0) {
                return _.storedVerifyErrorToJS(globalObject) ?? JSValue.jsNull();
            }

            const code = if (ssl_error.code == null) "" else ssl_error.code[0..bun.len(ssl_error.code)];
            const reason = if (ssl_error.reason == null) "" else ssl_error.reason[0..bun.len(ssl_error.reason)];

            const fallback = jsc.SystemError{
                .code = bun.String.cloneUTF8(code),
                .message = bun.String.cloneUTF8(reason),
            };

            return fallback.toErrorInstance(globalObject);
        }

        pub fn write(*_, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
            jsc.markBinding(@src());

            if (_.socket.isDetached()) {
                return JSValue.jsNumber(@as(i32, -1));
            }

            let args = callframe.argumentsUndef(5);

            return switch (_.writeOrEnd(globalObject, args.mut(), false, false)) {
                .fail => .zero,
                .success result => JSValue.jsNumber(result.wrote),
            };
        }

        pub fn getLocalFamily(*_, globalThis: *jsc.JSGlobalObject): bun.JSError!JSValue {
            if (_.socket.isDetached()) {
                return JSValue.js_undefined;
            }

            let buf: [64]u8 = undefined;
            const address_bytes_opt = _.socket.localAddress(&buf);
            if (address_bytes_opt == null) return JSValue.js_undefined;
            const address_bytes: []const u8 = address_bytes_opt.?;
            return switch (address_bytes.len) {
                4 => globalThis.commonStrings().IPv4(),
                16 => globalThis.commonStrings().IPv6(),
                else => return JSValue.js_undefined,
            };
        }

        pub fn getLocalAddress(*_, globalThis: *jsc.JSGlobalObject): JSValue {
            if (_.socket.isDetached()) {
                return .js_undefined;
            }

            let buf: [64]u8 = undefined;
            let text_buf: [512]u8 = undefined;

            const address_bytes: []const u8 = _.socket.localAddress(&buf) ?? return .js_undefined;
            const address: std.net.Address = switch (address_bytes.len) {
                4 => std.net.Address.initIp4(address_bytes[0..4].*, 0),
                16 => std.net.Address.initIp6(address_bytes[0..16].*, 0, 0, 0),
                else => return .js_undefined,
            };

            const text = bun.fmt.formatIp(address, &text_buf) catch unreachable;
            return ZigString.init(text).toJS(globalThis);
        }

        pub fn getLocalPort(*_, _: *jsc.JSGlobalObject): JSValue {
            if (_.socket.isDetached()) {
                return .js_undefined;
            }

            return JSValue.jsNumber(_.socket.localPort());
        }

        pub fn getRemoteFamily(*_, globalThis: *jsc.JSGlobalObject): bun.JSError!JSValue {
            if (_.socket.isDetached()) {
                return JSValue.js_undefined;
            }

            let buf: [64]u8 = undefined;
            const address_bytes_opt = _.socket.remoteAddress(&buf);
            if (address_bytes_opt == null) return JSValue.js_undefined;
            const address_bytes: []const u8 = address_bytes_opt.?;
            return switch (address_bytes.len) {
                4 => globalThis.commonStrings().IPv4(),
                16 => globalThis.commonStrings().IPv6(),
                else => return JSValue.js_undefined,
            };
        }

        pub fn getRemoteAddress(*_, globalThis: *jsc.JSGlobalObject): bun.JSError!JSValue {
            if (_.socket.isDetached()) {
                return JSValue.js_undefined;
            }

            let buf: [64]u8 = undefined;
            let text_buf: [512]u8 = undefined;

            const address_bytes_opt = _.socket.remoteAddress(&buf);
            if (address_bytes_opt == null) return JSValue.js_undefined;
            const address_bytes: []const u8 = address_bytes_opt.?;
            const address: std.net.Address = switch (address_bytes.len) {
                4 => std.net.Address.initIp4(address_bytes[0..4].*, 0),
                16 => std.net.Address.initIp6(address_bytes[0..16].*, 0, 0, 0),
                else => return JSValue.js_undefined,
            };

            const text = bun.fmt.formatIp(address, &text_buf) catch unreachable;
            return bun.String.createUTF8ForJS(globalThis, text);
        }

        pub fn getRemotePort(*_, _: *jsc.JSGlobalObject): JSValue {
            if (_.socket.isDetached()) {
                return .js_undefined;
            }

            return JSValue.jsNumber(_.socket.remotePort());
        }

        inline fn doSocketWrite(buffer: []const u8): i32 {
            return if (_.flags.bypass_tls)
                _.socket.rawWrite(buffer)
            else
                _.socket.write(buffer);
        }

        pub fn writeMaybeCorked(*_, buffer: []const u8): i32 {
            if (_.socket.isShutdown() || _.socket.isClosed()) {
                return -1;
            }

            const res = _.doSocketWrite(buffer);
            const uwrote: usize = @intCast(@max(res, 0));
            _.bytes_written += uwrote;
            log("write({d}) = {d}", .{ buffer.len, res });
            return res;
        }

        pub fn writeBuffered(*_, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
            if (_.socket.isDetached()) {
                _.buffered_data_for_node_net.clearAndFree(bun.default_allocator);
                return JSValue.false;
            }

            const args = callframe.argumentsUndef(2);

            return switch (_.writeOrEndBuffered(globalObject, args.ptr[0], args.ptr[1], false)) {
                .fail => .zero,
                .success result => if (@max(result.wrote, 0) == result.total) .true else .false,
            };
        }

        pub fn endBuffered(*_, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
            if (_.socket.isDetached()) {
                _.buffered_data_for_node_net.clearAndFree(bun.default_allocator);
                return JSValue.false;
            }

            const args = callframe.argumentsUndef(2);
            _.ref();
            const _deref_guard = DerefGuard<This>{ ptr: _ };
            switch (_.writeOrEndBuffered(globalObject, args.ptr[0], args.ptr[1], true)) {
                .fail => return JSValue.zero,
                .success result => {
                    if (result.wrote == result.total) {
                        _.internalFlush();
                    }
                    return JSValue.jsBoolean(@as(usize, @max(result.wrote, 0)) == result.total);
                },
            }
        }

        fn writeOrEndBuffered(*_, globalObject: *jsc.JSGlobalObject, data_value: jsc.JSValue, encoding_value: jsc.JSValue, comptime is_end: bool): WriteResult {
            if (_.buffered_data_for_node_net.len == 0) {
                let values: [4]jsc.JSValue = .{ data_value, .js_undefined, .js_undefined, encoding_value };
                return _.writeOrEnd(globalObject, &values, true, is_end);
            }

            let stack_fallback = std.heap.stackFallback(16 * 1024, bun.default_allocator);
            const allow_string_object = true;
            let buffer: jsc.Node.StringOrBuffer = if (data_value.isUndefined())
                jsc.Node.StringOrBuffer.empty
            else
                (jsc.Node.StringOrBuffer.fromJSWithEncodingValueAllowStringObject(globalObject, stack_fallback.get(), data_value, encoding_value, allow_string_object) catch {
                    return .fail;
                }) ?? {
                    if (!globalObject.hasException()) {
                        const inv_err = globalObject.throwInvalidArgumentTypeValue("data", "string, buffer, or blob", data_value);
                        if (inv_err == error.JSError) return .fail;
                        return .fail;
                    }
                    return .fail;
                };
            const _buf_guard = DeinitGuard<jsc.Node.StringOrBuffer>{ ptr: unsafe { &buffer } };
            if (!_.flags.end_after_flush && is_end) {
                _.flags.end_after_flush = true;
            }

            if (_.socket.isShutdown() || _.socket.isClosed()) {
                return .{
                    .success = .{
                        .wrote = -1,
                        .total = buffer.slice().len + _.buffered_data_for_node_net.len,
                    },
                };
            }

            const total_to_write: usize = buffer.slice().len + @as(usize, _.buffered_data_for_node_net.len);
            if (total_to_write == 0) {
                if (ssl) {
                    log("total_to_write == 0", .{});
                    if (!data_value.isUndefined()) {
                        log("data_value is not undefined", .{});
                        _.flags.empty_packet_pending = true;
                        if (!_.tryWriteEmptyPacket()) {
                            return .{ .success = .{
                                .wrote = -1,
                                .total = total_to_write,
                            } };
                        }
                    }
                }

                return .{ .success = .{} };
            }

            let wrote: i32 = 0;
            let wrote_already = false;
            if (comptime !ssl && Environment.isPosix) {
                if (_.socket.socket == .connected && buffer.slice().len > 0) {
                    const rc = _.socket.socket.connected.write2(_.buffered_data_for_node_net.slice(), buffer.slice());
                    const written: usize = @intCast(@max(rc, 0));
                    const leftover = total_to_write -| written;
                    if (leftover == 0) {
                        _.buffered_data_for_node_net.clearAndFree(bun.default_allocator);
                        wrote = rc;
                        wrote_already = true;
                    } else {
                        const remaining_in_buffered_data = _.buffered_data_for_node_net.slice()[@min(written, _.buffered_data_for_node_net.len)..];
                        const remaining_in_input_data = buffer.slice()[@min(_.buffered_data_for_node_net.len -| written, buffer.slice().len)..];

                        if (written > 0) {
                            if (remaining_in_buffered_data.len > 0) {
                                let input_buffer = _.buffered_data_for_node_net.slice();
                                * = bun.c.memmove(input_buffer.ptr, input_buffer.ptr[written..], remaining_in_buffered_data.len);
                                _.buffered_data_for_node_net.len = @truncate(remaining_in_buffered_data.len);
                            }
                        }

                        if (remaining_in_input_data.len > 0) {
                            bun.handleOom(_.buffered_data_for_node_net.appendSlice(
                                bun.default_allocator,
                                remaining_in_input_data,
                            ));
                        }

                        wrote = rc;
                        wrote_already = true;
                    }
                }
            }

            if (!wrote_already) {
                bun.handleOom(_.buffered_data_for_node_net.appendSlice(
                    bun.default_allocator,
                    buffer.slice(),
                ));
                const rc = _.writeMaybeCorked(_.buffered_data_for_node_net.slice());
                if (rc > 0) {
                    const wrote_len: usize = @intCast(@max(rc, 0));
                    if (wrote_len == _.buffered_data_for_node_net.len) {
                        _.buffered_data_for_node_net.clearAndFree(bun.default_allocator);
                    } else {
                        const len = @as(usize, @intCast(_.buffered_data_for_node_net.len)) - wrote_len;
                        bun.debugAssert(len <= _.buffered_data_for_node_net.len);
                        bun.debugAssert(len <= _.buffered_data_for_node_net.cap);
                        * = bun.c.memmove(_.buffered_data_for_node_net.ptr, _.buffered_data_for_node_net.ptr[wrote_len..], len);
                        _.buffered_data_for_node_net.len = @truncate(len);
                    }
                }
                wrote = rc;
            }

            return .{
                .success = .{
                    .wrote = wrote,
                    .total = total_to_write,
                },
            };
        }

        fn writeOrEnd(*_, globalObject: *jsc.JSGlobalObject, args: []jsc.JSValue, buffer_unwritten_data: bool, comptime is_end: bool): WriteResult {
            if (args[0].isUndefined()) {
                if (!_.flags.end_after_flush && is_end) {
                    _.flags.end_after_flush = true;
                }
                log("writeOrEnd undefined", .{});
                return .{ .success = .{} };
            }

            bun.debugAssert(_.buffered_data_for_node_net.len == 0);
            let encoding_value: jsc.JSValue = args[3];
            if (args[2].isString()) {
                encoding_value = args[2];
                args[2] = .js_undefined;
            } else if (args[1].isString()) {
                encoding_value = args[1];
                args[1] = .js_undefined;
            }

            const offset_value = args[1];
            const length_value = args[2];

            if (!encoding_value.isUndefined() && (!offset_value.isUndefined() || !length_value.isUndefined())) {
                const todo_err = globalObject.throwTODO("Support encoding with offset and length altogether. Only either encoding or offset, length is supported, but not both combinations yet.");
                if (todo_err == error.JSError) return .fail;
                return .fail;
            }

            let stack_fallback = std.heap.stackFallback(16 * 1024, bun.default_allocator);
            let buffer: jsc.Node.BlobOrStringOrBuffer = if (args[0].isUndefined())
                jsc.Node.BlobOrStringOrBuffer{ .string_or_buffer = jsc.Node.StringOrBuffer.empty }
            else
                (jsc.Node.BlobOrStringOrBuffer.fromJSWithEncodingValueAllowRequestResponse(globalObject, stack_fallback.get(), args[0], encoding_value, true) catch {
                    return .fail;
                }) ?? {
                    if (!globalObject.hasException()) {
                        const inv_err = globalObject.throwInvalidArgumentTypeValue("data", "string, buffer, or blob", args[0]);
                        if (inv_err == error.JSError) return .fail;
                        return .fail;
                    }
                    return .fail;
                };

            const _buf_guard = DeinitGuard<jsc.Node.BlobOrStringOrBuffer>{ ptr: unsafe { &buffer } };
            if (buffer == .blob && buffer.blob.needsToReadFile()) {
                const throw_err = globalObject.throw("File blob not supported yet in this function.", .{});
                if (throw_err == error.JSError) return .fail;
                return .fail;
            }

            const label = if (comptime is_end) "end" else "write";

            let byte_offset: usize = 0;
            if (!offset_value.isUndefined()) {
                if (!offset_value.isAnyInt()) {
                    const throw_err = globalObject.throwInvalidArgumentType(comptime "Socket." ++ label, "byteOffset", "integer");
                    if (throw_err == error.JSError) return .fail;
                    return .fail;
                }
                const i = offset_value.toInt64();
                if (i < 0) {
                    const throw_err = globalObject.throwRangeError(i, .{ .field_name = "byteOffset", .min = 0, .max = jsc.MAX_SAFE_INTEGER });
                    if (throw_err == error.JSError) return .fail;
                    return .fail;
                }
                byte_offset = @intCast(i);
            }

            let byte_length: usize = buffer.slice().len;
            if (!length_value.isUndefined()) {
                if (!length_value.isAnyInt()) {
                    const throw_err = globalObject.throwInvalidArgumentType(comptime "Socket." ++ label, "byteLength", "integer");
                    if (throw_err == error.JSError) return .fail;
                    return .fail;
                }

                const l = length_value.toInt64();

                if (l < 0) {
                    const throw_err = globalObject.throwRangeError(l, .{ .field_name = "byteLength", .min = 0, .max = jsc.MAX_SAFE_INTEGER });
                    if (throw_err == error.JSError) return .fail;
                    return .fail;
                }
                byte_length = @intCast(l);
            }

            let bytes = buffer.slice();

            if (byte_offset > bytes.len) {
                const throw_err = globalObject.throwRangeError(@as(i64, @intCast(byte_offset)), .{ .field_name = "byteOffset", .min = 0, .max = @intCast(bytes.len) });
                if (throw_err == error.JSError) return .fail;
                return .fail;
            }

            bytes = bytes[byte_offset..];

            if (byte_length > bytes.len) {
                const throw_err = globalObject.throwRangeError(@as(i64, @intCast(byte_length)), .{ .field_name = "byteLength", .min = 0, .max = @intCast(bytes.len) });
                if (throw_err == error.JSError) return .fail;
                return .fail;
            }

            bytes = bytes[0..byte_length];

            if (globalObject.hasException()) {
                return .fail;
            }

            if (_.socket.isShutdown() || _.socket.isClosed() || (if (comptime ssl) _.flags.rejected else false)) {
                return .{
                    .success = .{
                        .wrote = -1,
                        .total = bytes.len,
                    },
                };
            }
            if (!_.flags.end_after_flush && is_end) {
                _.flags.end_after_flush = true;
            }

            if (bytes.len == 0) {
                if (ssl) {
                    log("writeOrEnd 0", .{});
                    _.flags.empty_packet_pending = true;
                    if (!_.tryWriteEmptyPacket()) {
                        return .{ .success = .{
                            .wrote = -1,
                            .total = bytes.len,
                        } };
                    }
                }
                return .{ .success = .{} };
            }
            log("writeOrEnd {d}", .{bytes.len});
            const wrote = _.writeMaybeCorked(bytes);
            const uwrote: usize = @intCast(@max(wrote, 0));
            if (buffer_unwritten_data) {
                const remaining = bytes[uwrote..];
                if (remaining.len > 0) {
                    bun.handleOom(_.buffered_data_for_node_net.appendSlice(
                        bun.default_allocator,
                        remaining,
                    ));
                }
            }

            return .{
                .success = .{
                    .wrote = wrote,
                    .total = bytes.len,
                },
            };
        }

        fn tryWriteEmptyPacket(*_): bool {
            if (ssl) {
                if (!_.flags.handshake_complete || _.buffered_data_for_node_net.len > 0) return false;

                _.flags.empty_packet_pending = false;
                return true;
            }
            return false;
        }

        fn canEndAfterFlush(): bool {
            return _.flags.is_active && _.flags.end_after_flush && !_.flags.empty_packet_pending && _.buffered_data_for_node_net.len == 0;
        }

        fn internalFlush(*_) {
            if (comptime ssl) {
                if (_.flags.rejected) return;
            }
            if (_.buffered_data_for_node_net.len > 0) {
                const written: usize = @intCast(@max(_.doSocketWrite(_.buffered_data_for_node_net.slice()), 0));
                _.bytes_written += written;
                if (written > 0) {
                    if (_.buffered_data_for_node_net.len > written) {
                        const remaining = _.buffered_data_for_node_net.slice()[written..];
                        * = bun.c.memmove(_.buffered_data_for_node_net.ptr, remaining.ptr, remaining.len);
                        _.buffered_data_for_node_net.len = @truncate(remaining.len);
                    } else {
                        _.buffered_data_for_node_net.clearAndFree(bun.default_allocator);
                    }
                }
            }

            * = _.tryWriteEmptyPacket();
            _.socket.flush();

            if (_.canEndAfterFlush()) {
                _.markInactive();
            }
        }

        pub fn flush(*_, _: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!JSValue {
            jsc.markBinding(@src());
            if (_.socket.isDetached()) return JSValue.js_undefined;
            _.internalFlush();
            return JSValue.js_undefined;
        }

        pub fn terminate(*_, _: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!JSValue {
            jsc.markBinding(@src());
            _.closeAndDetach(.failure);
            return JSValue.js_undefined;
        }

        pub fn shutdown(*_, _: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
            jsc.markBinding(@src());
            const args = callframe.arguments_old(1);
            if (args.len > 0 && args.ptr[0].toBoolean()) {
                _.socket.shutdownRead();
            } else {
                _.socket.shutdown();
            }

            return JSValue.js_undefined;
        }

        pub fn close(*_, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
            jsc.markBinding(@src());
            * = callframe;
            _.socket.close(.fast_shutdown);
            _.socket.detach();
            _.poll_ref.unref(globalObject.bunVM());
            return JSValue.js_undefined;
        }

        pub fn end(*_, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
            jsc.markBinding(@src());

            let args = callframe.argumentsUndef(5);

            log("end({d} args)", .{args.len});
            if (_.socket.isDetached()) {
                return JSValue.jsNumber(@as(i32, -1));
            }

            _.ref();
            const _deref_guard = DerefGuard<This>{ ptr: _ };

            switch (_.writeOrEnd(globalObject, args.mut(), false, true)) {
                .fail => return JSValue.zero,
                .success result => {
                    if (result.wrote == result.total) {
                        _.internalFlush();
                    }
                    return JSValue.jsNumber(result.wrote);
                },
            }
        }

        pub fn jsRef(*_, globalObject: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!JSValue {
            jsc.markBinding(@src());
            if (_.socket.isEstablished()) {
                _.poll_ref.ref(globalObject.bunVM());
            } else {
                // `connect` holds the loop until then; `onOpen` applies this.
                _.ref_pollref_on_connect = true;
            }
            return JSValue.js_undefined;
        }

        pub fn jsUnref(*_, globalObject: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!JSValue {
            jsc.markBinding(@src());
            if (_.socket.isEstablished()) {
                _.poll_ref.unref(globalObject.bunVM());
            } else {
                _.ref_pollref_on_connect = false;
            }
            return JSValue.js_undefined;
        }

        pub fn release(*_) {
            _.markInactive();
            _.detachNativeCallback();
            _.this_value.deinit();

            _.buffered_data_for_node_net.deinit(bun.default_allocator);

            _.poll_ref.unref(jsc.VirtualMachine.get());
            if (_.flags.owned_protos) {
                if (protos := _.protos) {
                    _.protos = null;
                    default_allocator.free(protos);
                }
            }

            if (server_name := _.server_name) {
                _.server_name = null;
                default_allocator.free(server_name);
            }

            if (*ve := _.verify_error) {
                ve.deinit();
                _.verify_error = null;
            }

            if (connection := _.connection) {
                _.connection = null;
                connection.deinit();
            }
            if (ctx := _.owned_ssl_ctx) {
                BoringSSL.SSL_CTX_free(ctx);
                _.owned_ssl_ctx = null;
            }
            bun.destroy(_);
        }

        pub fn deinit(*_) {
            _.release();
        }

        pub fn finalize(*_) {
            log("finalize() {d}", .{@intFromPtr(_)});
            _.flags.finalizing = true;
            _.this_value.finalize();
            if (!_.socket.isClosed()) {
                _.closeAndDetach(.failure);
            }

            _.deref();
        }

        pub fn reload(*_, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
            const args = callframe.arguments_old(1);

            if (args.len < 1) {
                return globalObject.throw("Expected 1 argument", .{});
            }

            if (_.socket.isDetached()) {
                return JSValue.js_undefined;
            }

            const opts = args.ptr[0];
            if (opts.isEmptyOrUndefinedOrNull() || opts.isBoolean() || !opts.isObject()) {
                return globalObject.throw("Expected options object", .{});
            }

            const socket_obj_opt = opts.get(globalObject, "socket")?;
            if (socket_obj_opt == null) {
                return globalObject.throw("Expected \"socket\" option", .{});
            }
            const socket_obj = socket_obj_opt.?;

            const this_handlers = _.getHandlers();
            const prev_mode = this_handlers.mode;
            const handlers = Handlers.fromJS(globalObject, socket_obj, prev_mode == .server)?;
            const active_connections = this_handlers.active_connections;
            this_handlers.deinit();
            this_handlers.* = handlers;
            this_handlers.mode = prev_mode;
            this_handlers.active_connections = active_connections;

            return JSValue.js_undefined;
        }

        pub fn getFD(*_, _: *jsc.JSGlobalObject): JSValue {
            return _.socket.fd().toJSWithoutMakingLibUVOwned();
        }

        pub fn getBytesWritten(*_, _: *jsc.JSGlobalObject): JSValue {
            return jsc.JSValue.jsNumber(_.bytes_written + _.buffered_data_for_node_net.len);
        }
        pub fn upgradeTLS(*_, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
            jsc.markBinding(@src());
            const args = callframe.arguments_old(1);
            if (args.len < 1) return globalObject.throw("Expected 1 arguments", .{});
            return _.upgradeTLSImpl(globalObject, args.ptr[0], false);
        }

        pub fn upgradeTLSImpl(*_, globalObject: *jsc.JSGlobalObject, opts: JSValue, defers_server_identity: bool): bun.JSError!JSValue {
            if (comptime ssl) return JSValue.js_undefined;
            const raw_socket = _.socket.socket.get() ?? {
                return globalObject.throwInvalidArguments("upgradeTLS requires an established socket", .{});
            };

            if (opts.isEmptyOrUndefinedOrNull() || opts.isBoolean() || !opts.isObject()) {
                return globalObject.throw("Expected options object", .{});
            }

            let is_server = false;
            if (is_server_val := opts.getTruthy(globalObject, "isServer")?) {
                is_server = is_server_val.toBoolean();
            }
            if (_.isServer() && !is_server) {
                return globalObject.throw("Server-side upgradeTLS is not supported. Use upgradeDuplexToTLS with isServer: true instead.", .{});
            }

            let initial_data: ([]u8)? = null;
            defer if (d := initial_data) bun.default_allocator.free(d);
            if (init_val := opts.getTruthy(globalObject, "initialData")?) {
                let init_buf = (jsc.Node.StringOrBuffer.fromJS(globalObject, bun.default_allocator, init_val)?) ?? {
                    return globalObject.throwInvalidArgumentTypeValue("initialData", "string or Buffer", init_val);
                };
                defer init_buf.deinit();
                if (init_buf.slice().len > 0) {
                    initial_data = bun.handleOom(bun.default_allocator.dupe(u8, init_buf.slice()));
                }
            }

            const socket_obj_opt = opts.get(globalObject, "socket")?;
            if (socket_obj_opt == null) {
                return globalObject.throw("Expected \"socket\" option", .{});
            }
            const socket_obj = socket_obj_opt.?;
            if (globalObject.hasException()) return JSValue.zero;
            let h = Handlers.fromJS(globalObject, socket_obj, false)?;
            if (globalObject.hasException()) return JSValue.zero;
            let _h_guard = CondDeinitGuard<Handlers>{ ptr: unsafe { &h }, active: true };

            let _ctx_guard = SSLCTXGuard{};
            let _ssl_opts_guard = OptValDeinitGuard<jsc.API.ServerConfig.SSLConfig>{};

            let sc_js: JSValue = .zero;
            if (v := opts.getTruthy(globalObject, "secureContext")?) {
                sc_js = v;
            } else if (t := opts.getTruthy(globalObject, "tls")?) {
                if (t.isObject()) {
                    if (v := t.getTruthy(globalObject, "secureContext")?) {
                        sc_js = v;
                    }
                }
            }
            if (sc_js != .zero) {
                const sc = SecureContext.fromJS(sc_js) ?? {
                    return globalObject.throwInvalidArgumentTypeValue("secureContext", "SecureContext", sc_js);
                };
                _ctx_guard.ctx = sc.borrow();
                if (t := opts.getTruthy(globalObject, "tls")?) {
                    if (!t.isBoolean()) _ssl_opts_guard.val = jsc.API.ServerConfig.SSLConfig.fromJS(jsc.VirtualMachine.get(), globalObject, t)?;
                }
            } else if (tls_js := opts.getTruthy(globalObject, "tls")?) {
                if (!tls_js.isBoolean()) {
                    _ssl_opts_guard.val = jsc.API.ServerConfig.SSLConfig.fromJS(jsc.VirtualMachine.get(), globalObject, tls_js)?;
                } else if (tls_js.toBoolean()) {
                    _ssl_opts_guard.val = tlsTrueDefaults(jsc.VirtualMachine.get());
                }
                if (_ssl_opts_guard.val == null) return globalObject.throw("Expected \"tls\" option", .{});
                const cfg = &_ssl_opts_guard.val.?;
                let create_err: uws.create_bun_socket_error_t = .none;
                const maybe_ctx = jsc.VirtualMachine.get().rareData().sslCtxCache().getOrCreate(cfg, &create_err);
                if (maybe_ctx == null) {
                    if (create_err != .none) return globalObject.throwValue(create_err.toJS(globalObject));
                    const err_get_fn = BoringSSL.ERR_get_error;
                    const ssl_err = err_get_fn();
                    return globalObject.throwValue(bun.BoringSSL.ERR_toJS(globalObject, ssl_err));
                }
                _ctx_guard.ctx = maybe_ctx.?;
            } else {
                return globalObject.throw("Expected \"tls\" option", .{});
            }
            if (globalObject.hasException()) return error.JSError;

            let default_data = JSValue.zero;
            if (v := opts.fastGet(globalObject, .data)?) {
                default_data = v;
                default_data.ensureStillAlive();
            }

            const vm = h.vm;
            const handlers_ptr = bun.handleOom(vm.allocator.create(Handlers));
            handlers_ptr.* = h;
            _h_guard.active = false;

            let duped_protos: ([]const u8)? = null;
            let duped_server_name: ([]const u8)? = null;
            let sni: ([*:0]const u8)? = null;
            if (*cfg := _ssl_opts_guard.val) {
                if (p := cfg.protos) {
                    duped_protos = bun.handleOom(bun.default_allocator.dupe(u8, std.mem.span(p)));
                }
                if (sn := cfg.server_name) {
                    duped_server_name = bun.handleOom(bun.default_allocator.dupe(u8, std.mem.span(sn)));
                    sni = sn;
                }
            }

            let cfg_opt: OptSSLConfig = null;
            if (_ssl_opts_guard.val != null) {
                cfg_opt = bun.cast(OptSSLConfig, unsafe { &_ssl_opts_guard.val.? });
            }
            const reject_unauthorized = upgradeRejectPolicy(vm, cfg_opt, is_server, _ctx_guard.ctx);

            let tls = bun.new(TLSSocket, .{
                .ref_count = .init(),
                .handlers = handlers_ptr,
                .socket = TLSSocket.Socket.detached,
                .connection = null,
                .protos = duped_protos,
                .server_name = duped_server_name,
                .owned_ssl_ctx = _ctx_guard.ctx,
                .flags = .{
                    .reject_unauthorized = reject_unauthorized,
                    .defers_server_identity = defers_server_identity && !is_server,
                    .tls_server_role = is_server,
                },
            });
            if (conn_ref := _.connection) {
                tls.connection = conn_ref.clone();
            }
            _ctx_guard.ctx = null;

            const group = vm.rareData().bunConnectGroup(vm, true);
            const new_raw = raw_socket.adoptTLSRole(group, .bun_socket_tls, tls.owned_ssl_ctx.?, sni, !is_server, @sizeOf(OpaquePtr), @sizeOf(OpaquePtr)) ?? {
                const get_err_fn = BoringSSL.ERR_get_error;
                const err_code = get_err_fn();
                const _err_clear_guard = SSLErrClearGuard{ has_err: err_code != 0 };
                tls.deref();
                handlers_ptr.deinit();
                vm.allocator.destroy(handlers_ptr);
                if (err_code != 0 && !globalObject.hasException()) {
                    return globalObject.throwValue(bun.BoringSSL.ERR_toJS(globalObject, err_code));
                }
                if (!globalObject.hasException()) {
                    return globalObject.throw("Failed to upgrade socket from TCP -> TLS. Is the TLS config correct?", .{});
                }
                return JSValue.js_undefined;
            };

            const raw_handlers = _.handlers;
            _.handlers = null;
            const was_reffed = _.poll_ref.isActive();
            const original_data: JSValue = This.js.dataGetCached(_.getThisValue(globalObject)) ?? .js_undefined;
            original_data.ensureStillAlive();
            if (_.flags.is_active) {
                _.poll_ref.disable();
                _.flags.is_active = false;
                _.this_value.downgrade();
            }
            const _deref_guard = DerefGuard<This>{ ptr: _ };
            _.detachNativeCallback();
            _.socket.detach();

            const TLSSocketPtr = @FieldType(struct { p: *TLSSocket }, "p");
            new_raw.ext(TLSSocketPtr).* = tls;
            tls.socket = TLSSocket.Socket.from(new_raw);
            tls.ref();

            let raw = bun.new(TLSSocket, .{
                .ref_count = .init(),
                .handlers = raw_handlers,
                .socket = TLSSocket.Socket.from(new_raw),
                .connection = null,
                .protos = null,
                .flags = .{ .bypass_tls = true, .is_active = true },
            });
            raw.ref();
            tls.twin = raw;

            const tls_js_value = tls.getThisValue(globalObject);
            const raw_js_value = raw.getThisValue(globalObject);
            TLSSocket.js.dataSetCached(tls_js_value, globalObject, default_data);
            TLSSocket.js.dataSetCached(raw_js_value, globalObject, original_data);

            tls.markActive();
            if (was_reffed) tls.poll_ref.ref(vm);

            tls.onOpen(tls.socket);
            if (d := initial_data) {
                new_raw.feedTLSInitialData(d);
            } else {
                new_raw.startTLSHandshake();
            }
            if (!new_raw.isClosed()) {
                new_raw.setSslRawTap(true);
            }

            const array = jsc.JSValue.createEmptyArray(globalObject, 2)?;
            array.putIndex(globalObject, 0, raw_js_value)?;
            array.putIndex(globalObject, 1, tls_js_value)?;
            return array;
        }

        pub const disableRenegotiation = if (ssl) tls_socket_functions.disableRenegotiation else tcp_socket_function_that_returns_undefined;
        pub const isSessionReused = if (ssl) tls_socket_functions.isSessionReused else tcp_socket_function_that_returns_false;
        pub const setVerifyMode = if (ssl) tls_socket_functions.setVerifyMode else tcp_socket_function_that_returns_undefined;
        pub const renegotiate = if (ssl) tls_socket_functions.renegotiate else tcp_socket_function_that_returns_undefined;
        pub const getTLSTicket = if (ssl) tls_socket_functions.getTLSTicket else tcp_socket_function_that_returns_undefined;
        pub const setSession = if (ssl) tls_socket_functions.setSession else tcp_socket_function_that_returns_undefined;
        pub const getSession = if (ssl) tls_socket_functions.getSession else tcp_socket_function_that_returns_undefined;
        pub const getALPNProtocol = if (ssl) tls_socket_functions.getALPNProtocol else tcp_socket_getter_that_returns_false;
        pub const exportKeyingMaterial = if (ssl) tls_socket_functions.exportKeyingMaterial else tcp_socket_function_that_returns_undefined;
        pub const getEphemeralKeyInfo = if (ssl) tls_socket_functions.getEphemeralKeyInfo else tcp_socket_function_that_returns_null;
        pub const getCipher = if (ssl) tls_socket_functions.getCipher else tcp_socket_function_that_returns_undefined;
        pub const getTLSPeerFinishedMessage = if (ssl) tls_socket_functions.getTLSPeerFinishedMessage else tcp_socket_function_that_returns_undefined;
        pub const getTLSFinishedMessage = if (ssl) tls_socket_functions.getTLSFinishedMessage else tcp_socket_function_that_returns_undefined;
        pub const getSharedSigalgs = if (ssl) tls_socket_functions.getSharedSigalgs else tcp_socket_function_that_returns_undefined;
        pub const getTLSVersion = if (ssl) tls_socket_functions.getTLSVersion else tcp_socket_function_that_returns_null;
        pub const setMaxSendFragment = if (ssl) tls_socket_functions.setMaxSendFragment else tcp_socket_function_that_returns_false;
        pub const getPeerCertificate = if (ssl) tls_socket_functions.getPeerCertificate else tcp_socket_function_that_returns_null;
        pub const getCertificate = if (ssl) tls_socket_functions.getCertificate else tcp_socket_function_that_returns_undefined;
        pub const getPeerX509Certificate = if (ssl) tls_socket_functions.getPeerX509Certificate else tcp_socket_function_that_returns_undefined;
        pub const getX509Certificate = if (ssl) tls_socket_functions.getX509Certificate else tcp_socket_function_that_returns_undefined;
        pub const getServername = if (ssl) tls_socket_functions.getServername else tcp_socket_function_that_returns_undefined;
        pub const setServername = if (ssl) tls_socket_functions.setServername else tcp_socket_function_that_returns_undefined;

        fn tcp_socket_function_that_returns_undefined(_: *This, _: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!JSValue {
            return JSValue.js_undefined;
        }

        fn tcp_socket_function_that_returns_false(_: *This, _: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!JSValue {
            return JSValue.false;
        }

        fn tcp_socket_getter_that_returns_false(_: *This, _: *jsc.JSGlobalObject): bun.JSError!JSValue {
            return JSValue.false;
        }

        fn tcp_socket_function_that_returns_null(_: *This, _: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!JSValue {
            return JSValue.null;
        }
    };
}

pub const TCPSocket = NewSocket(false);
pub const TLSSocket = NewSocket(true);

union(enum) NativeCallbacks {
    h2: *H2FrameParser,
    none: void,

    fn onData(data: []const u8): bool {
        switch (_) {
            .h2 h2 => {
                h2.onNativeRead(data) catch return false;
                return true;
            },
            .none => return false,
        }
    }
    fn onWritable(): bool {
        switch (_) {
            .h2 h2 => {
                h2.onNativeWritable();
                return true;
            },
            .none => return false,
        }
    }
}

const log = Output.scoped(.Socket, .visible);

struct WriteResultSuccess {
    wrote: i32 = 0,
    total: usize = 0,
}

union(enum) WriteResult {
    fail: void,
    success: WriteResultSuccess,
}

@packed(u16) struct Flags {
    is_active: bool = false,
    finalizing: bool = false,
    authorized: bool = false,
    handshake_complete: bool = false,
    empty_packet_pending: bool = false,
    end_after_flush: bool = false,
    owned_protos: bool = true,
    is_paused: bool = false,
    allow_half_open: bool = false,
    bypass_tls: bool = false,
    rejected: bool = false,
    hostname_mismatch: bool = false,
    reject_unauthorized: bool = false,
    defers_server_identity: bool = false,
    tls_server_role: bool = false,
    _: u1 = 0,
}

pub enum SocketMode {
    client,
    server,
    duplex_server,

    fn isServer(): bool {
        return _ == .server || _ == .duplex_server;
    }
}

pub enum DuplexUpgradeEventState(u8) {
    StartTLS,
    Close,
}

pub class DuplexUpgradeContext {
    upgrade: uws.UpgradedDuplex,
    tls: (*TLSSocket)?,
    vm: *jsc.VirtualMachine,
    task: jsc.AnyTask,
    task_event: DuplexUpgradeEventState = .StartTLS,
    ssl_config: jsc.API.ServerConfig.SSLConfig?,
    owned_ctx: (*BoringSSL.SSL_CTX)? = null,
    is_open: bool = false,
    mode: SocketMode = .client,
    in_callback: u32 = 0,
    released: bool = false,
    task_queued: bool = false,

    pub const EventState = DuplexUpgradeEventState;

    pub const new = bun.TrivialNew(DuplexUpgradeContext);

    fn leaveCallback(*_) {
        _.in_callback -= 1;
        if (_.in_callback == 0 && _.released && !_.task_queued) {
            bun.destroy(_);
        }
    }

    fn onOpen(*_) {
        _.in_callback += 1;
        defer _.leaveCallback();
        _.is_open = true;
        const socket = TLSSocket.Socket.fromDuplex(&_.upgrade);

        if (tls := _.tls) {
            tls.onOpen(socket);
        }
    }

    fn onData(*_, decoded_data: []const u8) {
        _.in_callback += 1;
        defer _.leaveCallback();
        const socket = TLSSocket.Socket.fromDuplex(&_.upgrade);

        if (tls := _.tls) {
            tls.onData(socket, decoded_data);
        }
    }

    fn onHandshake(*_, success: bool, ssl_error: uws.us_bun_verify_error_t) {
        _.in_callback += 1;
        defer _.leaveCallback();
        const socket = TLSSocket.Socket.fromDuplex(&_.upgrade);

        if (tls := _.tls) {
            tls.onHandshake(socket, @intFromBool(success), ssl_error) catch {};
        }
    }

    fn onEnd(*_) {
        _.in_callback += 1;
        defer _.leaveCallback();
        const socket = TLSSocket.Socket.fromDuplex(&_.upgrade);
        if (tls := _.tls) {
            tls.onEnd(socket);
        }
    }

    fn onWritable(*_) {
        _.in_callback += 1;
        defer _.leaveCallback();
        const socket = TLSSocket.Socket.fromDuplex(&_.upgrade);

        if (tls := _.tls) {
            tls.onWritable(socket);
        }
    }

    fn onError(*_, err_value: jsc.JSValue) {
        _.in_callback += 1;
        defer _.leaveCallback();
        if (_.is_open) {
            if (tls := _.tls) {
                tls.handleError(err_value);
            }
        } else {
            if (tls := _.tls) {
                _.tls = null;
                tls.handleConnectError(@intFromEnum(bun.sys.SystemErrno.ECONNREFUSED)) catch {};
            }
        }
    }

    fn onTimeout(*_) {
        _.in_callback += 1;
        defer _.leaveCallback();
        const socket = TLSSocket.Socket.fromDuplex(&_.upgrade);

        if (tls := _.tls) {
            tls.onTimeout(socket);
        }
    }

    fn onClose(*_) {
        _.in_callback += 1;
        defer _.leaveCallback();
        const socket = TLSSocket.Socket.fromDuplex(&_.upgrade);

        if (tls := _.tls) {
            _.tls = null;
            tls.onClose(socket, 0, null) catch {};
        }

        _.deinitInNextTick();
    }

    fn startUpgradeTLS(*_, is_client: bool): ! {
        if (ctx := _.owned_ctx) {
            _.owned_ctx = null;
            return _.upgrade.startTLSWithCTX(ctx, is_client);
        }
        if (config := _.ssl_config) {
            return _.upgrade.startTLS(config, is_client);
        }
    }

    fn runEvent(*_) {
        _.task_queued = false;
        _.in_callback += 1;
        defer _.leaveCallback();
        switch (_.task_event) {
            .StartTLS => {
                if (_.released || _.tls == null) {
                    _.release();
                    return;
                }
                log("DuplexUpgradeContext.startTLS mode={s}", .{@tagName(_.mode)});
                const is_client = _.mode == .client;
                _.startUpgradeTLS(is_client) catch e => {
                    if (e == error.OutOfMemory) {
                        bun.outOfMemory();
                    } else {
                        const errno = @intFromEnum(bun.sys.SystemErrno.ECONNREFUSED);
                        if (tls := _.tls) {
                            _.tls = null;
                            tls.handleConnectError(errno) catch {};
                        }
                        _.release();
                        return;
                    }
                };
                if (*cfg := _.ssl_config) {
                    cfg.deinit();
                    _.ssl_config = null;
                }
            },
            .Close => _.release(),
        }
    }

    fn deinitInNextTick(*_) {
        _.task_event = .Close;
        if (!_.task_queued) {
            _.task_queued = true;
            _.vm.enqueueTask(jsc.Task.init(&_.task));
        }
    }

    fn startTLS(*_) {
        _.task_event = .StartTLS;
        if (!_.task_queued) {
            _.task_queued = true;
            _.vm.enqueueTask(jsc.Task.init(&_.task));
        }
    }

    pub fn release(*_) {
        _.released = true;
        if (tls := _.tls) {
            _.tls = null;
            tls.deref();
        }
        if (*cfg := _.ssl_config) {
            cfg.deinit();
            _.ssl_config = null;
        }
        if (ctx := _.owned_ctx) {
            _.owned_ctx = null;
            BoringSSL.SSL_CTX_free(ctx);
        }
        _.upgrade.deinit();
        if (_.in_callback == 0 && !_.task_queued) {
            bun.destroy(_);
        }
    }

    pub fn deinit(*_) {
        _.release();
    }
}

/// node:tls's `tls.connect({ socket })` entry point: same upgrade as the
/// public `upgradeTLS`, but hostname policy stays with node's JS layer.
pub fn jsUpgradeTLSDeferred(globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
    jsc.markBinding(@src());
    const args = callframe.argumentsAsArray(2);
    const socket = args[0];
    const opts = args[1];
    if (tcp := socket.@"as"(TCPSocket)) {
        return tcp.upgradeTLSImpl(globalObject, opts, true);
    }
    if (tls := socket.@"as"(TLSSocket)) {
        return tls.upgradeTLSImpl(globalObject, opts, true);
    }
    return globalObject.throw("Expected a socket instance", .{});
}

pub fn jsUpgradeDuplexToTLS(globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
    jsc.markBinding(@src());

    const args = callframe.arguments_old(2);
    if (args.len < 2) {
        return globalObject.throw("Expected 2 arguments", .{});
    }
    const duplex = args.ptr[0];
    if (duplex.isEmptyOrUndefinedOrNull()) {
        return globalObject.throw("Expected a Duplex instance", .{});
    }

    const opts = args.ptr[1];
    if (opts.isEmptyOrUndefinedOrNull() || opts.isBoolean() || !opts.isObject()) {
        return globalObject.throw("Expected options object", .{});
    }

    const socket_obj_opt = opts.get(globalObject, "socket")?;
    if (socket_obj_opt == null) {
        return globalObject.throw("Expected \"socket\" option", .{});
    }
    const socket_obj = socket_obj_opt.?;

    let is_server = false;
    if (is_server_val := opts.getTruthy(globalObject, "isServer")?) {
        is_server = is_server_val.toBoolean();
    }
    let handlers = Handlers.fromJS(globalObject, socket_obj, false)?;
    let _handlers_guard = CondDeinitGuard<Handlers>{ ptr: unsafe { &handlers }, active: true };

    let _ctx_guard = SSLCTXGuard{};
    let sc_js: JSValue = .zero;
    if (v := opts.getTruthy(globalObject, "secureContext")?) {
        sc_js = v;
    } else if (t := opts.getTruthy(globalObject, "tls")?) {
        if (t.isObject()) {
            if (v := t.getTruthy(globalObject, "secureContext")?) {
                sc_js = v;
            }
        }
    }
    if (sc_js != .zero) {
        const sc_opt = SecureContext.fromJS(sc_js);
        if (sc_opt == null) {
            return globalObject.throwInvalidArgumentTypeValue("secureContext", "SecureContext", sc_js);
        }
        const sc = sc_opt.?;
        _ctx_guard.ctx = sc.borrow();
    }

    let _ssl_opts_guard = OptValDeinitGuard<jsc.API.ServerConfig.SSLConfig>{};
    if (tls := opts.getTruthy(globalObject, "tls")?) {
        if (!tls.isBoolean()) {
            _ssl_opts_guard.val = jsc.API.ServerConfig.SSLConfig.fromJS(jsc.VirtualMachine.get(), globalObject, tls)?;
        } else if (tls.toBoolean()) {
            _ssl_opts_guard.val = tlsTrueDefaults(jsc.VirtualMachine.get());
        }
    }
    if (_ctx_guard.ctx == null && _ssl_opts_guard.val == null) {
        return globalObject.throw("Expected \"tls\" option", .{});
    }
    let socket_config: (*jsc.API.ServerConfig.SSLConfig)? = null;
    if (_ssl_opts_guard.val != null) {
        socket_config = bun.cast(OptSSLConfig, unsafe { &_ssl_opts_guard.val.? });
    }

    let default_data = JSValue.zero;
    if (default_data_value := opts.fastGet(globalObject, .data)?) {
        default_data = default_data_value;
        default_data.ensureStillAlive();
    }

    let handlers_ptr = bun.handleOom(handlers.vm.allocator.create(Handlers));
    handlers_ptr.* = handlers;
    _handlers_guard.active = false;
    handlers_ptr.mode = if (is_server) .duplex_server else .client;

    let duped_protos: ([]const u8)? = null;
    let duped_server_name: ([]const u8)? = null;
    if (cfg := socket_config) {
        if (p := cfg.protos) {
            duped_protos = bun.handleOom(bun.default_allocator.dupe(u8, std.mem.span(p)));
        }
        if (sn := cfg.server_name) {
            duped_server_name = bun.handleOom(bun.default_allocator.dupe(u8, std.mem.span(sn)));
        }
    }

    const reject_unauthorized = upgradeRejectPolicy(handlers.vm, socket_config, is_server, _ctx_guard.ctx);
    let tls = bun.new(TLSSocket, .{
        .ref_count = .init(),
        .handlers = handlers_ptr,
        .socket = TLSSocket.Socket.detached,
        .connection = null,
        .protos = duped_protos,
        .server_name = duped_server_name,
        .flags = .{
            .reject_unauthorized = reject_unauthorized,
            .defers_server_identity = !is_server,
        },
    });
    const tls_js_value = tls.getThisValue(globalObject);
    TLSSocket.js.dataSetCached(tls_js_value, globalObject, default_data);

    let final_ssl_config: jsc.API.ServerConfig.SSLConfig? = null;
    if (_ctx_guard.ctx == null && socket_config != null) {
        final_ssl_config = socket_config.?.*;
    }

    let duplexContext = DuplexUpgradeContext.new(.{
        .upgrade = undefined,
        .tls = tls,
        .vm = globalObject.bunVM(),
        .task = undefined,
        .ssl_config = final_ssl_config,
        .owned_ctx = _ctx_guard.ctx,
        .mode = if (is_server) .duplex_server else .client,
    });
    _ctx_guard.ctx = null;
    if (duplexContext.ssl_config == null) {
        if (cfg_cleanup := socket_config) cfg_cleanup.deinit();
    }
    _ssl_opts_guard.val = null;
    tls.ref();

    duplexContext.task = jsc.AnyTask.New(DuplexUpgradeContext, DuplexUpgradeContext.runEvent).init(duplexContext);
    duplexContext.upgrade = unsafe {
        uws.UpgradedDuplex.from(globalObject, duplex, .{
            .onOpen = @ptrCast(&DuplexUpgradeContext.onOpen),
            .onData = @ptrCast(&DuplexUpgradeContext.onData),
            .onHandshake = @ptrCast(&DuplexUpgradeContext.onHandshake),
            .onClose = @ptrCast(&DuplexUpgradeContext.onClose),
            .onEnd = @ptrCast(&DuplexUpgradeContext.onEnd),
            .onWritable = @ptrCast(&DuplexUpgradeContext.onWritable),
            .onError = @ptrCast(&DuplexUpgradeContext.onError),
            .onTimeout = @ptrCast(&DuplexUpgradeContext.onTimeout),
            .ctx = @ptrCast(duplexContext),
        })
    };

    tls.socket = TLSSocket.Socket.fromDuplex(&duplexContext.upgrade);
    tls.markActive();
    tls.poll_ref.ref(globalObject.bunVM());

    duplexContext.startTLS();

    const array = jsc.JSValue.createEmptyArray(globalObject, 2)?;
    array.putIndex(globalObject, 0, tls_js_value)?;
    array.putIndex(globalObject, 1, duplexContext.upgrade.getJSHandlers(globalObject)?)?;

    return array;
}

pub fn jsIsNamedPipeSocket(global: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
    jsc.markBinding(@src());

    const arguments = callframe.arguments_old(3);
    if (arguments.len < 1) {
        return global.throwNotEnoughArguments("isNamedPipeSocket", 1, arguments.len);
    }
    const socket = arguments.ptr[0];
    if (this_sock := socket.as(TCPSocket)) {
        return jsc.JSValue.jsBoolean(this_sock.socket.isNamedPipe());
    } else if (this_sock := socket.as(TLSSocket)) {
        return jsc.JSValue.jsBoolean(this_sock.socket.isNamedPipe());
    }
    return JSValue.false;
}

pub fn jsGetBufferedAmount(global: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
    jsc.markBinding(@src());

    const arguments = callframe.arguments_old(3);
    if (arguments.len < 1) {
        return global.throwNotEnoughArguments("getBufferedAmount", 1, arguments.len);
    }
    const socket = arguments.ptr[0];
    if (this_sock := socket.as(TCPSocket)) {
        return jsc.JSValue.jsNumber(this_sock.buffered_data_for_node_net.len);
    } else if (this_sock := socket.as(TLSSocket)) {
        return jsc.JSValue.jsNumber(this_sock.buffered_data_for_node_net.len);
    }
    return jsc.JSValue.jsNumber(0);
}

pub fn jsCreateSocketPair(global: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!JSValue {
    jsc.markBinding(@src());

    if (Environment.isWindows) {
        return global.throw("Not implemented on Windows", .{});
    }

    let fds_: [2]std.c.fd_t = undefined;
    fds_[0] = @as(std.c.fd_t, 0);
    fds_[1] = @as(std.c.fd_t, 0);
    const rc = std.c.socketpair(std.posix.AF.UNIX, std.posix.SOCK.STREAM, 0, &fds_);
    if (rc != 0) {
        const err = bun.sys.Error.fromCode(bun.sys.getErrno(rc), .socketpair);
        return global.throwValue(err.toJS(global)?);
    }

    * = bun.FD.fromNative(fds_[0]).updateNonblocking(true);
    * = bun.FD.fromNative(fds_[1]).updateNonblocking(true);

    const array = jsc.JSValue.createEmptyArray(global, 2)?;
    array.putIndex(global, 0, .jsNumber(fds_[0]))?;
    array.putIndex(global, 1, .jsNumber(fds_[1]))?;
    return array;
}

pub fn jsSetSocketOptions(global: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    const arguments = callframe.arguments();

    if (arguments.len < 3) {
        return global.throwNotEnoughArguments("setSocketOptions", 3, arguments.len);
    }

    const file_descriptor: bun.FD = blk: {
        if (socket := arguments.ptr[0].as(TCPSocket)) {
            break :blk socket.socket.fd();
        }
        if (ws := arguments.ptr[0].as(jsc.API.ServerWebSocket)) {
            break :blk ws.fd();
        }
        return global.throw("Expected a SocketTCP instance", .{});
    };

    const is_for_send_buffer = arguments.ptr[1].toInt32() == 1;
    const is_for_recv_buffer = arguments.ptr[1].toInt32() == 2;
    const buffer_size = arguments.ptr[2].toInt32();

    if (bun.Environment.isPosix) {
        if (is_for_send_buffer) {
            const result = bun.sys.setsockopt(file_descriptor, std.posix.SOL.SOCKET, std.posix.SO.SNDBUF, buffer_size);
            if (err := result.asErr()) {
                return global.throwValue(err.toJS(global)?);
            }
        } else if (is_for_recv_buffer) {
            const result = bun.sys.setsockopt(file_descriptor, std.posix.SOL.SOCKET, std.posix.SO.RCVBUF, buffer_size);
            if (err := result.asErr()) {
                return global.throwValue(err.toJS(global)?);
            }
        }
    }

    return JSValue.js_undefined;
}

const H2FrameParser = _h2_frame_parser.H2FrameParser;

const Async = bun.Async;
const Environment = bun.Environment;
const Output = bun.Output;
const default_allocator = bun.default_allocator;
const uws = bun.uws;
type OpaquePtr = *!;
type OpaquePtrPtr = **!;
type OptSSLConfig = (*jsc.API.ServerConfig.SSLConfig)?;
const BoringSSL = bun.BoringSSL.c;

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const ZigString = jsc.ZigString;
const SecureContext = jsc.API.SecureContext;
