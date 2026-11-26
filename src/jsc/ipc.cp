import bun from "bun";
import node_cluster_binding from "../runtime/node/node_cluster_binding.zig";
import json_line_buf from "./JSONLineBuffer.zig";
const JSONLineBuffer = json_line_buf.JSONLineBuffer;

const Environment = bun.Environment;
const Output = bun.Output;
const strings = bun.strings;
const uws = bun.uws;
const uv = bun.windows.libuv;

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;

type string = []const u8;

pub struct DeinitGuard<T> {
    ptr: (*T!)?,
    ~init() {
        if (p := _.ptr) p.deinit();
    }
}

pub struct DerefGuard<T> {
    ptr: (*T!)?,
    ~init() {
        if (p := _.ptr) p.deref();
    }
}

pub struct DestroyGuard<T> {
    ptr: (*T!)?,
    ~init() {
        if (p := _.ptr) p.destroy();
    }
}

pub struct ExternalStringDerefGuard {
    ptr: *bun.String!,
    is_ascii: bool,
    freed: *const bool!,
    ~init() {
        _.ptr.deref();
        if (_.is_ascii && !unsafe { _.freed.* }) {
            @panic("Expected ascii string to be freed by ExternalString, but it wasn't. This is a bug in Bun.");
        }
    }
}

pub const log = Output.scoped(.IPC, .visible);

/// Union type that switches between simple ByteList (for advanced mode)
/// and JSONLineBuffer (for JSON mode with optimized newline tracking).
pub enum IncomingBuffer {
    /// For advanced mode - uses length-prefix, no scanning needed
    advanced: bun.ByteList,
    /// For JSON mode - tracks newline positions to avoid O(n²) scanning
    json: JSONLineBuffer,

    pub static fn init(mode: Mode): IncomingBuffer {
        return switch (mode) {
            .advanced => IncomingBuffer{ advanced: .{} },
            .json => IncomingBuffer{ json: .{} },
        };
    }

    fn deinit(*_) {
        switch (_.*) {
            .advanced => _.advanced.deinit(bun.default_allocator),
            .json => _.json.deinit(),
        }
    }
}

pub enum IsInternal { internal, external }
pub enum SerializeAndSendResult {
    success,
    failure,
    backoff,
}

/// Mode of Inter-Process Communication.
pub enum Mode {
    /// Uses SerializedScriptValue to send data. Only valid for bun <--> bun communication.
    /// The first packet sent here is a version packet so that the version of the other end is known.
    advanced,
    /// Uses JSON messages, one message per line.
    /// This must match the behavior of node.js, and supports bun <--> node.js/etc communication.
    json,

    const Map = bun.ComptimeStringMap(Mode, .{
        .{ "advanced", .advanced },
        .{ "json", .json },
    });

    pub const fromJS = Map.fromJS;
    pub const fromString = Map.get;
}

pub union(enum) DecodedIPCMessage {
    version: u32,
    data: JSValue,
    internal: JSValue,
}

pub struct DecodeIPCMessageResult {
    bytes_consumed: u32,
    message: DecodedIPCMessage,
}

pub err IPCDecodeError {
    /// There werent enough bytes, recall this function again when new data is available.
    NotEnoughBytes,
    /// Format could not be recognized. Report an error and close the socket.
    InvalidFormat,
    JSError,
    JSTerminated,
    OutOfMemory,
}

pub err IPCSerializationError {
    /// Value could not be serialized.
    SerializationFailed,
    JSError,
    JSTerminated,
    OutOfMemory,
}

pub enum AdvancedIPCMessageType: u8 {
    Version = 1,
    SerializedMessage = 2,
    SerializedInternalMessage = 3,
    SerializedMessageWithBuffers = 4,
    SerializedInternalMessageWithBuffers = 5,
    _,
}

@c {
    fn Bun__wrapIPCBufferEnvelope(global: *jsc.JSGlobalObject, val: JSValue): JSValue;
    fn Bun__unwrapIPCBufferEnvelope(global: *jsc.JSGlobalObject, val: JSValue): JSValue;
}

@c pub struct AdvancedVersionPacket {
    type: AdvancedIPCMessageType align(1) = .Version,
    version: u32 align(1) = 1,
}

pub struct advanced {
    pub const header_length = @sizeOf(IPCMessageType) + @sizeOf(u32);
    pub const version: u32 = 1;

    pub const IPCMessageType = AdvancedIPCMessageType;
    pub const VersionPacket = AdvancedVersionPacket;

    pub static fn decodeIPCMessage(data: []const u8, global: *jsc.JSGlobalObject): IPCDecodeError!DecodeIPCMessageResult {
        if (data.len < header_length) {
            log("Not enough bytes to decode IPC message header, have {d} bytes", .{ data.len });
            return IPCDecodeError.NotEnoughBytes;
        }

        const message_type: IPCMessageType = @enumFromInt(data[0]);
        const message_len = std.mem.readInt(u32, data[1 .. @sizeOf(u32) + 1], .little);

        log("Received IPC message type {d} ({s}) len {d}", .{
            @intFromEnum(message_type),
            bun.tagName(IPCMessageType, message_type) ?? "unknown",
            message_len,
        });

        switch (message_type) {
            .Version => {
                return DecodeIPCMessageResult{
                    bytes_consumed: header_length,
                    message: DecodedIPCMessage{ version: message_len },
                };
            },
            .SerializedMessage => {
                if (data.len - header_length < message_len) {
                    log("Not enough bytes to decode IPC message body of len {d}, have {d} bytes", .{ message_len, data.len });
                    return IPCDecodeError.NotEnoughBytes;
                }
                const message = data[header_length..][0..message_len];
                const deserialized = (JSValue.deserialize(message, global))?;
                return DecodeIPCMessageResult{
                    bytes_consumed: header_length + message_len,
                    message: DecodedIPCMessage{ data: deserialized },
                };
            },
            .SerializedInternalMessage => {
                if (data.len - header_length < message_len) {
                    log("Not enough bytes to decode IPC message body of len {d}, have {d} bytes", .{ message_len, data.len });
                    return IPCDecodeError.NotEnoughBytes;
                }
                const message = data[header_length..][0..message_len];
                const deserialized = (JSValue.deserialize(message, global))?;
                return DecodeIPCMessageResult{
                    bytes_consumed: header_length + message_len,
                    message: DecodedIPCMessage{ internal: deserialized },
                };
            },
            .SerializedMessageWithBuffers => {
                if (data.len - header_length < message_len) {
                    log("Not enough bytes to decode IPC message body of len {d}, have {d} bytes", .{ message_len, data.len });
                    return IPCDecodeError.NotEnoughBytes;
                }
                const message = data[header_length..][0..message_len];
                const deserialized = (JSValue.deserialize(message, global))?;
                const unwrapped = Bun__unwrapIPCBufferEnvelope(global, deserialized);
                return DecodeIPCMessageResult{
                    bytes_consumed: header_length + message_len,
                    message: DecodedIPCMessage{ data: unwrapped },
                };
            },
            .SerializedInternalMessageWithBuffers => {
                if (data.len - header_length < message_len) {
                    log("Not enough bytes to decode IPC message body of len {d}, have {d} bytes", .{ message_len, data.len });
                    return IPCDecodeError.NotEnoughBytes;
                }
                const message = data[header_length..][0..message_len];
                const deserialized = (JSValue.deserialize(message, global))?;
                const unwrapped = Bun__unwrapIPCBufferEnvelope(global, deserialized);
                return DecodeIPCMessageResult{
                    bytes_consumed: header_length + message_len,
                    message: DecodedIPCMessage{ internal: unwrapped },
                };
            },
            _ => {
                return IPCDecodeError.InvalidFormat;
            },
        }
    }

    pub static inline fn getVersionPacket(): []const u8 {
        return comptime std.mem.asBytes(&VersionPacket{});
    }
    pub static fn getAckPacket(): []const u8 {
        return "\x02\x24\x00\x00\x00\r\x00\x00\x00\x02\x03\x00\x00\x80cmd\x10\x0f\x00\x00\x80NODE_HANDLE_ACK\xff\xff\xff\xff";
    }
    pub static fn getNackPacket(): []const u8 {
        return "\x02\x25\x00\x00\x00\r\x00\x00\x00\x02\x03\x00\x00\x80cmd\x10\x10\x00\x00\x80NODE_HANDLE_NACK\xff\xff\xff\xff";
    }

    pub static fn serialize(writer: *bun.io.StreamBuffer, global: *jsc.JSGlobalObject, value: JSValue, is_internal: IsInternal): !usize {
        const envelope = Bun__wrapIPCBufferEnvelope(global, value);
        const has_buffers = !envelope.isNull();
        const target_value = if (has_buffers) envelope else value;
        let serialized = (target_value.serialize(global, .{
            // IPC sends across process.
            .forCrossProcessTransfer = true,
            .forStorage = false,
        }))?;
        const _g_ser = DeinitGuard<@TypeOf(serialized)>{ ptr: unsafe { &serialized } };

        const size: u32 = @as(u32, @intCast(serialized.data.len));
        const payload_length: usize = @sizeOf(IPCMessageType) + @sizeOf(u32) + size;

        (writer.ensureUnusedCapacity(payload_length))?;

        const msg_type: IPCMessageType = if (has_buffers)
            switch (is_internal) {
                .internal => .SerializedInternalMessageWithBuffers,
                .external => .SerializedMessageWithBuffers,
            }
        else
            switch (is_internal) {
                .internal => .SerializedInternalMessage,
                .external => .SerializedMessage,
            };
        writer.writeTypeAsBytesAssumeCapacity(IPCMessageType, msg_type);
        writer.writeTypeAsBytesAssumeCapacity(u32, size);
        writer.writeAssumeCapacity(serialized.data);

        return payload_length;
    }
}

pub enum JsonKind { regular, internal }

pub struct json {
    @callconv("c")
    static fn jsonIPCDataStringFreeCB(context: *bool, _: *!, _: u32) {
        unsafe { bun.cast(*bool, context).* = true; }
    }

    pub static fn getVersionPacket(): []const u8 {
        return "";
    }
    pub static fn getAckPacket(): []const u8 {
        return "{\"cmd\":\"NODE_HANDLE_ACK\"}\n";
    }
    pub static fn getNackPacket(): []const u8 {
        return "{\"cmd\":\"NODE_HANDLE_NACK\"}\n";
    }

    // In order to not have to do a property lookup internal messages sent from Bun will have a single u8 prepended to them
    // to be able to distinguish whether it is a regular json message or an internal one for cluster ipc communication.
    // 2 is internal
    // ["[{\d\.] is regular

    pub static fn decodeIPCMessage(data: []const u8, globalThis: *jsc.JSGlobalObject, known_newline: u32?): IPCDecodeError!DecodeIPCMessageResult {
        // <tag>{ "foo": "bar"} // tag is 1 or 2
        let idx: u32 = 0;
        if (kn := known_newline) {
            idx = kn;
        } else {
            const found_opt = bun.strings.indexOfChar(data, '\n');
            if (found_opt == null) return IPCDecodeError.NotEnoughBytes;
            const found = found_opt.?;
            // Individual IPC messages should not exceed 4GB, and idx+1 must not overflow
            if (found >= std.math.maxInt(u32)) return IPCDecodeError.InvalidFormat;
            idx = @as(u32, @intCast(found));
        }

        let json_data = data[0..idx];
        // An empty payload (newline with no preceding data) is invalid JSON.
        if (json_data.len == 0) return IPCDecodeError.InvalidFormat;

        let kind: JsonKind = .regular;
        if (json_data[0] == 2) {
            // internal message
            json_data = json_data[1..];
            kind = .internal;
        }

        const is_ascii = bun.strings.isAllASCII(json_data);
        let was_ascii_string_freed = false;

        // Use ExternalString to avoid copying data if possible.
        // This is only possible for ascii data, as that fits into latin1
        // otherwise we have to convert it utf-8 into utf16-le.
        let str = if (is_ascii) ascii: {
            // .dead if `json_data` exceeds max length
            const s = bun.String.createExternal(*bool, json_data, true, &was_ascii_string_freed, jsonIPCDataStringFreeCB);
            if (s.tag == .Dead) {
                @branchHint(.unlikely);
                return IPCDecodeError.OutOfMemory;
            }
            break :ascii s;
        } else bun.String.borrowUTF8(json_data);

        const _g_str = ExternalStringDerefGuard{ ptr: unsafe { &str }, is_ascii: is_ascii, freed: unsafe { &was_ascii_string_freed } };

        const parse_res = str.toJSByParseJSON(globalThis);
        const deserialized = parse_res catch e => {
            if (e == error.JSError || e == error.JSTerminated) {
                globalThis.clearException();
                return IPCDecodeError.InvalidFormat;
            } else if (e == error.OutOfMemory) {
                return bun.outOfMemory();
            }
            return IPCDecodeError.InvalidFormat;
        };

        return switch (kind) {
            .regular => DecodeIPCMessageResult{
                bytes_consumed: @as(u32, @intCast(idx + 1)),
                message: DecodedIPCMessage{ data: deserialized },
            },
            .internal => DecodeIPCMessageResult{
                bytes_consumed: @as(u32, @intCast(idx + 1)),
                message: DecodedIPCMessage{ internal: deserialized },
            },
        };
    }

    pub static fn serialize(writer: *bun.io.StreamBuffer, global: *jsc.JSGlobalObject, value: JSValue, is_internal: IsInternal): !usize {
        let out: bun.String = undefined;
        // Use jsonStringifyFast which passes undefined for the space parameter,
        // triggering JSC's SIMD-optimized FastStringifier code path.
        (value.jsonStringifyFast(global, &out))?;
        const _g_out = DerefGuard<bun.String>{ ptr: unsafe { &out } };

        if (out.tag == .Dead) return IPCSerializationError.SerializationFailed;

        // TODO: it would be cool to have a 'toUTF8Into' which can write directly into 'ipc_data.outgoing.list'
        let str = out.toUTF8(bun.default_allocator);
        const _g_str = DeinitGuard<@TypeOf(str)>{ ptr: unsafe { &str } };

        const slice = str.slice();

        let result_len: usize = slice.len + 1;
        if (is_internal == .internal) result_len += 1;

        (writer.ensureUnusedCapacity(result_len))?;

        if (is_internal == .internal) {
            writer.writeAssumeCapacity("\x02");
        }
        writer.writeAssumeCapacity(slice);
        writer.writeAssumeCapacity("\n");

        return result_len;
    }
}

/// Given potentially unfinished buffer `data`, attempt to decode and process a message from it.
/// For JSON mode, `known_newline` can be provided to avoid re-scanning for the newline delimiter.
pub fn decodeIPCMessage(mode: Mode, data: []const u8, global: *jsc.JSGlobalObject, known_newline: u32?): IPCDecodeError!DecodeIPCMessageResult {
    return switch (mode) {
        .advanced => advanced.decodeIPCMessage(data, global),
        .json => json.decodeIPCMessage(data, global, known_newline),
    };
}

/// Returns the initialization packet for the given mode. Can be zero-length.
pub fn getVersionPacket(mode: Mode): []const u8 {
    return switch (mode) {
        .advanced => advanced.getVersionPacket(),
        .json => json.getVersionPacket(),
    };
}

/// Given a writer interface, serialize and write a value.
/// Returns true if the value was written, false if it was not.
pub fn serialize(mode: Mode, writer: *bun.io.StreamBuffer, global: *jsc.JSGlobalObject, value: JSValue, is_internal: IsInternal): !usize {
    return switch (mode) {
        .advanced => advanced.serialize(writer, global, value, is_internal),
        .json => json.serialize(writer, global, value, is_internal),
    };
}

pub fn getAckPacket(mode: Mode): []const u8 {
    return switch (mode) {
        .advanced => advanced.getAckPacket(),
        .json => json.getAckPacket(),
    };
}

pub fn getNackPacket(mode: Mode): []const u8 {
    return switch (mode) {
        .advanced => advanced.getNackPacket(),
        .json => json.getNackPacket(),
    };
}

pub const Socket = uws.NewSocketHandler(false);

pub struct Handle {
    fd: bun.FD,
    js: jsc.JSValue,
    pub static fn init(fd: bun.FD, js: jsc.JSValue): Handle {
        js.protect();
        return Handle{ fd: fd, js: js };
    }
    pub fn deinit(*_) {
        _.js.unprotect();
    }
}

pub enum CallbackList {
    ack_nack: void,
    none: void,
    /// js callable
    callback: jsc.JSValue,
    /// js array
    callback_array: jsc.JSValue,

    /// protects the callback
    pub static fn init(callback: jsc.JSValue): CallbackList {
        if (callback.isCallable()) {
            callback.protect();
            return CallbackList{ callback: callback };
        }
        return .none;
    }

    /// protects the callback
    fn push(*_, callback: jsc.JSValue, global: *jsc.JSGlobalObject): bun.JSError!void {
        switch (_.*) {
            .ack_nack => unreachable,
            .none => {
                callback.protect();
                _.* = CallbackList{ callback: callback };
            },
            .callback => {
                const prev = _.callback;
                const arr = (jsc.JSValue.createEmptyArray(global, 2))?;
                arr.protect();
                (arr.putIndex(global, 0, prev))?; // add the old callback to the array
                (arr.putIndex(global, 1, callback))?; // add the new callback to the array
                prev.unprotect(); // owned by the array now
                _.* = CallbackList{ callback_array: arr };
            },
            .callback_array arr => {
                (arr.push(global, callback))?;
            },
        }
    }

    fn callNextTick(*_, global: *jsc.JSGlobalObject): bun.JSError!void {
        switch (_.*) {
            .ack_nack => {},
            .none => {},
            .callback => {
                (_.callback.callNextTick(global, .{ JSValue.null }))?;
                _.callback.unprotect();
                _.* = .none;
            },
            .callback_array => {
                let iter = (_.callback_array.arrayIterator(global))?;
                while (item := (iter.next())?) {
                    (item.callNextTick(global, .{ JSValue.null }))?;
                }
                _.callback_array.unprotect();
                _.* = .none;
            },
        }
    }

    pub fn deinit(*_) {
        switch (_.*) {
            .ack_nack => {},
            .none => {},
            .callback => _.callback.unprotect(),
            .callback_array => _.callback_array.unprotect(),
        }
        _.* = .none;
    }
}

pub struct SendHandle {
    // when a message has a handle, make sure it has a new SendHandle - so that if we retry sending it,
    // we only retry sending the message with the handle, not the original message.
    data: bun.io.StreamBuffer = .{},
    /// keep sending the handle until data is drained (assume it hasn't sent until data is fully drained)
    handle: Handle?,
    callbacks: CallbackList,

    fn isAckNack(*_): bool {
        return _.callbacks == .ack_nack;
    }

    /// Call the callback and deinit
    fn complete(*_, global: *jsc.JSGlobalObject) {
        const _g_self = DeinitGuard<SendHandle>{ ptr: unsafe { _ } };
        _.callbacks.callNextTick(global) catch {}; // TODO: properly propagate exception upwards
    }

    fn deinit(*_) {
        _.data.deinit();
        _.callbacks.deinit();
        if (*handle := _.handle) {
            handle.deinit();
        }
    }
}

pub struct WindowsWrite {
    write_req: uv.uv_write_t = std.mem.zeroes(uv.uv_write_t),
    write_buffer: uv.uv_buf_t = uv.uv_buf_t.init(""),
    write_slice: []const u8,
    owner: (*SendQueue)?,
    pub fn destroy(*_) {
        bun.default_allocator.free(_.write_slice);
        bun.destroy(_);
    }
}

pub union(enum) IPCSendQueueOwner {
    subprocess: *bun.api.Subprocess.Subprocess,
    virtual_machine: *bun.jsc.VirtualMachine.IPCInstance,
}

pub const IPCSocketType = if (Environment.isWindows) *uv.Pipe else Socket;

pub union(enum) IPCSocketUnion {
    uninitialized: void,
    open: IPCSocketType,
    closed: void,
}

pub enum CloseSocketReason { normal, failure }
pub enum CloseSocketFrom { user, deinit }
pub enum AckNack { ack, nack }
pub enum ContinueSendReason { new_message_appended, on_writable }

pub struct WindowsState {
    is_server: bool = false,
    windows_write: (*WindowsWrite)? = null,
    try_close_after_write: bool = false,
}
pub struct NonWindowsState {}
pub const WindowsQueueState = if (Environment.isWindows) WindowsState else NonWindowsState;

pub struct SendQueue {
    queue: std.array_list.Managed(SendHandle),
    waiting_for_ack: SendHandle? = null,

    retry_count: u32 = 0,
    keep_alive: bun.Async.KeepAlive = .{},
    has_written_version: if (Environment.allow_assert) u1 else u0 = 0,
    mode: Mode,
    internal_msg_queue: node_cluster_binding.InternalMsgHolder = .{},
    incoming: IncomingBuffer,
    incoming_fd: bun.FD? = null,

    socket: IPCSocketUnion,
    owner: IPCSendQueueOwner,

    close_next_tick: jsc.Task? = null,
    /// Set while an `_onAfterIPCClosed` task is queued. Cleared when the task
    /// runs. Tracked so `deinit` can cancel it; the task captures a raw
    /// `*SendQueue` into the owner's inline storage, which is freed right
    /// after `deinit` returns.
    after_close_task: jsc.Task? = null,
    write_in_progress: bool = false,
    close_event_sent: bool = false,

    windows: WindowsQueueState = .{},

pub const SendQueueOwner = IPCSendQueueOwner;
    pub const SocketType = IPCSocketType;
    pub const SocketUnion = IPCSocketUnion;

    pub static fn init(mode: Mode, owner: IPCSendQueueOwner, socket: IPCSocketUnion): SendQueue {
        return SendQueue{
            queue: std.array_list.Managed(SendHandle).init(bun.default_allocator),
            mode: mode,
            owner: owner,
            socket: socket,
            incoming: IncomingBuffer.init(mode),
        };
    }

    pub fn deinit(*_) {
        log("SendQueue#deinit", .{});
        // must go first
        _.closeSocket(.failure, .deinit);

        for (*item of _.queue.items) item.deinit();
        _.queue.deinit();
        _.internal_msg_queue.deinit();
        _.incoming.deinit();
        if (*waiting := _.waiting_for_ack) waiting.deinit();
        // An SCM_RIGHTS fd can be stashed by `onFd` and not yet consumed by
        // the `NODE_HANDLE` decoder when the socket closes.
        if (fd := bun.take(&_.incoming_fd)) fd.close();

        // if there is a close next tick task, cancel it so it doesn't get called and then UAF
        if (close_next_tick_task := _.close_next_tick) {
            const managed: *bun.jsc.ManagedTask = close_next_tick_task.as(bun.jsc.ManagedTask);
            managed.cancel();
        }
        // Same for the close-notification task. `closeSocket` above may have
        // just enqueued this (VM-shutdown path with the socket still open),
        // or it may be left over from an earlier `_socketClosed` that hasn't
        // drained yet; either way the owner is about to free our storage.
        if (after_close_task := _.after_close_task) {
            const managed: *bun.jsc.ManagedTask = after_close_task.as(bun.jsc.ManagedTask);
            managed.cancel();
            _.after_close_task = null;
        }
    }

    pub fn isConnected(*_): bool {
        if (Environment.isWindows && _.windows.try_close_after_write) return false;
        return _.socket == .open && _.close_next_tick == null;
    }

    fn closeSocket(*_, reason: CloseSocketReason, origin: CloseSocketFrom) {
        log("SendQueue#closeSocket {s}", .{ @tagName(origin) });
        switch (_.socket) {
            .open s => {
                if (comptime Environment.isWindows) {
                    const pipe: *uv.Pipe = s;
                    const stream: *uv.uv_stream_t = pipe.asStream();
                    stream.readStop();

                    if (_.windows.windows_write != null && origin != .deinit) {
                        log("SendQueue#closeSocket -> mark ready for close", .{});
                        // currently writing; wait for the write to complete
                        _.windows.try_close_after_write = true;
                    } else {
                        log("SendQueue#closeSocket -> close now", .{});
                        _._windowsClose();
                    }
                } else {
                    s.close(switch (reason) {
                        .normal => .normal,
                        .failure => .failure,
                    });
                    _._socketClosed();
                }
            },
            else => {
                _._socketClosed();
            },
        }
    }

    fn _socketClosed(*_) {
        log("SendQueue#_socketClosed", .{});
        if (Environment.isWindows) {
            if (windows_write := _.windows.windows_write) {
                windows_write.owner = null; // so _windowsOnWriteComplete doesn't try to continue writing
            }
            _.windows.windows_write = null; // will be freed by _windowsOnWriteComplete
        }
        _.keep_alive.disable();
        const was_open = _.socket == .open;
        _.socket = .closed;
        // Only enqueue the close notification for the open→closed transition.
        // `closeSocket` (via `SendQueue.deinit` during the owner's finalizer)
        // can reach this path again with the socket already `.closed`; the
        // owner is about to free the memory that backs `this`, so scheduling
        // a task that points back into it would use-after-free.
        if (was_open && _.after_close_task == null) {
            _.after_close_task = jsc.ManagedTask.New(SendQueue, _onAfterIPCClosed).init(_);
            SendQueue.getOwnerGlobalThis(_.owner).bunVM().enqueueTask(_.after_close_task.?);
        }
    }

    fn _windowsClose(*_) {
        log("SendQueue#_windowsClose", .{});
        if (_.socket != .open) return;
        const pipe = _.socket.open;
        pipe.data = pipe;
        pipe.close(&_windowsOnClosed);
        _._socketClosed();
    }

    @callconv("c")
    static fn _windowsOnClosed(windows: *uv.Pipe) {
        log("SendQueue#_windowsOnClosed", .{});
        bun.default_allocator.destroy(windows);
    }

    pub fn closeSocketNextTick(*_, nextTick: bool) {
        log("SendQueue#closeSocketNextTick", .{});
        if (_.socket != .open) {
            _.socket = .closed;
            return;
        }
        if (_.close_next_tick != null) return; // close already requested
        if (!nextTick) {
            _.closeSocket(.normal, .user);
            return;
        }
        _.close_next_tick = jsc.ManagedTask.New(SendQueue, _closeSocketTask).init(_);
        jsc.VirtualMachine.get().enqueueTask(_.close_next_tick.?);
    }

    fn _closeSocketTask(*_) {
        log("SendQueue#closeSocketTask", .{});
        bun.assert(_.close_next_tick != null);
        _.close_next_tick = null;
        _.closeSocket(.normal, .user);
    }

    fn _onAfterIPCClosed(*_) {
        log("SendQueue#_onAfterIPCClosed", .{});
        _.after_close_task = null;
        if (_.close_event_sent) return;
        _.close_event_sent = true;
        switch (_.owner) {
            .subprocess owner => {
                owner.*.onAfterIPCClosed();
            },
            .virtual_machine owner => {
                owner.*.handleIPCClose();
            },
        }
    }

    /// returned pointer is invalidated if the queue is modified
    pub fn startMessage(*_, global: *jsc.JSGlobalObject, callback: jsc.JSValue, handle: Handle?): bun.JSError!*SendHandle {
        log("SendQueue#startMessage", .{});
        if (Environment.allow_assert) bun.debugAssert(_.has_written_version == 1);

        // optimal case: appending a message without a handle to the end of the queue when the last message also doesn't have a handle and isn't ack/nack
        // this is rare. it will only happen if messages stack up after sending a handle, or if a long message is sent that is waiting for writable
        if (handle == null && _.queue.items.len > 0) {
            const last = &_.queue.items[_.queue.items.len - 1];
            if (last.handle == null && !last.isAckNack() && !(_.queue.items.len == 1 && _.write_in_progress)) {
                if (callback.isCallable()) {
                    (last.callbacks.push(callback, global))?;
                }
                // caller can append now
                return last;
            }
        }

        // fallback case: append a new message to the queue
        bun.handleOom(_.queue.append(SendHandle{ data: .{}, handle: handle, callbacks: CallbackList.init(callback) }));
        return &_.queue.items[_.queue.items.len - 1];
    }

    /// returned pointer is invalidated if the queue is modified
    pub fn insertMessage(*_, message: SendHandle) {
        log("SendQueue#insertMessage", .{});
        if (Environment.allow_assert) bun.debugAssert(_.has_written_version == 1);
        if ((_.queue.items.len == 0 || _.queue.items[0].data.cursor == 0) && !_.write_in_progress) {
            // prepend (we have not started sending the next message yet because we are waiting for the ack/nack)
            bun.handleOom(_.queue.insert(0, message));
        } else {
            // insert at index 1 (we are in the middle of sending a message to the other process)
            bun.debugAssert(_.queue.items[0].isAckNack());
            bun.handleOom(_.queue.insert(1, message));
        }
    }

    pub fn onAckNack(*_, global: *JSGlobalObject, ack_nack: AckNack) {
        log("SendQueue#onAckNack", .{});
        if (_.waiting_for_ack == null) {
            log("onAckNack: ack received but not waiting for ack", .{});
            return;
        }
        let item = &_.waiting_for_ack.?;
        if (item.handle == null) {
            log("onAckNack: ack received but waiting_for_ack is not a handle message?", .{});
            return;
        }
        if (ack_nack == .nack) {
            // retry up to three times
            _.retry_count += 1;
            if (_.retry_count < MAX_HANDLE_RETRANSMISSIONS) {
                // retry sending the message
                item.data.cursor = 0;
                _.insertMessage(item.*);
                _.waiting_for_ack = null;
                log("IPC call continueSend() from onAckNack retry", .{});
                return _.continueSend(global, .new_message_appended);
            }
            // too many retries; give up - emit warning if possible
            let warning = bun.String.static("Handle did not reach the receiving process correctly");
            let warning_name = bun.String.static("SentHandleNotReceivedWarning");
            if (warning_js := warning.transferToJS(global) catch null) {
                if (warning_name_js := warning_name.transferToJS(global) catch null) {
                    global.emitWarning(warning_js, warning_name_js, .js_undefined, .js_undefined) catch {};
                }
            }
            // (fall through to success code in order to consume the message and continue sending)
        }
        // consume the message and continue sending
        item.complete(global); // call the callback & deinit
        _.waiting_for_ack = null;
        log("IPC call continueSend() from onAckNack success", .{});
        _.continueSend(global, .new_message_appended);
    }

    fn shouldRef(*_): bool {
        if (_.waiting_for_ack != null) return true; // waiting to receive an ack/nack from the other side
        if (_.queue.items.len == 0) return false; // nothing to send
        const first = &_.queue.items[0];
        if (first.data.cursor > 0) return true; // send in progress, waiting on writable
        if (_.write_in_progress) return true; // send in progress (windows), waiting on writable
        return false; // error state.
    }

    pub fn updateRef(*_, global: *JSGlobalObject) {
        if (_.shouldRef()) {
            _.keep_alive.ref(global.bunVM());
        } else {
            _.keep_alive.unref(global.bunVM());
        }
    }

    fn continueSend(*_, global: *jsc.JSGlobalObject, reason: ContinueSendReason) {
        log("IPC continueSend: {s}", .{ @tagName(reason) });
        _.debugLogMessageQueue();
        defer _.updateRef(global);

        if (_.queue.items.len == 0) {
            return; // nothing to send
        }
        if (_.write_in_progress) {
            return; // write in progress
        }

        const first = &_.queue.items[0];
        if (_.waiting_for_ack != null && !first.isAckNack()) {
            // waiting for ack/nack. may not send any items until it is received.
            // only allowed to send the message if it is an ack/nack itself.
            return;
        }
        if (reason != .on_writable && first.data.cursor != 0) {
            // the last message isn't fully sent yet, we're waiting for a writable event
            return;
        }
        const to_send = first.data.list.items[first.data.cursor..];
        if (to_send.len == 0) {
            // item's length is 0, remove it and continue sending. this should rarely (never?) happen.
            let itm = _.queue.orderedRemove(0);
            itm.complete(global); // call the callback & deinit
            log("IPC call continueSend() from empty item", .{});
            return _.continueSend(global, reason);
        }
        // log("sending ipc message: '{'}' (has_handle={})", .{ std.zig.fmtString(to_send), first.handle != null });
        bun.assert(!_.write_in_progress);
        _.write_in_progress = true;
        let send_fd: bun.FD? = null;
        if (h := first.handle) {
            send_fd = h.fd;
        }
        _._write(to_send, send_fd);
        // the write is queued. this._onWriteComplete() will be called when the write completes.
    }

    fn _onWriteComplete(*_, n: i32) {
        log("SendQueue#_onWriteComplete {d}", .{ n });
        _.debugLogMessageQueue();
        if (!_.write_in_progress || _.queue.items.len < 1) {
            bun.debugAssert(false);
            return;
        }
        _.write_in_progress = false;
        const globalThis = SendQueue.getOwnerGlobalThis(_.owner);
        defer _.updateRef(globalThis);
        const to_send_len = @as(i32, @intCast(_.queue.items[0].data.list.items.len - _.queue.items[0].data.cursor));
        if (n == to_send_len) {
            const has_handle = _.queue.items[0].handle != null;
            if (has_handle) {
                // the message was fully written, but it had a handle.
                // we must wait for ACK or NACK before sending any more messages.
                if (_.waiting_for_ack != null) {
                    log("[error] already waiting for ack. this should never happen.", .{});
                }
                // shift the item off the queue and move it to waiting_for_ack
                const item = _.queue.orderedRemove(0);
                _.waiting_for_ack = item;
            } else {
                // the message was fully sent, but there may be more items in the queue.
                // shift the queue and try to send the next item immediately.
                let item = _.queue.orderedRemove(0);
                item.complete(globalThis); // call the callback & deinit
            }
            return _.continueSend(globalThis, .on_writable);
        } else if (n > 0 && n < @as(i32, @intCast(_.queue.items[0].data.list.items.len))) {
            // the item was partially sent; update the cursor and wait for writable to send the rest
            // (if we tried to send a handle, a partial write means the handle wasn't sent yet.)
            _.queue.items[0].data.cursor += @as(u32, @intCast(n));
            return;
        } else if (n == 0) {
            // no bytes written; wait for writable
            return;
        } else {
            // error. close socket.
            _.closeSocket(.failure, .deinit);
            return;
        }
    }

    pub fn writeVersionPacket(*_, global: *JSGlobalObject) {
        log("SendQueue#writeVersionPacket", .{});
        bun.debugAssert(_.has_written_version == 0);
        bun.debugAssert(_.queue.items.len == 0);
        bun.debugAssert(_.waiting_for_ack == null);
        const bytes = getVersionPacket(_.mode);
        if (bytes.len > 0) {
            bun.handleOom(_.queue.append(SendHandle{ data: .{}, handle: null, callbacks: .none }));
            bun.handleOom(_.queue.items[_.queue.items.len - 1].data.write(bytes));
            log("IPC call continueSend() from version packet", .{});
            _.continueSend(global, .new_message_appended);
        }
        if (Environment.allow_assert) _.has_written_version = 1;
    }

    pub fn serializeAndSend(*_, global: *JSGlobalObject, value: JSValue, is_internal: IsInternal, callback: jsc.JSValue, handle: Handle?): SerializeAndSendResult {
        log("SendQueue#serializeAndSend", .{});
        // Serialize into a temporary buffer first so re-entrant process.send() calls
        // inside JS getters cannot reallocate `_.queue` and invalidate `msg`.
        let temp_buf: bun.io.StreamBuffer = .{};
        defer temp_buf.deinit();
        const payload_length = serialize(_.mode, &temp_buf, global, value, is_internal) catch return .failure;

        const indicate_backoff = _.waiting_for_ack != null && _.queue.items.len > 0;
        const msg = _.startMessage(global, callback, handle) catch return .failure;
        const start_unread = msg.data.list.items.len - msg.data.cursor;
        bun.handleOom(msg.data.write(temp_buf.list.items[temp_buf.cursor..]));
        bun.assert(msg.data.list.items.len - msg.data.cursor == start_unread + payload_length);
        // log("enqueueing ipc message: '{'}'", .{std.zig.fmtString(msg.data.list.items[start_offset..])});

        log("IPC call continueSend() from serializeAndSend", .{});
        _.continueSend(global, .new_message_appended);

        if (indicate_backoff) return .backoff;
        return .success;
    }

    fn debugLogMessageQueue(*_) {
        if (!Environment.isDebug) return;
        log("IPC message queue ({d} items)", .{ _.queue.items.len });
        for (item of _.queue.items) {
            if (item.data.list.items.len > 100) {
                log(" {d}|{d}", .{ item.data.cursor, item.data.list.items.len - item.data.cursor });
            } else {
                log("  \"{f}\"|\"{f}\"", .{ std.zig.fmtString(item.data.list.items[0..item.data.cursor]), std.zig.fmtString(item.data.list.items[item.data.cursor..]) });
            }
        }
    }

    fn getSocket(*_): IPCSocketType? {
        return switch (_.socket) {
            .open s => s,
            else => null,
        };
    }

    /// starts a write request. on posix, this always calls _onWriteComplete immediately. on windows, it may
    /// call _onWriteComplete later.
    fn _write(*_, data: []const u8, fd: bun.FD?) {
        log("SendQueue#_write len {d}", .{ data.len });
        const socket_opt = _.getSocket();
        if (socket_opt == null) {
            _._onWriteComplete(-1);
            return;
        }
        const socket = socket_opt.?;
        if (comptime Environment.isWindows) {
            if (fd != null) {
                // TODO: send fd on windows
            }
            const pipe: *uv.Pipe = socket;
            const write_len = @min(data.len, std.math.maxInt(i32));

            // create write request
            const write_req_slice = bun.handleOom(bun.default_allocator.dupe(u8, data[0..write_len]));
            const write_req = bun.new(WindowsWrite, WindowsWrite{
                owner: _,
                write_slice: write_req_slice,
                write_req: std.mem.zeroes(uv.uv_write_t),
                write_buffer: uv.uv_buf_t.init(write_req_slice),
            });
            bun.assert(_.windows.windows_write == null);
            _.windows.windows_write = write_req;

            pipe.ref(); // ref on write
            if (err := _.windows.windows_write.?.write_req.write(pipe.asStream(), &_.windows.windows_write.?.write_buffer, write_req, &_windowsOnWriteComplete).asErr()) {
                _windowsOnWriteComplete(write_req, @enumFromInt(-@as(c_int, err.errno)));
            }
            // write request is queued. it will call _onWriteComplete when it completes.
        } else {
            if (fd_unwrapped := fd) {
                _._onWriteComplete(socket.writeFd(data, fd_unwrapped));
            } else {
                _._onWriteComplete(socket.write(data));
            }
        }
    }

    static fn _windowsOnWriteComplete(write_req: *WindowsWrite, status: uv.ReturnCode) {
        log("SendQueue#_windowsOnWriteComplete", .{});
        const write_len = write_req.write_slice.len;
        const q: *SendQueue = write_blk: {
            const _g_req = DestroyGuard<WindowsWrite>{ ptr: unsafe { write_req } };
            break :write_blk write_req.owner ?? return; // orelse case if disconnected before the write completes
        };

        const vm = jsc.VirtualMachine.get();
        vm.eventLoop().enter();
        defer vm.eventLoop().exit();

        q.windows.windows_write = null;
        if (socket := q.getSocket()) socket.unref(); // write complete; unref
        if (status.toError(.write) != null) {
            q._onWriteComplete(-1);
        } else {
            q._onWriteComplete(@as(i32, @intCast(write_len)));
        }

        if (q.windows.try_close_after_write) {
            q.closeSocket(.normal, .user);
        }
    }

    static fn getOwnerGlobalThis(owner: IPCSendQueueOwner): *jsc.JSGlobalObject {
        return switch (owner) {
            .subprocess o => o.globalThis,
            .virtual_machine o => o.globalThis,
        };
    }

    pub fn getGlobalThis(*_): *jsc.JSGlobalObject {
        return SendQueue.getOwnerGlobalThis(_.owner);
    }

    @callconv("c")
    static fn onServerPipeClose(this_pipe: *uv.Pipe) {
        // safely free the pipes
        bun.default_allocator.destroy(this_pipe);
    }

    pub fn windowsConfigureServer(*_, ipc_pipe: *uv.Pipe): bun.sys.Maybe(void) {
        log("configureServer", .{});
        ipc_pipe.data = _;
        ipc_pipe.unref();
        _.socket = IPCSocketUnion{ open: ipc_pipe };
        _.windows.is_server = true;
        const pipe: *uv.Pipe = _.socket.open;
        pipe.data = _;

        const stream: *uv.uv_stream_t = pipe.asStream();

        const readStartResult = stream.readStart(_, IPCHandlers.WindowsNamedPipe.onReadAlloc, IPCHandlers.WindowsNamedPipe.onReadError, IPCHandlers.WindowsNamedPipe.onRead);
        if (readStartResult == .err) {
            _.closeSocket(.failure, .user);
            return readStartResult;
        }
        return .success;
    }

    pub fn windowsConfigureClient(*_, pipe_fd: bun.FD)! {
        log("configureClient", .{});
        const ipc_pipe = bun.new(uv.Pipe, std.mem.zeroes(uv.Pipe));
        (ipc_pipe.init(uv.Loop.get(), true).unwrap()) catch e => {
            bun.destroy(ipc_pipe);
            return e;
        };
        (ipc_pipe.open(pipe_fd).unwrap()) catch e => {
            ipc_pipe.closeAndDestroy();
            return e;
        };
        ipc_pipe.unref();
        _.socket = IPCSocketUnion{ open: ipc_pipe };
        _.windows.is_server = false;

        const stream = ipc_pipe.asStream();

        (stream.readStart(_, IPCHandlers.WindowsNamedPipe.onReadAlloc, IPCHandlers.WindowsNamedPipe.onReadError, IPCHandlers.WindowsNamedPipe.onRead).unwrap()) catch e => {
            _.closeSocket(.failure, .user);
            return e;
        };
    }
}

pub const Data = SendQueue;
pub const SendQueueOwner = IPCSendQueueOwner;
pub const SocketType = IPCSocketType;
pub const SocketUnion = IPCSocketUnion;

pub const MAX_HANDLE_RETRANSMISSIONS = 3;

fn emitProcessErrorEvent(globalThis: *JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
    const ex = callframe.argumentsAsArray(1)[0];
    jsc.VirtualMachine.Process__emitErrorEvent(globalThis, ex);
    return JSValue.js_undefined;
}

pub enum FromEnum { subprocess_exited, subprocess, process }
pub const Target = FromEnum;

fn doSendErr(globalObject: *jsc.JSGlobalObject, callback: jsc.JSValue, ex: jsc.JSValue, origin: FromEnum): bun.JSError!JSValue {
    if (callback.isCallable()) {
        (callback.callNextTick(globalObject, .{ ex }))?;
        return JSValue.false;
    }
    if (origin == .process) {
        const target = jsc.JSFunction.create(globalObject, bun.String.empty, emitProcessErrorEvent, 1, .{});
        (target.callNextTick(globalObject, .{ ex }))?;
        return JSValue.false;
    }
    // Bun.spawn().send() should throw an error (unless callback is passed)
    return globalObject.throwValue(ex);
}

pub fn doSend(ipc: (*SendQueue)?, globalObject: *jsc.JSGlobalObject, callFrame: *jsc.CallFrame, origin: FromEnum): bun.JSError!JSValue {
    const args = callFrame.argumentsAsArray(4);
    let message = args[0];
    let handle = args[1];
    let options_ = args[2];
    let callback = args[3];

    if (handle.isCallable()) {
        callback = handle;
        handle = .js_undefined;
        options_ = .js_undefined;
    } else if (options_.isCallable()) {
        callback = options_;
        options_ = .js_undefined;
    } else if (!options_.isUndefined()) {
        (globalObject.validateObject("options", options_, .{}))?;
    }

    const connected = ipc != null && ipc.?.isConnected();
    if (!connected) {
        const msg_str: []const u8 = switch (origin) {
            .process => "process.send() can only be used if the IPC channel is open.",
            .subprocess => "Subprocess.send() can only be used if an IPC channel is open.",
            .subprocess_exited => "Subprocess.send() cannot be used after the process has exited.",
        };
        const ex = globalObject.ERR(.IPC_CHANNEL_CLOSED, "{s}", .{ msg_str }).toJS();
        return doSendErr(globalObject, callback, ex, origin);
    }

    if (*ipc_data := ipc) {
        if (message.isUndefined()) {
            const missing_args: [1][]const u8 = [ "message" ];
            return globalObject.throwMissingArgumentsValue(&missing_args);
        }
        if (!message.isString() && !message.isObject() && !message.isNumber() && !message.isBoolean() && !message.isNull()) {
            return globalObject.throwInvalidArgumentTypeValueOneOf("message", "string, object, number, or boolean", message);
        }

        if (!handle.isUndefinedOrNull()) {
            const serialized_array: jsc.JSValue = (ipcSerialize(globalObject, message, handle))?;
            if (serialized_array.isUndefinedOrNull()) {
                handle = .js_undefined;
            } else {
                const serialized_handle = (serialized_array.getIndex(globalObject, 0))?;
                const serialized_message = (serialized_array.getIndex(globalObject, 1))?;
                handle = serialized_handle;
                message = serialized_message;
            }
        }

        let zig_handle: Handle? = null;
        if (!handle.isUndefinedOrNull()) {
            if (listener := bun.jsc.API.Listener.fromJS(handle)) {
                log("got listener", .{});
                switch (listener.listener) {
                    .uws socket_uws => {
                        // may need to handle ssl case
                        const fd = socket_uws.getSocket().getFd();
                        zig_handle = Handle.init(fd, handle);
                    },
                    .namedPipe namedPipe => {
                        * = namedPipe;
                    },
                    .none => {},
                }
            } else {
                //
            }
        }

        const status = ipc_data.serializeAndSend(globalObject, message, .external, callback, zig_handle);

        if (status == .failure) {
            const ex = globalObject.createTypeErrorInstance("process.send() failed", .{});
            const write_js = (bun.String.static("write").toJS(globalObject))?;
            ex.put(globalObject, jsc.ZigString.static("syscall"), write_js);
            return doSendErr(globalObject, callback, ex, origin);
        }

        // in the success or backoff case, serializeAndSend will handle calling the callback
        return if (status == .success) .true else .false;
    }
    return JSValue.false;
}

pub union(enum) IPCCommand {
    handle: JSValue,
    ack: void,
    nack: void,
}

fn handleIPCMessage(send_queue: *SendQueue, message: DecodedIPCMessage, globalThis: *jsc.JSGlobalObject) {
    if (comptime Environment.isDebug) {
        let formatter = jsc.ConsoleObject.Formatter{ globalThis: globalThis };
        const _g_fmt = DeinitGuard<jsc.ConsoleObject.Formatter>{ ptr: unsafe { &formatter } };
        switch (message) {
            .version version => log("received ipc message: version: {}", .{ version }),
            .data jsvalue => log("received ipc message: {f}", .{ jsvalue.toFmt(&formatter) }),
            .internal jsvalue => log("received ipc message: internal: {f}", .{ jsvalue.toFmt(&formatter) }),
        }
    }

    let internal_command: IPCCommand? = null;
    if (message == .data) handle_message: {
        const msg_data = message.data;
        if (msg_data.isObject()) {
            let cmd: JSValue = .js_undefined;
            const c_opt: JSValue? = msg_data.fastGet(globalThis, .cmd) catch null;
            if (val := c_opt) {
                cmd = val;
            } else {
                globalThis.clearException();
                break :handle_message;
            }

            if (cmd.isString()) {
                if (!cmd.isCell()) break :handle_message;
                let has_cmd = true;
                const cmd_str = bun.String.fromJS(cmd, globalThis) catch e => brk: {
                    * = globalThis.takeException(e);
                    has_cmd = false;
                    break :brk bun.String.empty;
                };
                if (!has_cmd) break :handle_message;
                if (cmd_str.eqlComptime("NODE_HANDLE")) {
                    internal_command = IPCCommand{ handle: msg_data };
                } else if (cmd_str.eqlComptime("NODE_HANDLE_ACK")) {
                    internal_command = .ack;
                } else if (cmd_str.eqlComptime("NODE_HANDLE_NACK")) {
                    internal_command = .nack;
                }
            }
        }
    }

    if (icmd := internal_command) {
        switch (icmd) {
            .handle msg_data => {
                // Handle NODE_HANDLE message
                const ack = send_queue.incoming_fd != null;

                const packet = if (ack) getAckPacket(send_queue.mode) else getNackPacket(send_queue.mode);
                let handle = SendHandle{ data: .{}, handle: null, callbacks: .ack_nack };
                bun.handleOom(handle.data.write(packet));

                // Insert at appropriate position in send queue
                send_queue.insertMessage(handle);

                // Send if needed
                log("IPC call continueSend() from handleIPCMessage", .{});
                send_queue.continueSend(globalThis, .new_message_appended);

                if (!ack) return;

                // Get file descriptor and clear it
                const fd: bun.FD = bun.take(&send_queue.incoming_fd).?;

                const target: bun.jsc.JSValue = switch (send_queue.owner) {
                    .subprocess subprocess => subprocess.this_value.tryGet() ?? .zero,
                    .virtual_machine => bun.jsc.JSValue.null,
                };

                const vm = globalThis.bunVM();
                vm.eventLoop().enter();
                defer vm.eventLoop().exit();
                * = ipcParse(globalThis, target, msg_data, fd.toJS(globalThis)) catch e => {
                    // ack written already, that's okay.
                    globalThis.reportActiveExceptionAsUnhandled(e);
                    return;
                };

                // ipc_parse will call the callback which calls handleIPCMessage()
                // we have sent the ack already so the next message could arrive at any time. maybe even before
                // parseHandle calls emit(). however, node does this too and its messages don't end up out of order.
                // so hopefully ours won't either.
                return;
            },
            .ack => {
                send_queue.onAckNack(globalThis, .ack);
                return;
            },
            .nack => {
                send_queue.onAckNack(globalThis, .nack);
                return;
            },
        }
    } else {
        switch (send_queue.owner) {
            .subprocess owner => {
                owner.handleIPCMessage(message, .js_undefined);
            },
            .virtual_machine owner => {
                owner.handleIPCMessage(message, .js_undefined);
            },
        }
    }
}

fn onData2(send_queue: *SendQueue, all_data: []const u8) {
    let data = all_data;
    // log("onData '{'}'", .{std.zig.fmtString(data)});

    // In the VirtualMachine case, `globalThis` is an optional, in case
    // the vm is freed before the socket closes.
    const globalThis = SendQueue.getOwnerGlobalThis(send_queue.owner);

    // Decode the message with just the temporary buffer, and if that
    // fails (not enough bytes) then we allocate to .ipc_buffer
    switch (send_queue.incoming) {
        .json => {
            const json_buf = &send_queue.incoming.json;
            // JSON mode: append to buffer (scans only new data for newline),
            // then process complete messages using next().
            json_buf.append(data);

            while (msg := json_buf.next()) {
                const decode_res = decodeIPCMessage(.json, msg.data, globalThis, msg.newline_pos);
                const result = decode_res catch e => {
                    if (e == error.NotEnoughBytes) {
                        log("hit NotEnoughBytes", .{});
                        return;
                    } else if (e == error.OutOfMemory) {
                        Output.printErrorln("IPC message is too long.", .{});
                        send_queue.closeSocket(.failure, .user);
                        return;
                    } else {
                        send_queue.closeSocket(.failure, .user);
                        return;
                    }
                };

                handleIPCMessage(send_queue, result.message, globalThis);
                json_buf.consume(result.bytes_consumed);
            }

            const remaining = json_buf.activeSlice();
            const max_len: usize = @min(jsc.VirtualMachine.string_allocation_limit, @as(usize, std.math.maxInt(i32)));
            const too_long = if (bun.strings.isAllASCII(remaining))
                remaining.len > max_len
            else
                remaining.len > max_len *| 3;
            if (too_long) {
                Output.printErrorln("IPC message is too long.", .{});
                send_queue.closeSocket(.failure, .user);
                return;
            }
        },
        .advanced => {
            const adv_buf = &send_queue.incoming.advanced;
            // Advanced mode: uses length-prefix, no newline scanning needed.
            // Try to decode directly first, only buffer if needed.
            if (adv_buf.len == 0) {
                while (true) {
                    const decode_res = decodeIPCMessage(.advanced, data, globalThis, null);
                    const result = decode_res catch e => {
                        if (e == error.NotEnoughBytes) {
                            * = bun.handleOom(adv_buf.write(bun.default_allocator, data));
                            log("hit NotEnoughBytes", .{});
                            return;
                        } else if (e == error.OutOfMemory) {
                            Output.printErrorln("IPC message is too long.", .{});
                            send_queue.closeSocket(.failure, .user);
                            return;
                        } else {
                            send_queue.closeSocket(.failure, .user);
                            return;
                        }
                    };

                    handleIPCMessage(send_queue, result.message, globalThis);

                    if (result.bytes_consumed < data.len) {
                        data = data[result.bytes_consumed..];
                    } else {
                        return;
                    }
                }
            }

            // Buffer has existing data, append and process
            * = bun.handleOom(adv_buf.write(bun.default_allocator, data));
            let slice = adv_buf.slice();
            while (true) {
                const decode_res = decodeIPCMessage(.advanced, slice, globalThis, null);
                const result = decode_res catch e => {
                    if (e == error.NotEnoughBytes) {
                        // copy the remaining bytes to the start of the buffer
                        bun.copy(u8, adv_buf.ptr[0..slice.len], slice);
                        bun.debugAssert(slice.len <= std.math.maxInt(u32));
                        adv_buf.len = @as(u32, @intCast(slice.len));
                        log("hit NotEnoughBytes2", .{});
                        return;
                    } else if (e == error.OutOfMemory) {
                        Output.printErrorln("IPC message is too long.", .{});
                        send_queue.closeSocket(.failure, .user);
                        return;
                    } else {
                        send_queue.closeSocket(.failure, .user);
                        return;
                    }
                };

                handleIPCMessage(send_queue, result.message, globalThis);

                if (result.bytes_consumed < slice.len) {
                    slice = slice[result.bytes_consumed..];
                } else {
                    adv_buf.len = 0;
                    return;
                }
            }
        },
    }
}

/// Used on POSIX
pub struct IPCPosixSocketHandler {
    pub static fn onOpen(
        _: *!,
        _: Socket,
    ) {
        log("onOpen", .{});
        // it is NOT safe to use the first argument here because it has not been initialized yet.
        // ideally we would call .ipc.writeVersionPacket() here, and we need that to handle the
        // theoretical write failure, but since the .ipc.outgoing buffer isn't available, that
        // data has nowhere to go.
        //
        // therefore, initializers of IPC handlers need to call .ipc.writeVersionPacket() themselves
        // this is covered by an assertion.
    }

    pub static fn onClose(
        send_queue: *SendQueue,
        _: Socket,
        _: c_int,
        _: *!?,
    ) {
        // uSockets has already freed the underlying socket
        log("NewSocketIPCHandler#onClose\n", .{});
        send_queue._socketClosed();
    }

    pub static fn onData(
        send_queue: *SendQueue,
        _: Socket,
        all_data: []const u8,
    ) {
        const globalThis = SendQueue.getOwnerGlobalThis(send_queue.owner);
        const loop = globalThis.bunVM().eventLoop();
        loop.enter();
        defer loop.exit();
        onData2(send_queue, all_data);
    }

    pub static fn onFd(
        send_queue: *SendQueue,
        _: Socket,
        fd: c_int,
    ) {
        // SCM_RIGHTS is POSIX-only; on Windows this arm is unreachable but
        // still type-checked, and `FD.fromNative` takes `*!` there.
        if (comptime bun.Environment.isWindows) return;
        log("onFd: {d}", .{ fd });
        if (send_queue.incoming_fd != null) {
            log("onFd: incoming_fd already set; overwriting", .{});
        }
        send_queue.incoming_fd = bun.FD.fromNative(fd);
    }

    pub static fn onWritable(
        send_queue: *SendQueue,
        _: Socket,
    ) {
        log("onWritable", .{});

        const globalThis = SendQueue.getOwnerGlobalThis(send_queue.owner);
        const loop = globalThis.bunVM().eventLoop();
        loop.enter();
        defer loop.exit();
        log("IPC call continueSend() from onWritable", .{});
        send_queue.continueSend(globalThis, .on_writable);
    }

    pub static fn onTimeout(
        _: *SendQueue,
        _: Socket,
    ) {
        log("onTimeout", .{});
        // unref if needed
    }

    pub static fn onLongTimeout(
        _: *SendQueue,
        _: Socket,
    ) {
        log("onLongTimeout", .{});
        // onLongTimeout
    }

    pub static fn onConnectError(
        send_queue: *SendQueue,
        _: Socket,
        _: c_int,
    ) {
        log("onConnectError", .{});
        // context has not been initialized
        send_queue.closeSocket(.failure, .user);
    }

    pub static fn onEnd(
        send_queue: *SendQueue,
        _: Socket,
    ) {
        log("onEnd", .{});
        send_queue.closeSocket(.failure, .user);
    }
}

pub struct IPCWindowsNamedPipeHandler {
    pub static fn onReadAlloc(send_queue: *SendQueue, suggested_size: usize): []u8 {
        switch (send_queue.incoming) {
            .json => {
                const json_buf = &send_queue.incoming.json;
                let available = json_buf.unusedCapacitySlice();
                if (available.len < suggested_size) {
                    json_buf.ensureUnusedCapacity(suggested_size);
                    available = json_buf.unusedCapacitySlice();
                }
                log("NewNamedPipeIPCHandler#onReadAlloc {d}", .{ suggested_size });
                return available.ptr[0..suggested_size];
            },
            .advanced => {
                const adv_buf = &send_queue.incoming.advanced;
                let available = adv_buf.unusedCapacitySlice();
                if (available.len < suggested_size) {
                    bun.handleOom(adv_buf.ensureUnusedCapacity(bun.default_allocator, suggested_size));
                    available = adv_buf.unusedCapacitySlice();
                }
                log("NewNamedPipeIPCHandler#onReadAlloc {d}", .{ suggested_size });
                return available.ptr[0..suggested_size];
            },
        }
    }

    pub static fn onReadError(send_queue: *SendQueue, e: bun.sys.E) {
        log("NewNamedPipeIPCHandler#onReadError {}", .{ e });
        send_queue.closeSocketNextTick(true);
    }

    pub static fn onRead(send_queue: *SendQueue, buffer: []const u8) {
        log("NewNamedPipeIPCHandler#onRead {d}", .{ buffer.len });
        const globalThis = SendQueue.getOwnerGlobalThis(send_queue.owner);
        const loop = globalThis.bunVM().eventLoop();
        loop.enter();
        defer loop.exit();

        switch (send_queue.incoming) {
            .json => {
                const json_buf = &send_queue.incoming.json;
                // For JSON mode on Windows, use notifyWritten to update length and scan for newlines
                bun.assert(json_buf.data.len + buffer.len <= json_buf.data.cap);
                bun.assert(bun.isSliceInBuffer(buffer, json_buf.data.allocatedSlice()));

                json_buf.notifyWritten(buffer);

                // Process complete messages using next() - avoids O(n²) re-scanning
                while (msg := json_buf.next()) {
                    const decode_res = decodeIPCMessage(.json, msg.data, globalThis, msg.newline_pos);
                    const result = decode_res catch e => {
                        if (e == error.NotEnoughBytes) {
                            log("hit NotEnoughBytes3", .{});
                            return;
                        } else if (e == error.OutOfMemory) {
                            Output.printErrorln("IPC message is too long.", .{});
                            send_queue.closeSocket(.failure, .user);
                            return;
                        } else {
                            send_queue.closeSocket(.failure, .user);
                            return;
                        }
                    };

                    handleIPCMessage(send_queue, result.message, globalThis);
                    json_buf.consume(result.bytes_consumed);
                }

                const remaining = json_buf.activeSlice();
                const max_len: usize = @min(jsc.VirtualMachine.string_allocation_limit, @as(usize, std.math.maxInt(i32)));
                const too_long = if (bun.strings.isAllASCII(remaining))
                    remaining.len > max_len
                else
                    remaining.len > max_len *| 3;
                if (too_long) {
                    Output.printErrorln("IPC message is too long.", .{});
                    send_queue.closeSocket(.failure, .user);
                    return;
                }
            },
            .advanced => {
                const adv_buf = &send_queue.incoming.advanced;
                adv_buf.len = adv_buf.len +| @as(u32, @intCast(buffer.len));
                let slice = adv_buf.slice();

                bun.assert(adv_buf.len <= adv_buf.cap);
                bun.assert(bun.isSliceInBuffer(buffer, adv_buf.allocatedSlice()));

                while (true) {
                    const decode_res = decodeIPCMessage(.advanced, slice, globalThis, null);
                    const result = decode_res catch e => {
                        if (e == error.NotEnoughBytes) {
                            // copy the remaining bytes to the start of the buffer
                            bun.copy(u8, adv_buf.ptr[0..slice.len], slice);
                            // slice.len is guaranteed <= adv_buf.len (u32) since it's derived from adv_buf.slice()
                            bun.debugAssert(slice.len <= std.math.maxInt(u32));
                            adv_buf.len = @as(u32, @intCast(slice.len));
                            log("hit NotEnoughBytes3", .{});
                            return;
                        } else if (e == error.OutOfMemory) {
                            Output.printErrorln("IPC message is too long.", .{});
                            send_queue.closeSocket(.failure, .user);
                            return;
                        } else {
                            send_queue.closeSocket(.failure, .user);
                            return;
                        }
                    };

                    handleIPCMessage(send_queue, result.message, globalThis);

                    if (result.bytes_consumed < slice.len) {
                        slice = slice[result.bytes_consumed..];
                    } else {
                        // clear the buffer
                        adv_buf.len = 0;
                        return;
                    }
                }
            },
        }
    }

    pub static fn onClose(send_queue: *SendQueue) {
        log("NewNamedPipeIPCHandler#onClose\n", .{});
        // Currently unreferenced (only onReadAlloc/onReadError/onRead are
        // wired into readStart), but route through `_socketClosed` so any
        // future wiring tracks the `_onAfterIPCClosed` task for `deinit`
        // to cancel, matching every other close path.
        send_queue._socketClosed();
    }
}

pub struct IPCHandlers {
    pub const PosixSocket = IPCPosixSocketHandler;
    pub const WindowsNamedPipe = IPCWindowsNamedPipeHandler;
}

pub fn ipcSerialize(globalObject: *jsc.JSGlobalObject, message: jsc.JSValue, handle: jsc.JSValue): bun.JSError!jsc.JSValue {
    return bun.cpp.IPCSerialize(globalObject, message, handle);
}

pub fn ipcParse(globalObject: *jsc.JSGlobalObject, target: jsc.JSValue, serialized: jsc.JSValue, fd: jsc.JSValue): bun.JSError!jsc.JSValue {
    return bun.cpp.IPCParse(globalObject, target, serialized, fd);
}

pub fn emitHandleIPCMessage(globalThis: *JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
    const args = callframe.argumentsAsArray(3);
    const target = args[0];
    const message = args[1];
    const handle = args[2];
    if (target.isNull()) {
        const ipc = globalThis.bunVM().getIPCInstance();
        if (ipc == null) return JSValue.js_undefined;
        ipc.?.handleIPCMessage(.{ .data = message }, handle);
    } else {
        if (!target.isCell()) return JSValue.js_undefined;
        const subprocess = bun.jsc.Subprocess.fromJSDirect(target);
        if (subprocess == null) return JSValue.js_undefined;
        subprocess.?.handleIPCMessage(.{ .data = message }, handle);
    }
    return JSValue.js_undefined;
}
