import bun from "bun";
import install from "../install/install.zig";
import Lockfile from "../install/lockfile.zig";
import ResolutionMod from "../install/resolution.zig";
import PackageManager from "../install/PackageManager.zig";
import workspace_selection from "../install/PackageManager/workspace_selection.zig";
import pm_cmd from "./package_manager_command.zig";
import StoreMod from "../install/isolated_install/Store.zig";

const Allocator = std.mem.Allocator;
const nm_dir_str: []const u8 = "node_modules";
const dot_bun_dir_str: []const u8 = ".bun";
const pkg_json_file_str: []const u8 = "package.json";
const DynamicBitSet = bun.bit_set.DynamicBitSet;
const Global = bun.Global;
const Output = bun.Output;
const JSON = bun.json;
const logger = bun.logger;
const strings = bun.strings;
const Semver = bun.Semver;
const index_sort = bun.collections.index_sort;
const reachable_mod = Lockfile.reachable;
const PackageManagerCommand = pm_cmd.PackageManagerCommand;

const PackageID = install.PackageID;
const Resolution = ResolutionMod.Resolution;
const Expr = bun.ast.Expr;

const UNKNOWN_LICENSE: []const u8 = "Unknown";
const MAX_SCAN_DEPTH: usize = 64;

const PackageInfo = struct {
    allocator: Allocator,
    name: ([]u8)? = null,
    version: ([]u8)? = null,
    license: []u8,
    homepage: ([]u8)? = null,
    author: ([]u8)? = null,
    description: ([]u8)? = null,
    path: []u8,

    fn deinit(*_) {
        if (n := _.name) _.allocator.free(n);
        if (v := _.version) _.allocator.free(v);
        _.allocator.free(_.license);
        if (h := _.homepage) _.allocator.free(h);
        if (a := _.author) _.allocator.free(a);
        if (d := _.description) _.allocator.free(d);
        _.allocator.free(_.path);
    }
};

const Entry = struct {
    allocator: Allocator,
    license: []u8,
    name: []u8,
    version: []u8,
    path: []u8,
    semver: Semver.Version? = null,
    homepage: ([]u8)? = null,
    author: ([]u8)? = null,
    description: ([]u8)? = null,
    dev_only: bool = false,

    fn deinit(*_) {
        _.allocator.free(_.license);
        _.allocator.free(_.name);
        _.allocator.free(_.version);
        _.allocator.free(_.path);
        if (h := _.homepage) _.allocator.free(h);
        if (a := _.author) _.allocator.free(a);
        if (d := _.description) _.allocator.free(d);
    }
};

const DirEntryItem = struct {
    name: []u8,
    kind: std.fs.File.Kind,
};

pub const LicensesFlags = struct {
    dev_only: bool = false,
    long: bool = false,
};

fn isIsolatedLinker(load_result: Lockfile.LoadResult, configured: install.PackageManager.Options.NodeLinker): bool {
    if (configured == .isolated) return true;
    if (configured == .hoisted) return false;
    let bun_nm_path: bun.AutoAbsPath = .initTopLevelDir();
    defer bun_nm_path.deinit();
    bun_nm_path.appendJoin(nm_dir_str);
    bun_nm_path.appendJoin(dot_bun_dir_str);
    switch (bun.sys.openatA(bun.FD.cwd(), bun_nm_path.sliceZ(), bun.O.RDONLY | bun.O.DIRECTORY, 0)) {
        .result fd => {
            fd.close();
            return true;
        },
        .err => {},
    }
    switch (load_result) {
        .ok ok => {
            return ok.lockfile.saved_config_version != null && ok.lockfile.saved_config_version.? == .v1;
        },
        else => return false,
    }
}

pub struct PmLicensesCommand {
    pub static fn exec(
        pm: *PackageManager,
        positionals: []const []const u8,
        original_cwd: []const u8,
        flags: LicensesFlags,
    )! {
        const allocator = pm.allocator;
        const log_level = pm.options.log_level;
        const not_silent = log_level != .silent;

        if (positionals.len > 1) {
            const subcommand = positionals[1];
            if (!strings.eqlComptime(subcommand, "list") && !strings.eqlComptime(subcommand, "ls")) {
                if (not_silent) {
                    Output.errGeneric("unknown subcommand \"{s}\" for bun pm licenses", .{subcommand});
                    Output.note("did you mean 'bun pm licenses list'?", .{});
                }
                Global.exit(1);
            }
            if (positionals.len > 2) {
                if (not_silent) {
                    Output.errGeneric("bun pm licenses {s} does not take arguments", .{subcommand});
                }
                Global.exit(1);
            }
        }

        const configured_linker = pm.options.node_linker;
        const load_result = pm.lockfile.loadFromCwd(pm, allocator, pm.log, true);
        PackageManagerCommand.handleLoadLockfileErrors(load_result, pm);
        const isolated = isIsolatedLinker(load_result, configured_linker);

        const json_output = pm.options.json_output;
        const features = pm.options.local_package_features;
        const lockfile = pm.lockfile;

        if (flags.dev_only && !features.dev_dependencies) {
            if (not_silent) {
                Output.errGeneric("--dev cannot be combined with --prod or --omit=dev", .{});
            }
            Global.exit(1);
        }

        let path: bun.AutoAbsPath = .initTopLevelDir();
        defer path.deinit();
        const top_len = path.len();
        path.appendJoin(nm_dir_str);
        if (!bun.sys.exists(path.slice())) {
            if (not_silent) {
                Output.errGeneric("node_modules not found, nothing to list", .{});
                Output.note("run 'bun install' first", .{});
            }
            Global.exit(1);
        }
        path.setLength(top_len);

        const filter_patterns = pm.options.filter_patterns;
        let roots = std.array_list.Managed(PackageID).init(allocator);
        defer roots.deinit();
        let follow_workspace_edges = true;

        if (filter_patterns.len == 0) {
            roots.append(pm.root_package_id.get(lockfile, pm.workspace_name_hash))?;
        } else {
            let selection = workspace_selection.selectLockfileWorkspaces(
                allocator,
                lockfile,
                filter_patterns,
                original_cwd,
                .implicit,
            )?;
            defer selection.unmatched_patterns.deinit();
            if (selection.ids.items.len == 0) {
                selection.ids.deinit();
                if (not_silent) {
                    const msg = workspace_selection.unmatchedMessage(allocator, filter_patterns)?;
                    defer allocator.free(msg);
                    Output.errGeneric("{s}", .{msg});
                }
                Global.exit(1);
            }
            if (not_silent) {
                workspace_selection.warnUnmatched(allocator, filter_patterns, selection.unmatched_patterns.items);
            }
            roots.appendSlice(selection.ids.items)?;
            selection.ids.deinit();
            follow_workspace_edges = false;
        }

        const options = reachable_mod.Options{
            root: 0,
            dev: features.dev_dependencies,
            optional: features.optional_dependencies,
            peer: features.peer_dependencies,
            optional_peer: features.peer_dependencies,
            bundled: true,
            platform: reachable_mod.Platform{ cpu: pm.options.cpu, os: pm.options.os },
        };
        const resolutions = lockfile.buffers.resolutions.items;
        let wanted = if (flags.dev_only)
            reachable_mod.devPackagesFrom(allocator, lockfile, resolutions, roots.items, follow_workspace_edges, options)?
        else
            reachable_mod.packagesFrom(allocator, lockfile, resolutions, roots.items, follow_workspace_edges, options)?;
        defer wanted.deinit();

        let production: DynamicBitSet? = null;
        defer if (*p := production) p.deinit();
        if (!json_output && features.dev_dependencies) {
            let prod_opts = options;
            prod_opts.dev = false;
            production = reachable_mod.packagesFrom(allocator, lockfile, resolutions, roots.items, follow_workspace_edges, prod_opts)?;
        }

        const locations = treeLocations(allocator, lockfile)?;
        defer {
            for (loc_opt of locations) {
                if (loc := loc_opt) allocator.free(loc);
            }
            allocator.free(locations);
        }

        const packages = lockfile.packages.slice();
        const pkg_names = packages.items(.name);
        const pkg_resolution = packages.items(.resolution);
        const buf = lockfile.buffers.string_bytes.items;

        let log = logger.Log.init(allocator);
        defer log.deinit();

        let store_entries: std.array_list.Managed([]u8)? = null;
        defer {
            if (*se := store_entries) {
                for (s of se.items) allocator.free(s);
                se.deinit();
            }
        }

        let disk_map: std.StringHashMap(PackageInfo)? = null;
        defer {
            if (*dm := disk_map) {
                let it = dm.iterator();
                while (kv := it.next()) {
                    allocator.free(kv.key_ptr.*);
                    kv.value_ptr.deinit();
                }
                dm.deinit();
            }
        }

        let entries = std.array_list.Managed(Entry).init(allocator);
        defer {
            for (*e of entries.items) e.deinit();
            entries.deinit();
        }

        let missing: usize = 0;
        let checked: usize = 0;

        let pkg_id: usize = 0;
        while (pkg_id < packages.len) : (pkg_id += 1) {
            if (!wanted.isSet(pkg_id)) continue;
            const resolution = pkg_resolution[pkg_id];
            if (resolution.tag == .root || resolution.tag == .workspace || resolution.tag == .symlink) {
                continue;
            }
            checked += 1;

            const version = std.fmt.allocPrint(allocator, "{f}", .{resolution.fmt(buf, .posix)})?;
            errdefer allocator.free(version);
            const is_npm = resolution.tag == .npm;

            let info_opt: PackageInfo? = null;
            if (isolated) {
                info_opt = readStoreInfo(allocator, *path, top_len, pkg_names[pkg_id], *resolution, buf, *log, *store_entries)?;
            }

            if (info_opt == null) {
                if (loc := locations[pkg_id]) {
                    const segs: [1][]const u8 = [loc];
                    info_opt = readPackageInfoAt(allocator, *path, top_len, &segs, *log)?;
                }
                if (is_npm) {
                    if (*info := info_opt) {
                        if (inst_ver := info.version) {
                            if (!strings.eql(inst_ver, version)) {
                                info.deinit();
                                info_opt = null;
                            }
                        }
                    }
                }
            }

            if (info_opt == null && !isolated) {
                info_opt = readStoreInfo(allocator, *path, top_len, pkg_names[pkg_id], *resolution, buf, *log, *store_entries)?;
            }

            if (info_opt == null && is_npm) {
                info_opt = takeDiskInfo(allocator, *path, top_len, *log, pkg_names[pkg_id].slice(buf), version, *disk_map)?;
            }

            if (info_opt == null) {
                allocator.free(version);
                missing += 1;
                continue;
            }
            let info = info_opt.?;

            const name_copy = allocator.dupe(u8, pkg_names[pkg_id].slice(buf)) catch |e| {
                allocator.free(version);
                info.deinit();
                return e;
            };
            let dev_only = false;
            if (prod := production) {
                if (!prod.isSet(pkg_id)) dev_only = true;
            }
            const semver_val: Semver.Version? = if (is_npm) resolution.value.npm.version else null;

            if (n := info.name) allocator.free(n);
            if (v := info.version) allocator.free(v);

            entries.append(Entry{
                allocator: allocator,
                license: info.license,
                name: name_copy,
                version: version,
                path: info.path,
                semver: semver_val,
                homepage: info.homepage,
                author: info.author,
                description: info.description,
                dev_only: dev_only,
            })?;
        }

        const SortCtx = struct {
            buf: []const u8,
            static fn lt(ctx: @This(), a: Entry, b: Entry): bool {
                const lo = licenseOrder(a.license, b.license);
                if (lo != .eq) return lo == .lt;
                const no = strings.order(a.name, b.name);
                if (no != .eq) return no == .lt;
                if (a.semver != null && b.semver != null) {
                    const vo = a.semver.?.order(b.semver.?, ctx.buf, ctx.buf);
                    if (vo != .eq) return vo == .lt;
                } else if (a.semver != null) {
                    return true;
                } else if (b.semver != null) {
                    return false;
                }
                return strings.order(a.version, b.version) == .lt;
            }
        };
        index_sort.sortSliceBy(Entry, allocator, entries.items, SortCtx{ buf: buf }, SortCtx.lt)?;

        if (json_output) {
            printJson(allocator, entries.items)?;
        } else {
            printText(allocator, entries.items, flags.long, checked)?;
        }

        Output.flush();
        if (missing > 0 && not_silent) {
            if (missing == 1) {
                Output.warn("1 package in bun.lock is not installed and was skipped", .{});
            } else {
                Output.warn("{d} packages in bun.lock are not installed and were skipped", .{missing});
            }
            Output.note("run 'bun install' first", .{});
            Output.flush();
        }
    }
}

fn licenseOrder(a: []const u8, b: []const u8): std.math.Order {
    const a_unk = strings.eqlComptime(a, UNKNOWN_LICENSE);
    const b_unk = strings.eqlComptime(b, UNKNOWN_LICENSE);
    if (a_unk != b_unk) return if (a_unk) .gt else .lt;

    const a_key = if (a.len > 0 && a[0] == '(') a[1..] else a;
    const b_key = if (b.len > 0 && b[0] == '(') b[1..] else b;
    const min_len = @min(a_key.len, b_key.len);
    let i: usize = 0;
    while (i < min_len) : (i += 1) {
        const ac = std.ascii.toLower(a_key[i]);
        const bc = std.ascii.toLower(b_key[i]);
        if (ac != bc) return if (ac < bc) .lt else .gt;
    }
    if (a_key.len != b_key.len) return if (a_key.len < b_key.len) .lt else .gt;
    return strings.order(a, b);
}

fn stripControl(allocator: Allocator, s: []const u8): ([]u8)?! {
    let has_ctrl = false;
    for (c of s) {
        if (std.ascii.isControl(c)) {
            has_ctrl = true;
            break;
        }
    }
    if (!has_ctrl) return null;
    let out = std.array_list.Managed(u8).init(allocator);
    errdefer out.deinit();
    for (c of s) {
        if (!std.ascii.isControl(c)) {
            out.append(c)?;
        }
    }
    return out.toOwnedSlice()?;
}

fn treeLocations(allocator: Allocator, lockfile: *const Lockfile): []([]u8)?! {
    const len = lockfile.packages.len;
    let out = allocator.alloc(([]u8)?, len)?;
    @memset(out, null);
    errdefer {
        for (item of out) {
            if (s := item) allocator.free(s);
        }
        allocator.free(out);
    }

    const dependencies = lockfile.buffers.dependencies.items;
    const resolutions = lockfile.buffers.resolutions.items;
    const buf = lockfile.buffers.string_bytes.items;

    let it = Lockfile.Tree.Iterator(.node_modules).init(lockfile);
    while (folder := it.next(null)) {
        for (dep_id of folder.dependencies) {
            const pkg_id = resolutions[dep_id];
            if (pkg_id >= len || out[pkg_id] != null) continue;
            const rel = folder.relative_path;
            const alias = dependencies[dep_id].name.slice(buf);
            out[pkg_id] = std.fmt.allocPrint(allocator, "{s}{c}{s}", .{ rel, std.fs.path.sep, alias })?;
        }
    }
    return out;
}

fn readPackageInfoAt(
    allocator: Allocator,
    path: *bun.AutoAbsPath,
    base_len: usize,
    segments: []const []const u8,
    log: *logger.Log,
): PackageInfo?! {
    path.setLength(base_len);
    for (seg of segments) {
        path.appendJoin(seg);
    }
    const dir_len = path.len();
    path.appendJoin(pkg_json_file_str);
    const full = path.slice();
    const info = readPackageInfo(allocator, full, full[0..dir_len], log)?;
    path.setLength(base_len);
    return info;
}

fn readPackageInfo(allocator: Allocator, path: []const u8, dir: []const u8, log: *logger.Log): PackageInfo?! {
    const file = bun.sys.openatA(bun.FD.cwd(), path, bun.O.RDONLY, 0).unwrap() catch return null;
    defer file.close();
    const contents = bun.sys.File{ handle: file }.readToEnd(allocator).unwrap() catch return null;
    defer allocator.free(contents);

    install.initializeStore();
    const source = logger.Source.initPathString(path, contents);
    const json = JSON.parsePackageJSONUTF8(*source, log, allocator) catch {
        return PackageInfo{
            allocator: allocator,
            license: allocator.dupe(u8, UNKNOWN_LICENSE)?,
            path: platformDir(allocator, dir)?,
        };
    };

    const homepage = stringField(allocator, json, "homepage")? ?? repositoryOf(allocator, json)?;
    return PackageInfo{
        allocator: allocator,
        name: stringField(allocator, json, "name")?,
        version: stringField(allocator, json, "version")?,
        license: licenseOf(allocator, json)?,
        homepage: homepage,
        author: authorOf(allocator, json)?,
        description: stringField(allocator, json, "description")?,
        path: platformDir(allocator, dir)?,
    };
}

fn platformDir(allocator: Allocator, dir: []const u8): []u8! {
    let out = allocator.dupe(u8, dir)?;
    bun.path.posixToPlatformInPlace(u8, out);
    return out;
}

fn strOf(expr: Expr): ([]const u8)? {
    if (s := expr.asString(bun.default_allocator)) {
        if (s.len > 0) return s;
    }
    return null;
}

fn stringField(allocator: Allocator, json: Expr, key: []const u8): ([]u8)?! {
    if (e := json.get(key)) {
        if (s := strOf(e)) {
            return allocator.dupe(u8, s)?;
        }
    }
    return null;
}

fn stringOrType(allocator: Allocator, expr: Expr): ([]u8)?! {
    if (s := strOf(expr)) {
        return allocator.dupe(u8, s)?;
    }
    if (t := stringField(allocator, expr, "type")?) return t;
    return stringField(allocator, expr, "name")?;
}

fn parseLicenseField(allocator: Allocator, field: Expr): ([]u8)?! {
    if (val := stringOrType(allocator, field)?) {
        return val;
    }
    let items_iter = field.asArray() ?? return null;
    let collected = std.array_list.Managed([]u8).init(allocator);
    defer {
        for (s of collected.items) allocator.free(s);
        collected.deinit();
    }
    while (item := items_iter.next()) {
        if (val := stringOrType(allocator, item)?) {
            collected.append(val)?;
        }
    }
    if (collected.items.len == 0) return null;
    if (collected.items.len == 1) {
        const res = collected.items[0];
        collected.items.len = 0;
        return res;
    }
    let joined = std.array_list.Managed(u8).init(allocator);
    errdefer joined.deinit();
    joined.append('(')?;
    let i: usize = 0;
    while (i < collected.items.len) : (i += 1) {
        if (i > 0) joined.appendSlice(" OR ")?;
        joined.appendSlice(collected.items[i])?;
    }
    joined.append(')')?;
    return joined.toOwnedSlice()?;
}

fn licenseOf(allocator: Allocator, json: Expr): []u8! {
    if (field := json.get("license")) {
        if (val := parseLicenseField(allocator, field)?) return val;
    }
    if (field := json.get("licenses")) {
        if (val := parseLicenseField(allocator, field)?) return val;
    }
    return allocator.dupe(u8, UNKNOWN_LICENSE)?;
}

fn repositoryOf(allocator: Allocator, json: Expr): ([]u8)?! {
    const repo = json.get("repository") ?? return null;
    if (s := strOf(repo)) return allocator.dupe(u8, s)?;
    return stringField(allocator, repo, "url")?;
}

fn authorOf(allocator: Allocator, json: Expr): ([]u8)?! {
    const author = json.get("author") ?? return null;
    if (s := strOf(author)) return allocator.dupe(u8, s)?;

    let out = std.array_list.Managed(u8).init(allocator);
    errdefer out.deinit();
    if (ne := author.get("name")) {
        if (name := strOf(ne)) out.appendSlice(name)?;
    }
    if (ee := author.get("email")) {
        if (email := strOf(ee)) {
            if (out.items.len > 0) out.append(' ')?;
            out.append('<')?;
            out.appendSlice(email)?;
            out.append('>')?;
        }
    }
    if (ue := author.get("url")) {
        if (url := strOf(ue)) {
            if (out.items.len > 0) out.append(' ')?;
            out.append('(')?;
            out.appendSlice(url)?;
            out.append(')')?;
        }
    }
    if (out.items.len == 0) {
        out.deinit();
        return null;
    }
    return out.toOwnedSlice()?;
}

fn readStoreInfo(
    allocator: Allocator,
    path: *bun.AutoAbsPath,
    top_len: usize,
    pkg_name: Semver.String,
    resolution: *const Resolution,
    buf: []const u8,
    log: *logger.Log,
    store_entries: *(std.array_list.Managed([]u8)?),
): PackageInfo?! {
    if (store_entries.* == null) {
        path.setLength(top_len);
        path.appendJoin(nm_dir_str);
        path.appendJoin(dot_bun_dir_str);
        const dir_items = listDir(allocator, path.slice())?;
        defer allocator.free(dir_items);
        let names = std.array_list.Managed([]u8).initCapacity(allocator, dir_items.len)?;
        for (item of dir_items) {
            names.appendAssumeCapacity(item.name);
        }
        path.setLength(top_len);
        store_entries.* = names;
    }

    const entries = store_entries.*.?.items;
    if (entries.len == 0) return null;

    let key = std.array_list.Managed(u8).init(allocator);
    defer key.deinit();
    if (resolution.tag == .folder) {
        key.writer().print("{f}@file+{f}", .{ pkg_name.fmtStorePath(buf), resolution.value.folder.fmtStorePath(buf) })?;
    } else {
        key.writer().print("{f}@{f}", .{ pkg_name.fmtStorePath(buf), resolution.fmtStorePath(buf) })?;
    }

    let lo: usize = 0;
    let hi: usize = entries.len;
    while (lo < hi) {
        const mid = lo + (hi - lo) / 2;
        if (strings.order(entries[mid], key.items) == .lt) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo >= entries.len) return null;
    const entry = entries[lo];
    const exact = strings.eql(entry, key.items);
    const peer_suffixed = entry.len > key.items.len && strings.startsWith(entry, key.items) && entry[key.items.len] == '+';
    if (!exact && !peer_suffixed) return null;

    const segs: [5][]const u8 = ["node_modules", ".bun", entry, "node_modules", pkg_name.slice(buf)];
    return readPackageInfoAt(allocator, path, top_len, &segs, log);
}

fn listDir(allocator: Allocator, path: []const u8): []DirEntryItem! {
    let out = std.array_list.Managed(DirEntryItem).init(allocator);
    errdefer {
        for (item of out.items) allocator.free(item.name);
        out.deinit();
    }
    let dir_res = bun.sys.openatA(bun.FD.cwd(), path, bun.O.RDONLY | bun.O.DIRECTORY, 0);
    const dir_fd = dir_res.unwrap() catch return out.toOwnedSlice()?;
    defer dir_fd.close();

    let iter = bun.iterateDir(dir_fd);
    while (entry_opt := iter.next().unwrap() catch null) {
        const name_copy = allocator.dupe(u8, entry_opt.name.slice())?;
        out.append(DirEntryItem{ name: name_copy, kind: entry_opt.kind })?;
    }

    const Ctx = struct {
        static fn lt(ctx: void, a: DirEntryItem, b: DirEntryItem): bool {
            * = ctx;
            return strings.order(a.name, b.name) == .lt;
        }
    };
    const empty_ctx: void = undefined;
    index_sort.sortSliceUnstableBy(DirEntryItem, allocator, out.items, empty_ctx, Ctx.lt)?;
    return out.toOwnedSlice()?;
}

fn diskKey(allocator: Allocator, name: []const u8, version: []const u8): []u8! {
    return std.fmt.allocPrint(allocator, "{s}@{s}", .{ name, version });
}

fn takeDiskInfo(
    allocator: Allocator,
    path: *bun.AutoAbsPath,
    top_len: usize,
    log: *logger.Log,
    name: []const u8,
    version: []const u8,
    disk_map: *(std.StringHashMap(PackageInfo)?),
): PackageInfo?! {
    if (disk_map.* == null) {
        let map = std.StringHashMap(PackageInfo).init(allocator);
        path.setLength(top_len);
        path.appendJoin(nm_dir_str);
        scanNodeModules(allocator, path, 0, log, *map)?;
        path.setLength(top_len);
        disk_map.* = map;
    }
    const key = diskKey(allocator, name, version)?;
    defer allocator.free(key);
    if (kv := disk_map.*.?.fetchRemove(key)) {
        allocator.free(kv.key);
        return kv.value;
    }
    return null;
}

fn scanNodeModules(
    allocator: Allocator,
    path: *bun.AutoAbsPath,
    depth: usize,
    log: *logger.Log,
    out: *std.StringHashMap(PackageInfo),
)!std.mem.Allocator.Error {
    const nm_len = path.len();
    const items = listDir(allocator, path.slice())?;
    defer {
        for (item of items) allocator.free(item.name);
        allocator.free(items);
    }

    for (item of items) {
        if (item.name.len == 0 || item.name[0] == '.' || (depth > 0 && item.kind == .sym_link)) {
            continue;
        }
        path.appendJoin(item.name);
        if (item.name[0] != '@') {
            scanPackage(allocator, path, depth, log, out)?;
        } else {
            const scope_len = path.len();
            const scoped_items = listDir(allocator, path.slice())?;
            defer {
                for (si of scoped_items) allocator.free(si.name);
                allocator.free(scoped_items);
            }
            for (si of scoped_items) {
                if (si.name.len == 0 || si.name[0] == '.' || (depth > 0 && si.kind == .sym_link)) {
                    continue;
                }
                path.appendJoin(si.name);
                scanPackage(allocator, path, depth, log, out)?;
                path.setLength(scope_len);
            }
        }
        path.setLength(nm_len);
    }
}

fn scanPackage(
    allocator: Allocator,
    path: *bun.AutoAbsPath,
    depth: usize,
    log: *logger.Log,
    out: *std.StringHashMap(PackageInfo),
)!std.mem.Allocator.Error {
    const pkg_len = path.len();
    const empty_segs: []const []const u8 = [];
    if (info_opt := readPackageInfoAt(allocator, path, pkg_len, empty_segs, log)?) {
        let info = info_opt;
        if (info.name != null && info.version != null) {
            const key = diskKey(allocator, info.name.?, info.version.?)?;
            if (out.contains(key)) {
                allocator.free(key);
                info.deinit();
            } else {
                out.put(key, info) catch |e| {
                    allocator.free(key);
                    info.deinit();
                    return e;
                };
            }
        } else {
            info.deinit();
        }
    }
    if (depth < MAX_SCAN_DEPTH) {
        path.appendJoin(nm_dir_str);
        scanNodeModules(allocator, path, depth + 1, log, out)?;
    }
    path.setLength(pkg_len);
}

fn printPrintable(allocator: Allocator, comptime fmt_str: []const u8, s: []const u8) {
    const stripped = (stripControl(allocator, s) catch null);
    defer if (st := stripped) allocator.free(st);
    const val = stripped ?? s;
    Output.prettyln(fmt_str, .{val});
}

fn printText(allocator: Allocator, entries: []const Entry, long: bool, checked: usize)! {
    if (entries.len == 0) {
        const suffix: []const u8 = if (checked == 1) "" else "s";
        Output.pretty("No packages to list <d>(checked {d} package{s} in bun.lock)<r> ", .{ checked, suffix });
        Output.printStartEndStdout(bun.cli.start_time, std.time.nanoTimestamp());
        Output.pretty("\n", .{});
        return;
    }

    let start: usize = 0;
    while (start < entries.len) {
        const license = entries[start].license;
        let end = start + 1;
        while (end < entries.len && strings.eql(entries[end].license, license)) {
            end += 1;
        }
        if (start > 0) Output.print("\n", .{});
        const stripped_lic = (stripControl(allocator, license) catch null);
        defer if (sl := stripped_lic) allocator.free(sl);
        Output.prettyln("<b>{s}<r> <d>({d})<r>", .{ stripped_lic ?? license, end - start });

        let i = start;
        while (i < end) : (i += 1) {
            const entry = entries[i];
            const last = i + 1 == end;
            const branch: []const u8 = if (last) "└──" else "├──";
            Output.pretty("<d>{s}<r> {s}<d>@{s}<r>", .{ branch, entry.name, entry.version });
            if (entry.dev_only) {
                Output.pretty(" <d>(dev)<r>", .{});
            }
            Output.print("\n", .{});
            if (long) {
                const fields: [3]([]u8)? = [entry.author, entry.description, entry.homepage];
                for (f_opt of fields) {
                    if (f := f_opt) {
                        if (last) {
                            printPrintable(allocator, "    <d>{s}<r>", f);
                        } else {
                            printPrintable(allocator, "<d>│   {s}<r>", f);
                        }
                    }
                }
            }
        }
        start = end;
    }
}

fn jsonString(out: *std.array_list.Managed(u8), s: []const u8)! {
    out.append('"')?;
    for (c of s) {
        switch (c) {
            '"' => out.appendSlice("\\\"")?,
            '\\' => out.appendSlice("\\\\")?,
            '\n' => out.appendSlice("\\n")?,
            '\r' => out.appendSlice("\\r")?,
            '\t' => out.appendSlice("\\t")?,
            0x08 => out.appendSlice("\\b")?,
            0x0c => out.appendSlice("\\f")?,
            0x00..=0x07, 0x0b, 0x0e..=0x1f => out.writer().print("\\u{x:0>4}", .{c})?,
            else => out.append(c)?,
        }
    }
    out.append('"')?;
}

fn printJson(allocator: Allocator, entries: []const Entry)! {
    let out = std.array_list.Managed(u8).init(allocator);
    defer out.deinit();
    const w = *out;

    if (entries.len == 0) {
        Output.writer().writeAll("{}\n")?;
        return;
    }

    out.appendSlice("{\n")?;
    let start: usize = 0;
    while (start < entries.len) {
        const license = entries[start].license;
        let end = start + 1;
        while (end < entries.len && strings.eql(entries[end].license, license)) {
            end += 1;
        }
        if (start > 0) out.appendSlice(",\n")?;
        out.appendSlice("  ")?;
        jsonString(w, license)?;
        out.appendSlice(": [")?;

        let group_start = start;
        let first_group = true;
        while (group_start < end) {
            const first = entries[group_start];
            let group_end = group_start + 1;
            while (group_end < end && strings.eql(entries[group_end].name, first.name)) {
                group_end += 1;
            }
            if (!first_group) out.append(',')?;
            first_group = false;
            out.appendSlice("\n    {\n      \"name\": ")?;
            jsonString(w, first.name)?;

            let kept = std.array_list.Managed(usize).init(allocator);
            defer kept.deinit();
            let gi = group_start;
            while (gi < group_end) : (gi += 1) {
                if (kept.items.len == 0 || !strings.eql(entries[kept.items[kept.items.len - 1]].version, entries[gi].version)) {
                    kept.append(gi)?;
                }
            }

            out.appendSlice(",\n      \"versions\": [")?;
            let ki: usize = 0;
            while (ki < kept.items.len) : (ki += 1) {
                if (ki > 0) out.appendSlice(", ")?;
                jsonString(w, entries[kept.items[ki]].version)?;
            }
            out.appendSlice("],\n      \"paths\": [")?;
            ki = 0;
            while (ki < kept.items.len) : (ki += 1) {
                if (ki > 0) out.appendSlice(", ")?;
                jsonString(w, entries[kept.items[ki]].path)?;
            }
            out.appendSlice("],\n      \"license\": ")?;
            jsonString(w, license)?;

            const newest = entries[group_end - 1];
            const extra_keys: [3][]const u8 = ["author", "description", "homepage"];
            const extra_vals: [3]([]u8)? = [newest.author, newest.description, newest.homepage];
            let ei: usize = 0;
            while (ei < 3) : (ei += 1) {
                if (val := extra_vals[ei]) {
                    out.appendSlice(",\n      \"")?;
                    out.appendSlice(extra_keys[ei])?;
                    out.appendSlice("\": ")?;
                    jsonString(w, val)?;
                }
            }
            out.appendSlice("\n    }")?;
            group_start = group_end;
        }
        out.appendSlice("\n  ]")?;
        start = end;
    }
    out.appendSlice("\n}\n")?;
    Output.writer().writeAll(out.items)?;
}
