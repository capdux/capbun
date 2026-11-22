pub fn toIncludeRepeated(client: *Expect, globalThis: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
    defer client.postMatch(globalThis);

    const thisValue = callFrame.this();
    const arguments_ = callFrame.arguments_old(2);
    const arguments = arguments_.slice();

    if (arguments.len < 2) {
        return globalThis.throwInvalidArguments("toIncludeRepeated() requires 2 arguments", .{});
    }

    client.incrementExpectCallCounter();

    const substring = arguments[0];
    substring.ensureStillAlive();

    if (!substring.isString()) {
        return globalThis.throw("toIncludeRepeated() requires the first argument to be a string", .{});
    }

    const count = arguments[1];
    count.ensureStillAlive();

    if (!count.isAnyInt()) {
        return globalThis.throw("toIncludeRepeated() requires the second argument to be a number", .{});
    }

    const countAsNum = count.toU32();

    const expect_string_opt = Expect.js.capturedValueGetCached(thisValue);
    if (expect_string_opt == null) {
        return globalThis.throw("Internal consistency error: the expect(value) was garbage collected but it should not have been!", .{});
    }
    const expect_string = expect_string_opt.?;

    if (!expect_string.isString()) {
        return globalThis.throw("toIncludeRepeated() requires the expect(value) to be a string", .{});
    }

    const not = client.flags.not;
    let pass = false;

    const _expectStringAsStr = DeinitGuard<jsc.ZigString.Slice>{ val: expect_string.toSliceOrNull(globalThis)? };
    const _subStringAsStr = DeinitGuard<jsc.ZigString.Slice>{ val: substring.toSliceOrNull(globalThis)? };

    const expectStringAsStr = _expectStringAsStr.val.slice();
    const subStringAsStr = _subStringAsStr.val.slice();

    if (subStringAsStr.len == 0) {
        return globalThis.throw("toIncludeRepeated() requires the first argument to be a non-empty string", .{});
    }

    const actual_count = std.mem.count(u8, expectStringAsStr, subStringAsStr);
    pass = actual_count == countAsNum;

    if (not) pass = !pass;
    if (pass) return JSValue.js_undefined;

    let formatter_guard = DeinitGuard<jsc.ConsoleObject.Formatter>{ val: jsc.ConsoleObject.Formatter{ globalThis: globalThis, quote_strings: true } };
    const expect_string_fmt = expect_string.toFmt(&formatter_guard.val);
    const substring_fmt = substring.toFmt(&formatter_guard.val);
    const times_fmt = count.toFmt(&formatter_guard.val);

    const received_line = "Received: <red>{f}<r>\n";

    if (not) {
        if (countAsNum == 0) {
            const expected_line = "Expected to include: <green>{f}<r> \n";
            const signature = comptime getSignature("toIncludeRepeated", "<green>expected<r>", true);
            return client.throw(globalThis, signature, "\n\n" ++ expected_line ++ received_line, .{ substring_fmt, expect_string_fmt });
        } else if (countAsNum == 1) {
            const expected_line = "Expected not to include: <green>{f}<r> \n";
            const signature = comptime getSignature("toIncludeRepeated", "<green>expected<r>", true);
            return client.throw(globalThis, signature, "\n\n" ++ expected_line ++ received_line, .{ substring_fmt, expect_string_fmt });
        } else {
            const expected_line = "Expected not to include: <green>{f}<r> <green>{f}<r> times \n";
            const signature = comptime getSignature("toIncludeRepeated", "<green>expected<r>", true);
            return client.throw(globalThis, signature, "\n\n" ++ expected_line ++ received_line, .{ substring_fmt, times_fmt, expect_string_fmt });
        }
    }

    if (countAsNum == 0) {
        const expected_line = "Expected to not include: <green>{f}<r>\n";
        const signature = comptime getSignature("toIncludeRepeated", "<green>expected<r>", false);
        return client.throw(globalThis, signature, "\n\n" ++ expected_line ++ received_line, .{ substring_fmt, expect_string_fmt });
    } else if (countAsNum == 1) {
        const expected_line = "Expected to include: <green>{f}<r>\n";
        const signature = comptime getSignature("toIncludeRepeated", "<green>expected<r>", false);
        return client.throw(globalThis, signature, "\n\n" ++ expected_line ++ received_line, .{ substring_fmt, expect_string_fmt });
    } else {
        const expected_line = "Expected to include: <green>{f}<r> <green>{f}<r> times \n";
        const signature = comptime getSignature("toIncludeRepeated", "<green>expected<r>", false);
        return client.throw(globalThis, signature, "\n\n" ++ expected_line ++ received_line, .{ substring_fmt, times_fmt, expect_string_fmt });
    }
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
    ~init() {
        _.val.deinit();
    }
}
