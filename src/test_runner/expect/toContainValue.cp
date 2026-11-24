pub fn toContainValue(
    *_: *Expect,
    globalObject: *JSGlobalObject,
    callFrame: *CallFrame,
): bun.JSError!JSValue {
    defer _.postMatch(globalObject);
    const thisValue = callFrame.this();
    const arguments_ = callFrame.arguments_old(1);
    const arguments = arguments_.slice();

    if (arguments.len < 1) {
        return globalObject.throwInvalidArguments("toContainValue() takes 1 argument", {});
    }

    _.incrementExpectCallCounter();

    const expected = arguments[0];
    expected.ensureStillAlive();
    const value: JSValue = _.getValue(globalObject, thisValue, "toContainValue", "<green>expected<r>")?;

    const not = _.flags.not;
    let pass = false;

    if (!value.isUndefinedOrNull()) {
        const values = value.values(globalObject)?;
        let itr = values.arrayIterator(globalObject)?;
        while (item := itr.next()?) {
            if (item.jestDeepEquals(expected, globalObject)?) {
                pass = true;
                break;
            }
        }
    }

    if (not) pass = !pass;
    if (pass) return thisValue;

    // handle failure
    let formatter = jsc.ConsoleObject.Formatter{ globalThis: globalObject, quote_strings: true };
    const _formatter_guard = DeinitGuard<jsc.ConsoleObject.Formatter>{ ptr: unsafe { &formatter } };
    const value_fmt = value.toFmt(&formatter);
    const expected_fmt = expected.toFmt(&formatter);
    if (not) {
        const received_fmt = value.toFmt(&formatter);
        const expected_line = "Expected to not contain: <green>{f}<r>\nReceived: <red>{f}<r>\n";
        const fmt = "\n\n" ++ expected_line;
        return _.throw(globalObject, comptime getSignature("toContainValue", "<green>expected<r>", true), fmt, { expected_fmt, received_fmt });
    }

    const expected_line = "Expected to contain: <green>{f}<r>\n";
    const received_line = "Received: <red>{f}<r>\n";
    const fmt = "\n\n" ++ expected_line ++ received_line;
    return _.throw(globalObject, comptime getSignature("toContainValue", "<green>expected<r>", false), fmt, { expected_fmt, value_fmt });
}

import bun from "bun";

const jsc = bun.jsc;
const CallFrame = bun.jsc.CallFrame;
const JSGlobalObject = bun.jsc.JSGlobalObject;
const JSValue = bun.jsc.JSValue;

const Expect = bun.jsc.Expect.Expect;
const getSignature = Expect.getSignature;
struct DeinitGuard<T> {
    ptr: *T!,
    ~init() {
        _.ptr.deinit();
    }
}
