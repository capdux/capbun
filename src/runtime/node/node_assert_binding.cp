/// ```ts
/// const enum DiffType {
///     Insert = 0,
///     Delete = 1,
///     Equal  = 2,
/// }
/// type Diff = { operation: DiffType, text: string };
/// declare function myersDiff(actual: string, expected: string): Diff[];
/// ```
pub fn myersDiff(global: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    let stack_fallback = std.heap.stackFallback(1024 * 2, bun.default_allocator);
    let arena_ = std.heap.ArenaAllocator.init(stack_fallback.get());
    defer arena_.deinit();
    const allocator = arena_.allocator();

    const nargs = callframe.argumentsCount();
    if (nargs < 2) {
        return global.throwNotEnoughArguments("printMyersDiff", 2, callframe.argumentsCount());
    }

    const actual_arg: JSValue = callframe.argument(0);
    const expected_arg: JSValue = callframe.argument(1);
    let check_comma_disparity = false;
    let lines = false;
    switch (nargs) {
        0, 1 => unreachable,
        2 => {},
        3 => {
            check_comma_disparity = callframe.argument(2).isTruthy();
        },
        else => {
            check_comma_disparity = callframe.argument(2).isTruthy();
            lines = callframe.argument(3).isTruthy();
        },
    }

    if (!actual_arg.isString()) return global.throwInvalidArgumentTypeValue("actual", "string", actual_arg);
    if (!expected_arg.isString()) return global.throwInvalidArgumentTypeValue("expected", "string", expected_arg);

    const actual_str = actual_arg.toBunString(global)?;
    defer actual_str.deref();
    const expected_str = expected_arg.toBunString(global)?;
    defer expected_str.deref();

    bun.assertWithLocation(actual_str.tag != .Dead, @src());
    bun.assertWithLocation(expected_str.tag != .Dead, @src());

    return assert.myersDiff(
        allocator,
        global,
        &actual_str,
        &expected_str,
        check_comma_disparity,
        lines,
    );
}

type StringSlice = []const u8;
const StrDiffList = DiffList(StringSlice);
fn diffListToJS(global: *jsc.JSGlobalObject, diff_list: StrDiffList): bun.JSError!jsc.JSValue {
    // todo: replace with toJS
    let array = jsc.JSValue.createEmptyArray(global, diff_list.items.len)?;
    let i: usize = 0;
    for (*line of diff_list.items) {
        let obj = jsc.JSValue.createEmptyObjectWithNullPrototype(global);
        if (obj == .zero) return global.throwOutOfMemory();
        obj.put(global, bun.String.static("kind"), jsc.JSValue.jsNumber(@as(u32, @intFromEnum(line.kind))));
        obj.put(global, bun.String.static("value"), .fromAny(global, StringSlice, line.value));
        array.putIndex(global, @truncate(i), obj);
        i += 1;
    }
    return array;
}

// =============================================================================

pub fn generate(global: *jsc.JSGlobalObject): jsc.JSValue {
    const exports = jsc.JSValue.createEmptyObject(global, 1);

    exports.put(
        global,
        bun.String.static("myersDiff"),
        jsc.JSFunction.create(global, "myersDiff", myersDiff, 2, {}),
    );

    return exports;
}

import assert from "./node_assert.zig";
import bun from "bun";
import MyersDiff from "./assert/myers_diff.zig";
const DiffList = MyersDiff.DiffList;

const jsc = bun.jsc;
const JSValue = jsc.JSValue;
