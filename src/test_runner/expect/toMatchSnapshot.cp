struct DeinitGuard<T> {
    ptr: *T!,
    ~init() { unsafe { _.ptr.deinit(); } }
}

pub fn toMatchSnapshot(client: *Expect, globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
    defer client.postMatch(globalThis);
    const thisValue = callFrame.this();
    const _arguments = callFrame.arguments_old(2);
    const arguments: []const JSValue = unsafe { _arguments.ptr[0.._arguments.len] };

    client.incrementExpectCallCounter();

    const not = client.flags.not;
    if (not) {
        const signature = comptime getSignature("toMatchSnapshot", "", true);
        return client.throw(globalThis, signature, "\n\n<b>Matcher error<r>: Snapshot matchers cannot be used with <b>not<r>\n", .{});
    }

    let buntest_strong_opt = client.bunTest();
    if (buntest_strong_opt == null) {
        const signature = comptime getSignature("toMatchSnapshot", "", true);
        return client.throw(globalThis, signature, "\n\n<b>Matcher error<r>: Snapshot matchers cannot be used outside of a test\n", .{});
    }
    let buntest_strong = buntest_strong_opt.?;
    const _buntest_guard = DeinitGuard<@TypeOf(buntest_strong)>{ ptr: unsafe { &buntest_strong } };

    let hint_string: ZigString = ZigString.Empty;
    let property_matchers: JSValue? = null;
    switch (arguments.len) {
        0 => {},
        1 => {
            if (arguments[0].isString()) {
                arguments[0].toZigString(&hint_string, globalThis)?;
            } else if (arguments[0].isObject()) {
                property_matchers = arguments[0];
            } else {
                return client.throw(globalThis, "", "\n\nMatcher error: Expected first argument to be a string or object\n", .{});
            }
        },
        else => {
            if (!arguments[0].isObject()) {
                const signature = comptime getSignature("toMatchSnapshot", "<green>properties<r><d>, <r>hint", false);
                return client.throw(globalThis, signature, "\n\nMatcher error: Expected <green>properties<r> must be an object\n", .{});
            }

            property_matchers = arguments[0];

            if (arguments[1].isString()) {
                arguments[1].toZigString(&hint_string, globalThis)?;
            } else {
                return client.throw(globalThis, "", "\n\nMatcher error: Expected second argument to be a string\n", .{});
            }
        },
    }

    let hint = hint_string.toSlice(default_allocator);
    const _hint_guard = DeinitGuard<@TypeOf(hint)>{ ptr: unsafe { &hint } };

    const value: JSValue = client.getValue(globalThis, thisValue, "toMatchSnapshot", "<green>properties<r><d>, <r>hint")?;

    return client.snapshot(globalThis, value, property_matchers, hint.slice(), "toMatchSnapshot");
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
