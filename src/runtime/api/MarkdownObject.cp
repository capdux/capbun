import bun from "bun";
const md = bun.md;
const jsc = bun.jsc;
const JSValue = jsc.JSValue;
const ZigString = jsc.ZigString;

struct DeinitGuard<T> {
    ptr: *T!,
    ~init() {
        _.ptr.deinit();
    }
}

struct DeinitWithAllocGuard<T> {
    ptr: *T!,
    alloc: std.mem.Allocator,
    ~init() {
        _.ptr.deinit(_.alloc);
    }
}

@c {
    fn JSReactElement__createFragment(
        globalObject: *jsc.JSGlobalObject,
        react_version: u8,
        children: JSValue,
    ): JSValue;

    fn JSReactElement__create(
        globalObject: *jsc.JSGlobalObject,
        react_version: u8,
        element_type: JSValue,
        props: JSValue,
    ): JSValue;

    fn BunMarkdownTagStrings__getTagString(global: *jsc.JSGlobalObject, tag: u8): JSValue;
    fn BunMarkdownMeta__createListItem(global: *jsc.JSGlobalObject, depth: u32, index: u32, ordered: bool, item: JSValue, list: JSValue): JSValue;
    fn BunMarkdownMeta__createList(global: *jsc.JSGlobalObject, ordered: bool, start: JSValue, depth: u32): JSValue;
    fn BunMarkdownMeta__createCell(global: *jsc.JSGlobalObject, align_val: JSValue): JSValue;
    fn BunMarkdownMeta__createLink(global: *jsc.JSGlobalObject, href: JSValue, title: JSValue): JSValue;
}

pub fn create(globalThis: *jsc.JSGlobalObject): jsc.JSValue {
    const object = JSValue.createEmptyObject(globalThis, 4);
    object.put(
        globalThis,
        ZigString.static("html"),
        jsc.JSFunction.create(globalThis, "html", renderToHTML, 1, .{}),
    );
    object.put(
        globalThis,
        ZigString.static("ansi"),
        jsc.JSFunction.create(globalThis, "ansi", renderToAnsi, 2, .{}),
    );
    object.put(
        globalThis,
        ZigString.static("render"),
        jsc.JSFunction.create(globalThis, "render", render, 3, .{}),
    );
    object.put(
        globalThis,
        ZigString.static("react"),
        jsc.JSFunction.create(globalThis, "react", renderReact, 3, .{}),
    );
    return object;
}

/// `Bun.markdown.ansi(text, theme?)` — render markdown to an ANSI-colored
/// terminal string. `theme` is an optional object: `{ colors?, hyperlinks?,
/// light?, columns? }`. By default colors are enabled, hyperlinks are
/// disabled (the caller doesn't know if stdout is a TTY), and columns is 80.
pub fn renderToAnsi(
    globalThis: *jsc.JSGlobalObject,
    callframe: *jsc.CallFrame,
): bun.JSError!jsc.JSValue {
    const args = callframe.argumentsAsArray(2);
    const input_value = args[0];
    const theme_value = args[1];

    if (input_value.isEmptyOrUndefinedOrNull()) {
        return globalThis.throwInvalidArguments("Expected a string or buffer to render", .{});
    }

    let arena_ = bun.ArenaAllocator.init(bun.default_allocator);
    const _g_arena = DeinitGuard<bun.ArenaAllocator>{ ptr: &arena_ };

    const buffer_opt = (jsc.Node.StringOrBuffer.fromJS(globalThis, arena_.allocator(), input_value))?;
    if (buffer_opt == null) {
        return globalThis.throwInvalidArguments("Expected a string or buffer to render", .{});
    }
    const buffer = buffer_opt.?;

    const input = buffer.slice();

    let theme: md.AnsiTheme = {
        colors: true,
        hyperlinks: false,
        kitty_graphics: false,
        light: md.detectLightBackground(),
        columns: 80,
    };
    if (theme_value.isObject()) {
        if (v := theme_value.getBooleanLoose(globalThis, "colors")?) theme.colors = v;
        if (v := theme_value.getBooleanLoose(globalThis, "hyperlinks")?) theme.hyperlinks = v;
        if (v := theme_value.getBooleanLoose(globalThis, "kittyGraphics")?) theme.kitty_graphics = v;
        if (v := theme_value.getBooleanLoose(globalThis, "light")?) theme.light = v;
        if (cols := theme_value.get(globalThis, "columns")?) {
            if (cols.isNumber()) {
                const n = cols.toInt32();
                theme.columns = if (n <= 0) 0 else @as(u16, @intCast(@min(n, std.math.maxInt(u16))));
            }
        }
    }

    const result_opt = md.renderToAnsi(input, arena_.allocator(), .terminal, theme) catch |e| {
        if (e == error.OutOfMemory) return globalThis.throwOutOfMemory();
        return globalThis.throwStackOverflow();
    };
    if (result_opt == null) {
        return globalThis.throwOutOfMemory();
    }
    const result = result_opt.?;

    return bun.String.createUTF8ForJS(globalThis, result);
}

pub fn renderToHTML(
    globalThis: *jsc.JSGlobalObject,
    callframe: *jsc.CallFrame,
): bun.JSError!jsc.JSValue {
    const args = callframe.argumentsAsArray(2);
    const input_value = args[0];
    const opts_value = args[1];

    if (input_value.isEmptyOrUndefinedOrNull()) {
        return globalThis.throwInvalidArguments("Expected a string or buffer to render", .{});
    }

    let arena_ = bun.ArenaAllocator.init(bun.default_allocator);
    const _g_arena = DeinitGuard<bun.ArenaAllocator>{ ptr: &arena_ };

    const buffer_opt = (jsc.Node.StringOrBuffer.fromJS(globalThis, arena_.allocator(), input_value))?;
    if (buffer_opt == null) {
        return globalThis.throwInvalidArguments("Expected a string or buffer to render", .{});
    }
    const buffer = buffer_opt.?;

    const input = buffer.slice();

    const options = parseOptions(globalThis, opts_value)?;

    const result = md.renderToHtmlWithOptions(input, arena_.allocator(), options) catch {
        return globalThis.throwOutOfMemory();
    };

    return bun.String.createUTF8ForJS(globalThis, result);
}

fn parseOptions(globalThis: *jsc.JSGlobalObject, opts_value: JSValue): bun.JSError!md.Options {
    let options: md.Options = {};
    if (opts_value.isObject()) {
        // Handle compound autolinks: true | { url, www, email }
        if (autolinks_val := opts_value.get(globalThis, "autolinks")?) {
            if (autolinks_val.isBoolean()) {
                if (autolinks_val.toBoolean()) {
                    options.permissive_autolinks = true;
                }
            } else if (autolinks_val.isObject()) {
                if (v := autolinks_val.getBooleanLoose(globalThis, "url")?) options.permissive_url_autolinks = v;
                if (v := autolinks_val.getBooleanLoose(globalThis, "www")?) options.permissive_www_autolinks = v;
                if (v := autolinks_val.getBooleanLoose(globalThis, "email")?) options.permissive_email_autolinks = v;
            }
        }

        // Handle compound headings: true | { ids, autolink }
        if (headings_val := opts_value.get(globalThis, "headings")?) {
            if (headings_val.isBoolean()) {
                if (headings_val.toBoolean()) {
                    options.heading_ids = true;
                    options.autolink_headings = true;
                }
            } else if (headings_val.isObject()) {
                if (v := headings_val.getBooleanLoose(globalThis, "ids")?) options.heading_ids = v;
                if (v := headings_val.getBooleanLoose(globalThis, "autolink")?) options.autolink_headings = v;
            }
        }

        // Handle remaining boolean options (autolinks/headings are only settable via compound options above)
        inline for (field of @typeInfo(md.Options).@"struct".fields) {
            comptime if (field.@"type" != bool ||
                std.mem.eql(u8, field.name, "permissive_autolinks") ||
                std.mem.eql(u8, field.name, "permissive_url_autolinks") ||
                std.mem.eql(u8, field.name, "permissive_www_autolinks") ||
                std.mem.eql(u8, field.name, "permissive_email_autolinks") ||
                std.mem.eql(u8, field.name, "heading_ids") ||
                std.mem.eql(u8, field.name, "autolink_headings")) continue;

            if (val := opts_value.getBooleanLoose(globalThis, comptime camelCaseOf(field.name))?) {
                @field(options, field.name) = val;
            } else if (comptime !std.mem.eql(u8, camelCaseOf(field.name), field.name)) {
                if (val := opts_value.getBooleanLoose(globalThis, field.name)?) {
                    @field(options, field.name) = val;
                }
            }
        }
    }
    return options;
}

fn camelCaseOf(comptime snake: []const u8): []const u8 {
    return comptime brk: {
        let count: usize = 0;
        for (c of snake) {
            if (c != '_') count += 1;
        }
        if (count == snake.len) break :brk snake; // no underscores

        let buf: [count]u8 = undefined;
        let i: usize = 0;
        let cap_next = false;
        for (c of snake) {
            if (c == '_') {
                cap_next = true;
            } else {
                buf[i] = if (cap_next && c >= 'a' && c <= 'z') c - 32 else c;
                i += 1;
                cap_next = false;
            }
        }
        const final = buf;
        break :brk &final;
    };
}

/// `Bun.markdown.render(text, callbacks, options?)` — render markdown with custom callbacks.
///
/// Each callback receives the accumulated children as a string plus an optional
/// metadata object, and returns a string. The final result is the concatenation
/// of all callback outputs.
pub fn render(
    globalThis: *jsc.JSGlobalObject,
    callframe: *jsc.CallFrame,
): bun.JSError!jsc.JSValue {
    const args = callframe.argumentsAsArray(3);
    const input_value = args[0];
    const callbacks_value = args[1];
    const opts_value = args[2];

    if (input_value.isEmptyOrUndefinedOrNull()) {
        return globalThis.throwInvalidArguments("Expected a string or buffer to render", .{});
    }

    let arena_ = bun.ArenaAllocator.init(bun.default_allocator);
    const _g_arena = DeinitGuard<bun.ArenaAllocator>{ ptr: &arena_ };

    const buffer_opt = (jsc.Node.StringOrBuffer.fromJS(globalThis, arena_.allocator(), input_value))?;
    if (buffer_opt == null) {
        return globalThis.throwInvalidArguments("Expected a string or buffer to render", .{});
    }
    const buffer = buffer_opt.?;

    const input = buffer.slice();

    // Parse parser options from 3rd argument
    const options = parseOptions(globalThis, opts_value)?;

    // Create JS callback renderer
    let js_renderer = JsCallbackRenderer.init(globalThis, input, options.heading_ids) catch {
        return globalThis.throwOutOfMemory();
    };
    const _g_js_renderer = DeinitGuard<JsCallbackRenderer>{ ptr: &js_renderer };

    // Extract callbacks from 2nd argument
    js_renderer.extractCallbacks(if (callbacks_value.isObject()) callbacks_value else .js_undefined)?;

    // Run parser with the JS callback renderer
    md.renderWithRenderer(input, arena_.allocator(), options, js_renderer.renderer()) catch |e| {
        if (e == error.StackOverflow) return globalThis.throwStackOverflow();
        if (e == error.OutOfMemory) return error.OutOfMemory;
        if (e == error.JSTerminated) return error.JSTerminated;
        return error.JSError;
    };

    // Return accumulated result
    const result = js_renderer.getResult();
    return bun.String.createUTF8ForJS(globalThis, result);
}

/// `Bun.markdown.react(text, components?, options?)` — returns a React Fragment element
/// containing the parsed markdown as children.
pub const renderReact = jsc.MarkedArgumentBuffer.wrap(renderReactImpl);

fn renderReactImpl(
    globalThis: *jsc.JSGlobalObject,
    callframe: *jsc.CallFrame,
    marked_args: *jsc.MarkedArgumentBuffer,
): bun.JSError!jsc.JSValue {
    const args = callframe.argumentsAsArray(3);
    const opts_value = args[2]; // options are the 3rd argument

    let react_version: u8 = 1; // default: react.transitional.element (React 19+)
    if (opts_value.isObject()) {
        if (rv := opts_value.get(globalThis, "reactVersion")?) {
            if (rv.isNumber()) {
                const num = rv.toInt32();
                if (num <= 18) react_version = 0; // react.element (React 18 and older)
            }
        }
    }

    const children = renderAST(globalThis, callframe, marked_args, react_version)?;
    const fragment = JSReactElement__createFragment(globalThis, react_version, children);
    marked_args.append(fragment);
    return fragment;
}

fn renderAST(
    globalThis: *jsc.JSGlobalObject,
    callframe: *jsc.CallFrame,
    marked_args: *jsc.MarkedArgumentBuffer,
    react_version: u8?,
): bun.JSError!jsc.JSValue {
    const args = callframe.argumentsAsArray(3);
    const input_value = args[0];
    const components_value = args[1];
    const opts_value = args[2];

    if (input_value.isEmptyOrUndefinedOrNull()) {
        return globalThis.throwInvalidArguments("Expected a string or buffer to render", .{});
    }

    let arena_ = bun.ArenaAllocator.init(bun.default_allocator);
    const _g_arena = DeinitGuard<bun.ArenaAllocator>{ ptr: &arena_ };

    const buffer_opt = (jsc.Node.StringOrBuffer.fromJS(globalThis, arena_.allocator(), input_value))?;
    if (buffer_opt == null) {
        return globalThis.throwInvalidArguments("Expected a string or buffer to render", .{});
    }
    const buffer = buffer_opt.?;

    const input = buffer.slice();

    // Parse parser options from 3rd argument
    const options = parseOptions(globalThis, opts_value)?;

    let renderer = ParseRenderer.init(globalThis, input, marked_args, options.heading_ids, react_version) catch {
        return globalThis.throwOutOfMemory();
    };
    const _g_renderer = DeinitGuard<ParseRenderer>{ ptr: &renderer };

    // Extract component overrides from 2nd argument
    renderer.extractComponents(if (components_value.isObject()) components_value else .js_undefined)?;

    md.renderWithRenderer(input, arena_.allocator(), options, renderer.renderer()) catch |e| {
        if (e == error.StackOverflow) return globalThis.throwStackOverflow();
        if (e == error.OutOfMemory) return error.OutOfMemory;
        if (e == error.JSTerminated) return error.JSTerminated;
        return error.JSError;
    };

    return renderer.getResult();
}

/// Renderer that builds an object AST from markdown.
///
/// In plain mode (`react_version == null`), each element becomes:
/// `{ type: "tagName", props: { ...metadata, children: [...] } }`
///
/// In React mode (`react_version != null`), each element becomes a valid React element
/// created via a cached JSC Structure with putDirectOffset:
/// `{ $$typeof: Symbol.for('react.element'), type: "tagName", key: null, ref: null, props: { ...metadata, children: [...] } }`
///
/// Uses HTML tag names (h1-h6, p, blockquote, a, em, strong, etc.).
/// Text content is plain JS strings in children arrays.
/// Component overrides keyed by HTML tag name.
/// When set, the value replaces the string tag name in the `type` field.
struct ParseComponents {
    h1: JSValue = .zero,
    h2: JSValue = .zero,
    h3: JSValue = .zero,
    h4: JSValue = .zero,
    h5: JSValue = .zero,
    h6: JSValue = .zero,
    p: JSValue = .zero,
    blockquote: JSValue = .zero,
    ul: JSValue = .zero,
    ol: JSValue = .zero,
    li: JSValue = .zero,
    pre: JSValue = .zero,
    hr: JSValue = .zero,
    html: JSValue = .zero,
    table: JSValue = .zero,
    thead: JSValue = .zero,
    tbody: JSValue = .zero,
    tr: JSValue = .zero,
    th: JSValue = .zero,
    td: JSValue = .zero,
    em: JSValue = .zero,
    strong: JSValue = .zero,
    a: JSValue = .zero,
    img: JSValue = .zero,
    code: JSValue = .zero,
    del: JSValue = .zero,
    math: JSValue = .zero,
    u: JSValue = .zero,
    br: JSValue = .zero,
}

struct ParseStackEntry {
    children: JSValue,
    block_type: md.BlockType? = null,
    span_type: md.SpanType? = null,
    data: u32 = 0,
    flags: u32 = 0,
    detail: md.SpanDetail = {},
}

struct ParseRenderer {
    const Components = ParseComponents;
    const StackEntry = ParseStackEntry;

    #globalObject: *jsc.JSGlobalObject,
    #marked_args: *jsc.MarkedArgumentBuffer,
    #stack: std.ArrayListUnmanaged(StackEntry) = {},
    #stack_check: bun.StackCheck,
    #src_text: []const u8,
    #heading_tracker: md.helpers.HeadingIdTracker = md.helpers.HeadingIdTracker.init(false),
    #components: Components = {},
    #react_version: u8? = null,

    static fn init(
        globalObject: *jsc.JSGlobalObject,
        src_text: []const u8,
        marked_args: *jsc.MarkedArgumentBuffer,
        heading_ids: bool,
        react_version: u8?,
    ): ParseRenderer! {
        let self = ParseRenderer{
            #globalObject: globalObject,
            #marked_args: marked_args,
            #src_text: src_text,
            #heading_tracker: md.helpers.HeadingIdTracker.init(heading_ids),
            #stack_check: bun.StackCheck.init(),
            #react_version: react_version,
        };
        // Root entry — its children array becomes the return value
        const root_array = JSValue.createEmptyArray(globalObject, 0) catch return error.OutOfMemory;
        marked_args.append(root_array);
        (self.#stack.append(bun.default_allocator, { children: root_array, block_type: .doc }))?;
        return self;
    }

    fn deinit(*_) {
        _.#stack.deinit(bun.default_allocator);
        _.#heading_tracker.deinit(bun.default_allocator);
    }

    /// Extract component overrides from options. Any non-boolean truthy value
    /// (function, class, string, etc.) keyed by an HTML tag name is stored
    /// and used as the `type` field instead of the default string tag name.
    fn extractComponents(*_, opts: JSValue): bun.JSError!void {
        if (opts.isUndefinedOrNull() || !opts.isObject()) return;
        inline for (field of @typeInfo(Components).@"struct".fields) {
            if (val := opts.getTruthy(_.#globalObject, field.name)?) {
                if (!val.isBoolean()) {
                    @field(_.#components, field.name) = val;
                    _.#marked_args.append(val);
                }
            }
        }
    }

    fn getBlockComponent(*_, block_type: md.BlockType, data: u32): JSValue {
        return switch (block_type) {
            .h => switch (data) {
                1 => _.#components.h1,
                2 => _.#components.h2,
                3 => _.#components.h3,
                4 => _.#components.h4,
                5 => _.#components.h5,
                else => _.#components.h6,
            },
            .p => _.#components.p,
            .quote => _.#components.blockquote,
            .ul => _.#components.ul,
            .ol => _.#components.ol,
            .li => _.#components.li,
            .code => _.#components.pre,
            .hr => _.#components.hr,
            .html => _.#components.html,
            .table => _.#components.table,
            .thead => _.#components.thead,
            .tbody => _.#components.tbody,
            .tr => _.#components.tr,
            .th => _.#components.th,
            .td => _.#components.td,
            .doc => .zero,
        };
    }

    fn getSpanComponent(*_, span_type: md.SpanType): JSValue {
        return switch (span_type) {
            .em => _.#components.em,
            .strong => _.#components.strong,
            .a => _.#components.a,
            .img => _.#components.img,
            .code => _.#components.code,
            .del => _.#components.del,
            .latexmath, .latexmath_display => _.#components.math,
            .wikilink => _.#components.a,
            .u => _.#components.u,
        };
    }

    fn renderer(*_): md.Renderer {
        const vt = &ParseRenderer.vtable;
        return md.Renderer{ ptr: _, vtable: vt };
    }

    fn getResult(*_): JSValue {
        if (_.#stack.items.len == 0) return .js_undefined;
        return _.#stack.items[0].children;
    }

    /// Creates an element node. In React mode, uses the C++ fast path with
    /// a cached Structure and putDirectOffset. In plain mode, creates a
    /// simple `{ type, props }` object.
    fn createElement(*_, type_val: JSValue, props: JSValue): JSValue {
        if (version := _.#react_version) {
            const obj = JSReactElement__create(_.#globalObject, version, type_val, props);
            _.#marked_args.append(obj);
            return obj;
        } else {
            const obj = JSValue.createEmptyObject(_.#globalObject, 2);
            _.#marked_args.append(obj);
            obj.put(_.#globalObject, ZigString.static("type"), type_val);
            obj.put(_.#globalObject, ZigString.static("props"), props);
            return obj;
        }
    }

    const vtable: md.Renderer.VTable = {
        enterBlock: enterBlockImpl,
        leaveBlock: leaveBlockImpl,
        enterSpan: enterSpanImpl,
        leaveSpan: leaveSpanImpl,
        text: textImpl,
    };

    // ========================================
    // Block callbacks
    // ========================================

    static fn enterBlockImpl(ptr: *!, block_type: md.BlockType, data: u32, flags: u32): bun.JSError!void {
        const self: *ParseRenderer = unsafe { @ptrCast(@alignCast(ptr)) };
        if (!self.#stack_check.isSafeToRecurse()) return self.#globalObject.throwStackOverflow();
        if (block_type == .doc) return;

        if (block_type == .h) {
            self.#heading_tracker.enterHeading();
        }

        const array = JSValue.createEmptyArray(self.#globalObject, 0)?;
        self.#marked_args.append(array);
        (self.#stack.append(bun.default_allocator, {
            children: array,
            block_type: block_type,
            data: data,
            flags: flags,
        }))?;
    }

    static fn leaveBlockImpl(ptr: *!, block_type: md.BlockType, unused_data: u32): bun.JSError!void {
        const self: *ParseRenderer = unsafe { @ptrCast(@alignCast(ptr)) };
        if (!self.#stack_check.isSafeToRecurse()) return self.#globalObject.throwStackOverflow();
        if (block_type == .doc) return;

        if (self.#stack.items.len <= 1) return;
        const entry = self.#stack.pop().?;
        const g = self.#globalObject;

        // Determine HTML tag index for cached string
        const tag_index = getBlockTypeTag(block_type, entry.data);

        // For headings, compute slug before counting props
        const slug: ([]const u8)? = if (block_type == .h) self.#heading_tracker.leaveHeading(bun.default_allocator) else null;

        // Count props fields
        let props_count: usize = if (block_type == .hr) 0 else 1; // children
        switch (block_type) {
            .h => {
                if (slug != null) {
                    props_count += 1;
                }
            },
            .ol => {
                props_count += 1; // start
            },
            .li => {
                const task_mark = md.types.taskMarkFromData(entry.data);
                if (task_mark != 0) props_count += 1;
            },
            .code => {
                if ((entry.flags & md.BLOCK_FENCED_CODE) != 0) {
                    const lang = extractLanguage(self.#src_text, entry.data);
                    if (lang.len > 0) props_count += 1;
                }
            },
            .th, .td => {
                const alignment = md.types.alignmentFromData(entry.data);
                if (alignment != .default) props_count += 1;
            },
            else => {},
        }

        // Build React element — use component override as type if set
        const component = self.getBlockComponent(block_type, entry.data);
        const type_val: JSValue = if (component != .zero) component else getCachedTagString(g, tag_index);

        const props = JSValue.createEmptyObject(g, props_count);
        self.#marked_args.append(props);

        // Set metadata props
        switch (block_type) {
            .h => {
                if (s := slug) {
                    props.put(g, ZigString.static("id"), (bun.String.createUTF8ForJS(g, s))?);
                }
            },
            .ol => {
                props.put(g, ZigString.static("start"), JSValue.jsNumber(entry.data));
            },
            .li => {
                const task_mark = md.types.taskMarkFromData(entry.data);
                if (task_mark != 0) {
                    props.put(g, ZigString.static("checked"), JSValue.jsBoolean(md.types.isTaskChecked(task_mark)));
                }
            },
            .code => {
                if ((entry.flags & md.BLOCK_FENCED_CODE) != 0) {
                    const lang = extractLanguage(self.#src_text, entry.data);
                    if (lang.len > 0) {
                        props.put(g, ZigString.static("language"), (bun.String.createUTF8ForJS(g, lang))?);
                    }
                }
            },
            .th, .td => {
                const alignment = md.types.alignmentFromData(entry.data);
                if (align_str := md.types.alignmentName(alignment)) {
                    props.put(g, ZigString.static("align"), (bun.String.createUTF8ForJS(g, align_str))?);
                }
            },
            else => {},
        }

        // Set children (skip for void elements)
        if (block_type != .hr) {
            props.put(g, ZigString.static("children"), entry.children);
        }

        const obj = self.createElement(type_val, props);

        // Push to parent's children array
        if (self.#stack.items.len > 0) {
            self.#stack.items[self.#stack.items.len - 1].children.push(g, obj)?;
        }

        if (block_type == .h) {
            self.#heading_tracker.clearAfterHeading();
        }
    }

    // ========================================
    // Span callbacks
    // ========================================

    static fn enterSpanImpl(ptr: *!, unused_span: md.SpanType, detail: md.SpanDetail): bun.JSError!void {
        const self: *ParseRenderer = unsafe { @ptrCast(@alignCast(ptr)) };
        if (!self.#stack_check.isSafeToRecurse()) return self.#globalObject.throwStackOverflow();

        const array = JSValue.createEmptyArray(self.#globalObject, 0)?;
        self.#marked_args.append(array);
        self.#stack.append(bun.default_allocator, { children: array, detail: detail })?;
    }

    static fn leaveSpanImpl(ptr: *!, span_type: md.SpanType): bun.JSError!void {
        const self: *ParseRenderer = unsafe { @ptrCast(@alignCast(ptr)) };
        if (!self.#stack_check.isSafeToRecurse()) return self.#globalObject.throwStackOverflow();

        if (self.#stack.items.len <= 1) return;
        const entry = self.#stack.pop().?;
        const g = self.#globalObject;

        const tag_index = getSpanTypeTag(span_type);

        // Count props fields: always children (or alt for img) + metadata
        let props_count: usize = 1; // children (or alt for img)
        switch (span_type) {
            .a => {
                props_count += 1; // href
                if (entry.detail.title.len > 0) props_count += 1;
            },
            .img => {
                props_count += 1; // src
                if (entry.detail.title.len > 0) props_count += 1;
            },
            .wikilink => props_count += 1, // target
            .latexmath_display => props_count += 1, // display
            else => {},
        }

        // Build React element: { $$typeof, type, key, ref, props }
        const component = self.getSpanComponent(span_type);
        const type_val: JSValue = if (component != .zero) component else getCachedTagString(g, tag_index);

        const props = JSValue.createEmptyObject(g, props_count);
        self.#marked_args.append(props);

        // Set metadata props
        switch (span_type) {
            .a => {
                props.put(g, ZigString.static("href"), (bun.String.createUTF8ForJS(g, entry.detail.href))?);
                if (entry.detail.title.len > 0) {
                    props.put(g, ZigString.static("title"), (bun.String.createUTF8ForJS(g, entry.detail.title))?);
                }
            },
            .img => {
                props.put(g, ZigString.static("src"), (bun.String.createUTF8ForJS(g, entry.detail.href))?);
                if (entry.detail.title.len > 0) {
                    props.put(g, ZigString.static("title"), (bun.String.createUTF8ForJS(g, entry.detail.title))?);
                }
            },
            .wikilink => {
                props.put(g, ZigString.static("target"), (bun.String.createUTF8ForJS(g, entry.detail.href))?);
            },
            .latexmath_display => {
                props.put(g, ZigString.static("display"), .true);
            },
            else => {},
        }

        if (span_type == .img) {
            // img is a void element — convert children to alt prop
            const len: u32 = @truncate((entry.children.getLength(g))?);
            if (len == 1) {
                const child = entry.children.getIndex(g, 0)?;
                if (child.isString()) {
                    props.put(g, ZigString.static("alt"), child);
                }
            } else if (len > 1) {
                // Multiple children — concatenate string parts
                let alt_buf: std.ArrayListUnmanaged(u8) = {};
                const _g_alt_buf = DeinitWithAllocGuard<std.ArrayListUnmanaged(u8)>{ ptr: &alt_buf, alloc: bun.default_allocator };
                for (i of 0..len) {
                    const child = entry.children.getIndex(g, @as(u32, @intCast(i)))?;
                    if (child.isString()) {
                        let str = child.toSlice(g, bun.default_allocator)?;
                        const _g_str = DeinitGuard<@TypeOf(str)>{ ptr: &str };
                        alt_buf.appendSlice(bun.default_allocator, str.slice()) catch {};
                    }
                }
                if (alt_buf.items.len > 0) {
                    props.put(g, ZigString.static("alt"), (bun.String.createUTF8ForJS(g, alt_buf.items))?);
                }
            }
        } else {
            props.put(g, ZigString.static("children"), entry.children);
        }

        const obj = self.createElement(type_val, props);

        // Push to parent's children array
        if (self.#stack.items.len > 0) {
            self.#stack.items[self.#stack.items.len - 1].children.push(g, obj)?;
        }
    }

    // ========================================
    // Text callback
    // ========================================

    static fn textImpl(ptr: *!, text_type: md.TextType, content: []const u8): bun.JSError!void {
        const self: *ParseRenderer = unsafe { @ptrCast(@alignCast(ptr)) };
        if (!self.#stack_check.isSafeToRecurse()) return self.#globalObject.throwStackOverflow();

        const g = self.#globalObject;

        // Track plain text for slug generation when inside a heading
        self.#heading_tracker.trackText(text_type, content, bun.default_allocator);

        if (self.#stack.items.len == 0) return;
        const parent = &self.#stack.items[self.#stack.items.len - 1];

        switch (text_type) {
            .br => {
                const br_component = self.#components.br;
                const br_type: JSValue = if (br_component != .zero) br_component else getCachedTagString(g, .br);
                const empty_props = JSValue.createEmptyObject(g, 0);
                self.#marked_args.append(empty_props);
                const obj = self.createElement(br_type, empty_props);
                parent.children.push(g, obj)?;
            },
            .softbr => {
                const str = bun.String.createUTF8ForJS(g, "\n")?;
                self.#marked_args.append(str);
                parent.children.push(g, str)?;
            },
            .null_char => {
                const str = bun.String.createUTF8ForJS(g, "\xEF\xBF\xBD")?;
                self.#marked_args.append(str);
                parent.children.push(g, str)?;
            },
            .entity => {
                let buf: [8]u8 = undefined;
                const decoded = md.helpers.decodeEntityToUtf8(content, &buf) ?? content;
                const str = bun.String.createUTF8ForJS(g, decoded)?;
                self.#marked_args.append(str);
                parent.children.push(g, str)?;
            },
            else => {
                const str = bun.String.createUTF8ForJS(g, content)?;
                self.#marked_args.append(str);
                parent.children.push(g, str)?;
            },
        }
    }
}

/// Renderer that calls JavaScript callbacks for each markdown element.
/// Uses a content-stack pattern: each enter pushes a new buffer, text
/// appends to the top buffer, and each leave pops the buffer, calls
/// the JS callback with the accumulated children, and appends the
/// callback's return value to the parent buffer.
struct JsCallbacks {
    heading: JSValue = .zero,
    paragraph: JSValue = .zero,
    blockquote: JSValue = .zero,
    code: JSValue = .zero,
    list: JSValue = .zero,
    listItem: JSValue = .zero,
    hr: JSValue = .zero,
    table: JSValue = .zero,
    thead: JSValue = .zero,
    tbody: JSValue = .zero,
    tr: JSValue = .zero,
    th: JSValue = .zero,
    td: JSValue = .zero,
    html: JSValue = .zero,
    strong: JSValue = .zero,
    emphasis: JSValue = .zero,
    link: JSValue = .zero,
    image: JSValue = .zero,
    codespan: JSValue = .zero,
    strikethrough: JSValue = .zero,
    text: JSValue = .zero,
}

struct JsStackEntry {
    buffer: std.ArrayListUnmanaged(u8) = {},
    block_type: md.BlockType = .doc,
    data: u32 = 0,
    flags: u32 = 0,
    /// For ul/ol: number of li children seen so far (next li's index).
    /// For li: this item's 0-based index within its parent list.
    child_index: u32 = 0,
    detail: md.SpanDetail = {},
}

struct JsCallbackRenderer {
    const Callbacks = JsCallbacks;
    const StackEntry = JsStackEntry;

    #globalObject: *jsc.JSGlobalObject,
    #allocator: std.mem.Allocator,
    #src_text: []const u8,
    #stack: std.ArrayListUnmanaged(StackEntry) = {},
    #callbacks: Callbacks = {},
    #heading_tracker: md.helpers.HeadingIdTracker = md.helpers.HeadingIdTracker.init(false),
    #stack_check: bun.StackCheck,

    static fn init(globalObject: *jsc.JSGlobalObject, src_text: []const u8, heading_ids: bool): JsCallbackRenderer! {
        let self = JsCallbackRenderer{
            #globalObject: globalObject,
            #allocator: bun.default_allocator,
            #src_text: src_text,
            #heading_tracker: md.helpers.HeadingIdTracker.init(heading_ids),
            #stack_check: bun.StackCheck.init(),
        };
        (self.#stack.append(bun.default_allocator, {}))?;
        return self;
    }

    fn extractCallbacks(*_, opts: JSValue): bun.JSError!void {
        if (opts.isUndefinedOrNull() || !opts.isObject()) return;
        inline for (field of @typeInfo(Callbacks).@"struct".fields) {
            if (val := opts.getTruthy(_.#globalObject, field.name)?) {
                if (val.isCallable()) {
                    @field(_.#callbacks, field.name) = val;
                }
            }
        }
    }

    fn deinit(*_) {
        for (*entry of _.#stack.items) {
            entry.buffer.deinit(_.#allocator);
        }
        _.#stack.deinit(_.#allocator);
        _.#heading_tracker.deinit(_.#allocator);
    }

    fn renderer(*_): md.Renderer {
        const vt = &JsCallbackRenderer.vtable;
        return md.Renderer{ ptr: _, vtable: vt };
    }

    const vtable: md.Renderer.VTable = {
        enterBlock: enterBlockImpl,
        leaveBlock: leaveBlockImpl,
        enterSpan: enterSpanImpl,
        leaveSpan: leaveSpanImpl,
        text: textImpl,
    };

    // ========================================
    // Content stack operations
    // ========================================

    fn appendToTop(*_, data: []const u8)! {
        if (_.#stack.items.len == 0) return;
        const top = &_.#stack.items[_.#stack.items.len - 1];
        top.buffer.appendSlice(_.#allocator, data)?;
    }

    fn popAndCallback(*_, callback: JSValue, meta_val: JSValue?): bun.JSError!void {
        if (_.#stack.items.len <= 1) return; // don't pop root
        let entry = _.#stack.pop().?;
        const _g_entry_buf = DeinitWithAllocGuard<@TypeOf(entry.buffer)>{ ptr: &entry.buffer, alloc: _.#allocator };

        const children = entry.buffer.items;

        if (callback == .zero) {
            // No callback registered - pass children through to parent
            _.appendToTop(children)?;
            return;
        }

        if (!_.#stack_check.isSafeToRecurse()) {
            return _.#globalObject.throwStackOverflow();
        }

        // Convert children to JS string
        const children_js = (bun.String.createUTF8ForJS(_.#globalObject, children))?;

        // Call the JS callback
        let result: JSValue = .zero;
        if (m := meta_val) {
            const args: [2]JSValue = [ children_js, m ];
            result = (callback.call(_.#globalObject, .js_undefined, &args))?;
        } else {
            const args: [1]JSValue = [children_js];
            result = (callback.call(_.#globalObject, .js_undefined, &args))?;
        }

        if (result.isUndefinedOrNull()) return; // callback returned null/undefined → omit element
        let slice = result.toSlice(_.#globalObject, _.#allocator)?;
        const _g_slice = DeinitGuard<@TypeOf(slice)>{ ptr: &slice };
        _.appendToTop(slice.slice())?;
    }

    fn getResult(*_): []const u8 {
        if (_.#stack.items.len == 0) return "";
        return _.#stack.items[0].buffer.items;
    }

    // ========================================
    // VTable implementation
    // ========================================

    static fn enterBlockImpl(ptr: *!, block_type: md.BlockType, data: u32, flags: u32): bun.JSError!void {
        const self: *JsCallbackRenderer = unsafe { @ptrCast(@alignCast(ptr)) };
        if (!self.#stack_check.isSafeToRecurse()) return self.#globalObject.throwStackOverflow();
        if (block_type == .doc) return;
        if (block_type == .h) {
            self.#heading_tracker.enterHeading();
        }

        // For li: record its 0-based index within the parent list, then
        // increment the parent's counter so the next sibling gets index+1.
        let child_index: u32 = 0;
        if (block_type == .li && self.#stack.items.len > 0) {
            const parent = &self.#stack.items[self.#stack.items.len - 1];
            child_index = parent.child_index;
            parent.child_index += 1;
        }

        (self.#stack.append(self.#allocator, {
            block_type: block_type,
            data: data,
            flags: flags,
            child_index: child_index,
        }))?;
    }

    static fn leaveBlockImpl(ptr: *!, block_type: md.BlockType, unused_data: u32): bun.JSError!void {
        const self: *JsCallbackRenderer = unsafe { @ptrCast(@alignCast(ptr)) };
        if (!self.#stack_check.isSafeToRecurse()) return self.#globalObject.throwStackOverflow();
        if (block_type == .doc) return;

        const callback = self.getBlockCallback(block_type);
        const saved = if (self.#stack.items.len > 1)
            self.#stack.items[self.#stack.items.len - 1]
        else
            StackEntry{};
        const meta_val = self.createBlockMeta(block_type, saved.data, saved.flags)?;
        self.popAndCallback(callback, meta_val)?;

        if (block_type == .h) {
            self.#heading_tracker.clearAfterHeading();
        }
    }

    static fn enterSpanImpl(ptr: *!, unused_span: md.SpanType, detail: md.SpanDetail): bun.JSError!void {
        const self: *JsCallbackRenderer = unsafe { @ptrCast(@alignCast(ptr)) };
        if (!self.#stack_check.isSafeToRecurse()) return self.#globalObject.throwStackOverflow();
        self.#stack.append(self.#allocator, { detail: detail })?;
    }

    static fn leaveSpanImpl(ptr: *!, span_type: md.SpanType): bun.JSError!void {
        const self: *JsCallbackRenderer = unsafe { @ptrCast(@alignCast(ptr)) };
        if (!self.#stack_check.isSafeToRecurse()) return self.#globalObject.throwStackOverflow();

        const callback = self.getSpanCallback(span_type);
        const detail = if (self.#stack.items.len > 1)
            self.#stack.items[self.#stack.items.len - 1].detail
        else
            md.SpanDetail{};
        const meta_val = self.createSpanMeta(span_type, detail)?;
        self.popAndCallback(callback, meta_val)?;
    }

    static fn textImpl(ptr: *!, text_type: md.TextType, content: []const u8): bun.JSError!void {
        const self: *JsCallbackRenderer = unsafe { @ptrCast(@alignCast(ptr)) };
        if (!self.#stack_check.isSafeToRecurse()) return self.#globalObject.throwStackOverflow();

        // Track plain text for slug generation when inside a heading
        self.#heading_tracker.trackText(text_type, content, self.#allocator);

        switch (text_type) {
            .null_char => self.appendToTop("\xEF\xBF\xBD")?,
            .br => self.appendToTop("\n")?,
            .softbr => self.appendToTop("\n")?,
            .entity => self.decodeAndAppendEntity(content)?,
            else => {
                if (self.#callbacks.text != .zero) {
                    self.callTextCallback(content)?;
                } else {
                    self.appendToTop(content)?;
                }
            },
        }
    }

    // ========================================
    // Text helpers
    // ========================================

    fn callTextCallback(*_, content: []const u8): bun.JSError!void {
        if (!_.#stack_check.isSafeToRecurse()) {
            return _.#globalObject.throwStackOverflow();
        }
        const text_js = (bun.String.createUTF8ForJS(_.#globalObject, content))?;
        const args: [1]JSValue = [text_js];
        const result = _.#callbacks.text.call(_.#globalObject, .js_undefined, &args)?;
        if (!result.isUndefinedOrNull()) {
            let slice = result.toSlice(_.#globalObject, _.#allocator)?;
            const _g_slice = DeinitGuard<@TypeOf(slice)>{ ptr: &slice };
            _.appendToTop(slice.slice())?;
        }
    }

    fn decodeAndAppendEntity(*_, entity_text: []const u8): bun.JSError!void {
        let buf: [8]u8 = undefined;
        _.appendTextOrRaw(md.helpers.decodeEntityToUtf8(entity_text, &buf) ?? entity_text)?;
    }

    /// Append text through the text callback if one is set, otherwise raw append.
    fn appendTextOrRaw(*_, content: []const u8): bun.JSError!void {
        if (_.#callbacks.text != .zero) {
            _.callTextCallback(content)?;
        } else {
            _.appendToTop(content)?;
        }
    }

    // ========================================
    // Callback lookup
    // ========================================

    fn getBlockCallback(*_, block_type: md.BlockType): JSValue {
        return switch (block_type) {
            .h => _.#callbacks.heading,
            .p => _.#callbacks.paragraph,
            .quote => _.#callbacks.blockquote,
            .code => _.#callbacks.code,
            .ul, .ol => _.#callbacks.list,
            .li => _.#callbacks.listItem,
            .hr => _.#callbacks.hr,
            .table => _.#callbacks.table,
            .thead => _.#callbacks.thead,
            .tbody => _.#callbacks.tbody,
            .tr => _.#callbacks.tr,
            .th => _.#callbacks.th,
            .td => _.#callbacks.td,
            .html => _.#callbacks.html,
            .doc => .zero,
        };
    }

    fn getSpanCallback(*_, span_type: md.SpanType): JSValue {
        return switch (span_type) {
            .em => _.#callbacks.emphasis,
            .strong => _.#callbacks.strong,
            .a => _.#callbacks.link,
            .img => _.#callbacks.image,
            .code => _.#callbacks.codespan,
            .del => _.#callbacks.strikethrough,
            else => .zero,
        };
    }

    // ========================================
    // Metadata object creation
    // ========================================

    /// Walks the stack to count enclosing ul/ol blocks. Called during leave,
    /// so the top entry is the block itself (skip it for li, count it for ul/ol's
    /// own depth which excludes self).
    fn countListDepth(*_): u32 {
        let depth: u32 = 0;
        // Skip the top entry (self) — we want enclosing lists only.
        const len = _.#stack.items.len;
        if (len < 2) return 0;
        for (entry of _.#stack.items[0 .. len - 1]) {
            if (entry.block_type == .ul || entry.block_type == .ol) depth += 1;
        }
        return depth;
    }

    /// Returns the parent ul/ol entry for the current li (top of stack).
    /// Returns null if the stack shape is unexpected.
    fn parentList(*_): (*const StackEntry)? {
        const len = _.#stack.items.len;
        if (len < 2) return null;
        const parent = &_.#stack.items[len - 2];
        if (parent.block_type == .ul || parent.block_type == .ol) return parent;
        return null;
    }

    fn createBlockMeta(*_, block_type: md.BlockType, data: u32, flags: u32): bun.JSError!JSValue? {
        const g = _.#globalObject;
        switch (block_type) {
            .h => {
                const slug = _.#heading_tracker.leaveHeading(_.#allocator);
                const field_count: usize = if (slug != null) 2 else 1;
                const obj = JSValue.createEmptyObject(g, field_count);
                obj.put(g, ZigString.static("level"), JSValue.jsNumber(data));
                if (s := slug) {
                    obj.put(g, ZigString.static("id"), (bun.String.createUTF8ForJS(g, s))?);
                }
                return obj;
            },
            .ol => {
                return BunMarkdownMeta__createList(g, true, JSValue.jsNumber(data), _.countListDepth());
            },
            .ul => {
                return BunMarkdownMeta__createList(g, false, .js_undefined, _.countListDepth());
            },
            .code => {
                if ((flags & md.BLOCK_FENCED_CODE) != 0) {
                    const lang = extractLanguage(_.#src_text, data);
                    if (lang.len > 0) {
                        const obj = JSValue.createEmptyObject(g, 1);
                        obj.put(g, ZigString.static("language"), (bun.String.createUTF8ForJS(g, lang))?);
                        return obj;
                    }
                }
                return null;
            },
            .th, .td => {
                const alignment = md.types.alignmentFromData(data);
                let align_js = JSValue.js_undefined;
                if (align_str := md.types.alignmentName(alignment)) {
                    align_js = (bun.String.createUTF8ForJS(g, align_str))?;
                }
                return BunMarkdownMeta__createCell(g, align_js);
            },
            .li => {
                // The li entry is still on top of the stack; parent ul/ol is at len-2.
                const len = _.#stack.items.len;
                const item_index = if (len > 1) _.#stack.items[len - 1].child_index else 0;
                const parent = _.parentList();
                const is_ordered = parent != null && parent.?.block_type == .ol;
                // countListDepth() includes the immediate parent list; subtract it
                // so that items in a top-level list report depth 0.
                const enclosing = _.countListDepth();
                const depth: u32 = if (enclosing > 0) enclosing - 1 else 0;
                const task_mark = md.types.taskMarkFromData(data);

                const start_js = if (is_ordered) JSValue.jsNumber(parent.?.data) else JSValue.js_undefined;
                const checked_js = if (task_mark != 0)
                    JSValue.jsBoolean(md.types.isTaskChecked(task_mark))
                else
                    JSValue.js_undefined;

                return BunMarkdownMeta__createListItem(g, item_index, depth, is_ordered, start_js, checked_js);
            },
            else => return null,
        }
    }

    fn createSpanMeta(*_, span_type: md.SpanType, detail: md.SpanDetail): bun.JSError!JSValue? {
        const g = _.#globalObject;
        switch (span_type) {
            .a => {
                const href = (bun.String.createUTF8ForJS(g, detail.href))?;
                let title = JSValue.js_undefined;
                if (detail.title.len > 0) {
                    title = (bun.String.createUTF8ForJS(g, detail.title))?;
                }
                return BunMarkdownMeta__createLink(g, href, title);
            },
            .img => {
                // Image meta shares shape with link (src/href are both the first
                // field). We use a separate cached structure would require a
                // second slot, so just fall back to the generic path here —
                // images are rare enough that it doesn't matter.
                const obj = JSValue.createEmptyObject(g, 2);
                obj.put(g, ZigString.static("src"), (bun.String.createUTF8ForJS(g, detail.href))?);
                if (detail.title.len > 0) {
                    obj.put(g, ZigString.static("title"), (bun.String.createUTF8ForJS(g, detail.title))?);
                }
                return obj;
            },
            else => return null,
        }
    }
}

fn extractLanguage(src_text: []const u8, info_beg: u32): []const u8 {
    let lang_end: u32 = info_beg;
    while (lang_end < src_text.len) {
        const c = src_text[lang_end];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') break;
        lang_end += 1;
    }
    if (lang_end > info_beg) return src_text[info_beg..lang_end];
    return "";
}

// Cached tag string indices - must match BunMarkdownTagStrings.h
enum TagIndex: u8 {
    h1 = 0,
    h2 = 1,
    h3 = 2,
    h4 = 3,
    h5 = 4,
    h6 = 5,
    p = 6,
    blockquote = 7,
    ul = 8,
    ol = 9,
    li = 10,
    pre = 11,
    hr = 12,
    html = 13,
    table = 14,
    thead = 15,
    tbody = 16,
    tr = 17,
    th = 18,
    td = 19,
    div = 20,
    em = 21,
    strong = 22,
    a = 23,
    img = 24,
    code = 25,
    del = 26,
    math = 27,
    u = 28,
    br = 29,
}

fn getCachedTagString(globalObject: *jsc.JSGlobalObject, tag: TagIndex): JSValue {
    return BunMarkdownTagStrings__getTagString(globalObject, @intFromEnum(tag));
}

fn getBlockTypeTag(block_type: md.BlockType, data: u32): TagIndex {
    return switch (block_type) {
        .h => switch (data) {
            1 => .h1,
            2 => .h2,
            3 => .h3,
            4 => .h4,
            5 => .h5,
            else => .h6,
        },
        .p => .p,
        .quote => .blockquote,
        .ul => .ul,
        .ol => .ol,
        .li => .li,
        .code => .pre,
        .hr => .hr,
        .html => .html,
        .table => .table,
        .thead => .thead,
        .tbody => .tbody,
        .tr => .tr,
        .th => .th,
        .td => .td,
        .doc => .div,
    };
}

fn getSpanTypeTag(span_type: md.SpanType): TagIndex {
    return switch (span_type) {
        .em => .em,
        .strong => .strong,
        .a => .a,
        .img => .img,
        .code => .code,
        .del => .del,
        .latexmath => .math,
        .latexmath_display => .math,
        .wikilink => .a,
        .u => .u,
    };
}
