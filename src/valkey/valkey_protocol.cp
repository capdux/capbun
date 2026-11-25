import bun from "bun";
import std from "std";
import protocol_jsc from "../runtime/valkey_jsc/protocol_jsc.zig";

pub struct DeinitWithAllocGuard<T> {
    ptr: *T!,
    alloc: std.mem.Allocator,
    ~init() {
        unsafe { _.ptr.deinit(_.alloc); }
    }
}

pub struct OptDeinitWithAllocGuard<T> {
    ptr: (*T!)? = null,
    alloc: std.mem.Allocator = undefined,
    ~init() {
        if (p := _.ptr) {
            unsafe { p.deinit(_.alloc); }
        }
    }
}

pub struct OptValDeinitWithAllocGuard<T> {
    val: T? = null,
    alloc: std.mem.Allocator = undefined,
    ~init() {
        if (v := _.val) {
            let mut_v = v;
            mut_v.deinit(_.alloc);
        }
    }
}

pub struct OptDestroyGuard<T> {
    ptr: (*T!)? = null,
    alloc: std.mem.Allocator = undefined,
    ~init() {
        if (p := _.ptr) {
            _.alloc.destroy(p);
        }
    }
}

pub struct OptFreeGuard<T> {
    slice: ([]T)? = null,
    alloc: std.mem.Allocator = undefined,
    ~init() {
        if (s := _.slice) {
            _.alloc.free(s);
        }
    }
}

pub struct SliceDeinitGuard<T> {
    slice: ([]T)? = null,
    count: *usize! = undefined,
    alloc: std.mem.Allocator = undefined,
    ~init() {
        if (s := _.slice) {
            let j: usize = 0;
            const n = unsafe { _.count.* };
            while (j < n) : (j += 1) {
                s[j].deinit(_.alloc);
            }
            _.alloc.free(s);
        }
    }
}

pub err RedisError {
    AuthenticationFailed,
    ConnectionClosed,
    InvalidArgument,
    InvalidArray,
    InvalidAttribute,
    InvalidBigNumber,
    InvalidBlobError,
    InvalidBoolean,
    InvalidBulkString,
    InvalidCommand,
    InvalidDouble,
    InvalidErrorString,
    InvalidInteger,
    InvalidMap,
    InvalidNull,
    InvalidPush,
    InvalidResponse,
    InvalidResponseType,
    InvalidSet,
    InvalidSimpleString,
    InvalidVerbatimString,
    JSError,
    OutOfMemory,
    JSTerminated,
    UnsupportedProtocol,
    ConnectionTimeout,
    IdleTimeout,
    NestingDepthExceeded,
}

pub const valkeyErrorToJS = protocol_jsc.valkeyErrorToJS;
const empty_resp_array: []RESPValue = [];
const null_resp_value: RESPValue = .Null;

// RESP protocol types
pub enum RESPType: u8 {
    // RESP2 types
    SimpleString = '+',
    Error = '-',
    Integer = ':',
    BulkString = '$',
    Array = '*',

    // RESP3 types
    Null = '_',
    Double = ',',
    Boolean = '#',
    VerbatimString = '=',
    Map = '%',
    Set = '~',
    Attribute = '|',
    Push = '>',
    BigNumber = '(',

    pub static fn fromByte(byte: u8): RESPType? {
        return switch (byte) {
            @intFromEnum(RESPType.SimpleString) => .SimpleString,
            @intFromEnum(RESPType.Error) => .Error,
            @intFromEnum(RESPType.Integer) => .Integer,
            @intFromEnum(RESPType.BulkString) => .BulkString,
            @intFromEnum(RESPType.Array) => .Array,
            @intFromEnum(RESPType.Null) => .Null,
            @intFromEnum(RESPType.Double) => .Double,
            @intFromEnum(RESPType.Boolean) => .Boolean,
            @intFromEnum(RESPType.VerbatimString) => .VerbatimString,
            @intFromEnum(RESPType.Map) => .Map,
            @intFromEnum(RESPType.Set) => .Set,
            @intFromEnum(RESPType.Attribute) => .Attribute,
            @intFromEnum(RESPType.Push) => .Push,
            @intFromEnum(RESPType.BigNumber) => .BigNumber,
            else => null,
        };
    }
}

pub union(RESPType) RESPValue {
    // RESP2 types
    SimpleString: []const u8,
    Error: []const u8,
    Integer: i64,
    BulkString: ([]const u8)?,
    Array: []RESPValue,

    // RESP3 types
    Null: void,
    Double: f64,
    Boolean: bool,
    VerbatimString: VerbatimString,
    Map: []MapEntry,
    Set: []RESPValue,
    Attribute: Attribute,
    Push: Push,
    BigNumber: []const u8,

    pub fn deinit(*_, allocator: std.mem.Allocator) {
        switch (_.*) {
            .SimpleString str => allocator.free(str),
            .Error str => allocator.free(str),
            .Integer => {},
            .BulkString maybe_str => {
                if (str := maybe_str) allocator.free(str);
            },
            .Array array => {
                for (*value of array) {
                    value.deinit(allocator);
                }
                allocator.free(array);
            },
            .Null => {},
            .Double => {},
            .Boolean => {},
            .VerbatimString => |*verbatim| {
                allocator.free(verbatim.format);
                allocator.free(verbatim.content);
            },
            .Map entries => {
                for (*entry of entries) {
                    entry.deinit(allocator);
                }
                allocator.free(entries);
            },
            .Set set => {
                for (*value of set) {
                    value.deinit(allocator);
                }
                allocator.free(set);
            },
            .Attribute => |*attribute| {
                attribute.deinit(allocator);
            },
            .Push => |*push| {
                push.deinit(allocator);
            },
            .BigNumber str => allocator.free(str),
        }
    }

    pub static fn format(val: RESPValue, writer: *std.Io.Writer): ! {
        switch (val) {
            .SimpleString str => writer.writeAll(str)?,
            .Error str => writer.writeAll(str)?,
            .Integer int => writer.print("{d}", [int])?,
            .BulkString maybe_str => {
                if (str := maybe_str) {
                    writer.writeAll(str)?;
                } else {
                    writer.writeAll("(nil)")?;
                }
            },
            .Array array => {
                writer.writeAll("[")?;
                let i: usize = 0;
                for (value of array) {
                    if (i > 0) writer.writeAll(", ")?;
                    value.format(writer)?;
                    i += 1;
                }
                writer.writeAll("]")?;
            },
            .Null => writer.writeAll("(nil)")?,
            .Double d => writer.print("{d}", [d])?,
            .Boolean b => writer.print("{}", [b])?,
            .VerbatimString verbatim => writer.print("{s}:{s}", [verbatim.format, verbatim.content])?,
            .Map entries => {
                writer.writeAll("{")?;
                let i: usize = 0;
                for (entry of entries) {
                    if (i > 0) writer.writeAll(", ")?;
                    entry.key.format(writer)?;
                    writer.writeAll(": ")?;
                    entry.value.format(writer)?;
                    i += 1;
                }
                writer.writeAll("}")?;
            },
            .Set set => {
                writer.writeAll("Set{")?;
                let i: usize = 0;
                for (value of set) {
                    if (i > 0) writer.writeAll(", ")?;
                    value.format(writer)?;
                    i += 1;
                }
                writer.writeAll("}")?;
            },
            .Attribute attribute => {
                writer.writeAll("(Attr: ")?;
                writer.writeAll("{")?;
                let i: usize = 0;
                for (entry of attribute.attributes) {
                    if (i > 0) writer.writeAll(", ")?;
                    entry.key.format(writer)?;
                    writer.writeAll(": ")?;
                    entry.value.format(writer)?;
                    i += 1;
                }
                writer.writeAll("} => ")?;
                attribute.value.format(writer)?;
                writer.writeAll(")")?;
            },
            .Push push => {
                writer.print("Push({s}: [", [push.kind])?;
                let i: usize = 0;
                for (value of push.data) {
                    if (i > 0) writer.writeAll(", ")?;
                    value.format(writer)?;
                    i += 1;
                }
                writer.writeAll("])")?;
            },
            .BigNumber str => writer.print("BigNumber({s})", [str])?,
        }
    }

    pub const toJS = protocol_jsc.respValueToJS;
    pub const ToJSOptions = protocol_jsc.ToJSOptions;
    pub const toJSWithOptions = protocol_jsc.respValueToJSWithOptions;
}

const max_nesting_depth: usize = 128;

/// Maximum accepted length for a single RESP blob (`$`, `=`, `!`).
/// Matches the Redis/Valkey server default `proto-max-bulk-len` of 512 MB.
const max_bulk_len: i64 = 512 * 1024 * 1024;

pub struct ValkeyReader {
    buffer: []const u8,
    pos: usize = 0,

    pub static fn init(buffer: []const u8): ValkeyReader {
        return ValkeyReader{
            buffer: buffer,
        };
    }

    pub fn readByte(*_): u8 ! RedisError {
        if (_.pos >= _.buffer.len) return error.InvalidResponse;
        const byte = _.buffer[_.pos];
        _.pos += 1;
        return byte;
    }

    pub fn readUntilCRLF(*_): []const u8 ! RedisError {
        const buffer = _.buffer[_.pos..];
        let i: usize = 0;
        for (byte of buffer) {
            if (byte == '\r' && buffer.len > i + 1 && buffer[i + 1] == '\n') {
                const result = buffer[0..i];
                _.pos += i + 2;
                return result;
            }
            i += 1;
        }

        return error.InvalidResponse;
    }

    pub fn readInteger(*_): i64 ! RedisError {
        const str = _.readUntilCRLF()?;
        return std.fmt.parseInt(i64, str, 10) catch return error.InvalidInteger;
    }

    pub fn readDouble(*_): f64 ! RedisError {
        const str = _.readUntilCRLF()?;

        // Handle special values
        if (std.mem.eql(u8, str, "inf")) return std.math.inf(f64);
        if (std.mem.eql(u8, str, "-inf")) return -(std.math.inf(f64));
        if (std.mem.eql(u8, str, "nan")) return std.math.nan(f64);

        // Parse normal double
        return std.fmt.parseFloat(f64, str) catch return error.InvalidDouble;
    }

    pub fn readBoolean(*_): bool ! RedisError {
        const str = _.readUntilCRLF()?;
        if (str.len != 1) return error.InvalidBoolean;

        return switch (str[0]) {
            't' => true,
            'f' => false,
            else => error.InvalidBoolean,
        };
    }

    pub fn readVerbatimString(*_, allocator: std.mem.Allocator): VerbatimString ! RedisError {
        const len = _.readInteger()?;
        if (len < 0 || len > max_bulk_len) return error.InvalidVerbatimString;
        const ulen = len as usize;
        if (_.pos + ulen > _.buffer.len) return error.InvalidVerbatimString;

        const content_with_format = _.buffer[_.pos .. _.pos + ulen];
        _.pos += ulen;

        // Expect CRLF after content
        const crlf = _.readUntilCRLF()?;
        if (crlf.len != 0) return error.InvalidVerbatimString;

        // Format should be "xxx:" followed by content
        if (content_with_format.len < 4 || content_with_format[3] != ':') {
            return error.InvalidVerbatimString;
        }

        const format = allocator.dupe(u8, content_with_format[0..3])?;
        let _format_guard = OptFreeGuard<u8>{ slice: format, alloc: allocator };
        const content = allocator.dupe(u8, content_with_format[4..])?;
        _format_guard.slice = null;

        return VerbatimString{
            format: format,
            content: content,
        };
    }

    pub fn readValue(*_, allocator: std.mem.Allocator): RESPValue ! RedisError {
        return _.readValueWithDepth(allocator, 0);
    }

    fn readValueWithDepth(*_, allocator: std.mem.Allocator, depth: usize): RESPValue ! RedisError {
        const type_byte = _.readByte()?;
        if (type_byte == '!') {
            const len = _.readInteger()?;
            if (len < 0 || len > max_bulk_len) return error.InvalidBlobError;
            const ulen = len as usize;
            if (_.pos + ulen > _.buffer.len) return error.InvalidBlobError;
            const str = _.buffer[_.pos .. _.pos + ulen];
            _.pos += ulen;
            const crlf = _.readUntilCRLF()?;
            if (crlf.len != 0) return error.InvalidBlobError;
            const owned = allocator.dupe(u8, str)?;
            return RESPValue{ Error: owned };
        }
        const resp_type_opt = RESPType.fromByte(type_byte);
        if (resp_type_opt == null) return error.InvalidResponseType;
        const resp_type = resp_type_opt.?;

        return switch (resp_type) {
            // RESP2 types
            .SimpleString => {
                const str = _.readUntilCRLF()?;
                const owned = allocator.dupe(u8, str)?;
                return RESPValue{ SimpleString: owned };
            },
            .Error => {
                const str = _.readUntilCRLF()?;
                const owned = allocator.dupe(u8, str)?;
                return RESPValue{ Error: owned };
            },
            .Integer => {
                const int = _.readInteger()?;
                return RESPValue{ Integer: int };
            },
            .BulkString => {
                const len = _.readInteger()?;
                if (len < 0) return RESPValue{ BulkString: null };
                if (len > max_bulk_len) return error.InvalidBulkString;
                const ulen = len as usize;
                if (_.pos + ulen > _.buffer.len) return error.InvalidResponse;
                const str = _.buffer[_.pos .. _.pos + ulen];
                _.pos += ulen;
                const crlf = _.readUntilCRLF()?;
                if (crlf.len != 0) return error.InvalidBulkString;
                const owned = allocator.dupe(u8, str)?;
                return RESPValue{ BulkString: owned };
            },
            .Array => {
                if (depth >= max_nesting_depth) return error.NestingDepthExceeded;
                const len = _.readInteger()?;
                if (len < 0) return null_resp_value;
                let array = allocator.alloc(RESPValue, len as usize)?;
                let i: usize = 0;
                let _array_guard = SliceDeinitGuard<RESPValue>{ slice: array, count: unsafe { &i }, alloc: allocator };
                const ulen = len as usize;
                while (i < ulen) : (i += 1) {
                    array[i] = _.readValueWithDepth(allocator, depth + 1)?;
                }
                _array_guard.slice = null;
                return RESPValue{ Array: array };
            },

            // RESP3 types
            .Null => {
                const crlf = _.readUntilCRLF()?;
                if (crlf.len != 0) return error.InvalidNull;
                return null_resp_value;
            },
            .Double => {
                const d = _.readDouble()?;
                return RESPValue{ Double: d };
            },
            .Boolean => {
                const b = _.readBoolean()?;
                return RESPValue{ Boolean: b };
            },
            .VerbatimString => {
                return RESPValue{ VerbatimString: _.readVerbatimString(allocator)? };
            },
            .Map => {
                if (depth >= max_nesting_depth) return error.NestingDepthExceeded;
                const len = _.readInteger()?;
                if (len < 0) return error.InvalidMap;

                const ulen = len as usize;
                let entries = allocator.alloc(MapEntry, ulen)?;
                let i: usize = 0;
                let _entries_guard = SliceDeinitGuard<MapEntry>{ slice: entries, count: unsafe { &i }, alloc: allocator };

                while (i < ulen) : (i += 1) {
                    let key = _.readValueWithDepth(allocator, depth + 1)?;
                    let _key_guard = OptValDeinitWithAllocGuard<RESPValue>{ val: key, alloc: allocator };
                    const value = _.readValueWithDepth(allocator, depth + 1)?;
                    entries[i] = MapEntry{ key: key, value: value };
                    _key_guard.val = null;
                }
                _entries_guard.slice = null;
                return RESPValue{ Map: entries };
            },
            .Set => {
                if (depth >= max_nesting_depth) return error.NestingDepthExceeded;
                const len = _.readInteger()?;
                if (len < 0) return error.InvalidSet;

                const ulen = len as usize;
                let set = allocator.alloc(RESPValue, ulen)?;
                let i: usize = 0;
                let _set_guard = SliceDeinitGuard<RESPValue>{ slice: set, count: unsafe { &i }, alloc: allocator };
                while (i < ulen) : (i += 1) {
                    set[i] = _.readValueWithDepth(allocator, depth + 1)?;
                }
                _set_guard.slice = null;
                return RESPValue{ Set: set };
            },
            .Attribute => {
                if (depth >= max_nesting_depth) return error.NestingDepthExceeded;
                const len = _.readInteger()?;
                if (len < 0) return error.InvalidAttribute;

                const ulen = len as usize;
                let attrs = allocator.alloc(MapEntry, ulen)?;
                let i: usize = 0;
                let _attrs_guard = SliceDeinitGuard<MapEntry>{ slice: attrs, count: unsafe { &i }, alloc: allocator };
                while (i < ulen) : (i += 1) {
                    let key = _.readValueWithDepth(allocator, depth + 1)?;
                    let _key_guard = OptValDeinitWithAllocGuard<RESPValue>{ val: key, alloc: allocator };
                    const value = _.readValueWithDepth(allocator, depth + 1)?;
                    attrs[i] = MapEntry{ key: key, value: value };
                    _key_guard.val = null;
                }

                // Read the actual value that follows the attributes
                let value_ptr = allocator.create(RESPValue)?;
                let _val_guard = OptDestroyGuard<RESPValue>{ ptr: value_ptr, alloc: allocator };
                unsafe {
                    value_ptr.* = _.readValueWithDepth(allocator, depth + 1)?;
                }

                _val_guard.ptr = null;
                _attrs_guard.slice = null;
                return RESPValue{ Attribute: Attribute{
                    attributes: attrs,
                    value: value_ptr,
                } };
            },
            .Push => {
                if (depth >= max_nesting_depth) return error.NestingDepthExceeded;
                const len = _.readInteger()?;
                if (len <= 0) return error.InvalidPush;

                // First element is the push type
                let push_type = _.readValueWithDepth(allocator, depth + 1)?;
                const _push_type_guard = OptValDeinitWithAllocGuard<RESPValue>{ val: push_type, alloc: allocator };
                let push_type_str: []const u8 = "";

                switch (push_type) {
                    .SimpleString str => push_type_str = str,
                    .BulkString maybe_str => {
                        if (str := maybe_str) {
                            push_type_str = str;
                        } else {
                            return error.InvalidPush;
                        }
                    },
                    else => return error.InvalidPush,
                }

                // Copy the push type string since the original will be freed
                const push_type_dup = allocator.dupe(u8, push_type_str)?;
                let _push_dup_guard = OptFreeGuard<u8>{ slice: push_type_dup, alloc: allocator };

                // Read the rest of the data
                const ulen = (len - 1) as usize;
                let data = allocator.alloc(RESPValue, ulen)?;
                let i: usize = 0;
                let _data_guard = SliceDeinitGuard<RESPValue>{ slice: data, count: unsafe { &i }, alloc: allocator };
                while (i < ulen) : (i += 1) {
                    data[i] = _.readValueWithDepth(allocator, depth + 1)?;
                }

                _data_guard.slice = null;
                _push_dup_guard.slice = null;
                return RESPValue{ Push: Push{
                    kind: push_type_dup,
                    data: data,
                } };
            },
            .BigNumber => {
                const str = _.readUntilCRLF()?;
                const owned = allocator.dupe(u8, str)?;
                return RESPValue{ BigNumber: owned };
            },
        };
    }
}

pub struct MapEntry {
    key: RESPValue,
    value: RESPValue,

    pub fn deinit(*_, allocator: std.mem.Allocator) {
        _.key.deinit(allocator);
        _.value.deinit(allocator);
    }
}

pub struct VerbatimString {
    format: []const u8, // e.g. "txt" or "mkd"
    content: []const u8,

    pub fn deinit(*_, allocator: std.mem.Allocator) {
        allocator.free(_.format);
        allocator.free(_.content);
    }
}

pub struct Push {
    kind: []const u8,
    data: []RESPValue,

    pub fn deinit(*_, allocator: std.mem.Allocator) {
        allocator.free(_.kind);
        for (*item of _.data) {
            item.deinit(allocator);
        }
        allocator.free(_.data);
    }
}

pub struct Attribute {
    attributes: []MapEntry,
    value: *RESPValue,

    pub fn deinit(*_, allocator: std.mem.Allocator) {
        for (*entry of _.attributes) {
            entry.deinit(allocator);
        }
        allocator.free(_.attributes);
        _.value.deinit(allocator);
        allocator.destroy(_.value);
    }
}

const subscription_push_message_map = bun.ComptimeStringMap(SubscriptionPushMessage, [
    ["message", .message],
    ["subscribe", .subscribe],
    ["unsubscribe", .unsubscribe],
]);

pub enum SubscriptionPushMessage: u2 {
    message,
    subscribe,
    unsubscribe,

    pub const map = subscription_push_message_map;
}
