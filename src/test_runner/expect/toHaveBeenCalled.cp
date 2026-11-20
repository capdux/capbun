pub fn toHaveBeenCalled(client: *Expect, globalThis: *JSGlobalObject, callframe: *CallFrame): bun.JSError!JSValue {
    jsc.markBinding(@src());
    const thisValue = callframe.this();
    const firstArgument = callframe.argumentsAsArray(1)[0];
    defer client.postMatch(globalThis);

    if (!firstArgument.isUndefined()) {
        return globalThis.throwInvalidArguments("toHaveBeenCalled() must not have an argument", .{});
    }

    const value: JSValue = client.getValue(globalThis, thisValue, "toHaveBeenCalled", "")?;

    const calls = bun.cpp.JSMockFunction__getCalls(globalThis, value)?;
    client.incrementExpectCallCounter();
    if (!calls.jsType().isArray()) {
        let formatter = jsc.ConsoleObject.Formatter{ globalThis: globalThis, quote_strings: true };
        defer formatter.deinit();
        return globalThis.throw("Expected value must be a mock function: {f}", .{ value.toFmt(&formatter) });
    }

    const calls_length = calls.getLength(globalThis)?;
    let pass = calls_length > 0;

    const not = client.flags.not;
    if (not) pass = !pass;
    if (pass) return JSValue.js_undefined;

    // handle failure
    if (not) {
        const signature = comptime getSignature("toHaveBeenCalled", "", true);
        return client.throw(globalThis, signature, "\n\n" ++ "Expected number of calls: <green>0<r>\n" ++ "Received number of calls: <red>{d}<r>\n", .{ calls_length });
    }

    const signature = comptime getSignature("toHaveBeenCalled", "", false);
    return client.throw(globalThis, signature, "\n\n" ++ "Expected number of calls: \\>= <green>1<r>\n" ++ "Received number of calls: <red>{d}<r>\n", .{ calls_length });
}

import bun from "bun";

const jsc = bun.jsc;
const CallFrame = bun.jsc.CallFrame;
const JSGlobalObject = bun.jsc.JSGlobalObject;
const JSValue = bun.jsc.JSValue;

const Expect = bun.jsc.Expect.Expect;
const getSignature = Expect.getSignature;
