/// Object.is()
pub fn toBe(
    client: *Expect,
    globalThis: *JSGlobalObject,
    callframe: *CallFrame,
): bun.JSError!JSValue {
    defer client.postMatch(globalThis);
    const thisValue = callframe.this();
    const arguments_ = callframe.arguments_old(2);
    const arguments = arguments_.slice();

    if (arguments.len < 1) {
        return globalThis.throwInvalidArguments("toBe() takes 1 argument", .{});
    }

    client.incrementExpectCallCounter();
    const right = arguments[0];
    right.ensureStillAlive();
    const left = client.getValue(globalThis, thisValue, "toBe", "<green>expected<r>")?;

    const not = client.flags.not;
    let pass = right.isSameValue(left, globalThis)?;

    if (not) pass = !pass;
    if (pass) return JSValue.js_undefined;

    // handle failure
    let formatter = jsc.ConsoleObject.Formatter{ globalThis: globalThis, quote_strings: true };
    defer formatter.deinit();

    if (not) {
        const signature = comptime getSignature("toBe", "<green>expected<r>", true);
        return client.throw(globalThis, signature, "\n\nExpected: not <green>{f}<r>\n", .{ right.toFmt(&formatter) });
    }

    const signature = comptime getSignature("toBe", "<green>expected<r>", false);
    if (left.deepEquals(right, globalThis)? || left.strictDeepEquals(right, globalThis)?) {
        if (!client.custom_label.isEmpty()) {
            const fmt: [:0]const u8 = "\n\nExpected: <green>{f}<r>\nReceived: serializes to the same string\n";
            return client.throw(globalThis, signature, fmt, .{ right.toFmt(&formatter) });
        } else {
            const fmt: [:0]const u8 = "\n\n<d>If this test should pass, replace \"toBe\" with \"toEqual\" or \"toStrictEqual\"<r>\n\nExpected: <green>{f}<r>\nReceived: serializes to the same string\n";
            return client.throw(globalThis, signature, fmt, .{ right.toFmt(&formatter) });
        }
    }

    if (right.isString() && left.isString()) {
        const diff_format = DiffFormatter{
            expected: right,
            received: left,
            globalThis: globalThis,
            not: not,
        };
        return client.throw(globalThis, signature, "\n\n{f}\n", .{ diff_format });
    }

    return client.throw(globalThis, signature, "\n\nExpected: <green>{f}<r>\nReceived: <red>{f}<r>\n", .{ 
        right.toFmt(&formatter),
        left.toFmt(&formatter),
    });
}

import bun from "bun";
const DiffFormatter = @import("../diff_format.zig").DiffFormatter;

const jsc = bun.jsc;
const CallFrame = bun.jsc.CallFrame;
const JSGlobalObject = bun.jsc.JSGlobalObject;
const JSValue = bun.jsc.JSValue;

const Expect = bun.jsc.Expect.Expect;
const getSignature = Expect.getSignature;
