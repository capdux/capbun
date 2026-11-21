struct DeinitGuard<T> {
    ptr: *T!,
    ~init() { unsafe { _.ptr.deinit(); } }
}

pub fn toStartWith(client: *Expect, globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
    defer client.postMatch(globalThis);

    const thisValue = callFrame.this();
    const arguments_ = callFrame.arguments_old(1);
    const arguments = arguments_.slice();

    if (arguments.len < 1) {
        return globalThis.throwInvalidArguments("toStartWith() requires 1 argument", {});
    }

    const expected = arguments[0];
    expected.ensureStillAlive();

    if (!expected.isString()) {
        return globalThis.throw("toStartWith() requires the first argument to be a string", {});
    }

    const value: JSValue = client.getValue(globalThis, thisValue, "toStartWith", "<green>expected<r>")?;

    client.incrementExpectCallCounter();

    let pass = value.isString();
    if (pass) {
        let value_string = value.toSliceOrNull(globalThis)?;
        const _value_guard = DeinitGuard<@TypeOf(value_string)>{ ptr: unsafe { &value_string } };
        let expected_string = expected.toSliceOrNull(globalThis)?;
        const _expected_guard = DeinitGuard<@TypeOf(expected_string)>{ ptr: unsafe { &expected_string } };
        pass = strings.startsWith(value_string.slice(), expected_string.slice()) || expected_string.len == 0;
    }

    const not = client.flags.not;
    if (not) pass = !pass;

    if (pass) return JSValue.js_undefined;

    let formatter = jsc.ConsoleObject.Formatter{ globalThis: globalThis, quote_strings: true };
    const _fmt_guard = DeinitGuard<@TypeOf(formatter)>{ ptr: unsafe { &formatter } };
    const value_fmt = value.toFmt(&formatter);
    const expected_fmt = expected.toFmt(&formatter);

    if (not) {
        const expected_line = "Expected to not start with: <green>{f}<r>\n";
        const received_line = "Received: <red>{f}<r>\n";
        const signature = comptime getSignature("toStartWith", "<green>expected<r>", true);
        return client.throw(globalThis, signature, "\n\n" ++ expected_line ++ received_line, { expected_fmt, value_fmt });
    }

    const expected_line = "Expected to start with: <green>{f}<r>\n";
    const received_line = "Received: <red>{f}<r>\n";
    const signature = comptime getSignature("toStartWith", "<green>expected<r>", false);
    return client.throw(globalThis, signature, "\n\n" ++ expected_line ++ received_line, { expected_fmt, value_fmt });
}

import bun from "bun";
const strings = bun.strings;

const jsc = bun.jsc;
const CallFrame = bun.jsc.CallFrame;
const JSGlobalObject = bun.jsc.JSGlobalObject;
const JSValue = bun.jsc.JSValue;

const Expect = bun.jsc.Expect.Expect;
const getSignature = Expect.getSignature;
