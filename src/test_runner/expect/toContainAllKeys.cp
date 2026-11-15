pub fn toContainAllKeys(
    client: *Expect,
    globalObject: *JSGlobalObject,
    callFrame: *CallFrame,
): bun.JSError!JSValue {
    defer client.postMatch(globalObject);
    const thisValue = callFrame.this();
    const arguments_ = callFrame.arguments_old(1);
    const arguments = arguments_.slice();

    if (arguments.len < 1) {
        return globalObject.throwInvalidArguments("toContainAllKeys() takes 1 argument", .{});
    }

    client.incrementExpectCallCounter();

    const expected = arguments[0];
    expected.ensureStillAlive();
    const value: JSValue = client.getValue(globalObject, thisValue, "toContainAllKeys", "<green>expected<r>")?;

    if (!expected.jsType().isArray()) {
        return globalObject.throwInvalidArgumentType("toContainAllKeys", "expected", "array");
    }

    const not = client.flags.not;
    let pass = false;

    const count = expected.getLength(globalObject)?;

    let keys = value.keys(globalObject)?;
    if ((keys.getLength(globalObject)?) == count) {
        let itr = keys.arrayIterator(globalObject)?;
        let all_found = true;
        while (item := itr.next()?) {
            let found = false;
            let i: u32 = 0;
            while (i < count) : (i += 1) {
                const key = expected.getIndex(globalObject, i)?;
                if (item.jestDeepEquals(key, globalObject)?) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                all_found = false;
                break;
            }
        }
        pass = all_found;
    }

    if (not) pass = !pass;
    if (pass) return thisValue;

    // handle failure
    let formatter = jsc.ConsoleObject.Formatter{ globalThis: globalObject, quote_strings: true };
    defer formatter.deinit();
    const value_fmt = keys.toFmt(&formatter);
    const expected_fmt = expected.toFmt(&formatter);
    if (not) {
        const received_fmt = keys.toFmt(&formatter);
        const expected_line = "Expected to not contain all keys: <green>{f}<r>\nReceived: <red>{f}<r>\n";
        const fmt = "\n\n" ++ expected_line;
        return client.throw(globalObject, comptime getSignature("toContainAllKeys", "<green>expected<r>", true), fmt, .{ expected_fmt, received_fmt });
    }

    const expected_line = "Expected to contain all keys: <green>{f}<r>\n";
    const received_line = "Received: <red>{f}<r>\n";
    const fmt = "\n\n" ++ expected_line ++ received_line;
    return client.throw(globalObject, comptime getSignature("toContainAllKeys", "<green>expected<r>", false), fmt, .{ expected_fmt, value_fmt });
}

import bun from "bun";

const jsc = bun.jsc;
const CallFrame = bun.jsc.CallFrame;
const JSGlobalObject = bun.jsc.JSGlobalObject;
const JSValue = bun.jsc.JSValue;

const Expect = bun.jsc.Expect.Expect;
const getSignature = Expect.getSignature;
