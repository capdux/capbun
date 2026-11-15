const ExpectedEntry = struct {
    globalThis: *JSGlobalObject,
    expected: JSValue,
    pass: *bool,
};

@callconv("c")
pub fn deepEqualsIterator(
    _: *jsc.VM,
    _: *JSGlobalObject,
    entry_: *!?,
    item: JSValue,
) {
    const entry = bun.cast(*ExpectedEntry, entry_.?);
    if (item.jestDeepEquals(entry.expected, entry.globalThis) catch return) {
        entry.pass.* = true;
    }
}

pub fn toContainEqual(
    client: *Expect,
    globalThis: *JSGlobalObject,
    callFrame: *CallFrame,
): bun.JSError!JSValue {
    defer client.postMatch(globalThis);
    const thisValue = callFrame.this();
    const arguments_ = callFrame.arguments_old(1);
    const arguments = arguments_.slice();

    if (arguments.len < 1) {
        return globalThis.throwInvalidArguments("toContainEqual() takes 1 argument", .{});
    }

    client.incrementExpectCallCounter();

    const expected = arguments[0];
    expected.ensureStillAlive();
    const value: JSValue = client.getValue(globalThis, thisValue, "toContainEqual", "<green>expected<r>")?;

    const not = client.flags.not;
    let pass = false;

    const value_type = value.jsType();
    const expected_type = expected.jsType();

    if (value_type.isArrayLike()) {
        let itr = value.arrayIterator(globalThis)?;
        while (item := itr.next()?) {
            if (item.jestDeepEquals(expected, globalThis)?) {
                pass = true;
                break;
            }
        }
    } else if (value_type.isStringLike() && expected_type.isStringLike()) {
        if (expected_type.isStringObjectLike() && value_type.isString()) {
            pass = false;
        } else {
            const value_string = value.toSliceOrNull(globalThis)?;
            defer value_string.deinit();
            const expected_string = expected.toSliceOrNull(globalThis)?;
            defer expected_string.deinit();

            // jest does not have a `typeof === "string"` check for `toContainEqual`.
            // it immediately spreads the value into an array.

            let expected_codepoint_cursor = strings.CodepointIterator.Cursor{};
            let expected_iter = strings.CodepointIterator.init(expected_string.slice());
            * = expected_iter.next(&expected_codepoint_cursor);

            if (expected_iter.next(&expected_codepoint_cursor)) {
                pass = false;
            } else {
                pass = strings.indexOf(value_string.slice(), expected_string.slice()) != null;
            }
        }
    } else if (value.isIterable(globalThis)?) {
        let expected_entry = ExpectedEntry{
            globalThis: globalThis,
            expected: expected,
            pass: &pass,
        };
        value.forEach(globalThis, &expected_entry, deepEqualsIterator)?;
    } else {
        return globalThis.throw("Received value must be an array type, or both received and expected values must be strings.", .{});
    }

    if (not) pass = !pass;
    if (pass) return thisValue;

    // handle failure
    let formatter = jsc.ConsoleObject.Formatter{ globalThis: globalThis, quote_strings: true };
    defer formatter.deinit();
    const value_fmt = value.toFmt(&formatter);
    const expected_fmt = expected.toFmt(&formatter);
    if (not) {
        const expected_line = "Expected to not contain: <green>{f}<r>\n";
        const signature = comptime getSignature("toContainEqual", "<green>expected<r>", true);
        return client.throw(globalThis, signature, "\n\n" ++ expected_line, .{ expected_fmt });
    }

    const expected_line = "Expected to contain: <green>{f}<r>\n";
    const received_line = "Received: <red>{f}<r>\n";
    const signature = comptime getSignature("toContainEqual", "<green>expected<r>", false);
    return client.throw(globalThis, signature, "\n\n" ++ expected_line ++ received_line, .{ expected_fmt, value_fmt });
}

import bun from "bun";
const strings = bun.strings;

const jsc = bun.jsc;
const CallFrame = bun.jsc.CallFrame;
const JSGlobalObject = bun.jsc.JSGlobalObject;
const JSValue = bun.jsc.JSValue;

const Expect = bun.jsc.Expect.Expect;
const getSignature = Expect.getSignature;
