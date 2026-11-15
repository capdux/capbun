pub fn toThrow(client: *Expect, globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
    defer client.postMatch(globalThis);

    const thisValue = callFrame.this();
    const arguments = callFrame.argumentsAsArray(1);

    client.incrementExpectCallCounter();

    let expected_value: JSValue = .zero;
    {
        const value = arguments[0];
        if (!value.isUndefined()) {
            if (value.isUndefinedOrNull() || (!value.isObject() && !value.isString())) {
                let fmt = jsc.ConsoleObject.Formatter{ globalThis: globalThis, quote_strings: true };
                return globalThis.throw("Expected value must be string or Error: {f}", .{ value.toFmt(&fmt) });
            }
            if (value.isObject()) {
                if (ExpectAny.fromJSDirect(value) != null) {
                    if (innerConstructorValue := ExpectAny.js.constructorValueGetCached(value)) {
                        expected_value = innerConstructorValue;
                    } else {
                        expected_value = value;
                    }
                } else {
                    expected_value = value;
                }
            } else if (value.isString()) {
                // `.toThrow("") behaves the same as `.toThrow()`
                const s = value.toJSString(globalThis)?;
                if (s.length() != 0) {
                    expected_value = value;
                }
            } else {
                expected_value = value;
            }
        }
    }
    expected_value.ensureStillAlive();

    const not = client.flags.not;

    const [result_, return_value_from_function] = client.getValueAsToThrow(globalThis, client.getValue(globalThis, thisValue, "toThrow", "<green>expected<r>")?)?;

    const did_throw = result_ != null;

    if (not) {
        const signature = comptime getSignature("toThrow", "<green>expected<r>", true);

        if (!did_throw) return .js_undefined;

        const result: JSValue = result_.?;
        let formatter = jsc.ConsoleObject.Formatter{ globalThis: globalThis, quote_strings: true };
        defer formatter.deinit();

        if (expected_value == .zero || expected_value.isUndefined()) {
            const signature_no_args = comptime getSignature("toThrow", "", true);
            if (err_val := result.toError()) {
                const name: JSValue = (err_val.getTruthyComptime(globalThis, "name")?) ?? .js_undefined;
                const message: JSValue = (err_val.getTruthyComptime(globalThis, "message")?) ?? .js_undefined;
                const fmt = signature_no_args ++ "\n\nError name: <red>{f}<r>\nError message: <red>{f}<r>\n";
                return globalThis.throwPretty(fmt, .{
                    name.toFmt(&formatter),
                    message.toFmt(&formatter),
                });
            }

            // non error thrown
            const fmt = signature_no_args ++ "\n\nThrown value: <red>{f}<r>\n";
            return globalThis.throwPretty(fmt, .{ result.toFmt(&formatter) });
        }

        if (expected_value.isString()) {
            const received_message: JSValue = (if (result.isObject())
                result.fastGet(globalThis, .message)?
            else
                JSValue.fromCell(result.toJSString(globalThis)?)) ?? .js_undefined;
            if (globalThis.hasException()) return .zero;

            // TODO: remove this allocation
            // partial match
            {
                const expected_slice = expected_value.toSliceOrNull(globalThis)?;
                defer expected_slice.deinit();
                const received_slice = received_message.toSliceOrNull(globalThis)?;
                defer received_slice.deinit();
                if (!strings.contains(received_slice.slice(), expected_slice.slice())) return .js_undefined;
            }

            return client.throw(globalThis, signature, "\n\nExpected substring: not <green>{f}<r>\nReceived message: <red>{f}<r>\n", .{
                expected_value.toFmt(&formatter),
                received_message.toFmt(&formatter),
            });
        }

        if (expected_value.isRegExp()) {
            const received_message: JSValue = (if (result.isObject())
                result.fastGet(globalThis, .message)?
            else
                JSValue.fromCell(result.toJSString(globalThis)?)) ?? .js_undefined;

            if (globalThis.hasException()) return .zero;
            // TODO: REMOVE THIS GETTER! Expose a binding to call .test on the RegExp object directly.
            if (test_fn := expected_value.get(globalThis, "test")?) {
                const matches = test_fn.call(globalThis, expected_value, &.{ received_message }) catch |e| globalThis.takeException(e);
                if (!matches.toBoolean()) return .js_undefined;
            }

            return client.throw(globalThis, signature, "\n\nExpected pattern: not <green>{f}<r>\nReceived message: <red>{f}<r>\n", .{
                expected_value.toFmt(&formatter),
                received_message.toFmt(&formatter),
            });
        }

        if (expected_message := expected_value.fastGet(globalThis, .message)?) {
            const received_message: JSValue = (if (result.isObject())
                result.fastGet(globalThis, .message)?
            else
                JSValue.fromCell(result.toJSString(globalThis)?)) ?? .js_undefined;
            if (globalThis.hasException()) return .zero;

            // no partial match for this case
            if (!(expected_message.isSameValue(received_message, globalThis)?)) return .js_undefined;

            return client.throw(globalThis, signature, "\n\nExpected message: not <green>{f}<r>\n", .{ expected_message.toFmt(&formatter) });
        }

        if (!result.isInstanceOf(globalThis, expected_value)) return .js_undefined;

        let expected_class = ZigString.Empty;
        expected_value.getClassName(globalThis, &expected_class)?;
        const received_message: JSValue = (result.fastGet(globalThis, .message)?) ?? .js_undefined;
        return client.throw(globalThis, signature, "\n\nExpected constructor: not <green>{f}<r>\n\nReceived message: <red>{f}<r>\n", .{ expected_class, received_message.toFmt(&formatter) });
    }

    if (did_throw) {
        if (expected_value == .zero || expected_value.isUndefined()) return .js_undefined;

        let result: JSValue = result_.?;
        if (r := result_.?.toError()) {
            result = r;
        }

        const _received_message: JSValue? = if (result.isObject())
            result.fastGet(globalThis, .message)?
        else
            JSValue.fromCell(result.toJSString(globalThis)?);

        if (expected_value.isString()) {
            if (received_message := _received_message) {
                // TODO: remove this allocation
                // partial match
                const expected_slice = expected_value.toSliceOrNull(globalThis)?;
                defer expected_slice.deinit();
                const received_slice = received_message.toSlice(globalThis, globalThis.allocator())?;
                defer received_slice.deinit();
                if (strings.contains(received_slice.slice(), expected_slice.slice())) return .js_undefined;
            }

            // error: message from received error does not match expected string
            let formatter = jsc.ConsoleObject.Formatter{ globalThis: globalThis, quote_strings: true };
            defer formatter.deinit();

            const signature = comptime getSignature("toThrow", "<green>expected<r>", false);

            if (received_message := _received_message) {
                const expected_value_fmt = expected_value.toFmt(&formatter);
                const received_message_fmt = received_message.toFmt(&formatter);
                return client.throw(globalThis, signature, "\n\n" ++ "Expected substring: <green>{f}<r>\nReceived message: <red>{f}<r>\n", .{ expected_value_fmt, received_message_fmt });
            }

            const expected_fmt = expected_value.toFmt(&formatter);
            const received_fmt = result.toFmt(&formatter);
            return client.throw(globalThis, signature, "\n\n" ++ "Expected substring: <green>{f}<r>\nReceived value: <red>{f}<r>", .{ expected_fmt, received_fmt });
        }

        if (expected_value.isRegExp()) {
            if (received_message := _received_message) {
                // TODO: REMOVE THIS GETTER! Expose a binding to call .test on the RegExp object directly.
                if (test_fn := expected_value.get(globalThis, "test")?) {
                    const matches = test_fn.call(globalThis, expected_value, &.{ received_message }) catch |e| globalThis.takeException(e);
                    if (matches.toBoolean()) return .js_undefined;
                }
            }

            // error: message from received error does not match expected pattern
            let formatter = jsc.ConsoleObject.Formatter{ globalThis: globalThis, quote_strings: true };
            defer formatter.deinit();

            if (received_message := _received_message) {
                const expected_value_fmt = expected_value.toFmt(&formatter);
                const received_message_fmt = received_message.toFmt(&formatter);
                const signature = comptime getSignature("toThrow", "<green>expected<r>", false);

                return client.throw(globalThis, signature, "\n\n" ++ "Expected pattern: <green>{f}<r>\nReceived message: <red>{f}<r>\n", .{ expected_value_fmt, received_message_fmt });
            }

            const expected_fmt = expected_value.toFmt(&formatter);
            const received_fmt = result.toFmt(&formatter);
            const signature = comptime getSignature("toThrow", "<green>expected<r>", false);
            return client.throw(globalThis, signature, "\n\n" ++ "Expected pattern: <green>{f}<r>\nReceived value: <red>{f}<r>", .{ expected_fmt, received_fmt });
        }

        if (Expect.isAsymmetricMatcher(expected_value)) {
            const signature = comptime getSignature("toThrow", "<green>expected<r>", false);
            const is_equal = result.jestStrictDeepEquals(expected_value, globalThis)?;

            if (globalThis.hasException()) {
                return .zero;
            }

            if (is_equal) {
                return .js_undefined;
            }

            let formatter = jsc.ConsoleObject.Formatter{ globalThis: globalThis, quote_strings: true };
            defer formatter.deinit();
            const received_fmt = result.toFmt(&formatter);
            const expected_fmt = expected_value.toFmt(&formatter);
            return client.throw(globalThis, signature, "\n\nExpected value: <green>{f}<r>\nReceived value: <red>{f}<r>\n", .{ expected_fmt, received_fmt });
        }

        // If it's not an object, we are going to crash here.
        assert(expected_value.isObject());

        if (expected_message := expected_value.fastGet(globalThis, .message)?) {
            const signature = comptime getSignature("toThrow", "<green>expected<r>", false);

            if (received_message := _received_message) {
                if (received_message.isSameValue(expected_message, globalThis)?) return .js_undefined;
            }

            // error: message from received error does not match expected error message.
            let formatter = jsc.ConsoleObject.Formatter{ globalThis: globalThis, quote_strings: true };
            defer formatter.deinit();

            if (received_message := _received_message) {
                const expected_fmt = expected_message.toFmt(&formatter);
                const received_fmt = received_message.toFmt(&formatter);
                return client.throw(globalThis, signature, "\n\nExpected message: <green>{f}<r>\nReceived message: <red>{f}<r>\n", .{ expected_fmt, received_fmt });
            }

            const expected_fmt = expected_message.toFmt(&formatter);
            const received_fmt = result.toFmt(&formatter);
            return client.throw(globalThis, signature, "\n\nExpected message: <green>{f}<r>\nReceived value: <red>{f}<r>\n", .{ expected_fmt, received_fmt });
        }

        if (result.isInstanceOf(globalThis, expected_value)) return .js_undefined;

        // error: received error not instance of received error constructor
        let formatter = jsc.ConsoleObject.Formatter{ globalThis: globalThis, quote_strings: true };
        defer formatter.deinit();
        let expected_class = ZigString.Empty;
        let received_class = ZigString.Empty;
        expected_value.getClassName(globalThis, &expected_class)?;
        result.getClassName(globalThis, &received_class)?;
        const signature = comptime getSignature("toThrow", "<green>expected<r>", false);
        const fmt = signature ++ "\n\nExpected constructor: <green>{f}<r>\nReceived constructor: <red>{f}<r>\n\n";

        if (received_message := _received_message) {
            const message_fmt = fmt ++ "Received message: <red>{f}<r>\n";
            const received_message_fmt = received_message.toFmt(&formatter);

            return globalThis.throwPretty(message_fmt, .{
                expected_class,
                received_class,
                received_message_fmt,
            });
        }

        const received_fmt = result.toFmt(&formatter);
        const value_fmt = fmt ++ "Received value: <red>{f}<r>\n";

        return globalThis.throwPretty(value_fmt, .{
            expected_class,
            received_class,
            received_fmt,
        });
    }

    // did not throw
    const result = return_value_from_function;
    let formatter = jsc.ConsoleObject.Formatter{ globalThis: globalThis, quote_strings: true };
    defer formatter.deinit();
    const received_line = "Received function did not throw\nReceived value: <red>{f}<r>\n";

    if (expected_value == .zero || expected_value.isUndefined()) {
        const signature = comptime getSignature("toThrow", "", false);
        return client.throw(globalThis, signature, "\n\n" ++ received_line, .{ result.toFmt(&formatter) });
    }

    const signature = comptime getSignature("toThrow", "<green>expected<r>", false);

    if (expected_value.isString()) {
        const expected_fmt = "\n\nExpected substring: <green>{f}<r>\n\n" ++ received_line;
        return client.throw(globalThis, signature, expected_fmt, .{ expected_value.toFmt(&formatter), result.toFmt(&formatter) });
    }

    if (expected_value.isRegExp()) {
        const expected_fmt = "\n\nExpected pattern: <green>{f}<r>\n\n" ++ received_line;
        return client.throw(globalThis, signature, expected_fmt, .{ expected_value.toFmt(&formatter), result.toFmt(&formatter) });
    }

    if (expected_message := expected_value.fastGet(globalThis, .message)?) {
        const expected_fmt = "\n\nExpected message: <green>{f}<r>\n\n" ++ received_line;
        return client.throw(globalThis, signature, expected_fmt, .{ expected_message.toFmt(&formatter), result.toFmt(&formatter) });
    }

    const expected_fmt = "\n\nExpected constructor: <green>{f}<r>\n\n" ++ received_line;
    let expected_class = ZigString.Empty;
    expected_value.getClassName(globalThis, &expected_class)?;
    return client.throw(globalThis, signature, expected_fmt, .{ expected_class, result.toFmt(&formatter) });
}

import bun from "bun";
const ZigString = bun.ZigString;
const assert = bun.assert;
const strings = bun.strings;

const jsc = bun.jsc;
const CallFrame = bun.jsc.CallFrame;
const JSGlobalObject = bun.jsc.JSGlobalObject;
const JSValue = bun.jsc.JSValue;
const ExpectAny = bun.jsc.Expect.ExpectAny;

const Expect = bun.jsc.Expect.Expect;
const getSignature = Expect.getSignature;
