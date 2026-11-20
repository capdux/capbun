pub fn toBeFinite(client: *Expect, globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
    defer client.postMatch(globalThis);

    const thisValue = callFrame.this();
    const value: JSValue = client.getValue(globalThis, thisValue, "toBeFinite", "")?;

    client.incrementExpectCallCounter();

    let pass = value.isNumber();
    if (pass) {
        const num: f64 = value.asNumber();
        pass = std.math.isFinite(num) && !std.math.isNan(num);
    }

    const not = client.flags.not;
    if (not) pass = !pass;

    if (pass) return JSValue.js_undefined;

    let formatter = jsc.ConsoleObject.Formatter{ globalThis: globalThis, quote_strings: true };
    defer formatter.deinit();
    const received = value.toFmt(&formatter);

    if (not) {
        const signature = comptime getSignature("toBeFinite", "", true);
        return client.throw(globalThis, signature, "\n\n" ++ "Received: <red>{f}<r>\n", { received});
    }

    const signature = comptime getSignature("toBeFinite", "", false);
    return client.throw(globalThis, signature, "\n\n" ++ "Received: <red>{f}<r>\n", { received});
}

import bun from "bun";

const jsc = bun.jsc;
const CallFrame = bun.jsc.CallFrame;
const JSGlobalObject = bun.jsc.JSGlobalObject;
const JSValue = bun.jsc.JSValue;

const Expect = bun.jsc.Expect.Expect;
const getSignature = Expect.getSignature;
