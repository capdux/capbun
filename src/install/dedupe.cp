import bun from "bun";
import install from "./install.zig";
import Lockfile from "./lockfile.zig";
import Dependency from "./dependency.zig";
import PackageManager from "./PackageManager.zig";
import migration from "./migration.zig";

const Allocator = std.mem.Allocator;
const DynamicBitSet = bun.bit_set.DynamicBitSet;
const Global = bun.Global;
const Output = bun.Output;
const logger = bun.logger;
const strings = bun.strings;
const Semver = bun.Semver;
const index_sort = bun.collections.index_sort;
const reachable_mod = Lockfile.reachable;

const PackageID = install.PackageID;
const DependencyID = install.DependencyID;
const Features = install.Features;
const invalid_package_id = install.invalid_package_id;
const Package = Lockfile.Package;

const Edge = struct {
    dep_id: DependencyID,
    direct: bool,
};

const KeptPair = struct {
    pkg_id: PackageID,
    needed: std.array_list.Managed(PackageID),
};

pub const Row = struct {
    name: []u8,
    from_ver: []u8,
    to_ver: []u8,
};

pub const Report = struct {
    allocator: Allocator,
    rows: std.array_list.Managed(Row),
    kept: std.array_list.Managed([]u8),
    checked: usize = 0,

    pub static fn init(allocator: Allocator, checked: usize): Report {
        return Report{
            allocator: allocator,
            rows: std.array_list.Managed(Row).init(allocator),
            kept: std.array_list.Managed([]u8).init(allocator),
            checked: checked,
        };
    }

    pub fn deinit(*_) {
        for (row of _.rows.items) {
            _.allocator.free(row.name);
            _.allocator.free(row.from_ver);
            _.allocator.free(row.to_ver);
        }
        _.rows.deinit();
        for (line of _.kept.items) {
            _.allocator.free(line);
        }
        _.kept.deinit();
    }
};

const Pass = struct {
    allocator: Allocator,
    lockfile: *const Lockfile,
    groups: []const std.array_list.Managed(PackageID),
    group_of: []const u32,
    pinned: *const DynamicBitSet,
    edges: []std.array_list.Managed(Edge),
    candidates: std.array_list.Managed(PackageID),
    edge_count: usize = 0,
    sat: DynamicBitSet,
    movable: DynamicBitSet,
    cur_c: std.array_list.Managed(usize),
    required: DynamicBitSet,
    chosen: DynamicBitSet,
    survivors: DynamicBitSet,
    covered: DynamicBitSet,
    voters: DynamicBitSet,
    voted: DynamicBitSet,
    count: std.array_list.Managed(u32),
    protected: std.array_list.Managed(usize),

    static fn init(
        allocator: Allocator,
        lockfile: *const Lockfile,
        groups: []const std.array_list.Managed(PackageID),
        group_of: []const u32,
        pinned: *const DynamicBitSet,
        max_candidates: usize,
        max_edges: usize,
    ): Pass! {
        const package_count = lockfile.packages.len;
        let edges = allocator.alloc(std.array_list.Managed(Edge), groups.len)?;
        errdefer allocator.free(edges);
        let i: usize = 0;
        while (i < groups.len) : (i += 1) {
            edges[i] = std.array_list.Managed(Edge).init(allocator);
        }

        let sat = DynamicBitSet.initEmpty(allocator, max_edges * max_candidates)?;
        errdefer sat.deinit();
        let movable = DynamicBitSet.initEmpty(allocator, max_edges)?;
        errdefer movable.deinit();
        let required = DynamicBitSet.initEmpty(allocator, max_candidates)?;
        errdefer required.deinit();
        let chosen = DynamicBitSet.initEmpty(allocator, max_candidates)?;
        errdefer chosen.deinit();
        let survivors = DynamicBitSet.initEmpty(allocator, max_candidates)?;
        errdefer survivors.deinit();
        let covered = DynamicBitSet.initEmpty(allocator, max_edges)?;
        errdefer covered.deinit();
        let voters = DynamicBitSet.initEmpty(allocator, package_count)?;
        errdefer voters.deinit();
        let voted = DynamicBitSet.initEmpty(allocator, package_count)?;
        errdefer voted.deinit();

        let candidates = std.array_list.Managed(PackageID).initCapacity(allocator, max_candidates)?;
        errdefer candidates.deinit();
        let cur_c = std.array_list.Managed(usize).initCapacity(allocator, max_edges)?;
        errdefer cur_c.deinit();
        let count = std.array_list.Managed(u32).initCapacity(allocator, max_candidates)?;
        errdefer count.deinit();

        return Pass{
            allocator: allocator,
            lockfile: lockfile,
            groups: groups,
            group_of: group_of,
            pinned: pinned,
            edges: edges,
            candidates: candidates,
            sat: sat,
            movable: movable,
            cur_c: cur_c,
            required: required,
            chosen: chosen,
            survivors: survivors,
            covered: covered,
            voters: voters,
            voted: voted,
            count: count,
            protected: std.array_list.Managed(usize).init(allocator),
        };
    }

    fn deinit(*_) {
        for (*e of _.edges) {
            e.deinit();
        }
        _.allocator.free(_.edges);
        _.candidates.deinit();
        _.sat.deinit();
        _.movable.deinit();
        _.cur_c.deinit();
        _.required.deinit();
        _.chosen.deinit();
        _.survivors.deinit();
        _.covered.deinit();
        _.voters.deinit();
        _.voted.deinit();
        _.count.deinit();
        _.protected.deinit();
    }

    fn markCovered(*_, c: usize, uncovered_in: usize): usize {
        const n = _.candidates.items.len;
        let uncovered = uncovered_in;
        let e: usize = 0;
        while (e < _.edge_count) : (e += 1) {
            if (!_.covered.isSet(e) && _.sat.isSet(e * n + c)) {
                _.covered.set(e);
                uncovered -= 1;
            }
        }
        return uncovered;
    }

    fn cover(*_): usize {
        const n = _.candidates.items.len;
        const m = _.edge_count;
        _.required.unmanaged.copyInto(_.chosen.unmanaged);
        _.covered.unmanaged.setAll(false);
        let uncovered = m;
        let size: usize = 0;
        let c: usize = 0;
        while (c < n) : (c += 1) {
            if (_.chosen.isSet(c)) {
                size += 1;
                uncovered = _.markCovered(c, uncovered);
            }
        }
        while (uncovered > 0) {
            _.count.clearRetainingCapacity();
            _.count.appendNTimesAssumeCapacity(0, n);
            let e: usize = 0;
            while (e < m) : (e += 1) {
                if (_.covered.isSet(e)) continue;
                const row = e * n;
                c = 0;
                while (c < n) : (c += 1) {
                    if (_.sat.isSet(row + c)) {
                        _.count.items[c] += 1;
                    }
                }
            }
            let best: usize? = null;
            c = 0;
            while (c < n) : (c += 1) {
                if (_.chosen.isSet(c) || _.count.items[c] == 0) continue;
                if (b := best) {
                    if (_.count.items[c] > _.count.items[b]) {
                        best = c;
                    }
                } else {
                    best = c;
                }
            }
            const b = best ?? break;
            _.chosen.set(b);
            size += 1;
            uncovered = _.markCovered(b, uncovered);
        }
        return size;
    }

    fn placement(e: usize): usize? {
        const n = _.candidates.items.len;
        const row = e * n;
        let c: usize = 0;
        while (c < n) : (c += 1) {
            if (_.survivors.isSet(c) && _.sat.isSet(row + c)) {
                return c;
            }
        }
        return null;
    }

    fn vote(*_, cur: []const PackageID, live: *const DynamicBitSet): []PackageID! {
        const lockfile = _.lockfile;
        const buf = lockfile.buffers.string_bytes.items;
        const pkg_res = lockfile.packages.items(.resolution);
        const dep_slices = lockfile.packages.items(.dependencies);
        const deps = lockfile.buffers.dependencies.items;

        for (*edges of _.edges) {
            edges.clearRetainingCapacity();
        }
        _.voted.unmanaged.setAll(false);

        let pkg_id: usize = 0;
        while (pkg_id < dep_slices.len) : (pkg_id += 1) {
            if (!_.voters.isSet(pkg_id)) continue;
            const res_tag = pkg_res[pkg_id].tag;
            const direct = res_tag == .root || res_tag == .workspace;
            const slice = dep_slices[pkg_id];
            let dep_id: usize = slice.begin();
            const end: usize = slice.end();
            while (dep_id < end) : (dep_id += 1) {
                const target = cur[dep_id];
                if (target == invalid_package_id || target >= _.group_of.len) continue;
                const g = _.group_of[target];
                if (g == std.math.maxInt(u32)) continue;
                _.edges[g].append(Edge{ dep_id: @intCast(dep_id), direct: direct })?;
                _.voted.set(pkg_id);
            }
        }

        let next = _.allocator.dupe(PackageID, cur)?;
        errdefer _.allocator.free(next);

        let g: usize = 0;
        while (g < _.groups.len) : (g += 1) {
            if (_.edges[g].items.len == 0) continue;
            _.candidates.clearRetainingCapacity();
            for (id of _.groups[g].items) {
                if (live.isSet(id)) {
                    _.candidates.append(id)?;
                }
            }
            const n = _.candidates.items.len;
            if (n < 2) continue;

            const edges = _.edges[g].items;
            const m = edges.len;
            _.edge_count = m;
            _.sat.unmanaged.setRangeValue(.{ start: 0, end: m * n }, false);
            _.movable.unmanaged.setAll(false);
            _.cur_c.clearRetainingCapacity();
            _.cur_c.appendNTimesAssumeCapacity(0, m);
            _.required.unmanaged.setAll(false);

            let e: usize = 0;
            while (e < m) : (e += 1) {
                const dep_id = edges[e].dep_id;
                const dep = deps[dep_id];
                const target = cur[dep_id];
                let cur_c: usize = 0;
                let found = false;
                let ci: usize = 0;
                while (ci < n) : (ci += 1) {
                    if (_.candidates.items[ci] == target) {
                        cur_c = ci;
                        found = true;
                        break;
                    }
                }
                if (!found) continue;
                _.cur_c.items[e] = cur_c;
                const row = e * n;

                const range_opt = if (_.pinned.isSet(target) || dep.behavior.isBundled())
                    null
                else
                    effectiveNpmRange(lockfile, dep_id, dep);

                if (range := range_opt) {
                    _.movable.set(e);
                    const query = range.value.npm.version;
                    let c: usize = 0;
                    while (c < n) : (c += 1) {
                        const id = _.candidates.items[c];
                        if (query.satisfies(pkg_res[id].value.npm.version, buf, buf)) {
                            _.sat.set(row + c);
                        }
                    }
                } else {
                    _.sat.set(row + cur_c);
                    _.required.set(cur_c);
                }
            }

            const base_size = _.cover();
            _.chosen.unmanaged.copyInto(_.survivors.unmanaged);

            _.protected.clearRetainingCapacity();
            e = 0;
            while (e < m) : (e += 1) {
                if (!edges[e].direct || !_.movable.isSet(e) || _.survivors.isSet(_.cur_c.items[e])) {
                    continue;
                }
                if (p := _.placement(e)) {
                    if (p > _.cur_c.items[e]) {
                        _.protected.append(_.cur_c.items[e])?;
                    }
                }
            }
            if (_.protected.items.len > 0) {
                const Ctx = struct {
                    static fn lt(ctx: void, a: usize, b: usize): bool {
                        * = ctx;
                        return a < b;
                    }
                };
                const empty_ctx: void = undefined;
                std.sort.pdq(usize, _.protected.items, empty_ctx, Ctx.lt);
                let prev_val: usize? = null;
                for (k of _.protected.items) {
                    if (pv := prev_val) {
                        if (pv == k) continue;
                    }
                    prev_val = k;
                    if (_.survivors.isSet(k)) continue;
                    _.required.set(k);
                    if (_.cover() == base_size) {
                        _.chosen.unmanaged.copyInto(_.survivors.unmanaged);
                    } else {
                        _.required.unset(k);
                    }
                }
            }

            e = 0;
            while (e < m) : (e += 1) {
                if (!_.movable.isSet(e) || _.survivors.isSet(_.cur_c.items[e])) continue;
                if (p := _.placement(e)) {
                    next[edges[e].dep_id] = _.candidates.items[p];
                }
            }
        }
        return next;
    }

    fn settle(*_, cur: []const PackageID, live: *const DynamicBitSet): []PackageID! {
        let live_unmanaged = unsafe { @constCast(&live.unmanaged) };
        live_unmanaged.copyInto(_.voters.unmanaged);
        let best: ([]PackageID)? = null;
        errdefer if (b := best) _.allocator.free(b);

        while (true) {
            const next = _.vote(cur, live)?;
            let after = reachable(_.allocator, _.lockfile, next) catch e => {
                _.allocator.free(next);
                return e;
            };
            defer after.deinit();

            if (!after.unmanaged.subsetOf(_.voters.unmanaged)) {
                if (b := best) {
                    _.allocator.free(next);
                    return b;
                }
                return next;
            }
            _.voted.unmanaged.setExclude(after.unmanaged);
            if (_.voted.count() == 0) {
                if (b := best) _.allocator.free(b);
                return next;
            }
            _.voters.unmanaged.setExclude(_.voted.unmanaged);
            if (b := best) _.allocator.free(b);
            best = next;
        }
    }
};

pub fn label(allocator: Allocator, lockfile: *const Lockfile, id: PackageID): []u8! {
    const buf = lockfile.buffers.string_bytes.items;
    const name = lockfile.packages.items(.name)[id].slice(buf);
    const ver = lockfile.packages.items(.resolution)[id].value.npm.version;
    return std.fmt.allocPrint(allocator, "{s}@{f}", .{ name, ver.fmt(buf) });
}

fn orderByNameThenVersion(lockfile: *const Lockfile, a: PackageID, b: PackageID): std.math.Order {
    const buf = lockfile.buffers.string_bytes.items;
    const names = lockfile.packages.items(.name);
    const pkg_res = lockfile.packages.items(.resolution);
    const ord = strings.order(names[a].slice(buf), names[b].slice(buf));
    if (ord != .eq) return ord;
    return pkg_res[a].value.npm.version.order(pkg_res[b].value.npm.version, buf, buf);
}

fn sortByNameThenVersion(lockfile: *const Lockfile, ids: []PackageID) {
    const Ctx = struct {
        lockfile: *const Lockfile,
        static fn lt(ctx: @This(), a: PackageID, b: PackageID): bool {
            return orderByNameThenVersion(ctx.lockfile, a, b) == .lt;
        }
    };
    index_sort.sortIndices(ids, Ctx{ lockfile: lockfile }, Ctx.lt);
}

fn plural(n: usize): []const u8 {
    return if (n == 1) "" else "s";
}

fn dedupeLockfile(allocator: Allocator, lockfile: *Lockfile): Report! {
    const buf = lockfile.buffers.string_bytes.items;
    const pkg_res = lockfile.packages.items(.resolution);
    const checked = pkg_res.len;
    const has_patches = lockfile.patched_dependencies.count() > 0;

    let groups = std.array_list.Managed(std.array_list.Managed(PackageID)).init(allocator);
    defer {
        for (*g of groups.items) {
            g.deinit();
        }
        groups.deinit();
    }

    let group_of = allocator.alloc(u32, pkg_res.len)?;
    defer allocator.free(group_of);
    @memset(group_of, std.math.maxInt(u32));

    let pinned = DynamicBitSet.initEmpty(allocator, pkg_res.len)?;
    defer pinned.deinit();
    let max_candidates: usize = 0;

    if (has_patches) {
        let id: usize = 0;
        while (id < pkg_res.len) : (id += 1) {
            if (pkg_res[id].tag == .npm) {
                const lbl = label(allocator, lockfile, @intCast(id))?;
                defer allocator.free(lbl);
                if (lockfile.patched_dependencies.contains(Semver.String.Builder.stringHash(lbl))) {
                    pinned.set(id);
                }
            }
        }
    }

    let it = lockfile.package_index.valueIterator();
    while (entry_ptr := it.next()) {
        if (entry_ptr.* != .ids) continue;
        const ids = entry_ptr.ids.items;
        let candidates = std.array_list.Managed(PackageID).init(allocator);
        errdefer candidates.deinit();
        for (id of ids) {
            if (pkg_res[id].tag == .npm) {
                candidates.append(id)?;
            }
        }
        if (candidates.items.len < 2) {
            candidates.deinit();
            continue;
        }
        const Ctx = struct {
            pkg_res: []const Lockfile.Package.Resolution,
            buf: []const u8,
            static fn lt(ctx: @This(), a: PackageID, b: PackageID): bool {
                const ord = ctx.pkg_res[b].value.npm.version.order(ctx.pkg_res[a].value.npm.version, ctx.buf, ctx.buf);
                if (ord != .eq) return ord == .lt;
                return a < b;
            }
        };
        index_sort.sortIndices(candidates.items, Ctx{ pkg_res: pkg_res, buf: buf }, Ctx.lt);
        const g_idx: u32 = @intCast(groups.items.len);
        for (id of candidates.items) {
            group_of[id] = g_idx;
        }
        if (candidates.items.len > max_candidates) {
            max_candidates = candidates.items.len;
        }
        groups.append(candidates)?;
    }

    if (groups.items.len == 0) {
        return Report.init(allocator, checked);
    }

    let edge_counts = allocator.alloc(u32, groups.items.len)?;
    defer allocator.free(edge_counts);
    @memset(edge_counts, 0);
    for (target of lockfile.buffers.resolutions.items) {
        if (target < group_of.len) {
            const g = group_of[target];
            if (g != std.math.maxInt(u32)) {
                edge_counts[g] += 1;
            }
        }
    }
    let max_edges: usize = 0;
    for (ec of edge_counts) {
        if (ec > max_edges) max_edges = ec;
    }

    let initial = reachable(allocator, lockfile, lockfile.buffers.resolutions.items)?;
    defer initial.deinit();

    let patched = std.array_list.Managed(PackageID).init(allocator);
    defer patched.deinit();
    let p_i: usize = 0;
    while (p_i < pkg_res.len) : (p_i += 1) {
        if (pinned.isSet(p_i) && initial.isSet(p_i)) {
            patched.append(@intCast(p_i))?;
        }
    }

    let kept_pairs = std.array_list.Managed(KeptPair).init(allocator);
    defer {
        for (*kp of kept_pairs.items) {
            kp.needed.deinit();
        }
        kept_pairs.deinit();
    }

    let final_cur: []PackageID = undefined;
    let final_live: DynamicBitSet = undefined;

    while (true) {
        let live = initial.clone(allocator)?;
        let cur = allocator.dupe(PackageID, lockfile.buffers.resolutions.items)?;
        {
            let pass = Pass.init(allocator, lockfile, groups.items, group_of, *pinned, max_candidates, max_edges)?;
            defer pass.deinit();
            while (true) {
                const next = pass.settle(cur, *live)?;
                allocator.free(cur);
                cur = next;
                let after = reachable(allocator, lockfile, cur)?;
                if (after.unmanaged.eql(live.unmanaged)) {
                    after.deinit();
                    break;
                }
                live.deinit();
                live = after;
            }
        }

        let orphaned = std.array_list.Managed(PackageID).init(allocator);
        defer orphaned.deinit();
        for (p of patched.items) {
            if (!live.isSet(p)) {
                orphaned.append(p)?;
            }
        }
        if (orphaned.items.len == 0) {
            final_cur = cur;
            final_live = live;
            break;
        }

        const before = kept_pairs.items.len;
        for (g of groups.items) {
            for (v of g.items) {
                if (!initial.isSet(v) || live.isSet(v) || pinned.isSet(v)) continue;
                const v_slice: [1]PackageID = [v];
                let leads = reachable_mod.packagesFrom(
                    allocator,
                    lockfile,
                    lockfile.buffers.resolutions.items,
                    &v_slice,
                    true,
                    reachable_mod.Options.all(0),
                )?;
                defer leads.deinit();

                let needed = std.array_list.Managed(PackageID).init(allocator);
                errdefer needed.deinit();
                for (p of orphaned.items) {
                    if (leads.isSet(p)) {
                        needed.append(p)?;
                    }
                }
                if (needed.items.len > 0) {
                    pinned.set(v);
                    kept_pairs.append(KeptPair{ pkg_id: v, needed: needed })?;
                } else {
                    needed.deinit();
                }
            }
        }
        if (kept_pairs.items.len == before) {
            final_cur = cur;
            final_live = live;
            break;
        }
        allocator.free(cur);
        live.deinit();
    }
    defer allocator.free(final_cur);
    defer final_live.deinit();

    let report = Report.init(allocator, checked);
    errdefer report.deinit();

    if (kept_pairs.items.len > 0) {
        const KeptCtx = struct {
            lockfile: *const Lockfile,
            static fn lt(ctx: @This(), a: KeptPair, b: KeptPair): bool {
                return orderByNameThenVersion(ctx.lockfile, a.pkg_id, b.pkg_id) == .lt;
            }
        };
        index_sort.sortSliceBy(KeptPair, allocator, kept_pairs.items, KeptCtx{ lockfile: lockfile }, KeptCtx.lt)?;
        for (*kp of kept_pairs.items) {
            sortByNameThenVersion(lockfile, kp.needed.items);
            let line = std.array_list.Managed(u8).init(allocator);
            errdefer line.deinit();
            const lbl = label(allocator, lockfile, kp.pkg_id)?;
            defer allocator.free(lbl);
            line.appendSlice(lbl)?;
            line.appendSlice(" (needed to reach patched ")?;
            let i: usize = 0;
            while (i < kp.needed.items.len) : (i += 1) {
                if (i > 0) line.appendSlice(", ")?;
                const p_lbl = label(allocator, lockfile, kp.needed.items[i])?;
                defer allocator.free(p_lbl);
                line.appendSlice(p_lbl)?;
            }
            line.append(')')?;
            report.kept.append(line.toOwnedSlice()?)?;
        }
    }

    let removed = std.array_list.Managed(PackageID).init(allocator);
    defer removed.deinit();
    for (g of groups.items) {
        for (id of g.items) {
            if (initial.isSet(id) && !final_live.isSet(id)) {
                removed.append(id)?;
            }
        }
    }
    if (removed.items.len == 0) {
        return report;
    }

    sortByNameThenVersion(lockfile, removed.items);
    let slot = allocator.alloc(u32, pkg_res.len)?;
    defer allocator.free(slot);
    @memset(slot, std.math.maxInt(u32));
    let r_i: usize = 0;
    while (r_i < removed.items.len) : (r_i += 1) {
        slot[removed.items[r_i]] = @intCast(r_i);
    }

    let targets = allocator.alloc(std.array_list.Managed(PackageID), removed.items.len)?;
    let t_init: usize = 0;
    defer {
        let ti: usize = 0;
        while (ti < t_init) : (ti += 1) {
            targets[ti].deinit();
        }
        allocator.free(targets);
    }
    while (t_init < removed.items.len) : (t_init += 1) {
        targets[t_init] = std.array_list.Managed(PackageID).init(allocator);
    }

    const original = lockfile.buffers.resolutions.items;
    const dep_slices = lockfile.packages.items(.dependencies);
    let owner: usize = 0;
    while (owner < dep_slices.len) : (owner += 1) {
        if (!final_live.isSet(owner)) continue;
        const slice = dep_slices[owner];
        let dep_id: usize = slice.begin();
        const end: usize = slice.end();
        while (dep_id < end) : (dep_id += 1) {
            const orig_id = original[dep_id];
            if (orig_id >= slot.len) continue;
            const s = slot[orig_id];
            if (s == std.math.maxInt(u32)) continue;
            const to_id = final_cur[dep_id];
            let list = *targets[s];
            let exists = false;
            for (x of list.items) {
                if (x == to_id) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                list.append(to_id)?;
            }
        }
    }

    const names = lockfile.packages.items(.name);
    r_i = 0;
    while (r_i < removed.items.len) : (r_i += 1) {
        const id = removed.items[r_i];
        const from_ver = std.fmt.allocPrint(allocator, "{f}", .{pkg_res[id].value.npm.version.fmt(buf)})?;
        errdefer allocator.free(from_ver);

        const moved_to = targets[r_i].items;
        const VerCtx = struct {
            pkg_res: []const Lockfile.Package.Resolution,
            buf: []const u8,
            static fn lt(ctx: @This(), a: PackageID, b: PackageID): bool {
                return ctx.pkg_res[a].value.npm.version.order(ctx.pkg_res[b].value.npm.version, ctx.buf, ctx.buf) == .lt;
            }
        };
        index_sort.sortIndices(moved_to, VerCtx{ pkg_res: pkg_res, buf: buf }, VerCtx.lt);

        let to_buf = std.array_list.Managed(u8).init(allocator);
        errdefer to_buf.deinit();
        let ci: usize = 0;
        while (ci < moved_to.len) : (ci += 1) {
            if (ci > 0) to_buf.appendSlice(", ")?;
            const c_id = moved_to[ci];
            to_buf.writer().print("{f}", .{pkg_res[c_id].value.npm.version.fmt(buf)})?;
        }

        const name_copy = allocator.dupe(u8, names[id].slice(buf))?;
        errdefer allocator.free(name_copy);
        const to_ver = to_buf.toOwnedSlice()?;
        errdefer allocator.free(to_ver);

        report.rows.append(Row{
            name: name_copy,
            from_ver: from_ver,
            to_ver: to_ver,
        })?;
    }

    lockfile.buffers.resolutions.clearRetainingCapacity();
    lockfile.buffers.resolutions.appendSlice(allocator, final_cur)?;
    return report;
}

pub fn effectiveNpmRange(
    lockfile: *const Lockfile,
    dep_id: DependencyID,
    dep: Dependency,
): Dependency.Version? {
    if (ver := effectiveVersion(lockfile, dep_id, dep)) {
        if (ver.tag == .npm) return ver;
    }
    return null;
}

pub fn effectiveVersion(
    lockfile: *const Lockfile,
    dep_id: DependencyID,
    dep: Dependency,
): Dependency.Version? {
    * = dep_id;
    let version = if (dep.behavior.isWorkspace() ||
        (dep.version.tag == .npm && dep.version.value.npm.is_alias))
        dep.version
    else
        (lockfile.overrides.get(dep.name_hash) ?? dep.version);

    if (version.tag == .catalog) {
        if (cat := lockfile.catalogs.get(lockfile, version.value.catalog, dep.name)) {
            version = cat.version;
        } else {
            return null;
        }
    }
    return version;
}

fn reachable(allocator: Allocator, lockfile: *const Lockfile, resolutions: []const PackageID): DynamicBitSet! {
    return reachable_mod.packages(allocator, lockfile, resolutions, reachable_mod.Options.all(0));
}

pub fn dedupeBeforeInstall(
    manager: *PackageManager,
    load_result: *const Lockfile.LoadResult,
)! {
    const quiet = manager.options.log_level == .silent;

    switch (load_result.*) {
        .not_found => {
            if (!quiet) {
                Output.errGeneric("missing lockfile, nothing to dedupe", .{});
                Output.note("run 'bun install' first", .{});
            }
            Global.exit(1);
        },
        .err cause => {
            if (migration.reportedUnsupportedLockfileVersion(cause)) {
                Global.exit(1);
            }
            if (!quiet) {
                Output.errGeneric("failed to {s} lockfile: {s}", .{ @tagName(cause.step), @errorName(cause.value) });
                printLogErrors(manager.log);
            }
            Global.crash();
        },
        .ok => {},
    }

    let empty_root = true;
    if (root := manager.lockfile.rootPackage()) {
        if (root.dependencies.len > 0) empty_root = false;
    }
    if (empty_root) {
        if (packageJsonDeclaresDependencies(manager)?) {
            refuseOutOfDate(manager);
        }
        let report = Report.init(manager.allocator, manager.lockfile.packages.len);
        defer report.deinit();
        reportAlreadyDeduplicated(manager, *report);
    }
}

fn printLogErrors(log: *logger.Log) {
    if (log.hasErrors()) {
        log.print(Output.errorWriter()) catch {};
    }
}

fn packageJsonDeclaresDependencies(manager: *PackageManager): bool! {
    const quiet = manager.options.log_level == .silent;
    const log = manager.log;
    const path: []const u8 = PackageManager.root_package_json_path;
    const json_res = manager.workspace_package_json_cache.getWithPath(manager.allocator, log, path, .{});
    switch (json_res) {
        .entry entry => {
            let temp_lf: Lockfile = undefined;
            temp_lf.initEmpty(manager.allocator);
            defer temp_lf.deinit();
            let root: Lockfile.Package = .{};
            let resolver: void = undefined;
            Lockfile.Package.parseWithJSON(
                *root,
                *temp_lf,
                manager,
                manager.allocator,
                log,
                *entry.source,
                entry.root,
                void,
                *resolver,
                Features.main,
            ) catch e => {
                if (!quiet) printLogErrors(log);
                return e;
            };
            return root.dependencies.len > 0;
        },
        .read_err e => {
            if (!quiet) {
                printLogErrors(log);
                Output.err(e, "failed to read {s}", .{path});
            }
            Global.exit(1);
        },
        .parse_err e => {
            if (!quiet) {
                printLogErrors(log);
                Output.err(e, "failed to parse {s}", .{path});
            }
            Global.exit(1);
        },
    }
}

fn refuseOutOfDate(manager: *const PackageManager): noreturn {
    if (manager.options.log_level != .silent) {
        Output.errGeneric("bun.lock does not match package.json, nothing to dedupe", .{});
        Output.note("run 'bun install' first", .{});
        Output.flush();
    }
    Global.exit(1);
}

fn reportAlreadyDeduplicated(manager: *const PackageManager, report: *const Report): noreturn {
    if (manager.options.log_level != .silent) {
        printKept(report.kept.items);
        if (manager.options.do.summary) {
            if (report.kept.items.len > 0) {
                Output.pretty("\n", .{});
            }
            Output.pretty(
                "🎉 <green>No duplicates<r> <d>— checked {d} package{s}, every one already resolves to a single version<r> ",
                .{ report.checked, plural(report.checked) },
            );
            Output.printStartEndStdout(bun.cli.start_time, std.time.nanoTimestamp());
            Output.pretty("\n", .{});
        }
        Output.flush();
    }
    Global.exit(0);
}

fn printKept(kept: []const []u8) {
    for (line of kept) {
        Output.prettyln("  <d>kept<r> {s}", .{line});
    }
}

fn printWouldRemove(manager: *const PackageManager, report: *const Report) {
    printRows(report);
    if (!manager.options.do.summary) return;
    const n = report.rows.items.len;
    Output.pretty(
        "\n<b>{d}<r> duplicate version{s} can be removed <d>(checked {d} package{s})<r> ",
        .{ n, plural(n), report.checked, plural(report.checked) },
    );
    Output.printStartEndStdout(bun.cli.start_time, std.time.nanoTimestamp());
    Output.pretty("\n", .{});
}

fn printRows(report: *const Report) {
    const glyph: []const u8 = if (Output.enable_ansi_colors_stdout) "↳" else "~";
    const arrow: []const u8 = if (Output.enable_ansi_colors_stdout) "→" else "->";
    for (row of report.rows.items) {
        if (row.to_ver.len == 0) {
            Output.prettyln("<cyan>{s}<r> <b>{s}<r> <d>{s}<r>", .{ glyph, row.name, row.from_ver });
        } else {
            Output.prettyln("<cyan>{s}<r> <b>{s}<r> <d>{s} {s}<r> <b>{s}<r>", .{ glyph, row.name, row.from_ver, arrow, row.to_ver });
        }
    }
    printKept(report.kept.items);
}

pub fn printDedupeSummary(manager: *const PackageManager, installed: u32, start_time: i128) {
    const report = manager.dedupe_report ?? return;
    if (manager.options.log_level == .silent) return;
    printRows(*report);
    if (!manager.options.do.summary) {
        Output.flush();
        return;
    }
    const n = report.rows.items.len;
    Output.pretty("\n<b>{d}<r> duplicate version{s} removed", .{ n, plural(n) });
    if (installed > 0) {
        Output.pretty(", <b>{d}<r> package{s} installed", .{ installed, plural(installed) });
    }
    Output.pretty(" <d>(checked {d} package{s})<r> ", .{ report.checked, plural(report.checked) });
    Output.printStartEndStdout(start_time, std.time.nanoTimestamp());
    Output.pretty("\n", .{});
    Output.flush();
}

pub fn dedupeAfterDiffer(manager: *PackageManager) {
    const quiet = manager.options.log_level == .silent;

    if (manager.summary.changesDependencies()) {
        refuseOutOfDate(manager);
    }

    let report = bun.handleOom(dedupeLockfile(manager.allocator, manager.lockfile));
    if (report.rows.items.len == 0) {
        defer report.deinit();
        reportAlreadyDeduplicated(manager, *report);
    }

    if (manager.options.dry_run) {
        defer report.deinit();
        if (!quiet) {
            printWouldRemove(manager, *report);
            Output.prettyln("  <cyan>bun dedupe<r>", .{});
            Output.flush();
        }
        Global.exit(if (manager.options.dedupe_check) 1 else 0);
    }

    if (!manager.options.do.save_lockfile) {
        defer report.deinit();
        if (!quiet) {
            const why: []const u8 = if (!manager.options.enable.frozen_lockfile)
                "--no-save was passed"
            else if (manager.options.local_package_features.dev_dependencies)
                "--frozen-lockfile was passed"
            else
                "--production implies --frozen-lockfile";
            printWouldRemove(manager, *report);
            Output.flush();
            const n = report.rows.items.len;
            Output.prettyErrorln(
                "<r><red>error<r><d>:<r> {d} duplicate version{s} can be removed, but {s}",
                .{ n, plural(n), why },
            );
            const pronoun: []const u8 = if (n == 1) "it" else "them";
            Output.note("run 'bun dedupe' to remove {s}, or 'bun dedupe --check' in CI", .{pronoun});
            Output.flush();
        }
        Global.exit(1);
    }

    manager.dedupe_report = report;
    manager.options.enable.force_save_lockfile = true;
}
