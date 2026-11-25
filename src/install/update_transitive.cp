import bun from "bun";
import install from "./install.zig";
import Lockfile from "./lockfile.zig";
import DependencyMod from "./dependency.zig";
import ResolutionMod from "./resolution.zig";
import PackageManagerMod from "./PackageManager.zig";
import PopulateManifestCache from "./PackageManager/PopulateManifestCache.zig";
import runTasksMod from "./PackageManager/runTasks.zig";
import PackageManifestMapMod from "./PackageManifestMap.zig";
import Npm from "./npm.zig";
import dedupe from "./dedupe.zig";
import audit_fix from "./audit_fix.zig";
import update_scope from "./update_scope.zig";

type string = []const u8;
const Allocator = std.mem.Allocator;
const DynamicBitSet = bun.bit_set.DynamicBitSet;
const Output = bun.Output;
const Semver = bun.Semver;
const String = Semver.String;
const strings = bun.strings;
const DependencyID = install.DependencyID;
const PackageID = install.PackageID;
const PackageNameHash = install.PackageNameHash;
const invalid_package_id = install.invalid_package_id;
const Dependency = DependencyMod.Dependency;
const Behavior = DependencyMod.Behavior;
const Resolution = ResolutionMod.Resolution;
const PackageManager = PackageManagerMod.PackageManager;
const PackageUpdateInfo = PackageManagerMod.PackageUpdateInfo;
const PackageManifest = Npm.PackageManifest;
const UpdateScope = update_scope.UpdateScope;

pub struct DirectOwner {
    owner: PackageID,
    start: u32,
    len: u32,
}

pub struct DirectRow {
    name_hash: PackageNameHash,
    behavior: Behavior,
    resolved: PackageID,
}

pub struct PackageIdPair {
    old: PackageID,
    new: PackageID,
}

pub struct DepMove {
    dep_id: DependencyID,
    from_id: PackageID,
}

pub struct Row {
    name: string,
    from_ver: string,
    to_ver: string,
    later: string,
}

pub struct Report {
    rows: std.array_list.Managed(Row),
    moved: std.array_list.Managed(PackageID),

    static fn init(allocator: Allocator): Report {
        return Report{
            rows: std.array_list.Managed(Row).init(allocator),
            moved: std.array_list.Managed(PackageID).init(allocator),
        };
    }

    fn deinit(*_) {
        _.rows.deinit();
        _.moved.deinit();
    }
}

pub struct Pin {
    dep_id: DependencyID,
    from_id: PackageID,
    to: Semver.Version?,
}

struct Want {
    literal: String?,
    version: Dependency.Version,
    dep_ids: std.array_list.Managed(DependencyID),
}

struct Instance {
    pkg_id: PackageID,
    current: Semver.Version,
    wants: std.array_list.Managed(Want),
    held: bool,
}

struct PreStringPatch {
    start_idx: usize,
    end_idx: usize,
    hash: u64,
    pre: string,
}

pub struct DirectDependencies {
    owners: std.array_list.Managed(DirectOwner),
    rows: std.array_list.Managed(DirectRow),

    static fn init(allocator: Allocator): DirectDependencies {
        return DirectDependencies{
            owners: std.array_list.Managed(DirectOwner).init(allocator),
            rows: std.array_list.Managed(DirectRow).init(allocator),
        };
    }

    fn deinit(*_) {
        _.owners.deinit();
        _.rows.deinit();
    }

    static fn snapshot(allocator: Allocator, lockfile: *const Lockfile): DirectDependencies {
        const pkg_res: []const Resolution = lockfile.packages.items(.resolution);
        const dep_slices: []const Lockfile.DependencySlice = lockfile.packages.items(.dependencies);
        const res_slices = lockfile.packages.items(.resolutions);
        const deps = lockfile.buffers.dependencies.items;
        const resolutions = lockfile.buffers.resolutions.items;

        let out = DirectDependencies.init(allocator);
        for (owner of 0..pkg_res.len) {
            if (pkg_res[owner].tag != .root && pkg_res[owner].tag != .workspace) {
                continue;
            }
            const start: u32 = @intCast(out.rows.items.len);
            const owner_deps = dep_slices[owner].get(deps);
            const owner_res = res_slices[owner].get(resolutions);
            for (i of 0..owner_deps.len) {
                bun.handleOom(out.rows.append(DirectRow{
                    name_hash: owner_deps[i].name_hash,
                    behavior: owner_deps[i].behavior,
                    resolved: owner_res[i],
                }));
            }
            bun.handleOom(out.owners.append(DirectOwner{
                owner: @intCast(owner),
                start: start,
                len: @intCast(out.rows.items.len - start),
            }));
        }
        return out;
    }

    fn redirectDependents(lockfile: *Lockfile, loaded_package_count: usize) {
        if (_.owners.items.len == 0 || loaded_package_count == 0) {
            return;
        }
        let pairs = _.movedPairs(lockfile.allocator, lockfile);
        defer pairs.deinit();
        redirect(lockfile, pairs.items);
    }

    fn movedPairs(allocator: Allocator, lockfile: *const Lockfile): std.array_list.Managed(PackageIdPair) {
        const packages_len = lockfile.packages.len;
        const pkg_res: []const Resolution = lockfile.packages.items(.resolution);
        const name_hashes: []const PackageNameHash = lockfile.packages.items(.name_hash);
        const dep_slices: []const Lockfile.DependencySlice = lockfile.packages.items(.dependencies);
        const res_slices = lockfile.packages.items(.resolutions);
        const deps = lockfile.buffers.dependencies.items;
        const resolutions = lockfile.buffers.resolutions.items;

        let pairs = std.array_list.Managed(PackageIdPair).init(allocator);
        for (o of _.owners.items) {
            const owner: usize = o.owner;
            if (owner >= packages_len) continue;
            const rows = _.rows.items[o.start .. o.start + o.len];
            let claimed: DynamicBitSet? = null;
            defer if (*c := claimed) c.deinit();

            const owner_deps = dep_slices[owner].get(deps);
            const owner_res = res_slices[owner].get(resolutions);
            for (i of 0..owner_deps.len) {
                const dep = owner_deps[i];
                const new_id = owner_res[i];
                let index: usize = 0;
                let found_idx = false;

                if (claimed == null && i < rows.len && rows[i].name_hash == dep.name_hash && @as(u8, @bitCast(rows[i].behavior)) == @as(u8, @bitCast(dep.behavior))) {
                    index = @as(usize, @intCast(i));
                    found_idx = true;
                } else {
                    if (claimed == null) {
                        let taken = bun.handleOom(DynamicBitSet.initEmpty(allocator, rows.len));
                        taken.setRangeValue(.{ .start = 0, .end = @min(@as(usize, @intCast(i)), rows.len) }, true);
                        claimed = taken;
                    }
                    if (*taken := claimed) {
                        if (@as(usize, @intCast(i)) < taken.capacity() && !taken.isSet(@as(usize, @intCast(i))) && rows[i].name_hash == dep.name_hash && @as(u8, @bitCast(rows[i].behavior)) == @as(u8, @bitCast(dep.behavior))) {
                            index = @as(usize, @intCast(i));
                            found_idx = true;
                        } else {
                            for (k of 0..rows.len) {
                                if (!taken.isSet(@as(usize, @intCast(k))) && rows[k].name_hash == dep.name_hash && @as(u8, @bitCast(rows[k].behavior)) == @as(u8, @bitCast(dep.behavior))) {
                                    index = @as(usize, @intCast(k));
                                    found_idx = true;
                                    break;
                                }
                            }
                        }
                        if (found_idx) {
                            taken.set(index);
                        }
                    }
                }
                if (!found_idx) continue;

                const old_id = rows[index].resolved;
                if (old_id == new_id || old_id >= packages_len || new_id >= packages_len) continue;
                if (pkg_res[new_id].tag != .npm) continue;
                if (name_hashes[old_id] != name_hashes[new_id]) continue;
                bun.handleOom(pairs.append(PackageIdPair{ old: old_id, new: new_id }));
            }
        }
        return pairs;
    }
}

fn formatVersion(allocator: Allocator, ver: Semver.Version, buf: string): string {
    return bun.handleOom(std.fmt.allocPrint(allocator, "{f}", .{ver.fmt(buf)}));
}

fn laterInCache(manager: *PackageManager, pkg_id: PackageID): string {
    const lockfile = manager.lockfile;
    const buf = lockfile.buffers.string_bytes.items;
    const str_slice = bun.Semver.String.slice;
    const name = str_slice(&lockfile.packages.items(.name)[pkg_id], buf);
    const name_hash = lockfile.packages.items(.name_hash)[pkg_id];
    const resolution = lockfile.packages.items(.resolution)[pkg_id];
    if (later := manager.formatLaterVersionInCache(name, name_hash, resolution)) {
        return bun.handleOom(std.fmt.allocPrint(manager.allocator, "{f}", .{later}));
    }
    return "";
}

fn rowsBetween(manager: *PackageManager, pairs: []const PackageIdPair): std.array_list.Managed(Row) {
    const alloc = manager.allocator;
    let rows = std.array_list.Managed(Row).init(alloc);
    for (pair of pairs) {
        const lockfile = manager.lockfile;
        const buf = lockfile.buffers.string_bytes.items;
        const pkg_res: []const Resolution = lockfile.packages.items(.resolution);
        if (pkg_res[pair.old].tag != .npm) continue;
        const str_slice = bun.Semver.String.slice;
        const name = bun.handleOom(alloc.dupe(u8, str_slice(&lockfile.packages.items(.name)[pair.old], buf)));
        const from_ver = formatVersion(alloc, pkg_res[pair.old].value.npm.version, buf);
        const to_ver = formatVersion(alloc, pkg_res[pair.new].value.npm.version, buf);
        const later = laterInCache(manager, pair.new);
        bun.handleOom(rows.append(Row{
            name: name,
            from_ver: from_ver,
            to_ver: to_ver,
            later: later,
        }));
    }
    return rows;
}

fn namedPairs(allocator: Allocator, lockfile: *const Lockfile, moved: []const DepMove): std.array_list.Managed(PackageIdPair) {
    const packages_len = lockfile.packages.len;
    const pkg_res: []const Resolution = lockfile.packages.items(.resolution);
    const name_hashes: []const PackageNameHash = lockfile.packages.items(.name_hash);
    const resolutions = lockfile.buffers.resolutions.items;

    let pairs = std.array_list.Managed(PackageIdPair).init(allocator);
    for (m of moved) {
        const from_id = m.from_id;
        const to_id = resolutions[m.dep_id];
        if (from_id != to_id && from_id < packages_len && to_id < packages_len && pkg_res[to_id].tag == .npm && name_hashes[from_id] == name_hashes[to_id]) {
            bun.handleOom(pairs.append(PackageIdPair{ old: from_id, new: to_id }));
        }
    }
    return pairs;
}

fn redirect(lockfile: *Lockfile, pairs: []const PackageIdPair) {
    if (pairs.len == 0) return;
    const alloc = lockfile.allocator;
    let new_of = bun.handleOom(alloc.alloc(PackageID, lockfile.packages.len));
    defer alloc.free(new_of);
    @memset(new_of, invalid_package_id);
    for (pair of pairs) {
        if (new_of[pair.old] == invalid_package_id) {
            new_of[pair.old] = pair.new;
        }
    }

    const buf = lockfile.buffers.string_bytes.items;
    const pkg_res: []const Resolution = lockfile.packages.items(.resolution);
    const deps = lockfile.buffers.dependencies.items;
    let resolutions = lockfile.buffers.resolutions.items;

    for (j of 0..resolutions.len) {
        const target = resolutions[j];
        if (target >= new_of.len) continue;
        const new_id = new_of[target];
        if (new_id == invalid_package_id) continue;
        const dep = deps[j];
        if (dep.behavior.isBundled()) continue;
        const range = dedupe.effectiveNpmRange(lockfile, @as(DependencyID, @intCast(j)), dep) ?? continue;
        if (range.value.npm.version.satisfies(pkg_res[new_id].value.npm.version, buf, buf)) {
            resolutions[j] = new_id;
        }
    }
}

pub struct TransitiveUpdate {
    pins: std.array_list.Managed(Pin),
    report: Report?,

    static fn initEmpty(allocator: Allocator): TransitiveUpdate {
        return TransitiveUpdate{
            pins: std.array_list.Managed(Pin).init(allocator),
            report: null,
        };
    }

    fn deinit(*_) {
        _.pins.deinit();
        if (*r := _.report) {
            r.deinit();
        }
    }

    static fn plan(manager: *PackageManager, direct: *const DirectDependencies): TransitiveUpdate! {
        const lockfile = manager.lockfile;
        let scope = UpdateScope.of(manager);
        defer scope.deinit();

        let edges = bun.handleOom(DynamicBitSet.initEmpty(lockfile.allocator, lockfile.buffers.dependencies.items.len));
        defer edges.deinit();

        const whole = scope.isWholeWorkspace();
        const owners_opt: (*const DynamicBitSet)? = if (!whole) scope.reachable(lockfile) else null;
        for (owner of 0..lockfile.packages.len) {
            let in_scope = true;
            if (owners_opt != null) {
                const r = owners_opt.?;
                in_scope = owner < r.capacity() && r.isSet(@as(usize, @intCast(owner)));
            }
            if (in_scope) {
                setRowsOf(&edges, lockfile, @as(usize, @intCast(owner)));
            }
        }

        let plan_res = planEdges(manager, &edges, direct)?;
        registerMoved(manager, plan_res.report.moved.items)?;
        const printed_here = manager.options.dry_run || manager.options.lockfile_only;
        if (!printed_here) {
            plan_res.report.deinit();
        }
        return TransitiveUpdate{
            pins: plan_res.pins,
            report: if (printed_here) plan_res.report else null,
        };
    }

    fn enqueue(manager: *PackageManager)! {
        return _.enqueueInner(manager, null);
    }

    fn enqueueTracked(manager: *PackageManager): DynamicBitSet! {
        let pinned = bun.handleOom(DynamicBitSet.initEmpty(manager.allocator, manager.lockfile.buffers.resolutions.items.len));
        _.enqueueInner(manager, &pinned)?;
        return pinned;
    }

    fn enqueueInner(manager: *PackageManager, pinned: (*DynamicBitSet)?)! {
        if (_.pins.items.len == 0) return;
        * = manager.getCacheDirectory();
        * = manager.getTemporaryDirectory();
        for (pin of _.pins.items) {
            if (manager.lockfile.buffers.resolutions.items[pin.dep_id] != pin.from_id) {
                continue;
            }
            if (to := pin.to) {
                audit_fix.enqueuePinned(manager, pin.dep_id, to)?;
            } else {
                reresolve(manager, pin.dep_id)?;
            }
            if (p := pinned) {
                p.set(pin.dep_id);
            }
            manager.summary.update += 1;
        }
    }

    fn printPlan(manager: *PackageManager, direct: *const DirectDependencies, named: []const DepMove) {
        const options = manager.options;
        if (manager.subcommand != .update || !(options.dry_run || options.lockfile_only) || !options.do.summary) {
            return;
        }
        const dry_run = options.dry_run;
        const alloc = manager.allocator;
        let rows = std.array_list.Managed(Row).init(alloc);
        defer rows.deinit();
        if (rep := _.report) {
            for (r of rep.rows.items) {
                bun.handleOom(rows.append(r));
            }
        }
        let pairs = direct.movedPairs(alloc, manager.lockfile);
        defer pairs.deinit();
        let np = namedPairs(alloc, manager.lockfile, named);
        defer np.deinit();
        for (p of np.items) {
            bun.handleOom(pairs.append(p));
        }
        let between = rowsBetween(manager, pairs.items);
        defer between.deinit();
        for (r of between.items) {
            bun.handleOom(rows.append(r));
        }
        sortDedupRows(&rows);

        let kept = keptPatchedRows(manager);
        defer kept.deinit();

        if (!dry_run && rows.items.len == 0 && kept.items.len == 0) {
            return;
        }
        Output.prettyln("", .{});
        printRows(rows.items);
        printKeptRows(kept.items);
        if (dry_run) {
            if (rows.items.len > 0 || kept.items.len > 0) {
                Output.prettyln("", .{});
            }
            if (rows.items.len == 0) {
                const n = manager.lockfile.packages.len;
                const s: string = if (n == 1) "" else "s";
                Output.pretty("Checked <green>{d} package{s}<r>, nothing to update ", .{ n, s });
            } else {
                const n = rows.items.len;
                const s: string = if (n == 1) "" else "s";
                Output.pretty("<green>{d}<r> package{s} would be updated ", .{ n, s });
            }
            Output.printStartEndStdout(bun.cli.start_time, std.time.nanoTimestamp());
            Output.prettyln("", .{});
        }
        Output.flush();
    }

    fn redirectDependents(lockfile: *Lockfile) {
        let moved = std.array_list.Managed(DepMove).init(lockfile.allocator);
        defer moved.deinit();
        for (pin of _.pins.items) {
            bun.handleOom(moved.append(DepMove{ dep_id: pin.dep_id, from_id: pin.from_id }));
        }
        redirectMovedEdges(lockfile, moved.items);
    }
}

pub fn refreshChildrenOf(manager: *PackageManager, package_ids: []const PackageID): std.array_list.Managed(DepMove)! {
    const alloc = manager.allocator;
    let out = std.array_list.Managed(DepMove).init(alloc);
    if (package_ids.len == 0) return out;

    const lockfile = manager.lockfile;
    let edges = bun.handleOom(DynamicBitSet.initEmpty(alloc, lockfile.buffers.dependencies.items.len));
    defer edges.deinit();
    for (owner of package_ids) {
        if (owner < lockfile.packages.len) {
            setRowsOf(&edges, lockfile, @as(usize, @intCast(owner)));
        }
    }
    let plan_res = planEdges(manager, &edges, &DirectDependencies.init(alloc))?;
    defer plan_res.report.deinit();
    registerMoved(manager, plan_res.report.moved.items)?;
    let update = TransitiveUpdate{ pins: plan_res.pins, report: null };
    defer update.pins.deinit();
    update.enqueue(manager)?;
    for (pin of update.pins.items) {
        bun.handleOom(out.append(DepMove{ dep_id: pin.dep_id, from_id: pin.from_id }));
    }
    return out;
}

fn reresolve(manager: *PackageManager, dep_id: DependencyID)! {
    let dep = manager.lockfile.buffers.dependencies.items[dep_id];
    dep.behavior.set(.peer, false);
    manager.lockfile.buffers.resolutions.items[dep_id] = invalid_package_id;
    manager.enqueueDependencyWithMain(dep_id, &dep, invalid_package_id, false)?;
}

pub fn enqueuePeerRows(
    manager: *PackageManager,
    rows: []const DependencyID,
    moved: *std.array_list.Managed(DepMove),
)! {
    if (rows.len > 0) {
        let targets = std.array_list.Managed(PackageID).init(manager.allocator);
        defer targets.deinit();
        for (row of rows) {
            bun.handleOom(targets.append(manager.lockfile.buffers.resolutions.items[row]));
        }
        const empty_ctx: void = undefined;
        std.mem.sort(PackageID, targets.items, empty_ctx, struct {
            static fn lessThan(unused_ctx: void, a: PackageID, b: PackageID): bool {
                * = unused_ctx;
                return a < b;
            }
        }.lessThan);
        // dedup in place
        let write_idx: usize = 0;
        for (i of 0..targets.items.len) {
            if (i == 0 || targets.items[i] != targets.items[write_idx - 1]) {
                targets.items[write_idx] = targets.items[i];
                write_idx += 1;
            }
        }
        targets.items.len = write_idx;

        PopulateManifestCache.populateManifestCache(manager, PopulateManifestCache.Packages{ ids: targets.items })?;
        printLog(manager)?;
        for (row of rows) {
            bun.handleOom(moved.append(DepMove{ dep_id: row, from_id: manager.lockfile.buffers.resolutions.items[row] }));
            reresolve(manager, row)?;
        }
    }

    let from_ids = std.array_list.Managed(PackageID).init(manager.allocator);
    defer from_ids.deinit();
    for (m of moved.items) {
        bun.handleOom(from_ids.append(m.from_id));
    }
    const empty_ctx: void = undefined;
    std.mem.sort(PackageID, from_ids.items, empty_ctx, struct {
        static fn lessThan(unused_ctx: void, a: PackageID, b: PackageID): bool {
            * = unused_ctx;
            return a < b;
        }
    }.lessThan);
    let w: usize = 0;
    for (i of 0..from_ids.items.len) {
        if (i == 0 || from_ids.items[i] != from_ids.items[w - 1]) {
            from_ids.items[w] = from_ids.items[i];
            w += 1;
        }
    }
    from_ids.items.len = w;
    registerMoved(manager, from_ids.items)?;
}

fn printLog(manager: *PackageManager)! {
    const failed = manager.log.hasErrors();
    if (manager.options.log_level != .silent || failed) {
        manager.log.print(Output.errorWriter())?;
    }
    manager.log.reset();
    if (failed) {
        return error.InstallFailed;
    }
}

pub fn redirectMovedEdges(lockfile: *Lockfile, moved: []const DepMove) {
    let pairs = std.array_list.Managed(PackageIdPair).init(lockfile.allocator);
    defer pairs.deinit();
    const pkg_res: []const Resolution = lockfile.packages.items(.resolution);
    const resolutions = lockfile.buffers.resolutions.items;
    for (m of moved) {
        const to_id = resolutions[m.dep_id];
        if (to_id != m.from_id && to_id < pkg_res.len && pkg_res[to_id].tag == .npm) {
            bun.handleOom(pairs.append(PackageIdPair{ old: m.from_id, new: to_id }));
        }
    }
    redirect(lockfile, pairs.items);
}

pub fn movedTargetsAfterClean(
    allocator: Allocator,
    before_clean: *const Lockfile,
    cleaned: *const Lockfile,
    moved: []const DepMove,
): std.array_list.Managed(PackageID) {
    let targets = std.array_list.Managed(PackageID).init(allocator);
    if (moved.len == 0) return targets;

    const old_resolutions = before_clean.buffers.resolutions.items;
    const old_name_hashes: []const PackageNameHash = before_clean.packages.items(.name_hash);
    const old_res: []const Resolution = before_clean.packages.items(.resolution);
    const old_buf = before_clean.buffers.string_bytes.items;
    const new_res: []const Resolution = cleaned.packages.items(.resolution);
    const new_buf = cleaned.buffers.string_bytes.items;

    for (m of moved) {
        if (m.dep_id >= old_resolutions.len) continue;
        const old_id = old_resolutions[m.dep_id];
        if (old_id >= old_res.len) continue;
        const entry = cleaned.package_index.get(old_name_hashes[old_id]) ?? continue;
        for (c of entry.slice()) {
            if (new_res[c].eql(&old_res[old_id], new_buf, old_buf)) {
                bun.handleOom(targets.append(c));
                break;
            }
        }
    }
    return targets;
}

pub fn registerMoved(manager: *PackageManager, moved: []const PackageID)! {
    if (moved.len == 0) return;
    const lockfile = manager.lockfile;
    const buf = lockfile.buffers.string_bytes.items;
    const names: []const String = lockfile.packages.items(.name);
    const res: []const Resolution = lockfile.packages.items(.resolution);

    for (pkg_id of moved) {
        if (pkg_id >= res.len || res[pkg_id].tag != .npm) continue;
        const current = res[pkg_id].value.npm.version;
        const str_slice = bun.Semver.String.slice;
        const entry = manager.updating_packages.getOrPut(manager.allocator, str_slice(&names[pkg_id], buf))?;
        if (entry.found_existing) {
            const info = entry.value_ptr.*;
            let keep = info.original_version_literal.len > 0;
            if (!keep) {
                if (orig := info.original_version) {
                    keep = orig.order(current, info.original_version_string_buf, buf) != .gt;
                }
            }
            if (keep) continue;
        }
        const tag_len = current.tag.pre.len() + current.tag.build.len();
        const tag_buf = bun.handleOom(manager.allocator.alloc(u8, tag_len));
        let cursor: []u8 = tag_buf;
        let original = current;
        original.tag = current.tag.cloneInto(buf, &cursor);
        entry.value_ptr.* = PackageUpdateInfo{
            original_version_literal: "",
            is_alias: false,
            original_version_string_buf: tag_buf,
            original_version: original,
        };
    }
}

fn sortDedupRows(rows: *std.array_list.Managed(Row)) {
    const empty_ctx: void = undefined;
    std.mem.sort(Row, rows.items, empty_ctx, struct {
        static fn lessThan(unused_ctx: void, a: Row, b: Row): bool {
            * = unused_ctx;
            const n_ord = std.mem.order(u8, a.name, b.name);
            if (n_ord != .eq) return n_ord == .lt;
            const f_ord = std.mem.order(u8, a.from_ver, b.from_ver);
            if (f_ord != .eq) return f_ord == .lt;
            const t_ord = std.mem.order(u8, a.to_ver, b.to_ver);
            if (t_ord != .eq) return t_ord == .lt;
            return std.mem.order(u8, a.later, b.later) == .lt;
        }
    }.lessThan);

    let write_idx: usize = 0;
    for (i of 0..rows.items.len) {
        if (i == 0 ||
            !strings.eqlLong(rows.items[i].name, rows.items[write_idx - 1].name, true) ||
            !strings.eqlLong(rows.items[i].from_ver, rows.items[write_idx - 1].from_ver, true) ||
            !strings.eqlLong(rows.items[i].to_ver, rows.items[write_idx - 1].to_ver, true))
        {
            rows.items[write_idx] = rows.items[i];
            write_idx += 1;
        }
    }
    rows.items.len = write_idx;
}

fn printRows(rows: []const Row) {
    for (row of rows) {
        audit_fix.prettyUpdateRow(row.name, row.from_ver, row.to_ver, false);
        if (row.later.len > 0) {
            Output.pretty(" <d>(<blue>v{s} available<r><d>)<r>", .{row.later});
        }
        Output.prettyln("", .{});
    }
}

fn keptPatchedRows(manager: *PackageManager): std.array_list.Managed(Row) {
    const alloc = manager.allocator;
    let rows = std.array_list.Managed(Row).init(alloc);
    let kept = manager.kept_patched;
    manager.kept_patched = .{};
    defer kept.deinit(alloc);

    const empty_ctx: void = undefined;
    std.mem.sort(PackageID, kept.items, empty_ctx, struct {
        static fn lessThan(unused_ctx: void, a: PackageID, b: PackageID): bool {
            * = unused_ctx;
            return a < b;
        }
    }.lessThan);

    let prev: PackageID = invalid_package_id;
    for (id of kept.items) {
        if (id == prev) continue;
        prev = id;
        const later = newestAllowed(manager, id) ?? continue;
        const lockfile = manager.lockfile;
        const buf = lockfile.buffers.string_bytes.items;
        const str_slice = bun.Semver.String.slice;
        const name = bun.handleOom(alloc.dupe(u8, str_slice(&lockfile.packages.items(.name)[id], buf)));
        const from_ver = formatVersion(alloc, lockfile.packages.items(.resolution)[id].value.npm.version, buf);
        bun.handleOom(rows.append(Row{
            name: name,
            from_ver: from_ver,
            to_ver: "",
            later: later,
        }));
    }
    return rows;
}

fn renderKeptRows(allocator: Allocator, rows: []const Row): string {
    let out = std.array_list.Managed(u8).init(allocator);
    for (row of rows) {
        const line = if (Output.enable_ansi_colors_stdout)
            bun.handleOom(std.fmt.allocPrint(allocator, Output.prettyFmt("<d>kept<r> <b>{s}<r><d>@{s}<r> <d>(patched, <blue>v{s} available<r><d>)<r>\n", true), .{ row.name, row.from_ver, row.later }))
        else
            bun.handleOom(std.fmt.allocPrint(allocator, Output.prettyFmt("<d>kept<r> <b>{s}<r><d>@{s}<r> <d>(patched, <blue>v{s} available<r><d>)<r>\n", false), .{ row.name, row.from_ver, row.later }));
        defer allocator.free(line);
        bun.handleOom(out.appendSlice(line));
    }
    return bun.handleOom(out.toOwnedSlice());
}

fn printKeptRows(rows: []const Row) {
    if (rows.len > 0) {
        for (row of rows) {
            Output.pretty("<d>kept<r> <b>{s}<r><d>@{s}<r> <d>(patched, <blue>v{s} available<r><d>)<r>\n", .{ row.name, row.from_ver, row.later });
        }
    }
}

pub fn printKeptPatched(manager: *PackageManager) {
    if (manager.kept_patched.items.len == 0) return;
    if (!manager.options.do.summary) {
        manager.kept_patched.clearRetainingCapacity();
        return;
    }
    let rows = keptPatchedRows(manager);
    defer rows.deinit();
    manager.kept_patched_text = renderKeptRows(manager.allocator, rows.items);
}

pub fn warnOrphanedPatches(manager: *PackageManager, root_package_json_path: [:0]const u8) {
    if (manager.lockfile.patched_dependencies.count() == 0 || manager.options.log_level == .silent) {
        return;
    }
    let keys = std.array_list.Managed(string).init(manager.allocator);
    defer keys.deinit();

    switch (manager.workspace_package_json_cache.getWithPath(
        manager.allocator,
        manager.log,
        root_package_json_path,
        .{},
    )) {
        .entry entry => {
            if (patched := entry.root.asProperty("patchedDependencies")) {
                if (patched.expr.data == .e_object) {
                    for (prop of patched.expr.data.e_object.properties.slice()) {
                        if (k := prop.key) {
                            if (ks := k.asString(manager.allocator)) {
                                bun.handleOom(keys.append(ks));
                            }
                        }
                    }
                }
            }
        },
        else => { return; },
    }

    const lockfile = manager.lockfile;
    const buf = lockfile.buffers.string_bytes.items;
    const pkg_res: []const Resolution = lockfile.packages.items(.resolution);
    for (key of keys.items) {
        const at = strings.lastIndexOfChar(key, '@') ?? continue;
        if (at == 0) continue;
        const patch = lockfile.patched_dependencies.get(String.Builder.stringHash(key)) ?? continue;
        const name = key[0..at];
        const version = key[at + 1 ..];
        const entry = lockfile.package_index.get(String.Builder.stringHash(name));
        let one_id: [1]PackageID = [@as(PackageID, 0)];
        const installed: []const PackageID = if (e := entry) switch (e) {
            .id v => brk: {
                one_id[0] = v;
                break :brk one_id[0..1];
            },
            .ids list => list.items,
        } else [];
        let non_npm = false;
        for (id of installed) {
            if (pkg_res[id].tag != .npm) {
                non_npm = true;
                break;
            }
        }
        if (non_npm) continue;

        let now = std.array_list.Managed(u8).init(manager.allocator);
        defer now.deinit();
        let still_applies = false;
        for (id of installed) {
            const current = formatVersion(manager.allocator, pkg_res[id].value.npm.version, buf);
            defer manager.allocator.free(current);
            if (strings.eqlLong(current, version, true)) {
                still_applies = true;
                break;
            }
            if (now.items.len > 0) bun.handleOom(now.appendSlice(", "));
            bun.handleOom(now.appendSlice(current));
        }
        if (still_applies) continue;
        if (now.items.len == 0) {
            const str_slice = bun.Semver.String.slice;
            Output.warn("{s} no longer applies ({s} is no longer installed)", .{ str_slice(&patch.path, buf), name });
        } else {
            const str_slice = bun.Semver.String.slice;
            Output.warn("{s} no longer applies ({s} is now {s})", .{ str_slice(&patch.path, buf), name, now.items });
        }
    }
    Output.flush();
}

fn newestAllowed(manager: *PackageManager, pkg_id: PackageID): string? {
    const min_age = manager.options.minimum_release_age_ms;
    const excludes = manager.options.minimum_release_age_excludes;
    const lockfile = manager.lockfile;
    const buf = lockfile.buffers.string_bytes.items;
    const res: []const Resolution = lockfile.packages.items(.resolution);
    if (pkg_id >= res.len || res[pkg_id].tag != .npm) return null;
    const current = res[pkg_id].value.npm.version;
    const str_slice = bun.Semver.String.slice;
    const name = str_slice(&lockfile.packages.items(.name)[pkg_id], buf);
    let expired = false;
    const manifest = manager.manifests.byNameHashAllowExpired(
        manager,
        manager.scopeForPackageName(name),
        lockfile.packages.items(.name_hash)[pkg_id],
        &expired,
        .load_from_memory_fallback_to_disk,
        min_age != null,
    ) ?? return null;
    const manifest_buf = manifest.string_buf;
    let best: Semver.Version? = null;
    const deps = lockfile.buffers.dependencies.items;
    const resolutions = lockfile.buffers.resolutions.items;
    for (dep_id of 0..deps.len) {
        if (resolutions[dep_id] != pkg_id) continue;
        const dep = deps[dep_id];
        if (dep.behavior.isBundled()) continue;
        const range = dedupe.effectiveNpmRange(lockfile, @as(DependencyID, @intCast(dep_id)), dep) ?? continue;
        const found = manifest.findBestVersionWithFilter(range.value.npm.version, buf, min_age, excludes).unwrap() ?? continue;
        if (best == null || found.version.order(best.?, manifest_buf, manifest_buf) == .gt) {
            best = found.version;
        }
    }
    const b = best ?? return null;
    if (b.order(current, manifest_buf, buf) == .gt) {
        return formatVersion(manager.allocator, b, manifest_buf);
    }
    return null;
}

fn setRowsOf(edges: *DynamicBitSet, lockfile: *const Lockfile, owner: usize) {
    const slice = lockfile.packages.items(.dependencies)[owner];
    if (slice.len == 0) return;
    const tag = lockfile.packages.items(.resolution)[owner].tag;
    if (tag == .root || tag == .workspace) return;
    edges.setRangeValue(.{ .start = slice.off, .end = slice.off + slice.len }, true);
}

pub fn plannablePeerRows(lockfile: *const Lockfile, direct: *const DirectDependencies): DynamicBitSet {
    const packages_len = lockfile.packages.len;
    const pkg_res: []const Resolution = lockfile.packages.items(.resolution);
    const dep_slices: []const Lockfile.DependencySlice = lockfile.packages.items(.dependencies);
    const res_slices = lockfile.packages.items(.resolutions);
    const deps = lockfile.buffers.dependencies.items;
    const resolutions = lockfile.buffers.resolutions.items;

    let providers = bun.handleOom(DynamicBitSet.initEmpty(lockfile.allocator, packages_len));
    defer providers.deinit();
    for (row of direct.rows.items) {
        if (!row.behavior.isPeer() && row.resolved < packages_len) {
            providers.set(row.resolved);
        }
    }
    for (owner of 0..packages_len) {
        const owner_deps = dep_slices[owner].get(deps);
        const owner_res = res_slices[owner].get(resolutions);
        for (i of 0..owner_deps.len) {
            const resolved = owner_res[i];
            if (!owner_deps[i].behavior.isPeer() && resolved < packages_len) {
                providers.set(resolved);
            }
        }
    }

    let rows = bun.handleOom(DynamicBitSet.initEmpty(lockfile.allocator, deps.len));
    for (owner of 0..packages_len) {
        if (pkg_res[owner].tag == .root || pkg_res[owner].tag == .workspace) continue;
        const first = dep_slices[owner].off;
        const owner_deps = dep_slices[owner].get(deps);
        const owner_res = res_slices[owner].get(resolutions);
        for (i of 0..owner_deps.len) {
            const dep = owner_deps[i];
            const resolved = owner_res[i];
            if (dep.behavior.isPeer() && !dep.behavior.isOptionalPeer() && resolved < packages_len && !providers.isSet(resolved)) {
                rows.set(@as(usize, first) + @as(usize, @intCast(i)));
            }
        }
    }
    return rows;
}

struct PlanResult {
    pins: std.array_list.Managed(Pin),
    report: Report,
}

fn planEdges(
    manager: *PackageManager,
    edges: *const DynamicBitSet,
    direct: *const DirectDependencies,
): PlanResult! {
    const alloc = manager.allocator;
    let instances = std.array_list.Managed(Instance).init(alloc);
    defer {
        for (*inst of instances.items) {
            for (*w of inst.wants.items) {
                w.dep_ids.deinit();
            }
            inst.wants.deinit();
        }
        instances.deinit();
    }
    let kept = std.array_list.Managed(PackageID).init(alloc);
    defer kept.deinit();

    {
        const lockfile = manager.lockfile;
        const res: []const Resolution = lockfile.packages.items(.resolution);
        const has_patches = lockfile.patched_dependencies.count() > 0;
        const SKIP: u32 = std.math.maxInt(u32) - 1;
        const UNSET: u32 = std.math.maxInt(u32);
        let instance_of = bun.handleOom(alloc.alloc(u32, res.len));
        defer alloc.free(instance_of);
        @memset(instance_of, UNSET);

        const no_overrides = lockfile.overrides.map.count() == 0;
        const buf = lockfile.buffers.string_bytes.items;
        const pkg_names: []const String = lockfile.packages.items(.name);
        const deps = lockfile.buffers.dependencies.items;
        const resolutions = lockfile.buffers.resolutions.items;

        let plannable_peers: DynamicBitSet? = null;
        defer if (*p := plannable_peers) p.deinit();

        let it = edges.iterator(.{ .kind = .set });
        while (dep_id := it.next()) {
            if (dep_id >= resolutions.len) break;
            const target = resolutions[dep_id];
            if (target >= instance_of.len) continue;
            const slot = instance_of[target];
            const dep = deps[dep_id];
            if (slot == SKIP || dep.behavior.isBundled()) continue;

            if (dep.behavior.isPeer()) {
                if (plannable_peers == null) {
                    plannable_peers = plannablePeerRows(lockfile, direct);
                }
                if (!plannable_peers.?.isSet(dep_id)) continue;
            }

            let instance_idx: u32 = slot;
            if (slot == UNSET) {
                if (res[target].tag != .npm) {
                    instance_of[target] = SKIP;
                    continue;
                }
                let held = false;
                if (has_patches) {
                    const lbl = dedupe.label(alloc, lockfile, target)?;
                    defer alloc.free(lbl);
                    held = lockfile.patched_dependencies.contains(String.Builder.stringHash(lbl));
                }
                if (held) {
                    bun.handleOom(kept.append(target));
                }
                instance_idx = @intCast(instances.items.len);
                instance_of[target] = instance_idx;
                bun.handleOom(instances.append(Instance{
                    pkg_id: target,
                    current: res[target].value.npm.version,
                    wants: std.array_list.Managed(Want).init(alloc),
                    held: held,
                }));
            }

            const dep_id_u32: DependencyID = @intCast(dep_id);
            let inst = &instances.items[instance_idx];
            let literal: String? = null;
            if (no_overrides) {
                if (dep.version.tag == .npm && !dep.version.value.npm.is_alias) {
                    literal = dep.version.literal;
                } else if (dep.version.tag == .dist_tag) {
                    literal = dep.version.literal;
                }
            }
            let matched_want = false;
            if (lit := literal) {
                for (*want of inst.wants.items) {
                    if (wl := want.literal) {
                        if (wl.eql(lit, buf, buf)) {
                            bun.handleOom(want.dep_ids.append(dep_id_u32));
                            matched_want = true;
                            break;
                        }
                    }
                }
            }
            if (matched_want) continue;

            const version = dedupe.effectiveVersion(lockfile, dep_id_u32, dep) ?? continue;
            const vname = switch (version.tag) {
                .npm => version.value.npm.name,
                .dist_tag => version.value.dist_tag.name,
                else => continue,
            };
            if (!vname.eql(pkg_names[inst.pkg_id], buf, buf)) continue;

            let dep_ids = std.array_list.Managed(DependencyID).init(alloc);
            bun.handleOom(dep_ids.append(dep_id_u32));
            bun.handleOom(inst.wants.append(Want{
                literal: literal,
                version: version,
                dep_ids: dep_ids,
            }));
        }
    }

    if (kept.items.len > 0) {
        bun.handleOom(manager.kept_patched.appendSlice(alloc, kept.items));
    }

    // Filter out instances with empty wants
    let write_i: usize = 0;
    for (i of 0..instances.items.len) {
        if (instances.items[i].wants.items.len > 0) {
            instances.items[write_i] = instances.items[i];
            write_i += 1;
        } else {
            instances.items[i].wants.deinit();
        }
    }
    instances.items.len = write_i;

    if (instances.items.len == 0) {
        return PlanResult{
            pins: std.array_list.Managed(Pin).init(alloc),
            report: Report.init(alloc),
        };
    }

    let ids = std.array_list.Managed(PackageID).init(alloc);
    defer ids.deinit();
    for (inst of instances.items) {
        bun.handleOom(ids.append(inst.pkg_id));
    }
    PopulateManifestCache.populateManifestCache(manager, PopulateManifestCache.Packages{ ids: ids.items })?;

    const min_age = manager.options.minimum_release_age_ms;
    const excludes = manager.options.minimum_release_age_excludes;
    const buf = manager.lockfile.buffers.string_bytes.items;
    const pkg_names: []const String = manager.lockfile.packages.items(.name);

    let pins = std.array_list.Managed(Pin).init(alloc);
    let report = Report.init(alloc);
    let pre_strings = std.array_list.Managed(PreStringPatch).init(alloc);
    defer pre_strings.deinit();

    for (inst of instances.items) {
        if (inst.held) continue;
        const str_slice = bun.Semver.String.slice;
        const name = str_slice(&pkg_names[inst.pkg_id], buf);
        let expired = false;
        const scope = manager.scopeForPackageName(name);
        const manifest = manager.manifests.byNameAllowExpired(
            manager,
            scope,
            name,
            &expired,
            .load_from_memory_fallback_to_disk,
            min_age != null,
        ) ?? continue;
        const manifest_buf = manifest.string_buf;
        const rows_before = report.rows.items.len;

        for (want of inst.wants.items) {
            let v: Semver.Version = undefined;
            let to_ver_opt: Semver.Version? = null;
            let later: string = "";
            if (want.version.tag == .npm) {
                const range = want.version.value.npm.version;
                const found = manifest.findBestVersionWithFilter(range, buf, min_age, excludes).unwrap() ?? continue;
                v = found.version;
                if (v.order(inst.current, manifest_buf, buf) != .gt) continue;
                if (!v.tag.pre.value.isInline()) {
                    const start_idx = pins.items.len;
                    const end_idx = start_idx + want.dep_ids.items.len;
                    bun.handleOom(pre_strings.append(PreStringPatch{
                        start_idx: start_idx,
                        end_idx: end_idx,
                        hash: v.tag.pre.hash,
                        pre: bun.handleOom(alloc.dupe(u8, v.tag.pre.slice(manifest_buf))),
                    }));
                }
                to_ver_opt = v;
                later = laterThan(alloc, manifest, v, min_age, excludes);
            } else {
                const tag = str_slice(&want.version.value.dist_tag.tag, buf);
                const found = manifest.findByDistTagWithFilter(tag, min_age, excludes).unwrap() ?? continue;
                if (found.version.order(inst.current, manifest_buf, buf) == .eq) continue;
                v = found.version;
                to_ver_opt = null;
            }
            for (dep_id of want.dep_ids.items) {
                bun.handleOom(pins.append(Pin{
                    dep_id: dep_id,
                    from_id: inst.pkg_id,
                    to: to_ver_opt,
                }));
            }
            bun.handleOom(report.rows.append(Row{
                name: bun.handleOom(alloc.dupe(u8, name)),
                from_ver: formatVersion(alloc, inst.current, buf),
                to_ver: formatVersion(alloc, v, manifest_buf),
                later: later,
            }));
        }
        if (report.rows.items.len != rows_before) {
            bun.handleOom(report.moved.append(inst.pkg_id));
        }
    }

    printLog(manager)?;

    for (patch of pre_strings.items) {
        let sb = manager.lockfile.stringBuilder();
        const pre = sb.appendWithHash(Semver.ExternalString, patch.pre, patch.hash);
        let idx: usize = patch.start_idx;
        while (idx < patch.end_idx) : (idx += 1) {
            if (*to := pins.items[idx].to) {
                to.tag.pre = pre;
            }
        }
    }

    sortDedupRows(&report.rows);
    return PlanResult{ pins: pins, report: report };
}

fn laterThan(
    allocator: Allocator,
    manifest: *const PackageManifest,
    v: Semver.Version,
    min_age: f64?,
    excludes: ([]const string)?,
): string {
    if (v.tag.hasPre()) return "";
    const manifest_buf = manifest.string_buf;
    const found = manifest.findByDistTagWithFilter("latest", min_age, excludes).unwrap() ?? return "";
    if (found.version.order(v, manifest_buf, manifest_buf) == .gt) {
        return formatVersion(allocator, found.version, manifest_buf);
    }
    return "";
}
