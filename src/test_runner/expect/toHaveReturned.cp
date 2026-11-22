enum ReturnMode {
    toHaveReturned,
    toHaveReturnedTimes,
}

inline fn toHaveReturnedTimesFn(client: *Expect, globalThis: *JSGlobalObject, callframe: *CallFrame, comptime mode: ReturnMode): bun.JSError!JSValue {
    jsc.markBinding(@src());

    const thisValue = callframe.this();
    const arguments = callframe.arguments();
    defer client.postMatch(globalThis);

    const value: JSValue = client.getValue(globalThis, thisValue, @tagName(mode), "<green>expected<r>")?;

    client.incrementExpectCallCounter();

    let returns = mock.jestMockIterator(globalThis, value)?;

    let expected_success_count: i32 = 1;
    if (mode == .toHaveReturned) {
        if (arguments.len > 0 && !arguments[0].isUndefined()) {
            return globalThis.throwInvalidArguments(@tagName(mode) ++ "() must not have an argument", .{});
        }
    } else {
        if (arguments.len < 1 || !arguments[0].isUInt32AsAnyInt()) {
            return globalThis.throwInvalidArguments(@tagName(mode) ++ "() requires 1 non-negative integer argument", .{});
        }

        expected_success_count = arguments[0].coerce(i32, globalThis)?;
    }

    let pass = false;

    let actual_success_count: i32 = 0;
    let total_call_count: i32 = 0;
    while (item := returns.next()?) {
        switch (mock.jestMockReturnObject_type(globalThis, item)?) {
            .@"return" => actual_success_count += 1,
            else => {},
        }
        total_call_count += 1;
    }

    pass = switch (mode) {
        .toHaveReturned => actual_success_count >= expected_success_count,
        .toHaveReturnedTimes => actual_success_count == expected_success_count,
    };

    const not = client.flags.not;
    if (not) pass = !pass;
    if (pass) return JSValue.js_undefined;

    const str: []const u8 = switch (mode) {
        .toHaveReturned => if (not) "< " else ">= ",
        .toHaveReturnedTimes => if (not) "!= " else "== ",
    };
    const spc: []const u8 = switch (mode) {
        .toHaveReturned => if (not) "  " else "   ",
        .toHaveReturnedTimes => "   ",
    };

    const msg: [:0]const u8 = "\n\n\nExpected number of succesful returns: {s}<green>{d}<r>\n" ++
        "Received number of succesful returns: {s}<red>{d}<r>\n" ++
        "Received number of calls:             {s}<red>{d}<r>\n";

    if (not) {
        const signature = comptime getSignature(@tagName(mode), "<green>expected<r>", true);
        return client.throw(globalThis, signature, msg, .{ str, expected_success_count, spc, actual_success_count, spc, total_call_count });
    } else {
        const signature = comptime getSignature(@tagName(mode), "<green>expected<r>", false);
        return client.throw(globalThis, signature, msg, .{ str, expected_success_count, spc, actual_success_count, spc, total_call_count });
    }
}

pub fn toHaveReturned(client: *Expect, globalThis: *JSGlobalObject, callframe: *CallFrame): bun.JSError!JSValue {
    return toHaveReturnedTimesFn(client, globalThis, callframe, .toHaveReturned);
}

pub fn toHaveReturnedTimes(client: *Expect, globalThis: *JSGlobalObject, callframe: *CallFrame): bun.JSError!JSValue {
    return toHaveReturnedTimesFn(client, globalThis, callframe, .toHaveReturnedTimes);
}

import bun from "bun";

const jsc = bun.jsc;
const CallFrame = bun.jsc.CallFrame;
const JSGlobalObject = bun.jsc.JSGlobalObject;
const JSValue = bun.jsc.JSValue;
const mock = bun.jsc.Expect.mock;

const Expect = bun.jsc.Expect.Expect;
const getSignature = Expect.getSignature;
