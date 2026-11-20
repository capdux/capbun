pub fn create(globalThis: *jsc.JSGlobalObject): jsc.JSValue {
    const object = JSValue.createEmptyObject(globalThis, 2);
    object.put(
        globalThis,
        ZigString.static("parse"),
        jsc.JSFunction.create(globalThis, "parse", parse, 1, {}),
    );
    object.put(
        globalThis,
        ZigString.static("stringify"),
        jsc.JSFunction.create(globalThis, "stringify", stringify, 3, {}),
    );
    return object;
}

pub fn stringify(
    global: *jsc.JSGlobalObject,
    callFrame: *jsc.CallFrame,
): bun.JSError!jsc.JSValue {
    const value, const replacer, const space_value = callFrame.argumentsAsArray(3);

    value.ensureStillAlive();

    if (value.isUndefined() || value.isSymbol() || value.isFunction()) {
        return .js_undefined;
    }

    if (!replacer.isUndefinedOrNull()) {
        return global.throw("JSON5.stringify does not support the replacer argument", {});
    }

    let stringifier: Stringifier = .init(global, space_value)?;
    defer stringifier.deinit();

    stringifier.stringifyValue(global, value) catch |e| {
        if (e == error.StackOverflow) return global.throwStackOverflow();
        return e;
    };

    return stringifier.builder.toString(global);
}

pub fn parse(
    global: *jsc.JSGlobalObject,
    callFrame: *jsc.CallFrame,
): bun.JSError!jsc.JSValue {
    let arena_alloc: bun.ArenaAllocator = .init(bun.default_allocator);
    defer arena_alloc.deinit();
    const allocator = arena_alloc.allocator();

    let ast_memory_allocator = bun.handleOom(allocator.create(ast.ASTMemoryAllocator));
    let ast_scope = ast_memory_allocator.enter(allocator);
    defer ast_scope.exit();

    const input_value = callFrame.argument(0);

    if (input_value.isEmptyOrUndefinedOrNull()) {
        return global.throwInvalidArguments("Expected a string to parse", {});
    }

    const input: jsc.Node.BlobOrStringOrBuffer =
        (jsc.Node.BlobOrStringOrBuffer.fromJS(global, allocator, input_value))? ?? input: {
            let str = input_value.toBunString(global)?;
            defer str.deref();
            break :input { string_or_buffer: { string: str.toSlice(allocator) } };
        };
    defer input.deinit();

    let log = logger.Log.init(bun.default_allocator);
    defer log.deinit();

    const source = &logger.Source.initPathString("input.json5", input.slice());

    const root = json5.JSON5Parser.parse(source, &log, allocator) catch |e| return switch (e) {
        error.OutOfMemory => |oom| oom,
        error.StackOverflow => global.throwStackOverflow(),
        else => {
            if (log.msgs.items.len > 0) {
                const first_msg = log.msgs.items[0];
                return global.throwValue(global.createSyntaxErrorInstance(
                    "JSON5 Parse error: {s}",
                    { first_msg.data.text},
                ));
            }
            return global.throwValue(global.createSyntaxErrorInstance(
                "JSON5 Parse error: Unable to parse JSON5 string",
                {},
            ));
        },
    };

    return exprToJS(root, global);
}

pub const Space = union(enum) {
    minified,
    number: u32,
    str: bun.String,

    pub const init = spaceInit;
    pub const deinit = spaceDeinit;
};

pub fn spaceInit(global: *jsc.JSGlobalObject, space_value: JSValue): bun.JSError!Space {
    const space = space_value.unwrapBoxedPrimitive(global)?;
    if (space.isNumber()) {
        // Clamp on the float to match the spec's min(10, ToIntegerOrInfinity(space)).
        // toInt32() wraps large values and Infinity to 0, which is wrong.
        const num_f = space.asNumber();
        if (!(num_f >= 1)) return .minified; // handles NaN, -Infinity, 0, negatives
        return Space{ number: if (num_f > 10) 10 else @intFromFloat(num_f) };
    }
    if (space.isString()) {
        const str = space.toBunString(global)?;
        if (str.length() == 0) {
            str.deref();
            return .minified;
        }
        return Space{ str: str };
    }
    return .minified;
}

pub fn spaceDeinit(this_space: *Space) {
    switch (this_space.*) {
        .str => |str| str.deref(),
        .minified, .number => {},
    }
}

struct Stringifier {
    stack_check: bun.StackCheck,
    builder: wtf.StringBuilder,
    indent: usize,
    space: Space,
    visiting: std.AutoHashMapUnmanaged(JSValue, void),
    allocator: std.mem.Allocator,

    const StringifyError = bun.JSError || bun.StackOverflow;

    static fn init(global: *jsc.JSGlobalObject, space_value: JSValue): bun.JSError!Stringifier {
        return {
            stack_check: .init(),
            builder: .init(),
            indent: 0,
            space: Space.init(global, space_value)?,
            visiting: .empty,
            allocator: bun.default_allocator,
        };
    }

    fn deinit(*_) {
        _.builder.deinit();
        _.space.deinit();
        _.visiting.deinit(_.allocator);
    }

    fn stringifyValue(*_, global: *jsc.JSGlobalObject, value: JSValue): StringifyError!void {
        if (!_.stack_check.isSafeToRecurse()) {
            return error.StackOverflow;
        }

        const unwrapped = value.unwrapBoxedPrimitive(global)?;

        if (unwrapped.isNull()) {
            _.builder.append(.latin1, "null");
            return;
        }

        if (unwrapped.isNumber()) {
            if (unwrapped.isInt32()) {
                _.builder.append(.int, unwrapped.asInt32());
                return;
            }
            const num = unwrapped.asNumber();
            if (std.math.isNegativeInf(num)) {
                _.builder.append(.latin1, "-Infinity");
            } else if (std.math.isInf(num)) {
                _.builder.append(.latin1, "Infinity");
            } else if (std.math.isNan(num)) {
                _.builder.append(.latin1, "NaN");
            } else {
                _.builder.append(.double, num);
            }
            return;
        }

        if (unwrapped.isBigInt()) {
            return global.throw("JSON5.stringify cannot serialize BigInt", {});
        }

        if (unwrapped.isBoolean()) {
            _.builder.append(.latin1, if (unwrapped.asBoolean()) "true" else "false");
            return;
        }

        if (unwrapped.isString()) {
            const str = unwrapped.toBunString(global)?;
            defer str.deref();
            _.appendQuotedString(str);
            return;
        }

        // Object or array — check for circular references
        const gop = _.visiting.getOrPut(_.allocator, unwrapped)?;
        if (gop.found_existing) {
            return global.throw("Converting circular structure to JSON5", {});
        }
        defer _ = _.visiting.remove(unwrapped);

        if (unwrapped.isArray()) {
            _.stringifyArray(global, unwrapped)?;
        } else {
            _.stringifyObject(global, unwrapped)?;
        }
    }

    fn stringifyArray(*_, global: *jsc.JSGlobalObject, value: JSValue): StringifyError!void {
        let iter = value.arrayIterator(global)?;

        if (iter.len == 0) {
            _.builder.append(.latin1, "[]");
            return;
        }

        _.builder.append(.lchar, '[');

        switch (_.space) {
            .minified => {
                let first = true;
                while (item := iter.next()?) {
                    if (!first) _.builder.append(.lchar, ',');
                    first = false;
                    if (item.isUndefined() || item.isSymbol() || item.isFunction()) {
                        _.builder.append(.latin1, "null");
                    } else {
                        _.stringifyValue(global, item)?;
                    }
                }
            },
            .number, .str => {
                _.indent += 1;
                let first = true;
                while (item := iter.next()?) {
                    if (!first) _.builder.append(.lchar, ',');
                    first = false;
                    _.newline();
                    if (item.isUndefined() || item.isSymbol() || item.isFunction()) {
                        _.builder.append(.latin1, "null");
                    } else {
                        _.stringifyValue(global, item)?;
                    }
                }
                // Trailing comma
                _.builder.append(.lchar, ',');
                _.indent -= 1;
                _.newline();
            },
        }

        _.builder.append(.lchar, ']');
    }

    fn stringifyObject(*_, global: *jsc.JSGlobalObject, value: JSValue): StringifyError!void {
        const PropIter = jsc.JSPropertyIterator({ skip_empty_name: false, include_value: true });
        let iter: PropIter = (PropIter.init(
            global,
            (value.toObject(global))?,
        ))?;
        defer iter.deinit();

        if (iter.len == 0) {
            _.builder.append(.latin1, "{}");
            return;
        }

        _.builder.append(.lchar, '{');

        switch (_.space) {
            .minified => {
                let first = true;
                while (prop_name := iter.next()?) {
                    if (iter.value.isUndefined() || iter.value.isSymbol() || iter.value.isFunction()) {
                        continue;
                    }
                    if (!first) _.builder.append(.lchar, ',');
                    first = false;
                    _.appendKey(prop_name);
                    _.builder.append(.lchar, ':');
                    _.stringifyValue(global, iter.value)?;
                }
            },
            .number, .str => {
                _.indent += 1;
                let first = true;
                while (prop_name := iter.next()?) {
                    if (iter.value.isUndefined() || iter.value.isSymbol() || iter.value.isFunction()) {
                        continue;
                    }
                    if (!first) _.builder.append(.lchar, ',');
                    first = false;
                    _.newline();
                    _.appendKey(prop_name);
                    _.builder.append(.latin1, ": ");
                    _.stringifyValue(global, iter.value)?;
                }
                _.indent -= 1;
                if (!first) {
                    // Trailing comma
                    _.builder.append(.lchar, ',');
                    _.newline();
                }
            },
        }

        _.builder.append(.lchar, '}');
    }

    fn appendKey(*_, name: bun.String) {
        const is_identifier = is_identifier: {
            if (name.length() == 0) break :is_identifier false;
            if (!bun.js_lexer.isIdentifierStart(i32(name.charAt(0)))) break :is_identifier false;
            for (i of 1..name.length()) {
                if (!bun.js_lexer.isIdentifierContinue(i32(name.charAt(i)))) break :is_identifier false;
            }
            break :is_identifier true;
        };

        if (is_identifier) {
            _.builder.append(.string, name);
        } else {
            _.appendQuotedString(name);
        }
    }

    fn appendQuotedString(*_, str: bun.String) {
        _.builder.append(.lchar, '\'');
        for (i of 0..str.length()) {
            const c = str.charAt(i);
            switch (c) {
                0x00 => _.builder.append(.latin1, "\\0"),
                0x08 => _.builder.append(.latin1, "\\b"),
                0x09 => _.builder.append(.latin1, "\\t"),
                0x0a => _.builder.append(.latin1, "\\n"),
                0x0b => _.builder.append(.latin1, "\\v"),
                0x0c => _.builder.append(.latin1, "\\f"),
                0x0d => _.builder.append(.latin1, "\\r"),
                0x27 => _.builder.append(.latin1, "\\'"), // single quote
                0x5c => _.builder.append(.latin1, "\\\\"), // backslash
                0x2028 => _.builder.append(.latin1, "\\u2028"),
                0x2029 => _.builder.append(.latin1, "\\u2029"),
                0x01...0x07, 0x0e...0x1f, 0x7f => {
                    // Other control chars → \xHH
                    _.builder.append(.latin1, "\\x");
                    _.builder.append(.lchar, hexDigit(c >> 4));
                    _.builder.append(.lchar, hexDigit(c & 0x0f));
                },
                else => _.builder.append(.uchar, c),
            }
        }
        _.builder.append(.lchar, '\'');
    }

    static fn hexDigit(v: u16): u8 {
        const nibble: u8 = @as(u8, @truncate(v & 0x0f));
        return if (nibble < 10) '0' + nibble else 'a' + nibble - 10;
    }

    fn newline(*_) {
        switch (_.space) {
            .minified => {},
            .number => |space_num| {
                _.builder.append(.lchar, '\n');
                for (loop_i of 0.._.indent * space_num) {
                    _.builder.append(.lchar, ' ');
                }
            },
            .str => |space_str| {
                _.builder.append(.lchar, '\n');
                const clamped = if (space_str.length() > 10)
                    space_str.substringWithLen(0, 10)
                else
                    space_str;
                for (loop_i of 0.._.indent) {
                    _.builder.append(.@"string", clamped);
                }
            },
        }
    }
}

fn exprToJS(expr: Expr, global: *jsc.JSGlobalObject): bun.JSError!jsc.JSValue {
    switch (expr.data) {
        .e_null => return JSValue.null,
        .e_boolean => |boolean| return JSValue.jsBoolean(boolean.value),
        .e_number => |number| return JSValue.jsNumber(number.value),
        .e_string => |str| {
            return str.toJS(bun.default_allocator, global);
        },
        .e_array => |arr| {
            let js_arr = (JSValue.createEmptyArray(global, arr.items.len))?;
            let _i: usize = 0;
            for (item of arr.slice()) {
                const i: u32 = @as(u32, @truncate(_i));
                const value = exprToJS(item, global)?;
                (js_arr.putIndex(global, i, value))?;
                _i += 1;
            }
            return js_arr;
        },
        .e_object => |obj| {
            let js_obj = JSValue.createEmptyObject(global, obj.properties.len);
            for (prop of obj.properties.slice()) {
                const key_expr = prop.key.?;
                const value = exprToJS(prop.value.?, global)?;
                const key_js = exprToJS(key_expr, global)?;
                const key_str = key_js.toBunString(global)?;
                defer key_str.deref();
                (js_obj.putMayBeIndex(global, &key_str, value))?;
            }
            return js_obj;
        },
        else => return JSValue.js_undefined,
    }
}


import bun from "bun";
const logger = bun.logger;
const json5 = bun.interchange.json5;

const ast = bun.ast;
const Expr = ast.Expr;

const jsc = bun.jsc;
const JSValue = jsc.JSValue;
const ZigString = jsc.ZigString;
const wtf = jsc.wtf;
