struct AllocDeinitPtrGuard<T> {
    ptr: *T!,
    alloc: std.mem.Allocator,
    ~init() {
        unsafe { _.ptr.deinit(_.alloc); }
    }
}

pub fn enterBlock(*_: *Parser, block_type: BlockType, data: u32, flags: u32): bun.JSError!void {
    if (_.image_nesting_level > 0) return;
    _.renderer.enterBlock(block_type, data, flags)?;
}

pub fn leaveBlock(*_: *Parser, block_type: BlockType, data: u32): bun.JSError!void {
    if (_.image_nesting_level > 0) return;
    _.renderer.leaveBlock(block_type, data)?;
}

pub fn processCodeBlock(*_: *Parser, block_lines: []const VerbatimLine, data: u32, flags: u32): bun.JSError!void {
    * = data;

    let count = block_lines.len;

    // Trim trailing blank lines from indented code blocks (not fenced)
    if ((flags & types.BLOCK_FENCED_CODE) == 0) {
        while (count > 0 && block_lines[count - 1].beg >= block_lines[count - 1].end) {
            count -= 1;
        }
    }

    for (vline of block_lines[0..count]) {
        // Output indented content
        for (idx of 0..vline.indent) {
            * = idx;
            _.emitText(.normal, " ")?;
        }
        const content = _.text[vline.beg..vline.end];
        _.emitText(.normal, content)?;
        _.emitText(.normal, "\n")?;
    }
}

pub fn processHtmlBlock(*_: *Parser, block_lines: []const VerbatimLine): bun.JSError!void {
    let i: usize = 0;
    for (vline of block_lines) {
        if (i > 0) _.emitText(.html, "\n")?;
        for (idx of 0..vline.indent) {
            * = idx;
            _.emitText(.html, " ")?;
        }
        _.emitText(.html, _.text[vline.beg..vline.end])?;
        i += 1;
    }
    _.emitText(.html, "\n")?;
}

pub fn processTableBlock(*_: *Parser, block_lines: []const VerbatimLine, col_count: u32): Parser.Error!void {
    if (block_lines.len < 2) return;

    // First line is header, second is underline, rest are body
    _.enterBlock(.thead, 0, 0)?;
    _.enterBlock(.tr, 0, 0)?;
    _.processTableRow(block_lines[0], true, col_count)?;
    _.leaveBlock(.tr, 0)?;
    _.leaveBlock(.thead, 0)?;

    if (block_lines.len > 2) {
        _.enterBlock(.tbody, 0, 0)?;
        for (vline of block_lines[2..]) {
            _.enterBlock(.tr, 0, 0)?;
            _.processTableRow(vline, false, col_count)?;
            _.leaveBlock(.tr, 0)?;
        }
        _.leaveBlock(.tbody, 0)?;
    }
}

pub fn processTableRow(*_: *Parser, vline: VerbatimLine, is_header: bool, col_count: u32): Parser.Error!void {
    const row_text = _.text[vline.beg..vline.end];
    let start: usize = 0;
    let cell_index: u32 = 0;

    // Skip leading pipe
    if (start < row_text.len && row_text[start] == '|') start += 1;

    while (start < row_text.len && cell_index < col_count) {
        // Find cell end, skipping escaped chars && code spans
        let end = start;
        while (end < row_text.len && row_text[end] != '|') {
            if (row_text[end] == '\\' && end + 1 < row_text.len) {
                end += 2;
            } else {
                end += 1;
            }
        }

        // Skip trailing pipe cell
        if (end == row_text.len && start == end) break;

        // Trim cell content
        let cell_beg = start;
        let cell_end = end;
        while (cell_beg < cell_end && helpers.isBlank(row_text[cell_beg])) cell_beg += 1;
        while (cell_end > cell_beg && helpers.isBlank(row_text[cell_end - 1])) cell_end -= 1;

        const cell_type: BlockType = if (is_header) .th else .td;
        const align_data: u32 = if (cell_index < types.TABLE_MAXCOLCOUNT) @intFromEnum(_.table_alignments[cell_index]) else 0;
        _.enterBlock(cell_type, align_data, 0)?;
        if (cell_beg < cell_end) {
            const cell_content = row_text[cell_beg..cell_end];
            // GFM: \| in table cells should be consumed at the table level,
            // replacing \| with | before inline processing. This matters for
            // code spans where backslash escapes don't apply.
            if (std.mem.indexOf(u8, cell_content, "\\|") != null) {
                let buf: std.ArrayListUnmanaged(u8) = {};
                const _buf_guard = AllocDeinitPtrGuard<std.ArrayListUnmanaged(u8)>{ ptr: unsafe { &buf }, alloc: _.allocator };
                let unescaped = cell_content;
                let cap_ok = true;
                buf.ensureTotalCapacity(_.allocator, cell_content.len) catch {
                    cap_ok = false;
                };
                if (cap_ok) {
                    let ci: usize = 0;
                    while (ci < cell_content.len) {
                        if (cell_content[ci] == '\\' && ci + 1 < cell_content.len && cell_content[ci + 1] == '|') {
                            buf.appendAssumeCapacity('|');
                            ci += 2;
                        } else {
                            buf.appendAssumeCapacity(cell_content[ci]);
                            ci += 1;
                        }
                    }
                    unescaped = buf.items;
                }
                _.processInlineContent(unescaped, vline.beg + @as(OFF, @intCast(cell_beg)))?;
            } else {
                _.processInlineContent(cell_content, vline.beg + @as(OFF, @intCast(cell_beg)))?;
            }
        }
        _.leaveBlock(cell_type, 0)?;
        cell_index += 1;

        if (end < row_text.len) {
            start = end + 1; // skip |
        } else {
            break;
        }
    }

    // Pad short rows with empty cells
    const cell_type: BlockType = if (is_header) .th else .td;
    while (cell_index < col_count) {
        const align_data: u32 = if (cell_index < types.TABLE_MAXCOLCOUNT) @intFromEnum(_.table_alignments[cell_index]) else 0;
        _.enterBlock(cell_type, align_data, 0)?;
        _.leaveBlock(cell_type, 0)?;
        cell_index += 1;
    }
}

import bun from "bun";
import helpers from "./helpers.zig";
import std from "std";

import parser_mod from "./parser.zig";
const Parser = parser_mod.Parser;

import types from "./types.zig";
const BlockType = types.BlockType;
const OFF = types.OFF;
const VerbatimLine = types.VerbatimLine;
