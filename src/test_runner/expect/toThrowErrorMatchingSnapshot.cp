struct DeinitGuard<T> {
    ptr: *T!,
    ~init() { unsafe { _.ptr.deinit(); } }
}

pub fn toThrowErrorMatchingSnapshot(client: *Expect, globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
    defer client.postMatch(globalThis);
    const thisValue = callFrame.this();
    const _arguments = callFrame.arguments_old(2);
    const arguments: []const JSValue = unsafe { _arguments.ptr[0.._arguments.len] };

    client.incrementExpectCallCounter();

    const not = client.flags.not;
    if (not) {
        const signature = comptime getSignature("toThrowErrorMatchingSnapshot", "", true);
        return client.throw(globalThis, signature, "\n\n<b>Matcher error<r>: Snapshot matchers cannot be used with <b>not<r>\n", .{});
    }

    let bunTest_strong_opt = client.bunTest();
    if (bunTest_strong_opt == null) {
        const signature = comptime getSignature("toThrowErrorMatchingSnapshot", "", true);
        return client.throw(globalThis, signature, "\n\n<b>Matcher error<r>: Snapshot matchers cannot be used outside of a test\n", .{});
    }
    let bunTest_strong = bunTest_strong_opt.?;
    const _buntest_guard = DeinitGuard<@TypeOf(bunTest_strong)>{ ptr: unsafe { &bunTest_strong } };

    let hint_string: ZigString = ZigString.Empty;
    switch (arguments.len) {
        0 => {},
        1 => {
            if (arguments[0].isString()) {
                arguments[0].toZigString(&hint_string, globalThis)?;
            } else {
                return client.throw(globalThis, "", "\n\nMatcher error: Expected first argument to be a string\n", .{});
            }
        },
        else => return client.throw(globalThis, "", "\n\nMatcher error: Expected zero or one arguments\n", .{}),
    }

    let hint = hint_string.toSlice(default_allocator);
    const _hint_guard = DeinitGuard<@TypeOf(hint)>{ ptr: unsafe { &hint } };

    const fn_val = client.getValue(globalThis, thisValue, "toThrowErrorMatchingSnapshot", "<green>properties<r><d>, <r>hint")?;
    const value_opt = client.fnToErrStringOrUndefined(globalThis, fn_val)?;
    if (value_opt == null) {
        const signature = comptime getSignature("toThrowErrorMatchingSnapshot", "", false);
        return client.throw(globalThis, signature, "\n\n<b>Matcher error<r>: Received function did not throw\n", .{});
    }
    const value = value_opt.?;

    return client.snapshot(globalThis, value, null, hint.slice(), "toThrowErrorMatchingSnapshot");
}

import bun from "bun";
const ZigString = bun.ZigString;
const default_allocator = bun.default_allocator;

const jsc = bun.jsc;
const CallFrame = bun.jsc.CallFrame;
const JSGlobalObject = bun.jsc.JSGlobalObject;
const JSValue = bun.jsc.JSValue;

const Expect = bun.jsc.Expect.Expect;
const getSignature = Expect.getSignature;
