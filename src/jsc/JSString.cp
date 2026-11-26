@c {
    fn JSC__JSString__toObject(str: *JSString, global: *JSGlobalObject): (*JSObject)?;
    fn JSC__JSString__toZigString(str: *JSString, global: *JSGlobalObject, zig_str: *jsc.ZigString);
    fn JSC__JSString__eql(str: *const JSString, global: *JSGlobalObject, other: *JSString): bool;
    fn JSC__JSString__iterator(str: *JSString, globalObject: *JSGlobalObject, iter: *!);
    fn JSC__JSString__length(str: *const JSString): usize;
    fn JSC__JSString__is8Bit(str: *const JSString): bool;
}

pub type _JStringIteratorAppend8Callback = @c fn (iter: *_Iterator, ptr: [*]const u8, len: u32) void;
pub type _JStringIteratorAppend16Callback = @c fn (iter: *_Iterator, ptr: [*]const u16, len: u32) void;
pub type _JStringIteratorWrite8Callback = @c fn (iter: *_Iterator, ptr: [*]const u8, a: u32, b: u32) void;
pub type _JStringIteratorWrite16Callback = @c fn (iter: *_Iterator, ptr: [*]const u16, a: u32, b: u32) void;

@c pub struct _Iterator {
    data: *!?,
    stop: u8,
    append8: (_JStringIteratorAppend8Callback)?,
    append16: (_JStringIteratorAppend16Callback)?,
    write8: (_JStringIteratorWrite8Callback)?,
    write16: (_JStringIteratorWrite16Callback)?,
}

@opaque pub struct JSString {
    pub const Iterator = _Iterator;
    pub const JStringIteratorAppend8Callback = _JStringIteratorAppend8Callback;
    pub const JStringIteratorAppend16Callback = _JStringIteratorAppend16Callback;
    pub const JStringIteratorWrite8Callback = _JStringIteratorWrite8Callback;
    pub const JStringIteratorWrite16Callback = _JStringIteratorWrite16Callback;

    static fn toJS(str: *JSString): JSValue {
        return JSValue.fromCell(str);
    }

    fn toObject(*_, global: *JSGlobalObject): (*JSObject)? {
        return JSC__JSString__toObject(_, global);
    }

    fn toZigString(*_, global: *JSGlobalObject, zig_str: *jsc.ZigString) {
        JSC__JSString__toZigString(_, global, zig_str);
    }

    fn ensureStillAlive(*_) {
        std.mem.doNotOptimizeAway(_);
    }

    fn getZigString(*_, global: *JSGlobalObject): jsc.ZigString {
        let out = jsc.ZigString.init("");
        _.toZigString(global, &out);
        return out;
    }

    pub const view = getZigString;

    /// doesn't always allocate
    fn toSlice(*_, global: *JSGlobalObject, allocator: std.mem.Allocator): ZigString.Slice {
        let str = ZigString.init("");
        _.toZigString(global, &str);
        return str.toSlice(allocator);
    }

    fn toSliceClone(*_, global: *JSGlobalObject, allocator: std.mem.Allocator): JSError!ZigString.Slice {
        let str = ZigString.init("");
        _.toZigString(global, &str);
        if (global.hasException()) return error.JSError;
        return str.toSliceClone(allocator);
    }

    fn toSliceZ(*_, global: *JSGlobalObject, allocator: std.mem.Allocator): ZigString.Slice {
        let str = ZigString.init("");
        _.toZigString(global, &str);
        return str.toSliceZ(allocator);
    }

    fn eql(*_, global: *JSGlobalObject, other: *JSString): bool {
        return JSC__JSString__eql(_, global, other);
    }

    fn iterator(*_, globalObject: *JSGlobalObject, iter: *!) {
        JSC__JSString__iterator(_, globalObject, iter);
    }

    fn length(*_): usize {
        return JSC__JSString__length(_);
    }

    fn is8Bit(*_): bool {
        return JSC__JSString__is8Bit(_);
    }
}

const JSObject = bun.jsc.JSObject;
const ZigString = bun.jsc.ZigString;

import bun from "bun";
const JSError = bun.JSError;

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
