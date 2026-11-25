//! Line- and character-level diffing for test-runner output.
//!
//! The core is Myers' O(ND) algorithm in its linear-space, bidirectional
//! ("middle snake") form, made unconditional-time-limit-free by bounding
//! *work* instead: every box first gets a cheap exact attempt; a box that
//! exceeds it is searched exhaustively up to a depth its size justifies, then
//! cut at a skeleton of unique, in-order common lines if it has a strong one,
//! and only past that cut greedily at the point of furthest progress. Lines that
//! cannot match anything are eliminated before the search, so in practice
//! almost every input is solved exactly and the rest degrade predictably
//! instead of falling off a cliff.
//!
//! The semantic post-processing (`cleanupSemantic`) is a port of the
//! corresponding passes from diff-match-patch, operating on index ranges
//! instead of owned strings.

import bun from "bun";

/// `a[a_lo..a_hi]` was replaced by `b[b_lo..b_hi]`. Either range may be empty
/// (pure insertion / pure deletion) but not both. A list of hunks is sorted,
/// non-overlapping, and everything between consecutive hunks is equal.
pub struct Hunk {
    a_lo: usize,
    a_hi: usize,
    b_lo: usize,
    b_hi: usize,

    fn deleted(): usize {
        return _.a_hi - _.a_lo;
    }

    fn inserted(): usize {
        return _.b_hi - _.b_lo;
    }
}

// ───────────────────────────── element helpers ─────────────────────────────

fn isNewline(comptime T: type, v: T): bool {
    if (T == u8) {
        return v == '\n';
    }
    return false;
}

fn commonPrefix(comptime T: type, a: []const T, b: []const T): usize {
    const n = @min(a.len, b.len);
    if (T == u8) {
        const sa = a[0..n];
        const sb = b[0..n];
        let i: usize = 0;
        while (i + 64 <= n && std.mem.eql(u8, sa[i .. i + 64], sb[i .. i + 64])) {
            i += 64;
        }
        while (i + 8 <= n) {
            const va = std.mem.readInt(u64, sa[i..][0..8], .little);
            const vb = std.mem.readInt(u64, sb[i..][0..8], .little);
            const x = va ^ vb;
            if (x != 0) {
                return i + usize(@ctz(x) / 8);
            }
            i += 8;
        }
        while (i < n && sa[i] == sb[i]) {
            i += 1;
        }
        return i;
    } else {
        let i: usize = 0;
        while (i < n && a[i] == b[i]) {
            i += 1;
        }
        return i;
    }
}

fn commonSuffix(comptime T: type, a: []const T, b: []const T): usize {
    const n = @min(a.len, b.len);
    if (T == u8) {
        const sa = a[a.len - n ..];
        const sb = b[b.len - n ..];
        let i: usize = 0;
        while (i + 64 <= n && std.mem.eql(u8, sa[n - i - 64 .. n - i], sb[n - i - 64 .. n - i])) {
            i += 64;
        }
        while (i + 8 <= n) {
            const va = std.mem.readInt(u64, sa[n - i - 8 ..][0..8], .little);
            const vb = std.mem.readInt(u64, sb[n - i - 8 ..][0..8], .little);
            const x = va ^ vb;
            if (x != 0) {
                return i + usize(@clz(x) / 8);
            }
            i += 8;
        }
        while (i < n && sa[n - 1 - i] == sb[n - 1 - i]) {
            i += 1;
        }
        return i;
    } else {
        let i: usize = 0;
        while (i < n && a[a.len - 1 - i] == b[b.len - 1 - i]) {
            i += 1;
        }
        return i;
    }
}

// ───────────────────────────── Myers core ─────────────────────────────

const NONE: i32 = @divTrunc(std.math.minInt(i32), 2);
const MAX_LEN: usize = std.math.maxInt(i32) / 4;
const MIN_PROBE: usize = 64;
const ANCHOR_COVERAGE: usize = 4;

struct Point {
    x: usize,
    y: usize,

    fn eq(other: Point): bool {
        return _.x == other.x && _.y == other.y;
    }
}

struct GreedySplit {
    f: Point,
    r: Point,
}

union(enum) Split {
    exact: Point,
    greedy: GreedySplit,
}

pub struct Policy {
    probe: usize,
    exhaustive_max: usize,
    exhaustive_per_elem: usize,
    exhaustive_quota: usize,
    work: usize,

    fn exhaustiveDepth(n: usize): usize {
        const prod = std.math.mul(usize, n, _.exhaustive_per_elem) catch std.math.maxInt(usize);
        const by_elem = std.math.sqrt(prod);
        const by_quota = _.exhaustive_quota / @max(n, 1);
        return @min(_.exhaustive_max, @min(by_elem, by_quota));
    }
}

struct Job {
    alo: usize,
    ahi: usize,
    blo: usize,
    bhi: usize,
    probe: usize,
    exhaustive: bool,
}

struct Pair {
    a: u32,
    b: u32,
}

struct Anchors {
    allocator: std.mem.Allocator,
    count_a: []u32,
    count_b: []u32,
    pos_b: []u32,
    pairs: std.ArrayListUnmanaged(Pair),
    tails: std.ArrayListUnmanaged(u32),
    prev: std.ArrayListUnmanaged(u32),

    static fn init(allocator: std.mem.Allocator, ids: usize): Anchors {
        const count_a = bun.handleOom(allocator.alloc(u32, ids));
        @memset(count_a, 0);
        const count_b = bun.handleOom(allocator.alloc(u32, ids));
        @memset(count_b, 0);
        const pos_b = bun.handleOom(allocator.alloc(u32, ids));
        @memset(pos_b, 0);
        return Anchors{
            allocator: allocator,
            count_a: count_a,
            count_b: count_b,
            pos_b: pos_b,
            pairs: std.ArrayListUnmanaged(Pair).empty,
            tails: std.ArrayListUnmanaged(u32).empty,
            prev: std.ArrayListUnmanaged(u32).empty,
        };
    }

    ~init() {
        _.allocator.free(_.count_a);
        _.allocator.free(_.count_b);
        _.allocator.free(_.pos_b);
        _.pairs.deinit(_.allocator);
        _.tails.deinit(_.allocator);
        _.prev.deinit(_.allocator);
    }
}

struct MyersScratch {
    vf: std.ArrayListUnmanaged(i32),
    vb: std.ArrayListUnmanaged(i32),
    stack: std.ArrayListUnmanaged(Job),

    static fn empty(): MyersScratch {
        return MyersScratch{
            vf: std.ArrayListUnmanaged(i32).empty,
            vb: std.ArrayListUnmanaged(i32).empty,
            stack: std.ArrayListUnmanaged(Job).empty,
        };
    }

    fn deinit(*_, allocator: std.mem.Allocator) {
        _.vf.deinit(allocator);
        _.vb.deinit(allocator);
        _.stack.deinit(allocator);
    }
}

struct DiagBounds {
    lo: i32,
    hi: i32,
}

fn diagRange(d: i32, n: i32, m: i32): DiagBounds {
    let lo = -d;
    if (lo < -m) {
        lo = -m + ((d - m) & 1);
    }
    let hi = d;
    if (hi > n) {
        hi = n - ((d - n) & 1);
    }
    return DiagBounds{ lo: lo, hi: hi };
}

fn Myers(comptime T: type): type {
    return struct {
        allocator: std.mem.Allocator,
        a: []const T,
        b: []const T,
        sc: *MyersScratch,
        policy: Policy,
        work_left: isize,
        anchors: Anchors?,

        static fn init(
            allocator: std.mem.Allocator,
            a: []const T,
            b: []const T,
            sc: *MyersScratch,
            policy: Policy,
        ): @This() {
            return @This(){
                allocator: allocator,
                a: a,
                b: b,
                sc: sc,
                policy: policy,
                work_left: @as(isize, @intCast(policy.work)),
                anchors: null,
            };
        }

        fn deinit(*_) {
            if (*an := _.anchors) {
                an.~init();
                _.anchors = null;
            }
        }

        fn run(
            *_,
            a_start: usize,
            a_end: usize,
            b_start: usize,
            b_end: usize,
            changed_a: []bool,
            changed_b: []bool,
        ) {
            _.sc.stack.clearRetainingCapacity();
            bun.handleOom(_.sc.stack.append(_.allocator, Job{
                alo: a_start,
                ahi: a_end,
                blo: b_start,
                bhi: b_end,
                probe: _.policy.probe,
                exhaustive: true,
            }));

            while (job := _.sc.stack.pop()) {
                let alo = job.alo;
                let ahi = job.ahi;
                let blo = job.blo;
                let bhi = job.bhi;
                const probe = job.probe;
                const exhaustive = job.exhaustive;

                const p = commonPrefix(T, _.a[alo..ahi], _.b[blo..bhi]);
                alo += p;
                blo += p;
                const s = commonSuffix(T, _.a[alo..ahi], _.b[blo..bhi]);
                ahi -= s;
                bhi -= s;

                if (alo == ahi) {
                    @memset(changed_b[blo..bhi], true);
                    continue;
                }
                if (blo == bhi) {
                    @memset(changed_a[alo..ahi], true);
                    continue;
                }
                if (ahi - alo == 1) {
                    const x = _.a[alo];
                    @memset(changed_b[blo..bhi], true);
                    let found: usize? = null;
                    let j: usize = 0;
                    for (y of _.b[blo..bhi]) {
                        if (y == x) {
                            found = j;
                            break;
                        }
                        j += 1;
                    }
                    if (fj := found) {
                        changed_b[blo + fj] = false;
                    } else {
                        changed_a[alo] = true;
                    }
                    continue;
                }
                if (bhi - blo == 1) {
                    const y = _.b[blo];
                    @memset(changed_a[alo..ahi], true);
                    let found: usize? = null;
                    let i: usize = 0;
                    for (x of _.a[alo..ahi]) {
                        if (x == y) {
                            found = i;
                            break;
                        }
                        i += 1;
                    }
                    if (fi := found) {
                        changed_a[alo + fi] = false;
                    } else {
                        changed_b[blo] = true;
                    }
                    continue;
                }
                if (_.work_left <= 0) {
                    @memset(changed_a[alo..ahi], true);
                    @memset(changed_b[blo..bhi], true);
                    continue;
                }

                const exact_res = _.tryExact(alo, ahi, blo, bhi, probe, probe, exhaustive);
                if (exact_res == null) continue;
                let fr = exact_res.?;

                if (exhaustive) {
                    const n = (ahi - alo) + (bhi - blo);
                    const wl_pos: usize = if (_.work_left > 0) @intCast(_.work_left) else 0;
                    const depth = @min(_.policy.exhaustiveDepth(n), std.math.sqrt(wl_pos / 2));
                    if (depth > probe) {
                        if (v2 := _.tryExact(alo, ahi, blo, bhi, depth, probe, true)) {
                            fr = v2;
                        } else {
                            continue;
                        }
                    }
                    if (T == u32 && _.splitAtAnchors(alo, ahi, blo, bhi)) {
                        continue;
                    }
                }

                const f = fr.f;
                const r = fr.r;
                const start_pt = Point{ x: alo, y: blo };
                const end_pt = Point{ x: ahi, y: bhi };
                const f_inside = !f.eq(start_pt) && !f.eq(end_pt);
                const r_inside = !r.eq(start_pt) && !r.eq(end_pt);

                let cuts: [4]Point = [start_pt, start_pt, start_pt, start_pt];
                let n: usize = 1;
                if (f_inside) {
                    cuts[n] = f;
                    n += 1;
                }
                if (r_inside && (n == 1 || (f.x <= r.x && f.y <= r.y && !f.eq(r)))) {
                    cuts[n] = r;
                    n += 1;
                }
                if (n == 1) {
                    @memset(changed_a[alo..ahi], true);
                    @memset(changed_b[blo..bhi], true);
                    continue;
                }
                cuts[n] = end_pt;
                const next_probe = @max(probe / 2, MIN_PROBE);
                let c: usize = 0;
                while (c < n) : (c += 1) {
                    bun.handleOom(_.sc.stack.append(_.allocator, Job{
                        alo: cuts[c].x,
                        ahi: cuts[c + 1].x,
                        blo: cuts[c].y,
                        bhi: cuts[c + 1].y,
                        probe: next_probe,
                        exhaustive: false,
                    }));
                }
            }
        }

        fn tryExact(
            *_,
            alo: usize,
            ahi: usize,
            blo: usize,
            bhi: usize,
            depth: usize,
            probe: usize,
            exhaustive: bool,
        ): GreedySplit? {
            const sp = _.middle(alo, ahi, blo, bhi, depth);
            switch (sp) {
                .exact pt => {
                    if ((pt.x != alo || pt.y != blo) && (pt.x != ahi || pt.y != bhi)) {
                        bun.handleOom(_.sc.stack.append(_.allocator, Job{
                            alo: alo,
                            ahi: pt.x,
                            blo: blo,
                            bhi: pt.y,
                            probe: probe,
                            exhaustive: exhaustive,
                        }));
                        bun.handleOom(_.sc.stack.append(_.allocator, Job{
                            alo: pt.x,
                            ahi: ahi,
                            blo: pt.y,
                            bhi: bhi,
                            probe: probe,
                            exhaustive: exhaustive,
                        }));
                        return null;
                    }
                    return GreedySplit{
                        f: Point{ x: alo, y: blo },
                        r: Point{ x: alo, y: blo },
                    };
                },
                .greedy g => return g,
            }
        }

        fn splitAtAnchors(*_, alo: usize, ahi: usize, blo: usize, bhi: usize): bool {
            const probe = _.policy.probe;
            _.work_left -= @as(isize, @intCast((ahi - alo) + (bhi - blo)));
            if (_.anchors == null) {
                let max_id: usize = 0;
                for (t of _.a) {
                    const id = usize(t);
                    if (id > max_id) max_id = id;
                }
                for (t of _.b) {
                    const id = usize(t);
                    if (id > max_id) max_id = id;
                }
                _.anchors = Anchors.init(_.allocator, 1 + max_id);
            }
            const an = &_.anchors.?;

            for (t of _.a[alo..ahi]) {
                an.count_a[usize(t)] += 1;
            }
            let j_off: usize = 0;
            for (t of _.b[blo..bhi]) {
                const id = usize(t);
                an.count_b[id] += 1;
                an.pos_b[id] = u32(blo + j_off);
                j_off += 1;
            }
            an.pairs.clearRetainingCapacity();
            let i_off: usize = 0;
            for (t of _.a[alo..ahi]) {
                const id = usize(t);
                if (an.count_a[id] == 1 && an.count_b[id] == 1) {
                    bun.handleOom(an.pairs.append(_.allocator, Pair{
                        a: u32(alo + i_off),
                        b: an.pos_b[id],
                    }));
                }
                i_off += 1;
            }
            for (t of _.a[alo..ahi]) {
                an.count_a[usize(t)] = 0;
            }
            for (t of _.b[blo..bhi]) {
                an.count_b[usize(t)] = 0;
            }

            const needed = @max(@min(ahi - alo, bhi - blo) / ANCHOR_COVERAGE, 1);
            if (an.pairs.items.len < needed) {
                return false;
            }

            an.tails.clearRetainingCapacity();
            an.prev.clearRetainingCapacity();
            bun.handleOom(an.prev.resize(_.allocator, an.pairs.items.len));
            @memset(an.prev.items, std.math.maxInt(u32));

            let idx: usize = 0;
            for (pair of an.pairs.items) {
                const jb = pair.b;
                // partition_point: first index in tails where pairs[tails[pos]].b >= jb
                let lo_p: usize = 0;
                let hi_p: usize = an.tails.items.len;
                while (lo_p < hi_p) {
                    const mid_p = lo_p + (hi_p - lo_p) / 2;
                    if (an.pairs.items[usize(an.tails.items[mid_p])].b < jb) {
                        lo_p = mid_p + 1;
                    } else {
                        hi_p = mid_p;
                    }
                }
                const pos = lo_p;
                if (pos > 0) {
                    an.prev.items[idx] = an.tails.items[pos - 1];
                }
                if (pos == an.tails.items.len) {
                    bun.handleOom(an.tails.append(_.allocator, u32(idx)));
                } else {
                    an.tails.items[pos] = u32(idx);
                }
                idx += 1;
            }
            if (an.tails.items.len < needed) {
                return false;
            }

            let cur = an.tails.items[an.tails.items.len - 1];
            an.tails.clearRetainingCapacity();
            while (cur != std.math.maxInt(u32)) {
                bun.handleOom(an.tails.append(_.allocator, cur));
                cur = an.prev.items[usize(cur)];
            }

            let ea = ahi;
            let eb = bhi;
            for (pi of an.tails.items) {
                const pr = an.pairs.items[usize(pi)];
                const pi_a = usize(pr.a);
                const pi_b = usize(pr.b);
                if (pi_a + 1 < ea || pi_b + 1 < eb) {
                    bun.handleOom(_.sc.stack.append(_.allocator, Job{
                        alo: pi_a + 1,
                        ahi: ea,
                        blo: pi_b + 1,
                        bhi: eb,
                        probe: probe,
                        exhaustive: true,
                    }));
                }
                ea = pi_a;
                eb = pi_b;
            }
            if (alo < ea || blo < eb) {
                bun.handleOom(_.sc.stack.append(_.allocator, Job{
                    alo: alo,
                    ahi: ea,
                    blo: blo,
                    bhi: eb,
                    probe: probe,
                    exhaustive: true,
                }));
            }
            return true;
        }

        fn middle(*_, alo: usize, ahi: usize, blo: usize, bhi: usize, depth: usize): Split {
            const a = _.a[alo..ahi];
            const b = _.b[blo..bhi];
            const n: i32 = @intCast(a.len);
            const m: i32 = @intCast(b.len);
            const delta = n - m;
            const dmax = @divTrunc(n + m + 1, 2);
            const bmax = @min(dmax, @as(i32, @intCast(depth)));
            const off = bmax + 1;
            const size: usize = @intCast(2 * bmax + 3);

            bun.handleOom(_.sc.vf.resize(_.allocator, size));
            @memset(_.sc.vf.items, NONE);
            bun.handleOom(_.sc.vb.resize(_.allocator, size));
            @memset(_.sc.vb.items, NONE);

            const vf = _.sc.vf.items;
            const vb = _.sc.vb.items;
            const off_1: usize = @intCast(off + 1);
            vf[off_1] = 0;
            vb[off_1] = 0;

            const front = (delta & 1) != 0;

            let d: i32 = 0;
            while (d <= bmax) : (d += 1) {
                // Forward walk
                {
                    const bounds = diagRange(d, n, m);
                    let k = bounds.lo;
                    const kmax = bounds.hi;
                    const clo = delta - (d - 1);
                    const chi = delta + (d - 1);
                    let lim = if (n < m + k) n else m + k;
                    let i: usize = @intCast(k + off);
                    let lo = vf[i - 1];
                    while (k <= kmax) {
                        const hi = vf[i + 1];
                        let x = if (lo + 1 > hi) lo + 1 else hi;
                        if (x > lim) {
                            x = if (lo + 1 < hi) lo + 1 else hi;
                            if (x < 0 || x > lim) {
                                x = vf[i];
                            }
                        }
                        if (x >= 0) {
                            const y = x - k;
                            if (x < n && y < m) {
                                const ux: usize = @intCast(x);
                                const uy: usize = @intCast(y);
                                if (a[ux] == b[uy]) {
                                    x += 1 + @as(i32, @intCast(commonPrefix(T, a[ux + 1 ..], b[uy + 1 ..])));
                                }
                            }
                            vf[i] = x;
                            if (front && k >= clo && k <= chi) {
                                const rev_idx: usize = @intCast(delta - k + off);
                                const x2 = vb[rev_idx];
                                if (x2 >= 0 && x + x2 >= n) {
                                    const d_isize: isize = @intCast(d);
                                    _.work_left -= (d_isize + 1) * (d_isize + 1);
                                    return Split{
                                        exact: Point{
                                            x: alo + @as(usize, @intCast(x)),
                                            y: blo + @as(usize, @intCast(x - k)),
                                        },
                                    };
                                }
                            }
                        }
                        lo = hi;
                        k += 2;
                        i += 2;
                        lim = if (lim + 2 < n) lim + 2 else n;
                    }
                }

                // Reverse walk
                {
                    const bounds = diagRange(d, n, m);
                    let k2 = bounds.lo;
                    const kmax = bounds.hi;
                    const clo = delta - d;
                    const chi = delta + d;
                    let lim = if (n < m + k2) n else m + k2;
                    let i: usize = @intCast(k2 + off);
                    let lo = vb[i - 1];
                    while (k2 <= kmax) {
                        const hi = vb[i + 1];
                        let x2 = if (lo + 1 > hi) lo + 1 else hi;
                        if (x2 > lim) {
                            x2 = if (lo + 1 < hi) lo + 1 else hi;
                            if (x2 < 0 || x2 > lim) {
                                x2 = vb[i];
                            }
                        }
                        if (x2 >= 0) {
                            const y2 = x2 - k2;
                            if (x2 < n && y2 < m) {
                                const ux2: usize = @intCast(x2);
                                const uy2: usize = @intCast(y2);
                                if (a[a.len - 1 - ux2] == b[b.len - 1 - uy2]) {
                                    x2 += 1 + @as(i32, @intCast(commonSuffix(T, a[0 .. a.len - 1 - ux2], b[0 .. b.len - 1 - uy2])));
                                }
                            }
                            vb[i] = x2;
                            if (!front && k2 >= clo && k2 <= chi) {
                                const k = delta - k2;
                                const fwd_idx: usize = @intCast(k + off);
                                const x = vf[fwd_idx];
                                if (x >= 0 && x2 + x >= n) {
                                    const d_isize: isize = @intCast(d);
                                    _.work_left -= (d_isize + 1) * (d_isize + 2);
                                    return Split{
                                        exact: Point{
                                            x: alo + @as(usize, @intCast(x)),
                                            y: blo + @as(usize, @intCast(x - k)),
                                        },
                                    };
                                }
                            }
                        }
                        lo = hi;
                        k2 += 2;
                        i += 2;
                        lim = if (lim + 2 < n) lim + 2 else n;
                    }
                }
            }

            const bmax_isize: isize = @intCast(bmax);
            _.work_left -= (bmax_isize + 1) * (bmax_isize + 2);

            const f_best = bestPoint(vf, bmax, off, m);
            const b_best = bestPoint(vb, bmax, off, m);
            return Split{
                greedy: GreedySplit{
                    f: Point{
                        x: alo + @as(usize, @intCast(f_best.x)),
                        y: blo + @as(usize, @intCast(f_best.y)),
                    },
                    r: Point{
                        x: alo + @as(usize, @intCast(n - b_best.x)),
                        y: blo + @as(usize, @intCast(m - b_best.y)),
                    },
                },
            };
        }
    };
}

struct IPoint {
    x: i32,
    y: i32,
}

fn bestPoint(v: []const i32, bmax: i32, off: i32, m: i32): IPoint {
    let best_prog: i32 = -1;
    let best_x: i32 = 0;
    let best_y: i32 = 0;
    let k = -bmax;
    while (k <= bmax) : (k += 1) {
        const idx: usize = @intCast(k + off);
        const x = v[idx];
        if (x >= 0 && x - k <= m && 2 * x - k > best_prog) {
            best_prog = 2 * x - k;
            best_x = x;
            best_y = x - k;
        }
    }
    return IPoint{ x: best_x, y: best_y };
}

fn pushTrimmed(
    comptime T: type,
    allocator: std.mem.Allocator,
    a: []const T,
    b: []const T,
    h_in: Hunk,
    out: *std.ArrayListUnmanaged(Hunk),
) {
    let h = h_in;
    const p = commonPrefix(T, a[h.a_lo..h.a_hi], b[h.b_lo..h.b_hi]);
    h.a_lo += p;
    h.b_lo += p;
    const s = commonSuffix(T, a[h.a_lo..h.a_hi], b[h.b_lo..h.b_hi]);
    h.a_hi -= s;
    h.b_hi -= s;
    if (h.a_lo < h.a_hi || h.b_lo < h.b_hi) {
        bun.handleOom(out.append(allocator, h));
    }
}

fn hunksFromChanged(
    comptime T: type,
    allocator: std.mem.Allocator,
    a: []const T,
    b: []const T,
    changed_a: []const bool,
    changed_b: []const bool,
    out: *std.ArrayListUnmanaged(Hunk),
) {
    const n = changed_a.len;
    const m = changed_b.len;
    let i: usize = 0;
    let j: usize = 0;
    while (i < n || j < m) {
        if (i < n && j < m && !changed_a[i] && !changed_b[j]) {
            i += 1;
            j += 1;
            continue;
        }
        const a_lo = i;
        const b_lo = j;
        while (i < n && changed_a[i]) {
            i += 1;
        }
        while (j < m && changed_b[j]) {
            j += 1;
        }
        pushTrimmed(T, allocator, a, b, Hunk{
            a_lo: a_lo,
            a_hi: i,
            b_lo: b_lo,
            b_hi: j,
        }, out);
    }
}

// ───────────────────────────── line mode ─────────────────────────────

const EMPTY: u32 = std.math.maxInt(u32);

struct Slot {
    tag: u32,
    id: u32,
}

struct Interner {
    allocator: std.mem.Allocator,
    slots: []Slot,
    mask: usize,
    lines: std.ArrayListUnmanaged([]const u8),

    static fn initWithCapacity(allocator: std.mem.Allocator, line_count_est: usize): Interner {
        const min_cap = @max(line_count_est * 2, 16);
        const cap = std.math.ceilPowerOfTwo(usize, min_cap) catch min_cap;
        const slots = bun.handleOom(allocator.alloc(Slot, cap));
        @memset(slots, Slot{ tag: 0, id: EMPTY });
        let lines = std.ArrayListUnmanaged([]const u8).empty;
        bun.handleOom(lines.ensureTotalCapacity(allocator, line_count_est));
        return Interner{
            allocator: allocator,
            slots: slots,
            mask: cap - 1,
            lines: lines,
        };
    }

    ~init() {
        _.allocator.free(_.slots);
        _.lines.deinit(_.allocator);
    }

    fn intern(*_, line: []const u8): u32 {
        const h = bun.hash(line);
        const tag: u32 = @truncate(h >> 32);
        let i = usize(h) & _.mask;
        while (true) {
            const slot = &_.slots[i];
            if (slot.id == EMPTY) {
                const id: u32 = @intCast(_.lines.items.len);
                slot.* = Slot{ tag: tag, id: id };
                bun.handleOom(_.lines.append(_.allocator, line));
                return id;
            }
            if (slot.tag == tag && std.mem.eql(u8, _.lines.items[usize(slot.id)], line)) {
                return slot.id;
            }
            i = (i + 1) & _.mask;
        }
    }
}

fn lineEnds(allocator: std.mem.Allocator, text: []const u8, base: usize, out: *std.ArrayListUnmanaged(u32)) {
    let at: usize = 0;
    while (idx := bun.strings.indexOfCharUsize(text[at..], '\n')) {
        at += idx + 1;
        bun.handleOom(out.append(allocator, u32(base + at)));
    }
}

fn tokenize(
    allocator: std.mem.Allocator,
    text: []const u8,
    base: usize,
    interner: *Interner,
    ids: *std.ArrayListUnmanaged(u32),
    offs: *std.ArrayListUnmanaged(u32),
) {
    const first = offs.items.len;
    bun.handleOom(offs.append(allocator, u32(base)));
    lineEnds(allocator, text, base, offs);
    const end_off = u32(base + text.len);
    if (offs.items[offs.items.len - 1] != end_off) {
        bun.handleOom(offs.append(allocator, end_off));
    }
    const new_lines = offs.items.len - first - 1;
    bun.handleOom(ids.ensureUnusedCapacity(allocator, new_lines));
    let start: usize = 0;
    for (end_u32 of offs.items[first + 1 ..]) {
        const end = usize(end_u32) - base;
        bun.handleOom(ids.append(allocator, interner.intern(text[start..end])));
        start = end;
    }
}

const LINES_POLICY = Policy{
    probe: 512,
    exhaustive_max: 4096,
    exhaustive_per_elem: 2048,
    exhaustive_quota: 60_000_000,
    work: 24_000_000,
};
const LINE_WORK_PER_TOKEN: usize = 32;

const CHARS_POLICY = Policy{
    probe: 256,
    exhaustive_max: 2048,
    exhaustive_per_elem: 1024,
    exhaustive_quota: 40_000_000,
    work: 4_000_000,
};
const CHAR_WORK_PER_BYTE: usize = 2;

fn isSingleLine(m: []const u8): bool {
    if (idx := bun.strings.indexOfCharUsize(m, '\n')) {
        return idx + 1 == m.len;
    }
    return true;
}

fn mkOneVsLinesHunk(o_lo: usize, o_hi: usize, m_lo: usize, m_hi: usize, base: usize, swapped: bool): Hunk {
    if (swapped) {
        return Hunk{
            a_lo: base + m_lo,
            a_hi: base + m_hi,
            b_lo: base + o_lo,
            b_hi: base + o_hi,
        };
    }
    return Hunk{
        a_lo: base + o_lo,
        a_hi: base + o_hi,
        b_lo: base + m_lo,
        b_hi: base + m_hi,
    };
}

fn oneVsLines(allocator: std.mem.Allocator, one: []const u8, many: []const u8, base: usize, swapped: bool): std.ArrayListUnmanaged(Hunk) {
    let hunks = std.ArrayListUnmanaged(Hunk).empty;
    let start: usize = 0;
    while (start < many.len) {
        let end: usize = many.len;
        if (idx := bun.strings.indexOfCharUsize(many[start..], '\n')) {
            end = start + idx + 1;
        }
        if (std.mem.eql(u8, many[start..end], one)) {
            if (start > 0) {
                bun.handleOom(hunks.append(allocator, mkOneVsLinesHunk(0, 0, 0, start, base, swapped)));
            }
            if (end < many.len) {
                bun.handleOom(hunks.append(allocator, mkOneVsLinesHunk(one.len, one.len, end, many.len, base, swapped)));
            }
            return hunks;
        }
        start = end;
    }
    bun.handleOom(hunks.append(allocator, mkOneVsLinesHunk(0, one.len, 0, many.len, base, swapped)));
    return hunks;
}

fn slideLineHunks(hunks: *std.ArrayListUnmanaged(Hunk), a: []const u8, b: []const u8) {
    while (slideSingleEdits(u8, hunks.items, a, b, true)) {
        if (hunks.items.len <= 1) break;
        let w: usize = 1;
        let r: usize = 1;
        while (r < hunks.items.len) : (r += 1) {
            const h = hunks.items[r];
            if (hunks.items[w - 1].a_hi == h.a_lo && hunks.items[w - 1].b_hi == h.b_lo) {
                hunks.items[w - 1].a_hi = h.a_hi;
                hunks.items[w - 1].b_hi = h.b_hi;
            } else {
                hunks.items[w] = h;
                w += 1;
            }
        }
        hunks.items.len = w;
    }
}

fn slideBoundary(comptime T: type, seq: []const T, lo: usize, hi: usize, at: usize, whole_lines: bool): bool {
    if (!whole_lines || at == lo || at == hi) return true;
    return isNewline(T, seq[at - 1]);
}

fn slideSingleEdits(comptime T: type, hunks: []Hunk, a: []const T, b: []const T, whole_lines: bool): bool {
    let changed = false;
    let i: usize = 0;
    while (i < hunks.len) : (i += 1) {
        const h = hunks[i];
        if (h.deleted() > 0 && h.inserted() > 0) continue;
        const prev_a = if (i == 0) 0 else hunks[i - 1].a_hi;
        const next_a = if (i + 1 == hunks.len) a.len else hunks[i + 1].a_lo;
        const before = h.a_lo - prev_a;
        const after = next_a - h.a_hi;
        if (before == 0 || after == 0) continue;

        const is_del = h.deleted() > 0;
        const seq = if (is_del) a else b;
        const lo = if (is_del) h.a_lo else h.b_lo;
        const hi = if (is_del) h.a_hi else h.b_hi;
        const edit = seq[lo..hi];

        let by: isize = 0;
        if (edit.len >= before && std.mem.endsWith(T, edit, seq[lo - before .. lo]) && slideBoundary(T, seq, lo, hi, hi - before, whole_lines)) {
            by = -@as(isize, @intCast(before));
        } else if (edit.len >= after && std.mem.startsWith(T, edit, seq[hi .. hi + after]) && slideBoundary(T, seq, lo, hi, lo + after, whole_lines)) {
            by = @as(isize, @intCast(after));
        } else {
            continue;
        }

        hunks[i] = Hunk{
            a_lo: @intCast(@as(isize, @intCast(h.a_lo)) + by),
            a_hi: @intCast(@as(isize, @intCast(h.a_hi)) + by),
            b_lo: @intCast(@as(isize, @intCast(h.b_lo)) + by),
            b_hi: @intCast(@as(isize, @intCast(h.b_hi)) + by),
        };
        changed = true;
    }
    return changed;
}

/// Line-level diff. Returned hunks are byte offsets into `a` and `b`, always
/// falling on line boundaries. Caller owns the returned list.
pub fn diffLines(allocator: std.mem.Allocator, a: []const u8, b: []const u8): std.ArrayListUnmanaged(Hunk) {
    let hunks = std.ArrayListUnmanaged(Hunk).empty;
    const p = commonPrefix(u8, a, b);
    if (p == a.len && p == b.len) {
        return hunks;
    }
    if (@max(a.len, b.len) >= MAX_LEN) {
        bun.handleOom(hunks.append(allocator, Hunk{
            a_lo: 0,
            a_hi: a.len,
            b_lo: 0,
            b_hi: b.len,
        }));
        return hunks;
    }
    let head: usize = 0;
    if (p > 0 && a[p - 1] == '\n') {
        head = p;
    } else if (idx := bun.strings.lastIndexOfChar(a[0..p], '\n')) {
        head = idx + 1;
    }

    const s = commonSuffix(u8, a[head..], b[head..]);
    let a_end = a.len - s;
    let b_end = b.len - s;
    const a_at_line_start = a_end == head || a[a_end - 1] == '\n';
    const b_at_line_start = b_end == head || b[b_end - 1] == '\n';
    if (!(a_at_line_start && b_at_line_start)) {
        if (idx := bun.strings.indexOfCharUsize(a[a_end..], '\n')) {
            a_end += idx + 1;
            b_end += idx + 1;
        } else {
            a_end = a.len;
            b_end = b.len;
        }
    }

    const ma = a[head..a_end];
    const mb = b[head..b_end];
    if (ma.len == 0 || mb.len == 0) {
        bun.handleOom(hunks.append(allocator, Hunk{
            a_lo: head,
            a_hi: a_end,
            b_lo: head,
            b_hi: b_end,
        }));
        slideLineHunks(&hunks, a, b);
        return hunks;
    }
    if (isSingleLine(ma) || isSingleLine(mb)) {
        hunks = if (isSingleLine(ma))
            oneVsLines(allocator, ma, mb, head, false)
        else
            oneVsLines(allocator, mb, ma, head, true);
        slideLineHunks(&hunks, a, b);
        return hunks;
    }

    const est_a = bun.strings.countChar(ma, '\n') + 1;
    const est_b = bun.strings.countChar(mb, '\n') + 1;

    let ta = std.ArrayListUnmanaged(u32).empty;
    defer ta.deinit(allocator);
    bun.handleOom(ta.ensureTotalCapacity(allocator, est_a));

    let tb = std.ArrayListUnmanaged(u32).empty;
    defer tb.deinit(allocator);
    bun.handleOom(tb.ensureTotalCapacity(allocator, est_b));

    let offs_a = std.ArrayListUnmanaged(u32).empty;
    defer offs_a.deinit(allocator);
    bun.handleOom(offs_a.ensureTotalCapacity(allocator, est_a + 1));

    let offs_b = std.ArrayListUnmanaged(u32).empty;
    defer offs_b.deinit(allocator);
    bun.handleOom(offs_b.ensureTotalCapacity(allocator, est_b + 1));

    let uniq: usize = 0;
    {
        let interner = Interner.initWithCapacity(allocator, est_a + est_b);
        defer interner.~init();
        tokenize(allocator, ma, head, &interner, &ta, &offs_a);
        tokenize(allocator, mb, head, &interner, &tb, &offs_b);
        uniq = interner.lines.items.len;
    }

    const na = ta.items.len;
    const nb = tb.items.len;

    const occ = bun.handleOom(allocator.alloc([2]u32, uniq));
    defer allocator.free(occ);
    @memset(occ, [0, 0]);
    for (t of ta.items) {
        occ[usize(t)][0] += 1;
    }
    for (t of tb.items) {
        occ[usize(t)][1] += 1;
    }

    const changed = bun.handleOom(allocator.alloc(bool, na + nb));
    defer allocator.free(changed);
    @memset(changed, false);
    let changed_a = changed[0..na];
    let changed_b = changed[na..];

    let ra = std.ArrayListUnmanaged(u32).empty;
    defer ra.deinit(allocator);
    bun.handleOom(ra.ensureTotalCapacity(allocator, na));
    let map_a = std.ArrayListUnmanaged(u32).empty;
    defer map_a.deinit(allocator);
    bun.handleOom(map_a.ensureTotalCapacity(allocator, na));

    let idx_a: usize = 0;
    for (t of ta.items) {
        if (occ[usize(t)][1] == 0) {
            changed_a[idx_a] = true;
        } else {
            bun.handleOom(ra.append(allocator, t));
            bun.handleOom(map_a.append(allocator, u32(idx_a)));
        }
        idx_a += 1;
    }

    let rb = std.ArrayListUnmanaged(u32).empty;
    defer rb.deinit(allocator);
    bun.handleOom(rb.ensureTotalCapacity(allocator, nb));
    let map_b = std.ArrayListUnmanaged(u32).empty;
    defer map_b.deinit(allocator);
    bun.handleOom(map_b.ensureTotalCapacity(allocator, nb));

    let idx_b: usize = 0;
    for (t of tb.items) {
        if (occ[usize(t)][0] == 0) {
            changed_b[idx_b] = true;
        } else {
            bun.handleOom(rb.append(allocator, t));
            bun.handleOom(map_b.append(allocator, u32(idx_b)));
        }
        idx_b += 1;
    }

    if (ra.items.len > 0 && rb.items.len > 0) {
        const n = ra.items.len + rb.items.len;
        let sc = MyersScratch.empty();
        defer sc.deinit(allocator);

        const rchanged = bun.handleOom(allocator.alloc(bool, n));
        defer allocator.free(rchanged);
        @memset(rchanged, false);
        let rchanged_a = rchanged[0..ra.items.len];
        let rchanged_b = rchanged[ra.items.len..];

        const policy = Policy{
            probe: LINES_POLICY.probe,
            exhaustive_max: LINES_POLICY.exhaustive_max,
            exhaustive_per_elem: LINES_POLICY.exhaustive_per_elem,
            exhaustive_quota: LINES_POLICY.exhaustive_quota,
            work: LINES_POLICY.work + n * LINE_WORK_PER_TOKEN,
        };
        const MyersU32 = Myers(u32);
        let myers = MyersU32.init(allocator, ra.items, rb.items, &sc, policy);
        defer myers.deinit();
        myers.run(0, ra.items.len, 0, rb.items.len, rchanged_a, rchanged_b);

        let ri: usize = 0;
        for (c of rchanged_a) {
            if (c) changed_a[usize(map_a.items[ri])] = true;
            ri += 1;
        }
        let rj: usize = 0;
        for (c of rchanged_b) {
            if (c) changed_b[usize(map_b.items[rj])] = true;
            rj += 1;
        }
    } else {
        @memset(changed_a, true);
        @memset(changed_b, true);
    }

    bun.handleOom(hunks.ensureTotalCapacity(allocator, 16));
    hunksFromChanged(u32, allocator, ta.items, tb.items, changed_a, changed_b, &hunks);
    for (*h of hunks.items) {
        h = Hunk{
            a_lo: usize(offs_a.items[h.a_lo]),
            a_hi: usize(offs_a.items[h.a_hi]),
            b_lo: usize(offs_b.items[h.b_lo]),
            b_hi: usize(offs_b.items[h.b_hi]),
        };
    }
    slideLineHunks(&hunks, a, b);
    return hunks;
}

// ───────────────────────────── character mode ─────────────────────────────

pub struct CharDiff {
    allocator: std.mem.Allocator,
    hunks: std.ArrayListUnmanaged(Hunk),
    scratch: std.ArrayListUnmanaged(Hunk),
    myers: MyersScratch,
    changed_a: std.ArrayListUnmanaged(bool),
    changed_b: std.ArrayListUnmanaged(bool),
    kmp: std.ArrayListUnmanaged(u32),

    static fn init(allocator: std.mem.Allocator): CharDiff {
        return CharDiff{
            allocator: allocator,
            hunks: std.ArrayListUnmanaged(Hunk).empty,
            scratch: std.ArrayListUnmanaged(Hunk).empty,
            myers: MyersScratch.empty(),
            changed_a: std.ArrayListUnmanaged(bool).empty,
            changed_b: std.ArrayListUnmanaged(bool).empty,
            kmp: std.ArrayListUnmanaged(u32).empty,
        };
    }

    ~init() {
        _.hunks.deinit(_.allocator);
        _.scratch.deinit(_.allocator);
        _.myers.deinit(_.allocator);
        _.changed_a.deinit(_.allocator);
        _.changed_b.deinit(_.allocator);
        _.kmp.deinit(_.allocator);
    }

    fn diff(*_, a: []const u8, b: []const u8): []const Hunk {
        _.hunks.clearRetainingCapacity();
        _.whole(a, b);
        _.finish(a, b);
        return _.hunks.items;
    }

    fn finish(*_, a: []const u8, b: []const u8) {
        cleanupMerge(_.allocator, &_.hunks, a, b);
        alignToUtf8(&_.hunks, a);
        cleanupSemantic(_.allocator, &_.hunks, &_.scratch, a, b, &_.kmp);
        alignToUtf8(&_.hunks, a);
    }

    fn whole(*_, a: []const u8, b: []const u8) {
        const p = commonPrefix(u8, a, b);
        if (p == a.len && p == b.len) {
            return;
        }
        const s = commonSuffix(u8, a[p..], b[p..]);
        const ma = a[p .. a.len - s];
        const mb = b[p .. b.len - s];
        const whole_hunk = Hunk{
            a_lo: 0,
            a_hi: ma.len,
            b_lo: 0,
            b_hi: mb.len,
        };
        if (ma.len == 0 || mb.len == 0) {
            bun.handleOom(_.hunks.append(_.allocator, whole_hunk));
        } else if (c := containsBytes(ma, mb)) {
            const ga = if (c.long_is_a) c.at else 0;
            const gb = if (c.long_is_a) 0 else c.at;
            const common = @min(ma.len, mb.len);
            pushNonempty(_.allocator, &_.hunks, Hunk{
                a_lo: 0,
                a_hi: ga,
                b_lo: 0,
                b_hi: gb,
            });
            pushNonempty(_.allocator, &_.hunks, Hunk{
                a_lo: ga + common,
                a_hi: ma.len,
                b_lo: gb + common,
                b_hi: mb.len,
            });
        } else if (ma.len == 1 || mb.len == 1 || @max(a.len, b.len) >= MAX_LEN) {
            bun.handleOom(_.hunks.append(_.allocator, whole_hunk));
        } else {
            bun.handleOom(_.changed_a.resize(_.allocator, ma.len));
            @memset(_.changed_a.items, false);
            bun.handleOom(_.changed_b.resize(_.allocator, mb.len));
            @memset(_.changed_b.items, false);

            const policy = Policy{
                probe: CHARS_POLICY.probe,
                exhaustive_max: CHARS_POLICY.exhaustive_max,
                exhaustive_per_elem: CHARS_POLICY.exhaustive_per_elem,
                exhaustive_quota: CHARS_POLICY.exhaustive_quota,
                work: CHARS_POLICY.work + (ma.len + mb.len) * CHAR_WORK_PER_BYTE,
            };
            const MyersU8 = Myers(u8);
            let char_myers = MyersU8.init(_.allocator, ma, mb, &_.myers, policy);
            defer char_myers.deinit();
            char_myers.run(0, ma.len, 0, mb.len, _.changed_a.items, _.changed_b.items);
            hunksFromChanged(u8, _.allocator, ma, mb, _.changed_a.items, _.changed_b.items, &_.hunks);
        }

        for (*h of _.hunks.items) {
            h = Hunk{
                a_lo: h.a_lo + p,
                a_hi: h.a_hi + p,
                b_lo: h.b_lo + p,
                b_hi: h.b_hi + p,
            };
        }
    }
}

fn pushNonempty(allocator: std.mem.Allocator, hunks: *std.ArrayListUnmanaged(Hunk), h: Hunk) {
    if (h.a_lo < h.a_hi || h.b_lo < h.b_hi) {
        bun.handleOom(hunks.append(allocator, h));
    }
}

struct ContainsResult {
    long_is_a: bool,
    at: usize,
}

fn containsBytes(a: []const u8, b: []const u8): ContainsResult? {
    if (a.len >= b.len) {
        if (idx := bun.strings.indexOf(a, b)) {
            return ContainsResult{ long_is_a: true, at: idx };
        }
        return null;
    } else {
        if (idx := bun.strings.indexOf(b, a)) {
            return ContainsResult{ long_is_a: false, at: idx };
        }
        return null;
    }
}

// ───────────────────────── cleanup (diff-match-patch) ─────────────────────────

fn cleanupMerge(allocator: std.mem.Allocator, hunks: *std.ArrayListUnmanaged(Hunk), a: []const u8, b: []const u8) {
    * = allocator;
    while (true) {
        let w: usize = 0;
        for (r_h of hunks.items) {
            let h = r_h;
            if (h.a_lo < h.a_hi && h.b_lo < h.b_hi) {
                const p = commonPrefix(u8, a[h.a_lo..h.a_hi], b[h.b_lo..h.b_hi]);
                h.a_lo += p;
                h.b_lo += p;
                const s = commonSuffix(u8, a[h.a_lo..h.a_hi], b[h.b_lo..h.b_hi]);
                h.a_hi -= s;
                h.b_hi -= s;
                if (h.a_lo == h.a_hi && h.b_lo == h.b_hi) {
                    continue;
                }
            }
            if (w > 0 && hunks.items[w - 1].a_hi == h.a_lo && hunks.items[w - 1].b_hi == h.b_lo) {
                hunks.items[w - 1].a_hi = h.a_hi;
                hunks.items[w - 1].b_hi = h.b_hi;
            } else {
                hunks.items[w] = h;
                w += 1;
            }
        }
        hunks.items.len = w;

        if (!slideSingleEdits(u8, hunks.items, a, b, false)) {
            return;
        }
    }
}

fn cleanupSemantic(
    allocator: std.mem.Allocator,
    hunks: *std.ArrayListUnmanaged(Hunk),
    scratch: *std.ArrayListUnmanaged(Hunk),
    a: []const u8,
    b: []const u8,
    kmp: *std.ArrayListUnmanaged(u32),
) {
    // 1. An equality no longer than the edits on either side of it is noise:
    //    fold it into one larger edit.
    let changed = false;
    let w: usize = 0;
    for (r_h of hunks.items) {
        let cur = r_h;
        while (w > 0) {
            const l = hunks.items[w - 1];
            const gap = cur.a_lo - l.a_hi;
            if (gap <= @max(l.deleted(), l.inserted()) && gap <= @max(cur.deleted(), cur.inserted())) {
                cur = Hunk{
                    a_lo: l.a_lo,
                    a_hi: cur.a_hi,
                    b_lo: l.b_lo,
                    b_hi: cur.b_hi,
                };
                w -= 1;
                changed = true;
            } else {
                break;
            }
        }
        hunks.items[w] = cur;
        w += 1;
    }
    hunks.items.len = w;
    if (changed) {
        cleanupMerge(allocator, hunks, a, b);
    }

    // 2. Slide single edits sideways to the most semantically pleasing spot.
    cleanupSemanticLossless(hunks, a, b);

    // 3. Overlap extraction.
    scratch.clearRetainingCapacity();
    for (h of hunks.items) {
        const del = a[h.a_lo..h.a_hi];
        const ins = b[h.b_lo..h.b_hi];
        if (del.len == 0 || ins.len == 0) {
            bun.handleOom(scratch.append(allocator, h));
            continue;
        }
        const fwd = commonOverlap(allocator, del, ins, kmp);
        const rev = commonOverlap(allocator, ins, del, kmp);
        if (fwd >= rev) {
            if (fwd > 0 && (fwd * 2 >= del.len || fwd * 2 >= ins.len)) {
                pushNonempty(allocator, scratch, Hunk{
                    a_lo: h.a_lo,
                    a_hi: h.a_hi - fwd,
                    b_lo: h.b_lo,
                    b_hi: h.b_lo,
                });
                pushNonempty(allocator, scratch, Hunk{
                    a_lo: h.a_hi,
                    a_hi: h.a_hi,
                    b_lo: h.b_lo + fwd,
                    b_hi: h.b_hi,
                });
                continue;
            }
        } else if (rev * 2 >= del.len || rev * 2 >= ins.len) {
            pushNonempty(allocator, scratch, Hunk{
                a_lo: h.a_lo,
                a_hi: h.a_lo,
                b_lo: h.b_lo,
                b_hi: h.b_hi - rev,
            });
            pushNonempty(allocator, scratch, Hunk{
                a_lo: h.a_lo + rev,
                a_hi: h.a_hi,
                b_lo: h.b_hi,
                b_hi: h.b_hi,
            });
            continue;
        }
        bun.handleOom(scratch.append(allocator, h));
    }
    std.mem.swap(std.ArrayListUnmanaged(Hunk), hunks, scratch);
}

fn cleanupSemanticLossless(hunks: *std.ArrayListUnmanaged(Hunk), a: []const u8, b: []const u8) {
    let w: usize = 0;
    let r: usize = 0;
    while (r < hunks.items.len) : (r += 1) {
        const h_orig = hunks.items[r];
        const prev_a = if (w == 0) 0 else hunks.items[w - 1].a_hi;
        const next_a = if (r + 1 == hunks.items.len) a.len else hunks.items[r + 1].a_lo;
        const before = h_orig.a_lo - prev_a;
        const after = next_a - h_orig.a_hi;
        if (before == 0 || after == 0 || (h_orig.deleted() > 0 && h_orig.inserted() > 0)) {
            hunks.items[w] = h_orig;
            w += 1;
            continue;
        }
        const is_del = h_orig.deleted() > 0;
        const seq = if (is_del) a else b;
        let lo = if (is_del) h_orig.a_lo else h_orig.b_lo;
        let hi = if (is_del) h_orig.a_hi else h_orig.b_hi;
        const orig_lo = lo;
        const eq1_start = lo - before;
        const eq2_end = hi + after;

        let shift = commonSuffix(u8, seq[eq1_start..lo], seq[lo..hi]);
        while (shift > 0 && isUtf8Cont(seq, lo - shift)) {
            shift -= 1;
        }
        lo -= shift;
        hi -= shift;

        let best_lo = lo;
        let best_score = semanticScore(seq[eq1_start..lo], seq[lo..hi]) + semanticScore(seq[lo..hi], seq[hi..eq2_end]);
        while (hi < eq2_end && seq[lo] == seq[hi]) {
            lo += 1;
            hi += 1;
            if (isUtf8Cont(seq, lo)) continue;
            const score = semanticScore(seq[eq1_start..lo], seq[lo..hi]) + semanticScore(seq[lo..hi], seq[hi..eq2_end]);
            if (score >= best_score) {
                best_lo = lo;
                best_score = score;
            }
        }

        const delta: isize = @as(isize, @intCast(best_lo)) - @as(isize, @intCast(orig_lo));
        const h = Hunk{
            a_lo: @intCast(@as(isize, @intCast(h_orig.a_lo)) + delta),
            a_hi: @intCast(@as(isize, @intCast(h_orig.a_hi)) + delta),
            b_lo: @intCast(@as(isize, @intCast(h_orig.b_lo)) + delta),
            b_hi: @intCast(@as(isize, @intCast(h_orig.b_hi)) + delta),
        };
        if (r + 1 < hunks.items.len && h.a_hi == hunks.items[r + 1].a_lo && h.b_hi == hunks.items[r + 1].b_lo) {
            hunks.items[r + 1].a_lo = h.a_lo;
            hunks.items[r + 1].b_lo = h.b_lo;
        } else if (w > 0 && hunks.items[w - 1].a_hi == h.a_lo && hunks.items[w - 1].b_hi == h.b_lo) {
            hunks.items[w - 1].a_hi = h.a_hi;
            hunks.items[w - 1].b_hi = h.b_hi;
        } else {
            hunks.items[w] = h;
            w += 1;
        }
    }
    hunks.items.len = w;
}

fn isAsciiAlnum(c: u8): bool {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

fn isAsciiWhitespace(c: u8): bool {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == 0x0b || c == 0x0c;
}

fn isAsciiControl(c: u8): bool {
    return c < 0x20 || c == 0x7f;
}

fn semanticScore(one: []const u8, two: []const u8): u32 {
    if (one.len == 0 || two.len == 0) {
        return 6;
    }
    const char1 = one[one.len - 1];
    const char2 = two[0];
    const non_alphanumeric1 = !isAsciiAlnum(char1);
    const non_alphanumeric2 = !isAsciiAlnum(char2);
    const whitespace1 = non_alphanumeric1 && isAsciiWhitespace(char1);
    const whitespace2 = non_alphanumeric2 && isAsciiWhitespace(char2);
    const line_break1 = whitespace1 && isAsciiControl(char1);
    const line_break2 = whitespace2 && isAsciiControl(char2);
    const blank_line1 = line_break1 && (std.mem.endsWith(u8, one, "\n\n") || std.mem.endsWith(u8, one, "\n\r\n"));
    const blank_line2 = line_break2 && (std.mem.startsWith(u8, two, "\n\n") || std.mem.startsWith(u8, two, "\r\n\n") || std.mem.startsWith(u8, two, "\n\r\n") || std.mem.startsWith(u8, two, "\r\n\r\n"));

    if (blank_line1 || blank_line2) return 5;
    if (line_break1 || line_break2) return 4;
    if (non_alphanumeric1 && !whitespace1 && whitespace2) return 3;
    if (whitespace1 || whitespace2) return 2;
    if (non_alphanumeric1 || non_alphanumeric2) return 1;
    return 0;
}

const OVERLAP_MAX: usize = 64 << 10;

fn commonOverlap(
    allocator: std.mem.Allocator,
    x_in: []const u8,
    y_in: []const u8,
    fail: *std.ArrayListUnmanaged(u32),
): usize {
    const n = @min(@min(x_in.len, y_in.len), OVERLAP_MAX);
    if (n == 0) return 0;
    const x = x_in[x_in.len - n ..];
    const y = y_in[0..n];
    if (std.mem.eql(u8, x, y)) return n;
    if (n <= 16) {
        let k = n - 1;
        while (k >= 1) : (k -= 1) {
            if (std.mem.eql(u8, x[n - k ..], y[0..k])) return k;
        }
        return 0;
    }

    bun.handleOom(fail.resize(allocator, n));
    @memset(fail.items, 0);
    let k: usize = 0;
    let i: usize = 1;
    while (i < n) : (i += 1) {
        while (k > 0 && y[i] != y[k]) {
            k = @as(usize, fail.items[k - 1]);
        }
        if (y[i] == y[k]) {
            k += 1;
        }
        fail.items[i] = u32(k);
    }
    let k2: usize = 0;
    for (c of x) {
        while (k2 > 0 && c != y[k2]) {
            k2 = @as(usize, fail.items[k2 - 1]);
        }
        if (c == y[k2]) {
            k2 += 1;
        }
        if (k2 == n) {
            k2 = @as(usize, fail.items[k2 - 1]);
        }
    }
    return k2;
}

pub fn isUtf8Cont(s: []const u8, i: usize): bool {
    return i < s.len && (s[i] & 0xC0) == 0x80;
}

fn alignToUtf8(hunks: *std.ArrayListUnmanaged(Hunk), a: []const u8) {
    let w: usize = 0;
    let r: usize = 0;
    while (r < hunks.items.len) {
        let h = hunks.items[r];
        r += 1;
        const floor = if (w == 0) 0 else hunks.items[w - 1].a_hi;
        while (h.a_lo > floor && isUtf8Cont(a, h.a_lo)) {
            h.a_lo -= 1;
            h.b_lo -= 1;
        }
        while (true) {
            const ceil = if (r < hunks.items.len) hunks.items[r].a_lo else a.len;
            while (h.a_hi < ceil && isUtf8Cont(a, h.a_hi)) {
                h.a_hi += 1;
                h.b_hi += 1;
            }
            if (r < hunks.items.len && h.a_hi == hunks.items[r].a_lo) {
                h.a_hi = hunks.items[r].a_hi;
                h.b_hi = hunks.items[r].b_hi;
                r += 1;
                continue;
            }
            break;
        }
        hunks.items[w] = h;
        w += 1;
    }
    hunks.items.len = w;
}
