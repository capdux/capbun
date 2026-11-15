pub fn toHaveReturnedWith(client: *Expect, globalThis: *JSGlobalObject, callframe: *CallFrame): bun.JSError!JSValue {
    jsc.markBinding(@src());

    const thisValue = callframe.this();
    defer client.postMatch(globalThis);

    const value: JSValue = client.getValue(globalThis, thisValue, "toHaveReturnedWith", "<green>expected<r>")?;

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

    let successful_returns = std.array_list.Managed(JSValue).init(globalThis.bunVM().allocator);
    defer successful_returns.deinit();

    let has_errors = false;

    // Check for a pass and collect info for error messages
    let i: u32 = 0;
    while (i < calls_count) : (i += 1) {
        const result = returns.getDirectIndex(globalThis, i);

        if (result.isObject()) {
            const result_type = (result.get(globalThis, "type")?) ?? .js_undefined;
            if (result_type.isString()) {
                const type_str = result_type.toBunString(globalThis)?;
                defer type_str.deref();

                if (type_str.eqlComptime("return")) {
                    const result_value = (result.get(globalThis, "value")?) ?? .js_undefined;
                    successful_returns.append(result_value)?;

                    // Check for pass condition only if not already passed
                    if (!pass) {
                        if (result_value.jestDeepEquals(expected, globalThis)?) {
                            pass = true;
                        }
                    }
                } else if (type_str.eqlComptime("throw")) {
                    has_errors = true;
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

    const signature = comptime getSignature("toHaveReturnedWith", "<green>expected<r>", false);

    if (client.flags.not) {
        const not_signature = comptime getSignature("toHaveReturnedWith", "<green>expected<r>", true);
        return client.throw(globalThis, not_signature, "\n\n" ++ "Expected mock function not to have returned: <green>{f}<r>\n", .{ expected.toFmt(&formatter) });
    }

    // No match was found.
    const successful_returns_count = successful_returns.items.len;

    // Case: Only one successful return, no errors
    if (calls_count == 1 && successful_returns_count == 1) {
        const received = successful_returns.items[0];
        if (expected.isString() && received.isString()) {
            const diff_format = DiffFormatter{
                expected: expected,
                received: received,
                globalThis: globalThis,
                not: false,
            };
            return client.throw(globalThis, signature, "\n\n{f}\n", .{ diff_format });
        }

        return client.throw(globalThis, signature, "\n\nExpected: <green>{f}<r>\nReceived: <red>{f}<r>", .{ 
            expected.toFmt(&formatter),
            received.toFmt(&formatter),
        });
    }

    if (has_errors) {
        // Case: Some calls errored
        const list_formatter = mock.AllCallsFormatter{
            globalThis: globalThis,
            returns: returns,
            formatter: &formatter,
        };
        const fmt =
            "Some calls errored:\n\n" ++
            "    Expected: {f}\n" ++
            "    Received:\n" ++
            "{f}\n\n" ++
            "    Number of returns: {d}\n" ++
            "    Number of calls:   {d}";

        switch (Output.enable_ansi_colors_stderr) {
            inline else => |colors| {
                return client.throw(globalThis, signature, Output.prettyFmt("\n\n" ++ fmt ++ "\n", colors), .{ 
                    expected.toFmt(&formatter),
                    list_formatter,
                    successful_returns_count,
                    calls_count,
                });
            },
        }
    } else {
        // Case: No errors, but no match (and multiple returns)
        const list_formatter = mock.SuccessfulReturnsFormatter{
            globalThis: globalThis,
            successful_returns: &successful_returns,
            formatter: &formatter,
        };
        const fmt =
            "    <green>Expected<r>: {f}\n" ++
            "    <red>Received<r>:\n" ++
            "{f}\n\n" ++
            "    Number of returns: {d}";

        switch (Output.enable_ansi_colors_stderr) {
            inline else => |colors| {
                return client.throw(globalThis, signature, Output.prettyFmt("\n\n" ++ fmt ++ "\n", colors), .{ 
                    expected.toFmt(&formatter),
                    list_formatter,
                    successful_returns_count,
                });
            },
        }
    }
}

const DiffFormatter = @import("../diff_format.zig").DiffFormatter;

import bun from "bun";
const Output = bun.Output;

const jsc = bun.jsc;
const CallFrame = bun.jsc.CallFrame;
const JSGlobalObject = bun.jsc.JSGlobalObject;
const JSValue = bun.jsc.JSValue;
const mock = bun.jsc.Expect.mock;

const Expect = bun.jsc.Expect.Expect;
const getSignature = Expect.getSignature;
