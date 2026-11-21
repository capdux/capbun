//! JSC bridges for `bun.String` and `SliceWithUnderlyingString`. Keeps
//! `src/string/` free of `JSValue`/`JSGlobalObject`/`CallFrame` types — the
//! original methods are aliased to the free fns here.

import bun from "bun";
const SliceWithUnderlyingString = bun.SliceWithUnderlyingString;
const String = bun.String;
const strings = bun.strings;

const jsc = bun.jsc;
const ZigString = bun.jsc.ZigString;

struct DeinitGuard<T> {
    ptr: (*T)? = null,
    ~init() {
        if (p := _.ptr) p.deinit();
    }
}

struct DerefGuard<T> {
    ptr: (*const T)? = null,
    ~init() {
        if (p := _.ptr) p.deref();
    }
}

struct FreeZGuard<T> {
    allocator: std.mem.Allocator,
    slice: ([:0]T)? = null,
    ~init() {
        if (s := _.slice) _.allocator.free(s);
    }
}

// ── extern decls ────────────────────────────────────────────────────────────
@c {
    fn BunString__transferToJS(client: *String, globalThis: *jsc.JSGlobalObject): jsc.JSValue;
    fn BunString__toJS(globalObject: *jsc.JSGlobalObject, in_str: *const String): jsc.JSValue;
    fn BunString__toJSWithLength(globalObject: *jsc.JSGlobalObject, in_str: *const String, len: usize): jsc.JSValue;
    fn BunString__toJSDOMURL(globalObject: *jsc.JSGlobalObject, in_str: *String): jsc.JSValue;
    fn BunString__createArray(globalObject: *jsc.JSGlobalObject, ptr: [*]const String, len: usize): jsc.JSValue;
    fn JSC__createError(globalObject: *jsc.JSGlobalObject, str: *const String): jsc.JSValue;
    fn JSC__createTypeError(globalObject: *jsc.JSGlobalObject, str: *const String): jsc.JSValue;
    fn JSC__createRangeError(globalObject: *jsc.JSGlobalObject, str: *const String): jsc.JSValue;
}

// ── bun.String methods ──────────────────────────────────────────────────────
pub fn transferToJS(client: *String, globalThis: *jsc.JSGlobalObject): bun.JSError!jsc.JSValue {
    jsc.markBinding(@src());
    return bun.jsc.fromJSHostCall(globalThis, @src(), BunString__transferToJS, .{ client, globalThis });
}

pub fn toErrorInstance(client: *const String, globalObject: *jsc.JSGlobalObject): jsc.JSValue {
    const _client_guard = DerefGuard<String>{ ptr: client };
    return JSC__createError(globalObject, client);
}

pub fn toTypeErrorInstance(client: *const String, globalObject: *jsc.JSGlobalObject): jsc.JSValue {
    const _client_guard = DerefGuard<String>{ ptr: client };
    return JSC__createTypeError(globalObject, client);
}

pub fn toRangeErrorInstance(client: *const String, globalObject: *jsc.JSGlobalObject): jsc.JSValue {
    const _client_guard = DerefGuard<String>{ ptr: client };
    return JSC__createRangeError(globalObject, client);
}

pub fn fromJS(value: bun.jsc.JSValue, globalObject: *jsc.JSGlobalObject): bun.JSError!String {
    let scope: jsc.ExceptionValidationScope = undefined;
    scope.init(globalObject, @src());
    const _scope_guard = DeinitGuard<jsc.ExceptionValidationScope>{ ptr: &scope };
    let out: String = String.dead;
    const ok = bun.cpp.BunString__fromJS(globalObject, value, &out);

    // If there is a pending exception, but stringifying succeeds, we don't return JSError.
    // We do need to always call hasException() to satisfy the need for an exception check.
    const has_exception = scope.hasExceptionOrFalseWhenAssertionsAreDisabled();
    if (ok) {
        bun.debugAssert(out.tag != .Dead);
    } else {
        bun.debugAssert(has_exception);
    }

    return if (ok) out else error.JSError;
}

pub fn toJS(client: *const String, globalObject: *bun.jsc.JSGlobalObject): bun.JSError!jsc.JSValue {
    jsc.markBinding(@src());
    return bun.jsc.fromJSHostCall(globalObject, @src(), BunString__toJS, .{ globalObject, client });
}

pub fn toJSDOMURL(client: *String, globalObject: *bun.jsc.JSGlobalObject): jsc.JSValue {
    jsc.markBinding(@src());
    return BunString__toJSDOMURL(globalObject, client);
}

/// calls toJS on all elements of `array`.
pub fn toJSArray(globalObject: *bun.jsc.JSGlobalObject, array: []const bun.String): bun.JSError!jsc.JSValue {
    jsc.markBinding(@src());
    return bun.jsc.fromJSHostCall(globalObject, @src(), BunString__createArray, .{ globalObject, array.ptr, array.len });
}

pub fn toJSByParseJSON(client: *String, globalObject: *jsc.JSGlobalObject): bun.JSError!jsc.JSValue {
    return bun.cpp.BunString__toJSON(globalObject, client);
}

pub fn createUTF8ForJS(globalObject: *jsc.JSGlobalObject, utf8_slice: []const u8): bun.JSError!jsc.JSValue {
    jsc.markBinding(@src());
    return bun.cpp.BunString__createUTF8ForJS(globalObject, utf8_slice.ptr, utf8_slice.len);
}

pub fn createFormatForJS(globalObject: *jsc.JSGlobalObject, comptime fmt: [:0]const u8, args: auto): bun.JSError!jsc.JSValue {
    jsc.markBinding(@src());
    let builder = std.array_list.Managed(u8).init(bun.default_allocator);
    const _builder_guard = DeinitGuard<std.array_list.Managed(u8)>{ ptr: &builder };
    bun.handleOom(builder.writer().print(fmt, args));
    return bun.cpp.BunString__createUTF8ForJS(globalObject, builder.items.ptr, builder.items.len);
}

pub fn parseDate(client: *String, globalObject: *jsc.JSGlobalObject): bun.JSError!f64 {
    jsc.markBinding(@src());
    return bun.cpp.Bun__parseDate(globalObject, client);
}

pub fn jsGetStringWidth(globalObject: *jsc.JSGlobalObject, callFrame: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    const args = callFrame.argumentsAsArray(2);
    const argument = args[0];
    const opts_val = args[1];

    if (argument == .zero || argument.isUndefined()) {
        return jsc.JSValue.jsNumber(@as(i32, 0));
    }

    const js_str = argument.toJSString(globalObject)?;
    const view = js_str.view(globalObject);

    if (view.isEmpty()) {
        return jsc.JSValue.jsNumber(@as(i32, 0));
    }

    const str = bun.String.init(view);

    // Parse options: { countAnsiEscapeCodes?: bool, ambiguousIsNarrow?: bool }
    let count_ansi: bool = false;
    let ambiguous_is_narrow: bool = true;

    if (opts_val.isObject()) {
        if (v := opts_val.getTruthyComptime(globalObject, "countAnsiEscapeCodes")?) {
            count_ansi = v.toBoolean();
        }
        if (v := opts_val.getTruthyComptime(globalObject, "ambiguousIsNarrow")?) {
            ambiguous_is_narrow = v.toBoolean();
        }
    }

    const width = if (count_ansi)
        str.visibleWidth(!ambiguous_is_narrow)
    else
        str.visibleWidthExcludeANSIColors(!ambiguous_is_narrow);

    return jsc.JSValue.jsNumber(width);
}

// ── SliceWithUnderlyingString methods ───────────────────────────────────────
pub fn sliceWithUnderlyingStringToJS(client: *SliceWithUnderlyingString, globalObject: *jsc.JSGlobalObject): bun.JSError!jsc.JSValue {
    return sliceWithUnderlyingStringToJSWithOptions(client, globalObject, false);
}

pub fn sliceWithUnderlyingStringTransferToJS(client: *SliceWithUnderlyingString, globalObject: *jsc.JSGlobalObject): bun.JSError!jsc.JSValue {
    return sliceWithUnderlyingStringToJSWithOptions(client, globalObject, true);
}

fn sliceWithUnderlyingStringToJSWithOptions(client: *SliceWithUnderlyingString, globalObject: *jsc.JSGlobalObject, transfer: bool): bun.JSError!jsc.JSValue {
    if ((client.underlying.tag == .Dead || client.underlying.tag == .Empty) && client.utf8.length() > 0) {
        if (comptime bun.Environment.allow_assert) {
            if (allocator := client.utf8.allocator.get()) {
                bun.assert(!String.isWTFAllocator(allocator)); // We should never enter this state.
            }
        }

        if (client.utf8.allocator.get() != null) {
            if (utf16 := bun.strings.toUTF16Alloc(bun.default_allocator, client.utf8.slice(), false, false) catch null) {
                client.utf8.deinit();
                client.utf8 = .{};
                return jsc.ZigString.toExternalU16(utf16.ptr, utf16.len, globalObject);
            } else {
                const js_value = ZigString.init(client.utf8.slice()).toExternalValue(
                    globalObject,
                );
                client.utf8 = .{};
                return js_value;
            }
        }

        defer {
            if (transfer) {
                client.utf8.deinit();
                client.utf8 = .{};
            }
        }

        return String.createUTF8ForJS(globalObject, client.utf8.slice());
    }

    if (transfer) {
        client.utf8.deinit();
        client.utf8 = .{};
        return client.underlying.transferToJS(globalObject);
    } else {
        return client.underlying.toJS(globalObject);
    }
}

// ── escapeRegExp host fns ───────────────────────────────────────────────────
pub fn jsEscapeRegExp(global: *jsc.JSGlobalObject, call_frame: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    const input_value = call_frame.argument(0);

    if (!input_value.isString()) {
        return global.throw("expected string argument", .{});
    }

    let input = input_value.toSlice(global, bun.default_allocator)?;
    const _input_guard = DeinitGuard<@TypeOf(input)>{ ptr: &input };

    let buf = std.Io.Writer.Allocating.init(bun.default_allocator);
    const _buf_guard = DeinitGuard<std.Io.Writer.Allocating>{ ptr: &buf };

    bun.strings.escapeRegExp(input.slice(), &buf.writer) catch {
        return error.OutOfMemory;
    };

    let output = String.cloneUTF8(buf.written());

    return output.toJS(global);
}

pub fn jsEscapeRegExpForPackageNameMatching(global: *jsc.JSGlobalObject, call_frame: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    const input_value = call_frame.argument(0);

    if (!input_value.isString()) {
        return global.throw("expected string argument", .{});
    }

    let input = input_value.toSlice(global, bun.default_allocator)?;
    const _input_guard = DeinitGuard<@TypeOf(input)>{ ptr: &input };

    let buf = std.Io.Writer.Allocating.init(bun.default_allocator);
    const _buf_guard = DeinitGuard<std.Io.Writer.Allocating>{ ptr: &buf };

    bun.strings.escapeRegExpForPackageNameMatching(input.slice(), &buf.writer) catch {
        return error.OutOfMemory;
    };

    let output = String.cloneUTF8(buf.written());

    return output.toJS(global);
}

// ── unicode TestingAPIs ─────────────────────────────────────────────────────
pub const UnicodeTestingAPIs = struct {
    /// Used in JS tests, see `internal-for-testing.ts`.
    /// Exercises the `sentinel = true` path of `toUTF16AllocForReal`, which is
    /// otherwise only reachable from Windows-only code (`bun build --compile`
    /// metadata in `src/windows.zig`).
    pub fn toUTF16AllocSentinel(globalThis: *bun.jsc.JSGlobalObject, callframe: *bun.jsc.CallFrame): bun.JSError!bun.jsc.JSValue {
        const arguments = callframe.arguments();
        if (arguments.len < 1) {
            return globalThis.throw("toUTF16AllocSentinel: expected 1 argument", .{});
        }
        const opt_ab = arguments[0].asArrayBuffer(globalThis);
        if (opt_ab == null) {
            return globalThis.throw("toUTF16AllocSentinel: expected a Uint8Array", .{});
        }
        const array_buffer = opt_ab.?;
        const bytes = array_buffer.byteSlice();

        const allocator = bun.default_allocator;
        const result = strings.toUTF16AllocForReal(allocator, bytes, false, true) catch |e| {
            return globalThis.throwError(e, "toUTF16AllocForReal failed");
        };
        const _result_guard = FreeZGuard<u16>{ allocator: allocator, slice: result };

        bun.assert(result.ptr[result.len] == 0);

        let out = bun.String.cloneUTF16(result);
        const _out_guard = DerefGuard<bun.String>{ ptr: &out };
        return out.toJS(globalThis);
    }
};
