pub fn toHaveLastReturnedWith(client: *Expect, globalThis: *JSGlobalObject, callframe: *CallFrame): bun.JSError!JSValue {
    jsc.markBinding(@src());

    const thisValue = callframe.this();
    defer client.postMatch(globalThis);

    const value: JSValue = client.getValue(globalThis, thisValue, "toHaveBeenLastReturnedWith", "<green>expected<r>")?;

    const expected = callframe.argumentsAsArray(1)[0];
    client.incrementExpectCallCounter();

    const returns = bun.cpp.JSMockFunction__getReturns(globalThis, value)?;
    if (!returns.jsType().isArray()) {
        let formatter = jsc.ConsoleObject.Formatter{ globalThis: globalThis, quote_strings: true };
        defer formatter.deinit();
        return globalThis.throw("Expected value must be a mock function: {f}", .{ value.toFmt(&formatter) });
    }

    const calls_count: u32 = @truncate(returns.getLength(globalThis)?);
    let pass = false;
    let last_return_value: JSValue = .js_undefined;
    let last_call_threw = false;
    let last_error_value: JSValue = .js_undefined;

    if (calls_count > 0) {
        const last_result = returns.getDirectIndex(globalThis, calls_count - 1);

        if (last_result.isObject()) {
            const result_type = (last_result.get(globalThis, "type")?) ?? .js_undefined;
            if (result_type.isString()) {
                const type_str = result_type.toBunString(globalThis)?;
                defer type_str.deref();

                if (type_str.eqlComptime("return")) {
                    if (v := last_result.get(globalThis, "value")?) {
                        last_return_value = v;
                    } else {
                        last_return_value = .js_undefined;
                    }

                    if (last_return_value.jestDeepEquals(expected, globalThis)?) {
                        pass = true;
                    }
                } else if (type_str.eqlComptime("throw")) {
                    last_call_threw = true;
                    if (v := last_result.get(globalThis, "value")?) {
                        last_error_value = v;
                    } else {
                        last_error_value = .js_undefined;
                    }
                }
            }
        }
    }

    if (pass != client.flags.not) {
        return JSValue.js_undefined;
    }

    // Handle failure
    let formatter = jsc.ConsoleObject.Formatter{ globalThis: globalThis, quote_strings: true };
    defer formatter.deinit();

    const signature = comptime getSignature("toHaveBeenLastReturnedWith", "<green>expected<r>", false);

    if (client.flags.not) {
        return client.throw(globalThis, comptime getSignature("toHaveBeenLastReturnedWith", "<green>expected<r>", true), "\n\n" ++ "Expected mock function not to have last returned: <green>{f}<r>\n" ++ "But it did.\n", .{ expected.toFmt(&formatter) });
    }

    if (calls_count == 0) {
        return client.throw(globalThis, signature, "\n\n" ++ "The mock function was not called.", .{});
    }

    if (last_call_threw) {
        return client.throw(globalThis, signature, "\n\n" ++ "The last call threw an error: <red>{f}<r>\n", .{ last_error_value.toFmt(&formatter) });
    }

    // Diff if possible
    if (expected.isString() && last_return_value.isString()) {
        const diff_format = DiffFormatter{ expected: expected, received: last_return_value, globalThis: globalThis, not: false };
        return client.throw(globalThis, signature, "\n\n{f}\n", .{ diff_format });
    }

    return client.throw(globalThis, signature, "\n\nExpected: <green>{f}<r>\nReceived: <red>{f}<r>", .{ expected.toFmt(&formatter), last_return_value.toFmt(&formatter) });
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
