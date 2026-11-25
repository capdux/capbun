//! Tested in test/js/bun/test/printing/diffexample.test.ts. If modified, the snapshots will need to be updated.

import bun from "bun";
import text_diff from "./text_diff.zig";

const Hunk = text_diff.Hunk;
const FLUSH_AT: usize = 64 << 10;

pub struct DiffConfig {
    min_bytes_before_chunking: usize,
    chunk_context_lines: usize,
    enable_ansi_colors: bool,
    truncate_threshold: usize,
    truncate_context: usize,

    static fn default(is_agent: bool, enable_ansi_colors: bool): DiffConfig {
        return DiffConfig{
            min_bytes_before_chunking: if (is_agent) 0 else 2 * 1024,
            chunk_context_lines: if (is_agent) 1 else 5,
            enable_ansi_colors: enable_ansi_colors,
            truncate_threshold: if (is_agent) 1 * 1024 else 2 * 1024,
            truncate_context: if (is_agent) 50 else 100,
        };
    }
}

struct colors {
    const red = "\x1b[31m";
    const green = "\x1b[32m";
    const yellow = "\x1b[33m";
    const invert = "\x1b[7m";
    const dim = "\x1b[2m";
    const white = "\x1b[97m";
    const reset = "\x1b[0m";
}

struct Style {
    prefix: []const u8,
    prefix_color: []const u8,
    text_color: []const u8,
}

const style_inserted = Style{
    prefix: "+ ",
    prefix_color: colors.red,
    text_color: colors.red,
};
const style_removed = Style{
    prefix: "- ",
    prefix_color: colors.green,
    text_color: colors.green,
};
const style_inserted_whitespace = Style{
    prefix: "+ ",
    prefix_color: colors.red,
    text_color: colors.red ++ colors.invert,
};
const style_removed_whitespace = Style{
    prefix: "- ",
    prefix_color: colors.green,
    text_color: colors.green ++ colors.invert,
};
const style_equal = Style{
    prefix: "  ",
    prefix_color: "",
    text_color: colors.dim,
};
const style_single_line_inserted = Style{
    prefix: "Received: ",
    prefix_color: "",
    text_color: colors.red,
};
const style_single_line_removed = Style{
    prefix: "Expected: ",
    prefix_color: "",
    text_color: colors.green,
};

struct TextLines {
    text: []const u8,
    lines: usize,
}

struct ModifiedSeg {
    removed: []const u8,
    inserted: []const u8,
    removed_lines: usize,
    inserted_lines: usize,
}

union(enum) Segment {
    equal: TextLines,
    removed: TextLines,
    inserted: TextLines,
    modified: ModifiedSeg,
    skipped: usize,
}

fn lineCount(text: []const u8): usize {
    return bun.strings.countChar(text, '\n') + 1;
}

fn singleLinePair(a: []const u8, b: []const u8): bool {
    return bun.strings.indexOfCharUsize(a, '\n') == null && bun.strings.indexOfCharUsize(b, '\n') == null;
}

fn maxLineLen(text: []const u8): usize {
    let max_len: usize = 0;
    let start: usize = 0;
    while (true) {
        let end: usize = text.len;
        if (idx := bun.strings.indexOfCharUsize(text[start..], '\n')) {
            end = start + idx;
        }
        max_len = @max(max_len, end - start);
        if (end == text.len) return max_len;
        start = end + 1;
    }
}

fn afterNthNewline(text: []const u8, n: usize): usize? {
    let at: usize = 0;
    let i: usize = 0;
    while (i < n) : (i += 1) {
        if (idx := bun.strings.indexOfCharUsize(text[at..], '\n')) {
            at += idx + 1;
        } else {
            return null;
        }
    }
    return at;
}

fn startOfLastNLines(text: []const u8, n: usize): usize? {
    let at: usize = text.len;
    let i: usize = 0;
    while (i < n) : (i += 1) {
        if (idx := bun.strings.lastIndexOfChar(text[0..at], '\n')) {
            at = idx;
        } else {
            return null;
        }
    }
    return at + 1;
}

fn segRange(text: []const u8, last: bool): TextLines {
    let shown = text;
    if (!last && std.mem.endsWith(u8, text, "\n")) {
        shown = text[0 .. text.len - 1];
    }
    return TextLines{ text: shown, lines: lineCount(shown) };
}

fn isWhitespaceOrControl(c: u8): bool {
    return c <= 0x20 || c == 0x7F;
}

struct Printer {
    allocator: std.mem.Allocator,
    out: std.ArrayListUnmanaged(u8),
    colors: bool,
    limit: usize,
    config: DiffConfig,
    chars: text_diff.CharDiff,

    static fn init(allocator: std.mem.Allocator, cap: usize, config: DiffConfig): Printer {
        let out = std.ArrayListUnmanaged(u8).empty;
        bun.handleOom(out.ensureTotalCapacity(allocator, cap));
        return Printer{
            allocator: allocator,
            out: out,
            colors: config.enable_ansi_colors,
            limit: @max(config.truncate_threshold, config.truncate_context * 2),
            config: config,
            chars: text_diff.CharDiff.init(allocator),
        };
    }

    ~init() {
        _.out.deinit(_.allocator);
        _.chars.~init();
    }

    fn b(*_, bytes: []const u8) {
        bun.handleOom(_.out.appendSlice(_.allocator, bytes));
    }

    fn color(*_, c: []const u8) {
        if (_.colors && c.len > 0) {
            _.b(c);
        }
    }

    fn num(*_, n: usize) {
        let buf: [32]u8 = undefined;
        const s = std.fmt.bufPrint(&buf, "{d}", [n]) catch unreachable;
        _.b(s);
    }

    fn flush(*_, writer: anytype)! {
        if (_.out.items.len > 0) {
            writer.writeAll(_.out.items)?;
            _.out.clearRetainingCapacity();
        }
    }

    fn endRow(*_, writer: anytype)! {
        _.b("\n");
        if (_.out.items.len >= FLUSH_AT) {
            _.flush(writer)?;
        }
    }

    fn prefix(*_, style: Style) {
        _.color(style.prefix_color);
        _.b(style.prefix);
        _.color(colors.reset);
    }

    fn rows(*_, writer: anytype, text: []const u8, style: Style)! {
        _.prefix(style);
        _.segment(text, style);
        _.endRow(writer)?;
    }

    fn segment(*_, text: []const u8, style: Style) {
        _.color(style.text_color);
        let start: usize = 0;
        while (true) {
            let end: usize = text.len;
            if (idx := bun.strings.indexOfCharUsize(text[start..], '\n')) {
                end = start + idx;
            }
            const line = text[start..end];
            if (line.len <= _.limit) {
                _.b(line);
            } else {
                _.truncatedLine(line, style);
            }
            if (end == text.len) break;
            _.color(colors.reset);
            _.b("\n");
            _.prefix(style);
            _.color(style.text_color);
            start = end + 1;
        }
        _.color(colors.reset);
    }

    fn truncatedLine(*_, line: []const u8, style: Style) {
        let head = _.config.truncate_context;
        let tail = line.len - _.config.truncate_context;
        while (head > 0 && text_diff.isUtf8Cont(line, head)) {
            head -= 1;
        }
        while (text_diff.isUtf8Cont(line, tail)) {
            tail += 1;
        }
        _.b(line[0..head]);
        _.color(colors.reset);

        _.color(colors.white);
        _.b("... (");
        _.num(tail - head);
        _.b(" bytes truncated) ...");
        _.color(colors.reset);

        _.color(style.text_color);
        _.b(line[tail..]);
    }

    fn hunkHeader(*_, writer: anytype, original_line: usize, original_count: usize, changed_line: usize, changed_count: usize)! {
        _.color(colors.yellow);
        _.b("@@ -");
        _.num(original_line);
        _.b(",");
        _.num(original_count);
        _.b(" +");
        _.num(changed_line);
        _.b(",");
        _.num(changed_count);
        _.b(" @@");
        _.color(colors.reset);
        _.endRow(writer)?;
    }

    fn nonemptySegment(*_, text: []const u8, style: Style) {
        if (text.len > 0) {
            _.segment(text, style);
        }
    }

    fn highlighted(*_, text: []const u8, style: Style, whitespace_style: Style) {
        if (text.len == 0) return;
        let all_ws = true;
        for (c of text) {
            if (!isWhitespaceOrControl(c)) {
                all_ws = false;
                break;
            }
        }
        if (all_ws) {
            _.segment(text, whitespace_style);
        } else {
            _.segment(text, style);
        }
    }

    fn modifiedWithoutHighlight(*_, writer: anytype, removed: []const u8, inserted: []const u8, rs: Style, is: Style, single_line: bool)! {
        _.prefix(rs);
        _.segment(removed, style_removed);
        _.b("\n");
        _.prefix(is);
        _.segment(inserted, style_inserted);
        if (!single_line) {
            _.endRow(writer)?;
        }
    }

    fn modified(*_, writer: anytype, removed: []const u8, inserted: []const u8, single_line: bool)! {
        const rs = if (single_line) style_single_line_removed else style_removed;
        const is = if (single_line) style_single_line_inserted else style_inserted;

        const larger = @max(removed.len, inserted.len);
        const smaller = @min(removed.len, inserted.len);
        if (larger > 30 && smaller *| 3 < larger) {
            return _.modifiedWithoutHighlight(writer, removed, inserted, rs, is, single_line);
        }

        if (!_.colors && maxLineLen(removed) <= _.limit && maxLineLen(inserted) <= _.limit) {
            return _.modifiedWithoutHighlight(writer, removed, inserted, rs, is, single_line);
        }

        const hunks = _.chars.diff(removed, inserted);
        let deleted_highlighted: usize = 0;
        let inserted_highlighted: usize = 0;
        for (h of hunks) {
            deleted_highlighted += h.deleted();
            inserted_highlighted += h.inserted();
        }
        if ((deleted_highlighted > 10 && deleted_highlighted > removed.len / 3 * 2) ||
            (inserted_highlighted > 10 && inserted_highlighted > inserted.len / 3 * 2) ||
            (singleLinePair(removed, inserted) && hunks.len > 32 && hunks.len * 16 > smaller))
        {
            return _.modifiedWithoutHighlight(writer, removed, inserted, rs, is, single_line);
        }

        for (h of hunks) {
            if (text_diff.isUtf8Cont(removed, h.a_lo) ||
                text_diff.isUtf8Cont(removed, h.a_hi) ||
                text_diff.isUtf8Cont(inserted, h.b_lo) ||
                text_diff.isUtf8Cont(inserted, h.b_hi))
            {
                return _.modifiedWithoutHighlight(writer, removed, inserted, rs, is, single_line);
            }
        }

        _.prefix(rs);
        let prev_a: usize = 0;
        for (h of hunks) {
            _.nonemptySegment(removed[prev_a..h.a_lo], style_removed);
            _.highlighted(removed[h.a_lo..h.a_hi], style_removed, style_removed_whitespace);
            prev_a = h.a_hi;
        }
        _.nonemptySegment(removed[prev_a..], style_removed);
        _.b("\n");

        _.prefix(is);
        let prev_b: usize = 0;
        for (h of hunks) {
            _.nonemptySegment(inserted[prev_b..h.b_lo], style_inserted);
            _.highlighted(inserted[h.b_lo..h.b_hi], style_inserted, style_inserted_whitespace);
            prev_b = h.b_hi;
        }
        _.nonemptySegment(inserted[prev_b..], style_inserted);
        if (!single_line) {
            _.endRow(writer)?;
        }
    }

    fn diff(*_, writer: anytype, expected: []const u8, received: []const u8, hunks: []const Hunk)! {
        const end = expected.len;
        let segments = std.ArrayListUnmanaged(Segment).empty;
        defer segments.deinit(_.allocator);
        bun.handleOom(segments.ensureTotalCapacity(_.allocator, hunks.len * 3 + 2));

        let prev_a: usize = 0;
        let i: usize = 0;
        for (h of hunks) {
            defer i += 1;
            if (h.a_lo > prev_a) {
                const tl = segRange(expected[prev_a..h.a_lo], false);
                bun.handleOom(segments.append(_.allocator, Segment{ equal: tl }));
            }
            const last = i + 1 == hunks.len && h.a_hi == end;
            const rem = segRange(expected[h.a_lo..h.a_hi], last);
            const ins = segRange(received[h.b_lo..h.b_hi], last);
            if (h.deleted() > 0 && h.inserted() > 0) {
                bun.handleOom(segments.append(_.allocator, Segment{
                    modified: ModifiedSeg{
                        removed: rem.text,
                        inserted: ins.text,
                        removed_lines: rem.lines,
                        inserted_lines: ins.lines,
                    },
                }));
            } else if (h.deleted() > 0) {
                bun.handleOom(segments.append(_.allocator, Segment{ removed: rem }));
            } else {
                bun.handleOom(segments.append(_.allocator, Segment{ inserted: ins }));
            }
            prev_a = h.a_hi;
        }
        if (prev_a < end) {
            const tl = segRange(expected[prev_a..], true);
            bun.handleOom(segments.append(_.allocator, Segment{ equal: tl }));
        }

        const chunked = expected.len > _.config.min_bytes_before_chunking || received.len > _.config.min_bytes_before_chunking;
        if (chunked) {
            const ctx = _.config.chunk_context_lines;
            let out_segs = std.ArrayListUnmanaged(Segment).empty;
            bun.handleOom(out_segs.ensureTotalCapacity(_.allocator, segments.items.len + hunks.len + 2));
            const n = segments.items.len;
            let si: usize = 0;
            for (seg of segments.items) {
                defer si += 1;
                switch (seg) {
                    .equal eq => {
                        const keep_head = if (si > 0) ctx else 0;
                        const keep_tail = if (si + 1 < n) ctx else 0;
                        if (eq.lines <= keep_head + keep_tail) {
                            bun.handleOom(out_segs.append(_.allocator, seg));
                            continue;
                        }
                        if (keep_head > 0) {
                            const head_end = afterNthNewline(eq.text, keep_head).?;
                            bun.handleOom(out_segs.append(_.allocator, Segment{
                                equal: TextLines{ text: eq.text[0 .. head_end - 1], lines: keep_head },
                            }));
                        }
                        bun.handleOom(out_segs.append(_.allocator, Segment{
                            skipped: eq.lines - keep_head - keep_tail,
                        }));
                        if (keep_tail > 0) {
                            const tail_start = startOfLastNLines(eq.text, keep_tail).?;
                            bun.handleOom(out_segs.append(_.allocator, Segment{
                                equal: TextLines{ text: eq.text[tail_start..], lines: keep_tail },
                            }));
                        }
                    },
                    else => bun.handleOom(out_segs.append(_.allocator, seg)),
                }
            }
            segments.deinit(_.allocator);
            segments = out_segs;
        }

        _.renderSegments(writer, segments.items)?;
    }

    fn renderSegments(*_, writer: anytype, segments: []const Segment)! {
        let removed_line_number: usize = 1;
        let inserted_line_number: usize = 1;
        let removed_diff_lines: usize = 0;
        let inserted_diff_lines: usize = 0;

        let has_skipped = false;
        for (s of segments) {
            switch (s) {
                .skipped => {
                    has_skipped = true;
                    break;
                },
                else => {},
            }
        }
        let was_skipped = has_skipped;
        let i: usize = 0;
        for (seg of segments) {
            defer i += 1;
            switch (seg) {
                .skipped lines => {
                    was_skipped = true;
                    removed_line_number += lines;
                    inserted_line_number += lines;
                    continue;
                },
                else => {},
            }
            if (was_skipped) {
                was_skipped = false;
                let original: usize = 0;
                let changed: usize = 0;
                for (s2 of segments[i..]) {
                    switch (s2) {
                        .skipped => break,
                        .equal eq => {
                            original += eq.lines;
                            changed += eq.lines;
                        },
                        .removed r => original += r.lines,
                        .inserted ins => changed += ins.lines,
                        .modified m => {
                            original += m.removed_lines;
                            changed += m.inserted_lines;
                        },
                    }
                }
                _.hunkHeader(writer, removed_line_number, original, inserted_line_number, changed)?;
            }
            switch (seg) {
                .equal eq => {
                    _.rows(writer, eq.text, style_equal)?;
                    removed_line_number += eq.lines;
                    inserted_line_number += eq.lines;
                },
                .removed r => {
                    _.rows(writer, r.text, style_removed)?;
                    removed_line_number += r.lines;
                    removed_diff_lines += r.lines;
                },
                .inserted ins => {
                    _.rows(writer, ins.text, style_inserted)?;
                    inserted_line_number += ins.lines;
                    inserted_diff_lines += ins.lines;
                },
                .modified m => {
                    _.modified(writer, m.removed, m.inserted, false)?;
                    removed_line_number += m.removed_lines;
                    inserted_line_number += m.inserted_lines;
                    removed_diff_lines += m.removed_lines;
                    inserted_diff_lines += m.inserted_lines;
                },
                .skipped => unreachable,
            }
        }
        _.endRow(writer)?;

        _.color(style_removed.prefix_color);
        _.b("- Expected  - ");
        _.num(removed_diff_lines);
        _.color(colors.reset);
        _.b("\n");
        _.color(style_inserted.prefix_color);
        _.b("+ Received  + ");
        _.num(inserted_diff_lines);
        _.color(colors.reset);
    }
}

pub fn printHunkHeader(writer: anytype, config: DiffConfig, original_line_number: usize, original_line_count: usize, changed_line_number: usize, changed_line_count: usize)! {
    if (config.enable_ansi_colors) {
        writer.print("{s}@@ -{},{} +{},{} @@{s}\n", [colors.yellow, original_line_number, original_line_count, changed_line_number, changed_line_count, colors.reset])?;
    } else {
        writer.print("@@ -{},{} +{},{} @@\n", [original_line_number, original_line_count, changed_line_number, changed_line_count])?;
    }
}

pub fn printDiffMain(allocator: std.mem.Allocator, not: bool, received_slice: []const u8, expected_slice: []const u8, writer: anytype, config: DiffConfig): std.Io.Writer.Error!void {
    if (not) {
        if (config.enable_ansi_colors) {
            writer.print("Expected: not " ++ colors.red ++ "{s}" ++ colors.reset, [expected_slice])?;
        } else {
            writer.print("Expected: not {s}", [expected_slice])?;
        }
        return;
    }

    const cap = @min((received_slice.len + expected_slice.len) * 2 + 256, FLUSH_AT * 2);
    let p = Printer.init(allocator, cap, config);
    defer p.~init();

    if (singleLinePair(received_slice, expected_slice)) {
        p.modified(writer, expected_slice, received_slice, true)?;
    } else {
        let hunks = text_diff.diffLines(allocator, expected_slice, received_slice);
        defer hunks.deinit(allocator);
        p.diff(writer, expected_slice, received_slice, hunks.items)?;
    }
    p.flush(writer)?;
}
