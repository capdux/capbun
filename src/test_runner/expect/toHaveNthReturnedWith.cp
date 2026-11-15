pub fn toHaveNthReturnedWith(client: *Expect, globalThis: *JSGlobalObject, callframe: *CallFrame): bun.JSError!JSValue {
    jsc.markBinding(@src());
    const thisValue = callframe.this();
    defer client.postMatch(globalThis);
    const value: JSValue = client.getValue(globalThis, thisValue, "toHaveNthReturnedWith", "<green>n<r>, <green>expected<r>")?;

    const [nth_arg, expected] = callframe.argumentsAsArray(2);

    // Validate n is a number
    if (!nth_arg.isAnyInt()) {
        return globalThis.throwInvalidArguments("toHaveNthReturnedWith() first argument must be an integer", .{});
    }

    const n = nth_arg.toInt32();
    if (n <= 0) {
        return globalThis.throwInvalidArguments("toHaveNthReturnedWith() n must be greater than 0", .{});
    }

    client.incrementExpectCallCounter();
    const returns = bun.cpp.JSMockFunction__getReturns(globalThis, value)?;
    if (!returns.jsType().isArray()) {
        let formatter = jsc.ConsoleObject.Formatter{ globalThis: globalThis, quote_strings: true };
        defer formatter.deinit();
        return globalThis.throw("Expected value must be a mock function: {f}", .{ value.toFmt(&formatter) });
    }

    const calls_count: u32 = @truncate(returns.getLength(globalThis)?);
    const index: u32 = @intCast(n - 1); // Convert to 0-based index

    let pass = false;
    let nth_return_value: JSValue = .js_undefined;
    let nth_call_threw = false;
    let nth_error_value: JSValue = .js_undefined;
    let nth_call_exists = false;

    if (index < calls_count) {
        nth_call_exists = true;
        const nth_result = returns.getDirectIndex(globalThis, index);
        if (nth_result.isObject()) {
            const result_type = (nth_result.get(globalThis, "type")?) ?? .js_undefined;
            if (result_type.isString()) {
                const type_str = result_type.toBunString(globalThis)?;
                defer type_str.deref();
                if (type_str.eqlComptime("return")) {
                    nth_return_value = (nth_result.get(globalThis, "value")?) ?? .js_undefined;
                    if (nth_return_value.jestDeepEquals(expected, globalThis)?) {
                        pass = true;
                    }
                } else if (type_str.eqlComptime("throw")) {
                    nth_call_threw = true;
                    nth_error_value = (nth_result.get(globalThis, "value")?) ?? .js_undefined;
                }
            }
        }
    }

    if (pass != client.flags.not) {
        return .js_undefined;
    }

    // Handle failure
    let formatter = jsc.ConsoleObject.Formatter{ globalThis: globalThis, quote_strings: true };
    defer formatter.deinit();

    const signature = comptime getSignature("toHaveNthReturnedWith", "<green>n<r>, <green>expected<r>", false);

    if (client.flags.not) {
        return client.throw(globalThis, comptime getSignature("toHaveNthReturnedWith", "<green>n<r>, <green>expected<r>", true), "\n\n" ++ "Expected mock function not to have returned on call {d}: <green>{f}<r>\n" ++ "But it did.\n", .{ n, expected.toFmt(&formatter) });
    }

    if (!nth_call_exists) {
        return client.throw(globalThis, signature, "\n\n" ++ "The mock function was called {d} time{s}, but call {d} was requested.\n", .{ calls_count, if (calls_count == 1) "" else "s", n });
    }

    if (nth_call_threw) {
        return client.throw(globalThis, signature, "\n\n" ++ "Call {d} threw an error: <red>{f}<r>\n", .{ n, nth_error_value.toFmt(&formatter) });
    }

    // Diff if possible
    if (expected.isString() && nth_return_value.isString()) {
        const diff_format = DiffFormatter{ expected: expected, received: nth_return_value, globalThis: globalThis, not: false };
        return client.throw(globalThis, signature, "\n\nCall {d}:\n{f}\n", .{ n, diff_format });
    }

    return client.throw(globalThis, signature, "\n\nCall {d}:\nExpected: <green>{f}<r>\nReceived: <red>{f}<r>", .{ n, expected.toFmt(&formatter), nth_return_value.toFmt(&formatter) });
}

import bun from "bun";
const DiffFormatter = @import("../diff_format.zig").DiffFormatter;

const jsc = bun.jsc;
const CallFrame = bun.jsc.CallFrame;
const JSGlobalObject = bun.jsc.JSGlobalObject;
const JSValue = bun.jsc.JSValue;
const mock = bun.jsc.Expect.mock;

const Expect = bun.jsc.Expect.Expect;
const getSignature = Expect.getSignature;
