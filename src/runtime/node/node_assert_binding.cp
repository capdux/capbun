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
    let arena_ = DeinitGuard<std.heap.ArenaAllocator>{ val: std.heap.ArenaAllocator.init(stack_fallback.get()) };
    const allocator = arena_.val.allocator();

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

    let actual_str = DerefGuard<bun.String>{ val: actual_arg.toBunString(global)? };
    let expected_str = DerefGuard<bun.String>{ val: expected_arg.toBunString(global)? };

    bun.assertWithLocation(actual_str.val.tag != .Dead, @src());
    bun.assertWithLocation(expected_str.val.tag != .Dead, @src());

    return assert.myersDiff(
        allocator,
        global,
        unsafe { &actual_str.val },
        unsafe { &expected_str.val },
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

pub struct DeinitGuard<T> { val: T, ~init() { _.val.deinit(); } }
pub struct DerefGuard<T> { val: T, ~init() { _.val.deref(); } }
