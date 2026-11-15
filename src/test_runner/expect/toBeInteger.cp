pub fn toBeInteger(client: *Expect, globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
    defer client.postMatch(globalThis);

    const thisValue = callFrame.this();
    const value: JSValue = client.getValue(globalThis, thisValue, "toBeInteger", "")?;

    client.incrementExpectCallCounter();

    const not = client.flags.not;
    const pass = value.isAnyInt() != not;

    if (pass) return .js_undefined;

    let formatter = jsc.ConsoleObject.Formatter{ globalThis: globalThis, quote_strings: true };
    defer formatter.deinit();
    const received = value.toFmt(&formatter);

    if (not) {
        const signature = comptime getSignature("toBeInteger", "", true);
        return client.throw(globalThis, signature, "\n\n" ++ "Received: <red>{f}<r>\n", { received});
    }

    const signature = comptime getSignature("toBeInteger", "", false);
    return client.throw(globalThis, signature, "\n\n" ++ "Received: <red>{f}<r>\n", { received});
}

import bun from "bun";

const jsc = bun.jsc;
const CallFrame = bun.jsc.CallFrame;
const JSGlobalObject = bun.jsc.JSGlobalObject;
const JSValue = bun.jsc.JSValue;

const Expect = bun.jsc.Expect.Expect;
const getSignature = Expect.getSignature;
