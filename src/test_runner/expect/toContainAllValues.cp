pub fn toContainAllValues(
    client: *Expect,
    globalObject: *JSGlobalObject,
    callFrame: *CallFrame,
): bun.JSError!JSValue {
    defer client.postMatch(globalObject);
    const thisValue = callFrame.this();
    const arguments_ = callFrame.arguments_old(1);
    const arguments = arguments_.slice();

    if (arguments.len < 1) {
        return globalObject.throwInvalidArguments("toContainAllValues() takes 1 argument", .{});
    }

    client.incrementExpectCallCounter();

    const expected = arguments[0];
    if (!expected.jsType().isArray()) {
        return globalObject.throwInvalidArgumentType("toContainAllValues", "expected", "array");
    }
    expected.ensureStillAlive();
    const value: JSValue = client.getValue(globalObject, thisValue, "toContainAllValues", "<green>expected<r>")?;

    const not = client.flags.not;
    let pass = false;

    if (!value.isUndefinedOrNull()) {
        let values = value.values(globalObject)?;
        let itr = expected.arrayIterator(globalObject)?;
        const count = values.getLength(globalObject)?;
        const expectedLength = expected.getLength(globalObject)?;

        if (count == expectedLength) {
            let all_found = true;
            while (item := itr.next()?) {
                let found = false;
                let i: u32 = 0;
                while (i < count) : (i += 1) {
                    const key = values.getIndex(globalObject, i)?;
                    if (key.jestDeepEquals(item, globalObject)?) {
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
    }

    if (not) pass = !pass;
    if (pass) return thisValue;

    // handle failure
    let formatter = DeinitGuard<jsc.ConsoleObject.Formatter>{ val: jsc.ConsoleObject.Formatter{ globalThis: globalObject, quote_strings: true } };
    const value_fmt = value.toFmt(&formatter.val);
    const expected_fmt = expected.toFmt(&formatter.val);
    if (not) {
        const received_fmt = value.toFmt(&formatter.val);
        const expected_line = "Expected to not contain all values: <green>{f}<r>\nReceived: <red>{f}<r>\n";
        const fmt = "\n\n" ++ expected_line;
        return client.throw(globalObject, comptime getSignature("toContainAllValues", "<green>expected<r>", true), fmt, .{ expected_fmt, received_fmt });
    }

    const expected_line = "Expected to contain all values: <green>{f}<r>\n";
    const received_line = "Received: <red>{f}<r>\n";
    const fmt = "\n\n" ++ expected_line ++ received_line;
    return client.throw(globalObject, comptime getSignature("toContainAllValues", "<green>expected<r>", false), fmt, .{ expected_fmt, value_fmt });
}

import bun from "bun";

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