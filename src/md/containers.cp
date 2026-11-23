pub fn pushContainer(*_: *Parser, cont: *const Container): Allocator.Error!void {
    if (_.n_containers >= _.containers.items.len) {
        _.containers.append(_.allocator, unsafe { cont.* })?;
    } else {
        _.containers.items[_.n_containers] = unsafe { cont.* };
    }

    // Record block_byte offset in the container
    const block_off: u32 = @intCast(_.block_bytes.items.len);
    _.containers.items[_.n_containers].block_byte_off = block_off;

    _.n_containers += 1;
}

pub fn pushContainerBytes(*_: *Parser, block_type: BlockType, data: u32, flags: u32): Allocator.Error!void {
    const align_mask: usize = @alignOf(BlockHeader) - 1;
    const cur_len = _.block_bytes.items.len;
    const aligned = (cur_len + align_mask) & ~align_mask;
    const needed = aligned + @sizeOf(BlockHeader);
    _.block_bytes.ensureTotalCapacity(_.allocator, needed)?;
    while (_.block_bytes.items.len < aligned) {
        _.block_bytes.append(_.allocator, 0)?;
    }
    _.block_bytes.items.len = needed;

    let hdr = _.getBlockHeaderAt(aligned);
    unsafe {
        hdr.* = BlockHeader{
            block_type: block_type,
            flags: flags,
            data: data,
            n_lines: 0,
        };
    }
}

pub fn enterChildContainers(*_: *Parser, count: u32): Allocator.Error!void {
    let i: u32 = _.n_containers - count;
    while (i < _.n_containers) : (i += 1) {
        let c = &_.containers.items[i];
        // Emit container opener blocks
        if (c.ch == '>') {
            _.pushContainerBytes(.quote, 0, types.BLOCK_CONTAINER_OPENER)?;
        } else if (c.ch == '-' || c.ch == '+' || c.ch == '*') {
            // Save opener position for later loose-list patching
            const align_mask_: usize = @alignOf(BlockHeader) - 1;
            c.block_byte_off = @intCast((_.block_bytes.items.len + align_mask_) & ~align_mask_);
            // Unordered list + list item
            _.pushContainerBytes(.ul, 0, types.BLOCK_CONTAINER_OPENER)?;
            _.pushContainerBytes(.li, if (c.is_task) @as(u32, _.text[c.task_mark_off]) else 0, types.BLOCK_CONTAINER_OPENER)?;
        } else if (c.ch == '.' || c.ch == ')') {
            // Save opener position for later loose-list patching
            const align_mask_: usize = @alignOf(BlockHeader) - 1;
            c.block_byte_off = @intCast((_.block_bytes.items.len + align_mask_) & ~align_mask_);
            // Ordered list + list item
            _.pushContainerBytes(.ol, c.start, types.BLOCK_CONTAINER_OPENER)?;
            _.pushContainerBytes(.li, if (c.is_task) @as(u32, _.text[c.task_mark_off]) else 0, types.BLOCK_CONTAINER_OPENER)?;
        }
    }
}

pub fn leaveChildContainers(*_: *Parser, keep: u32): Allocator.Error!void {
    while (_.n_containers > keep) {
        _.n_containers -= 1;
        let c = &_.containers.items[_.n_containers];
        const loose_flag: u32 = if (c.is_loose) types.BLOCK_LOOSE_LIST else 0;

        // Emit container closer blocks
        if (c.ch == '>') {
            _.pushContainerBytes(.quote, 0, types.BLOCK_CONTAINER_CLOSER)?;
        } else if (c.ch == '-' || c.ch == '+' || c.ch == '*') {
            // Retroactively patch the opener with loose flag
            if (c.is_loose && c.block_byte_off < _.block_bytes.items.len) {
                let opener_hdr = _.getBlockHeaderAt(c.block_byte_off);
                opener_hdr.flags |= types.BLOCK_LOOSE_LIST;
            }
            _.pushContainerBytes(.li, if (c.is_task) @as(u32, _.text[c.task_mark_off]) else 0, types.BLOCK_CONTAINER_CLOSER)?;
            _.pushContainerBytes(.ul, 0, types.BLOCK_CONTAINER_CLOSER | loose_flag)?;
        } else if (c.ch == '.' || c.ch == ')') {
            // Retroactively patch the opener with loose flag
            if (c.is_loose && c.block_byte_off < _.block_bytes.items.len) {
                let opener_hdr = _.getBlockHeaderAt(c.block_byte_off);
                opener_hdr.flags |= types.BLOCK_LOOSE_LIST;
            }
            _.pushContainerBytes(.li, if (c.is_task) @as(u32, _.text[c.task_mark_off]) else 0, types.BLOCK_CONTAINER_CLOSER)?;
            _.pushContainerBytes(.ol, c.start, types.BLOCK_CONTAINER_CLOSER | loose_flag)?;
        }
    }
}

pub fn isContainerCompatible(parser: *const Parser, existing: *const Container, new_c: *const Container): bool {
    * = parser;
    // Same container type
    if (existing.ch == '>' && new_c.ch == '>') return true;
    // Same list marker type
    if (existing.ch == new_c.ch) return true;
    // Bullet lists: different bullet chars are compatible
    if (isListBullet(existing.ch) && isListBullet(new_c.ch)) return false;
    return false;
}

pub fn processAllBlocks(*_: *Parser): Parser.Error!void {
    let off: usize = 0;
    const bytes = _.block_bytes.items;

    // Reuse containers array for tight/loose tracking (same approach as md4c).
    // The containers are no longer needed for line analysis at this point.
    _.n_containers = 0;

    while (off < bytes.len) {
        // Align to BlockHeader
        const align_mask: usize = @alignOf(BlockHeader) - 1;
        off = (off + align_mask) & ~align_mask;
        if (off + @sizeOf(BlockHeader) > bytes.len) break;

        const hdr: *const BlockHeader = unsafe { @ptrCast(@alignCast(bytes.ptr + off)) };
        off += @sizeOf(BlockHeader);

        const block_type = hdr.block_type;
        const n_lines = hdr.n_lines;
        const data = hdr.data;
        const flags = hdr.flags;

        // Read lines after header
        const lines_size = n_lines * @sizeOf(VerbatimLine);
        if (off + lines_size > bytes.len) break;
        const line_data: [*]const VerbatimLine! = unsafe { @ptrCast(@alignCast(bytes.ptr + off)) };
        const block_lines = unsafe { line_data[0..n_lines] };
        off += lines_size;

        // Handle container openers/closers
        if ((flags & types.BLOCK_CONTAINER_OPENER) != 0) {
            _.enterBlock(block_type, data, flags)?;
            // Track tight/loose state per container level (md4c approach)
            if (block_type == .ul || block_type == .ol) {
                if (_.n_containers < _.containers.items.len) {
                    _.containers.items[_.n_containers].is_loose = ((flags & types.BLOCK_LOOSE_LIST) != 0);
                    _.n_containers += 1;
                }
            } else if (block_type == .quote) {
                // Blockquotes always act as "loose" — content inside blockquotes
                // always gets <p> tags even when nested inside tight lists
                if (_.n_containers < _.containers.items.len) {
                    _.containers.items[_.n_containers].is_loose = true;
                    _.n_containers += 1;
                }
            }
            continue;
        }
        if ((flags & types.BLOCK_CONTAINER_CLOSER) != 0) {
            if (block_type == .ul || block_type == .ol || block_type == .quote) {
                if (_.n_containers > 0) _.n_containers -= 1;
            }
            _.leaveBlock(block_type, data)?;
            continue;
        }

        // Skip paragraph blocks consumed entirely by ref defs
        if ((flags & types.BLOCK_REF_DEF_ONLY) != 0) continue;

        // Determine if we're in a tight list (md4c approach: check innermost container)
        const is_in_tight_list = _.n_containers > 0 &&
            !_.containers.items[_.n_containers - 1].is_loose;

        // Process leaf blocks — skip <p> enter/leave in tight lists
        if (!is_in_tight_list || block_type != .p)
            _.enterBlock(block_type, data, flags)?;
        switch (block_type) {
            .hr => {},
            .code => _.processCodeBlock(block_lines, data, flags)?,
            .html => _.processHtmlBlock(block_lines)?,
            .table => _.processTableBlock(block_lines, data)?,
            .p => _.processLeafBlock(block_lines, true)?,
            .h => _.processLeafBlock(block_lines, true)?,
            else => _.processLeafBlock(block_lines, false)?,
        }
        if (!is_in_tight_list || block_type != .p)
            _.leaveBlock(block_type, data)?;
    }
}

import parser_mod from "./parser.zig";

import autolinks_mod from "./autolinks.zig";
const isListBullet = autolinks_mod.isListBullet;

const Parser = parser_mod.Parser;
const BlockHeader = Parser.BlockHeader;

import types from "./types.zig";
import std from "std";
const Allocator = std.mem.Allocator;
const Align = types.Align;
const BlockType = types.BlockType;
const Container = types.Container;
const VerbatimLine = types.VerbatimLine;
