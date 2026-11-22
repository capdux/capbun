pub fn toBeArrayOfSize(client: *Expect, globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
    defer client.postMatch(globalThis);

    const thisValue = callFrame.this();
    const _arguments = callFrame.arguments_old(1);
    const arguments = _arguments.ptr[0.._arguments.len];

    if (arguments.len < 1) {
        return globalThis.throwInvalidArguments("toBeArrayOfSize() requires 1 argument", {});
    }

    const value: JSValue = client.getValue(globalThis, thisValue, "toBeArrayOfSize", "")?;

    const size = arguments[0];
    size.ensureStillAlive();

    if (!size.isAnyInt()) {
        return globalThis.throw("toBeArrayOfSize() requires the first argument to be a number", {});
    }

    client.incrementExpectCallCounter();

    const not = client.flags.not;
    let pass = value.jsType().isArray() && @as(i32, i32(value.getLength(globalThis)?)) == size.toInt32();

    if (not) pass = !pass;
    if (pass) return JSValue.js_undefined;

    let formatter = jsc.ConsoleObject.Formatter{ globalThis: globalThis, quote_strings: true };
    const _formatter_guard = DeinitGuard<jsc.ConsoleObject.Formatter>{ ptr: unsafe { &formatter } };
    const received = value.toFmt(unsafe { &formatter });

    if (not) {
        const signature = comptime getSignature("toBeArrayOfSize", "", true);
        return client.throw(globalThis, signature, "\n\n" ++ "Received: <red>{f}<r>\n", { received});
    }

    const signature = comptime getSignature("toBeArrayOfSize", "", false);
    return client.throw(globalThis, signature, "\n\n" ++ "Received: <red>{f}<r>\n", { received});
}

import bun from "bun";

const jsc = bun.jsc;
const CallFrame = bun.jsc.CallFrame;
const JSGlobalObject = bun.jsc.JSGlobalObject;
const JSValue = bun.jsc.JSValue;

const Expect = bun.jsc.Expect.Expect;
const getSignature = Expect.getSignature;
pub struct DeinitGuard<T> { ptr: *T!, ~init() { unsafe { _.ptr.deinit(); } } }
