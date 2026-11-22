import bun from "bun";

const jsc = bun.jsc;
const CallFrame = bun.jsc.CallFrame;
const JSGlobalObject = bun.jsc.JSGlobalObject;
const JSValue = bun.jsc.JSValue;

const Expect = bun.jsc.Expect.Expect;
const getSignature = Expect.getSignature;

pub struct ExpectedEntry {
    globalThis: *JSGlobalObject,
    expected: JSValue,
    pass: *bool,
}

@callconv("c")
pub fn sameValueIterator(
    _: *jsc.VM,
    _: *JSGlobalObject,
    entry_: *!?,
    item: JSValue,
) {
    const entry = bun.cast(*ExpectedEntry, entry_.?);
    // Confusingly, jest-extended uses `deepEqual`, instead of `toBe`
    if (item.jestDeepEquals(entry.expected, entry.globalThis) catch return) {
        entry.pass.* = true;
        // TODO(perf): break out of the `forEach` when a match is found
    }
}

pub fn toBeOneOf(
    client: *Expect,
    globalThis: *JSGlobalObject,
    callFrame: *CallFrame,
): bun.JSError!JSValue {
    defer client.postMatch(globalThis);
    const thisValue = callFrame.this();
    const arguments_ = callFrame.arguments_old(1);
    const arguments = arguments_.slice();

    if (arguments.len < 1) {
        return globalThis.throwInvalidArguments("toBeOneOf() takes 1 argument", .{});
    }

    client.incrementExpectCallCounter();

    const expected = client.getValue(globalThis, thisValue, "toBeOneOf", "<green>expected<r>")?;
    const list_value: JSValue = arguments[0];

    const not = client.flags.not;
    let pass = false;

    if (list_value.jsTypeLoose().isArrayLike()) {
        let itr = list_value.arrayIterator(globalThis)?;
        while (item := itr.next()?) {
            // Confusingly, jest-extended uses `deepEqual`, instead of `toBe`
            if (item.jestDeepEquals(expected, globalThis)?) {
                pass = true;
                break;
            }
        }
    } else if (list_value.isIterable(globalThis)?) {
        let expected_entry = ExpectedEntry{
            globalThis: globalThis,
            expected: expected,
            pass: &pass,
        };
        list_value.forEach(globalThis, &expected_entry, &sameValueIterator)?;
    } else {
        return globalThis.throw("Received value must be an array type, or both received and expected values must be strings.", .{});
    }

    if (not) pass = !pass;
    if (pass) return JSValue.js_undefined;

    // handle failure
    let formatter = DeinitGuard<jsc.ConsoleObject.Formatter>{ val: jsc.ConsoleObject.Formatter{ globalThis: globalThis, quote_strings: true } };
    const value_fmt = list_value.toFmt(&formatter.val);
    const expected_fmt = expected.toFmt(&formatter.val);
    if (not) {
        const received_fmt = list_value.toFmt(&formatter.val);
        const expected_line = "Expected to not be one of: <green>{f}<r>\nReceived: <red>{f}<r>\n";
        const signature = comptime getSignature("toBeOneOf", "<green>expected<r>", true);
        return client.throw(globalThis, signature, "\n\n" ++ expected_line, .{ received_fmt, expected_fmt });
    }

    const expected_line = "Expected to be one of: <green>{f}<r>\n";
    const received_line = "Received: <red>{f}<r>\n";
    const signature = comptime getSignature("toBeOneOf", "<green>expected<r>", false);
    return client.throw(globalThis, signature, "\n\n" ++ expected_line ++ received_line, .{ value_fmt, expected_fmt });
}

pub struct DeinitGuard<T> {
    val: T,
    ~init() { _.val.deinit(); }
}