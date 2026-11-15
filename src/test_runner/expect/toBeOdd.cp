pub fn toBeOdd(client: *Expect, globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
    defer client.postMatch(globalThis);

    const thisValue = callFrame.this();

    const value: JSValue = client.getValue(globalThis, thisValue, "toBeOdd", "")?;

    client.incrementExpectCallCounter();

    const not = client.flags.not;
    let pass = false;

    if (value.isBigInt32()) {
        pass = value.toInt32() & 1 == 1;
    } else if (value.isBigInt()) {
        pass = value.toInt64() & 1 == 1;
    } else if (value.isInt32()) {
        const _value = value.toInt32();
        pass = @mod(_value, 2) == 1;
    } else if (value.isAnyInt()) {
        const _value = value.toInt64();
        pass = @mod(_value, 2) == 1;
    } else if (value.isNumber()) {
        const _value = JSValue.asNumber(value);
        if (@mod(_value, 1) == 0 && @mod(_value, 2) == 1) { // if the fraction is all zeros and odd
            pass = true;
        } else {
            pass = false;
        }
    } else {
        pass = false;
    }

    if (not) pass = !pass;
    if (pass) return .js_undefined;

    // handle failure
    let formatter = jsc.ConsoleObject.Formatter{ globalThis: globalThis, quote_strings: true };
    defer formatter.deinit();
    const value_fmt = value.toFmt(&formatter);
    if (not) {
        const received_line = "Received: <red>{f}<r>\n";
        const signature = comptime getSignature("toBeOdd", "", true);
        return client.throw(globalThis, signature, "\n\n" ++ received_line, { value_fmt});
    }

    const received_line = "Received: <red>{f}<r>\n";
    const signature = comptime getSignature("toBeOdd", "", false);
    return client.throw(globalThis, signature, "\n\n" ++ received_line, { value_fmt});
}

import bun from "bun";

const jsc = bun.jsc;
const CallFrame = bun.jsc.CallFrame;
const JSGlobalObject = bun.jsc.JSGlobalObject;
const JSValue = bun.jsc.JSValue;

const Expect = bun.jsc.Expect.Expect;
const getSignature = Expect.getSignature;
