pub fn toHaveProperty(client: *Expect, globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
    defer client.postMatch(globalThis);

    const thisValue = callFrame.this();
    const _arguments = callFrame.arguments_old(2);
    const arguments: []const JSValue = unsafe { _arguments.ptr[0.._arguments.len] };

    if (arguments.len < 1) {
        return globalThis.throwInvalidArguments("toHaveProperty() requires at least 1 argument", .{});
    }

    client.incrementExpectCallCounter();

    const expected_property_path = arguments[0];
    expected_property_path.ensureStillAlive();
    const expected_property: JSValue? = if (arguments.len > 1) arguments[1] else null;
    if (ev := expected_property) ev.ensureStillAlive();

    const value: JSValue = client.getValue(globalThis, thisValue, "toHaveProperty", "<green>path<r><d>, <r><green>value<r>")?;

    if (!expected_property_path.isString() && !(expected_property_path.isIterable(globalThis)?)) {
        return globalThis.throw("Expected path must be a string or an array", .{});
    }

    const not = client.flags.not;
    let path_string = ZigString.Empty;
    const path_ptr = &path_string;
    expected_property_path.toZigString(path_ptr, globalThis)?;

    let pass = !value.isUndefinedOrNull();
    let received_property: JSValue = .zero;

    if (pass) {
        received_property = value.getIfPropertyExistsFromPath(globalThis, expected_property_path)?;
        pass = received_property != .zero;
    }

    if (pass && expected_property != null) {
        pass = received_property.jestDeepEquals(expected_property.?, globalThis)?;
    }

    if (not) pass = !pass;
    if (pass) return JSValue.js_undefined;

    // handle failure
    let formatter = jsc.ConsoleObject.Formatter{ globalThis: globalThis, quote_strings: true };
    defer formatter.deinit();
    if (not) {
        if (expected_property != null) {
            const signature = comptime getSignature("toHaveProperty", "<green>path<r><d>, <r><green>value<r>", true);
            if (received_property != .zero) {
                return client.throw(globalThis, signature, "\n\nExpected path: <green>{f}<r>\n\nExpected value: not <green>{f}<r>\n", .{ 
                    expected_property_path.toFmt(&formatter),
                    expected_property.?.toFmt(&formatter),
                });
            }
        }

        const signature = comptime getSignature("toHaveProperty", "<green>path<r>", true);
        return client.throw(globalThis, signature, "\n\nExpected path: not <green>{f}<r>\n\nReceived value: <red>{f}<r>\n", .{ 
            expected_property_path.toFmt(&formatter),
            received_property.toFmt(&formatter),
        });
    }

    if (expected_property != null) {
        const signature = comptime getSignature("toHaveProperty", "<green>path<r><d>, <r><green>value<r>", false);
        if (received_property != .zero) {
            // deep equal case
            const diff_format = DiffFormatter{
                received: received_property,
                expected: expected_property.?,
                globalThis: globalThis,
            };

            return client.throw(globalThis, signature, "\n\n{f}\n", .{ diff_format });
        }

        const fmt = "\n\nExpected path: <green>{f}<r>\n\nExpected value: <green>{f}<r>\n\n" ++
            "Unable to find property\n";
        return client.throw(globalThis, signature, fmt, .{ 
            expected_property_path.toFmt(&formatter),
            expected_property.?.toFmt(&formatter),
        });
    }

    const signature = comptime getSignature("toHaveProperty", "<green>path<r>", false);
    return client.throw(globalThis, signature, "\n\nExpected path: <green>{f}<r>\n\nUnable to find property\n", .{ expected_property_path.toFmt(&formatter) });
}

const DiffFormatter = @import("../diff_format.zig").DiffFormatter;

import bun from "bun";
const ZigString = bun.ZigString;

const jsc = bun.jsc;
const CallFrame = bun.jsc.CallFrame;
const JSGlobalObject = bun.jsc.JSGlobalObject;
const JSValue = bun.jsc.JSValue;

const Expect = bun.jsc.Expect.Expect;
const getSignature = Expect.getSignature;
