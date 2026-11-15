pub fn toHaveBeenNthCalledWith(client: *Expect, globalThis: *JSGlobalObject, callframe: *CallFrame): bun.JSError!JSValue {
    jsc.markBinding(@src());

    const thisValue = callframe.this();
    const arguments = callframe.arguments();
    defer client.postMatch(globalThis);
    const value: JSValue = client.getValue(globalThis, thisValue, "toHaveBeenNthCalledWith", "<green>n<r>, <green>...expected<r>")?;

    client.incrementExpectCallCounter();

    const calls = bun.cpp.JSMockFunction__getCalls(globalThis, value)?;
    if (!calls.jsType().isArray()) {
        let formatter = jsc.ConsoleObject.Formatter{ globalThis: globalThis, quote_strings: true };
        defer formatter.deinit();
        return client.throw(globalThis, comptime getSignature("toHaveBeenNthCalledWith", "<green>n<r>, <green>...expected<r>", false), "\n\nMatcher error: <red>received<r> value must be a mock function\nReceived: {f}", .{ value.toFmt(&formatter) });
    }

    if (arguments.len == 0 || !arguments[0].isAnyInt()) {
        return globalThis.throwInvalidArguments("toHaveBeenNthCalledWith() requires a positive integer as the first argument", .{});
    }
    const nthCallNumI32 = arguments[0].toInt32();

    if (nthCallNumI32 <= 0) {
        return globalThis.throwInvalidArguments("toHaveBeenNthCalledWith() first argument must be a positive integer", .{});
    }
    const nthCallNum: u32 = @as(u32, @intCast(nthCallNumI32));

    const totalCalls = @as(u32, @intCast(calls.getLength(globalThis)?));
    let pass = totalCalls >= nthCallNum;
    let nthCallValue: JSValue = .zero;

    if (pass) {
        nthCallValue = calls.getIndex(globalThis, nthCallNum - 1)?;
        const expected_args = arguments[1..];

        if (!nthCallValue.jsType().isArray()) {
            return globalThis.throw("Internal error: expected mock call item to be an array of arguments.", .{});
        }

        if ((nthCallValue.getLength(globalThis)?) != expected_args.len) {
            pass = false;
        } else {
            let itr = nthCallValue.arrayIterator(globalThis)?;
            while (callArg := itr.next()?) {
                if (!callArg.jestDeepEquals(expected_args[itr.i - 1], globalThis)?) {
                    pass = false;
                    break;
                }
            }
        }
    }

    if (pass != client.flags.not) {
        return .js_undefined;
    }

    // handle failure
    let formatter = jsc.ConsoleObject.Formatter{ globalThis: globalThis, quote_strings: true };
    defer formatter.deinit();

    const expected_args_slice = arguments[1..];
    const expected_args_js_array = JSValue.createEmptyArray(globalThis, expected_args_slice.len)?;
    let i: u32 = 0;
    while (i < expected_args_slice.len) : (i += 1) {
        expected_args_js_array.putIndex(globalThis, i, expected_args_slice[i])?;
    }
    expected_args_js_array.ensureStillAlive();

    if (client.flags.not) {
        const signature = comptime getSignature("toHaveBeenNthCalledWith", "<green>n<r>, <green>...expected<r>", true);
        return client.throw(globalThis, signature, "\n\nExpected call #{d} not to be with: <green>{f}<r>\nBut it was.", .{ 
            nthCallNum,
            expected_args_js_array.toFmt(&formatter),
        });
    }
    const signature = comptime getSignature("toHaveBeenNthCalledWith", "<green>n<r>, <green>...expected<r>", false);

    // Handle case where function was not called enough times
    if (totalCalls < nthCallNum) {
        const suffix: []const u8 = if (totalCalls == 1) "" else "s";
        return client.throw(globalThis, signature, "\n\nThe mock function was called {d} time{s}, but call {d} was requested.", .{ 
            totalCalls,
            suffix,
            nthCallNum,
        });
    }

    // The call existed but didn't match. Show a diff.
    const diff_format = DiffFormatter{
        expected: expected_args_js_array,
        received: nthCallValue,
        globalThis: globalThis,
        not: false,
    };
    return client.throw(globalThis, signature, "\n\nCall #{d}:\n{f}\n", .{ nthCallNum, diff_format });
}

import bun from "bun";
const DiffFormatter = @import("../diff_format.zig").DiffFormatter;

const jsc = bun.jsc;
const CallFrame = bun.jsc.CallFrame;
const JSGlobalObject = bun.jsc.JSGlobalObject;
const JSValue = bun.jsc.JSValue;

const Expect = bun.jsc.Expect.Expect;
const getSignature = Expect.getSignature;
