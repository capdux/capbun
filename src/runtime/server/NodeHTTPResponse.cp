import bun from "bun";

const Environment = bun.Environment;
struct ServerWebSocketPtrHolder { p: *ServerWebSocket }
const HTTP = bun.http;
const Output = bun.Output;
const uws = bun.uws;
const HTTPStatusText = bun.api.server.HTTPStatusText;

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const ZigString = jsc.ZigString;
const AutoFlusher = jsc.WebCore.AutoFlusher;

const AnyServer = jsc.API.AnyServer;
const ServerWebSocket = jsc.API.ServerWebSocket;

const log = bun.Output.scoped(.NodeHTTPResponse, .visible);

@c {
    fn Bun__getNodeHTTPResponseThisValue(is_ssl: bool, socket: *!): jsc.JSValue;
    fn Bun__getNodeHTTPServerSocketThisValue(is_ssl: bool, socket: *!): jsc.JSValue;
    fn NodeHTTPServer__writeHead_http(
        globalObject: *jsc.JSGlobalObject,
        statusMessage: [*]const u8,
        statusMessageLength: usize,
        headersObjectValue: jsc.JSValue,
        response: *!,
    );
    fn NodeHTTPServer__writeHead_https(
        globalObject: *jsc.JSGlobalObject,
        statusMessage: [*]const u8,
        statusMessageLength: usize,
        headersObjectValue: jsc.JSValue,
        response: *!,
    );
}

struct DerefGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.deref(); }
    }
}

struct DeinitGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.deinit(); }
    }
}

struct OptionalDeinitGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe {
            if (*s := _.ptr.*) s.deinit();
        }
    }
}

struct FreeGuard<T> {
    allocator: std.mem.Allocator,
    slice: []const T,
    ~init() {
        _.allocator.free(_.slice);
    }
}

struct ClearAndFreeGuard<T> {
    ptr: *T!,
    allocator: std.mem.Allocator,
    ~init() {
        unsafe { _.ptr.clearAndFree(_.allocator); }
    }
}

struct UpgradeCleanupGuard {
    resp: *NodeHTTPResponse!,
    ~init() {
        unsafe {
            _.resp.setOnAbortedHandler();
            _.resp.upgrade_context.deinit();
        }
    }
}

struct OnDataLastGuard {
    resp: *NodeHTTPResponse!,
    last: bool,
    ~init() {
        if (_.last) {
            unsafe {
                if (_.resp.body_read_ref.has) {
                    _.resp.body_read_ref.unref(jsc.VirtualMachine.get());
                    _.resp.markRequestAsDoneIfNecessary();
                }
                _.resp.deref();
            }
        }
    }
}

struct BodyReadUnrefGuard {
    resp: *NodeHTTPResponse!,
    vm: *jsc.VirtualMachine!,
    ~init() {
        unsafe {
            if (_.resp.body_read_ref.has) {
                _.resp.body_read_ref.unref(_.vm);
            }
        }
    }
}

struct MarkDoneOnAbortGuard {
    resp: *NodeHTTPResponse!,
    is_abort: bool,
    ~init() {
        if (_.is_abort) {
            unsafe { _.resp.markRequestAsDoneIfNecessary(); }
        }
    }
}

pub @packed(u8) struct Flags {
    socket_closed: bool = false,
    request_has_completed: bool = false,
    ended: bool = false,
    upgraded: bool = false,
    hasCustomOnData: bool = false,
    is_request_pending: bool = true,
    is_data_buffered_during_pause: bool = false,
    /// Did we receive the last chunk of data during pause?
    is_data_buffered_during_pause_last: bool = false,

    /// Did the user end the request?
    fn isRequestedCompletedOrEnded(): bool {
        return _.request_has_completed || _.ended;
    }

    fn isDone(): bool {
        return _.isRequestedCompletedOrEnded() || _.socket_closed;
    }
}

pub struct UpgradeCTX {
    context: (*uws.WebSocketUpgradeContext)? = null,
    // request will be detached when go async
    request: (*uws.Request)? = null,

    // we need to store this, if we wanna to enable async upgrade
    sec_websocket_key: []const u8 = "",
    sec_websocket_protocol: []const u8 = "",
    sec_websocket_extensions: []const u8 = "",

    // this can be called multiple times
    fn deinit(*_) {
        const sec_websocket_key = _.sec_websocket_key;
        const sec_websocket_protocol = _.sec_websocket_protocol;
        const sec_websocket_extensions = _.sec_websocket_extensions;
        _.* = {};
        if (sec_websocket_extensions.len > 0) bun.default_allocator.free(sec_websocket_extensions);
        if (sec_websocket_protocol.len > 0) bun.default_allocator.free(sec_websocket_protocol);
        if (sec_websocket_key.len > 0) bun.default_allocator.free(sec_websocket_key);
    }

    fn preserveWebSocketHeadersIfNeeded(*_) {
        if (request := _.request) {
            _.request = null;

            const sec_websocket_key = request.header("sec-websocket-key") ?? "";
            const sec_websocket_protocol = request.header("sec-websocket-protocol") ?? "";
            const sec_websocket_extensions = request.header("sec-websocket-extensions") ?? "";

            if (sec_websocket_key.len > 0) {
                _.sec_websocket_key = bun.handleOom(bun.default_allocator.dupe(u8, sec_websocket_key));
            }
            if (sec_websocket_protocol.len > 0) {
                _.sec_websocket_protocol = bun.handleOom(bun.default_allocator.dupe(u8, sec_websocket_protocol));
            }
            if (sec_websocket_extensions.len > 0) {
                _.sec_websocket_extensions = bun.handleOom(bun.default_allocator.dupe(u8, sec_websocket_extensions));
            }
        }
    }
}

pub enum BodyReadState: u8 {
    none = 0,
    pending = 1,
    done = 2,
}

pub enum AbortEvent: u8 {
    none = 0,
    abort = 1,
    timeout = 2,
}

@root struct NodeHTTPResponse {

pub const js = jsc.Codegen.JSNodeHTTPResponse;
pub const toJS = js.toJS;
pub const fromJS = js.fromJS;
pub const fromJSDirect = js.fromJSDirect;

const RefCount = bun.ptr.RefCount(@This(), "ref_count", deinit, {});
pub const ref = RefCount.ref;
pub const deref = RefCount.deref;

ref_count: RefCount,

raw_response: uws.AnyResponse?,

flags: Flags = {},

poll_ref: jsc.Ref = {},

body_read_state: BodyReadState = .none,
body_read_ref: jsc.Ref = {},
promise: jsc.Strong.Optional = .empty,
server: AnyServer,

/// When you call pause() on the node:http IncomingMessage
/// We might've already read from the socket.
/// So we need to buffer that data.
/// This should be pretty uncommon though.
buffered_request_body_data_during_pause: bun.ByteList = {},
bytes_written: usize = 0,

upgrade_context: UpgradeCTX = {},

auto_flusher: AutoFlusher = {},

static fn getThisValue(client: *NodeHTTPResponse): jsc.JSValue {
    if (client.flags.socket_closed || client.flags.upgraded || client.raw_response == null) {
        return .zero;
    }

    return Bun__getNodeHTTPResponseThisValue(client.raw_response.? == .SSL, client.raw_response.?.socket());
}

static fn getServerSocketValue(client: *NodeHTTPResponse): jsc.JSValue {
    if (client.flags.socket_closed || client.flags.upgraded || client.raw_response == null) {
        return .zero;
    }
    return Bun__getNodeHTTPServerSocketThisValue(client.raw_response.? == .SSL, client.raw_response.?.socket());
}

static fn pauseSocket(client: *NodeHTTPResponse) {
    log("pauseSocket", {});
    if (client.flags.socket_closed || client.flags.upgraded || client.raw_response == null || client.raw_response.?.isConnectRequest()) {
        return;
    }

    client.raw_response.?.pause();
}

static fn resumeSocket(client: *NodeHTTPResponse) {
    log("resumeSocket", {});
    if (client.flags.socket_closed || client.flags.upgraded || client.raw_response == null || client.raw_response.?.isConnectRequest()) {
        return;
    }

    client.raw_response.?.@"resume"();
}

static fn upgrade(client: *NodeHTTPResponse, data_value: JSValue, sec_websocket_protocol: ZigString, sec_websocket_extensions: ZigString): bool {
    const upgrade_ctx = client.upgrade_context.context ?? return false;
    const ws_handler = client.server.webSocketHandler() ?? return false;
    const socketValue = client.getServerSocketValue();
    if (socketValue == .zero) {
        return false;
    }
    resumeSocket(client);

    const _guard_upgrade = UpgradeCleanupGuard{ resp: client };
    data_value.ensureStillAlive();

    const ws = ServerWebSocket.init(ws_handler, data_value, null);

    let sec_websocket_protocol_str: ZigString.Slice? = null;
    const _guard_proto = OptionalDeinitGuard<ZigString.Slice?>{ ptr: &sec_websocket_protocol_str };
    let sec_websocket_extensions_str: ZigString.Slice? = null;
    const _guard_ext = OptionalDeinitGuard<ZigString.Slice?>{ ptr: &sec_websocket_extensions_str };

    const sec_websocket_protocol_value = brk: {
        if (sec_websocket_protocol.isEmpty()) {
            if (request := client.upgrade_context.request) {
                break :brk request.header("sec-websocket-protocol") ?? "";
            } else {
                break :brk client.upgrade_context.sec_websocket_protocol;
            }
        }
        sec_websocket_protocol_str = sec_websocket_protocol.toSlice(bun.default_allocator);
        break :brk sec_websocket_protocol_str.?.slice();
    };

    const sec_websocket_extensions_value = brk: {
        if (sec_websocket_extensions.isEmpty()) {
            if (request := client.upgrade_context.request) {
                break :brk request.header("sec-websocket-extensions") ?? "";
            } else {
                break :brk client.upgrade_context.sec_websocket_extensions;
            }
        }
        sec_websocket_extensions_str = sec_websocket_extensions.toSlice(bun.default_allocator);
        break :brk sec_websocket_extensions_str.?.slice();
    };

    let websocket_key = client.upgrade_context.sec_websocket_key;
    if (request := client.upgrade_context.request) {
        websocket_key = request.header("sec-websocket-key") ?? "";
    }

    if (raw_response := client.raw_response) {
        client.raw_response = null;
        client.flags.upgraded = true;
        // Unref the poll_ref since the socket is now upgraded to WebSocket
        // and will have its own lifecycle management
        client.poll_ref.unref(client.server.globalThis().bunVM());
        if (raw_response == .SSL) {
            * = raw_response.SSL.upgrade(@TypeOf(@as(ServerWebSocketPtrHolder, undefined).p), ws, websocket_key, sec_websocket_protocol_value, sec_websocket_extensions_value, upgrade_ctx);
        } else {
            * = raw_response.TCP.upgrade(@TypeOf(@as(ServerWebSocketPtrHolder, undefined).p), ws, websocket_key, sec_websocket_protocol_value, sec_websocket_extensions_value, upgrade_ctx);
        }
    }
    return true;
}
pub fn maybeStopReadingBody(*_, vm: *jsc.VirtualMachine, thisValue: jsc.JSValue) {
    _.upgrade_context.deinit(); // we can discard the upgrade context now
    const tv = thisValue;

    if ((_.flags.upgraded || _.flags.socket_closed || _.flags.ended) &&
        (_.body_read_ref.has || _.body_read_state == .pending) &&
        (!_.flags.hasCustomOnData || js.onDataGetCached(tv) == null))
    {
        const had_ref = _.body_read_ref.has;
        if (!_.flags.upgraded && !_.flags.socket_closed) {
            log("clearOnData", {});
            if (raw_response := _.raw_response) {
                raw_response.clearOnData();
            }
        }

        _.body_read_ref.unref(vm);
        _.body_read_state = .done;

        if (had_ref) {
            _.markRequestAsDoneIfNecessary();
        }
    }
}

static fn shouldRequestBePending(client: *const NodeHTTPResponse): bool {
    if (client.flags.socket_closed) {
        return false;
    }

    if (client.flags.ended) {
        return client.body_read_state == .pending;
    }

    return true;
}

static fn dumpRequestBody(client: *NodeHTTPResponse, globalObject: *jsc.JSGlobalObject, _: *jsc.CallFrame, thisValue: jsc.JSValue): bun.JSError!jsc.JSValue {
    if (client.buffered_request_body_data_during_pause.cap > 0) {
        client.buffered_request_body_data_during_pause.clearAndFree(bun.default_allocator);
    }
    if (!client.flags.request_has_completed) {
        client.clearOnDataCallback(thisValue, globalObject);
    }

    return .js_undefined;
}

static fn markRequestAsDone(client: *NodeHTTPResponse) {
    log("markRequestAsDone()", {});
    const _guard_deref = DerefGuard<NodeHTTPResponse>{ ptr: client };
    client.flags.is_request_pending = false;

    client.clearOnDataCallback(client.getThisValue(), jsc.VirtualMachine.get().global);
    client.upgrade_context.deinit();

    client.buffered_request_body_data_during_pause.clearAndFree(bun.default_allocator);
    const server = client.server;
    client.poll_ref.unref(jsc.VirtualMachine.get());
    client.unregisterAutoFlush();

    server.onRequestComplete();
}

fn markRequestAsDoneIfNecessary(*_) {
    if (_.flags.is_request_pending && !_.shouldRequestBePending()) {
        _.markRequestAsDone();
    }
}

static fn create(
    any_server_tag: u64,
    globalObject: *jsc.JSGlobalObject,
    has_body: *bool,
    request: *uws.Request,
    is_ssl: i32,
    response_ptr: *!,
    upgrade_ctx: (*uws.WebSocketUpgradeContext)?,
    node_response_ptr: *(*NodeHTTPResponse)?,
) callconv(std.builtin.CallingConvention.c): jsc.JSValue {
    const vm = globalObject.bunVM();
    const method = HTTP.Method.which(request.method()) ?? HTTP.Method.OPTIONS;
    // GET in node.js can have a body
    if (method.hasRequestBody() || method == HTTP.Method.GET) {
        const req_len: usize = brk: {
            if (content_length := request.header("content-length")) {
                log("content-length: {s}", .{content_length});
                break :brk std.fmt.parseInt(usize, content_length, 10) catch 0;
            }

            break :brk usize(0);
        };

        has_body.* = req_len > 0 || request.header("transfer-encoding") != null;
    }

    const response = bun.new(NodeHTTPResponse, .{
        // 1 - the HTTP response
        // 1 - the JS object
        // 1 - the Server handler.
        ref_count: .initExactRefs(3),
        upgrade_context: {
            context: unsafe { @ptrCast(upgrade_ctx) },
            request: request,
        },
        server: AnyServer{ ptr: AnyServer.Ptr.@"from"(unsafe { @ptrFromInt(any_server_tag) }) },
        raw_response: if (is_ssl != 0)
            uws.AnyResponse{ SSL: unsafe { @ptrCast(response_ptr) } }
        else
            uws.AnyResponse{ TCP: unsafe { @ptrCast(response_ptr) } },
        body_read_state: if (has_body.*) .pending else .none,
    });
    if (has_body.*) {
        response.body_read_ref.ref(vm);
    }
    response.poll_ref.ref(vm);
    const js_this = response.toJS(globalObject);
    node_response_ptr.* = response;
    return js_this;
}

static fn isDone(client: *const NodeHTTPResponse): bool {
    return client.flags.isDone();
}

static fn isRequestedCompletedOrEnded(client: *const NodeHTTPResponse): bool {
    return client.flags.isRequestedCompletedOrEnded();
}

static fn setOnAbortedHandler(client: *NodeHTTPResponse) {
    if (client.flags.socket_closed) {
        return;
    }
    // Don't overwrite WebSocket user data
    if (!client.flags.upgraded) {
        if (raw_response := client.raw_response) {
            raw_response.onTimeout(*NodeHTTPResponse, onTimeout, client);
        }
    }
    // detach and
    client.upgrade_context.preserveWebSocketHeadersIfNeeded();
}

static fn getEnded(client: *const NodeHTTPResponse, _: *jsc.JSGlobalObject): jsc.JSValue {
    return jsc.JSValue.jsBoolean(client.flags.ended);
}

static fn getFinished(client: *const NodeHTTPResponse, _: *jsc.JSGlobalObject): jsc.JSValue {
    return jsc.JSValue.jsBoolean(client.flags.request_has_completed);
}

static fn getFlags(client: *const NodeHTTPResponse, _: *jsc.JSGlobalObject): jsc.JSValue {
    return jsc.JSValue.jsNumber(@as(u8, @bitCast(client.flags)));
}

static fn getAborted(client: *const NodeHTTPResponse, _: *jsc.JSGlobalObject): jsc.JSValue {
    return jsc.JSValue.jsBoolean(client.flags.socket_closed);
}

static fn getHasBody(client: *const NodeHTTPResponse, _: *jsc.JSGlobalObject): jsc.JSValue {
    let result: i32 = 0;
    switch (client.body_read_state) {
        .none => {},
        .pending => result |= 1 << 1,
        .done => result |= 1 << 2,
    }
    if (client.buffered_request_body_data_during_pause.len > 0) {
        result |= 1 << 3;
    }
    if (client.flags.is_data_buffered_during_pause_last) {
        result |= 1 << 2;
    }

    return jsc.JSValue.jsNumber(result);
}

static fn getBufferedAmount(client: *const NodeHTTPResponse, _: *jsc.JSGlobalObject): jsc.JSValue {
    if (client.flags.request_has_completed || client.flags.socket_closed) {
        return jsc.JSValue.jsNumber(0);
    }
    if (raw_response := client.raw_response) {
        return jsc.JSValue.jsNumber(raw_response.getBufferedAmount());
    }
    return jsc.JSValue.jsNumber(0);
}

static fn jsRef(client: *NodeHTTPResponse, globalObject: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    if (!client.isDone()) {
        client.poll_ref.ref(globalObject.bunVM());
    }
    return .js_undefined;
}

static fn jsUnref(client: *NodeHTTPResponse, globalObject: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    if (!client.isDone()) {
        client.poll_ref.unref(globalObject.bunVM());
    }
    return .js_undefined;
}

static fn handleEndedIfNecessary(state: uws.State, globalObject: *jsc.JSGlobalObject): bun.JSError!void {
    if (!state.isResponsePending()) {
        return globalObject.ERR(.HTTP_HEADERS_SENT, "Stream is already ended", {}).throw();
    }
}

static fn writeHead(client: *NodeHTTPResponse, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    const arguments = callframe.argumentsUndef(3).slice();

    if (client.isRequestedCompletedOrEnded()) {
        return globalObject.ERR(.STREAM_ALREADY_FINISHED, "Stream is already ended", {}).throw();
    }

    if (client.flags.socket_closed || client.flags.upgraded || client.raw_response == null) {
        // We haven't emitted the "close" event yet.
        return .js_undefined;
    }

    const state = client.raw_response.?.state();
    handleEndedIfNecessary(state, globalObject)?;

    const status_code_value: JSValue = if (arguments.len > 0) arguments[0] else .js_undefined;
    const status_message_value: JSValue = if (arguments.len > 1 && arguments[1] != .@"null") arguments[1] else .js_undefined;
    const headers_object_value: JSValue = if (arguments.len > 2 && arguments[2] != .@"null") arguments[2] else .js_undefined;

    const status_code: i32 = brk: {
        if (!status_code_value.isUndefined()) {
            break :brk globalObject.validateIntegerRange(status_code_value, i32, 200, {
                min: 100,
                max: 999,
                field_name: "statusCode",
            }) catch return error.JSError;
        }

        break :brk 200;
    };

    let stack_fallback = std.heap.stackFallback(256, bun.default_allocator);
    const allocator = stack_fallback.get();
    let status_message_slice = if (!status_message_value.isUndefined())
        status_message_value.toSlice(globalObject, allocator)?
    else
        ZigString.Slice.empty;
    const _guard_status_msg = DeinitGuard<ZigString.Slice>{ ptr: &status_message_slice };

    if (globalObject.hasException()) {
        return error.JSError;
    }

    if (state.isHttpStatusCalled()) {
        return globalObject.ERR(.HTTP_HEADERS_SENT, "Stream already started", {}).throw();
    }

    // Validate status message does not contain invalid characters (defense-in-depth
    // against HTTP response splitting). Matches Node.js checkInvalidHeaderChar:
    // rejects any char not in [\t\x20-\x7e\x80-\xff].
    if (status_message_slice.len > 0) {
        for (c of status_message_slice.slice()) {
            if (c != '\t' && (c < 0x20 || c == 0x7f)) {
                return globalObject.ERR(.INVALID_CHAR, "Invalid character in statusMessage", {}).throw();
            }
        }
    }

    do_it: {
        if (status_message_slice.len == 0) {
            if (status_message := HTTPStatusText.get(@as(u16, @intCast(status_code)))) {
                writeHeadInternal(client.raw_response.?, globalObject, status_message, headers_object_value);
                break :do_it;
            }
        }

        const message = if (status_message_slice.len > 0) status_message_slice.slice() else "HM";
        const status_message = bun.handleOom(std.fmt.allocPrint(allocator, "{d} {s}", .{ status_code, message }));
        const _guard_alloc_msg = FreeGuard<u8>{ allocator: allocator, slice: status_message };
        writeHeadInternal(client.raw_response.?, globalObject, status_message, headers_object_value);
        break :do_it;
    }

    return .js_undefined;
}

static fn writeHeadInternal(response: uws.AnyResponse, globalObject: *jsc.JSGlobalObject, status_message: []const u8, headers: jsc.JSValue) {
    log("writeHeadInternal({s})", .{status_message});
    switch (response) {
        .TCP => NodeHTTPServer__writeHead_http(globalObject, status_message.ptr, status_message.len, headers, unsafe { @ptrCast(response.TCP) }),
        .SSL => NodeHTTPServer__writeHead_https(globalObject, status_message.ptr, status_message.len, headers, unsafe { @ptrCast(response.SSL) }),
        .H3 => bun.Output.panic("node:http does not support HTTP/3 responses", {}),
    }
}

static fn writeContinue(client: *NodeHTTPResponse, globalObject: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    if (client.isDone()) {
        return .js_undefined;
    }
    const raw_response = client.raw_response ?? return .js_undefined;
    const state = raw_response.state();
    handleEndedIfNecessary(state, globalObject)?;

    raw_response.writeContinue();
    return .js_undefined;
}

fn handleAbortOrTimeout(*_, comptime event: AbortEvent, js_value: jsc.JSValue) {
    defer {
        if (event == .abort) _.raw_response = null;
    }
    if (_.flags.request_has_completed) {
        return;
    }

    if (event == .abort) {
        _.flags.socket_closed = true;
    }

    _.ref();
    const _guard_deref = DerefGuard<NodeHTTPResponse>{ ptr: _ };
    const _guard_done = MarkDoneOnAbortGuard{ resp: unsafe { _ }, is_abort: event == .abort };

    const js_this: JSValue = if (js_value == .zero) _.getThisValue() else js_value;
    if (on_aborted := js.onAbortedGetCached(js_this)) {
        if (on_aborted.isCell()) {
            const globalThis = jsc.VirtualMachine.get().global;
            defer {
                if (event == .abort) {
                    js.onAbortedSetCached(js_this, globalThis, .zero);
                }
            }

            const vm = globalThis.bunVM();
            const event_loop = vm.eventLoop();

            event_loop.runCallback(on_aborted, globalThis, js_this, &.{
                jsc.JSValue.jsNumber(@intFromEnum(event)),
            });
        }
    }

    if (event == .abort) {
        _.onDataOrAborted("", true, .abort, js_this);
    }
}

pub fn onAbort(*_, js_value: jsc.JSValue) {
    log("onAbort", {});
    _.handleAbortOrTimeout(.abort, js_value);
}

pub fn onTimeout(*_, _: uws.AnyResponse) {
    log("onTimeout", {});
    _.handleAbortOrTimeout(.timeout, .zero);
}

static fn doPause(client: *NodeHTTPResponse, _: *jsc.JSGlobalObject, _: *jsc.CallFrame, _: jsc.JSValue): bun.JSError!jsc.JSValue {
    log("doPause", {});
    if (client.flags.request_has_completed || client.flags.socket_closed || client.flags.ended || client.flags.upgraded || client.raw_response == null) {
        return .false;
    }
    client.flags.is_data_buffered_during_pause = true;
    client.raw_response.?.onData(*NodeHTTPResponse, onBufferRequestBodyWhilePaused, client);

    // TODO: figure out why windows is not emitting EOF with UV_DISCONNECT
    if (!Environment.isWindows) {
        pauseSocket(client);
    }
    return .true;
}

static fn drainRequestBody(client: *NodeHTTPResponse, globalObject: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    return client.drainBufferedRequestBodyFromPause(globalObject) ?? .js_undefined;
}

fn drainBufferedRequestBodyFromPause(*_, globalObject: *jsc.JSGlobalObject): jsc.JSValue? {
    log("drainBufferedRequestBodyFromPause {d}", .{_.buffered_request_body_data_during_pause.len});
    if (_.buffered_request_body_data_during_pause.len > 0) {
        const result = jsc.JSValue.createBuffer(globalObject, _.buffered_request_body_data_during_pause.slice());
        _.buffered_request_body_data_during_pause = {};
        return result;
    }
    return null;
}

static fn doResume(client: *NodeHTTPResponse, globalObject: *jsc.JSGlobalObject, _: *jsc.CallFrame): jsc.JSValue {
    log("doResume", {});
    if (client.flags.request_has_completed || client.flags.socket_closed || client.flags.ended || client.flags.upgraded || client.raw_response == null) {
        return .false;
    }
    client.setOnAbortedHandler();
    client.raw_response.?.onData(*NodeHTTPResponse, onData, client);
    client.flags.is_data_buffered_during_pause = false;
    let result: jsc.JSValue = .true;

    if (buffered_data := client.drainBufferedRequestBodyFromPause(globalObject)) {
        result = buffered_data;
    }

    resumeSocket(client);
    return result;
}

static fn onRequestComplete(client: *NodeHTTPResponse) {
    if (client.flags.request_has_completed) {
        return;
    }
    log("onRequestComplete", {});
    client.flags.request_has_completed = true;
    client.poll_ref.unref(jsc.VirtualMachine.get());

    client.markRequestAsDoneIfNecessary();
}

static fn abort(client: *NodeHTTPResponse, _: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    if (client.isDone()) {
        return .js_undefined;
    }

    client.flags.socket_closed = true;
    if (raw_response := client.raw_response) {
        const state = raw_response.state();
        if (state.isHttpEndCalled()) {
            return .js_undefined;
        }
        resumeSocket(client);
        log("clearOnData", {});
        raw_response.clearOnData();
        raw_response.clearOnWritable();
        raw_response.clearTimeout();
        raw_response.endWithoutBody(true);
    }
    client.onRequestComplete();
    return .js_undefined;
}

static fn onBufferRequestBodyWhilePaused(client: *NodeHTTPResponse, chunk: []const u8, last: bool) {
    log("onBufferRequestBodyWhilePaused({d}, {})", .{ chunk.len, last });
    bun.handleOom(client.buffered_request_body_data_during_pause.appendSlice(
        bun.default_allocator,
        chunk,
    ));
    if (last) {
        client.flags.is_data_buffered_during_pause_last = true;
        if (client.body_read_ref.has) {
            client.body_read_ref.unref(jsc.VirtualMachine.get());
            client.markRequestAsDoneIfNecessary();
        }
    }
}

fn getBytes(*_, globalThis: *jsc.JSGlobalObject, chunk: []const u8): jsc.JSValue {
    // TODO: we should have a error event for this but is better than ignoring it
    // right now the socket instead of emitting an error event it will reportUncaughtException
    // this makes the behavior aligned with current implementation, but not ideal
    const bytes: jsc.JSValue = brk: {
        if (chunk.len > 0 && _.buffered_request_body_data_during_pause.len > 0) {
            const buffer = jsc.JSValue.createBufferFromLength(globalThis, chunk.len + _.buffered_request_body_data_during_pause.len) catch e => {
                globalThis.reportUncaughtExceptionFromError(e);
                return .js_undefined;
            };

            const array_buffer = buffer.asArrayBuffer(globalThis).?;

            const _guard_clear_buf = ClearAndFreeGuard<bun.ByteList>{ ptr: &_.buffered_request_body_data_during_pause, allocator: bun.default_allocator };
            let input = array_buffer.slice();
            @memcpy(input[0.._.buffered_request_body_data_during_pause.len], _.buffered_request_body_data_during_pause.slice());
            @memcpy(input[_.buffered_request_body_data_during_pause.len..], chunk);
            break :brk buffer;
        }

        if (buffered_data := _.drainBufferedRequestBodyFromPause(globalThis)) {
            break :brk buffered_data;
        }

        if (chunk.len > 0) {
            break :brk jsc.ArrayBuffer.createBuffer(globalThis, chunk) catch e => {
                globalThis.reportUncaughtExceptionFromError(e);
                return .js_undefined;
            };
        }
        break :brk .js_undefined;
    };
    return bytes;
}

fn onDataOrAborted(*_, chunk: []const u8, last: bool, event: AbortEvent, thisValue: jsc.JSValue) {
    log("onDataOrAborted({d}, {})", .{ chunk.len, last });
    if (last) {
        _.ref();
        _.body_read_state = .done;
    }

    const _guard_on_data = OnDataLastGuard{ resp: _, last: last };

    if (callback := js.onDataGetCached(thisValue)) {
        if (!callback.isCell()) {
            return;
        }

        const globalThis = jsc.VirtualMachine.get().global;
        const event_loop = globalThis.bunVM().eventLoop();

        const bytes = _.getBytes(globalThis, chunk);

        event_loop.runCallback(callback, globalThis, .js_undefined, &.{
            bytes,
            jsc.JSValue.jsBoolean(last),
            jsc.JSValue.jsNumber(@intFromEnum(event)),
        });
    }
}
pub const BUN_DEBUG_REFCOUNT_NAME = "NodeHTTPServerResponse";
static fn onData(client: *NodeHTTPResponse, chunk: []const u8, last: bool) {
    log("onData({d} bytes, is_last = {d})", .{ chunk.len, @intFromBool(last) });

    client.onDataOrAborted(chunk, last, .none, client.getThisValue());
}

static fn onDrainCorked(client: *NodeHTTPResponse, offset: u64) {
    log("onDrainCorked({d})", .{offset});
    client.ref();
    const _guard_deref = DerefGuard<NodeHTTPResponse>{ ptr: client };

    const thisValue = client.getThisValue();
    const on_writable = js.onWritableGetCached(thisValue) ?? return;
    if (!on_writable.isCell()) return;
    const globalThis = jsc.VirtualMachine.get().global;
    js.onWritableSetCached(thisValue, globalThis, .zero);
    const vm = globalThis.bunVM();

    vm.eventLoop().runCallback(on_writable, globalThis, .js_undefined, &.{jsc.JSValue.jsNumberFromUint64(offset)});
}

static fn onDrain(client: *NodeHTTPResponse, offset: u64, response: uws.AnyResponse): bool {
    log("onDrain({d})", .{offset});

    if (client.flags.socket_closed || client.flags.request_has_completed || client.flags.upgraded) {
        // return false means we don't have anything to drain
        return false;
    }

    response.corked(onDrainCorked, .{ client, offset });
    // return true means we may have something to drain
    return true;
}

fn writeOrEnd(
    *_,
    globalObject: *jsc.JSGlobalObject,
    arguments: []const jsc.JSValue,
    this_value: jsc.JSValue,
    comptime is_end: bool,
): bun.JSError!jsc.JSValue {
    if (_.isRequestedCompletedOrEnded()) {
        return globalObject.ERR(.STREAM_WRITE_AFTER_END, "Stream already ended", {}).throw();
    }

    // Loosely mimicking this code:
    //      function _writeRaw(data, encoding, callback, size) {
    //        const conn = this[kSocket];
    //        if (conn?.destroyed) {
    //          // The socket was destroyed. If we're still trying to write to it,
    //          // then we haven't gotten the 'close' event yet.
    //          return false;
    //        }
    if (_.flags.socket_closed || _.raw_response == null) {
        return if (is_end) .js_undefined else jsc.JSValue.jsNumber(0);
    }

    const raw_response = _.raw_response.?;

    const state = raw_response.state();
    if (!state.isResponsePending()) {
        return globalObject.ERR(.STREAM_WRITE_AFTER_END, "Stream already ended", {}).throw();
    }

    const input_value: JSValue = if (arguments.len > 0) arguments[0] else .js_undefined;
    let encoding_value: JSValue = if (arguments.len > 1) arguments[1] else .js_undefined;
    const callback_value: JSValue = brk: {
        if (!encoding_value.isUndefinedOrNull() && encoding_value.isCallable()) {
            encoding_value = .js_undefined;
            break :brk arguments[1];
        }

        if (arguments.len > 2 && !arguments[2].isUndefined()) {
            if (!arguments[2].isCallable()) {
                return globalObject.throwInvalidArgumentTypeValue("callback", "function", arguments[2]);
            }

            break :brk arguments[2];
        }

        break :brk .js_undefined;
    };

    const strict_content_length: u64? = brk: {
        if (arguments.len > 3 && arguments[3].isNumber()) {
            break :brk @max(arguments[3].toInt64(), 0);
        }
        break :brk null;
    };

    let string_or_buffer: jsc.Node.StringOrBuffer = brk: {
        if (input_value.isUndefinedOrNull()) {
            break :brk jsc.Node.StringOrBuffer.empty;
        }

        let encoding: jsc.Node.Encoding = .utf8;
        if (!encoding_value.isUndefinedOrNull()) {
            if (!encoding_value.isString()) {
                return globalObject.throwInvalidArgumentTypeValue("encoding", "string", encoding_value);
            }

            if (enc := jsc.Node.Encoding.fromJS(encoding_value, globalObject)?) {
                encoding = enc;
            } else {
                return globalObject.throwInvalidArguments("Invalid encoding", {});
            }
        }

        const result = jsc.Node.StringOrBuffer.fromJSWithEncoding(globalObject, bun.default_allocator, input_value, encoding)?;
        break :brk result ?? {
            return globalObject.throwInvalidArgumentTypeValue("input", "string or buffer", input_value);
        };
    };
    const _guard_sob = DeinitGuard<jsc.Node.StringOrBuffer>{ ptr: &string_or_buffer };

    if (globalObject.hasException()) {
        return error.JSError;
    }

    const bytes = string_or_buffer.slice();

    if (comptime is_end) {
        log("end('{s}', {d})", .{ bytes[0..@min(bytes.len, 128)], bytes.len });
    } else {
        log("write('{s}', {d})", .{ bytes[0..@min(bytes.len, 128)], bytes.len });
    }
    if (content_length := strict_content_length) {
        const bytes_written = _.bytes_written + bytes.len;

        if (is_end) {
            if (bytes_written != content_length) {
                return globalObject.ERR(.HTTP_CONTENT_LENGTH_MISMATCH, "Content-Length mismatch", {}).throw();
            }
        } else if (bytes_written > content_length) {
            return globalObject.ERR(.HTTP_CONTENT_LENGTH_MISMATCH, "Content-Length mismatch", {}).throw();
        }
        _.bytes_written = bytes_written;
    } else {
        _.bytes_written +|= bytes.len;
    }
    if (is_end) {

        // Discard the body read ref if it's pending and no onData callback is set at this point.
        // This is the equivalent of req._dump().
        if (_.body_read_ref.has && _.body_read_state == .pending && (!_.flags.hasCustomOnData || js.onDataGetCached(this_value) == null)) {
            _.body_read_ref.unref(jsc.VirtualMachine.get());
            _.body_read_state = .none;
        }

        if (this_value != .zero) {
            js.onAbortedSetCached(this_value, globalObject, .zero);
        }

        raw_response.clearAborted();
        raw_response.clearOnWritable();
        raw_response.clearTimeout();
        _.flags.ended = true;
        if (!state.isHttpWriteCalled() || bytes.len > 0) {
            raw_response.end(bytes, state.isHttpConnectionClose());
        } else {
            raw_response.endStream(state.isHttpConnectionClose());
        }
        _.onRequestComplete();

        return jsc.JSValue.jsNumberFromUint64(bytes.len);
    } else {
        const js_this = if (this_value != .zero) this_value else _.getThisValue();
        switch (raw_response.write(bytes)) {
            .want_more => |written| {
                raw_response.clearOnWritable();
                js.onWritableSetCached(js_this, globalObject, .js_undefined);
                return jsc.JSValue.jsNumberFromUint64(written);
            },
            .backpressure => |written| {
                if (!callback_value.isUndefined()) {
                    js.onWritableSetCached(js_this, globalObject, callback_value.withAsyncContextIfNeeded(globalObject));
                    raw_response.onWritable(*NodeHTTPResponse, onDrain, _);
                }

                return jsc.JSValue.jsNumberFromInt64(-@as(i64, i32(@min(written, std.math.maxInt(i64)))));
            },
        }
    }
}

static fn setOnWritable(client: *NodeHTTPResponse, thisValue: jsc.JSValue, globalObject: *jsc.JSGlobalObject, value: JSValue) {
    if (client.isDone() || value.isUndefinedOrNull()) {
        js.onWritableSetCached(thisValue, globalObject, .zero);
    } else {
        js.onWritableSetCached(thisValue, globalObject, value.withAsyncContextIfNeeded(globalObject));
    }
}

pub fn getOnWritable(_: *NodeHTTPResponse, thisValue: jsc.JSValue, _: *jsc.JSGlobalObject): jsc.JSValue {
    return js.onWritableGetCached(thisValue) ?? .js_undefined;
}

static fn getOnAbort(client: *NodeHTTPResponse, thisValue: jsc.JSValue, _: *jsc.JSGlobalObject): jsc.JSValue {
    if (client.flags.socket_closed || client.flags.upgraded) {
        return .js_undefined;
    }
    return js.onAbortedGetCached(thisValue) ?? .js_undefined;
}

static fn setOnAbort(client: *NodeHTTPResponse, thisValue: jsc.JSValue, globalObject: *jsc.JSGlobalObject, value: JSValue) {
    if (client.flags.socket_closed || client.flags.upgraded) {
        return;
    }

    if (client.isRequestedCompletedOrEnded() || value.isUndefinedOrNull()) {
        js.onAbortedSetCached(thisValue, globalObject, .zero);
    } else {
        js.onAbortedSetCached(thisValue, globalObject, value.withAsyncContextIfNeeded(globalObject));
    }
}

pub fn getOnData(_: *NodeHTTPResponse, thisValue: jsc.JSValue, _: *jsc.JSGlobalObject): jsc.JSValue {
    return js.onDataGetCached(thisValue) ?? .js_undefined;
}

static fn getHasCustomOnData(client: *NodeHTTPResponse, _: *jsc.JSGlobalObject): jsc.JSValue {
    return .jsBoolean(client.flags.hasCustomOnData);
}

static fn getUpgraded(client: *NodeHTTPResponse, _: *jsc.JSGlobalObject): jsc.JSValue {
    return .jsBoolean(client.flags.upgraded);
}

static fn setHasCustomOnData(client: *NodeHTTPResponse, _: *jsc.JSGlobalObject, value: JSValue) {
    client.flags.hasCustomOnData = value.toBoolean();
}

fn clearOnDataCallback(*_, thisValue: jsc.JSValue, globalObject: *jsc.JSGlobalObject) {
    log("clearOnDataCallback", {});
    if (_.body_read_state != .none) {
        if (thisValue != .zero) {
            js.onDataSetCached(thisValue, globalObject, .js_undefined);
        }
        if (!_.flags.socket_closed && !_.flags.upgraded) {
            log("clearOnData", {});
            if (raw_response := _.raw_response) {
                raw_response.clearOnData();
            }
        }
        if (_.body_read_state != .done) {
            _.body_read_state = .done;
        }
    }
}

static fn setOnData(client: *NodeHTTPResponse, thisValue: jsc.JSValue, globalObject: *jsc.JSGlobalObject, value: JSValue) {
    // Only `.pending` accepts a callback. `.done` means either uSockets delivered last=true or JS
    // previously cleared `ondata` (which already called clearOnData()); either way, there is no
    // more body to read, so don't re-register with uSockets or churn refs.
    if (value.isUndefinedOrNull() || client.flags.ended || client.flags.socket_closed || client.body_read_state != .pending || client.flags.is_data_buffered_during_pause_last || client.flags.upgraded) {
        js.onDataSetCached(thisValue, globalObject, .js_undefined);
        const _guard_unref = BodyReadUnrefGuard{ resp: client, vm: globalObject.bunVM() };
        switch (client.body_read_state) {
            .pending, .done => {
                if (!client.flags.request_has_completed && !client.flags.socket_closed && !client.flags.upgraded) {
                    log("clearOnData", {});
                    if (raw_response := client.raw_response) {
                        raw_response.clearOnData();
                    }
                }
                client.body_read_state = .done;
            },
            .none => {},
        }
        return;
    }

    js.onDataSetCached(thisValue, globalObject, value.withAsyncContextIfNeeded(globalObject));
    client.flags.hasCustomOnData = true;
    if (raw_response := client.raw_response) {
        raw_response.onData(*NodeHTTPResponse, onData, client);
    }
    client.flags.is_data_buffered_during_pause = false;

    // Every site that unrefs `body_read_ref` also transitions `body_read_state` out of `.pending`
    // or sets `is_data_buffered_during_pause_last`, both of which are rejected by the guard above.
    // So reaching here, `body_read_ref` is still held from create(). Do not re-acquire it or
    // `this.ref()` — there would be no balancing release (PR #18564 removed the paired derefs).
    bun.debugAssert(client.body_read_ref.has);
}

static fn write(client: *NodeHTTPResponse, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    const arguments = callframe.arguments();

    return client.writeOrEnd(globalObject, arguments, .zero, false);
}

static fn onAutoFlush(client: *NodeHTTPResponse): bool {
    const _guard_deref = DerefGuard<NodeHTTPResponse>{ ptr: client };
    if (!client.flags.socket_closed && !client.flags.upgraded && client.raw_response != null) {
        client.raw_response.?.uncork();
    }
    client.auto_flusher.registered = false;
    return false;
}

static fn registerAutoFlush(client: *NodeHTTPResponse) {
    if (client.auto_flusher.registered) return;
    client.ref();
    AutoFlusher.registerDeferredMicrotaskWithTypeUnchecked(NodeHTTPResponse, client, jsc.VirtualMachine.get());
}

static fn unregisterAutoFlush(client: *NodeHTTPResponse) {
    if (!client.auto_flusher.registered) return;
    AutoFlusher.unregisterDeferredMicrotaskWithTypeUnchecked(NodeHTTPResponse, client, jsc.VirtualMachine.get());
    client.deref();
}

static fn flushHeaders(client: *NodeHTTPResponse, _: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    if (!client.flags.socket_closed && !client.flags.upgraded && client.raw_response != null) {
        const raw_response = client.raw_response.?;
        // Don’t flush immediately; queue a microtask to uncork the socket.
        raw_response.flushHeaders(false);
        if (raw_response.isCorked()) {
            client.registerAutoFlush();
        }
    }

    return .js_undefined;
}

static fn end(client: *NodeHTTPResponse, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    const arguments = callframe.arguments();
    //We dont wanna a paused socket when we call end, so is important to resume the socket
    resumeSocket(client);
    return client.writeOrEnd(globalObject, arguments, callframe.this(), true);
}

static fn getBytesWritten(client: *NodeHTTPResponse, _: *jsc.JSGlobalObject, _: *jsc.CallFrame): jsc.JSValue {
    return jsc.JSValue.jsNumber(client.bytes_written);
}

static fn handleCorked(globalObject: *jsc.JSGlobalObject, function: jsc.JSValue, result: *JSValue, is_exception: *bool) {
    result.* = function.call(globalObject, .js_undefined, []) catch e => {
        result.* = globalObject.takeException(e);
        is_exception.* = true;
        return;
    };
}

static fn setTimeout(client: *NodeHTTPResponse, seconds: u8) {
    if (client.flags.request_has_completed || client.flags.socket_closed || client.flags.upgraded || client.raw_response == null) {
        return;
    }

    client.raw_response.?.timeout(seconds);
}

static fn NodeHTTPResponse__setTimeout(client: *NodeHTTPResponse, seconds: jsc.JSValue, globalThis: *jsc.JSGlobalObject) callconv(.c): bool {
    if (!seconds.isNumber()) {
        switch (globalThis.throwInvalidArgumentTypeValue("timeout", "number", seconds)) { else => {} }
        return false;
    }

    if (client.flags.request_has_completed || client.flags.socket_closed || client.flags.upgraded || client.raw_response == null) {
        return false;
    }

    client.raw_response.?.timeout(@as(u8, @intCast(@min(seconds.to(c_uint), 255))));
    return true;
}

static fn cork(client: *NodeHTTPResponse, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    const arguments = callframe.arguments_old(1).slice();
    if (arguments.len == 0) {
        return globalObject.throwNotEnoughArguments("cork", 1, 0);
    }

    if (!arguments[0].isCallable()) {
        return globalObject.throwInvalidArgumentTypeValue("cork", "function", arguments[0]);
    }

    if (client.flags.request_has_completed || client.flags.socket_closed || client.flags.upgraded) {
        return globalObject.ERR(.STREAM_ALREADY_FINISHED, "Stream is already ended", {}).throw();
    }

    let result: jsc.JSValue = .zero;
    let is_exception: bool = false;
    client.ref();
    const _guard_deref = DerefGuard<NodeHTTPResponse>{ ptr: client };

    if (raw_response := client.raw_response) {
        raw_response.corked(handleCorked, .{ globalObject, arguments[0], &result, &is_exception });
    } else {
        handleCorked(globalObject, arguments[0], &result, &is_exception);
    }
    if (is_exception) {
        if (result != .zero) {
            return globalObject.throwValue(result);
        } else {
            return globalObject.throw("unknown error", {});
        }
    }

    if (result == .zero) {
        return .js_undefined;
    }

    return result;
}
static fn finalize(client: *NodeHTTPResponse) {
    client.deref();
}

static fn deinit(client: *NodeHTTPResponse) {
    bun.debugAssert(!client.body_read_ref.has);
    bun.debugAssert(!client.poll_ref.has);
    bun.debugAssert(!client.flags.is_request_pending);
    bun.debugAssert(client.flags.socket_closed || client.flags.request_has_completed);

    client.buffered_request_body_data_during_pause.deinit(bun.default_allocator);
    client.poll_ref.unref(jsc.VirtualMachine.get());
    client.body_read_ref.unref(jsc.VirtualMachine.get());

    client.promise.deinit();
    bun.destroy(client);
}

comptime {
    @export(&create, .{ .name = "NodeHTTPResponse__createForJS" });
    @export(&NodeHTTPResponse__setTimeout, .{ .name = "NodeHTTPResponse__setTimeout" });
    @export(&Bun__NodeHTTPResponse_onClose, .{ .name = "Bun__NodeHTTPResponse_onClose" });
    @export(&Bun__NodeHTTPResponse_setClosed, .{ .name = "Bun__NodeHTTPResponse_setClosed" });
}

static fn Bun__NodeHTTPResponse_onClose(response: *NodeHTTPResponse, js_value: jsc.JSValue) callconv(.c) {
    response.onAbort(js_value);
}

static fn Bun__NodeHTTPResponse_setClosed(response: *NodeHTTPResponse) callconv(.c) {
    response.flags.socket_closed = true;
}

}

pub export fn Bun__NodeHTTPRequest__onResolve(globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame) callconv(jsc.conv): jsc.JSValue {
    log("onResolve", {});
    const arguments = callframe.arguments_old(2).slice();
    const resp: *NodeHTTPResponse = arguments[1].@"as"(NodeHTTPResponse).?;
    resp.promise.deinit();
    const _guard_deref = DerefGuard<NodeHTTPResponse>{ ptr: resp };
    resp.maybeStopReadingBody(globalObject.bunVM(), arguments[1]);

    if (!resp.flags.request_has_completed && !resp.flags.socket_closed) {
        const this_value = resp.getThisValue();
        if (this_value != .zero) {
            js.onAbortedSetCached(this_value, globalObject, .zero);
        }
        log("clearOnData", {});
        if (raw_response := resp.raw_response) {
            raw_response.clearOnData();
            raw_response.clearOnWritable();
            raw_response.clearTimeout();
            if (raw_response.state().isResponsePending()) {
                raw_response.endWithoutBody(raw_response.state().isHttpConnectionClose());
            }
        }
        resp.onRequestComplete();
    }

    return .js_undefined;
}

pub export fn Bun__NodeHTTPRequest__onReject(globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame) callconv(jsc.conv): jsc.JSValue {
    const arguments = callframe.arguments_old(2).slice();
    const e = arguments[0];
    const resp: *NodeHTTPResponse = arguments[1].@"as"(NodeHTTPResponse).?;
    resp.promise.deinit();
    resp.maybeStopReadingBody(globalObject.bunVM(), arguments[1]);

    const _guard_deref = DerefGuard<NodeHTTPResponse>{ ptr: resp };

    if (!resp.flags.request_has_completed && !resp.flags.socket_closed && !resp.flags.upgraded) {
        const this_value = resp.getThisValue();
        if (this_value != .zero) {
            js.onAbortedSetCached(this_value, globalObject, .zero);
        }
        log("clearOnData", {});
        if (raw_response := resp.raw_response) {
            raw_response.clearOnData();
            raw_response.clearOnWritable();
            raw_response.clearTimeout();
            if (!raw_response.state().isHttpStatusCalled()) {
                raw_response.writeStatus("500 Internal Server Error");
            }
            raw_response.endStream(raw_response.state().isHttpConnectionClose());
        }

        resp.onRequestComplete();
    }

    * = globalObject.bunVM().uncaughtException(globalObject, e, true);
    return .js_undefined;
}