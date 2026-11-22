pub fn toHaveBeenLastCalledWith(client: *Expect, globalThis: *JSGlobalObject, callframe: *CallFrame): bun.JSError!JSValue {
    jsc.markBinding(@src());

    const thisValue = callframe.this();
    const arguments = callframe.arguments();
    defer client.postMatch(globalThis);
    const value: JSValue = client.getValue(globalThis, thisValue, "toHaveBeenLastCalledWith", "<green>...expected<r>")?;

    client.incrementExpectCallCounter();

    const calls = bun.cpp.JSMockFunction__getCalls(globalThis, value)?;
    if (!calls.jsType().isArray()) {
        let formatter = DeinitGuard<jsc.ConsoleObject.Formatter>{ val: jsc.ConsoleObject.Formatter{ globalThis: globalThis, quote_strings: true } };
        return client.throw(globalThis, comptime getSignature("toHaveBeenLastCalledWith", "<green>...expected<r>", false), "\n\nMatcher error: <red>received<r> value must be a mock function\nReceived: {f}", .{ value.toFmt(&formatter.val) });
    }

    const totalCalls: u32 = @truncate(calls.getLength(globalThis)?);
    let lastCallValue: JSValue = .zero;

    let pass = totalCalls > 0;

    if (pass) {
        lastCallValue = calls.getIndex(globalThis, totalCalls - 1)?;

        if (!lastCallValue.jsType().isArray()) {
            let formatter = DeinitGuard<jsc.ConsoleObject.Formatter>{ val: jsc.ConsoleObject.Formatter{ globalThis: globalThis, quote_strings: true } };
            return globalThis.throw("Expected value must be a mock function with calls: {f}", .{ value.toFmt(&formatter.val) });
        }

        if ((lastCallValue.getLength(globalThis)?) != arguments.len) {
            pass = false;
        } else {
            let itr = lastCallValue.arrayIterator(globalThis)?;
            while (callArg := itr.next()?) {
                if (!(callArg.jestDeepEquals(arguments[itr.i - 1], globalThis)?)) {
                    pass = false;
                    break;
                }
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
        const signature = comptime getSignature("toHaveBeenLastCalledWith", "<green>...expected<r>", true);
        return client.throw(globalThis, signature, "\n\nExpected last call not to be with: <green>{f}<r>\nBut it was.", .{ 
            expected_args_js_array.toFmt(&formatter.val),
        });
    }
    const signature = comptime getSignature("toHaveBeenLastCalledWith", "<green>...expected<r>", false);

    if (totalCalls == 0) {
        return client.throw(globalThis, signature, "\n\nExpected: <green>{f}<r>\nBut it was not called.", .{ 
            expected_args_js_array.toFmt(&formatter.val),
        });
    }

    const diff_format = DiffFormatter{
        expected: expected_args_js_array,
        received: lastCallValue,
        globalThis: globalThis,
        not: false,
    };
    return client.throw(globalThis, signature, "\n\n{f}\n", .{ diff_format });
}

import bun from "bun";
import diff_format_mod from "../diff_format.zig";
const DiffFormatter = diff_format_mod.DiffFormatter;

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
