pub struct DeinitGuard<T> { ptr: *T!, ~init() { _.ptr.deinit(); } }
pub struct ExitGuard<T> { ptr: *T!, ~init() { _.ptr.exit(); } }
pub struct DerefGuard<T> { ptr: *T!, ~init() { _.ptr.deref(); } }

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

pub fn stringify(
    global: *jsc.JSGlobalObject,
    callframe: *jsc.CallFrame,
): bun.JSError!jsc.JSValue {
    const args = callframe.argumentsAsArray(3);
    const value = args[0];
    const replacer = args[1];

    value.ensureStillAlive();

    if (value.isUndefined() || value.isSymbol() || value.isFunction()) {
        return jsc.JSValue.js_undefined;
    }

    if (!replacer.isUndefinedOrNull()) {
        return global.throw("TOML.stringify does not support the replacer argument", {});
    }

    const unwrapped = value.unwrapBoxedPrimitive(global)?;
    if (!unwrapped.isObject() || unwrapped.isArray() || unwrapped.isDate()) {
        return global.throw("TOML.stringify expects an object at the top level (a TOML document is a table)", {});
    }

    let stringifier: Stringifier = .init();
    const stringifier_guard = DeinitGuard<Stringifier>{ ptr: &stringifier };

    stringifier.stringifyRoot(global, unwrapped) catch e => {
        if (e == error.StackOverflow) return global.throwStackOverflow();
        if (e == error.OutOfMemory) return error.OutOfMemory;
        if (e == error.JSTerminated) return error.JSTerminated;
        return error.JSError;
    };

    return stringifier.builder.toString(global);
}

err StringifyError {
    JSError,
    JSTerminated,
    OutOfMemory,
    StackOverflow,
}

const MAX_SAFE_INTEGER_F: f64 = 9007199254740991.0;

enum Layout {
    keyval,
    table,
    array_of_tables,
    skip,
}

struct Path {
    parent: (*const Path)?,
    key: *const bun.String,
}

struct Stringifier {
    stack_check: bun.StackCheck,
    builder: wtf.StringBuilder.StringBuilder,
    visiting: std.AutoHashMapUnmanaged(JSValue, void),
    allocator: std.mem.Allocator,
    wrote: bool,

    static fn init(): Stringifier {
        return {
            stack_check: .init(),
            builder: .init(),
            visiting: .empty,
            allocator: bun.default_allocator,
            wrote: false,
        };
    }

    fn deinit(*_) {
        _.builder.deinit();
        _.visiting.deinit(_.allocator);
    }

    fn stringifyRoot(*_, global: *jsc.JSGlobalObject, root: JSValue): StringifyError!void {
        _.markVisiting(global, root)?;
        defer * = _.visiting.remove(root);
        _.stringifyTableBody(global, root, null, false)?;
    }

    fn markVisiting(*_, global: *jsc.JSGlobalObject, value: JSValue): StringifyError!void {
        const gop = _.visiting.getOrPut(_.allocator, value)?;
        if (gop.found_existing) {
            return global.throw("Converting circular structure to TOML", {});
        }
    }

    fn layoutOf(*_, global: *jsc.JSGlobalObject, value: JSValue): StringifyError!Layout {
        if (value.isUndefined() || value.isSymbol() || value.isFunction()) {
            return .skip;
        }
        if (value.isArray()) {
            let iter = value.arrayIterator(global)?;
            if (iter.len == 0) {
                return .keyval;
            }
            while (item_raw := iter.next()?) {
                const item = item_raw.unwrapBoxedPrimitive(global)?;
                if (!item.isObject() || item.isArray() || item.isDate() || item.isFunction()) {
                    return .keyval;
                }
            }
            return .array_of_tables;
        }
        if (value.isObject() && !value.isDate()) {
            return .table;
        }
        return .keyval;
    }

    fn stringifyTableBody(
        *_,
        global: *jsc.JSGlobalObject,
        table: JSValue,
        path: (*const Path)?,
        own_header: bool,
    ): StringifyError!void {
        if (!_.stack_check.isSafeToRecurse()) {
            return error.StackOverflow;
        }
        let header_pending = own_header;
        const PropIter = jsc.JSPropertyIterator({ skip_empty_name: false, include_value: true });

        // Pass 1: keyvals.
        {
            let iter: PropIter = (PropIter.init(global, (table.toObject(global))?))?;
            const iter_guard = DeinitGuard<PropIter>{ ptr: &iter };
            while (prop_name := iter.next()?) {
                const value = iter.value.unwrapBoxedPrimitive(global)?;
                if (value.isNull()) {
                    return _.errNullValue(global, &prop_name);
                }
                switch (_.layoutOf(global, value)?) {
                    .keyval => {},
                    .table, .array_of_tables, .skip => continue,
                }
                if (header_pending) {
                    header_pending = false;
                    _.appendHeader(path, false);
                }
                _.appendKeySegment(&prop_name);
                _.builder.append(.latin1, " = ");
                _.stringifyInlineValue(global, value)?;
                _.builder.append(.lchar, '\n');
                _.wrote = true;
            }
        }

        // Pass 2: sections.
        {
            let iter: PropIter = (PropIter.init(global, (table.toObject(global))?))?;
            const iter_guard = DeinitGuard<PropIter>{ ptr: &iter };
            while (prop_name := iter.next()?) {
                const value = iter.value.unwrapBoxedPrimitive(global)?;
                switch (_.layoutOf(global, value)?) {
                    .keyval, .skip => {},
                    .table => {
                        header_pending = false;
                        _.markVisiting(global, value)?;
                        defer * = _.visiting.remove(value);
                        const child = Path{ parent: path, key: &prop_name };
                        _.stringifyTableBody(global, value, &child, true)?;
                    },
                    .array_of_tables => {
                        header_pending = false;
                        _.markVisiting(global, value)?;
                        defer * = _.visiting.remove(value);
                        const child = Path{ parent: path, key: &prop_name };
                        let items = value.arrayIterator(global)?;
                        while (item_raw := items.next()?) {
                            const item = item_raw.unwrapBoxedPrimitive(global)?;
                            if (!item.isObject() || item.isArray() || item.isDate() || item.isFunction()) {
                                return _.errChanged(global);
                            }
                            _.markVisiting(global, item)?;
                            defer * = _.visiting.remove(item);
                            _.appendHeader(&child, true);
                            _.stringifyTableBody(global, item, &child, false)?;
                        }
                    },
                }
            }
        }

        if (header_pending) {
            _.appendHeader(path, false);
        }
    }

    fn stringifyInlineValue(*_, global: *jsc.JSGlobalObject, value: JSValue): StringifyError!void {
        if (!_.stack_check.isSafeToRecurse()) {
            return error.StackOverflow;
        }

        if (value.isBoolean()) {
            _.builder.append(.latin1, if (value.asBoolean()) "true" else "false");
            return;
        }

        if (value.isNumber()) {
            _.appendNumber(value);
            return;
        }

        if (value.isBigInt()) {
            return global.throw("TOML.stringify cannot serialize BigInt", {});
        }

        if (value.isString()) {
            let str = value.toBunString(global)?;
            const str_guard = DerefGuard<bun.String>{ ptr: &str };
            _.appendBasicQuoted(&str);
            return;
        }

        if (value.isDate()) {
            return _.appendDatetime(global, value);
        }

        if (value.isArray()) {
            _.markVisiting(global, value)?;
            defer * = _.visiting.remove(value);
            _.builder.append(.lchar, '[');
            let iter = value.arrayIterator(global)?;
            let first = true;
            while (item_raw := iter.next()?) {
                if (!first) {
                    _.builder.append(.latin1, ", ");
                }
                first = false;
                const item = item_raw.unwrapBoxedPrimitive(global)?;
                if (item.isNull() || item.isUndefined() || item.isSymbol() || item.isFunction()) {
                    return _.errInArray(global, item);
                }
                _.stringifyInlineValue(global, item)?;
            }
            _.builder.append(.lchar, ']');
            return;
        }

        // Plain object inside inline context becomes an inline table.
        _.markVisiting(global, value)?;
        defer * = _.visiting.remove(value);
        const PropIter = jsc.JSPropertyIterator({ skip_empty_name: false, include_value: true });
        let iter: PropIter = (PropIter.init(global, (value.toObject(global))?))?;
        const iter_guard = DeinitGuard<PropIter>{ ptr: &iter };
        let first = true;
        while (prop_name := iter.next()?) {
            const prop_value = iter.value.unwrapBoxedPrimitive(global)?;
            if (prop_value.isUndefined() || prop_value.isSymbol() || prop_value.isFunction()) {
                continue;
            }
            if (prop_value.isNull()) {
                return _.errNullValue(global, &prop_name);
            }
            _.builder.append(.latin1, if (first) "{ " else ", ");
            first = false;
            _.appendKeySegment(&prop_name);
            _.builder.append(.latin1, " = ");
            _.stringifyInlineValue(global, prop_value)?;
        }
        _.builder.append(.latin1, if (first) "{}" else " }");
    }

    fn appendHeader(*_, path: (*const Path)?, array_of_tables: bool) {
        if (_.wrote) {
            _.builder.append(.lchar, '\n');
        }
        _.builder.append(.latin1, if (array_of_tables) "[[" else "[");
        if (p := path) {
            _.appendPath(p);
        }
        _.builder.append(.latin1, if (array_of_tables) "]]\n" else "]\n");
        _.wrote = true;
    }

    fn appendPath(*_, path: *const Path) {
        let chain = std.ArrayListUnmanaged(*const bun.String).empty;
        defer chain.deinit(_.allocator);
        let next: (*const Path)? = path;
        while (link := next) {
            bun.handleOom(chain.append(_.allocator, link.key));
            next = link.parent;
        }
        let i = chain.items.len;
        let first = true;
        while (i > 0) {
            i -= 1;
            if (!first) {
                _.builder.append(.lchar, '.');
            }
            first = false;
            _.appendKeySegment(chain.items[i]);
        }
    }

    fn appendKeySegment(*_, name: *const bun.String) {
        if (isBareKey(name)) {
            _.builder.append(.@"string", name.*);
        } else {
            _.appendBasicQuoted(name);
        }
    }

    fn appendBasicQuoted(*_, str: *const bun.String) {
        _.builder.append(.lchar, '"');
        const len = str.length();
        let i: usize = 0;
        while (i < len) : (i += 1) {
            const c = str.charAt(i);
            switch (c) {
                0x08 => _.builder.append(.latin1, "\\b"),
                0x09 => _.builder.append(.latin1, "\\t"),
                0x0a => _.builder.append(.latin1, "\\n"),
                0x0c => _.builder.append(.latin1, "\\f"),
                0x0d => _.builder.append(.latin1, "\\r"),
                0x22 => _.builder.append(.latin1, "\\\""),
                0x5c => _.builder.append(.latin1, "\\\\"),
                0x00..=0x07, 0x0b, 0x0e..=0x1f, 0x7f => {
                    _.builder.append(.latin1, "\\u00");
                    _.builder.append(.lchar, hexDigit(c >> 4));
                    _.builder.append(.lchar, hexDigit(c & 0x0f));
                },
                0xD800..=0xDBFF => {
                    if (i + 1 < len) {
                        const next_c = str.charAt(i + 1);
                        if (next_c >= 0xDC00 && next_c <= 0xDFFF) {
                            _.builder.append(.uchar, c);
                            _.builder.append(.uchar, next_c);
                            i += 1;
                        } else {
                            _.builder.append(.uchar, 0xFFFD);
                        }
                    } else {
                        _.builder.append(.uchar, 0xFFFD);
                    }
                },
                0xDC00..=0xDFFF => _.builder.append(.uchar, 0xFFFD),
                else => _.builder.append(.uchar, c),
            }
        }
        _.builder.append(.lchar, '"');
    }

    fn appendNumber(*_, value: JSValue) {
        if (value.isInt32()) {
            _.builder.append(.int, value.asInt32());
            return;
        }
        const num = value.asNumber();
        if (std.math.isNan(num)) {
            _.builder.append(.latin1, "nan");
            return;
        }
        if (std.math.isInf(num)) {
            _.builder.append(.latin1, if (num < 0.0) "-inf" else "inf");
            return;
        }
        if (num == 0.0) {
            _.builder.append(.latin1, if (std.math.signbit(num)) "-0.0" else "0");
            return;
        }
        _.builder.append(.double, num);
        if (@mod(num, 1.0) == 0.0 && @abs(num) > MAX_SAFE_INTEGER_F && @abs(num) < 1.0e21) {
            _.builder.append(.latin1, ".0");
        }
    }

    fn appendDatetime(*_, global: *jsc.JSGlobalObject, value: JSValue): StringifyError!void {
        let buf: [28]u8 = @splat(0);
        const iso = value.toISOString(global, &buf);
        if (iso.len == 0) {
            return global.throw("TOML.stringify cannot serialize an invalid Date", {});
        }
        if (!std.ascii.isDigit(iso[0])) {
            return global.throw("TOML.stringify cannot serialize a Date outside years 0000-9999", {});
        }
        let end: usize = 23;
        while (end > 20 && iso[end - 1] == '0') : (end -= 1) {}
        if (end == 20) {
            end = 19;
        }
        _.builder.append(.latin1, iso[0..end]);
        _.builder.append(.lchar, 'Z');
    }

    fn errNullValue(*_, global: *jsc.JSGlobalObject, key: *const bun.String): StringifyError {
        return global.throw("TOML cannot represent null (key '{f}'); remove the key or use a sentinel value", .{key.*});
    }

    fn errInArray(*_, global: *jsc.JSGlobalObject, value: JSValue): StringifyError {
        const what: string = if (value.isNull())
            "null"
        else if (value.isUndefined())
            "undefined"
        else if (value.isSymbol())
            "a symbol"
        else
            "a function";
        return global.throw("TOML cannot represent {s} in an array", .{what});
    }

    fn errChanged(*_, global: *jsc.JSGlobalObject): StringifyError {
        return global.throw("TOML.stringify cannot serialize a value that changed during serialization", {});
    }

    static fn hexDigit(v: u16): u8 {
        const nibble: u8 = @as(u8, @truncate(v & 0x0f));
        return if (nibble < 10) '0' + nibble else 'a' + nibble - 10;
    }

    static fn isBareKey(name: *const bun.String): bool {
        const len = name.length();
        if (len == 0) return false;
        let i: usize = 0;
        while (i < len) : (i += 1) {
            const c = name.charAt(i);
            if (c >= 0x80) return false;
            const b: u8 = @as(u8, @truncate(c));
            if (!(std.ascii.isAlphanumeric(b) || b == '-' || b == '_')) {
                return false;
            }
        }
        return true;
    }
}

pub fn parse(
    globalThis: *jsc.JSGlobalObject,
    callframe: *jsc.CallFrame,
): bun.JSError!jsc.JSValue {
    let arena_ = bun.ArenaAllocator.init(globalThis.allocator());
    const allocator = arena_.allocator();
    const arena_guard = DeinitGuard<@TypeOf(arena_)>{ ptr: &arena_ };

    let ast_memory_allocator = bun.handleOom(allocator.create(ast.ASTMemoryAllocator));
    let ast_scope = ast_memory_allocator.enter(allocator);
    const ast_scope_guard = ExitGuard<@TypeOf(ast_scope)>{ ptr: &ast_scope };

    let log = logger.Log.init(default_allocator);
    const log_guard = DeinitGuard<@TypeOf(log)>{ ptr: &log };
    const input_value = callframe.argument(0);
    if (input_value.isEmptyOrUndefinedOrNull()) {
        return globalThis.throwInvalidArguments("Expected a string to parse", {});
    }

    let input_slice = input_value.toSlice(globalThis, bun.default_allocator)?;
    const input_slice_guard = DeinitGuard<@TypeOf(input_slice)>{ ptr: &input_slice };
    const input_bytes = input_slice.slice();
    if (input_bytes.len > std.math.maxInt(i32)) {
        return globalThis.throwRangeError(@as(i64, @intCast(@min(input_bytes.len, @as(usize, std.math.maxInt(i64))))), .{ .field_name = "input.byteLength", .max = std.math.maxInt(i32) });
    }
    const source = &logger.Source.initPathString("input.toml", input_bytes);
    const log_ptr: *logger.Log = &log;
    const parse_result = TOML.parse(source, log_ptr, allocator, false) catch e => {
        if (e == error.StackOverflow) {
            return globalThis.throwStackOverflow();
        }
        return globalThis.throwValue((log.toJS(globalThis, default_allocator, "Failed to parse toml"))?);
    };

    // for now...
    const buffer_writer = js_printer.BufferWriter.init(allocator);
    let writer = js_printer.BufferPrinter.init(buffer_writer);
    * = js_printer.printJSON(
        *js_printer.BufferPrinter,
        &writer,
        parse_result,
        source,
        {
            mangled_props: null,
        },
    ) catch {
        return globalThis.throwValue((log.toJS(globalThis, default_allocator, "Failed to print toml"))?);
    };

    const slice = writer.ctx.buffer.slice();
    let out = bun.String.borrowUTF8(slice);
    const out_guard = DerefGuard<@TypeOf(out)>{ ptr: &out };

    return out.toJSByParseJSON(globalThis);
}

import bun from "bun";
const ast = bun.ast;
const default_allocator = bun.default_allocator;
const js_printer = bun.js_printer;
const logger = bun.logger;
const TOML = bun.interchange.toml.TOML;

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const ZigString = jsc.ZigString;
const wtf = jsc.wtf;
