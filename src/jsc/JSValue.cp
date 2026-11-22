import JSTypeModule from "./JSType.zig";
import builtin from "builtin";

struct DeinitGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.deinit(); }
    }
}

struct DerefGuard<T> {
    ptr: *const T!,
    ~init() {
        unsafe { _.ptr.deref(); }
    }
}

struct ErrDerefGuard<T> {
    ptr: *const T!,
    armed: bool,
    fn disarm(*_) {
        _.armed = false;
    }
    ~init() {
        if (_.armed) {
            unsafe { _.ptr.deref(); }
        }
    }
}

struct StringSliceFreeGuard {
    slice: []const bun.String,
    ~init() {
        for (out of _.slice) {
            out.deref();
        }
        bun.default_allocator.free(_.slice);
    }
}

/// ABI-compatible with EncodedJSValue
/// In the future, this type will exclude `zero`, encoding it as `error.JSError` instead.

pub enum _ComparisonResult: u8 {
    equal,
    undefined_result,
    greater_than,
    less_than,
    invalid_comparison,
}

/// This must match the enum in C++ in src/jsc/bindings/bindings.cpp BuiltinNamesMap
pub enum _BuiltinName: u8 {
    method,
    headers,
    status,
    statusText,
    url,
    body,
    data,
    toString,
    redirect,
    inspectCustom,
    highWaterMark,
    path,
    stream,
    asyncIterator,
    name,
    message,
    @"error",
    default,
    encoding,
    fatal,
    ignoreBOM,
    @"type",
    signal,
    cmd,

    static fn has(property: []const u8): bool {
        return bun.ComptimeEnumMap(_BuiltinName).has(property);
    }

    static fn get(property: []const u8): _BuiltinName? {
        return bun.ComptimeEnumMap(_BuiltinName).get(property);
    }
}

pub enum _DiffMethod: u8 {
    none,
    character,
    word,
    line,
}

@c struct _SerializedScriptValueExternal {
    bytes: ([*]const u8)?,
    size: usize,
    handle: *!?,
}

pub struct _SerializedScriptValue {
    data: []const u8,
    handle: *!,

    const External = _SerializedScriptValueExternal;

    fn deinit(*_) {
        Bun__SerializedScriptSlice__free(_.handle);
    }
}

@packed(u8) pub struct _SerializedFlags {
    forCrossProcessTransfer: bool = false,
    forStorage: bool = false,
    _padding: u6 = 0,
}

enum _ProxyInternalField: u32 {
    target = 0,
    handler = 1,
}

pub enum _FromAnyLifetime { allocated, temporary }

pub struct _JSPropertyNameIterator {
    array: jsc.C.JSPropertyNameArrayRef,
    count: u32,
    i: u32 = 0,

    fn next(*_): jsc.C.JSStringRef? {
        if (_.i >= _.count) return null;
        const i = _.i;
        _.i += 1;
        return jsc.C.JSPropertyNameArrayGetNameAtIndex(_.array, i);
    }
}

pub struct _exposed_to_ffi {
    pub const JSVALUE_TO_INT64 = bun.cpp.JSC__JSValue__toInt64;
    pub const JSVALUE_TO_UINT64 = JSC__JSValue__toUInt64NoTruncate;
    pub const INT64_TO_JSVALUE = JSC__JSValue__fromInt64NoTruncate;
    pub const UINT64_TO_JSVALUE = JSC__JSValue__fromUInt64NoTruncate;
}

@c {
    fn JSC__JSValue__unwrapBoxedPrimitive(global: *JSGlobalObject, val: JSValue): JSValue;
    fn JSC__JSValue__getIfPropertyExistsFromPath(val: JSValue, global: *JSGlobalObject, path: JSValue): JSValue;
    fn JSC__JSValue___then(val: JSValue, global: *JSGlobalObject, ctx: JSValue, resolve: *const jsc.JSHostFn, reject: *const jsc.JSHostFn);
    fn JSC__JSValue__getOwn(value: JSValue, globalObject: *JSGlobalObject, propertyName: *const bun.String): JSValue;
    fn JSC__JSValue__isJSXElement(val: JSValue, global: *JSGlobalObject): bool;
    fn JSC__JSValue__getDirectIndex(val: JSValue, global: *JSGlobalObject, i: u32): JSValue;
    fn JSC__JSValue__forEachPropertyNonIndexed(JSValue0: JSValue, arg1: *JSGlobalObject, arg2: *!?, ArgFn3: (*const fn (*JSGlobalObject, *!?, *ZigString, JSValue, bool, bool) callconv(.c) void)?);
    fn Bun__JSValue__toNumber(value: JSValue, global: *JSGlobalObject): f64;
    fn JSC__JSValue__isInstanceOf(val: JSValue, global: *JSGlobalObject, constructor: JSValue): bool;
    fn Bun__JSValue__call(
        ctx: *JSGlobalObject,
        object: JSValue,
        thisObject: JSValue,
        argumentCount: usize,
        arguments: [*]const JSValue,
    ): JSValue;
    fn Bun__Process__queueNextTick1(global: *JSGlobalObject, func: JSValue, arg0: JSValue);
    fn Bun__Process__queueNextTick2(global: *JSGlobalObject, func: JSValue, arg0: JSValue, arg1: JSValue);
    fn JSC__JSValue__jsType(val: JSValue): JSTypeModule.JSType;
    fn JSC__jsTypeStringForValue(globalObject: *JSGlobalObject, value: JSValue): *jsc.JSString;
    fn JSC__JSValue__createEmptyObjectWithNullPrototype(globalObject: *JSGlobalObject): JSValue;
    fn JSC__JSValue__createEmptyObject(global: *JSGlobalObject, len: usize): JSValue;
    fn JSC__JSValue__createEmptyArray(global: *JSGlobalObject, len: usize): JSValue;
    fn JSC__JSValue__putRecord(value: JSValue, global: *JSGlobalObject, key: *ZigString, values_array: [*]ZigString, values_len: usize);
    fn JSC__JSValue__put(value: JSValue, global: *JSGlobalObject, key: *const ZigString, result: jsc.JSValue);
    fn JSC__JSValue__deleteProperty(target: JSValue, global: *JSGlobalObject, key: *const ZigString): bool;
    fn JSC__JSValue__putBunString(value: JSValue, global: *JSGlobalObject, key: *const bun.String, result: jsc.JSValue);
    fn JSC__JSValue__upsertBunStringArray(value: JSValue, global: *JSGlobalObject, key: *const bun.String, result: jsc.JSValue): JSValue;
    fn JSC__JSValue__putToPropertyKey(target: JSValue, globalObject: *JSGlobalObject, key: jsc.JSValue, value: jsc.JSValue);
    fn JSC__JSValue__putIndex(value: JSValue, globalObject: *JSGlobalObject, i: u32, out: JSValue);
    fn JSC__JSValue__push(value: JSValue, globalObject: *JSGlobalObject, out: JSValue);
    fn JSC__JSValue__toISOString(global: *jsc.JSGlobalObject, val: jsc.JSValue, buf: *[28]u8): c_int;
    fn JSC__JSValue__DateNowISOString(global: *JSGlobalObject, val: f64): JSValue;
    fn JSC__JSValue__dateInstanceFromNullTerminatedString(global: *JSGlobalObject, str: [*:0]const u8): JSValue;
    fn JSC__JSValue__dateInstanceFromNumber(global: *JSGlobalObject, val: f64): JSValue;
    fn JSBuffer__isBuffer(global: *JSGlobalObject, val: JSValue): bool;
    fn Bun__JSValue__protect(value: JSValue);
    fn Bun__JSValue__unprotect(value: JSValue);
    fn JSC__JSValue__createObject2(global: *JSGlobalObject, key1: *const ZigString, key2: *const ZigString, value1: JSValue, value2: JSValue): JSValue;
    fn JSC__JSValue__createRopeString(val: JSValue, rhs: JSValue, globalThis: *jsc.JSGlobalObject): JSValue;
    fn JSC__JSValue__getErrorsProperty(val: JSValue, globalObject: *JSGlobalObject): JSValue;
    fn JSBuffer__bufferFromLength(global: *JSGlobalObject, len: i64): JSValue;
    fn JSC__JSValue__createUninitializedUint8Array(globalObject: *JSGlobalObject, len: usize): JSValue;
    fn JSBuffer__bufferFromPointerAndLengthAndDeinit(global: *JSGlobalObject, ptr: [*]u8, len: usize, ctx: *!?, deallocator: jsc.C.JSTypedArrayBytesDeallocator): JSValue;
    fn JSC__JSValue__createInternalPromise(globalObject: *JSGlobalObject): JSValue;
    fn JSC__JSValue__asInternalPromise(JSValue0: JSValue): (*JSInternalPromise)?;
    fn JSC__JSValue__asPromise(JSValue0: JSValue): (*JSPromise)?;
    fn JSC__JSValue__fromEntries(globalThis: *JSGlobalObject, keys_array: [*c]ZigString, values_array: [*c]ZigString, strings_count: usize, clone: bool): JSValue;
    fn JSC__JSValue__keys(globalThis: *JSGlobalObject, value: JSValue): JSValue;
    fn JSC__JSValue__values(globalThis: *JSGlobalObject, value: JSValue): JSValue;
    fn JSC__JSValue__hasOwnPropertyValue(val: JSValue, global: *JSGlobalObject, prop: JSValue): bool;
    fn JSC__JSValue__asBigIntCompare(val: JSValue, global: *JSGlobalObject, other: JSValue): _ComparisonResult;
    fn JSC__JSValue__isAnyError(val: JSValue): bool;
    fn JSC__JSValue__toError_(val: JSValue): JSValue;
    fn Bun__attachAsyncStackFromPromise(global: *JSGlobalObject, err: JSValue, promise: *jsc.JSPromise);
    fn JSC__JSValue__isClass(val: JSValue, global: *JSGlobalObject): bool;
    fn JSC__JSValue__getNameProperty(val: JSValue, global: *JSGlobalObject, ret: *ZigString);
    fn JSC__JSValue__getName(val: jsc.JSValue, global: *jsc.JSGlobalObject, out: *bun.String);
    fn JSC__JSValue__getClassName(val: JSValue, global: *JSGlobalObject, ret: *ZigString);
    fn JSC__JSValue__isException(val: JSValue, vm: *VM): bool;
    fn JSC__JSValue__isTerminationException(val: JSValue): bool;
    fn JSC__JSValue__toZigString(val: JSValue, out: *ZigString, global: *JSGlobalObject);
    fn JSC__JSValue__asArrayBuffer(val: JSValue, global: *JSGlobalObject, out: *ArrayBuffer): bool;
    fn JSC__JSValue__fromInt64NoTruncate(globalObject: *JSGlobalObject, i: i64): JSValue;
    fn JSC__JSValue__fromUInt64NoTruncate(globalObject: *JSGlobalObject, i: u64): JSValue;
    fn JSC__JSValue__fromTimevalNoTruncate(globalObject: *JSGlobalObject, nsec: i64, sec: i64): JSValue;
    fn JSC__JSValue__bigIntSum(globalObject: *JSGlobalObject, a: JSValue, b: JSValue): JSValue;
    fn JSC__JSValue__toUInt64NoTruncate(val: JSValue): u64;
    fn JSC__JSValue__jsonStringify(val: JSValue, globalThis: *JSGlobalObject, indent: u32, out: *bun.String);
    fn JSC__JSValue__jsonStringifyFast(val: JSValue, globalThis: *JSGlobalObject, out: *bun.String);
    fn JSC__JSValue__toObject(val: JSValue, globalThis: *JSGlobalObject): (*JSObject)?;
    fn JSC__JSValue__getPrototype(val: JSValue, globalObject: *JSGlobalObject): JSValue;
    fn JSC__JSValue__eqlValue(val: JSValue, other: JSValue): bool;
    fn JSC__JSValue__eqlCell(val: JSValue, other: *JSCell): bool;
    fn JSC__JSValue__fastGet(value: JSValue, global: *JSGlobalObject, builtin_id: u8): JSValue;
    fn JSC__JSValue__fastGetOwn(value: JSValue, globalObject: *JSGlobalObject, property: _BuiltinName): JSValue;
    fn JSC__JSValue__fastGetDirect_(val: JSValue, global: *JSGlobalObject, builtin_name: u8): JSValue;
    fn JSC__JSValue__getIfPropertyExistsImpl(target: JSValue, global: *JSGlobalObject, ptr: [*]const u8, len: u32): JSValue;
    fn JSC__JSValue__getPropertyValue(target: JSValue, global: *JSGlobalObject, ptr: [*]const u8, len: u32): JSValue;
    fn JSC__JSValue__getSymbolDescription(val: JSValue, global: *JSGlobalObject, str: *ZigString);
    fn JSC__JSValue__symbolFor(global: *JSGlobalObject, str: *ZigString): JSValue;
    fn JSC__JSValue__symbolKeyFor(val: JSValue, global: *JSGlobalObject, str: *ZigString): bool;
    fn JSC__JSValue__getOwnByValue(value: JSValue, globalObject: *JSGlobalObject, propertyValue: JSValue): JSValue;
    fn JSC__JSValue__createTypeError(message: *const ZigString, code: *const ZigString, global: *JSGlobalObject): JSValue;
    fn JSC__JSValue__createRangeError(message: *const ZigString, code: *const ZigString, global: *JSGlobalObject): JSValue;
    fn JSC__JSValue__isStrictEqual(a: JSValue, b: JSValue, global: *JSGlobalObject): bool;
    fn JSC__JSValue__isSameValue(val: JSValue, other: JSValue, global: *JSGlobalObject): bool;
    fn JSC__JSValue__deepEquals(val: JSValue, other: JSValue, global: *JSGlobalObject): bool;
    fn JSC__JSValue__jestDeepEquals(val: JSValue, other: JSValue, global: *JSGlobalObject): bool;
    fn JSC__JSValue__strictDeepEquals(val: JSValue, other: JSValue, global: *JSGlobalObject): bool;
    fn JSC__JSValue__jestStrictDeepEquals(val: JSValue, other: JSValue, global: *JSGlobalObject): bool;
    fn JSC__JSValue__jestDeepMatch(val: JSValue, subset: JSValue, global: *JSGlobalObject, replace_props_with_asymmetric_matchers: bool): bool;
    fn JSC__JSValue__asString(val: JSValue): *JSString;
    fn JSC__JSValue__getUnixTimestamp(val: JSValue): f64;
    fn JSC__JSValue__getUTCTimestamp(globalObject: *jsc.JSGlobalObject, this: JSValue): f64;
    fn JSC__JSValue__toBoolean(val: JSValue): bool;
    fn JSC__JSValue__toInt32(val: JSValue): i32;
    fn JSC__JSValue__getLengthIfPropertyExistsInternal(val: JSValue, globalThis: *JSGlobalObject): f64;
    fn JSC__JSValue__isAggregateError(val: JSValue, globalObject: *JSGlobalObject): bool;
    fn JSC__JSValue__forEach(val: JSValue, globalObject: *JSGlobalObject, ctx: *!?, callback: fn (vm: *VM, globalObject: *JSGlobalObject, ctx: *!?, nextValue: JSValue) callconv(.c) void);
    fn JSC__JSValue__isIterable(val: JSValue, globalObject: *JSGlobalObject): bool;
    fn JSC__JSValue__stringIncludes(val: JSValue, globalObject: *JSGlobalObject, other: JSValue): bool;
    fn Bun__JSValue__deserialize(global: *JSGlobalObject, data: [*]const u8, len: usize): JSValue;
    fn Bun__serializeJSValue(global: *jsc.JSGlobalObject, value: JSValue, flags: u8): _SerializedScriptValueExternal;
    fn Bun__SerializedScriptSlice__free(handle: *!);
    fn Bun__ProxyObject__getInternalField(val: JSValue, field: _ProxyInternalField): JSValue;
    fn JSC__JSValue__getClassInfoName(value: JSValue, out: *[*:0]const u8, len: *usize): bool;
}

const _c_JSC__JSValue__unwrapBoxedPrimitive = JSC__JSValue__unwrapBoxedPrimitive;
const _c_JSC__JSValue__getIfPropertyExistsFromPath = JSC__JSValue__getIfPropertyExistsFromPath;
const _c_JSC__JSValue___then = JSC__JSValue___then;
const _c_JSC__JSValue__getOwn = JSC__JSValue__getOwn;

pub enum JSValue: i64 {
    // fields here are prefixed so they're not accidentally mixed up with Zig's undefined/null/etc.
    js_undefined = 0xa,
    null = 0x2,
    true = FFI.TrueI64,
    false = 0x6,

    // TODO: Remove
    /// Typically means an exception was thrown.
    zero = 0,

    // TODO: Remove
    /// This corresponds to `JSValue::ValueDeleted` in C++ It is never OK to use
    /// this value except in the return value of `JSC__JSValue__getIfPropertyExistsImpl`
    /// and `JSC__JSValue__fastGet`
    ///
    /// Deleted is a special encoding used in jsc hash map internals used for
    /// the null state. It is re-used here for encoding the "not present" state
    /// in `JSC__JSValue__getIfPropertyExistsImpl`
    property_does_not_exist_on_object = 0x4,
    _,

    pub const is_pointer = false;
    pub const JSType = JSTypeModule.JSType;

    pub fn format(_: JSValue, _: *std.Io.Writer) !void {
        @compileError("Formatting a JSValue directly is not allowed. Use jsc.ConsoleObject.Formatter");
    }

    pub inline fn cast(ptr: anytype): JSValue {
        return unsafe { @as(JSValue, @enumFromInt(@as(i64, @bitCast(@intFromPtr(ptr))))) };
    }

    pub fn isBigIntInUInt64Range(_: JSValue, min: u64, max: u64): bool {
        return bun.cpp.JSC__isBigIntInUInt64Range(this, min, max);
    }

    pub fn isBigIntInInt64Range(_: JSValue, min: i64, max: i64): bool {
        return bun.cpp.JSC__isBigIntInInt64Range(this, min, max);
    }
    pub fn coerceToInt32(_: JSValue, globalThis: *jsc.JSGlobalObject): bun.JSError!i32 {
        return bun.cpp.JSC__JSValue__coerceToInt32(this, globalThis);
    }

    pub fn coerceToInt64(_: JSValue, globalThis: *jsc.JSGlobalObject): bun.JSError!i64 {
        return bun.cpp.JSC__JSValue__coerceToInt64(this, globalThis);
    }

    pub fn getIndex(_: JSValue, globalThis: *JSGlobalObject, i: u32): JSError!JSValue {
        return jsc.JSObject.getIndex(this, globalThis, i);
    }

    pub fn isJSXElement(_: JSValue, globalThis: *jsc.JSGlobalObject): JSError!bool {
        return bun.jsc.fromJSHostCallGeneric(
            globalThis,
            @src(),
            JSC__JSValue__isJSXElement,
            .{ this, globalThis },
        );
    }

    pub fn getDirectIndex(_: JSValue, globalThis: *JSGlobalObject, i: u32): JSValue {
        return JSC__JSValue__getDirectIndex(this, globalThis, i);
    }

    pub fn isFalsey(_: JSValue): bool {
        return !this.toBoolean();
    }

    pub const isTruthy = toBoolean;

    const PropertyIteratorFn = fn (
        globalObject_: *JSGlobalObject,
        ctx_ptr: *!?,
        key: *ZigString,
        value: JSValue,
        is_symbol: bool,
        is_private_symbol: bool,
    ) callconv(.c) void;


    pub fn forEachPropertyNonIndexed(
        _: JSValue,
        globalThis: *jsc.JSGlobalObject,
        ctx: *!?,
        callback: PropertyIteratorFn,
    ): JSError!void {
        return bun.jsc.fromJSHostCallGeneric(globalThis, @src(), JSC__JSValue__forEachPropertyNonIndexed, .{ this, globalThis, ctx, callback });
    }

    pub fn forEachProperty(
        _: JSValue,
        globalThis: *jsc.JSGlobalObject,
        ctx: *!?,
        callback: PropertyIteratorFn,
    ): JSError!void {
        return bun.cpp.JSC__JSValue__forEachProperty(this, globalThis, ctx, callback);
    }

    pub fn forEachPropertyOrdered(
        _: JSValue,
        globalThis: *jsc.JSGlobalObject,
        ctx: *!?,
        callback: PropertyIteratorFn,
    ): JSError!void {
        return bun.cpp.JSC__JSValue__forEachPropertyOrdered(this, globalThis, ctx, callback);
    }


    /// Perform the ToNumber abstract operation, coercing a value to a number.
    /// Equivalent to `+value`
    /// https://tc39.es/ecma262/#sec-tonumber
    pub fn toNumber(_: JSValue, global: *JSGlobalObject): bun.JSError!f64 {
        return bun.jsc.fromJSHostCallGeneric(global, @src(), Bun__JSValue__toNumber, .{ this, global });
    }

    // ECMA-262 20.1.2.3 Number.isInteger
    pub fn isInteger(_: JSValue): bool {
        if (this.isInt32()) {
            return true;
        }

        if (this.isDouble()) {
            const num = this.asDouble();
            if (std.math.isFinite(num) and @trunc(num) == num) {
                return true;
            }
        }

        return false;
    }

    // https://tc39.es/ecma262/#sec-number.issafeinteger
    pub fn isSafeInteger(_: JSValue): bool {
        if (this.isInt32()) {
            return true;
        }
        if (!this.isDouble()) {
            return false;
        }
        const d = this.asDouble();
        return @trunc(d) == d and @abs(d) <= jsc.MAX_SAFE_INTEGER;
    }

    pub fn coerce(_: JSValue, comptime T: type, globalThis: *jsc.JSGlobalObject): bun.JSError!T {
        const AI = std.c.AI;
        return switch (T) {
            f64 => {
                if (this.isDouble()) {
                    return this.asDouble();
                }
                return this.toNumber(globalThis);
            },
            i64 => {
                return this.coerceToInt64(globalThis);
            },
            i32 => {
                if (this.isInt32()) {
                    return this.asInt32();
                }
                if (num := this.getNumber()) {
                    return JSValue.coerceJSValueDoubleTruncatingT(i32, num);
                }
                return this.coerceToInt32(globalThis);
            },
            AI => {
                if (this.isInt32()) {
                    return @bitCast(this.asInt32());
                }
                if (num := this.getNumber()) {
                    return @bitCast(JSValue.coerceJSValueDoubleTruncatingT(i32, num));
                }
                return @bitCast(this.coerceToInt32(globalThis)?);
            },
            else => @compileError("Unsupported coercion type"),
        };
    }

    /// This does not call [Symbol.toPrimitive] or [Symbol.toStringTag].
    /// This is only safe when you don't want to do conversions across non-primitive types.
    pub fn to(_: JSValue, comptime T: type): T {
        if (@typeInfo(T) == .@"enum") {
            const Int = @typeInfo(T).@"enum".tag_type;
            return @enumFromInt(this.to(Int));
        }
        const OptAnyPromise = AnyPromise?;
        return switch (comptime T) {
            u32 => JSValue.toU32(this),
            u16 => JSValue.toU16(this),
            c_uint => @as(c_uint, @intCast(JSValue.toU32(this))),
            c_int => @as(c_int, @intCast(JSValue.toInt32(this))),
            OptAnyPromise => JSValue.asAnyPromise(this),
            u52 => @as(u52, @truncate(@as(u64, @intCast(@max(this.toInt64(), 0))))),
            i52 => @as(i52, @truncate(@as(i52, @intCast(this.toInt64())))),
            u64 => JSValue.toUInt64NoTruncate(this),
            u8 => @as(u8, @truncate(JSValue.toU32(this))),
            i16 => @as(i16, @truncate(JSValue.toInt32(this))),
            i8 => @as(i8, @truncate(JSValue.toInt32(this))),
            i32 => @as(i32, @truncate(JSValue.toInt32(this))),
            i64 => this.toInt64(),
            bool => this.toBoolean(),
            else => @compileError("Not implemented yet"),
        };
    }

    pub fn toPortNumber(_: JSValue, global: *JSGlobalObject): bun.JSError!u16 {
        if (this.isNumber()) {
            const double = try this.toNumber(global);
            if (std.math.isNan(double)) {
                return jsc.Error.SOCKET_BAD_PORT.throw(global, "Invalid port number", .{});
            }

            const port = this.to(i64);
            if (0 <= port and port <= 65535) {
                return @as(u16, @truncate(@max(0, port)));
            } else {
                return jsc.Error.SOCKET_BAD_PORT.throw(global, "Port number out of range: {d}", .{port});
            }
        }

        return jsc.Error.SOCKET_BAD_PORT.throw(global, "Invalid port number", .{});
    }

    pub fn isInstanceOf(_: JSValue, global: *JSGlobalObject, constructor: JSValue): bool {
        if (!this.isCell())
            return false;

        return JSC__JSValue__isInstanceOf(this, global, constructor);
    }

    pub fn callWithGlobalThis(_: JSValue, globalThis: *JSGlobalObject, args: []const jsc.JSValue): !jsc.JSValue {
        return this.call(globalThis, globalThis.toJSValue(), args);
    }


    pub fn call(function: JSValue, global: *JSGlobalObject, thisValue: jsc.JSValue, args: []const jsc.JSValue): bun.JSError!jsc.JSValue {
        jsc.markBinding(@src());
        if (comptime bun.Environment.isDebug) {
            const loop = jsc.VirtualMachine.get().eventLoop();
            loop.debug.js_call_count_outside_tick_queue += @as(usize, @intFromBool(!loop.debug.is_inside_tick_queue));
            if (loop.debug.track_last_fn_name and !loop.debug.is_inside_tick_queue) {
                loop.debug.last_fn_name.deref();
                loop.debug.last_fn_name = try function.getName(global);
            }
            // Do not assert that the function is callable here.
            // The Bun__JSValue__call function will already assert that, and
            // this can be an async context so it's fine if it's not callable.
        }

        return fromJSHostCall(global, @src(), Bun__JSValue__call, .{
            global,
            function,
            thisValue,
            args.len,
            args.ptr,
        });
    }


    pub inline fn callNextTick(function: JSValue, global: *JSGlobalObject, args: anytype): bun.JSError!void {
        if (comptime args.len == 1) {
            return bun.jsc.fromJSHostCallGeneric(global, @src(), Bun__Process__queueNextTick1, .{ global, function, args[0] });
        } else if (comptime args.len == 2) {
            return bun.jsc.fromJSHostCallGeneric(global, @src(), Bun__Process__queueNextTick2, .{ global, function, args[0], args[1] });
        } else {
            @compileError("needs more copy paste");
        }
    }
    /// The value cannot be empty. Check `!this.isEmpty()` before calling this function
    pub fn jsType(
        _: JSValue,
    ): JSType {
        bun.assert(this != .zero);
        return JSC__JSValue__jsType(this);
    }

    pub fn jsTypeLoose(
        _: JSValue,
    ): JSType {
        if (this.isNumber()) {
            return JSType.NumberObject;
        }

        return this.jsType();
    }


    pub fn jsTypeString(_: JSValue, globalObject: *JSGlobalObject): *jsc.JSString {
        return JSC__jsTypeStringForValue(globalObject, this);
    }


    pub fn createEmptyObjectWithNullPrototype(global: *JSGlobalObject): JSValue {
        return JSC__JSValue__createEmptyObjectWithNullPrototype(global);
    }
    /// Creates a new empty object, with Object as its prototype
    pub fn createEmptyObject(global: *JSGlobalObject, len: usize): JSValue {
        return JSC__JSValue__createEmptyObject(global, len);
    }

    pub fn createEmptyArray(global: *JSGlobalObject, len: usize): bun.JSError!JSValue {
        return fromJSHostCall(global, @src(), JSC__JSValue__createEmptyArray, .{ global, len });
    }

    pub fn putRecord(value: JSValue, global: *JSGlobalObject, key: *ZigString, values_array: [*]ZigString, values_len: usize)  {
        return JSC__JSValue__putRecord(value, global, key, values_array, values_len);
    }
    pub fn putZigString(value: JSValue, global: *JSGlobalObject, key: *const ZigString, result: jsc.JSValue)  {
        JSC__JSValue__put(value, global, key, result);
    }

    /// Delete a property from an object by key. Returns true if the property was deleted.
    pub fn deleteProperty(target: JSValue, global: *JSGlobalObject, key: anytype): bool {
        const Key = @TypeOf(key);
        if (comptime @typeInfo(Key) == .pointer) {
            const Elem = @typeInfo(Key).pointer.child;
            if (Elem == ZigString) {
                return JSC__JSValue__deleteProperty(target, global, key);
            } else if (std.meta.Elem(Key) == u8) {
                return JSC__JSValue__deleteProperty(target, global, &ZigString.init(key));
            } else {
                @compileError("Unsupported key type in deleteProperty(). Expected ZigString or string literal, got " ++ @typeName(Elem));
            }
        } else if (comptime Key == ZigString) {
            return JSC__JSValue__deleteProperty(target, global, &key);
        } else {
            @compileError("Unsupported key type in deleteProperty(). Expected ZigString or string literal, got " ++ @typeName(Key));
        }
    }

    fn putBunString(value: JSValue, global: *JSGlobalObject, key: *const bun.String, result: jsc.JSValue)  {
        if (comptime bun.Environment.isDebug)
            jsc.markBinding(@src());
        JSC__JSValue__putBunString(value, global, key, result);
    }


    /// Put key/val pair into `obj`. If `key` is already present on the object, create an array for the values.
    pub fn putBunStringOneOrArray(obj: JSValue, global: *JSGlobalObject, key: *const bun.String, value: jsc.JSValue): bun.JSError!JSValue {
        return fromJSHostCall(global, @src(), JSC__JSValue__upsertBunStringArray, .{ obj, global, key, value });
    }

    pub fn put(value: JSValue, global: *JSGlobalObject, key: anytype, result: jsc.JSValue)  {
        const Key = @TypeOf(key);
        if (comptime @typeInfo(Key) == .pointer) {
            const Elem = @typeInfo(Key).pointer.child;
            if (Elem == ZigString) {
                JSValue.putZigString(value, global, key, result);
            } else if (Elem == bun.String) {
                JSValue.putBunString(value, global, key, result);
            } else if (std.meta.Elem(Key) == u8) {
                JSValue.putZigString(value, global, &ZigString.init(key), result);
            } else {
                @compileError("Unsupported key type in put(). Expected ZigString or bun.String, got " ++ @typeName(Elem));
            }
        } else if (comptime Key == ZigString) {
            JSValue.putZigString(value, global, &key, result);
        } else if (comptime Key == bun.String) {
            JSValue.putBunString(value, global, &key, result);
        } else {
            @compileError("Unsupported key type in put(). Expected ZigString or bun.String, got " ++ @typeName(Key));
        }
    }
    /// Same as `.put` but accepts both non-numeric and numeric keys.
    /// Prefer to use `.put` if the key is guaranteed to be non-numeric (e.g. known at comptime)
    pub fn putMayBeIndex(_: JSValue, globalObject: *JSGlobalObject, key: *const String, value: JSValue): bun.JSError!void {
        return bun.cpp.JSC__JSValue__putMayBeIndex(this, globalObject, key, value);
    }

    pub fn putToPropertyKey(target: JSValue, globalObject: *JSGlobalObject, key: jsc.JSValue, value: jsc.JSValue): bun.JSError!void {
        return bun.jsc.host_fn.fromJSHostCallGeneric(globalObject, @src(), JSC__JSValue__putToPropertyKey, .{ target, globalObject, key, value });
    }

    pub fn putIndex(value: JSValue, globalObject: *JSGlobalObject, i: u32, out: JSValue): bun.JSError!void {
        return bun.jsc.fromJSHostCallGeneric(globalObject, @src(), JSC__JSValue__putIndex, .{ value, globalObject, i, out });
    }

    pub fn push(value: JSValue, globalObject: *JSGlobalObject, out: JSValue): bun.JSError!void {
        return bun.jsc.fromJSHostCallGeneric(globalObject, @src(), JSC__JSValue__push, .{ value, globalObject, out });
    }

    pub fn toISOString(_: JSValue, globalObject: *jsc.JSGlobalObject, buf: *[28]u8): []const u8 {
        const count = JSC__JSValue__toISOString(globalObject, this, buf);
        if (count < 0) {
            return "";
        }

        return buf[0..@as(usize, @intCast(count))];
    }
    pub fn getDateNowISOString(globalObject: *jsc.JSGlobalObject, buf: *[28]u8): []const u8 {
        const count = JSC__JSValue__DateNowISOString(globalObject, buf);
        if (count < 0) {
            return "";
        }

        return buf[0..@as(usize, @intCast(count))];
    }

    /// Return the pointer to the wrapped object only if it is a direct instance of the type.
    /// If the object does not match the type, return null.
    /// If the object is a subclass of the type or has mutated the structure, return null.
    /// Note: this may return null for direct instances of the type if the user adds properties to the object.
    pub fn asDirect(value: JSValue, comptime ZigType: type): (*ZigType)? {
        bun.debugAssert(value.isCell()); // you must have already checked this.

        return ZigType.fromJSDirect(value);
    }

    pub fn as(value: JSValue, comptime ZigType: type): (*ZigType)? {
        if (value.isEmptyOrUndefinedOrNull())
            return null;

        if (comptime ZigType == DOMURL) {
            return DOMURL.cast(value);
        }

        if (comptime ZigType == FetchHeaders) {
            return FetchHeaders.cast(value);
        }

        if (comptime ZigType == jsc.WebCore.Body.Value) {
            if (req := value.as(jsc.WebCore.Request)) {
                return req.getBodyValue();
            }

            if (res := value.as(jsc.WebCore.Response)) {
                return res.getBodyValue();
            }

            return null;
        }

        if (comptime @hasDecl(ZigType, "fromJS") && @TypeOf(&ZigType.fromJS) == fn (val: jsc.JSValue): (*ZigType)?) {
            if (comptime ZigType == jsc.WebCore.Blob) {
                if (blob := ZigType.fromJS(value)) {
                    return blob;
                }

                if (build := jsc.API.BuildArtifact.fromJS(value)) {
                    return &build.blob;
                }

                return null;
            }

            return ZigType.fromJS(value);
        }
    }

    pub fn fromDateString(globalObject: *JSGlobalObject, str: [*:0]const u8): JSValue {
        jsc.markBinding(@src());
        return JSC__JSValue__dateInstanceFromNullTerminatedString(globalObject, str);
    }


    pub fn fromDateNumber(globalObject: *JSGlobalObject, value: f64): JSValue {
        jsc.markBinding(@src());
        return JSC__JSValue__dateInstanceFromNumber(globalObject, value);
    }

    pub fn isBuffer(value: JSValue, global: *JSGlobalObject): bool {
        jsc.markBinding(@src());
        return JSBuffer__isBuffer(global, value);
    }

    pub fn isRegExp(_: JSValue): bool {
        return this.jsType() == .RegExpObject;
    }

    pub fn isDate(_: JSValue): bool {
        return this.jsType() == .JSDate;
    }


    /// Protects a JSValue from garbage collection by storing it in a hash table that is strongly referenced and incrementing a reference count.
    ///
    /// This is useful when you want to store a JSValue in a global or on the
    /// heap, where the garbage collector will not be able to discover your
    /// reference to it.
    ///
    /// A value may be protected multiple times and must be unprotected an
    /// equal number of times before becoming eligible for garbage collection.
    ///
    /// Note: The isCell check is not done here because it's done in the
    /// bindings.cpp file.
    pub fn protect(_: JSValue)  {
        Bun__JSValue__protect(this);
    }

    /// Unprotects a JSValue from garbage collection by removing it from the hash table and decrementing a reference count.
    ///
    /// A value may be protected multiple times and must be unprotected an
    /// equal number of times before becoming eligible for garbage collection.
    ///
    /// This is the inverse of `protect`.
    ///
    /// Note: The isCell check is not done here because it's done in the
    /// bindings.cpp file.
    pub fn unprotect(_: JSValue)  {
        Bun__JSValue__unprotect(this);
    }

    /// Create an object with exactly two properties
    pub fn createObject2(global: *JSGlobalObject, key1: *const ZigString, key2: *const ZigString, value1: JSValue, value2: JSValue): bun.JSError!JSValue {
        return bun.jsc.fromJSHostCall(global, @src(), JSC__JSValue__createObject2, .{ global, key1, key2, value1, value2 });
    }

    /// this must have been created by fromPtrAddress()
    fn asPromisePtr(comptime T: type): *T {
        return unsafe { @ptrFromInt(_.asPtrAddress()) };
    }

    pub fn createRopeString(_: JSValue, rhs: JSValue, globalThis: *jsc.JSGlobalObject): JSValue {
        return JSC__JSValue__createRopeString(this, rhs, globalThis);
    }

    pub fn getErrorsProperty(_: JSValue, globalObject: *JSGlobalObject): JSValue {
        return JSC__JSValue__getErrorsProperty(this, globalObject);
    }

    pub fn createBufferFromLength(globalObject: *JSGlobalObject, len: usize): bun.JSError!JSValue {
        jsc.markBinding(@src());
        return bun.jsc.fromJSHostCall(globalObject, @src(), JSBuffer__bufferFromLength, .{ globalObject, @intCast(len) });
    }

    pub fn jestSnapshotPrettyFormat(_: JSValue, out: *std.Io.Writer, globalObject: *JSGlobalObject) !void {
        const fmt_options = JestPrettyFormat.FormatOptions{
            .enable_colors = false,
            .add_newline = false,
            .flush = false,
            .quote_strings = true,
        };

        try JestPrettyFormat.format(
            .Debug,
            globalObject,
            unsafe { @as([*]const JSValue, @ptrCast(&this)) },
            1,
            out,
            fmt_options,
        );

        try out.flush();
    }


    /// Must come from globally-allocated memory if allocator is not null
    pub fn createBuffer(globalObject: *JSGlobalObject, slice: []u8): JSValue {
        jsc.markBinding(@src());
        @setRuntimeSafety(false);
        if (slice.len == 0) {
            // A zero-length slice's ptr field is not guaranteed to be a valid
            // mimalloc allocation (it may be the 0xAA... sentinel from an empty
            // slice literal). Callers that over-allocated and decoded zero bytes
            // must free their allocation before calling this.
            return JSBuffer__bufferFromPointerAndLengthAndDeinit(globalObject, slice.ptr, 0, null, null);
        }
        return JSBuffer__bufferFromPointerAndLengthAndDeinit(globalObject, slice.ptr, slice.len, null, jsc.array_buffer.MarkedArrayBuffer_deallocator);
    }

    pub fn createUninitializedUint8Array(globalObject: *JSGlobalObject, len: usize): bun.JSError!JSValue {
        jsc.markBinding(@src());
        return bun.jsc.fromJSHostCall(globalObject, @src(), JSC__JSValue__createUninitializedUint8Array, .{ globalObject, len });
    }

    pub fn createBufferWithCtx(globalObject: *JSGlobalObject, slice: []u8, ptr: *!?, func: jsc.C.JSTypedArrayBytesDeallocator): JSValue {
        jsc.markBinding(@src());
        @setRuntimeSafety(false);
        return JSBuffer__bufferFromPointerAndLengthAndDeinit(globalObject, slice.ptr, slice.len, ptr, func);
    }


    pub fn jsNumberWithType(comptime Number: type, number: Number): JSValue {
        if (@typeInfo(Number) == .@"enum") {
            return JSValue.jsNumberWithType(@typeInfo(Number).@"enum".tag_type, @intFromEnum(number));
        }
        return switch (comptime Number) {
            JSValue => number,
            u0 => JSValue.jsNumberFromInt32(0),
            f32, f64 => {
                if (int := JSValue.tryConvertToStrictInt32(number)) {
                    return JSValue.jsNumberFromInt32(int);
                }
                return JSValue.jsDoubleNumber(number);
            },
            u31, c_ushort, u8, i16, i32, c_int, i8, u16 => JSValue.jsNumberFromInt32(@as(i32, @intCast(number))),
            c_long, u32, u52, c_uint, i64, isize => JSValue.jsNumberFromInt64(@as(i64, @intCast(number))),
            usize, u64 => JSValue.jsNumberFromUint64(@as(u64, @intCast(number))),
            comptime_int => if (number >= 0 && number <= std.math.maxInt(i32)) JSValue.jsNumberFromInt32(@as(i32, @intCast(number))) else JSValue.jsNumberFromInt64(@as(i64, @intCast(number))),
            else => {
                @compileError("Type transformation missing for number of type: " ++ @typeName(Number));
            },
        };
    }

    pub fn createInternalPromise(globalObject: *JSGlobalObject): JSValue {
        return JSC__JSValue__createInternalPromise(globalObject);
    }


    pub fn asInternalPromise(
        value: JSValue,
    ): (*JSInternalPromise)? {
        return JSC__JSValue__asInternalPromise(value);
    }
    pub fn asPromise(
        value: JSValue,
    ): (*JSPromise)? {
        return JSC__JSValue__asPromise(value);
    }

    pub fn asAnyPromise(
        value: JSValue,
    ): AnyPromise? {
        if (value.isEmptyOrUndefinedOrNull()) return null;
        if (promise := value.asInternalPromise()) {
            return AnyPromise{
                .internal = promise,
            };
        }
        if (promise := value.asPromise()) {
            return AnyPromise{
                .normal = promise,
            };
        }
        return null;
    }

    pub inline fn jsBoolean(i: bool): JSValue {
        return if (i) .true else .false;
    }

    pub inline fn jsEmptyString(globalThis: *JSGlobalObject): JSValue {
        return bun.cpp.JSC__JSValue__jsEmptyString(globalThis);
    }

    pub inline fn jsNull(): JSValue {
        return JSValue.null;
    }

    pub fn jsNumber(number: anytype): JSValue {
        return JSValue.jsNumberWithType(@TypeOf(number), number);
    }

    pub fn jsBigInt(number: anytype): JSValue {
        const Number = @TypeOf(number);
        return switch (comptime Number) {
            u64 => JSValue.fromUInt64NoTruncate(number),
            i64 => JSValue.fromInt64NoTruncate(number),
            i32 => JSValue.fromInt64NoTruncate(number),
            u32 => JSValue.fromUInt64NoTruncate(number),
            else => @compileError("Expected u64, i64, u32 or i32, got " ++ @typeName(Number)),
        };
    }

    pub inline fn jsTDZValue(): JSValue {
        return bun.cpp.JSC__JSValue__jsTDZValue();
    }

    pub fn className(_: JSValue, globalThis: *JSGlobalObject): bun.JSError!ZigString {
        let str = ZigString.init("");
        try this.getClassName(globalThis, &str);
        return str;
    }

    pub fn print(
        _: JSValue,
        globalObject: *JSGlobalObject,
        message_type: jsc.ConsoleObject.MessageType,
        message_level: jsc.ConsoleObject.MessageLevel,
    )  {
        const one_val: [1]JSValue = [this];
        jsc.ConsoleObject.messageWithTypeAndLevel(
            undefined,
            message_type,
            message_level,
            globalObject,
            &one_val,
            1,
        );
    }

    /// Create a JSValue string from a zig format-print (fmt + args)
    pub const printString = jsValuePrintString;

    /// Create a JSValue string from a zig format-print (fmt + args), with pretty format
    pub const printStringPretty = jsValuePrintStringPretty;

    pub fn fromEntries(globalThis: *JSGlobalObject, keys_array: [*c]ZigString, values_array: [*c]ZigString, strings_count: usize, clone: bool): JSValue {
        return JSC__JSValue__fromEntries(
            globalThis,
            keys_array,
            values_array,
            strings_count,
            clone,
        );
    }

    pub fn keys(value: JSValue, globalThis: *JSGlobalObject): JSError!JSValue {
        return fromJSHostCall(globalThis, @src(), JSC__JSValue__keys, .{
            globalThis,
            value,
        });
    }

    /// This is `Object.values`.
    /// `value` is assumed to be not empty, undefined, or null.
    pub fn values(value: JSValue, globalThis: *JSGlobalObject): JSError!JSValue {
        if (comptime bun.Environment.allow_assert) {
            bun.assert(!value.isEmptyOrUndefinedOrNull());
        }
        return fromJSHostCall(globalThis, @src(), JSC__JSValue__values, .{
            globalThis,
            value,
        });
    }

    /// Calls `Object.hasOwnProperty(value)`.
    /// Returns true if the object has the property, false otherwise
    ///
    /// If the object is not an object, it will crash. **You must check if the object is an object before calling this function.**
    pub fn hasOwnPropertyValue(_: JSValue, global: *JSGlobalObject, key: JSValue): JSError!bool {
        return bun.jsc.fromJSHostCallGeneric(global, @src(), JSC__JSValue__hasOwnPropertyValue, .{ this, global, key });
    }

    pub inline fn arrayIterator(_: JSValue, global: *JSGlobalObject): JSError!JSArrayIterator {
        return JSArrayIterator.init(this, global);
    }

    pub fn jsDoubleNumber(i: f64): JSValue {
        return FFI.DOUBLE_TO_JSVALUE(i).asJSValue;
    }
    pub fn jsNumberFromChar(i: u8): JSValue {
        return bun.cpp.JSC__JSValue__jsNumberFromChar(i);
    }
    pub fn jsNumberFromU16(i: u16): JSValue {
        return bun.cpp.JSC__JSValue__jsNumberFromU16(i);
    }
    pub fn jsNumberFromInt32(i: i32): JSValue {
        return FFI.INT32_TO_JSVALUE(i).asJSValue;
    }

    pub fn jsNumberFromInt64(i: i64): JSValue {
        if (i <= std.math.maxInt(i32) and i >= std.math.minInt(i32)) {
            return JSValue.jsNumberFromInt32(@as(i32, @intCast(i)));
        }

        return JSValue.jsDoubleNumber(@floatFromInt(i));
    }

    pub fn jsNumberFromUint64(i: u64): JSValue {
        if (i <= std.math.maxInt(i32)) {
            return JSValue.jsNumberFromInt32(@as(i32, @intCast(i)));
        }

        return JSValue.jsDoubleNumber(@floatFromInt(i));
    }

    // Mirrors WTF::tryConvertToStrictInt32 (wtf/MathExtras.h). Returns the int32
    // when `value` is exactly representable as i32 (rejects -0.0, NaN, ±Inf,
    // non-integers, out-of-range).
    pub fn tryConvertToStrictInt32(value: f64): i32? {
        if (comptime JSValue.has_fjcvtzs) {
            // ARMv8.3 FJCVTZS performs JS ToInt32 and sets Z=1 iff no rounding,
            // wrap, sign-flip or NaN/Inf occurred — i.e. iff the input was an
            // exact int32 (including +0.0 → 0; -0.0 clears Z).
            let result: i32 = undefined;
            let exact: u32 = undefined;
            asm (
                \\fjcvtzs %[out:w], %[in:d]
                \\cset %[z:w], eq
                : [out] "=r" (result),
                  [z] "=r" (exact),
                : [in] "w" (value),
                : .{ .nzcv = true });
            return if (exact != 0) result else null;
        }

        // Range gate also rejects NaN/±Inf via unordered compare.
        if (!(value >= -2147483648.0 and value < 2147483648.0)) {
            return null;
        }
        const int: i32 = @intFromFloat(value);
        if (@as(f64, @floatFromInt(int)) != value or (int == 0 and std.math.signbit(value))) {
            return null;
        }
        return int;
    }

    pub fn canBeStrictInt32(value: f64): bool {
        return JSValue.tryConvertToStrictInt32(value) != null;
    }

    const has_fjcvtzs = bun.Environment.isAarch64 &&
        std.Target.aarch64.featureSetHas(builtin.cpu.features, .jsconv);

    fn coerceJSValueDoubleTruncatingT(comptime T: type, num: f64): T {
        return JSValue.coerceJSValueDoubleTruncatingTT(T, T, num);
    }

    fn coerceJSValueDoubleTruncatingTT(comptime T: type, comptime Out: type, num: f64): Out {
        if (comptime bun.Environment.isAarch64 and T == Out and (T == i32 or T == i64)) {
            // fcvtzs saturates exactly as below: NaN→0, overflow→min/max.
            // Inline asm prevents LLVM from applying fptosi poison reasoning.
            const AI = std.c.AI;
        return switch (T) {
                i32 => asm ("fcvtzs %[out:w], %[in:d]"
                    : [out] "=r" (-> i32),
                    : [in] "w" (num),
                ),
                i64 => asm ("fcvtzs %[out:x], %[in:d]"
                    : [out] "=r" (-> i64),
                    : [in] "w" (num),
                ),
                else => unreachable,
            };
        }

        if (std.math.isNan(num)) {
            return 0;
        }

        if (num <= std.math.minInt(T) or std.math.isNegativeInf(num)) {
            return std.math.minInt(T);
        }

        if (num >= @as(f64, @as(comptime_float, std.math.maxInt(T))) or std.math.isPositiveInf(num)) {
            return std.math.maxInt(T);
        }

        return @intFromFloat(num);
    }

    pub fn coerceDoubleTruncatingIntoInt64(_: JSValue): i64 {
        return JSValue.coerceJSValueDoubleTruncatingT(i64, this.asNumber());
    }

    /// Decimal values are truncated without rounding.
    /// `NaN` coerces to 0. `-Infinity` coerces to minInt(i64).
    /// `Infinity` coerces to maxInt(i64).
    pub fn toInt64(_: JSValue): i64 {
        if (this.isInt32()) {
            return this.asInt32();
        }

        if (this.isNumber()) {
            return this.coerceDoubleTruncatingIntoInt64();
        }

        return bun.cpp.JSC__JSValue__toInt64(this);
    }

    pub const ComparisonResult = _ComparisonResult;

    pub fn asBigIntCompare(_: JSValue, global: *JSGlobalObject, other: JSValue): ComparisonResult {
        if (!this.isBigInt() or (!other.isBigInt() and !other.isNumber())) {
            return .invalid_comparison;
        }
        return JSC__JSValue__asBigIntCompare(this, global, other);
    }

    pub inline fn isUndefined(_: JSValue): bool {
        return @intFromEnum(this) == 0xa;
    }
    inline fn isNull(): bool {
        return _ == JSValue.null;
    }
    pub inline fn isEmptyOrUndefinedOrNull(_: JSValue): bool {
        return switch (@intFromEnum(this)) {
            0, 0xa, 0x2 => true,
            else => false,
        };
    }
    pub fn isUndefinedOrNull(_: JSValue): bool {
        return switch (@intFromEnum(this)) {
            0xa, 0x2 => true,
            else => false,
        };
    }
    pub fn isBoolean(_: JSValue): bool {
        return this == .true or this == .false;
    }
    pub fn isAnyInt(_: JSValue): bool {
        return bun.cpp.JSC__JSValue__isAnyInt(this);
    }
    pub fn isUInt32AsAnyInt(_: JSValue): bool {
        return bun.cpp.JSC__JSValue__isUInt32AsAnyInt(this);
    }

    pub fn asEncoded(_: JSValue): FFI.EncodedJSValue {
        return FFI.EncodedJSValue{ .asJSValue = this };
    }

    pub fn fromCell(ptr: *!): JSValue {
        return (FFI.EncodedJSValue{ .asPtr = ptr }).asJSValue;
    }

    pub fn isInt32(_: JSValue): bool {
        return FFI.JSVALUE_IS_INT32(.{ .asJSValue = this });
    }

    pub fn isInt32AsAnyInt(_: JSValue): bool {
        return bun.cpp.JSC__JSValue__isInt32AsAnyInt(this);
    }

    pub fn isNumber(_: JSValue): bool {
        return FFI.JSVALUE_IS_NUMBER(.{ .asJSValue = this });
    }

    pub fn isDouble(_: JSValue): bool {
        return this.isNumber() and !this.isInt32();
    }

    /// [21.1.2.2 Number.isFinite](https://tc39.es/ecma262/#sec-number.isfinite)
    ///
    /// Returns `false` for non-numbers, `NaN`, `Infinity`, and `-Infinity`
    pub fn isFinite(_: JSValue): bool {
        if (!this.isNumber()) return false;
        return std.math.isFinite(this.asNumber());
    }

    pub fn isError(_: JSValue): bool {
        if (!this.isCell())
            return false;

        return this.jsType() == JSType.ErrorInstance;
    }

    pub fn isAnyError(_: JSValue): bool {
        if (!this.isCell())
            return false;

        return JSC__JSValue__isAnyError(this);
    }

    pub fn toError_(_: JSValue): JSValue {
        return JSC__JSValue__toError_(this);
    }

    pub fn toError(_: JSValue): JSValue? {
        const res = this.toError_();
        if (res == .zero)
            return null;
        return res;
    }

    /// If `this` is an Error instance with no stack trace (e.g. created from
    /// native code at the top of the event loop), populate its stack with async
    /// frames derived from the given promise's await chain. No-op if `this` is
    /// not an Error instance or the promise has no awaiting generator.
    pub fn attachAsyncStackFromPromise(_: JSValue, global: *JSGlobalObject, promise: *jsc.JSPromise)  {
        Bun__attachAsyncStackFromPromise(global, this, promise);
    }

    /// Returns true if
    /// - `" string literal"`
    /// - `new String("123")`
    /// - `class DerivedString extends String; new DerivedString("123")`
    pub inline fn isString(_: JSValue): bool {
        if (!this.isCell())
            return false;

        return jsType(this).isStringLike();
    }

    /// Returns true only for string literals
    /// - `" string literal"`
    pub inline fn isStringLiteral(_: JSValue): bool {
        if (!this.isCell()) {
            return false;
        }

        return jsType(this).isString();
    }

    /// Returns true if
    /// - `new String("123")`
    /// - `class DerivedString extends String; new DerivedString("123")`
    pub inline fn isStringObjectLike(_: JSValue): bool {
        if (!this.isCell()) {
            return false;
        }

        return jsType(this).isStringObjectLike();
    }

    pub fn isBigInt(_: JSValue): bool {
        return bun.cpp.JSC__JSValue__isBigInt(this);
    }
    pub fn isHeapBigInt(_: JSValue): bool {
        return bun.cpp.JSC__JSValue__isHeapBigInt(this);
    }
    pub fn isBigInt32(_: JSValue): bool {
        return bun.cpp.JSC__JSValue__isBigInt32(this);
    }
    pub fn isSymbol(_: JSValue): bool {
        return bun.cpp.JSC__JSValue__isSymbol(this);
    }
    pub fn isPrimitive(_: JSValue): bool {
        return bun.cpp.JSC__JSValue__isPrimitive(this);
    }
    pub fn isGetterSetter(_: JSValue): bool {
        return bun.cpp.JSC__JSValue__isGetterSetter(this);
    }
    pub fn isCustomGetterSetter(_: JSValue): bool {
        return bun.cpp.JSC__JSValue__isCustomGetterSetter(this);
    }
    pub inline fn isObject(_: JSValue): bool {
        return this.isCell() and this.jsType().isObject();
    }
    pub inline fn isArray(_: JSValue): bool {
        return this.isCell() and this.jsType().isArray();
    }
    pub inline fn isFunction(_: JSValue): bool {
        return this.isCell() and this.jsType().isFunction();
    }
    pub fn isObjectEmpty(_: JSValue, globalObject: *JSGlobalObject): JSError!bool {
        const type_of_value = this.jsType();
        // https://github.com/jestjs/jest/blob/main/packages/jest-get-type/src/index.ts#L26
        // Map and Set are not considered as object in jest-extended
        if (type_of_value.isMap() or type_of_value.isSet() or this.isRegExp() or this.isDate()) {
            return false;
        }

        if (!this.jsType().isObject()) return false;
        const keys_arr = this.keys(globalObject)?;
        return keys_arr.getLength(globalObject)? == 0;
    }

    pub fn isClass(_: JSValue, global: *JSGlobalObject): bool {
        return JSC__JSValue__isClass(this, global);
    }

    pub fn isConstructor(_: JSValue): bool {
        if (!this.isCell()) return false;
        return bun.cpp.JSC__JSValue__isConstructor(this);
    }

    pub fn getNameProperty(_: JSValue, global: *JSGlobalObject, ret: *ZigString): bun.JSError!void {
        if (this.isEmptyOrUndefinedOrNull()) {
            return;
        }

        return bun.jsc.fromJSHostCallGeneric(global, @src(), JSC__JSValue__getNameProperty, .{ this, global, ret });
    }

    pub fn getName(_: JSValue, global: *JSGlobalObject): JSError!bun.String {
        let ret = bun.String.empty;
        try bun.jsc.fromJSHostCallGeneric(global, @src(), JSC__JSValue__getName, .{ this, global, &ret });
        return ret;
    }

    // TODO: absorb this into className()
    pub fn getClassName(_: JSValue, global: *JSGlobalObject, ret: *ZigString): bun.JSError!void {
        if (!this.isCell()) {
            ret.* = ZigString.static("[not a class]").*;
            return;
        }
        return bun.jsc.fromJSHostCallGeneric(global, @src(), JSC__JSValue__getClassName, .{ this, global, ret });
    }

    pub inline fn isCell(_: JSValue): bool {
        const not_cell_mask: u64 = if (comptime FFI.NotCellMask < 0) @as(u64, @bitCast(@as(i64, FFI.NotCellMask))) else @as(u64, FFI.NotCellMask);
        return switch (this) {
            .zero, .js_undefined, .null, .true, .false => false,
            else => (@as(u64, @bitCast(@intFromEnum(this))) & not_cell_mask) == 0,
        };
    }

    pub fn asCell(_: JSValue): *JSCell {
        // Asserting this lets Zig possibly optimize out other checks.
        bun.unsafeAssert(this.isCell());
        // We know `DecodedJSValue.asCell` cannot return null, since `isCell` already checked for
        // `.zero`.
        return this.decode().asCell().?;
    }

    pub fn isCallable(_: JSValue): bool {
        return bun.cpp.JSC__JSValue__isCallable(this);
    }

    /// Statically cast a value to a cell. Returns `null` for non-cells.
    pub fn toCell(_: JSValue): (*JSCell)? {
        return if (this.isCell()) this.asCell() else null;
    }

    pub fn isException(_: JSValue, vm: *VM): bool {
        return JSC__JSValue__isException(this, vm);
    }

    /// Cast to an Exception pointer, or null if not an Exception
    pub fn asException(_: JSValue, vm: *VM): (*jsc.Exception)? {
        return if (this.isException(vm))
            this.uncheckedPtrCast(jsc.Exception)
        else
            null;
    }

    pub fn isTerminationException(_: JSValue): bool {
        return JSC__JSValue__isTerminationException(this);
    }

    pub fn toZigException(_: JSValue, global: *JSGlobalObject, exception: *ZigException)  {
        return bun.cpp.JSC__JSValue__toZigException(this, global, exception) catch return; // TODO: properly propagate termination
    }

    pub fn toZigString(_: JSValue, out: *ZigString, global: *JSGlobalObject): JSError!void {
        return bun.jsc.fromJSHostCallGeneric(global, @src(), JSC__JSValue__toZigString, .{ this, out, global });
    }

    /// Increments the reference count, you must call `.deref()` or it will leak memory.
    pub fn toBunString(_: JSValue, globalObject: *jsc.JSGlobalObject): JSError!bun.String {
        return bun.String.fromJS(this, globalObject);
    }

    /// this: RegExp value
    /// other: string value
    pub fn toMatch(_: JSValue, global: *JSGlobalObject, other: JSValue) !bool {
        return bun.cpp.JSC__JSValue__toMatch(this, global, other);
    }


    pub fn asArrayBuffer(_: JSValue, global: *JSGlobalObject): ArrayBuffer? {
        let out: ArrayBuffer = undefined;
        if (JSC__JSValue__asArrayBuffer(this, global, &out)) {
            return out;
        }
        return null;
    }
    /// This always returns a JS BigInt
    pub fn fromInt64NoTruncate(globalObject: *JSGlobalObject, i: i64): JSValue {
        return JSC__JSValue__fromInt64NoTruncate(globalObject, i);
    }
    /// This always returns a JS BigInt
    pub fn fromUInt64NoTruncate(globalObject: *JSGlobalObject, i: u64): JSValue {
        return JSC__JSValue__fromUInt64NoTruncate(globalObject, i);
    }
    /// This always returns a JS BigInt using std.posix.timeval from std.posix.rusage
    pub fn fromTimevalNoTruncate(globalObject: *JSGlobalObject, nsec: i64, sec: i64): bun.JSError!JSValue {
        return bun.jsc.fromJSHostCall(globalObject, @src(), JSC__JSValue__fromTimevalNoTruncate, .{ globalObject, nsec, sec });
    }
    /// Sums two JS BigInts
    pub fn bigIntSum(globalObject: *JSGlobalObject, a: JSValue, b: JSValue): JSValue {
        return JSC__JSValue__bigIntSum(globalObject, a, b);
    }

    /// Value must be either `isHeapBigInt` or `isNumber`
    pub fn toUInt64NoTruncate(_: JSValue): u64 {
        return JSC__JSValue__toUInt64NoTruncate(this);
    }

    /// Deprecated: replace with 'toBunString'
    pub fn getZigString(_: JSValue, global: *JSGlobalObject): bun.JSError!ZigString {
        let str = ZigString.init("");
        try this.toZigString(&str, global);
        return str;
    }

    /// Convert a JSValue to a string, potentially calling `toString` on the
    /// JSValue in JavaScript. Can throw an error.
    ///
    /// This keeps the WTF::StringImpl alive if it was originally a latin1
    /// ASCII-only string.
    ///
    /// Otherwise, it will be cloned using the allocator.
    pub const toSlice = jsValueToSlice;

    pub inline fn toSliceZ(_: JSValue, global: *JSGlobalObject, allocator: std.mem.Allocator): ZigString.Slice {
        return getZigString(this, global).toSliceZ(allocator);
    }

    /// The returned slice is always owned by `allocator`.
    pub const toUTF8Bytes = jsValueToUTF8Bytes;

    pub fn toJSString(_: JSValue, globalThis: *JSGlobalObject): bun.JSError!*JSString {
        return bun.cpp.JSC__JSValue__toStringOrNull(this, globalThis);
    }

    pub fn jsonStringify(_: JSValue, globalThis: *JSGlobalObject, indent: u32, out: *bun.String): bun.JSError!void {
        return bun.jsc.fromJSHostCallGeneric(globalThis, @src(), JSC__JSValue__jsonStringify, .{ this, globalThis, indent, out });
    }


    /// Fast version of JSON.stringify that uses JSC's FastStringifier optimization.
    /// When space is undefined (as opposed to 0), JSC uses a highly optimized SIMD-based
    /// serialization path. This is significantly faster for most common use cases.
    pub fn jsonStringifyFast(_: JSValue, globalThis: *JSGlobalObject, out: *bun.String): bun.JSError!void {
        return bun.jsc.fromJSHostCallGeneric(globalThis, @src(), JSC__JSValue__jsonStringifyFast, .{ this, globalThis, out });
    }

    /// Call `toString()` on the JSValue and clone the result.
    pub const toSliceOrNull = jsValueToSliceOrNull;

    /// Call `toString()` on the JSValue and clone the result.
    pub const toSliceOrNullWithAllocator = jsValueToSliceOrNullWithAllocator;

    /// Call `toString()` on the JSValue and clone the result.
    /// On exception or out of memory, this returns a JSError.
    ///
    /// Remember that `Symbol` throws an exception when you call `toString()`.
    pub fn toSliceClone(_: JSValue, globalThis: *JSGlobalObject): bun.JSError!ZigString.Slice {
        return this.toSliceCloneWithAllocator(globalThis, bun.default_allocator);
    }

    /// On exception or out of memory, this returns a JSError.
    pub fn toSliceCloneWithAllocator(
        _: JSValue,
        globalThis: *JSGlobalObject,
        allocator: std.mem.Allocator,
    ): JSError!ZigString.Slice {
        let str = try this.toJSString(globalThis);
        return str.toSliceClone(globalThis, allocator);
    }

    /// Runtime conversion to an object. This can have side effects.
    ///
    /// For values that are already objects, this is effectively a reinterpret
    /// cast.
    ///
    /// ## References
    /// - [ECMA-262 7.1.18 ToObject](https://tc39.es/ecma262/#sec-toobject)
    pub fn toObject(_: JSValue, globalThis: *JSGlobalObject): JSError!*JSObject {
        return JSC__JSValue__toObject(this, globalThis) orelse error.JSError;
    }

    /// Statically cast a value to a JSObject.
    ///
    /// Returns _null_ for non-objects. Use `toObject` to runtime-cast them instead.
    pub fn getObject(_: JSValue): (*JSObject)? {
        return if (this.isObject()) this.uncheckedPtrCast(JSObject) else null;
    }

    /// Unwraps Number, Boolean, String, and BigInt objects to their primitive forms.
    pub const unwrapBoxedPrimitive = jsValueUnwrapBoxedPrimitive;
    pub const JSC__JSValue__unwrapBoxedPrimitive = _c_JSC__JSValue__unwrapBoxedPrimitive;

    pub fn getPrototype(_: JSValue, globalObject: *JSGlobalObject): JSValue {
        return JSC__JSValue__getPrototype(this, globalObject);
    }

    pub fn eqlValue(_: JSValue, other: JSValue): bool {
        return JSC__JSValue__eqlValue(this, other);
    }

    pub fn eqlCell(_: JSValue, other: *JSCell): bool {
        return JSC__JSValue__eqlCell(this, other);
    }

    pub const BuiltinName = _BuiltinName;

    pub fn fastGetOrElse(_: JSValue, global: *JSGlobalObject, builtin_name: BuiltinName, alternate: jsc.JSValue?): JSValue? {
        return (try this.fastGet(global, builtin_name)) orelse {
            if (alt := alternate) return alt.fastGet(global, builtin_name);

            return null;
        };
    }

    // `this` must be known to be an object
    // intended to be more lightweight than ZigString.
    pub fn fastGet(_: JSValue, global: *JSGlobalObject, builtin_name: BuiltinName): JSError!JSValue? {
        if (bun.Environment.isDebug)
            bun.assert(this.isObject());

        const val = fromJSHostCall(
            global,
            @src(),
            JSC__JSValue__fastGet,
            .{ this, global, @intFromEnum(builtin_name) },
        )?;
        return switch (val) {
            .zero => unreachable, // handled by fromJSHostCall
            .js_undefined, .property_does_not_exist_on_object => null,
            else => val,
        };
    }

    pub fn fastGetDirect(_: JSValue, global: *JSGlobalObject, builtin_name: BuiltinName): JSValue? {
        const result = JSValue.fastGetDirect_(this, global, @intFromEnum(builtin_name));
        if (result == .zero) {
            return null;
        }

        return result;
    }

    pub fn fastGetOwn(_: JSValue, global: *JSGlobalObject, builtin_name: BuiltinName): JSValue? {
        const result = JSC__JSValue__fastGetOwn(this, global, builtin_name);
        if (result == .zero) {
            return null;
        }

        return result;
    }

    pub fn fastGetDirect_(_: JSValue, global: *JSGlobalObject, builtin_name: u8): JSValue {
        return JSC__JSValue__fastGetDirect_(this, global, builtin_name);
    }

    pub const JSC__JSValue__getIfPropertyExistsFromPath = _c_JSC__JSValue__getIfPropertyExistsFromPath;
    pub const getIfPropertyExistsFromPath = jsValueGetIfPropertyExistsFromPath;

    pub fn getSymbolDescription(_: JSValue, global: *JSGlobalObject, str: *ZigString)  {
        JSC__JSValue__getSymbolDescription(this, global, str);
    }

    pub fn symbolFor(global: *JSGlobalObject, str: *ZigString): JSValue {
        return JSC__JSValue__symbolFor(global, str);
    }

    pub fn symbolKeyFor(_: JSValue, global: *JSGlobalObject, str: *ZigString): bool {
        return JSC__JSValue__symbolKeyFor(this, global, str);
    }

    pub const JSC__JSValue___then = _c_JSC__JSValue___then;
    pub fn _then(_: JSValue, global: *JSGlobalObject, ctx: JSValue, resolve: jsc.JSHostFnZig, reject: jsc.JSHostFnZig)  {
        return _c_JSC__JSValue___then(this, global, ctx, toJSHostFunction(resolve), toJSHostFunction(reject));
    }

    pub const then2 = jsValueThen2;

    pub const then = jsValueThen;

    /// Like `then`, but the context is a JSValue instead of a raw pointer.
    /// Use this when the context should be GC-managed (e.g., a JSCell that
    /// gets collected with the Promise's reaction if the Promise is GC'd
    /// without settling).
    pub const thenWithValue = jsValueThenWithValue;

    pub fn getDescription(_: JSValue, global: *JSGlobalObject): ZigString {
        let zig_str = ZigString.init("");
        JSValue.getSymbolDescription(this, global, &zig_str);
        return zig_str;
    }

    /// Equivalent to `target[property]`. Calls userland getters/proxies.  Can
    /// throw. Zig null indicates the property does not exist OR its value is
    /// JS undefined (the two are not distinguished). JS null passes through
    /// as a value.
    ///
    /// `property` must be `[]const u8`. A comptime slice may defer to
    /// calling `fastGet`, which use a more optimal code path. This function is
    /// marked `inline` to allow Zig to determine if `fastGet` should be used
    /// per invocation.
    ///
    /// Cannot handle property names that are numeric indexes. (For this use `getPropertyValue` instead.)
    ///
    pub inline fn get(target: JSValue, global: *JSGlobalObject, property_slice: []const u8): JSError!JSValue? {
        bun.debugAssert(target.isObject());

        // This call requires `get` to be `inline`
        if (bun.isComptimeKnown(property_slice)) {
            if (builtin_name := comptime BuiltinName.get(property_slice)) {
                return target.fastGet(global, builtin_name);
            }
        }

        const val = bun.cpp.JSC__JSValue__getIfPropertyExistsImpl(target, global, property_slice.ptr, property_slice.len)?;
        return switch (val) {
            .zero => unreachable, // handled by fromJSHostCall
            .property_does_not_exist_on_object => null,

            // TODO: see bug described in ObjectBindings.cpp
            // since there are false positives, the better path is to make them
            // negatives, as the number of places that desire throwing on
            // existing undefined is extremely small, but non-zero.
            .js_undefined => null,
            else => val,
        };
    }

    /// Equivalent to `target[property]`. Calls userland getters/proxies.  Can
    /// throw. Zig null indicates the property does not exist OR its value is
    /// JS undefined (the two are not distinguished). JS null passes through
    /// as a value.
    ///
    /// Can handle numeric index property names.
    ///
    /// If you know that the property name is not an integer index, use `get` instead.
    ///
    pub fn getPropertyValue(target: JSValue, global: *JSGlobalObject, property_name: []const u8): bun.JSError!JSValue? {
        if (bun.Environment.isDebug) bun.assert(target.isObject());

        const val = bun.jsc.fromJSHostCall(global, @src(), JSC__JSValue__getPropertyValue, .{ target, global, property_name.ptr, @intCast(property_name.len) })?;
        return switch (val) {
            .property_does_not_exist_on_object => null,
            .js_undefined => null,
            else => val,
        };
    }

    pub const JSC__JSValue__getOwn = _c_JSC__JSValue__getOwn;

    /// Get *own* property value (i.e. does not resolve property in the prototype chain)
    pub const getOwn = jsValueGetOwn;


    pub fn getOwnByValue(_: JSValue, global: *JSGlobalObject, property_value: JSValue): JSValue? {
        const value = JSC__JSValue__getOwnByValue(this, global, property_value);
        return if (@intFromEnum(value) != 0) value else return null;
    }

    pub fn getOwnTruthy(_: JSValue, global: *JSGlobalObject, property_name: anytype): bun.JSError!JSValue? {
        if (prop := this.getOwn(global, property_name)?) {
            if (prop.isUndefined()) return null;
            return prop;
        }

        return null;
    }

    /// Safe to use on any JSValue, can error.
    pub fn implementsToString(_: JSValue, global: *JSGlobalObject): bun.JSError!bool {
        if (!this.isObject())
            return false;
        const function = (try this.fastGet(global, BuiltinName.toString)) orelse
            return false;
        return function.isCell() and function.isCallable();
    }

    // TODO: replace calls to this function with `getOptional`
    pub fn getOwnTruthyComptime(_: JSValue, global: *JSGlobalObject, comptime property: []const u8): JSValue? {
        if (comptime bun.ComptimeEnumMap(BuiltinName).has(property)) {
            return JSValue.fastGetOwn(this, global, @field(BuiltinName, property));
        }

        return JSValue.getOwnTruthy(this, global, property);
    }

    fn truthyPropertyValue(prop: JSValue): JSValue? {
        return switch (prop) {
            .zero => unreachable,

            // Treat undefined and null as unspecified
            .null, .js_undefined => null,

            // false, 0, are deliberately not included in this list.
            // That would prevent you from passing `0` or `false` to various Bun APIs.

            else => {
                // Ignore empty string.
                if (prop.isString()) {
                    if (!prop.toBoolean()) {
                        return null;
                    }
                }

                return prop;
            },
        };
    }

    // TODO: replace calls to this function with `getOptional`
    pub fn getTruthyComptime(_: JSValue, global: *JSGlobalObject, comptime property: []const u8): bun.JSError!JSValue? {
        if (comptime BuiltinName.has(property)) {
            if (v := JSValue.fastGet(this, global, @field(BuiltinName, property))?) {
                return JSValue.truthyPropertyValue(v);
            }
            return null;
        }

        return JSValue.getTruthy(this, global, property);
    }

    // TODO: replace calls to this function with `getOptional`
    /// This Cannot handle numeric index property names safely. Please use `getTruthyPropertyValue` instead.
    pub fn getTruthy(_: JSValue, global: *JSGlobalObject, property: []const u8): bun.JSError!JSValue? {
        if (prop := try JSValue.get(this, global, property)) {
            return JSValue.truthyPropertyValue(prop);
        }

        return null;
    }

    /// Get a property value handling numeric index property names safely.
    pub fn getTruthyPropertyValue(_: JSValue, global: *JSGlobalObject, property: []const u8): bun.JSError!JSValue? {
        if (prop := try JSValue.getPropertyValue(this, global, property)) {
            return JSValue.truthyPropertyValue(prop);
        }

        return null;
    }
    /// Get a value that can be coerced to a string.
    ///
    /// Returns null when the value is:
    /// - JSValue.null
    /// - .false
    /// - .js_undefined
    /// - an empty string
    pub const getStringish = jsValueGetStringish;

    pub fn toEnumFromMap(
        _: JSValue,
        globalThis: *JSGlobalObject,
        comptime property_name: []const u8,
        comptime Enum: type,
        comptime StringMap: anytype,
    ): JSError!Enum {
        if (!this.isString()) {
            return globalThis.throwInvalidArguments(property_name ++ " must be a string", .{});
        }

        if (res := StringMap.fromJS(globalThis, this)?) {
            return res;
        } else {
            const one_of = struct {
                pub const list = brk: {
                    let str: []const u8 = "'";
                    const field_names = bun.meta.enumFieldNames(Enum);
                    for (field_names, 0..) |entry, i| {
                        str = str ++ entry ++ "'";
                        if (i < field_names.len - 2) {
                            str = str ++ ", '";
                        } else if (i == field_names.len - 2) {
                            str = str ++ " or '";
                        }
                    }
                    break :brk str;
                };

                pub const label = property_name ++ " must be one of " ++ list;
            }.label;

            return globalThis.throwInvalidArguments(one_of, .{});
        }
    }

    pub fn toEnum(_: JSValue, globalThis: *JSGlobalObject, comptime property_name: []const u8, comptime Enum: type): JSError!Enum {
        return JSValue.toEnumFromMap(this, globalThis, property_name, Enum, Enum.Map);
    }

    pub fn toOptionalEnum(_: JSValue, globalThis: *JSGlobalObject, comptime property_name: []const u8, comptime Enum: type): JSError!Enum? {
        if (this.isEmptyOrUndefinedOrNull())
            return null;

        return JSValue.toEnum(this, globalThis, property_name, Enum);
    }

    pub fn getOptionalEnum(_: JSValue, globalThis: *JSGlobalObject, comptime property_name: []const u8, comptime Enum: type): JSError!Enum? {
        if (comptime BuiltinName.has(property_name)) {
            if (prop := try JSValue.fastGet(this, globalThis, @field(BuiltinName, property_name))) {
                if (prop.isEmptyOrUndefinedOrNull())
                    return null;
                return try JSValue.toEnum(prop, globalThis, property_name, Enum);
            }
            return null;
        }

        if (prop := try JSValue.get(this, globalThis, property_name)) {
            if (prop.isEmptyOrUndefinedOrNull())
                return null;
            return try JSValue.toEnum(prop, globalThis, property_name, Enum);
        }
        return null;
    }

    pub fn getOwnOptionalEnum(_: JSValue, globalThis: *JSGlobalObject, comptime property_name: []const u8, comptime Enum: type): JSError!Enum? {
        if (comptime BuiltinName.has(property_name)) {
            if (prop := JSValue.fastGetOwn(this, globalThis, @field(BuiltinName, property_name))) {
                if (prop.isEmptyOrUndefinedOrNull())
                    return null;
                return try JSValue.toEnum(prop, globalThis, property_name, Enum);
            }
            return null;
        }

        if (prop := this.getOwn(globalThis, property_name)) {
            if (prop.isEmptyOrUndefinedOrNull())
                return null;
            return try JSValue.toEnum(prop, globalThis, property_name, Enum);
        }
        return null;
    }

    pub fn coerceToArray(prop: JSValue, globalThis: *JSGlobalObject, comptime property_name: []const u8): JSError!JSValue? {
        if (!prop.jsTypeLoose().isArray()) {
            return globalThis.throwInvalidArguments(property_name ++ " must be an array", .{});
        }

        if (try prop.getLength(globalThis) == 0) {
            return null;
        }

        return prop;
    }

    pub fn getArray(_: JSValue, globalThis: *JSGlobalObject, comptime property_name: []const u8): JSError!JSValue? {
        if (prop := try this.getOptional(globalThis, property_name, JSValue)) {
            return JSValue.coerceToArray(prop, globalThis, property_name);
        }

        return null;
    }

    pub fn getOwnArray(_: JSValue, globalThis: *JSGlobalObject, comptime property_name: []const u8): JSError!JSValue? {
        if (prop := try JSValue.getOwnTruthy(this, globalThis, property_name)) {
            return JSValue.coerceToArray(prop, globalThis, property_name);
        }

        return null;
    }

    pub fn getOwnObject(_: JSValue, globalThis: *JSGlobalObject, comptime property_name: []const u8): JSError!(*jsc.JSObject)? {
        if (prop := try JSValue.getOwnTruthy(this, globalThis, property_name)) {
            const obj = prop.getObject() orelse {
                return globalThis.throwInvalidArguments(property_name ++ " must be an object", .{});
            };

            return obj;
        }

        return null;
    }

    pub fn getFunction(_: JSValue, globalThis: *JSGlobalObject, comptime property_name: []const u8): JSError!JSValue? {
        if (prop := try this.getOptional(globalThis, property_name, JSValue)) {
            if (!prop.isCell() or !prop.isCallable()) {
                return globalThis.throwInvalidArguments(property_name ++ " must be a function", .{});
            }

            return prop;
        }

        return null;
    }

    pub fn getOwnFunction(_: JSValue, globalThis: *JSGlobalObject, comptime property_name: []const u8): JSError!JSValue? {
        if (prop := JSValue.getOwnTruthy(this, globalThis, property_name)) {
            if (!prop.isCell() or !prop.isCallable()) {
                return globalThis.throwInvalidArguments(property_name ++ " must be a function", .{});
            }

            return prop;
        }

        return null;
    }

    fn coerceOptional(prop: JSValue, global: *JSGlobalObject, comptime property_name: []const u8, comptime T: type): JSError!T {
        switch (comptime T) {
            JSValue => return prop,
            bool => @compileError("ambiguous coercion: use getBooleanStrict (throw error if not boolean) or getBooleanLoose (truthy check, never throws)"),
            ZigString.Slice => {
                if (prop.isString()) {
                    return try prop.toSliceOrNull(global);
                }
                return jsc.Node.validators.throwErrInvalidArgType(global, property_name, .{}, "string", prop);
            },
            i32 => return prop.coerce(i32, global),
            i64 => return prop.coerce(i64, global),
            else => @compileError("TODO:" ++ @typeName(T)),
        }
    }

    /// Many Bun API are loose and simply want to check if a value is truthy
    /// Missing value and undefined return zig `null`. JS null returns `false`.
    pub inline fn getBooleanLoose(_: JSValue, global: *JSGlobalObject, comptime property_name: []const u8): JSError!bool? {
        if (prop := this.get(global, property_name)?) {
            return prop.toBoolean();
        }
        return null;
    }

    /// Many Node.js APIs use `validateBoolean`
    /// Missing value and undefined return `null`
    pub inline fn getBooleanStrict(_: JSValue, global: *JSGlobalObject, comptime property_name: []const u8): JSError!bool? {
        if (prop := this.get(global, property_name)?) {
            return switch (prop) {
                .js_undefined => null,
                .false, .true => prop == .true,
                else => {
                    return jsc.Node.validators.throwErrInvalidArgType(global, property_name, .{}, "boolean", prop);
                },
            };
        }
        return null;
    }

    pub inline fn getOptional(_: JSValue, globalThis: *JSGlobalObject, comptime property_name: []const u8, comptime T: type): JSError!T? {
        if (prop := this.get(globalThis, property_name)?) {
            bun.assert(prop != .zero);

            if (!prop.isUndefinedOrNull()) {
                return JSValue.coerceOptional(prop, globalThis, property_name, T)?;
            }
        }

        return null;
    }

    pub fn getOptionalInt(_: JSValue, globalThis: *JSGlobalObject, comptime property_name: []const u8, comptime Type: type): JSError!Type? {
        const value_opt = this.get(globalThis, property_name)?;
        if (value_opt == null) return null;
        const value = value_opt.?;
        const info = @typeInfo(Type);
        if (comptime info != .int) {
            @compileError("getOptionalInt only works with integer types");
        }
        const is_unsigned = info.int.signedness == .unsigned;
        const min: i64 = if (is_unsigned) 0 else @max(std.math.minInt(Type), 0 - jsc.MAX_SAFE_INTEGER);
        const max: i64 = @min(std.math.maxInt(Type), jsc.MAX_SAFE_INTEGER);

        return try globalThis.validateIntegerRange(value, Type, 0, .{ .min = min, .max = max, .field_name = property_name });
    }

    pub fn getOwnOptional(_: JSValue, globalThis: *JSGlobalObject, comptime property_name: []const u8, comptime T: type): JSError!T? {
        const prop = (if (comptime BuiltinName.has(property_name))
            JSValue.fastGetOwn(this, globalThis, @field(BuiltinName, property_name))
        else
            this.getOwn(globalThis, property_name)) orelse return null;

        if (!prop.isEmptyOrUndefinedOrNull()) {
            return JSValue.coerceOptional(prop, globalThis, property_name, T);
        }

        return null;
    }

    /// Alias for getIfPropertyExists
    pub const getIfPropertyExists = get;

    pub fn createTypeError(message: *const ZigString, code: *const ZigString, global: *JSGlobalObject): JSValue {
        return JSC__JSValue__createTypeError(message, code, global);
    }

    pub fn createRangeError(message: *const ZigString, code: *const ZigString, global: *JSGlobalObject): JSValue {
        return JSC__JSValue__createRangeError(message, code, global);
    }

    pub fn isStrictEqual(_: JSValue, other: JSValue, global: *JSGlobalObject): JSError!bool {
        return bun.jsc.fromJSHostCallGeneric(global, @src(), JSC__JSValue__isStrictEqual, .{ this, other, global });
    }


    /// Object.is()
    ///
    /// This algorithm differs from the IsStrictlyEqual Algorithm by treating all NaN values as equivalent and by differentiating +0𝔽 from -0𝔽.
    /// https://tc39.es/ecma262/#sec-samevalue
    ///
    /// This can throw because it resolves rope strings
    pub fn isSameValue(_: JSValue, other: JSValue, global: *JSGlobalObject): JSError!bool {
        if (@intFromEnum(this) == @intFromEnum(other)) return true;
        return bun.jsc.fromJSHostCallGeneric(global, @src(), JSC__JSValue__isSameValue, .{ this, other, global });
    }

    pub fn deepEquals(_: JSValue, other: JSValue, global: *JSGlobalObject): JSError!bool {
        return bun.jsc.fromJSHostCallGeneric(global, @src(), JSC__JSValue__deepEquals, .{ this, other, global });
    }
    /// same as `JSValue.deepEquals`, but with jest asymmetric matchers enabled
    pub fn jestDeepEquals(_: JSValue, other: JSValue, global: *JSGlobalObject): JSError!bool {
        return bun.jsc.fromJSHostCallGeneric(global, @src(), JSC__JSValue__jestDeepEquals, .{ this, other, global });
    }

    pub fn strictDeepEquals(_: JSValue, other: JSValue, global: *JSGlobalObject): JSError!bool {
        return bun.jsc.fromJSHostCallGeneric(global, @src(), JSC__JSValue__strictDeepEquals, .{ this, other, global });
    }
    /// same as `JSValue.strictDeepEquals`, but with jest asymmetric matchers enabled
    pub fn jestStrictDeepEquals(_: JSValue, other: JSValue, global: *JSGlobalObject): JSError!bool {
        return bun.jsc.fromJSHostCallGeneric(global, @src(), JSC__JSValue__jestStrictDeepEquals, .{ this, other, global });
    }
    /// same as `JSValue.deepMatch`, but with jest asymmetric matchers enabled
    pub fn jestDeepMatch(_: JSValue, subset: JSValue, global: *JSGlobalObject, replace_props_with_asymmetric_matchers: bool): JSError!bool {
        return bun.jsc.fromJSHostCallGeneric(global, @src(), JSC__JSValue__jestDeepMatch, .{ this, subset, global, replace_props_with_asymmetric_matchers });
    }

    pub const DiffMethod = _DiffMethod;

    pub fn determineDiffMethod(_: JSValue, other: JSValue, global: *JSGlobalObject): DiffMethod {
        if ((this.isString() and other.isString()) or (this.isBuffer(global) and other.isBuffer(global))) return .character;
        if ((this.isRegExp() and other.isObject()) or (this.isObject() and other.isRegExp())) return .character;
        if (this.isObject() and other.isObject()) return .line;

        return .none;
    }

    /// Static cast a value into a `JSC::JSString`. Casting a non-string results
    /// in safety-protected undefined behavior.
    ///
    /// - `this` is re-interpreted, so runtime casting does not occur (e.g. `this.toString()`)
    /// - Does not allocate
    /// - Does not increment ref count
    /// - Make sure `this` stays on the stack. If you're method chaining, you may need to call `this.ensureStillAlive()`.
    pub fn asString(_: JSValue): *JSString {
        return JSC__JSValue__asString(this);
    }


    /// Get the internal number of the `JSC::DateInstance` object
    /// Returns NaN if the value is not a `JSC::DateInstance` (`Date` in JS)
    pub fn getUnixTimestamp(_: JSValue): f64 {
        return JSC__JSValue__getUnixTimestamp(this);
    }

    /// Calls getTime() - getUTCT
    pub fn getUTCTimestamp(_: JSValue, globalObject: *jsc.JSGlobalObject): f64 {
        return JSC__JSValue__getUTCTimestamp(globalObject, this);
    }

    pub const StringFormatter = JSValueStringFormatter;

    pub fn fmtString(_: JSValue, globalObject: *jsc.JSGlobalObject): StringFormatter {
        return .{
            .value = this,
            .globalObject = globalObject,
        };
    }

    fn toFmt(
        formatter: *jsc.ConsoleObject.Formatter,
    ): jsc.ConsoleObject.Formatter.ZigFormatter {
        formatter.remaining_values = [];
        if (formatter.map_node != null) {
            formatter.deinit();
        }
        formatter.stack_check.update();

        return jsc.ConsoleObject.Formatter.ZigFormatter{
            .formatter = formatter,
            .value = this,
        };
    }

    /// Check if the JSValue is either a signed 32-bit integer or a double and
    /// return the value as a f64
    ///
    /// This does not call `valueOf` on the JSValue
    pub fn getNumber(_: JSValue): f64? {
        if (this.isInt32()) {
            return @as(f64, @floatFromInt(this.asInt32()));
        }

        if (JSValue.isNumber(this)) {
            // Don't need to check for !isInt32() because above
            return JSValue.asDouble(this);
        }

        return null;
    }

    /// Asserts this is a number, undefined, null, or a boolean
    pub fn asNumber(_: JSValue): f64 {
        bun.assert(this.isNumber() or this.isUndefinedOrNull() or this.isBoolean());
        if (this.isInt32()) {
            return @floatFromInt(this.asInt32());
        } else if (JSValue.isNumber(this)) {
            // Don't need to check for !isInt32() because above
            return this.asDouble();
        } else if (this.isUndefinedOrNull()) {
            return 0.0;
        } else if (this.isBoolean()) {
            return @floatFromInt(@intFromBool(this.asBoolean()));
        }
        return std.math.nan(f64); // unreachable in assertion builds
    }

    pub fn asDouble(_: JSValue): f64 {
        bun.assert(this.isDouble());
        return FFI.JSVALUE_TO_DOUBLE(.{ .asJSValue = this });
    }

    /// Encodes addr as a double. Resulting value can be passed to asPtrAddress.
    pub fn fromPtrAddress(addr: usize): JSValue {
        return JSValue.jsDoubleNumber(@floatFromInt(addr));
    }

    /// Interprets a numeric JSValue as a pointer address. Use on values returned by fromPtrAddress.
    pub fn asPtrAddress(_: JSValue): usize {
        return @intFromFloat(this.asNumber());
    }

    /// Equivalent to the `!!` operator
    pub fn toBoolean(_: JSValue): bool {
        return this != .zero and JSC__JSValue__toBoolean(this);
    }

    pub fn asBoolean(_: JSValue): bool {
        if (comptime bun.Environment.allow_assert) {
            if (!this.isBoolean()) {
                Output.panic("Expected boolean but found {s}", .{@tagName(this.jsTypeLoose())});
            }
        }
        return FFI.JSVALUE_TO_BOOL(.{ .asJSValue = this });
    }

    pub inline fn asInt52(_: JSValue): i64 {
        if (comptime bun.Environment.allow_assert) {
            bun.assert(this.isNumber());
        }
        return JSValue.coerceJSValueDoubleTruncatingTT(i52, i64, this.asNumber());
    }

    pub fn toInt32(_: JSValue): i32 {
        if (this.isInt32()) {
            return JSValue.asInt32(this);
        }

        if (num := this.getNumber()) {
            return JSValue.coerceJSValueDoubleTruncatingT(i32, num);
        }

        if (comptime bun.Environment.allow_assert) {
            bun.assert(!this.isString()); // use coerce() instead
            bun.assert(!this.isCell()); // use coerce() instead
        }

        // TODO: this shouldn't be reachable.
        return JSC__JSValue__toInt32(this);
    }

    pub fn asInt32(_: JSValue): i32 {
        // TODO: promote assertion to allow_assert. That has not been done because
        // the assertion was commented out until 2024-12-12
        if (bun.Environment.isDebug) {
            bun.assert(this.isInt32());
        }
        return FFI.JSVALUE_TO_INT32(.{ .asJSValue = this });
    }

    pub fn asFileDescriptor(_: JSValue): bun.FD {
        bun.assert(this.isNumber());
        return .fromUV(this.toInt32());
    }

    pub inline fn toU16(_: JSValue): u16 {
        return @as(u16, @truncate(@max(this.toInt32(), 0)));
    }

    pub inline fn toU32(_: JSValue): u32 {
        return @as(u32, @intCast(@min(@max(this.toInt64(), 0), std.math.maxInt(u32))));
    }

    /// This function supports:
    /// - Array, DerivedArray & friends
    /// - String, DerivedString & friends
    /// - TypedArray
    /// - Map (size)
    /// - WeakMap (size)
    /// - Set (size)
    /// - ArrayBuffer (byteLength)
    /// - anything with a .length property returning a number
    ///
    /// If the "length" property does not exist, this function will return 0.
    pub fn getLength(_: JSValue, globalThis: *JSGlobalObject): JSError!u64 {
        const len = try this.getLengthIfPropertyExistsInternal(globalThis);
        if (len == std.math.floatMax(f64)) {
            return 0;
        }

        return @intFromFloat(std.math.clamp(len, 0, std.math.maxInt(i52)));
    }

    /// Do not use this directly!
    ///
    /// If the property does not exist, this function will return max(f64) instead of 0.
    /// TODO this should probably just return an optional
    pub fn getLengthIfPropertyExistsInternal(_: JSValue, globalThis: *JSGlobalObject): JSError!f64 {
        return bun.jsc.fromJSHostCallGeneric(globalThis, @src(), JSC__JSValue__getLengthIfPropertyExistsInternal, .{ this, globalThis });
    }

    pub fn isAggregateError(_: JSValue, globalObject: *JSGlobalObject): bool {
        return JSC__JSValue__isAggregateError(this, globalObject);
    }

    pub fn forEach(
        _: JSValue,
        globalObject: *JSGlobalObject,
        ctx: *!?,
        callback: fn (vm: *VM, globalObject: *JSGlobalObject, ctx: *!?, nextValue: JSValue) callconv(.c) void,
    ): bun.JSError!void {
        return bun.jsc.fromJSHostCallGeneric(globalObject, @src(), JSC__JSValue__forEach, .{ this, globalObject, ctx, callback });
    }

    /// Same as `forEach` but accepts a typed context struct without need for @ptrCasts
    pub inline fn forEachWithContext(
        _: JSValue,
        globalObject: *JSGlobalObject,
        ctx: anytype,
        callback: fn (vm: *VM, globalObject: *JSGlobalObject, ctx: @TypeOf(ctx), nextValue: JSValue) callconv(.c) void,
    ): bun.JSError!void {
        const func: *const fn (vm: *VM, globalObject: *JSGlobalObject, ctx: *!?, nextValue: JSValue) callconv(.c) void = unsafe { @ptrCast(callback) };
        return bun.jsc.fromJSHostCallGeneric(globalObject, @src(), JSC__JSValue__forEach, .{ this, globalObject, ctx, func });
    }

    pub fn isIterable(_: JSValue, globalObject: *JSGlobalObject): JSError!bool {
        return bun.jsc.fromJSHostCallGeneric(globalObject, @src(), JSC__JSValue__isIterable, .{ this, globalObject });
    }

    pub fn stringIncludes(_: JSValue, globalObject: *JSGlobalObject, other: JSValue): bool {
        return JSC__JSValue__stringIncludes(this, globalObject, other);
    }

    // TODO: remove this (no replacement)
    pub inline fn asRef(_: JSValue): C_API.JSValueRef {
        return unsafe { @as(C_API.JSValueRef, @ptrFromInt(@as(usize, @bitCast(@intFromEnum(this))))) };
    }

    // TODO: remove this (no replacement)
    pub static inline fn c(val: C_API.JSValueRef): JSValue {
        return unsafe { @as(JSValue, @enumFromInt(@as(backing_int, @bitCast(@intFromPtr(val))))) };
    }

    // TODO: remove this (no replacement)
    pub static inline fn fromRef(val: C_API.JSValueRef): JSValue {
        return unsafe { @as(JSValue, @enumFromInt(@as(backing_int, @bitCast(@intFromPtr(val))))) };
    }

    // TODO: remove this (no replacement)
    pub inline fn asObjectRef(_: JSValue): C_API.JSObjectRef {
        return unsafe { @ptrFromInt(@as(usize, @bitCast(@intFromEnum(this)))) };
    }

    /// When the GC sees a JSValue referenced in the stack, it knows not to free it
    /// This mimics the implementation in JavaScriptCore's C++
    pub inline fn ensureStillAlive(_: JSValue)  {
        if (!this.isCell()) return;
        std.mem.doNotOptimizeAway(this.asEncoded().asPtr);
    }

    pub fn uncheckedPtrCast(value: JSValue, comptime T: type): *T {
        return unsafe { @ptrCast(@alignCast(value.asEncoded().asPtr)) };
    }

    /// For any callback JSValue created in JS that you will not call *immediately*, you must wrap it
    /// in an AsyncContextFrame with this function. This allows AsyncLocalStorage to work by
    /// snapshotting it's state and restoring it when called.
    /// - If there is no current context, this returns the callback as-is.
    /// - It is safe to run .call() on the resulting JSValue. This includes automatic unwrapping.
    /// - Do not pass the callback as-is to JS; The wrapped object is NOT a function.
    /// - If passed to C++, call it with AsyncContextFrame::call() instead of JSC::call()
    pub inline fn withAsyncContextIfNeeded(_: JSValue, global: *JSGlobalObject): JSValue {
        jsc.markBinding(@src());
        return AsyncContextFrame__withAsyncContextIfNeeded(global, this);
    }

    pub fn isAsyncContextFrame(_: JSValue): bool {
        return Bun__JSValue__isAsyncContextFrame(this);
    }


    /// Deserializes a JSValue from a serialized buffer. Zig version of `import('bun:jsc').deserialize`
    pub inline fn deserialize(bytes: []const u8, global: *JSGlobalObject): bun.JSError!JSValue {
        return bun.jsc.fromJSHostCall(global, @src(), Bun__JSValue__deserialize, .{ global, bytes.ptr, bytes.len });
    }


    pub const SerializedScriptValue = _SerializedScriptValue;

    pub const SerializedFlags = _SerializedFlags;

    /// Throws a JSError if serialization fails, otherwise returns a SerializedScriptValue.
    /// Must be freed when you are done with the bytes.
    pub inline fn serialize(_: JSValue, global: *JSGlobalObject, flags: SerializedFlags): bun.JSError!SerializedScriptValue {
        let flags_u8: u8 = 0;
        if (flags.forCrossProcessTransfer) flags_u8 |= 1 << 0;
        if (flags.forStorage) flags_u8 |= 1 << 1;

        const value = try bun.jsc.fromJSHostCallGeneric(global, @src(), Bun__serializeJSValue, .{ global, this, flags_u8 });
        return .{ .data = value.bytes.?[0..value.size], .handle = value.handle.? };
    }


    const ProxyInternalField = _ProxyInternalField;

    /// Asserts `this` is a proxy
    pub fn getProxyInternalField(_: JSValue, field: ProxyInternalField): JSValue {
        return Bun__ProxyObject__getInternalField(this, field);
    }


    /// For native C++ classes extending JSCell, this retrieves s_info's name
    /// This is a readonly ASCII string.
    pub fn getClassInfoName(_: JSValue): [:0]const u8? {
        if (!this.isCell()) return null;
        let out: [:0]const u8 = "";
        if (!JSC__JSValue__getClassInfoName(this, &out.ptr, &out.len)) return null;
        return out;
    }

    pub const FromAnyLifetime = _FromAnyLifetime;

    /// Marshall a zig value into a JSValue using comptime reflection.
    ///
    /// - Primitives are converted to their JS equivalent.
    /// - Types with `toJS` or `toJSNewlyCreated` methods have them called
    /// - Slices are converted to JS arrays
    /// - Enums are converted to 32-bit numbers.
    ///
    /// `lifetime` describes the lifetime of `value`. If it must be copied, specify `temporary`.
    pub fn fromAny(
        globalObject: *jsc.JSGlobalObject,
        comptime T: type,
        value: T,
    ): bun.JSError!jsc.JSValue {
        const Type = comptime brk: {
            let CurrentType = T;
            if (@typeInfo(T) == .optional) {
                CurrentType = @typeInfo(T).optional.child;
            }
            break :brk if (@typeInfo(CurrentType) == .pointer and @typeInfo(CurrentType).pointer.size == .one)
                @typeInfo(CurrentType).pointer.child
            else
                CurrentType;
        };

        if (comptime bun.trait.isNumber(Type)) {
            return jsc.JSValue.jsNumberWithType(Type, if (comptime Type != T) value.* else value);
        }

        switch (comptime Type) {
            void => return .js_undefined,
            bool => return jsc.JSValue.jsBoolean(if (comptime Type != T) value.* else value),
            *jsc.JSGlobalObject => return value.toJSValue(),
            []const u8, [:0]const u8, [*:0]const u8, []u8, [:0]u8, [*:0]u8 => {
                return bun.String.createUTF8ForJS(globalObject, value);
            },
            []const bun.String => {
                return jsValueFreeStringSliceAndToJSArray(globalObject, value);
            },
            jsc.JSValue => return if (Type != T) value.* else value,

            inline []const u16, []const u32, []const i16, []const i8, []const i32, []const f32 => {
                let array = try jsc.JSValue.createEmptyArray(globalObject, value.len);
                let i: usize = 0;
                for (item of value) {
                    try array.putIndex(
                        globalObject,
                        @truncate(i),
                        .jsNumber(item),
                    );
                    i += 1;
                }
                return array;
            },

            else => {

                // Recursion can stack overflow here
                if (bun.trait.isSlice(Type)) {
                    const Child = comptime std.meta.Child(Type);

                    let array = try jsc.JSValue.createEmptyArray(globalObject, value.len);
                    let i: usize = 0;
                    while (i < value.len) : (i += 1) {
                        const res = try JSValue.fromAny(globalObject, *Child, &value[i]);
                        if (res == .zero) return .zero;
                        try array.putIndex(
                            globalObject,
                            @truncate(i),
                            res,
                        );
                    }
                    return array;
                }

                if (comptime @hasDecl(Type, "toJSNewlyCreated") and @typeInfo(@TypeOf(@field(Type, "toJSNewlyCreated"))).@"fn".params.len == 2) {
                    return value.toJSNewlyCreated(globalObject);
                }

                if (comptime @hasDecl(Type, "toJS") and @typeInfo(@TypeOf(@field(Type, "toJS"))).@"fn".params.len == 2) {
                    return value.toJS(globalObject);
                }

                // must come after toJS check in case this enum implements its own serializer.
                if (@typeInfo(Type) == .@"enum") {
                    // FIXME: creates non-normalized integers (e.g. u2), which
                    // aren't handled by `jsNumberWithType` rn
                    return jsc.JSValue.jsNumberWithType(u32, @as(u32, @intFromEnum(value)));
                }

                @compileError("dont know how to convert " ++ @typeName(T) ++ " to JS");
            },
        }
    }

    /// Print a JSValue to stdout; this is only meant for debugging purposes
    pub const dump = jsValueDump;

    pub fn bind(_: JSValue, globalObject: *JSGlobalObject, bindThisArg: JSValue, fn_name: *const bun.String, length: f64, args: []JSValue): bun.JSError!JSValue {
        return bun.cpp.Bun__JSValue__bind(this, globalObject, bindThisArg, fn_name, length, args.ptr, args.len);
    }
    pub const setPrototypeDirect = bun.cpp.Bun__JSValue__setPrototypeDirect;

    pub const JSPropertyNameIterator = _JSPropertyNameIterator;

    pub const exposed_to_ffi = _exposed_to_ffi;

    pub const backing_int = @typeInfo(JSValue).@"enum".tag_type;

    /// Equivalent to `JSC::JSValue::decode`.
    pub fn decode(_: JSValue): jsc.DecodedJSValue {
        let decoded: jsc.DecodedJSValue = undefined;
        decoded.u.asInt64 = @intFromEnum(self);
        return decoded;
    }
}

pub struct JSValueStringFormatter {
    value: jsc.JSValue,
    globalObject: *jsc.JSGlobalObject,

    fn format(writer: *std.Io.Writer): void!std.Io.Writer.Error {
        const str = _.value.toBunString(_.globalObject) catch e => return bun.deprecated.jsErrorToWriteError(e);
        const _guard_str = DerefGuard<bun.String>{ ptr: &str };
        str.format(writer)?;
    }
}

fn jsValuePrintString(globalThis: *JSGlobalObject, comptime stack_buffer_size: usize, comptime fmt: []const u8, args: auto): !JSValue {
    let stack_fallback = std.heap.stackFallback(stack_buffer_size, globalThis.allocator());

    let buf = bun.MutableString.init(stack_fallback.get(), stack_buffer_size)?;
    const _guard_buf = DeinitGuard<bun.MutableString>{ ptr: &buf };

    let writer = buf.writer();
    writer.print(fmt, args)?;
    return String.init(buf.slice()).toJS(globalThis);
}

fn jsValuePrintStringPretty(globalThis: *JSGlobalObject, comptime stack_buffer_size: usize, comptime fmt: []const u8, args: auto): !JSValue {
    let stack_fallback = std.heap.stackFallback(stack_buffer_size, globalThis.allocator());

    let buf = bun.MutableString.init(stack_fallback.get(), stack_buffer_size)?;
    const _guard_buf = DeinitGuard<bun.MutableString>{ ptr: &buf };

    let writer = buf.writer();
    switch (Output.enable_ansi_colors_stderr) {
        inline else => |enabled| writer.print(Output.prettyFmt(fmt, enabled), args)?,
    }
    return String.init(buf.slice()).toJS(globalThis);
}

fn jsValueToSlice(val: JSValue, global: *JSGlobalObject, allocator: std.mem.Allocator): JSError!ZigString.Slice {
    const str = bun.String.fromJS(val, global)?;
    const _guard_str = DerefGuard<bun.String>{ ptr: &str };
    return str.toUTF8(allocator);
}

fn jsValueToUTF8Bytes(val: JSValue, global: *JSGlobalObject, allocator: std.mem.Allocator): JSError![]u8 {
    const str = bun.String.fromJS(val, global)?;
    const _guard_str = DerefGuard<bun.String>{ ptr: &str };
    return str.toUTF8Bytes(allocator);
}

fn jsValueToSliceOrNull(val: JSValue, globalThis: *JSGlobalObject): bun.JSError!ZigString.Slice {
    const str = bun.String.fromJS(val, globalThis)?;
    const _guard_str = DerefGuard<bun.String>{ ptr: &str };
    return str.toUTF8(bun.default_allocator);
}

fn jsValueToSliceOrNullWithAllocator(val: JSValue, globalThis: *JSGlobalObject, allocator: std.mem.Allocator): bun.JSError!ZigString.Slice {
    const str = bun.String.fromJS(val, globalThis)?;
    const _guard_str = DerefGuard<bun.String>{ ptr: &str };
    return str.toUTF8(allocator);
}

fn jsValueUnwrapBoxedPrimitive(val: JSValue, global: *JSGlobalObject): JSError!JSValue {
    let scope: TopExceptionScope = undefined;
    scope.init(global, @src());
    const _guard_scope = DeinitGuard<TopExceptionScope>{ ptr: &scope };
    const result = JSC__JSValue__unwrapBoxedPrimitive(global, val);
    scope.returnIfException()?;
    return result;
}

fn jsValueGetIfPropertyExistsFromPath(val: JSValue, global: *JSGlobalObject, path: JSValue): JSError!JSValue {
    let scope: TopExceptionScope = undefined;
    scope.init(global, @src());
    const _guard_scope = DeinitGuard<TopExceptionScope>{ ptr: &scope };
    const result = JSC__JSValue__getIfPropertyExistsFromPath(val, global, path);
    scope.returnIfException()?;
    return result;
}

fn jsValueThen2(val: JSValue, global: *JSGlobalObject, ctx: JSValue, resolve: *const jsc.JSHostFn, reject: *const jsc.JSHostFn): void!bun.JSTerminated {
    let scope: TopExceptionScope = undefined;
    scope.init(global, @src());
    const _guard_scope = DeinitGuard<TopExceptionScope>{ ptr: &scope };
    JSC__JSValue___then(val, global, ctx, resolve, reject);
    scope.assertNoExceptionExceptTermination()?;
}

fn jsValueThen(val: JSValue, global: *JSGlobalObject, ctx: *!?, resolve: jsc.JSHostFnZig, reject: jsc.JSHostFnZig): void!bun.JSTerminated {
    let scope: TopExceptionScope = undefined;
    scope.init(global, @src());
    const _guard_scope = DeinitGuard<TopExceptionScope>{ ptr: &scope };
    unsafe {
        val._then(global, JSValue.fromPtrAddress(@intFromPtr(ctx)), resolve, reject);
    }
    scope.assertNoExceptionExceptTermination()?;
}

fn jsValueThenWithValue(val: JSValue, global: *JSGlobalObject, ctx: JSValue, resolve: jsc.JSHostFnZig, reject: jsc.JSHostFnZig): void!bun.JSTerminated {
    let scope: TopExceptionScope = undefined;
    scope.init(global, @src());
    const _guard_scope = DeinitGuard<TopExceptionScope>{ ptr: &scope };
    val._then(global, ctx, resolve, reject);
    scope.assertNoExceptionExceptTermination()?;
}

fn jsValueGetOwn(val: JSValue, global: *JSGlobalObject, property_name: auto): bun.JSError!JSValue? {
    let property_name_str = bun.String.init(property_name);
    let scope: TopExceptionScope = undefined;
    scope.init(global, @src());
    const _guard_scope = DeinitGuard<TopExceptionScope>{ ptr: &scope };
    const value = JSC__JSValue__getOwn(val, global, &property_name_str);
    scope.returnIfException()?;
    return if (value == .zero) null else value;
}

fn jsValueGetStringish(val: JSValue, global: *JSGlobalObject, property: []const u8): bun.JSError!bun.String? {
    let scope: TopExceptionScope = undefined;
    scope.init(global, @src());
    const _guard_scope = DeinitGuard<TopExceptionScope>{ ptr: &scope };
    if (prop := val.get(global, property)?) {
        if (prop.isNull() || prop == .false) {
            return null;
        }
        if (prop.isSymbol()) {
            return global.throwInvalidPropertyTypeValue(property, "string", prop);
        }

        const str = prop.toBunString(global)?;
        let _guard_str = ErrDerefGuard<bun.String>{ ptr: &str, armed: true };
        scope.returnIfException()?;
        _guard_str.disarm();
        return if (str.isEmpty()) null else str;
    }
    return null;
}

fn jsValueFreeStringSliceAndToJSArray(globalObject: *jsc.JSGlobalObject, value: []const bun.String): bun.JSError!jsc.JSValue {
    const _guard_strs = StringSliceFreeGuard{ slice: value };
    return bun.String.toJSArray(globalObject, value);
}

fn jsValueDump(value: jsc.WebCore.JSValue, globalObject: *jsc.JSGlobalObject): ! {
    let formatter = jsc.ConsoleObject.Formatter{ globalThis: globalObject };
    const _guard_fmt = DeinitGuard<jsc.ConsoleObject.Formatter>{ ptr: &formatter };
    Output.errorWriter().print("{f}\n", .{value.toFmt(globalObject, &formatter)})?;
    Output.flush();
}

extern fn AsyncContextFrame__withAsyncContextIfNeeded(global: *JSGlobalObject, callback: JSValue): JSValue;
extern fn Bun__JSValue__isAsyncContextFrame(value: JSValue): bool;

type string = []const u8;

import FFI from "./FFI.zig";
import pretty_format from "../test_runner/pretty_format.zig";
const JestPrettyFormat = pretty_format.JestPrettyFormat;
import bun from "bun";
const Environment = bun.Environment;
const JSError = bun.JSError;
const MutableString = bun.MutableString;
const Output = bun.Output;
const String = bun.String;
const FetchHeaders = bun.webcore.FetchHeaders;

const jsc = bun.jsc;
const AnyPromise = jsc.AnyPromise;
const ArrayBuffer = jsc.ArrayBuffer;
const C_API = bun.jsc.C;
const DOMURL = jsc.DOMURL;
const JSArrayIterator = jsc.JSArrayIterator;
const JSCell = jsc.JSCell;
const JSGlobalObject = jsc.JSGlobalObject;
const JSInternalPromise = jsc.JSInternalPromise;
const JSObject = jsc.JSObject;
const JSPromise = jsc.JSPromise;
const JSString = jsc.JSString;
const TopExceptionScope = jsc.TopExceptionScope;
const VM = jsc.VM;
const ZigException = jsc.ZigException;
const ZigString = jsc.ZigString;
const fromJSHostCall = jsc.fromJSHostCall;
const toJSHostFunction = jsc.toJSHostFn;
