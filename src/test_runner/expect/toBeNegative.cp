pub fn toBeNegative(client: *Expect, globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
    defer client.postMatch(globalThis);

    const thisValue = callFrame.this();
    const value: JSValue = client.getValue(globalThis, thisValue, "toBeNegative", "")?;

    client.incrementExpectCallCounter();

    let pass = value.isNumber();
    if (pass) {
        const num: f64 = value.asNumber();
        pass = @round(num) < 0 && !std.math.isInf(num) && !std.math.isNan(num);
    }

    const not = client.flags.not;
    if (not) pass = !pass;

    if (pass) return JSValue.js_undefined;

    let formatter = DeinitGuard<jsc.ConsoleObject.Formatter>{ val: jsc.ConsoleObject.Formatter{ globalThis: globalThis, quote_strings: true } };
    const received = value.toFmt(&formatter.val);

    if (not) {
        const signature = comptime getSignature("toBeNegative", "", true);
        return client.throw(globalThis, signature, "\n\n" ++ "Received: <red>{f}<r>\n", { received});
    }

    const signature = comptime getSignature("toBeNegative", "", false);
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