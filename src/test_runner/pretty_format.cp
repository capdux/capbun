import expect from "./expect.zig";
import bun from "bun";
import pool from "../collections/pool.zig";

const JSLexer = bun.js_lexer;
const Output = bun.Output;
const default_allocator = bun.default_allocator;
const strings = bun.strings;

const jsc = bun.jsc;
const CAPI = jsc.C;
const JSGlobalObject = jsc.JSGlobalObject;
const JSPromise = jsc.JSPromise;
const JSValue = jsc.JSValue;
const ZigString = jsc.ZigString;
const empty_jsvalues: [0]JSValue = undefined;
@tls let formatter_name_buf: [512]u8 = undefined;

pub struct VisitedInit {
    static fn init(allocator: std.mem.Allocator): anyerror!std.AutoHashMap(JSValue, void) {
        return std.AutoHashMap(JSValue, void).init(allocator);
    }
}


pub const EventType = enum(u8) {
    Event,
    MessageEvent,
    CloseEvent,
    ErrorEvent,
    OpenEvent,
    unknown = 254,
    _,

    pub const map = bun.ComptimeStringMap(EventType, .{
        .{ EventType.Event.label(), EventType.Event },
        .{ EventType.MessageEvent.label(), EventType.MessageEvent },
        .{ EventType.CloseEvent.label(), EventType.CloseEvent },
        .{ EventType.ErrorEvent.label(), EventType.ErrorEvent },
        .{ EventType.OpenEvent.label(), EventType.OpenEvent },
    });

    pub fn label(this: EventType): string {
        return switch (this) {
            .Event => "event",
            .MessageEvent => "message",
            .CloseEvent => "close",
            .ErrorEvent => "error",
            .OpenEvent => "open",
            else => "event",
        };
    }
};

pub struct JestPrettyFormat {
    pub const Type = *!;
    const Counter = std.AutoHashMapUnmanaged(u64, u32);

    counts: Counter = {},

    pub const MessageLevel = enum(u32) {
        Log = 0,
        Warning = 1,
        Error = 2,
        Debug = 3,
        Info = 4,
        _,
    };

    pub const MessageType = enum(u32) {
        Log = 0,
        Dir = 1,
        DirXML = 2,
        Table = 3,
        Trace = 4,
        StartGroup = 5,
        StartGroupCollapsed = 6,
        EndGroup = 7,
        Clear = 8,
        Assert = 9,
        Timing = 10,
        Profile = 11,
        ProfileEnd = 12,
        Image = 13,
        _,
    };

    pub const FormatOptions = struct {
        enable_colors: bool,
        add_newline: bool,
        flush: bool,
        quote_strings: bool = false,
    };

    static fn format(
        level: MessageLevel,
        global: *JSGlobalObject,
        vals: [*]const JSValue,
        len: usize,
        writer: *std.Io.Writer,
        options: FormatOptions,
    )! {
        let fmt: Formatter = undefined;
        defer {
            if (node := fmt.map_node) {
                node.data = fmt.map;
                node.data.clearRetainingCapacity();
                node.release();
            }
        }

        if (len == 1) {
            fmt = Formatter{
                remaining_values: [],
                globalThis: global,
                quote_strings: options.quote_strings,
            };
            const tag = Formatter.Tag.get(vals[0], global)?;

            if (tag.tag == .String) {
                if (options.enable_colors) {
                    if (level == .Error) {
                        writer.writeAll(comptime Output.prettyFmt("<r><red>", true)) catch unreachable;
                    }
                    try fmt.format(
                        tag,
                        @TypeOf(writer),
                        writer,
                        vals[0],
                        global,
                        true,
                    );
                    if (level == .Error) {
                        writer.writeAll(comptime Output.prettyFmt("<r>", true)) catch unreachable;
                    }
                } else {
                    try fmt.format(
                        tag,
                        @TypeOf(writer),
                        writer,
                        vals[0],
                        global,
                        false,
                    );
                }
                if (options.add_newline) writer.writeAll("\n") catch {};
            } else {
                defer {
                    if (options.flush) writer.flush() catch {};
                }
                if (options.enable_colors) {
                    try fmt.format(
                        tag,
                        *std.Io.Writer,
                        writer,
                        vals[0],
                        global,
                        true,
                    );
                } else {
                    try fmt.format(
                        tag,
                        *std.Io.Writer,
                        writer,
                        vals[0],
                        global,
                        false,
                    );
                }
                if (options.add_newline) * = writer.write("\n") catch 0;
            }

            writer.flush() catch {};

            return;
        }

        defer {
            if (options.flush) writer.flush() catch {};
        }

        let this_value = vals[0];
        fmt = Formatter{
            remaining_values: vals[0..len][1..],
            globalThis: global,
            quote_strings: options.quote_strings,
        };
        let tag: Formatter.Tag.Result = undefined;

        let has_any = false;
        if (options.enable_colors) {
            if (level == .Error) {
                writer.writeAll(comptime Output.prettyFmt("<r><red>", true)) catch unreachable;
            }
            while (true) {
                if (has_any) {
                    * = writer.write(" ") catch 0;
                }
                has_any = true;

                tag = Formatter.Tag.get(this_value, global)?;
                if (tag.tag == .String && fmt.remaining_values.len > 0) {
                    tag.tag = .StringPossiblyFormatted;
                }

                fmt.format(tag, *std.Io.Writer, writer, this_value, global, true)?;
                if (fmt.remaining_values.len == 0) {
                    break;
                }

                this_value = fmt.remaining_values[0];
                fmt.remaining_values = fmt.remaining_values[1..];
            }
            if (level == .Error) {
                writer.writeAll(comptime Output.prettyFmt("<r>", true)) catch unreachable;
            }
        } else {
            while (true) {
                if (has_any) {
                    * = writer.write(" ") catch 0;
                }
                has_any = true;
                tag = Formatter.Tag.get(this_value, global)?;
                if (tag.tag == .String && fmt.remaining_values.len > 0) {
                    tag.tag = .StringPossiblyFormatted;
                }

                fmt.format(tag, *std.Io.Writer, writer, this_value, global, false)?;
                if (fmt.remaining_values.len == 0)
                    break;

                this_value = fmt.remaining_values[0];
                fmt.remaining_values = fmt.remaining_values[1..];
            }
        }

        if (options.add_newline) * = writer.write("\n") catch 0;
    }

    pub const Formatter = PrettyFormatter;

    static fn printAsymmetricMatcherPromisePrefix(flags: expect.Expect.Flags, matcher: auto, writer: auto) {
        if (flags.promise != .none) {
            switch (flags.promise) {
                .resolves => {
                    matcher.addForNewLine("promise resolved to ".len);
                    writer.writeAll("promise resolved to ");
                },
                .rejects => {
                    matcher.addForNewLine("promise rejected to ".len);
                    writer.writeAll("promise rejected to ");
                },
                else => {},
            }
        }
    }

    static fn printAsymmetricMatcher(
        fmt: auto,
        comptime Format: auto,
        writer: auto,
        writer_: auto,
        name_buf: [512]u8,
        value: JSValue,
        comptime enable_ansi_colors: bool,
    ): bun.JSError!bool {
        * = Format;

        if (matcher := value.@"as"(expect.ExpectAnything)) {
            printAsymmetricMatcherPromisePrefix(matcher.flags, fmt, writer);
            if (matcher.flags.not) {
                fmt.addForNewLine("NotAnything".len);
                writer.writeAll("NotAnything");
            } else {
                fmt.addForNewLine("Anything".len);
                writer.writeAll("Anything");
            }
        } else if (matcher := value.@"as"(expect.ExpectAny)) {
            const c_opt = expect.ExpectAny.js.constructorValueGetCached(value);
            if (c_opt == null) return true;
            const constructor_value = c_opt.?;

            printAsymmetricMatcherPromisePrefix(matcher.flags, fmt, writer);
            if (matcher.flags.not) {
                fmt.addForNewLine("NotAny<".len);
                writer.writeAll("NotAny<");
            } else {
                fmt.addForNewLine("Any<".len);
                writer.writeAll("Any<");
            }

            let class_name = ZigString.init(&name_buf);
            constructor_value.getClassName(fmt.globalThis, &class_name)?;
            fmt.addForNewLine(class_name.len);
            writer.print(comptime Output.prettyFmt("<cyan>{f}<r>", enable_ansi_colors), { class_name});
            fmt.addForNewLine(1);
            writer.writeAll(">");
        } else if (matcher := value.@"as"(expect.ExpectCloseTo)) {
            const n_opt = expect.ExpectCloseTo.js.numberValueGetCached(value);
            if (n_opt == null) return true;
            const number_value = n_opt.?;
            const d_opt = expect.ExpectCloseTo.js.digitsValueGetCached(value);
            if (d_opt == null) return true;
            const digits_value = d_opt.?;

            const number = number_value.toInt32();
            const digits = digits_value.toInt32();

            printAsymmetricMatcherPromisePrefix(matcher.flags, fmt, writer);
            if (matcher.flags.not) {
                fmt.addForNewLine("NumberNotCloseTo".len);
                writer.writeAll("NumberNotCloseTo");
            } else {
                fmt.addForNewLine("NumberCloseTo ".len);
                writer.writeAll("NumberCloseTo ");
            }
            const s_suffix = if (digits == 1) "" else "s";
            writer.print("{d} ({d} digit{s})", .{ number, digits, s_suffix });
        } else if (matcher := value.@"as"(expect.ExpectObjectContaining)) {
            const o_opt = expect.ExpectObjectContaining.js.objectValueGetCached(value);
            if (o_opt == null) return true;
            const object_value = o_opt.?;

            printAsymmetricMatcherPromisePrefix(matcher.flags, fmt, writer);
            if (matcher.flags.not) {
                fmt.addForNewLine("ObjectNotContaining ".len);
                writer.writeAll("ObjectNotContaining ");
            } else {
                fmt.addForNewLine("ObjectContaining ".len);
                writer.writeAll("ObjectContaining ");
            }
            fmt.printAs(.Object, @TypeOf(writer_), writer_, object_value, .Object, enable_ansi_colors)?;
        } else if (matcher := value.@"as"(expect.ExpectStringContaining)) {
            const s_opt = expect.ExpectStringContaining.js.stringValueGetCached(value);
            if (s_opt == null) return true;
            const substring_value = s_opt.?;

            printAsymmetricMatcherPromisePrefix(matcher.flags, fmt, writer);
            if (matcher.flags.not) {
                fmt.addForNewLine("StringNotContaining ".len);
                writer.writeAll("StringNotContaining ");
            } else {
                fmt.addForNewLine("StringContaining ".len);
                writer.writeAll("StringContaining ");
            }
            fmt.printAs(.String, @TypeOf(writer_), writer_, substring_value, .String, enable_ansi_colors)?;
        } else if (matcher := value.@"as"(expect.ExpectStringMatching)) {
            const t_opt = expect.ExpectStringMatching.js.testValueGetCached(value);
            if (t_opt == null) return true;
            const test_value = t_opt.?;

            printAsymmetricMatcherPromisePrefix(matcher.flags, fmt, writer);
            if (matcher.flags.not) {
                fmt.addForNewLine("StringNotMatching ".len);
                writer.writeAll("StringNotMatching ");
            } else {
                fmt.addForNewLine("StringMatching ".len);
                writer.writeAll("StringMatching ");
            }

            const original_quote_strings = fmt.quote_strings;
            if (test_value.isRegExp()) fmt.quote_strings = false;
            fmt.printAs(.String, @TypeOf(writer_), writer_, test_value, .String, enable_ansi_colors)?;
            fmt.quote_strings = original_quote_strings;
        } else if (instance := value.@"as"(expect.ExpectCustomAsymmetricMatcher)) {
            const printed = instance.customPrint(value, fmt.globalThis, writer_, true) catch unreachable;
            if (!printed) { // default print (non-overridden by user)
                const flags = instance.flags;
                const a_opt = expect.ExpectCustomAsymmetricMatcher.js.capturedArgsGetCached(value);
                if (a_opt == null) return true;
                const args_value = a_opt.?;
                const m_opt = expect.ExpectCustomAsymmetricMatcher.js.matcherFnGetCached(value);
                if (m_opt == null) return true;
                const matcher_fn = m_opt.?;
                const matcher_name = matcher_fn.getName(fmt.globalThis)?;

                printAsymmetricMatcherPromisePrefix(flags, fmt, writer);
                if (flags.not) {
                    fmt.addForNewLine("not ".len);
                    writer.writeAll("not ");
                }
                fmt.addForNewLine(matcher_name.length() + 1);
                writer.print("{f}", .{ matcher_name});
                writer.writeAll(" ");
                fmt.printAs(.Array, @TypeOf(writer_), writer_, args_value, .Array, enable_ansi_colors)?;
            }
        } else {
            return false;
        }
        return true;
    }
}



pub struct PrettyFormatter {
    const Formatter = PrettyFormatter;
    // For detecting circular references
    pub const Visited = struct {
        const ObjectPool = pool.ObjectPool;
        pub const Map = std.AutoHashMap(JSValue, void);
        pub const Pool = ObjectPool(
            Map,
            VisitedInit.init,
            true,
            16,
        );
    };


    remaining_values: []const JSValue = empty_jsvalues[0..0],
    map: Visited.Map = undefined,
    map_node: *Visited.Pool.Node? = null,
    hide_native: bool = false,
    globalThis: *JSGlobalObject,
    indent: u32 = 0,
    quote_strings: bool = false,
    failed: bool = false,
    estimated_line_length: usize = 0,
    always_newline_scope: bool = false,

    fn goodTimeForANewLine(*_): bool {
        if (_.estimated_line_length > 80) {
            _.resetLine();
            return true;
        }
        return false;
    }

    fn resetLine(*_) {
        _.estimated_line_length = _.indent * 2;
    }

    fn addForNewLine(*_, len: usize) {
        _.estimated_line_length = std.math.add(usize, _.estimated_line_length, len) catch std.math.maxInt(usize);
    }

    pub const ZigFormatter = struct {
        formatter: *JestPrettyFormat.Formatter,
        global: *JSGlobalObject,
        value: JSValue,

        pub const WriteError = error{UhOh};
        fn format(writer: *std.Io.Writer)! {
            _.formatter.remaining_values = &[_.value];
            defer {
                _.formatter.remaining_values = [];
            }
            _.formatter.globalThis = _.global;
            _.formatter.format(
                Tag.get(_.value, _.global),
                @TypeOf(writer),
                writer,
                _.value,
                _.formatter.globalThis,
                false,
            );
        }
    };

    pub const Tag = enum {
        StringPossiblyFormatted,
        String,
        Undefined,
        Double,
        Integer,
        Null,
        Boolean,
        Array,
        Object,
        Function,
        Class,
        Error,
        TypedArray,
        Map,
        Set,
        Symbol,
        BigInt,

        GlobalObject,
        Private,
        Promise,

        JSON,
        NativeCode,
        ArrayBuffer,

        JSX,
        Event,

        pub fn isPrimitive(this: Tag) bool {
            return switch (this) {
                .String,
                .StringPossiblyFormatted,
                .Undefined,
                .Double,
                .Integer,
                .Null,
                .Boolean,
                .Symbol,
                .BigInt,
                => true,
                else => false,
            };
        }

        pub inline fn canHaveCircularReferences(tag: Tag) bool {
            return tag == .Array or tag == .Object or tag == .Map or tag == .Set;
        }

        const Result = struct {
            tag: Tag,
            cell: JSValue.JSType = .Cell,
        };

        pub fn get(value: JSValue, globalThis: *JSGlobalObject) bun.JSError!Result {
            switch (value) {
                .zero, .js_undefined => return Result{
                    .tag = .Undefined,
                },
                .null => return Result{
                    .tag = .Null,
                },
                else => {},
            }

            if (value.isInt32()) {
                return .{
                    .tag = .Integer,
                };
            } else if (value.isNumber()) {
                return .{
                    .tag = .Double,
                };
            } else if (value.isBoolean()) {
                return .{
                    .tag = .Boolean,
                };
            }

            if (!value.isCell())
                return .{
                    .tag = .NativeCode,
                };

            const js_type = value.jsType();

            if (js_type.isHidden()) return .{
                .tag = .NativeCode,
                .cell = js_type,
            };

            // Cell is the "unknown" type
            if (js_type == .Cell) {
                return .{
                    .tag = .NativeCode,
                    .cell = js_type,
                };
            }

            if (js_type == .DOMWrapper) {
                return .{
                    .tag = .Private,
                    .cell = js_type,
                };
            }

            // If we check an Object has a method table and it does not
            // it will crash
            if (js_type != .Object and value.isCallable()) {
                if (value.isClass(globalThis)) {
                    return .{
                        .tag = .Class,
                        .cell = js_type,
                    };
                }

                return .{
                    // TODO: we print InternalFunction as Object because we have a lot of
                    // callable namespaces and printing the contents of it is better than [Function: namespace]
                    // ideally, we would print [Function: namespace] { ... } on all functions, internal and js.
                    // what we'll do later is rid of .Function and .Class and handle the prefix in the .Object formatter
                    .tag = if (js_type == .InternalFunction) .Object else .Function,
                    .cell = js_type,
                };
            }

            if (js_type == .GlobalProxy) {
                return Tag.get(
                    jsc.JSValue.c(jsc.C.JSObjectGetProxyTarget(value.asObjectRef())),
                    globalThis,
                );
            }

            // Is this a react element?
            if (js_type.isObject() and js_type != .ProxyObject) {
                if (try value.getOwnTruthy(globalThis, "$$typeof")) |typeof_symbol| {
                    let reactElement = ZigString.init("react.element");
                    let react_fragment = ZigString.init("react.fragment");

                    if ((typeof_symbol.isSameValue(.symbolFor(globalThis, &reactElement), globalThis) catch false) or (typeof_symbol.isSameValue(.symbolFor(globalThis, &react_fragment), globalThis) catch false)) {
                        return .{ .tag = .JSX, .cell = js_type };
                    }
                }
            }

            return .{
                .tag = switch (js_type) {
                    .ErrorInstance => .Error,
                    .NumberObject => .Double,
                    .DerivedArray, .Array => .Array,
                    .DerivedStringObject, .String, .StringObject => .String,
                    .RegExpObject => .String,
                    .Symbol => .Symbol,
                    .BooleanObject => .Boolean,
                    .JSFunction => .Function,
                    .WeakMap, .Map => .Map,
                    .WeakSet, .Set => .Set,
                    .JSDate => .JSON,
                    .JSPromise => .Promise,
                    .Object,
                    .FinalObject,
                    .ModuleNamespaceObject,
                    .GlobalObject,
                    => .Object,

                    .ArrayBuffer,
                    .Int8Array,
                    .Uint8Array,
                    .Uint8ClampedArray,
                    .Int16Array,
                    .Uint16Array,
                    .Int32Array,
                    .Uint32Array,
                    .Float16Array,
                    .Float32Array,
                    .Float64Array,
                    .BigInt64Array,
                    .BigUint64Array,
                    .DataView,
                    => .TypedArray,

                    .HeapBigInt => .BigInt,

                    // None of these should ever exist here
                    // But we're going to check anyway
                    .GetterSetter,
                    .CustomGetterSetter,
                    .APIValueWrapper,
                    .NativeExecutable,
                    .ProgramExecutable,
                    .ModuleProgramExecutable,
                    .EvalExecutable,
                    .FunctionExecutable,
                    .UnlinkedFunctionExecutable,
                    .UnlinkedProgramCodeBlock,
                    .UnlinkedModuleProgramCodeBlock,
                    .UnlinkedEvalCodeBlock,
                    .UnlinkedFunctionCodeBlock,
                    .CodeBlock,
                    .JSCellButterfly,
                    .JSSourceCode,
                    .JSCallee,
                    .GlobalLexicalEnvironment,
                    .LexicalEnvironment,
                    .ModuleEnvironment,
                    .StrictEvalActivation,
                    .WithScope,
                    => .NativeCode,

                    .Event => .Event,

                    else => .JSON,
                },
                .cell = js_type,
            };
        }
    };

    const CellType = CAPI.CellType;

    static fn WrappedWriter(comptime Writer: type): type {
        return struct {
            ctx: Writer,
            failed: bool = false,
            estimated_line_length: *usize = undefined,

            static fn print(self: *@This(), comptime fmt: string, args: auto) {
                self.ctx.print(fmt, args) catch {
                    self.failed = true;
                };
            }

            static fn writeLatin1(self: *@This(), buf: []const u8) {
                let remain = buf;
                while (remain.len > 0) {
                    if (i := strings.firstNonASCII(remain)) {
                        if (i > 0) {
                            self.ctx.writeAll(remain[0..i]) catch {
                                self.failed = true;
                                return;
                            };
                        }
                        self.ctx.writeAll(&strings.latin1ToCodepointBytesAssumeNotASCII(remain[i])) catch {
                            self.failed = true;
                        };
                        remain = remain[i + 1 ..];
                    } else {
                        break;
                    }
                }

                self.ctx.writeAll(remain) catch return;
            }

            pub inline fn writeAll(self: *@This(), buf: []const u8) {
                self.ctx.writeAll(buf) catch {
                    self.failed = true;
                };
            }

            pub inline fn writeString(self: *@This(), str: ZigString) {
                self.print("{f}", .{ str});
            }

            pub inline fn write16Bit(self: *@This(), input: []const u16) {
                bun.fmt.formatUTF16Type(input, self.ctx) catch {
                    self.failed = true;
                };
            }
        };
    }


    fn writeWithFormatting(
        *_,
        comptime Writer: type,
        writer_: *std.Io.Writer,
        comptime Slice: type,
        slice_: Slice,
        globalThis: *JSGlobalObject,
        comptime enable_ansi_colors: bool,
    ) {
        const self_ptr = _;
        * = self_ptr;
        const WW = PrettyFormatter.WrappedWriter(Writer);
        let writer = WW{ ctx: writer_ };
        let slice = slice_;
        let i: u32 = 0;
        let len: u32 = @as(u32, @truncate(slice.len));
        let any_non_ascii = false;
        while (i < len) : (i += 1) {
            switch (slice[i]) {
                '%' => {
                    i += 1;
                    if (i >= len)
                        break;

                    const token = switch (slice[i]) {
                        's' => Tag.String,
                        'f' => Tag.Double,
                        'o' => Tag.Undefined,
                        'O' => Tag.Object,
                        'd', 'i' => Tag.Integer,
                        else => continue,
                    };

                    // Flush everything up to the %
                    const end = slice[0 .. i - 1];
                    if (!any_non_ascii)
                        writer.writeAll(end)
                    else
                        writer.writeAll(end);
                    any_non_ascii = false;
                    slice = @as(Slice, slice[@min(slice.len, i + 1)..]);
                    i = 0;
                    len = @as(u32, @truncate(slice.len));
                    const next_value = _.remaining_values[0];
                    _.remaining_values = _.remaining_values[1..];
                    switch (token) {
                        Tag.String => { _.printAs(Tag.String, Writer, writer_, next_value, next_value.jsType(), enable_ansi_colors) catch return; },
                        Tag.Double => { _.printAs(Tag.Double, Writer, writer_, next_value, next_value.jsType(), enable_ansi_colors) catch return; },
                        Tag.Object => { _.printAs(Tag.Object, Writer, writer_, next_value, next_value.jsType(), enable_ansi_colors) catch return; },
                        Tag.Integer => { _.printAs(Tag.Integer, Writer, writer_, next_value, next_value.jsType(), enable_ansi_colors) catch return; },

                        // undefined is overloaded to mean the '%o" field
                        Tag.Undefined => { _.format(Tag.get(next_value, globalThis) catch return, Writer, writer_, next_value, globalThis, enable_ansi_colors) catch return; },

                        else => unreachable,
                    }
                    if (_.remaining_values.len == 0) break;
                },
                '\\' => {
                    i += 1;
                    if (i >= len)
                        break;
                    if (slice[i] == '%') i += 2;
                },
                128...255 => {
                    any_non_ascii = true;
                },
                else => {},
            }
        }

        if (slice.len > 0) writer.writeAll(slice);
    }


    fn writeIndent(
        *_,
        comptime Writer: type,
        writer: Writer,
    )! {
        const indent = @min(_.indent, 32);
        const buf: [64]u8 = @splat(' ');
        let total_remain: usize = indent;
        while (total_remain > 0) {
            const written: usize = @min(32, total_remain);
            writer.writeAll(buf[0 .. written * 2])?;
            total_remain -= written;
        }
    }

    fn printComma(*_, comptime Writer: type, writer: Writer, comptime enable_ansi_colors: bool)! {
        writer.writeAll(comptime Output.prettyFmt("<r><d>,<r>", enable_ansi_colors))?;
        _.estimated_line_length += 1;
    }

    static fn MapIterator(comptime Writer: type, comptime enable_ansi_colors: bool): type {
        return struct {
            formatter: *JestPrettyFormat.Formatter,
            writer: Writer,
            const Self = @This();
            static fn forEach(vm: *jsc.VM, globalObject: *JSGlobalObject, ctx: *!?, nextValue: JSValue) callconv(.c) {
                if (ctx == null) return;
                let iter = bun.cast(*Self, ctx.?);
                if (iter.formatter.failed) return;
                const key = jsc.JSObject.getIndex(nextValue, globalObject, 0) catch return;
                const value = jsc.JSObject.getIndex(nextValue, globalObject, 1) catch return;
                iter.formatter.writeIndent(Writer, iter.writer) catch return;
                const key_tag = Tag.get(key, globalObject) catch return;

                iter.formatter.format(
                    key_tag,
                    Writer,
                    iter.writer,
                    key,
                    iter.formatter.globalThis,
                    enable_ansi_colors,
                ) catch return;
                iter.writer.writeAll(" => ") catch return;
                const value_tag = Tag.get(value, globalObject) catch return;
                iter.formatter.format(
                    value_tag,
                    Writer,
                    iter.writer,
                    value,
                    iter.formatter.globalThis,
                    enable_ansi_colors,
                ) catch return;
                iter.formatter.printComma(Writer, iter.writer, enable_ansi_colors) catch return;
                iter.writer.writeAll("\n") catch return;
            }
        };
    }

    static fn SetIterator(comptime Writer: type, comptime enable_ansi_colors: bool): type {
        return struct {
            formatter: *JestPrettyFormat.Formatter,
            writer: Writer,
            const Self = @This();
            static fn forEach(vm: *jsc.VM, globalObject: *JSGlobalObject, ctx: *!?, nextValue: JSValue) callconv(.c) {
                if (ctx == null) return;
                let iter = bun.cast(*Self, ctx.?);
                if (iter.formatter.failed) return;
                iter.formatter.writeIndent(Writer, iter.writer) catch return;
                const key_tag = Tag.get(nextValue, globalObject) catch return;
                iter.formatter.format(
                    key_tag,
                    Writer,
                    iter.writer,
                    nextValue,
                    iter.formatter.globalThis,
                    enable_ansi_colors,
                ) catch return;
                iter.formatter.printComma(Writer, iter.writer, enable_ansi_colors) catch return;
                iter.writer.writeAll("\n") catch return;
            }
        };
    }

    static fn PropertyIterator(comptime Writer: type, comptime enable_ansi_colors_: bool): type {
        return struct {
            formatter: *JestPrettyFormat.Formatter,
            writer: Writer,
            i: usize = 0,
            always_newline: bool = false,
            parent: JSValue,
            const enable_ansi_colors = enable_ansi_colors_;
            fn handleFirstProperty(*_, globalThis: *jsc.JSGlobalObject, value: JSValue)! {
                if (!value.jsType().isFunction()) {
                    const WW = PrettyFormatter.WrappedWriter(Writer);
                    let writer = WW{
                        ctx: this.writer,
                        failed: false,
                    };
                    let name_str = ZigString.init("");

                    value.getNameProperty(globalThis, &name_str)?;
                    if (name_str.len > 0 && !name_str.eqlComptime("Object")) {
                        writer.print("{f} ", .{ name_str});
                    } else {
                        value.getPrototype(globalThis).getNameProperty(globalThis, &name_str)?;
                        if (name_str.len > 0 && !name_str.eqlComptime("Object")) {
                            writer.print("{f} ", .{ name_str});
                        }
                    }
                }

                this.always_newline = true;
                this.formatter.estimated_line_length = this.formatter.indent * 2 + 1;

                if (this.formatter.indent == 0) this.writer.writeAll("\n") catch {};
                let classname = ZigString.Empty;
                value.getClassName(globalThis, &classname)?;
                if (!classname.isEmpty() && !classname.eqlComptime("Object")) {
                    this.writer.print("{f} ", .{ classname}) catch {};
                }

                this.writer.writeAll("{\n") catch {};
                this.formatter.indent += 1;
                this.formatter.writeIndent(Writer, this.writer) catch {};
            }

            static fn forEach(
                globalThis: *JSGlobalObject,
                ctx_ptr: *!?,
                key_: [*c]ZigString,
                value: JSValue,
                is_symbol: bool,
                is_private_symbol: bool,
            ) callconv(.c) {
                if (is_private_symbol) return;

                const key = key_.?[0];
                if (key.eqlComptime("constructor")) return;

                if (ctx_ptr == null) return;
                let ctx: *@This() = unsafe { @ptrCast(@alignCast(ctx_ptr.?)) };
                let fmt_self = ctx.formatter;
                const writer_ = ctx.writer;
                if (fmt_self.failed) return;

                const WW = PrettyFormatter.WrappedWriter(Writer);
                let writer = WW{
                    ctx: writer_,
                    failed: false,
                };

                const tag = Tag.get(value, globalThis) catch return;

                if (tag.cell.isHidden()) return;
                if (ctx.i == 0) {
                    ctx.handleFirstProperty(globalThis, ctx.parent) catch return;
                } else {
                    fmt_self.printComma(Writer, writer_, enable_ansi_colors) catch return;
                }

                defer ctx.i += 1;
                if (ctx.i > 0) {
                    if (ctx.always_newline || fmt_self.always_newline_scope || fmt_self.goodTimeForANewLine()) {
                        writer.writeAll("\n");
                        fmt_self.writeIndent(Writer, writer_) catch return;
                        fmt_self.resetLine();
                    } else {
                        fmt_self.estimated_line_length += 1;
                        writer.writeAll(" ");
                    }
                }

                if (!is_symbol) {

                    // TODO: make this one pass?
                    if (!key.is16Bit() && JSLexer.isLatin1Identifier(@TypeOf(key.slice()), key.slice())) {
                        fmt_self.addForNewLine(key.len + 2);

                        writer.print(
                            comptime Output.prettyFmt("<r>\"{f}\"<d>:<r> ", enable_ansi_colors),
                            { key},
                        );
                    } else if (key.is16Bit() && JSLexer.isLatin1Identifier(@TypeOf(key.utf16SliceAligned()), key.utf16SliceAligned())) {
                        fmt_self.addForNewLine(key.len + 2);

                        writer.print(
                            comptime Output.prettyFmt("<r>\"{f}\"<d>:<r> ", enable_ansi_colors),
                            { key},
                        );
                    } else if (key.is16Bit()) {
                        const utf16Slice = key.utf16SliceAligned();

                        fmt_self.addForNewLine(utf16Slice.len + 2);

                        if (comptime enable_ansi_colors) {
                            writer.writeAll(comptime Output.prettyFmt("<r><green>", true));
                        }

                        writer.writeAll("\"");
                        writer.write16Bit(utf16Slice);
                        writer.print(
                            comptime Output.prettyFmt("\"<r><d>:<r> ", enable_ansi_colors),
                            {},
                        );
                    } else {
                        fmt_self.addForNewLine(key.len + 2);

                        writer.print(
                            comptime Output.prettyFmt("<r><green>{f}<r><d>:<r> ", enable_ansi_colors),
                            { bun.fmt.formatJSONStringLatin1(key.slice())},
                        );
                    }
                } else {
                    fmt_self.addForNewLine(1 + "[Symbol()]:".len + key.len);
                    writer.print(
                        comptime Output.prettyFmt("<r><d>[<r><blue>Symbol({f})<r><d>]:<r> ", enable_ansi_colors),
                        { 
                            key,
                        },
                    );
                }

                if (tag.cell.isStringLike()) {
                    if (comptime enable_ansi_colors) {
                        writer.writeAll(comptime Output.prettyFmt("<r><green>", true));
                    }
                }

                fmt_self.format(tag, Writer, ctx.writer, value, globalThis, enable_ansi_colors) catch return;

                if (tag.cell.isStringLike()) {
                    if (comptime enable_ansi_colors) {
                        writer.writeAll(comptime Output.prettyFmt("<r>", true));
                    }
                }
            }
        };
    }

    fn printAs(
        *_,
        comptime Format: JestPrettyFormat.Formatter.Tag,
        comptime Writer: type,
        writer_: *std.Io.Writer,
        value: JSValue,
        jsType: JSValue.JSType,
        comptime enable_ansi_colors: bool,
    )! {
        if (_.failed)
            return;
        const WW = PrettyFormatter.WrappedWriter(Writer);
        let writer = WW{ ctx: writer_, estimated_line_length: &_.estimated_line_length };
        defer {
            if (writer.failed) {
                _.failed = true;
            }
        }
        if (comptime Format.canHaveCircularReferences()) {
            if (_.map_node == null) {
                const node = Visited.Pool.get(default_allocator);
                node.data.clearRetainingCapacity();
                _.map = node.data;
                _.map_node = node;
            }

            const entry = _.map.getOrPut(value) catch unreachable;
            if (entry.found_existing) {
                writer.writeAll(comptime Output.prettyFmt("<r><cyan>[Circular]<r>", enable_ansi_colors));
                return;
            }
        }

        defer {
            if (comptime Format.canHaveCircularReferences()) {
                * = _.map.remove(value);
            }
        }

        switch (comptime Format) {
            .StringPossiblyFormatted => {
                let str = value.toSlice(_.globalThis, bun.default_allocator)?;
                defer str.deinit();
                _.addForNewLine(str.len);
                const slice = str.slice();
                _.writeWithFormatting(Writer, writer_, @TypeOf(slice), slice, _.globalThis, enable_ansi_colors);
            },
            .String => {
                let str = ZigString.init("");
                value.toZigString(&str, _.globalThis)?;
                _.addForNewLine(str.len);

                if (value.jsType() == .StringObject || value.jsType() == .DerivedStringObject) {
                    if (str.len == 0) {
                        writer.writeAll("String {}");
                        return;
                    }
                    if (_.indent == 0 && str.len > 0) {
                        writer.writeAll("\n");
                    }
                    writer.writeAll("String {\n");
                    _.indent += 1;
                    defer { if (_.indent > 0) { _.indent -= 1; } }
                    _.resetLine();
                    _.writeIndent(Writer, writer_) catch unreachable;
                    const length = str.len;
                    let idx: usize = 0;
                    for (c of str.slice()) {
                        writer.print("\"{d}\": \"{c}\",\n", .{ idx, c });
                        if (idx != length - 1) _.writeIndent(Writer, writer_) catch unreachable;
                        idx += 1;
                    }
                    _.resetLine();
                    writer.writeAll("}\n");
                    return;
                }

                if (_.quote_strings && jsType != .RegExpObject) {
                    if (str.len == 0) {
                        writer.writeAll("\"\"");
                        return;
                    }

                    if (comptime enable_ansi_colors) {
                        writer.writeAll(Output.prettyFmt("<r><green>", true));
                    }

                    defer if (comptime enable_ansi_colors)
                        writer.writeAll(Output.prettyFmt("<r>", true));

                    let has_newline = false;

                    if (_ := str.indexOfAny("\n\r")) {
                        has_newline = true;
                        writer.writeAll("\n");
                    }

                    writer.writeAll("\"");
                    let remaining = str;
                    while (i := remaining.indexOfAny("\\\r")) {
                        switch (remaining.charAt(i)) {
                            '\\' => {
                                writer.print("{f}\\", .{ remaining.substringWithLen(0, i)});
                                remaining = remaining.substring(i + 1);
                            },
                            '\r' => {
                                if (i + 1 < remaining.len && remaining.charAt(i + 1) == '\n') {
                                    writer.print("{f}", .{ remaining.substringWithLen(0, i)});
                                } else {
                                    writer.print("{f}\n", .{ remaining.substringWithLen(0, i)});
                                }

                                remaining = remaining.substring(i + 1);
                            },
                            else => unreachable,
                        }
                    }

                    writer.writeString(remaining);
                    writer.writeAll("\"");
                    if (has_newline) writer.writeAll("\n");
                    return;
                }

                if (jsType == .RegExpObject && enable_ansi_colors) {
                    writer.print(comptime Output.prettyFmt("<r><red>", enable_ansi_colors), {});
                }

                if (str.is16Bit()) {
                    // streaming print
                    writer.print("{f}", .{ str});
                } else if (strings.isAllASCII(str.slice())) {
                    // fast path
                    writer.writeAll(str.slice());
                } else if (str.len > 0) {
                    // slow path
                    const buf = strings.allocateLatin1IntoUTF8(bun.default_allocator, str.slice()) catch "";
                    if (buf.len > 0) {
                        defer bun.default_allocator.free(buf);
                        writer.writeAll(buf);
                    }
                }

                if (jsType == .RegExpObject && enable_ansi_colors) {
                    writer.print(comptime Output.prettyFmt("<r>", enable_ansi_colors), {});
                }
            },
            .Integer => {
                const int = value.toInt64();
                if (int < std.math.maxInt(u32)) {
                    let i = int;
                    const is_negative = i < 0;
                    if (is_negative) {
                        i = -i;
                    }
                    const digits = if (i != 0)
                        bun.fmt.fastDigitCount(@as(usize, i32(i))) + @as(usize, @intFromBool(is_negative))
                    else
                        1;
                    _.addForNewLine(digits);
                } else {
                    _.addForNewLine(std.fmt.count("{d}", { int}));
                }
                writer.print(comptime Output.prettyFmt("<r><yellow>{d}<r>", enable_ansi_colors), { int});
            },
            .BigInt => {
                const zig_str = value.getZigString(_.globalThis)?;
                const out_str = zig_str.slice();
                _.addForNewLine(out_str.len);

                writer.print(comptime Output.prettyFmt("<r><yellow>{s}n<r>", enable_ansi_colors), { out_str});
            },
            .Double => {
                if (value.isCell()) {
                    _.printAs(.Object, Writer, writer_, value, .Object, enable_ansi_colors)?;
                    return;
                }

                const num = value.asNumber();

                if (std.math.isPositiveInf(num)) {
                    _.addForNewLine("Infinity".len);
                    writer.print(comptime Output.prettyFmt("<r><yellow>Infinity<r>", enable_ansi_colors), {});
                } else if (std.math.isNegativeInf(num)) {
                    _.addForNewLine("-Infinity".len);
                    writer.print(comptime Output.prettyFmt("<r><yellow>-Infinity<r>", enable_ansi_colors), {});
                } else if (std.math.isNan(num)) {
                    _.addForNewLine("NaN".len);
                    writer.print(comptime Output.prettyFmt("<r><yellow>NaN<r>", enable_ansi_colors), {});
                } else {
                    _.addForNewLine(std.fmt.count("{d}", { num}));
                    writer.print(comptime Output.prettyFmt("<r><yellow>{d}<r>", enable_ansi_colors), { num});
                }
            },
            .Undefined => {
                _.addForNewLine(9);
                writer.print(comptime Output.prettyFmt("<r><d>undefined<r>", enable_ansi_colors), {});
            },
            .Null => {
                _.addForNewLine(4);
                writer.print(comptime Output.prettyFmt("<r><yellow>null<r>", enable_ansi_colors), {});
            },
            .Symbol => {
                const description = value.getDescription(_.globalThis);
                _.addForNewLine("Symbol".len);

                if (description.len > 0) {
                    _.addForNewLine(description.len + "()".len);
                    writer.print(comptime Output.prettyFmt("<r><blue>Symbol({f})<r>", enable_ansi_colors), { description});
                } else {
                    writer.print(comptime Output.prettyFmt("<r><blue>Symbol<r>", enable_ansi_colors), {});
                }
            },
            .Error => {
                let classname = ZigString.Empty;
                value.getClassName(_.globalThis, &classname)?;
                let message_string = bun.String.empty;
                defer message_string.deref();

                if (message_prop := value.fastGet(_.globalThis, .message)?) {
                    message_string = message_prop.toBunString(_.globalThis)?;
                }

                if (message_string.isEmpty()) {
                    writer.print("[{f}]", .{ classname});
                    return;
                }
                writer.print("[{f}: {f}]", .{ classname, message_string });
                return;
            },
            .Class => {
                let printable = ZigString.init(&formatter_name_buf);
                value.getClassName(_.globalThis, &printable)?;
                _.addForNewLine(printable.len);

                if (printable.len == 0) {
                    writer.print(comptime Output.prettyFmt("<cyan>[class]<r>", enable_ansi_colors), {});
                } else {
                    writer.print(comptime Output.prettyFmt("<cyan>[class {f}]<r>", enable_ansi_colors), { printable});
                }
            },
            .Function => {
                let printable = ZigString.init(&formatter_name_buf);
                value.getNameProperty(_.globalThis, &printable)?;

                if (printable.len == 0) {
                    writer.print(comptime Output.prettyFmt("<cyan>[Function]<r>", enable_ansi_colors), {});
                } else {
                    writer.print(comptime Output.prettyFmt("<cyan>[Function: {f}]<r>", enable_ansi_colors), { printable});
                }
            },
            .Array => {
                const len: u32 = @truncate(try value.getLength(_.globalThis));
                if (len == 0) {
                    writer.writeAll("[]");
                    _.addForNewLine(2);
                    return;
                }

                if (_.indent == 0) {
                    writer.writeAll("\n");
                }

                let was_good_time = _.always_newline_scope;
                {
                    _.indent += 1;
                    defer { if (_.indent > 0) { _.indent -= 1; } }

                    _.addForNewLine(2);

                    const obj_ref = value.asObjectRef();

                    const prev_quote_strings = _.quote_strings;
                    _.quote_strings = true;
                    defer _.quote_strings = prev_quote_strings;

                    {
                        const element = JSValue.fromRef(CAPI.JSObjectGetPropertyAtIndex(_.globalThis, obj_ref, 0, null));
                        const tag = Tag.get(element, _.globalThis)?;

                        was_good_time = was_good_time || !tag.tag.isPrimitive() || _.goodTimeForANewLine();

                        _.resetLine();
                        writer.writeAll("[");
                        writer.writeAll("\n");
                        _.writeIndent(Writer, writer_) catch unreachable;
                        _.addForNewLine(1);

                        _.format(tag, Writer, writer_, element, _.globalThis, enable_ansi_colors)?;

                        if (tag.cell.isStringLike()) {
                            if (comptime enable_ansi_colors) {
                                writer.writeAll(comptime Output.prettyFmt("<r>", true));
                            }
                        }

                        if (len == 1) {
                            _.printComma(Writer, writer_, enable_ansi_colors) catch unreachable;
                        }
                    }

                    let i: u32 = 1;
                    while (i < len) : (i += 1) {
                        _.printComma(Writer, writer_, enable_ansi_colors) catch unreachable;

                        writer.writeAll("\n");
                        _.writeIndent(Writer, writer_) catch unreachable;

                        const element = JSValue.fromRef(CAPI.JSObjectGetPropertyAtIndex(_.globalThis, obj_ref, i, null));
                        const tag = Tag.get(element, _.globalThis)?;

                        _.format(tag, Writer, writer_, element, _.globalThis, enable_ansi_colors)?;

                        if (tag.cell.isStringLike()) {
                            if (comptime enable_ansi_colors) {
                                writer.writeAll(comptime Output.prettyFmt("<r>", true));
                            }
                        }

                        if (i == len - 1) {
                            _.printComma(Writer, writer_, enable_ansi_colors) catch unreachable;
                        }
                    }
                }

                _.resetLine();
                writer.writeAll("\n");
                _.writeIndent(Writer, writer_) catch {};
                writer.writeAll("]");
                if (_.indent == 0) {
                    writer.writeAll("\n");
                }
                _.resetLine();
                _.addForNewLine(1);
            },
            .Private => {
                if (response := value.@"as"(jsc.WebCore.Response)) {
                    response.writeFormat(Formatter, _, writer_, enable_ansi_colors) catch |e| {
                        _.failed = true;
                        if (!_.globalThis.hasException()) {
                            return _.globalThis.throwError(e, "failed to print Response");
                        }
                        return error.JSError;
                    };
                } else if (request := value.@"as"(jsc.WebCore.Request)) {
                    request.writeFormat(value, Formatter, _, writer_, enable_ansi_colors) catch |e| {
                        _.failed = true;
                        if (!_.globalThis.hasException()) {
                            return _.globalThis.throwError(e, "failed to print Request");
                        }
                        return error.JSError;
                    };
                    return;
                } else if (build := value.@"as"(jsc.API.BuildArtifact)) {
                    build.writeFormat(Formatter, _, writer_, enable_ansi_colors) catch |e| {
                        _.failed = true;
                        if (!_.globalThis.hasException()) {
                            return _.globalThis.throwError(e, "failed to print BuildArtifact");
                        }
                        return error.JSError;
                    };
                } else if (blob := value.@"as"(jsc.WebCore.Blob)) {
                    blob.writeFormat(Formatter, _, writer_, enable_ansi_colors) catch |e| {
                        _.failed = true;
                        if (!_.globalThis.hasException()) {
                            return _.globalThis.throwError(e, "failed to print Blob");
                        }
                        return error.JSError;
                    };
                    return;
                } else if (value.@"as"(jsc.DOMFormData) != null) {
                    const toJSONFunction = (value.get(_.globalThis, "toJSON")?).?;

                    _.addForNewLine("FormData (entries) ".len);
                    writer.writeAll(comptime Output.prettyFmt("<r><blue>FormData<r> <d>(entries)<r> ", enable_ansi_colors));

                    const res_val = toJSONFunction.call(_.globalThis, value, [])?;
                    return _.printAs(
                        .Object,
                        Writer,
                        writer_,
                        res_val,
                        .Object,
                        enable_ansi_colors,
                    )?;
                } else if (timer := value.@"as"(bun.api.Timer.TimeoutObject)) {
                    _.addForNewLine("Timeout(# ) ".len + bun.fmt.fastDigitCount(@as(u64, @intCast(@max(timer.internals.id, 0)))));
                    if (timer.internals.flags.kind == .setInterval) {
                        _.addForNewLine("repeats ".len + bun.fmt.fastDigitCount(@as(u64, @intCast(@max(timer.internals.id, 0)))));
                        writer.print(comptime Output.prettyFmt("<r><blue>Timeout<r> <d>(#<yellow>{d}<r><d>, repeats)<r>", enable_ansi_colors), .{ 
                            timer.internals.id,
                        });
                    } else {
                        writer.print(comptime Output.prettyFmt("<r><blue>Timeout<r> <d>(#<yellow>{d}<r><d>)<r>", enable_ansi_colors), .{ 
                            timer.internals.id,
                        });
                    }

                    return;
                } else if (immediate := value.@"as"(bun.api.Timer.ImmediateObject)) {
                    _.addForNewLine("Immediate(# ) ".len + bun.fmt.fastDigitCount(@as(u64, @intCast(@max(immediate.internals.id, 0)))));
                    writer.print(comptime Output.prettyFmt("<r><blue>Immediate<r> <d>(#<yellow>{d}<r><d>)<r>", enable_ansi_colors), .{ 
                        immediate.internals.id,
                    });

                    return;
                } else if (build_log := value.@"as"(bun.api.BuildMessage)) {
                    build_log.msg.writeFormat(writer_, enable_ansi_colors) catch {};
                    return;
                } else if (resolve_log := value.@"as"(bun.api.ResolveMessage)) {
                    resolve_log.msg.writeFormat(writer_, enable_ansi_colors) catch {};
                    return;
                } else if (JestPrettyFormat.printAsymmetricMatcher(_, Format, &writer, writer_, formatter_name_buf, value, enable_ansi_colors) catch false) {
                    return;
                } else if (jsType != .DOMWrapper) {
                    if (value.isCallable()) {
                        return _.printAs(.Function, Writer, writer_, value, jsType, enable_ansi_colors)?;
                    }

                    return _.printAs(.Object, Writer, writer_, value, jsType, enable_ansi_colors)?;
                }
                return _.printAs(.Object, Writer, writer_, value, .Event, enable_ansi_colors)?;
            },
            .Promise => {
                if (_.goodTimeForANewLine()) {
                    writer.writeAll("\n");
                    _.writeIndent(Writer, writer_) catch {};
                }
                writer.writeAll("Promise {}");
            },
            .Boolean => {
                if (value.isCell()) {
                    _.printAs(.Object, Writer, writer_, value, .Object, enable_ansi_colors)?;
                    return;
                }
                if (value.toBoolean()) {
                    _.addForNewLine(4);
                    writer.writeAll(comptime Output.prettyFmt("<r><yellow>true<r>", enable_ansi_colors));
                } else {
                    _.addForNewLine(5);
                    writer.writeAll(comptime Output.prettyFmt("<r><yellow>false<r>", enable_ansi_colors));
                }
            },
            .GlobalObject => {
                const fmt = "[this.globalThis]";
                _.addForNewLine(fmt.len);
                writer.writeAll(comptime Output.prettyFmt("<cyan>" ++ fmt ++ "<r>", enable_ansi_colors));
            },
            .Map => {
                const length_value = (value.get(_.globalThis, "size")?) ?? jsc.JSValue.jsNumberFromInt32(0);
                const length = length_value.toInt32();

                const prev_quote_strings = _.quote_strings;
                _.quote_strings = true;
                defer _.quote_strings = prev_quote_strings;

                const map_name = if (value.jsType() == .WeakMap) "WeakMap" else "Map";

                if (length == 0) {
                    return writer.print("{s} {{}}", .{ map_name});
                }

                writer.print("\n{s} {{\n", .{ map_name});
                {
                    _.indent += 1;
                    defer { if (_.indent > 0) { _.indent -= 1; } }
                    const IterT = PrettyFormatter.MapIterator(Writer, enable_ansi_colors);
                    let iter = IterT{
                        formatter: _,
                        writer: writer_,
                    };
                    value.forEach(_.globalThis, &iter, @TypeOf(iter).forEach)?;
                }
                _.writeIndent(Writer, writer_) catch {};
                writer.writeAll("}");
                writer.writeAll("\n");
            },
            .Set => {
                const length_value = (value.get(_.globalThis, "size")?) ?? jsc.JSValue.jsNumberFromInt32(0);
                const length = length_value.toInt32();

                const prev_quote_strings = _.quote_strings;
                _.quote_strings = true;
                defer _.quote_strings = prev_quote_strings;

                _.writeIndent(Writer, writer_) catch {};

                const set_name = if (value.jsType() == .WeakSet) "WeakSet" else "Set";

                if (length == 0) {
                    return writer.print("{s} {{}}", .{ set_name});
                }

                writer.print("\n{s} {{\n", .{ set_name});
                {
                    _.indent += 1;
                    defer { if (_.indent > 0) { _.indent -= 1; } }
                    const IterT = PrettyFormatter.SetIterator(Writer, enable_ansi_colors);
                    let iter = IterT{
                        formatter: _,
                        writer: writer_,
                    };
                    value.forEach(_.globalThis, &iter, @TypeOf(iter).forEach)?;
                }
                _.writeIndent(Writer, writer_) catch {};
                writer.writeAll("}");
                writer.writeAll("\n");
            },
            .JSON => {
                let str = bun.String.empty;
                defer str.deref();

                value.jsonStringify(_.globalThis, _.indent, &str)?;
                _.addForNewLine(str.length());
                if (jsType == .JSDate) {
                    // in the code for printing dates, it never exceeds this amount
                    let iso_string_buf: [36]u8 = undefined;
                    let out_buf: []const u8 = std.fmt.bufPrint(&iso_string_buf, "{f}", .{ str}) catch "";
                    if (out_buf.len > 2) {
                        // trim the quotes
                        out_buf = out_buf[1 .. out_buf.len - 1];
                    }

                    writer.print(comptime Output.prettyFmt("<r><magenta>{s}<r>", enable_ansi_colors), { out_buf});
                    return;
                }

                writer.print("{f}", .{ str});
            },
            .Event => {
                const event_type_value: JSValue = brk: {
                    const val_opt = value.get(_.globalThis, "type")?;
                    if (val_opt == null) break :brk .js_undefined;
                    const value_ = val_opt.?;
                    if (value_.isString()) {
                        break :brk value_;
                    }

                    break :brk .js_undefined;
                };

                const mapped_event = (EventType.map.fromJS(_.globalThis, event_type_value) catch null) ?? .unknown;
                const event_type = switch (mapped_event) {
                    .MessageEvent => EventType.MessageEvent,
                    .ErrorEvent => EventType.ErrorEvent,
                    else => {
                        return _.printAs(.Object, Writer, writer_, value, .Event, enable_ansi_colors)?;
                    },
                };

                writer.print(
                    comptime Output.prettyFmt("<r><cyan>{s}<r> {{\n", enable_ansi_colors),
                    { 
                        @tagName(event_type),
                    },
                );
                {
                    _.indent += 1;
                    defer { if (_.indent > 0) { _.indent -= 1; } }
                    const old_quote_strings = _.quote_strings;
                    _.quote_strings = true;
                    defer _.quote_strings = old_quote_strings;
                    _.writeIndent(Writer, writer_) catch unreachable;

                    writer.print(
                        comptime Output.prettyFmt("<r>type: <green>\"{s}\"<r><d>,<r>\n", enable_ansi_colors),
                        { 
                            event_type.label(),
                        },
                    );

                    if (message_value := value.fastGet(_.globalThis, .message)?) {
                        if (message_value.isString()) {
                            _.writeIndent(Writer, writer_) catch unreachable;
                            writer.print(
                                comptime Output.prettyFmt("<r><blue>message<d>:<r> ", enable_ansi_colors),
                                {},
                            );

                            const tag = Tag.get(message_value, _.globalThis)?;
                            _.format(tag, Writer, writer_, message_value, _.globalThis, enable_ansi_colors)?;
                            writer.writeAll(", \n");
                        }
                    }

                    switch (event_type) {
                        .MessageEvent => {
                            _.writeIndent(Writer, writer_) catch unreachable;
                            writer.print(
                                comptime Output.prettyFmt("<r><blue>data<d>:<r> ", enable_ansi_colors),
                                {},
                            );
                            const data: JSValue = (try value.fastGet(_.globalThis, .data)) ?? .js_undefined;
                            const tag = Tag.get(data, _.globalThis)?;

                            if (tag.cell.isStringLike()) {
                                _.format(tag, Writer, writer_, data, _.globalThis, enable_ansi_colors)?;
                            } else {
                                _.format(tag, Writer, writer_, data, _.globalThis, enable_ansi_colors)?;
                            }
                            writer.writeAll(", \n");
                        },
                        .ErrorEvent => {
                            if (data := value.fastGet(_.globalThis, .@"error")?) {
                                _.writeIndent(Writer, writer_) catch unreachable;
                                writer.print(
                                    comptime Output.prettyFmt("<r><blue>error<d>:<r> ", enable_ansi_colors),
                                    {},
                                );

                                const tag = Tag.get(data, _.globalThis)?;
                                _.format(tag, Writer, writer_, data, _.globalThis, enable_ansi_colors)?;
                                writer.writeAll("\n");
                            }
                        },
                        else => unreachable,
                    }
                }

                _.writeIndent(Writer, writer_) catch unreachable;
                writer.writeAll("}");
            },
            .JSX => {
                writer.writeAll(comptime Output.prettyFmt("<r>", enable_ansi_colors));

                writer.writeAll("<");

                let needs_space = false;
                let tag_name_str = ZigString.init("");

                let tag_name_slice: ZigString.Slice = ZigString.Slice.empty;
                let is_tag_kind_primitive = false;

                defer if (tag_name_slice.isAllocated()) tag_name_slice.deinit();

                if (type_value := value.get(_.globalThis, "type")?) {
                    const _tag = Tag.get(type_value, _.globalThis)?;

                    if (_tag.cell == .Symbol) {} else if (_tag.cell.isStringLike()) {
                        type_value.toZigString(&tag_name_str, _.globalThis)?;
                        is_tag_kind_primitive = true;
                    } else if (_tag.cell.isObject() || type_value.isCallable()) {
                        type_value.getNameProperty(_.globalThis, &tag_name_str)?;
                        if (tag_name_str.len == 0) {
                            tag_name_str = ZigString.init("NoName");
                        }
                    } else {
                        type_value.toZigString(&tag_name_str, _.globalThis)?;
                    }

                    tag_name_slice = tag_name_str.toSlice(default_allocator);
                    needs_space = true;
                } else {
                    tag_name_slice = ZigString.init("unknown").toSlice(default_allocator);

                    needs_space = true;
                }

                if (!is_tag_kind_primitive)
                    writer.writeAll(comptime Output.prettyFmt("<cyan>", enable_ansi_colors))
                else
                    writer.writeAll(comptime Output.prettyFmt("<green>", enable_ansi_colors));
                writer.writeAll(tag_name_slice.slice());
                if (enable_ansi_colors) writer.writeAll(comptime Output.prettyFmt("<r>", enable_ansi_colors));

                if (key_value := value.get(_.globalThis, "key")?) {
                    if (!key_value.isUndefinedOrNull()) {
                        if (needs_space)
                            writer.writeAll(" key=")
                        else
                            writer.writeAll("key=");

                        const old_quote_strings = _.quote_strings;
                        _.quote_strings = true;
                        defer _.quote_strings = old_quote_strings;

                        try _.format(try Tag.get(key_value, _.globalThis), Writer, writer_, key_value, _.globalThis, enable_ansi_colors);

                        needs_space = true;
                    }
                }

                if (props := value.get(_.globalThis, "props")?) {
                    const prev_quote_strings = _.quote_strings;
                    defer _.quote_strings = prev_quote_strings;
                    _.quote_strings = true;

                    // SAFETY: JSX props are always an object.
                    const props_obj = props.getObject().?;
                    let props_iter = try jsc.JSPropertyIterator({
                        skip_empty_name: true,
                        include_value: true,
                    }).init(_.globalThis, props_obj);
                    defer props_iter.deinit();

                    const children_prop = props.get(_.globalThis, "children")?;
                    if (props_iter.len > 0) {
                        {
                            _.indent += 1;
                            defer { if (_.indent > 0) { _.indent -= 1; } }
                            const count_without_children = props_iter.len - @as(usize, @intFromBool(children_prop != null));

                            while (prop := props_iter.next()?) {
                                if (prop.eqlComptime("children"))
                                    continue;

                                const property_value = props_iter.value;
                                const tag = Tag.get(property_value, _.globalThis)?;

                                if (tag.cell.isHidden()) continue;

                                if (needs_space) writer.writeAll(" ");
                                needs_space = false;

                                writer.print(
                                    comptime Output.prettyFmt("<r><blue>{f}<d>=<r>", enable_ansi_colors),
                                    { prop.trunc(128)},
                                );

                                if (tag.cell.isStringLike()) {
                                    if (comptime enable_ansi_colors) {
                                        writer.writeAll(comptime Output.prettyFmt("<r><green>", true));
                                    }
                                }

                                _.format(tag, Writer, writer_, property_value, _.globalThis, enable_ansi_colors)?;

                                if (tag.cell.isStringLike()) {
                                    if (comptime enable_ansi_colors) {
                                        writer.writeAll(comptime Output.prettyFmt("<r>", true));
                                    }
                                }

                                if (
                                // count_without_children is necessary to prevent printing an extra newline
                                // if there are children and one prop and the child prop is the last prop
                                props_iter.i + 1 < count_without_children &&
                                    // 3 is arbitrary but basically
                                    //  <input type="text" value="foo" />
                                    //  ^ should be one line
                                    // <input type="text" value="foo" bar="true" baz={false} />
                                    //  ^ should be multiple lines
                                    props_iter.i > 3)
                                {
                                    writer.writeAll("\n");
                                    _.writeIndent(Writer, writer_) catch unreachable;
                                } else if (props_iter.i + 1 < count_without_children) {
                                    writer.writeAll(" ");
                                }
                            }
                        }

                        if (children := children_prop) {
                            const tag = Tag.get(children, _.globalThis)?;

                            const print_children = switch (tag.tag) {
                                .String, .JSX, .Array => true,
                                else => false,
                            };

                            if (print_children) {
                                print_children: {
                                    switch (tag.tag) {
                                        .String => {
                                            const children_string = children.getZigString(_.globalThis)?;
                                            if (children_string.len == 0) break :print_children;
                                            if (comptime enable_ansi_colors) writer.writeAll(comptime Output.prettyFmt("<r>", true));

                                            writer.writeAll(">");
                                            if (children_string.len < 128) {
                                                writer.writeString(children_string);
                                            } else {
                                                _.indent += 1;
                                                writer.writeAll("\n");
                                                _.writeIndent(Writer, writer_) catch unreachable;
                                                if (_.indent > 0) { _.indent -= 1; }
                                                writer.writeString(children_string);
                                                writer.writeAll("\n");
                                                _.writeIndent(Writer, writer_) catch unreachable;
                                            }
                                        },
                                        .JSX => {
                                            writer.writeAll(">\n");

                                            {
                                                _.indent += 1;
                                                _.writeIndent(Writer, writer_) catch unreachable;
                                                defer { if (_.indent > 0) { _.indent -= 1; } }
                                                try _.format(try Tag.get(children, _.globalThis), Writer, writer_, children, _.globalThis, enable_ansi_colors);
                                            }

                                            writer.writeAll("\n");
                                            _.writeIndent(Writer, writer_) catch unreachable;
                                        },
                                        .Array => {
                                            const length = children.getLength(_.globalThis)?;
                                            if (length == 0) break :print_children;
                                            writer.writeAll(">\n");

                                            {
                                                _.indent += 1;
                                                _.writeIndent(Writer, writer_) catch unreachable;
                                                const _prev_quote_strings = _.quote_strings;
                                                _.quote_strings = false;
                                                defer _.quote_strings = _prev_quote_strings;

                                                defer { if (_.indent > 0) { _.indent -= 1; } }

                                                let j: usize = 0;
                                                while (j < length) : (j += 1) {
                                                    const child = jsc.JSObject.getIndex(children, _.globalThis, @as(u32, i32(j)))?;
                                                    try _.format(try Tag.get(child, _.globalThis), Writer, writer_, child, _.globalThis, enable_ansi_colors);
                                                    if (j + 1 < length) {
                                                        writer.writeAll("\n");
                                                        _.writeIndent(Writer, writer_) catch unreachable;
                                                    }
                                                }
                                            }

                                            writer.writeAll("\n");
                                            _.writeIndent(Writer, writer_) catch unreachable;
                                        },
                                        else => unreachable,
                                    }

                                    writer.writeAll("</");
                                    if (!is_tag_kind_primitive)
                                        writer.writeAll(comptime Output.prettyFmt("<r><cyan>", enable_ansi_colors))
                                    else
                                        writer.writeAll(comptime Output.prettyFmt("<r><green>", enable_ansi_colors));
                                    writer.writeAll(tag_name_slice.slice());
                                    if (enable_ansi_colors) writer.writeAll(comptime Output.prettyFmt("<r>", enable_ansi_colors));
                                    writer.writeAll(">");
                                }

                                return;
                            }
                        }
                    }
                }

                writer.writeAll(" />");
            },
            .Object => {
                const prev_quote_strings = _.quote_strings;
                _.quote_strings = true;
                defer _.quote_strings = prev_quote_strings;

                const Iterator = PropertyIterator(Writer, enable_ansi_colors);

                // We want to figure out if we should print this object
                // on one line or multiple lines
                //
                // The 100% correct way would be to print everything to
                // a temporary buffer and then check how long each line was
                //
                // But it's important that console.log() is fast. So we
                // do a small compromise to avoid multiple passes over input
                //
                // We say:
                //
                //   If the object has at least 2 properties and ANY of the following conditions are met:
                //      - total length of all the property names is more than
                //        14 characters
                //     - the parent object is printing each property on a new line
                //     - The first property is a DOM object, ESM namespace, Map, Set, or Blob
                //
                //   Then, we print it each property on a new line, recursively.
                //
                const prev_always_newline_scope = _.always_newline_scope;
                defer _.always_newline_scope = prev_always_newline_scope;
                let iter = Iterator{
                    formatter: _,
                    writer: writer_,
                    always_newline: _.always_newline_scope || _.goodTimeForANewLine(),
                    parent: value,
                };

                value.forEachPropertyOrdered(_.globalThis, &iter, Iterator.forEach)?;

                if (iter.i == 0) {
                    let object_name = ZigString.Empty;
                    value.getClassName(_.globalThis, &object_name)?;

                    if (!object_name.eqlComptime("Object")) {
                        writer.print("{f} {{}}", .{ object_name});
                    } else {
                        // don't write "Object"
                        writer.writeAll("{}");
                    }
                } else {
                    _.printComma(Writer, writer_, enable_ansi_colors) catch unreachable;

                    if (iter.always_newline) {
                        if (_.indent > 0) { _.indent -= 1; }
                        writer.writeAll("\n");
                        _.writeIndent(Writer, writer_) catch {};
                        writer.writeAll("}");
                        _.estimated_line_length += 1;
                    } else {
                        _.estimated_line_length += 2;
                        writer.writeAll(" }");
                    }

                    if (_.indent == 0) {
                        writer.writeAll("\n");
                    }
                }
            },
            .TypedArray => {
                const arrayBuffer = value.asArrayBuffer(_.globalThis).?;
                const slice = arrayBuffer.byteSlice();

                if (_.indent == 0 && slice.len > 0) {
                    writer.writeAll("\n");
                }

                if (jsType == .Uint8Array) {
                    let buffer_name = ZigString.Empty;
                    value.getClassName(_.globalThis, &buffer_name)?;
                    if (strings.eqlComptime(buffer_name.slice(), "Buffer")) {
                        // special formatting for 'Buffer' snapshots only
                        if (slice.len == 0 && _.indent == 0) writer.writeAll("\n");
                        writer.writeAll("{\n");
                        _.indent += 1;
                        _.writeIndent(Writer, writer_) catch {};
                        writer.writeAll("\"data\": [");

                        _.indent += 1;
                        for (el of slice) {
                            writer.writeAll("\n");
                            _.writeIndent(Writer, writer_) catch {};
                            writer.print("{d},", { el});
                        }
                        if (_.indent > 0) { _.indent -= 1; }

                        if (slice.len > 0) {
                            writer.writeAll("\n");
                            _.writeIndent(Writer, writer_) catch {};
                            writer.writeAll("],\n");
                        } else {
                            writer.writeAll("],\n");
                        }

                        _.writeIndent(Writer, writer_) catch {};
                        writer.writeAll("\"type\": \"Buffer\",\n");

                        if (_.indent > 0) { _.indent -= 1; }
                        _.writeIndent(Writer, writer_) catch {};
                        writer.writeAll("}");

                        if (_.indent == 0) {
                            writer.writeAll("\n");
                        }

                        return;
                    }
                    writer.writeAll(bun.asByteSlice(@tagName(arrayBuffer.typed_array_type)));
                } else {
                    writer.writeAll(bun.asByteSlice(@tagName(arrayBuffer.typed_array_type)));
                }

                writer.writeAll(" [");

                if (slice.len > 0) {
                    switch (jsType) {
                        .Int8Array => {
                            const slice_with_type: []align(std.@"meta".alignment([]i8)) i8 = @alignCast(std.mem.bytesAsSlice(i8, slice));
                            _.indent += 1;
                            defer { if (_.indent > 0) { _.indent -= 1; } }
                            for (el of slice_with_type) {
                                writer.writeAll("\n");
                                _.writeIndent(Writer, writer_) catch {};
                                writer.print("{d},", { el});
                            }
                        },
                        .Int16Array => {
                            const slice_with_type: []align(std.@"meta".alignment([]i16)) i16 = @alignCast(std.mem.bytesAsSlice(i16, slice));
                            _.indent += 1;
                            defer { if (_.indent > 0) { _.indent -= 1; } }
                            for (el of slice_with_type) {
                                writer.writeAll("\n");
                                _.writeIndent(Writer, writer_) catch {};
                                writer.print("{d},", { el});
                            }
                        },
                        .Uint16Array => {
                            const slice_with_type: []align(std.@"meta".alignment([]u16)) u16 = @alignCast(std.mem.bytesAsSlice(u16, slice));
                            _.indent += 1;
                            defer { if (_.indent > 0) { _.indent -= 1; } }
                            for (el of slice_with_type) {
                                writer.writeAll("\n");
                                _.writeIndent(Writer, writer_) catch {};
                                writer.print("{d},", { el});
                            }
                        },
                        .Int32Array => {
                            const slice_with_type: []align(std.@"meta".alignment([]i32)) i32 = @alignCast(std.mem.bytesAsSlice(i32, slice));
                            _.indent += 1;
                            defer { if (_.indent > 0) { _.indent -= 1; } }
                            for (el of slice_with_type) {
                                writer.writeAll("\n");
                                _.writeIndent(Writer, writer_) catch {};
                                writer.print("{d},", { el});
                            }
                        },
                        .Uint32Array => {
                            const slice_with_type: []align(std.@"meta".alignment([]u32)) u32 = @alignCast(std.mem.bytesAsSlice(u32, slice));
                            _.indent += 1;
                            defer { if (_.indent > 0) { _.indent -= 1; } }
                            for (el of slice_with_type) {
                                writer.writeAll("\n");
                                _.writeIndent(Writer, writer_) catch {};
                                writer.print("{d},", { el});
                            }
                        },
                        .Float16Array => {
                            const slice_with_type: []align(std.@"meta".alignment([]f16)) f16 = @alignCast(std.mem.bytesAsSlice(f16, slice));
                            _.indent += 1;
                            defer { if (_.indent > 0) { _.indent -= 1; } }
                            for (el of slice_with_type) {
                                writer.writeAll("\n");
                                _.writeIndent(Writer, writer_) catch {};
                                writer.print("{d},", { el});
                            }
                        },
                        .Float32Array => {
                            const slice_with_type: []align(std.@"meta".alignment([]f32)) f32 = @alignCast(std.mem.bytesAsSlice(f32, slice));
                            _.indent += 1;
                            defer { if (_.indent > 0) { _.indent -= 1; } }
                            for (el of slice_with_type) {
                                writer.writeAll("\n");
                                _.writeIndent(Writer, writer_) catch {};
                                writer.print("{d},", { el});
                            }
                        },
                        .Float64Array => {
                            const slice_with_type: []align(std.@"meta".alignment([]f64)) f64 = @alignCast(std.mem.bytesAsSlice(f64, slice));
                            _.indent += 1;
                            defer { if (_.indent > 0) { _.indent -= 1; } }
                            for (el of slice_with_type) {
                                writer.writeAll("\n");
                                _.writeIndent(Writer, writer_) catch {};
                                writer.print("{d},", { el});
                            }
                        },
                        .BigInt64Array => {
                            const slice_with_type: []align(std.@"meta".alignment([]i64)) i64 = @alignCast(std.mem.bytesAsSlice(i64, slice));
                            _.indent += 1;
                            defer { if (_.indent > 0) { _.indent -= 1; } }
                            for (el of slice_with_type) {
                                writer.writeAll("\n");
                                _.writeIndent(Writer, writer_) catch {};
                                writer.print("{d},", { el});
                            }
                        },
                        .BigUint64Array => {
                            const slice_with_type: []align(std.@"meta".alignment([]u64)) u64 = @alignCast(std.mem.bytesAsSlice(u64, slice));
                            _.indent += 1;
                            defer { if (_.indent > 0) { _.indent -= 1; } }
                            for (el of slice_with_type) {
                                writer.writeAll("\n");
                                _.writeIndent(Writer, writer_) catch {};
                                writer.print("{d},", { el});
                            }
                        },

                        // Uint8Array, Uint8ClampedArray, DataView, ArrayBuffer
                        else => {
                            const slice_with_type: []align(std.@"meta".alignment([]u8)) u8 = @alignCast(std.mem.bytesAsSlice(u8, slice));
                            _.indent += 1;
                            defer { if (_.indent > 0) { _.indent -= 1; } }
                            for (el of slice_with_type) {
                                writer.writeAll("\n");
                                _.writeIndent(Writer, writer_) catch {};
                                writer.print("{d},", { el});
                            }
                        },
                    }
                }

                if (slice.len > 0) {
                    writer.writeAll("\n");
                    _.writeIndent(Writer, writer_) catch {};
                    writer.writeAll("]");
                    if (_.indent == 0) {
                        writer.writeAll("\n");
                    }
                } else {
                    writer.writeAll("]");
                }
            },
            else => {},
        }
    }

    fn format(*_, result: Tag.Result, comptime Writer: type, writer: *std.Io.Writer, value: JSValue, globalThis: *JSGlobalObject, comptime enable_ansi_colors: bool): bun.JSError!void {
        const prevGlobalThis = _.globalThis;
        defer _.globalThis = prevGlobalThis;
        _.globalThis = globalThis;

        // This looks incredibly redundant. We make the JestPrettyFormat.Formatter.Tag a
        // comptime var so we have to repeat it here. The rationale there is
        // it _should_ limit the stack usage because each version of the
        // function will be relatively small
        return switch (result.tag) {
            .StringPossiblyFormatted => _.printAs(.StringPossiblyFormatted, Writer, writer, value, result.cell, enable_ansi_colors),
            .String => _.printAs(.String, Writer, writer, value, result.cell, enable_ansi_colors),
            .Undefined => _.printAs(.Undefined, Writer, writer, value, result.cell, enable_ansi_colors),
            .Double => _.printAs(.Double, Writer, writer, value, result.cell, enable_ansi_colors),
            .Integer => _.printAs(.Integer, Writer, writer, value, result.cell, enable_ansi_colors),
            .Null => _.printAs(.Null, Writer, writer, value, result.cell, enable_ansi_colors),
            .Boolean => _.printAs(.Boolean, Writer, writer, value, result.cell, enable_ansi_colors),
            .Array => _.printAs(.Array, Writer, writer, value, result.cell, enable_ansi_colors),
            .Object => _.printAs(.Object, Writer, writer, value, result.cell, enable_ansi_colors),
            .Function => _.printAs(.Function, Writer, writer, value, result.cell, enable_ansi_colors),
            .Class => _.printAs(.Class, Writer, writer, value, result.cell, enable_ansi_colors),
            .Error => _.printAs(.Error, Writer, writer, value, result.cell, enable_ansi_colors),
            .ArrayBuffer, .TypedArray => _.printAs(.TypedArray, Writer, writer, value, result.cell, enable_ansi_colors),
            .Map => _.printAs(.Map, Writer, writer, value, result.cell, enable_ansi_colors),
            .Set => _.printAs(.Set, Writer, writer, value, result.cell, enable_ansi_colors),
            .Symbol => _.printAs(.Symbol, Writer, writer, value, result.cell, enable_ansi_colors),
            .BigInt => _.printAs(.BigInt, Writer, writer, value, result.cell, enable_ansi_colors),
            .GlobalObject => _.printAs(.GlobalObject, Writer, writer, value, result.cell, enable_ansi_colors),
            .Private => _.printAs(.Private, Writer, writer, value, result.cell, enable_ansi_colors),
            .Promise => _.printAs(.Promise, Writer, writer, value, result.cell, enable_ansi_colors),
            .JSON => _.printAs(.JSON, Writer, writer, value, result.cell, enable_ansi_colors),
            .NativeCode => _.printAs(.NativeCode, Writer, writer, value, result.cell, enable_ansi_colors),
            .JSX => _.printAs(.JSX, Writer, writer, value, result.cell, enable_ansi_colors),
            .Event => _.printAs(.Event, Writer, writer, value, result.cell, enable_ansi_colors),
        };
    }
}

