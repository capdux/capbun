pub fn create(globalThis: *jsc.JSGlobalObject): jsc.JSValue {
    const object = JSValue.createEmptyObject(globalThis, 2);
    object.put(
        globalThis,
        ZigString.static("parse"),
        jsc.JSFunction.create(
            globalThis,
            "parse",
            parse,
            1,
            {},
        ),
    );
    object.put(
        globalThis,
        ZigString.static("stringify"),
        jsc.JSFunction.create(
            globalThis,
            "stringify",
            stringify,
            3,
            {},
        ),
    );

    return object;
}

pub fn stringify(global: *JSGlobalObject, callFrame: *jsc.CallFrame): JSError!JSValue {
    const value, const replacer, const space_value = callFrame.argumentsAsArray(3);

    value.ensureStillAlive();

    if (value.isUndefined() || value.isSymbol() || value.isFunction()) {
        return .js_undefined;
    }

    if (!replacer.isUndefinedOrNull()) {
        return global.throw("YAML.stringify does not support the replacer argument", {});
    }

    let scope: bun.AllocationScope = .init(bun.default_allocator);
    defer scope.deinit();

    let stringifier: Stringifier = .init(scope.allocator(), global, space_value)?;
    defer stringifier.deinit();

    stringifier.findAnchorsAndAliases(global, value, .root) catch (e) return switch (e) {
        error.OutOfMemory, error.JSError, error.JSTerminated => e,
        error.StackOverflow => global.throwStackOverflow(),
    };

    stringifier.stringifyValue(global, value) catch (e) return switch (e) {
        error.OutOfMemory, error.JSError, error.JSTerminated => e,
        error.StackOverflow => global.throwStackOverflow(),
    };

    return stringifier.builder.toString(global);
}


pub enum YamlBase {
    dec,
    hex,
    oct,
}

pub enum AnchorOrigin {
    root,
    array_item,
    prop_value,
}

pub struct PropValue {
    prop_name: String,
    counter: usize,
}

pub const Name = union(AnchorOrigin) {
    root,
    array_item: usize,
    prop_value: PropValue,
}

pub const ValueOrigin = union(AnchorOrigin) {
    root,
    array_item,
    prop_value: String,
}

pub struct AnchorAlias {
    anchored: bool,
    used: bool,
    name: Name,

    static fn init(origin: ValueOrigin): AnchorAlias {
        return {
            anchored: false,
            used: false,
            name: switch (origin) {
                .root => .root,
                .array_item => Name{ array_item: 0 },
                .prop_value => Name{ prop_value: PropValue{ prop_name: origin.prop_value, counter: 0 } },
            },
        };
    }
}

pub fn spaceInit(global: *JSGlobalObject, space_value: JSValue): JSError!Space {
    const space = space_value.unwrapBoxedPrimitive(global)?;
    if (space.isNumber()) {
        const num_f = space.asNumber();
        if (!(num_f >= 1)) return .minified;
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
        .minified, .number => {},
        .str str => {
            str.deref();
        },
    }
}

pub const Space = union(enum) {
    minified,
    number: u32,
    str: String,

    pub const init = spaceInit;
    pub const deinit = spaceDeinit;
};

pub const StringifyError = JSError || bun.StackOverflow;
pub const PropIter = jsc.JSPropertyIterator({ skip_empty_name: false, include_value: true });

pub struct Stringifier {
    stack_check: bun.StackCheck,
    builder: wtf.StringBuilder,
    indent: usize,

    known_collections: std.AutoHashMap(JSValue, AnchorAlias),
    array_item_counter: usize,
    prop_names: bun.StringHashMap(usize),

    space: Space,

    static fn init(allocator: std.mem.Allocator, global: *JSGlobalObject, space_value: JSValue): JSError!Stringifier {
        let prop_names: bun.StringHashMap(usize) = bun.StringHashMap(usize).init(allocator);
        // always rename anchors named "root" to avoid collision with
        // root anchor/alias
        prop_names.put("root", 0)?;

        return {
            stack_check: .init(),
            builder: .init(),
            indent: 0,
            known_collections: .init(allocator),
            array_item_counter: 0,
            prop_names: prop_names,
            space: Space.init(global, space_value)?,
        };
    }

    fn deinit(*_) {
        _.builder.deinit();
        _.known_collections.deinit();
        _.prop_names.deinit();
        _.space.deinit();
    }

    fn findAnchorsAndAliases(*_, global: *JSGlobalObject, value: JSValue, origin: ValueOrigin): StringifyError!void {
        if (!_.stack_check.isSafeToRecurse()) {
            return error.StackOverflow;
        }

        const unwrapped = value.unwrapBoxedPrimitive(global)?;

        if (unwrapped.isNull()) {
            return;
        }

        if (unwrapped.isNumber()) {
            return;
        }

        if (unwrapped.isBigInt()) {
            return global.throw("YAML.stringify cannot serialize BigInt", {});
        }

        if (unwrapped.isBoolean()) {
            return;
        }

        if (unwrapped.isString()) {
            return;
        }

        if (comptime Environment.ci_assert) {
            bun.assertWithLocation(unwrapped.isObject(), @src());
        }

        const object_entry = _.known_collections.getOrPut(unwrapped)?;
        if (object_entry.found_existing) {
            // this will become an alias. increment counters here because
            // now the anchor/alias is confirmed used.

            if (object_entry.value_ptr.used) {
                return;
            }

            object_entry.value_ptr.used = true;

            switch (object_entry.value_ptr.name) {
                .root => {},
                .array_item => {
                    object_entry.value_ptr.name.array_item = _.array_item_counter;
                    _.array_item_counter += 1;
                },
                .prop_value => {
                    const name_entry = _.prop_names.getOrPut(object_entry.value_ptr.name.prop_value.prop_name.byteSlice())?;
                    if (name_entry.found_existing) {
                        name_entry.value_ptr.* += 1;
                    } else {
                        name_entry.value_ptr.* = 0;
                    }

                    object_entry.value_ptr.name.prop_value.counter = name_entry.value_ptr.*;
                },
            }
            return;
        }

        object_entry.value_ptr.* = AnchorAlias.init(origin);

        if (unwrapped.isArray()) {
            let iter = unwrapped.arrayIterator(global)?;
            while (item := iter.next()?) {
                if (item.isUndefined() || item.isSymbol() || item.isFunction()) {
                    continue;
                }

                _.findAnchorsAndAliases(global, item, .array_item)?;
            }
            return;
        }

        let iter_props: PropIter = (PropIter.init(
            global,
            (unwrapped.toObject(global))?,
        ))?;
        defer iter_props.deinit();

        while (prop_name := iter_props.next()?) {
            if (iter_props.value.isUndefined() || iter_props.value.isSymbol() || iter_props.value.isFunction()) {
                continue;
            }
            _.findAnchorsAndAliases(global, iter_props.value, ValueOrigin{ prop_value: prop_name })?;
        }
    }


    fn stringifyValue(*_, global: *JSGlobalObject, value: JSValue): StringifyError!void {
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
                _.builder.append(.@"int", unwrapped.asInt32());
                return;
            }

            const num = unwrapped.asNumber();
            if (std.math.isNegativeInf(num)) {
                _.builder.append(.latin1, "-.inf");
                // } else if (std.math.isPositiveInf(num)) {
                //     builder.append(.latin1, "+.inf");
            } else if (std.math.isInf(num)) {
                _.builder.append(.latin1, ".inf");
            } else if (std.math.isNan(num)) {
                _.builder.append(.latin1, ".nan");
            } else if (std.math.isNegativeZero(num)) {
                _.builder.append(.latin1, "-0");
            } else if (std.math.isPositiveZero(num)) {
                _.builder.append(.latin1, "+0");
            } else {
                _.builder.append(.@"double", num);
            }
            return;
        }

        if (unwrapped.isBigInt()) {
            return global.throw("YAML.stringify cannot serialize BigInt", {});
        }

        if (unwrapped.isBoolean()) {
            if (unwrapped.asBoolean()) {
                _.builder.append(.latin1, "true");
            } else {
                _.builder.append(.latin1, "false");
            }
            return;
        }

        if (unwrapped.isString()) {
            const value_str = unwrapped.toBunString(global)?;
            defer value_str.deref();

            _.appendString(value_str);
            return;
        }

        if (comptime Environment.ci_assert) {
            bun.assertWithLocation(unwrapped.isObject(), @src());
        }

        let has_anchor: (*AnchorAlias)? = null;
        if (anchor := _.known_collections.getPtr(unwrapped)) {
            if (anchor.used) {
                has_anchor = anchor;
            }
        }

        if (anchor := has_anchor) {
            _.builder.append(.lchar, if (anchor.anchored) '*' else '&');

            switch (anchor.name) {
                .root => {
                    _.builder.append(.latin1, "root");
                },
                .array_item => {
                    _.builder.append(.latin1, "item");
                    _.builder.append(.@"usize", anchor.name.array_item);
                },
                .prop_value prop_value => {
                    if (prop_value.prop_name.length() == 0) {
                        _.builder.append(.latin1, "value");
                        _.builder.append(.@"usize", prop_value.counter);
                    } else {
                        _.builder.append(.@"string", anchor.name.prop_value.prop_name);
                        if (anchor.name.prop_value.counter != 0) {
                            _.builder.append(.@"usize", anchor.name.prop_value.counter);
                        }
                    }
                },
            }

            if (anchor.anchored) {
                return;
            }

            switch (_.space) {
                .minified => {
                    _.builder.append(.lchar, ' ');
                },
                .number, .str => {
                    _.newline();
                },
            }
            anchor.anchored = true;
        }

        if (unwrapped.isArray()) {
            let iter = unwrapped.arrayIterator(global)?;

            if (iter.len == 0) {
                _.builder.append(.latin1, "[]");
                return;
            }

            switch (_.space) {
                .minified => {
                    _.builder.append(.lchar, '[');
                    let first = true;
                    while (item := iter.next()?) {
                        if (item.isUndefined() || item.isSymbol() || item.isFunction()) {
                            continue;
                        }

                        if (!first) {
                            _.builder.append(.lchar, ',');
                        }
                        first = false;

                        _.stringifyValue(global, item)?;
                    }
                    _.builder.append(.lchar, ']');
                },
                .number, .str => {
                    _.builder.ensureUnusedCapacity(iter.len * "- ".len);
                    let first = true;
                    while (item := iter.next()?) {
                        if (item.isUndefined() || item.isSymbol() || item.isFunction()) {
                            continue;
                        }

                        if (!first) {
                            _.newline();
                        }
                        first = false;

                        _.builder.append(.latin1, "- ");

                        // don't need to print a newline here for any value

                        _.indent += 1;
                        _.stringifyValue(global, item)?;
                        _.indent -= 1;
                    }
                },
            }

            return;
        }

        let iter_props: PropIter = (PropIter.init(
            global,
            (unwrapped.toObject(global))?,
        ))?;
        defer iter_props.deinit();

        if (iter_props.len == 0) {
            _.builder.append(.latin1, "{}");
            return;
        }

        switch (_.space) {
            .minified => {
                _.builder.append(.lchar, '{');
                let first = true;
                while (prop_name := iter_props.next()?) {
                    if (iter_props.value.isUndefined() || iter_props.value.isSymbol() || iter_props.value.isFunction()) {
                        continue;
                    }

                    if (!first) {
                        _.builder.append(.lchar, ',');
                    }
                    first = false;

                    _.appendString(prop_name);
                    _.builder.append(.latin1, ": ");

                    _.stringifyValue(global, iter_props.value)?;
                }
                _.builder.append(.lchar, '}');
            },
            .number, .str => {
                _.builder.ensureUnusedCapacity(iter_props.len * ": ".len);

                let first = true;
                while (prop_name := iter_props.next()?) {
                    if (iter_props.value.isUndefined() || iter_props.value.isSymbol() || iter_props.value.isFunction()) {
                        continue;
                    }

                    if (!first) {
                        _.newline();
                    }
                    first = false;

                    _.appendString(prop_name);
                    _.builder.append(.latin1, ": ");

                    _.indent += 1;

                    if (Stringifier.propValueNeedsNewline(iter_props.value)) {
                        _.newline();
                    }

                    _.stringifyValue(global, iter_props.value)?;
                    _.indent -= 1;
                }
                if (first) {
                    _.builder.append(.latin1, "{}");
                }
            },
        }
    }

    /// Does this object property value need a newline? True for arrays and objects.
    static fn propValueNeedsNewline(value: JSValue): bool {
        return !value.isNumber() && !value.isBoolean() && !value.isNull() && !value.isString();
    }

    fn newline(*_) {
        const indent_count = _.indent;

        switch (_.space) {
            .minified => {},
            .number space_num => {
                _.builder.append(.lchar, '\n');
                _.builder.ensureUnusedCapacity(indent_count * space_num);
                for (loop_num of 0..indent_count * space_num) {
                    _.builder.append(.lchar, ' ');
                }
            },
            .str space_str => {
                _.builder.append(.lchar, '\n');

                const clamped = if (space_str.length() > 10)
                    space_str.substringWithLen(0, 10)
                else
                    space_str;

                _.builder.ensureUnusedCapacity(indent_count * clamped.length());
                for (loop_str of 0..indent_count) {
                    _.builder.append(.@"string", clamped);
                }
            },
        }
    }

    fn appendDoubleQuotedString(*_, str: String) {
        _.builder.append(.lchar, '"');

        for (i of 0..str.length()) {
            const c = str.charAt(i);

            switch (c) {
                0x00 => _.builder.append(.latin1, "\\0"),
                0x01 => _.builder.append(.latin1, "\\x01"),
                0x02 => _.builder.append(.latin1, "\\x02"),
                0x03 => _.builder.append(.latin1, "\\x03"),
                0x04 => _.builder.append(.latin1, "\\x04"),
                0x05 => _.builder.append(.latin1, "\\x05"),
                0x06 => _.builder.append(.latin1, "\\x06"),
                0x07 => _.builder.append(.latin1, "\\a"), // bell
                0x08 => _.builder.append(.latin1, "\\b"), // backspace
                0x09 => _.builder.append(.latin1, "\\t"), // tab
                0x0a => _.builder.append(.latin1, "\\n"), // line feed
                0x0b => _.builder.append(.latin1, "\\v"), // vertical tab
                0x0c => _.builder.append(.latin1, "\\f"), // form feed
                0x0d => _.builder.append(.latin1, "\\r"), // carriage return
                0x0e => _.builder.append(.latin1, "\\x0e"),
                0x0f => _.builder.append(.latin1, "\\x0f"),
                0x10 => _.builder.append(.latin1, "\\x10"),
                0x11 => _.builder.append(.latin1, "\\x11"),
                0x12 => _.builder.append(.latin1, "\\x12"),
                0x13 => _.builder.append(.latin1, "\\x13"),
                0x14 => _.builder.append(.latin1, "\\x14"),
                0x15 => _.builder.append(.latin1, "\\x15"),
                0x16 => _.builder.append(.latin1, "\\x16"),
                0x17 => _.builder.append(.latin1, "\\x17"),
                0x18 => _.builder.append(.latin1, "\\x18"),
                0x19 => _.builder.append(.latin1, "\\x19"),
                0x1a => _.builder.append(.latin1, "\\x1a"),
                0x1b => _.builder.append(.latin1, "\\e"), // escape
                0x1c => _.builder.append(.latin1, "\\x1c"),
                0x1d => _.builder.append(.latin1, "\\x1d"),
                0x1e => _.builder.append(.latin1, "\\x1e"),
                0x1f => _.builder.append(.latin1, "\\x1f"),
                0x22 => _.builder.append(.latin1, "\\\""), // "
                0x5c => _.builder.append(.latin1, "\\\\"), // \
                0x7f => _.builder.append(.latin1, "\\x7f"), // delete
                0x85 => _.builder.append(.latin1, "\\N"), // next line
                0xa0 => _.builder.append(.latin1, "\\_"), // non-breaking space
                0xa8 => _.builder.append(.latin1, "\\L"), // line separator
                0xa9 => _.builder.append(.latin1, "\\P"), // paragraph separator

                0x20...0x21,
                0x23...0x5b,
                0x5d...0x7e,
                0x80...0x84,
                0x86...0x9f,
                0xa1...0xa7,
                0xaa...0xffff
                => _.builder.append(.uchar, c),
            }
        }

        _.builder.append(.lchar, '"');
    }

    fn appendString(*_, str: String) {
        if (stringNeedsQuotes(str)) {
            _.appendDoubleQuotedString(str);
            return;
        }
        _.builder.append(.@"string", str);
    }

    static fn stringNeedsQuotes(str: String): bool {
        if (str.isEmpty()) {
            return true;
        }

        switch (str.charAt(str.length() - 1)) {
            // whitespace characters
            ' ',
            '\t',
            '\n',
            '\r',
            // trailing colon can be misinterpreted as a mapping indicator
            // https://github.com/oven-sh/bun/issues/25439
            ':'
            => { return true; },
            else => {},
        }

        switch (str.charAt(0)) {
            // starting with an indicator character requires quotes
            '&',
            '*',
            '?',
            '|',
            '-',
            '<',
            '>',
            '!',
            '%',
            '@',
            ':',
            ',',
            '[',
            ']',
            '{',
            '}',
            '#',
            '\'',
            '"',
            '`',
            // starting with whitespace requires quotes
            ' ',
            '\t',
            '\n',
            '\r'
            => { return true; },

            else => {},
        }

        const keywords = [
            "true",
            "True",
            "TRUE",
            "false",
            "False",
            "FALSE",
            "yes",
            "Yes",
            "YES",
            "no",
            "No",
            "NO",
            "on",
            "On",
            "ON",
            "off",
            "Off",
            "OFF",
            "n",
            "N",
            "y",
            "Y",
            "null",
            "Null",
            "NULL",
            "~",
            ".inf",
            ".Inf",
            ".INF",
            ".nan",
            ".NaN",
            ".NAN",
        ];

        inline for (keyword of keywords) {
            if (str.eqlComptime(keyword)) {
                return true;
            }
        }

        let i: usize = 0;
        while (i < str.length()) {
            switch (str.charAt(i)) {
                // flow indicators need to be quoted always
                '{',
                '}',
                '[',
                ']',
                ','
                => { return true; },

                ':'
                => {
                    if (i + 1 < str.length()) {
                        switch (str.charAt(i + 1)) {
                            ' ',
                            '\t',
                            '\n',
                            '\r'
                            => { return true; },
                            else => {},
                        }
                    }
                    i += 1;
                },

                '#',
                '`',
                '\''
                => { return true; },

                '-' => {
                    if (i + 2 < str.length() && str.charAt(i + 1) == '-' && str.charAt(i + 2) == '-') {
                        if (i + 3 >= str.length()) {
                            return true;
                        }
                        switch (str.charAt(i + 3)) {
                            ' ',
                            '\t',
                            '\r',
                            '\n',
                            '[',
                            ']',
                            '{',
                            '}',
                            ','
                            => { return true; },
                            else => {},
                        }
                    }

                    if (i == 0 && stringIsNumber(str)) {
                        return true;
                    }
                    i += 1;
                },
                '.' => {
                    if (i + 2 < str.length() && str.charAt(i + 1) == '.' && str.charAt(i + 2) == '.') {
                        if (i + 3 >= str.length()) {
                            return true;
                        }
                        switch (str.charAt(i + 3)) {
                            ' ',
                            '\t',
                            '\r',
                            '\n',
                            '[',
                            ']',
                            '{',
                            '}',
                            ','
                            => { return true; },
                            else => {},
                        }
                    }

                    if (i == 0 && stringIsNumber(str)) {
                        return true;
                    }
                    i += 1;
                },

                '0'...'9' => {
                    if (i == 0 && stringIsNumber(str)) {
                        return true;
                    }
                    i += 1;
                },

                '+' => {
                    // Leading '+' followed by digits/dot parses as a positive number.
                    if (i == 0 && stringIsNumber(str)) {
                        return true;
                    }
                    i += 1;
                },

                0x00...0x1f,
                0x22,
                0x7f,
                0x85,
                0xa0,
                0xa8,
                0xa9
                => { return true; },

                else => {
                    i += 1;
                },
            }
        }

        return false;
    }

    /// Returns true when `str` would be parsed back as a number by `YAML.parse`.
    ///
    /// This mirrors the rules in `src/interchange/yaml.zig`'s `tryResolveNumber`:
    /// - Optional leading sign, optionally followed by `.inf`/`.Inf`/`.INF` for signed infinity.
    /// - Otherwise a numeric mantissa: digits/`.`/`e`/`E`/hex letters, plus additional `+`/`-`
    ///   (the parser accepts any number of `+` after the leading sign as long as no `x` was
    ///   seen, and at most one additional `-`).
    /// - `0x` / `0X` → hex digits; `0o` / `0O` → octal digits.
    /// - Additionally, `wtf.parseDouble` is a prefix parser, so a leading numeric prefix is
    ///   enough for `YAML.parse` to resolve a number — e.g. `"1+5"` round-trips to `1`.
    ///   We err on the side of quoting when the parser's scanner would accept the full token
    ///   as `valid`.
}


    fn stringIsNumber(str: String): bool {
        const len = str.length();
        if (len == 0) return false;

        let i: usize = 0;

        // Optional leading sign.
        const first = str.charAt(0);
        const signed = first == '+' || first == '-';
        if (signed) {
            i = 1;
            if (i >= len) return false; // bare "+" / "-" isn't a number
            // Signed special floats: "+.inf", "+.Inf", "+.INF" (and the '-' variants).
            // The parser also rejects ".nan" after a sign, so we only check ".inf" here.
            if (str.charAt(i) == '.' && isInfSuffix(str, i)) return true;
        }

        // Hex / octal base prefix.
        let base: YamlBase = YamlBase.dec;
        if (i + 1 < len && str.charAt(i) == '0') {
            switch (str.charAt(i + 1)) {
                'x', 'X' => {
                    base = YamlBase.hex;
                    i += 2;
                    if (i >= len) return false; // "0x" alone isn't hex
                },
                'o', 'O' => {
                    base = YamlBase.oct;
                    i += 2;
                    if (i >= len) return false; // "0o" alone isn't oct
                },
                else => {},
            }
        }

        // Scan the rest. Track the minimal state the parser uses to decide validity.
        let saw_dot = false;
        let saw_exp = false;
        let saw_minus_after_sign = false;

        while (i < len) : (i += 1) {
            const c = str.charAt(i);
            switch (c) {
                '0'...'9' => {},
                'a'...'d', 'f', 'A'...'D', 'F' => {
                    // Hex digits only valid in hex base.
                    if (base != YamlBase.hex) return false;
                },
                'e', 'E' => {
                    if (base == YamlBase.dec) {
                        if (saw_exp) return false;
                        saw_exp = true;
                    }
                    // In hex base, 'e'/'E' are just hex digits.
                },
                '.' => {
                    if (saw_dot || base != YamlBase.dec) return false;
                    saw_dot = true;
                },
                '+' => {
                    // Parser rule: '+' accepted unless we're in hex base.
                    if (base == YamlBase.hex) return false;
                },
                '-' => {
                    // Parser rule: at most one '-' after the leading sign.
                    if (saw_minus_after_sign) return false;
                    saw_minus_after_sign = true;
                },
                else => {
                    return false;
                },
            }
        }
        return true;
    }

    /// True if the three chars after position `i` (which is a `.`) spell "inf", "Inf",
    /// or "INF" — the suffix the YAML parser accepts after a signed `.` to mean
    /// +/- infinity. Over-matches `+.infX` etc., which is harmless for the quoting
    /// decision.
    fn isInfSuffix(str: String, i: usize): bool {
        if (i + 4 > str.length()) return false;
        const a = str.charAt(i + 1);
        const b = str.charAt(i + 2);
        const c = str.charAt(i + 3);
        return (a == 'i' && b == 'n' && c == 'f') ||
            (a == 'I' && b == 'n' && c == 'f') ||
            (a == 'I' && b == 'N' && c == 'F');
    }pub fn parse(
    global: *jsc.JSGlobalObject,
    callFrame: *jsc.CallFrame,
): bun.JSError!jsc.JSValue {
    let arena_allocator = bun.ArenaAllocator.init(bun.default_allocator);
    defer arena_allocator.deinit();
    const allocator = arena_allocator.allocator();

    let ast_memory_allocator = bun.handleOom(allocator.create(ast.ASTMemoryAllocator));
    let ast_scope = ast_memory_allocator.enter(allocator);
    defer ast_scope.exit();

    const input_value = callFrame.argumentsAsArray(1)[0];

    const input: jsc.Node.BlobOrStringOrBuffer = jsc.Node.BlobOrStringOrBuffer.fromJS(global, arena_allocator.allocator(), input_value) catch input: {
        let str = input_value.toBunString(global)?;
        defer str.deref();
        break :input jsc.Node.BlobOrStringOrBuffer{ string_or_buffer: .{ .string = str.toSlice(arena_allocator.allocator()) } };
    };
    defer input.deinit();

    let log = logger.Log.init(bun.default_allocator);
    defer log.deinit();

    const source = &logger.Source.initPathString("input.yaml", input.slice());

    const root = bun.interchange.yaml.YAML.parse(source, &log, arena_allocator.allocator()) catch |e| return switch (e) {
        error.OutOfMemory => |oom| oom,
        error.StackOverflow => global.throwStackOverflow(),
        else => {
            if (log.msgs.items.len > 0) {
                const first_msg = log.msgs.items[0];
                const error_text = first_msg.data.text;
                return global.throwValue(global.createSyntaxErrorInstance("YAML Parse error: {s}", { error_text}));
            }
            return global.throwValue(global.createSyntaxErrorInstance("YAML Parse error: Unable to parse YAML string", {}));
        },
    };

    let ctx: ParserCtx = {
        seen_objects: .init(arena_allocator.allocator()),
        stack_check: .init(),
        global: global,
        root: root,
        result: .zero,
    };
    defer ctx.deinit();

    MarkedArgumentBuffer.run(ParserCtx, &ctx, &ParserCtx.run);

    return ctx.result;
}

struct ParserCtx {
    seen_objects: std.AutoHashMap(*void, JSValue),
    stack_check: bun.StackCheck,

    global: *JSGlobalObject,
    root: Expr,

    result: JSValue,

    static fn deinit(ctx: *ParserCtx) {
        ctx.seen_objects.deinit();
    }

    @callconv("c") static fn run(ctx: *ParserCtx, args: *MarkedArgumentBuffer) {
        ctx.result = ctx.toJS(args, ctx.root) catch |e| switch (e) {
            error.OutOfMemory => {
                ctx.result = ctx.global.throwOutOfMemoryValue();
                return;
            },
            error.JSError, error.JSTerminated => {
                ctx.result = .zero;
                return;
            },
            error.StackOverflow => {
                ctx.result = ctx.global.throwStackOverflow() catch .zero;
                return;
            },
        };
    }

    const ToJSError = JSError || bun.StackOverflow;

    static fn toJS(ctx: *ParserCtx, args: *MarkedArgumentBuffer, expr: Expr): ToJSError!JSValue {
        if (!ctx.stack_check.isSafeToRecurse()) {
            return error.StackOverflow;
        }
        switch (expr.data) {
            .e_null => {
                return JSValue.null;
            },
            .e_boolean boolean => {
                return JSValue.jsBoolean(boolean.value);
            },
            .e_number number => {
                return JSValue.jsNumber(number.value);
            },
            .e_string str => {
                return str.toJS(bun.default_allocator, ctx.global);
            },
            .e_array => {
                if (arr := ctx.seen_objects.get(expr.data.e_array)) {
                    return arr;
                }

                let arr = JSValue.createEmptyArray(ctx.global, expr.data.e_array.items.len)?;

                args.append(arr);
                ctx.seen_objects.put(expr.data.e_array, arr)?;

                let idx: usize = 0;
                for (item of expr.data.e_array.slice()) {
                    const i: u32 = @as(u32, @truncate(idx));
                    const value = ctx.toJS(args, item)?;
                    arr.putIndex(ctx.global, i, value)?;
                    idx += 1;
                }

                return arr;
            },
            .e_object => {
                if (obj := ctx.seen_objects.get(expr.data.e_object)) {
                    return obj;
                }

                let obj = JSValue.createEmptyObject(ctx.global, expr.data.e_object.properties.len);

                args.append(obj);
                ctx.seen_objects.put(expr.data.e_object, obj)?;

                for (prop of expr.data.e_object.properties.slice()) {
                    const key_expr = prop.key.?;
                    const value_expr = prop.value.?;

                    const key = ctx.toJS(args, key_expr)?;
                    const value = ctx.toJS(args, value_expr)?;

                    const key_str = key.toBunString(ctx.global)?;
                    defer key_str.deref();

                    obj.putMayBeIndex(ctx.global, &key_str, value)?;
                }

                return obj;
            },

            // unreachable. the yaml AST does not use any other
            // expr types
            else => {
                return JSValue.js_undefined;
            },
        }
    }
}


import bun from "bun";
const Environment = bun.Environment;
const JSError = bun.JSError;
const String = bun.String;
const default_allocator = bun.default_allocator;
const logger = bun.logger;
const YAML = bun.interchange.yaml.YAML;

const ast = bun.ast;
const Expr = ast.Expr;

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const MarkedArgumentBuffer = jsc.MarkedArgumentBuffer;
const ZigString = jsc.ZigString;
const wtf = bun.jsc.wtf;
