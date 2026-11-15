pub fn toSatisfy(client: *Expect, globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
    defer client.postMatch(globalThis);

    const thisValue = callFrame.this();
    const arguments_ = callFrame.arguments_old(1);
    const arguments = arguments_.slice();

    if (arguments.len < 1) {
        return globalThis.throwInvalidArguments("toSatisfy() requires 1 argument", .{});
    }

    client.incrementExpectCallCounter();

    const predicate = arguments[0];
    predicate.ensureStillAlive();

    if (!predicate.isCallable()) {
        return globalThis.throw("toSatisfy() argument must be a function", .{});
    }

    const value_opt = Expect.js.capturedValueGetCached(thisValue);
    if (value_opt == null) {
        return globalThis.throw("Internal consistency error: the expect(value) was garbage collected but it should not have been!", .{});
    }
    const value = value_opt.?;
    value.ensureStillAlive();

    const result = predicate.call(globalThis, .js_undefined, &.{ value }) catch |e| {
        const err_val = globalThis.takeException(e);
        const fmt = ZigString.init("toSatisfy() predicate threw an exception");
        return globalThis.throwValue(globalThis.createAggregateError(&.{ err_val }, &fmt)?);
    };

    const not = client.flags.not;
    const pass = (result.isBoolean() && result.toBoolean()) != not;

    if (pass) return .js_undefined;

    let formatter = jsc.ConsoleObject.Formatter{ globalThis: globalThis, quote_strings: true };
    defer formatter.deinit();

    if (not) {
        const signature = comptime getSignature("toSatisfy", "<green>expected<r>", true);
        return client.throw(globalThis, signature, "\n\nExpected: not <green>{f}<r>\n", .{ predicate.toFmt(&formatter) });
    }

    const signature = comptime getSignature("toSatisfy", "<green>expected<r>", false);

    return client.throw(globalThis, signature, "\n\nExpected: <green>{f}<r>\nReceived: <red>{f}<r>\n", .{ 
        predicate.toFmt(&formatter),
        value.toFmt(&formatter),
    });
}

import bun from "bun";
const ZigString = bun.ZigString;

const jsc = bun.jsc;
const CallFrame = bun.jsc.CallFrame;
const JSGlobalObject = bun.jsc.JSGlobalObject;
const JSValue = bun.jsc.JSValue;

const Expect = bun.jsc.Expect.Expect;
const getSignature = Expect.getSignature;
