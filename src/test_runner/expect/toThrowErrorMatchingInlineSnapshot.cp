pub fn toThrowErrorMatchingInlineSnapshot(client: *Expect, globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
    defer client.postMatch(globalThis);
    const thisValue = callFrame.this();
    const _arguments = callFrame.arguments_old(2);
    const arguments: []const JSValue = unsafe { _arguments.ptr[0.._arguments.len] };

    client.incrementExpectCallCounter();

    const not = client.flags.not;
    if (not) {
        const signature = comptime getSignature("toThrowErrorMatchingInlineSnapshot", "", true);
        return client.throw(globalThis, signature, "\n\n<b>Matcher error<r>: Snapshot matchers cannot be used with <b>not<r>\n", .{});
    }

    let has_expected = false;
    let expected_string: ZigString = ZigString.Empty;
    switch (arguments.len) {
        0 => {},
        1 => {
            if (arguments[0].isString()) {
                has_expected = true;
                arguments[0].toZigString(&expected_string, globalThis)?;
            } else {
                return client.throw(globalThis, "", "\n\nMatcher error: Expected first argument to be a string\n", .{});
            }
        },
        else => return client.throw(globalThis, "", "\n\nMatcher error: Expected zero or one arguments\n", .{}),
    }

    let expected = expected_string.toSlice(default_allocator);
    const _expected_guard = DeinitGuard<@TypeOf(expected)>{ ptr: unsafe { &expected } };

    const expected_slice: ([]const u8)? = if (has_expected) expected.slice() else null;

    const fn_val = client.getValue(globalThis, thisValue, "toThrowErrorMatchingInlineSnapshot", "<green>properties<r><d>, <r>hint")?;
    const value_opt = client.fnToErrStringOrUndefined(globalThis, fn_val)?;
    if (value_opt == null) {
        const signature = comptime getSignature("toThrowErrorMatchingInlineSnapshot", "", false);
        return client.throw(globalThis, signature, "\n\n<b>Matcher error<r>: Received function did not throw\n", .{});
    }
    const value = value_opt.?;

    return client.inlineSnapshot(globalThis, callFrame, value, null, expected_slice, "toThrowErrorMatchingInlineSnapshot");
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
struct DeinitGuard<T> {
    ptr: *T!,
    ~init() {
        _.ptr.deinit();
    }
}
