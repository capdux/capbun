pub fn toMatchInlineSnapshot(client: *Expect, globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
    defer client.postMatch(globalThis);
    const thisValue = callFrame.this();
    const _arguments = callFrame.arguments_old(2);
    const arguments: []const JSValue = unsafe { _arguments.ptr[0.._arguments.len] };

    client.incrementExpectCallCounter();

    const not = client.flags.not;
    if (not) {
        const signature = comptime getSignature("toMatchInlineSnapshot", "", true);
        return client.throw(globalThis, signature, "\n\n<b>Matcher error<r>: Snapshot matchers cannot be used with <b>not<r>\n", {});
    }

    let has_expected = false;
    let expected_string: ZigString = ZigString.Empty;
    let property_matchers: JSValue? = null;
    switch (arguments.len) {
        0 => {},
        1 => {
            if (arguments[0].isString()) {
                has_expected = true;
                arguments[0].toZigString(&expected_string, globalThis)?;
            } else if (arguments[0].isObject()) {
                property_matchers = arguments[0];
            } else {
                return client.throw(globalThis, "", "\n\nMatcher error: Expected first argument to be a string or object\n", {});
            }
        },
        else => {
            if (!arguments[0].isObject()) {
                const signature = comptime getSignature("toMatchInlineSnapshot", "<green>properties<r><d>, <r>hint", false);
                return client.throw(globalThis, signature, "\n\nMatcher error: Expected <green>properties<r> must be an object\n", {});
            }

            property_matchers = arguments[0];

            if (arguments[1].isString()) {
                has_expected = true;
                arguments[1].toZigString(&expected_string, globalThis)?;
            }
        },
    }

    let expected = expected_string.toSlice(default_allocator);
    const _expected_guard = DeinitGuard<@TypeOf(expected)>{ ptr: unsafe { &expected } };

    const expected_slice: ([]const u8)? = if (has_expected) expected.slice() else null;

    const value = client.getValue(globalThis, thisValue, "toMatchInlineSnapshot", "<green>properties<r><d>, <r>hint")?;
    return client.inlineSnapshot(globalThis, callFrame, value, property_matchers, expected_slice, "toMatchInlineSnapshot");
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
