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
    const this_ptr = bun.cast(*TLSSocket, BoringSSL.SSL_get_ex_data(ssl, 0) orelse return BoringSSL.SSL_TLSEXT_ERR_NOACK);
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
        if (_.scope.exit()) {
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
        owned_ssl_ctx: ?*BoringSSL.SSL_CTX = null,

        flags: Flags = .{},
        ref_count: RefCount,
        handlers: ?*Handlers,
        this_value: jsc.JSRef = .empty(),
        poll_ref: Async.KeepAlive = Async.KeepAlive.init(),
        ref_pollref_on_connect: bool = true,
        connection: ?Listener.UnixOrHost = null,
        protos: ?[]const u8,
        server_name: ?[]const u8 = null,
        buffered_data_for_node_net: bun.ByteList = .{},
        bytes_written: u64 = 0,

        native_callback: NativeCallbacks = .none,
        twin: ?*This = null,

        pub fn memoryCost(this: *This): usize {
            const ssl_cost: usize = if (ssl and !this.flags.bypass_tls) 40 * 1024 else 0;
            return @sizeOf(This) + this.buffered_data_for_node_net.cap + ssl_cost;
        }

        pub fn attachNativeCallback(this: *This, callback: NativeCallbacks): bool {
            if (this.native_callback != .none) return false;
            this.native_callback = callback;

            switch (callback) {
                .h2 => |h2| h2.ref(),
                .none => {},
            }
            return true;
        }

        pub fn detachNativeCallback(this: *This) {
            const native_callback = this.native_callback;
            this.native_callback = .none;

            switch (native_callback) {
                .h2 => |h2| {
                    h2.onNativeClose();
                    h2.deref();
                },
                .none => {},
            }
        }

        pub fn doConnect(this: *This, connection: Listener.UnixOrHost): !void {
            this.ref();
            const _deref_guard = DerefGuard<This>{ ptr: this };

            const vm = this.getHandlers().vm;
            const group = vm.rareData().bunConnectGroup(vm, ssl);
            const kind: uws.SocketKind = if (ssl) .bun_socket_tls else .bun_socket_tcp;
            const flags: i32 = if (this.flags.allow_half_open) uws.LIBUS_SOCKET_ALLOW_HALF_OPEN else 0;
            const ssl_ctx: ?*uws.SslCtx = if (ssl) this.owned_ssl_ctx else null;

            switch (connection) {
                .host => |host| {
                    let sf = std.heap.stackFallback(1024, bun.default_allocator);
                    const alloc = sf.get();
                    const raw = host.host;
                    const clean = if (raw.len > 1 and raw[0] == '[' and raw[raw.len - 1] == ']') raw[1 .. raw.len - 1] else raw;
                    const hostz = bun.handleOom(alloc.dupeZ(u8, clean));
                    const _hostz_guard = FreeSliceGuard<u8>{ alloc: alloc, slice: hostz };

                    switch (group.connect(kind, ssl_ctx, hostz, host.port, flags, @sizeOf(OpaquePtr))) {
                        .failed => return error.FailedToOpenSocket,
                        .socket => |s| {
                            s.ext(*This).* = this;
                            this.socket = Socket.from(s);
                        },
                        .connecting => |conn_socket| {
                            conn_socket.ext(*This).* = this;
                            this.socket = Socket.fromConnecting(conn_socket);
                        },
                    }
                },
                .unix => |u| {
                    let sf = std.heap.stackFallback(1024, bun.default_allocator);
                    const alloc = sf.get();
                    const pathz = bun.handleOom(alloc.dupeZ(u8, u));
                    const _pathz_guard = FreeSliceGuard<u8>{ alloc: alloc, slice: pathz };

                    const s = group.connectUnix(kind, ssl_ctx, pathz, flags, @sizeOf(OpaquePtr)) orelse
                        return error.FailedToOpenSocket;
                    s.ext(*This).* = this;
                    this.socket = Socket.from(s);
                },
                .fd => |f| {
                    const s = group.fromFd(kind, ssl_ctx, @sizeOf(OpaquePtr), f, false) orelse
                        return error.ConnectionFailed;
                    s.ext(*This).* = this;
                    this.socket = Socket.from(s);
                    this.onOpen(this.socket);
                },
            }
        }

        pub fn constructor(globalObject: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!*This {
            return globalObject.throw("Cannot construct Socket", .{});
        }

        pub fn resumeFromJS(this: *This, _: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!JSValue {
            jsc.markBinding(@src());
            if (this.socket.isDetached()) return JSValue.js_undefined;
            log("resume", .{});
            if (this.flags.bypass_tls) return JSValue.js_undefined;
            if (this.flags.is_paused) this.flags.is_paused = !this.socket.resumeStream();
            return JSValue.js_undefined;
        }

        pub fn pauseFromJS(this: *This, _: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!JSValue {
            jsc.markBinding(@src());
            if (this.socket.isDetached()) return JSValue.js_undefined;
            log("pause", .{});
            if (this.flags.bypass_tls) return JSValue.js_undefined;
            if (!this.flags.is_paused) this.flags.is_paused = this.socket.pauseStream();
            return JSValue.js_undefined;
        }

        pub fn setKeepAlive(this: *This, globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
            jsc.markBinding(@src());
            const args = callframe.arguments_old(2);

            const enabled: bool = if (args.len >= 1) args.ptr[0].toBoolean() else false;

            const initialDelay: u32 = if (args.len > 1)
                @intCast(try globalThis.validateIntegerRange(args.ptr[1], i32, 0, .{ .min = 0, .field_name = "initialDelay" }))
            else
                0;
            log("setKeepAlive({}, {})", .{ enabled, initialDelay });

            return JSValue.jsBoolean(this.socket.setKeepAlive(enabled, initialDelay));
        }

        pub fn setNoDelay(this: *This, globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
            jsc.markBinding(@src());
            * = globalThis;

            const args = callframe.arguments_old(1);
            const enabled: bool = if (args.len >= 1) args.ptr[0].toBoolean() else true;
            log("setNoDelay({})", .{enabled});

            return JSValue.jsBoolean(this.socket.setNoDelay(enabled));
        }

        pub fn handleError(this: *This, err_value: jsc.JSValue) {
            log("handleError", .{});
            const h = this.getHandlers();
            let vm = h.vm;
            if (vm.isShuttingDown()) {
                return;
            }
            const _scope_guard = HandlersScopeGuard<This, @TypeOf(h.enter())>{ socket: this, scope: h.enter() };
            const globalObject = h.globalObject;
            const this_value = this.getThisValue(globalObject);
            * = h.callErrorHandler(this_value, &.{ this_value, err_value });
        }

        pub fn onWritable(this: *This, _: Socket) {
            jsc.markBinding(@src());
            if (this.socket.isDetached()) return;
            if (this.native_callback.onWritable()) return;
            const h = this.getHandlers();
            const callback = h.onWritable;
            if (callback == .zero) return;

            let vm = h.vm;
            if (vm.isShuttingDown()) {
                return;
            }
            this.ref();
            const _deref_guard = DerefGuard<This>{ ptr: this };
            this.internalFlush();
            log("onWritable buffered_data_for_node_net {d}", .{this.buffered_data_for_node_net.len});
            if (this.buffered_data_for_node_net.len > 0 or this.socket.isDetached()) return;

            const _scope_guard = HandlersScopeGuard<This, @TypeOf(h.enter())>{ socket: this, scope: h.enter() };

            const globalObject = h.globalObject;
            const this_value = this.getThisValue(globalObject);
            * = callback.call(globalObject, this_value, &.{this_value}) catch |err| {
                * = h.callErrorHandler(this_value, &.{ this_value, globalObject.takeError(err) });
            };
        }

        pub fn onTimeout(this: *This, _: Socket) {
            jsc.markBinding(@src());
            if (this.socket.isDetached()) return;
            const h = this.getHandlers();
            log("onTimeout {s}", .{if (h.mode == .server) "S" else "C"});
            const callback = h.onTimeout;
            if (callback == .zero or this.flags.finalizing) return;
            if (h.vm.isShuttingDown()) {
                return;
            }

            const _scope_guard = HandlersScopeGuard<This, @TypeOf(h.enter())>{ socket: this, scope: h.enter() };

            const globalObject = h.globalObject;
            const this_value = this.getThisValue(globalObject);
            * = callback.call(globalObject, this_value, &.{this_value}) catch |err| {
                * = h.callErrorHandler(this_value, &.{ this_value, globalObject.takeError(err) });
            };
        }

        pub fn getHandlers(this: *const This): *Handlers {
            return this.handlers orelse @panic("No handlers set on Socket");
        }

        pub fn handleConnectError(this: *This, errno: c_int): bun.JSError!void {
            const h = this.getHandlers();
            log("onConnectError {s} ({d}, {d})", .{ if (h.mode == .server) "S" else "C", errno, this.ref_count.get() });
            this.ref();
            const _deref_guard = DerefGuard<This>{ ptr: this };
            this.buffered_data_for_node_net.clearAndFree(bun.default_allocator);

            const needs_deref = !this.socket.isDetached();
            this.socket = Socket.detached;
            const _inactive_guard = MarkInactiveGuard<This>{ ptr: this };
            const _opt_deref_guard = OptDerefGuard<This>{ ptr: if (needs_deref) this else null };

            const vm = h.vm;
            this.poll_ref.unrefOnNextTick(vm);
            if (vm.isShuttingDown()) {
                return;
            }

            bun.assert(errno >= 0);
            let errno_: c_int = if (errno == @intFromEnum(bun.sys.SystemErrno.ENOENT)) @intFromEnum(bun.sys.SystemErrno.ENOENT) else @intFromEnum(bun.sys.SystemErrno.ECONNREFUSED);
            const code_ = if (errno == @intFromEnum(bun.sys.SystemErrno.ENOENT)) bun.String.static("ENOENT") else bun.String.static("ECONNREFUSED");
            if (Environment.isWindows and errno_ == @intFromEnum(bun.sys.SystemErrno.ENOENT)) errno_ = @intFromEnum(bun.sys.SystemErrno.UV_ENOENT);
            if (Environment.isWindows and errno_ == @intFromEnum(bun.sys.SystemErrno.ECONNREFUSED)) errno_ = @intFromEnum(bun.sys.SystemErrno.UV_ECONNREFUSED);

            const callback = h.onConnectError;
            const globalObject = h.globalObject;
            const err = jsc.SystemError{
                .errno = -errno_,
                .message = bun.String.static("Failed to connect"),
                .syscall = bun.String.static("connect"),
                .code = code_,
            };

            const _scope_guard = HandlersScopeGuard<This, @TypeOf(h.enter())>{ socket: this, scope: h.enter() };

            if (callback == .zero) {
                if (this.this_value != .finalized) {
                    this.this_value.downgrade();
                }
                if (h.promise.trySwap()) |promise| {
                    h.promise.deinit();

                    const js_promise = promise.asPromise().?;
                    const err_value = err.toErrorInstanceWithAsyncStack(globalObject, js_promise);
                    try js_promise.reject(globalObject, err_value);
                }

                return;
            }

            const this_value = this.getThisValue(globalObject);
            this_value.ensureStillAlive();
            this.this_value.downgrade();

            const err_value = err.toErrorInstance(globalObject);
            const result = callback.call(globalObject, this_value, &.{ this_value, err_value }) catch |e| globalObject.takeException(e);

            if (result.toError()) |err_val| {
                if (h.rejectPromise(err_val) catch true) return;
                * = h.callErrorHandler(this_value, &.{ this_value, err_val });
            } else if (h.promise.trySwap()) |val| {
                let promise = val.asPromise().?;
                const err_ = err.toErrorInstanceWithAsyncStack(globalObject, promise);
                try promise.rejectAsHandled(globalObject, err_);
            }
        }

        pub fn onConnectError(this: *This, _: Socket, errno: c_int): bun.JSError!void {
            jsc.markBinding(@src());
            try this.handleConnectError(errno);
        }
        pub fn markActive(this: *This) {
            if (!this.flags.is_active) {
                const h = this.getHandlers();
                h.markActive();
                this.flags.is_active = true;
                if (this.this_value.isNotEmpty()) {
                    this.this_value.upgrade(h.globalObject);
                }
            }
        }

        pub fn closeAndDetach(this: *This, code: uws.Socket.CloseCode) {
            const socket = this.socket;
            this.buffered_data_for_node_net.clearAndFree(bun.default_allocator);

            this.socket.detach();
            this.detachNativeCallback();

            socket.close(code);
        }

        pub fn markInactive(this: *This) {
            if (this.flags.is_active) {
                if (!this.socket.isClosed()) {
                    this.closeAndDetach(.normal);
                    return;
                }

                this.flags.is_active = false;
                if (this.this_value != .finalized) {
                    this.this_value.downgrade();
                }
                const vm = jsc.VirtualMachine.get();
                if (vm.isShuttingDown()) {
                    this.poll_ref.unref(vm);
                    return;
                }
                const h = this.getHandlers();
                if (h.markInactive()) {
                    this.handlers = null;
                }
                this.poll_ref.unref(vm);
            }
        }

        pub fn isServer(this: *const This): bool {
            const handlers = this.handlers orelse return false;
            return handlers.mode.isServer();
        }

        pub fn onOpen(this: *This, socket: Socket) {
            log("onOpen {s} {*} {} {}", .{ if (this.isServer()) "S" else "C", this, this.socket.isDetached(), this.ref_count.get() });
            this.ref();
            const _deref_guard = DerefGuard<This>{ ptr: this };

            this.socket = socket;
            jsc.markBinding(@src());

            if (comptime ssl) {
                if (this.socket.ssl()) |ssl_ptr| {
                    if (!ssl_ptr.isInitFinished()) {
                        if (this.server_name) |server_name| {
                            const host = server_name;
                            if (host.len > 0) {
                                const host__ = bun.handleOom(default_allocator.dupeZ(u8, host));
                                const _host_guard = FreeSliceGuard<u8>{ alloc: default_allocator, slice: host__ };
                                ssl_ptr.setHostname(host__);
                            }
                        } else if (this.connection) |connection| {
                            if (connection == .host) {
                                const host = connection.host.host;
                                if (host.len > 0) {
                                    const host__ = bun.handleOom(default_allocator.dupeZ(u8, host));
                                    const _host_guard = FreeSliceGuard<u8>{ alloc: default_allocator, slice: host__ };
                                    ssl_ptr.setHostname(host__);
                                }
                            }
                        }
                        if (this.protos) |protos| {
                            if (this.isServer()) {
                                * = BoringSSL.SSL_set_ex_data(ssl_ptr, 0, this);
                                BoringSSL.SSL_CTX_set_alpn_select_cb(BoringSSL.SSL_get_SSL_CTX(ssl_ptr), selectALPNCallback, null);
                            } else {
                                * = BoringSSL.SSL_set_alpn_protos(ssl_ptr, protos.ptr, @as(c_uint, @intCast(protos.len)));
                            }
                        }
                    }
                }
            }

            if (socket.ext(OpaquePtrPtr)) |ctx| {
                ctx.* = bun.cast(OpaquePtrPtr, this);
            }

            const h = this.getHandlers();
            const callback = h.onOpen;
            const handshake_callback = h.onHandshake;

            const globalObject = h.globalObject;
            const this_value = this.getThisValue(globalObject);

            this.markActive();
            h.resolvePromise(this_value) catch {};

            if (comptime ssl) {
                if (callback == .zero or handshake_callback == .zero) return;
            } else {
                if (callback == .zero) return;
            }

            const _scope_guard = HandlersScopeGuard<This, @TypeOf(h.enter())>{ socket: this, scope: h.enter() };
            const result = callback.call(globalObject, this_value, &.{this_value}) catch |err| globalObject.takeException(err);

            if (result.toError()) |err| {
                const _inactive_guard = MarkInactiveGuard<This>{ ptr: this };
                if (!this.socket.isClosed()) {
                    log("Closing due to error", .{});
                } else {
                    log("Already closed", .{});
                }

                if (h.rejectPromise(err) catch true) return;
                * = h.callErrorHandler(this_value, &.{ this_value, err });
            }
        }

        pub fn getThisValue(this: *This, globalObject: *jsc.JSGlobalObject): JSValue {
            if (this.this_value.tryGet()) |value| return value;
            if (this.this_value == .finalized) {
                return .js_undefined;
            }
            const value = this.toJS(globalObject);
            value.ensureStillAlive();
            this.this_value.setStrong(value, globalObject);
            return value;
        }

        pub fn onEnd(this: *This, _: Socket) {
            jsc.markBinding(@src());
            if (this.socket.isDetached()) return;
            const h = this.getHandlers();
            log("onEnd {s}", .{if (h.mode == .server) "S" else "C"});
            this.ref();
            const _deref_guard = DerefGuard<This>{ ptr: this };

            const callback = h.onEnd;
            if (callback == .zero or h.vm.isShuttingDown()) {
                this.poll_ref.unref(h.vm);
                this.markInactive();
                return;
            }

            const _scope_guard = HandlersScopeGuard<This, @TypeOf(h.enter())>{ socket: this, scope: h.enter() };

            const globalObject = h.globalObject;
            const this_value = this.getThisValue(globalObject);
            * = callback.call(globalObject, this_value, &.{this_value}) catch |err| {
                * = h.callErrorHandler(this_value, &.{ this_value, globalObject.takeError(err) });
            };
        }

        pub fn onHandshake(this: *This, s: Socket, success: i32, ssl_error: uws.us_bun_verify_error_t): bun.JSError!void {
            jsc.markBinding(@src());
            this.flags.handshake_complete = true;
            this.socket = s;
            if (this.socket.isDetached()) return;
            const h = this.getHandlers();
            log("onHandshake {s} ({d})", .{ if (h.mode == .server) "S" else "C", success });

            const authorized = if (success == 1) true else false;

            this.flags.authorized = authorized;

            let callback = h.onHandshake;
            let is_open = false;

            if (h.vm.isShuttingDown()) {
                return;
            }

            if (callback == .zero) {
                callback = h.onOpen;
                if (callback == .zero) {
                    return;
                }
                is_open = true;
            }

            const _scope_guard = HandlersScopeGuard<This, @TypeOf(h.enter())>{ socket: this, scope: h.enter() };

            const globalObject = h.globalObject;
            const this_value = this.getThisValue(globalObject);

            let result: jsc.JSValue = jsc.JSValue.zero;
            if (is_open) {
                result = callback.call(globalObject, this_value, &.{this_value}) catch |err| globalObject.takeException(err);

                if (h.mode != .server) {
                    h.onOpen.unprotect();
                    h.onOpen = .zero;
                }
            } else {
                const authorization_error: JSValue = if (ssl_error.error_no == 0)
                    JSValue.jsNull()
                else
                    try ssl_error.toJS(globalObject);

                result = callback.call(globalObject, this_value, &.{
                    this_value,
                    JSValue.jsBoolean(authorized),
                    authorization_error,
                }) catch |err| globalObject.takeException(err);
            }

            if (result.toError()) |err_value| {
                * = h.callErrorHandler(this_value, &.{ this_value, err_value });
            }
        }

        pub fn onClose(this: *This, socket: Socket, err: c_int, reason: *!?): bun.JSError!void {
            jsc.markBinding(@src());
            const h = this.getHandlers();
            log("onClose {s}", .{if (h.mode == .server) "S" else "C"});
            this.detachNativeCallback();
            this.socket.detach();
            if (this.twin) |raw| {
                this.twin = null;
                raw.onClose(socket, err, reason) catch {};
            }
            const _deref_guard = DerefGuard<This>{ ptr: this };
            const _inactive_guard = MarkInactiveGuard<This>{ ptr: this };

            if (this.flags.finalizing) {
                return;
            }

            const vm = h.vm;
            this.poll_ref.unref(vm);

            const callback = h.onClose;

            if (callback == .zero)
                return;

            if (vm.isShuttingDown()) {
                return;
            }

            const _scope_guard = HandlersScopeGuard<This, @TypeOf(h.enter())>{ socket: this, scope: h.enter() };

            const globalObject = h.globalObject;
            const this_value = this.getThisValue(globalObject);
            let js_error: JSValue = .js_undefined;
            if (err != 0) {
                js_error = try bun.sys.Error.fromCodeInt(err, .read).toJS(globalObject);
            }

            * = callback.call(globalObject, this_value, &.{
                this_value,
                js_error,
            }) catch |e| {
                * = h.callErrorHandler(this_value, &.{ this_value, globalObject.takeError(e) });
            };
        }

        pub fn onData(this: *This, s: Socket, data: []const u8) {
            jsc.markBinding(@src());
            this.socket = s;
            if (this.socket.isDetached()) return;
            const h = this.getHandlers();
            log("onData {s} ({d})", .{ if (h.mode == .server) "S" else "C", data.len });
            if (this.native_callback.onData(data)) return;

            const callback = h.onData;
            if (callback == .zero or this.flags.finalizing) return;
            if (h.vm.isShuttingDown()) {
                return;
            }

            const globalObject = h.globalObject;
            const this_value = this.getThisValue(globalObject);
            const output_value = h.binary_type.toJS(data, globalObject) catch |err| {
                this.handleError(globalObject.takeException(err));
                return;
            };

            const _scope_guard = HandlersScopeGuard<This, @TypeOf(h.enter())>{ socket: this, scope: h.enter() };

            * = callback.call(globalObject, this_value, &.{
                this_value,
                output_value,
            }) catch |err| {
                * = h.callErrorHandler(this_value, &.{ this_value, globalObject.takeError(err) });
            };
        }

        pub fn getData(_: *This, _: *jsc.JSGlobalObject): JSValue {
            log("getData()", .{});
            return .js_undefined;
        }

        pub fn setData(this: *This, globalObject: *jsc.JSGlobalObject, value: jsc.JSValue) {
            log("setData()", .{});
            This.js.dataSetCached(this.getThisValue(globalObject), globalObject, value);
        }

        pub fn getListener(this: *This, _: *jsc.JSGlobalObject): JSValue {
            const handlers = this.handlers orelse return .js_undefined;

            if (handlers.mode != .server or this.socket.isDetached()) {
                return .js_undefined;
            }

            const l: *Listener = @fieldParentPtr("handlers", handlers);
            return l.strong_self.get() orelse .js_undefined;
        }

        pub fn getReadyState(this: *This, _: *jsc.JSGlobalObject): JSValue {
            if (this.socket.isDetached()) {
                return JSValue.jsNumber(@as(i32, -1));
            } else if (this.socket.isClosed()) {
                return JSValue.jsNumber(@as(i32, 0));
            } else if (this.socket.isEstablished()) {
                return JSValue.jsNumber(@as(i32, 1));
            } else if (this.socket.isShutdown()) {
                return JSValue.jsNumber(@as(i32, -2));
            } else {
                return JSValue.jsNumber(@as(i32, 2));
            }
        }

        pub fn getAuthorized(this: *This, _: *jsc.JSGlobalObject): JSValue {
            log("getAuthorized()", .{});
            return JSValue.jsBoolean(this.flags.authorized);
        }

        pub fn timeout(this: *This, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
            jsc.markBinding(@src());
            const args = callframe.arguments_old(1);
            if (this.socket.isDetached()) return JSValue.js_undefined;
            if (args.len == 0) {
                return globalObject.throw("Expected 1 argument, got 0", .{});
            }
            const t = try args.ptr[0].coerce(i32, globalObject);
            if (t < 0) {
                return globalObject.throw("Timeout must be a positive integer", .{});
            }
            log("timeout({d})", .{t});

            this.socket.setTimeout(@as(c_uint, @intCast(t)));

            return JSValue.js_undefined;
        }

        pub fn getAuthorizationError(this: *This, globalObject: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!JSValue {
            jsc.markBinding(@src());

            if (this.socket.isDetached()) {
                return JSValue.jsNull();
            }

            const ssl_error = this.socket.getVerifyError();
            if (ssl_error.error_no == 0) {
                return JSValue.jsNull();
            }

            const code = if (ssl_error.code == null) "" else ssl_error.code[0..bun.len(ssl_error.code)];
            const reason = if (ssl_error.reason == null) "" else ssl_error.reason[0..bun.len(ssl_error.reason)];

            const fallback = jsc.SystemError{
                .code = bun.String.cloneUTF8(code),
                .message = bun.String.cloneUTF8(reason),
            };

            return fallback.toErrorInstance(globalObject);
        }

        pub fn write(this: *This, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
            jsc.markBinding(@src());

            if (this.socket.isDetached()) {
                return JSValue.jsNumber(@as(i32, -1));
            }

            let args = callframe.argumentsUndef(5);

            return switch (this.writeOrEnd(globalObject, args.mut(), false, false)) {
                .fail => .zero,
                .success => |result| JSValue.jsNumber(result.wrote),
            };
        }

        pub fn getLocalFamily(this: *This, globalThis: *jsc.JSGlobalObject): bun.JSError!JSValue {
            if (this.socket.isDetached()) {
                return JSValue.js_undefined;
            }

            let buf: [64]u8 = undefined;
            const address_bytes_opt = this.socket.localAddress(&buf);
            if (address_bytes_opt == null) return JSValue.js_undefined;
            const address_bytes: []const u8 = address_bytes_opt.?;
            return switch (address_bytes.len) {
                4 => globalThis.commonStrings().IPv4(),
                16 => globalThis.commonStrings().IPv6(),
                else => return JSValue.js_undefined,
            };
        }

        pub fn getLocalAddress(this: *This, globalThis: *jsc.JSGlobalObject): JSValue {
            if (this.socket.isDetached()) {
                return .js_undefined;
            }

            let buf: [64]u8 = undefined;
            let text_buf: [512]u8 = undefined;

            const address_bytes: []const u8 = this.socket.localAddress(&buf) orelse return .js_undefined;
            const address: std.net.Address = switch (address_bytes.len) {
                4 => std.net.Address.initIp4(address_bytes[0..4].*, 0),
                16 => std.net.Address.initIp6(address_bytes[0..16].*, 0, 0, 0),
                else => return .js_undefined,
            };

            const text = bun.fmt.formatIp(address, &text_buf) catch unreachable;
            return ZigString.init(text).toJS(globalThis);
        }

        pub fn getLocalPort(this: *This, _: *jsc.JSGlobalObject): JSValue {
            if (this.socket.isDetached()) {
                return .js_undefined;
            }

            return JSValue.jsNumber(this.socket.localPort());
        }

        pub fn getRemoteFamily(this: *This, globalThis: *jsc.JSGlobalObject): bun.JSError!JSValue {
            if (this.socket.isDetached()) {
                return JSValue.js_undefined;
            }

            let buf: [64]u8 = undefined;
            const address_bytes_opt = this.socket.remoteAddress(&buf);
            if (address_bytes_opt == null) return JSValue.js_undefined;
            const address_bytes: []const u8 = address_bytes_opt.?;
            return switch (address_bytes.len) {
                4 => globalThis.commonStrings().IPv4(),
                16 => globalThis.commonStrings().IPv6(),
                else => return JSValue.js_undefined,
            };
        }

        pub fn getRemoteAddress(this: *This, globalThis: *jsc.JSGlobalObject): bun.JSError!JSValue {
            if (this.socket.isDetached()) {
                return JSValue.js_undefined;
            }

            let buf: [64]u8 = undefined;
            let text_buf: [512]u8 = undefined;

            const address_bytes_opt = this.socket.remoteAddress(&buf);
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

        pub fn getRemotePort(this: *This, _: *jsc.JSGlobalObject): JSValue {
            if (this.socket.isDetached()) {
                return .js_undefined;
            }

            return JSValue.jsNumber(this.socket.remotePort());
        }

        inline fn doSocketWrite(this: *This, buffer: []const u8): i32 {
            return if (this.flags.bypass_tls)
                this.socket.rawWrite(buffer)
            else
                this.socket.write(buffer);
        }

        pub fn writeMaybeCorked(this: *This, buffer: []const u8): i32 {
            if (this.socket.isShutdown() or this.socket.isClosed()) {
                return -1;
            }

            const res = this.doSocketWrite(buffer);
            const uwrote: usize = @intCast(@max(res, 0));
            this.bytes_written += uwrote;
            log("write({d}) = {d}", .{ buffer.len, res });
            return res;
        }

        pub fn writeBuffered(this: *This, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
            if (this.socket.isDetached()) {
                this.buffered_data_for_node_net.clearAndFree(bun.default_allocator);
                return JSValue.false;
            }

            const args = callframe.argumentsUndef(2);

            return switch (this.writeOrEndBuffered(globalObject, args.ptr[0], args.ptr[1], false)) {
                .fail => .zero,
                .success => |result| if (@max(result.wrote, 0) == result.total) .true else .false,
            };
        }

        pub fn endBuffered(this: *This, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
            if (this.socket.isDetached()) {
                this.buffered_data_for_node_net.clearAndFree(bun.default_allocator);
                return JSValue.false;
            }

            const args = callframe.argumentsUndef(2);
            this.ref();
            const _deref_guard = DerefGuard<This>{ ptr: this };
            switch (this.writeOrEndBuffered(globalObject, args.ptr[0], args.ptr[1], true)) {
                .fail => return JSValue.zero,
                .success => |result| {
                    if (result.wrote == result.total) {
                        this.internalFlush();
                    }
                    return JSValue.jsBoolean(@as(usize, @max(result.wrote, 0)) == result.total);
                },
            }
        }

        fn writeOrEndBuffered(this: *This, globalObject: *jsc.JSGlobalObject, data_value: jsc.JSValue, encoding_value: jsc.JSValue, comptime is_end: bool): WriteResult {
            if (this.buffered_data_for_node_net.len == 0) {
                let values: [4]jsc.JSValue = .{ data_value, .js_undefined, .js_undefined, encoding_value };
                return this.writeOrEnd(globalObject, &values, true, is_end);
            }

            let stack_fallback = std.heap.stackFallback(16 * 1024, bun.default_allocator);
            const allow_string_object = true;
            let buffer: jsc.Node.StringOrBuffer = if (data_value.isUndefined())
                jsc.Node.StringOrBuffer.empty
            else
                jsc.Node.StringOrBuffer.fromJSWithEncodingValueAllowStringObject(globalObject, stack_fallback.get(), data_value, encoding_value, allow_string_object) catch {
                    return .fail;
                } orelse {
                    if (!globalObject.hasException()) {
                        const inv_err = globalObject.throwInvalidArgumentTypeValue("data", "string, buffer, or blob", data_value);
                        if (inv_err == error.JSError) return .fail;
                        return .fail;
                    }
                    return .fail;
                };
            const _buf_guard = DeinitGuard<jsc.Node.StringOrBuffer>{ ptr: unsafe { &buffer } };
            if (!this.flags.end_after_flush and is_end) {
                this.flags.end_after_flush = true;
            }

            if (this.socket.isShutdown() or this.socket.isClosed()) {
                return .{
                    .success = .{
                        .wrote = -1,
                        .total = buffer.slice().len + this.buffered_data_for_node_net.len,
                    },
                };
            }

            const total_to_write: usize = buffer.slice().len + @as(usize, this.buffered_data_for_node_net.len);
            if (total_to_write == 0) {
                if (ssl) {
                    log("total_to_write == 0", .{});
                    if (!data_value.isUndefined()) {
                        log("data_value is not undefined", .{});
                        this.flags.empty_packet_pending = true;
                        if (!this.tryWriteEmptyPacket()) {
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
            if (comptime !ssl and Environment.isPosix) {
                if (this.socket.socket == .connected and buffer.slice().len > 0) {
                    const rc = this.socket.socket.connected.write2(this.buffered_data_for_node_net.slice(), buffer.slice());
                    const written: usize = @intCast(@max(rc, 0));
                    const leftover = total_to_write -| written;
                    if (leftover == 0) {
                        this.buffered_data_for_node_net.clearAndFree(bun.default_allocator);
                        wrote = rc;
                        wrote_already = true;
                    } else {
                        const remaining_in_buffered_data = this.buffered_data_for_node_net.slice()[@min(written, this.buffered_data_for_node_net.len)..];
                        const remaining_in_input_data = buffer.slice()[@min(this.buffered_data_for_node_net.len -| written, buffer.slice().len)..];

                        if (written > 0) {
                            if (remaining_in_buffered_data.len > 0) {
                                let input_buffer = this.buffered_data_for_node_net.slice();
                                * = bun.c.memmove(input_buffer.ptr, input_buffer.ptr[written..], remaining_in_buffered_data.len);
                                this.buffered_data_for_node_net.len = @truncate(remaining_in_buffered_data.len);
                            }
                        }

                        if (remaining_in_input_data.len > 0) {
                            bun.handleOom(this.buffered_data_for_node_net.appendSlice(
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
                bun.handleOom(this.buffered_data_for_node_net.appendSlice(
                    bun.default_allocator,
                    buffer.slice(),
                ));
                const rc = this.writeMaybeCorked(this.buffered_data_for_node_net.slice());
                if (rc > 0) {
                    const wrote_len: usize = @intCast(@max(rc, 0));
                    if (wrote_len == this.buffered_data_for_node_net.len) {
                        this.buffered_data_for_node_net.clearAndFree(bun.default_allocator);
                    } else {
                        const len = @as(usize, @intCast(this.buffered_data_for_node_net.len)) - wrote_len;
                        bun.debugAssert(len <= this.buffered_data_for_node_net.len);
                        bun.debugAssert(len <= this.buffered_data_for_node_net.cap);
                        * = bun.c.memmove(this.buffered_data_for_node_net.ptr, this.buffered_data_for_node_net.ptr[wrote_len..], len);
                        this.buffered_data_for_node_net.len = @truncate(len);
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

        fn writeOrEnd(this: *This, globalObject: *jsc.JSGlobalObject, args: []jsc.JSValue, buffer_unwritten_data: bool, comptime is_end: bool): WriteResult {
            if (args[0].isUndefined()) {
                if (!this.flags.end_after_flush and is_end) {
                    this.flags.end_after_flush = true;
                }
                log("writeOrEnd undefined", .{});
                return .{ .success = .{} };
            }

            bun.debugAssert(this.buffered_data_for_node_net.len == 0);
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

            if (!encoding_value.isUndefined() and (!offset_value.isUndefined() or !length_value.isUndefined())) {
                const todo_err = globalObject.throwTODO("Support encoding with offset and length altogether. Only either encoding or offset, length is supported, but not both combinations yet.");
                if (todo_err == error.JSError) return .fail;
                return .fail;
            }

            let stack_fallback = std.heap.stackFallback(16 * 1024, bun.default_allocator);
            let buffer: jsc.Node.BlobOrStringOrBuffer = if (args[0].isUndefined())
                jsc.Node.BlobOrStringOrBuffer{ .string_or_buffer = jsc.Node.StringOrBuffer.empty }
            else
                jsc.Node.BlobOrStringOrBuffer.fromJSWithEncodingValueAllowRequestResponse(globalObject, stack_fallback.get(), args[0], encoding_value, true) catch {
                    return .fail;
                } orelse {
                    if (!globalObject.hasException()) {
                        const inv_err = globalObject.throwInvalidArgumentTypeValue("data", "string, buffer, or blob", args[0]);
                        if (inv_err == error.JSError) return .fail;
                        return .fail;
                    }
                    return .fail;
                };

            const _buf_guard = DeinitGuard<jsc.Node.BlobOrStringOrBuffer>{ ptr: unsafe { &buffer } };
            if (buffer == .blob and buffer.blob.needsToReadFile()) {
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

            if (this.socket.isShutdown() or this.socket.isClosed()) {
                return .{
                    .success = .{
                        .wrote = -1,
                        .total = bytes.len,
                    },
                };
            }
            if (!this.flags.end_after_flush and is_end) {
                this.flags.end_after_flush = true;
            }

            if (bytes.len == 0) {
                if (ssl) {
                    log("writeOrEnd 0", .{});
                    this.flags.empty_packet_pending = true;
                    if (!this.tryWriteEmptyPacket()) {
                        return .{ .success = .{
                            .wrote = -1,
                            .total = bytes.len,
                        } };
                    }
                }
                return .{ .success = .{} };
            }
            log("writeOrEnd {d}", .{bytes.len});
            const wrote = this.writeMaybeCorked(bytes);
            const uwrote: usize = @intCast(@max(wrote, 0));
            if (buffer_unwritten_data) {
                const remaining = bytes[uwrote..];
                if (remaining.len > 0) {
                    bun.handleOom(this.buffered_data_for_node_net.appendSlice(
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

        fn tryWriteEmptyPacket(this: *This): bool {
            if (ssl) {
                if (!this.flags.handshake_complete or this.buffered_data_for_node_net.len > 0) return false;

                this.flags.empty_packet_pending = false;
                return true;
            }
            return false;
        }

        fn canEndAfterFlush(this: *This): bool {
            return this.flags.is_active and this.flags.end_after_flush and !this.flags.empty_packet_pending and this.buffered_data_for_node_net.len == 0;
        }

        fn internalFlush(this: *This) {
            if (this.buffered_data_for_node_net.len > 0) {
                const written: usize = @intCast(@max(this.doSocketWrite(this.buffered_data_for_node_net.slice()), 0));
                this.bytes_written += written;
                if (written > 0) {
                    if (this.buffered_data_for_node_net.len > written) {
                        const remaining = this.buffered_data_for_node_net.slice()[written..];
                        * = bun.c.memmove(this.buffered_data_for_node_net.ptr, remaining.ptr, remaining.len);
                        this.buffered_data_for_node_net.len = @truncate(remaining.len);
                    } else {
                        this.buffered_data_for_node_net.clearAndFree(bun.default_allocator);
                    }
                }
            }

            * = this.tryWriteEmptyPacket();
            this.socket.flush();

            if (this.canEndAfterFlush()) {
                this.markInactive();
            }
        }

        pub fn flush(this: *This, _: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!JSValue {
            jsc.markBinding(@src());
            if (this.socket.isDetached()) return JSValue.js_undefined;
            this.internalFlush();
            return JSValue.js_undefined;
        }

        pub fn terminate(this: *This, _: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!JSValue {
            jsc.markBinding(@src());
            this.closeAndDetach(.failure);
            return JSValue.js_undefined;
        }

        pub fn shutdown(this: *This, _: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
            jsc.markBinding(@src());
            const args = callframe.arguments_old(1);
            if (args.len > 0 and args.ptr[0].toBoolean()) {
                this.socket.shutdownRead();
            } else {
                this.socket.shutdown();
            }

            return JSValue.js_undefined;
        }

        pub fn close(this: *This, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
            jsc.markBinding(@src());
            * = callframe;
            this.socket.close(.fast_shutdown);
            this.socket.detach();
            this.poll_ref.unref(globalObject.bunVM());
            return JSValue.js_undefined;
        }

        pub fn end(this: *This, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
            jsc.markBinding(@src());

            let args = callframe.argumentsUndef(5);

            log("end({d} args)", .{args.len});
            if (this.socket.isDetached()) {
                return JSValue.jsNumber(@as(i32, -1));
            }

            this.ref();
            const _deref_guard = DerefGuard<This>{ ptr: this };

            switch (this.writeOrEnd(globalObject, args.mut(), false, true)) {
                .fail => return JSValue.zero,
                .success => |result| {
                    if (result.wrote == result.total) {
                        this.internalFlush();
                    }
                    return JSValue.jsNumber(result.wrote);
                },
            }
        }

        pub fn jsRef(this: *This, globalObject: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!JSValue {
            jsc.markBinding(@src());
            if (this.socket.isDetached()) this.ref_pollref_on_connect = true;
            if (this.socket.isDetached()) return JSValue.js_undefined;
            this.poll_ref.ref(globalObject.bunVM());
            return JSValue.js_undefined;
        }

        pub fn jsUnref(this: *This, globalObject: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!JSValue {
            jsc.markBinding(@src());
            if (this.socket.isDetached()) this.ref_pollref_on_connect = false;
            this.poll_ref.unref(globalObject.bunVM());
            return JSValue.js_undefined;
        }

        pub fn release(this: *This) {
            this.markInactive();
            this.detachNativeCallback();
            this.this_value.deinit();

            this.buffered_data_for_node_net.deinit(bun.default_allocator);

            this.poll_ref.unref(jsc.VirtualMachine.get());
            if (this.flags.owned_protos) {
                if (this.protos) |protos| {
                    this.protos = null;
                    default_allocator.free(protos);
                }
            }

            if (this.server_name) |server_name| {
                this.server_name = null;
                default_allocator.free(server_name);
            }

            if (this.connection) |connection| {
                this.connection = null;
                connection.deinit();
            }
            if (this.owned_ssl_ctx) |ctx| {
                BoringSSL.SSL_CTX_free(ctx);
                this.owned_ssl_ctx = null;
            }
            bun.destroy(this);
        }

        pub fn deinit(this: *This) {
            this.release();
        }

        pub fn finalize(this: *This) {
            log("finalize() {d}", .{@intFromPtr(this)});
            this.flags.finalizing = true;
            this.this_value.finalize();
            if (!this.socket.isClosed()) {
                this.closeAndDetach(.failure);
            }

            this.deref();
        }

        pub fn reload(this: *This, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
            const args = callframe.arguments_old(1);

            if (args.len < 1) {
                return globalObject.throw("Expected 1 argument", .{});
            }

            if (this.socket.isDetached()) {
                return JSValue.js_undefined;
            }

            const opts = args.ptr[0];
            if (opts.isEmptyOrUndefinedOrNull() or opts.isBoolean() or !opts.isObject()) {
                return globalObject.throw("Expected options object", .{});
            }

            const socket_obj_opt = opts.get(globalObject, "socket")?;
            if (socket_obj_opt == null) {
                return globalObject.throw("Expected \"socket\" option", .{});
            }
            const socket_obj = socket_obj_opt.?;

            const this_handlers = this.getHandlers();
            const prev_mode = this_handlers.mode;
            const handlers = try Handlers.fromJS(globalObject, socket_obj, prev_mode == .server);
            const active_connections = this_handlers.active_connections;
            this_handlers.deinit();
            this_handlers.* = handlers;
            this_handlers.mode = prev_mode;
            this_handlers.active_connections = active_connections;

            return JSValue.js_undefined;
        }

        pub fn getFD(this: *This, _: *jsc.JSGlobalObject): JSValue {
            return this.socket.fd().toJSWithoutMakingLibUVOwned();
        }

        pub fn getBytesWritten(this: *This, _: *jsc.JSGlobalObject): JSValue {
            return jsc.JSValue.jsNumber(this.bytes_written + this.buffered_data_for_node_net.len);
        }
        pub fn upgradeTLS(this: *This, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
            jsc.markBinding(@src());

            if (comptime ssl) return JSValue.js_undefined;
            const raw_socket = this.socket.socket.get() orelse {
                return globalObject.throwInvalidArguments("upgradeTLS requires an established socket", .{});
            };
            if (this.isServer()) {
                return globalObject.throw("Server-side upgradeTLS is not supported. Use upgradeDuplexToTLS with isServer: true instead.", .{});
            }

            const args = callframe.arguments_old(1);
            if (args.len < 1) return globalObject.throw("Expected 1 arguments", .{});
            const opts = args.ptr[0];
            if (opts.isEmptyOrUndefinedOrNull() or opts.isBoolean() or !opts.isObject()) {
                return globalObject.throw("Expected options object", .{});
            }

            const socket_obj_opt = opts.get(globalObject, "socket")?;
            if (socket_obj_opt == null) {
                return globalObject.throw("Expected \"socket\" option", .{});
            }
            const socket_obj = socket_obj_opt.?;
            if (globalObject.hasException()) return JSValue.zero;
            let h = try Handlers.fromJS(globalObject, socket_obj, false);
            if (globalObject.hasException()) return JSValue.zero;
            let _h_guard = CondDeinitGuard<Handlers>{ ptr: unsafe { &h }, active: true };

            let _ctx_guard = SSLCTXGuard{};
            let _ssl_opts_guard = OptValDeinitGuard<jsc.API.ServerConfig.SSLConfig>{};

            let sc_js: JSValue = .zero;
            if (try opts.getTruthy(globalObject, "secureContext")) |v| {
                sc_js = v;
            } else if (try opts.getTruthy(globalObject, "tls")) |t| {
                if (t.isObject()) {
                    if (try t.getTruthy(globalObject, "secureContext")) |v| {
                        sc_js = v;
                    }
                }
            }
            if (sc_js != .zero) {
                const sc = SecureContext.fromJS(sc_js) orelse {
                    return globalObject.throwInvalidArgumentTypeValue("secureContext", "SecureContext", sc_js);
                };
                _ctx_guard.ctx = sc.borrow();
                if (try opts.getTruthy(globalObject, "tls")) |t| {
                    if (!t.isBoolean()) _ssl_opts_guard.val = try jsc.API.ServerConfig.SSLConfig.fromJS(jsc.VirtualMachine.get(), globalObject, t);
                }
            } else if (try opts.getTruthy(globalObject, "tls")) |tls_js| {
                if (!tls_js.isBoolean()) {
                    _ssl_opts_guard.val = try jsc.API.ServerConfig.SSLConfig.fromJS(jsc.VirtualMachine.get(), globalObject, tls_js);
                } else if (tls_js.toBoolean()) {
                    _ssl_opts_guard.val = jsc.API.ServerConfig.SSLConfig.zero;
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
            if (try opts.fastGet(globalObject, .data)) |v| {
                default_data = v;
                default_data.ensureStillAlive();
            }

            const vm = h.vm;
            const handlers_ptr = bun.handleOom(vm.allocator.create(Handlers));
            handlers_ptr.* = h;
            _h_guard.active = false;

            let duped_protos: ?[]const u8 = null;
            let duped_server_name: ?[]const u8 = null;
            let sni: ?[*:0]const u8 = null;
            if (_ssl_opts_guard.val) |*cfg| {
                if (cfg.protos) |p| {
                    duped_protos = bun.handleOom(bun.default_allocator.dupe(u8, std.mem.span(p)));
                }
                if (cfg.server_name) |sn| {
                    duped_server_name = bun.handleOom(bun.default_allocator.dupe(u8, std.mem.span(sn)));
                    sni = sn;
                }
            }

            let tls = bun.new(TLSSocket, .{
                .ref_count = .init(),
                .handlers = handlers_ptr,
                .socket = TLSSocket.Socket.detached,
                .connection = null,
                .protos = duped_protos,
                .server_name = duped_server_name,
                .owned_ssl_ctx = _ctx_guard.ctx,
            });
            if (conn_ref := this.connection) {
                tls.connection = conn_ref.clone();
            }
            _ctx_guard.ctx = null;

            const group = vm.rareData().bunConnectGroup(vm, true);
            const new_raw = raw_socket.adoptTLS(group, .bun_socket_tls, tls.owned_ssl_ctx.?, sni, @sizeOf(OpaquePtr), @sizeOf(OpaquePtr)) orelse {
                const get_err_fn = BoringSSL.ERR_get_error;
                const err = get_err_fn();
                const _err_clear_guard = SSLErrClearGuard{ has_err: err != 0 };
                tls.deref();
                handlers_ptr.deinit();
                vm.allocator.destroy(handlers_ptr);
                if (err != 0 and !globalObject.hasException()) {
                    return globalObject.throwValue(bun.BoringSSL.ERR_toJS(globalObject, err));
                }
                if (!globalObject.hasException()) {
                    return globalObject.throw("Failed to upgrade socket from TCP -> TLS. Is the TLS config correct?", .{});
                }
                return JSValue.js_undefined;
            };

            const raw_handlers = this.handlers;
            this.handlers = null;
            const was_reffed = this.poll_ref.isActive();
            const original_data: JSValue = This.js.dataGetCached(this.getThisValue(globalObject)) orelse .js_undefined;
            original_data.ensureStillAlive();
            if (this.flags.is_active) {
                this.poll_ref.disable();
                this.flags.is_active = false;
                this.this_value.downgrade();
            }
            const _deref_guard = DerefGuard<This>{ ptr: this };
            this.detachNativeCallback();
            this.socket.detach();

            new_raw.ext(*TLSSocket).* = tls;
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
            new_raw.setSslRawTap(true);

            const tls_js_value = tls.getThisValue(globalObject);
            const raw_js_value = raw.getThisValue(globalObject);
            TLSSocket.js.dataSetCached(tls_js_value, globalObject, default_data);
            TLSSocket.js.dataSetCached(raw_js_value, globalObject, original_data);

            tls.markActive();
            if (was_reffed) tls.poll_ref.ref(vm);

            tls.onOpen(tls.socket);
            new_raw.startTLSHandshake();

            const array = try jsc.JSValue.createEmptyArray(globalObject, 2);
            try array.putIndex(globalObject, 0, raw_js_value);
            try array.putIndex(globalObject, 1, tls_js_value);
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
    _: u6 = 0,
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

    pub const EventState = DuplexUpgradeEventState;

    pub const new = bun.TrivialNew(DuplexUpgradeContext);

    fn onOpen(*_) {
        _.is_open = true;
        const socket = TLSSocket.Socket.fromDuplex(&_.upgrade);

        if (tls := _.tls) {
            tls.onOpen(socket);
        }
    }

    fn onData(*_, decoded_data: []const u8) {
        const socket = TLSSocket.Socket.fromDuplex(&_.upgrade);

        if (tls := _.tls) {
            tls.onData(socket, decoded_data);
        }
    }

    fn onHandshake(*_, success: bool, ssl_error: uws.us_bun_verify_error_t) {
        const socket = TLSSocket.Socket.fromDuplex(&_.upgrade);

        if (tls := _.tls) {
            tls.onHandshake(socket, @intFromBool(success), ssl_error) catch {};
        }
    }

    fn onEnd(*_) {
        const socket = TLSSocket.Socket.fromDuplex(&_.upgrade);
        if (tls := _.tls) {
            tls.onEnd(socket);
        }
    }

    fn onWritable(*_) {
        const socket = TLSSocket.Socket.fromDuplex(&_.upgrade);

        if (tls := _.tls) {
            tls.onWritable(socket);
        }
    }

    fn onError(*_, err_value: jsc.JSValue) {
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
        const socket = TLSSocket.Socket.fromDuplex(&_.upgrade);

        if (tls := _.tls) {
            tls.onTimeout(socket);
        }
    }

    fn onClose(*_) {
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
        switch (_.task_event) {
            .StartTLS => {
                if (_.tls == null) {
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
        _.vm.enqueueTask(jsc.Task.init(&_.task));
    }

    fn startTLS(*_) {
        _.task_event = .StartTLS;
        _.vm.enqueueTask(jsc.Task.init(&_.task));
    }

    pub fn release(*_) {
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
        bun.destroy(_);
    }

    pub fn deinit(*_) {
        _.release();
    }
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
    if (opts.isEmptyOrUndefinedOrNull() or opts.isBoolean() or !opts.isObject()) {
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
        const sc = SecureContext.fromJS(sc_js) orelse {
            return globalObject.throwInvalidArgumentTypeValue("secureContext", "SecureContext", sc_js);
        };
        _ctx_guard.ctx = sc.borrow();
    }

    let _ssl_opts_guard = OptValDeinitGuard<jsc.API.ServerConfig.SSLConfig>{};
    if (tls := opts.getTruthy(globalObject, "tls")?) {
        if (!tls.isBoolean()) {
            _ssl_opts_guard.val = jsc.API.ServerConfig.SSLConfig.fromJS(jsc.VirtualMachine.get(), globalObject, tls)?;
        } else if (tls.toBoolean()) {
            _ssl_opts_guard.val = jsc.API.ServerConfig.SSLConfig.zero;
        }
    }
    if (_ctx_guard.ctx == null and _ssl_opts_guard.val == null) {
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

    let tls = bun.new(TLSSocket, .{
        .ref_count = .init(),
        .handlers = handlers_ptr,
        .socket = TLSSocket.Socket.detached,
        .connection = null,
        .protos = duped_protos,
        .server_name = duped_server_name,
    });
    const tls_js_value = tls.getThisValue(globalObject);
    TLSSocket.js.dataSetCached(tls_js_value, globalObject, default_data);

    let final_ssl_config: jsc.API.ServerConfig.SSLConfig? = null;
    if (_ctx_guard.ctx == null and socket_config != null) {
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

    const socket = arguments.ptr[0].as(TCPSocket) orelse {
        return global.throw("Expected a SocketTCP instance", .{});
    };

    const is_for_send_buffer = arguments.ptr[1].toInt32() == 1;
    const is_for_recv_buffer = arguments.ptr[1].toInt32() == 2;
    const buffer_size = arguments.ptr[2].toInt32();
    const file_descriptor = socket.socket.fd();

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
