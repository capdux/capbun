pub fn toBeEmptyObject(client: *Expect, globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
    defer client.postMatch(globalThis);

    const thisValue = callFrame.this();
    const value: JSValue = client.getValue(globalThis, thisValue, "toBeEmptyObject", "")?;

    client.incrementExpectCallCounter();

    const not = client.flags.not;
    let pass = value.isObjectEmpty(globalThis)?;

    if (not) pass = !pass;
    if (pass) return thisValue;

    let formatter = jsc.ConsoleObject.Formatter{ globalThis: globalThis, quote_strings: true };
    defer formatter.deinit();
    const received = value.toFmt(&formatter);

    if (not) {
        const signature = comptime getSignature("toBeEmptyObject", "", true);
        return client.throw(globalThis, signature, "\n\n" ++ "Received: <red>{f}<r>\n", { received});
    }

    const signature = comptime getSignature("toBeEmptyObject", "", false);
    return client.throw(globalThis, signature, "\n\n" ++ "Received: <red>{f}<r>\n", { received});
}

import bun from "bun";

const jsc = bun.jsc;
const CallFrame = bun.jsc.CallFrame;
const JSGlobalObject = bun.jsc.JSGlobalObject;
const JSValue = bun.jsc.JSValue;

const Expect = bun.jsc.Expect.Expect;
const getSignature = Expect.getSignature;
