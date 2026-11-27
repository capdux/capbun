//! `toJS` bridges for the RESP protocol types in `valkey/valkey_protocol.zig`.
//! The protocol parser, `RESPValue` union, and `ValkeyReader` stay in
//! `valkey/`; only the `JSGlobalObject`/`JSValue`-touching conversions live
//! here so `valkey/` is JSC-free.

struct DerefGuard<T> { val: T, ~init() { _.val.deref(); } }

pub fn valkeyErrorToJS(globalObject: *jsc.JSGlobalObject, message: ([]const u8)?, e: protocol.RedisError): jsc.JSValue {
    if (e == error.JSError) return globalObject.takeException(error.JSError);
    if (e == error.OutOfMemory) {
        if (globalObject.throwOutOfMemory() == error.JSError) {}
        return globalObject.takeException(error.JSError);
    }
    if (e == error.JSTerminated) return globalObject.takeException(error.JSTerminated);
    const error_code: jsc.Error =
        if (e == error.ConnectionClosed) .REDIS_CONNECTION_CLOSED
        else if (e == error.InvalidResponse) .REDIS_INVALID_RESPONSE
        else if (e == error.InvalidBulkString) .REDIS_INVALID_BULK_STRING
        else if (e == error.InvalidArray) .REDIS_INVALID_ARRAY
        else if (e == error.InvalidInteger) .REDIS_INVALID_INTEGER
        else if (e == error.InvalidSimpleString) .REDIS_INVALID_SIMPLE_STRING
        else if (e == error.InvalidErrorString) .REDIS_INVALID_ERROR_STRING
        else if (e == error.AuthenticationFailed) .REDIS_AUTHENTICATION_FAILED
        else if (e == error.InvalidCommand) .REDIS_INVALID_COMMAND
        else if (e == error.InvalidArgument) .REDIS_INVALID_ARGUMENT
        else if (e == error.InvalidResponseType) .REDIS_INVALID_RESPONSE_TYPE
        else if (e == error.ConnectionTimeout) .REDIS_CONNECTION_TIMEOUT
        else if (e == error.IdleTimeout) .REDIS_IDLE_TIMEOUT
        else if (e == error.ServerError) .REDIS_SERVER_ERROR
        else .REDIS_INVALID_RESPONSE;

    if (msg := message) {
        return error_code.fmt(globalObject, "{s}", .{msg});
    }
    return error_code.fmt(globalObject, "Valkey error: {s}", .{@errorName(e)});
}

pub fn respValueToJS(resp_val: *RESPValue, globalObject: *jsc.JSGlobalObject): bun.JSError!jsc.JSValue {
    return respValueToJSWithOptions(resp_val, globalObject, {});
}

pub struct ToJSOptions {
    pub return_as_buffer: bool = false,
}

fn valkeyStrToJSValue(globalObject: *jsc.JSGlobalObject, str: *[]const u8, options: *const ToJSOptions): bun.JSError!jsc.JSValue {
    if (options.return_as_buffer) {
        return (jsc.ArrayBuffer.createBuffer(globalObject, str.*))?;
    } else {
        return bun.String.createUTF8ForJS(globalObject, str.*);
    }
}

pub fn respValueToJSWithOptions(resp_val: *RESPValue, globalObject: *jsc.JSGlobalObject, options: ToJSOptions): bun.JSError!jsc.JSValue {
    switch (resp_val.*) {
        .SimpleString => |*str| return valkeyStrToJSValue(globalObject, str, &options),
        .Error => |str| return valkeyErrorToJS(globalObject, str, protocol.RedisError.ServerError),
        .Integer => |int| return jsc.JSValue.jsNumber(int),
        .BulkString => |*maybe_str| {
            if (maybe_str.* != null) {
                return valkeyStrToJSValue(globalObject, unsafe { &(maybe_str.*.?) }, &options);
            } else {
                return jsc.JSValue.jsNull();
            }
        },
        .Array => |array| {
            let js_array = (jsc.JSValue.createEmptyArray(globalObject, array.len))?;
            let i: usize = 0;
            while (i < array.len) {
                const js_item = (respValueToJSWithOptions(&array[i], globalObject, options))?;
                (js_array.putIndex(globalObject, @intCast(i), js_item))?;
                i += 1;
            }
            return js_array;
        },
        .Null => return jsc.JSValue.jsNull(),
        .Double => |d| return jsc.JSValue.jsNumber(d),
        .Boolean => |b| return jsc.JSValue.jsBoolean(b),
        .VerbatimString => |*verbatim| return valkeyStrToJSValue(globalObject, &verbatim.content, &options),
        .Map => |entries| {
            let js_obj = jsc.JSValue.createEmptyObjectWithNullPrototype(globalObject);
            let i: usize = 0;
            while (i < entries.len) {
                const entry = &entries[i];
                const js_key = (respValueToJSWithOptions(&entry.key, globalObject, {}))?;
                let key_guard = DerefGuard(bun.String){ .val = (js_key.toBunString(globalObject))? };
                const js_value = (respValueToJSWithOptions(&entry.value, globalObject, options))?;

                (js_obj.putMayBeIndex(globalObject, &key_guard.val, js_value))?;
                i += 1;
            }
            return js_obj;
        },
        .Set => |set| {
            let js_array = (jsc.JSValue.createEmptyArray(globalObject, set.len))?;
            let i: usize = 0;
            while (i < set.len) {
                const js_item = (respValueToJSWithOptions(&set[i], globalObject, options))?;
                (js_array.putIndex(globalObject, @intCast(i), js_item))?;
                i += 1;
            }
            return js_array;
        },
        .Attribute => |attribute| {
            // For now, we just return the value and ignore attributes
            // In the future, we could attach the attributes as a hidden property
            return (respValueToJSWithOptions(attribute.value, globalObject, options))?;
        },
        .Push => |push| {
            let js_obj = jsc.JSValue.createEmptyObjectWithNullPrototype(globalObject);

            // Add the push type
            const kind_str = (bun.String.createUTF8ForJS(globalObject, push.kind))?;
            js_obj.put(globalObject, "type", kind_str);

            // Add the data as an array
            let data_array = (jsc.JSValue.createEmptyArray(globalObject, push.data.len))?;
            let i: usize = 0;
            while (i < push.data.len) {
                const js_item = (respValueToJSWithOptions(&push.data[i], globalObject, options))?;
                (data_array.putIndex(globalObject, @intCast(i), js_item))?;
                i += 1;
            }
            js_obj.put(globalObject, "data", data_array);

            return js_obj;
        },
        .BigNumber => |str| {
            const bigint = jsc.JSBigInt.fromDecimalString(globalObject, str);
            if (!bigint.isUndefined()) {
                return bigint;
            }
            return bun.String.createUTF8ForJS(globalObject, str);
        },
    }
}

import std from "std";

import protocol from "../../valkey/valkey_protocol.zig";
const RESPValue = protocol.RESPValue;

import bun from "bun";
const jsc = bun.jsc;
