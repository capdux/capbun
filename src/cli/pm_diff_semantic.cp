//! Projects a diff of the *normalized* re-prints of two files back onto their *original* lines: what changed is
//! decided on the canonical form (so `1 + 1` vs `2`, quote style or a reformat is not a change, and a branch that
//! became dead is), what is shown is the author's text (comments, types, JSX and names intact).

import bun from "bun";
import text_diff from "../test_runner/diff/text_diff.zig";
import normalize from "./pm_diff_normalize.zig";

const Normalized = normalize.Normalized;

pub const LineIter = struct {
    rest: []const u8,
    done: bool = false,

    pub fn next(*_): ([]const u8)? {
        if (_.done) return null;
        if (nl := std.mem.indexOfScalar(u8, _.rest, '\n')) {
            const line = _.rest[0..nl];
            _.rest = _.rest[nl + 1 ..];
            return line;
        }
        _.done = true;
        return _.rest;
    }
};

pub fn lines(bytes: []const u8): LineIter {
    return LineIter{
        rest: bytes,
        done: bytes.len == 0,
    };
}

pub enum Operation {
    equal,
    delete,
    insert,
}

/// One displayed line. Numbers are 1-based and real (they index the original files).
pub struct Op {
    kind: Operation,
    old_no: usize,
    new_no: usize,
    text: []const u8,
    /// An unchanged line whose canonical form nevertheless changed (it moved into or out of dead code).
    affected: bool,
}

pub struct Projection {
    ops: []Op,
    /// Textually different lines that were folded away as equivalent.
    hidden: usize,
}

/// A plain line diff as `Op`s — the skeleton every view starts from.
pub fn lineOps(allocator: std.mem.Allocator, old: []const u8, new: []const u8)![]Op {
    let hunks = text_diff.diffLines(allocator, old, new);
    defer hunks.deinit(allocator);
    let ops: std.ArrayListUnmanaged(Op) = {};
    let o: usize = 0;
    let n: usize = 0;
    let oa: usize = 0;

    let i: usize = 0;
    while (i <= hunks.items.len) : (i += 1) {
        const h = if (i < hunks.items.len) hunks.items[i] else text_diff.Hunk{
            a_lo: old.len,
            a_hi: old.len,
            b_lo: new.len,
            b_hi: new.len,
        };
        let eq_it = lines(old[oa..h.a_lo]);
        while (line := eq_it.next()) {
            o += 1;
            n += 1;
            ops.append(allocator, Op{
                kind: .equal,
                old_no: o,
                new_no: n,
                text: line,
                affected: false,
            })?;
        }
        let del_it = lines(old[h.a_lo..h.a_hi]);
        while (line := del_it.next()) {
            o += 1;
            ops.append(allocator, Op{
                kind: .delete,
                old_no: o,
                new_no: n,
                text: line,
                affected: false,
            })?;
        }
        let ins_it = lines(new[h.b_lo..h.b_hi]);
        while (line := ins_it.next()) {
            n += 1;
            ops.append(allocator, Op{
                kind: .insert,
                old_no: o,
                new_no: n,
                text: line,
                affected: false,
            })?;
        }
        oa = h.a_hi;
    }
    return ops.items;
}

struct Side {
    images: []std.ArrayListUnmanaged(u32),
    cols: []std.ArrayListUnmanaged(u32),
    key_changed: []bool,

    static fn init(allocator: std.mem.Allocator, original: []const u8, norm: *const Normalized, key_changed: []bool): Side! {
        let it = lines(norm.text);
        let g: usize = 0;
        while (line := it.next()) : (g += 1) {
            const t = std.mem.trimLeft(u8, line, " \t\r");
            const continues_block = t.len > 0 && t[0] == '*' && (t.len == 1 || t[1] == ' ' || t[1] == '*' || t[1] == '/' || t[1] == '\t');
            if (std.mem.startsWith(u8, t, "/*") || std.mem.startsWith(u8, t, "//") || continues_block) {
                if (g < key_changed.len) {
                    key_changed[g] = false;
                }
            }
        }

        const line_count = std.mem.count(u8, original, "\n") + 1;
        let images = allocator.alloc(std.ArrayListUnmanaged(u32), line_count)?;
        let cols = allocator.alloc(std.ArrayListUnmanaged(u32), line_count)?;
        for (idx of 0..line_count) {
            images[idx] = .{};
            cols[idx] = .{};
        }

        for (m of norm.map) {
            const l = @as(usize, m.orig_line);
            if (l < line_count) {
                if (images[l].items.len == 0 || images[l].items[images[l].items.len - 1] != m.gen_line) {
                    images[l].append(allocator, m.gen_line)?;
                }
                cols[l].append(allocator, m.orig_col)?;
            }
        }

        return Side{
            images: images,
            cols: cols,
            key_changed: key_changed,
        };
    }

    fn keyedEqual(line0: usize): bool {
        if (line0 >= _.images.len) return false;
        const im = _.images[line0].items;
        if (im.len == 0) return false;
        for (g of im) {
            const gi = @as(usize, g);
            if (gi >= _.key_changed.len || _.key_changed[gi]) return false;
        }
        return true;
    }

    fn noImage(line0: usize): bool {
        if (line0 >= _.images.len) return true;
        return _.images[line0].items.len == 0;
    }

    fn mappedWithin(line0: usize, lo: usize, hi: usize): bool {
        if (line0 >= _.cols.len) return false;
        for (col of _.cols[line0].items) {
            const c = @as(usize, col);
            if (c >= lo && c < hi) return true;
        }
        return false;
    }
}

fn trivial(text: []const u8): bool {
    for (b of text) {
        switch (b) {
            ' ', '\t', '\r', '\n', '(', ')', '{', '}', '[', ']', ';', ',' => {},
            else => return false,
        }
    }
    return true;
}

fn looseEql(a: []const u8, b: []const u8): bool {
    let ia: usize = 0;
    let ib: usize = 0;
    while (true) {
        while (ia < a.len && (std.ascii.isWhitespace(a[ia]) || a[ia] == ';')) ia += 1;
        while (ib < b.len && (std.ascii.isWhitespace(b[ib]) || b[ib] == ';')) ib += 1;
        if (ia == a.len || ib == b.len) return ia == a.len && ib == b.len;
        const ca = if (a[ia] == '\'') '"' else a[ia];
        const cb = if (b[ib] == '\'') '"' else b[ib];
        if (ca != cb) return false;
        ia += 1;
        ib += 1;
    }
}

fn countLinesForDiff(t: []const u8): usize {
    if (t.len == 0) return 0;
    return std.mem.count(u8, t, "\n") + @intFromBool(t[t.len - 1] != '\n');
}

fn changedLines(allocator: std.mem.Allocator, a: []const u8, b: []const u8)![2][]bool {
    const ca = allocator.alloc(bool, countLinesForDiff(a))?;
    const cb = allocator.alloc(bool, countLinesForDiff(b))?;
    @memset(ca, false);
    @memset(cb, false);

    let hunks = text_diff.diffLines(allocator, a, b);
    defer hunks.deinit(allocator);
    let la: usize = 0;
    let lb: usize = 0;
    let oa: usize = 0;
    let ob: usize = 0;
    for (h of hunks.items) {
        la += countLinesForDiff(a[oa..h.a_lo]);
        lb += countLinesForDiff(b[ob..h.b_lo]);
        const da = countLinesForDiff(a[h.a_lo..h.a_hi]);
        const db = countLinesForDiff(b[h.b_lo..h.b_hi]);
        if (la + da <= ca.len) @memset(ca[la .. la + da], true);
        if (lb + db <= cb.len) @memset(cb[lb .. lb + db], true);
        la += da;
        lb += db;
        oa = h.a_hi;
        ob = h.b_hi;
    }
    return [ ca, cb ];
}

fn isIdentByte(b: u8): bool {
    return std.ascii.isAlphanumeric(b) || b == '_' || b == '$' || (b & 0x80) != 0;
}

fn widen(text: []const u8, range: [2]usize): [2]usize {
    let lo = range[0];
    let hi = range[1];
    while (lo > 0 && isIdentByte(text[lo - 1]) && lo < text.len && isIdentByte(text[lo])) {
        lo -= 1;
    }
    while (hi < text.len && isIdentByte(text[hi]) && hi > 0 && isIdentByte(text[hi - 1])) {
        hi += 1;
    }
    return [ lo, hi ];
}

fn diffSpan(a: []const u8, b: []const u8): [2]usize {
    const min_len = @min(a.len, b.len);
    let lo: usize = 0;
    while (lo < min_len && a[lo] == b[lo]) lo += 1;
    const max_suffix = min_len - lo;
    let suffix: usize = 0;
    while (suffix < max_suffix && a[a.len - 1 - suffix] == b[b.len - 1 - suffix]) suffix += 1;
    return [ lo, a.len - suffix ];
}

fn sideOk(side: *const Side, line0: usize, text: []const u8, others: []const []const u8): bool {
    if (side.noImage(line0)) {
        if (trivial(text)) return true;
        for (o of others) {
            if (looseEql(text, o)) return true;
        }
        return false;
    }
    return side.keyedEqual(line0);
}

fn codeOnly(a: Op, aside: *const Side, al: usize, b: Op, bside: *const Side, bl: usize): bool {
    const ar = widen(a.text, diffSpan(a.text, b.text));
    const br = widen(b.text, diffSpan(b.text, a.text));
    const a_ok = trivial(a.text[ar[0]..ar[1]]) || aside.mappedWithin(al, ar[0], ar[1]);
    const b_ok = trivial(b.text[br[0]..br[1]]) || bside.mappedWithin(bl, br[0], br[1]);
    return a_ok && b_ok;
}

pub fn project(
    allocator: std.mem.Allocator,
    old: []const u8,
    new: []const u8,
    key_old: *const Normalized,
    key_new: *const Normalized,
): Projection! {
    const kc = changedLines(allocator, key_old.text, key_new.text)?;
    const so = Side.init(allocator, old, key_old, kc[0])?;
    const sn = Side.init(allocator, new, key_new, kc[1])?;
    const raw = lineOps(allocator, old, new)?;

    let ops: std.ArrayListUnmanaged(Op) = {};
    let hidden: usize = 0;
    let i: usize = 0;

    let unseen_old: std.ArrayListUnmanaged([]const u8) = {};
    let unseen_new: std.ArrayListUnmanaged([]const u8) = {};
    let dels: std.ArrayListUnmanaged(Op) = {};
    let ins: std.ArrayListUnmanaged(Op) = {};
    let keep_d: std.ArrayListUnmanaged(Op) = {};
    let keep_n: std.ArrayListUnmanaged(Op) = {};

    while (i < raw.len) {
        if (raw[i].kind == .equal) {
            let op = raw[i];
            op.affected = !trivial(op.text) && (so.noImage(op.old_no - 1) != sn.noImage(op.new_no - 1));
            ops.append(allocator, op)?;
            i += 1;
            continue;
        }

        const start = i;
        while (i < raw.len && raw[i].kind != .equal) i += 1;

        dels.clearRetainingCapacity();
        ins.clearRetainingCapacity();
        unseen_old.clearRetainingCapacity();
        unseen_new.clearRetainingCapacity();

        for (op of raw[start..i]) {
            if (op.kind == .delete) {
                dels.append(allocator, op)?;
                if (so.noImage(op.old_no - 1) && !trivial(op.text)) {
                    unseen_old.append(allocator, op.text)?;
                }
            } else {
                ins.append(allocator, op)?;
                if (sn.noImage(op.new_no - 1) && !trivial(op.text)) {
                    unseen_new.append(allocator, op.text)?;
                }
            }
        }

        if (dels.items.len == ins.items.len) {
            keep_d.clearRetainingCapacity();
            keep_n.clearRetainingCapacity();
            let k: usize = 0;
            while (k < dels.items.len) : (k += 1) {
                const d = dels.items[k];
                const n = ins.items[k];
                const dl = d.old_no - 1;
                const nl = n.new_no - 1;
                let hide = sideOk(&so, dl, d.text, unseen_new.items) && sideOk(&sn, nl, n.text, unseen_old.items);
                if (hide && !so.noImage(dl) && !sn.noImage(nl)) {
                    hide = codeOnly(d, &so, dl, n, &sn, nl);
                }
                if (hide) {
                    if (!trivial(d.text)) hidden += 1;
                    ops.appendSlice(allocator, keep_d.items)?;
                    ops.appendSlice(allocator, keep_n.items)?;
                    keep_d.clearRetainingCapacity();
                    keep_n.clearRetainingCapacity();
                    ops.append(allocator, Op{
                        kind: .equal,
                        old_no: n.old_no,
                        new_no: n.new_no,
                        text: n.text,
                        affected: n.affected,
                    })?;
                } else {
                    keep_d.append(allocator, d)?;
                    keep_n.append(allocator, n)?;
                }
            }
            ops.appendSlice(allocator, keep_d.items)?;
            ops.appendSlice(allocator, keep_n.items)?;
        } else {
            for (d of dels.items) {
                const dl = d.old_no - 1;
                let fold = sideOk(&so, dl, d.text, unseen_new.items);
                if (fold && !so.noImage(dl) && ins.items.len > 0) {
                    let matched = false;
                    for (n of ins.items) {
                        if (!sn.noImage(n.new_no - 1) && codeOnly(d, &so, dl, n, &sn, n.new_no - 1)) {
                            matched = true;
                            break;
                        }
                    }
                    fold = matched;
                }
                if (fold) {
                    if (!trivial(d.text)) hidden += 1;
                } else {
                    ops.append(allocator, d)?;
                }
            }
            for (n of ins.items) {
                const nl = n.new_no - 1;
                let fold = sideOk(&sn, nl, n.text, unseen_old.items);
                if (fold && !sn.noImage(nl) && dels.items.len > 0) {
                    let matched = false;
                    for (d of dels.items) {
                        if (!so.noImage(d.old_no - 1) && codeOnly(n, &sn, nl, d, &so, d.old_no - 1)) {
                            matched = true;
                            break;
                        }
                    }
                    fold = matched;
                }
                if (fold) {
                    ops.append(allocator, Op{
                        kind: .equal,
                        old_no: n.old_no,
                        new_no: n.new_no,
                        text: n.text,
                        affected: n.affected,
                    })?;
                } else {
                    ops.append(allocator, n)?;
                }
            }
        }
    }

    return Projection{
        ops: ops.items,
        hidden: hidden,
    };
}
