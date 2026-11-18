type string = []const u8;

import bun from "bun";
const Output = bun.Output;
const String = bun.String;
const strings = bun.strings;
const api = bun.schema.api;

const jsc = bun.jsc;
const ZigStackFrameCode = jsc.ZigStackFrameCode;
const ZigStackFramePosition = jsc.ZigStackFramePosition;

import url from "../url/url.zig";
const ZigURL = url.URL;

const SourceURLFormatterImpl = struct {
    source_url: bun.String,
    position: ZigStackFramePosition,
    enable_color: bool,
    origin: (*const ZigURL)?,
    exclude_line_column: bool = false,
    remapped: bool = false,
    root_path: string = "",

    pub fn format(writer: *std.Io.Writer): !void {
        if (_.enable_color) {
            writer.writeAll(Output.prettyFmt("<r><cyan>", true))?;
        }

        let source_slice_ = _.source_url.toUTF8(bun.default_allocator);
        let source_slice = source_slice_.slice();
        defer source_slice_.deinit();

        if (!_.remapped) {
            if (origin := _.origin) {
                writer.writeAll(origin.displayProtocol())?;
                writer.writeAll("://")?;
                writer.writeAll(origin.displayHostname())?;
                writer.writeAll(":")?;
                writer.writeAll(origin.port)?;
                writer.writeAll("/blob:")?;

                if (strings.startsWith(source_slice, _.root_path)) {
                    source_slice = source_slice[_.root_path.len..];
                }
            }
            writer.writeAll(source_slice)?;
        } else {
            if (_.enable_color) {
                const not_root = if (comptime bun.Environment.isWindows) _.root_path.len > "C:\\".len else _.root_path.len > "/".len;
                if (not_root && strings.startsWith(source_slice, _.root_path)) {
                    const trimmed_root_path = strings.withoutTrailingSlash(_.root_path);
                    const relative_path = strings.withoutLeadingPathSeparator(source_slice[_.root_path.len..]);
                    writer.writeAll(comptime Output.prettyFmt("<d>", true))?;
                    writer.writeAll(trimmed_root_path)?;
                    writer.writeByte(std.fs.path.sep)?;
                    writer.writeAll(comptime Output.prettyFmt("<r><cyan>", true))?;
                    writer.writeAll(relative_path)?;
                } else {
                    writer.writeAll(source_slice)?;
                }
            } else {
                writer.writeAll(source_slice)?;
            }
        }

        if (source_slice.len > 0 && (_.position.line.isValid() || _.position.column.isValid())) {
            if (_.enable_color) {
                writer.writeAll(comptime Output.prettyFmt("<r><d>:", true))?;
            } else {
                writer.writeAll(":")?;
            }
        }

        if (_.enable_color) {
            if (_.position.line.isValid() || _.position.column.isValid()) {
                writer.writeAll(comptime Output.prettyFmt("<r>", true))?;
            } else {
                writer.writeAll(comptime Output.prettyFmt("<r>", true))?;
            }
        }

        if (!_.exclude_line_column) {
            if (_.position.line.isValid() && _.position.column.isValid()) {
                if (_.enable_color) {
                    writer.print(
                        comptime Output.prettyFmt("<yellow>{d}<r><d>:<yellow>{d}<r>", true),
                        .{ _.position.line.oneBased(), _.position.column.oneBased() },
                    )?;
                } else {
                    writer.print("{d}:{d}", .{ 
                        _.position.line.oneBased(),
                        _.position.column.oneBased(),
                    })?;
                }
            } else if (_.position.line.isValid()) {
                if (_.enable_color) {
                    writer.print(
                        comptime Output.prettyFmt("<yellow>{d}<r>", true),
                        .{ 
                            _.position.line.oneBased(),
                        },
                    )?;
                } else {
                    writer.print("{d}", .{ 
                        _.position.line.oneBased(),
                    })?;
                }
            }
        }
    }
}

const NameFormatterImpl = struct {
    function_name: String,
    code_type: ZigStackFrameCode,
    enable_color: bool,
    is_async: bool,

    pub fn format(writer: *std.Io.Writer): !void {
        const name = _.function_name;

        switch (_.code_type) {
            .Eval => {
                if (_.enable_color) {
                    writer.print(comptime Output.prettyFmt("<r><d>", true) ++ "eval" ++ Output.prettyFmt("<r>", true), .{})?;
                } else {
                    writer.writeAll("eval")?;
                }
                if (!name.isEmpty()) {
                    if (_.enable_color) {
                        writer.print(comptime Output.prettyFmt(" <r><b><i>{f}<r>", true), .{ name })?;
                    } else {
                        writer.print(" {f}", .{ name })?;
                    }
                }
            },
            .Function => {
                if (!name.isEmpty()) {
                    if (_.enable_color) {
                        if (_.is_async) {
                            writer.print(comptime Output.prettyFmt("<r><b><i>async {f}<r>", true), .{ name })?;
                        } else {
                            writer.print(comptime Output.prettyFmt("<r><b><i>{f}<r>", true), .{ name })?;
                        }
                    } else {
                        if (_.is_async) {
                            writer.print("async {f}", .{ name })?;
                        } else {
                            writer.print("{f}", .{ name })?;
                        }
                    }
                } else {
                    if (_.enable_color) {
                        if (_.is_async) {
                            writer.print(comptime Output.prettyFmt("<r><d>", true) ++ "async <anonymous>" ++ Output.prettyFmt("<r>", true), .{})?;
                        } else {
                            writer.print(comptime Output.prettyFmt("<r><d>", true) ++ "<anonymous>" ++ Output.prettyFmt("<r>", true), .{})?;
                        }
                    } else {
                        if (_.is_async) {
                            writer.writeAll("async ")?;
                        }
                        writer.writeAll("<anonymous>")?;
                    }
                }
            },
            .Global => {},
            .Wasm => {
                if (!name.isEmpty()) {
                    writer.print("{f}", .{ name })?;
                } else {
                    writer.writeAll("WASM")?;
                }
            },
            .Constructor => {
                writer.print("new {f}", .{ name })?;
            },
            else => {
                if (!name.isEmpty()) {
                    writer.print("{f}", .{ name })?;
                }
            },
        }
    }
}

/// Represents a single frame in a stack trace
@c pub struct ZigStackFrame {
    function_name: String,
    source_url: String,
    position: ZigStackFramePosition,
    code_type: ZigStackFrameCode,
    is_async: bool,

    /// This informs formatters whether to display as a blob URL or not
    remapped: bool = false,

    /// -1 means not set.
    jsc_stack_frame_index: i32 = -1,

    pub const SourceURLFormatter = SourceURLFormatterImpl;
    pub const NameFormatter = NameFormatterImpl;

    pub const Zero: ZigStackFrame = {
        function_name: .empty,
        code_type: .None,
        source_url: .empty,
        position: .invalid,
        is_async: false,
        jsc_stack_frame_index: -1,
    };

    pub fn deinit(*_) {
        _.function_name.deref();
        _.source_url.deref();
    }

    pub fn toAPI(root_path: string, origin: (*const ZigURL)?, allocator: std.mem.Allocator): !api.StackFrame {
        let frame: api.StackFrame = comptime std.mem.zeroes(api.StackFrame);
        if (!_.function_name.isEmpty()) {
            let slicer = _.function_name.toUTF8(allocator);
            frame.function_name = (slicer.cloneIfBorrowed(allocator)?).slice();
        }

        if (!_.source_url.isEmpty()) {
            frame.file = std.fmt.allocPrint(allocator, "{f}", .{ _.sourceURLFormatter(root_path, origin, true, false) })?;
        }

        frame.position = _.position;
        frame.scope = @as(api.StackFrameScope, @enumFromInt(@intFromEnum(_.code_type)));

        return frame;
    }

    pub fn nameFormatter(comptime enable_color: bool): NameFormatterImpl {
        return NameFormatterImpl{ function_name: _.function_name, code_type: _.code_type, enable_color: enable_color, is_async: _.is_async };
    }

    pub fn sourceURLFormatter(root_path: string, origin: (*const ZigURL)?, exclude_line_column: bool, comptime enable_color: bool): SourceURLFormatterImpl {
        return SourceURLFormatterImpl{
            source_url: _.source_url,
            exclude_line_column: exclude_line_column,
            origin: origin,
            root_path: root_path,
            position: _.position,
            enable_color: enable_color,
            remapped: _.remapped,
        };
    }
}
