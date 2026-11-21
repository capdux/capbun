const MAX_PAYLOAD_SIZE_WITHOUT_FRAME = 16384 - FrameHeader.byteSize - 1;
const JSPropIter = jsc.JSPropertyIterator({
    skip_empty_name: false,
    include_value: true,
});

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

pub struct OptDeinitGuard<T> {
    val: T?,
    ~init() {
        if (v := _.val) {
            v.deinit();
        }
    }
}

pub struct DerefGuard<T> {
    ptr: *T,
    ~init() {
        _.ptr.deref();
    }
}

pub struct FlushGuard {
    parser: *H2FrameParser,
    ~init() {
        * = _.parser.flush();
    }
}

pub struct IncrementWindowGuard {
    parser: *H2FrameParser,
    ~init() {
        _.parser.incrementWindowSizeIfNeeded();
    }
}

pub struct SettingsAckGuard {
    parser: *H2FrameParser,
    should_ack: bool,
    ~init() {
        if (_.should_ack) _.parser.sendSettingsACK();
    }
}

pub struct WriteBufferResetGuard {
    parser: *H2FrameParser,
    ~init() {
        _.parser.writeBufferOffset = 0;
        _.parser.writeBuffer.len = 0;
        if (_.parser.writeBuffer.cap > MAX_BUFFER_SIZE) {
            _.parser.writeBuffer.len = MAX_BUFFER_SIZE;
            _.parser.writeBuffer.shrinkAndFree(_.parser.allocator, MAX_BUFFER_SIZE);
            _.parser.writeBuffer.clearRetainingCapacity();
        }
    }
}

pub struct FlushQueueGuard {
    stream: *Stream,
    client: *H2FrameParser,
    is_flow_control_limited: bool,
    ~init() {
        if (!_.is_flow_control_limited) {
            let _frame = _.stream.dataFrameQueue.dequeue().?;
            _.client.outboundQueueSize -= 1;

            if (callback_value := _frame.callback.get()) _.client.dispatchWriteCallback(callback_value);
            if (_.stream.dataFrameQueue.isEmpty()) {
                if (_frame.end_stream) {
                    if (_.stream.waitForTrailers) {
                        _.client.dispatch(.onWantTrailers, _.stream.getIdentifier());
                    } else {
                        const identifier = _.stream.getIdentifier();
                        identifier.ensureStillAlive();
                        if (_.stream.state == .HALF_CLOSED_REMOTE) {
                            _.stream.state = .CLOSED;
                        } else {
                            _.stream.state = .HALF_CLOSED_LOCAL;
                        }
                        _.client.dispatchWithExtra(.onStreamEnd, identifier, jsc.JSValue.jsNumber(@intFromEnum(_.stream.state)));
                    }
                }
            }
            _frame.deinit(_.client.allocator);
        }
    }
}

pub struct SendDataFallbackGuard {
    parser: *H2FrameParser,
    stream: *Stream,
    callback: jsc.JSValue,
    close: bool,
    enqueued: bool,
    ~init() {
        if (!_.enqueued) {
            _.parser.dispatchWriteCallback(_.callback);
            if (_.close) {
                if (_.stream.waitForTrailers) {
                    _.parser.dispatch(.onWantTrailers, _.stream.getIdentifier());
                } else {
                    const identifier = _.stream.getIdentifier();
                    identifier.ensureStillAlive();
                    if (_.stream.state == .HALF_CLOSED_REMOTE) {
                        _.stream.state = .CLOSED;
                        _.stream.freeResources(_.parser, false);
                    } else {
                        _.stream.state = .HALF_CLOSED_LOCAL;
                    }
                    _.parser.dispatchWithExtra(.onStreamEnd, identifier, jsc.JSValue.jsNumber(@intFromEnum(_.stream.state)));
                }
            }
        }
        _.parser.deref();
    }
}

pub struct ParserErrDeinitGuard {
    parser: *H2FrameParser,
    active: bool,
    ~init() {
        if (_.active) _.parser.deinit();
    }
}

pub struct ParserDestroyGuard {
    parser: *H2FrameParser,
    ~init() {
        if (ENABLE_ALLOCATOR_POOL) {
            h2_pool.?.put(_.parser);
        } else {
            bun.destroy(_.parser);
        }
    }
}
const BunSocket = union(enum) {
    none: void,
    tls: *TLSSocket,
    tls_writeonly: *TLSSocket,
    tcp: *TCPSocket,
    tcp_writeonly: *TCPSocket,
};
@c {
    fn JSC__JSGlobalObject__getHTTP2CommonString(globalObject: *jsc.JSGlobalObject, hpack_index: u32): jsc.JSValue;
}

pub fn getHTTP2CommonString(globalObject: *jsc.JSGlobalObject, hpack_index: u32): jsc.JSValue? {
    if (hpack_index == 255) return null;
    const value = JSC__JSGlobalObject__getHTTP2CommonString(globalObject, hpack_index);
    if (value.isEmptyOrUndefinedOrNull()) return null;
    return value;
}

const MAX_WINDOW_SIZE = std.math.maxInt(i32);
const MAX_HEADER_TABLE_SIZE = std.math.maxInt(u32);
const MAX_STREAM_ID = std.math.maxInt(i32);
const MAX_FRAME_SIZE = std.math.maxInt(u24);
const DEFAULT_WINDOW_SIZE = std.math.maxInt(u16);
// Float versions for range validation before integer conversion
const MAX_WINDOW_SIZE_F64: f64 = @floatFromInt(MAX_WINDOW_SIZE);
const MAX_HEADER_TABLE_SIZE_F64: f64 = @floatFromInt(MAX_HEADER_TABLE_SIZE);
const MAX_FRAME_SIZE_F64: f64 = @floatFromInt(MAX_FRAME_SIZE);
// RFC 7541 Section 4.1: Each header entry has 32 bytes of overhead
// for the HPACK dynamic table entry structure
const HPACK_ENTRY_OVERHEAD = 32;
// Maximum number of custom settings (same as Node.js MAX_ADDITIONAL_SETTINGS)
const MAX_CUSTOM_SETTINGS = 10;
// Maximum custom setting ID (0xFFFF per RFC 7540)
const MAX_CUSTOM_SETTING_ID: f64 = 0xFFFF;

enum PaddingStrategy {
    none,
    aligned,
    max,
}

const FrameType = enum(u8) {
    HTTP_FRAME_DATA = 0x00,
    HTTP_FRAME_HEADERS = 0x01,
    HTTP_FRAME_PRIORITY = 0x02,
    HTTP_FRAME_RST_STREAM = 0x03,
    HTTP_FRAME_SETTINGS = 0x04,
    HTTP_FRAME_PUSH_PROMISE = 0x05,
    HTTP_FRAME_PING = 0x06,
    HTTP_FRAME_GOAWAY = 0x07,
    HTTP_FRAME_WINDOW_UPDATE = 0x08,
    HTTP_FRAME_CONTINUATION = 0x09, // RFC 7540 Section 6.10: Continues header block fragments
    HTTP_FRAME_ALTSVC = 0x0A, // https://datatracker.ietf.org/doc/html/rfc7838#section-7.2
    HTTP_FRAME_ORIGIN = 0x0C, // https://datatracker.ietf.org/doc/html/rfc8336#section-2
};

const PingFrameFlags = enum(u8) {
    ACK = 0x1,
};

const DataFrameFlags = enum(u8) {
    END_STREAM = 0x1,
    PADDED = 0x8,
};

const HeadersFrameFlags = enum(u8) {
    END_STREAM = 0x1,
    END_HEADERS = 0x4,
    PADDED = 0x8,
    PRIORITY = 0x20,
};

const SettingsFlags = enum(u8) {
    ACK = 0x1,
};

const ErrorCode = enum(u32) {
    NO_ERROR = 0x0,
    PROTOCOL_ERROR = 0x1,
    INTERNAL_ERROR = 0x2,
    FLOW_CONTROL_ERROR = 0x3,
    SETTINGS_TIMEOUT = 0x4,
    STREAM_CLOSED = 0x5,
    FRAME_SIZE_ERROR = 0x6,
    REFUSED_STREAM = 0x7,
    CANCEL = 0x8,
    COMPRESSION_ERROR = 0x9,
    CONNECT_ERROR = 0xa,
    ENHANCE_YOUR_CALM = 0xb,
    INADEQUATE_SECURITY = 0xc,
    HTTP_1_1_REQUIRED = 0xd,
    MAX_PENDING_SETTINGS_ACK = 0xe,
    _, // we can have unsupported extension/custom error codes types
};

const SettingsType = enum(u16) {
    SETTINGS_HEADER_TABLE_SIZE = 0x1,
    SETTINGS_ENABLE_PUSH = 0x2,
    SETTINGS_MAX_CONCURRENT_STREAMS = 0x3,
    SETTINGS_INITIAL_WINDOW_SIZE = 0x4,
    SETTINGS_MAX_FRAME_SIZE = 0x5,
    SETTINGS_MAX_HEADER_LIST_SIZE = 0x6,

    // non standard extension settings here (we still dont support this ones)
    SETTINGS_ENABLE_CONNECT_PROTOCOL = 0x8,
    SETTINGS_NO_RFC7540_PRIORITIES = 0x9,
    _, // we can have more unsupported extension settings types
};

inline fn u32FromBytes(src: []const u8): u32 {
    bun.debugAssert(src.len == 4);
    return std.mem.readInt(u32, src[0..4], .big);
}

@packed(u32) struct UInt31WithReserved {
    reserved: bool = false,
    uint31: u31 = 0,

    const log = Output.scoped(.UInt31WithReserved, .visible);

    static inline fn @"from"(value: u32): UInt31WithReserved {
        return { uint31: @truncate(value & 0x7fffffff), reserved: (value & 0x80000000) != 0 };
    }

    static inline fn init(value: u31, reserved: bool): UInt31WithReserved {
        return { uint31: value, reserved: reserved };
    }

    static inline fn toUInt32(value: UInt31WithReserved): u32 {
        return @bitCast(value);
    }

    static inline fn fromBytes(src: []const u8): UInt31WithReserved {
        const value: u32 = u32FromBytes(src);
        return { uint31: @truncate(value & 0x7fffffff), reserved: (value & 0x80000000) != 0 };
    }

    inline fn write(comptime Writer: type, writer: Writer): bool {
        let value: u32 = @as(u32, _.uint31);
        if (_.reserved) {
            value |= 0x80000000;
        }

        value = @byteSwap(value);

        return (writer.write(std.mem.asBytes(&value)) catch 0) != 0;
    }
}

@packed(u40) struct StreamPriority {
    streamIdentifier: u32 = 0,
    weight: u8 = 0,

    const byteSize: usize = 5;
    inline fn write(*_, comptime Writer: type, writer: Writer): bool {
        let swap = _.*;
        std.mem.byteSwapAllFields(StreamPriority, &swap);

        return (writer.write(std.mem.asBytes(&swap)[0..StreamPriority.byteSize]) catch 0) != 0;
    }

    static inline fn @"from"(dst: *StreamPriority, src: []const u8) {
        @memcpy(std.mem.asBytes(dst), src);
        std.mem.byteSwapAllFields(StreamPriority, dst);
    }
}

@packed(u72) struct FrameHeader {
    length: u24 = 0,
    @"type": u8 = @intFromEnum(FrameType.HTTP_FRAME_SETTINGS),
    flags: u8 = 0,
    streamIdentifier: u32 = 0,

    const byteSize: usize = 9;
    inline fn write(*_, comptime Writer: type, writer: Writer): bool {
        let swap = _.*;
        std.mem.byteSwapAllFields(FrameHeader, &swap);

        return (writer.write(std.mem.asBytes(&swap)[0..FrameHeader.byteSize]) catch 0) != 0;
    }

    static inline fn @"from"(dst: *FrameHeader, src: []const u8, offset: usize, comptime end: bool) {
        @memcpy(std.mem.asBytes(dst)[offset .. src.len + offset], src);
        if (comptime end) {
            std.mem.byteSwapAllFields(FrameHeader, dst);
        }
    }
}

@packed(u48) struct SettingsPayloadUnit {
    @"type": u16,
    value: u32,
    const byteSize: usize = 6;
    static inline fn @"from"(dst: *SettingsPayloadUnit, src: []const u8, offset: usize, comptime end: bool) {
        @memcpy(std.mem.asBytes(dst)[offset .. src.len + offset], src);
        if (comptime end) {
            std.mem.byteSwapAllFields(SettingsPayloadUnit, dst);
        }
    }
}

@packed(u336) struct FullSettingsPayload {
    _headerTableSizeType: u16 = @intFromEnum(SettingsType.SETTINGS_HEADER_TABLE_SIZE),
    headerTableSize: u32 = 4096,
    _enablePushType: u16 = @intFromEnum(SettingsType.SETTINGS_ENABLE_PUSH),
    enablePush: u32 = 1,
    _maxConcurrentStreamsType: u16 = @intFromEnum(SettingsType.SETTINGS_MAX_CONCURRENT_STREAMS),
    maxConcurrentStreams: u32 = 4294967295,
    _initialWindowSizeType: u16 = @intFromEnum(SettingsType.SETTINGS_INITIAL_WINDOW_SIZE),
    initialWindowSize: u32 = 65535,
    _maxFrameSizeType: u16 = @intFromEnum(SettingsType.SETTINGS_MAX_FRAME_SIZE),
    maxFrameSize: u32 = 16384,
    _maxHeaderListSizeType: u16 = @intFromEnum(SettingsType.SETTINGS_MAX_HEADER_LIST_SIZE),
    maxHeaderListSize: u32 = 65535,
    _enableConnectProtocolType: u16 = @intFromEnum(SettingsType.SETTINGS_ENABLE_CONNECT_PROTOCOL),
    enableConnectProtocol: u32 = 0,
    const byteSize: usize = 42;
    fn toJS(*_, globalObject: *jsc.JSGlobalObject): jsc.JSValue {
        let result = JSValue.createEmptyObject(globalObject, 8);
        result.put(globalObject, jsc.ZigString.static("headerTableSize"), jsc.JSValue.jsNumber(_.headerTableSize));
        result.put(globalObject, jsc.ZigString.static("enablePush"), jsc.JSValue.jsBoolean(_.enablePush > 0));
        result.put(globalObject, jsc.ZigString.static("maxConcurrentStreams"), jsc.JSValue.jsNumber(_.maxConcurrentStreams));
        result.put(globalObject, jsc.ZigString.static("initialWindowSize"), jsc.JSValue.jsNumber(_.initialWindowSize));
        result.put(globalObject, jsc.ZigString.static("maxFrameSize"), jsc.JSValue.jsNumber(_.maxFrameSize));
        result.put(globalObject, jsc.ZigString.static("maxHeaderListSize"), jsc.JSValue.jsNumber(_.maxHeaderListSize));
        result.put(globalObject, jsc.ZigString.static("maxHeaderSize"), jsc.JSValue.jsNumber(_.maxHeaderListSize));
        result.put(globalObject, jsc.ZigString.static("enableConnectProtocol"), jsc.JSValue.jsBoolean(_.enableConnectProtocol > 0));
        return result;
    }

    fn updateWith(*_, option: SettingsPayloadUnit) {
        switch (@as(SettingsType, @enumFromInt(option.@"type"))) {
            .SETTINGS_HEADER_TABLE_SIZE => _.headerTableSize = option.value,
            .SETTINGS_ENABLE_PUSH => _.enablePush = option.value,
            .SETTINGS_MAX_CONCURRENT_STREAMS => _.maxConcurrentStreams = option.value,
            .SETTINGS_INITIAL_WINDOW_SIZE => _.initialWindowSize = option.value,
            .SETTINGS_MAX_FRAME_SIZE => _.maxFrameSize = option.value,
            .SETTINGS_MAX_HEADER_LIST_SIZE => _.maxHeaderListSize = option.value,
            .SETTINGS_ENABLE_CONNECT_PROTOCOL => _.enableConnectProtocol = option.value,
            else => {},
        }
    }
    fn write(*_, comptime Writer: type, writer: Writer): bool {
        let swap = _.*;

        std.mem.byteSwapAllFields(FullSettingsPayload, &swap);
        return (writer.write(std.mem.asBytes(&swap)[0..FullSettingsPayload.byteSize]) catch 0) != 0;
    }
}

const ValidResponsePseudoHeaders = bun.ComptimeStringMap(void, .{
    .{":status"},
});

const ValidRequestPseudoHeaders = bun.ComptimeStringMap(void, .{
    .{":method"},
    .{":authority"},
    .{":scheme"},
    .{":path"},
    .{":protocol"},
});

const SingleValueHeaders = bun.ComptimeStringMap(void, .{
    .{":status"},
    .{":method"},
    .{":authority"},
    .{":scheme"},
    .{":path"},
    .{":protocol"},
    .{"access-control-allow-credentials"},
    .{"access-control-max-age"},
    .{"access-control-request-method"},
    .{"age"},
    .{"authorization"},
    .{"content-encoding"},
    .{"content-language"},
    .{"content-length"},
    .{"content-location"},
    .{"content-md5"},
    .{"content-range"},
    .{"content-type"},
    .{"date"},
    .{"dnt"},
    .{"etag"},
    .{"expires"},
    .{"from"},
    .{"host"},
    .{"if-match"},
    .{"if-modified-since"},
    .{"if-none-match"},
    .{"if-range"},
    .{"if-unmodified-since"},
    .{"last-modified"},
    .{"location"},
    .{"max-forwards"},
    .{"proxy-authorization"},
    .{"range"},
    .{"referer"},
    .{"retry-after"},
    .{"tk"},
    .{"upgrade-insecure-requests"},
    .{"user-agent"},
    .{"x-content-type-options"},
});

pub fn jsGetUnpackedSettings(globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    jsc.markBinding(@src());
    let settings: FullSettingsPayload = {};

    const args_list = callframe.arguments_old(1);
    if (args_list.len < 1) {
        return settings.toJS(globalObject);
    }

    const data_arg = args_list.ptr[0];

    if (array_buffer := data_arg.asArrayBuffer(globalObject)) {
        let payload = array_buffer.byteSlice();
        const settingByteSize = SettingsPayloadUnit.byteSize;
        if (payload.len < settingByteSize || payload.len % settingByteSize != 0) {
            return globalObject.throw("Expected buf to be a Buffer of at least 6 bytes and a multiple of 6 bytes", {});
        }

        let i: usize = 0;
        while (i < payload.len) : (i += settingByteSize) {
            let unit: SettingsPayloadUnit = undefined;
            SettingsPayloadUnit.@"from"(&unit, payload[i .. i + settingByteSize], 0, true);
            settings.updateWith(unit);
        }
        return settings.toJS(globalObject);
    } else if (!data_arg.isEmptyOrUndefinedOrNull()) {
        return globalObject.throw("Expected buf to be a Buffer", {});
    } else {
        return settings.toJS(globalObject);
    }
}

pub fn jsAssertSettings(globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    const args_list = callframe.arguments_old(1);
    if (args_list.len < 1) {
        return globalObject.throw("Expected settings to be a object", {});
    }

    if (args_list.len > 0 && !args_list.ptr[0].isEmptyOrUndefinedOrNull()) {
        const options = args_list.ptr[0];
        if (!options.isObject()) {
            return globalObject.throw("Expected settings to be a object", {});
        }

        if (headerTableSize := options.get(globalObject, "headerTableSize")?) {
            if (headerTableSize.isNumber()) {
                const value = headerTableSize.asNumber();
                if (value < 0 || value > MAX_HEADER_TABLE_SIZE_F64) {
                    return globalObject.ERR(.HTTP2_INVALID_SETTING_VALUE_RangeError, "Expected headerTableSize to be a number between 0 and 2^32-1", {}).throw();
                }
            } else if (!headerTableSize.isEmptyOrUndefinedOrNull()) {
                return globalObject.ERR(.HTTP2_INVALID_SETTING_VALUE_RangeError, "Expected headerTableSize to be a number", {}).throw();
            }
        }

        if (enablePush := options.get(globalObject, "enablePush")?) {
            if (!enablePush.isBoolean() && !enablePush.isUndefined()) {
                return globalObject.ERR(.HTTP2_INVALID_SETTING_VALUE, "Expected enablePush to be a boolean", {}).throw();
            }
        }

        if (initialWindowSize := options.get(globalObject, "initialWindowSize")?) {
            if (initialWindowSize.isNumber()) {
                const value = initialWindowSize.asNumber();
                if (value < 0 || value > MAX_WINDOW_SIZE_F64) {
                    return globalObject.ERR(.HTTP2_INVALID_SETTING_VALUE_RangeError, "Expected initialWindowSize to be a number between 0 and 2^32-1", {}).throw();
                }
            } else if (!initialWindowSize.isEmptyOrUndefinedOrNull()) {
                return globalObject.ERR(.HTTP2_INVALID_SETTING_VALUE_RangeError, "Expected initialWindowSize to be a number", {}).throw();
            }
        }

        if (maxFrameSize := options.get(globalObject, "maxFrameSize")?) {
            if (maxFrameSize.isNumber()) {
                const value = maxFrameSize.asNumber();
                if (value < 16384 || value > MAX_FRAME_SIZE_F64) {
                    return globalObject.ERR(.HTTP2_INVALID_SETTING_VALUE_RangeError, "Expected maxFrameSize to be a number between 16,384 and 2^24-1", {}).throw();
                }
            } else if (!maxFrameSize.isEmptyOrUndefinedOrNull()) {
                return globalObject.ERR(.HTTP2_INVALID_SETTING_VALUE_RangeError, "Expected maxFrameSize to be a number", {}).throw();
            }
        }

        if (maxConcurrentStreams := options.get(globalObject, "maxConcurrentStreams")?) {
            if (maxConcurrentStreams.isNumber()) {
                const value = maxConcurrentStreams.asNumber();
                if (value < 0 || value > MAX_HEADER_TABLE_SIZE_F64) {
                    return globalObject.ERR(.HTTP2_INVALID_SETTING_VALUE_RangeError, "Expected maxConcurrentStreams to be a number between 0 and 2^32-1", {}).throw();
                }
            } else if (!maxConcurrentStreams.isEmptyOrUndefinedOrNull()) {
                return globalObject.ERR(.HTTP2_INVALID_SETTING_VALUE_RangeError, "Expected maxConcurrentStreams to be a number", {}).throw();
            }
        }

        if (maxHeaderListSize := options.get(globalObject, "maxHeaderListSize")?) {
            if (maxHeaderListSize.isNumber()) {
                const value = maxHeaderListSize.asNumber();
                if (value < 0 || value > MAX_HEADER_TABLE_SIZE_F64) {
                    return globalObject.ERR(.HTTP2_INVALID_SETTING_VALUE_RangeError, "Expected maxHeaderListSize to be a number between 0 and 2^32-1", {}).throw();
                }
            } else if (!maxHeaderListSize.isEmptyOrUndefinedOrNull()) {
                return globalObject.ERR(.HTTP2_INVALID_SETTING_VALUE_RangeError, "Expected maxHeaderListSize to be a number", {}).throw();
            }
        }

        if (maxHeaderSize := options.get(globalObject, "maxHeaderSize")?) {
            if (maxHeaderSize.isNumber()) {
                const value = maxHeaderSize.asNumber();
                if (value < 0 || value > MAX_HEADER_TABLE_SIZE_F64) {
                    return globalObject.ERR(.HTTP2_INVALID_SETTING_VALUE_RangeError, "Expected maxHeaderSize to be a number between 0 and 2^32-1", {}).throw();
                }
            } else if (!maxHeaderSize.isEmptyOrUndefinedOrNull()) {
                return globalObject.ERR(.HTTP2_INVALID_SETTING_VALUE_RangeError, "Expected maxHeaderSize to be a number", {}).throw();
            }
        }
    }
    return JSValue.js_undefined;
}

pub fn jsGetPackedSettings(globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    let settings: FullSettingsPayload = {};
    const args_list = callframe.arguments_old(1);

    if (args_list.len > 0 && !args_list.ptr[0].isEmptyOrUndefinedOrNull()) {
        const options = args_list.ptr[0];

        if (!options.isObject()) {
            return globalObject.throw("Expected settings to be a object", {});
        }

        if (headerTableSize := options.get(globalObject, "headerTableSize")?) {
            if (headerTableSize.isNumber()) {
                const headerTableSizeValue = headerTableSize.toInt32();
                if (headerTableSizeValue > MAX_HEADER_TABLE_SIZE || headerTableSizeValue < 0) {
                    return globalObject.throw("Expected headerTableSize to be a number between 0 and 2^32-1", {});
                }
                settings.headerTableSize = @intCast(headerTableSizeValue);
            } else if (!headerTableSize.isEmptyOrUndefinedOrNull()) {
                return globalObject.throw("Expected headerTableSize to be a number", {});
            }
        }

        if (enablePush := options.get(globalObject, "enablePush")?) {
            if (enablePush.isBoolean()) {
                settings.enablePush = if (enablePush.asBoolean()) 1 else 0;
            } else if (!enablePush.isEmptyOrUndefinedOrNull()) {
                return globalObject.throw("Expected enablePush to be a boolean", {});
            }
        }

        if (initialWindowSize := options.get(globalObject, "initialWindowSize")?) {
            if (initialWindowSize.isNumber()) {
                const initialWindowSizeValue = initialWindowSize.toInt32();
                if (initialWindowSizeValue > MAX_HEADER_TABLE_SIZE || initialWindowSizeValue < 0) {
                    return globalObject.throw("Expected initialWindowSize to be a number between 0 and 2^32-1", {});
                }
                settings.initialWindowSize = @intCast(initialWindowSizeValue);
            } else if (!initialWindowSize.isEmptyOrUndefinedOrNull()) {
                return globalObject.throw("Expected initialWindowSize to be a number", {});
            }
        }

        if (maxFrameSize := options.get(globalObject, "maxFrameSize")?) {
            if (maxFrameSize.isNumber()) {
                const maxFrameSizeValue = maxFrameSize.toInt32();
                if (maxFrameSizeValue > MAX_FRAME_SIZE || maxFrameSizeValue < 16384) {
                    return globalObject.throw("Expected maxFrameSize to be a number between 16,384 and 2^24-1", {});
                }
                settings.maxFrameSize = @intCast(maxFrameSizeValue);
            } else if (!maxFrameSize.isEmptyOrUndefinedOrNull()) {
                return globalObject.throw("Expected maxFrameSize to be a number", {});
            }
        }

        if (maxConcurrentStreams := options.get(globalObject, "maxConcurrentStreams")?) {
            if (maxConcurrentStreams.isNumber()) {
                const maxConcurrentStreamsValue = maxConcurrentStreams.toInt32();
                if (maxConcurrentStreamsValue > MAX_HEADER_TABLE_SIZE || maxConcurrentStreamsValue < 0) {
                    return globalObject.throw("Expected maxConcurrentStreams to be a number between 0 and 2^32-1", {});
                }
                settings.maxConcurrentStreams = @intCast(maxConcurrentStreamsValue);
            } else if (!maxConcurrentStreams.isEmptyOrUndefinedOrNull()) {
                return globalObject.throw("Expected maxConcurrentStreams to be a number", {});
            }
        }

        if (maxHeaderListSize := options.get(globalObject, "maxHeaderListSize")?) {
            if (maxHeaderListSize.isNumber()) {
                const maxHeaderListSizeValue = maxHeaderListSize.toInt32();
                if (maxHeaderListSizeValue > MAX_HEADER_TABLE_SIZE || maxHeaderListSizeValue < 0) {
                    return globalObject.throw("Expected maxHeaderListSize to be a number between 0 and 2^32-1", {});
                }
                settings.maxHeaderListSize = @intCast(maxHeaderListSizeValue);
            } else if (!maxHeaderListSize.isEmptyOrUndefinedOrNull()) {
                return globalObject.throw("Expected maxHeaderListSize to be a number", {});
            }
        }

        if (maxHeaderSize := options.get(globalObject, "maxHeaderSize")?) {
            if (maxHeaderSize.isNumber()) {
                const maxHeaderSizeValue = maxHeaderSize.toInt32();
                if (maxHeaderSizeValue > MAX_HEADER_TABLE_SIZE || maxHeaderSizeValue < 0) {
                    return globalObject.throw("Expected maxHeaderSize to be a number between 0 and 2^32-1", {});
                }
                settings.maxHeaderListSize = @intCast(maxHeaderSizeValue);
            } else if (!maxHeaderSize.isEmptyOrUndefinedOrNull()) {
                return globalObject.throw("Expected maxHeaderSize to be a number", {});
            }
        }
    }

    std.mem.byteSwapAllFields(FullSettingsPayload, &settings);
    const bytes = std.mem.asBytes(&settings)[0..FullSettingsPayload.byteSize];
    const binary_type: BinaryType = .Buffer;
    return binary_type.toJS(bytes, globalObject);
}

struct Handlers {
    binary_type: BinaryType = .Buffer,

    vm: *jsc.VirtualMachine,
    globalObject: *jsc.JSGlobalObject,

    fn callEventHandler(*_, comptime event: H2FrameParser.js.gc, thisValue: JSValue, context: jsc.JSValue, data: []const JSValue): bool {
        const _callback_opt = event.get(thisValue);
        if (_callback_opt == null) return false;
        const callback = _callback_opt.?;

        _.vm.eventLoop().runCallback(callback, _.globalObject, context, data);
        return true;
    }

    fn callWriteCallback(*_, callback: jsc.JSValue, data: []const JSValue): bool {
        if (!callback.isCallable()) return false;
        _.vm.eventLoop().runCallback(callback, _.globalObject, .js_undefined, data);
        return true;
    }

    fn callEventHandlerWithResult(*_, comptime event: H2FrameParser.js.gc, thisValue: JSValue, data: []const JSValue): JSValue {
        const _callback_opt = event.get(thisValue);
        if (_callback_opt == null) return .zero;
        const callback = _callback_opt.?;

        return _.vm.eventLoop().runCallbackWithResult(callback, _.globalObject, thisValue, data);
    }

    static fn fromJS(globalObject: *jsc.JSGlobalObject, opts: jsc.JSValue, thisValue: jsc.JSValue): bun.JSError!Handlers {
        let handlers = Handlers{
            vm: globalObject.bunVM(),
            globalObject: globalObject,
        };

        if (opts.isEmptyOrUndefinedOrNull() || opts.isBoolean() || !opts.isObject()) {
            return globalObject.throwInvalidArguments("Expected \"handlers\" to be an object", {});
        }

        const pairs = .{
            .{ "onStreamStart", "streamStart" },
            .{ "onStreamHeaders", "streamHeaders" },
            .{ "onStreamEnd", "streamEnd" },
            .{ "onStreamData", "streamData" },
            .{ "onStreamError", "streamError" },
            .{ "onRemoteSettings", "remoteSettings" },
            .{ "onLocalSettings", "localSettings" },
            .{ "onWantTrailers", "wantTrailers" },
            .{ "onPing", "ping" },
            .{ "onEnd", "end" },
            // .{ "onError", "error" } using fastGet(.error) now
            .{ "onGoAway", "goaway" },
            .{ "onAborted", "aborted" },
            .{ "onWrite", "write" },
            .{ "onAltSvc", "altsvc" },
            .{ "onOrigin", "origin" },
            .{ "onFrameError", "frameError" },
        };

        inline for (pair of pairs) {
            if (callback_value := opts.getTruthy(globalObject, pair[1])?) {
                if (!callback_value.isCell() || !callback_value.isCallable()) {
                    return globalObject.throwInvalidArguments("Expected \"{s}\" callback to be a function", .{pair[1]});
                }

                @field(H2FrameParser.js.gc, pair[0]).set(thisValue, globalObject, callback_value.withAsyncContextIfNeeded(globalObject));
            }
        }

        if (callback_value := opts.fastGet(globalObject, .@"error")?) {
            if (!callback_value.isCell() || !callback_value.isCallable()) {
                return globalObject.throwInvalidArguments("Expected \"error\" callback to be a function", {});
            }

            H2FrameParser.js.gc.onError.set(thisValue, globalObject, callback_value.withAsyncContextIfNeeded(globalObject));
        }

        // onWrite is required for duplex support or if more than 1 parser is attached to the same socket (unliked)
        if (H2FrameParser.js.gc.onWrite.get(thisValue) == .zero) {
            return globalObject.throwInvalidArguments("Expected at least \"write\" callback", {});
        }

        if (binary_type_value := opts.getTruthy(globalObject, "binaryType")?) {
            if (!binary_type_value.isString()) {
                return globalObject.throwInvalidArguments("Expected \"binaryType\" to be a string", {});
            }

            const _opt_614 = BinaryType.fromJSValue(globalObject, binary_type_value)?;
            if (_opt_614 == null) {
                return globalObject.throwInvalidArguments("Expected 'binaryType' to be 'ArrayBuffer', 'Uint8Array', or 'Buffer'", {});
            }
            handlers.binary_type = _opt_614.?;
        }

        return handlers;
    }
}

pub const H2FrameParserConstructor = H2FrameParser.js.getConstructor;

const h2_log = Output.scoped(.H2FrameParser, .visible);
const ENABLE_AUTO_CORK = false; // ENABLE CORK OPTIMIZATION
const ENABLE_ALLOCATOR_POOL = true; // ENABLE HIVE ALLOCATOR OPTIMIZATION
const FRAME_PLUS_SETTINGS_SIZE = FrameHeader.byteSize + FullSettingsPayload.byteSize;
const FRAME_PLUS_4 = FrameHeader.byteSize + 4;
const FRAME_PLUS_8 = FrameHeader.byteSize + 8;
const FRAME_PLUS_2 = FrameHeader.byteSize + 2;
const PREFACE_PLUS_SETTINGS_SIZE = 24 + FrameHeader.byteSize + FullSettingsPayload.byteSize;
const FRAME_PLUS_16384 = FrameHeader.byteSize + 16384;
const FRAME_HEADER_SIZE = FrameHeader.byteSize;
let empty_js_values: [0]jsc.JSValue = undefined;
const MAX_BUFFER_SIZE = 32768;
const SINGLE_VALUE_HEADERS_COUNT = SingleValueHeaders.keys().len;
const H2FrameParserHiveAllocator = bun.HiveArray(H2FrameParser, 256).Fallback;

@tls let CORK_BUFFER: [16386]u8 = undefined;
@tls let CORK_OFFSET: u16 = 0;
@tls let CORKED_H2: (*H2FrameParser)? = null;
@tls let h2_pool: (*H2FrameParserHiveAllocator)? = null;
@tls let shared_request_buffer: [16384]u8 = undefined;

/// The streams hashmap may mutate when growing we use this when we need to make sure its safe to iterate over it
pub struct StreamResumableIterator {
    parser: *H2FrameParser,
    index: u32 = 0,
    static fn init(parser: *H2FrameParser): StreamResumableIterator {
        return { index: 0, parser: parser };
    }
    fn next(*_): (*Stream)? {
        let it = _.parser.streams.iterator();
        if (it.index > it.hm.capacity() || _.index > it.hm.capacity()) return null;
        // resume the iterator from the same index if possible
        it.index = _.index;
        while (item := it.next()) {
            _.index = it.index;
            return item.value_ptr.*;
        }
        _.index = it.index;
        return null;
    }
}

pub enum FlushState {
    no_action,
    flushed,
    backpressure,
}

enum StreamState: u8 {
    IDLE = 1,
    RESERVED_LOCAL = 3,
    RESERVED_REMOTE = 4,
    OPEN = 2,
    HALF_CLOSED_LOCAL = 5,
    HALF_CLOSED_REMOTE = 6,
    CLOSED = 7,
}

struct SignalRef {
    signal: *jsc.WebCore.AbortSignal,
    parser: *H2FrameParser,
    stream_id: u32,

    pub const new = bun.TrivialNew(SignalRef);

    fn isAborted(*_): bool {
        return _.signal.aborted();
    }

    fn abortListener(*_, reason: JSValue) {
        h2_log("abortListener", {});
        reason.ensureStillAlive();
        const _stream_opt = _.parser.streams.get(_.stream_id);
        if (_stream_opt == null) return;
        const stream = _stream_opt.?;
        if (stream.state != .CLOSED) {
            _.parser.abortStream(stream, Bun__wrapAbortError(_.parser.globalThis, reason));
        }
    }

    fn deinit(*_) {
        _.signal.detach(_);
        _.parser.deref();
        bun.destroy(_);
    }
}

struct PendingFrame {
    end_stream: bool = false, // end_stream flag
    len: u32 = 0, // actually payload size
    offset: u32 = 0, // offset into the buffer (if partial flush due to flow control)
    buffer: []u8 = "", // allocated buffer if len > 0
    callback: jsc.Strong.Optional = .empty, // JSCallback for done

    fn deinit(*_, allocator: Allocator) {
        if (_.buffer.len > 0) {
            allocator.free(_.buffer);
            _.buffer = "";
        }
        _.len = 0;
        _.callback.deinit();
    }

    fn slice(): []u8 {
        return _.buffer[_.offset.._.len];
    }
}

struct PendingQueue {
    data: std.ArrayListUnmanaged(PendingFrame) = {},
    front: usize = 0,
    len: usize = 0,

    fn deinit(*_, allocator: Allocator) {
        _.front = 0;
        _.len = 0;
        let data = _.data;
        if (data.capacity > 0) {
            _.data = {};
            data.clearAndFree(allocator);
        }
    }

    fn enqueue(*_, value: PendingFrame, allocator: Allocator) {
        bun.handleOom(_.data.append(allocator, value));
        _.len += 1;
        h2_log("PendingQueue.enqueue {}", .{_.len});
    }

    fn peek(*_): (*PendingFrame)? {
        if (_.len == 0) {
            return null;
        }
        return &_.data.items[0];
    }

    fn peekLast(*_): (*PendingFrame)? {
        if (_.len == 0) {
            return null;
        }
        return &_.data.items[_.data.items.len - 1];
    }

    fn slice(*_): []PendingFrame {
        if (_.len == 0) return [];
        return _.data.items[_.front..][0.._.len];
    }
    fn peekFront(*_): (*PendingFrame)? {
        if (_.len == 0) return null;
        return &_.data.items[_.front];
    }
    fn dequeue(*_): PendingFrame? {
        if (_.len == 0) {
            h2_log("PendingQueue.dequeue null", {});
            return null;
        }
        const value = _.data.items[_.front];
        _.data.items[_.front] = {};
        _.len -= 1;
        if (_.len == 0) {
            _.front = 0;
            _.data.clearRetainingCapacity();
        } else {
            _.front += 1;
        }
        h2_log("PendingQueue.dequeue {}", .{_.len});

        return value;
    }

    fn isEmpty(*_): bool {
        return _.len == 0;
    }
}

struct Stream {
    id: u32 = 0,
    state: StreamState = .IDLE,
    jsContext: jsc.Strong.Optional = .empty,
    waitForTrailers: bool = false,
    closeAfterDrain: bool = false,
    endAfterHeaders: bool = false,
    isWaitingMoreHeaders: bool = false,
    padding: u8? = null,
    paddingStrategy: PaddingStrategy = .none,
    rstCode: u32 = 0,
    streamDependency: u32 = 0,
    exclusive: bool = false,
    weight: u16 = 36,
    // current window size for the stream
    windowSize: u64 = 65535,
    // used window size for the stream
    usedWindowSize: u64 = 0,
    // remote window size for the stream
    remoteWindowSize: u64,
    // remote used window size for the stream
    remoteUsedWindowSize: u64 = 0,
    signal: (*SignalRef)? = null,

    // when we have backpressure we queue the data e round robin the Streams
    dataFrameQueue: PendingQueue,

    // Stream is heap-allocated and stored by pointer in the streams map so that
    // *Stream stays valid across re-entrant JS callbacks that may insert into
    // (and rehash) the map. Lifetime == H2FrameParser lifetime; freed in deinit().
    pub const new = bun.TrivialNew(Stream);





    fn getPadding(
        *_,
        frameLen: usize,
        maxLen: usize,
    ): u8 {
        switch (_.paddingStrategy) {
            .none => return 0,
            .aligned => {
                const diff = (frameLen + 9) % 8;
                // already multiple of 8
                if (diff == 0) return 0;

                let paddedLen = frameLen + (8 - diff);
                // limit to maxLen
                paddedLen = @min(maxLen, paddedLen);
                return @min(paddedLen -| frameLen, 255);
            },
            .max => return @min(maxLen -| frameLen, 255),
        }
    }
    fn flushQueue(*_, client: *H2FrameParser, written: *usize): FlushState {
        if (_.canSendData()) {
            // try to flush one frame
            if (frame := _.dataFrameQueue.peekFront()) {
                const no_backpressure = brk: {
                    let fq_guard = FlushQueueGuard{
                        stream: _,
                        client: client,
                        is_flow_control_limited: false,
                    };

                    const writer = client.toWriter();

                    if (frame.len == 0) {

                        // flush a zero payload frame
                        let dataHeader: FrameHeader = {
                            @"type": @intFromEnum(FrameType.HTTP_FRAME_DATA),
                            flags: if (frame.end_stream && !_.waitForTrailers) @intFromEnum(DataFrameFlags.END_STREAM) else 0,
                            streamIdentifier: @intCast(_.id),
                            length: 0,
                        };
                        break :brk dataHeader.write(@TypeOf(writer), writer);
                    } else {
                        const frame_slice = frame.slice();
                        const max_size = @min(@min(frame_slice.len, _.remoteWindowSize -| _.remoteUsedWindowSize, client.remoteWindowSize -| client.remoteUsedWindowSize), MAX_PAYLOAD_SIZE_WITHOUT_FRAME);
                        if (max_size == 0) {
                            fq_guard.is_flow_control_limited = true;
                            h2_log("dataFrame flow control limited {} {} {} {} {} {}", .{ frame_slice.len, _.remoteWindowSize, _.remoteUsedWindowSize, client.remoteWindowSize, client.remoteUsedWindowSize, max_size });
                            // we are flow control limited lets return backpressure if is limited in the connection so we short circuit the flush
                            return if (client.remoteWindowSize == client.remoteUsedWindowSize) .backpressure else .no_action;
                        }
                        if (max_size < frame_slice.len) {
                            fq_guard.is_flow_control_limited = true;
                            // we need to break the frame into smaller chunks
                            frame.offset += @as(u32, @intCast(max_size));
                            const able_to_send = frame_slice[0..max_size];
                            client.queuedDataSize -= able_to_send.len;
                            written.* += able_to_send.len;

                            const padding = _.getPadding(able_to_send.len, max_size - 1);
                            const payload_size = able_to_send.len + (if (padding != 0) @as(usize, @intCast(padding)) + 1 else 0);
                            h2_log("padding: {d} size: {d} max_size: {d} payload_size: {d}", .{ padding, able_to_send.len, max_size, payload_size });
                            _.remoteUsedWindowSize += payload_size;
                            client.remoteUsedWindowSize += payload_size;

                            let flags: u8 = 0; // we ignore end_stream for now because we know we have more data to send
                            if (padding != 0) {
                                flags |= @intFromEnum(DataFrameFlags.PADDED);
                            }
                            let dataHeader: FrameHeader = {
                                @"type": @intFromEnum(FrameType.HTTP_FRAME_DATA),
                                flags: flags,
                                streamIdentifier: @intCast(_.id),
                                length: @intCast(payload_size),
                            };
                            * = dataHeader.write(@TypeOf(writer), writer);
                            if (padding != 0) {
                                let buffer = shared_request_buffer[0..];
                                bun.memmove(buffer[1..][0..able_to_send.len], able_to_send);
                                buffer[0] = padding;
                                break :brk (writer.write(buffer[0..payload_size]) catch 0) != 0;
                            } else {
                                break :brk (writer.write(able_to_send) catch 0) != 0;
                            }
                        } else {

                            // flush with some payload
                            client.queuedDataSize -= frame_slice.len;
                            written.* += frame_slice.len;

                            const padding = _.getPadding(frame_slice.len, max_size - 1);
                            const payload_size = frame_slice.len + (if (padding != 0) @as(usize, @intCast(padding)) + 1 else 0);
                            h2_log("padding: {d} size: {d} max_size: {d} payload_size: {d}", .{ padding, frame_slice.len, max_size, payload_size });
                            _.remoteUsedWindowSize += payload_size;
                            client.remoteUsedWindowSize += payload_size;
                            let flags: u8 = if (frame.end_stream && !_.waitForTrailers) @intFromEnum(DataFrameFlags.END_STREAM) else 0;
                            if (padding != 0) {
                                flags |= @intFromEnum(DataFrameFlags.PADDED);
                            }
                            let dataHeader: FrameHeader = {
                                @"type": @intFromEnum(FrameType.HTTP_FRAME_DATA),
                                flags: flags,
                                streamIdentifier: @intCast(_.id),
                                length: @intCast(payload_size),
                            };
                            * = dataHeader.write(@TypeOf(writer), writer);
                            if (padding != 0) {
                                let buffer = shared_request_buffer[0..];
                                bun.memmove(buffer[1..][0..frame_slice.len], frame_slice);
                                buffer[0] = padding;
                                break :brk (writer.write(buffer[0..payload_size]) catch 0) != 0;
                            } else {
                                break :brk (writer.write(frame_slice) catch 0) != 0;
                            }
                        }
                    }
                };

                return if (no_backpressure) .flushed else .backpressure;
            }
        }
        // empty or cannot send data
        return .no_action;
    }

    fn queueFrame(*_, client: *H2FrameParser, bytes: []const u8, callback: jsc.JSValue, end_stream: bool) {
        const globalThis = client.globalThis;

        if (last_frame := _.dataFrameQueue.peekLast()) {
            if (bytes.len == 0) {
                // just merge the end_stream
                last_frame.end_stream = end_stream;
                // we can only hold 1 callback at a time so we conclude the last one, and keep the last one as pending
                // this is fine is like a per-stream CORKING in a frame level
                if (old_callback := last_frame.callback.get()) {
                    client.dispatchWriteCallback(old_callback);
                    last_frame.callback.deinit();
                }
                last_frame.callback = .create(callback, globalThis);
                return;
            }
            if (last_frame.len == 0) {
                // we have an empty frame with means we can just use this frame with a new buffer
                last_frame.buffer = bun.handleOom(client.allocator.alloc(u8, MAX_PAYLOAD_SIZE_WITHOUT_FRAME));
            }
            const max_size = MAX_PAYLOAD_SIZE_WITHOUT_FRAME;
            const remaining = max_size - last_frame.len;
            if (remaining > 0) {
                // ok we can cork frames
                const consumed_len = @min(remaining, bytes.len);
                const merge = bytes[0..consumed_len];
                @memcpy(last_frame.buffer[last_frame.len .. last_frame.len + consumed_len], merge);
                last_frame.len += @as(u32, @intCast(consumed_len));
                h2_log("dataFrame merged {}", .{consumed_len});

                client.queuedDataSize += consumed_len;
                //lets fallthrough if we still have some data
                const more_data = bytes[consumed_len..];
                if (more_data.len == 0) {
                    last_frame.end_stream = end_stream;
                    // we can only hold 1 callback at a time so we conclude the last one, and keep the last one as pending
                    // this is fine is like a per-stream CORKING in a frame level
                    if (old_callback := last_frame.callback.get()) {
                        client.dispatchWriteCallback(old_callback);
                        last_frame.callback.deinit();
                    }
                    last_frame.callback = .create(callback, globalThis);
                    return;
                }
                // we keep the old callback because the new will be part of another frame
                return _.queueFrame(client, more_data, callback, end_stream);
            }
        }
        h2_log("{s} queued {} {}", .{ if (client.isServer) "server" else "client", bytes.len, end_stream });

        const frame: PendingFrame = {
            end_stream: end_stream,
            len: @intCast(bytes.len),
            // we need to clone this data to send it later
            buffer: if (bytes.len == 0) "" else bun.handleOom(client.allocator.alloc(u8, MAX_PAYLOAD_SIZE_WITHOUT_FRAME)),
            callback: if (callback.isCallable()) jsc.Strong.Optional.create(callback, globalThis) else .empty,
        };
        if (bytes.len > 0) {
            @memcpy(frame.buffer[0..bytes.len], bytes);
            client.globalThis.vm().reportExtraMemory(bytes.len);
        }
        h2_log("dataFrame enqueued {}", .{frame.len});
        _.dataFrameQueue.enqueue(frame, client.allocator);
        client.outboundQueueSize += 1;
        client.queuedDataSize += frame.len;
    }

    static fn init(streamIdentifier: u32, initialWindowSize: u32, remoteWindowSize: u32, paddingStrategy: PaddingStrategy): Stream {
        const stream = Stream{
            id: streamIdentifier,
            state: .OPEN,
            windowSize: initialWindowSize,
            remoteWindowSize: remoteWindowSize,
            usedWindowSize: 0,
            weight: 36,
            dataFrameQueue: {},
            paddingStrategy: paddingStrategy,
        };
        return stream;
    }

    /// Returns true if the stream can still receive data from the remote peer.
    /// Per RFC 7540 Section 5.1:
    /// - OPEN: both endpoints can send and receive
    /// - HALF_CLOSED_LOCAL: local sent END_STREAM, but can still receive from remote
    /// - HALF_CLOSED_REMOTE: remote sent END_STREAM, no more data to receive
    /// - CLOSED: stream is finished
    fn canReceiveData(*_): bool {
        return switch (_.state) {
            .IDLE, .RESERVED_LOCAL, .RESERVED_REMOTE, .OPEN, .HALF_CLOSED_LOCAL => true,
            .HALF_CLOSED_REMOTE, .CLOSED => false,
        };
    }

    fn canSendData(*_): bool {
        return switch (_.state) {
            .IDLE, .RESERVED_LOCAL, .RESERVED_REMOTE, .OPEN, .HALF_CLOSED_REMOTE => true,
            .HALF_CLOSED_LOCAL, .CLOSED => false,
        };
    }

    fn setContext(*_, value: JSValue, globalObject: *jsc.JSGlobalObject) {
        let context_guard = DeinitGuard<jsc.Strong.Optional>{ val: _.jsContext };
        _.jsContext = .create(value, globalObject);
    }

    fn getIdentifier(): JSValue {
        return _.jsContext.get() ?? jsc.JSValue.jsNumber(_.id);
    }

    fn attachSignal(*_, parser: *H2FrameParser, signal: *jsc.WebCore.AbortSignal) {
        // we need a stable pointer to know what signal points to what stream_id + parser
        let signal_ref = SignalRef.new({
            signal: signal,
            parser: parser,
            stream_id: _.id,
        });
        signal_ref.signal = signal.ref().listen(SignalRef, signal_ref, SignalRef.abortListener);
        //TODO: We should not need this ref counting here, since Parser owns Stream
        parser.ref();
        _.signal = signal_ref;
    }

    fn detachContext(*_) {
        _.jsContext.deinit();
    }

    fn cleanQueue(*_, client: *H2FrameParser, comptime finalizing: bool) {
        h2_log("cleanQueue len: {} front: {} outboundQueueSize: {}", .{ _.dataFrameQueue.len, _.dataFrameQueue.front, client.outboundQueueSize });

        let queue_guard = DeinitWithAllocGuard<PendingQueue>{ val: _.dataFrameQueue, alloc: client.allocator };
        _.dataFrameQueue = {};
        while (item := queue_guard.val.dequeue()) {
            let frame = item;
            const len = frame.slice().len;
            h2_log("dataFrame dropped {}", .{len});
            client.queuedDataSize -= len;
            if (!finalizing) {
                if (callback_value := frame.callback.get()) client.dispatchWriteCallback(callback_value);
            }
            frame.deinit(client.allocator);
            client.outboundQueueSize -= 1;
        }
    }
    /// this can be called multiple times
    fn freeResources(*_, client: *H2FrameParser, comptime finalizing: bool) {
        _.detachContext();
        _.cleanQueue(client, finalizing);
        if (signal := _.signal) {
            _.signal = null;
            signal.deinit();
        }
        // unsafe to ask GC to run if we are already inside GC
        if (!finalizing) {
            jsc.VirtualMachine.get().eventLoop().processGCTimer();
        }
    }
}

struct FramePayload {
    data: []const u8,
    end: usize,
}

struct DirectWriterStruct {
    writer: *H2FrameParser,
    fn write(data: []const u8): !usize {
        return if (_.writer.write(data)) data.len else 0;
    }
}

struct MemoryWriter {
    buffer: []u8,
    offset: usize = 0,
    fn slice(*_): []const u8 {
        return _.buffer[0.._.offset];
    }
    fn write(*_, data: []const u8): !usize {
        const pending = _.buffer[_.offset..];
        bun.debugAssert(pending.len >= data.len);
        @memcpy(pending[0..data.len], data);
        _.offset += data.len;
        return data.len;
    }
}

pub struct H2FrameParser {
    pub const log = h2_log;
    const Self = @This();
    pub const js = jsc.Codegen.JSH2FrameParser;
    pub const toJS = js.toJS;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;

    const RefCount = bun.ptr.RefCount(@This(), "ref_count", deinit, {});
    pub const ref = RefCount.ref;
    pub const deref = RefCount.deref;

    strong_this: jsc.JSRef = .empty(),
    globalThis: *jsc.JSGlobalObject,
    allocator: Allocator,
    handlers: Handlers,
    native_socket: BunSocket = { none: {} },
    localSettings: FullSettingsPayload = {},
    // only available after receiving settings or ACK
    remoteSettings: FullSettingsPayload? = null,
    // current frame being read
    currentFrame: FrameHeader? = null,
    // remaining bytes to read for the current frame
    remainingLength: i32 = 0,
    // buffer if more data is needed for the current frame
    readBuffer: MutableString,

    // local Window limits the download of data

    // current window size for the connection
    windowSize: u64 = DEFAULT_WINDOW_SIZE,
    // used window size for the connection
    usedWindowSize: u64 = 0,

    // remote Window limits the upload of data
    // remote window size for the connection
    remoteWindowSize: u64 = DEFAULT_WINDOW_SIZE,
    // remote used window size for the connection
    remoteUsedWindowSize: u64 = 0,

    maxHeaderListPairs: u32 = 128,
    maxRejectedStreams: u32 = 100,
    maxOutstandingSettings: u32 = 10,
    outstandingSettings: u32 = 0,
    rejectedStreams: u32 = 0,
    maxSessionMemory: u32 = 10, //this limit is in MB
    queuedDataSize: u64 = 0, // this is in bytes
    maxOutstandingPings: u64 = 10,
    outStandingPings: u64 = 0,
    maxSendHeaderBlockLength: u32 = 0,
    lastStreamID: u32 = 0,
    isServer: bool = false,
    prefaceReceivedLen: u8 = 0,
    // we buffer requests until we get the first settings ACK
    writeBuffer: bun.ByteList = {},
    writeBufferOffset: usize = 0,
    // TODO: this will be removed when I re-add header and data priorization
    outboundQueueSize: usize = 0,

    streams: bun.U32HashMap(*Stream),

    hpack: (*lshpack.HPACK)? = null,

    has_nonnative_backpressure: bool = false,
    ref_count: RefCount,

    auto_flusher: AutoFlusher = {},
    paddingStrategy: PaddingStrategy = .none,


    /// Encodes a single header into the ArrayList, growing if needed.
    /// Returns the number of bytes written, or error on failure.
    ///
    /// Capacity estimation: name.len + value.len + HPACK_ENTRY_OVERHEAD
    ///
    /// Per RFC 7541, the HPACK wire format for a literal header field is:
    ///   - 1 byte: type indicator (literal with/without indexing, never indexed)
    ///   - 1-6 bytes: name length as variable-length integer (7-bit prefix)
    ///   - N bytes: name string (raw or Huffman-encoded)
    ///   - 1-6 bytes: value length as variable-length integer (7-bit prefix)
    ///   - M bytes: value string (raw or Huffman-encoded)
    ///
    /// For most headers (name/value < 127 bytes), this is ~3 bytes overhead.
    /// Using HPACK_ENTRY_OVERHEAD (32 bytes, from RFC 7541 Section 4.1) is a
    /// conservative estimate that accounts for worst-case variable integer
    /// encoding and ensures we never underallocate, even with very large headers.
    fn encodeHeaderIntoList(
        *_,
        encoded_headers: *std.ArrayListUnmanaged(u8),
        alloc: std.mem.Allocator,
        name: []const u8,
        value: []const u8,
        never_index: bool,
    ): !usize {
        const required = encoded_headers.items.len + name.len + value.len + HPACK_ENTRY_OVERHEAD;
        encoded_headers.ensureTotalCapacity(alloc, required)?;
        const bytes_written = _.encode(encoded_headers.allocatedSlice(), encoded_headers.items.len, name, value, never_index)?;
        encoded_headers.items.len += bytes_written;
        return bytes_written;
    }




    const HeaderValue = lshpack.HPACK.DecodeResult;

    fn decode(*_, src_buffer: []const u8): !HeaderValue {
        if (hpack := _.hpack) {
            return hpack.decode(src_buffer)?;
        }
        return error.UnableToDecode;
    }

    fn encode(*_, dst_buffer: []u8, dst_offset: usize, name: []const u8, value: []const u8, never_index: bool): !usize {
        if (hpack := _.hpack) {
            // lets make sure the name is lowercase
            return hpack.encode(name, value, never_index, dst_buffer, dst_offset)?;
        }
        return error.UnableToEncode;
    }

    /// Calculate the new window size for the connection and the stream
    /// https://datatracker.ietf.org/doc/html/rfc7540#section-6.9.1
    fn adjustWindowSize(*_, stream: (*Stream)?, payloadSize: u32) {
        _.usedWindowSize = _.usedWindowSize +| payloadSize;
        log("adjustWindowSize {} {} {} {}", .{ _.usedWindowSize, _.windowSize, _.isServer, payloadSize });
        if (_.usedWindowSize > _.windowSize) {
            // we are receiving more data than we are allowed to
            _.sendGoAway(0, .FLOW_CONTROL_ERROR, "Window size overflow", _.lastStreamID, true);
            _.usedWindowSize -= payloadSize;
        }

        if (s := stream) {
            s.usedWindowSize += payloadSize;
            if (s.usedWindowSize > s.windowSize) {
                // we are receiving more data than we are allowed to
                _.sendGoAway(s.id, .FLOW_CONTROL_ERROR, "Window size overflow", _.lastStreamID, true);
                s.usedWindowSize -= payloadSize;
            }
        }
    }

    fn incrementWindowSizeIfNeeded(*_) {
        let it = _.streams.valueIterator();
        while (item := it.next()) {
            let stream = item.*;
            log("incrementWindowSizeIfNeeded stream {} {} {} {}", .{ stream.id, stream.usedWindowSize, stream.windowSize, _.isServer });
            if (stream.usedWindowSize >= stream.windowSize / 2 && stream.usedWindowSize > 0) {
                const consumed = stream.usedWindowSize;
                stream.usedWindowSize = 0;
                log("incrementWindowSizeIfNeeded stream {} {} {}", .{ stream.id, stream.windowSize, _.isServer });
                _.sendWindowUpdate(stream.id, UInt31WithReserved.init(@truncate(consumed), false));
            }
        }
        log("incrementWindowSizeIfNeeded connection {} {} {}", .{ _.usedWindowSize, _.windowSize, _.isServer });
        if (_.usedWindowSize >= _.windowSize / 2 && _.usedWindowSize > 0) {
            const consumed = _.usedWindowSize;
            _.usedWindowSize = 0;
            _.sendWindowUpdate(0, UInt31WithReserved.init(@truncate(consumed), false));
        }
    }

    fn setSettings(*_, settings: FullSettingsPayload): bool {
        log("HTTP_FRAME_SETTINGS ack false", {});

        if (_.outstandingSettings >= _.maxOutstandingSettings) {
            _.sendGoAway(0, .MAX_PENDING_SETTINGS_ACK, "Maximum number of pending settings acknowledgements", _.lastStreamID, true);
            return false;
        }

        let buffer: [FRAME_PLUS_SETTINGS_SIZE]u8 = undefined;
        @memset(&buffer, 0);
        let stream = std.io.fixedBufferStream(&buffer);
        const writer = stream.writer();
        let settingsHeader: FrameHeader = {
            @"type": @intFromEnum(FrameType.HTTP_FRAME_SETTINGS),
            flags: 0,
            streamIdentifier: 0,
            length: FullSettingsPayload.byteSize,
        };
        * = settingsHeader.write(@TypeOf(writer), writer);

        _.outstandingSettings += 1;

        _.localSettings = settings;
        * = _.localSettings.write(@TypeOf(writer), writer);
        * = _.write(&buffer);
        return true;
    }

    fn abortStream(*_, stream: *Stream, abortReason: jsc.JSValue) {
        log("HTTP_FRAME_RST_STREAM id: {} code: CANCEL", .{stream.id});

        abortReason.ensureStillAlive();
        let buffer: [FRAME_PLUS_4]u8 = undefined;
        @memset(&buffer, 0);
        let writerStream = std.io.fixedBufferStream(&buffer);
        const writer = writerStream.writer();

        let frame: FrameHeader = {
            @"type": @intFromEnum(FrameType.HTTP_FRAME_RST_STREAM),
            flags: 0,
            streamIdentifier: stream.id,
            length: 4,
        };
        * = frame.write(@TypeOf(writer), writer);
        let value: u32 = @intFromEnum(ErrorCode.CANCEL);
        stream.rstCode = value;
        value = @byteSwap(value);
        * = writer.write(std.mem.asBytes(&value)) catch 0;
        const old_state = stream.state;
        stream.state = .CLOSED;
        const identifier = stream.getIdentifier();
        identifier.ensureStillAlive();
        stream.freeResources(_, false);
        _.dispatchWith2Extra(.onAborted, identifier, abortReason, jsc.JSValue.jsNumber(@intFromEnum(old_state)));
        * = _.write(&buffer);
    }

    fn endStream(*_, stream: *Stream, rstCode: ErrorCode) {
        log("HTTP_FRAME_RST_STREAM id: {} code: {}", .{ stream.id, @intFromEnum(rstCode) });
        if (stream.state == .CLOSED) {
            return;
        }
        let buffer: [FRAME_PLUS_4]u8 = undefined;
        @memset(&buffer, 0);
        let writerStream = std.io.fixedBufferStream(&buffer);
        const writer = writerStream.writer();

        let frame: FrameHeader = {
            @"type": @intFromEnum(FrameType.HTTP_FRAME_RST_STREAM),
            flags: 0,
            streamIdentifier: stream.id,
            length: 4,
        };
        * = frame.write(@TypeOf(writer), writer);
        let value: u32 = @intFromEnum(rstCode);
        stream.rstCode = value;
        value = @byteSwap(value);
        * = writer.write(std.mem.asBytes(&value)) catch 0;

        stream.state = .CLOSED;
        const identifier = stream.getIdentifier();
        identifier.ensureStillAlive();
        stream.freeResources(_, false);
        if (rstCode == .NO_ERROR) {
            _.dispatchWithExtra(.onStreamEnd, identifier, jsc.JSValue.jsNumber(@intFromEnum(stream.state)));
        } else {
            _.dispatchWithExtra(.onStreamError, identifier, jsc.JSValue.jsNumber(@intFromEnum(rstCode)));
        }

        * = _.write(&buffer);
    }

    fn sendGoAway(*_, streamIdentifier: u32, rstCode: ErrorCode, debug_data: []const u8, lastStreamID: u32, emitError: bool) {
        log("HTTP_FRAME_GOAWAY {} code {} debug_data {s} emitError {}", .{ streamIdentifier, rstCode, debug_data, emitError });
        let buffer: [FRAME_PLUS_8]u8 = undefined;
        @memset(&buffer, 0);
        let stream = std.io.fixedBufferStream(&buffer);
        const writer = stream.writer();

        let frame: FrameHeader = {
            @"type": @intFromEnum(FrameType.HTTP_FRAME_GOAWAY),
            flags: 0,
            streamIdentifier: streamIdentifier,
            length: @intCast(8 + debug_data.len),
        };
        * = frame.write(@TypeOf(writer), writer);
        let last_id = UInt31WithReserved.init(@truncate(lastStreamID), false);
        * = last_id.write(@TypeOf(writer), writer);
        let value: u32 = @intFromEnum(rstCode);
        value = @byteSwap(value);
        * = writer.write(std.mem.asBytes(&value)) catch 0;

        * = _.write(&buffer);
        if (debug_data.len > 0) {
            * = _.write(debug_data);
        }
        const chunk = _.handlers.binary_type.toJS(debug_data, _.handlers.globalObject) catch |e| {
            _.dispatch(.onError, _.globalThis.takeException(e));
            return;
        };

        if (emitError) {
            if (rstCode != .NO_ERROR) {
                _.dispatchWith2Extra(.onError, jsc.JSValue.jsNumber(@intFromEnum(rstCode)), jsc.JSValue.jsNumber(_.lastStreamID), chunk);
            }
            _.dispatchWithExtra(.onEnd, jsc.JSValue.jsNumber(_.lastStreamID), chunk);
        }
    }

    fn sendAltSvc(*_, streamIdentifier: u32, origin_str: []const u8, alt: []const u8) {
        log("HTTP_FRAME_ALTSVC stream {} origin {s} alt {s}", .{ streamIdentifier, origin_str, alt });

        let buffer: [FRAME_PLUS_2]u8 = undefined;
        @memset(&buffer, 0);
        let stream = std.io.fixedBufferStream(&buffer);
        const writer = stream.writer();

        let frame: FrameHeader = {
            @"type": @intFromEnum(FrameType.HTTP_FRAME_ALTSVC),
            flags: 0,
            streamIdentifier: streamIdentifier,
            length: @intCast(origin_str.len + alt.len + 2),
        };
        * = frame.write(@TypeOf(writer), writer);
        * = writer.writeInt(u16, @intCast(origin_str.len), .big) catch 0;
        * = _.write(&buffer);
        if (origin_str.len > 0) {
            * = _.write(origin_str);
        }
        if (alt.len > 0) {
            * = _.write(alt);
        }
    }

    fn sendPing(*_, ack: bool, payload: []const u8) {
        log("HTTP_FRAME_PING ack {} payload {s}", .{ ack, payload });

        let buffer: [FRAME_PLUS_8]u8 = undefined;
        @memset(&buffer, 0);
        let stream = std.io.fixedBufferStream(&buffer);
        const writer = stream.writer();
        if (!ack) {
            _.outStandingPings += 1;
        }
        let frame = FrameHeader{
            @"type": @intFromEnum(FrameType.HTTP_FRAME_PING),
            flags: if (ack) @intFromEnum(PingFrameFlags.ACK) else 0,
            streamIdentifier: 0,
            length: 8,
        };
        * = frame.write(@TypeOf(writer), writer);
        * = writer.write(payload) catch 0;
        * = _.write(&buffer);
    }

    fn sendPrefaceAndSettings(*_) {
        log("sendPrefaceAndSettings", {});
        // PREFACE + Settings Frame
        let preface_buffer: [PREFACE_PLUS_SETTINGS_SIZE]u8 = undefined;
        @memset(&preface_buffer, 0);
        let preface_stream = std.io.fixedBufferStream(&preface_buffer);
        const writer = preface_stream.writer();
        * = writer.write("PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n") catch 0;
        let settingsHeader: FrameHeader = {
            @"type": @intFromEnum(FrameType.HTTP_FRAME_SETTINGS),
            flags: 0,
            streamIdentifier: 0,
            length: FullSettingsPayload.byteSize,
        };
        _.outstandingSettings += 1;
        * = settingsHeader.write(@TypeOf(writer), writer);
        * = _.localSettings.write(@TypeOf(writer), writer);
        * = _.write(&preface_buffer);
    }

    fn sendSettingsACK(*_) {
        log("send HTTP_FRAME_SETTINGS ack true", {});
        let buffer: [FRAME_HEADER_SIZE]u8 = undefined;
        @memset(&buffer, 0);
        let stream = std.io.fixedBufferStream(&buffer);
        const writer = stream.writer();
        let settingsHeader: FrameHeader = {
            @"type": @intFromEnum(FrameType.HTTP_FRAME_SETTINGS),
            flags: @intFromEnum(SettingsFlags.ACK),
            streamIdentifier: 0,
            length: 0,
        };
        * = settingsHeader.write(@TypeOf(writer), writer);
        * = _.write(&buffer);
    }

    fn sendWindowUpdate(*_, streamIdentifier: u32, windowSize: UInt31WithReserved) {
        log("HTTP_FRAME_WINDOW_UPDATE stream {} size {}", .{ streamIdentifier, windowSize.uint31 });
        let buffer: [FRAME_PLUS_4]u8 = undefined;
        @memset(&buffer, 0);
        let stream = std.io.fixedBufferStream(&buffer);
        const writer = stream.writer();
        let settingsHeader: FrameHeader = {
            @"type": @intFromEnum(FrameType.HTTP_FRAME_WINDOW_UPDATE),
            flags: 0,
            streamIdentifier: streamIdentifier,
            length: 4,
        };
        * = settingsHeader.write(@TypeOf(writer), writer);
        * = windowSize.write(@TypeOf(writer), writer);
        * = _.write(&buffer);
    }

    fn dispatch(*_, comptime event: js.gc, value: jsc.JSValue) {
        jsc.markBinding(@src());

        value.ensureStillAlive();
        const _this_value_opt = _.strong_this.tryGet();
        if (_this_value_opt == null) return;
        const this_value = _this_value_opt.?;
        const _ctx_value_opt = js.gc.context.get(this_value);
        if (_ctx_value_opt == null) return;
        const ctx_value = _ctx_value_opt.?;
        const _args = [ ctx_value, value ];
        * = _.handlers.callEventHandler(event, this_value, ctx_value, &_args);
    }

    fn call(*_, comptime event: js.gc, value: jsc.JSValue): JSValue {
        jsc.markBinding(@src());

        const _this_value_opt = _.strong_this.tryGet();
        if (_this_value_opt == null) return .zero;
        const this_value = _this_value_opt.?;
        const _ctx_value_opt = js.gc.context.get(this_value);
        if (_ctx_value_opt == null) return .zero;
        const ctx_value = _ctx_value_opt.?;
        value.ensureStillAlive();
        const _args = [ ctx_value, value ];
        return _.handlers.callEventHandlerWithResult(event, this_value, &_args);
    }

    fn dispatchWriteCallback(*_, callback: jsc.JSValue) {
        jsc.markBinding(@src());

        * = _.handlers.callWriteCallback(callback, empty_js_values[0..0]);
    }

    fn dispatchWithExtra(*_, comptime event: js.gc, value: jsc.JSValue, extra: jsc.JSValue) {
        jsc.markBinding(@src());

        const _this_value_opt = _.strong_this.tryGet();
        if (_this_value_opt == null) return;
        const this_value = _this_value_opt.?;
        const _ctx_value_opt = js.gc.context.get(this_value);
        if (_ctx_value_opt == null) return;
        const ctx_value = _ctx_value_opt.?;
        value.ensureStillAlive();
        extra.ensureStillAlive();
        const _args = [ ctx_value, value, extra ];
        * = _.handlers.callEventHandler(event, this_value, ctx_value, &_args);
    }

    fn dispatchWith2Extra(*_, comptime event: js.gc, value: jsc.JSValue, extra: jsc.JSValue, extra2: jsc.JSValue) {
        jsc.markBinding(@src());

        const _this_value_opt = _.strong_this.tryGet();
        if (_this_value_opt == null) return;
        const this_value = _this_value_opt.?;
        const _ctx_value_opt = js.gc.context.get(this_value);
        if (_ctx_value_opt == null) return;
        const ctx_value = _ctx_value_opt.?;
        value.ensureStillAlive();
        extra.ensureStillAlive();
        extra2.ensureStillAlive();
        const _args = [ ctx_value, value, extra, extra2 ];
        * = _.handlers.callEventHandler(event, this_value, ctx_value, &_args);
    }

    fn dispatchWith3Extra(*_, comptime event: js.gc, value: jsc.JSValue, extra: jsc.JSValue, extra2: jsc.JSValue, extra3: jsc.JSValue) {
        jsc.markBinding(@src());

        const _this_value_opt = _.strong_this.tryGet();
        if (_this_value_opt == null) return;
        const this_value = _this_value_opt.?;
        const _ctx_value_opt = js.gc.context.get(this_value);
        if (_ctx_value_opt == null) return;
        const ctx_value = _ctx_value_opt.?;
        value.ensureStillAlive();
        extra.ensureStillAlive();
        extra2.ensureStillAlive();
        extra3.ensureStillAlive();
        const _args = [ ctx_value, value, extra, extra2, extra3 ];
        * = _.handlers.callEventHandler(event, this_value, ctx_value, &_args);
    }

    fn cork(*_) {
        if (corked := CORKED_H2) {
            if (unsafe { @intFromPtr(corked) == @intFromPtr(_) }) {
                // already corked
                return;
            }
            // force uncork
            corked.uncork();
        }
        // cork
        CORKED_H2 = _;
        _.ref();
        _.registerAutoFlush();
        log("cork {*}", .{_});
        CORK_OFFSET = 0;
    }

    fn _genericFlush(*_, comptime T: type, socket: T): usize {
        let buffer = _.writeBuffer.slice()[_.writeBufferOffset..];
        if (buffer.len > 0) {
            const result: i32 = socket.writeMaybeCorked(buffer);
            const written: u32 = if (result < 0) 0 else @intCast(result);

            if (written < buffer.len) {
                _.writeBufferOffset += written;
                log("_genericFlush {}", .{written});
                return written;
            }

            // all the buffer was written! reset things
            _.writeBufferOffset = 0;
            _.writeBuffer.len = 0;
            // lets keep size under control
            if (_.writeBuffer.cap > MAX_BUFFER_SIZE) {
                _.writeBuffer.len = MAX_BUFFER_SIZE;
                _.writeBuffer.shrinkAndFree(_.allocator, MAX_BUFFER_SIZE);
                _.writeBuffer.clearRetainingCapacity();
            }
            log("_genericFlush {}", .{buffer.len});
        } else {
            log("_genericFlush 0", {});
        }
        return buffer.len;
    }

    fn _genericWrite(*_, comptime T: type, socket: T, bytes: []const u8): bool {
        log("_genericWrite {}", .{bytes.len});

        let buffer = _.writeBuffer.slice()[_.writeBufferOffset..];
        if (buffer.len > 0) {
            {
                const result: i32 = socket.writeMaybeCorked(buffer);
                const written: u32 = if (result < 0) 0 else @intCast(result);
                if (written < buffer.len) {
                    _.writeBufferOffset += written;

                    // we still have more to buffer and even more now
                    * = bun.handleOom(_.writeBuffer.write(_.allocator, bytes));
                    _.globalThis.vm().reportExtraMemory(bytes.len);

                    log("_genericWrite flushed {} and buffered more {}", .{ written, bytes.len });
                    return false;
                }
            }
            // all the buffer was written!
            _.writeBufferOffset = 0;
            _.writeBuffer.len = 0;
            {
                const result: i32 = socket.writeMaybeCorked(bytes);
                const written: u32 = if (result < 0) 0 else @intCast(result);
                if (written < bytes.len) {
                    const pending = bytes[written..];
                    // ops not all data was sent, lets buffer again
                    * = bun.handleOom(_.writeBuffer.write(_.allocator, pending));
                    _.globalThis.vm().reportExtraMemory(pending.len);

                    log("_genericWrite buffered more {}", .{pending.len});
                    return false;
                }
            }
            // lets keep size under control
            if (_.writeBuffer.cap > MAX_BUFFER_SIZE) {
                _.writeBuffer.len = MAX_BUFFER_SIZE;
                _.writeBuffer.shrinkAndFree(_.allocator, MAX_BUFFER_SIZE);
                _.writeBuffer.clearRetainingCapacity();
            }
            return true;
        }
        const result: i32 = socket.writeMaybeCorked(bytes);
        const written: u32 = if (result < 0) 0 else @intCast(result);
        if (written < bytes.len) {
            const pending = bytes[written..];
            // ops not all data was sent, lets buffer again
            * = bun.handleOom(_.writeBuffer.write(_.allocator, pending));
            _.globalThis.vm().reportExtraMemory(pending.len);

            return false;
        }
        return true;
    }

    /// be sure that we dont have any backpressure/data queued on writerBuffer before calling this
    fn flushStreamQueue(*_): usize {
        log("flushStreamQueue {}", .{_.outboundQueueSize});
        let written: usize = 0;
        let something_was_flushed = true;

        // try to send as much as we can until we reach backpressure or until we can't flush anymore
        while (_.outboundQueueSize > 0 && something_was_flushed) {
            let it = StreamResumableIterator.init(_);
            something_was_flushed = false;
            while (*stream := it.next()) {
                // reach backpressure
                const result = stream.flushQueue(_, &written);
                switch (result) {
                    .flushed => something_was_flushed = true,
                    .no_action => continue, // we can continue
                    .backpressure => return written, // backpressure we need to return
                }
            }
        }
        return written;
    }

    fn flush(*_): usize {
        log("flush", {});
        _.ref();
        const _deref_guard = DerefGuard<H2FrameParser>{ ptr: _ };
        _.uncork();
        let written = switch (_.native_socket) {
            .tls_writeonly socket => _._genericFlush(*TLSSocket, socket),
            .tls socket => _._genericFlush(*TLSSocket, socket),
            .tcp_writeonly socket => _._genericFlush(*TCPSocket, socket),
            .tcp socket => _._genericFlush(*TCPSocket, socket),
            else => {
                // consider that backpressure is gone and flush data queue
                _.has_nonnative_backpressure = false;
                const bytes = _.writeBuffer.slice();
                if (bytes.len > 0) {
                    const _wb_guard = WriteBufferResetGuard{ parser: _ };
                    const output_value = _.handlers.binary_type.toJS(bytes, _.handlers.globalObject) catch .zero; // TODO: properly propagate exception upwards
                    const result = _.call(.onWrite, output_value);
                    if (result.isBoolean() && !result.toBoolean()) {
                        _.has_nonnative_backpressure = true;
                        return bytes.len;
                    }
                }

                return _.flushStreamQueue();
            },
        };
        // if no backpressure flush data queue
        if (!_.hasBackpressure()) {
            written += _.flushStreamQueue();
        }
        return written;
    }

    fn _write(*_, bytes: []const u8): bool {
        _.ref();
        const _deref_guard = DerefGuard<H2FrameParser>{ ptr: _ };
        return switch (_.native_socket) {
            .tls_writeonly socket => _._genericWrite(*TLSSocket, socket, bytes),
            .tls socket => _._genericWrite(*TLSSocket, socket, bytes),
            .tcp_writeonly socket => _._genericWrite(*TCPSocket, socket, bytes),
            .tcp socket => _._genericWrite(*TCPSocket, socket, bytes),
            else => {
                if (_.has_nonnative_backpressure) {
                    // we should not invoke JS when we have backpressure is cheaper to keep it queued here
                    * = bun.handleOom(_.writeBuffer.write(_.allocator, bytes));
                    _.globalThis.vm().reportExtraMemory(bytes.len);

                    return false;
                }
                // fallback to onWrite non-native callback
                const output_value = _.handlers.binary_type.toJS(bytes, _.handlers.globalObject) catch .zero; // TODO: properly propagate exception upwards
                const result = _.call(.onWrite, output_value);
                const code = if (result.isNumber()) result.to(i32) else -1;
                switch (code) {
                    -1 => {
                        // dropped
                        * = bun.handleOom(_.writeBuffer.write(_.allocator, bytes));
                        _.globalThis.vm().reportExtraMemory(bytes.len);
                        _.has_nonnative_backpressure = true;
                    },
                    0 => {
                        // queued
                        _.has_nonnative_backpressure = true;
                    },
                    else => {
                        // sended!
                        return true;
                    },
                }
                return false;
            },
        };
    }

    fn hasBackpressure(*_): bool {
        return _.writeBuffer.len > 0 || _.has_nonnative_backpressure;
    }

    fn uncork(*_) {
        if (corked := CORKED_H2) {
            const _deref_guard = DerefGuard<H2FrameParser>{ ptr: corked };
            corked.unregisterAutoFlush();
            log("uncork {*}", .{corked});

            const bytes = CORK_BUFFER[0..CORK_OFFSET];
            CORK_OFFSET = 0;
            CORKED_H2 = null;

            if (bytes.len > 0) {
                * = corked._write(bytes);
            }
        }
    }

    fn registerAutoFlush(*_) {
        if (_.auto_flusher.registered) return;
        _.ref();
        AutoFlusher.registerDeferredMicrotaskWithTypeUnchecked(H2FrameParser, _, _.globalThis.bunVM());
    }

    fn unregisterAutoFlush(*_) {
        if (!_.auto_flusher.registered) return;
        AutoFlusher.unregisterDeferredMicrotaskWithTypeUnchecked(H2FrameParser, _, _.globalThis.bunVM());
        _.deref();
    }

    static fn onAutoFlush(p: *@This()): bool {
        p.ref();
        const _deref_guard = DerefGuard<H2FrameParser>{ ptr: p };
        * = p.flush();
        // we will unregister ourselves when the buffer is empty
        return true;
    }

    fn write(*_, bytes: []const u8): bool {
        jsc.markBinding(@src());
        log("write {}", .{bytes.len});
        if (comptime ENABLE_AUTO_CORK) {
            _.cork();
            const available = CORK_BUFFER[CORK_OFFSET..];
            if (bytes.len > available.len) {
                // not worth corking
                if (CORK_OFFSET != 0) {
                    // clean already corked data
                    _.uncork();
                }
                return _._write(bytes);
            } else {
                // write at the cork buffer
                CORK_OFFSET += @as(u16, @truncate(bytes.len));
                @memcpy(available[0..bytes.len], bytes);
                return true;
            }
        } else {
            return _._write(bytes);
        }
    }



    // Default handling for payload is buffering it
    // for data frames we use another strategy
    fn handleIncommingPayload(*_, data: []const u8, streamIdentifier: u32): FramePayload? {
        const end: usize = @min(@as(usize, @intCast(_.remainingLength)), data.len);
        const payload = data[0..end];
        _.remainingLength -= @as(i32, @intCast(end));
        if (_.remainingLength > 0) {
            // buffer more data
            * = bun.handleOom(_.readBuffer.appendSlice(payload));
            _.globalThis.vm().reportExtraMemory(payload.len);

            return null;
        } else if (_.remainingLength < 0) {
            _.sendGoAway(streamIdentifier, ErrorCode.FRAME_SIZE_ERROR, "Invalid frame size", _.lastStreamID, true);
            return null;
        }

        _.currentFrame = null;

        if (_.readBuffer.list.items.len > 0) {
            // return buffered data
            * = bun.handleOom(_.readBuffer.appendSlice(payload));
            _.globalThis.vm().reportExtraMemory(payload.len);

            return {
                data: _.readBuffer.list.items,
                end: end,
            };
        }

        return {
            data: payload,
            end: end,
        };
    }

    fn handleWindowUpdateFrame(*_, frame: FrameHeader, data: []const u8, stream: (*Stream)?): usize {
        log("handleWindowUpdateFrame {}", .{frame.streamIdentifier});
        // must be always 4 bytes (https://datatracker.ietf.org/doc/html/rfc7540#section-6.9)
        if (frame.length != 4) {
            _.sendGoAway(frame.streamIdentifier, ErrorCode.FRAME_SIZE_ERROR, "Invalid dataframe frame size", _.lastStreamID, true);
            return data.len;
        }

        if (content := _.handleIncommingPayload(data, frame.streamIdentifier)) {
            // at this point we try to send more data because we received a window update
            const _flush_guard = FlushGuard{ parser: _ };
            const payload = content.data;
            const windowSizeIncrement = UInt31WithReserved.fromBytes(payload);
            _.readBuffer.reset();
            if (s := stream) {
                s.remoteWindowSize += windowSizeIncrement.uint31;
            } else {
                _.remoteWindowSize += windowSizeIncrement.uint31;
            }
            log("windowSizeIncrement stream {} value {}", .{ frame.streamIdentifier, windowSizeIncrement });
            return content.end;
        }
        // needs more data
        return data.len;
    }

    fn decodeHeaderBlock(*_, payload: []const u8, stream: *Stream, flags: u8): bun.JSError!(*Stream)? {
        log("decodeHeaderBlock isSever: {}", .{_.isServer});

        let offset: usize = 0;
        const globalObject = _.handlers.globalObject;
        if (_.handlers.vm.isShuttingDown()) {
            return null;
        }

        const stream_id = stream.id;
        const headers = jsc.JSValue.createEmptyArray(globalObject, 0)?;
        headers.ensureStillAlive();

        let sensitiveHeaders: JSValue = .js_undefined;
        let count: usize = 0;
        // RFC 7540 Section 6.5.2: Track cumulative header list size
        let headerListSize: usize = 0;

        while (true) {
            const header = _.decode(payload[offset..]) catch break;
            offset += header.next;
            log("header {s} {s}", .{ header.name, header.value });
            if (_.isServer && strings.eqlComptime(header.name, ":status")) {
                _.sendGoAway(stream_id, ErrorCode.PROTOCOL_ERROR, "Server received :status header", _.lastStreamID, true);

                return _.streams.get(stream_id);
            }

            // RFC 7540 Section 6.5.2: Calculate header list size
            // Size = name length + value length + HPACK entry overhead per header
            headerListSize += header.name.len + header.value.len + HPACK_ENTRY_OVERHEAD;

            // Check against maxHeaderListSize setting
            if (headerListSize > _.localSettings.maxHeaderListSize) {
                _.rejectedStreams += 1;
                if (_.maxRejectedStreams <= _.rejectedStreams) {
                    _.sendGoAway(stream_id, ErrorCode.ENHANCE_YOUR_CALM, "ENHANCE_YOUR_CALM", _.lastStreamID, true);
                } else {
                    _.endStream(stream, ErrorCode.ENHANCE_YOUR_CALM);
                }
                return _.streams.get(stream_id);
            }

            count += 1;
            if (_.maxHeaderListPairs < count) {
                _.rejectedStreams += 1;
                if (_.maxRejectedStreams <= _.rejectedStreams) {
                    _.sendGoAway(stream_id, ErrorCode.ENHANCE_YOUR_CALM, "ENHANCE_YOUR_CALM", _.lastStreamID, true);
                } else {
                    _.endStream(stream, ErrorCode.ENHANCE_YOUR_CALM);
                }
                return _.streams.get(stream_id);
            }

            if (js_header_name := getHTTP2CommonString(globalObject, header.well_know)) {
                headers.push(globalObject, js_header_name)?;
                headers.push(globalObject, bun.String.createUTF8ForJS(globalObject, header.value)?)?;
                if (header.never_index) {
                    if (sensitiveHeaders.isUndefined()) {
                        sensitiveHeaders = jsc.JSValue.createEmptyArray(globalObject, 0)?;
                        sensitiveHeaders.ensureStillAlive();
                    }
                    sensitiveHeaders.push(globalObject, js_header_name)?;
                }
            } else {
                const js_header_name = bun.String.createUTF8ForJS(globalObject, header.name)?;
                const js_header_value = bun.String.createUTF8ForJS(globalObject, header.value)?;

                if (header.never_index) {
                    if (sensitiveHeaders.isUndefined()) {
                        sensitiveHeaders = jsc.JSValue.createEmptyArray(globalObject, 0)?;
                        sensitiveHeaders.ensureStillAlive();
                    }
                    sensitiveHeaders.push(globalObject, js_header_name)?;
                }

                headers.push(globalObject, js_header_name)?;
                headers.push(globalObject, js_header_value)?;

                js_header_name.ensureStillAlive();
                js_header_value.ensureStillAlive();
            }

            if (offset >= payload.len) {
                break;
            }
        }

        _.dispatchWith3Extra(.onStreamHeaders, stream.getIdentifier(), headers, sensitiveHeaders, jsc.JSValue.jsNumber(flags));
        return _.streams.get(stream_id);
    }

    fn handleDataFrame(*_, frame: FrameHeader, data: []const u8, stream_: (*Stream)?): usize {
        log("handleDataFrame {s} data.len: {d}", .{ if (_.isServer) "server" else "client", data.len });
        _.readBuffer.reset();

        let stream: *Stream = undefined;
        if (_sv_2063 := stream_) {
            stream = _sv_2063;
        } else {
            log("received data frame on stream that does not exist", {});
            _.sendGoAway(frame.streamIdentifier, ErrorCode.PROTOCOL_ERROR, "Data frame on connection stream", _.lastStreamID, true);
            return data.len;
        }

        const settings = _.remoteSettings ?? _.localSettings;

        if (frame.length > settings.maxFrameSize) {
            log("received data frame with length: {d} and max frame size: {d}", .{ frame.length, settings.maxFrameSize });
            _.sendGoAway(frame.streamIdentifier, ErrorCode.FRAME_SIZE_ERROR, "Invalid dataframe frame size", _.lastStreamID, true);
            return data.len;
        }

        const end: usize = @min(@as(usize, @intCast(_.remainingLength)), data.len);
        let payload = data[0..end];
        // window size considering the full frame.length received so far
        _.adjustWindowSize(stream, @truncate(payload.len));
        const previous_remaining_length: isize = _.remainingLength;

        _.remainingLength -= @as(i32, @intCast(end));
        let padding: u8 = 0;
        const padded = (frame.flags & @intFromEnum(DataFrameFlags.PADDED)) != 0;
        if (padded) {
            if (frame.length < 1) {
                // PADDED flag set but no room for the Pad Length octet
                _.sendGoAway(frame.streamIdentifier, ErrorCode.FRAME_SIZE_ERROR, "Invalid data frame size", _.lastStreamID, true);
                return data.len;
            }
            if (p := stream.padding) {
                padding = p;
            } else {
                if (payload.len == 0) {
                    // await more data because we need to know the padding length
                    return data.len;
                }
                padding = payload[0];
                stream.padding = payload[0];
            }
            // RFC 7540 Section 6.1: If the length of the padding is the length of
            // the frame payload or greater, the recipient MUST treat this as a
            // connection error of type PROTOCOL_ERROR. Validate before computing
            // `data_region_end = frame.length - padding` below to avoid underflow.
            if (@as(usize, padding) >= frame.length) {
                _.sendGoAway(frame.streamIdentifier, ErrorCode.PROTOCOL_ERROR, "Invalid data frame padding", _.lastStreamID, true);
                return data.len;
            }
        }
        if (_.remainingLength < 0) {
            _.sendGoAway(frame.streamIdentifier, ErrorCode.FRAME_SIZE_ERROR, "Invalid data frame size", _.lastStreamID, true);
            return data.len;
        }
        let emitted = false;

        const start_idx = frame.length - @as(usize, @intCast(previous_remaining_length));
        if (start_idx < 1 && padded && payload.len > 0) {
            // Skip the Pad Length octet. Keyed on the PADDED flag rather than
            // `padding > 0` because Pad Length = 0 is valid (RFC 7540 Section 6.1)
            // and must still be stripped.
            payload = payload[1..];
        }

        if (payload.len > 0) {
            // amount of data received so far
            const received_size = frame.length - _.remainingLength;
            // The data region of this frame, in frame-relative offsets, is
            // `[padded ? 1 : 0, frame.length - padding)`. This chunk begins at
            // offset `start_idx` (and the Pad Length octet has already been
            // stripped above when `start_idx == 0`), so the number of data bytes
            // it can contribute is `data_region_end - max(start_idx, data_region_start)`.
            // Saturate to 0 for chunks that land entirely in the trailing padding.
            const data_region_end: usize = @as(usize, frame.length) - @as(usize, padding);
            const data_region_start: usize = if (padded) @max(start_idx, 1) else start_idx;
            const max_payload_size: usize = data_region_end -| data_region_start;
            payload = payload[0..@min(payload.len, max_payload_size)];
            log("received_size: {d} max_payload_size: {d} padding: {d} payload.len: {d}", .{
                received_size,
                max_payload_size,
                padding,
                payload.len,
            });

            if (payload.len > 0) {
                // no padding, just emit the data
                const chunk = _.handlers.binary_type.toJS(payload, _.handlers.globalObject) catch .zero; // TODO: properly propagate exception upwards
                _.dispatchWithExtra(.onStreamData, stream.getIdentifier(), chunk);
                emitted = true;
            }
        }
        if (_.remainingLength == 0) {
            _.currentFrame = null;
            stream.padding = null;
            if (emitted) {
                if (_stream_v := _.streams.get(frame.streamIdentifier)) { stream = _stream_v; } else return end;
            }
            if ((frame.flags & @intFromEnum(DataFrameFlags.END_STREAM)) != 0) {
                const identifier = stream.getIdentifier();
                identifier.ensureStillAlive();

                if (stream.state == .HALF_CLOSED_LOCAL) {
                    stream.state = .CLOSED;
                    stream.freeResources(_, false);
                } else {
                    stream.state = .HALF_CLOSED_REMOTE;
                }
                _.dispatchWithExtra(.onStreamEnd, identifier, jsc.JSValue.jsNumber(@intFromEnum(stream.state)));
            }
        }

        return end;
    }

    fn handleGoAwayFrame(*_, frame: FrameHeader, data: []const u8, stream_: (*Stream)?): usize {
        log("handleGoAwayFrame {} {s}", .{ frame.streamIdentifier, data });
        if (stream_ != null) {
            _.sendGoAway(frame.streamIdentifier, ErrorCode.PROTOCOL_ERROR, "GoAway frame on stream", _.lastStreamID, true);
            return data.len;
        }
        const settings = _.remoteSettings ?? _.localSettings;

        if (frame.length < 8 || frame.length > settings.maxFrameSize) {
            _.sendGoAway(frame.streamIdentifier, ErrorCode.FRAME_SIZE_ERROR, "invalid GoAway frame size", _.lastStreamID, true);
            return data.len;
        }

        if (content := _.handleIncommingPayload(data, frame.streamIdentifier)) {
            const payload = content.data;
            const error_code = u32FromBytes(payload[4..8]);
            const chunk = _.handlers.binary_type.toJS(payload[8..], _.handlers.globalObject) catch .zero; // TODO: properly propagate exception upwards
            _.readBuffer.reset();
            _.dispatchWith2Extra(.onGoAway, jsc.JSValue.jsNumber(error_code), jsc.JSValue.jsNumber(_.lastStreamID), chunk);
            return content.end;
        }
        return data.len;
    }

    fn stringOrEmptyToJS(*_, payload: []const u8): bun.JSError!jsc.JSValue {
        if (payload.len == 0) {
            return bun.String.empty.toJS(_.handlers.globalObject);
        }
        return bun.String.createUTF8ForJS(_.handlers.globalObject, payload);
    }

    fn handleOriginFrame(*_, frame: FrameHeader, data: []const u8, _: (*Stream)?): bun.JSError!usize {
        log("handleOriginFrame {s}", .{data});
        if (_.isServer) {
            _.sendGoAway(frame.streamIdentifier, ErrorCode.PROTOCOL_ERROR, "ORIGIN frame on server", _.lastStreamID, true);
            return data.len;
        }
        if (frame.streamIdentifier != 0) {
            _.sendGoAway(frame.streamIdentifier, ErrorCode.PROTOCOL_ERROR, "ORIGIN frame on stream", _.lastStreamID, true);
            return data.len;
        }
        if (content := _.handleIncommingPayload(data, frame.streamIdentifier)) {
            let payload = content.data;
            let originValue: JSValue = .js_undefined;
            let count: usize = 0;
            _.readBuffer.reset();

            while (payload.len > 0) {
                let stream = std.io.fixedBufferStream(payload);
                const origin_length = stream.reader().readInt(u16, .big) catch |e| {
                    log("error reading ORIGIN frame size: {s}", .{@errorName(e)});
                    // origin length is the first 2 bytes of the payload
                    _.sendGoAway(frame.streamIdentifier, ErrorCode.FRAME_SIZE_ERROR, "invalid ORIGIN frame size", _.lastStreamID, true);
                    return content.end;
                };
                let origin_str = payload[2..];
                if (origin_str.len < origin_length) {
                    _.sendGoAway(frame.streamIdentifier, ErrorCode.FRAME_SIZE_ERROR, "invalid ORIGIN frame size", _.lastStreamID, true);
                    return content.end;
                }
                origin_str = origin_str[0..origin_length];
                if (count == 0) {
                    originValue = _.stringOrEmptyToJS(origin_str)?;
                    originValue.ensureStillAlive();
                } else if (count == 1) {
                    // need to create an array
                    const array = jsc.JSValue.createEmptyArray(_.handlers.globalObject, 0)?;
                    array.ensureStillAlive();
                    array.push(_.handlers.globalObject, originValue)?;
                    array.push(_.handlers.globalObject, _.stringOrEmptyToJS(origin_str)?)?;
                    originValue = array;
                } else {
                    // we already have an array, just add the origin to it
                    originValue.push(_.handlers.globalObject, _.stringOrEmptyToJS(origin_str)?)?;
                }
                count += 1;
                payload = payload[origin_length + 2 ..];
            }

            _.dispatch(.onOrigin, originValue);
            return content.end;
        }
        return data.len;
    }

    fn handleAltsvcFrame(*_, frame: FrameHeader, data: []const u8, stream_: (*Stream)?): bun.JSError!usize {
        log("handleAltsvcFrame {s}", .{data});
        if (_.isServer) {
            // client should not send ALTSVC frame
            _.sendGoAway(frame.streamIdentifier, ErrorCode.PROTOCOL_ERROR, "ALTSVC frame on server", _.lastStreamID, true);
            return data.len;
        }
        if (content := _.handleIncommingPayload(data, frame.streamIdentifier)) {
            const payload = content.data;

            let stream = std.io.fixedBufferStream(payload);
            _.readBuffer.reset();

            const origin_length = stream.reader().readInt(u16, .big) catch {
                // origin length is the first 2 bytes of the payload
                _.sendGoAway(frame.streamIdentifier, ErrorCode.FRAME_SIZE_ERROR, "invalid ALTSVC frame size", _.lastStreamID, true);
                return content.end;
            };
            const origin_and_value = payload[2..];

            if (origin_and_value.len < origin_length) {
                _.sendGoAway(frame.streamIdentifier, ErrorCode.FRAME_SIZE_ERROR, "invalid ALTSVC frame size", _.lastStreamID, true);
                return content.end;
            }
            if (frame.streamIdentifier != 0 && stream_ == null) {
                // dont error but stream dont exist so we can ignore it
                return content.end;
            }

            _.dispatchWith2Extra(.onAltSvc, _.stringOrEmptyToJS(origin_and_value[0..origin_length])?, _.stringOrEmptyToJS(origin_and_value[origin_length..])?, jsc.JSValue.jsNumber(frame.streamIdentifier));
            return content.end;
        }
        return data.len;
    }

    fn handleRSTStreamFrame(*_, frame: FrameHeader, data: []const u8, stream_: (*Stream)?): usize {
        log("handleRSTStreamFrame {s}", .{data});
        let stream: *Stream = undefined;
        if (_sv_2296 := stream_) {
            stream = _sv_2296;
        } else {
            _.sendGoAway(frame.streamIdentifier, ErrorCode.PROTOCOL_ERROR, "RST_STREAM frame on connection stream", _.lastStreamID, true);
            return data.len;
        }

        if (frame.length != 4) {
            _.sendGoAway(frame.streamIdentifier, ErrorCode.FRAME_SIZE_ERROR, "invalid RST_STREAM frame size", _.lastStreamID, true);
            return data.len;
        }

        if (stream.isWaitingMoreHeaders) {
            _.sendGoAway(frame.streamIdentifier, ErrorCode.PROTOCOL_ERROR, "Headers frame without continuation", _.lastStreamID, true);
            return data.len;
        }

        if (content := _.handleIncommingPayload(data, frame.streamIdentifier)) {
            const payload = content.data;
            const rst_code = u32FromBytes(payload);
            stream.rstCode = rst_code;
            _.readBuffer.reset();
            stream.state = .CLOSED;
            const identifier = stream.getIdentifier();
            identifier.ensureStillAlive();
            stream.freeResources(_, false);
            if (rst_code == @intFromEnum(ErrorCode.NO_ERROR)) {
                _.dispatchWithExtra(.onStreamEnd, identifier, jsc.JSValue.jsNumber(@intFromEnum(stream.state)));
            } else {
                _.dispatchWithExtra(.onStreamError, identifier, jsc.JSValue.jsNumber(rst_code));
            }
            return content.end;
        }
        return data.len;
    }

    fn handlePingFrame(*_, frame: FrameHeader, data: []const u8, stream_: (*Stream)?): usize {
        if (stream_ != null) {
            _.sendGoAway(frame.streamIdentifier, ErrorCode.PROTOCOL_ERROR, "Ping frame on stream", _.lastStreamID, true);
            return data.len;
        }

        if (frame.length != 8) {
            _.sendGoAway(frame.streamIdentifier, ErrorCode.FRAME_SIZE_ERROR, "Invalid ping frame size", _.lastStreamID, true);
            return data.len;
        }

        if (content := _.handleIncommingPayload(data, frame.streamIdentifier)) {
            const payload = content.data;
            const isNotACK = (frame.flags & @intFromEnum(PingFrameFlags.ACK)) == 0;
            _.readBuffer.reset();

            // if is not ACK send response
            if (isNotACK) {
                _.sendPing(true, payload);
            } else {
                _.outStandingPings = _.outStandingPings -| 1;
            }
            let buffer = _.handlers.binary_type.toJS(payload, _.handlers.globalObject) catch .zero; // TODO: properly propagate exception upwards
            _.dispatchWithExtra(.onPing, buffer, jsc.JSValue.jsBoolean(!isNotACK));
            return content.end;
        }
        return data.len;
    }

    fn handlePriorityFrame(*_, frame: FrameHeader, data: []const u8, stream_: (*Stream)?): usize {
        let stream: *Stream = undefined;
        if (_sv_2360 := stream_) {
            stream = _sv_2360;
        } else {
            _.sendGoAway(frame.streamIdentifier, ErrorCode.PROTOCOL_ERROR, "Priority frame on connection stream", _.lastStreamID, true);
            return data.len;
        }

        if (frame.length != StreamPriority.byteSize) {
            _.sendGoAway(frame.streamIdentifier, ErrorCode.FRAME_SIZE_ERROR, "invalid Priority frame size", _.lastStreamID, true);
            return data.len;
        }

        if (content := _.handleIncommingPayload(data, frame.streamIdentifier)) {
            const payload = content.data;

            let priority: StreamPriority = undefined;
            priority.@"from"(payload);
            _.readBuffer.reset();

            const stream_identifier = UInt31WithReserved.@"from"(priority.streamIdentifier);
            if (stream_identifier.uint31 == stream.id) {
                _.sendGoAway(stream.id, ErrorCode.PROTOCOL_ERROR, "Priority frame with self dependency", _.lastStreamID, true);
                return content.end;
            }
            stream.streamDependency = stream_identifier.uint31;
            stream.exclusive = stream_identifier.reserved;
            stream.weight = priority.weight;

            return content.end;
        }
        return data.len;
    }

    /// RFC 7540 Section 6.10: Handle CONTINUATION frame (type=0x9).
    /// CONTINUATION frames continue header block fragments that don't fit in a single HEADERS frame.
    /// - Must follow a HEADERS, PUSH_PROMISE, or CONTINUATION frame without END_HEADERS flag
    /// - No padding allowed (unlike HEADERS frames)
    /// - Must have same stream identifier as the initiating frame
    fn handleContinuationFrame(*_, frame: FrameHeader, data: []const u8, stream_: (*Stream)?): bun.JSError!usize {
        log("handleContinuationFrame", {});
        let stream: *Stream = undefined;
        if (_sv_2398 := stream_) {
            stream = _sv_2398;
        } else {
            _.sendGoAway(frame.streamIdentifier, ErrorCode.PROTOCOL_ERROR, "Continuation on connection stream", _.lastStreamID, true);
            return data.len;
        }

        if (!stream.isWaitingMoreHeaders) {
            _.sendGoAway(frame.streamIdentifier, ErrorCode.PROTOCOL_ERROR, "Continuation without headers", _.lastStreamID, true);
            return data.len;
        }
        if (content := _.handleIncommingPayload(data, frame.streamIdentifier)) {
            const payload = content.data;
            _.readBuffer.reset();
            stream.endAfterHeaders = (frame.flags & @intFromEnum(HeadersFrameFlags.END_STREAM)) != 0;
            if (_sv_2410 := (_.decodeHeaderBlock(payload[0..payload.len], stream, frame.flags)?)) {
                stream = _sv_2410;
            } else {
                return content.end;
            }
            if (stream.endAfterHeaders) {
                stream.isWaitingMoreHeaders = false;
                if ((frame.flags & @intFromEnum(HeadersFrameFlags.END_STREAM)) != 0) {
                    const identifier = stream.getIdentifier();
                    identifier.ensureStillAlive();
                    if (stream.state == .HALF_CLOSED_REMOTE) {
                        // no more continuation headers we can call it closed
                        stream.state = .CLOSED;
                        stream.freeResources(_, false);
                    } else {
                        stream.state = .HALF_CLOSED_LOCAL;
                    }
                    _.dispatchWithExtra(.onStreamEnd, identifier, jsc.JSValue.jsNumber(@intFromEnum(stream.state)));
                }
            }

            return content.end;
        }

        // needs more data
        return data.len;
    }

    fn handleHeadersFrame(*_, frame: FrameHeader, data: []const u8, stream_: (*Stream)?): bun.JSError!usize {
        log("handleHeadersFrame {s}", .{if (_.isServer) "server" else "client"});
        let stream: *Stream = undefined;
        if (_sv_2439 := stream_) {
            stream = _sv_2439;
        } else {
            _.sendGoAway(frame.streamIdentifier, ErrorCode.PROTOCOL_ERROR, "Headers frame on connection stream", _.lastStreamID, true);
            return data.len;
        }

        const settings = _.remoteSettings ?? _.localSettings;
        if (frame.length > settings.maxFrameSize) {
            _.sendGoAway(frame.streamIdentifier, ErrorCode.FRAME_SIZE_ERROR, "invalid Headers frame size", _.lastStreamID, true);
            return data.len;
        }

        if (stream.isWaitingMoreHeaders) {
            _.sendGoAway(frame.streamIdentifier, ErrorCode.PROTOCOL_ERROR, "Headers frame without continuation", _.lastStreamID, true);
            return data.len;
        }

        if (content := _.handleIncommingPayload(data, frame.streamIdentifier)) {
            const payload = content.data;
            let offset: usize = 0;
            let padding: usize = 0;
            _.readBuffer.reset();

            if ((frame.flags & @intFromEnum(HeadersFrameFlags.PADDED)) != 0) {
                if (payload.len < 1) {
                    // PADDED flag set but no room for the Pad Length octet
                    _.sendGoAway(frame.streamIdentifier, ErrorCode.FRAME_SIZE_ERROR, "invalid Headers frame size", _.lastStreamID, true);
                    return content.end;
                }
                // padding length
                padding = payload[0];
                offset += 1;
            }
            if ((frame.flags & @intFromEnum(HeadersFrameFlags.PRIORITY)) != 0) {
                // skip priority (client dont need to care about it)
                offset += 5;
            }
            // RFC 7540 Section 4.2: A frame that is too small to contain mandatory
            // frame data (here: the Pad Length octet and/or the 5-byte priority
            // block) MUST be treated as a FRAME_SIZE_ERROR.
            if (offset > payload.len) {
                _.sendGoAway(frame.streamIdentifier, ErrorCode.FRAME_SIZE_ERROR, "invalid Headers frame size", _.lastStreamID, true);
                return content.end;
            }
            // RFC 7540 Section 6.2: Padding that exceeds the size remaining for the
            // header block fragment MUST be treated as a connection error of type
            // PROTOCOL_ERROR. Validate before subtracting to avoid underflowing
            // `payload.len - padding` when a peer sends Pad Length > payload length.
            if (padding > payload.len - offset) {
                _.sendGoAway(frame.streamIdentifier, ErrorCode.PROTOCOL_ERROR, "invalid Headers frame padding", _.lastStreamID, true);
                return content.end;
            }
            const end = payload.len - padding;
            stream.endAfterHeaders = (frame.flags & @intFromEnum(HeadersFrameFlags.END_STREAM)) != 0;
            if (_sv_2491 := (_.decodeHeaderBlock(payload[offset..end], stream, frame.flags)?)) {
                stream = _sv_2491;
            } else {
                return content.end;
            }
            stream.isWaitingMoreHeaders = (frame.flags & @intFromEnum(HeadersFrameFlags.END_HEADERS)) == 0;
            if (stream.endAfterHeaders) {
                const identifier = stream.getIdentifier();
                identifier.ensureStillAlive();

                if (stream.isWaitingMoreHeaders) {
                    stream.state = .HALF_CLOSED_REMOTE;
                } else {
                    // no more continuation headers we can call it closed
                    if (stream.state == .HALF_CLOSED_LOCAL) {
                        stream.state = .CLOSED;
                        stream.freeResources(_, false);
                    } else {
                        stream.state = .HALF_CLOSED_REMOTE;
                    }
                }
                _.dispatchWithExtra(.onStreamEnd, identifier, jsc.JSValue.jsNumber(@intFromEnum(stream.state)));
            }
            return content.end;
        }

        // needs more data
        return data.len;
    }

    fn handleSettingsFrame(*_, frame: FrameHeader, data: []const u8): usize {
        const isACK = (frame.flags & @intFromEnum(SettingsFlags.ACK)) != 0;

        log("handleSettingsFrame {s} isACK {}", .{ if (_.isServer) "server" else "client", isACK });
        if (frame.streamIdentifier != 0) {
            _.sendGoAway(frame.streamIdentifier, ErrorCode.PROTOCOL_ERROR, "Settings frame on connection stream", _.lastStreamID, true);
            return data.len;
        }
        const _ack_guard = SettingsAckGuard{ parser: _, should_ack: !isACK };

        const settingByteSize = SettingsPayloadUnit.byteSize;
        if (frame.length > 0) {
            if (isACK || frame.length % settingByteSize != 0) {
                log("invalid settings frame size", {});
                _.sendGoAway(frame.streamIdentifier, ErrorCode.FRAME_SIZE_ERROR, "Invalid settings frame size", _.lastStreamID, true);
                return data.len;
            }
        } else {
            if (isACK) {
                // we received an ACK
                log("settings frame ACK", {});

                // we can now write any request
                if (_.outstandingSettings > 0) {
                    _.outstandingSettings -= 1;

                    // Per RFC 7540 Section 6.9.2: When INITIAL_WINDOW_SIZE changes, adjust
                    // all existing stream windows by the difference. Now that our SETTINGS
                    // is ACKed, the peer knows about our window size, so we can enforce it.
                    if (_.outstandingSettings == 0 && _.localSettings.initialWindowSize != DEFAULT_WINDOW_SIZE) {
                        const old_size: i64 = DEFAULT_WINDOW_SIZE;
                        const new_size: i64 = _.localSettings.initialWindowSize;
                        const delta = new_size - old_size;
                        let it = _.streams.valueIterator();
                        while (item := it.next()) {
                            let stream = item.*;
                            // Adjust the stream's local window size by the delta
                            if (delta >= 0) {
                                stream.windowSize = stream.windowSize +| @as(u64, @intCast(delta));
                            } else {
                                stream.windowSize = stream.windowSize -| @as(u64, @intCast(-delta));
                            }
                        }
                        log("adjusted stream windows by delta {} (old: {}, new: {})", .{ delta, old_size, new_size });
                    }
                }

                _.dispatch(.onLocalSettings, _.localSettings.toJS(_.handlers.globalObject));
            } else {
                const _flush_guard = FlushGuard{ parser: _ };
                const _win_guard = IncrementWindowGuard{ parser: _ };
                log("empty settings has remoteSettings? {}", .{_.remoteSettings != null});
                if (_.remoteSettings == null) {

                    // ok empty settings so default settings
                    let remoteSettings: FullSettingsPayload = {};
                    _.remoteSettings = remoteSettings;
                    log("remoteSettings.initialWindowSize: {} {} {}", .{ remoteSettings.initialWindowSize, _.remoteUsedWindowSize, _.remoteWindowSize });

                    if (remoteSettings.initialWindowSize >= _.remoteWindowSize) {
                        let it = _.streams.valueIterator();
                        while (item := it.next()) {
                            let stream = item.*;
                            if (remoteSettings.initialWindowSize >= stream.remoteWindowSize) {
                                stream.remoteWindowSize = remoteSettings.initialWindowSize;
                            }
                        }
                    }
                    _.dispatch(.onRemoteSettings, remoteSettings.toJS(_.handlers.globalObject));
                }
            }

            _.currentFrame = null;
            return 0;
        }
        if (content := _.handleIncommingPayload(data, frame.streamIdentifier)) {
            const _flush_guard = FlushGuard{ parser: _ };
            const _win_guard = IncrementWindowGuard{ parser: _ };
            let remoteSettings: FullSettingsPayload = _.remoteSettings ?? {};
            let i: usize = 0;
            const payload = content.data;
            while (i < payload.len) : (i += settingByteSize) {
                let unit: SettingsPayloadUnit = undefined;
                SettingsPayloadUnit.@"from"(&unit, payload[i .. i + settingByteSize], 0, true);
                remoteSettings.updateWith(unit);
                log("remoteSettings: {} {} isServer: {}", .{ @as(SettingsType, @enumFromInt(unit.@"type")), unit.value, _.isServer });
            }
            _.readBuffer.reset();
            _.remoteSettings = remoteSettings;
            log("remoteSettings.initialWindowSize: {} {} {}", .{ remoteSettings.initialWindowSize, _.remoteUsedWindowSize, _.remoteWindowSize });
            if (remoteSettings.initialWindowSize >= _.remoteWindowSize) {
                let it = _.streams.valueIterator();
                while (item := it.next()) {
                    let stream = item.*;
                    if (remoteSettings.initialWindowSize >= stream.remoteWindowSize) {
                        stream.remoteWindowSize = remoteSettings.initialWindowSize;
                    }
                }
            }
            _.dispatch(.onRemoteSettings, remoteSettings.toJS(_.handlers.globalObject));
            return content.end;
        }
        // needs more data
        return data.len;
    }

    /// Returned *Stream is heap-allocated and stable for the lifetime of this H2FrameParser.
    fn handleReceivedStreamID(*_, streamIdentifier: u32): (*Stream)? {
        // connection stream
        if (streamIdentifier == 0) {
            return null;
        }

        // already exists
        if (stream := _.streams.get(streamIdentifier)) {
            return stream;
        }

        if (streamIdentifier > _.lastStreamID) {
            _.lastStreamID = streamIdentifier;
        }

        // new stream open
        // Per RFC 7540 Section 6.5.1: The sender of SETTINGS can only rely on the
        // setting being applied AFTER receiving SETTINGS_ACK. Until then, the peer
        // hasn't seen our settings and uses the default window size.
        // So we must accept data up to DEFAULT_WINDOW_SIZE until our SETTINGS is ACKed.
        const local_window_size = if (_.outstandingSettings > 0)
            DEFAULT_WINDOW_SIZE
        else
            _.localSettings.initialWindowSize;
        const stream = Stream.new(Stream.init(
            streamIdentifier,
            local_window_size,
            if (_.remoteSettings != null) _.remoteSettings.?.initialWindowSize else DEFAULT_WINDOW_SIZE,
            _.paddingStrategy,
        ));
        bun.handleOom(_.streams.put(streamIdentifier, stream));

        const _this_value_opt = _.strong_this.tryGet();
        if (_this_value_opt == null) return stream;
        const this_value = _this_value_opt.?;
        const _ctx_value_opt = js.gc.context.get(this_value);
        if (_ctx_value_opt == null) return stream;
        const ctx_value = _ctx_value_opt.?;
        const _callback_opt = js.gc.onStreamStart.get(this_value);
        if (_callback_opt == null) return stream;
        const callback = _callback_opt.?;

        const _args = [ ctx_value, jsc.JSValue.jsNumber(streamIdentifier) ];
        * = callback.call(_.handlers.globalObject, ctx_value, &_args) catch |e| {
            _.handlers.globalObject.reportActiveExceptionAsUnhandled(e);
        };
        return stream;
    }

    fn readBytes(*_, bytes: []const u8): bun.JSError!usize {
        log("read {}", .{bytes.len});
        if (_.isServer && _.prefaceReceivedLen < 24) {
            // Handle Server Preface
            const preface_missing: usize = @as(usize, 24) - @as(usize, _.prefaceReceivedLen);
            const preface_available = @min(preface_missing, bytes.len);
            if (!strings.eql(bytes[0..preface_available], "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"[_.prefaceReceivedLen .. preface_available + _.prefaceReceivedLen])) {
                // invalid preface
                log("invalid preface", {});
                _.sendGoAway(0, ErrorCode.PROTOCOL_ERROR, "Invalid preface", _.lastStreamID, true);
                return preface_available;
            }
            _.prefaceReceivedLen += @as(u8, @intCast(preface_available));
            return preface_available;
        }
        if (header := _.currentFrame) {
            log("current frame {s} {} {} {} {}", .{ if (_.isServer) "server" else "client", header.@"type", header.length, header.flags, header.streamIdentifier });

            const stream = _.handleReceivedStreamID(header.streamIdentifier);
            return switch (header.@"type") {
                @intFromEnum(FrameType.HTTP_FRAME_SETTINGS) => _.handleSettingsFrame(header, bytes),
                @intFromEnum(FrameType.HTTP_FRAME_WINDOW_UPDATE) => _.handleWindowUpdateFrame(header, bytes, stream),
                @intFromEnum(FrameType.HTTP_FRAME_HEADERS) => _.handleHeadersFrame(header, bytes, stream),
                @intFromEnum(FrameType.HTTP_FRAME_DATA) => _.handleDataFrame(header, bytes, stream),
                @intFromEnum(FrameType.HTTP_FRAME_CONTINUATION) => _.handleContinuationFrame(header, bytes, stream),
                @intFromEnum(FrameType.HTTP_FRAME_PRIORITY) => _.handlePriorityFrame(header, bytes, stream),
                @intFromEnum(FrameType.HTTP_FRAME_PING) => _.handlePingFrame(header, bytes, stream),
                @intFromEnum(FrameType.HTTP_FRAME_GOAWAY) => _.handleGoAwayFrame(header, bytes, stream),
                @intFromEnum(FrameType.HTTP_FRAME_RST_STREAM) => _.handleRSTStreamFrame(header, bytes, stream),
                @intFromEnum(FrameType.HTTP_FRAME_ALTSVC) => _.handleAltsvcFrame(header, bytes, stream),
                @intFromEnum(FrameType.HTTP_FRAME_ORIGIN) => _.handleOriginFrame(header, bytes, stream),
                else => {
                    _.sendGoAway(header.streamIdentifier, ErrorCode.PROTOCOL_ERROR, "Unknown frame type", _.lastStreamID, true);
                    return bytes.len;
                },
            };
        }

        // nothing to do
        if (bytes.len == 0) return bytes.len;

        const buffered_data = _.readBuffer.list.items.len;

        let header: FrameHeader = { flags: 0 };
        // we can have less than 9 bytes buffered
        if (buffered_data > 0) {
            const total = buffered_data + bytes.len;
            if (total < FrameHeader.byteSize) {
                // buffer more data
                * = bun.handleOom(_.readBuffer.appendSlice(bytes));
                _.globalThis.vm().reportExtraMemory(bytes.len);

                return bytes.len;
            }
            FrameHeader.@"from"(&header, _.readBuffer.list.items[0..buffered_data], 0, false);
            const needed = FrameHeader.byteSize - buffered_data;
            FrameHeader.@"from"(&header, bytes[0..needed], buffered_data, true);
            // ignore the reserved bit
            const id = UInt31WithReserved.@"from"(header.streamIdentifier);
            header.streamIdentifier = @intCast(id.uint31);
            // reset for later use
            _.readBuffer.reset();

            _.currentFrame = header;
            _.remainingLength = header.length;
            log("new frame {} {} {} {}", .{ header.@"type", header.length, header.flags, header.streamIdentifier });
            const stream = _.handleReceivedStreamID(header.streamIdentifier);

            return switch (header.@"type") {
                @intFromEnum(FrameType.HTTP_FRAME_SETTINGS) => _.handleSettingsFrame(header, bytes[needed..]) + needed,
                @intFromEnum(FrameType.HTTP_FRAME_WINDOW_UPDATE) => _.handleWindowUpdateFrame(header, bytes[needed..], stream) + needed,
                @intFromEnum(FrameType.HTTP_FRAME_HEADERS) => (_.handleHeadersFrame(header, bytes[needed..], stream)?) + needed,
                @intFromEnum(FrameType.HTTP_FRAME_DATA) => _.handleDataFrame(header, bytes[needed..], stream) + needed,
                @intFromEnum(FrameType.HTTP_FRAME_CONTINUATION) => (_.handleContinuationFrame(header, bytes[needed..], stream)?) + needed,
                @intFromEnum(FrameType.HTTP_FRAME_PRIORITY) => _.handlePriorityFrame(header, bytes[needed..], stream) + needed,
                @intFromEnum(FrameType.HTTP_FRAME_PING) => _.handlePingFrame(header, bytes[needed..], stream) + needed,
                @intFromEnum(FrameType.HTTP_FRAME_GOAWAY) => _.handleGoAwayFrame(header, bytes[needed..], stream) + needed,
                @intFromEnum(FrameType.HTTP_FRAME_RST_STREAM) => _.handleRSTStreamFrame(header, bytes[needed..], stream) + needed,
                @intFromEnum(FrameType.HTTP_FRAME_ALTSVC) => (_.handleAltsvcFrame(header, bytes[needed..], stream)?) + needed,
                @intFromEnum(FrameType.HTTP_FRAME_ORIGIN) => (_.handleOriginFrame(header, bytes[needed..], stream)?) + needed,
                else => {
                    _.sendGoAway(header.streamIdentifier, ErrorCode.PROTOCOL_ERROR, "Unknown frame type", _.lastStreamID, true);
                    return bytes.len;
                },
            };
        }

        if (bytes.len < FrameHeader.byteSize) {
            // buffer more dheaderata
            bun.handleOom(_.readBuffer.appendSlice(bytes));
            _.globalThis.vm().reportExtraMemory(bytes.len);

            return bytes.len;
        }

        FrameHeader.@"from"(&header, bytes[0..FrameHeader.byteSize], 0, true);

        log("new frame {s} {} {} {} {}", .{ if (_.isServer) "server" else "client", header.@"type", header.length, header.flags, header.streamIdentifier });
        _.currentFrame = header;
        _.remainingLength = header.length;
        const stream = _.handleReceivedStreamID(header.streamIdentifier);
        return switch (header.@"type") {
            @intFromEnum(FrameType.HTTP_FRAME_SETTINGS) => _.handleSettingsFrame(header, bytes[FrameHeader.byteSize..]) + FrameHeader.byteSize,
            @intFromEnum(FrameType.HTTP_FRAME_WINDOW_UPDATE) => _.handleWindowUpdateFrame(header, bytes[FrameHeader.byteSize..], stream) + FrameHeader.byteSize,
            @intFromEnum(FrameType.HTTP_FRAME_HEADERS) => (_.handleHeadersFrame(header, bytes[FrameHeader.byteSize..], stream)?) + FrameHeader.byteSize,
            @intFromEnum(FrameType.HTTP_FRAME_DATA) => _.handleDataFrame(header, bytes[FrameHeader.byteSize..], stream) + FrameHeader.byteSize,
            @intFromEnum(FrameType.HTTP_FRAME_CONTINUATION) => (_.handleContinuationFrame(header, bytes[FrameHeader.byteSize..], stream)?) + FrameHeader.byteSize,
            @intFromEnum(FrameType.HTTP_FRAME_PRIORITY) => _.handlePriorityFrame(header, bytes[FrameHeader.byteSize..], stream) + FrameHeader.byteSize,
            @intFromEnum(FrameType.HTTP_FRAME_PING) => _.handlePingFrame(header, bytes[FrameHeader.byteSize..], stream) + FrameHeader.byteSize,
            @intFromEnum(FrameType.HTTP_FRAME_GOAWAY) => _.handleGoAwayFrame(header, bytes[FrameHeader.byteSize..], stream) + FrameHeader.byteSize,
            @intFromEnum(FrameType.HTTP_FRAME_RST_STREAM) => _.handleRSTStreamFrame(header, bytes[FrameHeader.byteSize..], stream) + FrameHeader.byteSize,
            @intFromEnum(FrameType.HTTP_FRAME_ALTSVC) => (_.handleAltsvcFrame(header, bytes[FrameHeader.byteSize..], stream)?) + FrameHeader.byteSize,
            @intFromEnum(FrameType.HTTP_FRAME_ORIGIN) => (_.handleOriginFrame(header, bytes[FrameHeader.byteSize..], stream)?) + FrameHeader.byteSize,
            else => {
                _.sendGoAway(header.streamIdentifier, ErrorCode.PROTOCOL_ERROR, "Unknown frame type", _.lastStreamID, true);
                return bytes.len;
            },
        };
    }



    fn toWriter(*_): DirectWriterStruct {
        return DirectWriterStruct{ writer: _ };
    }

    fn setEncoding(*_, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
        jsc.markBinding(@src());
        const args_list = callframe.arguments_old(1);
        if (args_list.len < 1) {
            return globalObject.throw("Expected encoding argument", {});
        }
        const _opt_2808 = BinaryType.fromJSValue(globalObject, args_list.ptr[0]);
        if (_opt_2808 == null) {
            const e = jsc.toInvalidArguments("Expected 'binaryType' to be 'arraybuffer', 'uint8array', 'buffer'", {}, globalObject).asObjectRef();
            return globalObject.throwValue(e);
        }
        _.handlers.binary_type = _opt_2808.?;

        return JSValue.js_undefined;
    }

    fn loadSettingsFromJSValue(*_, globalObject: *jsc.JSGlobalObject, options: jsc.JSValue): bun.JSError!void {
        if (options.isEmptyOrUndefinedOrNull() || !options.isObject()) {
            return globalObject.throw("Expected settings to be a object", {});
        }

        if (headerTableSize := options.get(globalObject, "headerTableSize")?) {
            if (headerTableSize.isNumber()) {
                const value = headerTableSize.asNumber();
                if (value < 0 || value > MAX_HEADER_TABLE_SIZE_F64) {
                    return globalObject.ERR(.HTTP2_INVALID_SETTING_VALUE_RangeError, "Expected headerTableSize to be a number between 0 and 2^32-1", {}).throw();
                }
                _.localSettings.headerTableSize = @intFromFloat(value);
            } else if (!headerTableSize.isEmptyOrUndefinedOrNull()) {
                return globalObject.ERR(.HTTP2_INVALID_SETTING_VALUE_RangeError, "Expected headerTableSize to be a number", {}).throw();
            }
        }

        if (enablePush := options.get(globalObject, "enablePush")?) {
            if (enablePush.isBoolean()) {
                _.localSettings.enablePush = if (enablePush.asBoolean()) 1 else 0;
            } else if (!enablePush.isUndefined()) {
                return globalObject.ERR(.HTTP2_INVALID_SETTING_VALUE, "Expected enablePush to be a boolean", {}).throw();
            }
        }

        if (initialWindowSize := options.get(globalObject, "initialWindowSize")?) {
            if (initialWindowSize.isNumber()) {
                const value = initialWindowSize.asNumber();
                if (value < 0 || value > MAX_WINDOW_SIZE_F64) {
                    return globalObject.ERR(.HTTP2_INVALID_SETTING_VALUE_RangeError, "Expected initialWindowSize to be a number between 0 and 2^32-1", {}).throw();
                }
                log("initialWindowSize: {d}", .{@as(u32, @intFromFloat(value))});
                _.localSettings.initialWindowSize = @intFromFloat(value);
            } else if (!initialWindowSize.isEmptyOrUndefinedOrNull()) {
                return globalObject.ERR(.HTTP2_INVALID_SETTING_VALUE_RangeError, "Expected initialWindowSize to be a number", {}).throw();
            }
        }

        if (maxFrameSize := options.get(globalObject, "maxFrameSize")?) {
            if (maxFrameSize.isNumber()) {
                const value = maxFrameSize.asNumber();
                if (value < 16384 || value > MAX_FRAME_SIZE_F64) {
                    return globalObject.ERR(.HTTP2_INVALID_SETTING_VALUE_RangeError, "Expected maxFrameSize to be a number between 16,384 and 2^24-1", {}).throw();
                }
                _.localSettings.maxFrameSize = @intFromFloat(value);
            } else if (!maxFrameSize.isEmptyOrUndefinedOrNull()) {
                return globalObject.ERR(.HTTP2_INVALID_SETTING_VALUE_RangeError, "Expected maxFrameSize to be a number", {}).throw();
            }
        }

        if (maxConcurrentStreams := options.get(globalObject, "maxConcurrentStreams")?) {
            if (maxConcurrentStreams.isNumber()) {
                const value = maxConcurrentStreams.asNumber();
                if (value < 0 || value > MAX_HEADER_TABLE_SIZE_F64) {
                    return globalObject.ERR(.HTTP2_INVALID_SETTING_VALUE_RangeError, "Expected maxConcurrentStreams to be a number between 0 and 2^32-1", {}).throw();
                }
                _.localSettings.maxConcurrentStreams = @intFromFloat(value);
            } else if (!maxConcurrentStreams.isEmptyOrUndefinedOrNull()) {
                return globalObject.ERR(.HTTP2_INVALID_SETTING_VALUE_RangeError, "Expected maxConcurrentStreams to be a number", {}).throw();
            }
        }

        if (maxHeaderListSize := options.get(globalObject, "maxHeaderListSize")?) {
            if (maxHeaderListSize.isNumber()) {
                const value = maxHeaderListSize.asNumber();
                if (value < 0 || value > MAX_HEADER_TABLE_SIZE_F64) {
                    return globalObject.ERR(.HTTP2_INVALID_SETTING_VALUE_RangeError, "Expected maxHeaderListSize to be a number between 0 and 2^32-1", {}).throw();
                }
                _.localSettings.maxHeaderListSize = @intFromFloat(value);
            } else if (!maxHeaderListSize.isEmptyOrUndefinedOrNull()) {
                return globalObject.ERR(.HTTP2_INVALID_SETTING_VALUE_RangeError, "Expected maxHeaderListSize to be a number", {}).throw();
            }
        }

        if (maxHeaderSize := options.get(globalObject, "maxHeaderSize")?) {
            if (maxHeaderSize.isNumber()) {
                const value = maxHeaderSize.asNumber();
                if (value < 0 || value > MAX_HEADER_TABLE_SIZE_F64) {
                    return globalObject.ERR(.HTTP2_INVALID_SETTING_VALUE_RangeError, "Expected maxHeaderSize to be a number between 0 and 2^32-1", {}).throw();
                }
                _.localSettings.maxHeaderListSize = @intFromFloat(value);
            } else if (!maxHeaderSize.isEmptyOrUndefinedOrNull()) {
                return globalObject.ERR(.HTTP2_INVALID_SETTING_VALUE_RangeError, "Expected maxHeaderSize to be a number", {}).throw();
            }
        }

        // Validate customSettings
        if (customSettings := options.get(globalObject, "customSettings")?) {
            if (!customSettings.isUndefined()) {
                const _custom_settings_obj_opt_2904 = customSettings.getObject();
                if (_custom_settings_obj_opt_2904 == null) {
                    return globalObject.ERR(.HTTP2_INVALID_SETTING_VALUE, "Expected customSettings to be an object", {}).throw();
                }
                const custom_settings_obj = _custom_settings_obj_opt_2904.?;

                let count: usize = 0;
                let iter_guard = DeinitGuard<JSPropIter>{
                    val: try JSPropIter.init(globalObject, custom_settings_obj),
                };

                while (prop_name := iter_guard.val.next()?) {
                    count += 1;
                    if (count > MAX_CUSTOM_SETTINGS) {
                        return globalObject.ERR(.HTTP2_TOO_MANY_CUSTOM_SETTINGS, "Number of custom settings exceeds MAX_ADDITIONAL_SETTINGS", {}).throw();
                    }

                    // Validate setting ID (key) is in range [0, 0xFFFF]
                    const setting_id_str = DeinitGuard<bun.String.Slice>{ val: prop_name.toUTF8(bun.default_allocator) };
                    const setting_id = std.fmt.parseInt(u32, setting_id_str.val.slice(), 10) catch {
                        return globalObject.ERR(.HTTP2_INVALID_SETTING_VALUE_RangeError, "Invalid custom setting identifier", {}).throw();
                    };
                    if (setting_id > 0xFFFF) {
                        return globalObject.ERR(.HTTP2_INVALID_SETTING_VALUE_RangeError, "Invalid custom setting identifier", {}).throw();
                    }

                    // Validate setting value is in range [0, 2^32-1]
                    const setting_value = iter_guard.val.value;
                    if (setting_value.isNumber()) {
                        const value = setting_value.asNumber();
                        if (value < 0 || value > MAX_HEADER_TABLE_SIZE_F64) {
                            return globalObject.ERR(.HTTP2_INVALID_SETTING_VALUE_RangeError, "Invalid custom setting value", {}).throw();
                        }
                    } else {
                        return globalObject.ERR(.HTTP2_INVALID_SETTING_VALUE_RangeError, "Expected custom setting value to be a number", {}).throw();
                    }
                }
            }
        }
        return;
    }

    fn updateSettings(*_, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
        jsc.markBinding(@src());
        const args_list = callframe.arguments_old(1);
        if (args_list.len < 1) {
            return globalObject.throw("Expected settings argument", {});
        }

        const options = args_list.ptr[0];

        _.loadSettingsFromJSValue(globalObject, options)?;

        return JSValue.jsBoolean(_.setSettings(_.localSettings));
    }

    fn setLocalWindowSize(*_, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
        jsc.markBinding(@src());
        const args_list = callframe.arguments_old(1);
        if (args_list.len < 1) {
            return globalObject.throwInvalidArguments("Expected windowSize argument", {});
        }
        const windowSize = args_list.ptr[0];
        if (!windowSize.isNumber()) {
            return globalObject.throwInvalidArguments("Expected windowSize to be a number", {});
        }
        const windowSizeValue: u32 = windowSize.to(u32);
        if (_.usedWindowSize > windowSizeValue) {
            return globalObject.throwInvalidArguments("Expected windowSize to be greater than usedWindowSize", {});
        }
        const oldWindowSize = _.windowSize;
        _.windowSize = windowSizeValue;
        if (_.localSettings.initialWindowSize < windowSizeValue) {
            _.localSettings.initialWindowSize = windowSizeValue;
        }
        // Send a connection-level WINDOW_UPDATE frame to the peer so it knows
        // about the increased window.  Per RFC 9113 Section 6.9, the
        // INITIAL_WINDOW_SIZE setting only applies to stream-level windows;
        // the connection-level window must be updated explicitly.
        if (windowSizeValue > oldWindowSize) {
            const increment: u31 = @truncate(windowSizeValue - oldWindowSize);
            _.sendWindowUpdate(0, UInt31WithReserved.init(increment, false));
        }
        let it = _.streams.valueIterator();
        while (item := it.next()) {
            let stream = item.*;
            if (stream.usedWindowSize > windowSizeValue) {
                continue;
            }
            stream.windowSize = windowSizeValue;
        }
        return JSValue.js_undefined;
    }

    fn getCurrentState(*_, globalObject: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!JSValue {
        jsc.markBinding(@src());
        let result = JSValue.createEmptyObject(globalObject, 9);
        result.put(globalObject, jsc.ZigString.static("effectiveLocalWindowSize"), jsc.JSValue.jsNumber(_.windowSize));
        result.put(globalObject, jsc.ZigString.static("effectiveRecvDataLength"), jsc.JSValue.jsNumber(_.windowSize - _.usedWindowSize));
        result.put(globalObject, jsc.ZigString.static("nextStreamID"), jsc.JSValue.jsNumber(_.getNextStreamID()));
        result.put(globalObject, jsc.ZigString.static("lastProcStreamID"), jsc.JSValue.jsNumber(_.lastStreamID));

        const settings: FullSettingsPayload = _.remoteSettings ?? {};
        result.put(globalObject, jsc.ZigString.static("remoteWindowSize"), jsc.JSValue.jsNumber(settings.initialWindowSize));
        result.put(globalObject, jsc.ZigString.static("localWindowSize"), jsc.JSValue.jsNumber(_.localSettings.initialWindowSize));
        result.put(globalObject, jsc.ZigString.static("deflateDynamicTableSize"), jsc.JSValue.jsNumber(_.localSettings.headerTableSize));
        result.put(globalObject, jsc.ZigString.static("inflateDynamicTableSize"), jsc.JSValue.jsNumber(_.localSettings.headerTableSize));
        result.put(globalObject, jsc.ZigString.static("outboundQueueSize"), jsc.JSValue.jsNumber(_.outboundQueueSize));
        return result;
    }

    fn goaway(*_, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
        jsc.markBinding(@src());
        const args_list = callframe.arguments_old(3);
        if (args_list.len < 1) {
            return globalObject.throw("Expected errorCode argument", {});
        }

        const error_code_arg = args_list.ptr[0];

        if (!error_code_arg.isNumber()) {
            return globalObject.throw("Expected errorCode to be a number", {});
        }
        const errorCode = error_code_arg.toInt32();
        if (errorCode < 1 && errorCode > 13) {
            return globalObject.throw("invalid errorCode", {});
        }

        let lastStreamID = _.lastStreamID;
        if (args_list.len >= 2) {
            const last_stream_arg = args_list.ptr[1];
            if (!last_stream_arg.isEmptyOrUndefinedOrNull()) {
                if (!last_stream_arg.isNumber()) {
                    return globalObject.throw("Expected lastStreamId to be a number", {});
                }
                const id = last_stream_arg.toInt32();
                if (id < 0 && id > MAX_STREAM_ID) {
                    return globalObject.throw("Expected lastStreamId to be a number between 1 and 2147483647", {});
                }
                lastStreamID = @intCast(id);
            }
            if (args_list.len >= 3) {
                const opaque_data_arg = args_list.ptr[2];
                if (!opaque_data_arg.isEmptyOrUndefinedOrNull()) {
                    if (array_buffer := opaque_data_arg.asArrayBuffer(globalObject)) {
                        const slice = array_buffer.byteSlice();
                        _.sendGoAway(0, @enumFromInt(errorCode), slice, lastStreamID, false);
                        return JSValue.js_undefined;
                    }
                }
            }
        }

        _.sendGoAway(0, @enumFromInt(errorCode), "", lastStreamID, false);
        return JSValue.js_undefined;
    }

    fn ping(*_, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
        jsc.markBinding(@src());
        const args_list = callframe.arguments_old(1);
        if (args_list.len < 1) {
            return globalObject.throw("Expected payload argument", {});
        }

        if (_.outStandingPings >= _.maxOutstandingPings) {
            const exception = globalObject.toTypeError(.HTTP2_PING_CANCEL, "HTTP2 ping cancelled", {});
            return globalObject.throwValue(exception);
        }

        if (array_buffer := args_list.ptr[0].asArrayBuffer(globalObject)) {
            const slice = array_buffer.slice();
            _.sendPing(false, slice);
            return JSValue.js_undefined;
        }

        return globalObject.throw("Expected payload to be a Buffer", {});
    }

    fn origin(*_, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
        jsc.markBinding(@src());
        const origin_arg = callframe.argument(0);
        if (origin_arg.isEmptyOrUndefinedOrNull()) {
            // empty origin frame
            let buffer: [FRAME_HEADER_SIZE]u8 = undefined;
            @memset(&buffer, 0);
            let stream = std.io.fixedBufferStream(&buffer);
            const writer = stream.writer();

            let frame: FrameHeader = {
                @"type": @intFromEnum(FrameType.HTTP_FRAME_ORIGIN),
                flags: 0,
                streamIdentifier: 0,
                length: 0,
            };
            * = frame.write(@TypeOf(writer), writer);
            * = _.write(&buffer);
            return JSValue.js_undefined;
        }

        if (origin_arg.isString()) {
            const origin_string = DeinitGuard<bun.String.Slice>{ val: origin_arg.toSlice(globalObject, bun.default_allocator)? };
            const slice = origin_string.val.slice();
            if (slice.len + 2 > 16384) {
                const exception = globalObject.toTypeError(.HTTP2_ORIGIN_LENGTH, "HTTP/2 ORIGIN frames are limited to 16382 bytes", {});
                return globalObject.throwValue(exception);
            }

            let buffer: [FRAME_PLUS_2]u8 = undefined;
            @memset(&buffer, 0);
            let stream = std.io.fixedBufferStream(&buffer);
            const writer = stream.writer();

            let frame: FrameHeader = {
                @"type": @intFromEnum(FrameType.HTTP_FRAME_ORIGIN),
                flags: 0,
                streamIdentifier: 0,
                length: @intCast(slice.len + 2),
            };
            * = frame.write(@TypeOf(writer), writer);
            * = writer.writeInt(u16, @intCast(slice.len), .big) catch 0;
            * = _.write(&buffer);
            if (slice.len > 0) {
                * = _.write(slice);
            }
        } else if (origin_arg.isArray()) {
            let buffer: [FRAME_PLUS_16384]u8 = undefined;
            @memset(&buffer, 0);
            let stream = std.io.fixedBufferStream(&buffer);
            const writer = stream.writer();
            stream.seekTo(FrameHeader.byteSize) catch {};
            let value_iter = origin_arg.arrayIterator(globalObject)?;

            while (item := value_iter.next()?) {
                if (!item.isString()) {
                    return globalObject.throwInvalidArguments("Expected origin to be a string or an array of strings", {});
                }
                const origin_string = DeinitGuard<bun.String.Slice>{ val: item.toSlice(globalObject, bun.default_allocator)? };
                const slice = origin_string.val.slice();
                * = writer.writeInt(u16, @intCast(slice.len), .big) catch {
                    const exception = globalObject.toTypeError(.HTTP2_ORIGIN_LENGTH, "HTTP/2 ORIGIN frames are limited to 16382 bytes", {});
                    return globalObject.throwValue(exception);
                };

                * = writer.write(slice) catch {
                    const exception = globalObject.toTypeError(.HTTP2_ORIGIN_LENGTH, "HTTP/2 ORIGIN frames are limited to 16382 bytes", {});
                    return globalObject.throwValue(exception);
                };
            }

            const total_length: u32 = @intCast(stream.getPos() catch FrameHeader.byteSize);
            let frame: FrameHeader = {
                @"type": @intFromEnum(FrameType.HTTP_FRAME_ORIGIN),
                flags: 0,
                streamIdentifier: 0,
                length: @intCast(total_length - FrameHeader.byteSize), // payload length
            };
            stream.reset();
            * = frame.write(@TypeOf(writer), writer);
            * = _.write(buffer[0..total_length]);
        }
        return JSValue.js_undefined;
    }

    fn altsvc(*_, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
        jsc.markBinding(@src());
        let origin_slice = OptDeinitGuard<bun.String.Slice>{ val: null };
        let value_slice = OptDeinitGuard<bun.String.Slice>{ val: null };

        let origin_str: []const u8 = "";
        let value_str: []const u8 = "";
        let stream_id: u32 = 0;
        const origin_string = callframe.argument(0);
        if (!origin_string.isEmptyOrUndefinedOrNull()) {
            if (!origin_string.isString()) {
                return globalObject.throwInvalidArgumentTypeValue("origin", "origin", origin_string);
            }

            origin_slice.val = origin_string.toSlice(globalObject, bun.default_allocator)?;
            origin_str = origin_slice.val.?.slice();
        }

        const value_string = callframe.argument(1);
        if (!value_string.isEmptyOrUndefinedOrNull()) {
            if (!value_string.isString()) {
                return globalObject.throwInvalidArgumentTypeValue("value", "value", value_string);
            }
            value_slice.val = value_string.toSlice(globalObject, bun.default_allocator)?;
            value_str = value_slice.val.?.slice();
        }

        const stream_id_js = callframe.argument(2);
        if (!stream_id_js.isEmptyOrUndefinedOrNull()) {
            if (!stream_id_js.isNumber()) {
                return globalObject.throw("Expected streamId to be a number", {});
            }
            stream_id = stream_id_js.toU32();
        }
        if (stream_id > 0) {
            // dont error but dont send frame to invalid stream id
            if (_.streams.get(stream_id) == null) {
                return JSValue.js_undefined;
            }
        }
        _.sendAltSvc(stream_id, origin_str, value_str);
        return JSValue.js_undefined;
    }

    fn getEndAfterHeaders(*_, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
        jsc.markBinding(@src());
        const args_list = callframe.arguments_old(1);
        if (args_list.len < 1) {
            return globalObject.throw("Expected stream argument", {});
        }
        const stream_arg = args_list.ptr[0];

        if (!stream_arg.isNumber()) {
            return globalObject.throw("Invalid stream id", {});
        }

        const stream_id = stream_arg.toU32();
        if (stream_id == 0) {
            return globalObject.throw("Invalid stream id", {});
        }

        let stream: *Stream = undefined;
        if (_sv_3239 := _.streams.get(stream_id)) {
            stream = _sv_3239;
        } else {
            return globalObject.throw("Invalid stream id", {});
        }

        return jsc.JSValue.jsBoolean(stream.endAfterHeaders);
    }

    fn isStreamAborted(*_, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
        jsc.markBinding(@src());
        const args_list = callframe.arguments_old(1);
        if (args_list.len < 1) {
            return globalObject.throw("Expected stream argument", {});
        }
        const stream_arg = args_list.ptr[0];

        if (!stream_arg.isNumber()) {
            return globalObject.throw("Invalid stream id", {});
        }

        const stream_id = stream_arg.toU32();
        if (stream_id == 0) {
            return globalObject.throw("Invalid stream id", {});
        }

        let stream: *Stream = undefined;
        if (_sv_3263 := _.streams.get(stream_id)) {
            stream = _sv_3263;
        } else {
            return globalObject.throw("Invalid stream id", {});
        }

        if (signal_ref := stream.signal) {
            return jsc.JSValue.jsBoolean(signal_ref.isAborted());
        }
        // closed with cancel = aborted
        return jsc.JSValue.jsBoolean(stream.state == .CLOSED && stream.rstCode == @intFromEnum(ErrorCode.CANCEL));
    }

    fn getStreamState(*_, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
        jsc.markBinding(@src());
        const args_list = callframe.arguments_old(1);
        if (args_list.len < 1) {
            return globalObject.throw("Expected stream argument", {});
        }
        const stream_arg = args_list.ptr[0];

        if (!stream_arg.isNumber()) {
            return globalObject.throw("Invalid stream id", {});
        }

        const stream_id = stream_arg.toU32();
        if (stream_id == 0) {
            return globalObject.throw("Invalid stream id", {});
        }

        let stream: *Stream = undefined;
        if (_sv_3291 := _.streams.get(stream_id)) {
            stream = _sv_3291;
        } else {
            return globalObject.throw("Invalid stream id", {});
        }
        let state = jsc.JSValue.createEmptyObject(globalObject, 6);

        state.put(globalObject, jsc.ZigString.static("localWindowSize"), jsc.JSValue.jsNumber(stream.windowSize));
        state.put(globalObject, jsc.ZigString.static("state"), jsc.JSValue.jsNumber(@intFromEnum(stream.state)));
        const local_close: i32 = if (stream.canSendData()) 0 else 1;
        const remote_close: i32 = if (stream.canReceiveData()) 0 else 1;
        state.put(globalObject, jsc.ZigString.static("localClose"), jsc.JSValue.jsNumber(local_close));
        state.put(globalObject, jsc.ZigString.static("remoteClose"), jsc.JSValue.jsNumber(remote_close));
        // TODO: sumDependencyWeight
        state.put(globalObject, jsc.ZigString.static("sumDependencyWeight"), jsc.JSValue.jsNumber(0));
        state.put(globalObject, jsc.ZigString.static("weight"), jsc.JSValue.jsNumber(stream.weight));

        return state;
    }

    fn setStreamPriority(*_, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
        jsc.markBinding(@src());
        const args_list = callframe.arguments_old(2);
        if (args_list.len < 2) {
            return globalObject.throw("Expected stream and options arguments", {});
        }
        const stream_arg = args_list.ptr[0];
        const options = args_list.ptr[1];

        if (!stream_arg.isNumber()) {
            return globalObject.throw("Invalid stream id", {});
        }

        const stream_id = stream_arg.toU32();
        if (stream_id == 0) {
            return globalObject.throw("Invalid stream id", {});
        }

        let stream: *Stream = undefined;
        if (_sv_3325 := _.streams.get(stream_id)) {
            stream = _sv_3325;
        } else {
            return globalObject.throw("Invalid stream id", {});
        }

        if (!stream.canSendData() && !stream.canReceiveData()) {
            return JSValue.false;
        }

        if (!options.isObject()) {
            return globalObject.throw("Invalid priority", {});
        }

        let weight = stream.weight;
        let exclusive = stream.exclusive;
        let parent_id = stream.streamDependency;
        let silent = false;
        if (js_weight := options.get(globalObject, "weight")?) {
            if (js_weight.isNumber()) {
                const weight_u32 = js_weight.toU32();
                if (weight_u32 > 255) {
                    return globalObject.throw("Invalid weight", {});
                }
                weight = @intCast(weight_u32);
            }
        }

        if (js_parent := options.get(globalObject, "parent")?) {
            if (js_parent.isNumber()) {
                parent_id = js_parent.toU32();
                if (parent_id == 0 || parent_id > MAX_STREAM_ID) {
                    return globalObject.throw("Invalid stream id", {});
                }
            }
        }

        if (js_exclusive := options.get(globalObject, "exclusive")?) {
            exclusive = js_exclusive.toBoolean();
        }

        if (js_silent := options.get(globalObject, "silent")?) {
            if (js_silent.isBoolean()) {
                silent = js_silent.asBoolean();
            } else {
                return globalObject.ERR(.INVALID_ARG_TYPE, "options.silent must be a boolean", {}).throw();
            }
        }
        if (parent_id == stream.id) {
            _.sendGoAway(stream.id, ErrorCode.PROTOCOL_ERROR, "Stream with self dependency", _.lastStreamID, true);
            return JSValue.false;
        }

        stream.streamDependency = parent_id;
        stream.exclusive = exclusive;
        stream.weight = @intCast(weight);

        if (!silent) {
            let stream_identifier: UInt31WithReserved = {
                reserved: stream.exclusive,
                uint31: @truncate(stream.streamDependency),
            };

            let priority: StreamPriority = {
                streamIdentifier: stream_identifier.toUInt32(),
                weight: @truncate(stream.weight),
            };
            let frame: FrameHeader = {
                @"type": @intFromEnum(FrameType.HTTP_FRAME_PRIORITY),
                flags: 0,
                streamIdentifier: stream.id,
                length: @intCast(StreamPriority.byteSize),
            };

            const writer = _.toWriter();
            * = frame.write(@TypeOf(writer), writer);
            * = priority.write(@TypeOf(writer), writer);
        }
        return JSValue.true;
    }

    fn rstStream(*_, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
        log("rstStream", {});
        jsc.markBinding(@src());
        const args_list = callframe.arguments_old(2);
        if (args_list.len < 2) {
            return globalObject.throw("Expected stream and code arguments", {});
        }
        const stream_arg = args_list.ptr[0];
        const error_arg = args_list.ptr[1];

        if (!stream_arg.isNumber()) {
            return globalObject.throw("Invalid stream id", {});
        }

        const stream_id = stream_arg.toU32();
        if (stream_id == 0 || stream_id > MAX_STREAM_ID) {
            return globalObject.throw("Invalid stream id", {});
        }

        let stream: *Stream = undefined;
        if (_sv_3423 := _.streams.get(stream_id)) {
            stream = _sv_3423;
        } else {
            return globalObject.throw("Invalid stream id", {});
        }
        if (!error_arg.isNumber()) {
            return globalObject.throw("Invalid ErrorCode", {});
        }

        const error_code = error_arg.toU32();

        _.endStream(stream, @enumFromInt(error_code));

        return JSValue.true;
    }



    // get memory usage in MB
    fn getSessionMemoryUsage(*_): usize {
        return (_.writeBuffer.len + _.queuedDataSize) / 1024 / 1024;
    }

    // get memory in bytes
    fn getBufferSize(*_, _: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!JSValue {
        jsc.markBinding(@src());
        return jsc.JSValue.jsNumber(_.writeBuffer.len + _.queuedDataSize);
    }

    fn sendData(*_, stream: *Stream, payload: []const u8, close: bool, callback: jsc.JSValue) {
        log("HTTP_FRAME_DATA {s} sendData({}, {}, {})", .{ if (_.isServer) "server" else "client", stream.id, payload.len, close });

        const writer = _.toWriter();
        const stream_id = stream.id;
        _.ref();
        let send_guard = SendDataFallbackGuard{
            parser: _,
            stream: stream,
            callback: callback,
            close: close,
            enqueued: false,
        };
        const can_close = close && !stream.waitForTrailers;
        if (payload.len == 0) {
            // empty payload we still need to send a frame
            let dataHeader: FrameHeader = {
                @"type": @intFromEnum(FrameType.HTTP_FRAME_DATA),
                flags: if (can_close) @intFromEnum(DataFrameFlags.END_STREAM) else 0,
                streamIdentifier: @intCast(stream_id),
                length: 0,
            };
            if (_.hasBackpressure() || _.outboundQueueSize > 0) {
                send_guard.enqueued = true;
                stream.queueFrame(_, "", callback, close);
            } else {
                * = dataHeader.write(@TypeOf(writer), writer);
            }
        } else {
            let offset: usize = 0;

            while (offset < payload.len) {
                // max frame size will always be at least 16384 (but we need to respect the flow control)
                let max_size = @min(@min(MAX_PAYLOAD_SIZE_WITHOUT_FRAME, _.remoteWindowSize -| _.remoteUsedWindowSize), stream.remoteWindowSize -| stream.remoteUsedWindowSize);
                let is_flow_control_limited = false;
                if (max_size == 0) {
                    is_flow_control_limited = true;
                    // this will be handled later if cannot send the entire payload in one frame
                    max_size = MAX_PAYLOAD_SIZE_WITHOUT_FRAME;
                }
                const size = @min(payload.len - offset, max_size);

                const slice = payload[offset..(size + offset)];
                offset += size;
                const end_stream = offset >= payload.len && can_close;

                if (_.hasBackpressure() || _.outboundQueueSize > 0 || is_flow_control_limited) {
                    send_guard.enqueued = true;
                    // write the full frame in memory and queue the frame
                    // the callback will only be called after the last frame is sended
                    stream.queueFrame(_, slice, if (offset >= payload.len) callback else .js_undefined, offset >= payload.len && close);
                } else {
                    const padding = stream.getPadding(size, max_size - 1);
                    const payload_size = size + (if (padding != 0) @as(usize, @intCast(padding)) + 1 else 0);
                    log("padding: {d} size: {d} max_size: {d} payload_size: {d}", .{ padding, size, max_size, payload_size });
                    stream.remoteUsedWindowSize += payload_size;
                    _.remoteUsedWindowSize += payload_size;
                    let flags: u8 = if (end_stream) @intFromEnum(DataFrameFlags.END_STREAM) else 0;
                    if (padding != 0) {
                        flags |= @intFromEnum(DataFrameFlags.PADDED);
                    }
                    let dataHeader: FrameHeader = {
                        @"type": @intFromEnum(FrameType.HTTP_FRAME_DATA),
                        flags: flags,
                        streamIdentifier: @intCast(stream_id),
                        length: @truncate(payload_size),
                    };
                    * = dataHeader.write(@TypeOf(writer), writer);
                    if (padding != 0) {
                        let buffer = shared_request_buffer[0..];
                        bun.memmove(buffer[1..][0..slice.len], slice);
                        buffer[0] = padding;
                        * = writer.write(buffer[0..payload_size]) catch 0;
                    } else {
                        * = writer.write(slice) catch 0;
                    }
                }
            }
        }
    }

    fn noTrailers(*_, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
        jsc.markBinding(@src());
        const args_list = callframe.arguments_old(1);
        if (args_list.len < 1) {
            return globalObject.throw("Expected stream, headers and sensitiveHeaders arguments", {});
        }

        const stream_arg = args_list.ptr[0];

        if (!stream_arg.isNumber()) {
            return globalObject.throw("Expected stream to be a number", {});
        }

        const stream_id = stream_arg.toU32();
        if (stream_id == 0 || stream_id > MAX_STREAM_ID) {
            return globalObject.throw("Invalid stream id", {});
        }

        let stream: *Stream = undefined;
        if (_sv_3565 := _.streams.get(@intCast(stream_id))) {
            stream = _sv_3565;
        } else {
            return globalObject.throw("Invalid stream id", {});
        }

        stream.waitForTrailers = false;
        // sendData() drives the close itself: when the frame goes out
        // immediately the defer at its top advances state and dispatches
        // onStreamEnd; when it queues under backpressure, flushQueue() does
        // the same once the frame is actually written. Doing it again here
        // (the old code did) sets HALF_CLOSED_LOCAL/CLOSED before the queued
        // frame is flushed, so canSendData() rejects it / freeResources()
        // drops it and the peer never receives END_STREAM.
        _.sendData(stream, "", true, .js_undefined);
        return JSValue.js_undefined;
    }

    /// validate header name and convert to lowecase if needed
    static fn toValidHeaderName(input: []const u8, out: []u8): ![]const u8 {
        let in_slice: []const u8 = input;
        let out_slice: []u8 = out;
        let has_upper = false;
        if (input.len > 4096) return error.InvalidHeaderName;
        bun.assert(out.len >= input.len);
        // lets validate and convert to lowercase in one pass
        begin: while (true) {
            let i: usize = 0;
            while (i < in_slice.len) : (i += 1) {
                const c = in_slice[i];
                if (c >= 'A' && c <= 'Z') {
                    bun.copy(u8, out_slice, in_slice[0..i]);
                    out_slice[i] = std.ascii.toLower(c);
                    const end = i + 1;
                    in_slice = in_slice[end..];
                    out_slice = out_slice[end..];
                    has_upper = true;
                    continue :begin;
                } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '!' || c == '#' || c == '$' || c == '%' || c == '&' || c == '\'' || c == '*' || c == '+' || c == '-' || c == '.' || c == '^' || c == '_' || c == '`' || c == '|' || c == '~') {
                } else if (c == ':') {
                    // only allow pseudoheaders at the beginning
                    if (i != 0 || has_upper) {
                        return error.InvalidHeaderName;
                    }
                    continue;
                } else {
                    return error.InvalidHeaderName;
                }
            }

            if (has_upper) bun.copy(u8, out_slice, in_slice);
            break :begin;
        }

        return (if (has_upper) out[0..input.len] else input);
    }

    fn sendTrailers(*_, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
        jsc.markBinding(@src());
        const args_list = callframe.arguments_old(3);
        if (args_list.len < 3) {
            return globalObject.throw("Expected stream, headers and sensitiveHeaders arguments", {});
        }

        const stream_arg = args_list.ptr[0];
        const headers_arg = args_list.ptr[1];
        const sensitive_arg = args_list.ptr[2];

        if (!stream_arg.isNumber()) {
            return globalObject.throw("Expected stream to be a number", {});
        }

        const stream_id = stream_arg.toU32();
        if (stream_id == 0 || stream_id > MAX_STREAM_ID) {
            return globalObject.throw("Invalid stream id", {});
        }

        let stream: *Stream = undefined;
        if (_sv_3642 := _.streams.get(@intCast(stream_id))) {
            stream = _sv_3642;
        } else {
            return globalObject.throw("Invalid stream id", {});
        }

        const _headers_obj_opt_3646 = headers_arg.getObject();
        if (_headers_obj_opt_3646 == null) {
            return globalObject.throw("Expected headers to be an object", {});
        }
        const headers_obj = _headers_obj_opt_3646.?;

        if (!sensitive_arg.isObject()) {
            return globalObject.throw("Expected sensitiveHeaders to be an object", {});
        }

        // Use remote settings maxFrameSize if available, otherwise default to localSettings
        const settings = _.remoteSettings ?? _.localSettings;
        * = settings;
        // Use shared buffer when possible, fall back to heap for large headers
        let buf_fallback = bun.allocators.BufferFallbackAllocator.init(&shared_request_buffer, bun.default_allocator);
        const alloc = buf_fallback.allocator();
        // Use ArrayList with initial capacity of shared buffer size, doubling when needed
        let encoded_headers_guard = DeinitWithAllocGuard<std.ArrayListUnmanaged(u8)>{
            val: {},
            alloc: alloc,
        };
        // Pre-allocate to shared buffer size (this uses the stack buffer via BufferFallbackAllocator)
        encoded_headers_guard.val.ensureTotalCapacity(alloc, shared_request_buffer.len) catch {
            return globalObject.throw("Failed to allocate header buffer", {});
        };
        // max header name length for lshpack
        let name_buffer: [4096]u8 = undefined;
        @memset(&name_buffer, 0);

        let iter_guard = DeinitGuard<JSPropIter>{
            val: JSPropIter.init(globalObject, headers_obj)?,
        };

        let single_value_headers: [SINGLE_VALUE_HEADERS_COUNT]bool = undefined;
        @memset(&single_value_headers, false);

        // Encode trailer headers using HPACK
        while (header_name := iter_guard.val.next()?) {
            if (header_name.length() == 0) continue;

            const name_slice = DeinitGuard<bun.String.Slice>{ val: header_name.toUTF8(bun.default_allocator) };
            const name = name_slice.val.slice();

            if (header_name.charAt(0) == ':') {
                const exception = globalObject.toTypeError(.HTTP2_INVALID_PSEUDOHEADER, "\"{s}\" is an invalid pseudoheader or is used incorrectly", .{name});
                return globalObject.throwValue(exception);
            }

            let js_value = iter_guard.val.value;
            if (js_value.isUndefinedOrNull()) {
                const exception = globalObject.toTypeError(.HTTP2_INVALID_HEADER_VALUE, "Invalid value for header \"{s}\"", .{name});
                return globalObject.throwValue(exception);
            }
            const validated_name = toValidHeaderName(name, name_buffer[0..name.len]) catch {
                const exception = globalObject.toTypeError(.INVALID_HTTP_TOKEN, "The arguments Header name is invalid. Received {s}", .{name});
                return globalObject.throwValue(exception);
            };

            if (js_value.jsType().isArray()) {
                // https://github.com/oven-sh/bun/issues/8940
                let value_iter = js_value.arrayIterator(globalObject)?;

                const svh_index_of = SingleValueHeaders.indexOf;
                if (idx := svh_index_of(validated_name)) {
                    if (value_iter.len > 1 || single_value_headers[idx]) {
                        const exception = globalObject.toTypeError(.HTTP2_HEADER_SINGLE_VALUE, "Header field \"{s}\" must only have a single value", .{validated_name});
                        return globalObject.throwValue(exception);
                    }
                    single_value_headers[idx] = true;
                }

                while (item := value_iter.next()?) {
                    if (item.isEmptyOrUndefinedOrNull()) {
                        const exception = globalObject.toTypeError(.HTTP2_INVALID_HEADER_VALUE, "Invalid value for header \"{s}\"", .{validated_name});
                        return globalObject.throwValue(exception);
                    }

                    const value_str = item.toJSString(globalObject) catch {
                        globalObject.clearException();
                        const exception = globalObject.toTypeError(.HTTP2_INVALID_HEADER_VALUE, "Invalid value for header \"{s}\"", .{validated_name});
                        return globalObject.throwValue(exception);
                    };

                    const never_index = (sensitive_arg.getTruthyPropertyValue(globalObject, validated_name)? ?? sensitive_arg.getTruthyPropertyValue(globalObject, name)?) != null;

                    const value_slice = DeinitGuard<bun.String.Slice>{ val: value_str.toSlice(globalObject, bun.default_allocator) };
                    const value = value_slice.val.slice();
                    log("encode header {s} {s}", .{ validated_name, value });

                    * = _.encodeHeaderIntoList(&encoded_headers_guard.val, alloc, validated_name, value, never_index) catch |e| {
                        if (e == error.OutOfMemory) {
                            return globalObject.throw("Failed to allocate header buffer", {});
                        }
                        stream.state = .CLOSED;
                        const identifier = stream.getIdentifier();
                        identifier.ensureStillAlive();
                        stream.freeResources(_, false);
                        stream.rstCode = @intFromEnum(ErrorCode.FRAME_SIZE_ERROR);
                        _.dispatchWith2Extra(
                            .onFrameError,
                            identifier,
                            jsc.JSValue.jsNumber(@intFromEnum(FrameType.HTTP_FRAME_HEADERS)),
                            jsc.JSValue.jsNumber(@intFromEnum(ErrorCode.FRAME_SIZE_ERROR)),
                        );
                        _.dispatchWithExtra(.onStreamError, identifier, jsc.JSValue.jsNumber(stream.rstCode));
                        return JSValue.js_undefined;
                    };
                }
            } else {
                const svh_index_of = SingleValueHeaders.indexOf;
                if (idx := svh_index_of(validated_name)) {
                    if (single_value_headers[idx]) {
                        const exception = globalObject.toTypeError(.HTTP2_HEADER_SINGLE_VALUE, "Header field \"{s}\" must only have a single value", .{validated_name});
                        return globalObject.throwValue(exception);
                    }
                    single_value_headers[idx] = true;
                }
                const value_str = js_value.toJSString(globalObject) catch {
                    globalObject.clearException();
                    const exception = globalObject.toTypeError(.HTTP2_INVALID_HEADER_VALUE, "Invalid value for header \"{s}\"", .{validated_name});
                    return globalObject.throwValue(exception);
                };

                const never_index = (sensitive_arg.getTruthyPropertyValue(globalObject, validated_name)? ?? sensitive_arg.getTruthyPropertyValue(globalObject, name)?) != null;

                const value_slice = DeinitGuard<bun.String.Slice>{ val: value_str.toSlice(globalObject, bun.default_allocator) };
                const value = value_slice.val.slice();
                log("encode header {s} {s}", .{ name, value });

                * = _.encodeHeaderIntoList(&encoded_headers_guard.val, alloc, validated_name, value, never_index) catch |e| {
                    if (e == error.OutOfMemory) {
                        return globalObject.throw("Failed to allocate header buffer", {});
                    }
                    stream.state = .CLOSED;
                    const identifier = stream.getIdentifier();
                    identifier.ensureStillAlive();
                    stream.freeResources(_, false);
                    stream.rstCode = @intFromEnum(ErrorCode.FRAME_SIZE_ERROR);
                    _.dispatchWith2Extra(
                        .onFrameError,
                        identifier,
                        jsc.JSValue.jsNumber(@intFromEnum(FrameType.HTTP_FRAME_HEADERS)),
                        jsc.JSValue.jsNumber(@intFromEnum(ErrorCode.FRAME_SIZE_ERROR)),
                    );
                    _.dispatchWithExtra(.onStreamError, identifier, jsc.JSValue.jsNumber(stream.rstCode));
                    return JSValue.js_undefined;
                };
            }
        }
        const encoded_data = encoded_headers_guard.val.items;
        const encoded_size = encoded_data.len;

        // RFC 7540 Section 8.1: Trailers are sent as a HEADERS frame with END_STREAM flag
        const base_flags: u8 = @intFromEnum(HeadersFrameFlags.END_STREAM);
        // RFC 7540 Section 4.2: SETTINGS_MAX_FRAME_SIZE determines max frame payload
        const actual_max_frame_size = (_.remoteSettings ?? _.localSettings).maxFrameSize;

        log("trailers encoded_size {}", .{encoded_size});

        const writer = _.toWriter();

        // RFC 7540 Section 6.2 & 6.10: Check if we need CONTINUATION frames
        if (encoded_size <= actual_max_frame_size) {
            // Single HEADERS frame - header block fits in one frame
            let frame: FrameHeader = {
                @"type": @intFromEnum(FrameType.HTTP_FRAME_HEADERS),
                flags: base_flags | @intFromEnum(HeadersFrameFlags.END_HEADERS),
                streamIdentifier: stream.id,
                length: @intCast(encoded_size),
            };
            * = frame.write(@TypeOf(writer), writer);
            * = writer.write(encoded_data) catch 0;
        } else {
            // RFC 7540 Section 6.2 & 6.10: Header block exceeds MAX_FRAME_SIZE.
            // Must split into HEADERS frame followed by one or more CONTINUATION frames.
            // Note: CONTINUATION frames cannot have padding (Section 6.10) - they carry
            // only header block fragments. END_HEADERS must be set on the last frame.
            log("Using CONTINUATION frames for trailers: encoded_size={d} max_frame_size={d}", .{ encoded_size, actual_max_frame_size });

            // RFC 7540 Section 6.2: First chunk goes in HEADERS frame (without END_HEADERS flag)
            // Trailers use END_STREAM but NOT END_HEADERS when more frames follow
            const first_chunk_size = actual_max_frame_size;

            let headers_frame: FrameHeader = {
                @"type": @intFromEnum(FrameType.HTTP_FRAME_HEADERS),
                flags: base_flags, // END_STREAM but NOT END_HEADERS
                streamIdentifier: stream.id,
                length: @intCast(first_chunk_size),
            };
            * = headers_frame.write(@TypeOf(writer), writer);
            * = writer.write(encoded_data[0..first_chunk_size]) catch 0;

            // RFC 7540 Section 6.10: CONTINUATION frames carry remaining header block fragments.
            // They have no padding, no priority - just frame header + header block fragment.
            let offset: usize = first_chunk_size;
            while (offset < encoded_size) {
                const remaining = encoded_size - offset;
                const chunk_size = @min(remaining, actual_max_frame_size);
                const is_last = (offset + chunk_size >= encoded_size);

                // RFC 7540 Section 6.10: END_HEADERS flag must be set on the last frame
                let cont_frame: FrameHeader = {
                    @"type": @intFromEnum(FrameType.HTTP_FRAME_CONTINUATION),
                    flags: if (is_last) @intFromEnum(HeadersFrameFlags.END_HEADERS) else 0,
                    streamIdentifier: stream.id,
                    length: @intCast(chunk_size),
                };
                * = cont_frame.write(@TypeOf(writer), writer);
                * = writer.write(encoded_data[offset..][0..chunk_size]) catch 0;

                offset += chunk_size;
            }
        }
        const identifier = stream.getIdentifier();
        identifier.ensureStillAlive();
        if (stream.state == .HALF_CLOSED_REMOTE) {
            stream.state = .CLOSED;
            stream.freeResources(_, false);
        } else {
            stream.state = .HALF_CLOSED_LOCAL;
        }
        _.dispatchWithExtra(.onStreamEnd, identifier, jsc.JSValue.jsNumber(@intFromEnum(stream.state)));
        return JSValue.js_undefined;
    }

    fn writeStream(*_, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
        jsc.markBinding(@src());
        const args = callframe.argumentsUndef(5);
        const stream_arg = args.ptr[0];
        const data_arg = args.ptr[1];
        const encoding_arg = args.ptr[2];
        const close_arg = args.ptr[3];
        const callback_arg = args.ptr[4];

        if (!stream_arg.isNumber()) {
            return globalObject.throw("Expected stream to be a number", {});
        }

        const stream_id = stream_arg.toU32();
        if (stream_id == 0 || stream_id > MAX_STREAM_ID) {
            return globalObject.throw("Invalid stream id", {});
        }
        const close = close_arg.toBoolean();

        let stream: *Stream = undefined;
        if (_sv_3890 := _.streams.get(@intCast(stream_id))) {
            stream = _sv_3890;
        } else {
            return globalObject.throw("Invalid stream id", {});
        }
        if (!stream.canSendData()) {
            _.dispatchWriteCallback(callback_arg);
            return JSValue.false;
        }

        const encoding: jsc.Node.Encoding = brk: {
            if (encoding_arg.isUndefined()) {
                break :brk .utf8;
            }

            if (!encoding_arg.isString()) {
                return globalObject.throwInvalidArgumentTypeValue("write", "encoding", encoding_arg);
            }

            const _enc_opt = jsc.Node.Encoding.fromJS(encoding_arg, globalObject)?;
            if (_enc_opt == null) {
                return globalObject.throwInvalidArgumentTypeValue("write", "encoding", encoding_arg);
            }
            break :brk _enc_opt.?;
        };

        const _buf_opt = jsc.Node.StringOrBuffer.fromJSWithEncoding(
            globalObject,
            bun.default_allocator,
            data_arg,
            encoding,
        )?;
        if (_buf_opt == null) {
            return globalObject.throwInvalidArgumentTypeValue("write", "Buffer or String", data_arg);
        }
        let buffer = DeinitGuard<jsc.Node.StringOrBuffer>{ val: _buf_opt.? };

        _.sendData(stream, buffer.val.slice(), close, callback_arg);

        return JSValue.true;
    }

    fn getNextStreamID(*_): u32 {
        let stream_id: u32 = _.lastStreamID;
        if (_.isServer) {
            if (stream_id % 2 == 0) {
                stream_id += 2;
            } else {
                stream_id += 1;
            }
        } else {
            if (stream_id % 2 == 0) {
                stream_id += 1;
            } else if (stream_id == 0) {
                stream_id = 1;
            } else {
                stream_id += 2;
            }
        }
        return stream_id;
    }

    fn setNextStreamID(*_, _: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
        jsc.markBinding(@src());
        const args_list = callframe.arguments();
        bun.debugAssert(args_list.len >= 1);
        const stream_id_arg = args_list.ptr[0];
        bun.debugAssert(stream_id_arg.isNumber());
        _.lastStreamID = stream_id_arg.to(u32);
        // to set the next stream id we need to decrement because we only keep the last stream id
        if (_.isServer) {
            if (_.lastStreamID % 2 == 0) {
                _.lastStreamID -= 2;
            } else {
                _.lastStreamID -= 1;
            }
        } else {
            if (_.lastStreamID % 2 == 0) {
                _.lastStreamID -= 1;
            } else if (_.lastStreamID == 1) {
                _.lastStreamID = 0;
            } else {
                _.lastStreamID -= 2;
            }
        }
        return JSValue.js_undefined;
    }

    fn hasNativeRead(*_, _: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!JSValue {
        return jsc.JSValue.jsBoolean(_.native_socket == .tcp || _.native_socket == .tls);
    }

    fn getNextStream(*_, _: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!JSValue {
        jsc.markBinding(@src());

        const id = _.getNextStreamID();
        if (id > MAX_STREAM_ID) {
            return jsc.JSValue.jsNumber(-1);
        }
        if (_.handleReceivedStreamID(id) == null) {
            return jsc.JSValue.jsNumber(-1);
        }

        return jsc.JSValue.jsNumber(id);
    }

    fn getStreamContext(*_, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
        jsc.markBinding(@src());
        const args_list = callframe.arguments_old(1);
        if (args_list.len < 1) {
            return globalObject.throw("Expected stream_id argument", {});
        }

        const stream_id_arg = args_list.ptr[0];
        if (!stream_id_arg.isNumber()) {
            return globalObject.throw("Expected stream_id to be a number", {});
        }

        let stream: *Stream = undefined;
        if (_sv_4003 := _.streams.get(stream_id_arg.to(u32))) {
            stream = _sv_4003;
        } else {
            return globalObject.throw("Invalid stream id", {});
        }

        return stream.jsContext.get() ?? .js_undefined;
    }

    fn setStreamContext(*_, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
        jsc.markBinding(@src());
        const args_list = callframe.arguments_old(2);
        if (args_list.len < 2) {
            return globalObject.throw("Expected stream_id and context arguments", {});
        }

        const stream_id_arg = args_list.ptr[0];
        if (!stream_id_arg.isNumber()) {
            return globalObject.throw("Expected stream_id to be a number", {});
        }
        let stream: *Stream = undefined;
        if (_sv_4021 := _.streams.get(stream_id_arg.to(u32))) {
            stream = _sv_4021;
        } else {
            return globalObject.throw("Invalid stream id", {});
        }
        const context_arg = args_list.ptr[1];
        if (!context_arg.isObject()) {
            return globalObject.throw("Expected context to be an object", {});
        }

        stream.setContext(context_arg, globalObject);
        return JSValue.js_undefined;
    }

    fn forEachStream(*_, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
        jsc.markBinding(@src());
        const args = callframe.arguments();
        if (args.len < 1 || !args[0].isCallable()) {
            return JSValue.js_undefined;
        }
        const callback = args[0];
        const thisValue: JSValue = if (args.len > 1) args[1] else .js_undefined;
        let count: u32 = 0;
        let it = StreamResumableIterator.init(_);
        while (*stream := it.next()) {
            const value = stream.jsContext.get() ?? continue;
            const _args = [value];
            _.handlers.vm.eventLoop().runCallback(callback, globalObject, thisValue, &_args);
            count += 1;
        }
        return JSValue.js_undefined;
    }

    fn emitAbortToAllStreams(*_, _: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!jsc.JSValue {
        jsc.markBinding(@src());
        let it = StreamResumableIterator.init(_);
        while (*stream := it.next()) {

            // this is the oposite logic of emitErrorToallStreams, in this case we wanna to cancel this streams
            if (_.isServer) {
                if (stream.id % 2 == 0) continue;
            } else if (stream.id % 2 != 0) continue;
            if (stream.state != .CLOSED) {
                const old_state = stream.state;
                stream.state = .CLOSED;
                stream.rstCode = @intFromEnum(ErrorCode.CANCEL);
                const identifier = stream.getIdentifier();
                identifier.ensureStillAlive();
                stream.freeResources(_, false);
                _.dispatchWith2Extra(.onAborted, identifier, .js_undefined, jsc.JSValue.jsNumber(@intFromEnum(old_state)));
            }
        }
        return JSValue.js_undefined;
    }

    fn emitErrorToAllStreams(*_, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
        jsc.markBinding(@src());

        const args_list = callframe.arguments_old(1);
        if (args_list.len < 1) {
            return globalObject.throw("Expected error argument", {});
        }

        let it = StreamResumableIterator.init(_);
        while (*stream := it.next()) {
            // if (this.isServer) {
            //     if (stream.id % 2 == 0) continue;
            // } else if (stream.id % 2 != 0) continue;
            if (stream.state != .CLOSED) {
                stream.state = .CLOSED;
                stream.rstCode = args_list.ptr[0].to(u32);
                const identifier = stream.getIdentifier();
                identifier.ensureStillAlive();
                stream.freeResources(_, false);
                _.dispatchWithExtra(.onStreamError, identifier, args_list.ptr[0]);
            }
        }
        return JSValue.js_undefined;
    }

    fn flushFromJS(*_, _: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!JSValue {
        jsc.markBinding(@src());

        return jsc.JSValue.jsNumber(_.flush());
    }

    fn request(*_, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
        jsc.markBinding(@src());
        log("request", {});

        const args_list = callframe.arguments_old(5);
        if (args_list.len < 4) {
            return globalObject.throw("Expected stream_id, stream_ctx, headers and sensitiveHeaders arguments", {});
        }

        const stream_id_arg = args_list.ptr[0];
        const stream_ctx_arg = args_list.ptr[1];

        const headers_arg = args_list.ptr[2];
        const sensitive_arg = args_list.ptr[3];

        const _headers_obj_opt_4119 = headers_arg.getObject();
        if (_headers_obj_opt_4119 == null) {
            return globalObject.throw("Expected headers to be an object", {});
        }
        const headers_obj = _headers_obj_opt_4119.?;

        if (!sensitive_arg.isObject()) {
            return globalObject.throw("Expected sensitiveHeaders to be an object", {});
        }
        // Use remote settings maxFrameSize if available, otherwise use localSettings.
        const settings = _.remoteSettings ?? _.localSettings;
        * = settings;
        // Use shared buffer when possible, fall back to heap for large headers
        let buf_fallback = bun.allocators.BufferFallbackAllocator.init(&shared_request_buffer, bun.default_allocator);
        const alloc = buf_fallback.allocator();
        // Use ArrayList with initial capacity of shared buffer size, doubling when needed
        let encoded_headers_guard = DeinitWithAllocGuard<std.ArrayListUnmanaged(u8)>{
            val: {},
            alloc: alloc,
        };
        // Pre-allocate to shared buffer size (this uses the stack buffer via BufferFallbackAllocator)
        encoded_headers_guard.val.ensureTotalCapacity(alloc, shared_request_buffer.len) catch {
            return globalObject.throw("Failed to allocate header buffer", {});
        };
        // max header name length for lshpack
        let name_buffer: [4096]u8 = undefined;
        @memset(&name_buffer, 0);
        const stream_id: u32 = if (!stream_id_arg.isEmptyOrUndefinedOrNull() && stream_id_arg.isNumber()) stream_id_arg.to(u32) else _.getNextStreamID();
        if (stream_id > MAX_STREAM_ID) {
            return jsc.JSValue.jsNumber(-1);
        }

        // we iterate twice, because pseudo headers must be sent first, but can appear anywhere in the headers object
        let iter_guard = DeinitGuard<JSPropIter>{
            val: JSPropIter.init(globalObject, headers_obj)?,
        };
        let single_value_headers: [SINGLE_VALUE_HEADERS_COUNT]bool = undefined;
        @memset(&single_value_headers, false);

        for (ignore_pseudo_headers of 0..2) {
            iter_guard.val.reset();

            while (header_name := iter_guard.val.next()?) {
                if (header_name.length() == 0) continue;

                const name_slice = DeinitGuard<bun.String.Slice>{ val: header_name.toUTF8(bun.default_allocator) };
                const name = name_slice.val.slice();

                const validated_name = toValidHeaderName(name, name_buffer[0..name.len]) catch {
                    const exception = globalObject.toTypeError(.INVALID_HTTP_TOKEN, "The arguments Header name is invalid. Received \"{s}\"", .{name});
                    return globalObject.throwValue(exception);
                };

                if (header_name.charAt(0) == ':') {
                    if (ignore_pseudo_headers == 1) continue;

                    if (_.isServer) {
                        if (!ValidResponsePseudoHeaders.has(validated_name)) {
                            if (!globalObject.hasException()) {
                                return globalObject.ERR(.HTTP2_INVALID_PSEUDOHEADER, "\"{s}\" is an invalid pseudoheader or is used incorrectly", .{name}).throw();
                            }
                            return jsc.JSValue.zero;
                        }
                    } else {
                        if (!ValidRequestPseudoHeaders.has(validated_name)) {
                            if (!globalObject.hasException()) {
                                return globalObject.ERR(.HTTP2_INVALID_PSEUDOHEADER, "\"{s}\" is an invalid pseudoheader or is used incorrectly", .{name}).throw();
                            }
                            return jsc.JSValue.zero;
                        }
                    }
                } else if (ignore_pseudo_headers == 0) {
                    continue;
                }

                const js_value = iter_guard.val.value;
                if (js_value.isUndefinedOrNull()) {
                    const exception = globalObject.toTypeError(.HTTP2_INVALID_HEADER_VALUE, "Invalid value for header \"{s}\"", .{name});
                    return globalObject.throwValue(exception);
                }

                if (js_value.jsType().isArray()) {
                    log("array header {s}", .{name});
                    // https://github.com/oven-sh/bun/issues/8940
                    let value_iter = js_value.arrayIterator(globalObject)?;

                    const svh_index_of = SingleValueHeaders.indexOf;
                if (idx := svh_index_of(validated_name)) {
                        if (value_iter.len > 1 || single_value_headers[idx]) {
                            if (!globalObject.hasException()) {
                                const exception = globalObject.toTypeError(.HTTP2_HEADER_SINGLE_VALUE, "Header field \"{s}\" must only have a single value", .{validated_name});
                                return globalObject.throwValue(exception);
                            }
                            return jsc.JSValue.zero;
                        }
                        single_value_headers[idx] = true;
                    }

                    while (item := value_iter.next()?) {
                        if (item.isEmptyOrUndefinedOrNull()) {
                            if (!globalObject.hasException()) {
                                return globalObject.ERR(.HTTP2_INVALID_HEADER_VALUE, "Invalid value for header \"{s}\"", .{validated_name}).throw();
                            }
                            return jsc.JSValue.zero;
                        }

                        const value_str = item.toJSString(globalObject) catch {
                            globalObject.clearException();
                            return globalObject.ERR(.HTTP2_INVALID_HEADER_VALUE, "Invalid value for header \"{s}\"", .{validated_name}).throw();
                        };

                        const never_index = (sensitive_arg.getTruthyPropertyValue(globalObject, validated_name)? ?? sensitive_arg.getTruthyPropertyValue(globalObject, name)?) != null;

                        const value_slice = DeinitGuard<bun.String.Slice>{ val: value_str.toSlice(globalObject, bun.default_allocator) };
                        const value = value_slice.val.slice();
                        log("encode header {s} {s}", .{ validated_name, value });

                        * = _.encodeHeaderIntoList(&encoded_headers_guard.val, alloc, validated_name, value, never_index) catch |e| {
                            if (e == error.OutOfMemory) {
                                return globalObject.throw("Failed to allocate header buffer", {});
                            }
                            let stream: *Stream = undefined;
                            if (_sv_4240 := _.handleReceivedStreamID(stream_id)) {
                                stream = _sv_4240;
                            } else {
                                return jsc.JSValue.jsNumber(-1);
                            }
                            if (!stream_ctx_arg.isEmptyOrUndefinedOrNull() && stream_ctx_arg.isObject()) {
                                stream.setContext(stream_ctx_arg, globalObject);
                            }
                            stream.state = .CLOSED;
                            stream.rstCode = @intFromEnum(ErrorCode.COMPRESSION_ERROR);
                            _.dispatchWithExtra(.onStreamError, stream.getIdentifier(), jsc.JSValue.jsNumber(stream.rstCode));
                            return JSValue.js_undefined;
                        };
                    }
                } else if (!js_value.isEmptyOrUndefinedOrNull()) {
                    log("single header {s}", .{name});
                    const svh_index_of = SingleValueHeaders.indexOf;
                if (idx := svh_index_of(validated_name)) {
                        if (single_value_headers[idx]) {
                            const exception = globalObject.toTypeError(.HTTP2_HEADER_SINGLE_VALUE, "Header field \"{s}\" must only have a single value", .{validated_name});
                            return globalObject.throwValue(exception);
                        }
                        single_value_headers[idx] = true;
                    }
                    const value_str = js_value.toJSString(globalObject) catch {
                        globalObject.clearException();
                        return globalObject.ERR(.HTTP2_INVALID_HEADER_VALUE, "Invalid value for header \"{s}\"", .{name}).throw();
                    };

                    const never_index = (sensitive_arg.getTruthyPropertyValue(globalObject, validated_name)? ?? sensitive_arg.getTruthyPropertyValue(globalObject, name)?) != null;

                    const value_slice = DeinitGuard<bun.String.Slice>{ val: value_str.toSlice(globalObject, bun.default_allocator) };
                    const value = value_slice.val.slice();
                    log("encode header {s} {s}", .{ validated_name, value });

                    * = _.encodeHeaderIntoList(&encoded_headers_guard.val, alloc, validated_name, value, never_index) catch |e| {
                        if (e == error.OutOfMemory) {
                            return globalObject.throw("Failed to allocate header buffer", {});
                        }
                        let stream: *Stream = undefined;
                        if (_sv_4277 := _.handleReceivedStreamID(stream_id)) {
                            stream = _sv_4277;
                        } else {
                            return jsc.JSValue.jsNumber(-1);
                        }
                        stream.state = .CLOSED;
                        if (!stream_ctx_arg.isEmptyOrUndefinedOrNull() && stream_ctx_arg.isObject()) {
                            stream.setContext(stream_ctx_arg, globalObject);
                        }
                        stream.rstCode = @intFromEnum(ErrorCode.COMPRESSION_ERROR);
                        _.dispatchWithExtra(.onStreamError, stream.getIdentifier(), jsc.JSValue.jsNumber(stream.rstCode));
                        return jsc.JSValue.jsNumber(stream_id);
                    };
                }
            }
        }
        const encoded_data = encoded_headers_guard.val.items;
        const encoded_size = encoded_data.len;

        let stream: *Stream = undefined;
        if (_sv_4294 := _.handleReceivedStreamID(stream_id)) {
            stream = _sv_4294;
        } else {
            return jsc.JSValue.jsNumber(-1);
        }
        if (!stream_ctx_arg.isEmptyOrUndefinedOrNull() && stream_ctx_arg.isObject()) {
            stream.setContext(stream_ctx_arg, globalObject);
        }
        let flags: u8 = @intFromEnum(HeadersFrameFlags.END_HEADERS);
        let exclusive: bool = false;
        let has_priority: bool = false;
        let weight: i32 = 0;
        let parent: i32 = 0;
        let silent: bool = false;
        let waitForTrailers: bool = false;
        let end_stream: bool = false;
        if (args_list.len > 4 && !args_list.ptr[4].isEmptyOrUndefinedOrNull()) {
            const options = args_list.ptr[4];
            if (!options.isObject()) {
                stream.state = .CLOSED;
                stream.rstCode = @intFromEnum(ErrorCode.INTERNAL_ERROR);
                _.dispatchWithExtra(.onStreamError, stream.getIdentifier(), jsc.JSValue.jsNumber(stream.rstCode));
                return jsc.JSValue.jsNumber(stream_id);
            }

            if (padding_js := options.get(globalObject, "paddingStrategy")?) {
                if (padding_js.isNumber()) {
                    stream.paddingStrategy = switch (padding_js.to(u32)) {
                        1 => .aligned,
                        2 => .max,
                        else => .none,
                    };
                }
            }

            if (trailes_js := options.get(globalObject, "waitForTrailers")?) {
                if (trailes_js.isBoolean()) {
                    waitForTrailers = trailes_js.asBoolean();
                    stream.waitForTrailers = waitForTrailers;
                }
            }

            if (silent_js := options.get(globalObject, "silent")?) {
                if (silent_js.isBoolean()) {
                    silent = silent_js.asBoolean();
                } else {
                    return globalObject.throwInvalidArgumentTypeValue("options.silent", "boolean", silent_js);
                }
            }

            if (end_stream_js := options.get(globalObject, "endStream")?) {
                if (end_stream_js.isBoolean()) {
                    if (end_stream_js.asBoolean()) {
                        end_stream = true;
                        // will end the stream after trailers
                        if (!waitForTrailers || _.isServer) {
                            flags |= @intFromEnum(HeadersFrameFlags.END_STREAM);
                        }
                    }
                } else {
                    return globalObject.throwInvalidArgumentTypeValue("options.endStream", "boolean", end_stream_js);
                }
            }

            if (exclusive_js := options.get(globalObject, "exclusive")?) {
                if (exclusive_js.isBoolean()) {
                    if (exclusive_js.asBoolean()) {
                        exclusive = true;
                        stream.exclusive = true;
                        has_priority = true;
                    }
                } else {
                    return globalObject.throwInvalidArgumentTypeValue("options.exclusive", "boolean", exclusive_js);
                }
            }

            if (parent_js := options.get(globalObject, "parent")?) {
                if (parent_js.isNumber() || parent_js.isInt32()) {
                    has_priority = true;
                    parent = parent_js.toInt32();
                    if (parent <= 0 || parent > MAX_STREAM_ID) {
                        stream.state = .CLOSED;
                        stream.rstCode = @intFromEnum(ErrorCode.INTERNAL_ERROR);
                        _.dispatchWithExtra(.onStreamError, stream.getIdentifier(), jsc.JSValue.jsNumber(stream.rstCode));
                        return jsc.JSValue.jsNumber(stream.id);
                    }
                    stream.streamDependency = @intCast(parent);
                } else {
                    return globalObject.throwInvalidArgumentTypeValue("options.parent", "number", parent_js);
                }
            }

            if (weight_js := options.get(globalObject, "weight")?) {
                if (weight_js.isNumber() || weight_js.isInt32()) {
                    has_priority = true;
                    weight = weight_js.toInt32();
                    if (weight < 1 || weight > std.math.maxInt(u8)) {
                        stream.state = .CLOSED;
                        stream.rstCode = @intFromEnum(ErrorCode.INTERNAL_ERROR);
                        _.dispatchWithExtra(.onStreamError, stream.getIdentifier(), jsc.JSValue.jsNumber(stream.rstCode));
                        return jsc.JSValue.jsNumber(stream_id);
                    }
                    stream.weight = @intCast(weight);
                } else {
                    return globalObject.throwInvalidArgumentTypeValue("options.weight", "number", weight_js);
                }

                if (weight < 1 || weight > std.math.maxInt(u8)) {
                    stream.state = .CLOSED;
                    stream.rstCode = @intFromEnum(ErrorCode.INTERNAL_ERROR);
                    _.dispatchWithExtra(.onStreamError, stream.getIdentifier(), jsc.JSValue.jsNumber(stream.rstCode));
                    return jsc.JSValue.jsNumber(stream_id);
                }

                stream.weight = @intCast(weight);
            }

            if (signal_arg := options.get(globalObject, "signal")?) {
                if (signal_ := signal_arg.@"as"(jsc.WebCore.AbortSignal)) {
                    if (signal_.aborted()) {
                        stream.state = .IDLE;
                        _.abortStream(stream, Bun__wrapAbortError(globalObject, signal_.abortReason()));
                        return jsc.JSValue.jsNumber(stream_id);
                    }
                    stream.attachSignal(_, signal_);
                } else {
                    return globalObject.throwInvalidArgumentTypeValue("options.signal", "AbortSignal", signal_arg);
                }
            }
        }

        // too much memory being use
        if (_.getSessionMemoryUsage() > _.maxSessionMemory) {
            stream.state = .CLOSED;
            stream.rstCode = @intFromEnum(ErrorCode.ENHANCE_YOUR_CALM);
            _.rejectedStreams += 1;
            _.dispatchWithExtra(.onStreamError, stream.getIdentifier(), jsc.JSValue.jsNumber(stream.rstCode));
            if (_.rejectedStreams >= _.maxRejectedStreams) {
                const chunk = _.handlers.binary_type.toJS("ENHANCE_YOUR_CALM", _.handlers.globalObject)?;
                _.dispatchWith2Extra(.onError, jsc.JSValue.jsNumber(@intFromEnum(ErrorCode.ENHANCE_YOUR_CALM)), jsc.JSValue.jsNumber(_.lastStreamID), chunk);
            }
            return jsc.JSValue.jsNumber(stream_id);
        }
        let length: usize = encoded_size;
        if (has_priority) {
            length += 5;
            flags |= @intFromEnum(HeadersFrameFlags.PRIORITY);
        }

        log("request encoded_size {}", .{encoded_size});

        // Check if headers block exceeds maxSendHeaderBlockLength
        if (_.maxSendHeaderBlockLength != 0 && encoded_size > _.maxSendHeaderBlockLength) {
            stream.state = .CLOSED;
            stream.rstCode = @intFromEnum(ErrorCode.REFUSED_STREAM);

            _.dispatchWith2Extra(
                .onFrameError,
                stream.getIdentifier(),
                jsc.JSValue.jsNumber(@intFromEnum(FrameType.HTTP_FRAME_HEADERS)),
                jsc.JSValue.jsNumber(@intFromEnum(ErrorCode.FRAME_SIZE_ERROR)),
            );

            _.dispatchWithExtra(.onStreamError, stream.getIdentifier(), jsc.JSValue.jsNumber(stream.rstCode));
            return jsc.JSValue.jsNumber(stream_id);
        }

        // RFC 7540 Section 4.2: SETTINGS_MAX_FRAME_SIZE determines max frame payload (default 16384)
        const actual_max_frame_size = (_.remoteSettings ?? _.localSettings).maxFrameSize;

        // RFC 7540 Section 6.2: HEADERS frame can include optional PRIORITY (5 bytes)
        const priority_overhead: usize = if (has_priority) StreamPriority.byteSize else 0;

        // Compute available payload budget for HEADERS frame (before padding is applied)
        const available_payload = actual_max_frame_size - priority_overhead;

        // RFC 7540 Section 6.10: CONTINUATION frames carry ONLY header block fragments.
        // Unlike HEADERS frames, CONTINUATION frames CANNOT have padding or priority fields.
        // When we need CONTINUATION frames, disable padding to keep the logic simple.
        // Pass available_payload as maxLen so getPadding can apply padding when headers fit in one frame.
        const padding: u8 = if (encoded_size > available_payload) 0 else stream.getPadding(encoded_size, available_payload);
        const padding_overhead: usize = if (padding != 0) @as(usize, @intCast(padding)) + 1 else 0;

        // Max payload for HEADERS frame (accounting for priority and padding overhead)
        const headers_frame_max_payload = available_payload - padding_overhead;

        const writer = _.toWriter();

        // Check if we need CONTINUATION frames
        if (encoded_size <= headers_frame_max_payload) {
            // Single HEADERS frame - fits in one frame
            const payload_size = encoded_size + priority_overhead + padding_overhead;
            log("padding: {d} size: {d} max_size: {d} payload_size: {d}", .{ padding, encoded_size, encoded_data.len, payload_size });

            if (padding != 0) {
                flags |= @intFromEnum(HeadersFrameFlags.PADDED);
            }

            let frame: FrameHeader = {
                @"type": @intFromEnum(FrameType.HTTP_FRAME_HEADERS),
                flags: flags,
                streamIdentifier: stream.id,
                length: @intCast(payload_size),
            };
            * = frame.write(@TypeOf(writer), writer);

            // Write priority data if present
            if (has_priority) {
                let stream_identifier: UInt31WithReserved = {
                    reserved: exclusive,
                    uint31: @intCast(parent),
                };
                let priority_data: StreamPriority = {
                    streamIdentifier: stream_identifier.toUInt32(),
                    weight: @intCast(weight),
                };
                * = priority_data.write(@TypeOf(writer), writer);
            }

            // Handle padding
            if (padding != 0) {
                // Need extra capacity for padding length byte and padding bytes
                encoded_headers_guard.val.ensureTotalCapacity(alloc, encoded_size + padding_overhead) catch {
                    return globalObject.throw("Failed to allocate padding buffer", {});
                };
                let buffer = encoded_headers_guard.val.allocatedSlice();
                bun.memmove(buffer[1..][0..encoded_size], buffer[0..encoded_size]);
                buffer[0] = padding;
                * = writer.write(buffer[0 .. encoded_size + padding_overhead]) catch 0;
            } else {
                * = writer.write(encoded_data) catch 0;
            }
        } else {
            // RFC 7540 Section 6.2 & 6.10: Header blocks exceeding MAX_FRAME_SIZE must be split
            // into HEADERS frame followed by one or more CONTINUATION frames.
            // - HEADERS frame without END_HEADERS flag indicates more frames follow
            // - CONTINUATION frames carry remaining header block fragments
            // - Last frame (HEADERS or CONTINUATION) must have END_HEADERS flag set
            // - All frames must have the same stream identifier
            // - No other frames can be interleaved on this stream until END_HEADERS
            log("Using CONTINUATION frames: encoded_size={d} max_frame_payload={d}", .{ encoded_size, actual_max_frame_size });

            // RFC 7540 Section 6.2: First chunk goes in HEADERS frame (without END_HEADERS flag)
            // HEADERS frame can carry PRIORITY but CONTINUATION frames cannot.
            const first_chunk_size = actual_max_frame_size - priority_overhead;
            const headers_flags = flags & ~@as(u8, @intFromEnum(HeadersFrameFlags.END_HEADERS));

            let headers_frame: FrameHeader = {
                @"type": @intFromEnum(FrameType.HTTP_FRAME_HEADERS),
                flags: headers_flags | (if (has_priority) @intFromEnum(HeadersFrameFlags.PRIORITY) else 0),
                streamIdentifier: stream.id,
                length: @intCast(first_chunk_size + priority_overhead),
            };
            * = headers_frame.write(@TypeOf(writer), writer);

            // Write priority data if present (only in HEADERS frame, not CONTINUATION)
            if (has_priority) {
                let stream_identifier: UInt31WithReserved = {
                    reserved: exclusive,
                    uint31: @intCast(parent),
                };
                let priority_data: StreamPriority = {
                    streamIdentifier: stream_identifier.toUInt32(),
                    weight: @intCast(weight),
                };
                * = priority_data.write(@TypeOf(writer), writer);
            }

            // Write first chunk of header block fragment
            * = writer.write(encoded_data[0..first_chunk_size]) catch 0;

            // RFC 7540 Section 6.10: CONTINUATION frames carry remaining header block fragments.
            // CONTINUATION frame format: just frame header + header block fragment (no padding, no priority)
            let offset: usize = first_chunk_size;
            while (offset < encoded_size) {
                const remaining = encoded_size - offset;
                const chunk_size = @min(remaining, actual_max_frame_size);
                const is_last = (offset + chunk_size >= encoded_size);

                // RFC 7540 Section 6.10: END_HEADERS flag must be set on the last frame
                let cont_frame: FrameHeader = {
                    @"type": @intFromEnum(FrameType.HTTP_FRAME_CONTINUATION),
                    flags: if (is_last) @intFromEnum(HeadersFrameFlags.END_HEADERS) else 0,
                    streamIdentifier: stream.id,
                    length: @intCast(chunk_size),
                };
                * = cont_frame.write(@TypeOf(writer), writer);
                * = writer.write(encoded_data[offset..][0..chunk_size]) catch 0;

                offset += chunk_size;
            }
        }

        if (end_stream) {
            stream.endAfterHeaders = true;
            stream.state = .HALF_CLOSED_LOCAL;

            if (waitForTrailers) {
                _.dispatch(.onWantTrailers, stream.getIdentifier());
                return jsc.JSValue.jsNumber(stream_id);
            }
        } else {
            stream.waitForTrailers = waitForTrailers;
        }

        if (silent) {
            // TODO: should we make use of this in the future? We validate it.
        }

        return jsc.JSValue.jsNumber(stream_id);
    }

    fn read(*_, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
        jsc.markBinding(@src());
        const args_list = callframe.arguments_old(1);
        if (args_list.len < 1) {
            return globalObject.throw("Expected 1 argument", {});
        }
        const _win_guard = IncrementWindowGuard{ parser: _ };
        const buffer = args_list.ptr[0];
        buffer.ensureStillAlive();
        if (array_buffer := buffer.asArrayBuffer(globalObject)) {
            let bytes = array_buffer.byteSlice();
            // read all the bytes
            while (bytes.len > 0) {
                const result = _.readBytes(bytes)?;
                bytes = bytes[result..];
            }
            return JSValue.js_undefined;
        }
        return globalObject.throw("Expected data to be a Buffer or ArrayBuffer", {});
    }

    fn onNativeRead(*_, data: []const u8): bun.JSError!void {
        log("onNativeRead", {});
        _.ref();
        const _deref_guard = DerefGuard<H2FrameParser>{ ptr: _ };
        const _win_guard = IncrementWindowGuard{ parser: _ };
        let bytes = data;
        while (bytes.len > 0) {
            const result = _.readBytes(bytes)?;
            bytes = bytes[result..];
        }
    }

    fn onNativeWritable(*_) {
        * = _.flush();
    }

    fn onNativeClose(*_) {
        log("onNativeClose", {});
        _.detachNativeSocket();
    }

    fn setNativeSocketFromJS(*_, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
        jsc.markBinding(@src());
        const args_list = callframe.arguments_old(1);
        if (args_list.len < 1) {
            return globalObject.throw("Expected socket argument", {});
        }

        const socket_js = args_list.ptr[0];
        _.detachNativeSocket();
        if (socket := JSTLSSocket.fromJS(socket_js)) {
            log("TLSSocket attached", {});
            const _flush_guard = FlushGuard{ parser: _ };
            if (socket.attachNativeCallback({ h2: _ })) {
                _.native_socket = { tls: socket };
            } else {
                socket.ref();

                _.native_socket = { tls_writeonly: socket };
            }
            // if we started with non native and go to native we now control the backpressure internally
            _.has_nonnative_backpressure = false;
        } else if (socket := JSTCPSocket.fromJS(socket_js)) {
            log("TCPSocket attached", {});
            const _flush_guard = FlushGuard{ parser: _ };

            if (socket.attachNativeCallback({ h2: _ })) {
                _.native_socket = { tcp: socket };
            } else {
                socket.ref();

                _.native_socket = { tcp_writeonly: socket };
            }
            // if we started with non native and go to native we now control the backpressure internally
            _.has_nonnative_backpressure = false;
        }
        return JSValue.js_undefined;
    }

    fn detachNativeSocket(*_) {
        const native_socket = _.native_socket;
        _.native_socket = { none: {} };

        switch (native_socket) {
            .tcp socket => {
                socket.detachNativeCallback();
            },
            .tls socket => {
                socket.detachNativeCallback();
            },
            .tcp_writeonly socket => {
                socket.deref();
            },
            .tls_writeonly socket => {
                socket.deref();
            },
            .none => {},
        }
    }

    static fn constructor(globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame, thisValue: jsc.JSValue): bun.JSError!*H2FrameParser {
        const args_list = callframe.arguments_old(1);
        if (args_list.len < 1) {
            return globalObject.throw("Expected 1 argument", {});
        }

        const options = args_list.ptr[0];
        if (options.isEmptyOrUndefinedOrNull() || options.isBoolean() || !options.isObject()) {
            return globalObject.throwInvalidArguments("expected options as argument", {});
        }

        const _context_obj_opt_4710 = options.get(globalObject, "context")?;
        if (_context_obj_opt_4710 == null) {
            return globalObject.throw("Expected \"context\" option", {});
        }
        const context_obj = _context_obj_opt_4710.?;
        let handler_js = jsc.JSValue.zero;
        if (handlers_ := options.get(globalObject, "handlers")?) {
            handler_js = handlers_;
        }
        const handlers = Handlers.fromJS(globalObject, handler_js, thisValue)?;

        let parser = brk: {
            if (ENABLE_ALLOCATOR_POOL) {
                if (h2_pool == null) {
                    h2_pool = bun.handleOom(bun.default_allocator.create(H2FrameParserHiveAllocator));
                    h2_pool.?.* = H2FrameParserHiveAllocator.init(bun.default_allocator);
                }
                const p = bun.handleOom(h2_pool.?.tryGet());

                p.* = H2FrameParser{
                    ref_count: .init(),
                    handlers: handlers,
                    globalThis: globalObject,
                    allocator: bun.default_allocator,
                    readBuffer: {
                        allocator: bun.default_allocator,
                        list: {
                            items: [],
                            capacity: 0,
                        },
                    },
                    streams: bun.U32HashMap(*Stream).init(bun.default_allocator),
                };
                break :brk p;
            } else {
                break :brk bun.new(H2FrameParser, {
                    ref_count: .init(),
                    handlers: handlers,
                    globalThis: globalObject,
                    allocator: bun.default_allocator,
                    readBuffer: {
                        allocator: bun.default_allocator,
                        list: {
                            items: [],
                            capacity: 0,
                        },
                    },
                    streams: bun.U32HashMap(*Stream).init(bun.default_allocator),
                });
            }
        };
        let _err_guard = ParserErrDeinitGuard{ parser: parser, active: true };

        // check if socket is provided, and if it is a valid native socket
        if (socket_js := options.get(globalObject, "native")?) {
            if (socket := JSTLSSocket.fromJS(socket_js)) {
                log("TLSSocket attached", {});
                const _flush_guard = FlushGuard{ parser: parser };
                if (socket.attachNativeCallback({ h2: parser })) {
                    parser.native_socket = { tls: socket };
                } else {
                    socket.ref();

                    parser.native_socket = { tls_writeonly: socket };
                }
            } else if (socket := JSTCPSocket.fromJS(socket_js)) {
                log("TCPSocket attached", {});
                const _flush_guard = FlushGuard{ parser: parser };
                if (socket.attachNativeCallback({ h2: parser })) {
                    parser.native_socket = { tcp: socket };
                } else {
                    socket.ref();

                    parser.native_socket = { tcp_writeonly: socket };
                }
            }
        }
        if (settings_js := options.get(globalObject, "settings")?) {
            if (!settings_js.isEmptyOrUndefinedOrNull()) {
                log("settings received in the constructor", {});
                parser.loadSettingsFromJSValue(globalObject, settings_js)?;

                if (max_pings := settings_js.get(globalObject, "maxOutstandingPings")?) {
                    if (max_pings.isNumber()) {
                        parser.maxOutstandingPings = max_pings.to(u64);
                    }
                }
                if (max_memory := settings_js.get(globalObject, "maxSessionMemory")?) {
                    if (max_memory.isNumber()) {
                        parser.maxSessionMemory = @truncate(max_memory.to(u64));
                        if (parser.maxSessionMemory < 1) {
                            parser.maxSessionMemory = 1;
                        }
                    }
                }
                if (max_header_list_pairs := settings_js.get(globalObject, "maxHeaderListPairs")?) {
                    if (max_header_list_pairs.isNumber()) {
                        parser.maxHeaderListPairs = @truncate(max_header_list_pairs.to(u64));
                        if (parser.maxHeaderListPairs < 4) {
                            parser.maxHeaderListPairs = 4;
                        }
                    }
                }
                if (max_rejected_streams := settings_js.get(globalObject, "maxSessionRejectedStreams")?) {
                    if (max_rejected_streams.isNumber()) {
                        parser.maxRejectedStreams = @truncate(max_rejected_streams.to(u64));
                    }
                }
                if (max_outstanding_settings := settings_js.get(globalObject, "maxOutstandingSettings")?) {
                    if (max_outstanding_settings.isNumber()) {
                        parser.maxOutstandingSettings = @max(1, @as(u32, @truncate(max_outstanding_settings.to(u64))));
                    }
                }
                if (max_send_header_block_length := settings_js.get(globalObject, "maxSendHeaderBlockLength")?) {
                    if (max_send_header_block_length.isNumber()) {
                        parser.maxSendHeaderBlockLength = @bitCast(max_send_header_block_length.toInt32());
                    }
                }
                if (padding_strategy := settings_js.get(globalObject, "paddingStrategy")?) {
                    if (padding_strategy.isNumber()) {
                        parser.paddingStrategy = switch (padding_strategy.to(u32)) {
                            1 => .aligned,
                            2 => .max,
                            else => .none,
                        };
                    }
                }
            }
        }
        let is_server = false;
        if (type_js := options.get(globalObject, "type")?) {
            is_server = type_js.isNumber() && type_js.to(u32) == 0;
        }

        parser.isServer = is_server;
        js.gc.context.set(thisValue, globalObject, context_obj);

        parser.strong_this.setStrong(thisValue, globalObject);

        parser.hpack = lshpack.HPACK.init(parser.localSettings.headerTableSize);
        if (is_server) {
            * = parser.setSettings(parser.localSettings);
        } else {
            // consider that we need to queue until the first flush
            parser.has_nonnative_backpressure = true;
            parser.sendPrefaceAndSettings();
        }
        _err_guard.active = false;
        return parser;
    }

    fn detachFromJS(*_, _: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!JSValue {
        jsc.markBinding(@src());
        let it = StreamResumableIterator.init(_);
        while (*stream := it.next()) {
            stream.freeResources(_, false);
        }
        _.detach();
        if (this_value := _.strong_this.tryGet()) {
            js.gc.context.clear(this_value, _.globalThis);
            _.strong_this.setWeak(this_value);
        }
        return JSValue.js_undefined;
    }

    /// be careful when calling detach be sure that the socket is closed and the parser not accesible anymore
    /// this function can be called multiple times, it will erase stream info
    fn detach(*_) {
        _.uncork();
        _.unregisterAutoFlush();
        _.detachNativeSocket();

        _.readBuffer.deinit();
        _.writeBuffer.clearAndFree(_.allocator);
        _.writeBufferOffset = 0;

        if (hpack := _.hpack) {
            hpack.deinit();
            _.hpack = null;
        }
    }

    fn deinit(*_) {
        log("deinit", {});

        const _destroy_guard = ParserDestroyGuard{ parser: _ };
        _.detach();
        _.strong_this.deinit();
        let it = _.streams.valueIterator();
        while (item := it.next()) {
            let stream = item.*;
            stream.freeResources(_, true);
            bun.destroy(stream);
        }
        let _streams_guard = DeinitGuard<bun.U32HashMap(*Stream)>{ val: _.streams };
        _.streams = bun.U32HashMap(*Stream).init(bun.default_allocator);
    }

    fn finalize(*_) {
        log("finalize", {});
        _.strong_this.deinit();
        _.deref();
    }
}

@c {
    fn Bun__wrapAbortError(globalObject: *jsc.JSGlobalObject, cause: jsc.JSValue): jsc.JSValue;
}

import lshpack from "../../../http/lshpack.zig";
const Allocator = std.mem.Allocator;

import bun from "bun";
const MutableString = bun.MutableString;
const Output = bun.Output;
const strings = bun.strings;

const TCPSocket = bun.api.socket.TCPSocket;
const TLSSocket = bun.api.socket.TLSSocket;

const jsc = bun.jsc;
const JSValue = jsc.JSValue;
const AutoFlusher = jsc.WebCore.AutoFlusher;
const BinaryType = jsc.ArrayBuffer.BinaryType;

const JSTCPSocket = jsc.Codegen.JSTCPSocket;
const JSTLSSocket = jsc.Codegen.JSTLSSocket;
