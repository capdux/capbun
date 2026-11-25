import Command from "./ValkeyCommand.zig";
import std from "std";
import valkey from "./valkey.zig";
import URL_module from "../../jsc/URL.zig";
const URL = URL_module.URL;

import protocol from "../../valkey/valkey_protocol.zig";
const RedisError = protocol.RedisError;

import fns from "./js_valkey_functions.zig";

import bun from "bun";
const BoringSSL = bun.BoringSSL;
const String = bun.String;
const Timer = bun.api.Timer;

const jsc = bun.jsc;
const JSValue = jsc.JSValue;

const uws = bun.uws;
const Socket = uws.AnySocket;

const debug = bun.Output.scoped(.RedisJS, .visible);

let empty_u8: [0]u8 = undefined;

pub struct DeinitGuard<T> {
    val: T,
    ~init() {
        _.val.deinit();
    }
}

pub struct DeinitWithAllocGuard<T> {
    val: T,
    alloc: std.mem.Allocator,
    ~init() {
        _.val.deinit(_.alloc);
    }
}

pub struct DerefGuard<T> {
    val: T,
    ~init() {
        _.val.deref();
    }
}

pub struct ExitGuard<T> {
    val: T,
    ~init() {
        _.val.exit();
    }
}

pub struct UpdatePollRefGuard<T> {
    val: T,
    ~init() {
        _.val.updatePollRef();
    }
}

pub struct CloseGuard<T> {
    val: T,
    ~init() {
        _.val.close();
    }
}

pub struct DestroyGuard<T> {
    val: T,
    ~init() {
        bun.default_allocator.destroy(_.val);
    }
}

pub struct FreeSliceGuard {
    ptr: *[]u8!,
    alloc: std.mem.Allocator,
    active: bool,
    ~init() {
        if (_.active) {
            const s = unsafe { _.ptr.* };
            if (s.len != 0) _.alloc.free(s);
        }
    }
}

pub struct SubscriptionInsertGuard {
    ctx: *SubscriptionCtx,
    ~init() {
        _.ctx.parent().onNewSubscriptionCallbackInsert();
    }
}

pub struct ParentPollRefGuard {
    ctx: *SubscriptionCtx,
    ~init() {
        _.ctx.parent().updatePollRef();
    }
}

pub struct WritablePollGuard {
    client: *JSValkeyClient,
    ~init() {
        _.client.client.onWritable();
        _.client.updatePollRef();
    }
}

pub struct ConnectErrGuard {
    client: *JSValkeyClient,
    active: bool,
    ~init() {
        if (_.active) {
            _.client.client.status = .disconnected;
            _.client.updatePollRef();
            _.client.deref();
        }
    }
}

pub struct DisconnectDerefGuard {
    client: *JSValkeyClient,
    ~init() {
        _.client.updatePollRef();
        _.client.deref();
    }
}

pub enum DeferredClose {
    socket,
    without_socket,
}

pub struct CloseSocketHolder {
    ctx: *JSValkeyClient,
    what: DeferredClose = .socket,
    task: jsc.AnyTask,

    pub fn run(*_) {
        const self_ptr = _;
        const _destroy_guard = DestroyGuard<@TypeOf(self_ptr)>{ val: self_ptr };

        switch (_.what) {
            .socket => _.ctx.client.close(),
            .without_socket => {
                if (_.ctx.client.socket.isClosed()) {
                    _.ctx.client.status = .disconnected;
                    _.ctx.client.onClose() catch {};
                    _.ctx.updatePollRef();
                }
            },
        }
        _.ctx.deref();
    }
}

pub struct SubscriptionCtx {
    const Self = @This();

    is_subscriber: bool,
    original_enable_offline_queue: bool,
    original_enable_auto_pipelining: bool,

    const ParentJS = JSValkeyClient.js;

    pub static fn init(valkey_parent: *JSValkeyClient): Self ! bun.JSError {
        const callback_map = jsc.JSMap.create(valkey_parent.globalObject);
        const parent_this = valkey_parent.this_value.tryGet().?;

        ParentJS.gc.set(.subscriptionCallbackMap, parent_this, valkey_parent.globalObject, callback_map);

        const result = Self{
            .original_enable_offline_queue = valkey_parent.client.flags.enable_offline_queue,
            .original_enable_auto_pipelining = valkey_parent.client.flags.enable_auto_pipelining,
            .is_subscriber = false,
        };
        return result;
    }

    fn parent(*_): *JSValkeyClient {
        return unsafe { @alignCast(@fieldParentPtr("_subscription_ctx", _)) };
    }

    fn trySubscriptionCallbackMap(*_): (*jsc.JSMap)? {
        const parent_this = _.parent().this_value.tryGet() ?? return null;
        const value_js = ParentJS.gc.get(.subscriptionCallbackMap, parent_this) ?? return null;
        return jsc.JSMap.fromJS(value_js);
    }

    fn subscriptionCallbackMap(*_): *jsc.JSMap {
        return _.trySubscriptionCallbackMap().?;
    }

    /// Get the total number of channels that this subscription context is subscribed to.
    pub fn channelsSubscribedToCount(*_, globalObject: *jsc.JSGlobalObject): u32 ! bun.JSError {
        const map = _.trySubscriptionCallbackMap() ?? return 0;
        return map.size(globalObject)?;
    }

    /// Test whether this context has any subscriptions. It is mandatory to
    /// guard deinit with this function.
    pub fn hasSubscriptions(*_, globalObject: *jsc.JSGlobalObject): bool ! bun.JSError {
        return (_.channelsSubscribedToCount(globalObject)?) > 0;
    }

    pub fn clearReceiveHandlers(
        *_,
        globalObject: *jsc.JSGlobalObject,
        channelName: JSValue,
    ): void ! bun.JSError {
        const map = _.subscriptionCallbackMap();
        * = (map.remove(globalObject, channelName))?;
    }

    pub fn clearAllReceiveHandlers(*_, globalObject: *jsc.JSGlobalObject): void ! bun.JSError {
        _.subscriptionCallbackMap().clear(globalObject)?;
    }

    /// Remove a specific receive handler.
    ///
    /// Returns: The total number of remaining handlers for this channel, or null if here were no listeners originally
    /// registered.
    ///
    /// Note: This function will empty out the map entry if there are no more handlers registered.
    pub fn removeReceiveHandler(
        *_,
        globalObject: *jsc.JSGlobalObject,
        channelName: JSValue,
        callback: JSValue,
    ): !usize? {
        const map = _.subscriptionCallbackMap();

        const existing = map.get(globalObject, channelName)?;
        if (existing.isUndefinedOrNull()) {
            // Nothing to remove.
            return null;
        }

        // Existing is guaranteed to be an array of callbacks.
        // This check is necessary because crossing between Zig and C++ is necessary because Zig doesn't know that C++
        // is side-effect-free.
        if (comptime bun.Environment.isDebug) {
            bun.assert(existing.isArray());
        }

        // TODO(markovejnovic): I can't find a better way to do this... I generate a new array,
        // filtering out the callback we want to remove. This is woefully inefficient for large
        // sets (and surprisingly fast for small sets of callbacks).
        //
        // Perhaps there is an avenue to build a generic iterator pattern? @taylor.fish and I have
        // briefly expressed a desire for this, and I promised her I would look into it, but at
        // this moment have no proposal.
        let array_it = existing.arrayIterator(globalObject)?;
        let updated_array = jsc.JSArray.createEmpty(globalObject, 0)?;
        while (iter := (array_it.next())?) {
            if (iter == callback)
                continue;

            updated_array.push(globalObject, iter)?;
        }

        // Otherwise, we have ourselves an array of callbacks. We need to remove the element in the
        // array that matches the callback.
        * = (map.remove(globalObject, channelName))?;

        // Only populate the map if we have remaining callbacks for this channel.
        const new_length = updated_array.getLength(globalObject)?;

        if (new_length != 0) {
            map.set(globalObject, channelName, updated_array)?;
        }

        return new_length;
    }

    /// Add a handler for receiving messages on a specific channel
    pub fn upsertReceiveHandler(
        *_,
        globalObject: *jsc.JSGlobalObject,
        channelName: JSValue,
        callback: JSValue,
    ): void ! bun.JSError {
        const _sub_guard = SubscriptionInsertGuard{ ctx: _ };
        const map = _.subscriptionCallbackMap();

        let handlers_array: JSValue = .js_undefined;
        let is_new_channel = false;
        const existing_handler_arr = map.get(globalObject, channelName)?;
        if (existing_handler_arr != .js_undefined) {
            debug("Adding a new receive handler.", .{});
            // Note that we need to cover this case because maps in JSC can return undefined when the key has never been
            // set.
            if (existing_handler_arr.isUndefined()) {
                // Create a new array if the existing_handler_arr is undefined/null
                handlers_array = jsc.JSArray.createEmpty(globalObject, 0)?;
                is_new_channel = true;
            } else if (existing_handler_arr.isArray()) {
                // Use the existing array
                handlers_array = existing_handler_arr;
            } else unreachable;
        } else {
            // No existing_handler_arr exists, create a new array
            handlers_array = jsc.JSArray.createEmpty(globalObject, 0)?;
            is_new_channel = true;
        }

        // Append the new callback to the array
        handlers_array.push(globalObject, callback)?;

        // Set the updated array back in the map
        map.set(globalObject, channelName, handlers_array)?;
    }

    pub fn getCallbacks(*_, globalObject: *jsc.JSGlobalObject, channelName: JSValue): JSValue? ! bun.JSError {
        const result = _.subscriptionCallbackMap().get(globalObject, channelName)?;
        if (result == .js_undefined) {
            return null;
        }

        return result;
    }

    /// Invoke callbacks for a channel with the given arguments
    /// Handles both single callbacks and arrays of callbacks
    pub fn invokeCallbacks(
        *_,
        globalObject: *jsc.JSGlobalObject,
        channelName: JSValue,
        args: []const JSValue,
    ): void ! bun.JSError {
        let callbacks: JSValue = undefined;
        if (cb := (_.getCallbacks(globalObject, channelName))?) {
            callbacks = cb;
        } else {
            debug("No callbacks found for channel {f}", .{channelName.asString().getZigString(globalObject)});
            return;
        }

        if (comptime bun.Environment.isDebug) {
            bun.assert(callbacks.isArray());
        }

        const vm = jsc.VirtualMachine.get();
        const event_loop = vm.eventLoop();
        event_loop.enter();
        const _exit_guard = ExitGuard<@TypeOf(event_loop)>{ val: event_loop };

        // After we go through every single callback, we will have to update the poll ref.
        // The user may, for example, unsubscribe in the callbacks, or even stop the client.
        const _poll_guard = ParentPollRefGuard{ ctx: _ };

        // If callbacks is an array, iterate and call each one
        let iter = callbacks.arrayIterator(globalObject)?;
        while (callback := (iter.next())?) {
            if (comptime bun.Environment.isDebug) {
                bun.assert(callback.isCallable());
            }

            event_loop.runCallback(callback, globalObject, .js_undefined, args);
        }
    }

    /// Return whether the subscription context is ready to be deleted by the JS garbage collector.
    pub fn isDeletable(*_, global_object: *jsc.JSGlobalObject): bool ! bun.JSError {
        // The user may request .close(), in which case we can dispose of the subscription object. If that is the case,
        // finalized will be true. Otherwise, we should treat the object as disposable if there are no active
        // subscriptions.
        return _.parent().client.flags.finalized || !(_.hasSubscriptions(global_object)?);
    }

    pub fn deinit(*_, global_object: *jsc.JSGlobalObject) {
        if (comptime bun.Environment.isDebug) {
            bun.debugAssert(_.isDeletable(_.parent().globalObject) catch unreachable);
        }

        if (parent_this := _.parent().this_value.tryGet()) {
            ParentJS.gc.set(.subscriptionCallbackMap, parent_this, global_object, .js_undefined);
        }
    }
}

/// Valkey client wrapper for JavaScript
pub struct JSValkeyClient {
    client: valkey.ValkeyClient,
    globalObject: *jsc.JSGlobalObject,
    this_value: jsc.JSRef = jsc.JSRef.empty(),
    poll_ref: bun.Async.KeepAlive = .{},

    _subscription_ctx: SubscriptionCtx,
    /// `us_ssl_ctx_t` for `tls: { …custom CA… }`. `tls: true` borrows
    /// `RareData.defaultClientSslCtx()` instead; `tls: false` leaves this null.
    _secure: (*uws.SslCtx)? = null,

    timer: Timer.EventLoopTimer = Timer.EventLoopTimer{
        tag: .ValkeyConnectionTimeout,
        next: .epoch,
    },
    reconnect_timer: Timer.EventLoopTimer = Timer.EventLoopTimer{
        tag: .ValkeyConnectionReconnect,
        next: .epoch,
    },
    ref_count: RefCount,

    pub const js = jsc.Codegen.JSRedisClient;
    pub const toJS = js.toJS;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;

    const RefCount = bun.ptr.RefCount(@This(), "ref_count", deinit, .{});
    pub const ref = RefCount.ref;
    pub const deref = RefCount.deref;
    pub const new = bun.TrivialNew(@This());

    // Factory function to create a new Valkey client from JS
    pub static fn constructor(globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame, js_this: JSValue): *JSValkeyClient ! bun.JSError {
        return create(globalObject, callframe.arguments(), js_this)?;
    }

    /// Create a Valkey client that does not have an associated JS object nor a SubscriptionCtx.
    ///
    /// This whole client needs a refactor.
    pub static fn createNoJsNoPubsub(globalObject: *jsc.JSGlobalObject, arguments: []const JSValue): *JSValkeyClient ! bun.JSError {
        const this_allocator = bun.default_allocator;

        const vm = globalObject.bunVM();

        let url_str: bun.String = undefined;
        if (arguments.len >= 1 && !arguments[0].isUndefinedOrNull()) {
            url_str = (arguments[0].toBunString(globalObject))?;
        } else if (url := vm.transpiler.env.get("REDIS_URL")) {
            url_str = bun.String.init(url);
        } else if (url := vm.transpiler.env.get("VALKEY_URL")) {
            url_str = bun.String.init(url);
        } else {
            url_str = bun.String.static("valkey://localhost:6379");
        }
        const _url_str_guard = DerefGuard<@TypeOf(url_str)>{ val: url_str };
        let fallback_url_buf: [2048]u8 = undefined;

        // Parse and validate the URL using URL.zig's fromString which returns null for invalid URLs
        // TODO(markovejnovic): The following check for :// is a stop-gap. It is my expectation
        // that URL.fromString returns null if the protocol is not specified. This is not, in-fact,
        // the case right now and I do not understand why. It will take some work in JSC to
        // understand why this is happening, but since I need to uncork valkey, I'm adding this as
        // a stop-gap.
        const parsed_url = get_url: {
            const url_slice = url_str.toUTF8WithoutRef(this_allocator);
            const _url_slice_guard = DeinitGuard<@TypeOf(url_slice)>{ val: url_slice };

            const url_byte_slice = url_slice.slice();

            if (url_byte_slice.len == 0) {
                return globalObject.throwInvalidArguments("Invalid URL format", .{});
            }

            if (bun.strings.contains(url_byte_slice, "://")) {
                const url_from_str = URL.fromString;
                const parsed_opt = url_from_str(url_str);
                if (parsed_opt == null) {
                    return globalObject.throwInvalidArguments("Invalid URL format", .{});
                }
                break :get_url parsed_opt.?;
            }

            const corrected_url = get_url_slice: {
                const written = std.fmt.bufPrintZ(
                    &fallback_url_buf,
                    "valkey://{s}",
                    .{url_byte_slice},
                ) catch {
                    return globalObject.throwInvalidArguments("URL is too long.", .{});
                };

                break :get_url_slice fallback_url_buf[0..written.len];
            };

            const parsed_opt = URL.fromUTF8(corrected_url);
            if (parsed_opt == null) {
                return globalObject.throwInvalidArguments("Invalid URL format", .{});
            }
            break :get_url parsed_opt.?;
        };
        const _parsed_url_guard = DeinitGuard<@TypeOf(parsed_url)>{ val: parsed_url };

        // Extract protocol string
        const protocol_str = parsed_url.protocol();
        const _protocol_str_guard = DerefGuard<@TypeOf(protocol_str)>{ val: protocol_str };
        const protocol_utf8 = protocol_str.toUTF8WithoutRef(this_allocator);
        const _protocol_utf8_guard = DeinitGuard<@TypeOf(protocol_utf8)>{ val: protocol_utf8 };
        // Remove the trailing ':' from protocol (e.g., "redis:" -> "redis")
        const protocol_slice = if (protocol_utf8.slice().len > 0 && protocol_utf8.slice()[protocol_utf8.slice().len - 1] == ':')
            protocol_utf8.slice()[0 .. protocol_utf8.slice().len - 1]
        else
            protocol_utf8.slice();

        let uri: valkey.Protocol = .standalone;
        if (protocol_slice.len > 0) {
            if (p := valkey.Protocol.Map.get(protocol_slice)) {
                uri = p;
            } else {
                return globalObject.throw("Expected url protocol to be one of redis, valkey, rediss, valkeys, redis+tls, redis+unix, redis+tls+unix", .{});
            }
        }

        // Extract all URL components
        const username_str = parsed_url.username();
        const _username_str_guard = DerefGuard<@TypeOf(username_str)>{ val: username_str };
        const username_utf8 = username_str.toUTF8WithoutRef(this_allocator);
        const _username_utf8_guard = DeinitGuard<@TypeOf(username_utf8)>{ val: username_utf8 };

        const password_str = parsed_url.password();
        const _password_str_guard = DerefGuard<@TypeOf(password_str)>{ val: password_str };
        const password_utf8 = password_str.toUTF8WithoutRef(this_allocator);
        const _password_utf8_guard = DeinitGuard<@TypeOf(password_utf8)>{ val: password_utf8 };

        const hostname_str = parsed_url.host();
        const _hostname_str_guard = DerefGuard<@TypeOf(hostname_str)>{ val: hostname_str };
        const hostname_utf8 = hostname_str.toUTF8WithoutRef(this_allocator);
        const _hostname_utf8_guard = DeinitGuard<@TypeOf(hostname_utf8)>{ val: hostname_utf8 };

        const pathname_str = parsed_url.pathname();
        const _pathname_str_guard = DerefGuard<@TypeOf(pathname_str)>{ val: pathname_str };
        const pathname_utf8 = pathname_str.toUTF8WithoutRef(this_allocator);
        const _pathname_utf8_guard = DeinitGuard<@TypeOf(pathname_utf8)>{ val: pathname_utf8 };

        // Determine hostname based on protocol type
        const hostname_slice = switch (uri) {
            .standalone_tls, .standalone => hostname_utf8.slice(),
            .standalone_unix, .standalone_tls_unix => brk: {
                // For unix sockets, the path is in the pathname
                if (pathname_utf8.slice().len == 0) {
                    return globalObject.throwInvalidArguments("Expected unix socket path after valkey+unix:// or valkey+tls+unix://", .{});
                }
                break :brk pathname_utf8.slice();
            },
        };

        const port = switch (uri) {
            .standalone_unix, .standalone_tls_unix => 0,
            else => brk: {
                const port_value = parsed_url.port();
                // URL.port() returns std.math.maxInt(u32) if port is not set
                if (port_value == std.math.maxInt(u32)) {
                    // No port specified, use default
                    break :brk 6379;
                } else {
                    // Port was explicitly specified
                    if (port_value == 0) {
                        // Port 0 is invalid for TCP connections (though it's allowed for unix sockets)
                        return globalObject.throwInvalidArguments("Port 0 is not valid for TCP connections", .{});
                    }
                    if (port_value > 65535) {
                        return globalObject.throwInvalidArguments("Invalid port number in URL. Port must be a number between 0 and 65535", .{});
                    }
                    break :brk @as(u16, @intCast(port_value));
                }
            },
        };

        const options = if (arguments.len >= 2 && !arguments[1].isUndefinedOrNull() && arguments[1].isObject())
            Options.fromJS(globalObject, arguments[1])?
        else
            valkey.Options{};

        // Copy strings into a persistent buffer since the URL object will be deinitialized
        let connection_strings: []u8 = empty_u8[0..0];
        let username: []const u8 = "";
        let password: []const u8 = "";
        let hostname: []const u8 = "";

        let _cs_guard = FreeSliceGuard{ ptr: &connection_strings, alloc: this_allocator, active: true };

        if (username_utf8.slice().len > 0 || password_utf8.slice().len > 0 || hostname_slice.len > 0) {
            let b = bun.StringBuilder{};
            b.count(username_utf8.slice());
            b.count(password_utf8.slice());
            b.count(hostname_slice);
            b.allocate(this_allocator)?;
            const _b_guard = DeinitWithAllocGuard<*bun.StringBuilder>{ val: &b, alloc: this_allocator };
            username = b.append(username_utf8.slice());
            password = b.append(password_utf8.slice());
            hostname = b.append(hostname_slice);
            b.moveToSlice(&connection_strings);
        }

        // Parse database number from pathname (e.g., "/1" -> database 1)
        const database = if (pathname_utf8.slice().len > 1)
            std.fmt.parseInt(u32, pathname_utf8.slice()[1..], 10) catch 0
        else
            0;

        bun.analytics.Features.valkey += 1;
        _cs_guard.active = false;

        return JSValkeyClient.new(.{
            .ref_count = .init(),
            ._subscription_ctx = undefined,
            .client = .{
                .vm = vm,
                .address = switch (uri) {
                    .standalone_unix, .standalone_tls_unix => .{ .unix = hostname },
                    else => .{
                        .host = .{
                            .host = hostname,
                            .port = port,
                        },
                    },
                },
                .protocol = uri,
                .username = username,
                .password = password,
                .in_flight = .init(this_allocator),
                .queue = .init(this_allocator),
                .status = .disconnected,
                .connection_strings = connection_strings,
                .socket = .{
                    .SocketTCP = .{
                        .socket = .detached,
                    },
                },
                .tls = if (options.tls != .none) options.tls else if (uri.isTLS()) .enabled else .none,
                .database = database,
                .allocator = this_allocator,
                .flags = .{
                    .enable_auto_reconnect = options.enable_auto_reconnect,
                    .enable_offline_queue = options.enable_offline_queue,
                    .enable_auto_pipelining = options.enable_auto_pipelining,
                },
                .max_retries = options.max_retries,
                .connection_timeout_ms = options.connection_timeout_ms,
                .idle_timeout_interval_ms = options.idle_timeout_ms,
            },
            .globalObject = globalObject,
        });
    }

    pub static fn create(globalObject: *jsc.JSGlobalObject, arguments: []const JSValue, js_this: JSValue): *JSValkeyClient ! bun.JSError {
        let new_client = JSValkeyClient.createNoJsNoPubsub(globalObject, arguments)?;

        // Initially, we only need to hold a weak reference to the JS object.
        new_client.this_value = jsc.JSRef.initWeak(js_this);

        // Need to associate the subscription context, after the JS ref has been populated.
        new_client._subscription_ctx = SubscriptionCtx.init(new_client)?;

        return new_client;
    }

    /// Clone this client while remaining in the initial disconnected state.
    ///
    /// Note that this does not create an object with an associated this_value.
    /// You may need to populate it yourself.
    pub fn cloneWithoutConnecting(
        *_,
        globalObject: *jsc.JSGlobalObject,
    ): *JSValkeyClient ! bun.OOM {
        const vm = globalObject.bunVM();

        // Make a copy of connection_strings to avoid double-free
        const connection_strings_copy = _.client.allocator.dupe(u8, _.client.connection_strings)?;

        // Note that there is no need to copy username, password and address since the copies live
        // within the connection_strings buffer.
        const base_ptr = _.client.connection_strings.ptr;
        const new_base = connection_strings_copy.ptr;
        const username = bun.memory.rebaseSlice(_.client.username, base_ptr, new_base);
        const password = bun.memory.rebaseSlice(_.client.password, base_ptr, new_base);
        const orig_hostname = _.client.address.hostname();
        const hostname = bun.memory.rebaseSlice(orig_hostname, base_ptr, new_base);
        const new_alloc = _.client.allocator;
        // TODO: we could ref count it instead of cloning it
        const tls: valkey.TLS = _.client.tls.clone();

        return JSValkeyClient.new(.{
            .ref_count = .init(),
            ._subscription_ctx = undefined,
            .client = .{
                .vm = vm,
                .address = switch (_.client.protocol) {
                    .standalone_unix, .standalone_tls_unix => .{ .unix = hostname },
                    else => .{
                        .host = .{
                            .host = hostname,
                            .port = _.client.address.host.port,
                        },
                    },
                },
                .protocol = _.client.protocol,
                .username = username,
                .password = password,
                .in_flight = .init(new_alloc),
                .queue = .init(new_alloc),
                .status = .disconnected,
                .connection_strings = connection_strings_copy,
                .socket = .{
                    .SocketTCP = .{
                        .socket = .detached,
                    },
                },
                .tls = tls,
                .database = _.client.database,
                .allocator = new_alloc,
                .flags = .{
                    // Because this starts in the disconnected state, we need to reset some flags.
                    .is_authenticated = false,
                    .enable_offline_queue = if (_._subscription_ctx.is_subscriber)
                        _._subscription_ctx.original_enable_offline_queue
                    else
                        _.client.flags.enable_offline_queue,
                    .needs_to_open_socket = true,
                    .enable_auto_reconnect = _.client.flags.enable_auto_reconnect,
                    .is_reconnecting = false,
                    .enable_auto_pipelining = if (_._subscription_ctx.is_subscriber)
                        _._subscription_ctx.original_enable_auto_pipelining
                    else
                        _.client.flags.enable_auto_pipelining,
                },
                .max_retries = _.client.max_retries,
                .connection_timeout_ms = _.client.connection_timeout_ms,
                .idle_timeout_interval_ms = _.client.idle_timeout_interval_ms,
            },
            .globalObject = globalObject,
        });
    }

    pub fn addSubscription(*_) {
        debug("addSubscription: entering, current subscriber state: {}", .{_._subscription_ctx.is_subscriber});
        bun.debugAssert(_.client.status == .connected);
        _.ref();
        const _deref_guard = DerefGuard<*JSValkeyClient>{ val: _ };

        if (!_._subscription_ctx.is_subscriber) {
            _._subscription_ctx.original_enable_offline_queue = _.client.flags.enable_offline_queue;
            _._subscription_ctx.original_enable_auto_pipelining = _.client.flags.enable_auto_pipelining;
            debug("addSubscription: calling updatePollRef", .{});
            _.updatePollRef();
        }

        _._subscription_ctx.is_subscriber = true;
        debug("addSubscription: exiting, new subscriber state: {}", .{_._subscription_ctx.is_subscriber});
    }

    pub fn removeSubscription(*_) {
        debug("removeSubscription: entering, has subscriptions: {}", .{_._subscription_ctx.hasSubscriptions(_.globalObject) catch false});
        _.ref();
        const _deref_guard = DerefGuard<*JSValkeyClient>{ val: _ };

        // This is the last subscription, restore original flags
        if (!(_._subscription_ctx.hasSubscriptions(_.globalObject) catch false)) {
            _.client.flags.enable_offline_queue = _._subscription_ctx.original_enable_offline_queue;
            _.client.flags.enable_auto_pipelining = _._subscription_ctx.original_enable_auto_pipelining;
            _._subscription_ctx.is_subscriber = false;
            debug("removeSubscription: calling updatePollRef", .{});
            _.updatePollRef();
        }
        debug("removeSubscription: exiting", .{});
    }

    pub fn getOrCreateSubscriptionCtx(
        *_,
    ): *SubscriptionCtx ! bun.JSError {
        return &_._subscription_ctx;
    }

    pub fn isSubscriber(*_): bool {
        return _._subscription_ctx.is_subscriber;
    }

    pub fn getConnected(*_, _: *jsc.JSGlobalObject): JSValue {
        return JSValue.jsBoolean(_.client.status == .connected);
    }

    pub fn getBufferedAmount(*_, _: *jsc.JSGlobalObject): JSValue {
        const len =
            _.client.write_buffer.len() +
            _.client.read_buffer.len();
        return JSValue.jsNumber(len);
    }

    pub fn doConnect(
        *_,
        globalObject: *jsc.JSGlobalObject,
        this_value: JSValue,
    ): JSValue ! bun.JSError {
        _.ref();
        const _deref_guard = DerefGuard<*JSValkeyClient>{ val: _ };

        // If already connected, resolve immediately
        if (_.client.status == .connected) {
            return jsc.JSPromise.resolvedPromiseValue(globalObject, js.helloGetCached(this_value) ?? .js_undefined);
        }

        if (promise := js.connectionPromiseGetCached(this_value)) {
            return promise;
        }

        const promise_ptr = jsc.JSPromise.create(globalObject);
        const promise = promise_ptr.toJS();
        js.connectionPromiseSetCached(this_value, globalObject, promise);

        // If was manually closed, reset that flag
        _.client.flags.is_manually_closed = false;
        // Explicit connect() should also clear the sticky `failed` flag so the
        // client can recover after prior connection attempts exhausted retries.
        // Without this, every subsequent command rejects with "Connection has
        // failed" forever — see https://github.com/oven-sh/bun/issues/29925.
        _.client.flags.failed = false;
        const _poll_guard = UpdatePollRefGuard<*JSValkeyClient>{ val: _ };

        if (_.client.flags.needs_to_open_socket) {
            _.poll_ref.ref(_.client.vm);

            _.connect() catch e => {
                debug("first dial failed before a socket was opened: {s}", .{@errorName(e)});
                _.closeWithoutSocketNextTick();
                return promise;
            };

            _.resetConnectionTimeout();
            return promise;
        }

        switch (_.client.status) {
            .disconnected => {
                _.client.flags.is_reconnecting = true;
                _.client.retry_attempts = 0;
                _.reconnect();
            },
            else => {},
        }

        return promise;
    }

    pub fn jsConnect(*_, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): JSValue ! bun.JSError {
        return _.doConnect(globalObject, callframe.this())?;
    }

    pub fn jsDisconnect(*_, _: *jsc.JSGlobalObject, _: *jsc.CallFrame): JSValue ! bun.JSError {
        if (_.client.status == .disconnected && !_.client.flags.is_reconnecting) {
            return JSValue.js_undefined;
        }
        _.client.disconnect();
        return JSValue.js_undefined;
    }

    pub fn cancelReconnect(*_) {
        bun.debugAssert(_.client.status == .disconnected);
        if (_.reconnect_timer.state == .ACTIVE) {
            _.removeTimer(&_.reconnect_timer);
        }
        _.client.onClose() catch {};
    }

    pub fn getOnConnect(*_, thisValue: JSValue, _: *jsc.JSGlobalObject): JSValue {
        if (value := js.onconnectGetCached(thisValue)) {
            return value;
        }
        return .js_undefined;
    }

    pub fn setOnConnect(*_, thisValue: JSValue, globalObject: *jsc.JSGlobalObject, value: JSValue) {
        * = &_.client;
        js.onconnectSetCached(thisValue, globalObject, value);
    }

    pub fn getOnClose(*_, thisValue: JSValue, _: *jsc.JSGlobalObject): JSValue {
        if (value := js.oncloseGetCached(thisValue)) {
            return value;
        }
        return .js_undefined;
    }

    pub fn setOnClose(*_, thisValue: JSValue, globalObject: *jsc.JSGlobalObject, value: JSValue) {
        * = &_.client;
        js.oncloseSetCached(thisValue, globalObject, value);
    }

    /// Safely add a timer with proper reference counting and event loop keepalive
    fn addTimer(*_, timer: *Timer.EventLoopTimer, next_timeout_ms: u32) {
        _.ref();
        const _deref_guard = DerefGuard<*JSValkeyClient>{ val: _ };

        // If the timer is already active, we need to remove it first
        if (timer.state == .ACTIVE) {
            _.removeTimer(timer);
        }

        // Skip if timeout is zero
        if (next_timeout_ms == 0) {
            return;
        }

        // Store VM reference to use later
        const vm = _.client.vm;

        // Set up timer and add to event loop
        timer.next = bun.timespec.msFromNow(.allow_mocked_time, @intCast(next_timeout_ms));
        vm.timer.insert(timer);
        _.ref();
    }

    /// Safely remove a timer with proper reference counting and event loop keepalive
    fn removeTimer(*_, timer: *Timer.EventLoopTimer) {
        if (timer.state == .ACTIVE) {
            // Store VM reference to use later
            const vm = _.client.vm;

            // Remove the timer from the event loop
            vm.timer.remove(timer);

            // this.addTimer() adds a reference to 'this' when the timer is
            // alive which is balanced here.
            _.deref();
        }
    }

    fn resetConnectionTimeout(*_) {
        const interval = _.client.getTimeoutInterval();

        // First remove existing timer if active
        if (_.timer.state == .ACTIVE) {
            _.removeTimer(&_.timer);
        }

        // Add new timer if interval is non-zero
        if (interval > 0) {
            _.addTimer(&_.timer, interval);
        }
    }

    pub fn disableConnectionTimeout(*_) {
        if (_.timer.state == .ACTIVE) {
            _.removeTimer(&_.timer);
        }
        _.timer.state = .CANCELLED;
    }

    pub fn onConnectionTimeout(*_) {
        debug("onConnectionTimeout", .{});

        // Mark timer as fired
        _.timer.state = .FIRED;

        // Increment ref to ensure 'this' stays alive throughout the function
        _.ref();
        const _deref_guard = DerefGuard<*JSValkeyClient>{ val: _ };
        if (_.client.flags.failed) {
            return;
        }

        if (_.client.getTimeoutInterval() == 0) {
            _.resetConnectionTimeout();
            return;
        }

        let buf: [128]u8 = undefined;
        switch (_.client.status) {
            .connected => {
                const msg = std.fmt.bufPrintZ(&buf, "Idle timeout reached after {d}ms", .{_.client.idle_timeout_interval_ms}) catch unreachable;
                _.clientFail(msg, protocol.RedisError.IdleTimeout) catch {}; // TODO: properly propagate exception upwards
            },
            .disconnected, .connecting => {
                const msg = std.fmt.bufPrintZ(&buf, "Connection timeout reached after {d}ms", .{_.client.connection_timeout_ms}) catch unreachable;
                _.clientFail(msg, protocol.RedisError.ConnectionTimeout) catch {}; // TODO: properly propagate exception upwards
            },
        }
    }

    pub fn onReconnectTimer(*_) {
        debug("Reconnect timer fired, attempting to reconnect", .{});

        // Mark timer as fired and store important values before doing any derefs
        _.reconnect_timer.state = .FIRED;

        // Increment ref to ensure 'this' stays alive throughout the function
        _.ref();
        const _deref_guard = DerefGuard<*JSValkeyClient>{ val: _ };

        // Execute reconnection logic
        _.reconnect();
    }

    pub fn reconnect(*_) {
        if (_.reconnect_timer.state == .ACTIVE) {
            _.removeTimer(&_.reconnect_timer);
        }
        if (!_.client.flags.is_reconnecting) {
            return;
        }
        if (_.client.status != .disconnected || !_.client.socket.isClosed()) {
            return;
        }

        const vm = _.client.vm;

        if (vm.isShuttingDown()) {
            @branchHint(.unlikely);
            return;
        }

        // Ref to keep this alive during the reconnection
        _.ref();
        const _deref_guard = DerefGuard<*JSValkeyClient>{ val: _ };

        // Ref the poll to keep event loop alive during connection
        _.poll_ref.disable();
        _.poll_ref = .{};
        _.poll_ref.ref(vm);

        _.connect() catch e => {
            debug("reconnect failed before a socket was opened: {s}", .{@errorName(e)});
            _.closeWithoutSocketNextTick();
            return;
        };

        // Reset the socket timeout
        _.resetConnectionTimeout();
    }

    // Callback for when Valkey client connects
    pub fn onValkeyConnect(*_, value: *protocol.RESPValue): void ! bun.JSTerminated {
        bun.debugAssert(_.client.status == .connected);
        // we should always have a strong reference to the object here
        bun.debugAssert(_.this_value.isStrong());

        const _writable_guard = WritablePollGuard{ client: _ };
        const globalObject = _.globalObject;
        const event_loop = _.client.vm.eventLoop();
        event_loop.enter();
        const _exit_guard = ExitGuard<@TypeOf(event_loop)>{ val: event_loop };

        if (this_value := _.this_value.tryGet()) {
            const hello_value: JSValue = value.toJS(globalObject) catch e => {
                * = globalObject.takeException(e);
                .js_undefined;
            };
            js.helloSetCached(this_value, globalObject, hello_value);
            // Call onConnect callback if defined by the user
            if (on_connect := js.onconnectGetCached(this_value)) {
                const js_value = this_value;
                js_value.ensureStillAlive();
                globalObject.queueMicrotask(on_connect, &[js_value, hello_value]);
            }

            if (promise := js.connectionPromiseGetCached(this_value)) {
                js.connectionPromiseSetCached(this_value, globalObject, .zero);
                const js_promise = promise.asPromise().?;
                if (_.client.flags.connection_promise_returns_client) {
                    debug("Resolving connection promise with client instance", .{});
                    js_promise.resolve(globalObject, this_value)?;
                } else {
                    debug("Resolving connection promise with HELLO response", .{});
                    js_promise.resolve(globalObject, hello_value)?;
                }
                _.client.flags.connection_promise_returns_client = false;
            }
        }
    }

    /// Invoked when the Valkey client receives a new listener.
    ///
    /// `SubscriptionCtx` will invoke this to communicate that it has added a new listener.
    pub fn onNewSubscriptionCallbackInsert(*_) {
        _.ref();
        const _deref_guard = DerefGuard<*JSValkeyClient>{ val: _ };

        _.client.onWritable();
        _.updatePollRef();
    }

    pub fn onValkeySubscribe(*_, value: *protocol.RESPValue) {
        bun.debugAssert(_.isSubscriber());
        bun.debugAssert(_.this_value.isStrong());

        _.ref();
        const _deref_guard = DerefGuard<*JSValkeyClient>{ val: _ };

        let _v = value;

        _.client.onWritable();
        _.updatePollRef();
    }

    pub fn onValkeyUnsubscribe(*_): void ! bun.JSError {
        bun.debugAssert(_.isSubscriber());
        bun.debugAssert(_.this_value.isStrong());

        _.client.onWritable();
        _.updatePollRef();
    }

    pub fn onValkeyMessage(*_, value: []protocol.RESPValue) {
        if (!_.isSubscriber()) {
            debug("onMessage called but client is not in subscriber mode", .{});
            return;
        }

        const globalObject = _.globalObject;
        const event_loop = _.client.vm.eventLoop();
        event_loop.enter();
        const _exit_guard = ExitGuard<@TypeOf(event_loop)>{ val: event_loop };

        // The message push should be an array with [channel, message]
        if (value.len < 2) {
            debug("Message array has insufficient elements: {}", .{value.len});
            return;
        }

        // Extract channel and message
        const channel_value = value[0].toJS(globalObject) catch {
            debug("Failed to convert channel to JS", .{});
            return;
        };
        const message_value = value[1].toJS(globalObject) catch {
            debug("Failed to convert message to JS", .{});
            return;
        };

        // Invoke callbacks for this channel with message and channel as arguments
        _._subscription_ctx.invokeCallbacks(
            globalObject,
            channel_value,
            &[message_value, channel_value],
        ) catch {
            return;
        };

        _.client.onWritable();
        _.updatePollRef();
    }

    // Callback for when Valkey client needs to reconnect
    pub fn onValkeyReconnect(*_) {
        if (_.timer.state == .ACTIVE) {
            _.removeTimer(&_.timer);
        }
        // Schedule reconnection using our safe timer methods
        if (_.reconnect_timer.state == .ACTIVE) {
            _.removeTimer(&_.reconnect_timer);
        }

        const delay_ms = _.client.getReconnectDelay();
        if (delay_ms > 0) {
            _.addTimer(&_.reconnect_timer, delay_ms);
        }
    }

    // Callback for when Valkey client closes
    pub fn onValkeyClose(*_): void ! bun.JSTerminated {
        const globalObject = _.globalObject;

        // Update poll reference to allow garbage collection of disconnected clients
        const _deref_guard = DerefGuard<*JSValkeyClient>{ val: _ };
        const _poll_guard = UpdatePollRefGuard<*JSValkeyClient>{ val: _ };

        if (_.timer.state == .ACTIVE) {
            _.removeTimer(&_.timer);
        }

        let this_jsvalue: JSValue = undefined;
        if (v := _.this_value.tryGet()) {
            this_jsvalue = v;
        } else return;
        this_jsvalue.ensureStillAlive();

        // Create an error value
        const error_value = protocol.valkeyErrorToJS(globalObject, "Connection closed", protocol.RedisError.ConnectionClosed);

        const loop = _.client.vm.eventLoop();
        loop.enter();
        const _exit_guard = ExitGuard<@TypeOf(loop)>{ val: loop };

        if (!this_jsvalue.isUndefined()) {
            if (promise := js.connectionPromiseGetCached(this_jsvalue)) {
                js.connectionPromiseSetCached(this_jsvalue, globalObject, .zero);
                promise.asPromise().?.reject(globalObject, error_value)?;
            }
        }

        // Call onClose callback if it exists
        if (on_close := js.oncloseGetCached(this_jsvalue)) {
            * = on_close.call(
                globalObject,
                this_jsvalue,
                &[error_value],
            ) catch e => globalObject.reportActiveExceptionAsUnhandled(e);
        }
    }

    // Callback for when Valkey client times out
    pub fn onValkeyTimeout(*_) {
        _.clientFail("Connection timeout", protocol.RedisError.ConnectionClosed);
    }

    pub fn clientFail(*_, message: []const u8, err: protocol.RedisError): void ! bun.JSTerminated {
        _.client.fail(message, err)?;
    }

    pub fn failWithJSValue(*_, value: JSValue) {
        const this_value_opt = _.this_value.tryGet();
        if (this_value_opt == null) return;
        const this_value = this_value_opt.?;
        const globalObject = _.globalObject;
        if (on_close := js.oncloseGetCached(this_value)) {
            const loop = _.client.vm.eventLoop();
            loop.enter();
            const _exit_guard = ExitGuard<@TypeOf(loop)>{ val: loop };
            * = on_close.call(
                globalObject,
                this_value,
                &[value],
            ) catch e => globalObject.reportActiveExceptionAsUnhandled(e);
        }
    }

    fn closeWithoutSocketNextTick(*_) {
        _.client.status = .connecting;
        _.updatePollRef();
        _.enqueueDeferredClose(.without_socket);
    }

    fn enqueueDeferredClose(*_, what: DeferredClose) {
        _.ref();
        // socket close can potentially call JS so we need to enqueue the deinit
        const Holder = CloseSocketHolder;
        let holder = bun.handleOom(bun.default_allocator.create(Holder));
        holder.* = Holder{
            ctx: _,
            what: what,
            task: undefined,
        };
        holder.task = jsc.AnyTask.New(Holder, Holder.run).init(holder);

        _.client.vm.enqueueTask(jsc.Task.init(&holder.task));
    }

    fn closeSocketNextTick(*_) {
        if (_.client.socket.isClosed()) return;
        _.enqueueDeferredClose(.socket);
    }

    pub fn finalize(*_) {
        const _deref_guard = DerefGuard<*JSValkeyClient>{ val: _ };

        _.stopTimers();
        _.this_value.finalize();
        _.client.flags.finalized = true;
        _.closeSocketNextTick();
        // We do not need to free the subscription context here because we're
        // guaranteed to have freed it by virtue of the fact that we are
        // garbage collected now and the subscription context holds a reference
        // to us. If we still had a subscription context, we would never be
        // garbage collected.
        bun.debugAssert(!_._subscription_ctx.is_subscriber);
    }

    pub fn stopTimers(*_) {
        // Use safe timer removal methods to ensure proper reference counting
        if (_.timer.state == .ACTIVE) {
            _.removeTimer(&_.timer);
        }
        if (_.reconnect_timer.state == .ACTIVE) {
            _.removeTimer(&_.reconnect_timer);
        }
    }

    fn connect(*_): !void {
        bun.debugAssert(_.client.socket.isClosed());
        _.client.flags.needs_to_open_socket = false;
        const vm = _.client.vm;

        _.ref();
        const _deref_guard = DerefGuard<*JSValkeyClient>{ val: _ };

        const is_tls = _.client.tls != .none;
        const group = if (is_tls) vm.rareData().valkeyGroup(vm, true) else vm.rareData().valkeyGroup(vm, false);
        const ssl_ctx: (*uws.SslCtx)? = switch (_.client.tls) {
            .none => null,
            .enabled => vm.rareData().defaultClientSslCtx(),
            .custom => |*custom| brk: {
                * = custom;
                // Reuse across reconnect — the SSL_CTX is the only thing the
                // old `_socket_ctx` cache existed to preserve.
                if (_._secure == null) {
                    let err: uws.create_bun_socket_error_t = .none;
                    // Per-VM weak cache: a `duplicate()`'d client (or any
                    // other client with the same config) hits the same
                    // `SSL_CTX*` instead of rebuilding.
                    if (ctx := vm.rareData().sslCtxCache().getOrCreate(custom, &err)) {
                        _._secure = ctx;
                    } else {
                        _.client.flags.enable_auto_reconnect = false;
                        _.clientFail("Failed to create TLS context", protocol.RedisError.ConnectionClosed)?;
                        _.closeWithoutSocketNextTick();
                        return;
                    }
                }
                break :brk _._secure.?;
            },
        };

        _.ref();
        // Balance the ref above if connect() throws — the caller (e.g. send())
        // only knows to clean up its own state, not the keep-alive ref.
        let _connect_guard = ConnectErrGuard{ client: _, active: true };
        _.client.status = .connecting;
        _.updatePollRef();
        _.client.socket = _.client.address.connect(&_.client, group, ssl_ctx, is_tls)?;
        _connect_guard.active = false;
    }

    pub fn ensureDialing(*_) {
        if (!_.client.flags.needs_to_open_socket) {
            return;
        }

        _.connect() catch e => {
            debug("first dial failed before a socket was opened: {s}", .{@errorName(e)});
            _.closeWithoutSocketNextTick();
            return;
        };
        _.resetConnectionTimeout();
    }

    pub fn send(*_, globalThis: *jsc.JSGlobalObject, _: JSValue, command: *const Command): !*jsc.JSPromise {
        _.ensureDialing();

        const _poll_guard = UpdatePollRefGuard<*JSValkeyClient>{ val: _ };
        const send_fn = valkey.ValkeyClient.send;
        return send_fn(&_.client, globalThis, command)?;
    }

    // Getter for memory cost - useful for diagnostics
    pub fn memoryCost(*_): usize {
        // TODO(markovejnovic): This is most-likely wrong because I didn't know better.
        let memory_cost: usize = @sizeOf(JSValkeyClient);

        // Add size of all internal buffers
        memory_cost += _.client.write_buffer.byte_list.cap;
        memory_cost += _.client.read_buffer.byte_list.cap;

        // Add queue sizes
        memory_cost += _.client.in_flight.count * @sizeOf(valkey.Command.PromisePair);
        for (command of _.client.queue.readableSlice(0)) {
            memory_cost += command.serialized_data.len;
        }
        memory_cost += _.client.queue.count * @sizeOf(valkey.Command.Entry);
        return memory_cost;
    }

    fn deinit(*_) {
        bun.debugAssert(_.client.socket.isClosed());
        if (s := _._secure) { bun.BoringSSL.c.SSL_CTX_free(s); }
        _.client.deinit(null);
        _.poll_ref.disable();
        _.stopTimers();
        _.ref_count.assertNoRefs();

        bun.destroy(_);
    }

    /// Keep the event loop alive, or don't keep it alive
    ///
    /// This requires this_value to be alive.
    pub fn updatePollRef(*_) {
        // TODO(markovejnovic): This function is such a crazy cop out. We really
        // should be treating valkey as a state machine, with well-defined
        // state and modes in which it tracks and manages its own lifecycle.
        // This is a mess beyond belief and it is incredibly fragile.
        const has_pending_commands = _.client.hasAnyPendingCommands();

        const has_activity = has_pending_commands ||
            (_._subscription_ctx.hasSubscriptions(_.globalObject) catch false) ||
            _.client.flags.is_reconnecting;

        // There's a couple cases to handle here:
        if (has_activity || _.client.status == .connecting) {
            // If we currently have pending activity or we are connecting, we need to keep the event
            // loop alive.
            _.poll_ref.ref(_.client.vm);
        } else {
            // There is no pending activity so it is safe to remove the event
            // loop.
            _.poll_ref.unref(_.client.vm);
        }

        if (_.this_value.isEmpty()) {
            return;
        }

        // Orthogonal to this, we need to manage the strong reference to the JS
        // object.
        switch (_.client.status) {
            .connecting, .connected => {
                // Whenever we're connected, we need to keep the object alive.
                //
                // TODO(markovejnovic): This is a leak.
                // Note this is an intentional leak. Unless the user manually
                // closes the connection, the object will stay alive forever,
                // even if it falls out of scope. This is kind of stupid, since
                // if the object is out of scope, and isn't subscribed upon,
                // how exactly is the user going to call anything on the object?
                //
                // It is 100% safe to drop the strong reference there and let
                // the object be GC'd, but we're not doing that now.
                debug("upgrading this_value since we are connected/connecting", .{});
                _.this_value.upgrade(_.globalObject);
            },
            .disconnected => {
                // If we're disconnected, we need to check if we have
                // any pending activity.
                if (has_activity) {
                    debug("upgrading this_value since there is pending activity", .{});
                    // If we have pending activity, we need to keep the object
                    // alive.
                    _.this_value.upgrade(_.globalObject);
                } else {
                    debug("downgrading this_value since there is no pending activity", .{});
                    // If we don't have any pending activity, we can drop the
                    // strong reference.
                    _.this_value.downgrade();
                }
            },
        }
    }

    pub const jsSend = fns.jsSend;
    pub const @"type" = fns.type;
    pub const append = fns.append;
    pub const bitcount = fns.bitcount;
    pub const blpop = fns.blpop;
    pub const brpop = fns.brpop;
    pub const copy = fns.copy;
    pub const decr = fns.decr;
    pub const decrby = fns.decrby;
    pub const del = fns.del;
    pub const dump = fns.dump;
    pub const duplicate = fns.duplicate;
    pub const exists = fns.exists;
    pub const expire = fns.expire;
    pub const expireat = fns.expireat;
    pub const expiretime = fns.expiretime;
    pub const get = fns.get;
    pub const getBuffer = fns.getBuffer;
    pub const getbit = fns.getbit;
    pub const getdel = fns.getdel;
    pub const getex = fns.getex;
    pub const getrange = fns.getrange;
    pub const getset = fns.getset;
    pub const hgetall = fns.hgetall;
    pub const hget = fns.hget;
    pub const hincrby = fns.hincrby;
    pub const hincrbyfloat = fns.hincrbyfloat;
    pub const hkeys = fns.hkeys;
    pub const hdel = fns.hdel;
    pub const hexists = fns.hexists;
    pub const hgetdel = fns.hgetdel;
    pub const hgetex = fns.hgetex;
    pub const hlen = fns.hlen;
    pub const hmget = fns.hmget;
    pub const hmset = fns.hmset;
    pub const hrandfield = fns.hrandfield;
    pub const hscan = fns.hscan;
    pub const hset = fns.hset;
    pub const hsetex = fns.hsetex;
    pub const hsetnx = fns.hsetnx;
    pub const hstrlen = fns.hstrlen;
    pub const hvals = fns.hvals;
    pub const hexpire = fns.hexpire;
    pub const hexpireat = fns.hexpireat;
    pub const hexpiretime = fns.hexpiretime;
    pub const hpersist = fns.hpersist;
    pub const hpexpire = fns.hpexpire;
    pub const hpexpireat = fns.hpexpireat;
    pub const hpexpiretime = fns.hpexpiretime;
    pub const hpttl = fns.hpttl;
    pub const httl = fns.httl;
    pub const incr = fns.incr;
    pub const incrby = fns.incrby;
    pub const incrbyfloat = fns.incrbyfloat;
    pub const keys = fns.keys;
    pub const lindex = fns.lindex;
    pub const linsert = fns.linsert;
    pub const llen = fns.llen;
    pub const lmove = fns.lmove;
    pub const lmpop = fns.lmpop;
    pub const lpop = fns.lpop;
    pub const lpos = fns.lpos;
    pub const lpush = fns.lpush;
    pub const lpushx = fns.lpushx;
    pub const lrange = fns.lrange;
    pub const lrem = fns.lrem;
    pub const lset = fns.lset;
    pub const ltrim = fns.ltrim;
    pub const mget = fns.mget;
    pub const mset = fns.mset;
    pub const msetnx = fns.msetnx;
    pub const persist = fns.persist;
    pub const pexpire = fns.pexpire;
    pub const pexpireat = fns.pexpireat;
    pub const pexpiretime = fns.pexpiretime;
    pub const pfadd = fns.pfadd;
    pub const ping = fns.ping;
    pub const psetex = fns.psetex;
    pub const psubscribe = fns.psubscribe;
    pub const pttl = fns.pttl;
    pub const publish = fns.publish;
    pub const pubsub = fns.pubsub;
    pub const punsubscribe = fns.punsubscribe;
    pub const randomkey = fns.randomkey;
    pub const rename = fns.rename;
    pub const renamenx = fns.renamenx;
    pub const rpop = fns.rpop;
    pub const rpoplpush = fns.rpoplpush;
    pub const rpush = fns.rpush;
    pub const rpushx = fns.rpushx;
    pub const sadd = fns.sadd;
    pub const scan = fns.scan;
    pub const scard = fns.scard;
    pub const script = fns.script;
    pub const sdiff = fns.sdiff;
    pub const sdiffstore = fns.sdiffstore;
    pub const sinter = fns.sinter;
    pub const sintercard = fns.sintercard;
    pub const sinterstore = fns.sinterstore;
    pub const select = fns.select;
    pub const set = fns.set;
    pub const setbit = fns.setbit;
    pub const setex = fns.setex;
    pub const setnx = fns.setnx;
    pub const setrange = fns.setrange;
    pub const sismember = fns.sismember;
    pub const smembers = fns.smembers;
    pub const smismember = fns.smismember;
    pub const smove = fns.smove;
    pub const spop = fns.spop;
    pub const spublish = fns.spublish;
    pub const srandmember = fns.srandmember;
    pub const srem = fns.srem;
    pub const sscan = fns.sscan;
    pub const strlen = fns.strlen;
    pub const subscribe = fns.subscribe;
    pub const substr = fns.substr;
    pub const sunion = fns.sunion;
    pub const sunionstore = fns.sunionstore;
    pub const touch = fns.touch;
    pub const ttl = fns.ttl;
    pub const unlink = fns.unlink;
    pub const unsubscribe = fns.unsubscribe;
    pub const zcard = fns.zcard;
    pub const zcount = fns.zcount;
    pub const zlexcount = fns.zlexcount;
    pub const zpopmax = fns.zpopmax;
    pub const zpopmin = fns.zpopmin;
    pub const zrandmember = fns.zrandmember;
    pub const zrange = fns.zrange;
    pub const zrangebylex = fns.zrangebylex;
    pub const zrangebyscore = fns.zrangebyscore;
    pub const zrangestore = fns.zrangestore;
    pub const zrank = fns.zrank;
    pub const zrem = fns.zrem;
    pub const zremrangebylex = fns.zremrangebylex;
    pub const zremrangebyrank = fns.zremrangebyrank;
    pub const zremrangebyscore = fns.zremrangebyscore;
    pub const zrevrange = fns.zrevrange;
    pub const zrevrangebylex = fns.zrevrangebylex;
    pub const zrevrangebyscore = fns.zrevrangebyscore;
    pub const zrevrank = fns.zrevrank;
    pub const zscore = fns.zscore;
    pub const zincrby = fns.zincrby;
    pub const zmscore = fns.zmscore;
    pub const zadd = fns.zadd;
    pub const zscan = fns.zscan;
    pub const zdiff = fns.zdiff;
    pub const zdiffstore = fns.zdiffstore;
    pub const zinter = fns.zinter;
    pub const zintercard = fns.zintercard;
    pub const zinterstore = fns.zinterstore;
    pub const zunion = fns.zunion;
    pub const zunionstore = fns.zunionstore;
    pub const zmpop = fns.zmpop;
    pub const bzmpop = fns.bzmpop;
    pub const bzpopmin = fns.bzpopmin;
    pub const bzpopmax = fns.bzpopmax;
    pub const blmove = fns.blmove;
    pub const blmpop = fns.blmpop;
    pub const brpoplpush = fns.brpoplpush;
}

/// Referenced by `dispatch.zig` (kind = `.valkey[_tls]`).
pub fn SocketHandler(comptime ssl: bool): type {
    return struct {
        const SocketType = uws.NewSocketHandler(ssl);
        static fn to_socket(s: SocketType): Socket {
            if (comptime ssl) {
                return Socket{ .SocketTLS = s };
            }

            return Socket{ .SocketTCP = s };
        }
        pub static fn onOpen(client: *JSValkeyClient, socket: SocketType): void ! bun.JSTerminated {
            client.client.socket = to_socket(socket);
            (client.client.onOpen(to_socket(socket)))?;
        }

        static fn onHandshake_(client: *JSValkeyClient, _: anytype, success: i32, ssl_error: uws.us_bun_verify_error_t): void ! bun.JSTerminated {
            debug("onHandshake: {d} error={d} reason={s} code={s}", .{
                success,
                ssl_error.error_no,
                if (ssl_error.reason != null) bun.span(ssl_error.reason[0..bun.len(ssl_error.reason) :0]) else "no reason",
                if (ssl_error.code != null) bun.span(ssl_error.code[0..bun.len(ssl_error.code) :0]) else "no code",
            });
            const handshake_success = if (success == 1) true else false;
            client.ref();
            const _deref_guard = DerefGuard<@TypeOf(client)>{ val: client };
            const _poll_guard = UpdatePollRefGuard<@TypeOf(client)>{ val: client };
            const vm = client.client.vm;
            if (handshake_success) {
                if (client.client.tls.rejectUnauthorized(vm)) {
                    // only reject the connection if reject_unauthorized == true
                    if (ssl_error.error_no != 0) {
                        // Certificate chain validation failed.
                        return (failHandshakeWithVerifyError(client, vm, &ssl_error))?;
                    }

                    // Certificate chain is valid; verify the hostname matches the
                    // certificate. Prefer the SNI servername if one was set, otherwise
                    // fall back to the host from the connection URL. Unix-domain
                    // sockets have no hostname to verify, so skip the identity check
                    // for redis+tls+unix:// / valkey+tls+unix:// connections.
                    const ssl_ptr: *BoringSSL.c.SSL = @ptrCast(client.client.socket.getNativeHandle());
                    let hostname: []const u8 = "";
                    if (servername := BoringSSL.c.SSL_get_servername(ssl_ptr, 0)) {
                        hostname = servername[0..bun.len(servername)];
                    } else {
                        hostname = switch (client.client.address) {
                            .host => |h| h.host,
                            .unix => "",
                        };
                    }
                    // URL.host() serialises IPv6 literals with surrounding brackets
                    // (e.g. "[::1]"). Strip them so checkServerIdentity can recognise
                    // the value as an IP and match against IP SAN entries; this
                    // mirrors what connectAnon already does before getaddrinfo.
                    if (hostname.len >= 2 && hostname[0] == '[' && hostname[hostname.len - 1] == ']') {
                        hostname = hostname[1 .. hostname.len - 1];
                    }
                    if (hostname.len > 0 && !BoringSSL.checkServerIdentity(ssl_ptr, hostname)) {
                        const e = client.globalObject.ERR(.TLS_CERT_ALTNAME_INVALID, "Hostname/IP does not match certificate's altnames: Host: {s}", .{hostname}).toJS();
                        return (failHandshake(client, vm, e))?;
                    }
                }
                (client.client.start())?;
            } else {
                // if we are here is because the server rejected us, and the error_no is the cause of this
                // no matter if reject_unauthorized is false, because we were disconnected by the server
                return (failHandshakeWithVerifyError(client, vm, &ssl_error))?;
            }
        }

        static fn failHandshakeWithVerifyError(client: *JSValkeyClient, vm: *jsc.VirtualMachine, ssl_error: *const uws.us_bun_verify_error_t): void ! bun.JSTerminated {
            const ssl_js_value = ssl_error.toJS(client.globalObject) catch e => switch (e) {
                error.JSTerminated => return error.JSTerminated,
                error.OutOfMemory => bun.outOfMemory(),
                error.JSError => {
                    // Clear any pending exception since we can't convert it to JS,
                    // but still fail-close the connection so we never fall through
                    // to the authenticated state after a rejected handshake.
                    client.globalObject.clearException();
                    client.client.flags.is_authenticated = false;
                    client.client.flags.is_manually_closed = true;
                    client.client.close();
                    return;
                },
            };
            return (failHandshake(client, vm, ssl_js_value))?;
        }

        static fn failHandshake(client: *JSValkeyClient, vm: *jsc.VirtualMachine, err_value: jsc.JSValue): void ! bun.JSTerminated {
            client.client.flags.is_authenticated = false;
            const loop = vm.eventLoop();
            loop.enter();
            const _exit_guard = ExitGuard<@TypeOf(loop)>{ val: loop };
            (client.client.failWithJSValue(client.globalObject, err_value))?;
        }

        pub const onHandshake = if (ssl) onHandshake_ else null;

        pub static fn onClose(client: *JSValkeyClient, _: SocketType, _: i32, _: *!?) {
            // No need to deref since client.client.onClose() invokes onValkeyClose which does the deref.

            debug("Socket closed.", .{});
            client.ref();
            // Ensure the socket pointer is updated.
            client.client.socket = .{ .SocketTCP = .detached };
            client.client.status = .disconnected;
            const _disc_guard = DisconnectDerefGuard{ client: client };

            client.client.onClose() catch {}; // TODO: properly propagate exception upwards
        }

        pub static fn onEnd(client: *JSValkeyClient, socket: SocketType) {
            * = client;
            * = socket;

            // Half-opened sockets are not allowed.
            // usockets will always call onClose after onEnd in this case so we don't need to do anything here
        }

        pub static fn onConnectError(client: *JSValkeyClient, _: SocketType, _: i32): void ! bun.JSTerminated {
            // Ensure the socket pointer is updated.
            client.client.socket = .{ .SocketTCP = .detached };
            client.client.status = .disconnected;
            client.ref();
            const _disc_guard = DisconnectDerefGuard{ client: client };

            (client.client.onClose())?;
        }

        pub static fn onTimeout(client: *JSValkeyClient, socket: SocketType) {
            debug("Socket timed out.", .{});

            client.client.socket = to_socket(socket);
            // Handle socket timeout
        }

        pub static fn onData(client: *JSValkeyClient, socket: SocketType, data: []const u8) {
            // Ensure the socket pointer is updated.
            client.client.socket = to_socket(socket);

            client.ref();
            const _deref_guard = DerefGuard<@TypeOf(client)>{ val: client };
            client.client.onData(data) catch {}; // TODO: properly propagate exception upwards
            client.updatePollRef();
        }

        pub static fn onWritable(client: *JSValkeyClient, socket: SocketType) {
            client.client.socket = to_socket(socket);
            client.ref();
            const _deref_guard = DerefGuard<@TypeOf(client)>{ val: client };
            client.client.onWritable();
            client.updatePollRef();
        }
    }
}

// Parse JavaScript options into Valkey client options
struct Options {
    pub static fn fromJS(globalObject: *jsc.JSGlobalObject, options_obj: jsc.JSValue): !valkey.Options {
        let result = valkey.Options{
            enable_auto_pipelining: !bun.feature_flag.BUN_FEATURE_FLAG_DISABLE_REDIS_AUTO_PIPELINING.get(),
        };

        if (idle_timeout := (options_obj.getOptionalInt(globalObject, "idleTimeout", u32))?) {
            result.idle_timeout_ms = idle_timeout;
        }

        if (connection_timeout := (options_obj.getOptionalInt(globalObject, "connectionTimeout", u32))?) {
            result.connection_timeout_ms = connection_timeout;
        }

        if (auto_reconnect := (options_obj.getIfPropertyExists(globalObject, "autoReconnect"))?) {
            result.enable_auto_reconnect = auto_reconnect.toBoolean();
        }

        if (max_retries := (options_obj.getOptionalInt(globalObject, "maxRetries", u32))?) {
            result.max_retries = max_retries;
        }

        if (enable_offline_queue := (options_obj.getIfPropertyExists(globalObject, "enableOfflineQueue"))?) {
            result.enable_offline_queue = enable_offline_queue.toBoolean();
        }

        if (enable_auto_pipelining := (options_obj.getIfPropertyExists(globalObject, "enableAutoPipelining"))?) {
            result.enable_auto_pipelining = enable_auto_pipelining.toBoolean();
        }

        if (tls := (options_obj.getIfPropertyExists(globalObject, "tls"))?) {
            if (tls.isBoolean() || tls.isUndefinedOrNull()) {
                result.tls = if (tls.toBoolean()) .enabled else .none;
            } else if (tls.isObject()) {
                if (ssl_config := (jsc.API.ServerConfig.SSLConfig.fromJS(globalObject.bunVM(), globalObject, tls))?) {
                    result.tls = .{ .custom = ssl_config };
                } else {
                    return globalObject.throwInvalidArgumentType("tls", "tls", "object");
                }
            } else {
                return globalObject.throwInvalidArgumentType("tls", "tls", "boolean or object");
            }
        }

        return result;
    }
}
