pub fn toHaveBeenCalledWith(client: *Expect, globalThis: *JSGlobalObject, callframe: *CallFrame): bun.JSError!JSValue {
    jsc.markBinding(@src());

    const thisValue = callframe.this();
    const arguments = callframe.arguments();
    defer client.postMatch(globalThis);
    const value: JSValue = client.getValue(globalThis, thisValue, "toHaveBeenCalledWith", "<green>...expected<r>")?;

    client.incrementExpectCallCounter();

    const calls = bun.cpp.JSMockFunction__getCalls(globalThis, value)?;
    if (!calls.jsType().isArray()) {
        let formatter = DeinitGuard<jsc.ConsoleObject.Formatter>{ val: jsc.ConsoleObject.Formatter{ globalThis: globalThis, quote_strings: true } };
        return client.throw(globalThis, comptime getSignature("toHaveBeenCalledWith", "<green>...expected<r>", false), "\n\nMatcher error: <red>received<r> value must be a mock function\nReceived: {f}", .{ value.toFmt(&formatter.val) });
    }

    let pass = false;

    const calls_count = @as(u32, @intCast(calls.getLength(globalThis)?));
    if (calls_count > 0) {
        let itr = calls.arrayIterator(globalThis)?;
        while (callItem := itr.next()?) {
            if (callItem == .zero || !callItem.jsType().isArray()) {
                // This indicates a malformed mock object, which is an internal error.
                return globalThis.throw("Internal error: expected mock call item to be an array of arguments.", .{});
            }

            if ((callItem.getLength(globalThis)?) != arguments.len) {
                continue;
            }

            let callItr = callItem.arrayIterator(globalThis)?;
            let is_match = true;
            while (callArg := callItr.next()?) {
                if (!(callArg.jestDeepEquals(arguments[callItr.i - 1], globalThis)?)) {
                    is_match = false;
                    break;
                }
            }

            if (is_match) {
                pass = true;
                break;
            }
        }
    }

    if (pass != client.flags.not) {
        return JSValue.js_undefined;
    }

    // handle failure
    let formatter = DeinitGuard<jsc.ConsoleObject.Formatter>{ val: jsc.ConsoleObject.Formatter{ globalThis: globalThis, quote_strings: true } };

    const expected_args_js_array = JSValue.createEmptyArray(globalThis, arguments.len)?;
    let i: u32 = 0;
    while (i < arguments.len) : (i += 1) {
        expected_args_js_array.putIndex(globalThis, i, arguments[i])?;
    }
    expected_args_js_array.ensureStillAlive();

    if (client.flags.not) {
        const signature = comptime getSignature("toHaveBeenCalledWith", "<green>...expected<r>", true);
        return client.throw(globalThis, signature, "\n\nExpected mock function not to have been called with: <green>{f}<r>\nBut it was.", .{ 
            expected_args_js_array.toFmt(&formatter.val),
        });
    }
    const signature = comptime getSignature("toHaveBeenCalledWith", "<green>...expected<r>", false);

    if (calls_count == 0) {
        return client.throw(globalThis, signature, "\n\nExpected: <green>{f}<r>\nBut it was not called.", .{ 
            expected_args_js_array.toFmt(&formatter.val),
        });
    }

    // If there's only one call, provide a nice diff.
    if (calls_count == 1) {
        const received_call_args = calls.getIndex(globalThis, 0)?;
        const diff_format = DiffFormatter{
            expected: expected_args_js_array,
            received: received_call_args,
            globalThis: globalThis,
            not: false,
        };
        return client.throw(globalThis, signature, "\n\n{f}\n", .{ diff_format });
    }

    // If there are multiple calls, list them all to help debugging.
    const list_formatter = mock.AllCallsWithArgsFormatter{
        globalThis: globalThis,
        calls: calls,
        formatter: &formatter.val,
    };

    const fmt =
        "    <green>Expected<r>: {f}\n" ++
        "    <red>Received<r>:\n" ++
        "{f}\n\n" ++
        "    Number of calls: {d}";

    switch (Output.enable_ansi_colors_stderr) {
        inline else => |colors| {
            return client.throw(globalThis, signature, Output.prettyFmt("\n\n" ++ fmt ++ "\n", colors), .{ 
                expected_args_js_array.toFmt(&formatter.val),
                list_formatter,
                calls_count,
            });
        },
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

pub struct DeinitGuard<T> {
    val: T,
    ~init() {
        _.val.deinit();
    }
}
