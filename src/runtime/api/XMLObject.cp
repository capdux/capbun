import std from "std";
import bun from "bun";

const logger = bun.logger;
const xml = bun.interchange.xml;
const ast = bun.ast;
const Expr = ast.Expr;
const jsc = bun.jsc;
const JSValue = jsc.JSValue;
const ZigString = jsc.ZigString;
const wtf = jsc.wtf;
const PropIter = jsc.JSPropertyIterator({ skip_empty_name: false, include_value: true });

struct DeinitGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.deinit(); }
    }
}

struct DerefGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.deref(); }
    }
}

pub fn create(globalThis: *jsc.JSGlobalObject): jsc.JSValue {
    const object = JSValue.createEmptyObject(globalThis, 2);
    object.put(
        globalThis,
        ZigString.static("parse"),
        jsc.JSFunction.create(globalThis, "parse", parse, 2, {}),
    );
    object.put(
        globalThis,
        ZigString.static("stringify"),
        jsc.JSFunction.create(globalThis, "stringify", stringify, 3, {}),
    );
    return object;
}

pub fn parse(global: *jsc.JSGlobalObject, callFrame: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    const input_value = callFrame.argument(0);
    const options_val = callFrame.argument(1);

    let compact = true;
    if (!options_val.isUndefinedOrNull()) {
        if (!options_val.isObject() || options_val.isArray() || options_val.isCallable()) {
            return global.throwInvalidArguments("XML.parse options must be an object", {});
        }
        if (c_val := options_val.getBooleanStrict(global, "compact")?) {
            compact = c_val;
        }
    }

    if (input_value.isEmptyOrUndefinedOrNull()) {
        return global.throwInvalidArguments("Expected a string or Buffer to parse", {});
    }

    let arena_alloc: bun.ArenaAllocator = .init(bun.default_allocator);
    const arena_guard = DeinitGuard<bun.ArenaAllocator>{ ptr: unsafe { &arena_alloc } };
    const allocator = arena_alloc.allocator();

    let log = logger.Log.init(bun.default_allocator);
    const log_guard = DeinitGuard<logger.Log>{ ptr: unsafe { &log } };

    const is_binary_input = input_value.isObject() && (input_value.as(jsc.WebCore.Blob) != null || input_value.asArrayBuffer(global) != null);

    if (is_binary_input) {
        if (blob := input_value.as(jsc.WebCore.Blob)) {
            if (blob.needsToReadFile() || blob.isS3()) {
                return global.throwInvalidArguments("File blob cannot be used here", .{});
            }
        }
        let blob_or_str = (jsc.Node.BlobOrStringOrBuffer.fromJS(global, allocator, input_value))?;
        if (*input := blob_or_str) {
            const input_guard = DeinitGuard<jsc.Node.BlobOrStringOrBuffer>{ ptr: unsafe { &input } };
            let input_bytes = input.slice();
            if (ab := input_value.asArrayBuffer(global)) {
                if (ab.shared) {
                    input_bytes = bun.handleOom(allocator.dupe(u8, input_bytes));
                }
            }
            if (input_bytes.len > std.math.maxInt(i32)) {
                return global.throwRangeError(@as(i64, @intCast(@min(input_bytes.len, @as(usize, std.math.maxInt(i64))))), .{ .field_name = "input.byteLength", .max = std.math.maxInt(i32) });
            }
            const source = &logger.Source.initPathString("input.xml", input_bytes);
            const root_expr = xml.parse(source, &log, allocator, xml.Options{
                compact: compact,
                encoding: .Bytes,
            }) catch e => {
                return handleParseError(global, &log, e);
            };
            return exprToJS(root_expr, global);
        }
        return JSValue.js_undefined;
    }

    {
        let str = input_value.toBunString(global)?;
        const str_guard = DerefGuard<bun.String>{ ptr: unsafe { &str } };
        if (str.isUTF16()) {
            const units = str.utf16();
            if (units.len > std.math.maxInt(i32)) {
                return global.throwRangeError(@as(i64, @intCast(@min(units.len, @as(usize, std.math.maxInt(i64))))), .{ .field_name = "input.length", .max = std.math.maxInt(i32) });
            }
            const source = &logger.Source.initPathString("input.xml", "");
            const root_expr = xml.parseUtf16(source, units, &log, allocator, compact) catch e => {
                return handleParseError(global, &log, e);
            };
            return exprToJS(root_expr, global);
        } else {
            const slice = str.toUTF8(allocator);
            defer slice.deinit();
            const input_bytes = slice.slice();
            if (input_bytes.len > std.math.maxInt(i32)) {
                return global.throwRangeError(@as(i64, @intCast(@min(input_bytes.len, @as(usize, std.math.maxInt(i64))))), .{ .field_name = "input.byteLength", .max = std.math.maxInt(i32) });
            }
            const source = &logger.Source.initPathString("input.xml", input_bytes);
            const root_expr = xml.parse(source, &log, allocator, xml.Options{
                compact: compact,
                encoding: .Text,
            }) catch e => {
                return handleParseError(global, &log, e);
            };
            return exprToJS(root_expr, global);
        }
    }
}

fn handleParseError(global: *jsc.JSGlobalObject, log: *logger.Log, e: xml.XMLError): bun.JSError!jsc.JSValue {
    if (e == error.OutOfMemory) return error.OutOfMemory;
    if (e == error.StackOverflow) return global.throwStackOverflow();
    if (log.msgs.items.len > 0) {
        const first_msg = log.msgs.items[0];
        return global.throwValue(global.createSyntaxErrorInstance(
            "XML Parse error: {s}",
            .{first_msg.data.text},
        ));
    }
    return global.throwValue(global.createSyntaxErrorInstance(
        "XML Parse error: Unable to parse XML",
        .{},
    ));
}

pub fn stringify(global: *jsc.JSGlobalObject, callFrame: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    const args = callFrame.argumentsAsArray(3);
    const value = args[0];
    const replacer = args[1];
    const space_value = args[2];

    value.ensureStillAlive();

    if (skipped(value)) {
        return JSValue.js_undefined;
    }

    if (!replacer.isUndefinedOrNull()) {
        return global.throw("XML.stringify does not support the replacer argument", {});
    }

    const root = value.unwrapBoxedPrimitive(global)?;
    if (!root.isObject() || root.isArray()) {
        return global.throw("XML.stringify expects an object: a { name, attributes, children } node or an object with a single root element key", {});
    }

    let stringifier = Stringifier.init(global, space_value)?;
    const stringifier_guard = DeinitGuard<Stringifier>{ ptr: unsafe { &stringifier } };

    if (isNode(global, root)?) {
        stringifier.stringifyNode(global, root) catch e => {
            return handleStringifyError(global, e);
        };
    } else {
        stringifier.stringifyCompactDocument(global, root) catch e => {
            return handleStringifyError(global, e);
        };
    }

    return stringifier.builder.toString(global);
}

fn handleStringifyError(global: *jsc.JSGlobalObject, e: StringifyError): bun.JSError!jsc.JSValue {
    if (e == error.StackOverflow) return global.throwStackOverflow();
    if (e == error.OutOfMemory) return error.OutOfMemory;
    if (e == error.JSTerminated) return error.JSTerminated;
    return error.JSError;
}

fn skipped(value: JSValue): bool {
    return value.isUndefined() || value.isSymbol() || value.isFunction();
}

fn isNode(global: *jsc.JSGlobalObject, value: JSValue): bun.JSError!bool {
    if (!value.isObject() || value.isArray()) return false;
    const name_val = (value.get(global, "name"))? ?? return false;
    if (!name_val.isString()) return false;
    if ((value.get(global, "children"))? != null) return true;
    if ((value.get(global, "attributes"))? != null) return true;
    if (value.hasOwnPropertyValue(global, bun.String.static("children").toJS(global)?)?) return true;
    if (value.hasOwnPropertyValue(global, bun.String.static("attributes").toJS(global)?)?) return true;
    return false;
}

pub union(enum) Space {
    minified: void,
    number: u32,
    str: bun.String,

    static fn init(global: *jsc.JSGlobalObject, space_value: JSValue): bun.JSError!Space {
        const space = space_value.unwrapBoxedPrimitive(global)?;
        if (space.isNumber()) {
            const n = space.asNumber();
            if (std.math.isNan(n) || n < 1.0) return .{ minified: undefined };
            const clamped: u32 = if (n > 10.0) 10 else @as(u32, @intFromFloat(n));
            return .{ .number = clamped };
        }
        if (space.isString()) {
            let str = space.toBunString(global)?;
            if (str.length() == 0) {
                str.deref();
                return .{ minified: undefined };
            }
            return .{ .str = str };
        }
        return .{ minified: undefined };
    }

    fn deinit(*_) {
        switch (_.*) {
            .str *s => s.deref(),
            else => {},
        }
    }

    fn isPretty(): bool {
        return _ != .minified;
    }
}

err StringifyError {
    JSError,
    OutOfMemory,
    JSTerminated,
    StackOverflow,
}

union(enum) Scalar {
    skip: void,
    empty: void,
    text: bun.String,
}

struct Stringifier {
    builder: wtf.StringBuilder.StringBuilder,
    indent: usize,
    space: Space,
    visiting: std.AutoHashMapUnmanaged(i64, void),
    depth: usize,

    static fn init(global: *jsc.JSGlobalObject, space_value: JSValue): bun.JSError!Stringifier {
        return Stringifier{
            builder: wtf.StringBuilder.StringBuilder.init(),
            indent: 0,
            space: Space.init(global, space_value)?,
            visiting: .{},
            depth: 0,
        };
    }

    fn deinit(*_) {
        _.builder.deinit();
        _.space.deinit();
        _.visiting.deinit(bun.default_allocator);
    }

    fn markVisiting(*_, global: *jsc.JSGlobalObject, value: JSValue): StringifyError!void {
        const key = @intFromEnum(value);
        const gop = _.visiting.getOrPut(bun.default_allocator, key) catch return error.OutOfMemory;
        if (gop.found_existing) {
            return global.throw("Converting circular structure to XML", {});
        }
    }

    fn unmarkVisiting(*_, value: JSValue) {
        * = _.visiting.remove(@intFromEnum(value));
    }

    fn stringifyNode(*_, global: *jsc.JSGlobalObject, node: JSValue): StringifyError!void {
        if (_.depth >= 512) return error.StackOverflow;
        _.depth += 1;
        defer _.depth -= 1;
        _.markVisiting(global, node)?;
        defer _.unmarkVisiting(node);
        return _.stringifyNodeInner(global, node);
    }

    fn stringifyNodeInner(*_, global: *jsc.JSGlobalObject, node: JSValue): StringifyError!void {
        const name_val = node.get(global, "name")?;
        if (name_val == null || !name_val.?.isString()) {
            return global.throw("XML.stringify: element children must be strings, { name, attributes, children } elements, { comment } or { target, data }", {});
        }
        let name = name_val.?.toBunString(global)?;
        const name_guard = DerefGuard<bun.String>{ ptr: unsafe { &name } };
        _.checkName(global, &name, "element")?;

        _.builder.append(.lchar, '<');
        _.builder.append(.string, name);

        if (attrs := node.get(global, "attributes")?) {
            if (!attrs.isNull()) {
                if (!attrs.isObject() || attrs.isArray()) {
                    return global.throw("XML.stringify: a node's attributes must be an object", {});
                }
                let iter = PropIter.init(global, attrs.toObject(global)?)?;
                defer iter.deinit();
                while (attr_name := iter.next()?) {
                    const sc = _.scalar(global, iter.value, "an attribute value")?;
                    switch (sc) {
                        .skip, .empty => {},
                        .text mut_t => {
                            let t = mut_t;
                            const t_guard = DerefGuard<bun.String>{ ptr: unsafe { &t } };
                            _.appendAttribute(global, &attr_name, &t)?;
                        },
                    }
                }
            }
        }

        let children_opt: JSValue? = null;
        if (ch := node.get(global, "children")?) {
            if (ch.isArray()) {
                children_opt = ch;
            } else if (!ch.isNull()) {
                return global.throw("XML.stringify: a node's children must be an array", {});
            }
        }
        if (children_opt == null) {
            _.builder.append(.latin1, "/>");
            return;
        }
        const children = children_opt.?;

        let count: usize = 0;
        let has_text = false;
        {
            let iter = children.arrayIterator(global)?;
            while (child_raw := iter.next()?) {
                const child = child_raw.unwrapBoxedPrimitive(global)?;
                if (child.isNull() || skipped(child)) continue;
                count += 1;
                if (!child.isObject() || child.isArray() || child.isDate()) {
                    has_text = true;
                }
            }
        }
        if (count == 0) {
            _.builder.append(.latin1, "/>");
            return;
        }
        _.builder.append(.lchar, '>');
        const pretty = _.space.isPretty() && !has_text;
        if (pretty) _.indent += 1;
        let iter = children.arrayIterator(global)?;
        while (child_raw := iter.next()?) {
            const child = child_raw.unwrapBoxedPrimitive(global)?;
            if (child.isNull() || skipped(child)) continue;
            if (pretty) _.newline();
            if (child.isObject() && !child.isArray() && !child.isDate()) {
                const child_name = child.get(global, "name")?;
                if (child_name == null && _.stringifyMarkup(global, child)?) {
                    continue;
                }
                _.stringifyNode(global, child)?;
            } else if (child.isArray()) {
                return global.throw("XML.stringify: a node's children cannot contain arrays", {});
            } else {
                const sc = _.scalar(global, child, "a text child")?;
                switch (sc) {
                    .skip, .empty => {},
                    .text mut_t => {
                        let t = mut_t;
                        const t_guard = DerefGuard<bun.String>{ ptr: unsafe { &t } };
                        _.appendText(global, &t)?;
                    },
                }
            }
        }
        if (pretty) {
            _.indent -= 1;
            _.newline();
        }
        _.appendEndTag(name);
    }

    fn stringifyMarkup(*_, global: *jsc.JSGlobalObject, child: JSValue): StringifyError!bool {
        if (comment := child.get(global, "comment")?) {
            if ((child.get(global, "target"))? != null) {
                return global.throw("XML.stringify: a child with both 'comment' and 'target' is neither a comment nor a processing instruction", {});
            }
            if (!comment.isString()) {
                return global.throw("XML.stringify: a comment node's 'comment' must be a string", {});
            }
            let text = comment.toBunString(global)?;
            const text_guard = DerefGuard<bun.String>{ ptr: unsafe { &text } };
            const len = text.length();
            let i: usize = 0;
            let prev_dash = false;
            while (i < len) {
                const pair = codePointAt(&text, i);
                const cp = pair[0];
                const w = pair[1];
                i += w;
                if (!xml.isXmlChar(cp)) {
                    return global.throw("XML.stringify: XML cannot represent the character U+{X:0>4}", .{cp});
                }
                if (cp == 0x2D && (prev_dash || i == len)) {
                    return global.throw("XML.stringify: a comment cannot contain '--' or end with '-'", {});
                }
                prev_dash = cp == 0x2D;
            }
            _.builder.append(.latin1, "<!--");
            _.builder.append(.string, text);
            _.builder.append(.latin1, "-->");
            return true;
        }
        if (target_val := child.get(global, "target")?) {
            if (!target_val.isString()) {
                return global.throw("XML.stringify: a processing instruction's 'target' must be a string", {});
            }
            let target = target_val.toBunString(global)?;
            const target_guard = DerefGuard<bun.String>{ ptr: unsafe { &target } };
            _.checkName(global, &target, "processing instruction target")?;
            if (target.length() == 3) {
                const c0 = target.charAt(0) | 0x20;
                const c1 = target.charAt(1) | 0x20;
                const c2 = target.charAt(2) | 0x20;
                if (c0 == 'x' && c1 == 'm' && c2 == 'l') {
                    return global.throw("XML.stringify: 'xml' is reserved and cannot be a processing instruction target", {});
                }
            }
            _.builder.append(.latin1, "<?");
            _.builder.append(.string, target);
            if (data_val := child.get(global, "data")?) {
                if (!data_val.isNull()) {
                    if (!data_val.isString()) {
                        return global.throw("XML.stringify: a processing instruction's 'data' must be a string", {});
                    }
                    let data = data_val.toBunString(global)?;
                    const data_guard = DerefGuard<bun.String>{ ptr: unsafe { &data } };
                    const len = data.length();
                    if (len > 0) {
                        let i: usize = 0;
                        let prev_q = false;
                        while (i < len) {
                            const pair = codePointAt(&data, i);
                            const cp = pair[0];
                            const w = pair[1];
                            i += w;
                            if (!xml.isXmlChar(cp)) {
                                return global.throw("XML.stringify: XML cannot represent the character U+{X:0>4}", .{cp});
                            }
                            if (prev_q && cp == 0x3E) {
                                return global.throw("XML.stringify: processing instruction data cannot contain '?>'", {});
                            }
                            prev_q = cp == 0x3F;
                        }
                        _.builder.append(.lchar, ' ');
                        _.builder.append(.string, data);
                    }
                }
            }
            _.builder.append(.latin1, "?>");
            return true;
        }
        return false;
    }

    fn stringifyCompactDocument(*_, global: *jsc.JSGlobalObject, document: JSValue): StringifyError!void {
        _.markVisiting(global, document)?;
        defer _.unmarkVisiting(document);
        return _.stringifyCompactDocumentInner(global, document);
    }

    fn stringifyCompactDocumentInner(*_, global: *jsc.JSGlobalObject, document: JSValue): StringifyError!void {
        let root_name: bun.String? = null;
        let root_val: JSValue = .zero;
        defer {
            if (*rn := root_name) rn.deref();
        }
        let iter = PropIter.init(global, document.toObject(global)?)?;
        defer iter.deinit();
        while (key := iter.next()?) {
            const val = iter.value.unwrapBoxedPrimitive(global)?;
            if (skipped(val)) continue;
            if (key.hasPrefixComptime("@") || key.hasPrefixComptime("#")) {
                return global.throw("XML.stringify: the top-level object is the document, so it can only contain the root element", {});
            }
            if (root_name != null) {
                return global.throw("XML.stringify: an XML document has exactly one root element, but the top-level object has more than one key", {});
            }
            if (val.isArray()) {
                return global.throw("XML.stringify: the root element cannot be an array (an XML document has exactly one root element)", {});
            }
            root_name = key.dupeRef();
            root_val = val;
        }
        if (rn := root_name) {
            return _.stringifyCompactElement(global, &rn, root_val, false);
        }
        return global.throw("XML.stringify: the top-level object must have one key naming the root element", {});
    }

    static fn hasOutput(global: *jsc.JSGlobalObject, value: JSValue): bun.JSError!bool {
        const val = value.unwrapBoxedPrimitive(global)?;
        if (skipped(val)) return false;
        if (!val.isArray()) return true;
        let iter = val.arrayIterator(global)?;
        while (item_raw := iter.next()?) {
            const item = item_raw.unwrapBoxedPrimitive(global)?;
            if (!skipped(item)) return true;
        }
        return false;
    }

    fn stringifyCompactElement(*_, global: *jsc.JSGlobalObject, name: *const bun.String, value: JSValue, separate: bool): StringifyError!void {
        if (_.depth >= 512) return error.StackOverflow;
        _.depth += 1;
        defer _.depth -= 1;
        const val = value.unwrapBoxedPrimitive(global)?;
        if (!val.isObject() || val.isDate()) {
            return _.stringifyCompactLeaf(global, name, val);
        }
        _.markVisiting(global, val)?;
        defer _.unmarkVisiting(val);
        if (val.isArray()) {
            return _.stringifyCompactArray(global, name, val, separate);
        }
        return _.stringifyCompactObject(global, name, val);
    }

    fn stringifyCompactArray(*_, global: *jsc.JSGlobalObject, name: *const bun.String, value: JSValue, separate: bool): StringifyError!void {
        let iter = value.arrayIterator(global)?;
        let first = true;
        while (item_raw := iter.next()?) {
            const item = item_raw.unwrapBoxedPrimitive(global)?;
            if (skipped(item)) continue;
            if (item.isArray()) {
                return global.throw("XML.stringify: nested arrays cannot be represented", {});
            }
            if (!first && separate) _.newline();
            first = false;
            _.stringifyCompactElement(global, name, item, separate)?;
        }
    }

    fn stringifyCompactLeaf(*_, global: *jsc.JSGlobalObject, name: *const bun.String, value: JSValue): StringifyError!void {
        _.checkName(global, name, "element")?;
        const sc = _.scalar(global, value, "element content")?;
        switch (sc) {
            .skip => {},
            .empty => _.appendEmptyElement(name.*),
            .text mut_t => {
                let t = mut_t;
                const t_guard = DerefGuard<bun.String>{ ptr: unsafe { &t } };
                if (t.length() == 0) {
                    _.appendEmptyElement(name.*);
                } else {
                    _.builder.append(.lchar, '<');
                    _.builder.append(.string, name.*);
                    _.builder.append(.lchar, '>');
                    _.appendText(global, &t)?;
                    _.appendEndTag(name.*);
                }
            },
        }
    }

    fn stringifyCompactObject(*_, global: *jsc.JSGlobalObject, name: *const bun.String, value: JSValue): StringifyError!void {
        _.checkName(global, name, "element")?;
        const obj = value.toObject(global)?;
        _.builder.append(.lchar, '<');
        _.builder.append(.string, name.*);
        let has_elements = false;
        let has_text = false;
        {
            let iter = PropIter.init(global, obj)?;
            defer iter.deinit();
            while (key := iter.next()?) {
                const child = iter.value;
                if (skipped(child)) continue;
                if (key.hasPrefixComptime("@")) {
                    const sc = _.scalar(global, child, "an attribute value")?;
                    switch (sc) {
                        .skip, .empty => {},
                        .text mut_t => {
                            let t = mut_t;
                            const t_guard = DerefGuard<bun.String>{ ptr: unsafe { &t } };
                            let sub = key.substring(1);
                            const sub_guard = DerefGuard<bun.String>{ ptr: unsafe { &sub } };
                            _.appendAttribute(global, &sub, &t)?;
                        },
                    }
                } else if (key.eqlComptime("#text")) {
                    const sc = _.scalar(global, child, "#text")?;
                    switch (sc) {
                        .text mut_t => {
                            let t = mut_t;
                            const t_guard = DerefGuard<bun.String>{ ptr: unsafe { &t } };
                            if (t.length() > 0) has_text = true;
                        },
                        else => {},
                    }
                } else if (key.hasPrefixComptime("#")) {
                    return global.throw("XML.stringify: unknown key (keys starting with '#' are reserved; text content is \"#text\")", {});
                } else if (hasOutput(global, child)?) {
                    has_elements = true;
                }
            }
        }
        if (!has_elements && !has_text) {
            _.builder.append(.latin1, "/>");
            return;
        }
        _.builder.append(.lchar, '>');
        const pretty = _.space.isPretty() && !has_text;
        if (pretty) _.indent += 1;
        {
            let iter = PropIter.init(global, obj)?;
            defer iter.deinit();
            while (key := iter.next()?) {
                const child = iter.value;
                if (skipped(child) || key.hasPrefixComptime("@")) continue;
                if (key.eqlComptime("#text")) {
                    const sc = _.scalar(global, child, "#text")?;
                    if (sc == .text) {
                        let t = sc.text;
                        const t_guard = DerefGuard<bun.String>{ ptr: unsafe { &t } };
                        _.appendText(global, &t)?;
                    }
                    continue;
                }
                if (!(hasOutput(global, child)?)) continue;
                if (pretty) _.newline();
                _.stringifyCompactElement(global, &key, child, pretty)?;
            }
        }
        if (pretty) {
            _.indent -= 1;
            _.newline();
        }
        _.appendEndTag(name.*);
    }

    fn scalar(*_, global: *jsc.JSGlobalObject, value: JSValue, what: string): StringifyError!Scalar {
        const val = value.unwrapBoxedPrimitive(global)?;
        if (skipped(val)) return .{ skip: undefined };
        if (val.isNull()) return .{ empty: undefined };
        if (val.isString() || val.isNumber() || val.isBoolean() || val.isBigInt()) {
            return .{ .text = val.toBunString(global)? };
        }
        if (val.isDate()) {
            let buf: [28]u8 = @splat(0);
            const iso = val.toISOString(global, &buf);
            if (iso.len > 0) {
                return .{ .text = bun.String.cloneUTF8(iso) };
            }
            return global.throw("XML.stringify cannot serialize an invalid Date", {});
        }
        return global.throw("XML.stringify: {s} must be a string, number, boolean, bigint, Date, or null", .{what});
    }

    fn checkName(*_, global: *jsc.JSGlobalObject, name: *const bun.String, what: string): StringifyError!void {
        const len = name.length();
        let valid = len > 0;
        let i: usize = 0;
        while (valid && i < len) {
            const pair = codePointAt(name, i);
            const cp = pair[0];
            const width = pair[1];
            valid = if (i == 0) xml.isNameStartChar(cp) else xml.isNameChar(cp);
            i += width;
        }
        if (!valid) {
            const s = name.toUTF8(bun.default_allocator);
            defer s.deinit();
            return global.throw("XML.stringify: '{s}' is not a valid XML {s} name", .{ s.slice(), what });
        }
    }

    fn appendEmptyElement(*_, name: bun.String) {
        _.builder.append(.lchar, '<');
        _.builder.append(.string, name);
        _.builder.append(.latin1, "/>");
    }

    fn appendEndTag(*_, name: bun.String) {
        _.builder.append(.latin1, "</");
        _.builder.append(.string, name);
        _.builder.append(.lchar, '>');
    }

    fn appendAttribute(*_, global: *jsc.JSGlobalObject, name: *const bun.String, value: *const bun.String): StringifyError!void {
        _.checkName(global, name, "attribute")?;
        _.builder.append(.lchar, ' ');
        _.builder.append(.string, name.*);
        _.builder.append(.latin1, "=\"");
        _.appendEscaped(global, value, true)?;
        _.builder.append(.lchar, '"');
    }

    fn appendText(*_, global: *jsc.JSGlobalObject, text: *const bun.String): StringifyError!void {
        return _.appendEscaped(global, text, false);
    }

    fn appendEscaped(*_, global: *jsc.JSGlobalObject, text: *const bun.String, attribute: bool): StringifyError!void {
        const len = text.length();
        let i: usize = 0;
        while (i < len) {
            const pair = codePointAt(text, i);
            const cp = pair[0];
            const width = pair[1];
            i += width;
            if (cp == 0x26) {
                _.builder.append(.latin1, "&amp;");
            } else if (cp == 0x3C) {
                _.builder.append(.latin1, "&lt;");
            } else if (cp == 0x3E) {
                _.builder.append(.latin1, "&gt;");
            } else if (cp == 0x22 && attribute) {
                _.builder.append(.latin1, "&quot;");
            } else if (cp == 0x09 && attribute) {
                _.builder.append(.latin1, "&#x9;");
            } else if (cp == 0x0A && attribute) {
                _.builder.append(.latin1, "&#xA;");
            } else if (cp == 0x0D) {
                _.builder.append(.latin1, "&#xD;");
            } else if (!xml.isXmlChar(cp)) {
                return global.throw("XML.stringify: XML cannot represent the character U+{X:0>4}", .{cp});
            } else if (cp >= 0x10000) {
                _.builder.append(.uchar, text.charAt(i - 2));
                _.builder.append(.uchar, text.charAt(i - 1));
            } else {
                _.builder.append(.uchar, @as(u16, @intCast(cp)));
            }
        }
    }

    fn newline(*_) {
        switch (_.space) {
            .minified => {},
            .number n => {
                _.builder.append(.lchar, '\n');
                let k: usize = 0;
                const total = _.indent * @as(usize, n);
                while (k < total) : (k += 1) {
                    _.builder.append(.lchar, ' ');
                }
            },
            .str s => {
                _.builder.append(.lchar, '\n');
                const clamped = if (s.length() > 10) s.substringWithLen(0, 10) else s;
                let k: usize = 0;
                while (k < _.indent) : (k += 1) {
                    _.builder.append(.string, clamped);
                }
            },
        }
    }
}

fn codePointAt(s: *const bun.String, i: usize): [2]u32 {
    const c = s.charAt(i);
    if (bun.strings.u16IsLead(c) && i + 1 < s.length()) {
        const nxt = s.charAt(i + 1);
        if (bun.strings.u16IsTrail(nxt)) {
            return [ bun.strings.u16GetSupplementary(c, nxt), 2 ];
        }
    }
    return [ @as(u32, c), 1 ];
}

fn exprToJS(expr: Expr, global: *jsc.JSGlobalObject): bun.JSError!jsc.JSValue {
    switch (expr.data) {
        .e_null => return JSValue.null,
        .e_boolean boolean => return JSValue.jsBoolean(boolean.value),
        .e_number number => return JSValue.jsNumber(number.value),
        .e_string str => return str.toJS(bun.default_allocator, global),
        .e_array arr => {
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
        .e_object obj => {
            let js_obj = JSValue.createEmptyObject(global, obj.properties.len);
            for (prop of obj.properties.slice()) {
                const key_expr = prop.key.?;
                const value = exprToJS(prop.value.?, global)?;
                const key_js = exprToJS(key_expr, global)?;
                let key_str = key_js.toBunString(global)?;
                const key_str_guard = DerefGuard<bun.String>{ ptr: unsafe { &key_str } };
                (js_obj.putMayBeIndex(global, &key_str, value))?;
            }
            return js_obj;
        },
        else => return JSValue.js_undefined,
    }
}
