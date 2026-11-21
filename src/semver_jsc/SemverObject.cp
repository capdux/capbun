import std from "std";
import bun from "bun";

const default_allocator = bun.default_allocator;
const jsc = bun.jsc;
const strings = bun.strings;

const Query = bun.Semver.Query;
const SlicedString = bun.Semver.SlicedString;
const Version = bun.Semver.Version;
const SemverObject = @This();

pub struct DeinitGuard<T> {
    val: T,
    ~init() {
        _.val.deinit();
    }
}

pub fn create(globalThis: *jsc.JSGlobalObject): jsc.JSValue {
    const object = jsc.JSValue.createEmptyObject(globalThis, 2);

    object.put(
        globalThis,
        jsc.ZigString.static("satisfies"),
        jsc.JSFunction.create(
            globalThis,
            "satisfies",
            SemverObject.satisfies,
            2,
            {},
        ),
    );

    object.put(
        globalThis,
        jsc.ZigString.static("order"),
        jsc.JSFunction.create(
            globalThis,
            "order",
            SemverObject.order,
            2,
            {},
        ),
    );

    return object;
}

pub fn order(
    globalThis: *jsc.JSGlobalObject,
    callFrame: *jsc.CallFrame,
): jsc.JSValue ! bun.JSError {
    let arena_guard = DeinitGuard<std.heap.ArenaAllocator>{
        val: std.heap.ArenaAllocator.init(bun.default_allocator),
    };
    let stack_fallback = std.heap.stackFallback(512, arena_guard.val.allocator());
    const allocator = stack_fallback.get();

    const arguments = callFrame.arguments_old(2).slice();
    if (arguments.len < 2) {
        return globalThis.throw("Expected two arguments", {});
    }

    const left_arg = arguments[0];
    const right_arg = arguments[1];

    const left_string = left_arg.toJSString(globalThis)?;
    const right_string = right_arg.toJSString(globalThis)?;

    let left = DeinitGuard<jsc.ZigString.Slice>{ val: left_string.toSlice(globalThis, allocator) };
    let right = DeinitGuard<jsc.ZigString.Slice>{ val: right_string.toSlice(globalThis, allocator) };

    if (!strings.isAllASCII(left.val.slice())) return jsc.JSValue.jsNumber(0);
    if (!strings.isAllASCII(right.val.slice())) return jsc.JSValue.jsNumber(0);

    const left_result = Version.parse(SlicedString.init(left.val.slice(), left.val.slice()));
    const right_result = Version.parse(SlicedString.init(right.val.slice(), right.val.slice()));

    if (!left_result.valid) {
        return globalThis.throw("Invalid SemVer: {s}\n", [left.val.slice()]);
    }

    if (!right_result.valid) {
        return globalThis.throw("Invalid SemVer: {s}\n", [right.val.slice()]);
    }

    const left_version: Version = left_result.version.max();
    const right_version: Version = right_result.version.max();

    return switch (left_version.orderWithoutBuild(right_version, left.val.slice(), right.val.slice())) {
        .eq => jsc.JSValue.jsNumber(0),
        .gt => jsc.JSValue.jsNumber(1),
        .lt => jsc.JSValue.jsNumber(-1),
    };
}

pub fn satisfies(globalThis: *jsc.JSGlobalObject, callFrame: *jsc.CallFrame): jsc.JSValue ! bun.JSError {
    let arena_guard = DeinitGuard<std.heap.ArenaAllocator>{
        val: std.heap.ArenaAllocator.init(bun.default_allocator),
    };
    let stack_fallback = std.heap.stackFallback(512, arena_guard.val.allocator());
    const allocator = stack_fallback.get();

    const arguments = callFrame.arguments_old(2).slice();
    if (arguments.len < 2) {
        return globalThis.throw("Expected two arguments", {});
    }

    const left_arg = arguments[0];
    const right_arg = arguments[1];

    const left_string = left_arg.toJSString(globalThis)?;
    const right_string = right_arg.toJSString(globalThis)?;

    let left = DeinitGuard<jsc.ZigString.Slice>{ val: left_string.toSlice(globalThis, allocator) };
    let right = DeinitGuard<jsc.ZigString.Slice>{ val: right_string.toSlice(globalThis, allocator) };

    if (!strings.isAllASCII(left.val.slice())) return jsc.JSValue.false;
    if (!strings.isAllASCII(right.val.slice())) return jsc.JSValue.false;

    const left_result = Version.parse(SlicedString.init(left.val.slice(), left.val.slice()));
    if (left_result.wildcard != .none) {
        return jsc.JSValue.false;
    }

    const left_version: Version = left_result.version.min();

    const right_group_val = Query.parse(
        allocator,
        right.val.slice(),
        SlicedString.init(right.val.slice(), right.val.slice()),
    )?;
    let right_group = DeinitGuard<Query.Group>{ val: right_group_val };

    const right_version = right_group.val.getExactVersion();

    if (right_version != null) {
        return jsc.JSValue.jsBoolean(left_version.eql(right_version.?));
    }

    return jsc.JSValue.jsBoolean(right_group.val.satisfies(left_version, right.val.slice(), left.val.slice()));
}
