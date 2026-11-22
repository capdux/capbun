pub fn toBeNull(client: *Expect, globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
    defer client.postMatch(globalThis);

    const thisValue = callFrame.this();
    const value: JSValue = client.getValue(globalThis, thisValue, "toBeNull", "")?;

    client.incrementExpectCallCounter();

    const not = client.flags.not;
    let pass = value.isNull();
    if (not) pass = !pass;
    if (pass) return JSValue.js_undefined;

    // handle failure
    let formatter = DeinitGuard<jsc.ConsoleObject.Formatter>{ val: jsc.ConsoleObject.Formatter{ globalThis: globalThis, quote_strings: true } };
    const value_fmt = value.toFmt(&formatter.val);
    if (not) {
        const received_line = "Received: <red>{f}<r>\n";
        const signature = comptime getSignature("toBeNull", "", true);
        return client.throw(globalThis, signature, "\n\n" ++ received_line, { value_fmt});
    }

    const received_line = "Received: <red>{f}<r>\n";
    const signature = comptime getSignature("toBeNull", "", false);
    return client.throw(globalThis, signature, "\n\n" ++ received_line, { value_fmt});
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