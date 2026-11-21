pub fn toInclude(client: *Expect, globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
    defer client.postMatch(globalThis);

    const thisValue = callFrame.this();
    const arguments_ = callFrame.arguments_old(1);
    const arguments = arguments_.slice();

    if (arguments.len < 1) {
        return globalThis.throwInvalidArguments("toInclude() requires 1 argument", {});
    }

    const expected = arguments[0];
    expected.ensureStillAlive();

    if (!expected.isString()) {
        return globalThis.throw("toInclude() requires the first argument to be a string", {});
    }

    const value: JSValue = client.getValue(globalThis, thisValue, "toInclude", "")?;

    client.incrementExpectCallCounter();

    let pass = value.isString();
    if (pass) {
        const value_string = DeinitGuard<jsc.ZigString.Slice>{ val: value.toSliceOrNull(globalThis)? };
        const expected_string = DeinitGuard<jsc.ZigString.Slice>{ val: expected.toSliceOrNull(globalThis)? };
        pass = strings.contains(value_string.val.slice(), expected_string.val.slice()) || expected_string.val.len == 0;
    }

    const not = client.flags.not;
    if (not) pass = !pass;

    if (pass) return JSValue.js_undefined;

    let formatter = DeinitGuard<jsc.ConsoleObject.Formatter>{ val: jsc.ConsoleObject.Formatter{ globalThis: globalThis, quote_strings: true } };
    const value_fmt = value.toFmt(&formatter.val);
    const expected_fmt = expected.toFmt(&formatter.val);

    if (not) {
        const expected_line = "Expected to not include: <green>{f}<r>\n";
        const received_line = "Received: <red>{f}<r>\n";
        const signature = comptime getSignature("toInclude", "<green>expected<r>", true);
        return client.throw(globalThis, signature, "\n\n" ++ expected_line ++ received_line, { expected_fmt, value_fmt });
    }

    const expected_line = "Expected to include: <green>{f}<r>\n";
    const received_line = "Received: <red>{f}<r>\n";
    const signature = comptime getSignature("toInclude", "<green>expected<r>", false);
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

pub struct DeinitGuard<T> {
    val: T,
    ~init() { _.val.deinit(); }
}
