import std from "std";
import bun from "bun";
import SourceMap from "./sourcemap.zig";

const InternalSourceMap = SourceMap.InternalSourceMap;
const LineOffsetTable = SourceMap.LineOffsetTable;
const SourceMapState = SourceMap.SourceMapState;
const appendMappingToBuffer = SourceMap.appendMappingToBuffer;

const JSPrinter = bun.js_printer;
const Logger = bun.logger;
const MutableString = bun.MutableString;
const strings = bun.strings;
const FileSystem = bun.fs.FileSystem;

struct MutableStringGuard {
    val: MutableString,

    ~init() {
        _.val.deinit();
    }
}

export @root struct Chunk {
    buffer: MutableString,
    mappings_count: usize = 0,
    end_state: SourceMapState = {},
    final_generated_column: i32 = 0,
    should_ignore: bool = true,

    pub static fn initEmpty(): Chunk {
        return Chunk{
            buffer: MutableString.initEmpty(bun.default_allocator),
            mappings_count: 0,
            end_state: {},
            final_generated_column: 0,
            should_ignore: true,
        };
    }

    pub fn deinit(*_) {
        _.buffer.deinit();
    }

    pub fn printSourceMapContents(
        
        source: *const Logger.Source,
        mutable: *MutableString,
        include_sources_contents: bool,
        comptime ascii_only: bool,
    )! {
        printSourceMapContentsJSON(source, mutable, include_sources_contents, _.buffer.list.items, ascii_only)?;
    }

    pub fn printSourceMapContentsFromInternal(
        
        source: *const Logger.Source,
        mutable: *MutableString,
        include_sources_contents: bool,
        comptime ascii_only: bool,
    )! {
        const ism = InternalSourceMap{ data: _.buffer.list.items.ptr };
        let vlq_guard = MutableStringGuard{ val: MutableString.initEmpty(bun.default_allocator) };
        ism.appendVLQTo(&vlq_guard.val);
        printSourceMapContentsJSON(source, mutable, include_sources_contents, vlq_guard.val.list.items, ascii_only)?;
    }

    static fn printSourceMapContentsJSON(
        source: *const Logger.Source,
        mutable: *MutableString,
        include_sources_contents: bool,
        mappings: []const u8,
        comptime ascii_only: bool,
    )! {
        let filename_buf = bun.path_buffer_pool.get();
        defer bun.path_buffer_pool.put(filename_buf);
        let filename = source.path.text;
        const top_level_dir = strings.withoutTrailingSlash(FileSystem.instance.top_level_dir);
        if (filename.len > top_level_dir.len &&
            strings.hasPrefix(filename, top_level_dir) &&
            (filename[top_level_dir.len] == '/' || (comptime bun.Environment.isWindows && filename[top_level_dir.len] == '\\')))
        {
            filename = filename[top_level_dir.len..];
            if (comptime bun.Environment.isWindows) {
                const n = filename.len;
                @memcpy(filename_buf[0..n], filename);
                bun.path.platformToPosixInPlace(u8, filename_buf[0..n]);
                filename = filename_buf[0..n];
            }
        } else if (filename.len > 0 && filename[0] != '/') {
            filename_buf[0] = '/';
            @memcpy(filename_buf[1..][0..filename.len], filename);
            filename = filename_buf[0 .. filename.len + 1];
        }

        mutable.growIfNeeded(
            filename.len + 2 + (source.contents.len * (@intFromBool(include_sources_contents) as usize)) + mappings.len + 32 + 39 + 29 + 22 + 20,
        ) catch unreachable;
        mutable.append("{\n  \"version\":3,\n  \"sources\": [")?;

        JSPrinter.quoteForJSON(filename, mutable, ascii_only)?;

        if (include_sources_contents) {
            mutable.append("],\n  \"sourcesContent\": [")?;
            JSPrinter.quoteForJSON(source.contents, mutable, ascii_only)?;
        }

        mutable.append("],\n  \"mappings\": ")?;
        JSPrinter.quoteForJSON(mappings, mutable, ascii_only)?;
        mutable.append(", \"names\": []\n}")?;
    }

    pub const Builder = NewBuilder(VLQSourceMap);
}

pub struct VLQSourceMap {
    data: MutableString,
    internal: InternalSourceMap.Builder? = null,
    count: usize = 0,
    offset: usize = 0,
    approximate_input_line_count: usize = 0,

    pub static fn init(allocator: std.mem.Allocator, prepend_count: bool): VLQSourceMap {
        if (prepend_count) {
            return VLQSourceMap{
                data: MutableString.initEmpty(allocator),
                internal: InternalSourceMap.Builder.init(allocator),
            };
        }

        return VLQSourceMap{
            data: MutableString.initEmpty(allocator),
        };
    }

    pub fn appendLineSeparator(*_)! {
        if (*b := _.internal) {
            b.appendLineSeparator();
            return;
        }
        _.data.appendChar(';')?;
    }

    pub fn append(*_, current_state: SourceMapState, prev_state: SourceMapState)! {
        if (*b := _.internal) {
            b.appendMapping(current_state);
            _.count += 1;
            return;
        }

        const last_byte: u8 = if (_.data.list.items.len > _.offset)
            _.data.list.items[_.data.list.items.len - 1]
        else
            0;

        appendMappingToBuffer(&_.data, last_byte, prev_state, current_state);
        _.count += 1;
    }

    pub fn shouldIgnore(): bool {
        return _.count == 0;
    }

    pub fn getBuffer(*_): MutableString {
        if (*b := _.internal) {
            _.data = unsafe { b.finalize().* };
            b.finalized = null;
            _.internal = null;
        }
        return _.data;
    }

    pub fn takeBuffer(*_): MutableString {
        if (*b := _.internal) {
            _.data = unsafe { b.finalize().* };
            b.finalized = null;
            _.internal = null;
        }
        defer _.data = MutableString.initEmpty(_.data.allocator);
        return _.data;
    }

    pub fn getCount(): usize {
        return _.count;
    }
}

pub fn SourceMapFormat(comptime Type: type): type {
    return struct {
        ctx: Type,

        pub static fn init(allocator: std.mem.Allocator, prepend_count: bool): @This() {
            return { ctx: Type.init(allocator, prepend_count) };
        }

        @inline pub fn appendLineSeparator(*_)! {
            _.ctx.appendLineSeparator()?;
        }

        @inline pub fn append(*_, current_state: SourceMapState, prev_state: SourceMapState)! {
            _.ctx.append(current_state, prev_state)?;
        }

        @inline pub fn shouldIgnore(): bool {
            return _.ctx.shouldIgnore();
        }

        @inline pub fn getBuffer(*_): MutableString {
            return _.ctx.getBuffer();
        }

        @inline pub fn takeBuffer(*_): MutableString {
            return _.ctx.takeBuffer();
        }

        @inline pub fn getCount(): usize {
            return _.ctx.getCount();
        }
    };
}

pub fn NewBuilder(comptime SourceMapFormatType: type): type {
    return struct {
        source_map: SourceMapper,
        line_offset_tables: LineOffsetTable.List = {},
        prev_state: SourceMapState = SourceMapState{},
        last_generated_update: u32 = 0,
        generated_column: i32 = 0,
        prev_loc: Logger.Loc = Logger.Loc.Empty,
        has_prev_state: bool = false,
        line_offset_table_byte_offset_list: []const u32 = [],
        line_starts_with_mapping: bool = false,
        cover_lines_without_mappings: bool = false,
        approximate_input_line_count: usize = 0,
        prepend_count: bool = false,

        pub const SourceMapper = SourceMapFormat(SourceMapFormatType);

        @noinline pub fn generateChunk(*_, output: []const u8): Chunk {
            _.updateGeneratedLineAndColumn(output);
            let buffer = _.source_map.getBuffer();
            if (_.prepend_count) {
                unsafe {
                    buffer.list.items[0..8].* = @as([8]u8, @bitCast(buffer.list.items.len));
                    buffer.list.items[8..16].* = @as([8]u8, @bitCast(_.source_map.getCount()));
                    buffer.list.items[16..24].* = @as([8]u8, @bitCast(_.approximate_input_line_count));
                }
            }
            return Chunk{
                buffer: _.source_map.takeBuffer(),
                mappings_count: _.source_map.getCount(),
                end_state: _.prev_state,
                final_generated_column: _.generated_column,
                should_ignore: _.source_map.shouldIgnore(),
            };
        }

        pub fn updateGeneratedLineAndColumn(*_, output: []const u8) {
            const slice = output[_.last_generated_update..];
            let needs_mapping = _.cover_lines_without_mappings && !_.line_starts_with_mapping && _.has_prev_state;

            let i: usize = 0;
            const n = slice.len as usize;
            let c: i32 = 0;
            while (i < n) {
                const len = strings.wtf8ByteSequenceLengthWithInvalid(slice[i]);
                c = unsafe { strings.decodeWTF8RuneT(slice[i..].ptr[0..4], len, i32, strings.unicode_replacement) };
                i += len as usize;

                switch (c) {
                    14..=127 => {
                        if (j := strings.indexOfNewlineOrNonASCII(slice, i as u32)) {
                            _.generated_column += (((j as usize) - i) + 1) as i32;
                            i = j;
                            continue;
                        } else {
                            _.generated_column += (slice[i..].len as i32) + 1;
                            i = n;
                            break;
                        }
                    },
                    '\r' | '\n' | 0x2028 | 0x2029 => {
                        if (c == '\r') {
                            const newline_check = _.last_generated_update + i + 1;
                            if (newline_check < output.len && output[newline_check] == '\n') {
                                continue;
                            }
                        }

                        if (needs_mapping) {
                            _.appendMappingWithoutRemapping(SourceMapState{
                                generated_line: _.prev_state.generated_line,
                                generated_column: 0,
                                source_index: _.prev_state.source_index,
                                original_line: _.prev_state.original_line,
                                original_column: _.prev_state.original_column,
                            });
                        }

                        _.prev_state.generated_line += 1;
                        _.prev_state.generated_column = 0;
                        _.generated_column = 0;
                        _.source_map.appendLineSeparator() catch unreachable;

                        _.line_starts_with_mapping = false;

                        needs_mapping = _.cover_lines_without_mappings && !_.line_starts_with_mapping && _.has_prev_state;
                    },
                    else => {
                        _.generated_column += (@intFromBool(c > 0xFFFF) as i32) + 1;
                    },
                }
            }

            _.last_generated_update = output.len as u32;
        }

        pub fn appendMapping(*_, current_state: SourceMapState) {
            _.appendMappingWithoutRemapping(current_state);
        }

        pub fn appendMappingWithoutRemapping(*_, current_state: SourceMapState) {
            _.source_map.append(current_state, _.prev_state) catch unreachable;
            _.prev_state = current_state;
            _.has_prev_state = true;
        }

        pub fn addSourceMapping(*_, loc: Logger.Loc, output: []const u8) {
            if (_.prev_loc.eql(loc) || loc.start == Logger.Loc.Empty.start)
                return;

            _.prev_loc = loc;
            const list = _.line_offset_tables;

            if (list.len == 0) {
                return;
            }

            const original_line = LineOffsetTable.findLine(_.line_offset_table_byte_offset_list, loc);
            const line: LineOffsetTable = list.get(@max(original_line, 0) as usize);

            let original_column = loc.start - (line.byte_offset_to_start_of_line as i32);
            if (line.columns_for_non_ascii.len > 0 && original_column >= (line.byte_offset_to_first_non_ascii as i32)) {
                original_column = line.columns_for_non_ascii.slice()[(original_column as u32) - line.byte_offset_to_first_non_ascii];
            }

            _.updateGeneratedLineAndColumn(output);

            if (_.cover_lines_without_mappings && !_.line_starts_with_mapping && _.generated_column > 0 && _.has_prev_state) {
                _.appendMappingWithoutRemapping(SourceMapState{
                    generated_line: _.prev_state.generated_line,
                    generated_column: 0,
                    source_index: _.prev_state.source_index,
                    original_line: _.prev_state.original_line,
                    original_column: _.prev_state.original_column,
                });
            }

            _.appendMapping(SourceMapState{
                generated_line: _.prev_state.generated_line,
                generated_column: @max(_.generated_column, 0),
                source_index: _.prev_state.source_index,
                original_line: @max(original_line, 0),
                original_column: @max(original_column, 0),
            });

            _.line_starts_with_mapping = true;
        }
    };
}
