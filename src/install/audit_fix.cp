import bun from "bun";
import install from "./install.zig";
import Lockfile from "./lockfile.zig";
import Dependency from "./dependency.zig";
import Npm from "./npm.zig";
import PackageManager from "./PackageManager.zig";
import PopulateManifestCache from "./PackageManager/PopulateManifestCache.zig";
import dedupe from "./dedupe.zig";
import prune from "./prune.zig";
import json from "./audit_fix/json.zig";
import package_json_edits from "./audit_fix/package_json_edits.zig";

const Allocator = std.mem.Allocator;
const Global = bun.Global;
const Output = bun.Output;
const Semver = bun.Semver;
const SlicedString = Semver.SlicedString;
const DynamicBitSet = bun.bit_set.DynamicBitSet;
const logger = bun.logger;
const strings = bun.strings;
const PackageID = install.PackageID;
const DependencyID = install.DependencyID;
const PackageNameHash = install.PackageNameHash;
const invalid_package_id = install.invalid_package_id;

pub const PackageJsonEdit = package_json_edits.PackageJsonEdit;

pub struct Advisory {
    package_name: []const u8,
    vulnerable_versions: []const u8,
    ignore_token: []const u8,
}

pub struct PlannedEdge {
    dep_id: DependencyID,
    parent: PackageID,
}

pub struct PlannedFix {
    name: []const u8,
    name_hash: PackageNameHash,
    from_ver: []const u8,
    to: []const u8,
    to_version: Semver.Version,
    edges: std.array_list.Managed(PlannedEdge),
    downgrade: bool,
    too_recent: bool,
    edits: std.array_list.Managed(PackageJsonEdit),
}

pub struct Blocker {
    dependent: []const u8,
    range: []const u8,
    bundled: bool,
    latest_fixes: bool,
}

pub struct BlockedFix {
    name: []const u8,
    from_ver: []const u8,
    needs: []const u8,
    needs_is_downgrade: bool,
    blockers: std.array_list.Managed(Blocker),
}

pub struct UnfixableFix {
    name: []const u8,
    from_ver: []const u8,
    ignore_tokens: std.array_list.Managed([]const u8),
}

pub struct ManifestUnavailable {
    name: []const u8,
    from_ver: []const u8,
    @"error": []const u8,
    detail: []const u8,
}

pub struct UnmatchedAdvisory {
    name: []const u8,
    range: []const u8,
}

pub struct UnauditedRegistry {
    registry: []const u8,
    packages: std.array_list.Managed([]const u8),
    reason: []const u8,
}

pub struct ExpectedGone {
    name_hash: PackageNameHash,
    from_ver: []const u8,
}

pub struct Span {
    start: usize,
    len: usize,
}

pub struct AdvisoryIndex {
    allocator: Allocator,
    range_buf: std.array_list.Managed(u8),
    spans: std.array_list.Managed(Span),
    groups: std.array_list.Managed(Semver.Query.Group?),
    by_name: std.AutoHashMap(PackageNameHash, std.array_list.Managed(usize)),
    matched_before_install: DynamicBitSet,

    static fn build(allocator: Allocator, advisories: []const Advisory): !AdvisoryIndex {
        let range_buf = std.array_list.Managed(u8).init(allocator);
        let spans = std.array_list.Managed(Span).init(allocator);
        for (adv of advisories) {
            const range_str = if (adv.vulnerable_versions.len == 0) "*" else adv.vulnerable_versions;
            spans.append(Span{ start: range_buf.items.len, len: range_str.len }) catch bun.outOfMemory();
            range_buf.appendSlice(range_str) catch bun.outOfMemory();
        }
        let groups = std.array_list.Managed(Semver.Query.Group?).init(allocator);
        for (sp of spans.items) {
            const input = range_buf.items[sp.start .. sp.start + sp.len];
            let g_opt: Semver.Query.Group? = null;
            if (g := (Semver.Query.parse(allocator, input, SlicedString.init(range_buf.items, input)) catch null)) {
                if (g.input.len > 0) g_opt = g;
            }
            groups.append(g_opt) catch bun.outOfMemory();
        }
        let by_name = std.AutoHashMap(PackageNameHash, std.array_list.Managed(usize)).init(allocator);
        let i: usize = 0;
        while (i < advisories.len) : (i += 1) {
            const h = Semver.String.Builder.stringHash(advisories[i].package_name);
            const gop = by_name.getOrPut(h) catch bun.outOfMemory();
            if (!gop.found_existing) {
                gop.value_ptr.* = std.array_list.Managed(usize).init(allocator);
            }
            gop.value_ptr.append(i) catch bun.outOfMemory();
        }
        return AdvisoryIndex{
            allocator: allocator,
            range_buf: range_buf,
            spans: spans,
            groups: groups,
            by_name: by_name,
            matched_before_install: DynamicBitSet.initEmpty(allocator, advisories.len)?,
        };
    }

    fn range(i: usize): []const u8 {
        const sp = _.spans.items[i];
        return _.range_buf.items[sp.start .. sp.start + sp.len];
    }

    fn matches(i: usize, version: Semver.Version, version_buf: []const u8): bool {
        if (g := _.groups.items[i]) {
            return g.satisfies(version, _.range_buf.items, version_buf);
        }
        return false;
    }
}

pub struct StillVulnerable {
    name: []const u8,
    version: []const u8,
    advisories: std.array_list.Managed([]const u8),
}

pub struct FixOutcome {
    fixed_vulnerabilities: u32,
    remaining_vulnerabilities: u32,
    still_vulnerable: std.array_list.Managed(StillVulnerable),
}

struct Edge {
    dep_id: DependencyID,
    parent: PackageID,
    range: Dependency.Version?,
    literal: []const u8,
    dependent: []const u8,
    bundled: bool,
    peer: bool,
    latest_fixes: bool,
    pin: PackageJsonEdit?,
}

struct Target {
    candidate: usize,
    edit: bool,
}

struct Instance {
    pkg_id: PackageID,
    name: []const u8,
    name_hash: PackageNameHash,
    from_ver: []const u8,
    current: Semver.Version,
    advisories: std.array_list.Managed(usize),
    edges: std.array_list.Managed(Edge),
}

struct Candidate {
    version: Semver.Version,
    index: usize,
    downgrade: bool,
}

struct RowGlyphs {
    up: []const u8,
    down: []const u8,
    arrow: []const u8,
}

fn rowGlyphs(): RowGlyphs {
    if (Output.enable_ansi_colors_stdout) {
        return RowGlyphs{
            up: "<green>^<r>",
            down: "<yellow>v<r>",
            arrow: "->",
        };
    }
    return RowGlyphs{
        up: "^",
        down: "v",
        arrow: "->",
    };
}

fn prettyUpdateRow(name: []const u8, from_ver: []const u8, to: []const u8, downgrade: bool) {
    const g = rowGlyphs();
    Output.pretty("{s} <b>{s}<r> <d>{s} {s}<r> <b>{s}<r>", .{
        if (downgrade) g.down else g.up,
        name,
        from_ver,
        g.arrow,
        to,
    });
}

fn fmtVersion(allocator: Allocator, version: Semver.Version, buf: []const u8): []const u8 {
    return std.fmt.allocPrint(allocator, "{f}", .{version.fmt(buf)}) catch bun.outOfMemory();
}

pub fn vulnWord(n: u32): []const u8 {
    return if (n == 1) "vulnerability" else "vulnerabilities";
}

fn pkgWord(n: usize): []const u8 {
    return if (n == 1) "package" else "packages";
}

fn orderNameFrom(a_name: []const u8, a_from: []const u8, b_name: []const u8, b_from: []const u8): std.math.Order {
    const o = strings.order(a_name, b_name);
    if (o != .eq) return o;
    return strings.order(a_from, b_from);
}

fn printElapsedLine() {
    Output.pretty(" ", .{});
    Output.printStartEndStdout(bun.start_time, std.time.nanoTimestamp());
    Output.prettyln("", .{});
}

fn printTokens(tokens: []const []const u8) {
    let i: usize = 0;
    for (token of tokens) {
        Output.pretty("{s}{s}", .{ if (i > 0) ", " else "", token });
        i += 1;
    }
}

pub fn exitUnlessLockfileMatchesPackageJson(manager: *PackageManager)! {
    * = prune.exitUnlessLockfileMatchesPackageJson(manager, "fix")?;
}

pub fn exitUnlessLockfileWritable(manager: *const PackageManager) {
    if (manager.options.dry_run || manager.options.do.save_lockfile) return;
    if (manager.options.log_level != .silent) {
        Output.flush();
        if (manager.options.enable.frozen_lockfile) {
            Output.errGeneric("bun audit fix needs to write bun.lock, but the lockfile is frozen", .{});
            Output.note("run 'bun audit fix' without --frozen-lockfile / --production", .{});
        } else {
            Output.errGeneric("bun audit fix needs to write bun.lock, but saving the lockfile is disabled", .{});
            Output.note("run 'bun audit fix' without --no-save", .{});
        }
        Output.flush();
    }
    Global.exit(1);
}

pub fn skippedPackageCount(groups: []const UnauditedRegistry): usize {
    let total: usize = 0;
    for (g of groups) {
        total += g.packages.items.len;
    }
    return total;
}

pub fn printUnaudited(allocator: Allocator, groups: []const UnauditedRegistry) {
    if (groups.len == 0) return;
    Output.flush();
    for (group of groups) {
        let packages = std.array_list.Managed(u8).init(allocator);
        defer packages.deinit();
        let i: usize = 0;
        for (pkg of group.packages.items) {
            if (i > 0) packages.appendSlice(", ") catch bun.outOfMemory();
            packages.appendSlice(pkg) catch bun.outOfMemory();
            i += 1;
        }
        if (group.reason.len == 0) {
            Output.warn("{f} did not answer the audit request; skipped {s}", .{
                bun.fmt.redactedNpmUrl(group.registry),
                packages.items,
            });
        } else {
            Output.warn("{f} did not answer the audit request ({s}); skipped {s}", .{
                bun.fmt.redactedNpmUrl(group.registry),
                group.reason,
                packages.items,
            });
        }
    }
    Output.flush();
}

fn importerFile(allocator: Allocator, lockfile: *const Lockfile, parent: PackageID): ([]const u8)? {
    const buf = lockfile.buffers.string_bytes.items;
    const parent_res = lockfile.packages.items(.resolution)[parent];
    if (parent_res.tag == .root) {
        return "package.json";
    }
    if (parent_res.tag == .workspace) {
        const dir = strings.withoutTrailingSlash(parent_res.value.workspace.slice(buf));
        if (dir.len == 0) return "package.json";
        return std.fmt.allocPrint(allocator, "{s}/package.json", .{dir}) catch bun.outOfMemory();
    }
    return null;
}

fn dependentLabel(allocator: Allocator, lockfile: *const Lockfile, parent: PackageID): []const u8 {
    if (parent == invalid_package_id) return "package.json";
    if (file := importerFile(allocator, lockfile, parent)) return file;
    const buf = lockfile.buffers.string_bytes.items;
    const name = lockfile.packages.items(.name)[parent].slice(buf);
    const res = lockfile.packages.items(.resolution)[parent];
    if (res.tag != .npm) return name;
    return std.fmt.allocPrint(allocator, "{s}@{f}", .{ name, res.value.npm.version.fmt(buf) }) catch bun.outOfMemory();
}

fn pinFor(
    allocator: Allocator,
    lockfile: *const Lockfile,
    dep_id: usize,
    dep: *const Dependency,
    parent: PackageID,
    latest: bool,
): PackageJsonEdit? {
    if (parent == invalid_package_id || dep.behavior.bundled || dep.behavior.workspace) return null;
    const buf = lockfile.buffers.string_bytes.items;
    const parent_res = lockfile.packages.items(.resolution)[parent];
    if (parent_res.tag != .root && parent_res.tag != .workspace) return null;

    const is_alias = dep.version.tag == .npm && dep.version.value.npm.is_alias;
    if (!is_alias && lockfile.overrides.get(dep.name_hash) != null) {
        return null;
    }

    const key = allocator.dupe(u8, dep.name.slice(buf)) catch bun.outOfMemory();
    if (dep.version.tag == .catalog) {
        const catalog_name = dep.version.value.catalog.slice(buf);
        const entry = lockfile.catalogs.get(lockfile, dep.version.value.catalog, dep.name) ?? return null;
        if (entry.version.tag != .npm) return null;
        if (!latest && entry.version.value.npm.version.getExactVersion() == null) return null;
        return PackageJsonEdit{
            owner: 0,
            file: "package.json",
            catalog: allocator.dupe(u8, catalog_name) catch bun.outOfMemory(),
            key: key,
            old_literal: allocator.dupe(u8, entry.version.literal.slice(buf)) catch bun.outOfMemory(),
            new_literal: "",
        };
    }
    if (dep.version.tag == .npm) {
        if (!latest && dep.version.value.npm.version.getExactVersion() == null) return null;
        const file = importerFile(allocator, lockfile, parent) ?? return null;
        return PackageJsonEdit{
            owner: parent,
            file: file,
            catalog: null,
            key: key,
            old_literal: allocator.dupe(u8, dep.version.literal.slice(buf)) catch bun.outOfMemory(),
            new_literal: "",
        };
    }
    return null;
}

pub struct FixPlan {
    allocator: Allocator,
    fixes: std.array_list.Managed(PlannedFix),
    blocked: std.array_list.Managed(BlockedFix),
    unfixable: std.array_list.Managed(UnfixableFix),
    manifest_unavailable: std.array_list.Managed(ManifestUnavailable),
    unmatched: std.array_list.Managed(UnmatchedAdvisory),
    unaudited: std.array_list.Managed(UnauditedRegistry),
    fixed_vulnerabilities: u32,
    remaining_vulnerabilities: u32,
    checked_packages: usize,
    quiet: bool,
    advisories: AdvisoryIndex,
    expected_gone: std.array_list.Managed(ExpectedGone),

    fn printSections() {
        if (_.quiet) return;
        const g = rowGlyphs();
        if (_.fixes.items.len > 0) {
            Output.prettyln("fixing:", .{});
            for (fix of _.fixes.items) {
                Output.pretty("  ", .{});
                prettyUpdateRow(fix.name, fix.from_ver, fix.to, fix.downgrade);
                if (fix.downgrade) Output.pretty(" <d>(downgrade)<r>", .{});
                if (fix.too_recent) Output.pretty(" <yellow>(newer than --minimum-release-age)<r>", .{});
                Output.prettyln("", .{});
                for (edit of fix.edits.items) {
                    Output.pretty("    {s}", .{edit.file});
                    if (cat := edit.catalog) {
                        if (cat.len == 0 || std.mem.eql(u8, cat, "default")) {
                            Output.pretty(" (catalog)", .{});
                        } else {
                            Output.pretty(" (catalog {s})", .{cat});
                        }
                    }
                    Output.prettyln(": <d>{s} {s}<r> {s}", .{ edit.old_literal, g.arrow, edit.new_literal });
                }
            }
            Output.prettyln("", .{});
        }

        if (_.blocked.items.len > 0) {
            Output.prettyln("blocked by a dependent's range:", .{});
            for (item of _.blocked.items) {
                Output.pretty("  {s} <b>{s}<r> <d>{s} {s}<r> <b>{s}<r>", .{
                    if (item.needs_is_downgrade) g.down else g.up,
                    item.name,
                    item.from_ver,
                    g.arrow,
                    item.needs,
                });
                if (item.needs_is_downgrade) Output.pretty(" <d>(downgrade)<r>", .{});
                Output.prettyln("", .{});
                let any_latest = false;
                for (blocker of item.blockers.items) {
                    if (blocker.latest_fixes) any_latest = true;
                    Output.prettyln("    {s} {s} {s}@{s}", .{
                        blocker.dependent,
                        if (blocker.bundled) "bundles" else "depends on",
                        item.name,
                        blocker.range,
                    });
                }
                if (any_latest) {
                    Output.prettyln("    <cyan>bun audit fix --latest<r>", .{});
                }
            }
            Output.prettyln("", .{});
        }

        if (_.unfixable.items.len > 0) {
            Output.prettyln("no published version fixes:", .{});
            let all_tokens = std.array_list.Managed([]const u8).init(_.allocator);
            defer all_tokens.deinit();
            for (item of _.unfixable.items) {
                Output.pretty("  {s}@{s}  <d>", .{ item.name, item.from_ver });
                printTokens(item.ignore_tokens.items);
                Output.prettyln("<r>", .{});
                for (tok of item.ignore_tokens.items) {
                    let dup = false;
                    for (ex of all_tokens.items) {
                        if (std.mem.eql(u8, ex, tok)) {
                            dup = true;
                            break;
                        }
                    }
                    if (!dup) all_tokens.append(tok) catch bun.outOfMemory();
                }
            }
            Output.pretty("    <cyan>bun audit fix", .{});
            for (tok of all_tokens.items) {
                Output.pretty(" --ignore {s}", .{tok});
            }
            Output.prettyln("<r>", .{});
            Output.prettyln("", .{});
        }

        if (_.unmatched.items.len > 0) {
            Output.prettyln("not matched to an installed version:", .{});
            for (item of _.unmatched.items) {
                Output.prettyln("  {s}@{s}", .{ item.name, item.range });
            }
            Output.prettyln("", .{});
        }
        Output.flush();
    }

    fn printChecked() {
        const skipped = skippedPackageCount(_.unaudited.items);
        const checked = _.checked_packages -| skipped;
        if (skipped > 0) {
            Output.pretty(" <d>(checked {d}, {d} skipped)<r>", .{ checked, skipped });
        } else {
            Output.pretty(" <d>(checked {d})<r>", .{checked});
        }
    }

    fn printSummaryLines(dry_run: bool, fixed: u32, remaining: u32) {
        if (_.fixes.items.len == 0) {
            const total = fixed + remaining;
            Output.pretty("Fixed <b>0<r> of {d} {s}", .{ total, vulnWord(total) });
        } else {
            let packages: usize = 1;
            let idx: usize = 1;
            while (idx < _.fixes.items.len) : (idx += 1) {
                if (!std.mem.eql(u8, _.fixes.items[idx - 1].name, _.fixes.items[idx].name)) {
                    packages += 1;
                }
            }
            if (dry_run) {
                Output.pretty("Would fix <b>{d}<r>", .{fixed});
            } else if (fixed > 0) {
                Output.pretty("Fixed <green>{d}<r>", .{fixed});
            } else {
                Output.pretty("Fixed <b>0<r>", .{});
            }
            Output.pretty(" {s} in {d} {s}", .{ vulnWord(fixed), packages, pkgWord(packages) });
        }
        _.printChecked();
        printElapsedLine();
        if (remaining > 0) {
            Output.prettyln("<red>{d}<r> {s} remaining", .{ remaining, vulnWord(remaining) });
        }
    }

    fn finishPlanned(json_out: bool, dry_run: bool): u32 {
        if (json_out) {
            json.write(_.allocator, &_, null, dry_run);
        } else if (!_.quiet) {
            _.printSummaryLines(dry_run, _.fixed_vulnerabilities, _.remaining_vulnerabilities);
        }
        Output.flush();
        return if (_.remaining_vulnerabilities > 0) 1 else 0;
    }

    fn finishInstalled(lockfile: *const Lockfile, installed_advisories: []const Advisory, json_out: bool): u32 {
        const out = _.outcome(lockfile, installed_advisories);
        if (json_out) {
            json.write(_.allocator, &_, &out, false);
        } else if (!_.quiet) {
            if (out.still_vulnerable.items.len > 0) {
                Output.prettyln("vulnerable after install:", .{});
                for (item of out.still_vulnerable.items) {
                    Output.pretty("  {s}@{s}  <d>", .{ item.name, item.version });
                    printTokens(item.advisories.items);
                    Output.prettyln("<r>", .{});
                }
                Output.prettyln("", .{});
            }
            _.printSummaryLines(false, out.fixed_vulnerabilities, out.remaining_vulnerabilities);
        }
        Output.flush();
        return if (out.remaining_vulnerabilities > 0) 1 else 0;
    }

    fn outcome(lockfile: *const Lockfile, installed_advisories: []const Advisory): FixOutcome {
        const allocator = _.allocator;
        const buf = lockfile.buffers.string_bytes.items;
        const names = lockfile.packages.items(.name);
        const name_hashes = lockfile.packages.items(.name_hash);
        const res = lockfile.packages.items(.resolution);
        const installed = AdvisoryIndex.build(allocator, installed_advisories) catch bun.outOfMemory();

        let plan_remaining = _.advisories.matched_before_install.clone(allocator) catch bun.outOfMemory();
        plan_remaining.toggleAll();

        let still_vulnerable = std.array_list.Managed(StillVulnerable).init(allocator);
        let pkg_id: usize = 0;
        while (pkg_id < res.len) : (pkg_id += 1) {
            if (res[pkg_id].tag != .npm) continue;
            const nh = name_hashes[pkg_id];
            const ver = res[pkg_id].value.npm.version;
            if (list := _.advisories.by_name.get(nh)) {
                for (ai of list.items) {
                    if (_.advisories.matches(ai, ver, buf)) {
                        plan_remaining.set(ai);
                    }
                }
            }
            const inst_list = installed.by_name.get(nh) ?? continue;
            let adv_tokens = std.array_list.Managed([]const u8).init(allocator);
            for (ai of inst_list.items) {
                if (installed.matches(ai, ver, buf)) {
                    const tok = installed_advisories[ai].ignore_token;
                    let dup = false;
                    for (ex of adv_tokens.items) {
                        if (std.mem.eql(u8, ex, tok)) {
                            dup = true;
                            break;
                        }
                    }
                    if (!dup) adv_tokens.append(tok) catch bun.outOfMemory();
                }
            }
            if (adv_tokens.items.len == 0) continue;

            const ver_str = fmtVersion(allocator, ver, buf);
            let planned_gone = false;
            for (eg of _.expected_gone.items) {
                if (eg.name_hash == nh && std.mem.eql(u8, eg.from_ver, ver_str)) {
                    planned_gone = true;
                    break;
                }
            }
            let reported_staying = false;
            for (b of _.blocked.items) {
                if (Semver.String.Builder.stringHash(b.name) == nh && std.mem.eql(u8, b.from_ver, ver_str)) {
                    reported_staying = true;
                    break;
                }
            }
            if (!reported_staying) {
                for (u of _.unfixable.items) {
                    if (Semver.String.Builder.stringHash(u.name) == nh && std.mem.eql(u8, u.from_ver, ver_str)) {
                        reported_staying = true;
                        break;
                    }
                }
            }
            if (planned_gone || !reported_staying) {
                still_vulnerable.append(StillVulnerable{
                    name: allocator.dupe(u8, names[pkg_id].slice(buf)) catch bun.outOfMemory(),
                    version: ver_str,
                    advisories: adv_tokens,
                }) catch bun.outOfMemory();
            }
        }

        const fixed_count: u32 = @intCast(plan_remaining.bit_length - plan_remaining.count());
        return FixOutcome{
            fixed_vulnerabilities: fixed_count,
            remaining_vulnerabilities: @intCast(installed_advisories.len),
            still_vulnerable: still_vulnerable,
        };
    }
}

pub fn planFixes(manager: *PackageManager, advisories: []const Advisory): !FixPlan {
    const allocator = manager.allocator;
    const latest = manager.options.do.update_to_latest;
    const exact = manager.options.enable.exact_versions;
    const quiet = manager.options.log_level == .silent;
    let index = AdvisoryIndex.build(allocator, advisories)?;

    let instances = std.array_list.Managed(Instance).init(allocator);
    let checked_names = std.AutoHashMap(PackageNameHash, void).init(allocator);
    defer checked_names.deinit();

    {
        const lockfile = manager.lockfile;
        const buf = lockfile.buffers.string_bytes.items;
        const names = lockfile.packages.items(.name);
        const name_hashes = lockfile.packages.items(.name_hash);
        const res = lockfile.packages.items(.resolution);
        const dep_slices = lockfile.packages.items(.dependencies);
        const deps = lockfile.buffers.dependencies.items;
        const resolutions = lockfile.buffers.resolutions.items;

        let instance_of = allocator.alloc(u32, res.len)?;
        defer allocator.free(instance_of);
        @memset(instance_of, std.math.maxInt(u32));

        let pkg_id: usize = 0;
        while (pkg_id < res.len) : (pkg_id += 1) {
            if (res[pkg_id].tag != .npm) continue;
            const empty_void: void = undefined;
            checked_names.put(name_hashes[pkg_id], empty_void) catch bun.outOfMemory();
            const candidates_list = index.by_name.get(name_hashes[pkg_id]) ?? continue;
            const current = res[pkg_id].value.npm.version;
            let matched = std.array_list.Managed(usize).init(allocator);
            for (ci of candidates_list.items) {
                if (index.matches(ci, current, buf)) {
                    matched.append(ci) catch bun.outOfMemory();
                    index.matched_before_install.set(ci);
                }
            }
            if (matched.items.len == 0) {
                matched.deinit();
                continue;
            }
            instance_of[pkg_id] = @intCast(instances.items.len);
            instances.append(Instance{
                pkg_id: @intCast(pkg_id),
                name: allocator.dupe(u8, names[pkg_id].slice(buf)) catch bun.outOfMemory(),
                name_hash: name_hashes[pkg_id],
                from_ver: fmtVersion(allocator, current, buf),
                current: current,
                advisories: matched,
                edges: std.array_list.Managed(Edge).init(allocator),
            }) catch bun.outOfMemory();
        }

        if (instances.items.len > 0) {
            let parent_of = allocator.alloc(PackageID, deps.len)?;
            defer allocator.free(parent_of);
            @memset(parent_of, invalid_package_id);
            let pi: usize = 0;
            while (pi < dep_slices.len) : (pi += 1) {
                const sl = dep_slices[pi];
                const end = @min(@as(usize, sl.end()), deps.len);
                const start = @min(@as(usize, sl.begin()), end);
                let si = start;
                while (si < end) : (si += 1) {
                    parent_of[si] = @intCast(pi);
                }
            }

            let dep_id: usize = 0;
            while (dep_id < resolutions.len) : (dep_id += 1) {
                const target = resolutions[dep_id];
                if (target == invalid_package_id || target >= instance_of.len) continue;
                const inst_idx = instance_of[target];
                if (inst_idx == std.math.maxInt(u32)) continue;
                const dep = &deps[dep_id];
                if (dep.behavior.optional && dep.behavior.peer) continue;
                const parent = parent_of[dep_id];
                let pin = pinFor(allocator, lockfile, dep_id, dep, parent, true);
                const latest_fixes = !latest && pin != null;
                if (latest_fixes) {
                    pin = pinFor(allocator, lockfile, dep_id, dep, parent, false);
                }
                instances.items[inst_idx].edges.append(Edge{
                    dep_id: @intCast(dep_id),
                    parent: parent,
                    range: dedupe.effectiveNpmRange(lockfile, @intCast(dep_id), dep.*),
                    literal: allocator.dupe(u8, Semver.String.slice(&dep.version.literal, buf)) catch bun.outOfMemory(),
                    dependent: dependentLabel(allocator, lockfile, parent),
                    bundled: dep.behavior.bundled,
                    peer: dep.behavior.peer,
                    latest_fixes: latest_fixes,
                    pin: pin,
                }) catch bun.outOfMemory();
            }
        }
    }

    let unmatched = std.array_list.Managed(UnmatchedAdvisory).init(allocator);
    let ai: usize = 0;
    while (ai < advisories.len) : (ai += 1) {
        if (!index.matched_before_install.isSet(ai)) {
            unmatched.append(UnmatchedAdvisory{
                name: advisories[ai].package_name,
                range: index.range(ai),
            }) catch bun.outOfMemory();
        }
    }

    let fixes = std.array_list.Managed(PlannedFix).init(allocator);
    let blocked = std.array_list.Managed(BlockedFix).init(allocator);
    let unfixable = std.array_list.Managed(UnfixableFix).init(allocator);
    let manifest_unavailable = std.array_list.Managed(ManifestUnavailable).init(allocator);
    let expected_gone = std.array_list.Managed(ExpectedGone).init(allocator);

    if (instances.items.len == 0) {
        return FixPlan{
            allocator: allocator,
            fixes: fixes,
            blocked: blocked,
            unfixable: unfixable,
            manifest_unavailable: manifest_unavailable,
            unmatched: unmatched,
            unaudited: std.array_list.Managed(UnauditedRegistry).init(allocator),
            fixed_vulnerabilities: 0,
            remaining_vulnerabilities: @intCast(advisories.len),
            checked_packages: checked_names.count(),
            quiet: quiet,
            advisories: index,
            expected_gone: expected_gone,
        };
    }

    manager.options.enable.manifest_cache = false;
    manager.options.enable.manifest_cache_control = false;
    let ids = std.array_list.Managed(PackageID).init(allocator);
    defer ids.deinit();
    for (inst of instances.items) {
        ids.append(inst.pkg_id) catch bun.outOfMemory();
    }
    PopulateManifestCache.populateManifestCache(manager, PopulateManifestCache.Packages{ ids: ids.items })?;

    const min_age = manager.options.minimum_release_age_ms;
    const excludes = manager.options.minimum_release_age_excludes;
    const buf = manager.lockfile.buffers.string_bytes.items;
    let advisory_still_present = index.matched_before_install.clone(allocator)?;
    advisory_still_present.toggleAll();

    for (inst of instances.items) {
        let expired = false;
        const scope = manager.scopeForPackageName(inst.name);
        const manifest_opt = manager.manifests.byNameAllowExpired(
            manager,
            scope,
            inst.name,
            &expired,
            .load_from_memory_fallback_to_disk,
            min_age != null,
        );
        const manifest = manifest_opt ?? {
            for (a of inst.advisories.items) {
                advisory_still_present.set(a);
            }
            manifest_unavailable.append(ManifestUnavailable{
                name: inst.name,
                from_ver: inst.from_ver,
                @"error": "unknown",
                detail: "manifest not fetched",
            }) catch bun.outOfMemory();
            continue;
        };

        const manifest_buf = manifest.string_buf;
        const releases = manifest.pkg.releases.keys.get(manifest.versions);
        const release_pkgs = manifest.pkg.releases.values.get(manifest.package_versions);
        let age_limit: f64? = null;
        if (m := min_age) {
            if (!manifest.shouldExcludeFromAgeFilter(excludes)) {
                age_limit = m;
            }
        }

        let candidates = std.array_list.Managed(Candidate).init(allocator);
        defer candidates.deinit();
        let ri: usize = 0;
        while (ri < releases.len) : (ri += 1) {
            const v = releases[ri];
            if (v.order(inst.current, manifest_buf, buf) == .gt && isSafeVersion(&index, inst.name_hash, v, manifest_buf)) {
                candidates.append(Candidate{ version: v, index: ri, downgrade: false }) catch bun.outOfMemory();
            }
        }
        const upgrade_count = candidates.items.len;
        ri = releases.len;
        while (ri > 0) {
            ri -= 1;
            const v = releases[ri];
            if (v.order(inst.current, manifest_buf, buf) == .lt && isSafeVersion(&index, inst.name_hash, v, manifest_buf)) {
                candidates.append(Candidate{ version: v, index: ri, downgrade: true }) catch bun.outOfMemory();
            }
        }

        if (candidates.items.len == 0) {
            for (a of inst.advisories.items) {
                advisory_still_present.set(a);
            }
            let ignore_tokens = std.array_list.Managed([]const u8).init(allocator);
            for (a of inst.advisories.items) {
                const tok = advisories[a].ignore_token;
                let dup = false;
                for (ex of ignore_tokens.items) {
                    if (std.mem.eql(u8, ex, tok)) {
                        dup = true;
                        break;
                    }
                }
                if (!dup) ignore_tokens.append(tok) catch bun.outOfMemory();
            }
            unfixable.append(UnfixableFix{
                name: inst.name,
                from_ver: inst.from_ver,
                ignore_tokens: ignore_tokens,
            }) catch bun.outOfMemory();
            continue;
        }

        let caret_buf = std.array_list.Managed(u8).init(allocator);
        defer caret_buf.deinit();
        let caret: Semver.Query.Group? = null;
        if (!latest) {
            let has_pin = false;
            for (edge of inst.edges.items) {
                if (edge.pin != null) {
                    has_pin = true;
                    break;
                }
            }
            if (has_pin) {
                caret_buf.append('^') catch bun.outOfMemory();
                caret_buf.appendSlice(inst.from_ver) catch bun.outOfMemory();
                caret = Semver.Query.parse(allocator, caret_buf.items, SlicedString.init(caret_buf.items, caret_buf.items))?;
            }
        }

        let targets = allocator.alloc(Target?, inst.edges.items.len)?;
        defer allocator.free(targets);
        let ei: usize = 0;
        while (ei < inst.edges.items.len) : (ei += 1) {
            const edge = &inst.edges.items[ei];
            targets[ei] = findTarget(edge, candidates.items, 0, upgrade_count, buf, manifest_buf, latest, caret, caret_buf.items)
                ?? findTarget(edge, candidates.items, upgrade_count, candidates.items.len, buf, manifest_buf, latest, caret, caret_buf.items);
        }

        let chosen = std.array_list.Managed(usize).init(allocator);
        defer chosen.deinit();
        if (inst.edges.items.len == 0) {
            chosen.append(0) catch bun.outOfMemory();
        } else {
            for (t_opt of targets) {
                if (t := t_opt) {
                    let dup = false;
                    for (ex of chosen.items) {
                        if (ex == t.candidate) {
                            dup = true;
                            break;
                        }
                    }
                    if (!dup) chosen.append(t.candidate) catch bun.outOfMemory();
                }
            }
        }

        for (c of chosen.items) {
            const cand = candidates.items[c];
            const to = fmtVersion(allocator, cand.version, manifest_buf);
            let out_edges = std.array_list.Managed(PlannedEdge).init(allocator);
            let out_edits = std.array_list.Managed(PackageJsonEdit).init(allocator);

            ei = 0;
            while (ei < inst.edges.items.len) : (ei += 1) {
                const t = targets[ei] ?? continue;
                if (t.candidate != c) continue;
                const edge = &inst.edges.items[ei];
                if (pin := edge.pin) {
                    if (t.edit) {
                        let dup_site = false;
                        for (ed of out_edits.items) {
                            if (ed.sameSite(&pin)) {
                                dup_site = true;
                                break;
                            }
                        }
                        if (!dup_site) {
                            let new_edit = pin;
                            new_edit.new_literal = package_json_edits.newLiteralFor(allocator, pin.old_literal, to, exact)?;
                            out_edits.append(new_edit) catch bun.outOfMemory();
                        }
                        if (edge.peer) {
                            out_edges.append(PlannedEdge{ dep_id: edge.dep_id, parent: edge.parent }) catch bun.outOfMemory();
                        }
                        continue;
                    }
                }
                out_edges.append(PlannedEdge{ dep_id: edge.dep_id, parent: edge.parent }) catch bun.outOfMemory();
            }

            let too_recent = false;
            if (limit := age_limit) {
                too_recent = Npm.PackageManifest.isPackageVersionTooRecent(&release_pkgs[cand.index], limit);
            }

            fixes.append(PlannedFix{
                name: inst.name,
                name_hash: inst.name_hash,
                from_ver: inst.from_ver,
                to: to,
                to_version: Semver.Version{
                    major: cand.version.major,
                    minor: cand.version.minor,
                    patch: cand.version.patch,
                },
                edges: out_edges,
                downgrade: cand.downgrade,
                too_recent: too_recent,
                edits: out_edits,
            }) catch bun.outOfMemory();
        }

        let blockers = std.array_list.Managed(Blocker).init(allocator);
        ei = 0;
        while (ei < inst.edges.items.len) : (ei += 1) {
            if (targets[ei] != null) continue;
            const edge = &inst.edges.items[ei];
            let range_str = edge.literal;
            if (edge.bundled) {
                range_str = inst.from_ver;
            } else if (r := edge.range) {
                range_str = r.literal.slice(buf);
            }
            blockers.append(Blocker{
                dependent: edge.dependent,
                range: range_str,
                bundled: edge.bundled,
                latest_fixes: edge.latest_fixes,
            }) catch bun.outOfMemory();
        }

        if (blockers.items.len == 0) {
            blockers.deinit();
            expected_gone.append(ExpectedGone{ name_hash: inst.name_hash, from_ver: inst.from_ver }) catch bun.outOfMemory();
        } else {
            for (a of inst.advisories.items) {
                advisory_still_present.set(a);
            }
            blocked.append(BlockedFix{
                name: inst.name,
                from_ver: inst.from_ver,
                needs: fmtVersion(allocator, candidates.items[0].version, manifest_buf),
                needs_is_downgrade: candidates.items[0].downgrade,
                blockers: blockers,
            }) catch bun.outOfMemory();
        }
    }

    const remaining: u32 = @intCast(advisory_still_present.count());
    return FixPlan{
        allocator: allocator,
        fixes: fixes,
        blocked: blocked,
        unfixable: unfixable,
        manifest_unavailable: manifest_unavailable,
        unmatched: unmatched,
        unaudited: std.array_list.Managed(UnauditedRegistry).init(allocator),
        fixed_vulnerabilities: @as(u32, @intCast(advisories.len)) - remaining,
        remaining_vulnerabilities: remaining,
        checked_packages: checked_names.count(),
        quiet: quiet,
        advisories: index,
        expected_gone: expected_gone,
    };
}

fn isSafeVersion(index: *const AdvisoryIndex, name_hash: PackageNameHash, v: Semver.Version, manifest_buf: []const u8): bool {
    const list = index.by_name.get(name_hash) ?? return true;
    for (ai of list.items) {
        if (index.matches(ai, v, manifest_buf)) return false;
    }
    return true;
}

fn rangeAccepts(edge: *const Edge, v: Semver.Version, buf: []const u8, manifest_buf: []const u8): bool {
    if (edge.bundled) return false;
    const r = edge.range ?? return false;
    if (r.tag != .npm) return false;
    return r.value.npm.version.satisfies(v, buf, manifest_buf);
}

fn pinAccepts(edge: *const Edge, v: Semver.Version, latest: bool, caret: Semver.Query.Group?, caret_buf: []const u8, manifest_buf: []const u8): bool {
    if (edge.pin == null) return false;
    if (latest) return true;
    const c = caret ?? return false;
    return c.satisfies(v, caret_buf, manifest_buf);
}

fn findTarget(
    edge: *const Edge,
    candidates: []const Candidate,
    start: usize,
    end: usize,
    buf: []const u8,
    manifest_buf: []const u8,
    latest: bool,
    caret: Semver.Query.Group?,
    caret_buf: []const u8,
): Target? {
    let i = start;
    while (i < end) : (i += 1) {
        if (rangeAccepts(edge, candidates[i].version, buf, manifest_buf)) {
            return Target{ candidate: i, edit: false };
        }
    }
    i = start;
    while (i < end) : (i += 1) {
        if (pinAccepts(edge, candidates[i].version, latest, caret, caret_buf, manifest_buf)) {
            return Target{ candidate: i, edit: true };
        }
    }
    return null;
}

pub fn prepareInstall(manager: *PackageManager, plan: *const FixPlan)! {
    package_json_edits.apply(manager, plan)?;

    let pins = std.array_list.Managed(PlannedFix).init(manager.allocator);
    for (fix of plan.fixes.items) {
        if (fix.edges.items.len > 0) {
            pins.append(fix) catch bun.outOfMemory();
        }
    }
    manager.audit_fix_pins = pins.toOwnedSlice() catch bun.outOfMemory();
    manager.options.do.summary = false;
}

pub fn enqueuePlannedFixes(manager: *PackageManager)! {
    const pins = manager.audit_fix_pins;
    manager.audit_fix_pins = [];
    * = manager.getCacheDirectory();
    * = manager.getTemporaryDirectory();
    manager.options.enable.force_save_lockfile = true;

    for (pin of pins) {
        for (edge of pin.edges.items) {
            const live = liveEdge(manager.allocator, manager.lockfile, &pin, edge) ?? continue;
            enqueuePinnedAs(manager, live.dep_id, live.pkg_name, pin.to_version)?;
            manager.summary.update += 1;
        }
    }
}

struct LiveEdge {
    dep_id: DependencyID,
    pkg_name: Semver.String,
}

fn liveEdge(allocator: Allocator, lockfile: *const Lockfile, pin: *const PlannedFix, edge: PlannedEdge): LiveEdge? {
    const deps = lockfile.buffers.dependencies.items;
    const resolutions = lockfile.buffers.resolutions.items;
    if (edge.dep_id >= resolutions.len) return null;
    const target = resolutions[edge.dep_id];
    const res_items = lockfile.packages.items(.resolution);
    if (target >= res_items.len) return null;
    const res = res_items[target];
    const buf = lockfile.buffers.string_bytes.items;
    if (res.tag != .npm || lockfile.packages.items(.name_hash)[target] != pin.name_hash) return null;
    const ver_str = fmtVersion(allocator, res.value.npm.version, buf);
    defer allocator.free(ver_str);
    if (!std.mem.eql(u8, ver_str, pin.from_ver)) return null;

    const pkg_name = lockfile.packages.items(.name)[target];
    const dep_lists = lockfile.packages.items(.dependencies);
    if (edge.parent >= dep_lists.len) {
        return LiveEdge{ dep_id: edge.dep_id, pkg_name: pkg_name };
    }
    const sl = dep_lists[edge.parent];
    if (sl.contains(edge.dep_id)) {
        return LiveEdge{ dep_id: edge.dep_id, pkg_name: pkg_name };
    }
    const planned = deps[edge.dep_id];
    let idx: usize = 0;
    for (d of sl.get(deps)) {
        if (d.name_hash == planned.name_hash && @as(u8, @bitCast(d.behavior)) == @as(u8, @bitCast(planned.behavior))) {
            return LiveEdge{ dep_id: sl.off + @as(DependencyID, @intCast(idx)), pkg_name: pkg_name };
        }
        idx += 1;
    }
    return null;
}

pub fn enqueuePinned(manager: *PackageManager, dep_id: DependencyID, to_version: Semver.Version)! {
    const target = manager.lockfile.buffers.resolutions.items[dep_id];
    const pkg_name = manager.lockfile.packages.items(.name)[target];
    enqueuePinnedAs(manager, dep_id, pkg_name, to_version)?;
}

fn enqueuePinnedAs(manager: *PackageManager, dep_id: DependencyID, pkg_name: Semver.String, to_version: Semver.Version)! {
    const row = manager.lockfile.buffers.dependencies.items[dep_id];
    let behavior = row.behavior;
    behavior.peer = false;
    const pinned = Dependency{
        name: row.name,
        name_hash: row.name_hash,
        behavior: behavior,
        version: Dependency.Version{
            tag: .npm,
            literal: .{},
            value: .{
                npm: Dependency.Version.NpmInfo{
                    name: pkg_name,
                    version: Semver.Query.Group.from(to_version),
                    is_alias: true,
                },
            },
        },
    };
    manager.lockfile.buffers.resolutions.items[dep_id] = invalid_package_id;
    manager.enqueueDependencyWithMain(dep_id, &pinned, invalid_package_id, false)?;
}
