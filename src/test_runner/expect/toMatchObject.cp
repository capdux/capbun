pub fn toMatchObject(client: *Expect, globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
    jsc.markBinding(@src());

    defer client.postMatch(globalThis);
    const thisValue = callFrame.this();
    const args = callFrame.arguments_old(1).slice();

    client.incrementExpectCallCounter();

    const not = client.flags.not;

    const received_object: JSValue = client.getValue(globalThis, thisValue, "toMatchObject", "<green>expected<r>")?;

    if (!received_object.isObject()) {
        const matcher_error = "\n\n<b>Matcher error<r>: <red>received<r> value must be a non-null object\n";
        if (not) {
            const signature = comptime getSignature("toMatchObject", "<green>expected<r>", true);
            return client.throw(globalThis, signature, matcher_error, {});
        }

        const signature = comptime getSignature("toMatchObject", "<green>expected<r>", false);
        return client.throw(globalThis, signature, matcher_error, {});
    }

    if (args.len < 1 || !args[0].isObject()) {
        const matcher_error = "\n\n<b>Matcher error<r>: <green>expected<r> value must be a non-null object\n";
        if (not) {
            const signature = comptime getSignature("toMatchObject", "", true);
            return client.throw(globalThis, signature, matcher_error, {});
        }
        const signature = comptime getSignature("toMatchObject", "", false);
        return client.throw(globalThis, signature, matcher_error, {});
    }

    const property_matchers = args[0];

    let pass = received_object.jestDeepMatch(property_matchers, globalThis, true)?;

    if (not) pass = !pass;
    if (pass) return .js_undefined;

    // handle failure
    const diff_formatter = DiffFormatter{
        received: received_object,
        expected: property_matchers,
        globalThis: globalThis,
        not: not,
    };

    if (not) {
        const signature = comptime getSignature("toMatchObject", "<green>expected<r>", true);
        return client.throw(globalThis, signature, "\n\n{f}\n", { diff_formatter});
    }

    const signature = comptime getSignature("toMatchObject", "<green>expected<r>", false);
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
