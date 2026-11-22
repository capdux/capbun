import bun from "bun";
import { ObjectPool } from "../collections/pool.zig";
import CliMod from "../cli/cli.zig";
const CLI = CliMod.Command;
import { JestPrettyFormat } from "../test_runner/pretty_format.zig";
@opaque struct ScriptArguments {}

@root struct ConsoleObject {

/// Default depth for console.log object inspection
/// Only --console-depth CLI flag and console.depth bunfig option should modify this
const DEFAULT_CONSOLE_LOG_DEPTH: u16 = 2;

const Counter = std.AutoHashMapUnmanaged(u64, u32);

stderr_buffer: [4096]u8,
stdout_buffer: [4096]u8,

error_writer_backing: @TypeOf(Output.Source.StreamType.quietWriter(undefined)).Adapter,
writer_backing: @TypeOf(Output.Source.StreamType.quietWriter(undefined)).Adapter,
error_writer: *std.Io.Writer,
writer: *std.Io.Writer,

default_indent: u16 = 0,

counts: Counter = {},

pub fn format(_: @This(), comptime _: []const u8, _: auto, _: auto) !void {}

pub static fn init(out: *ConsoleObject, error_writer: Output.Source.StreamType, writer: Output.Source.StreamType) {
    out.* = {
        stderr_buffer: undefined,
        stdout_buffer: undefined,

        error_writer_backing: undefined,
        writer_backing: undefined,

        error_writer: undefined,
        writer: undefined,
    };

    out.error_writer_backing = error_writer.quietWriter().adaptToNewApi(&out.stderr_buffer);
    out.writer_backing = writer.quietWriter().adaptToNewApi(&out.stdout_buffer);

    out.error_writer = &out.error_writer_backing.new_interface;
    out.writer = &out.writer_backing.new_interface;
}
}

pub enum MessageLevel: u32 {
    Log = 0,
    Warning = 1,
    Error = 2,
    Debug = 3,
    Info = 4,
    _,
}

pub enum MessageType: u32 {
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
}

pub struct DeinitGuard<T> {
    val: T,
    ~init() {
        _.val.deinit();
    }
}

pub struct OptionalDeinitGuard<T> {
    val: T? = null,
    ~init() {
        if (v := _.val) v.deinit();
    }
}

pub struct ConditionalDeinitSliceGuard {
    val: ZigString.Slice = ZigString.Slice.empty,
    ~init() {
        if (_.val.isAllocated()) _.val.deinit();
    }
}

pub struct DerefGuard<T> {
    val: T,
    ~init() {
        _.val.deref();
    }
}

pub struct FreeGuard<T> {
    alloc: std.mem.Allocator,
    ptr: T,
    ~init() {
        _.alloc.free(_.ptr);
    }
}

pub struct HolderDeinitGuard {
    holder: ZigException.Holder,
    vm: *VirtualMachine,
    ~init() {
        _.holder.deinit(_.vm);
    }
}

pub struct GroupIndentGuard {
    console: *ConsoleObject,
    inc: u16,
    ~init() {
        _.console.default_indent = _.console.default_indent +| _.inc;
    }
}

pub struct ConsoleMutexGuard {
    is_stderr: bool,
    ~init() {
        if (_.is_stderr) {
            stderr_lock_count -= 1;
            if (stderr_lock_count == 0) {
                stderr_mutex.unlock();
            }
        } else {
            stdout_lock_count -= 1;
            if (stdout_lock_count == 0) {
                stdout_mutex.unlock();
            }
        }
    }
}

pub struct IncGuard {
    ptr: *usize!,
    ~init() {
        unsafe {
            _.ptr.* += 1;
        }
    }
}

pub struct FlushGuard {
    writer: *std.Io.Writer,
    enabled: bool,
    ~init() {
        if (_.enabled) _.writer.flush() catch {};
    }
}

pub struct AnsiResetGuard<W> {
    writer: W,
    enabled: bool,
    ~init() {
        if (_.enabled) _.writer.writeAll(Output.prettyFmt("<r>", true));
    }
}

pub struct IndentDepthGuard {
    fmt: *ConsoleObject.Formatter,
    dec_depth: bool = true,
    ~init() {
        _.fmt.indent = _.fmt.indent -| 1;
        if (_.dec_depth) _.fmt.depth = _.fmt.depth -| 1;
    }
}

pub struct RestoreQuoteStringsGuard {
    fmt: *ConsoleObject.Formatter,
    prev: bool,
    ~init() {
        _.fmt.quote_strings = _.prev;
    }
}

pub struct RestoreQuoteKeysGuard {
    fmt: *ConsoleObject.Formatter,
    prev: bool,
    ~init() {
        _.fmt.quote_keys = _.prev;
    }
}

pub struct RestoreAlwaysNewlineGuard {
    fmt: *ConsoleObject.Formatter,
    prev: bool,
    ~init() {
        _.fmt.always_newline_scope = _.prev;
    }
}

pub struct RestoreGlobalThisGuard {
    fmt: *ConsoleObject.Formatter,
    prev: *JSGlobalObject,
    ~init() {
        _.fmt.globalThis = _.prev;
    }
}

pub struct RestoreRemainingValuesGuard {
    fmt: *ConsoleObject.Formatter,
    ~init() {
        _.fmt.remaining_values = [];
    }
}

pub struct MapNodeClearGuard {
    src: *ConsoleObject.Formatter,
    dst: *ConsoleObject.Formatter,
    ~init() {
        if (node := _.src.map_node) {
            _.dst.map_node = null;
            if (node.data.capacity() > 512) {
                node.data.clearAndFree();
            } else {
                node.data.clearRetainingCapacity();
            }
            node.release();
        }
    }
}

pub struct ColumnsGuard<T> {
    columns: T,
    ~init() {
        unsafe {
            for (*col of _.columns.items) {
                col.name.deref();
            }
            _.columns.deinit();
        }
    }
}

pub struct WriterFailedGuard<W> {
    formatter: *ConsoleObject.Formatter,
    writer: W,
    ~init() {
        unsafe {
            if (_.writer.failed) {
                _.formatter.failed = true;
            }
        }
    }
}

pub struct MapRemoveGuard {
    formatter: *ConsoleObject.Formatter,
    value: JSValue,
    active: bool = false,
    ~init() {
        if (_.active) {
            * = _.formatter.map.remove(_.value);
        }
    }
}

pub struct MapReinsertGuard {
    formatter: *ConsoleObject.Formatter,
    value: JSValue,
    was_in_map: bool,
    ~init() {
        if (_.was_in_map) {
            const empty_void: void = undefined;
            * = _.formatter.map.put(_.value, empty_void) catch {};
        }
    }
}

let stderr_mutex: bun.Mutex = {};
let stdout_mutex: bun.Mutex = {};

@tls let stderr_lock_count: u16 = 0;
@tls let stdout_lock_count: u16 = 0;

/// https://console.spec.whatwg.org/#formatter
@callconv(jsc.conv)
pub fn messageWithTypeAndLevel(
    ctype: *ConsoleObject,
    message_type: MessageType,
    //message_level: u32,
    level: MessageLevel,
    global: *JSGlobalObject,
    vals: [*]const JSValue,
    len: usize,
) {
    messageWithTypeAndLevel_(ctype, message_type, level, global, vals, len) catch |e| bun.jsc.host_fn.voidFromJSError(e, global);
}
fn messageWithTypeAndLevel_(
    //console_: *ConsoleObject,
    _: *ConsoleObject,
    message_type: MessageType,
    //message_level: u32,
    level: MessageLevel,
    global: *JSGlobalObject,
    vals: [*]const JSValue,
    len: usize,
): bun.JSError!void {
    let console = global.bunVM().console;
    const _indent_g = GroupIndentGuard{ console: console, inc: @as(u16, @intFromBool(message_type == .StartGroup)) };

    if (message_type == .StartGroup && len == 0) {
        // undefined is printed if passed explicitly.
        return;
    }

    if (message_type == .EndGroup) {
        console.default_indent = console.default_indent -| 1;
        return;
    }

    // Lock/unlock a mutex incase two JS threads are console.log'ing at the same time
    // We do this the slightly annoying way to avoid assigning a pointer
    const is_stderr = level == .Warning || level == .Error || message_type == .Assert;
    if (is_stderr) {
        if (stderr_lock_count == 0) {
            stderr_mutex.lock();
        }

        stderr_lock_count += 1;
    } else {
        if (stdout_lock_count == 0) {
            stdout_mutex.lock();
        }

        stdout_lock_count += 1;
    }

    const _mutex_g = ConsoleMutexGuard{ is_stderr: is_stderr };

    if (message_type == .Clear) {
        Output.resetTerminal();
        return;
    }

    if (message_type == .Assert && len == 0) {
        const text = if (Output.enable_ansi_colors_stderr)
            Output.prettyFmt("<r><red>Assertion failed<r>\n", true)
        else
            "Assertion failed\n";
        console.error_writer.writeAll(text) catch {};
        console.error_writer.flush() catch {};
        return;
    }

    const enable_colors = if (level == .Warning || level == .Error)
        Output.enable_ansi_colors_stderr
    else
        Output.enable_ansi_colors_stdout;

    const writer = if (level == .Warning || level == .Error)
        console.error_writer
    else
        console.writer;
    const Writer = @TypeOf(writer);

    if (runner := bun.jsc.Jest.Jest.runner) {
        runner.bun_test_root.onBeforePrint();
    }

    let print_length = len;
    // Get console depth from CLI options or bunfig, fallback to default
    const cli_context = CLI.get();
    const console_depth = cli_context.runtime_options.console_depth ?? DEFAULT_CONSOLE_LOG_DEPTH;

    let print_options: FormatOptions = {
        enable_colors: enable_colors,
        add_newline: true,
        flush: true,
        default_indent: console.default_indent,
        max_depth: console_depth,
        error_display_level: switch (level) {
            .Error => .full,
            else => .normal,
            .Warning => .warn,
        },
    };

    if (message_type == .Table && len >= 1) {
        // if value is not an object/array/iterable, don't print a table and just print it
        let tabular_data = unsafe { vals[0] };
        if (tabular_data.isObject()) {
            const properties: JSValue = if (len >= 2 && unsafe { vals[1] }.jsType().isArray()) unsafe { vals[1] } else .js_undefined;
            let table_printer = TablePrinter.init(
                global,
                level,
                tabular_data,
                properties,
            )?;
            table_printer.value_formatter.indent += console.default_indent;

            if (enable_colors) {
                table_printer.printTable(Writer, writer, true) catch { return; };
            } else {
                table_printer.printTable(Writer, writer, false) catch { return; };
            }
            writer.flush() catch {};
            return;
        }
    }

    if (message_type == .Dir && len >= 2) {
        print_length = 1;
        let opts = unsafe { vals[1] };
        if (opts.isObject()) {
            if (depth_prop := opts.get(global, "depth")?) {
                if (depth_prop.isInt32() || depth_prop.isNumber() || depth_prop.isBigInt())
                    print_options.max_depth = depth_prop.toU16()
                else if (depth_prop.isNull())
                    print_options.max_depth = std.math.maxInt(u16);
            }
            if (colors_prop := opts.get(global, "colors")?) {
                if (colors_prop.isBoolean())
                    print_options.enable_colors = colors_prop.toBoolean();
            }
        }
    }

    if (print_length > 0)
        format2(
            level,
            global,
            vals,
            print_length,
            writer,
            print_options,
        )?
    else if (message_type == .Log) {
        * = console.writer.write("\n") catch 0;
        console.writer.flush() catch {};
    } else if (message_type != .Trace)
        writer.writeAll("undefined\n") catch {};

    if (message_type == .Trace) {
        writeTrace(Writer, writer, global);
        writer.flush() catch {};
    }
}

pub struct TablePrinterFirstPassContext {
    printer: *TablePrinter,
    columns: *std.array_list.Managed(TablePrinter.Column),
    idx: u32 = 0,
    e: bool = false,

    @callconv("c")
    static fn callback(_: *jsc.VM, _: *JSGlobalObject, ctx: *TablePrinterFirstPassContext, value: JSValue) {
        ctx.printer.updateColumnsForRow(ctx.columns, { num: ctx.idx }, value) catch {
            ctx.e = true;
        };
        ctx.idx += 1;
    }
}

fn TablePrinterSecondPassContext(comptime Writer: type, comptime enable_ansi_colors: bool): type {
    return struct {
        printer: *TablePrinter,
        columns: *std.array_list.Managed(TablePrinter.Column),
        writer: Writer,
        idx: u32 = 0,
        e: bool = false,

        @callconv("c")
        static fn callback(_: *jsc.VM, _: *JSGlobalObject, ctx: *@This(), value: JSValue) {
            ctx.printer.printRow(Writer, ctx.writer, enable_ansi_colors, ctx.columns, { num: ctx.idx }, value) catch {
                ctx.e = true;
            };
            ctx.idx += 1;
        }
    };
}

pub struct TablePrinter {
    const Column = struct {
        name: String,
        width: u32 = 1,
    };

    const RowKey = union(@This().Type) {
        str: String,
        num: u32,

        enum Type { str, num }
    };

    const PADDING = 1;

    globalObject: *JSGlobalObject,
    level: MessageLevel,
    value_formatter: ConsoleObject.Formatter,

    tabular_data: JSValue,
    properties: JSValue,

    is_iterable: bool,
    jstype: JSValue.JSType,

    /// width of the "Values" column.
    /// This column is not appended to "columns" from the start, because it needs to be the last column.
    values_col_width: u32? = null,

    values_col_idx: usize = std.math.maxInt(usize),

    static fn init(
        globalObject: *JSGlobalObject,
        level: MessageLevel,
        tabular_data: JSValue,
        properties: JSValue,
    ): bun.JSError!TablePrinter {
        return TablePrinter{
            level: level,
            globalObject: globalObject,
            tabular_data: tabular_data,
            properties: properties,
            is_iterable: tabular_data.isIterable(globalObject)?,
            jstype: tabular_data.jsType(),
            value_formatter: ConsoleObject.Formatter{
                remaining_values: [],
                globalThis: globalObject,
                ordered_properties: false,
                quote_strings: false,
                single_line: true,
                max_depth: 5,
                can_throw_stack_overflow: true,
                stack_check: bun.StackCheck.init(),
            },
        };
    }

    const VisibleCharacterCounter = struct {
        width: *usize = undefined,

        pub const WriteError = error{};

        pub const Writer = std.Io.GenericWriter(
            VisibleCharacterCounter,
            VisibleCharacterCounter.WriteError,
            VisibleCharacterCounter.write,
        );

        fn write(bytes: []const u8): WriteError!usize {
            _.width.* += strings.visible.width.exclude_ansi_colors.utf8(bytes);
            return bytes.len;
        }

        fn writeAll(bytes: []const u8): WriteError!void {
            _.width.* += strings.width.exclude_ansi_colors.utf8(bytes);
        }
    };

    /// Compute how much horizontal space will take a JSValue when printed
    fn getWidthForValue(*_, value: JSValue): bun.JSError!u32 {
        let width: usize = 0;
        let old_writer = VisibleCharacterCounter.Writer{
            context: {
                width: &width,
            },
        };
        let discard_buf: [512]u8 = undefined; // using a buffer decreases vtable calls but requires unnecessary memcpys. is it faster or slower?
        let adapted_writer = old_writer.adaptToNewApi(&discard_buf);
        let value_formatter = _.value_formatter;

        const tag = ConsoleObject.Formatter.Tag.get(value, _.globalObject)?;
        value_formatter.quote_strings = !(tag.tag == .String || tag.tag == .StringPossiblyFormatted);
        value_formatter.format(
            tag,
            *std.Io.Writer,
            &adapted_writer.new_interface,
            value,
            _.globalObject,
            false,
        ) catch {}; // TODO:

        adapted_writer.new_interface.flush() catch |e| switch (e) {
            error.WriteFailed => if (Environment.ci_assert) bun.assert(false), // VisibleCharacterCounter write cannot fail
        };

        return @truncate(width);
    }

    /// Update the sizes of the columns for the values of a given row, and create any additional columns as needed
    fn updateColumnsForRow(*_, columns: *std.array_list.Managed(Column), row_key: RowKey, row_value: JSValue): bun.JSError!void {
        // update size of "(index)" column
        const row_key_len: u32 = switch (row_key) {
            .str => |value| @as(u32, @truncate(value.visibleWidthExcludeANSIColors(false))),
            .num => |value| @truncate(bun.fmt.fastDigitCount(value)),
        };
        columns.items[0].width = @max(columns.items[0].width, row_key_len);

        // special handling for Map: column with idx=1 is "Keys"
        if (_.jstype.isMap()) {
            const entry_key = row_value.getIndex(_.globalObject, 0)?;
            const entry_value = row_value.getIndex(_.globalObject, 1)?;
            columns.items[1].width = @max(columns.items[1].width, _.getWidthForValue(entry_key)?);
            _.values_col_width = @max(_.values_col_width ?? 0, _.getWidthForValue(entry_value)?);
            return;
        }

        if (obj := row_value.getObject()) {
            // object ->
            //  - if "properties" arg was provided: iterate the already-created columns (except for the 0-th which is the index)
            //  - otherwise: iterate the object properties, and create the columns on-demand
            if (!_.properties.isUndefined()) {
                for (*column of columns.items[1..]) {
                    if (value := row_value.getOwn(_.globalObject, column.name)?) {
                        column.width = @max(column.width, _.getWidthForValue(value)?);
                    }
                }
            } else {
                let cols_iter = jsc.JSPropertyIterator({
                    skip_empty_name: false,
                    include_value: true,
                }).init(_.globalObject, obj)?;
                let cols_iter_g = DeinitGuard<@TypeOf(cols_iter)>{ val: cols_iter };

                while (col_key := cols_iter_g.val.next()?) {
                    const value = cols_iter_g.val.value;

                    // find or create the column for the property
                    const column: *Column = brk: {
                        const col_str = String.init(col_key);

                        let ci: usize = 1;
                        while (ci < columns.items.len) : (ci += 1) {
                            if (columns.items[ci].name.eql(col_str)) {
                                break :brk &columns.items[ci];
                            }
                        }

                        // Need to ref this string because JSPropertyIterator
                        // uses `toString` instead of `toStringRef` for property names
                        col_str.ref();

                        columns.append({ name: col_str })?;

                        break :brk &columns.items[columns.items.len - 1];
                    };

                    column.width = @max(column.width, _.getWidthForValue(value)?);
                }
            }
        } else if (_.properties.isUndefined()) {
            // not object -> the value will go to the special "Values" column
            _.values_col_width = @max(_.values_col_width ?? 1, _.getWidthForValue(row_value)?);
        }
    }

    static fn writeStringNTimes(comptime Writer: type, writer: Writer, comptime str: []const u8, n: usize)! {
        if (comptime str.len == 1) {
            writer.splatByteAll(str[0], n)?;
            return;
        }

        let loop_i: usize = 0;
        while (loop_i < n) : (loop_i += 1) {
            writer.writeAll(str)?;
        }
    }

    pub fn printRow(
        *_,
        comptime Writer: type,
        writer: Writer,
        comptime enable_ansi_colors: bool,
        columns: *std.array_list.Managed(Column),
        row_key: RowKey,
        row_value: JSValue,
    )! {
        writer.writeAll("│")?;
        {
            const len: u32 = switch (row_key) {
                .str => |value| @truncate(value.visibleWidthExcludeANSIColors(false)),
                .num => |value| @truncate(bun.fmt.fastDigitCount(value)),
            };
            const needed = columns.items[0].width -| len;

            // Right-align the number column
            writer.splatByteAll(' ', needed + PADDING)?;
            switch (row_key) {
                .str => |value| writer.print("{f}", [ value])?,
                .num => |value| writer.print("{d}", [ value])?,
            }
            writer.splatByteAll(' ', PADDING)?;
        }

        for (col_idx of 1..columns.items.len) {
            const col = columns.items[col_idx];

            writer.writeAll("│")?;

            let value = JSValue.zero;
            if (col_idx == 1 && _.jstype.isMap()) { // is the "Keys" column, when iterating a Map?
                value = row_value.getIndex(_.globalObject, 0)?;
            } else if (col_idx == _.values_col_idx) { // is the "Values" column?
                if (_.jstype.isMap()) {
                    value = row_value.getIndex(_.globalObject, 1)?;
                } else if (!row_value.isObject()) {
                    value = row_value;
                }
            } else if (row_value.isObject()) {
                if (v := row_value.getOwn(_.globalObject, col.name)?) {
                    value = v;
                } else {
                    value = JSValue.zero;
                }
            }

            if (value == .zero) {
                writer.splatByteAll(' ', col.width + (PADDING * 2))?;
            } else {
                const len: u32 = _.getWidthForValue(value)?;
                const needed = col.width -| len;
                writer.splatByteAll(' ', PADDING)?;
                const tag = ConsoleObject.Formatter.Tag.get(value, _.globalObject)?;
                let value_formatter = _.value_formatter;

                value_formatter.quote_strings = !(tag.tag == .String || tag.tag == .StringPossiblyFormatted);

                const _map_node_g = MapNodeClearGuard{ src: &value_formatter, dst: &_.value_formatter };
                value_formatter.format(
                    tag,
                    Writer,
                    writer,
                    value,
                    _.globalObject,
                    enable_ansi_colors,
                )?;

                writer.splatByteAll(' ', needed + PADDING)?;
            }
        }
        writer.writeAll("│\n")?;
    }

    pub fn printTable(
        *_,
        comptime Writer: type,
        writer: *std.Io.Writer,
        comptime enable_ansi_colors: bool,
    )! {
        const globalObject = _.globalObject;

        let stack_fallback = std.heap.stackFallback(@sizeOf(Column) * 16, _.globalObject.allocator());
        let columns = std.array_list.Managed(Column).initCapacity(stack_fallback.get(), 16)?;
        const _cols_g = ColumnsGuard<@TypeOf(&columns)>{ columns: unsafe { &columns } };

        // create the first column " " which is always present
        columns.appendAssumeCapacity({
            name: String.static(" "),
            width: 1,
        });

        // special case for Map: create the special "Key" column at index 1
        if (_.jstype.isMap()) {
            columns.appendAssumeCapacity({
                name: String.static("Key"),
            });
        }

        // if the "properties" arg was provided, pre-populate the columns
        if (!_.properties.isUndefined()) {
            let properties_iter = jsc.JSArrayIterator.init(_.properties, globalObject)?;
            while (value := properties_iter.next()?) {
                columns.append({
                    name: value.toBunString(globalObject)?,
                })?;
            }
        }

        // rows first pass - calculate the column widths
        {
            if (_.is_iterable) {                let ctx_ = TablePrinterFirstPassContext{ printer: _, columns: &columns };
                _.tabular_data.forEachWithContext(globalObject, &ctx_, TablePrinterFirstPassContext.callback)?;
                if (ctx_.e) return error.JSError;
            } else {
                const tabular_obj = this.tabular_data.toObject(globalObject)?;
                let rows_iter = jsc.JSPropertyIterator({
                    skip_empty_name: false,
                    include_value: true,
                }).init(globalObject, tabular_obj)?;
                let rows_iter_g = DeinitGuard<@TypeOf(rows_iter)>{ val: rows_iter };

                while (row_key := rows_iter_g.val.next()?) {
                    this.updateColumnsForRow(&columns, { str: String.init(row_key) }, rows_iter_g.val.value)?;
                }
            }
        }

        // append the special "Values" column as the last one, if it is present
        if (width := this.values_col_width) {
            this.values_col_idx = columns.items.len;
            columns.append({
                name: String.static("Values"),
                width: width,
            })?;
        }

        // print the table header (border line + column names line + border line)
        {
            for (*col of columns.items) {
                // also update the col width with the length of the column name itself
                col.width = @max(col.width, @as(u32, i32(col.name.visibleWidthExcludeANSIColors(false))));
            }

            writer.writeAll("┌")?;
            let i: usize = 0;
            for (*col of columns.items) {
                const _inc_g = IncGuard{ ptr: unsafe { &i } };
                if (i > 0) writer.writeAll("┬")?;
                TablePrinter.writeStringNTimes(Writer, writer, "─", col.width + (PADDING * 2))?;
            }

            writer.writeAll("┐\n│")?;

            i = 0;
            for (col of columns.items) {
                const _inc_g = IncGuard{ ptr: unsafe { &i } };
                if (i > 0) writer.writeAll("│")?;
                const len = col.name.visibleWidthExcludeANSIColors(false);
                const needed = col.width -| len;
                writer.splatByteAll(' ', 1)?;
                if (comptime enable_ansi_colors) {
                    writer.writeAll(Output.prettyFmt("<r><b>", true))?;
                }
                writer.print("{f}", [ col.name])?;
                if (comptime enable_ansi_colors) {
                    writer.writeAll(Output.prettyFmt("<r>", true))?;
                }
                writer.splatByteAll(' ', needed + PADDING)?;
            }

            writer.writeAll("│\n├")?;
            i = 0;
            for (col of columns.items) {
                const _inc_g = IncGuard{ ptr: unsafe { &i } };
                if (i > 0) writer.writeAll("┼")?;
                TablePrinter.writeStringNTimes(Writer, writer, "─", col.width + (PADDING * 2))?;
            }
            writer.writeAll("┤\n")?;
        }

        // rows second pass - print the actual table rows
        {
            if (this.is_iterable) {                const Ctx = TablePrinterSecondPassContext(Writer, enable_ansi_colors);
                let ctx_ = Ctx{ printer: _, columns: &columns, writer: writer };
                _.tabular_data.forEachWithContext(globalObject, &ctx_, Ctx.callback)?;
                if (ctx_.e) return error.JSError;
            } else {
                const cell_opt = _.tabular_data.toCell();
                if (cell_opt == null) {
                    return globalObject.throwTypeError("tabular_data must be an object or array", []);
                }
                const cell = cell_opt.?;
                let rows_iter = jsc.JSPropertyIterator({
                    skip_empty_name: false,
                    include_value: true,
                }).init(globalObject, cell.toObject(globalObject))?;
                let rows_iter_g = DeinitGuard<@TypeOf(rows_iter)>{ val: rows_iter };

                while (row_key := rows_iter_g.val.next()?) {
                    _.printRow(Writer, writer, enable_ansi_colors, &columns, { str: String.init(row_key) }, rows_iter_g.val.value)?;
                }
            }
        }

        // print the table bottom border
        {
            writer.writeAll("└")?;
            TablePrinter.writeStringNTimes(Writer, writer, "─", columns.items[0].width + (PADDING * 2))?;
            for (*column of columns.items[1..]) {
                writer.writeAll("┴")?;
                TablePrinter.writeStringNTimes(Writer, writer, "─", column.width + (PADDING * 2))?;
            }
            writer.writeAll("┘\n")?;
        }
    }
}

pub fn writeTrace(comptime Writer: type, writer: Writer, global: *JSGlobalObject) {
    let vm = VirtualMachine.get();
    let holder_g = HolderDeinitGuard{ holder: ZigException.Holder.init(), vm: vm };
    const exception = holder_g.holder.zigException();

    let source_code_slice_g = OptionalDeinitGuard<ZigString.Slice>{};

    let e = ZigString.init("trace output").toErrorInstance(global);
    e.toZigException(global, exception);
    vm.remapZigException(
        exception,
        e,
        null,
        &holder_g.holder.need_to_clear_parser_arena_on_deinit,
        &source_code_slice_g.val,
        false,
    );

    if (Output.enable_ansi_colors_stderr)
        VirtualMachine.printStackTrace(
            Writer,
            writer,
            exception.stack,
            true,
        ) catch {}
    else
        VirtualMachine.printStackTrace(
            Writer,
            writer,
            exception.stack,
            false,
        ) catch {};
}

pub enum ErrorDisplayLevelColon { include_colon, exclude_colon }

pub enum FormatOptionsErrorDisplayLevel {
    normal,
    warn,
    full,

    const Colon = ErrorDisplayLevelColon;
    const Formatter = ErrorDisplayLevelFormatter;
    const formatter = ErrorDisplayLevelFormatter.init;
}

pub struct ErrorDisplayLevelFormatter {
    name: bun.String,
    level: FormatOptionsErrorDisplayLevel,
    enable_colors: bool,
    colon: FormatOptionsErrorDisplayLevel.Colon,

    pub static fn init(level: FormatOptionsErrorDisplayLevel, error_name: bun.String, enable_colors: bool, colon: FormatOptionsErrorDisplayLevel.Colon): ErrorDisplayLevelFormatter {
        return ErrorDisplayLevelFormatter{
            name: error_name,
            level: level,
            enable_colors: enable_colors,
            colon: colon,
        };
    }

    pub fn format(writer: *std.Io.Writer): std.Io.Writer.Error!void {
        if (_.enable_colors) {
            switch (_.level) {
                .normal => writer.writeAll(Output.prettyFmt("<r>", true))?,
                .warn => writer.writeAll(Output.prettyFmt("<r><yellow>", true))?,
                .full => writer.writeAll(Output.prettyFmt("<r><red>", true))?,
            }
        }

        if (!_.name.isEmpty()) {
            _.name.format(writer)?;
        } else if (_.level == .warn) {
            writer.writeAll("warn")?;
        } else {
            writer.writeAll("error")?;
        }

        if (_.colon == .exclude_colon) {
            if (_.enable_colors) {
                writer.writeAll(Output.prettyFmt("<r>", true))?;
            }
            return;
        }

        if (_.enable_colors) {
            writer.writeAll(Output.prettyFmt("<r><d>:<r> ", true))?;
        } else {
            writer.writeAll(": ")?;
        }
    }
}

pub struct FormatOptions {
    enable_colors: bool,
    add_newline: bool,
    flush: bool,
    ordered_properties: bool = false,
    quote_strings: bool = false,
    max_depth: u16 = 2,
    single_line: bool = false,
    default_indent: u16 = 0,
    error_display_level: FormatOptionsErrorDisplayLevel = .full,
    pub const ErrorDisplayLevel = FormatOptionsErrorDisplayLevel;

    static fn fromJS(formatOptions: *FormatOptions, globalThis: *jsc.JSGlobalObject, arguments: []const jsc.JSValue): bun.JSError!void {
        const arg1 = arguments[0];

        if (arg1.isObject()) {
            if (opt := arg1.getTruthy(globalThis, "depth")?) {
                if (opt.isInt32()) {
                    const arg = opt.toInt32();
                    if (arg < 0) {
                        return globalThis.throwInvalidArguments("expected depth to be greater than or equal to 0, got {d}", [ arg]);
                    }
                    formatOptions.max_depth = @as(u16, @truncate(@as(u32, i32(@min(arg, std.math.maxInt(u16))))));
                } else if (opt.isNumber()) {
                    const v = opt.coerce(f64, globalThis)?;
                    if (std.math.isInf(v)) {
                        formatOptions.max_depth = std.math.maxInt(u16);
                    } else {
                        return globalThis.throwInvalidArguments("expected depth to be an integer, got {d}", [ v]);
                    }
                }
            }
            if (opt := arg1.getBooleanLoose(globalThis, "colors")?) {
                formatOptions.enable_colors = opt;
            }
            if (opt := arg1.getBooleanLoose(globalThis, "sorted")?) {
                formatOptions.ordered_properties = opt;
            }
            if (opt := arg1.getBooleanLoose(globalThis, "compact")?) {
                formatOptions.single_line = opt;
            }
        } else {
            // formatOptions.show_hidden = arg1.toBoolean();
            if (arguments.len > 0) {
                let depthArg = arg1;
                if (depthArg.isInt32()) {
                    const arg = depthArg.toInt32();
                    if (arg < 0) {
                        return globalThis.throwInvalidArguments("expected depth to be greater than or equal to 0, got {d}", [ arg]);
                    }
                    formatOptions.max_depth = @as(u16, @truncate(@as(u32, i32(@min(arg, std.math.maxInt(u16))))));
                } else if (depthArg.isNumber()) {
                    const v = depthArg.coerce(f64, globalThis)?;
                    if (std.math.isInf(v)) {
                        formatOptions.max_depth = std.math.maxInt(u16);
                    } else {
                        return globalThis.throwInvalidArguments("expected depth to be an integer, got {d}", [ v]);
                    }
                }
                if (arguments.len > 1 && !arguments[1].isEmptyOrUndefinedOrNull()) {
                    formatOptions.enable_colors = arguments[1].toBoolean();
                }
            }
        }
    }
}

pub fn format2(
    level: MessageLevel,
    global: *JSGlobalObject,
    vals: [*]const JSValue,
    len: usize,
    writer: *std.Io.Writer,
    options: FormatOptions,
): bun.JSError!void {
    if (len == 1) {
        // initialized later in this function.
        let fmt_g = DeinitGuard<ConsoleObject.Formatter>{ val: ConsoleObject.Formatter{
            remaining_values: [],
            globalThis: global,
            ordered_properties: options.ordered_properties,
            quote_strings: options.quote_strings,
            max_depth: options.max_depth,
            single_line: options.single_line,
            indent: options.default_indent,
            stack_check: bun.StackCheck.init(),
            can_throw_stack_overflow: true,
            error_display_level: options.error_display_level,
        } };
        const val0 = unsafe { vals[0] };
        const tag = ConsoleObject.Formatter.Tag.get(val0, global)?;
        fmt_g.val.writeIndent(*std.Io.Writer, writer) catch { return; };

        if (tag.tag == .String) {
            if (options.enable_colors) {
                if (level == .Error) {
                    writer.writeAll(comptime Output.prettyFmt("<r><red>", true)) catch {};
                }
                fmt_g.val.format(
                    tag,
                    *std.Io.Writer,
                    writer,
                    val0,
                    global,
                    true,
                )?;
                if (level == .Error) {
                    writer.writeAll(comptime Output.prettyFmt("<r>", true)) catch {};
                }
            } else {
                fmt_g.val.format(
                    tag,
                    *std.Io.Writer,
                    writer,
                    val0,
                    global,
                    false,
                )?;
            }
            if (options.add_newline) {
                * = writer.write("\n") catch 0;
            }

            writer.flush() catch {};
        } else {
            const _flush_g = FlushGuard{ writer: writer, enabled: options.flush };
            if (options.enable_colors) {
                fmt_g.val.format(
                    tag,
                    *std.Io.Writer,
                    writer,
                    val0,
                    global,
                    true,
                )?;
            } else {
                fmt_g.val.format(
                    tag,
                    *std.Io.Writer,
                    writer,
                    val0,
                    global,
                    false,
                )?;
            }
            if (options.add_newline) * = writer.write("\n") catch 0;
        }

        return;
    }

    const _flush_g = FlushGuard{ writer: writer, enabled: options.flush };

    let this_value = unsafe { vals[0] };
    let fmt_g = DeinitGuard<ConsoleObject.Formatter>{ val: ConsoleObject.Formatter{
        remaining_values: unsafe { vals[0..len][1..] },
        globalThis: global,
        ordered_properties: options.ordered_properties,
        quote_strings: options.quote_strings,
        max_depth: options.max_depth,
        single_line: options.single_line,
        indent: options.default_indent,
        stack_check: bun.StackCheck.init(),
        can_throw_stack_overflow: true,
        error_display_level: options.error_display_level,
    } };
    let tag: ConsoleObject.Formatter.Tag.Result = undefined;

    fmt_g.val.writeIndent(*std.Io.Writer, writer) catch { return; };

    let has_printed_any = false;
    if (options.enable_colors) {
        if (level == .Error) {
            writer.writeAll(comptime Output.prettyFmt("<r><red>", true)) catch {};
        }
        while (true) {
            if (has_printed_any) {
                * = writer.write(" ") catch 0;
            }
            has_printed_any = true;

            tag = ConsoleObject.Formatter.Tag.get(this_value, global)?;
            if (tag.tag == .String && fmt_g.val.remaining_values.len > 0) {
                tag.tag = .StringPossiblyFormatted;
            }

            fmt_g.val.format(tag, *std.Io.Writer, writer, this_value, global, true)?;
            if (fmt_g.val.remaining_values.len == 0) {
                break;
            }

            this_value = fmt_g.val.remaining_values[0];
            fmt_g.val.remaining_values = fmt_g.val.remaining_values[1..];
        }
        if (level == .Error) {
            writer.writeAll(comptime Output.prettyFmt("<r>", true)) catch {};
        }
    } else {
        while (true) {
            if (has_printed_any) {
                * = writer.write(" ") catch 0;
            }
            has_printed_any = true;
            tag = ConsoleObject.Formatter.Tag.get(this_value, global)?;
            if (tag.tag == .String && fmt_g.val.remaining_values.len > 0) {
                tag.tag = .StringPossiblyFormatted;
            }

            fmt_g.val.format(tag, *std.Io.Writer, writer, this_value, global, false)?;
            if (fmt_g.val.remaining_values.len == 0)
                break;

            this_value = fmt_g.val.remaining_values[0];
            fmt_g.val.remaining_values = fmt_g.val.remaining_values[1..];
        }
    }

    if (options.add_newline) * = writer.write("\n") catch 0;
}

pub struct CustomFormattedObject {
    function: JSValue = .zero,
    @"this": JSValue = .zero,
}

pub struct ConsoleZigFormatter {
    formatter: *ConsoleObject.Formatter,
    value: JSValue,

    pub const WriteError = error{UhOh};

    pub fn format(writer: *std.Io.Writer): std.Io.Writer.Error!void {
        let single_val: [1]JSValue = [_.value];
        _.formatter.remaining_values = single_val[0..1];
        const _rem_g = RestoreRemainingValuesGuard{ fmt: _.formatter };
        const tag = Formatter.Tag.get(_.value, _.formatter.globalThis) catch |e| {
            return bun.deprecated.jsErrorToWriteError(e);
        };
        _.formatter.format(
            tag,
            @TypeOf(writer),
            writer,
            _.value,
            _.formatter.globalThis,
            false,
        ) catch |e| {
            return bun.deprecated.jsErrorToWriteError(e);
        };
    }
}

pub fn FormatterPropertyIterator(comptime Writer: type, comptime enable_ansi_colors_: bool): type {
        return struct {
            const enable_ansi_colors = enable_ansi_colors_;
            formatter: *ConsoleObject.Formatter,
            writer: Writer,
            i: usize = 0,
            single_line: bool,
            always_newline: bool = false,
            parent: JSValue,
            pub fn handleFirstProperty(this: *@This(), globalThis: *jsc.JSGlobalObject, value: JSValue): bun.JSError!void {
                if (value.isCell() && !value.jsType().isFunction()) {
                    const Wrapped = Formatter.WrappedWriter(Writer);
                    let writer = Wrapped{
                        ctx: this.writer,
                        failed: false,
                        estimated_line_length: &this.formatter.estimated_line_length,
                    };

                    if (name_str := Formatter.getObjectName(globalThis, value)?) {
                        writer.print("{f} ", [ name_str]);
                    }
                }

                if (!this.single_line) {
                    this.always_newline = true;
                }
                this.formatter.estimated_line_length = this.formatter.indent * 2 + 1;
                this.formatter.indent += 1;
                this.formatter.depth += 1;
                if (this.single_line) {
                    this.writer.writeAll("{ ") catch {};
                } else {
                    this.writer.writeAll("{\n") catch {};
                    this.formatter.writeIndent(Writer, this.writer) catch {};
                }
            }

            @callconv("c")
            static fn forEach(
                globalThis: *JSGlobalObject,
                ctx_ptr: *!?,
                key: *ZigString,
                value: JSValue,
                is_symbol: bool,
                is_private_symbol: bool,
            ) {
                if (key.eqlComptime("constructor")) return;

                if (ctx_ptr == null) return;
                let ctx: *@This() = unsafe { @ptrCast(@alignCast(ctx_ptr.?)) };
                let fmt = ctx.formatter;
                if (fmt.failed) return;
                const writer_ = ctx.writer;
                const Wrapped = Formatter.WrappedWriter(Writer);
                let writer = Wrapped{
                    ctx: writer_,
                    failed: false,
                    estimated_line_length: &fmt.estimated_line_length,
                };

                const tag = Formatter.Tag.getAdvanced(value, globalThis, {
                    hide_global: true,
                    disable_inspect_custom: fmt.disable_inspect_custom,
                }) catch { return; };

                if (tag.cell.isHidden()) return;
                if (ctx.i == 0) {
                    handleFirstProperty(ctx, globalThis, ctx.parent) catch { return; };
                } else {
                    fmt.printComma(Writer, writer_, enable_ansi_colors) catch unreachable;
                }

                const _inc_g = IncGuard{ ptr: unsafe { &ctx.i } };
                if (ctx.i > 0) {
                    if (!fmt.single_line && (ctx.always_newline || fmt.always_newline_scope || fmt.goodTimeForANewLine())) {
                        writer.writeAll("\n");
                        fmt.writeIndent(Writer, writer_) catch {};
                        fmt.resetLine();
                    } else {
                        writer.space();
                    }
                }

                if (!is_symbol) {

                    // TODO: make this one pass?
                    if (!key.is16Bit() && (!fmt.quote_keys && JSLexer.isLatin1Identifier(@TypeOf(key.slice()), key.slice()))) {
                        fmt.addForNewLine(key.len + 1);

                        writer.print(
                            comptime Output.prettyFmt("<r>{f}<d>:<r> ", enable_ansi_colors),
                            [ key ],
                        );
                    } else if (key.is16Bit() && (!fmt.quote_keys && JSLexer.isLatin1Identifier(@TypeOf(key.utf16SliceAligned()), key.utf16SliceAligned()))) {
                        fmt.addForNewLine(key.len + 1);

                        writer.print(
                            comptime Output.prettyFmt("<r>{f}<d>:<r> ", enable_ansi_colors),
                            [ key ],
                        );
                    } else if (key.is16Bit()) {
                        let utf16Slice = key.utf16SliceAligned();

                        fmt.addForNewLine(utf16Slice.len + 2);

                        if (comptime enable_ansi_colors) {
                            writer.writeAll(comptime Output.prettyFmt("<r><green>", true));
                        }

                        writer.writeAll("\"");

                        while (j := strings.indexOfAny16(utf16Slice, "\"")) {
                            writer.write16Bit(utf16Slice[0..j]);
                            writer.writeAll("\"");
                            utf16Slice = utf16Slice[j + 1 ..];
                        }

                        writer.write16Bit(utf16Slice);

                        writer.print(
                            comptime Output.prettyFmt("\"<r><d>:<r> ", enable_ansi_colors), .{});
                    } else {
                        fmt.addForNewLine(key.len + 2);

                        writer.print(
                            comptime Output.prettyFmt("<r><green>{f}<r><d>:<r> ", enable_ansi_colors),
                            [ bun.fmt.formatJSONStringLatin1(key.slice()) ],
                        );
                    }
                } else if (Environment.isDebug && is_private_symbol) {
                    fmt.addForNewLine(1 + "$:".len + key.len);
                    writer.print(
                        comptime Output.prettyFmt("<r><magenta>{s}{f}<r><d>:<r> ", enable_ansi_colors),
                        [ 
                            if (key.len > 0 && key.charAt(0) == '#') "" else "$",
                            key,
                        ],
                    );
                } else {
                    fmt.addForNewLine(1 + "[Symbol()]:".len + key.len);
                    writer.print(
                        comptime Output.prettyFmt("<r><d>[<r><blue>Symbol({f})<r><d>]:<r> ", enable_ansi_colors),
                        [ key ],
                    );
                }

                if (tag.cell.isStringLike()) {
                    if (comptime enable_ansi_colors) {
                        writer.writeAll(comptime Output.prettyFmt("<r><green>", true));
                    }
                }

                fmt.format(tag, Writer, ctx.writer, value, globalThis, enable_ansi_colors) catch {}; // TODO:

                if (tag.cell.isStringLike()) {
                    if (comptime enable_ansi_colors) {
                        writer.writeAll(comptime Output.prettyFmt("<r>", true));
                    }
                }
            }
        };
    }

pub fn getFormatterTag(value: JSValue, globalThis: *JSGlobalObject): bun.JSError!Formatter.Tag.Result {
    return Formatter.Tag.getAdvanced(value, globalThis, { hide_global: false });
}

pub fn getAdvancedFormatterTag(value: JSValue, globalThis: *JSGlobalObject, opts: Formatter.Tag.Options): bun.JSError!Formatter.Tag.Result {

            switch (value) {
                .zero, .js_undefined => return Formatter.Tag.Result{
                    tag: .Undefined,
                },
                .null => return Formatter.Tag.Result{
                    tag: .Null,
                },
                else => {},
            }

            if (value.isInt32()) {
                return {
                    tag: .Integer,
                };
            } else if (value.isNumber()) {
                return {
                    tag: .Double,
                };
            } else if (value.isBoolean()) {
                return {
                    tag: .Boolean,
                };
            }

            if (!value.isCell())
                return {
                    tag: .NativeCode,
                };

            const js_type = value.jsType();

            if (js_type.isHidden()) return {
                tag: .NativeCode,
                cell: js_type,
            };

            if (js_type == .Cell) {
                return {
                    tag: .NativeCode,
                    cell: js_type,
                };
            }

            if (js_type.canGet() && js_type != .ProxyObject && !opts.disable_inspect_custom) {
                // Attempt to get custom formatter
                const inspect_custom_opt = value.fastGet(globalThis, .inspectCustom) catch { return { tag: .RevokedProxy }; };
                if (callback_value := inspect_custom_opt) {
                    if (callback_value.isCallable()) {
                        return {
                            tag: {
                                CustomFormattedObject: {
                                    function: callback_value,
                                    this: value,
                                },
                            },
                            cell: js_type,
                        };
                    }
                }
            }

            if (js_type == .DOMWrapper) {
                return {
                    tag: .Private,
                    cell: js_type,
                };
            }

            // If we check an Object has a method table and it does not
            // it will crash
            if (js_type != .Object && js_type != .ProxyObject && value.isCallable()) {
                if (value.isClass(globalThis)) {
                    return {
                        tag: .Class,
                        cell: js_type,
                    };
                }

                return { 
                    // TODO: we print InternalFunction as Object because we have a lot of
                    // callable namespaces and printing the contents of it is better than [Function: namespace]
                    // ideally, we would print [Function: namespace] { ... } on all functions, internal and js.
                    // what we'll do later is rid of .Function and .Class and handle the prefix in the .Object formatter
                    tag: if (js_type == .InternalFunction) .Object else .Function,
                    cell: js_type,
                };
            }

            if (js_type == .GlobalProxy) {
                if (!opts.hide_global) {
                    return Formatter.Tag.get(
                        jsc.JSValue.c(jsc.C.JSObjectGetProxyTarget(value.asObjectRef())),
                        globalThis,
                    );
                }
                return {
                    tag: .GlobalObject,
                    cell: js_type,
                };
            }

            // Is this a react element?
            if (js_type.isObject() && js_type != .ProxyObject) {
                if (typeof_symbol := value.getOwnTruthy(globalThis, "$$typeof")?) {
                    // React 18 and below
                    let react_element_legacy = ZigString.init("react.element");
                    // For React 19 - https://github.com/oven-sh/bun/issues/17223
                    let react_element_transitional = ZigString.init("react.transitional.element");
                    let react_fragment = ZigString.init("react.fragment");

                    if (typeof_symbol.isSameValue(.symbolFor(globalThis, &react_element_legacy), globalThis)? ||
                        typeof_symbol.isSameValue(.symbolFor(globalThis, &react_element_transitional), globalThis)? ||
                        typeof_symbol.isSameValue(.symbolFor(globalThis, &react_fragment), globalThis)?)
                    {
                        return { tag: .JSX, cell: js_type };
                    }
                }
            }

            return {
                tag: switch (js_type) {
                    .ErrorInstance => .Error,
                    .NumberObject => .Double,
                    .DerivedArray, .Array, .DirectArguments, .ScopedArguments, .ClonedArguments => .Array,
                    .DerivedStringObject, .String, .StringObject => .String,
                    .RegExpObject => .String,
                    .Symbol => .Symbol,
                    .BooleanObject => .Boolean,
                    .JSFunction => .Function,
                    .WeakMap, .Map => .Map,
                    .MapIterator => .MapIterator,
                    .SetIterator => .SetIterator,
                    .WeakSet, .Set => .Set,
                    .JSDate => .JSON,
                    .JSPromise => .Promise,

                    .WrapForValidIterator,
                    .RegExpStringIterator,
                    .JSArrayIterator,
                    .Iterator,
                    .IteratorHelper,
                    .Object,
                    .FinalObject,
                    .ModuleNamespaceObject => .Object,

                    .ProxyObject => brk: {
                        const handler = value.getProxyInternalField(.handler);
                        if (handler == .zero || handler.isUndefinedOrNull()) {
                            break :brk .RevokedProxy;
                        }
                        break :brk .Proxy;
                    },

                    .GlobalObject => if (!opts.hide_global)
                        .Object
                    else
                        .GlobalObject,

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
                    .DataView => .TypedArray,

                    .HeapBigInt => .BigInt,

                    // None of these should ever exist here
                    // But we're going to check anyway
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
                    .WithScope => .NativeCode,

                    .Event => .Event,

                    .GetterSetter => .GetterSetter,
                    .CustomGetterSetter => .CustomGetterSetter,

                    .JSAsJSONType => .toJSON,

                    else => .JSON,
                },
                cell: js_type,
            };
}

/// https://console.spec.whatwg.org/#formatter
enum FormatterPercentTag {
    s, // s
    i, // i or d
    f, // f
    o, // o
    O, // O
    c, // c
    j, // j
}

pub const FormatterTag = enum {
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
    MapIterator,
    SetIterator,
    Set,
    BigInt,
    Symbol,

    CustomFormattedObject,

    GlobalObject,
    Private,
    Promise,

    JSON,
    toJSON,
    NativeCode,

    JSX,
    Event,

    GetterSetter,
    CustomGetterSetter,

    Proxy,
    RevokedProxy,

    pub fn isPrimitive(this: FormatterTag): bool {
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

    pub fn canHaveCircularReferences(tag: FormatterTag): bool {
        return switch (tag) {
            .Function, .Array, .Object, .Map, .Set, .Error, .Class, .Event => true,
            else => false,
        };
    }

    const Result = struct {
        tag: union(FormatterTag) {
            StringPossiblyFormatted: void,
            String: void,
            Undefined: void,
            Double: void,
            Integer: void,
            Null: void,
            Boolean: void,
            Array: void,
            Object: void,
            Function: void,
            Class: void,
            Error: void,
            TypedArray: void,
            Map: void,
            MapIterator: void,
            SetIterator: void,
            Set: void,
            BigInt: void,
            Symbol: void,
            CustomFormattedObject: CustomFormattedObject,
            GlobalObject: void,
            Private: void,
            Promise: void,
            JSON: void,
            toJSON: void,
            NativeCode: void,
            JSX: void,
            Event: void,
            GetterSetter: void,
            CustomGetterSetter: void,
            Proxy: void,
            RevokedProxy: void,

            pub fn isPrimitive(this: @This()): bool {
                return @as(FormatterTag, this).isPrimitive();
            }

            pub fn tag(this: @This()): FormatterTag {
                return @as(FormatterTag, this);
            }
        },
        cell: JSValue.JSType = JSValue.JSType.Cell,
    };

    pub const Options = packed struct {
        hide_global: bool = false,
        disable_inspect_custom: bool = false,
    };

    pub const get = getFormatterTag;
    pub const getAdvanced = getAdvancedFormatterTag;
};

pub struct Formatter {
    globalThis: *JSGlobalObject,

    remaining_values: []const JSValue = [],
    map: Visited.Map = undefined,
    map_node: (*Visited.Pool.Node)? = null,
    hide_native: bool = false,
    indent: u32 = 0,
    depth: u16 = 0,
    max_depth: u16 = 8,
    quote_strings: bool = false,
    quote_keys: bool = false,
    failed: bool = false,
    estimated_line_length: usize = 0,
    always_newline_scope: bool = false,
    single_line: bool = false,
    ordered_properties: bool = false,
    custom_formatted_object: CustomFormattedObject = {},
    disable_inspect_custom: bool = false,
    stack_check: bun.StackCheck = { cached_stack_end: std.math.maxInt(usize) },
    can_throw_stack_overflow: bool = false,
    error_display_level: FormatOptions.ErrorDisplayLevel = .full,
    /// If ArrayBuffer-like objects contain ascii text, the buffer is printed as a string.
    /// Set true in the error printer so that ShellError prints a more readable message.
    format_buffer_as_text: bool = false,

    fn deinit(*_) {
        if (node := bun.take(&_.map_node)) {
            node.data = _.map;
            if (node.data.capacity() > 512) {
                node.data.clearAndFree();
            } else {
                node.data.clearRetainingCapacity();
            }
            node.release();
        }
    }

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
        _.estimated_line_length = _.estimated_line_length +| len;
    }

    pub const ZigFormatter = ConsoleZigFormatter;

    // For detecting circular references
    pub const Visited = struct {
        pub const Map = std.AutoHashMap(JSValue, void);
        pub const Pool = ObjectPool(
            Map,
            struct {
                static fn init(allocator: std.mem.Allocator): anyerror!Map {
                    return Map.init(allocator);
                }
            }.init,
            true,
            16,
        );
    };

    pub const Tag = FormatterTag;

    const CellType = jsc.C.CellType;
@tls let name_buf: [512]u8 = undefined;

    const PercentTag = FormatterPercentTag;

    pub fn writeWithFormatting(
        *_,
        comptime Writer: type,
        writer_: Writer,
        comptime Slice: type,
        slice_: Slice,
        global: *JSGlobalObject,
        comptime enable_ansi_colors: bool,
    ): bun.JSError!void {
        const Wrapped = WrappedWriter(Writer);
        let writer = Wrapped{
            ctx: writer_,
            estimated_line_length: &_.estimated_line_length,
        };
        let slice: []const u8 = @as([]const u8, slice_);
        let i: u32 = 0;
        let len: u32 = @as(u32, @truncate(slice.len));
        let hit_percent = false;
        while (i < len) : (i += 1) {
            if (hit_percent) {
                i = 0;
                hit_percent = false;
            }

            switch (slice[i]) {
                '%' => {
                    i += 1;
                    if (i >= len)
                        break;

                    if (_.remaining_values.len == 0)
                        break;

                    const token: PercentTag = switch (slice[i]) {
                        's' => .s,
                        'f' => .f,
                        'o' => .o,
                        'O' => .O,
                        'd', 'i' => .i,
                        'c' => .c,
                        'j' => .j,
                        '%' => {
                            // print up to and including the first %
                            const end = slice[0..i];
                            writer.writeAll(end);
                            // then skip the second % so we dont hit it again
                            slice = slice[@min(slice.len, i + 1)..];
                            len = @truncate(slice.len);
                            i = 0;
                            continue;
                        },
                        else => continue,
                    };

                    // Flush everything up to the %
                    const end = slice[0 .. i - 1];
                    writer.writeAll(end);
                    slice = slice[@min(slice.len, i + 1)..];
                    i = 0;
                    hit_percent = true;
                    len = @truncate(slice.len);
                    const next_value = _.remaining_values[0];
                    _.remaining_values = _.remaining_values[1..];

                    // https://console.spec.whatwg.org/#formatter
                    const max_before_e_notation = 1000000000000000000000.0;
                    const min_before_e_notation = 0.000001;
                    switch (token) {
                        .s => _.printAs(Tag.String, Writer, writer_, next_value, next_value.jsType(), enable_ansi_colors)?,
                        .i => {
                            // 1. If Type(current) is Symbol, let converted be NaN
                            // 2. Otherwise, let converted be the result of Call(%parseInt%, undefined, current, 10)
                            const int: i64 = brk: {
                                // This logic is convoluted because %parseInt% will coerce the argument to a string
                                // first. As an optimization, we can check if the argument is a number and
                                // skip such coercion.
                                if (next_value.isInt32()) {
                                    // Already an int, parseInt will parse to itself.
                                    break :brk next_value.asInt32();
                                }

                                if (next_value.isNumber() || !next_value.isSymbol()) double_convert: {
                                    let value = next_value.toNumber(global)?;

                                    if (!std.math.isFinite(value)) {
                                        // for NaN and the string Infinity and -Infinity, parseInt returns NaN
                                        break :double_convert;
                                    }

                                    // simulate parseInt, which converts the argument to a string and
                                    // then back to a number, without converting it to a string
                                    if (value == 0) {
                                        break :brk 0;
                                    }

                                    const sign: i64 = if (value < 0) -1 else 1;
                                    value = @abs(value);
                                    if (value >= max_before_e_notation) {
                                        // toString prints 1.000+e0, which parseInt will stop at
                                        // the '.' or the '+', this gives us a single digit value.
                                        while (value >= 10) value /= 10;
                                        break :brk @as(i64, @intFromFloat(@floor(value))) * sign;
                                    } else if (value < min_before_e_notation) {
                                        // toString prints 1.000-e0, which parseInt will stop at
                                        // the '.' or the '-', this gives us a single digit value.
                                        while (value < 1) value *= 10;
                                        break :brk @as(i64, @intFromFloat(@floor(value))) * sign;
                                    }

                                    // parsing stops at '.', so this is equal to @floor
                                    break :brk @as(i64, @intFromFloat(@floor(value))) * sign;
                                }

                                // for NaN and the string Infinity and -Infinity, parseInt returns NaN
                                _.addForNewLine("NaN".len);
                                writer.print("NaN", .{});
                                continue;
                            };

                            if (int < std.math.maxInt(u32)) {
                                const is_negative = int < 0;
                                const digits = if (i != 0)
                                    bun.fmt.fastDigitCount(@as(u64, i32(@abs(int)))) + @as(u64, @intFromBool(is_negative))
                                else
                                    1;
                                _.addForNewLine(digits);
                            } else {
                                _.addForNewLine(std.fmt.count("{d}", [ int]));
                            }
                            writer.print("{d}", [ int]);
                        },

                        .f => {
                            // 1. If Type(current) is Symbol, let converted be NaN
                            // 2. Otherwise, let converted be the result of Call(%parseFloat%, undefined, [current]).
                            const converted: f64 = brk: {
                                if (next_value.isInt32()) {
                                    const int = next_value.asInt32();
                                    const is_negative = int < 0;
                                    const digits = if (i != 0)
                                        bun.fmt.fastDigitCount(@as(u64, i32(@abs(int)))) + @as(u64, @intFromBool(is_negative))
                                    else
                                        1;
                                    _.addForNewLine(digits);
                                    writer.print("{d}", [ int]);
                                    continue;
                                }
                                if (next_value.isNumber()) {
                                    break :brk next_value.asNumber();
                                }
                                if (next_value.isSymbol()) {
                                    break :brk std.math.nan(f64);
                                }
                                // TODO: this is not perfectly emulating parseFloat,
                                // because spec says to convert the value to a string
                                // and then parse as a number, but we are just coercing
                                // a number.
                                break :brk next_value.toNumber(global)?;
                            };

                            const abs = @abs(converted);
                            if (abs < max_before_e_notation && abs >= min_before_e_notation) {
                                _.addForNewLine(std.fmt.count("{d}", [ converted]));
                                writer.print("{d}", [ converted]);
                            } else if (std.math.isNan(converted)) {
                                _.addForNewLine("NaN".len);
                                writer.writeAll("NaN");
                            } else if (std.math.isInf(converted)) {
                                _.addForNewLine("Infinity".len + @as(usize, @intFromBool(converted < 0)));
                                if (converted < 0) {
                                    writer.writeAll("-");
                                }
                                writer.writeAll("Infinity");
                            } else {
                                let buf: [124]u8 = undefined;
                                const formatted = bun.fmt.FormatDouble.dtoa(&buf, converted);
                                _.addForNewLine(formatted.len);
                                writer.print("{s}", [ formatted]);
                            }
                        },

                        inline .o, .O => |t| {
                            if (t == .o) {
                                // TODO: Node.js applies the following extra formatter options.
                                //
                                // this.max_depth = 4;
                                // this.show_proxy = true;
                                // this.show_hidden = true;
                                //
                                // Spec defines %o as:
                                // > An object with optimally useful formatting is an
                                // > implementation-specific, potentially-interactive representation
                                // > of an object judged to be maximally useful and informative.
                            }
                            _.format(Tag.get(next_value, global)?, Writer, writer_, next_value, global, enable_ansi_colors)?;
                        },

                        .c => {
                            // TODO: Implement %c
                        },

                        .j => {
                            // JSON.stringify the value using FastStringifier for SIMD optimization
                            let str_g = DerefGuard<bun.String>{ val: bun.String.empty };

                            next_value.jsonStringifyFast(global, &str_g.val)?;
                            _.addForNewLine(str_g.val.length());
                            writer.print("{f}", [ str_g.val]);
                        },
                    }
                    if (_.remaining_values.len == 0) break;
                },
                else => {},
            }
        }

        if (slice.len > 0) writer.writeAll(slice);
    }

    static fn WrappedWriter(comptime Writer: type): type {
        if (Writer != *std.Io.Writer && @hasDecl(Writer, "is_wrapped_writer")) {
            @compileError("Do not nest WrappedWriter");
        }

        return struct {
            ctx: Writer,
            failed: bool = false,
            estimated_line_length: *usize,

            pub const is_wrapped_writer = true;

            pub fn print(self: *@This(), comptime fmt: string, args: auto) {
                self.ctx.print(fmt, args) catch {
                    self.failed = true;
                };
            }

            pub fn space(self: *@This()) {
                self.estimated_line_length.* += 1;
                self.ctx.writeAll(" ") catch {
                    self.failed = true;
                };
            }

            pub fn pretty(self: *@This(), comptime fmt: string, comptime enable_ansi_color: bool, args: auto) {
                const length_ignoring_formatted_values = comptime brk: {
                    const fmt_str = Output.prettyFmt(fmt, false);
                    let length: usize = 0;
                    let i: usize = 0;
                    while (i < fmt_str.len) : (i += 1) {
                        switch (fmt_str[i]) {
                            '{' => {
                                i += 1;
                                if (i >= fmt_str.len)
                                    break;
                                if (fmt_str[i] == '{') {
                                    @compileError("Format string too complicated for pretty() right now");
                                }

                                while (i < fmt_str.len) : (i += 1) {
                                    if (fmt_str[i] == '}') {
                                        break;
                                    }
                                }
                                if (i >= fmt_str.len)
                                    break;
                            },
                            128..=255 => {
                                @compileError("Format string too complicated for pretty() right now");
                            },
                            else => {},
                        }
                        length += 1;
                    }

                    break :brk length;
                };

                self.estimated_line_length.* += length_ignoring_formatted_values;
                self.ctx.print(comptime Output.prettyFmt(fmt, enable_ansi_color), args) catch {
                    self.failed = true;
                };
            }

            pub fn writeLatin1(self: *@This(), buf: []const u8) {
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

                self.ctx.writeAll(remain) catch { return; };
            }

            pub inline fn writeAll(self: *@This(), buf: []const u8) {
                self.ctx.writeAll(buf) catch {
                    self.failed = true;
                };
            }

            pub inline fn writeString(self: *@This(), str: ZigString) {
                self.print("{f}", [ str]);
            }

            pub inline fn write16Bit(self: *@This(), input: []const u16) {
                bun.fmt.formatUTF16Type(input, self.ctx) catch {
                    self.failed = true;
                };
            }
        };
    }

    const indentation_buf = [_]u8{' '} ** 64;
    pub fn writeIndent(
        *_,
        comptime Writer: type,
        writer: Writer,
    )! {
        let total_remain: u32 = _.indent;
        while (total_remain > 0) {
            const written: u8 = @min(32, total_remain);
            writer.writeAll(indentation_buf[0 .. written * 2])?;
            total_remain = total_remain -| written;
        }
    }

    fn printComma(*_, comptime Writer: type, writer: Writer, comptime enable_ansi_colors: bool)! {
        writer.writeAll(comptime Output.prettyFmt("<r><d>,<r>", enable_ansi_colors))?;
        _.estimated_line_length += 1;
    }

    static fn MapIterator(comptime Writer: type, comptime enable_ansi_colors: bool, comptime is_iterator: bool, comptime single_line: bool): type {
        return struct {
            formatter: *ConsoleObject.Formatter,
            writer: Writer,
            count: usize = 0,
            @callconv("c")
            static fn forEach(_: *jsc.VM, globalObject: *JSGlobalObject, ctx: *!?, nextValue: JSValue) {
                if (ctx == null) return;
                let it: *@This() = unsafe { @ptrCast(@alignCast(ctx.?)) };
                if (it.formatter.failed) return;
                if (single_line && it.count > 0) {
                    it.formatter.printComma(Writer, it.writer, enable_ansi_colors) catch unreachable;
                    it.writer.writeAll(" ") catch unreachable;
                }
                if (!is_iterator) {
                    const key = nextValue.getIndex(globalObject, 0) catch { return; };
                    const value = nextValue.getIndex(globalObject, 1) catch { return; };

                    if (!single_line) {
                        it.formatter.writeIndent(Writer, it.writer) catch unreachable;
                    }
                    const key_tag = Tag.getAdvanced(key, globalObject, {
                        hide_global: true,
                        disable_inspect_custom: it.formatter.disable_inspect_custom,
                    }) catch { return; };

                    it.formatter.format(
                        key_tag,
                        Writer,
                        it.writer,
                        key,
                        it.formatter.globalThis,
                        enable_ansi_colors,
                    ) catch {}; // TODO:
                    it.writer.writeAll(": ") catch unreachable;
                    const value_tag = Tag.getAdvanced(value, globalObject, {
                        hide_global: true,
                        disable_inspect_custom: it.formatter.disable_inspect_custom,
                    }) catch { return; };
                    it.formatter.format(
                        value_tag,
                        Writer,
                        it.writer,
                        value,
                        it.formatter.globalThis,
                        enable_ansi_colors,
                    ) catch {}; // TODO:
                } else {
                    if (!single_line) {
                        it.writer.writeAll("\n") catch unreachable;
                        it.formatter.writeIndent(Writer, it.writer) catch unreachable;
                    }
                    const tag = Tag.getAdvanced(nextValue, globalObject, {
                        hide_global: true,
                        disable_inspect_custom: it.formatter.disable_inspect_custom,
                    }) catch { return; };
                    it.formatter.format(
                        tag,
                        Writer,
                        it.writer,
                        nextValue,
                        it.formatter.globalThis,
                        enable_ansi_colors,
                    ) catch {}; // TODO:
                }
                it.count += 1;
                if (!single_line) {
                    it.formatter.printComma(Writer, it.writer, enable_ansi_colors) catch unreachable;
                    if (!is_iterator) {
                        it.writer.writeAll("\n") catch unreachable;
                    }
                }
            }
        };
    }

    static fn SetIterator(comptime Writer: type, comptime enable_ansi_colors: bool, comptime single_line: bool): type {
        return struct {
            formatter: *ConsoleObject.Formatter,
            writer: Writer,
            is_first: bool = true,
            @callconv("c")
            static fn forEach(_: *jsc.VM, globalObject: *JSGlobalObject, ctx: *!?, nextValue: JSValue) {
                if (ctx == null) return;
                let it: *@This() = unsafe { @ptrCast(@alignCast(ctx.?)) };
                if (it.formatter.failed) return;
                if (single_line) {
                    if (!it.is_first) {
                        it.formatter.printComma(Writer, it.writer, enable_ansi_colors) catch unreachable;
                        it.writer.writeAll(" ") catch unreachable;
                    }
                    it.is_first = false;
                } else {
                    it.formatter.writeIndent(Writer, it.writer) catch {};
                }
                const key_tag = Tag.getAdvanced(nextValue, globalObject, {
                    hide_global: true,
                    disable_inspect_custom: it.formatter.disable_inspect_custom,
                }) catch { return; };
                it.formatter.format(
                    key_tag,
                    Writer,
                    it.writer,
                    nextValue,
                    it.formatter.globalThis,
                    enable_ansi_colors,
                ) catch {}; // TODO:

                if (!single_line) {
                    it.formatter.printComma(Writer, it.writer, enable_ansi_colors) catch unreachable;
                    it.writer.writeAll("\n") catch unreachable;
                }
            }
        };
    }

    pub const PropertyIterator = FormatterPropertyIterator;

    static fn getObjectName(globalThis: *jsc.JSGlobalObject, value: JSValue): bun.JSError!ZigString? {
        let name_str = ZigString.init("");
        value.getClassName(globalThis, &name_str)?;
        if (!name_str.eqlComptime("Object")) {
            return name_str;
        } else if (value.getPrototype(globalThis).eqlValue(JSValue.null)) {
            return ZigString.static("[Object: null prototype]").*;
        }
        return null;
    }

    extern fn JSC__JSValue__callCustomInspectFunction(
        global: *jsc.JSGlobalObject,
        function: JSValue,
        this_val: JSValue,
        depth: u32,
        max_depth: u32,
        colors: bool,
    ): JSValue;

    pub fn printAs(
        *_,
        comptime Format: ConsoleObject.Formatter.Tag,
        comptime Writer: type,
        writer_: Writer,
        value: JSValue,
        jsType: JSValue.JSType,
        comptime enable_ansi_colors: bool,
    ): bun.JSError!void {
        if (_.failed)
            return;
        if (_.globalThis.hasException()) {
            return error.JSError;
        }

        // If we call
        //   `return try this.printAs`
        //
        // Then we can get a spurious `[Circular]` due to the value already being present in the map.
        let remove_before_recurse = false;

        const Wrapped = WrappedWriter(Writer);
        let writer = Wrapped{ ctx: writer_, estimated_line_length: &_.estimated_line_length };
        const _wf_g = WriterFailedGuard<@TypeOf(&writer)>{ formatter: _, writer: unsafe { &writer } };
        if (comptime Format.canHaveCircularReferences()) {
            if (!_.stack_check.isSafeToRecurse()) {
                _.failed = true;
                if (_.can_throw_stack_overflow) {
                    return _.globalThis.throwStackOverflow();
                }
                return;
            }

            if (_.map_node == null) {
                _.map_node = Visited.Pool.get(default_allocator);
                _.map_node.?.data.clearRetainingCapacity();
                _.map = _.map_node.?.data;
            }

            const entry = _.map.getOrPut(value) catch unreachable;
            if (entry.found_existing) {
                writer.writeAll(comptime Output.prettyFmt("<r><cyan>[Circular]<r>", enable_ansi_colors));
                return;
            } else {
                remove_before_recurse = true;
            }
        }

        let _map_rem_g = MapRemoveGuard{ formatter: _, value: value, active: Format.canHaveCircularReferences() && remove_before_recurse };

        switch (comptime Format) {
            .StringPossiblyFormatted => {
                let str = value.toSlice(_.globalThis, bun.default_allocator)?;
                const _str_g = DeinitGuard<@TypeOf(str)>{ val: str };
                _.addForNewLine(str.len);
                const slice = str.slice();
                _.writeWithFormatting(Writer, writer_, @TypeOf(slice), slice, _.globalThis, enable_ansi_colors)?;
            },
            .String => {
                // This is called from the '%s' formatter, so it can actually be any value
                const str: bun.String = bun.String.fromJS(value, _.globalThis)?;
                const _str_g = DerefGuard<bun.String>{ val: str };
                _.addForNewLine(str.length());

                if (_.quote_strings && jsType != .RegExpObject) {
                    if (str.isEmpty()) {
                        writer.writeAll("\"\"");
                        return;
                    }

                    if (comptime enable_ansi_colors) {
                        writer.writeAll(Output.prettyFmt("<r><green>", true));
                    }

                    const _ansi_g = AnsiResetGuard<*Wrapped>{ writer: &writer, enabled: enable_ansi_colors };

                    if (str.isUTF16()) {
                        _.printAs(.JSON, Writer, writer_, value, .StringObject, enable_ansi_colors)?;
                        return;
                    }

                    JSPrinter.writeJSONString(str.latin1(), Writer, writer_, .latin1) catch unreachable;

                    return;
                }

                if (jsType == .StringObject) {
                    if (enable_ansi_colors) {
                        writer.print(comptime Output.prettyFmt("<r><green>", enable_ansi_colors), .{});
                    }
                    const _ansi_g = AnsiResetGuard<*Wrapped>{ writer: &writer, enabled: enable_ansi_colors };

                    writer.print("[String: ", .{});

                    if (str.isUTF16()) {
                        _.printAs(.JSON, Writer, writer_, value, .StringObject, enable_ansi_colors)?;
                    } else {
                        JSPrinter.writeJSONString(str.latin1(), Writer, writer_, .latin1) catch unreachable;
                    }

                    writer.print("]", .{});
                    return;
                }

                if (jsType == .RegExpObject && enable_ansi_colors) {
                    writer.print(comptime Output.prettyFmt("<r><red>", enable_ansi_colors), .{});
                }

                if (str.isUTF16()) {
                    // streaming print
                    writer.print("{f}", [ str]);
                } else if (slice := str.asUTF8()) {
                    // fast path
                    writer.writeAll(slice);
                } else if (!str.isEmpty()) {
                    // slow path
                    const buf = strings.allocateLatin1IntoUTF8(bun.default_allocator, str.latin1()) catch "";
                    if (buf.len > 0) {
                        const _buf_g = FreeGuard<@TypeOf(buf)>{ alloc: bun.default_allocator, ptr: buf };
                        writer.writeAll(buf);
                    }
                }

                if (jsType == .RegExpObject && enable_ansi_colors) {
                    writer.print(comptime Output.prettyFmt("<r>", enable_ansi_colors), .{});
                }
            },
            .Integer => {
                const int = value.coerce(i64, _.globalThis)?;
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
                    _.addForNewLine(std.fmt.count("{d}", [ int]));
                }
                writer.print(comptime Output.prettyFmt("<r><yellow>{d}<r>", enable_ansi_colors), [ int]);
            },
            .BigInt => {
                const zig_str = value.getZigString(_.globalThis)?;
                const out_str = zig_str.slice();
                _.addForNewLine(out_str.len);

                writer.print(comptime Output.prettyFmt("<r><yellow>{s}n<r>", enable_ansi_colors), [ out_str]);
            },
            .Double => {
                if (value.isCell()) {
                    let number_name = ZigString.Empty;
                    value.getClassName(_.globalThis, &number_name)?;

                    let number_value = ZigString.Empty;
                    value.toZigString(&number_value, _.globalThis)?;

                    if (!strings.eqlComptime(number_name.slice(), "Number")) {
                        _.addForNewLine(number_name.len + number_value.len + "[Number ():]".len);
                        writer.print(comptime Output.prettyFmt("<r><yellow>[Number ({f}): {f}]<r>", enable_ansi_colors), { 
                            number_name,
                            number_value,
                        });
                        return;
                    }

                    _.addForNewLine(number_name.len + number_value.len + 4);
                    writer.print(comptime Output.prettyFmt("<r><yellow>[{f}: {f}]<r>", enable_ansi_colors), { 
                        number_name,
                        number_value,
                    });
                    return;
                }

                const num = value.asNumber();

                if (std.math.isPositiveInf(num)) {
                    _.addForNewLine("Infinity".len);
                    writer.print(comptime Output.prettyFmt("<r><yellow>Infinity<r>", enable_ansi_colors), .{});
                } else if (std.math.isNegativeInf(num)) {
                    _.addForNewLine("-Infinity".len);
                    writer.print(comptime Output.prettyFmt("<r><yellow>-Infinity<r>", enable_ansi_colors), .{});
                } else if (std.math.isNan(num)) {
                    _.addForNewLine("NaN".len);
                    writer.print(comptime Output.prettyFmt("<r><yellow>NaN<r>", enable_ansi_colors), .{});
                } else {
                    let buf: [124]u8 = undefined;
                    const formatted = bun.fmt.FormatDouble.dtoaWithNegativeZero(&buf, num);
                    _.addForNewLine(formatted.len);
                    writer.print(comptime Output.prettyFmt("<r><yellow>{s}<r>", enable_ansi_colors), [ formatted]);
                }
            },
            .Undefined => {
                _.addForNewLine(9);
                writer.print(comptime Output.prettyFmt("<r><d>undefined<r>", enable_ansi_colors), .{});
            },
            .Null => {
                _.addForNewLine(4);
                writer.print(comptime Output.prettyFmt("<r><yellow>null<r>", enable_ansi_colors), .{});
            },
            .CustomFormattedObject => {
                // Call custom inspect function. Will return the error if there is one
                // we'll need to pass the callback through to the "this" value in here
                const result = bun.jsc.fromJSHostCall(_.globalThis, @src(), JSC__JSValue__callCustomInspectFunction, [
                    _.globalThis,
                    _.custom_formatted_object.function,
                    _.custom_formatted_object.@"this",
                    _.max_depth -| _.depth,
                    _.max_depth,
                    enable_ansi_colors,
                ])?;
                // Strings are printed directly, otherwise we recurse. It is possible to end up in an infinite loop.
                if (result.isString()) {
                    writer.print("{f}", [ result.fmtString(_.globalThis)]);
                } else {
                    _.format(ConsoleObject.Formatter.Tag.get(result, _.globalThis)?, Writer, writer_, result, _.globalThis, enable_ansi_colors)?;
                }
            },
            .Symbol => {
                const description = value.getDescription(_.globalThis);
                _.addForNewLine("Symbol".len);

                if (description.len > 0) {
                    _.addForNewLine(description.len + "()".len);
                    writer.print(comptime Output.prettyFmt("<r><blue>Symbol({f})<r>", enable_ansi_colors), [ description]);
                } else {
                    writer.print(comptime Output.prettyFmt("<r><blue>Symbol()<r>", enable_ansi_colors), .{});
                }
            },
            .Error => {
                // Temporarily remove from the visited map to allow printErrorlikeObject to process it
                // The circular reference check is already done in printAs, so we know it's safe
                const was_in_map = if (_.map_node != null) _.map.remove(value) else false;
                const _reinsert_g = MapReinsertGuard{ formatter: _, value: value, was_in_map: was_in_map };

                VirtualMachine.get().printErrorlikeObject(
                    value,
                    null,
                    null,
                    _,
                    Writer,
                    writer_,
                    enable_ansi_colors,
                    false,
                );
            },
            .Class => {
                // Prefer the constructor's own `.name` property over
                // `getClassName` / `calculatedClassName`. For DOM / WebCore
                // InternalFunction constructors like `ReadableStreamBYOBReader`,
                // `calculatedClassName` walks the prototype chain and hits
                // `Function.prototype.constructor === Function`, returning
                // "Function". The `.name` property is set to the real class
                // name on the constructor itself. See #29225.
                let printable = value.getName(_.globalThis)?;
                const _print_g = DerefGuard<bun.String>{ val: printable };
                _.addForNewLine(printable.length());

                // Only report `extends` when the parent is itself a class
                // (i.e. `class Foo extends Bar`). Built-in and DOM constructors
                // have `Function.prototype` as their prototype, which would
                // render as `[class X extends Function]` and is noise.
                const proto = value.getPrototype(_.globalThis);
                const proto_is_class = !proto.isEmptyOrUndefinedOrNull() && proto.isCell() && proto.isClass(_.globalThis);
                let printable_proto: bun.String = if (proto_is_class) proto.getName(_.globalThis)? else bun.String.empty;
                const _proto_g = DerefGuard<bun.String>{ val: printable_proto };
                _.addForNewLine(printable_proto.length());

                if (printable.isEmpty()) {
                    if (printable_proto.isEmpty()) {
                        writer.print(comptime Output.prettyFmt("<cyan>[class (anonymous)]<r>", enable_ansi_colors), .{});
                    } else {
                        writer.print(comptime Output.prettyFmt("<cyan>[class (anonymous) extends {f}]<r>", enable_ansi_colors), [ printable_proto]);
                    }
                } else {
                    if (printable_proto.isEmpty()) {
                        writer.print(comptime Output.prettyFmt("<cyan>[class {f}]<r>", enable_ansi_colors), [ printable]);
                    } else {
                        writer.print(comptime Output.prettyFmt("<cyan>[class {f} extends {f}]<r>", enable_ansi_colors), { printable, printable_proto });
                    }
                }
            },
            .Function => {
                let printable = value.getName(_.globalThis)?;
                const _print_g = DerefGuard<bun.String>{ val: printable };

                const proto = value.getPrototype(_.globalThis);
                const func_name = proto.getName(_.globalThis)?; // "Function" | "AsyncFunction" | "GeneratorFunction" | "AsyncGeneratorFunction"
                const _fn_g = DerefGuard<bun.String>{ val: func_name };

                if (printable.isEmpty() || func_name.eql(printable)) {
                    if (func_name.isEmpty()) {
                        writer.print(comptime Output.prettyFmt("<cyan>[Function]<r>", enable_ansi_colors), .{});
                    } else {
                        writer.print(comptime Output.prettyFmt("<cyan>[{f}]<r>", enable_ansi_colors), [ func_name]);
                    }
                } else {
                    if (func_name.isEmpty()) {
                        writer.print(comptime Output.prettyFmt("<cyan>[Function: {f}]<r>", enable_ansi_colors), [ printable]);
                    } else {
                        writer.print(comptime Output.prettyFmt("<cyan>[{f}: {f}]<r>", enable_ansi_colors), { func_name, printable });
                    }
                }
            },
            .GetterSetter => {
                const cell = value.asCell();
                const getterSetter = cell.getGetterSetter();
                const hasGetter = !getterSetter.isGetterNull();
                const hasSetter = !getterSetter.isSetterNull();
                if (hasGetter && hasSetter) {
                    writer.print(comptime Output.prettyFmt("<cyan>[Getter/Setter]<r>", enable_ansi_colors), .{});
                } else if (hasGetter) {
                    writer.print(comptime Output.prettyFmt("<cyan>[Getter]<r>", enable_ansi_colors), .{});
                } else if (hasSetter) {
                    writer.print(comptime Output.prettyFmt("<cyan>[Setter]<r>", enable_ansi_colors), .{});
                }
            },
            .CustomGetterSetter => {
                const cell = value.asCell();
                const getterSetter = cell.getCustomGetterSetter();
                const hasGetter = !getterSetter.isGetterNull();
                const hasSetter = !getterSetter.isSetterNull();
                if (hasGetter && hasSetter) {
                    writer.print(comptime Output.prettyFmt("<cyan>[Getter/Setter]<r>", enable_ansi_colors), .{});
                } else if (hasGetter) {
                    writer.print(comptime Output.prettyFmt("<cyan>[Getter]<r>", enable_ansi_colors), .{});
                } else if (hasSetter) {
                    writer.print(comptime Output.prettyFmt("<cyan>[Setter]<r>", enable_ansi_colors), .{});
                }
            },
            .Array => {
                const len = value.getLength(_.globalThis)?;

                // TODO: DerivedArray does not get passed along in JSType, and it's not clear why.
                // if (jsType == .DerivedArray) {
                //     var printable = value.className(this.globalThis);

                //     if (!printable.isEmpty()) {
                //         writer.print(comptime Output.prettyFmt("{}<r><d>(<r><yellow><r>{d}<r><d>)<r> ", enable_ansi_colors), .{ printable, len });
                //     }
                // }

                if (len == 0) {
                    writer.writeAll("[]");
                    _.addForNewLine(2);
                    return;
                }

                let was_good_time = _.always_newline_scope ||
                    // heuristic: more than 10, probably should have a newline before it
                    len > 10;
                {
                    _.indent += 1;
                    _.depth += 1;
                    const _id_g = IndentDepthGuard{ fmt: _ };

                    _.addForNewLine(2);

                    const _qs_g = RestoreQuoteStringsGuard{ fmt: _, prev: _.quote_strings };
                    _.quote_strings = true;
                    let empty_start: u32? = null;
                    first: {
                        const element = value.getDirectIndex(_.globalThis, 0);

                        const tag = Tag.getAdvanced(element, _.globalThis, {
                            hide_global: true,
                            disable_inspect_custom: _.disable_inspect_custom,
                        })?;

                        was_good_time = was_good_time || !tag.tag.isPrimitive() || _.goodTimeForANewLine();

                        if (!_.single_line && (_.ordered_properties || was_good_time)) {
                            _.resetLine();
                            writer.writeAll("[");
                            writer.writeAll("\n");
                            _.writeIndent(Writer, writer_) catch unreachable;
                            _.addForNewLine(1);
                        } else {
                            writer.writeAll("[ ");
                            _.addForNewLine(2);
                        }

                        if (element == .zero) {
                            empty_start = 0;
                            break :first;
                        }

                        _.format(tag, Writer, writer_, element, _.globalThis, enable_ansi_colors)?;

                        if (tag.cell.isStringLike()) {
                            if (comptime enable_ansi_colors) {
                                writer.writeAll(comptime Output.prettyFmt("<r>", true));
                            }
                        }
                    }

                    let i: u32 = 1;
                    let nonempty_count: u32 = 1;

                    while (i < len) : (i += 1) {
                        const element = value.getDirectIndex(_.globalThis, i);
                        if (element == .zero) {
                            if (empty_start == null) {
                                empty_start = i;
                            }
                            continue;
                        }
                        if (nonempty_count >= 100) {
                            _.printComma(Writer, writer_, enable_ansi_colors) catch unreachable;
                            writer.writeAll("\n"); // we want the line break to be unconditional here
                            _.estimated_line_length = 0;
                            _.writeIndent(Writer, writer_) catch unreachable;
                            writer.pretty("<r><d>... {d} more items<r>", enable_ansi_colors, [ len - i]);
                            break;
                        }
                        nonempty_count += 1;

                        if (empty := empty_start) {
                            if (empty > 0) {
                                _.printComma(Writer, writer_, enable_ansi_colors) catch unreachable;
                                if (!_.single_line && (_.ordered_properties || _.goodTimeForANewLine())) {
                                    was_good_time = true;
                                    writer.writeAll("\n");
                                    _.writeIndent(Writer, writer_) catch unreachable;
                                } else {
                                    writer.space();
                                }
                            }
                            const empty_count = i - empty;
                            if (empty_count == 1) {
                                writer.pretty("<r><d>empty item<r>", enable_ansi_colors, .{});
                            } else {
                                _.estimated_line_length += bun.fmt.fastDigitCount(empty_count);
                                writer.pretty("<r><d>{d} x empty items<r>", enable_ansi_colors, [ empty_count]);
                            }
                            empty_start = null;
                        }

                        _.printComma(Writer, writer_, enable_ansi_colors) catch unreachable;
                        if (!_.single_line && (_.ordered_properties || _.goodTimeForANewLine())) {
                            writer.writeAll("\n");
                            was_good_time = true;
                            _.writeIndent(Writer, writer_) catch unreachable;
                        } else {
                            writer.space();
                        }

                        const tag = Tag.getAdvanced(element, _.globalThis, {
                            hide_global: true,
                            disable_inspect_custom: _.disable_inspect_custom,
                        })?;

                        _.format(tag, Writer, writer_, element, _.globalThis, enable_ansi_colors)?;

                        if (tag.cell.isStringLike()) {
                            if (comptime enable_ansi_colors) {
                                writer.writeAll(comptime Output.prettyFmt("<r>", true));
                            }
                        }
                    }

                    if (empty := empty_start) {
                        if (empty > 0) {
                            _.printComma(Writer, writer_, enable_ansi_colors) catch unreachable;
                            if (!_.single_line && (_.ordered_properties || _.goodTimeForANewLine())) {
                                writer.writeAll("\n");
                                was_good_time = true;
                                _.writeIndent(Writer, writer_) catch unreachable;
                            } else {
                                writer.space();
                            }
                        }

                        empty_start = null;

                        const empty_count = len - empty;
                        if (empty_count == 1) {
                            writer.pretty("<r><d>empty item<r>", enable_ansi_colors, .{});
                        } else {
                            _.estimated_line_length += bun.fmt.fastDigitCount(empty_count);
                            writer.pretty("<r><d>{d} x empty items<r>", enable_ansi_colors, [ empty_count]);
                        }
                    }

                    if (!jsType.isArguments()) {
                        const Iterator = PropertyIterator(Writer, enable_ansi_colors);
                        let iter = Iterator{
                            formatter: _,
                            writer: writer_,
                            always_newline: !_.single_line && (_.always_newline_scope || _.goodTimeForANewLine()),
                            single_line: _.single_line,
                            parent: value,
                            i: i,
                        };
                        value.forEachPropertyNonIndexed(_.globalThis, &iter, Iterator.forEach)?;
                        if (_.failed) return;
                    }
                }

                if (!_.single_line && (_.ordered_properties || was_good_time || _.goodTimeForANewLine())) {
                    _.resetLine();
                    writer.writeAll("\n");
                    _.writeIndent(Writer, writer_) catch {};
                    writer.writeAll("]");
                    _.resetLine();
                    _.addForNewLine(1);
                } else {
                    writer.writeAll(" ]");
                    _.addForNewLine(2);
                }
            },
            .Private => {
                if (response := value.@"as"(jsc.WebCore.Response)) {
                    response.writeFormat(ConsoleObject.Formatter, _, writer_, enable_ansi_colors) catch {};
                    return;
                } else if (request := value.@"as"(jsc.WebCore.Request)) {
                    request.writeFormat(value, ConsoleObject.Formatter, _, writer_, enable_ansi_colors) catch {};
                    return;
                } else if (build := value.@"as"(jsc.API.BuildArtifact)) {
                    build.writeFormat(ConsoleObject.Formatter, _, writer_, enable_ansi_colors) catch {};
                    return;
                } else if (blob := value.@"as"(jsc.WebCore.Blob)) {
                    blob.writeFormat(ConsoleObject.Formatter, _, writer_, enable_ansi_colors) catch {};
                    return;
                } else if (s3client := value.@"as"(jsc.WebCore.S3Client)) {
                    s3client.writeFormat(ConsoleObject.Formatter, _, writer_, enable_ansi_colors) catch {};
                    return;
                } else if (archive := value.@"as"(jsc.API.Archive)) {
                    archive.writeFormat(ConsoleObject.Formatter, _, writer_, enable_ansi_colors) catch {};
                    return;
                } else if (value.@"as"(bun.webcore.FetchHeaders) != null) {
                    if (toJSONFunction := value.get(_.globalThis, "toJSON")?) {
                        _.addForNewLine("Headers ".len);
                        writer.writeAll(comptime Output.prettyFmt("<r>Headers ", enable_ansi_colors));
                        const _qk_g = RestoreQuoteKeysGuard{ fmt: _, prev: _.quote_keys };
                        _.quote_keys = true;

                        return _.printAs(
                            .Object,
                            Writer,
                            writer_,
                            toJSONFunction.call(_.globalThis, value, []) catch |e|
                                _.globalThis.takeException(e),
                            .Object,
                            enable_ansi_colors,
                        )?;
                    }
                } else if (value.@"as"(jsc.DOMFormData) != null) {
                    if (toJSONFunction := value.get(_.globalThis, "toJSON")?) {
                        const _qk_g = RestoreQuoteKeysGuard{ fmt: _, prev: _.quote_keys };
                        _.quote_keys = true;

                        return _.printAs(
                            .Object,
                            Writer,
                            writer_,
                            toJSONFunction.call(_.globalThis, value, []) catch |e|
                                _.globalThis.takeException(e),
                            .Object,
                            enable_ansi_colors,
                        )?;
                    }

                    // this case should never happen
                    return _.printAs(.Undefined, Writer, writer_, .js_undefined, .Cell, enable_ansi_colors)?;
                } else if (timer := value.@"as"(bun.api.Timer.TimeoutObject)) {
                    _.addForNewLine("Timeout(# ) ".len + bun.fmt.fastDigitCount(@as(u64, i32(@max(timer.internals.id, 0)))));
                    if (timer.internals.flags.kind == .setInterval) {
                        _.addForNewLine("repeats ".len + bun.fmt.fastDigitCount(@as(u64, i32(@max(timer.internals.id, 0)))));
                        writer.print(comptime Output.prettyFmt("<r><blue>Timeout<r> <d>(#<yellow>{d}<r><d>, repeats)<r>", enable_ansi_colors), [ 
                            timer.internals.id,
                        ]);
                    } else {
                        writer.print(comptime Output.prettyFmt("<r><blue>Timeout<r> <d>(#<yellow>{d}<r><d>)<r>", enable_ansi_colors), [ 
                            timer.internals.id,
                        ]);
                    }

                    return;
                } else if (immediate := value.@"as"(bun.api.Timer.ImmediateObject)) {
                    _.addForNewLine("Immediate(# ) ".len + bun.fmt.fastDigitCount(@as(u64, i32(@max(immediate.internals.id, 0)))));
                    writer.print(comptime Output.prettyFmt("<r><blue>Immediate<r> <d>(#<yellow>{d}<r><d>)<r>", enable_ansi_colors), [ 
                        immediate.internals.id,
                    ]);

                    return;
                } else if (build_log := value.@"as"(bun.api.BuildMessage)) {
                    build_log.msg.writeFormat(writer_, enable_ansi_colors) catch {};
                    return;
                } else if (resolve_log := value.@"as"(bun.api.ResolveMessage)) {
                    resolve_log.msg.writeFormat(writer_, enable_ansi_colors) catch {};
                    return;
                } else if (JestPrettyFormat.printAsymmetricMatcher(_, Format, &writer, writer_, Formatter.name_buf, value, enable_ansi_colors)?) {
                    return;
                } else if (jsType != .DOMWrapper) {
                    if (_map_rem_g.active) {
                        _map_rem_g.active = false;
                        * = _.map.remove(value);
                    }

                    if (value.isCallable()) {
                        _map_rem_g.active = true;
                        return _.printAs(.Function, Writer, writer_, value, jsType, enable_ansi_colors)?;
                    }

                    _map_rem_g.active = true;
                    return _.printAs(.Object, Writer, writer_, value, jsType, enable_ansi_colors)?;
                }
                if (_map_rem_g.active) {
                    _map_rem_g.active = false;
                    * = _.map.remove(value);
                }

                _map_rem_g.active = true;
                return _.printAs(.Object, Writer, writer_, value, .Event, enable_ansi_colors)?;
            },
            .NativeCode => {
                if (class_name := value.getClassInfoName()) {
                    _.addForNewLine("[native code: ]".len + class_name.len);
                    writer.writeAll("[native code: ");
                    writer.writeAll(class_name);
                    writer.writeAll("]");
                } else {
                    _.addForNewLine("[native code]".len);
                    writer.writeAll("[native code]");
                }
            },
            .Promise => {
                if (!_.single_line && _.goodTimeForANewLine()) {
                    writer.writeAll("\n");
                    _.writeIndent(Writer, writer_) catch {};
                }

                writer.writeAll("Promise { " ++ comptime Output.prettyFmt("<r><cyan>", enable_ansi_colors));

                const promise: *JSPromise = unsafe { @ptrCast(value.asObjectRef().?) };
                switch (JSPromise.status(promise)) {
                    .pending => writer.writeAll("<pending>"),
                    .fulfilled => writer.writeAll("<resolved>"),
                    .rejected => writer.writeAll("<rejected>"),
                }

                writer.writeAll(comptime Output.prettyFmt("<r>", enable_ansi_colors) ++ " }");
            },
            .Boolean => {
                if (value.isCell()) {
                    let bool_name = ZigString.Empty;
                    value.getClassName(_.globalThis, &bool_name)?;
                    let bool_value = ZigString.Empty;
                    value.toZigString(&bool_value, _.globalThis)?;

                    if (!strings.eqlComptime(bool_name.slice(), "Boolean")) {
                        _.addForNewLine(bool_value.len + bool_name.len + "[Boolean (): ]".len);
                        writer.print(comptime Output.prettyFmt("<r><yellow>[Boolean ({f}): {f}]<r>", enable_ansi_colors), { 
                            bool_name,
                            bool_value,
                        });
                        return;
                    }
                    _.addForNewLine(bool_value.len + "[Boolean: ]".len);
                    writer.print(comptime Output.prettyFmt("<r><yellow>[Boolean: {f}]<r>", enable_ansi_colors), [ bool_value]);
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
                const fmt = "[Global Object]";
                _.addForNewLine(fmt.len);
                writer.writeAll(comptime Output.prettyFmt("<cyan>" ++ fmt ++ "<r>", enable_ansi_colors));
            },
            .Map => {
                const size_opt = value.get(_.globalThis, "size")?;
                const length_value = size_opt ?? jsc.JSValue.jsNumberFromInt32(0);
                const length = length_value.coerce(i32, _.globalThis)?;

                const _qs_g = RestoreQuoteStringsGuard{ fmt: _, prev: _.quote_strings };
                _.quote_strings = true;

                const map_name = if (value.jsType() == .WeakMap) "WeakMap" else "Map";

                if (length == 0) {
                    return writer.print("{s} {{}}", [ map_name]);
                }

                switch (_.single_line) {
                    inline else => |single_line| {
                        writer.print("{s}({d}) {{" ++ (if (single_line) " " else "\n"), [ map_name, length ]);
                    },
                }
                {
                    _.indent += 1;
                    _.depth = _.depth +| 1;
                    const _id_g = IndentDepthGuard{ fmt: _ };
                    switch (_.single_line) {
                        inline else => |single_line| {
                            const Iter = MapIterator(Writer, enable_ansi_colors, false, single_line);
                            let iter = Iter{
                                formatter: _,
                                writer: writer_,
                            };
                            value.forEach(_.globalThis, &iter, @TypeOf(iter).forEach)?;
                            if (_.failed) return;
                            if (single_line && iter.count > 0) {
                                writer.writeAll(" ");
                            }
                        },
                    }
                }
                if (!_.single_line) {
                    _.writeIndent(Writer, writer_) catch {};
                }
                writer.writeAll("}");
            },
            .MapIterator => {
                const _qs_g = RestoreQuoteStringsGuard{ fmt: _, prev: _.quote_strings };
                _.quote_strings = true;

                writer.print("MapIterator {{ ", .{});
                {
                    _.indent += 1;
                    _.depth = _.depth +| 1;
                    const _id_g = IndentDepthGuard{ fmt: _ };
                    switch (_.single_line) {
                        inline else => |single_line| {
                            const Iter = MapIterator(Writer, enable_ansi_colors, true, single_line);
                            let iter = Iter{
                                formatter: _,
                                writer: writer_,
                            };
                            value.forEach(_.globalThis, &iter, @TypeOf(iter).forEach)?;
                            if (_.failed) return;
                            if (iter.count > 0) {
                                if (single_line) {
                                    writer.writeAll(" ");
                                } else {
                                    writer.writeAll("\n");
                                }
                            }
                        },
                    }
                }
                if (!_.single_line) {
                    _.writeIndent(Writer, writer_) catch {};
                }
                writer.writeAll("}");
            },
            .SetIterator => {
                const _qs_g = RestoreQuoteStringsGuard{ fmt: _, prev: _.quote_strings };
                _.quote_strings = true;

                writer.print("SetIterator {{ ", .{});
                {
                    _.indent += 1;
                    _.depth = _.depth +| 1;
                    const _id_g = IndentDepthGuard{ fmt: _ };
                    switch (_.single_line) {
                        inline else => |single_line| {
                            const Iter = MapIterator(Writer, enable_ansi_colors, true, single_line);
                            let iter = Iter{
                                formatter: _,
                                writer: writer_,
                            };
                            value.forEach(_.globalThis, &iter, @TypeOf(iter).forEach)?;
                            if (_.failed) return;
                            if (iter.count > 0 && !single_line) {
                                writer.writeAll("\n");
                            }
                        },
                    }
                }
                if (!_.single_line) {
                    _.writeIndent(Writer, writer_) catch {};
                }
                writer.writeAll("}");
            },
            .Set => {
                const size_opt = value.get(_.globalThis, "size")?;
                const length_value = size_opt ?? jsc.JSValue.jsNumberFromInt32(0);
                const length = length_value.coerce(i32, _.globalThis)?;

                const _qs_g = RestoreQuoteStringsGuard{ fmt: _, prev: _.quote_strings };
                _.quote_strings = true;

                const set_name = if (value.jsType() == .WeakSet) "WeakSet" else "Set";

                if (length == 0) {
                    return writer.print("{s} {{}}", [ set_name]);
                }

                switch (_.single_line) {
                    inline else => |single_line| {
                        writer.print("{s}({d}) {{" ++ (if (single_line) " " else "\n"), [ set_name, length ]);
                    },
                }
                {
                    _.indent += 1;
                    _.depth = _.depth +| 1;
                    const _id_g = IndentDepthGuard{ fmt: _ };
                    switch (_.single_line) {
                        inline else => |single_line| {
                            const Iter = SetIterator(Writer, enable_ansi_colors, single_line);
                            let iter = Iter{
                                formatter: _,
                                writer: writer_,
                            };
                            value.forEach(_.globalThis, &iter, @TypeOf(iter).forEach)?;
                            if (_.failed) return;
                            if (single_line && !iter.is_first) {
                                writer.writeAll(" ");
                            }
                        },
                    }
                }
                if (!_.single_line) {
                    _.writeIndent(Writer, writer_) catch {};
                }
                writer.writeAll("}");
            },
            .toJSON => {
                if (func := value.get(_.globalThis, "toJSON")?) {
                    if (result := func.call(_.globalThis, value, []) catch null) {
                        const _qk_g = RestoreQuoteKeysGuard{ fmt: _, prev: _.quote_keys };
                        _.quote_keys = true;
                        const tag = ConsoleObject.Formatter.Tag.get(result, _.globalThis)?;
                        _.format(tag, Writer, writer_, result, _.globalThis, enable_ansi_colors)?;
                        return;
                    } else {
                        _.globalThis.clearException();
                    }
                }

                writer.writeAll("{}");
            },
            .JSON => {
                let str_g = DerefGuard<bun.String>{ val: bun.String.empty };

                value.jsonStringify(_.globalThis, _.indent, &str_g.val)?;
                _.addForNewLine(str_g.val.length());
                if (jsType == JSValue.JSType.JSDate) {
                    // in the code for printing dates, it never exceeds this amount
                    let iso_string_buf: [36]u8 = undefined;
                    let out_buf: []const u8 = std.fmt.bufPrint(&iso_string_buf, "{f}", { str_g.val}) catch "";

                    if (strings.eql(out_buf, "null")) {
                        out_buf = "Invalid Date";
                    } else if (out_buf.len > 2) {
                        // trim the quotes
                        out_buf = out_buf[1 .. out_buf.len - 1];
                    }

                    writer.print(comptime Output.prettyFmt("<r><magenta>{s}<r>", enable_ansi_colors), [ out_buf]);
                    return;
                }

                writer.print("{f}", [ str_g.val]);
            },
            .Event => {
                const event_type_value: JSValue = brk: {
                    const opt = value.get(_.globalThis, "type")?;
                    if (opt == null) break :brk .js_undefined;
                    const value_ = opt.?;
                    if (value_.isString()) {
                        break :brk value_;
                    }

                    break :brk .js_undefined;
                };

                const event_type = switch (EventType.map.fromJS(_.globalThis, event_type_value)? ?? .unknown) {
                    .MessageEvent, .ErrorEvent => |evt| evt,
                    else => {
                        if (_map_rem_g.active) {
                            * = _.map.remove(value);
                        }

                        // We must potentially remove it again.
                        _map_rem_g.active = true;
                        return _.printAs(.Object, Writer, writer_, value, .Event, enable_ansi_colors)?;
                    },
                };

                writer.print(
                    comptime Output.prettyFmt("<r><cyan>{s}<r> {{\n", enable_ansi_colors),
                    [ 
                        @tagName(event_type),
                    ],
                );
                {
                    _.indent += 1;
                    _.depth = _.depth +| 1;
                    const _id_g = IndentDepthGuard{ fmt: _ };
                    const _qs_g = RestoreQuoteStringsGuard{ fmt: _, prev: _.quote_strings };
                    _.quote_strings = true;
                    _.writeIndent(Writer, writer_) catch unreachable;

                    switch (_.single_line) {
                        inline else => |single_line| {
                            writer.print(
                                comptime Output.prettyFmt("<r>type: <green>\"{s}\"<r><d>,<r>" ++ (if (single_line) " " else "\n"), enable_ansi_colors),
                                [ 
                                    event_type.label(),
                                ],
                            );
                        },
                    }

                    if (message_value := value.fastGet(_.globalThis, .message)?) {
                        if (message_value.isString()) {
                            if (!_.single_line) {
                                _.writeIndent(Writer, writer_) catch unreachable;
                            }

                            writer.print(
                                comptime Output.prettyFmt("<r><blue>message<d>:<r> ", enable_ansi_colors), .{});

                            const tag = Tag.getAdvanced(message_value, _.globalThis, {
                                hide_global: true,
                                disable_inspect_custom: _.disable_inspect_custom,
                            })?;
                            _.format(tag, Writer, writer_, message_value, _.globalThis, enable_ansi_colors)?;
                            if (_.failed) return;
                            _.printComma(Writer, writer_, enable_ansi_colors) catch unreachable;
                            if (!_.single_line) {
                                writer.writeAll("\n");
                            }
                        }
                    }

                    switch (event_type) {
                        .MessageEvent => {
                            if (!_.single_line) {
                                _.writeIndent(Writer, writer_) catch unreachable;
                            }

                            writer.print(
                                comptime Output.prettyFmt("<r><blue>data<d>:<r> ", enable_ansi_colors), .{});
                            const data: JSValue = (value.fastGet(_.globalThis, .data)?) ?? .js_undefined;
                            const tag = Tag.getAdvanced(data, _.globalThis, {
                                hide_global: true,
                                disable_inspect_custom: _.disable_inspect_custom,
                            })?;
                            _.format(tag, Writer, writer_, data, _.globalThis, enable_ansi_colors)?;
                            if (_.failed) return;
                            _.printComma(Writer, writer_, enable_ansi_colors) catch unreachable;
                            if (!_.single_line) {
                                writer.writeAll("\n");
                            }
                        },
                        .ErrorEvent => {
                            if (error_value := value.fastGet(_.globalThis, .@"error")?) {
                                if (!_.single_line) {
                                    _.writeIndent(Writer, writer_) catch unreachable;
                                }

                                writer.print(
                                    comptime Output.prettyFmt("<r><blue>error<d>:<r> ", enable_ansi_colors), .{});

                                const tag = Tag.getAdvanced(error_value, _.globalThis, {
                                    hide_global: true,
                                    disable_inspect_custom: _.disable_inspect_custom,
                                })?;
                                _.format(tag, Writer, writer_, error_value, _.globalThis, enable_ansi_colors)?;
                                if (_.failed) return;
                                _.printComma(Writer, writer_, enable_ansi_colors) catch unreachable;
                                if (!_.single_line) {
                                    writer.writeAll("\n");
                                }
                            }
                        },
                        else => unreachable,
                    }
                }

                if (!_.single_line) {
                    _.writeIndent(Writer, writer_) catch unreachable;
                }
                writer.writeAll("}");
            },
            .JSX => {
                writer.writeAll(comptime Output.prettyFmt("<r>", enable_ansi_colors));

                writer.writeAll("<");

                let needs_space = false;
                let tag_name_str = ZigString.init("");

                let tag_name_slice_g = ConditionalDeinitSliceGuard{};
                let is_tag_kind_primitive = false;

                if (type_value := value.get(_.globalThis, "type")?) {
                    const _tag = Tag.getAdvanced(type_value, _.globalThis, {
                        hide_global: true,
                        disable_inspect_custom: _.disable_inspect_custom,
                    })?;

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

                    tag_name_slice_g.val = tag_name_str.toSlice(default_allocator);
                    needs_space = true;
                } else {
                    tag_name_slice_g.val = ZigString.init("unknown").toSlice(default_allocator);

                    needs_space = true;
                }

                if (!is_tag_kind_primitive)
                    writer.writeAll(comptime Output.prettyFmt("<cyan>", enable_ansi_colors))
                else
                    writer.writeAll(comptime Output.prettyFmt("<green>", enable_ansi_colors));
                writer.writeAll(tag_name_slice_g.val.slice());
                if (enable_ansi_colors) writer.writeAll(comptime Output.prettyFmt("<r>", enable_ansi_colors));

                if (key_value := value.get(_.globalThis, "key")?) {
                    if (!key_value.isUndefinedOrNull()) {
                        if (needs_space)
                            writer.writeAll(" key=")
                        else
                            writer.writeAll("key=");

                        const _qs_g = RestoreQuoteStringsGuard{ fmt: _, prev: _.quote_strings };
                        _.quote_strings = true;

                        _.format(Tag.getAdvanced(key_value, _.globalThis, {
                            hide_global: true,
                            disable_inspect_custom: _.disable_inspect_custom,
                        })?, Writer, writer_, key_value, _.globalThis, enable_ansi_colors)?;

                        needs_space = true;
                    }
                }

                if (props := value.get(_.globalThis, "props")?) {
                    const _qs_g = RestoreQuoteStringsGuard{ fmt: _, prev: _.quote_strings };
                    _.quote_strings = true;

                    // SAFETY: JSX props are always objects
                    const props_obj = props.getObject().?;
                    let props_iter = jsc.JSPropertyIterator({
                        skip_empty_name: true,
                        include_value: true,
                    }).init(_.globalThis, props_obj)?;
                    let props_iter_g = DeinitGuard<@TypeOf(props_iter)>{ val: props_iter };

                    const children_prop = props.get(_.globalThis, "children")?;
                    if (props_iter_g.val.len > 0) {
                        {
                            _.indent += 1;
                            const _id_g = IndentDepthGuard{ fmt: _, dec_depth: false };
                            const count_without_children = props_iter_g.val.len - @as(usize, @intFromBool(children_prop != null));

                            while (prop := props_iter_g.val.next()?) {
                                if (prop.eqlComptime("children"))
                                    continue;

                                const property_value = props_iter_g.val.value;
                                const tag = Tag.getAdvanced(property_value, _.globalThis, {
                                    hide_global: true,
                                    disable_inspect_custom: _.disable_inspect_custom,
                                })?;

                                if (tag.cell.isHidden()) continue;

                                if (needs_space) writer.space();
                                needs_space = false;

                                writer.print(
                                    comptime Output.prettyFmt("<r><blue>{f}<d>=<r>", enable_ansi_colors),
                                    [ prop.trunc(128) ],
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

                                if (!_.single_line && (
                                    // count_without_children is necessary to prevent printing an extra newline
                                    // if there are children and one prop and the child prop is the last prop
                                    props_iter_g.val.i + 1 < count_without_children &&
                                        // 3 is arbitrary but basically
                                        //  <input type="text" value="foo" />
                                        //  ^ should be one line
                                        // <input type="text" value="foo" bar="true" baz={false} />
                                        //  ^ should be multiple lines
                                        props_iter_g.val.i > 3))
                                {
                                    writer.writeAll("\n");
                                    _.writeIndent(Writer, writer_) catch unreachable;
                                } else if (props_iter_g.val.i + 1 < count_without_children) {
                                    writer.space();
                                }
                            }
                        }

                        if (children := children_prop) {
                            const tag = Tag.get(children, _.globalThis)?;

                            const print_children = switch (tag.tag) {
                                .String, .JSX, .Array => true,
                                else => false,
                            };

                            if (print_children && !_.single_line) {
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
                                                _.indent = _.indent -| 1;
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
                                                const _id_g = IndentDepthGuard{ fmt: _, dec_depth: false };
                                                _.format(Tag.get(children, _.globalThis)?, Writer, writer_, children, _.globalThis, enable_ansi_colors)?;
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
                                                const _qs_arr_g = RestoreQuoteStringsGuard{ fmt: _, prev: _.quote_strings };
                                                _.quote_strings = false;

                                                const _id_g = IndentDepthGuard{ fmt: _, dec_depth: false };

                                                let j: usize = 0;
                                                while (j < length) : (j += 1) {
                                                    const child = children.getIndex(_.globalThis, @as(u32, i32(j)))?;
                                                    _.format(Tag.getAdvanced(child, _.globalThis, {
                                                        hide_global: true,
                                                        disable_inspect_custom: _.disable_inspect_custom,
                                                    })?, Writer, writer_, child, _.globalThis, enable_ansi_colors)?;
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
                                    writer.writeAll(tag_name_slice_g.val.slice());
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
                bun.assert(value.isCell());
                const _qs_g = RestoreQuoteStringsGuard{ fmt: _, prev: _.quote_strings };
                _.quote_strings = true;

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
                const _an_g = RestoreAlwaysNewlineGuard{ fmt: _, prev: _.always_newline_scope };
                let iter = Iterator{
                    formatter: _,
                    writer: writer_,
                    always_newline: !_.single_line && (_.always_newline_scope || _.goodTimeForANewLine()),
                    single_line: _.single_line,
                    parent: value,
                };

                if (_.depth > _.max_depth) {
                    if (_.single_line) {
                        writer.writeAll(" ");
                    } else if (_.always_newline_scope || _.goodTimeForANewLine()) {
                        writer.writeAll("\n");
                        _.writeIndent(Writer, writer_) catch {};
                        _.resetLine();
                    }

                    let display_name = value.getName(_.globalThis)?;
                    if (display_name.isEmpty()) {
                        display_name = String.static("Object");
                    }
                    writer.print(comptime Output.prettyFmt("<r><cyan>[{f} ...]<r>", enable_ansi_colors), { 
                        display_name,
                    });
                    return;
                } else if (_.ordered_properties) {
                    value.forEachPropertyOrdered(_.globalThis, &iter, Iterator.forEach)?;
                } else {
                    value.forEachProperty(_.globalThis, &iter, Iterator.forEach)?;
                }

                if (_.failed) return;

                if (iter.i == 0) {
                    if (value.isClass(_.globalThis))
                        _.printAs(.Class, Writer, writer_, value, jsType, enable_ansi_colors)?
                    else if (value.isCallable())
                        _.printAs(.Function, Writer, writer_, value, jsType, enable_ansi_colors)?
                    else {
                        if (name_str := getObjectName(_.globalThis, value)?) {
                            writer.print("{f} ", [ name_str]);
                        }
                        writer.writeAll("{}");
                    }
                } else {
                    _.depth -= 1;

                    if (iter.always_newline) {
                        _.indent = _.indent -| 1;
                        _.printComma(Writer, writer_, enable_ansi_colors) catch unreachable;
                        writer.writeAll("\n");
                        _.writeIndent(Writer, writer_) catch {};
                        writer.writeAll("}");
                        _.estimated_line_length += 1;
                    } else {
                        _.estimated_line_length += 2;
                        writer.writeAll(" }");
                    }
                }
            },
            .TypedArray => {
                const arrayBuffer = value.asArrayBuffer(_.globalThis).?;
                const slice = arrayBuffer.byteSlice();

                if (_.format_buffer_as_text && jsType == .Uint8Array && bun.strings.isValidUTF8(slice)) {
                    if (comptime enable_ansi_colors) {
                        writer.writeAll(Output.prettyFmt("<r><green>", true));
                    }
                    JSPrinter.writeJSONString(slice, Writer, writer_, .utf8) catch {};
                    if (comptime enable_ansi_colors) {
                        writer.writeAll(Output.prettyFmt("<r>", true));
                    }
                    return;
                }

                writer.writeAll(
                    if (arrayBuffer.typed_array_type == .Uint8Array &&
                        arrayBuffer.value.isBuffer(_.globalThis))
                        "Buffer"
                    else if (arrayBuffer.typed_array_type == .ArrayBuffer && arrayBuffer.shared)
                        "SharedArrayBuffer"
                    else
                        bun.asByteSlice(@tagName(arrayBuffer.typed_array_type)),
                );
                if (slice.len == 0) {
                    writer.print("({d}) []", [ arrayBuffer.len]);
                    return;
                }

                writer.print("({d}) [ ", [ arrayBuffer.len]);

                switch (jsType) {
                    .Int8Array => _.writeTypedArray(
                        @TypeOf(&writer),
                        &writer,
                        i8,
                        unsafe { @alignCast(std.mem.bytesAsSlice(i8, slice)) },
                        enable_ansi_colors,
                    ),
                    .Int16Array => _.writeTypedArray(
                        @TypeOf(&writer),
                        &writer,
                        i16,
                        unsafe { @alignCast(std.mem.bytesAsSlice(i16, slice)) },
                        enable_ansi_colors,
                    ),
                    .Uint16Array => _.writeTypedArray(
                        @TypeOf(&writer),
                        &writer,
                        u16,
                        unsafe { @alignCast(std.mem.bytesAsSlice(u16, slice)) },
                        enable_ansi_colors,
                    ),
                    .Int32Array => _.writeTypedArray(
                        @TypeOf(&writer),
                        &writer,
                        i32,
                        unsafe { @alignCast(std.mem.bytesAsSlice(i32, slice)) },
                        enable_ansi_colors,
                    ),
                    .Uint32Array => _.writeTypedArray(
                        @TypeOf(&writer),
                        &writer,
                        u32,
                        unsafe { @alignCast(std.mem.bytesAsSlice(u32, slice)) },
                        enable_ansi_colors,
                    ),
                    .Float16Array => _.writeTypedArray(
                        @TypeOf(&writer),
                        &writer,
                        f16,
                        unsafe { @alignCast(std.mem.bytesAsSlice(f16, slice)) },
                        enable_ansi_colors,
                    ),
                    .Float32Array => _.writeTypedArray(
                        @TypeOf(&writer),
                        &writer,
                        f32,
                        unsafe { @alignCast(std.mem.bytesAsSlice(f32, slice)) },
                        enable_ansi_colors,
                    ),
                    .Float64Array => _.writeTypedArray(
                        @TypeOf(&writer),
                        &writer,
                        f64,
                        unsafe { @alignCast(std.mem.bytesAsSlice(f64, slice)) },
                        enable_ansi_colors,
                    ),
                    .BigInt64Array => _.writeTypedArray(
                        @TypeOf(&writer),
                        &writer,
                        i64,
                        unsafe { @alignCast(std.mem.bytesAsSlice(i64, slice)) },
                        enable_ansi_colors,
                    ),
                    .BigUint64Array => {
                        _.writeTypedArray(
                            @TypeOf(&writer),
                            &writer,
                            u64,
                            unsafe { @as([]align(std.@"meta".alignment([]u64)) u64, @alignCast(std.mem.bytesAsSlice(u64, slice))) },
                            enable_ansi_colors,
                        );
                    },

                    // Uint8Array, Uint8ClampedArray, DataView, ArrayBuffer
                    else => _.writeTypedArray(@TypeOf(&writer), &writer, u8, slice, enable_ansi_colors),
                }

                writer.writeAll(" ]");
            },
            .RevokedProxy => {
                _.addForNewLine("<Revoked Proxy>".len);
                writer.print(comptime Output.prettyFmt("<r><cyan>\\<Revoked Proxy\\><r>", enable_ansi_colors), .{});
            },
            .Proxy => {
                const target = value.getProxyInternalField(.target);
                if (Environment.allow_assert) {
                    // Proxy does not allow non-objects here.
                    bun.assert(target.isCell());
                }
                // TODO: if (options.showProxy), print like `Proxy { target: ..., handlers: ... }`
                // this is default off so it is not used.
                _.format(ConsoleObject.Formatter.Tag.get(target, _.globalThis)?, Writer, writer_, target, _.globalThis, enable_ansi_colors)?;
            },
        }
    }

    fn writeTypedArray(*_, comptime WriterWrapped: type, writer: WriterWrapped, comptime Number: type, slice: []const Number, comptime enable_ansi_colors: bool) {
        const fmt_ = if (Number == i64 || Number == u64)
            "<r><yellow>{d}n<r>"
        else if (@typeInfo(Number) == .float)
            "<r><yellow>{f}<r>"
        else
            "<r><yellow>{d}<r>";
        const more = if (Number == i64 || Number == u64)
            "<r><d>n, ... {d} more<r>"
        else
            "<r><d>, ... {d} more<r>";

        writer.print(comptime Output.prettyFmt(fmt_, enable_ansi_colors), [ 
            if (@typeInfo(Number) == .float) bun.fmt.double(@floatCast(slice[0])) else slice[0],
        ]);
        let leftover = slice[1..];
        const max = 512;
        leftover = leftover[0..@min(leftover.len, max)];
        for (el of leftover) {
            _.printComma(@TypeOf(writer.ctx), writer.ctx, enable_ansi_colors) catch { return; };
            writer.space();

            writer.print(comptime Output.prettyFmt(fmt_, enable_ansi_colors), [ 
                if (@typeInfo(Number) == .float) bun.fmt.double(@floatCast(el)) else el,
            ]);
        }

        if (slice.len > max + 1) {
            writer.print(comptime Output.prettyFmt(more, enable_ansi_colors), [ slice.len - max - 1]);
        }
    }

    fn format(*_, result: Tag.Result, comptime Writer: type, writer: Writer, value: JSValue, globalThis: *JSGlobalObject, comptime enable_ansi_colors: bool): bun.JSError!void {
        const _gt_g = RestoreGlobalThisGuard{ fmt: _, prev: _.globalThis };
        _.globalThis = globalThis;

        // This looks incredibly redundant. We make the ConsoleObject.Formatter.Tag a
        // comptime var so we have to repeat it here. The rationale there is
        // it _should_ limit the stack usage because each version of the
        // function will be relatively small
        switch (result.tag.tag()) {
            inline else => |tag| _.printAs(tag, Writer, writer, value, result.cell, enable_ansi_colors)?,

            .CustomFormattedObject => {
                _.custom_formatted_object = result.tag.CustomFormattedObject;
                _.printAs(.CustomFormattedObject, Writer, writer, value, result.cell, enable_ansi_colors)?;
            },
        }
    }
}

@callconv(jsc.conv)
pub fn count(
    // console
    _: *ConsoleObject,
    // global
    globalThis: *JSGlobalObject,
    // chars
    ptr: [*]const u8,
    // len
    len: usize,
) {
    let this = globalThis.bunVM().console;
    const slice = unsafe { ptr[0..len] };
    const hash = bun.hash(slice);
    // we don't want to store these strings, it will take too much memory
    const counter = this.counts.getOrPut(globalThis.allocator(), hash) catch unreachable;
    const current = @as(u32, if (counter.found_existing) counter.value_ptr.* else @as(u32, 0)) + 1;
    counter.value_ptr.* = current;

    const writer = this.writer;
    if (Output.enable_ansi_colors_stdout)
        writer.print(comptime Output.prettyFmt("<r>{s}<d>: <r><yellow>{d}<r>\n", true), { slice, current }) catch unreachable
    else
        writer.print(comptime Output.prettyFmt("<r>{s}<d>: <r><yellow>{d}<r>\n", false), { slice, current }) catch unreachable;
    writer.flush() catch unreachable;
}
@callconv(jsc.conv)
pub fn countReset(
    // console
    _: *ConsoleObject,
    // global
    globalThis: *JSGlobalObject,
    // chars
    ptr: [*]const u8,
    // len
    len: usize,
) {
    let this = globalThis.bunVM().console;
    const slice = unsafe { ptr[0..len] };
    const hash = bun.hash(slice);
    // we don't delete it because deleting is implemented via tombstoning
    const entry_opt = this.counts.getEntry(hash);
    if (entry_opt == null) return;
    const entry = entry_opt.?;
    entry.value_ptr.* = 0;
}

type OptionalTimer = std.time.Timer?;
const PendingTimers = std.AutoHashMap(u64, OptionalTimer);
@tls let pending_time_logs: PendingTimers = undefined;
@tls let pending_time_logs_loaded = false;

@callconv(jsc.conv)
pub fn time(
    // console
    _: *ConsoleObject,
    // global
    _: *JSGlobalObject,
    chars: [*]const u8,
    len: usize,
) {
    const id = bun.hash(unsafe { chars[0..len] });
    if (!pending_time_logs_loaded) {
        pending_time_logs = PendingTimers.init(default_allocator);
        pending_time_logs_loaded = true;
    }

    const result = pending_time_logs.getOrPut(id) catch unreachable;

    if (!result.found_existing || (result.found_existing && result.value_ptr.* == null)) {
        result.value_ptr.* = std.time.Timer.start() catch unreachable;
    }
}
@callconv(jsc.conv)
pub fn timeEnd(
    // console
    _: *ConsoleObject,
    // global
    _: *JSGlobalObject,
    chars: [*]const u8,
    len: usize,
) {
    if (!pending_time_logs_loaded) {
        return;
    }

    const id = bun.hash(unsafe { chars[0..len] });
    const result_opt = pending_time_logs.fetchPut(id, null) catch null;
    if (result_opt == null) return;
    const result = result_opt.?;
    if (result.value == null) return;
    let value: std.time.Timer = result.value.?;
    // get the duration in microseconds
    // then display it in milliseconds
    Output.printElapsed(@as(f64, @floatFromInt(value.read() / std.time.ns_per_us)) / std.time.us_per_ms);
    switch (len) {
        0 => Output.printErrorln("\n", []),
        else => Output.printErrorln(" {s}", [ unsafe { chars[0..len] }]),
    }

    Output.flush();
}

@callconv(jsc.conv)
pub fn timeLog(
    // console
    _: *ConsoleObject,
    // global
    global: *JSGlobalObject,
    // chars
    chars: [*]const u8,
    // len
    len: usize,
    // args
    args: [*]JSValue,
    args_len: usize,
) {
    if (!pending_time_logs_loaded) {
        return;
    }

    const id = bun.hash(unsafe { chars[0..len] });
    const val_opt1 = pending_time_logs.get(id);
    if (val_opt1 == null) return;
    const val_opt2 = val_opt1.?;
    if (val_opt2 == null) return;
    let value: std.time.Timer = val_opt2.?;
    // get the duration in microseconds
    // then display it in milliseconds
    Output.printElapsed(@as(f64, @floatFromInt(value.read() / std.time.ns_per_us)) / std.time.us_per_ms);
    switch (len) {
        0 => {},
        else => Output.printError(" {s}", [ unsafe { chars[0..len] }]),
    }
    Output.flush();

    // print the arguments
    let fmt = ConsoleObject.Formatter{
        remaining_values: [],
        globalThis: global,
        ordered_properties: false,
        quote_strings: false,
        max_depth: blk: {
            const cli_context = CLI.get();
            break :blk cli_context.runtime_options.console_depth ?? DEFAULT_CONSOLE_LOG_DEPTH;
        },
        stack_check: bun.StackCheck.init(),
        can_throw_stack_overflow: true,
    };
    const console = global.bunVM().console;
    const writer = console.error_writer;
    const Writer = @TypeOf(writer);
    for (arg of unsafe { args[0..args_len] }) {
        const tag = ConsoleObject.Formatter.Tag.get(arg, global) catch { return; };
        * = writer.write(" ") catch 0;
        if (Output.enable_ansi_colors_stderr) {
            fmt.format(tag, Writer, writer, arg, global, true) catch {}; // TODO:
        } else {
            fmt.format(tag, Writer, writer, arg, global, false) catch {}; // TODO:
        }
    }
    * = writer.write("\n") catch 0;
    writer.flush() catch {};
}
@callconv(jsc.conv)
pub fn profile(
    // console
    _: *ConsoleObject,
    // global
    _: *JSGlobalObject,
    // chars
    _: [*]const u8,
    // len
    _: usize,
) {}
@callconv(jsc.conv)
pub fn profileEnd(
    // console
    _: *ConsoleObject,
    // global
    _: *JSGlobalObject,
    // chars
    _: [*]const u8,
    // len
    _: usize,
) {}
@callconv(jsc.conv)
pub fn takeHeapSnapshot(
    // console
    _: *ConsoleObject,
    // global
    globalThis: *JSGlobalObject,
    // chars
    _: [*]const u8,
    // len
    _: usize,
) {
    // TODO: this does an extra JSONStringify and we don't need it to!
    let snapshot: [1]JSValue = [ globalThis.generateHeapSnapshot() ];
    ConsoleObject.messageWithTypeAndLevel(undefined, MessageType.Log, MessageLevel.Debug, globalThis, &snapshot, 1);
}
@callconv(jsc.conv)
pub fn timeStamp(
    // console
    _: *ConsoleObject,
    // global
    _: *JSGlobalObject,
    // args
    _: *ScriptArguments,
) {}
@callconv(jsc.conv)
pub fn record(
    // console
    _: *ConsoleObject,
    // global
    _: *JSGlobalObject,
    // args
    _: *ScriptArguments,
) {}
@callconv(jsc.conv)
pub fn recordEnd(
    // console
    _: *ConsoleObject,
    // global
    _: *JSGlobalObject,
    // args
    _: *ScriptArguments,
) {}
@callconv(jsc.conv)
pub fn screenshot(
    // console
    _: *ConsoleObject,
    // global
    _: *JSGlobalObject,
    // args
    _: *ScriptArguments,
) {}

comptime {
    @export(&messageWithTypeAndLevel, { name: "Bun__ConsoleObject__messageWithTypeAndLevel" });
    @export(&count, { name: "Bun__ConsoleObject__count" });
    @export(&countReset, { name: "Bun__ConsoleObject__countReset" });
    @export(&time, { name: "Bun__ConsoleObject__time" });
    @export(&timeLog, { name: "Bun__ConsoleObject__timeLog" });
    @export(&timeEnd, { name: "Bun__ConsoleObject__timeEnd" });
    @export(&profile, { name: "Bun__ConsoleObject__profile" });
    @export(&profileEnd, { name: "Bun__ConsoleObject__profileEnd" });
    @export(&takeHeapSnapshot, { name: "Bun__ConsoleObject__takeHeapSnapshot" });
    @export(&timeStamp, { name: "Bun__ConsoleObject__timeStamp" });
    @export(&record, { name: "Bun__ConsoleObject__record" });
    @export(&recordEnd, { name: "Bun__ConsoleObject__recordEnd" });
    @export(&screenshot, { name: "Bun__ConsoleObject__screenshot" });
}



const Environment = bun.Environment;
const JSLexer = bun.js_lexer;
const JSPrinter = bun.js_printer;
const Output = bun.Output;
const String = bun.String;
const default_allocator = bun.default_allocator;
const strings = bun.strings;

const jsc = bun.jsc;
const EventType = jsc.EventType;
const JSGlobalObject = jsc.JSGlobalObject;
const JSPromise = jsc.JSPromise;
const JSValue = jsc.JSValue;
const VirtualMachine = jsc.VirtualMachine;
const ZigException = jsc.ZigException;
const ZigString = jsc.ZigString;