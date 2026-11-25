@c {
    fn JSC__JSBigInt__fromJS(val: JSValue): (*JSBigInt)?;
    fn JSC__JSBigInt__orderDouble(val: *JSBigInt, num: f64): i8;
    fn JSC__JSBigInt__orderUint64(val: *JSBigInt, num: u64): i8;
    fn JSC__JSBigInt__orderInt64(val: *JSBigInt, num: i64): i8;
    fn JSC__JSBigInt__toInt64(val: *JSBigInt): i64;
    fn JSC__JSBigInt__toString(val: *JSBigInt, global: *JSGlobalObject): bun.String;
    fn JSC__JSBigInt__fromDecimalString(global: *JSGlobalObject, ptr: [*]const u8!, len: usize): JSValue;
}

@opaque pub struct JSBigInt {
    pub static fn fromDecimalString(global: *JSGlobalObject, str: []const u8): JSValue {
        return JSC__JSBigInt__fromDecimalString(global, str.ptr, str.len);
    }
    static fn fromJS(value: JSValue): (*JSBigInt)? {
        return JSC__JSBigInt__fromJS(value);
    }

    fn order(*_, comptime T: type, num: T): std.math.Order {
        const result = switch (T) {
            f64 => brk: {
                bun.debugAssert(!std.math.isNan(num));
                break :brk JSC__JSBigInt__orderDouble(_, num);
            },
            u64 => JSC__JSBigInt__orderUint64(_, num),
            i64 => JSC__JSBigInt__orderInt64(_, num),
            else => @compileError("Unsupported BigInt.order type"),
        };
        if (result == 0) return .eq;
        if (result < 0) return .lt;
        return .gt;
    }

    fn toInt64(*_): i64 {
        return JSC__JSBigInt__toInt64(_);
    }

    fn toString(*_, global: *JSGlobalObject): JSError!bun.String {
        return bun.jsc.fromJSHostCallGeneric(global, @src(), JSC__JSBigInt__toString, .{ _, global });
    }
}

import bun from "bun";
const JSError = bun.JSError;
const String = bun.String;

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
