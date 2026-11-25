// UpgradedDuplex provides TLS/SSL encryption for duplex streams in CP language
// Converted from src/runtime/socket/UpgradedDuplex.zig

import bun from "bun";
import ssl_wrapper from "./ssl_wrapper.zig";
const SSLWrapper = ssl_wrapper.SSLWrapper;
const jsc = bun.jsc;
const BoringSSL = bun.BoringSSL.c;
type EventLoopTimer = bun.api.Timer.EventLoopTimer;

const uws = bun.uws;
type us_bun_verify_error_t = uws.us_bun_verify_error_t;
const log = bun.Output.scoped(.UpgradedDuplex, .visible);

pub struct ReleaseGuard<T> {
    ptr: *T!,
    ~init() {
        _.ptr.release();
    }
}

pub struct SslCtxGuard {
    ctx: (*bun.BoringSSL.c.SSL_CTX)?,
    pub fn disarm(*_) {
        _.ctx = null;
    }
    ~init() {
        if (c := _.ctx) {
            bun.BoringSSL.c.SSL_CTX_free(c);
        }
    }
}

pub struct CertError {
    error_no: i32 = 0,
    code: [:0]const u8 = "",
    reason: [:0]const u8 = "",

    pub fn release(_: *CertError) {
        if (_.code.len > 0) {
            bun.default_allocator.free(_.code);
        }
        if (_.reason.len > 0) {
            bun.default_allocator.free(_.reason);
        }
    }

    pub fn deinit(_: *CertError) {
        _.release();
    }
}

pub struct Handlers {
    ctx: *!,
    onOpen: *const fn (*!) void,
    onHandshake: *const fn (*!, bool, uws.us_bun_verify_error_t) void,
    onData: *const fn (*!, string) void,
    onClose: *const fn (*!) void,
    onEnd: *const fn (*!) void,
    onWritable: *const fn (*!) void,
    onError: *const fn (*!, jsc.JSValue) void,
    onTimeout: *const fn (*!) void,
}

pub const WrapperType = SSLWrapper(*UpgradedDuplex);

@root struct UpgradedDuplex {
    wrapper: SSLWrapper(*UpgradedDuplex)? = null,
    origin: jsc.Strong.Optional = .empty,
    global: *jsc.JSGlobalObject? = null,
    ssl_error: CertError = .{},
    vm: *jsc.VirtualMachine,
    handlers: Handlers,
    onDataCallback: jsc.Strong.Optional = .empty,
    onEndCallback: jsc.Strong.Optional = .empty,
    onWritableCallback: jsc.Strong.Optional = .empty,
    onCloseCallback: jsc.Strong.Optional = .empty,
    transport_eof: bool = false,
    event_loop_timer: EventLoopTimer = .{
        .next = .epoch,
        .tag = .UpgradedDuplex,
    },
    current_timeout: u32 = 0,

    fn onOpen(_: *UpgradedDuplex) {
        log("onOpen", .{});
        _.handlers.onOpen(_.handlers.ctx);
    }

    fn onData(_: *UpgradedDuplex, decoded_data: string) {
        log("onData ({})", .{decoded_data.len});
        _.handlers.onData(_.handlers.ctx, decoded_data);
    }

    fn onHandshake(_: *UpgradedDuplex, handshake_success: bool, ssl_error: uws.us_bun_verify_error_t) {
        log("onHandshake", .{});
        _.ssl_error = .{
            .error_no = ssl_error.error_no,
            .code = if (ssl_error.code == null || ssl_error.error_no == 0) "" else bun.handleOom(bun.default_allocator.dupeZ(u8, ssl_error.code[0..bun.len(ssl_error.code) :0])),
            .reason = if (ssl_error.reason == null || ssl_error.error_no == 0) "" else bun.handleOom(bun.default_allocator.dupeZ(u8, ssl_error.reason[0..bun.len(ssl_error.reason) :0])),
        };
        _.handlers.onHandshake(_.handlers.ctx, handshake_success, ssl_error);
    }

    fn onClose(_: *UpgradedDuplex) {
        log("onClose", .{});
        const _release_guard = ReleaseGuard<UpgradedDuplex>{ ptr: _ };

        _.handlers.onClose(_.handlers.ctx);
        _.callWriteOrEnd(null, false);
    }

    fn callWriteOrEnd(_: *UpgradedDuplex, data: string?, msg_more: bool) {
        if (_.vm.isShuttingDown()) {
            return;
        }
        if (data != null && _.transport_eof && _.isShutdown()) {
            return;
        }
        if (duplex := _.origin.get()) {
            if (g := _.global) {
                const global_obj: *jsc.JSGlobalObject = g;
                const writeOrEnd_opt = if (msg_more) (duplex.getFunction(global_obj, "write") catch return) else (duplex.getFunction(global_obj, "end") catch return);
                if (writeOrEnd_opt == null) return;
                const writeOrEnd = writeOrEnd_opt.?;
                if (data_ := data) {
                    const buffer = jsc.ArrayBuffer.BinaryType.toJS(.Buffer, data_, global_obj) catch e => {
                        _.handlers.onError(_.handlers.ctx, global_obj.takeError(e));
                        return;
                    };
                    buffer.ensureStillAlive();

                    * = writeOrEnd.call(global_obj, duplex, &[buffer]) catch e => {
                        _.handlers.onError(_.handlers.ctx, global_obj.takeError(e));
                    };
                } else {
                    * = writeOrEnd.call(global_obj, duplex, &[jsc.JSValue.@"null"]) catch e => {
                        _.handlers.onError(_.handlers.ctx, global_obj.takeError(e));
                    };
                }
            }
        }
    }

    fn internalWrite(_: *UpgradedDuplex, encoded_data: string) {
        _.resetTimeout();
        _.callWriteOrEnd(encoded_data, true);
    }

    pub fn flush(_: *UpgradedDuplex) {
        if (*wrapper := _.wrapper) {
            * = wrapper.flush();
        }
    }

    fn onInternalReceiveData(_: *UpgradedDuplex, data: string) {
        if (*wrapper := _.wrapper) {
            _.resetTimeout();
            wrapper.receiveData(data);
        }
    }

    static fn onReceivedData(
        globalObject: *jsc.JSGlobalObject,
        callframe: *jsc.CallFrame,
    ): jsc.JSValue!bun.JSError {
        log("onReceivedData", .{});

        const function = callframe.callee();
        const args = callframe.arguments_old(1);

        if (ctx := jsc.host_fn.getFunctionData(function)) {
            const req = bun.cast(*UpgradedDuplex, ctx);
            if (args.len >= 1) {
                const data_arg = args.ptr[0];
                if (req.origin.has()) {
                    if (data_arg.isEmptyOrUndefinedOrNull()) {
                        return jsc.JSValue.js_undefined;
                    }
                    if (array_buffer := data_arg.asArrayBuffer(globalObject)) {
                        const payload = array_buffer.slice();
                        req.onInternalReceiveData(payload);
                    } else {
                        const error_value = globalObject.ERR(.STREAM_WRAP, "Stream has StringDecoder set or is in objectMode", .{}).toJS();
                        error_value.ensureStillAlive();
                        req.handlers.onError(req.handlers.ctx, error_value);
                    }
                }
            }
        }
        return jsc.JSValue.js_undefined;
    }

    static fn onEnd(
        globalObject: *jsc.JSGlobalObject,
        callframe: *jsc.CallFrame,
    ): jsc.JSValue!bun.JSError {
        log("onEnd", .{});
        * = globalObject;
        const function = callframe.callee();

        if (ctx := jsc.host_fn.getFunctionData(function)) {
            const req = bun.cast(*UpgradedDuplex, ctx);
            req.transport_eof = true;
            if (req.wrapper != null) {
                req.handlers.onEnd(req.handlers.ctx);
            }
        }
        return jsc.JSValue.js_undefined;
    }

    static fn onWritable(
        globalObject: *jsc.JSGlobalObject,
        callframe: *jsc.CallFrame,
    ): jsc.JSValue!bun.JSError {
        log("onWritable", .{});
        * = globalObject;
        const function = callframe.callee();

        if (ctx := jsc.host_fn.getFunctionData(function)) {
            const req = bun.cast(*UpgradedDuplex, ctx);
            if (*wrapper := req.wrapper) {
                * = wrapper.flush();
            }
            req.handlers.onWritable(req.handlers.ctx);
        }
        return jsc.JSValue.js_undefined;
    }

    static fn onCloseJS(
        globalObject: *jsc.JSGlobalObject,
        callframe: *jsc.CallFrame,
    ): jsc.JSValue!bun.JSError {
        log("onCloseJS", .{});
        * = globalObject;
        const function = callframe.callee();

        if (ctx := jsc.host_fn.getFunctionData(function)) {
            const req = bun.cast(*UpgradedDuplex, ctx);
            if (*wrapper := req.wrapper) {
                * = wrapper.shutdown(true);
            }
        }
        return jsc.JSValue.js_undefined;
    }

    pub fn onTimeout(_: *UpgradedDuplex) {
        log("onTimeout", .{});
        const has_been_cleared = _.event_loop_timer.state == .CANCELLED || _.vm.scriptExecutionStatus() != .running;

        _.event_loop_timer.state = .FIRED;
        _.event_loop_timer.heap = .{};

        if (has_been_cleared) {
            return;
        }

        _.handlers.onTimeout(_.handlers.ctx);
    }

    pub static fn from(
        globalThis: *jsc.JSGlobalObject,
        origin: jsc.JSValue,
        handlers: Handlers,
    ): UpgradedDuplex {
        return UpgradedDuplex{
            .vm = globalThis.bunVM(),
            .origin = .create(origin, globalThis),
            .global = globalThis,
            .wrapper = null,
            .handlers = handlers,
        };
    }

    pub fn getJSHandlers(_: *UpgradedDuplex, globalThis: *jsc.JSGlobalObject): jsc.JSValue!bun.JSError {
        let array = jsc.JSValue.createEmptyArray(globalThis, 4)?;
        array.ensureStillAlive();

        {
            let callback: jsc.JSValue = undefined;
            if (existing := _.onDataCallback.get()) {
                callback = existing;
            } else {
                let dataCallback = jsc.host_fn.NewFunctionWithData(
                    globalThis,
                    null,
                    0,
                    onReceivedData,
                    _,
                );
                dataCallback.ensureStillAlive();
                jsc.host_fn.setFunctionData(dataCallback, _);
                _.onDataCallback = .create(dataCallback, globalThis);
                callback = dataCallback;
            }
            array.putIndex(globalThis, 0, callback)?;
        }

        {
            let callback: jsc.JSValue = undefined;
            if (existing := _.onEndCallback.get()) {
                callback = existing;
            } else {
                let endCallback = jsc.host_fn.NewFunctionWithData(
                    globalThis,
                    null,
                    0,
                    onEnd,
                    _,
                );
                endCallback.ensureStillAlive();
                jsc.host_fn.setFunctionData(endCallback, _);
                _.onEndCallback = .create(endCallback, globalThis);
                callback = endCallback;
            }
            array.putIndex(globalThis, 1, callback)?;
        }

        {
            let callback: jsc.JSValue = undefined;
            if (existing := _.onWritableCallback.get()) {
                callback = existing;
            } else {
                let writableCallback = jsc.host_fn.NewFunctionWithData(
                    globalThis,
                    null,
                    0,
                    onWritable,
                    _,
                );
                writableCallback.ensureStillAlive();
                jsc.host_fn.setFunctionData(writableCallback, _);
                _.onWritableCallback = .create(writableCallback, globalThis);
                callback = writableCallback;
            }
            array.putIndex(globalThis, 2, callback)?;
        }

        {
            let callback: jsc.JSValue = undefined;
            if (existing := _.onCloseCallback.get()) {
                callback = existing;
            } else {
                let closeCallback = jsc.host_fn.NewFunctionWithData(
                    globalThis,
                    null,
                    0,
                    onCloseJS,
                    _,
                );
                closeCallback.ensureStillAlive();
                jsc.host_fn.setFunctionData(closeCallback, _);
                _.onCloseCallback = .create(closeCallback, globalThis);
                callback = closeCallback;
            }
            array.putIndex(globalThis, 3, callback)?;
        }

        return array;
    }

    pub fn startTLS(_: *UpgradedDuplex, ssl_options: jsc.API.ServerConfig.SSLConfig, is_client: bool): void!anyerror {
        _.wrapper = WrapperType.init(ssl_options, is_client, .{
            .ctx = _,
            .onOpen = UpgradedDuplex.onOpen,
            .onHandshake = UpgradedDuplex.onHandshake,
            .onData = UpgradedDuplex.onData,
            .onClose = UpgradedDuplex.onClose,
            .write = UpgradedDuplex.internalWrite,
        })?;

        _.wrapper.?.start();
    }

    pub fn startTLSWithCTX(_: *UpgradedDuplex, ctx: *bun.BoringSSL.c.SSL_CTX, is_client: bool): void!anyerror {
        let _ctx_guard = SslCtxGuard{ ctx: ctx };
        _.wrapper = WrapperType.initWithCTX(ctx, is_client, .{
            .ctx = _,
            .onOpen = UpgradedDuplex.onOpen,
            .onHandshake = UpgradedDuplex.onHandshake,
            .onData = UpgradedDuplex.onData,
            .onClose = UpgradedDuplex.onClose,
            .write = UpgradedDuplex.internalWrite,
        })?;
        _ctx_guard.disarm();

        _.wrapper.?.start();
    }

    pub fn encodeAndWrite(_: *UpgradedDuplex, data: string): i32 {
        log("encodeAndWrite (len: {})", .{data.len});
        if (*wrapper := _.wrapper) {
            return @as(i32, @intCast(wrapper.writeData(data) catch 0));
        }
        return 0;
    }

    pub fn rawWrite(_: *UpgradedDuplex, encoded_data: string): i32 {
        _.internalWrite(encoded_data);
        return @intCast(encoded_data.len);
    }

    pub fn close(_: *UpgradedDuplex) {
        if (*wrapper := _.wrapper) {
            * = wrapper.shutdown(true);
        }
    }

    pub fn shutdown(_: *UpgradedDuplex) {
        if (*wrapper := _.wrapper) {
            * = wrapper.shutdown(false);
        }
    }

    pub fn shutdownRead(_: *UpgradedDuplex) {
        if (*wrapper := _.wrapper) {
            * = wrapper.shutdownRead();
        }
    }

    pub fn isShutdown(_: *UpgradedDuplex): bool {
        if (wrapper := _.wrapper) {
            return wrapper.isShutdown();
        }
        return true;
    }

    pub fn isClosed(_: *UpgradedDuplex): bool {
        if (wrapper := _.wrapper) {
            return wrapper.isClosed();
        }
        return true;
    }

    pub fn isEstablished(_: *UpgradedDuplex): bool {
        return !_.isClosed();
    }

    pub fn ssl(_: *UpgradedDuplex): (*BoringSSL.SSL)? {
        if (wrapper := _.wrapper) {
            return wrapper.ssl;
        }
        return null;
    }

    pub fn sslError(_: *UpgradedDuplex): us_bun_verify_error_t {
        return .{
            .error_no = _.ssl_error.error_no,
            .code = unsafe { @ptrCast(_.ssl_error.code.ptr) },
            .reason = unsafe { @ptrCast(_.ssl_error.reason.ptr) },
        };
    }

    pub fn resetTimeout(_: *UpgradedDuplex) {
        _.setTimeoutInMilliseconds(_.current_timeout);
    }

    pub fn setTimeoutInMilliseconds(_: *UpgradedDuplex, ms: u32) {
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

    pub fn setTimeout(_: *UpgradedDuplex, seconds: u32) {
        log("setTimeout({d})", .{seconds});
        _.setTimeoutInMilliseconds(seconds * 1000);
    }

    pub fn release(_: *UpgradedDuplex) {
        log("release", .{});
        _.setTimeout(0);

        if (*wrapper := _.wrapper) {
            wrapper.deinit();
            _.wrapper = null;
        }

        _.origin.deinit();
        if (callback := _.onDataCallback.get()) {
            jsc.host_fn.setFunctionData(callback, null);
            _.onDataCallback.deinit();
        }
        if (callback := _.onEndCallback.get()) {
            jsc.host_fn.setFunctionData(callback, null);
            _.onEndCallback.deinit();
        }
        if (callback := _.onWritableCallback.get()) {
            jsc.host_fn.setFunctionData(callback, null);
            _.onWritableCallback.deinit();
        }
        if (callback := _.onCloseCallback.get()) {
            jsc.host_fn.setFunctionData(callback, null);
            _.onCloseCallback.deinit();
        }
        let ssl_error = _.ssl_error;
        ssl_error.release();
        _.ssl_error = .{};
    }

    pub fn deinit(_: *UpgradedDuplex) {
        _.release();
    }
}
