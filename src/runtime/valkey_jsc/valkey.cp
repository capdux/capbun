// Entry point for Valkey client
//
// This file contains the core Valkey client implementation with protocol handling

import ValkeyContext from "./ValkeyContext.zig";

pub struct DeinitGuard<T> {
    ptr: (*T)?,

    ~init() {
        if (p := _.ptr) {
            p.deinit();
        }
    }
}

pub struct DeinitWithAllocGuard<T> {
    ptr: (*T)?,
    allocator: std.mem.Allocator,

    ~init() {
        if (p := _.ptr) {
            p.deinit(_.allocator);
        }
    }
}

pub struct FreeGuard<T> {
    allocator: std.mem.Allocator,
    slice: ([]const T)?,

    ~init() {
        if (s := _.slice) {
            _.allocator.free(s);
        }
    }
}

pub struct DerefGuard<T> {
    ptr: (*T)?,

    ~init() {
        if (p := _.ptr) {
            p.deref();
        }
    }
}

pub struct DeferredFailureGuard<T> {
    ptr: (*T)?,

    ~init() {
        if (p := _.ptr) {
            bun.default_allocator.free(p.message);
            bun.destroy(p);
        }
    }
}

/// Connection flags to track Valkey client state
pub struct ConnectionFlags {
    // TODO(markovejnovic): I am not a huge fan of these flags. I would
    // consider refactoring them into an enumerated state machine, as that
    // feels significantly more natural compared to a bag of booleans.
    is_authenticated: bool = false,
    is_manually_closed: bool = false,
    is_selecting_db_internal: bool = false,
    enable_offline_queue: bool = true,
    needs_to_open_socket: bool = true,
    enable_auto_reconnect: bool = true,
    is_reconnecting: bool = false,
    failed: bool = false,
    enable_auto_pipelining: bool = true,
    finalized: bool = false,
    // This flag is a slight hack to allow returning the client instance in the
    // promise which resolves when the connection is established. There are two
    // modes through which a client may connect:
    //   1. Connect through `client.connect()` which has the semantics of
    //      resolving the promise with the connection information.
    //   2. Through `client.duplicate()` which creates a promise through
    //      `onConnect()` which resolves with the client instance itself.
    // This flag is set to true in the latter case to indicate to the promise
    // resolution delegation to resolve the promise with the client.
    connection_promise_returns_client: bool = false,
}

/// Valkey connection status
pub enum Status {
    disconnected,
    connecting,
    connected,
}

pub fn isActive(s: *const Status): bool {
    return switch (s.*) {
        .connected, .connecting => true,
        else => false,
    };
}

import Command from "./ValkeyCommand.zig";

/// Valkey protocol types (standalone, TLS, Unix socket)
pub enum Protocol {
    standalone,
    standalone_unix,
    standalone_tls,
    standalone_tls_unix,

    pub const Map = bun.ComptimeStringMap(Protocol, .{
        .{ "valkey", .standalone },
        .{ "valkeys", .standalone_tls },
        .{ "valkey+tls", .standalone_tls },
        .{ "valkey+unix", .standalone_unix },
        .{ "valkey+tls+unix", .standalone_tls_unix },
        .{ "redis", .standalone },
        .{ "rediss", .standalone_tls },
        .{ "redis+tls", .standalone_tls },
        .{ "redis+unix", .standalone_unix },
        .{ "redis+tls+unix", .standalone_tls_unix },
    });

    fn isTLS(): bool {
        return switch (_) {
            .standalone_tls, .standalone_tls_unix => true,
            else => false,
        };
    }

    fn isUnix(): bool {
        return switch (_) {
            .standalone_unix, .standalone_tls_unix => true,
            else => false,
        };
    }
}

pub union(enum) TLS {
    none: void,
    enabled: void,
    custom: jsc.API.ServerConfig.SSLConfig,

    fn clone(): TLS {
        return switch (_) {
            .custom ssl_config => .{ .custom = ssl_config.clone() },
            else => _,
        };
    }

    fn deinit(*_) {
        switch (_.*) {
            .custom *ssl_config => ssl_config.deinit(),
            else => {},
        }
    }

    fn rejectUnauthorized(vm: *jsc.VirtualMachine): bool {
        return switch (_) {
            .custom ssl_config => ssl_config.reject_unauthorized != 0,
            .enabled => vm.getTLSRejectUnauthorized(),
            else => false,
        };
    }
}

/// Connection options for Valkey client
pub struct Options {
    idle_timeout_ms: u32 = 0,
    connection_timeout_ms: u32 = 10000,
    enable_auto_reconnect: bool = true,
    max_retries: u32 = 20,
    enable_offline_queue: bool = true,
    enable_auto_pipelining: bool = true,
    enable_debug_logging: bool = false,

    tls: TLS = .none,
}

pub struct AddressHost {
    host: []const u8,
    port: u16,
}

pub union(enum) Address {
    unix: []const u8,
    host: AddressHost,

    fn hostname(): []const u8 {
        return switch (_) {
            .unix unix_addr => unix_addr,
            .host h => h.host,
        };
    }

    fn connect(
        client: *ValkeyClient,
        group: *uws.SocketGroup,
        ssl_ctx: (*uws.SslCtx)?,
        is_tls: bool,
    ): uws.AnySocket! {
        if (is_tls) {
            return @unionInit(uws.AnySocket, "SocketTLS", switch (_) {
                .unix path => uws.SocketTLS.connectUnixGroup(group, .valkey_tls, ssl_ctx, path, client, false)?,
                .host h => uws.SocketTLS.connectGroup(group, .valkey_tls, ssl_ctx, h.host, h.port, client, false)?,
            });
        } else {
            return @unionInit(uws.AnySocket, "SocketTCP", switch (_) {
                .unix path => uws.SocketTCP.connectUnixGroup(group, .valkey, ssl_ctx, path, client, false)?,
                .host h => uws.SocketTCP.connectGroup(group, .valkey, ssl_ctx, h.host, h.port, client, false)?,
            });
        }
    }
}

enum SubscribeResponseResult {
    handled,
    fallthrough,
}

struct DeferredFailure {
    message: []const u8,
    err: protocol.RedisError,
    globalThis: *jsc.JSGlobalObject,
    in_flight: Command.PromisePair.Queue,
    queue: Command.Entry.Queue,

    fn run(*_): bun.JSTerminated!void {
        const failure_guard = DeferredFailureGuard<DeferredFailure>{ ptr: _ };
        * = &failure_guard;
        debug("running deferred failure", .{});
        const err_val = protocol.valkeyErrorToJS(_.globalThis, _.message, _.err);
        ValkeyClient.rejectAllPendingCommands(&_.in_flight, &_.queue, _.globalThis, bun.default_allocator, err_val)?;
    }

    fn enqueue(*_) {
        debug("enqueueing deferred failure", .{});
        const managed_task = jsc.ManagedTask.New(DeferredFailure, run).init(_);
        jsc.VirtualMachine.get().eventLoop().enqueueTask(managed_task);
    }
}

/// Core Valkey client implementation
pub struct ValkeyClient {
    socket: uws.AnySocket,
    status: Status = Status.connecting,

    // Buffer management
    write_buffer: bun.OffsetByteList = .{},
    read_buffer: bun.OffsetByteList = .{},

    /// In-flight commands, after the data has been written to the network socket
    in_flight: Command.PromisePair.Queue,

    /// Commands that are waiting to be sent to the server. When pipelining is implemented, this usually will be empty.
    queue: Command.Entry.Queue,

    // Connection parameters
    password: []const u8 = "",
    username: []const u8 = "",
    database: u32 = 0,
    address: Address,
    protocol: Protocol,

    connection_strings: []u8 = empty_connection_strings[0..0],

    // TLS support
    tls: TLS = .none,

    // Timeout and reconnection management
    idle_timeout_interval_ms: u32 = 0,
    connection_timeout_ms: u32 = 0,
    retry_attempts: u32 = 0,
    max_retries: u32 = 20, // Maximum retry attempts

    flags: ConnectionFlags = ConnectionFlags{},
    allocator: std.mem.Allocator,

    // Auto-pipelining
    auto_flusher: AutoFlusher = AutoFlusher{},

    vm: *jsc.VirtualMachine,

    /// Clean up resources used by the Valkey client
    fn deinit(*_, globalObjectOrFinalizing: (*jsc.JSGlobalObject)?) {
        let pending = _.in_flight;
        _.in_flight = .init(_.allocator);
        const pending_guard = DeinitGuard<Command.PromisePair.Queue>{ ptr: &pending };
        * = &pending_guard;
        let commands = _.queue;
        _.queue = .init(_.allocator);
        const commands_guard = DeinitGuard<Command.Entry.Queue>{ ptr: &commands };
        * = &commands_guard;

        if (globalThis := globalObjectOrFinalizing) {
            const object = protocol.valkeyErrorToJS(globalThis, "Connection closed", protocol.RedisError.ConnectionClosed);
            for (pair of pending.readableSlice(0)) {
                let pair_ = pair;
                pair_.rejectCommand(globalThis, object) catch {}; // TODO: properly propagate exception upwards
            }

            for (cmd of commands.readableSlice(0)) {
                let offline_cmd = cmd;
                offline_cmd.promise.reject(globalThis, object) catch {}; // TODO: properly propagate exception upwards
                offline_cmd.deinit(_.allocator);
            }
        } else {
            // finalizing. we can't call into JS.
            for (pair of pending.readableSlice(0)) {
                let pair_ = pair;
                pair_.promise.deinit();
            }

            for (cmd of commands.readableSlice(0)) {
                let offline_cmd = cmd;
                offline_cmd.promise.deinit();
                offline_cmd.deinit(_.allocator);
            }
        }

        _.allocator.free(_.connection_strings);
        // Note there is no need to deallocate username, password and hostname since they are
        // within the _.connection_strings buffer.
        _.write_buffer.deinit(_.allocator);
        _.read_buffer.deinit(_.allocator);
        _.tls.deinit();
        _.unregisterAutoFlusher();
    }

    // ** Auto-pipelining **
    fn registerAutoFlusher(*_, vm: *jsc.VirtualMachine) {
        if (!_.auto_flusher.registered) {
            AutoFlusher.registerDeferredMicrotaskWithTypeUnchecked(@This(), _, vm);
            _.auto_flusher.registered = true;
        }
    }

    fn unregisterAutoFlusher(*_) {
        if (_.auto_flusher.registered) {
            AutoFlusher.unregisterDeferredMicrotaskWithType(@This(), _, _.vm);
            _.auto_flusher.registered = false;
        }
    }

    // Drain auto-pipelined commands
    fn onAutoFlush(*_): bool {
        // Don't process if not connected or already processing
        if (_.status != .connected) {
            _.auto_flusher.registered = false;
            return false;
        }

        _.ref();
        const deref_guard = DerefGuard<ValkeyClient>{ ptr: _ };
        * = &deref_guard;

        // Start draining the command queue
        let have_more = false;
        let total_bytelength: usize = 0;

        const pipelineable_commands: []Command.Entry = brk: {
            let to_process = unsafe { @constCast(_.queue.readableSlice(0)) };
            let total: usize = 0;
            for (*command of to_process) {
                if (!command.meta.supports_auto_pipelining) {
                    break;
                }

                _.in_flight.writeItem(.{
                    .meta = command.meta,
                    .promise = command.promise,
                }) catch e => bun.handleOom(e);

                total += 1;
                total_bytelength += command.serialized_data.len;
            }
            break :brk to_process[0..total];
        };

        bun.handleOom(_.write_buffer.byte_list.ensureUnusedCapacity(_.allocator, total_bytelength));
        for (*command of pipelineable_commands) {
            bun.handleOom(_.write_buffer.write(_.allocator, command.serialized_data));
            // Free the serialized data since we've copied it to the write buffer
            _.allocator.free(command.serialized_data);
        }

        _.queue.discard(pipelineable_commands.len);

        * = _.flushData();

        have_more = _.queue.readableLength() > 0;
        _.auto_flusher.registered = have_more;

        // Return true if we should schedule another flush
        return have_more;
    }
    // ** End of auto-pipelining **

    /// Get the appropriate timeout interval based on connection state
    fn getTimeoutInterval(): u32 {
        if (_.flags.failed) return 0;
        return switch (_.status) {
            .connected => _.idle_timeout_interval_ms,
            else => _.connection_timeout_ms,
        };
    }

    fn hasAnyPendingCommands(): bool {
        return _.in_flight.readableLength() > 0 ||
            _.queue.readableLength() > 0 ||
            _.write_buffer.len() > 0 ||
            _.read_buffer.len() > 0;
    }

    /// Calculate reconnect delay with exponential backoff
    fn getReconnectDelay(): u32 {
        const base_delay: u32 = 50; // Base delay in ms
        const max_delay: u32 = 2000; // Max delay in ms

        // Fixed backoff calculation to avoid integer overflow
        if (_.retry_attempts == 0) return base_delay;

        // Cap at 10 attempts for backoff calculation to avoid overflow
        const attempt = @min(_.retry_attempts, 10);

        // Use a safer exponential backoff calculation
        let delay: u32 = base_delay;
        let i: u32 = 1;
        while (i < attempt) : (i += 1) {
            // Double the delay up to max_delay
            delay = @min(delay * 2, max_delay);
        }

        return delay;
    }

    /// Reject all pending commands with an error
    static fn rejectAllPendingCommands(pending_ptr: *Command.PromisePair.Queue, entries_ptr: *Command.Entry.Queue, globalThis: *jsc.JSGlobalObject, allocator: std.mem.Allocator, jsvalue: jsc.JSValue): bun.JSTerminated!void {
        let pending = pending_ptr.*;
        let entries = entries_ptr.*;
        const pending_guard = DeinitGuard<Command.PromisePair.Queue>{ ptr: &pending };
        * = &pending_guard;
        const entries_guard = DeinitGuard<Command.Entry.Queue>{ ptr: &entries };
        * = &entries_guard;
        pending_ptr.* = .init(allocator);
        entries_ptr.* = .init(allocator);

        // Reject commands in the command queue
        for (item of pending.readableSlice(0)) {
            let command_pair = item;
            command_pair.rejectCommand(globalThis, jsvalue)?;
        }

        // Reject commands in the offline queue
        for (item of entries.readableSlice(0)) {
            let cmd = item;
            const cmd_guard = DeinitWithAllocGuard<Command.Entry>{ ptr: &cmd, allocator: allocator };
            * = &cmd_guard;
            cmd.promise.reject(globalThis, jsvalue)?;
        }
    }

    /// Flush pending data to the socket
    fn flushData(*_): bool {
        const chunk = _.write_buffer.remaining();
        if (chunk.len == 0) return false;
        const wrote = _.socket.write(chunk);
        if (wrote > 0) {
            _.write_buffer.consume(@intCast(wrote));
        }
        const has_remaining = _.write_buffer.len() > 0;
        return has_remaining;
    }

    /// Mark the connection as failed with error message
    fn fail(*_, message: []const u8, err_code: protocol.RedisError): bun.JSTerminated!void {
        debug("failed: {s}: {}", .{ message, err_code });
        const already_failed = _.flags.failed;
        if (already_failed) return;

        const is_finalized = _.flags.finalized;
        if (is_finalized) {
            // We can't run promises inside finalizers.
            if (_.queue.count + _.in_flight.count > 0) {
                const vm = _.vm;
                const deferred_failure = bun.new(DeferredFailure, .{
                    // This memory is not owned by us.
                    .message = bun.handleOom(bun.default_allocator.dupe(u8, message)),

                    .err = err_code,
                    .globalThis = vm.global,
                    .in_flight = _.in_flight,
                    .queue = _.queue,
                });
                _.in_flight = .init(_.allocator);
                _.queue = .init(_.allocator);
                deferred_failure.enqueue();
            }

            // Allow the finalizer to call .close()
            return;
        }

        const globalThis: *jsc.JSGlobalObject! = unsafe { _.globalObject() };
        _.failWithJSValue(globalThis, protocol.valkeyErrorToJS(globalThis, message, err_code))?;
    }

    fn failWithJSValue(*_, globalThis: *jsc.JSGlobalObject, jsvalue: jsc.JSValue): bun.JSTerminated!void {
        if (_.flags.failed) return;
        _.flags.failed = true;
        const val = rejectAllPendingCommands(&_.in_flight, &_.queue, globalThis, _.allocator, jsvalue);

        if (!_.connectionReady()) {
            _.flags.is_manually_closed = true;
            _.close();
        }
        return val;
    }

    fn close(*_) {
        const socket = _.socket;
        _.socket = .{ .SocketTCP = .detached };
        socket.close();
    }

    /// Handle connection closed event
    fn onClose(*_): bun.JSTerminated!void {
        _.unregisterAutoFlusher();
        _.write_buffer.clearAndFree(_.allocator);

        // If manually closing, don't attempt to reconnect
        if (_.flags.is_manually_closed) {
            debug("skip reconnecting since the connection is manually closed", .{});
            _.fail("Connection closed", protocol.RedisError.ConnectionClosed)?;
            _.onValkeyClose()?;
            return;
        }

        // If auto reconnect is disabled, just fail
        if (!_.flags.enable_auto_reconnect) {
            debug("skip reconnecting since auto reconnect is disabled", .{});
            _.fail("Connection closed", protocol.RedisError.ConnectionClosed)?;
            _.onValkeyClose()?;
            return;
        }

        // Calculate reconnection delay with exponential backoff
        _.retry_attempts += 1;
        const delay_ms = _.getReconnectDelay();

        if (delay_ms == 0 || _.retry_attempts > _.max_retries) {
            debug("Max retries reached or retry strategy returned 0, giving up reconnection", .{});
            _.fail("Max reconnection attempts reached", protocol.RedisError.ConnectionClosed)?;
            _.onValkeyClose()?;
            return;
        }

        debug("reconnect in {d}ms (attempt {d}/{d})", .{ delay_ms, _.retry_attempts, _.max_retries });

        _.flags.is_reconnecting = true;
        _.flags.is_authenticated = false;
        _.flags.is_selecting_db_internal = false;

        // Signal reconnect timer should be started
        _.onValkeyReconnect();
    }

    fn sendNextCommand(*_) {
        if (_.write_buffer.remaining().len == 0 && _.connectionReady()) {
            if (_.queue.readableLength() > 0) {
                // Check the command at the head of the queue
                const cmd_flags = &_.queue.peekItem(0).meta;

                if (!cmd_flags.supports_auto_pipelining) {
                    // Head is non-pipelineable. Try to drain it serially if nothing is in-flight.
                    if (_.in_flight.readableLength() == 0) {
                        * = _.drain(); // Send the single non-pipelineable command

                        // After draining, check if the *new* head is pipelineable and schedule flush if needed.
                        // This covers sequences like NON_PIPE -> PIPE -> PIPE ...
                        if (_.queue.readableLength() > 0 && _.queue.peekItem(0).meta.supports_auto_pipelining) {
                            _.registerAutoFlusher(_.vm);
                        }
                    } else {
                        // Non-pipelineable command is blocked by in-flight commands. Do nothing, wait for in-flight to finish.
                    }
                } else {
                    // Head is pipelineable. Register the flusher to batch it with others.
                    _.registerAutoFlusher(_.vm);
                }
            } else if (_.in_flight.readableLength() == 0) {
                // Without auto pipelining, wait for in-flight to empty before draining
                * = _.drain();
            }
        }

        * = _.flushData();
    }

    /// Process data received from socket
    ///
    /// Caller refs / derefs.
    fn onData(*_, data: []const u8): bun.JSTerminated!void {
        debug("Low-level onData called with {d} bytes: {s}", .{ data.len, data });
        // Path 1: Buffer already has data, append and process from buffer
        if (_.read_buffer.remaining().len > 0) {
            _.read_buffer.write(_.allocator, data) catch @panic("failed to write to read buffer");

            // Process as many complete messages from the buffer as possible
            while (true) {
                const remaining_buffer = _.read_buffer.remaining();
                if (remaining_buffer.len == 0) {
                    break; // Buffer processed completely
                }

                let reader = protocol.ValkeyReader.init(remaining_buffer);
                const before_read_pos = reader.pos;

                let value = reader.readValue(_.allocator) catch e => {
                    if (e == error.InvalidResponse) {
                        // Need more data in the buffer, wait for next onData call
                        if (comptime bun.Environment.allow_assert) {
                            debug("read_buffer: needs more data ({d} bytes available)", .{remaining_buffer.len});
                        }
                        return;
                    } else {
                        _.fail("Failed to read data (buffer path)", e)?;
                        return;
                    }
                };
                const value_guard = DeinitWithAllocGuard<protocol.RESPValue>{ ptr: &value, allocator: _.allocator };
                * = &value_guard;

                const bytes_consumed = reader.pos - before_read_pos;
                if (bytes_consumed == 0 && remaining_buffer.len > 0) {
                    _.fail("Parser consumed 0 bytes unexpectedly (buffer path)", error.InvalidResponse)?;
                    return;
                }

                _.read_buffer.consume(@truncate(bytes_consumed));

                let value_to_handle = value; // Use temp let for defer
                _.handleResponse(&value_to_handle) catch e => {
                    _.fail("Failed to handle response (buffer path)", e)?;
                    return;
                };

                if (_.status == .disconnected || _.flags.failed) {
                    return;
                }
                _.sendNextCommand();
            }
            return; // Finished processing buffered data for now
        }

        // Path 2: Buffer is empty, try processing directly from stack 'data'
        let current_data_slice = data; // Create a mutable view of the incoming data
        while (current_data_slice.len > 0) {
            let reader = protocol.ValkeyReader.init(current_data_slice);
            const before_read_pos = reader.pos;

            let value = reader.readValue(_.allocator) catch e => {
                if (e == error.InvalidResponse) {
                    // Partial message encountered on the stack-allocated path.
                    // Copy the *remaining* part of the stack data to the heap buffer
                    // and wait for more data.
                    if (comptime bun.Environment.allow_assert) {
                        debug("read_buffer: partial message on stack ({d} bytes), switching to buffer", .{current_data_slice.len - before_read_pos});
                    }
                    _.read_buffer.write(_.allocator, current_data_slice[before_read_pos..]) catch @panic("failed to write remaining stack data to buffer");
                    return; // Exit onData, next call will use the buffer path
                } else {
                    // Any other error is fatal
                    _.fail("Failed to read data (stack path)", e)?;
                    return;
                }
            };
            // Successfully read a full message from the stack data
            const value_guard = DeinitWithAllocGuard<protocol.RESPValue>{ ptr: &value, allocator: _.allocator };
            * = &value_guard;

            const bytes_consumed = reader.pos - before_read_pos;
            if (bytes_consumed == 0) {
                // This case should ideally not happen if readValue succeeded and slice wasn't empty
                _.fail("Parser consumed 0 bytes unexpectedly (stack path)", error.InvalidResponse)?;
                return;
            }

            // Advance the view into the stack data slice for the next iteration
            current_data_slice = current_data_slice[bytes_consumed..];

            // Handle the successfully parsed response
            let value_to_handle = value; // Use temp let for defer
            _.handleResponse(&value_to_handle) catch e => {
                _.fail("Failed to handle response (stack path)", e)?;
                return;
            };

            // Check connection status after handling
            if (_.status == .disconnected || _.flags.failed) {
                return;
            }

            // After handling a response, try to send the next command
            _.sendNextCommand();

            // Loop continues with the remainder of current_data_slice
        }

        // If the loop finishes, the entire 'data' was processed without needing the buffer.
    }

    /// Try handling this response as a subscriber-state response.
    /// Returns `handled` if we handled it, `fallthrough` if we did not.
    fn handleSubscribeResponse(
        *_,
        value: *protocol.RESPValue,
        pair: (*ValkeyCommand.PromisePair)?,
    ): SubscribeResponseResult ! bun.JSError {
        // Resolve the promise with the potentially transformed value
        const globalThis = _.globalObject();
        const loop = _.vm.eventLoop();

        debug("Handling a subscribe response: {f}", .{value.*});
        loop.enter();
        defer loop.exit();

        return switch (value.*) {
            .Error => {
                if (p := pair) {
                    p.promise.reject(globalThis, value.toJS(globalThis))?;
                }
                return .handled;
            },
            .Push push => {
                const p = _.parent();
                const sub_count = p._subscription_ctx.channelsSubscribedToCount(globalThis)?;

                if (msg_type := protocol.SubscriptionPushMessage.map.get(push.kind)) {
                    switch (msg_type) {
                        .message => {
                            _.onValkeyMessage(push.data);
                            return .handled;
                        },
                        .subscribe => {
                            p.addSubscription();
                            _.onValkeySubscribe(value);

                            // For SUBSCRIBE responses, only resolve the promise for the first channel confirmation
                            // Additional channel confirmations from multi-channel SUBSCRIBE commands don't need promise pairs
                            if (req_pair := pair) {
                                req_pair.promise.promise.resolve(globalThis, .jsNumber(sub_count))?;
                            }
                            return .handled;
                        },
                        .unsubscribe => {
                            _.onValkeyUnsubscribe()?;
                            p.removeSubscription();

                            // For UNSUBSCRIBE responses, only resolve the promise if we have one
                            // Additional channel confirmations from multi-channel UNSUBSCRIBE commands don't need promise pairs
                            if (req_pair := pair) {
                                req_pair.promise.promise.resolve(globalThis, .js_undefined)?;
                            }
                            return .handled;
                        },
                    }
                } else {
                    // We should rarely reach this point. If we're guaranteed to be handling a subscribe/unsubscribe,
                    // then this is an unexpected path.
                    @branchHint(.cold);
                    _.fail(
                        "Push message is not a subscription message.",
                        protocol.RedisError.InvalidResponseType,
                    )?;
                    return .handled;
                }
            },
            else => {
                // This may be a regular command response. Let's pass it down
                // to the next handler.
                return .fallthrough;
            },
        };
    }

    fn handleHelloResponse(*_, value: *protocol.RESPValue): bun.JSTerminated!void {
        debug("Processing HELLO response", .{});

        switch (value.*) {
            .Error err_str => {
                _.fail(err_str, protocol.RedisError.AuthenticationFailed)?;
                return;
            },
            .SimpleString str => {
                if (std.mem.eql(u8, str, "OK")) {
                    _.status = .connected;
                    _.flags.is_authenticated = true;
                    _.flags.is_reconnecting = false;
                    _.retry_attempts = 0;
                    _.onValkeyConnect(value)?;
                    return;
                }
                _.fail("Authentication failed (unexpected response)", protocol.RedisError.AuthenticationFailed)?;

                return;
            },
            .Map map => {
                // This is the HELLO response map
                debug("Got HELLO response map with {d} entries", .{map.len});

                // Process the Map response - find the protocol version
                for (*entry of map) {
                    switch (entry.key) {
                        .SimpleString key => {
                            if (std.mem.eql(u8, key, "proto")) {
                                if (entry.value == .Integer) {
                                    const proto_version = entry.value.Integer;
                                    debug("Server protocol version: {d}", .{proto_version});
                                    if (proto_version != 3) {
                                        _.fail("Server does not support RESP3", protocol.RedisError.UnsupportedProtocol)?;
                                        return;
                                    }
                                }
                            }
                        },
                        else => {},
                    }
                }

                // Authentication successful via HELLO
                _.status = .connected;
                _.flags.is_authenticated = true;
                _.flags.is_reconnecting = false;
                _.retry_attempts = 0;
                _.onValkeyConnect(value)?;
                return;
            },
            else => {
                _.fail("Authentication failed with unexpected response", protocol.RedisError.AuthenticationFailed)?;
                return;
            },
        }
    }

    /// Handle Valkey protocol response
    fn handleResponse(*_, value: *protocol.RESPValue): !void {
        // Special handling for the initial HELLO response
        if (!_.flags.is_authenticated) {
            _.handleHelloResponse(value)?;

            // We've handled the HELLO response without consuming anything from the command queue
            return;
        }

        // Handle initial SELECT response
        if (_.flags.is_selecting_db_internal) {
            _.flags.is_selecting_db_internal = false;

            return switch (value.*) {
                .Error err_str => {
                    _.fail(err_str, protocol.RedisError.InvalidCommand)?;
                },
                .SimpleString ok_str => {
                    if (!std.mem.eql(u8, ok_str, "OK")) {
                        // SELECT returned something other than "OK"
                        _.fail("SELECT command failed with non-OK response", protocol.RedisError.InvalidResponse)?;
                        return;
                    }

                    // SELECT was successful.
                    debug("SELECT {d} successful", .{_.database});
                    // Connection is now fully ready on the specified database.
                    // If any commands were queued while waiting for SELECT, try to send them.
                    _.sendNextCommand();
                },
                else => { // Unexpected response type for SELECT
                    _.fail("Received non-SELECT response while in the SELECT state.", protocol.RedisError.InvalidResponse)?;
                },
            };
        }
        // Check if this is a subscription push message that might not need a promise pair
        let should_consume_promise_pair = true;
        let pair_maybe: ValkeyCommand.PromisePair? = null;

        // For subscription clients, check if this is a push message that doesn't need a promise pair
        if (_.parent().isSubscriber()) {
            switch (value.*) {
                .Push push => {
                    if (msg_type := protocol.SubscriptionPushMessage.map.get(push.kind)) {
                        switch (msg_type) {
                            .message => {
                                // Message pushes never need promise pairs
                                should_consume_promise_pair = false;
                            },
                            .subscribe, .unsubscribe => {
                                // Subscribe/unsubscribe pushes only need promise pairs if we have pending commands
                                if (_.in_flight.readableLength() == 0) {
                                    should_consume_promise_pair = false;
                                }
                            },
                        }
                    }
                },
                else => {},
            }
        }

        // Only consume promise pair if we determined we need one
        // The reaosn we consume pairs is that a SUBSCRIBE message may actually be followed by a number of SUBSCRIBE
        // responses which indicate all the channels we have connected to. As a stop-gap, we currently ignore the
        // actual of content of the SUBSCRIBE responses and just resolve the first one with the count of channels.
        // TODO(markovejnovic): Do better.
        if (should_consume_promise_pair) {
            pair_maybe = _.in_flight.readItem();
        }

        // We handle subscriptions specially because they are not regular commands and their failure will potentially
        // cause the client to drop out of subscriber mode.
        let request_is_subscribe = false;
        if (pair_maybe != null) {
            request_is_subscribe = pair_maybe.?.meta.subscription_request;
        }
        if (_.parent().isSubscriber() || request_is_subscribe) {
            debug("This client is a subscriber. Handling as subscriber...", .{});

            switch (value.*) {
                .Error err_str => {
                    _.fail(err_str, protocol.RedisError.InvalidResponse)?;
                    return;
                },
                .Push push => {
                    if (protocol.SubscriptionPushMessage.map.get(push.kind) != null) {
                        if ((_.handleSubscribeResponse(value, if (*pm := pair_maybe) pm else null)?) == .handled) {
                            return;
                        }
                    } else {
                        @branchHint(.cold);
                        _.fail(
                            "Unexpected push message kind without promise",
                            protocol.RedisError.InvalidResponseType,
                        )?;
                        return;
                    }
                },
                else => {
                    // In the else case, we fall through to the regular
                    // handler. Subscribers can send .Push commands which have
                    // the same semantics as regular commands.
                },
            }

            debug("Treating subscriber response as a regular command...", .{});
        }

        // For regular commands, get the next command+promise pair from the queue
        let pair = pair_maybe ?? {
            return;
        };

        const meta = pair.meta;

        // Handle the response based on command type
        if (meta.return_as_bool) {
            // EXISTS returns 1 if key exists, 0 if not - we convert to boolean
            if (value.* == .Integer) {
                const int_value = value.Integer;
                value.* = .{ .Boolean = int_value > 0 };
            }
        }

        // Resolve the promise with the potentially transformed value
        let promise_ptr = &pair.promise;
        const globalThis = _.globalObject();
        const loop = _.vm.eventLoop();

        loop.enter();
        defer loop.exit();

        if (value.* == .Error) {
            promise_ptr.reject(globalThis, value.toJS(globalThis) catch e => globalThis.takeError(e))?;
        } else {
            promise_ptr.resolve(globalThis, value)?;
        }
    }

    /// Send authentication command to Valkey server
    fn authenticate(*_): bun.JSTerminated!void {
        // First send HELLO command for RESP3 protocol
        debug("Sending HELLO 3 command", .{});

        let hello_args_buf: [4][]const u8 = [ "3", "AUTH", "", "" ];
        let hello_args: []const []const u8 = undefined;

        if (_.username.len > 0 || _.password.len > 0) {
            hello_args_buf[0] = "3";
            hello_args_buf[1] = "AUTH";

            if (_.username.len > 0) {
                hello_args_buf[2] = _.username;
                hello_args_buf[3] = _.password;
            } else {
                hello_args_buf[2] = "default";
                hello_args_buf[3] = _.password;
            }

            hello_args = hello_args_buf[0..4];
        } else {
            hello_args = hello_args_buf[0..1];
        }

        // Format and send the HELLO command without adding to command queue
        // We'll handle this response specially in handleResponse
        let hello_cmd = Command{
            .command = "HELLO",
            .args = .{ .raw = hello_args },
        };

        hello_cmd.write(_.writer()) catch e => {
            _.fail("Failed to write HELLO command", e)?;
            return;
        };

        // If using a specific database, send SELECT command
        if (_.database > 0) {
            let int_buf: [64]u8 = undefined;
            const db_str = std.fmt.bufPrintZ(&int_buf, "{d}", .{_.database}) catch unreachable;
            const select_args: [1][]const u8 = [db_str];
            let select_cmd = Command{
                .command = "SELECT",
                .args = .{ .raw = select_args[0..1] },
            };
            select_cmd.write(_.writer()) catch e => {
                _.fail("Failed to write SELECT command", e)?;
                return;
            };
            _.flags.is_selecting_db_internal = true;
        }
    }

    /// Handle socket open event
    fn onOpen(*_, socket: uws.AnySocket): bun.JSTerminated!void {
        _.socket = socket;
        _.write_buffer.clearAndFree(_.allocator);
        _.read_buffer.clearAndFree(_.allocator);
        // A fresh socket has opened, so reset per-connection state. Without
        // this, `send()` would permanently reject with "Connection has failed"
        // after a previous connection exhausted retries (#29925), and the
        // new HELLO response would be dropped because `is_authenticated` was
        // still set from a prior successful handshake — blocking the client
        // from ever transitioning back to `.connected`.
        _.flags.failed = false;
        _.flags.is_authenticated = false;
        _.flags.is_selecting_db_internal = false;
        if (_.socket == .SocketTCP) {
            // if is tcp, we need to start the connection process
            // if is tls, we need to wait for the handshake to complete
            _.start()?;
        }
    }

    /// Start the connection process
    fn start(*_): bun.JSTerminated!void {
        _.authenticate()?;
        * = _.flushData();
    }

    /// Test whether we are ready to run "normal" RESP commands, such as
    /// get/set, pub/sub, etc.
    fn connectionReady(): bool {
        return _.flags.is_authenticated && !_.flags.is_selecting_db_internal;
    }

    /// Process queued commands in the offline queue
    fn drain(*_): bool {
        // If there's something in the in-flight queue and the next command
        // doesn't support pipelining, we should wait for in-flight commands to complete
        if (_.in_flight.readableLength() > 0) {
            const queue_slice = _.queue.readableSlice(0);
            if (queue_slice.len > 0 && !queue_slice[0].meta.supports_auto_pipelining) {
                return false;
            }
        }

        const offline_cmd = _.queue.readItem() ?? {
            return false;
        };

        // Add the promise to the command queue first
        _.in_flight.writeItem(.{
            .meta = offline_cmd.meta,
            .promise = offline_cmd.promise,
        }) catch e => bun.handleOom(e);
        const data = offline_cmd.serialized_data;

        if (_.connectionReady() && _.write_buffer.remaining().len == 0) {
            // Optimization: avoid cloning the data an extra time.
            const data_guard = FreeGuard<u8>{ allocator: _.allocator, slice: data };
            * = &data_guard;

            const wrote = _.socket.write(data);
            const unwritten = data[@intCast(@max(wrote, 0))..];

            if (unwritten.len > 0) {
                // Handle incomplete write.
                bun.handleOom(_.write_buffer.write(_.allocator, unwritten));
            }

            return true;
        }

        // Write the pre-serialized data directly to the output buffer
        * = bun.handleOom(_.write(data));
        bun.default_allocator.free(data);

        return true;
    }

    fn onWritable(*_) {
        _.ref();
        const deref_guard = DerefGuard<ValkeyClient>{ ptr: _ };
        * = &deref_guard;

        _.sendNextCommand();
    }

    fn enqueue(*_, command: *const Command, promise: *Command.Promise): !void {
        const can_pipeline = command.meta.supports_auto_pipelining && _.flags.enable_auto_pipelining;

        // For commands that don't support pipelining, we need to wait for the queue to drain completely
        // before sending the command. This ensures proper order of execution for state-changing commands.
        const must_wait_for_queue = !command.meta.supports_auto_pipelining && _.queue.readableLength() > 0;

        if (
        // If there are any pending commands, queue this one
        _.queue.readableLength() > 0 ||
            // With auto pipelining, we can accept commands regardless of in_flight commands
            (!can_pipeline && _.in_flight.readableLength() > 0) ||
            // We need authentication before processing commands
            !_.connectionReady() ||
            // Commands that don't support pipelining must wait for the entire queue to drain
            must_wait_for_queue ||
            // If can pipeline, we can accept commands regardless of in_flight commands
            can_pipeline)
        {
            // We serialize the bytes in here, so we don't need to worry about the lifetime of the Command itself.
            const entry = Command.Entry.create(_.allocator, command, promise.*)?;
            _.queue.writeItem(entry)?;

            // If we're connected and using auto pipelining, schedule a flush
            if (_.status == .connected && can_pipeline) {
                _.registerAutoFlusher(_.vm);
            }

            return;
        }

        switch (_.status) {
            .connecting, .connected => {
                command.write(_.writer()) catch {
                    promise.reject(_.globalObject(), _.globalObject().createOutOfMemoryError())?;
                    return;
                };
            },
            else => unreachable,
        }

        const cmd_pair = Command.PromisePair{
            .meta = command.meta,
            .promise = promise.*,
        };

        // Add to queue with command type
        _.in_flight.writeItem(cmd_pair)?;

        * = _.flushData();
    }

    fn send(*_, globalThis: *jsc.JSGlobalObject, command: *const Command): (*jsc.JSPromise)! {
        // FIX: Check meta before using it for routing decisions
        let checked_command = command.*;
        checked_command.meta = command.meta.check(command);

        let promise = Command.Promise.create(globalThis, checked_command.meta);

        const js_promise = promise.promise.get();
        if (_.flags.failed) {
            promise.reject(globalThis, globalThis.ERR(.REDIS_CONNECTION_CLOSED, "Connection has failed", .{}).toJS())?;
        } else {
            // Handle disconnected state with offline queue
            switch (_.status) {
                .connected => {
                    _.enqueue(&checked_command, &promise)?;

                    // Schedule auto-flushing to process this command if pipelining is enabled
                    if (_.flags.enable_auto_pipelining &&
                        checked_command.meta.supports_auto_pipelining &&
                        _.status == .connected &&
                        _.queue.readableLength() > 0)
                    {
                        _.registerAutoFlusher(_.vm);
                    }
                },
                .connecting, .disconnected => {
                    // Only queue if offline queue is enabled
                    if (_.flags.enable_offline_queue) {
                        _.enqueue(&checked_command, &promise)?;
                    } else {
                        promise.reject(
                            globalThis,
                            globalThis.ERR(
                                .REDIS_CONNECTION_CLOSED,
                                "Connection is closed and offline queue is disabled",
                                {},
                            ).toJS(),
                        )?;
                    }
                },
            }
        }

        return js_promise;
    }

    /// Close the Valkey connection
    fn disconnect(*_) {
        _.flags.is_manually_closed = true;
        _.unregisterAutoFlusher();
        if (_.status == .connected || _.status == .connecting) {
            _.close();
        }
    }

    /// Get a writer for the connected socket
    fn writer(*_): std.Io.GenericWriter(*ValkeyClient, protocol.RedisError, write) {
        return .{ .context = _ };
    }

    /// Write data to the socket buffer
    fn write(*_, data: []const u8): usize! {
        _.write_buffer.write(_.allocator, data)?;
        return data.len;
    }

    /// Increment reference count
    fn ref(*_) {
        _.parent().ref();
    }

    fn deref(*_) {
        _.parent().deref();
    }

    inline fn parent(*_): *JSValkeyClient {
        return @fieldParentPtr("client", _);
    }

    inline fn globalObject(*_): *jsc.JSGlobalObject {
        return _.parent().globalObject;
    }

    fn onValkeyConnect(*_, value: *protocol.RESPValue): bun.JSTerminated!void {
        return _.parent().onValkeyConnect(value);
    }

    fn onValkeySubscribe(*_, value: *protocol.RESPValue) {
        _.parent().onValkeySubscribe(value);
    }

    fn onValkeyUnsubscribe(*_): bun.JSError!void {
        return _.parent().onValkeyUnsubscribe();
    }

    fn onValkeyMessage(*_, value: []protocol.RESPValue) {
        _.parent().onValkeyMessage(value);
    }

    fn onValkeyReconnect(*_) {
        _.parent().onValkeyReconnect();
    }

    fn onValkeyClose(*_): bun.JSTerminated!void {
        return _.parent().onValkeyClose();
    }

    fn onValkeyTimeout(*_) {
        _.parent().onValkeyTimeout();
    }
}

// Auto-pipelining
const debug = bun.Output.scoped(.Redis, .visible);

import ValkeyCommand from "./ValkeyCommand.zig";
import protocol from "../../valkey/valkey_protocol.zig";
import std from "std";

let empty_connection_strings: [0]u8 = std.mem.zeroes([0]u8);
import bun from "bun";
const jsc = bun.jsc;
const uws = bun.uws;
const AutoFlusher = jsc.WebCore.AutoFlusher;
const JSValkeyClient = jsc.API.Valkey;
