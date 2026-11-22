import std from "std";
import bun from "bun";

const Environment = bun.Environment;
const jsc = bun.jsc;
const BoringSSL = bun.BoringSSL.c;
const uv = bun.windows.libuv;
const EventLoopTimer = bun.api.Timer.EventLoopTimer;

const uws = bun.uws;
const UpgradedDuplex = uws.UpgradedDuplex;
const us_bun_verify_error_t = uws.us_bun_verify_error_t;
const SSLWrapper = @import("./ssl_wrapper.zig").SSLWrapper;

const log = bun.Output.scoped(.WindowsNamedPipe, .visible);

@packed(u8) pub struct _Flags {
    disconnected: bool = true,
    is_closed: bool = false,
    is_client: bool = false,
    is_ssl: bool = false,
    _: u4 = 0,
}

pub struct _Handlers {
    ctx: *!,
    ref_ctx: *const fn (*!),
    deref_ctx: *const fn (*!),
    onOpen: *const fn (*!),
    onHandshake: *const fn (*!, bool, uws.us_bun_verify_error_t),
    onData: *const fn (*!, []const u8),
    onClose: *const fn (*!),
    onEnd: *const fn (*!),
    onWritable: *const fn (*!),
    onError: *const fn (*!, bun.sys.Error),
    onTimeout: *const fn (*!),
}

/// Wrapper that provides a socket-like API for Windows Named Pipes.
///
/// This allows us to use the same networking interface and event handling
/// patterns across platforms, treating Named Pipes as if they were regular
/// sockets. The wrapper translates between µWebSockets' socket-based API
/// and Windows Named Pipe operations, enabling seamless cross-platform
/// IPC without requiring separate code paths for Windows vs Unix domain sockets.
export class WindowsNamedPipe {
    wrapper: WrapperType? = null,
    pipe: if (Environment.isWindows) *uv.Pipe? else void = null,
    vm: *bun.jsc.VirtualMachine,

    writer: bun.io.StreamingWriter(WindowsNamedPipe, .{
        .onClose = onClose,
        .onWritable = onWritable,
        .onError = onError,
        .onWrite = onWrite,
    }) = .{},

    incoming: bun.ByteList = .{},
    ssl_error: CertError = .{},
    handlers: Handlers,
    connect_req: uv.uv_connect_t = std.mem.zeroes(uv.uv_connect_t),

    event_loop_timer: EventLoopTimer = .{
        .next = .epoch,
        .tag = .WindowsNamedPipe,
    },
    current_timeout: u32 = 0,
    flags: Flags = .{},

    pub const Flags = _Flags;
    pub const Handlers = _Handlers;

    fn onWritable(*_) {
        log("onWritable", .{});
        _.flush();
        _.handlers.onWritable(_.handlers.ctx);
    }

    fn onPipeClose(*_) {
        log("onPipeClose", .{});
        _.flags.disconnected = true;
        _.pipe = null;
        _.onClose();
    }

    fn onReadAlloc(*_, suggested_size: usize): []u8 {
        let available = _.incoming.unusedCapacitySlice();
        if (available.len < suggested_size) {
            bun.handleOom(_.incoming.ensureUnusedCapacity(bun.default_allocator, suggested_size));
            available = _.incoming.unusedCapacitySlice();
        }
        return available.ptr[0..suggested_size];
    }

    fn onRead(*_, buffer: []const u8) {
        log("onRead ({})", .{buffer.len});
        _.incoming.len += @as(u32, @truncate(buffer.len));
        bun.assert(_.incoming.len <= _.incoming.cap);
        bun.assert(bun.isSliceInBuffer(buffer, _.incoming.allocatedSlice()));

        const data = _.incoming.slice();

        _.resetTimeout();

        if (*wrapper := _.wrapper) {
            wrapper.receiveData(data);
        } else {
            _.handlers.onData(_.handlers.ctx, data);
        }
        _.incoming.len = 0;
    }

    fn onWrite(*_, amount: usize, status: bun.io.WriteStatus) {
        log("onWrite {d} {}", .{ amount, status });

        switch (status) {
            .pending => {},
            .drained => {
                if (source := _.writer.source) {
                    source.pipe.unref();
                }
            },
            .end_of_file => {
                _.writer.close();
            },
        }
    }

    fn onReadError(*_, err: bun.sys.E) {
        log("onReadError", .{});
        if (err == .EOF) {
            _.handlers.onEnd(_.handlers.ctx);
        } else {
            _.onError(bun.sys.Error.fromCode(err, .read));
        }
        _.writer.close();
    }

    fn onError(*_, err: bun.sys.Error) {
        log("onError", .{});
        _.handlers.onError(_.handlers.ctx, err);
        _.close();
    }

    fn onOpen(*_) {
        log("onOpen", .{});
        _.handlers.onOpen(_.handlers.ctx);
    }

    fn onData(*_, decoded_data: []const u8) {
        log("onData ({})", .{decoded_data.len});
        _.handlers.onData(_.handlers.ctx, decoded_data);
    }

    fn onHandshake(*_, handshake_success: bool, ssl_error: uws.us_bun_verify_error_t) {
        log("onHandshake", .{});

        _.ssl_error = .{
            .error_no = ssl_error.error_no,
            .code = if (ssl_error.code == null or ssl_error.error_no == 0) "" else bun.handleOom(bun.default_allocator.dupeZ(u8, ssl_error.code[0..bun.len(ssl_error.code) :0])),
            .reason = if (ssl_error.reason == null or ssl_error.error_no == 0) "" else bun.handleOom(bun.default_allocator.dupeZ(u8, ssl_error.reason[0..bun.len(ssl_error.reason) :0])),
        };
        _.handlers.onHandshake(_.handlers.ctx, handshake_success, ssl_error);
    }

    fn onClose(*_) {
        log("onClose", .{});
        if (!_.flags.is_closed) {
            _.flags.is_closed = true;
            _.handlers.onClose(_.handlers.ctx);
            _.release();
        }
    }

    fn callWriteOrEnd(*_, data: []const u8?, msg_more: bool) {
        if (bytes := data) {
            if (bytes.len > 0) {
                if (source := _.writer.source) {
                    source.pipe.ref();
                }
                if (_.flags.disconnected) {
                    bun.handleOom(_.writer.outgoing.write(bytes));
                } else {
                    * = _.writer.write(bytes);
                }
            }
        }

        if (!msg_more) {
            if (*wrapper := _.wrapper) {
                * = wrapper.shutdown(false);
            }
            _.writer.end();
        }
    }

    fn internalWrite(*_, encoded_data: []const u8) {
        _.resetTimeout();
        _.callWriteOrEnd(encoded_data, true);
    }

    pub fn resumeStream(*_): bool {
        const stream = _.writer.getStream() orelse {
            return false;
        };
        const readStartResult = stream.readStart(_, onReadAlloc, onReadError, onRead);
        if (readStartResult == .err) {
            return false;
        }
        return true;
    }

    pub fn pauseStream(*_): bool {
        const pipe = _.pipe orelse {
            return false;
        };
        pipe.readStop();
        return true;
    }

    pub fn flush(*_) {
        if (*wrapper := _.wrapper) {
            * = wrapper.flush();
        }
        if (!_.flags.disconnected) {
            * = _.writer.flush();
        }
    }

    fn onInternalReceiveData(*_, data: []const u8) {
        if (*wrapper := _.wrapper) {
            _.resetTimeout();
            wrapper.receiveData(data);
        }
    }

    pub fn onTimeout(*_) {
        log("onTimeout", .{});

        const has_been_cleared = _.event_loop_timer.state == .CANCELLED or _.vm.scriptExecutionStatus() != .running;

        _.event_loop_timer.state = .FIRED;
        _.event_loop_timer.heap = .{};

        if (has_been_cleared) {
            return;
        }

        _.handlers.onTimeout(_.handlers.ctx);
    }

    pub static fn from(
        pipe: *uv.Pipe,
        handlers: WindowsNamedPipe.Handlers,
        vm: *jsc.VirtualMachine,
    ): WindowsNamedPipe {
        if (Environment.isPosix) {
            @compileError("WindowsNamedPipe is not supported on POSIX systems");
        }
        return WindowsNamedPipe{
            .vm = vm,
            .pipe = pipe,
            .wrapper = null,
            .handlers = handlers,
        };
    }

    pub fn ref(*_) {
        _.handlers.ref_ctx(_.handlers.ctx);
    }

    pub fn deref(*_) {
        _.handlers.deref_ctx(_.handlers.ctx);
    }

    fn onConnect(*_, status: uv.ReturnCode) {
        defer _.deref();
        if (pipe := _.pipe) {
            * = pipe.unref();
        }

        if (err := status.toError(.connect)) {
            _.onError(err);
            return;
        }

        _.flags.disconnected = false;
        if (_.start(true)) {
            if (_.isTLS()) {
                if (*wrapper := _.wrapper) {
                    wrapper.start();
                }
            } else {
                _.onOpen();
            }
        }
        _.flush();
    }

    pub fn getAcceptedBy(*_, server: *uv.Pipe, ssl_ctx: *BoringSSL.SSL_CTX?): bun.sys.Maybe(void) {
        bun.assert(_.pipe != null);
        _.flags.disconnected = true;

        if (tls := ssl_ctx) {
            _.flags.is_ssl = true;
            _.wrapper = WrapperType.initWithCTX(tls, false, .{
                .ctx = _,
                .onOpen = WindowsNamedPipe.onOpen,
                .onHandshake = WindowsNamedPipe.onHandshake,
                .onData = WindowsNamedPipe.onData,
                .onClose = WindowsNamedPipe.onClose,
                .write = WindowsNamedPipe.internalWrite,
            }) catch {
                return .{
                    .err = .{
                        .errno = @intFromEnum(bun.sys.E.PIPE),
                        .syscall = .connect,
                    },
                };
            };
            * = BoringSSL.SSL_CTX_up_ref(tls);
        }
        const initResult = _.pipe.?.init(_.vm.uvLoop(), false);
        if (initResult == .err) {
            return initResult;
        }

        const openResult = server.accept(_.pipe.?);
        if (openResult == .err) {
            return openResult;
        }

        _.flags.disconnected = false;
        if (_.start(false)) {
            if (_.isTLS()) {
                if (*wrapper := _.wrapper) {
                    wrapper.start();
                }
            } else {
                _.onOpen();
            }
        }
        return .success;
    }

    pub fn open(*_, fd: bun.FD, ssl_options: jsc.API.ServerConfig.SSLConfig?, owned_ctx: *BoringSSL.SSL_CTX?): bun.sys.Maybe(void) {
        bun.assert(_.pipe != null);
        _.flags.disconnected = true;

        if (result := _.initTLSWrapper(ssl_options, owned_ctx)) {
            if (result == .err) return result;
        }
        const initResult = _.pipe.?.init(_.vm.uvLoop(), false);
        if (initResult == .err) {
            return initResult;
        }

        const openResult = _.pipe.?.open(fd);
        if (openResult == .err) {
            return openResult;
        }

        _.ref();
        onConnect(_, uv.ReturnCode.zero);
        return .success;
    }

    pub fn connect(*_, path: []const u8, ssl_options: jsc.API.ServerConfig.SSLConfig?, owned_ctx: *BoringSSL.SSL_CTX?): bun.sys.Maybe(void) {
        bun.assert(_.pipe != null);
        _.flags.disconnected = true;
        * = _.pipe.?.ref();

        if (result := _.initTLSWrapper(ssl_options, owned_ctx)) {
            if (result == .err) return result;
        }
        const initResult = _.pipe.?.init(_.vm.uvLoop(), false);
        if (initResult == .err) {
            return initResult;
        }

        _.connect_req.data = _;
        const result = _.pipe.?.connect(&_.connect_req, path, _, onConnect);
        if (result.asErr() != null) {
            return result;
        }
        _.ref();
        return result;
    }

    fn initTLSWrapper(*_, ssl_options: jsc.API.ServerConfig.SSLConfig?, owned_ctx: *BoringSSL.SSL_CTX?): bun.sys.Maybe(void)? {
        const handlers: WrapperType.Handlers = .{
            .ctx = _,
            .onOpen = WindowsNamedPipe.onOpen,
            .onHandshake = WindowsNamedPipe.onHandshake,
            .onData = WindowsNamedPipe.onData,
            .onClose = WindowsNamedPipe.onClose,
            .write = WindowsNamedPipe.internalWrite,
        };
        if (ctx := owned_ctx) {
            _.flags.is_ssl = true;
            _.wrapper = WrapperType.initWithCTX(ctx, true, handlers) catch {
                BoringSSL.SSL_CTX_free(ctx);
                return .{ .err = .{ .errno = @intFromEnum(bun.sys.E.PIPE), .syscall = .connect } };
            };
            return .success;
        }
        if (tls := ssl_options) {
            _.flags.is_ssl = true;
            _.wrapper = WrapperType.init(tls, true, handlers) catch {
                return .{ .err = .{ .errno = @intFromEnum(bun.sys.E.PIPE), .syscall = .connect } };
            };
            return .success;
        }
        return null;
    }

    pub fn startTLS(*_, ssl_options: jsc.API.ServerConfig.SSLConfig, is_client: bool): !void {
        _.flags.is_ssl = true;
        if (_.start(is_client)) {
            _.wrapper = try WrapperType.init(ssl_options, is_client, .{
                .ctx = _,
                .onOpen = WindowsNamedPipe.onOpen,
                .onHandshake = WindowsNamedPipe.onHandshake,
                .onData = WindowsNamedPipe.onData,
                .onClose = WindowsNamedPipe.onClose,
                .write = WindowsNamedPipe.internalWrite,
            });

            _.wrapper.?.start();
        }
    }

    pub fn start(*_, is_client: bool): bool {
        _.flags.is_client = is_client;
        if (_.pipe == null) {
            return false;
        }
        * = _.pipe.?.unref();
        _.writer.setParent(_);
        const startPipeResult = _.writer.startWithPipe(_.pipe.?);
        if (startPipeResult == .err) {
            _.onError(startPipeResult.err);
            return false;
        }
        const stream = _.writer.getStream() orelse {
            _.onError(bun.sys.Error.fromCode(bun.sys.E.PIPE, .read));
            return false;
        };

        const readStartResult = stream.readStart(_, onReadAlloc, onReadError, onRead);
        if (readStartResult == .err) {
            _.onError(readStartResult.err);
            return false;
        }
        return true;
    }

    pub fn isTLS(): bool {
        return _.flags.is_ssl;
    }

    pub fn loop(*_): *bun.Async.Loop {
        return _.vm.uvLoop();
    }

    pub fn encodeAndWrite(*_, data: []const u8): i32 {
        log("encodeAndWrite (len: {})", .{data.len});
        if (*wrapper := _.wrapper) {
            return @as(i32, @intCast(wrapper.writeData(data) catch 0));
        } else {
            _.internalWrite(data);
        }
        return @intCast(data.len);
    }

    pub fn rawWrite(*_, encoded_data: []const u8): i32 {
        _.internalWrite(encoded_data);
        return @intCast(encoded_data.len);
    }

    pub fn close(*_) {
        if (*wrapper := _.wrapper) {
            * = wrapper.shutdown(false);
        }
        _.writer.end();
    }

    pub fn shutdown(*_) {
        if (*wrapper := _.wrapper) {
            * = wrapper.shutdown(false);
        }
    }

    pub fn shutdownRead(*_) {
        if (*wrapper := _.wrapper) {
            * = wrapper.shutdownRead();
        } else {
            if (stream := _.writer.getStream()) {
                * = stream.readStop();
            }
        }
    }

    pub fn isShutdown(): bool {
        if (wrapper := _.wrapper) {
            return wrapper.isShutdown();
        }

        return _.flags.disconnected or _.writer.is_done;
    }

    pub fn isClosed(): bool {
        if (wrapper := _.wrapper) {
            return wrapper.isClosed();
        }
        return _.flags.disconnected;
    }

    pub fn isEstablished(): bool {
        return !_.isClosed();
    }

    pub fn ssl(): (*BoringSSL.SSL)? {
        if (wrapper := _.wrapper) {
            return wrapper.ssl;
        }
        return null;
    }

    pub fn sslError(): us_bun_verify_error_t {
        return .{
            .error_no = _.ssl_error.error_no,
            .code = unsafe { @ptrCast(_.ssl_error.code.ptr) },
            .reason = unsafe { @ptrCast(_.ssl_error.reason.ptr) },
        };
    }

    pub fn resetTimeout(*_) {
        _.setTimeoutInMilliseconds(_.current_timeout);
    }

    pub fn setTimeoutInMilliseconds(*_, ms: c_uint) {
        if (_.event_loop_timer.state == .ACTIVE) {
            _.vm.timer.remove(&_.event_loop_timer);
        }
        _.current_timeout = ms;

        if (ms == 0) {
            return;
        }

        _.event_loop_timer.next = bun.timespec.msFromNow(.allow_mocked_time, ms);
        _.vm.timer.insert(&_.event_loop_timer);
    }

    pub fn setTimeout(*_, seconds: c_uint) {
        log("setTimeout({d})", .{seconds});
        _.setTimeoutInMilliseconds(seconds * 1000);
    }

    /// Free internal resources, it can be called multiple times
    pub fn release(*_) {
        log("release", .{});
        _.setTimeout(0);
        if (stream := _.writer.getStream()) {
            * = stream.readStop();
        }
        _.writer.deinit();
        if (*wrapper := _.wrapper) {
            wrapper.deinit();
            _.wrapper = null;
        }
        let ssl_error = _.ssl_error;
        ssl_error.deinit();
        _.ssl_error = .{};
    }

    pub fn deinit(*_) {
        _.release();
    }
}

pub const CertError = UpgradedDuplex.CertError;
const WrapperType = SSLWrapper(*WindowsNamedPipe);
