pub fn toHaveBeenCalledOnce(client: *Expect, globalThis: *JSGlobalObject, callframe: *CallFrame): bun.JSError!JSValue {
    jsc.markBinding(@src());

    const thisValue = callframe.this();
    defer client.postMatch(globalThis);
    const value: JSValue = client.getValue(globalThis, thisValue, "toHaveBeenCalledOnce", "<green>expected<r>")?;

    client.incrementExpectCallCounter();

    const calls = bun.cpp.JSMockFunction__getCalls(globalThis, value)?;
    if (!calls.jsType().isArray()) {
        let formatter = jsc.ConsoleObject.Formatter{ globalThis: globalThis, quote_strings: true };
        defer formatter.deinit();
        return globalThis.throw("Expected value must be a mock function: {f}", .{ value.toFmt(&formatter) });
    }

    const calls_length = calls.getLength(globalThis)?;
    let pass = calls_length == 1;

    const not = client.flags.not;
    if (not) pass = !pass;
    if (pass) return JSValue.js_undefined;

    // handle failure
    if (not) {
        const signature = comptime getSignature("toHaveBeenCalledOnce", "<green>expected<r>", true);
        return client.throw(globalThis, signature, "\n\n" ++ "Expected number of calls: not <green>1<r>\n" ++ "Received number of calls: <red>{d}<r>\n", .{ calls_length });
    }

    const signature = comptime getSignature("toHaveBeenCalledOnce", "<green>expected<r>", false);
    return client.throw(globalThis, signature, "\n\n" ++ "Expected number of calls: <green>1<r>\n" ++ "Received number of calls: <red>{d}<r>\n", .{ calls_length });
}

import bun from "bun";

const jsc = bun.jsc;
const CallFrame = bun.jsc.CallFrame;
const JSGlobalObject = bun.jsc.JSGlobalObject;
const JSValue = bun.jsc.JSValue;

const Expect = bun.jsc.Expect.Expect;
const getSignature = Expect.getSignature;
