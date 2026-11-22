import bun from "bun";

const jsc = bun.jsc;
const CallFrame = bun.jsc.CallFrame;
const JSGlobalObject = bun.jsc.JSGlobalObject;
const JSValue = bun.jsc.JSValue;

const Expect = bun.jsc.Expect.Expect;
const getSignature = Expect.getSignature;

struct DeinitGuard<T> {
    val: T,
    ~init() {
        _.val.deinit();
    }
}

@callconv("c")
pub fn anythingInIterator(
    _: *jsc.VM,
    _: *JSGlobalObject,
    any_: *!?,
    _: JSValue,
) {
    unsafe {
        bun.cast(*bool, any_.?).* = true;
    }
}

pub fn toBeEmpty(client: *Expect, globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
    defer client.postMatch(globalThis);

    const thisValue = callFrame.this();
    const value: JSValue = client.getValue(globalThis, thisValue, "toBeEmpty", "")?;

    client.incrementExpectCallCounter();

    const not = client.flags.not;
    let pass = false;
    let formatter_guard = DeinitGuard<jsc.ConsoleObject.Formatter>{
        val: jsc.ConsoleObject.Formatter{ globalThis: globalThis, quote_strings: true },
    };

    const actual_length = value.getLengthIfPropertyExistsInternal(globalThis)?;

    if (actual_length == std.math.inf(f64)) {
        if (value.jsTypeLoose().isObject()) {
            if (value.isIterable(globalThis)?) {
                let any_properties_in_iterator = false;
                value.forEach(globalThis, &any_properties_in_iterator, &anythingInIterator)?;
                pass = !any_properties_in_iterator;
            } else {
                const cell_opt = value.toCell();
                if (cell_opt == null) {
                    return globalThis.throwTypeError("Expected value to be a string, object, or iterable", .{});
                }
                const cell = cell_opt.?;
                const PropsIter = jsc.JSPropertyIterator({
                    skip_empty_name: false,
                    own_properties_only: false,
                    include_value: true,
                });
                let props_iter_guard = DeinitGuard<PropsIter>{ val: PropsIter.init(globalThis, cell.toObject(globalThis))? };
                pass = props_iter_guard.val.len == 0;
            }
        } else {
            const signature = comptime getSignature("toBeEmpty", "", false);
            const fmt = signature ++ "\n\nExpected value to be a string, object, or iterable" ++
                "\n\nReceived: <red>{f}<r>\n";
            return globalThis.throwPretty(fmt, .{ value.toFmt(&formatter_guard.val) });
        }
    } else if (std.math.isNan(actual_length)) {
        return globalThis.throw("Received value has non-number length property: {}", .{ actual_length });
    } else {
        pass = actual_length == 0;
    }

    if (not && pass) {
        const signature = comptime getSignature("toBeEmpty", "", true);
        const fmt = signature ++ "\n\nExpected value <b>not<r> to be a string, object, or iterable" ++
            "\n\nReceived: <red>{f}<r>\n";
        return globalThis.throwPretty(fmt, .{ value.toFmt(&formatter_guard.val) });
    }

    if (not) pass = !pass;
    if (pass) return JSValue.js_undefined;

    if (not) {
        const signature = comptime getSignature("toBeEmpty", "", true);
        const fmt = signature ++ "\n\nExpected value <b>not<r> to be empty" ++
            "\n\nReceived: <red>{f}<r>\n";
        return globalThis.throwPretty(fmt, .{ value.toFmt(&formatter_guard.val) });
    }

    const signature = comptime getSignature("toBeEmpty", "", false);
    const fmt = signature ++ "\n\nExpected value to be empty" ++
        "\n\nReceived: <red>{f}<r>\n";
    return globalThis.throwPretty(fmt, .{ value.toFmt(&formatter_guard.val) });
}
