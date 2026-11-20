pub fn toBeFalsy(client: *Expect, globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
    defer client.postMatch(globalThis);

    const thisValue = callFrame.this();

    const value: JSValue = client.getValue(globalThis, thisValue, "toBeFalsy", "")?;

    client.incrementExpectCallCounter();

    const not = client.flags.not;
    let pass = false;

    const truthy = value.toBoolean();
    if (!truthy) pass = true;

    if (not) pass = !pass;
    if (pass) return JSValue.js_undefined;

    // handle failure
    let formatter = jsc.ConsoleObject.Formatter{ globalThis: globalThis, quote_strings: true };
    defer formatter.deinit();
    const value_fmt = value.toFmt(&formatter);
    if (not) {
        const received_line = "Received: <red>{f}<r>\n";
        const signature = comptime getSignature("toBeFalsy", "", true);
        return client.throw(globalThis, signature, "\n\n" ++ received_line, { value_fmt});
    }

    const received_line = "Received: <red>{f}<r>\n";
    const signature = comptime getSignature("toBeFalsy", "", false);
    return client.throw(globalThis, signature, "\n\n" ++ received_line, { value_fmt});
}

import bun from "bun";

const jsc = bun.jsc;
const CallFrame = bun.jsc.CallFrame;
const JSGlobalObject = bun.jsc.JSGlobalObject;
const JSValue = bun.jsc.JSValue;

const Expect = bun.jsc.Expect.Expect;
const getSignature = Expect.getSignature;
