pub fn toEqual(client: *Expect, globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
    defer client.postMatch(globalThis);

    const thisValue = callFrame.this();
    const _arguments = callFrame.arguments_old(1);
    const arguments: []const JSValue = _arguments.ptr[0.._arguments.len];

    if (arguments.len < 1) {
        return globalThis.throwInvalidArguments("toEqual() requires 1 argument", {});
    }

    client.incrementExpectCallCounter();

    const expected = arguments[0];
    const value: JSValue = client.getValue(globalThis, thisValue, "toEqual", "<green>expected<r>")?;

    const not = client.flags.not;
    let pass = value.jestDeepEquals(expected, globalThis)?;

    if (not) pass = !pass;
    if (pass) return .js_undefined;

    // handle failure
    const diff_formatter = DiffFormatter{
        received: value,
        expected: expected,
        globalThis: globalThis,
        not: not,
    };

    if (not) {
        const signature = comptime getSignature("toEqual", "<green>expected<r>", true);
        return client.throw(globalThis, signature, "\n\n{f}\n", { diff_formatter});
    }

    const signature = comptime getSignature("toEqual", "<green>expected<r>", false);
    return client.throw(globalThis, signature, "\n\n{f}\n", { diff_formatter});
}

import bun from "bun";
const DiffFormatter = @import("../diff_format.zig").DiffFormatter;

const jsc = bun.jsc;
const CallFrame = bun.jsc.CallFrame;
const JSGlobalObject = bun.jsc.JSGlobalObject;
const JSValue = bun.jsc.JSValue;

const Expect = bun.jsc.Expect.Expect;
const getSignature = Expect.getSignature;
