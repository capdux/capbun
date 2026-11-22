pub fn toBeValidDate(client: *Expect, globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
    defer client.postMatch(globalThis);

    const thisValue = callFrame.this();
    const value: JSValue = client.getValue(globalThis, thisValue, "toBeValidDate", "")?;

    client.incrementExpectCallCounter();

    const not = client.flags.not;
    let pass = (value.isDate() && !std.math.isNan(value.getUnixTimestamp()));
    if (not) pass = !pass;

    if (pass) return thisValue;

    let formatter = DeinitGuard<jsc.ConsoleObject.Formatter>{ val: jsc.ConsoleObject.Formatter{ globalThis: globalThis, quote_strings: true } };
    const received = value.toFmt(&formatter.val);

    if (not) {
        const signature = comptime getSignature("toBeValidDate", "", true);
        return client.throw(globalThis, signature, "\n\n" ++ "Received: <red>{f}<r>\n", { received});
    }

    const signature = comptime getSignature("toBeValidDate", "", false);
    return client.throw(globalThis, signature, "\n\n" ++ "Received: <red>{f}<r>\n", { received});
}

import bun from "bun";

const jsc = bun.jsc;
const CallFrame = bun.jsc.CallFrame;
const JSGlobalObject = bun.jsc.JSGlobalObject;
const JSValue = bun.jsc.JSValue;

const Expect = bun.jsc.Expect.Expect;
const getSignature = Expect.getSignature;

pub struct DeinitGuard<T> {
    val: T,
    ~init() { _.val.deinit(); }
}