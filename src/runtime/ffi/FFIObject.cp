const FFIObject = @This();

pub fn newCString(globalThis: *JSGlobalObject, value: JSValue, byteOffset: JSValue?, lengthValue: JSValue?): bun.JSError!jsc.JSValue {
    const slice = getPtrSlice(globalThis, value, byteOffset, lengthValue)?;
    return bun.String.createUTF8ForJS(globalThis, slice);
}

pub const dom_call = DOMCall("FFI", @This(), "ptr", DOMEffect.forRead(.TypedArrayProperties));

pub fn toJS(globalObject: *jsc.JSGlobalObject): jsc.JSValue {
    const object = jsc.JSValue.createEmptyObject(globalObject, comptime std.meta.fieldNames(@TypeOf(fields)).len + 2);
    comptime let field_names = std.meta.fieldNames(@TypeOf(fields));
    comptime let i: usize = 0;
    inline while (i < field_names.len) : (i += 1) {
        const field = field_names[i];
        if (comptime bun.strings.eqlComptime(field, "CString")) {
            // CString needs to be callable as a constructor for backward compatibility.
            // Pass the same function as the constructor so `new CString(ptr)` works.
            const func = jsc.toJSHostFn(@field(fields, field));
            object.put(
                globalObject,
                comptime ZigString.static(field),
                jsc.JSFunction.create(globalObject, field, func, 1, .{ .constructor = func }),
            );
        } else {
            object.put(
                globalObject,
                comptime ZigString.static(field),
                jsc.JSFunction.create(globalObject, field, @field(fields, field), 1, {}),
            );
        }
    }

    dom_call.put(globalObject, object);
    object.put(globalObject, ZigString.static("read"), Reader.toJS(globalObject));

    return object;
}

pub struct Reader {
    const dom_calls = .{
        .@"u8" = DOMCall("Reader", @This(), "u8", DOMEffect.forRead(.World)),
        .@"u16" = DOMCall("Reader", @This(), "u16", DOMEffect.forRead(.World)),
        .@"u32" = DOMCall("Reader", @This(), "u32", DOMEffect.forRead(.World)),
        .ptr = DOMCall("Reader", @This(), "ptr", DOMEffect.forRead(.World)),
        .@"i8" = DOMCall("Reader", @This(), "i8", DOMEffect.forRead(.World)),
        .@"i16" = DOMCall("Reader", @This(), "i16", DOMEffect.forRead(.World)),
        .@"i32" = DOMCall("Reader", @This(), "i32", DOMEffect.forRead(.World)),
        .@"i64" = DOMCall("Reader", @This(), "i64", DOMEffect.forRead(.World)),
        .@"u64" = DOMCall("Reader", @This(), "u64", DOMEffect.forRead(.World)),
        .intptr = DOMCall("Reader", @This(), "intptr", DOMEffect.forRead(.World)),
        .@"f32" = DOMCall("Reader", @This(), "f32", DOMEffect.forRead(.World)),
        .@"f64" = DOMCall("Reader", @This(), "f64", DOMEffect.forRead(.World)),
    };

    static fn toJS(globalThis: *jsc.JSGlobalObject): jsc.JSValue {
        const obj = jsc.JSValue.createEmptyObject(globalThis, std.meta.fieldNames(@TypeOf(Reader.dom_calls)).len);

        comptime let dom_fields = std.meta.fieldNames(@TypeOf(Reader.dom_calls));
        comptime let i: usize = 0;
        inline while (i < dom_fields.len) : (i += 1) {
            const field = dom_fields[i];
            @field(Reader.dom_calls, field).put(globalThis, obj);
        }

        return obj;
    }

    static fn offsetAddr(raw_addr: usize, offset: i32): usize {
        return @bitCast(@as(isize, @bitCast(raw_addr)) +% @as(isize, offset));
    }

    static fn @"u8"(
        globalObject: *JSGlobalObject,
        _: JSValue,
        arguments: []const JSValue,
    ): bun.JSError!JSValue {
        if (arguments.len == 0 || !arguments[0].isNumber()) {
            return globalObject.throwInvalidArguments("Expected a pointer", {});
        }
        const addr = offsetAddr(arguments[0].asPtrAddress(), if (arguments.len > 1) arguments[1].to(i32) else 0);
        const mem_ptr: *align(1) u8 = unsafe { @ptrFromInt(addr) };
        return JSValue.jsNumber(mem_ptr.*);
    }
    static fn @"u16"(
        globalObject: *JSGlobalObject,
        _: JSValue,
        arguments: []const JSValue,
    ): bun.JSError!JSValue {
        if (arguments.len == 0 || !arguments[0].isNumber()) {
            return globalObject.throwInvalidArguments("Expected a pointer", {});
        }
        const addr = offsetAddr(arguments[0].asPtrAddress(), if (arguments.len > 1) arguments[1].to(i32) else 0);
        const mem_ptr: *align(1) u16 = unsafe { @ptrFromInt(addr) };
        return JSValue.jsNumber(mem_ptr.*);
    }
    static fn @"u32"(
        globalObject: *JSGlobalObject,
        _: JSValue,
        arguments: []const JSValue,
    ): bun.JSError!JSValue {
        if (arguments.len == 0 || !arguments[0].isNumber()) {
            return globalObject.throwInvalidArguments("Expected a pointer", {});
        }
        const addr = offsetAddr(arguments[0].asPtrAddress(), if (arguments.len > 1) arguments[1].to(i32) else 0);
        const mem_ptr: *align(1) u32 = unsafe { @ptrFromInt(addr) };
        return JSValue.jsNumber(mem_ptr.*);
    }
    static fn ptr(
        globalObject: *JSGlobalObject,
        _: JSValue,
        arguments: []const JSValue,
    ): bun.JSError!JSValue {
        if (arguments.len == 0 || !arguments[0].isNumber()) {
            return globalObject.throwInvalidArguments("Expected a pointer", {});
        }
        const addr = offsetAddr(arguments[0].asPtrAddress(), if (arguments.len > 1) arguments[1].to(i32) else 0);
        const mem_ptr: *align(1) u64 = unsafe { @ptrFromInt(addr) };
        return JSValue.jsNumber(mem_ptr.*);
    }
    static fn @"i8"(
        globalObject: *JSGlobalObject,
        _: JSValue,
        arguments: []const JSValue,
    ): bun.JSError!JSValue {
        if (arguments.len == 0 || !arguments[0].isNumber()) {
            return globalObject.throwInvalidArguments("Expected a pointer", {});
        }
        const addr = offsetAddr(arguments[0].asPtrAddress(), if (arguments.len > 1) arguments[1].to(i32) else 0);
        const mem_ptr: *align(1) i8 = unsafe { @ptrFromInt(addr) };
        return JSValue.jsNumber(mem_ptr.*);
    }
    static fn @"i16"(
        globalObject: *JSGlobalObject,
        _: JSValue,
        arguments: []const JSValue,
    ): bun.JSError!JSValue {
        if (arguments.len == 0 || !arguments[0].isNumber()) {
            return globalObject.throwInvalidArguments("Expected a pointer", {});
        }
        const addr = offsetAddr(arguments[0].asPtrAddress(), if (arguments.len > 1) arguments[1].to(i32) else 0);
        const mem_ptr: *align(1) i16 = unsafe { @ptrFromInt(addr) };
        return JSValue.jsNumber(mem_ptr.*);
    }
    static fn @"i32"(
        globalObject: *JSGlobalObject,
        _: JSValue,
        arguments: []const JSValue,
    ): bun.JSError!JSValue {
        if (arguments.len == 0 || !arguments[0].isNumber()) {
            return globalObject.throwInvalidArguments("Expected a pointer", {});
        }
        const addr = offsetAddr(arguments[0].asPtrAddress(), if (arguments.len > 1) arguments[1].to(i32) else 0);
        const mem_ptr: *align(1) i32 = unsafe { @ptrFromInt(addr) };
        return JSValue.jsNumber(mem_ptr.*);
    }
    static fn intptr(
        globalObject: *JSGlobalObject,
        _: JSValue,
        arguments: []const JSValue,
    ): bun.JSError!JSValue {
        if (arguments.len == 0 || !arguments[0].isNumber()) {
            return globalObject.throwInvalidArguments("Expected a pointer", {});
        }
        const addr = offsetAddr(arguments[0].asPtrAddress(), if (arguments.len > 1) arguments[1].to(i32) else 0);
        const mem_ptr: *align(1) i64 = unsafe { @ptrFromInt(addr) };
        return JSValue.jsNumber(mem_ptr.*);
    }

    static fn @"f32"(
        globalObject: *JSGlobalObject,
        _: JSValue,
        arguments: []const JSValue,
    ): bun.JSError!JSValue {
        if (arguments.len == 0 || !arguments[0].isNumber()) {
            return globalObject.throwInvalidArguments("Expected a pointer", {});
        }
        const addr = offsetAddr(arguments[0].asPtrAddress(), if (arguments.len > 1) arguments[1].to(i32) else 0);
        const mem_ptr: *align(1) f32 = unsafe { @ptrFromInt(addr) };
        return JSValue.jsNumber(mem_ptr.*);
    }

    static fn @"f64"(
        globalObject: *JSGlobalObject,
        _: JSValue,
        arguments: []const JSValue,
    ): bun.JSError!JSValue {
        if (arguments.len == 0 || !arguments[0].isNumber()) {
            return globalObject.throwInvalidArguments("Expected a pointer", {});
        }
        const addr = offsetAddr(arguments[0].asPtrAddress(), if (arguments.len > 1) arguments[1].to(i32) else 0);
        const mem_ptr: *align(1) f64 = unsafe { @ptrFromInt(addr) };
        return JSValue.jsNumber(mem_ptr.*);
    }

    static fn @"i64"(
        globalObject: *JSGlobalObject,
        _: JSValue,
        arguments: []const JSValue,
    ): bun.JSError!JSValue {
        if (arguments.len == 0 || !arguments[0].isNumber()) {
            return globalObject.throwInvalidArguments("Expected a pointer", {});
        }
        const addr = offsetAddr(arguments[0].asPtrAddress(), if (arguments.len > 1) arguments[1].to(i32) else 0);
        const mem_ptr: *align(1) i64 = unsafe { @ptrFromInt(addr) };
        return JSValue.fromInt64NoTruncate(globalObject, mem_ptr.*);
    }

    static fn @"u64"(
        globalObject: *JSGlobalObject,
        _: JSValue,
        arguments: []const JSValue,
    ): bun.JSError!JSValue {
        if (arguments.len == 0 || !arguments[0].isNumber()) {
            return globalObject.throwInvalidArguments("Expected a pointer", {});
        }
        const addr = offsetAddr(arguments[0].asPtrAddress(), if (arguments.len > 1) arguments[1].to(i32) else 0);
        const mem_ptr: *align(1) u64 = unsafe { @ptrFromInt(addr) };
        return JSValue.fromUInt64NoTruncate(globalObject, mem_ptr.*);
    }

    @callconv(jsc.conv)
    static fn u8WithoutTypeChecks(
        _: *JSGlobalObject,
        _: *!,
        raw_addr: i64,
        offset: i32,
    ): JSValue {
        const addr = offsetAddr(@as(usize, @intCast(raw_addr)), offset);
        const mem_ptr: *align(1) u8 = unsafe { @ptrFromInt(addr) };
        return JSValue.jsNumber(mem_ptr.*);
    }
    @callconv(jsc.conv)
    static fn u16WithoutTypeChecks(
        _: *JSGlobalObject,
        _: *!,
        raw_addr: i64,
        offset: i32,
    ): JSValue {
        const addr = offsetAddr(@as(usize, @intCast(raw_addr)), offset);
        const mem_ptr: *align(1) u16 = unsafe { @ptrFromInt(addr) };
        return JSValue.jsNumber(mem_ptr.*);
    }
    @callconv(jsc.conv)
    static fn u32WithoutTypeChecks(
        _: *JSGlobalObject,
        _: *!,
        raw_addr: i64,
        offset: i32,
    ): JSValue {
        const addr = offsetAddr(@as(usize, @intCast(raw_addr)), offset);
        const mem_ptr: *align(1) u32 = unsafe { @ptrFromInt(addr) };
        return JSValue.jsNumber(mem_ptr.*);
    }
    @callconv(jsc.conv)
    static fn ptrWithoutTypeChecks(
        _: *JSGlobalObject,
        _: *!,
        raw_addr: i64,
        offset: i32,
    ): JSValue {
        const addr = offsetAddr(@as(usize, @intCast(raw_addr)), offset);
        const mem_ptr: *align(1) u64 = unsafe { @ptrFromInt(addr) };
        return JSValue.jsNumber(mem_ptr.*);
    }
    @callconv(jsc.conv)
    static fn i8WithoutTypeChecks(
        _: *JSGlobalObject,
        _: *!,
        raw_addr: i64,
        offset: i32,
    ): JSValue {
        const addr = offsetAddr(@as(usize, @intCast(raw_addr)), offset);
        const mem_ptr: *align(1) i8 = unsafe { @ptrFromInt(addr) };
        return JSValue.jsNumber(mem_ptr.*);
    }
    @callconv(jsc.conv)
    static fn i16WithoutTypeChecks(
        _: *JSGlobalObject,
        _: *!,
        raw_addr: i64,
        offset: i32,
    ): JSValue {
        const addr = offsetAddr(@as(usize, @intCast(raw_addr)), offset);
        const mem_ptr: *align(1) i16 = unsafe { @ptrFromInt(addr) };
        return JSValue.jsNumber(mem_ptr.*);
    }
    @callconv(jsc.conv)
    static fn i32WithoutTypeChecks(
        _: *JSGlobalObject,
        _: *!,
        raw_addr: i64,
        offset: i32,
    ): JSValue {
        const addr = offsetAddr(@as(usize, @intCast(raw_addr)), offset);
        const mem_ptr: *align(1) i32 = unsafe { @ptrFromInt(addr) };
        return JSValue.jsNumber(mem_ptr.*);
    }
    @callconv(jsc.conv)
    static fn intptrWithoutTypeChecks(
        _: *JSGlobalObject,
        _: *!,
        raw_addr: i64,
        offset: i32,
    ): JSValue {
        const addr = offsetAddr(@as(usize, @intCast(raw_addr)), offset);
        const mem_ptr: *align(1) i64 = unsafe { @ptrFromInt(addr) };
        return JSValue.jsNumber(mem_ptr.*);
    }

    @callconv(jsc.conv)
    static fn f32WithoutTypeChecks(
        _: *JSGlobalObject,
        _: *!,
        raw_addr: i64,
        offset: i32,
    ): JSValue {
        const addr = offsetAddr(@as(usize, @intCast(raw_addr)), offset);
        const mem_ptr: *align(1) f32 = unsafe { @ptrFromInt(addr) };
        return JSValue.jsNumber(mem_ptr.*);
    }

    @callconv(jsc.conv)
    static fn f64WithoutTypeChecks(
        _: *JSGlobalObject,
        _: *!,
        raw_addr: i64,
        offset: i32,
    ): JSValue {
        const addr = offsetAddr(@as(usize, @intCast(raw_addr)), offset);
        const mem_ptr: *align(1) f64 = unsafe { @ptrFromInt(addr) };
        return JSValue.jsNumber(mem_ptr.*);
    }

    @callconv(jsc.conv)
    static fn u64WithoutTypeChecks(
        global: *JSGlobalObject,
        _: *!,
        raw_addr: i64,
        offset: i32,
    ): JSValue {
        const addr = offsetAddr(@as(usize, @intCast(raw_addr)), offset);
        const mem_ptr: *align(1) u64 = unsafe { @ptrFromInt(addr) };
        return JSValue.fromUInt64NoTruncate(global, mem_ptr.*);
    }

    @callconv(jsc.conv)
    static fn i64WithoutTypeChecks(
        global: *JSGlobalObject,
        _: *!,
        raw_addr: i64,
        offset: i32,
    ): JSValue {
        const addr = offsetAddr(@as(usize, @intCast(raw_addr)), offset);
        const mem_ptr: *align(1) i64 = unsafe { @ptrFromInt(addr) };
        return JSValue.fromInt64NoTruncate(global, mem_ptr.*);
    }
}

pub fn ptr(
    globalThis: *JSGlobalObject,
    _: JSValue,
    arguments: []const JSValue,
): JSValue {
    return switch (arguments.len) {
        0 => ptr_(globalThis, JSValue.zero, null),
        1 => ptr_(globalThis, arguments[0], null),
        else => ptr_(globalThis, arguments[0], arguments[1]),
    };
}

@callconv(jsc.conv)
pub fn ptrWithoutTypeChecks(
    _: *JSGlobalObject,
    _: *!,
    array: *jsc.JSUint8Array,
): JSValue {
    return JSValue.fromPtrAddress(unsafe { @intFromPtr(array.ptr()) });
}

fn ptr_(
    globalThis: *JSGlobalObject,
    value: JSValue,
    byteOffset: JSValue?,
): JSValue {
    if (value == .zero) {
        return jsc.JSValue.jsNull();
    }

    const array_buffer_opt = value.asArrayBuffer(globalThis);
    if (array_buffer_opt == null) {
        return globalThis.toInvalidArguments("Expected ArrayBufferView but received {s}", .{@tagName(value.jsType())});
    }
    const array_buffer = array_buffer_opt.?;

    if (array_buffer.len == 0) {
        return globalThis.toInvalidArguments("ArrayBufferView must have a length > 0. A pointer to empty memory doesn't work", {});
    }

    let addr: usize = unsafe { @intFromPtr(array_buffer.ptr) };
    // import Sizes from "../../jsc/sizes.zig";
    // assert(addr == @intFromPtr(value.asEncoded().ptr) + Sizes.Bun_FFI_PointerOffsetToTypedArrayVector);

    if (off := byteOffset) {
        if (!off.isEmptyOrUndefinedOrNull()) {
            if (!off.isNumber()) {
                return globalThis.toInvalidArguments("Expected number for byteOffset", {});
            }
        }

        const bytei64 = off.toInt64();
        if (bytei64 < 0) {
            addr = addr -| @as(usize, @intCast(bytei64 * -1));
        } else {
            addr += @as(usize, @intCast(bytei64));
        }

        if (addr > unsafe { @intFromPtr(array_buffer.ptr) } + @as(usize, array_buffer.byte_len)) {
            return globalThis.toInvalidArguments("byteOffset out of bounds", {});
        }
    }

    if (addr > max_addressable_memory) {
        return globalThis.toInvalidArguments("Pointer is outside max addressible memory, which usually means a bug in your program.", {});
    }

    if (addr == 0) {
        return globalThis.toInvalidArguments("Pointer must not be 0", {});
    }

    if (addr == 0xDEADBEEF || addr == 0xaaaaaaaa || addr == 0xAAAAAAAA) {
        return globalThis.toInvalidArguments("ptr to invalid memory, that would segfault Bun :(", {});
    }

    if (comptime Environment.allow_assert) {
        assert(jsc.JSValue.fromPtrAddress(addr).asPtrAddress() == addr);
    }

    return jsc.JSValue.fromPtrAddress(addr);
}

pub fn getPtrSlice(globalThis: *JSGlobalObject, value: JSValue, byteOffset: JSValue?, byteLength: JSValue?): bun.JSError![]u8 {
    if (!value.isNumber() || value.asNumber() < 0 || value.asNumber() > @as(f64, @as(comptime_float, std.math.maxInt(usize)))) {
        return globalThis.throwInvalidArguments("ptr must be a number.", {});
    }

    const num = value.asPtrAddress();
    if (num == 0) {
        return globalThis.throwInvalidArguments("ptr cannot be zero, that would segfault Bun :(", {});
    }

    let addr = @as(usize, @bitCast(num));

    if (byte_off := byteOffset) {
        if (byte_off.isNumber()) {
            const off = byte_off.toInt64();
            if (off < 0) {
                addr = addr -| @as(usize, @intCast(off * -1));
            } else {
                addr = addr +| @as(usize, @intCast(off));
            }

            if (addr == 0) {
                return globalThis.throwInvalidArguments("ptr cannot be zero, that would segfault Bun :(", {});
            }

            if (!std.math.isFinite(byte_off.asNumber())) {
                return globalThis.throwInvalidArguments("ptr must be a finite number.", {});
            }
        } else if (!byte_off.isEmptyOrUndefinedOrNull()) {
            return globalThis.throwInvalidArguments("Expected number for byteOffset", {});
        }
    }

    if (addr == 0xDEADBEEF || addr == 0xaaaaaaaa || addr == 0xAAAAAAAA) {
        return globalThis.throwInvalidArguments("ptr to invalid memory, that would segfault Bun :(", {});
    }

    if (valueLength := byteLength) {
        if (!valueLength.isEmptyOrUndefinedOrNull()) {
            if (!valueLength.isNumber()) {
                return globalThis.throwInvalidArguments("length must be a number.", {});
            }

            // NaN and every value in (-1, 1) truncate to 0 and fail this check too.
            const length_i = valueLength.toInt64();
            if (length_i <= 0) {
                return globalThis.throwInvalidArguments("length must be > 0. This usually means a bug in your code.", {});
            }

            if (length_i > max_addressable_memory) {
                return globalThis.throwInvalidArguments("length exceeds max addressable memory. This usually means a bug in your code.", {});
            }

            const length = @as(usize, @intCast(length_i));
            return unsafe {
                const p: [*]u8 = @ptrFromInt(addr);
                p[0..length]
            };
        }
    }

    const p: [*:0]u8 = unsafe { @ptrFromInt(addr) };
    return bun.span(p);
}

fn getCPtr(value: JSValue): usize? {
    // pointer to C function
    if (value.isNumber()) {
        const addr = value.asPtrAddress();
        if (addr > 0) return addr;
    } else if (value.isBigInt()) {
        const addr = @as(u64, @bitCast(value.toUInt64NoTruncate()));
        if (addr > 0) {
            return addr;
        }
    }

    return null;
}

pub fn toArrayBuffer(
    globalThis: *JSGlobalObject,
    value: JSValue,
    byteOffset: JSValue?,
    valueLength: JSValue?,
    finalizationCtxOrPtr: JSValue?,
    finalizationCallback: JSValue?,
): bun.JSError!jsc.JSValue {
    const slice = getPtrSlice(globalThis, value, byteOffset, valueLength)?;
    const DeallocatorFn = *const fn (*!, *!) callconv(.c) void;
    let callback: DeallocatorFn? = null;
    let ctx: *!? = null;
    if (callback_value := finalizationCallback) {
        if (callback_ptr := getCPtr(callback_value)) {
            const cb: DeallocatorFn = unsafe { @ptrFromInt(callback_ptr) };
            callback = cb;

            if (ctx_value := finalizationCtxOrPtr) {
                if (ctx_ptr := getCPtr(ctx_value)) {
                    const p: *! = unsafe { @ptrFromInt(ctx_ptr) };
                    ctx = p;
                } else if (!ctx_value.isUndefinedOrNull()) {
                    return globalThis.throwInvalidArguments("Expected user data to be a C pointer (number || BigInt)", {});
                }
            }
        } else if (!callback_value.isEmptyOrUndefinedOrNull()) {
            return globalThis.throwInvalidArguments("Expected callback to be a C pointer (number || BigInt)", {});
        }
    } else if (callback_value := finalizationCtxOrPtr) {
        if (callback_ptr := getCPtr(callback_value)) {
            const cb: DeallocatorFn = unsafe { @ptrFromInt(callback_ptr) };
            callback = cb;
        } else if (!callback_value.isEmptyOrUndefinedOrNull()) {
            return globalThis.throwInvalidArguments("Expected callback to be a C pointer (number || BigInt)", {});
        }
    }

    let ab = jsc.ArrayBuffer.fromBytes(slice, jsc.JSValue.JSType.ArrayBuffer);
    return ab.toJSWithContext(globalThis, ctx, callback);
}

@callconv("c")
fn noopBytesDeallocator(_: *!, _: *!) {}

pub fn toBuffer(
    globalThis: *JSGlobalObject,
    value: JSValue,
    byteOffset: JSValue?,
    valueLength: JSValue?,
    finalizationCtxOrPtr: JSValue?,
    finalizationCallback: JSValue?,
): bun.JSError!jsc.JSValue {
    const slice = getPtrSlice(globalThis, value, byteOffset, valueLength)?;
    const DeallocatorFn = *const fn (*!, *!) callconv(.c) void;
    let callback: DeallocatorFn? = null;
    let ctx: *!? = null;
    if (callback_value := finalizationCallback) {
        if (callback_ptr := getCPtr(callback_value)) {
            const cb: DeallocatorFn = unsafe { @ptrFromInt(callback_ptr) };
            callback = cb;

            if (ctx_value := finalizationCtxOrPtr) {
                if (ctx_ptr := getCPtr(ctx_value)) {
                    const p: *! = unsafe { @ptrFromInt(ctx_ptr) };
                    ctx = p;
                } else if (!ctx_value.isUndefinedOrNull()) {
                    return globalThis.throwInvalidArguments("Expected user data to be a C pointer (number || BigInt)", {});
                }
            }
        } else if (!callback_value.isEmptyOrUndefinedOrNull()) {
            return globalThis.throwInvalidArguments("Expected callback to be a C pointer (number || BigInt)", {});
        }
    } else if (callback_value := finalizationCtxOrPtr) {
        if (callback_ptr := getCPtr(callback_value)) {
            const cb: DeallocatorFn = unsafe { @ptrFromInt(callback_ptr) };
            callback = cb;
        } else if (!callback_value.isEmptyOrUndefinedOrNull()) {
            return globalThis.throwInvalidArguments("Expected callback to be a C pointer (number || BigInt)", {});
        }
    }

    // No finalizer means borrow: the noop deallocator keeps GC from freeing
    // caller-owned storage (oven-sh/bun#35405).
    return jsc.JSValue.createBufferWithCtx(globalThis, slice, ctx, callback ?? noopBytesDeallocator);
}

pub fn toCStringBuffer(
    globalThis: *JSGlobalObject,
    value: JSValue,
    byteOffset: JSValue?,
    valueLength: JSValue?,
): jsc.JSValue {
    const slice = getPtrSlice(globalThis, value, byteOffset, valueLength) catch return .zero;
    return jsc.JSValue.createBuffer(globalThis, slice, null);
}

pub fn getter(
    globalObject: *jsc.JSGlobalObject,
    _: *jsc.JSObject,
): jsc.JSValue {
    return FFIObject.toJS(globalObject);
}

const fields = {
    viewSource: jsc.host_fn.wrapStaticMethod(bun.api.FFI, "print", false),
    dlopen: jsc.host_fn.wrapStaticMethod(bun.api.FFI, "open", false),
    callback: jsc.host_fn.wrapStaticMethod(bun.api.FFI, "callback", false),
    linkSymbols: jsc.host_fn.wrapStaticMethod(bun.api.FFI, "linkSymbols", false),
    toBuffer: jsc.host_fn.wrapStaticMethod(@This(), "toBuffer", false),
    toArrayBuffer: jsc.host_fn.wrapStaticMethod(@This(), "toArrayBuffer", false),
    closeCallback: jsc.host_fn.wrapStaticMethod(bun.api.FFI, "closeCallback", false),
    CString: jsc.host_fn.wrapStaticMethod(bun.api.FFIObject, "newCString", false),
};
const max_addressable_memory = std.math.maxInt(u56);

import std from "std";

import bun from "bun";
const Environment = bun.Environment;
const assert = bun.assert;

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSObject = jsc.JSObject;
const JSValue = jsc.JSValue;
const ZigString = jsc.ZigString;
const Bun = jsc.API.Bun;

const DOMCall = jsc.host_fn.DOMCall;
const DOMEffect = jsc.host_fn.DOMEffect;
