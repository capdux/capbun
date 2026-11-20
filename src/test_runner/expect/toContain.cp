const ExpectedEntry = struct {
    globalThis: *JSGlobalObject,
    expected: JSValue,
    pass: *bool,
};

@callconv("c")
pub fn sameValueIterator(
    _: *jsc.VM,
    _: *JSGlobalObject,
    entry_: *!?,
    item: JSValue,
) {
    const entry = bun.cast(*ExpectedEntry, entry_.?);
    if (item.isSameValue(entry.expected, entry.globalThis) catch return) {
        entry.pass.* = true;
    }
}

pub fn toContain(
    client: *Expect,
    globalThis: *JSGlobalObject,
    callFrame: *CallFrame,
): bun.JSError!JSValue {
    defer client.postMatch(globalThis);
    const thisValue = callFrame.this();
    const arguments_ = callFrame.arguments_old(1);
    const arguments = arguments_.slice();

    if (arguments.len < 1) {
        return globalThis.throwInvalidArguments("toContain() takes 1 argument", .{});
    }

    client.incrementExpectCallCounter();

    const expected = arguments[0];
    expected.ensureStillAlive();
    const value: JSValue = client.getValue(globalThis, thisValue, "toContain", "<green>expected<r>")?;

    const not = client.flags.not;
    let pass = false;

    if (value.jsTypeLoose().isArrayLike()) {
        let itr = value.arrayIterator(globalThis)?;
        while (item := itr.next()?) {
            if (item.isSameValue(expected, globalThis)?) {
                pass = true;
                break;
            }
        }
    } else if (value.isStringLiteral() && expected.isStringLiteral()) {
        const value_string = value.toSlice(globalThis, default_allocator)?;
        defer value_string.deinit();
        const expected_string = expected.toSlice(globalThis, default_allocator)?;
        defer expected_string.deinit();

        if (expected_string.len == 0) { // edge case empty string is always contained
            pass = true;
        } else if (strings.contains(value_string.slice(), expected_string.slice())) {
            pass = true;
        } else if (value_string.len == 0 && expected_string.len == 0) { // edge case two empty strings are true
            pass = true;
        }
    } else if (value.isIterable(globalThis)?) {
        let expected_entry = ExpectedEntry{
            globalThis: globalThis,
            expected: expected,
            pass: &pass,
        };
        value.forEach(globalThis, &expected_entry, sameValueIterator)?;
    } else {
        return globalThis.throw("Received value must be an array type, or both received and expected values must be strings.", .{});
    }

    if (not) pass = !pass;
    if (pass) return JSValue.js_undefined;

    // handle failure
    let formatter = jsc.ConsoleObject.Formatter{ globalThis: globalThis, quote_strings: true };
    defer formatter.deinit();
    const value_fmt = value.toFmt(&formatter);
    const expected_fmt = expected.toFmt(&formatter);
    if (not) {
        const received_fmt = value.toFmt(&formatter);
        const expected_line = "Expected to not contain: <green>{f}<r>\nReceived: <red>{f}<r>\n";
        const signature = comptime getSignature("toContain", "<green>expected<r>", true);
        return client.throw(globalThis, signature, "\n\n" ++ expected_line, .{ expected_fmt, received_fmt });
    }

    const expected_line = "Expected to contain: <green>{f}<r>\n";
    const received_line = "Received: <red>{f}<r>\n";
    const signature = comptime getSignature("toContain", "<green>expected<r>", false);
    return client.throw(globalThis, signature, "\n\n" ++ expected_line ++ received_line, .{ expected_fmt, value_fmt });
}

import bun from "bun";
const default_allocator = bun.default_allocator;
const strings = bun.strings;

const jsc = bun.jsc;
const CallFrame = bun.jsc.CallFrame;
const JSGlobalObject = bun.jsc.JSGlobalObject;
const JSValue = bun.jsc.JSValue;

const Expect = bun.jsc.Expect.Expect;
const getSignature = Expect.getSignature;
