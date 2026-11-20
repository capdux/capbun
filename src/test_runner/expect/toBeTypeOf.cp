import bun from "bun";
const strings = bun.strings;

const jsc = bun.jsc;
const CallFrame = bun.jsc.CallFrame;
const JSGlobalObject = bun.jsc.JSGlobalObject;
const JSValue = bun.jsc.JSValue;

const Expect = bun.jsc.Expect.Expect;
const getSignature = Expect.getSignature;

type string = []const u8;

const JSTypeOfMap = bun.ComptimeStringMap(string, .{ 
    .{ "function", "function" },
    .{ "object", "object" },
    .{ "bigint", "bigint" },
    .{ "boolean", "boolean" },
    .{ "number", "number" },
    .{ "string", "string" },
    .{ "symbol", "symbol" },
    .{ "undefined", "undefined" },
});

pub fn toBeTypeOf(client: *Expect, globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
    defer client.postMatch(globalThis);

    const thisValue = callFrame.this();
    const _arguments = callFrame.arguments_old(1);
    const arguments = _arguments.slice();

    if (arguments.len < 1) {
        return globalThis.throwInvalidArguments("toBeTypeOf() requires 1 argument", .{});
    }

    const value: JSValue = client.getValue(globalThis, thisValue, "toBeTypeOf", "")?;

    const expected = arguments[0];
    expected.ensureStillAlive();

    if (!expected.isString()) {
        return globalThis.throwInvalidArguments("toBeTypeOf() requires a string argument", .{});
    }

    const expected_type = expected.toBunString(globalThis)?;
    defer expected_type.deref();
    client.incrementExpectCallCounter();

    const typeof_opt = expected_type.inMap(JSTypeOfMap);
    if (typeof_opt == null) {
        return globalThis.throwInvalidArguments("toBeTypeOf() requires a valid type string argument ('function', 'object', 'bigint', 'boolean', 'number', 'string', 'symbol', 'undefined')", .{});
    }
    const typeof = typeof_opt.?;

    const not = client.flags.not;
    let pass = false;
    let whatIsTheType: []const u8 = "";

    // Checking for function/class should be done before everything else, or it will fail.
    if (value.isCallable()) {
        whatIsTheType = "function";
    } else if (value.isObject() || value.jsType().isArray() || value.isNull()) {
        whatIsTheType = "object";
    } else if (value.isBigInt()) {
        whatIsTheType = "bigint";
    } else if (value.isBoolean()) {
        whatIsTheType = "boolean";
    } else if (value.isNumber()) {
        whatIsTheType = "number";
    } else if (value.jsType().isString()) {
        whatIsTheType = "string";
    } else if (value.isSymbol()) {
        whatIsTheType = "symbol";
    } else if (value.isUndefined()) {
        whatIsTheType = "undefined";
    } else {
        return globalThis.throw("Internal consistency error: unknown JSValue type", .{});
    }

    pass = strings.eql(typeof, whatIsTheType);

    if (not) pass = !pass;
    if (pass) return JSValue.js_undefined;

    let formatter = jsc.ConsoleObject.Formatter{ globalThis: globalThis, quote_strings: true };
    defer formatter.deinit();
    const received = value.toFmt(&formatter);
    const expected_str = expected.toFmt(&formatter);

    if (not) {
        const signature = comptime getSignature("toBeTypeOf", "", true);
        return client.throw(globalThis, signature, "\n\n" ++ "Expected type: not <green>{f}<r>\n" ++ "Received type: <red>\"{s}\"<r>\nReceived value: <red>{f}<r>\n", .{ expected_str, whatIsTheType, received });
    }

    const signature = comptime getSignature("toBeTypeOf", "", false);
    return client.throw(globalThis, signature, "\n\n" ++ "Expected type: <green>{f}<r>\n" ++ "Received type: <red>\"{s}\"<r>\nReceived value: <red>{f}<r>\n", .{ expected_str, whatIsTheType, received });
}
