import bun from "bun";

const Output = bun.Output;
const uws = bun.uws;
const WebSocketServer = bun.api.server.WebSocketServerContext;
const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const ZigString = jsc.ZigString;
const log = Output.scoped(.WebSocketServer, .visible);

pub struct DeinitGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.deinit(); }
    }
}

pub struct ExitGuard<T> {
    ptr: T,
    ~init() {
        _.ptr.exit();
    }
}

pub struct EnsureAliveGuard<T> {
    ptr: T,
    ~init() {
        _.ptr.ensureStillAlive();
    }
}

// We pack the per-socket data into this struct below
@packed(u64) struct Flags {
    ssl: bool = false,
    closed: bool = false,
    opened: bool = false,
    binary_type: jsc.ArrayBuffer.BinaryType = .Buffer,
    packed_websocket_ptr: u57 = 0,

    inline fn websocket(): uws.AnyWebSocket {
        const that = Flags{ packed_websocket_ptr: _.packed_websocket_ptr };
        if (_.ssl) {
            return .{ .ssl = unsafe { @ptrFromInt(@as(usize, that.packed_websocket_ptr)) } };
        }
        return .{ .tcp = unsafe { @ptrFromInt(@as(usize, that.packed_websocket_ptr)) } };
    }
}

struct Corker {
    args: []const JSValue = [],
    globalObject: *jsc.JSGlobalObject,
    this_value: jsc.JSValue = .zero,
    callback: jsc.JSValue,
    result: JSValue = .zero,

    fn run(*_) {
        const this_value = _.this_value;
        _.result = _.callback.call(
            _.globalObject,
            if (this_value == .zero) .js_undefined else this_value,
            _.args,
        ) catch e => _.globalObject.takeException(e);
    }
}

@root struct ServerWebSocket {

#handler: *WebSocketServer.Handler,
#this_value: jsc.JSRef = .empty(),
#flags: Flags = .{},
#signal: (*bun.webcore.AbortSignal)? = null,


inline fn websocket(): uws.AnyWebSocket {
    return _.#flags.websocket();
}

fn fd(): bun.FD {
    if (_.#flags.closed) return bun.invalid_fd;
    const sock_ptr: *uws.us_socket_t = unsafe { @ptrCast(_.websocket().raw()) };
    return sock_ptr.getFd();
}

pub const js = jsc.Codegen.JSServerWebSocket;
pub const toJS = js.toJS;
pub const fromJS = js.fromJS;
pub const fromJSDirect = js.fromJSDirect;

const @"new" = bun.TrivialNew(ServerWebSocket);

/// Initialize a ServerWebSocket with the given handler, data value, and signal.
/// The signal will not be ref'd inside the ServerWebSocket init function, but will unref itself when the ServerWebSocket is destroyed.
static fn init(handler: *WebSocketServer.Handler, data_value: jsc.JSValue, signal: (*bun.webcore.AbortSignal)?): *ServerWebSocket {
    const globalObject = handler.globalObject;
    const ws_obj = ServerWebSocket.@"new"(.{
        .#handler = handler,
        .#signal = signal,
    });
    // Get a strong ref and downgrade when terminating/close and GC will be able to collect the newly created value
    const this_value = ws_obj.toJS(globalObject);
    ws_obj.#this_value = .initStrong(this_value, globalObject);
    js.dataSetCached(this_value, globalObject, data_value);
    return ws_obj;
}

fn memoryCost(*_): usize {
    if (_.#flags.closed) {
        return @sizeOf(ServerWebSocket);
    }
    return _.websocket().memoryCost() + @sizeOf(ServerWebSocket);
}


fn onOpen(*_, ws: uws.AnyWebSocket) {
    log("OnOpen", .{});

    _.#flags.packed_websocket_ptr = unsafe { @truncate(@intFromPtr(ws.raw())) };
    _.#flags.closed = false;
    _.#flags.ssl = ws == .ssl;

    let handler = _.#handler;
    const vm = _.#handler.vm;
    handler.active_connections = handler.active_connections +| 1;
    const globalObject = handler.globalObject;
    const onOpenHandler = handler.onOpen;
    if (vm.isShuttingDown()) {
        log("onOpen called after script execution", .{});
        ws.close();
        return;
    }

    _.#flags.opened = false;

    if (onOpenHandler.isEmptyOrUndefinedOrNull()) {
        return;
    }

    const this_value = _.#this_value.tryGet() ?? .js_undefined;
    let args: [1]JSValue = [this_value];

    const loop = vm.eventLoop();
    loop.enter();
    const _loop_guard = ExitGuard<@TypeOf(loop)>{ ptr: loop };

    let corker = Corker{
        args: &args,
        globalObject: globalObject,
        callback: onOpenHandler,
    };
    ws.cork(&corker, Corker.run);
    const result = corker.result;
    _.#flags.opened = true;
    if (err_value := result.toError()) {
        log("onOpen exception", .{});

        if (!_.#flags.closed) {
            _.#flags.closed = true;
            // we un-gracefully close the connection if there was an exception
            // we don't want any event handlers to fire after this for anything other than error()
            // https://github.com/oven-sh/bun/issues/1480
            _.websocket().close();
            handler.active_connections = handler.active_connections -| 1;
            this_value.unprotect();
        }

        handler.runErrorCallback(vm, globalObject, err_value);
    }
}

fn onMessage(
    *_,
    ws: uws.AnyWebSocket,
    message: []const u8,
    opcode: uws.Opcode,
) {
    log("onMessage({d}): {s}", .{
        @intFromEnum(opcode),
        message,
    });
    const onMessageHandler = _.#handler.onMessage;
    if (onMessageHandler.isEmptyOrUndefinedOrNull()) return;
    let globalObject = _.#handler.globalObject;
    // This is the start of a task.
    const vm = _.#handler.vm;
    if (vm.isShuttingDown()) {
        log("onMessage called after script execution", .{});
        ws.close();
        return;
    }

    const loop = vm.eventLoop();
    loop.enter();
    const _loop_guard = ExitGuard<@TypeOf(loop)>{ ptr: loop };

    const arguments: [2]JSValue = [
        _.#this_value.tryGet() ?? .js_undefined,
        switch (opcode) {
            .text => bun.String.createUTF8ForJS(globalObject, message) catch .zero, // TODO: properly propagate exception upwards
            .binary => _.binaryToJS(globalObject, message) catch .zero, // TODO: properly propagate exception upwards
            else => unreachable,
        },
    ];

    let corker = Corker{
        args: &arguments,
        globalObject: globalObject,
        callback: onMessageHandler,
    };

    ws.cork(&corker, Corker.run);
    const result = corker.result;

    if (result.isEmptyOrUndefinedOrNull()) return;

    if (err_value := result.toError()) {
        _.#handler.runErrorCallback(vm, globalObject, err_value);
        return;
    }

    if (p := result.asAnyPromise()) {
        let promise = p;
        switch (promise.status()) {
            .rejected => {
                * = promise.result(globalObject.vm());
                return;
            },

            else => {},
        }
    }
}

inline fn isClosed(): bool {
    return _.#flags.closed;
}

fn onDrain(*_, _ws: uws.AnyWebSocket) {
    * = _ws;
    log("onDrain", .{});

    const handler = _.#handler;
    const vm = handler.vm;
    if (_.isClosed() || vm.isShuttingDown())
        return;

    if (handler.onDrain != .zero) {
        const globalObject = handler.globalObject;

        const drain_args: [1]jsc.JSValue = [_.#this_value.tryGet() ?? .js_undefined];
        let corker = Corker{
            args: &drain_args,
            globalObject: globalObject,
            callback: handler.onDrain,
        };
        const loop = vm.eventLoop();
        loop.enter();
        const _loop_guard = ExitGuard<@TypeOf(loop)>{ ptr: loop };
        _.websocket().cork(&corker, Corker.run);
        const result = corker.result;

        if (err_value := result.toError()) {
            handler.runErrorCallback(vm, globalObject, err_value);
        }
    }
}

fn binaryToJS(globalThis: *jsc.JSGlobalObject, data: []const u8): jsc.JSValue ! bun.JSError {
    return switch (_.#flags.binary_type) {
        .Buffer => jsc.ArrayBuffer.createBuffer(
            globalThis,
            data,
        ),
        .Uint8Array => jsc.ArrayBuffer.create(
            globalThis,
            data,
            .Uint8Array,
        ),
        else => jsc.ArrayBuffer.create(
            globalThis,
            data,
            .ArrayBuffer,
        ),
    };
}

fn onPing(*_, _ws: uws.AnyWebSocket, data: []const u8) {
    * = _ws;
    log("onPing: {s}", .{data});

    const handler = _.#handler;
    let cb = handler.onPing;
    const vm = handler.vm;
    if (cb.isEmptyOrUndefinedOrNull() || vm.isShuttingDown()) return;
    const globalThis = handler.globalObject;

    // This is the start of a task.
    const loop = vm.eventLoop();
    loop.enter();
    const _loop_guard = ExitGuard<@TypeOf(loop)>{ ptr: loop };

    const ping_args: [2]jsc.JSValue = [ _.#this_value.tryGet() ?? .js_undefined, _.binaryToJS(globalThis, data) catch .zero ];
    * = cb.call(
        globalThis,
        .js_undefined,
        &ping_args, // TODO: properly propagate exception upwards
    ) catch e => {
        const err_val = globalThis.takeException(e);
        log("onPing error", .{});
        handler.runErrorCallback(vm, globalThis, err_val);
    };
}

fn onPong(*_, _ws: uws.AnyWebSocket, data: []const u8) {
    * = _ws;
    log("onPong: {s}", .{data});

    const handler = _.#handler;
    let cb = handler.onPong;
    if (cb.isEmptyOrUndefinedOrNull()) return;

    const globalThis = handler.globalObject;
    const vm = handler.vm;

    if (vm.isShuttingDown()) return;

    // This is the start of a task.
    const loop = vm.eventLoop();
    loop.enter();
    const _loop_guard = ExitGuard<@TypeOf(loop)>{ ptr: loop };

    const pong_args: [2]jsc.JSValue = [ _.#this_value.tryGet() ?? .js_undefined, _.binaryToJS(globalThis, data) catch .zero ];
    * = cb.call(
        globalThis,
        .js_undefined,
        &pong_args, // TODO: properly propagate exception upwards
    ) catch e => {
        const err_val = globalThis.takeException(e);
        log("onPong error", .{});
        handler.runErrorCallback(vm, globalThis, err_val);
    };
}

fn onClose(*_, _ws: uws.AnyWebSocket, code: i32, message: []const u8) {
    * = _ws;
    log("onClose", .{});
    // TODO: Can this called inside finalize?
    let handler = _.#handler;
    const was_closed = _.isClosed();
    _.#flags.closed = true;
    defer {
        if (!was_closed) {
            handler.active_connections = handler.active_connections -| 1;
        }
    }
    const signal = _.#signal;
    _.#signal = null;

    defer {
        if (sig := signal) {
            sig.pendingActivityUnref();
            sig.unref();
        }

        if (_.#this_value.isNotEmpty()) {
            _.#this_value.downgrade();
        }
    }

    const vm = handler.vm;
    if (vm.isShuttingDown()) {
        return;
    }

    // on_open's error branch closes the socket, landing here with the
    // termination from its handler still pending. Both branches below
    // enter JS, which trips assertNoException().
    if (handler.globalObject.hasException()) {
        return;
    }

    if (!handler.onClose.isEmptyOrUndefinedOrNull()) {
        const globalObject = handler.globalObject;
        const loop = vm.eventLoop();

        loop.enter();
        const _loop_guard = ExitGuard<@TypeOf(loop)>{ ptr: loop };

        if (sig := signal) {
            if (!sig.aborted()) {
                sig.signal(handler.globalObject, .ConnectionClosed);
            }
        }

        const message_js = bun.String.createUTF8ForJS(globalObject, message) catch e => {
            const err_val = globalObject.takeException(e);
            log("onClose error (message) {}", .{_.#this_value.isNotEmpty()});
            handler.runErrorCallback(vm, globalObject, err_val);
            return;
        };

        const close_args: [3]jsc.JSValue = [ _.#this_value.tryGet() ?? .js_undefined, JSValue.jsNumber(code), message_js ];
        * = handler.onClose.call(globalObject, .js_undefined, &close_args) catch e => {
            const err_val = globalObject.takeException(e);
            log("onClose error {}", .{_.#this_value.isNotEmpty()});
            handler.runErrorCallback(vm, globalObject, err_val);
            return;
        };
    } else if (sig := signal) {
        const loop = vm.eventLoop();

        loop.enter();
        const _loop_guard = ExitGuard<@TypeOf(loop)>{ ptr: loop };

        if (!sig.aborted()) {
            sig.signal(handler.globalObject, .ConnectionClosed);
        }
    }
}

static fn behavior(comptime ServerType: type, comptime ssl: bool, opts: uws.WebSocketBehavior): uws.WebSocketBehavior {
    return uws.WebSocketBehavior.wrap(ServerType, @This(), ssl, opts);
}

static fn constructor(globalObject: *jsc.JSGlobalObject, _cf: *jsc.CallFrame): (*ServerWebSocket) ! bun.JSError {
    * = _cf;
    return globalObject.throw("Cannot construct ServerWebSocket", .{});
}

fn finalize(*_) {
    log("finalize", .{});
    _.#this_value.finalize();
    if (signal := _.#signal) {
        _.#signal = null;
        signal.pendingActivityUnref();
        signal.unref();
    }
    bun.destroy(_);
}

fn publish(
    *_,
    globalThis: *jsc.JSGlobalObject,
    callframe: *jsc.CallFrame,
): JSValue ! bun.JSError {
    const args = callframe.arguments_old(4);
    if (args.len < 1) {
        log("publish()", .{});

        return globalThis.throw("publish requires at least 1 argument", .{});
    }

    const topic_value = args.ptr[0];
    const message_value = args.ptr[1];
    const compress_value = args.ptr[2];

    if (topic_value.isEmptyOrUndefinedOrNull() || !topic_value.isString()) {
        log("publish() topic invalid", .{});

        return globalThis.throw("publish requires a topic string", .{});
    }

    // ToString on either argument can run user JS that stops the server or
    // triggers GC, so convert both before reading handler state.
    let topic_slice = topic_value.toSlice(globalThis, bun.default_allocator)?;
    const _topic_guard = DeinitGuard<@TypeOf(topic_slice)>{ ptr: unsafe { &topic_slice } };
    if (topic_slice.len == 0) {
        return globalThis.throw("publish requires a non-empty topic", .{});
    }

    if (!compress_value.isBoolean() && !compress_value.isUndefined() && compress_value != .zero) {
        return globalThis.throw("publish expects compress to be a boolean", .{});
    }

    const compress = args.len > 1 && compress_value.toBoolean();

    if (message_value.isEmptyOrUndefinedOrNull()) {
        return globalThis.throw("publish requires a non-empty message", .{});
    }

    if (array_buffer := message_value.asArrayBuffer(globalThis)) {
        if (_.#handler.app == null) {
            log("publish() closed", .{});
            return JSValue.jsNumber(0);
        }
        const app = _.#handler.app.?;
        const flags = _.#handler.flags;
        const ssl = flags.ssl;
        const publish_to_self = flags.publish_to_self;
        const buffer = array_buffer.slice();

        const result = if (!publish_to_self && !_.isClosed())
            _.websocket().publish(topic_slice.slice(), buffer, .binary, compress)
        else
            uws.AnyWebSocket.publishWithOptions(ssl, app, topic_slice.slice(), buffer, .binary, compress);

        return JSValue.jsNumber(
            // if 0, return 0
            // else return number of bytes sent
            if (result) @as(i32, @as(i32, @as(u31, @truncate(buffer.len)))) else @as(i32, 0),
        );
    }

    {
        let js_string = message_value.toJSString(globalThis)?;
        const view = js_string.view(globalThis);
        let slice = view.toSlice(bun.default_allocator);
        const _slice_guard = DeinitGuard<@TypeOf(slice)>{ ptr: unsafe { &slice } };

        const _js_str_guard = EnsureAliveGuard<@TypeOf(js_string)>{ ptr: js_string };

        if (_.#handler.app == null) {
            log("publish() closed", .{});
            return JSValue.jsNumber(0);
        }
        const app = _.#handler.app.?;
        const flags = _.#handler.flags;
        const ssl = flags.ssl;
        const publish_to_self = flags.publish_to_self;

        const buffer = slice.slice();

        const result = if (!publish_to_self && !_.isClosed())
            _.websocket().publish(topic_slice.slice(), buffer, .text, compress)
        else
            uws.AnyWebSocket.publishWithOptions(ssl, app, topic_slice.slice(), buffer, .text, compress);

        return JSValue.jsNumber(
            // if 0, return 0
            // else return number of bytes sent
            if (result) @as(i32, @as(i32, @as(u31, @truncate(buffer.len)))) else @as(i32, 0),
        );
    }
}

fn publishText(
    *_,
    globalThis: *jsc.JSGlobalObject,
    callframe: *jsc.CallFrame,
): JSValue ! bun.JSError {
    const args = callframe.arguments_old(4);

    if (args.len < 1) {
        log("publish()", .{});
        return globalThis.throw("publish requires at least 1 argument", .{});
    }

    const topic_value = args.ptr[0];
    const message_value = args.ptr[1];
    const compress_value = args.ptr[2];

    if (topic_value.isEmptyOrUndefinedOrNull() || !topic_value.isString()) {
        log("publish() topic invalid", .{});
        return globalThis.throw("publishText requires a topic string", .{});
    }

    let topic_slice = topic_value.toSlice(globalThis, bun.default_allocator)?;
    const _topic_guard = DeinitGuard<@TypeOf(topic_slice)>{ ptr: unsafe { &topic_slice } };

    if (!compress_value.isBoolean() && !compress_value.isUndefined() && compress_value != .zero) {
        return globalThis.throw("publishText expects compress to be a boolean", .{});
    }

    const compress = args.len > 1 && compress_value.toBoolean();

    if (message_value.isEmptyOrUndefinedOrNull() || !message_value.isString()) {
        return globalThis.throw("publishText requires a non-empty message", .{});
    }

    let js_string = message_value.toJSString(globalThis)?;
    const view = js_string.view(globalThis);
    let slice = view.toSlice(bun.default_allocator);
    const _slice_guard = DeinitGuard<@TypeOf(slice)>{ ptr: unsafe { &slice } };

    const _js_str_guard = EnsureAliveGuard<@TypeOf(js_string)>{ ptr: js_string };

    if (_.#handler.app == null) {
        log("publish() closed", .{});
        return JSValue.jsNumber(0);
    }
    const app = _.#handler.app.?;
    const flags = _.#handler.flags;
    const ssl = flags.ssl;
    const publish_to_self = flags.publish_to_self;

    const buffer = slice.slice();

    const result = if (!publish_to_self && !_.isClosed())
        _.websocket().publish(topic_slice.slice(), buffer, .text, compress)
    else
        uws.AnyWebSocket.publishWithOptions(ssl, app, topic_slice.slice(), buffer, .text, compress);

    return JSValue.jsNumber(
        // if 0, return 0
        // else return number of bytes sent
        if (result) @as(i32, @as(i32, @as(u31, @truncate(buffer.len)))) else @as(i32, 0),
    );
}

fn publishBinary(
    *_,
    globalThis: *jsc.JSGlobalObject,
    callframe: *jsc.CallFrame,
): JSValue ! bun.JSError {
    const args = callframe.arguments_old(4);

    if (args.len < 1) {
        log("publishBinary()", .{});
        return globalThis.throw("publishBinary requires at least 1 argument", .{});
    }

    const topic_value = args.ptr[0];
    const message_value = args.ptr[1];
    const compress_value = args.ptr[2];

    if (topic_value.isEmptyOrUndefinedOrNull() || !topic_value.isString()) {
        log("publishBinary() topic invalid", .{});
        return globalThis.throw("publishBinary requires a topic string", .{});
    }

    let topic_slice = topic_value.toSlice(globalThis, bun.default_allocator)?;
    const _topic_guard = DeinitGuard<@TypeOf(topic_slice)>{ ptr: unsafe { &topic_slice } };
    if (topic_slice.len == 0) {
        return globalThis.throw("publishBinary requires a non-empty topic", .{});
    }

    if (!compress_value.isBoolean() && !compress_value.isUndefined() && compress_value != .zero) {
        return globalThis.throw("publishBinary expects compress to be a boolean", .{});
    }

    const compress = args.len > 1 && compress_value.toBoolean();

    if (message_value.isEmptyOrUndefinedOrNull()) {
        return globalThis.throw("publishBinary requires a non-empty message", .{});
    }

    const array_buffer_opt = message_value.asArrayBuffer(globalThis);
    if (array_buffer_opt == null) {
        return globalThis.throw("publishBinary expects an ArrayBufferView", .{});
    }
    const array_buffer = array_buffer_opt.?;
    const buffer = array_buffer.slice();

    if (_.#handler.app == null) {
        log("publish() closed", .{});
        return JSValue.jsNumber(0);
    }
    const app = _.#handler.app.?;
    const flags = _.#handler.flags;
    const ssl = flags.ssl;
    const publish_to_self = flags.publish_to_self;

    const result = if (!publish_to_self && !_.isClosed())
        _.websocket().publish(topic_slice.slice(), buffer, .binary, compress)
    else
        uws.AnyWebSocket.publishWithOptions(ssl, app, topic_slice.slice(), buffer, .binary, compress);

    return JSValue.jsNumber(
        // if 0, return 0
        // else return number of bytes sent
        if (result) @as(i32, @as(i32, @as(u31, @truncate(buffer.len)))) else @as(i32, 0),
    );
}

fn publishBinaryWithoutTypeChecks(
    *_,
    globalThis: *jsc.JSGlobalObject,
    topic_str: *jsc.JSString,
    array: *jsc.JSUint8Array,
): jsc.JSValue ! bun.JSError {
    if (_.#handler.app == null) {
        log("publish() closed", .{});
        return JSValue.jsNumber(0);
    }
    const app = _.#handler.app.?;
    const flags = _.#handler.flags;
    const ssl = flags.ssl;
    const publish_to_self = flags.publish_to_self;

    let topic_slice = topic_str.toSlice(globalThis, bun.default_allocator);
    const _topic_guard = DeinitGuard<@TypeOf(topic_slice)>{ ptr: unsafe { &topic_slice } };
    if (topic_slice.len == 0) {
        return globalThis.throw("publishBinary requires a non-empty topic", .{});
    }

    const compress = true;

    const buffer = array.slice();
    if (buffer.len == 0) {
        return jsc.JSValue.jsNumber(0);
    }

    const result = if (!publish_to_self && !_.isClosed())
        _.websocket().publish(topic_slice.slice(), buffer, .binary, compress)
    else
        uws.AnyWebSocket.publishWithOptions(ssl, app, topic_slice.slice(), buffer, .binary, compress);

    return JSValue.jsNumber(
        // if 0, return 0
        // else return number of bytes sent
        if (result) @as(i32, @as(i32, @as(u31, @truncate(buffer.len)))) else @as(i32, 0),
    );
}

fn publishTextWithoutTypeChecks(
    *_,
    globalThis: *jsc.JSGlobalObject,
    topic_str: *jsc.JSString,
    str: *jsc.JSString,
): jsc.JSValue ! bun.JSError {
    if (_.#handler.app == null) {
        log("publish() closed", .{});
        return JSValue.jsNumber(0);
    }
    const app = _.#handler.app.?;
    const flags = _.#handler.flags;
    const ssl = flags.ssl;
    const publish_to_self = flags.publish_to_self;

    let topic_slice = topic_str.toSlice(globalThis, bun.default_allocator);
    const _topic_guard = DeinitGuard<@TypeOf(topic_slice)>{ ptr: unsafe { &topic_slice } };
    if (topic_slice.len == 0) {
        return globalThis.throw("publishBinary requires a non-empty topic", .{});
    }

    const compress = true;

    let slice = str.toSlice(globalThis, bun.default_allocator);
    const _slice_guard = DeinitGuard<@TypeOf(slice)>{ ptr: unsafe { &slice } };
    const buffer = slice.slice();

    if (buffer.len == 0) {
        return jsc.JSValue.jsNumber(0);
    }

    const result = if (!publish_to_self && !_.isClosed())
        _.websocket().publish(topic_slice.slice(), buffer, .text, compress)
    else
        uws.AnyWebSocket.publishWithOptions(ssl, app, topic_slice.slice(), buffer, .text, compress);

    return JSValue.jsNumber(
        // if 0, return 0
        // else return number of bytes sent
        if (result) @as(i32, @as(i32, @as(u31, @truncate(buffer.len)))) else @as(i32, 0),
    );
}

fn cork(
    *_,
    globalThis: *jsc.JSGlobalObject,
    callframe: *jsc.CallFrame,
    this_value: jsc.JSValue,
): JSValue ! bun.JSError {
    const args = callframe.arguments_old(1);

    if (args.len < 1) {
        return globalThis.throwNotEnoughArguments("cork", 1, 0);
    }

    const callback = args.ptr[0];
    if (callback.isEmptyOrUndefinedOrNull() || !callback.isCallable()) {
        return globalThis.throwInvalidArgumentTypeValue("cork", "callback", callback);
    }

    if (_.isClosed()) {
        return JSValue.js_undefined;
    }

    let corker = Corker{
        globalObject: globalThis,
        this_value: this_value,
        callback: callback,
    };
    _.websocket().cork(&corker, Corker.run);

    const result = corker.result;

    if (result.isAnyError()) {
        return globalThis.throwValue(result);
    }

    return result;
}

fn send(
    *_,
    globalThis: *jsc.JSGlobalObject,
    callframe: *jsc.CallFrame,
): JSValue ! bun.JSError {
    const args = callframe.arguments_old(2);

    if (args.len < 1) {
        log("send()", .{});
        return globalThis.throw("send requires at least 1 argument", .{});
    }

    if (_.isClosed()) {
        log("send() closed", .{});
        return JSValue.jsNumber(0);
    }

    const message_value = args.ptr[0];
    const compress_value = args.ptr[1];

    if (!compress_value.isBoolean() && !compress_value.isUndefined() && compress_value != .zero) {
        return globalThis.throw("send expects compress to be a boolean", .{});
    }

    const compress = args.len > 1 && compress_value.toBoolean();

    if (message_value.isEmptyOrUndefinedOrNull()) {
        return globalThis.throw("send requires a non-empty message", .{});
    }

    if (buffer := message_value.asArrayBuffer(globalThis)) {
        switch (_.websocket().send(buffer.slice(), .binary, compress, true)) {
            .backpressure => {
                log("send() backpressure ({d} bytes)", .{buffer.len});
                return JSValue.jsNumber(-1);
            },
            .success => {
                log("send() success ({d} bytes)", .{buffer.len});
                return JSValue.jsNumber(buffer.slice().len);
            },
            .dropped => {
                log("send() dropped ({d} bytes)", .{buffer.len});
                return JSValue.jsNumber(0);
            },
        }
    }

    {
        let js_string = message_value.toJSString(globalThis)?;
        const view = js_string.view(globalThis);
        let slice = view.toSlice(bun.default_allocator);
        const _slice_guard = DeinitGuard<@TypeOf(slice)>{ ptr: unsafe { &slice } };

        const _js_str_guard = EnsureAliveGuard<@TypeOf(js_string)>{ ptr: js_string };

        const buffer = slice.slice();
        switch (_.websocket().send(buffer, .text, compress, true)) {
            .backpressure => {
                log("send() backpressure ({d} bytes string)", .{buffer.len});
                return JSValue.jsNumber(-1);
            },
            .success => {
                log("send() success ({d} bytes string)", .{buffer.len});
                return JSValue.jsNumber(buffer.len);
            },
            .dropped => {
                log("send() dropped ({d} bytes string)", .{buffer.len});
                return JSValue.jsNumber(0);
            },
        }
    }
}

fn sendText(
    *_,
    globalThis: *jsc.JSGlobalObject,
    callframe: *jsc.CallFrame,
): JSValue ! bun.JSError {
    const args = callframe.arguments_old(2);

    if (args.len < 1) {
        log("sendText()", .{});
        return globalThis.throw("sendText requires at least 1 argument", .{});
    }

    if (_.isClosed()) {
        log("sendText() closed", .{});
        return JSValue.jsNumber(0);
    }

    const message_value = args.ptr[0];
    const compress_value = args.ptr[1];

    if (!compress_value.isBoolean() && !compress_value.isUndefined() && compress_value != .zero) {
        return globalThis.throw("sendText expects compress to be a boolean", .{});
    }

    const compress = args.len > 1 && compress_value.toBoolean();

    if (message_value.isEmptyOrUndefinedOrNull() || !message_value.isString()) {
        return globalThis.throw("sendText expects a string", .{});
    }
    let js_string = message_value.toJSString(globalThis)?;
    const view = js_string.view(globalThis);
    let slice = view.toSlice(bun.default_allocator);
    const _slice_guard = DeinitGuard<@TypeOf(slice)>{ ptr: unsafe { &slice } };

    const _js_str_guard = EnsureAliveGuard<@TypeOf(js_string)>{ ptr: js_string };

    const buffer = slice.slice();
    switch (_.websocket().send(buffer, .text, compress, true)) {
        .backpressure => {
            log("sendText() backpressure ({d} bytes string)", .{buffer.len});
            return JSValue.jsNumber(-1);
        },
        .success => {
            log("sendText() success ({d} bytes string)", .{buffer.len});
            return JSValue.jsNumber(buffer.len);
        },
        .dropped => {
            log("sendText() dropped ({d} bytes string)", .{buffer.len});
            return JSValue.jsNumber(0);
        },
    }
}

fn sendTextWithoutTypeChecks(
    *_,
    globalThis: *jsc.JSGlobalObject,
    message_str: *jsc.JSString,
    compress: bool,
): JSValue {
    if (_.isClosed()) {
        log("sendText() closed", .{});
        return JSValue.jsNumber(0);
    }

    let string_slice = message_str.toSlice(globalThis, bun.default_allocator);
    const _slice_guard = DeinitGuard<@TypeOf(string_slice)>{ ptr: unsafe { &string_slice } };

    const buffer = string_slice.slice();
    switch (_.websocket().send(buffer, .text, compress, true)) {
        .backpressure => {
            log("sendText() backpressure ({d} bytes string)", .{buffer.len});
            return JSValue.jsNumber(-1);
        },
        .success => {
            log("sendText() success ({d} bytes string)", .{buffer.len});
            return JSValue.jsNumber(buffer.len);
        },
        .dropped => {
            log("sendText() dropped ({d} bytes string)", .{buffer.len});
            return JSValue.jsNumber(0);
        },
    }
}

fn sendBinary(
    *_,
    globalThis: *jsc.JSGlobalObject,
    callframe: *jsc.CallFrame,
): JSValue ! bun.JSError {
    const args = callframe.arguments_old(2);

    if (args.len < 1) {
        log("sendBinary()", .{});
        return globalThis.throw("sendBinary requires at least 1 argument", .{});
    }

    if (_.isClosed()) {
        log("sendBinary() closed", .{});
        return JSValue.jsNumber(0);
    }

    const message_value = args.ptr[0];
    const compress_value = args.ptr[1];

    if (!compress_value.isBoolean() && !compress_value.isUndefined() && compress_value != .zero) {
        return globalThis.throw("sendBinary expects compress to be a boolean", .{});
    }

    const compress = args.len > 1 && compress_value.toBoolean();

    const buffer_opt = message_value.asArrayBuffer(globalThis);
    if (buffer_opt == null) {
        return globalThis.throw("sendBinary requires an ArrayBufferView", .{});
    }
    const buffer = buffer_opt.?;

    switch (_.websocket().send(buffer.slice(), .binary, compress, true)) {
        .backpressure => {
            log("sendBinary() backpressure ({d} bytes)", .{buffer.len});
            return JSValue.jsNumber(-1);
        },
        .success => {
            log("sendBinary() success ({d} bytes)", .{buffer.len});
            return JSValue.jsNumber(buffer.slice().len);
        },
        .dropped => {
            log("sendBinary() dropped ({d} bytes)", .{buffer.len});
            return JSValue.jsNumber(0);
        },
    }
}

fn sendBinaryWithoutTypeChecks(
    *_,
    _: *jsc.JSGlobalObject,
    array_buffer: *jsc.JSUint8Array,
    compress: bool,
): JSValue {
    if (_.isClosed()) {
        log("sendBinary() closed", .{});
        return JSValue.jsNumber(0);
    }

    const buffer = array_buffer.slice();

    switch (_.websocket().send(buffer, .binary, compress, true)) {
        .backpressure => {
            log("sendBinary() backpressure ({d} bytes)", .{buffer.len});
            return JSValue.jsNumber(-1);
        },
        .success => {
            log("sendBinary() success ({d} bytes)", .{buffer.len});
            return JSValue.jsNumber(buffer.len);
        },
        .dropped => {
            log("sendBinary() dropped ({d} bytes)", .{buffer.len});
            return JSValue.jsNumber(0);
        },
    }
}

fn ping(
    *_,
    globalThis: *jsc.JSGlobalObject,
    callframe: *jsc.CallFrame,
): JSValue ! bun.JSError {
    return _.sendPing(globalThis, callframe, "ping", .ping);
}

fn pong(
    *_,
    globalThis: *jsc.JSGlobalObject,
    callframe: *jsc.CallFrame,
): JSValue ! bun.JSError {
    return _.sendPing(globalThis, callframe, "pong", .pong);
}

const MAX_CONTROL_FRAME_PAYLOAD: usize = 125;

fn throwControlFrameTooLarge(globalThis: *jsc.JSGlobalObject, len: usize): bun.JSError {
    return globalThis.throwValue(globalThis.createRangeError(
        "The data size must not be greater than {d} bytes. Received {d} bytes.",
        .{ MAX_CONTROL_FRAME_PAYLOAD, len },
    ));
}

inline fn sendPing(
    *_,
    globalThis: *jsc.JSGlobalObject,
    callframe: *jsc.CallFrame,
    comptime name: string,
    comptime opcode: uws.Opcode,
): JSValue ! bun.JSError {
    const args = callframe.arguments_old(2);

    if (_.isClosed()) {
        return JSValue.jsNumber(0);
    }

    if (args.len > 0) {
        let value = args.ptr[0];
        if (!value.isEmptyOrUndefinedOrNull()) {
            if (data := value.asArrayBuffer(globalThis)) {
                const buffer = data.slice();
                if (buffer.len > MAX_CONTROL_FRAME_PAYLOAD) {
                    return throwControlFrameTooLarge(globalThis, buffer.len);
                }

                switch (_.websocket().send(buffer, opcode, false, true)) {
                    .backpressure => {
                        log("{s}() backpressure ({d} bytes)", .{ name, buffer.len });
                        return JSValue.jsNumber(-1);
                    },
                    .success => {
                        log("{s}() success ({d} bytes)", .{ name, buffer.len });
                        return JSValue.jsNumber(buffer.len);
                    },
                    .dropped => {
                        log("{s}() dropped ({d} bytes)", .{ name, buffer.len });
                        return JSValue.jsNumber(0);
                    },
                }
            } else if (value.isString()) {
                const js_str = value.toJSString(globalThis)?;
                let string_value = js_str.toSlice(globalThis, bun.default_allocator);
                const _str_guard = DeinitGuard<@TypeOf(string_value)>{ ptr: unsafe { &string_value } };
                const buffer = string_value.slice();
                if (buffer.len > MAX_CONTROL_FRAME_PAYLOAD) {
                    return throwControlFrameTooLarge(globalThis, buffer.len);
                }

                switch (_.websocket().send(buffer, opcode, false, true)) {
                    .backpressure => {
                        log("{s}() backpressure ({d} bytes)", .{ name, buffer.len });
                        return JSValue.jsNumber(-1);
                    },
                    .success => {
                        log("{s}() success ({d} bytes)", .{ name, buffer.len });
                        return JSValue.jsNumber(buffer.len);
                    },
                    .dropped => {
                        log("{s}() dropped ({d} bytes)", .{ name, buffer.len });
                        return JSValue.jsNumber(0);
                    },
                }
            } else {
                return globalThis.throwPretty("{s} requires a string or BufferSource", .{name});
            }
        }
    }

    switch (_.websocket().send([], opcode, false, true)) {
        .backpressure => {
            log("{s}() backpressure ({d} bytes)", .{ name, 0 });
            return JSValue.jsNumber(-1);
        },
        .success => {
            log("{s}() success ({d} bytes)", .{ name, 0 });
            return JSValue.jsNumber(0);
        },
        .dropped => {
            log("{s}() dropped ({d} bytes)", .{ name, 0 });
            return JSValue.jsNumber(0);
        },
    }
}

fn getData(
    *_,
    _: *jsc.JSGlobalObject,
): JSValue {
    log("getData()", .{});
    if (this_value := _.#this_value.tryGet()) {
        return js.dataGetCached(this_value) ?? .js_undefined;
    }
    return .js_undefined;
}

fn setData(
    *_,
    globalObject: *jsc.JSGlobalObject,
    value: jsc.JSValue,
) {
    log("setData()", .{});
    if (this_value := _.#this_value.tryGet()) {
        js.dataSetCached(this_value, globalObject, value);
    }
}

fn getReadyState(
    *_,
    _: *jsc.JSGlobalObject,
): JSValue {
    log("getReadyState()", .{});

    if (_.isClosed()) {
        return JSValue.jsNumber(3);
    }

    return JSValue.jsNumber(1);
}

fn close(
    *_,
    globalThis: *jsc.JSGlobalObject,
    callframe: *jsc.CallFrame,
    // Since close() can lead to the close() callback being called, let's always ensure the `this` value is up to date.
    _: jsc.JSValue,
): JSValue ! bun.JSError {
    const args = callframe.arguments_old(2);
    log("close()", .{});

    if (_.isClosed()) {
        return JSValue.js_undefined;
    }

    const code = brk: {
        if (args.ptr[0] == .zero || args.ptr[0].isUndefined()) {
            // default exception code
            break :brk 1000;
        }

        if (!args.ptr[0].isNumber()) {
            return globalThis.throwInvalidArguments("close requires a numeric code or undefined", .{});
        }

        break :brk args.ptr[0].coerce(i32, globalThis)?;
    };

    let message_value: ZigString.Slice = brk: {
        if (args.ptr[1] == .zero || args.ptr[1].isUndefined()) break :brk ZigString.Slice.empty;
        break :brk args.ptr[1].toSliceOrNull(globalThis)?;
    };

    const _msg_guard = DeinitGuard<@TypeOf(message_value)>{ ptr: unsafe { &message_value } };

    _.#flags.closed = true;
    _.websocket().end(code, message_value.slice());
    return JSValue.js_undefined;
}

fn terminate(
    *_,
    _: *jsc.JSGlobalObject,
    _: *jsc.CallFrame,
    _: jsc.JSValue,
): JSValue ! bun.JSError {
    log("terminate()", .{});

    if (_.isClosed()) {
        return JSValue.js_undefined;
    }

    _.#flags.closed = true;
    _.websocket().close();

    return JSValue.js_undefined;
}

fn getBinaryType(
    *_,
    globalThis: *jsc.JSGlobalObject,
): JSValue ! bun.JSError {
    log("getBinaryType()", .{});

    return switch (_.#flags.binary_type) {
        .Uint8Array => globalThis.commonStrings().uint8array(),
        .Buffer => globalThis.commonStrings().nodebuffer(),
        .ArrayBuffer => globalThis.commonStrings().arraybuffer(),
        else => @panic("Invalid binary type"),
    };
}

fn setBinaryType(*_, globalThis: *jsc.JSGlobalObject, value: jsc.JSValue) ! bun.JSError {
    log("setBinaryType()", .{});

    const btype = jsc.ArrayBuffer.BinaryType.fromJSValue(globalThis, value)?;
    switch (btype ??
        // some other value which we don't support
        .Float64Array) {
        .ArrayBuffer, .Buffer, .Uint8Array => |val| {
            _.#flags.binary_type = val;
            return;
        },
        else => {
            return globalThis.throw("binaryType must be either \"uint8array\" or \"arraybuffer\" or \"nodebuffer\"", .{});
        },
    }
}

fn getBufferedAmount(
    *_,
    _: *jsc.JSGlobalObject,
    _: *jsc.CallFrame,
): JSValue ! bun.JSError {
    log("getBufferedAmount()", .{});

    if (_.isClosed()) {
        return JSValue.jsNumber(0);
    }

    return JSValue.jsNumber(_.websocket().getBufferedAmount());
}
fn subscribe(
    *_,
    globalThis: *jsc.JSGlobalObject,
    callframe: *jsc.CallFrame,
): JSValue ! bun.JSError {
    const args = callframe.arguments_old(1);
    if (args.len < 1) {
        return globalThis.throw("subscribe requires at least 1 argument", .{});
    }

    if (_.isClosed()) {
        return JSValue.true;
    }

    if (!args.ptr[0].isString()) {
        return globalThis.throwInvalidArgumentTypeValue("topic", "string", args.ptr[0]);
    }

    let topic = args.ptr[0].toSlice(globalThis, bun.default_allocator)?;
    const _topic_guard = DeinitGuard<@TypeOf(topic)>{ ptr: unsafe { &topic } };

    if (topic.len == 0) {
        return globalThis.throw("subscribe requires a non-empty topic name", .{});
    }

    return JSValue.jsBoolean(_.websocket().subscribe(topic.slice()));
}
fn unsubscribe(*_, globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): JSValue ! bun.JSError {
    const args = callframe.arguments_old(1);
    if (args.len < 1) {
        return globalThis.throw("unsubscribe requires at least 1 argument", .{});
    }

    if (_.isClosed()) {
        return JSValue.true;
    }

    if (!args.ptr[0].isString()) {
        return globalThis.throwInvalidArgumentTypeValue("topic", "string", args.ptr[0]);
    }

    let topic = args.ptr[0].toSlice(globalThis, bun.default_allocator)?;
    const _topic_guard = DeinitGuard<@TypeOf(topic)>{ ptr: unsafe { &topic } };

    if (topic.len == 0) {
        return globalThis.throw("unsubscribe requires a non-empty topic name", .{});
    }

    return JSValue.jsBoolean(_.websocket().unsubscribe(topic.slice()));
}
fn isSubscribed(
    *_,
    globalThis: *jsc.JSGlobalObject,
    callframe: *jsc.CallFrame,
): JSValue ! bun.JSError {
    const args = callframe.arguments_old(1);
    if (args.len < 1) {
        return globalThis.throw("isSubscribed requires at least 1 argument", .{});
    }

    if (_.isClosed()) {
        return JSValue.false;
    }

    if (!args.ptr[0].isString()) {
        return globalThis.throwInvalidArgumentTypeValue("topic", "string", args.ptr[0]);
    }

    let topic = args.ptr[0].toSlice(globalThis, bun.default_allocator)?;
    const _topic_guard = DeinitGuard<@TypeOf(topic)>{ ptr: unsafe { &topic } };

    if (topic.len == 0) {
        return globalThis.throw("isSubscribed requires a non-empty topic name", .{});
    }

    return JSValue.jsBoolean(_.websocket().isSubscribed(topic.slice()));
}

fn getSubscriptions(
    *_,
    globalThis: *jsc.JSGlobalObject,
): JSValue ! bun.JSError {
    if (_.isClosed()) {
        return JSValue.createEmptyArray(globalThis, 0)?;
    }

    // Get the JSValue directly from C++
    return _.websocket().getTopicsAsJSArray(globalThis);
}

fn getRemoteAddress(
    *_,
    globalThis: *jsc.JSGlobalObject,
): JSValue ! bun.JSError {
    if (_.isClosed()) {
        return JSValue.js_undefined;
    }

    let buf: [64]u8 = std.mem.zeroes([64]u8);
    let text_buf: [512]u8 = undefined;

    const address_bytes = _.websocket().getRemoteAddress(&buf);
    const address: std.net.Address = switch (address_bytes.len) {
        4 => std.net.Address.initIp4(address_bytes[0..4].*, 0),
        16 => std.net.Address.initIp6(address_bytes[0..16].*, 0, 0, 0),
        else => return JSValue.js_undefined,
    };

    const text = bun.fmt.formatIp(address, &text_buf) catch unreachable;
    return bun.String.createUTF8ForJS(globalThis, text);
}

}
