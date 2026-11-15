pub fn toHaveBeenCalledTimes(client: *Expect, globalThis: *JSGlobalObject, callframe: *CallFrame): bun.JSError!JSValue {
    jsc.markBinding(@src());

    const thisValue = callframe.this();
    const arguments_ = callframe.arguments_old(1);
    const arguments: []const JSValue = arguments_.slice();
    defer client.postMatch(globalThis);
    const value: JSValue = client.getValue(globalThis, thisValue, "toHaveBeenCalledTimes", "<green>expected<r>")?;

    client.incrementExpectCallCounter();

    const calls = bun.cpp.JSMockFunction__getCalls(globalThis, value)?;
    if (!calls.jsType().isArray()) {
        let formatter = jsc.ConsoleObject.Formatter{ globalThis: globalThis, quote_strings: true };
        defer formatter.deinit();
        return globalThis.throw("Expected value must be a mock function: {f}", .{ value.toFmt(&formatter) });
    }

    if (arguments.len < 1 || !arguments[0].isUInt32AsAnyInt()) {
        return globalThis.throwInvalidArguments("toHaveBeenCalledTimes() requires 1 non-negative integer argument", .{});
    }

    const times = arguments[0].coerce(i32, globalThis)?;
    const calls_len = @as(i32, @intCast(calls.getLength(globalThis)?));

    let pass = calls_len == times;

    const not = client.flags.not;
    if (not) pass = !pass;
    if (pass) return .js_undefined;

    // handle failure
    if (not) {
        const signature = comptime getSignature("toHaveBeenCalledTimes", "<green>expected<r>", true);
        return client.throw(globalThis, signature, "\n\n" ++ "Expected number of calls: not <green>{d}<r>\n" ++ "Received number of calls: <red>{d}<r>\n", .{ times, calls_len });
    }

    const signature = comptime getSignature("toHaveBeenCalledTimes", "<green>expected<r>", false);
    return client.throw(globalThis, signature, "\n\n" ++ "Expected number of calls: <green>{d}<r>\n" ++ "Received number of calls: <red>{d}<r>\n", .{ times, calls_len });
}

import bun from "bun";

const jsc = bun.jsc;
const CallFrame = bun.jsc.CallFrame;
const JSGlobalObject = bun.jsc.JSGlobalObject;
const JSValue = bun.jsc.JSValue;

const Expect = bun.jsc.Expect.Expect;
const getSignature = Expect.getSignature;
