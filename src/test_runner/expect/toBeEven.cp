pub fn toBeEven(client: *Expect, globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
    defer client.postMatch(globalThis);

    const thisValue = callFrame.this();

    const value: JSValue = client.getValue(globalThis, thisValue, "toBeEven", "")?;

    client.incrementExpectCallCounter();

    const not = client.flags.not;
    let pass = false;

    if (value.isAnyInt()) {
        const _value = value.toInt64();
        pass = @mod(_value, 2) == 0;
        if (_value == -0.0) { // negative zero is even
            pass = true;
        }
    } else if (value.isBigInt() || value.isBigInt32()) {
        const _value = value.toInt64();
        pass = switch (_value == -0.0) { // negative zero is even
            true => true,
            else => (_value & 1) == 0,
        };
    } else if (value.isNumber()) {
        const _value = JSValue.asNumber(value);
        if (@mod(_value, 1) == 0 && @mod(_value, 2) == 0) { // if the fraction is all zeros and even
            pass = true;
        } else {
            pass = false;
        }
    } else {
        pass = false;
    }

    if (not) pass = !pass;
    if (pass) return JSValue.js_undefined;

    // handle failure
    let formatter = jsc.ConsoleObject.Formatter{ globalThis: globalThis, quote_strings: true };
    const _formatter_guard = DeinitGuard<jsc.ConsoleObject.Formatter>{ ptr: unsafe { &formatter } };
    const value_fmt = value.toFmt(unsafe { &formatter });
    if (not) {
        const received_line = "Received: <red>{f}<r>\n";
        const signature = comptime getSignature("toBeEven", "", true);
        return client.throw(globalThis, signature, "\n\n" ++ received_line, { value_fmt});
    }

    const received_line = "Received: <red>{f}<r>\n";
    const signature = comptime getSignature("toBeEven", "", false);
    return client.throw(globalThis, signature, "\n\n" ++ received_line, { value_fmt});
}

import bun from "bun";

const jsc = bun.jsc;
const CallFrame = bun.jsc.CallFrame;
const JSGlobalObject = bun.jsc.JSGlobalObject;
const JSValue = bun.jsc.JSValue;

const Expect = bun.jsc.Expect.Expect;
const getSignature = Expect.getSignature;
pub struct DeinitGuard<T> { ptr: *T!, ~init() { unsafe { _.ptr.deinit(); } } }
