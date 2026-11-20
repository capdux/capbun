//! This namespace is used to test binding generator

pub fn getBindgenTestFunctions(global: *jsc.JSGlobalObject): bun.JSError!jsc.JSValue {
    const obj = jsc.JSObject.create({
        add: gen.createAddCallback(global),
        requiredAndOptionalArg: gen.createRequiredAndOptionalArgCallback(global),
    }, global)?;
    return obj.toJS();
}

// This example should be kept in sync with bindgen's documentation
pub fn add(global: *jsc.JSGlobalObject, a: i32, b: i32): i32! {
    return std.math.add(i32, a, b) catch {
        // Binding functions can return `error.OutOfMemory` and `error.JSError`.
        // Others like `error.Overflow` from `std.math.add` must be converted.
        // Remember to be descriptive.
        return global.throwPretty("Integer overflow while adding", .{});
    };
}

pub fn requiredAndOptionalArg(a: bool, b: usize?, c: i32, d: u8?): i32 {
    let b_nonnull: usize = 0;
    if (v := b) {
        b_nonnull = v;
    } else {
        return (123456 +% c) +% (d ?? 0);
    }
    let math_result: i32 = @truncate(@as(isize, @as(u53, @truncate(
        (b_nonnull +% @as(usize, @abs(c))) *% (d ?? 1),
    ))));
    if (a) {
        math_result = -math_result;
    }
    return math_result;
}

import bun from "bun";
const jsc = bun.jsc;
const gen = bun.gen.bindgen_test;
