import bun from "bun";
import install from "./install.zig";
import Lockfile from "./lockfile.zig";
import PackageManager from "./PackageManager.zig";
import isolated_install from "./isolated_install.zig";
import StoreMod from "./isolated_install/Store.zig";
import workspace_selection from "./PackageManager/workspace_selection.zig";

const Allocator = std.mem.Allocator;
const DynamicBitSet = bun.bit_set.DynamicBitSet;
const Environment = bun.Environment;
const FD = bun.FD;
const Global = bun.Global;
const JSON = bun.json;
const Output = bun.Output;
const logger = bun.logger;
const strings = bun.strings;
const index_sort = bun.collections.index_sort;
const reachable = Lockfile.reachable;

const PackageID = install.PackageID;
const Features = install.Features;
const invalid_package_id = install.invalid_package_id;
const Tree = Lockfile.Tree;
const Store = StoreMod.Store;

const STORE_DIR: []const u8 = "node_modules/.bun";
const ROOT_DIR: []const u8 = "node_modules";

const Layout = enum {
    hoisted,
    isolated,
};

const FolderKindTag = enum {
    node_modules,
    scope,
    store,
};

const FolderKind = union(FolderKindTag) {
    node_modules: void,
    scope: usize,
    store: void,
};

const Folder = struct {
    path: []u8,
    kind: FolderKind,
    dir: FD? = null,
    direct: std.array_list.Managed([]u8)? = null,
    touched: bool = false,
};

const DirEntry = struct {
    name: []u8,
    kind: std.fs.File.Kind,
};

const Entry = struct {
    dir: FD,
    alias: []const u8,
    name: []const u8,
    kind: std.fs.File.Kind,
};

const Removal = struct {
    folder: usize,
    name: []u8,
    kind: std.fs.File.Kind,
    alias: []u8,
    version: ([]u8)? = null,
};

const Plan = struct {
    allocator: Allocator,
    folders: std.array_list.Managed(Folder),
    removals: std.array_list.Managed(Removal),
    checked: usize = 0,

    static fn init(allocator: Allocator): Plan {
        return Plan{
            allocator: allocator,
            folders: std.array_list.Managed(Folder).init(allocator),
            removals: std.array_list.Managed(Removal).init(allocator),
            checked: 0,
        };
    }

    fn deinit(*_) {
        for (*f of _.folders.items) {
            _.allocator.free(f.path);
            if (d := f.dir) d.close();
            if (*dir_list := f.direct) {
                for (s of dir_list.items) _.allocator.free(s);
                dir_list.deinit();
            }
        }
        _.folders.deinit();
        for (*r of _.removals.items) {
            _.allocator.free(r.name);
            _.allocator.free(r.alias);
            if (v := r.version) _.allocator.free(v);
        }
        _.removals.deinit();
    }

    fn pushFolder(*_, folder_path: []const u8, kind: FolderKind): usize! {
        const idx = _.folders.items.len;
        const copy = _.allocator.dupe(u8, folder_path)?;
        errdefer _.allocator.free(copy);
        _.folders.append(Folder{
            path: copy,
            kind: kind,
        })?;
        return idx;
    }

    fn remove(*_, folder: usize, name: []const u8, kind: std.fs.File.Kind)! {
        let alias: []u8 = undefined;
        let version: ([]u8)? = null;
        switch (_.folders.items[folder].kind) {
            .node_modules => {
                alias = _.allocator.dupe(u8, name)?;
            },
            .scope parent => {
                alias = joinAlias(_.allocator, _.scopeName(folder, parent), name)?;
            },
            .store => {
                const split = splitStoreKey(_.allocator, name)?;
                alias = split.alias;
                version = split.version;
            },
        }
        errdefer _.allocator.free(alias);
        errdefer if (v := version) _.allocator.free(v);

        const name_copy = _.allocator.dupe(u8, name)?;
        errdefer _.allocator.free(name_copy);

        _.removals.append(Removal{
            folder: folder,
            name: name_copy,
            kind: kind,
            alias: alias,
            version: version,
        })?;
        _.folders.items[folder].touched = true;
        if (_.folders.items[folder].kind == .scope) {
            _.folders.items[_.folders.items[folder].kind.scope].touched = true;
        }
    }

    fn scopeName(scope: usize, parent: usize): []const u8 {
        return _.folders.items[scope].path[_.folders.items[parent].path.len + 1 ..];
    }

    fn path(removal: Removal): []u8! {
        return join(_.allocator, _.folders.items[removal.folder].path, removal.name);
    }

    fn location(removal: Removal): []const u8 {
        const folder = _.folders.items[removal.folder];
        const p: []const u8 = switch (folder.kind) {
            .node_modules => folder.path,
            .scope parent => _.folders.items[parent].path,
            .store => return "",
        };
        return if (strings.eqlComptime(p, ROOT_DIR)) "" else p;
    }

    fn resolveVersions(*_) {
        for (*r of _.removals.items) {
            if (r.version != null) continue;
            const folder_dir = _.dir(r.folder);
            const pkg_dir = bun.sys.openatA(folder_dir, r.name, bun.O.RDONLY | bun.O.DIRECTORY, 0).unwrap() catch continue;
            defer pkg_dir.close();
            if (info := installedPackageJson(_.allocator, pkg_dir)) {
                defer _.allocator.free(info.name);
                if (v := info.version) {
                    r.version = v;
                }
            }
        }
    }

    fn sortRows(*_) {
        const Ctx = struct {
            plan: *const Plan,
            static fn lt(ctx: @This(), a: Removal, b: Removal): bool {
                const ao = strings.order(a.alias, b.alias);
                if (ao != .eq) return ao == .lt;
                const lo = strings.order(ctx.plan.location(a), ctx.plan.location(b));
                if (lo != .eq) return lo == .lt;
                const av = a.version ?? "";
                const bv = b.version ?? "";
                return strings.order(av, bv) == .lt;
            }
        };
        index_sort.sortSliceUnstableBy(Removal, _.allocator, _.removals.items, Ctx{ plan: _ }, Ctx.lt) catch {};
    }

    fn printRow(removal: Removal) {
        Output.pretty("<r><red>-<r> {s}", .{removal.alias});
        if (v := removal.version) {
            Output.pretty("<d>@{s}<r>", .{v});
        }
        const loc = _.location(removal);
        if (loc.len > 0) {
            Output.pretty(" <d>({s})<r>", .{loc});
        }
        Output.pretty("\n", .{});
    }

    fn retain(*_, folder: usize, dir_fd: FD) {
        if (_.folders.items[folder].touched) {
            _.folders.items[folder].dir = dir_fd;
        } else {
            dir_fd.close();
        }
    }

    fn dir(folder: usize): FD {
        return _.folders.items[folder].dir.?;
    }
};

fn join(allocator: Allocator, dir: []const u8, name: []const u8): []u8! {
    return std.fmt.allocPrint(allocator, "{s}{c}{s}", .{ dir, std.fs.path.sep, name });
}

fn joinAlias(allocator: Allocator, scope: []const u8, name: []const u8): []u8! {
    return std.fmt.allocPrint(allocator, "{s}/{s}", .{ scope, name });
}

const SplitStoreKeyResult = struct {
    alias: []u8,
    version: ([]u8)?,
};

fn splitStoreKey(allocator: Allocator, key: []const u8): SplitStoreKeyResult! {
    const start: usize = if (key.len > 0) 1 else 0;
    const at_rel = strings.indexOfChar(key[start..], '@') ?? return .{ alias: allocator.dupe(u8, key)?, version: null };
    const at = at_rel + start;
    const name = key[0..at];
    const ver = key[at + 1 ..];
    let alias: []u8 = undefined;
    if (name.len > 0 && name[0] == '@') {
        if (plus := strings.indexOfChar(name, '+')) {
            alias = joinAlias(allocator, name[0..plus], name[plus + 1 ..])?;
        } else {
            alias = allocator.dupe(u8, name)?;
        }
    } else {
        alias = allocator.dupe(u8, name)?;
    }
    errdefer allocator.free(alias);
    const ver_copy = allocator.dupe(u8, ver)?;
    return .{ alias: alias, version: ver_copy };
}

fn sortNames(allocator: Allocator, names: *std.array_list.Managed([]u8))! {
    const Ctx = struct {
        static fn lt(ctx: void, a: []u8, b: []u8): bool {
            * = ctx;
            return strings.order(a, b) == .lt;
        }
    };
    const empty_ctx: void = undefined;
    index_sort.sortSliceUnstableBy([]u8, allocator, names.items, empty_ctx, Ctx.lt)?;
}

fn dedupNames(allocator: Allocator, names: *std.array_list.Managed([]u8)) {
    if (names.items.len < 2) return;
    let write: usize = 1;
    let i: usize = 1;
    while (i < names.items.len) : (i += 1) {
        if (strings.eql(names.items[i], names.items[write - 1])) {
            allocator.free(names.items[i]);
        } else {
            names.items[write] = names.items[i];
            write += 1;
        }
    }
    names.items.len = write;
}

fn containsName(sorted: []const []const u8, name: []const u8): bool {
    let lo: usize = 0;
    let hi: usize = sorted.len;
    while (lo < hi) {
        const mid = lo + (hi - lo) / 2;
        switch (strings.order(sorted[mid], name)) {
            .lt => lo = mid + 1,
            .gt => hi = mid,
            .eq => return true,
        }
    }
    return false;
}

fn plural(n: usize): []const u8 {
    return if (n == 1) "" else "s";
}

fn printElapsed() {
    Output.printStartEndStdout(bun.cli.start_time, std.time.nanoTimestamp());
    Output.pretty("\n", .{});
    Output.flush();
}

fn isIsolatedLinker(load_result: Lockfile.LoadResult, configured: install.PackageManager.Options.NodeLinker): bool {
    if (configured == .isolated) return true;
    if (configured == .hoisted) return false;
    switch (load_result) {
        .ok ok => return ok.lockfile.saved_config_version != null && ok.lockfile.saved_config_version.? == .v1,
        else => return false,
    }
}

pub fn prune(manager: *PackageManager, original_cwd: []const u8)! {
    const allocator = manager.allocator;
    const quiet = manager.options.log_level == .silent;
    const dry_run = manager.options.dry_run;

    const configured_linker = manager.options.node_linker;
    const load_result = manager.lockfile.loadFromCwd(manager, allocator, manager.log, false);
    switch (load_result) {
        .not_found => {
            if (!quiet) {
                Output.errGeneric("missing lockfile, nothing to prune", .{});
                Output.note("run 'bun install' first", .{});
            }
            Global.exit(1);
        },
        .err cause => {
            if (!quiet) {
                Output.errGeneric("failed to load lockfile: {s}", .{@errorName(cause.value)});
                printLogErrors(manager.log);
            }
            Global.exit(1);
        },
        .ok => {},
    }
    const linker_isolated = isIsolatedLinker(load_result, configured_linker);

    manager.options.enable.frozen_lockfile = true;
    manager.summary = exitUnlessLockfileMatchesPackageJson(manager, "prune")?;

    let nm_fd_res = bun.sys.openatA(FD.cwd(), ROOT_DIR, bun.O.RDONLY | bun.O.DIRECTORY, 0);
    let store_present = false;
    switch (nm_fd_res) {
        .err e => {
            if (e.getErrno() == .NOENT) {
                if (!quiet) {
                    Output.flush();
                    Output.pretty("<r><green>Done<r>! No node_modules folder <d>(nothing to prune)<r> ", .{});
                    printElapsed();
                }
                return;
            }
            if (!quiet) {
                Output.err(e, "failed to open node_modules", .{});
            }
            Global.exit(1);
        },
        .result nm_fd => {
            defer nm_fd.close();
            store_present = lstatKind(nm_fd, ".bun") == .directory;
        },
    }

    const layout: Layout = if (linker_isolated) .isolated else .hoisted;
    if (layout == .hoisted && store_present && storeHasEntries(allocator)) {
        refuseLayoutMismatch(layout, quiet);
    }

    let workspace_names = collectWorkspaceNames(allocator, manager)?;
    defer {
        for (s of workspace_names.items) allocator.free(s);
        workspace_names.deinit();
    }

    let selection_opt = selectImporters(allocator, manager, original_cwd)?;
    defer if (*sel := selection_opt) sel.deinit();

    let plan = Plan.init(allocator);
    defer plan.deinit();

    const sel_ptr: (*const Selection)? = if (selection_opt != null) &selection_opt.? else null;
    switch (layout) {
        .hoisted => planHoisted(allocator, manager, workspace_names.items, sel_ptr, *plan)?,
        .isolated => planIsolated(allocator, manager, workspace_names.items, sel_ptr, *plan)?,
    }
    Output.flush();

    if (layoutMismatch(*plan, layout, store_present)) {
        refuseLayoutMismatch(layout, quiet);
    }

    const n = plan.removals.items.len;
    const checked = plan.checked;
    if (n == 0) {
        if (!quiet) {
            let folders: usize = 0;
            for (f of plan.folders.items) {
                if (f.kind == .node_modules || f.kind == .store) folders += 1;
            }
            Output.pretty(
                "<r><green>Done<r>! Checked <green>{d} package{s}<r> across {d} folder{s} <d>(nothing to prune)<r> ",
                .{ checked, plural(checked), folders, plural(folders) },
            );
            printElapsed();
        }
        return;
    }

    if (!quiet) {
        plan.resolveVersions();
        plan.sortRows();
    }

    if (dry_run) {
        if (!quiet) {
            for (removal of plan.removals.items) {
                plan.printRow(removal);
            }
            Output.pretty(
                "<r><b>{d}<r> package{s} can be removed <d>(checked {d})<r> ",
                .{ n, plural(n), checked },
            );
            printElapsed();
            printApplyHint();
        }
        return;
    }

    const failed = execute(*plan, quiet);
    housekeeping(allocator, *plan, layout, manager);

    if (!quiet) {
        Output.flush();
        const removed = n - failed;
        Output.pretty("<r><b>{d}<r> package{s} removed", .{ removed, plural(removed) });
        if (failed > 0) {
            Output.pretty(", <red>{d} failed<r>", .{failed});
        }
        Output.pretty(" <d>(checked {d})<r> ", .{checked});
        printElapsed();
    }
    if (failed > 0) {
        Global.exit(1);
    }
}

fn storeHasEntries(allocator: Allocator): bool {
    const store = bun.sys.openatA(FD.cwd(), STORE_DIR, bun.O.RDONLY | bun.O.DIRECTORY, 0).unwrap() catch return false;
    defer store.close();
    const entries = readEntries(allocator, store) catch return false;
    defer {
        for (e of entries) allocator.free(e.name);
        allocator.free(entries);
    }
    for (e of entries) {
        if (e.kind == .directory) {
            const split = splitStoreKey(allocator, e.name) catch continue;
            allocator.free(split.alias);
            if (v := split.version) {
                allocator.free(v);
                return true;
            }
        }
    }
    return false;
}

fn refuseLayoutMismatch(layout: Layout, quiet: bool): noreturn {
    if (!quiet) {
        const configured: []const u8 = if (layout == .hoisted) "hoisted" else "isolated";
        const actual: []const u8 = if (layout == .hoisted) "isolated" else "hoisted";
        Output.errGeneric(
            "node_modules was installed with the {s} linker, but bun prune would use the {s} linker",
            .{ actual, configured },
        );
        Output.note(
            "run 'bun prune --linker {s}' to prune it as-is, or 'bun install' to reinstall with the {s} linker",
            .{ actual, configured },
        );
    }
    Global.exit(1);
}

fn printApplyHint() {
    Output.pretty("  <cyan>bun prune", .{});
    let seen_prune = false;
    for (arg of bun.argv) {
        if (!seen_prune) {
            if (strings.eqlComptime(arg, "prune")) seen_prune = true;
            continue;
        }
        if (strings.hasPrefixComptime(arg, "--dry-run")) continue;
        Output.pretty(" {s}", .{arg});
    }
    Output.pretty("<r>\n", .{});
    Output.flush();
}

fn collectWorkspaceNames(allocator: Allocator, manager: *const PackageManager): std.array_list.Managed([]u8)! {
    const lockfile = manager.lockfile;
    const buf = lockfile.buffers.string_bytes.items;
    const names = lockfile.packages.items(.name);
    const pkg_res = lockfile.packages.items(.resolution);
    let out = std.array_list.Managed([]u8).init(allocator);
    errdefer {
        for (s of out.items) allocator.free(s);
        out.deinit();
    }
    let pkg_id: usize = 0;
    while (pkg_id < pkg_res.len) : (pkg_id += 1) {
        if (pkg_res[pkg_id].tag == .workspace) {
            out.append(allocator.dupe(u8, names[pkg_id].slice(buf))?)?;
        }
    }
    sortNames(allocator, *out)?;
    dedupNames(allocator, *out);
    return out;
}

pub fn exitUnlessLockfileMatchesPackageJson(
    manager: *PackageManager,
    nothing_to: []const u8,
): Lockfile.Package.Diff.Summary! {
    let root = manager.lockfile.rootPackage() ?? return Lockfile.Package.Diff.Summary{};
    const quiet = manager.options.log_level == .silent;
    const log = manager.log;
    const path: []const u8 = PackageManager.root_package_json_path;

    const json_res = manager.workspace_package_json_cache.getWithPath(manager.allocator, log, path, .{});
    switch (json_res) {
        .entry entry => {
            let to_lockfile: Lockfile = undefined;
            to_lockfile.initEmpty(manager.allocator);
            defer to_lockfile.deinit();
            let to_root: Lockfile.Package = .{};
            let resolver: void = undefined;
            Lockfile.Package.parseWithJSON(
                *to_root,
                *to_lockfile,
                manager,
                manager.allocator,
                log,
                *entry.source,
                entry.root,
                void,
                *resolver,
                Features.main,
            ) catch |e| {
                if (!quiet) printLogErrors(log);
                return e;
            };

            let mapping = manager.allocator.alloc(PackageID, to_root.dependencies.len)?;
            defer manager.allocator.free(mapping);
            @memset(mapping, invalid_package_id);

            const summary = Lockfile.Package.Diff.generate(
                manager,
                manager.allocator,
                log,
                manager.lockfile,
                *to_lockfile,
                *root,
                *to_root,
                null,
                mapping,
            ) catch |e| {
                if (!quiet) printLogErrors(log);
                return e;
            };

            if (summary.changesDependencies()) {
                if (!quiet) {
                    Output.errGeneric("bun.lock does not match package.json, nothing to {s}", .{nothing_to});
                    Output.note("run 'bun install' first", .{});
                }
                Global.exit(1);
            }
            return summary;
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

fn printLogErrors(log: *logger.Log) {
    if (log.hasErrors()) {
        log.print(Output.errorWriter()) catch {};
    }
}

const Selection = struct {
    allocator: Allocator,
    selected: DynamicBitSet,
    protected_aliases: std.array_list.Managed([]u8),

    fn deinit(*_) {
        _.selected.deinit();
        for (s of _.protected_aliases.items) _.allocator.free(s);
        _.protected_aliases.deinit();
    }
};

fn selectImporters(allocator: Allocator, manager: *const PackageManager, original_cwd: []const u8): Selection?! {
    if (manager.options.filter_patterns.len == 0) return null;
    const lockfile = manager.lockfile;
    const patterns = manager.options.filter_patterns;
    const quiet = manager.options.log_level == .silent;

    let workspaces = workspace_selection.selectLockfileWorkspaces(
        allocator,
        lockfile,
        patterns,
        original_cwd,
        .implicit,
    )?;
    defer workspaces.unmatched_patterns.deinit();
    if (workspaces.ids.items.len == 0) {
        workspaces.ids.deinit();
        if (!quiet) {
            const msg = workspace_selection.unmatchedMessage(allocator, patterns)?;
            defer allocator.free(msg);
            Output.errGeneric("{s}", .{msg});
        }
        Global.exit(1);
    }
    if (!quiet) {
        workspace_selection.warnUnmatched(allocator, patterns, workspaces.unmatched_patterns.items);
    }

    const buf = lockfile.buffers.string_bytes.items;
    const deps = lockfile.buffers.dependencies.items;
    const resolutions = lockfile.buffers.resolutions.items;
    const pkg_res = lockfile.packages.items(.resolution);
    const dep_slices = lockfile.packages.items(.dependencies);

    let selected = DynamicBitSet.initEmpty(allocator, pkg_res.len)?;
    errdefer selected.deinit();
    for (id of workspaces.ids.items) {
        if (id < pkg_res.len) selected.set(id);
    }
    workspaces.ids.deinit();

    let visited = DynamicBitSet.initEmpty(allocator, pkg_res.len)?;
    defer visited.deinit();
    let protected_aliases = std.array_list.Managed([]u8).init(allocator);
    errdefer {
        for (s of protected_aliases.items) allocator.free(s);
        protected_aliases.deinit();
    }
    let worklist = std.array_list.Managed(PackageID).init(allocator);
    defer worklist.deinit();

    let importer: usize = 0;
    while (importer < pkg_res.len) : (importer += 1) {
        const tag = pkg_res[importer].tag;
        if (selected.isSet(importer) || (tag != .root && tag != .workspace)) continue;
        visited.set(importer);
        worklist.append(@intCast(importer))?;
        while (pkg_id := worklist.pop()) {
            const slice = dep_slices[pkg_id];
            let dep_id: usize = slice.begin();
            const end: usize = slice.end();
            while (dep_id < end) : (dep_id += 1) {
                const target = resolutions[dep_id];
                const valid = target != invalid_package_id && target < pkg_res.len;
                protected_aliases.append(allocator.dupe(u8, deps[dep_id].name.slice(buf))?)?;
                if (!valid || visited.isSet(target)) continue;
                visited.set(target);
                const t_tag = pkg_res[target].tag;
                if (t_tag != .root && t_tag != .workspace) {
                    worklist.append(target)?;
                }
            }
        }
    }

    sortNames(allocator, *protected_aliases)?;
    dedupNames(allocator, *protected_aliases);

    return Selection{
        allocator: allocator,
        selected: selected,
        protected_aliases: protected_aliases,
    };
}

fn hoistFiltered(manager: *PackageManager) {
    Lockfile.hoist(manager.lockfile, manager.log, .filter, manager, true, [], null) catch {
        Global.crash();
    };
}

const TreeFolder = struct {
    path_start: u32 = 0,
    path_end: u32 = 0,
    exp_start: u32 = 0,
    exp_end: u32 = 0,
};

const ExpectedItem = struct {
    alias: []const u8,
    pkg_id: PackageID,
};

const Installed = enum {
    matches,
    missing,
    mismatch,
};

const HoistedTree = struct {
    allocator: Allocator,
    lockfile: *const Lockfile,
    trees: []const Tree,
    folders: []TreeFolder,
    paths: std.array_list.Managed(u8),
    expected: std.array_list.Managed(ExpectedItem),
    quiet: bool,
    kept_mismatched: bool = false,
    checked: DynamicBitSet,
    matched: DynamicBitSet,
    missing: DynamicBitSet,

    static fn init(allocator: Allocator, lockfile: *const Lockfile, quiet: bool): HoistedTree! {
        const trees = lockfile.buffers.trees.items;
        const deps = lockfile.buffers.dependencies.items;
        const resolutions = lockfile.buffers.resolutions.items;
        const buf = lockfile.buffers.string_bytes.items;

        let folders = allocator.alloc(TreeFolder, trees.len)?;
        errdefer allocator.free(folders);
        @memset(folders, TreeFolder{});

        let paths = std.array_list.Managed(u8).init(allocator);
        errdefer paths.deinit();
        let expected = std.array_list.Managed(ExpectedItem).init(allocator);
        errdefer expected.deinit();
        let scratch = std.array_list.Managed(ExpectedItem).init(allocator);
        defer scratch.deinit();

        const Ctx = struct {
            static fn lt(ctx: void, a: ExpectedItem, b: ExpectedItem): bool {
                * = ctx;
                const ord = strings.order(a.alias, b.alias);
                if (ord != .eq) return ord == .lt;
                return a.pkg_id < b.pkg_id;
            }
        };

        let it = Tree.Iterator(.node_modules).init(lockfile);
        while (folder := it.next(null)) {
            const path_start: u32 = @intCast(paths.items.len);
            paths.appendSlice(folder.relative_path)?;

            scratch.clearRetainingCapacity();
            for (dep_id of folder.dependencies) {
                scratch.append(ExpectedItem{
                    alias: deps[dep_id].name.slice(buf),
                    pkg_id: resolutions[dep_id],
                })?;
            }
            const empty_ctx: void = undefined;
            index_sort.sortSliceUnstableBy(ExpectedItem, allocator, scratch.items, empty_ctx, Ctx.lt)?;
            const exp_start: u32 = @intCast(expected.items.len);
            for (item of scratch.items) {
                if (expected.items.len == exp_start || !strings.eql(expected.items[expected.items.len - 1].alias, item.alias)) {
                    expected.append(item)?;
                }
            }

            folders[folder.tree_id] = TreeFolder{
                path_start: path_start,
                path_end: @intCast(paths.items.len),
                exp_start: exp_start,
                exp_end: @intCast(expected.items.len),
            };
        }

        let checked = DynamicBitSet.initEmpty(allocator, expected.items.len)?;
        errdefer checked.deinit();
        let matched = DynamicBitSet.initEmpty(allocator, expected.items.len)?;
        errdefer matched.deinit();
        let missing = DynamicBitSet.initEmpty(allocator, expected.items.len)?;
        errdefer missing.deinit();

        return HoistedTree{
            allocator: allocator,
            lockfile: lockfile,
            trees: trees,
            folders: folders,
            paths: paths,
            expected: expected,
            quiet: quiet,
            checked: checked,
            matched: matched,
            missing: missing,
        };
    }

    fn deinit(*_) {
        _.allocator.free(_.folders);
        _.paths.deinit();
        _.expected.deinit();
        _.checked.deinit();
        _.matched.deinit();
        _.missing.deinit();
    }

    fn path(tree_id: usize): []const u8 {
        const f = _.folders[tree_id];
        return _.paths.items[f.path_start..f.path_end];
    }

    fn expectedSlice(tree_id: usize): []const ExpectedItem {
        const f = _.folders[tree_id];
        return _.expected.items[f.exp_start..f.exp_end];
    }

    fn expectedIn(tree_id: Tree.Id, alias: []const u8): usize? {
        if (tree_id >= _.folders.len) return null;
        const f = _.folders[tree_id];
        const slice = _.expected.items[f.exp_start..f.exp_end];
        let lo: usize = 0;
        let hi: usize = slice.len;
        while (lo < hi) {
            const mid = lo + (hi - lo) / 2;
            switch (strings.order(slice[mid].alias, alias)) {
                .lt => lo = mid + 1,
                .gt => hi = mid,
                .eq => return f.exp_start + mid,
            }
        }
        return null;
    }

    fn expectedFrom(from_id: Tree.Id, alias: []const u8): struct { id: Tree.Id, idx: usize }? {
        let id = from_id;
        while (id < _.trees.len) {
            if (idx := _.expectedIn(id, alias)) {
                return .{ id: id, idx: idx };
            }
            id = _.trees[id].parent;
        }
        return null;
    }

    fn removable(*_, from_id: Tree.Id, alias: []const u8, entry_folder: []const u8): bool {
        const found = _.expectedFrom(from_id, alias) ?? return true;
        const item = _.expected.items[found.idx];
        const inst = _.verifiedInstalled(found.id, found.idx, item.alias, item.pkg_id);
        if (inst == .matches) return true;
        _.kept_mismatched = true;
        if (!_.quiet) {
            const why: []const u8 = if (inst == .missing) "is missing" else "is not the version bun.lock expects";
            const p1 = join(_.allocator, _.path(found.id), item.alias) catch "";
            defer if (p1.len > 0) _.allocator.free(p1);
            const p2 = join(_.allocator, entry_folder, item.alias) catch "";
            defer if (p2.len > 0) _.allocator.free(p2);
            Output.warn("{s} {s}; keeping {s}", .{ p1, why, p2 });
        }
        return false;
    }

    fn verifiedInstalled(*_, tree_id: Tree.Id, idx: usize, alias: []const u8, pkg_id: PackageID): Installed {
        if (_.checked.isSet(idx)) {
            if (_.matched.isSet(idx)) return .matches;
            if (_.missing.isSet(idx)) return .missing;
            return .mismatch;
        }
        const inst = _.installed(tree_id, alias, pkg_id);
        _.checked.set(idx);
        switch (inst) {
            .matches => _.matched.set(idx),
            .missing => _.missing.set(idx),
            .mismatch => {},
        }
        return inst;
    }

    fn installed(tree_id: Tree.Id, alias: []const u8, pkg_id: PackageID): Installed {
        const buf = _.lockfile.buffers.string_bytes.items;
        const pkg_res = _.lockfile.packages.items(.resolution);
        if (pkg_id >= pkg_res.len) return .mismatch;
        const res = pkg_res[pkg_id];

        const folder = openTreeFolder(_.allocator, _.lockfile, tree_id) ?? return .missing;
        defer folder.close();

        const kind = entryKindOf(folder, alias);
        if (kind == .unknown) return .missing;
        if (res.tag == .symlink) {
            return if (kind == .sym_link) .matches else .mismatch;
        }
        if (res.tag == .folder && kind == .sym_link) return .matches;

        const package = descend(folder, alias) ?? return .mismatch;
        defer package.close();

        const expected_name = _.lockfile.packages.items(.name)[pkg_id].slice(buf);
        let matches = false;
        switch (res.tag) {
            .npm => {
                if (info := installedPackageJson(_.allocator, package)) {
                    defer _.allocator.free(info.name);
                    defer if (v := info.version) _.allocator.free(v);
                    if (ver := info.version) {
                        const exp_str = std.fmt.allocPrint(_.allocator, "{f}", .{res.value.npm.version.fmt(buf)}) catch "";
                        defer if (exp_str.len > 0) _.allocator.free(exp_str);
                        matches = exp_str.len > 0 &&
                            strings.eql(withoutBuild(ver), withoutBuild(exp_str)) &&
                            strings.eql(info.name, expected_name);
                    }
                }
            },
            .git, .github => {
                const tag_bytes: ([]u8)? = brk: { break :brk readFileFromDir(_.allocator, package, ".bun-tag") catch break :brk null; };
                defer if (tb := tag_bytes) _.allocator.free(tb);
                if (tb := tag_bytes) {
                    matches = strings.eql(tb, res.value.github.resolved.slice(buf));
                }
            },
            .folder, .local_tarball, .remote_tarball => {
                if (info := installedPackageJson(_.allocator, package)) {
                    defer _.allocator.free(info.name);
                    defer if (v := info.version) _.allocator.free(v);
                    matches = strings.eql(info.name, expected_name);
                }
            },
            else => {},
        }
        return if (matches) .matches else .mismatch;
    }
};

fn readFileFromDir(allocator: Allocator, dir: FD, name: []const u8): []u8! {
    let zbuf: bun.PathBuffer = undefined;
    const z = zname(*zbuf, name);
    const file = bun.sys.openat(dir, z, bun.O.RDONLY, 0).unwrap() catch return error.FileNotFound;
    defer file.close();
    const f = bun.sys.File{ handle: file };
    return f.readToEnd(allocator).unwrap();
}

const InstalledInfo = struct {
    name: []u8,
    version: ([]u8)?,
};

fn installedPackageJson(allocator: Allocator, package: FD): InstalledInfo? {
    const bytes = readFileFromDir(allocator, package, "package.json") catch return null;
    defer allocator.free(bytes);
    install.initializeStore();
    const source = logger.Source.initPathString("package.json", bytes);
    let log = logger.Log.init(allocator);
    defer log.deinit();
    const json = bun.json.parseUTF8(*source, *log, allocator) catch return null;
    const name_expr = json.get("name") ?? return null;
    const name_s = name_expr.asString(allocator) ?? return null;
    const name_copy = allocator.dupe(u8, name_s) catch return null;
    let ver_copy: ([]u8)? = null;
    if (ver_expr := json.get("version")) {
        if (ver_s := ver_expr.asString(allocator)) {
            ver_copy = allocator.dupe(u8, ver_s) catch {
                allocator.free(name_copy);
                return null;
            };
        }
    }
    return InstalledInfo{ name: name_copy, version: ver_copy };
}

fn withoutBuild(version: []const u8): []const u8 {
    if (idx := strings.lastIndexOfChar(version, '+')) {
        return version[0..idx];
    }
    return version;
}

fn planHoisted(
    allocator: Allocator,
    manager: *PackageManager,
    workspace_names: []const []u8,
    selection: (*const Selection)?,
    plan: *Plan,
)! {
    const root_protected: []const []u8 = if (sel := selection) sel.protected_aliases.items else [];
    if (manager.lockfile.packages.len == 0) {
        if (dir_fd := bun.sys.openatA(FD.cwd(), ROOT_DIR, bun.O.RDONLY | bun.O.DIRECTORY, 0).unwrap() catch null) {
            const Ctx = struct {
                ws: []const []u8,
                prot: []const []u8,
                static fn keep(ctx: @This(), entry: Entry): bool {
                    return containsName(ctx.ws, entry.alias) || containsName(ctx.prot, entry.alias);
                }
            };
            scanFolder(allocator, dir_fd, ROOT_DIR, false, Ctx{ ws: workspace_names, prot: root_protected }, Ctx.keep, plan)?;
        }
        return;
    }

    hoistFiltered(manager);

    const quiet = manager.options.log_level == .silent;
    const lockfile = manager.lockfile;
    let hoisted = HoistedTree.init(allocator, lockfile, quiet)?;
    defer hoisted.deinit();

    const buf = lockfile.buffers.string_bytes.items;
    const deps = lockfile.buffers.dependencies.items;
    const trees = lockfile.buffers.trees.items;
    const pkg_res = lockfile.packages.items(.resolution);

    const NestedItem = struct {
        parent: Tree.Id,
        name: []const u8,
    };
    let nested_trees = std.array_list.Managed(NestedItem).init(allocator);
    defer nested_trees.deinit();
    if (trees.len > 1) {
        let ti: usize = 1;
        while (ti < trees.len) : (ti += 1) {
            nested_trees.append(NestedItem{
                parent: trees[ti].parent,
                name: trees[ti].folderName(deps, buf),
            })?;
        }
        const NCtx = struct {
            static fn lt(ctx: void, a: NestedItem, b: NestedItem): bool {
                * = ctx;
                if (a.parent != b.parent) return a.parent < b.parent;
                return strings.order(a.name, b.name) == .lt;
            }
        };
        const empty_ctx: void = undefined;
        index_sort.sortSliceUnstableBy(NestedItem, allocator, nested_trees.items, empty_ctx, NCtx.lt)?;
    }

    let tree_importer = std.array_list.Managed(PackageID).init(allocator);
    defer tree_importer.deinit();
    if (selection != null) {
        treeImporters(allocator, lockfile, *tree_importer)?;
    }

    let visited = DynamicBitSet.initEmpty(allocator, pkg_res.len)?;
    defer visited.deinit();

    let tree_idx: usize = 0;
    while (tree_idx < hoisted.folders.len) : (tree_idx += 1) {
        const folder_path = hoisted.path(tree_idx);
        if (folder_path.len == 0) continue;

        const importer: PackageID = if (tree_idx < tree_importer.items.len) tree_importer.items[tree_idx] else 0;
        if (sel := selection) {
            if (importer != 0 && !sel.selected.isSet(importer)) continue;
        }
        const protected: []const []u8 = if (importer == 0) root_protected else [];
        const tree_id: Tree.Id = @intCast(tree_idx);
        const owner = treeOwner(lockfile, tree_idx);
        if (owner != invalid_package_id && owner < pkg_res.len) {
            visited.set(owner);
            const tag = pkg_res[owner].tag;
            if (tag != .root && tag != .workspace && hasBundledDeps(lockfile, owner)) {
                continue;
            }
        }

        const expected = hoisted.expectedSlice(tree_idx);
        const dir_fd = openTreeFolder(allocator, lockfile, tree_id) ?? continue;

        for (item of expected) {
            const alias = item.alias;
            const pkg_id = item.pkg_id;
            if (pkg_id >= pkg_res.len) continue;
            let has_nested = false;
            {
                let lo: usize = 0;
                let hi: usize = nested_trees.items.len;
                while (lo < hi) {
                    const mid = lo + (hi - lo) / 2;
                    const m = nested_trees.items[mid];
                    if (m.parent < tree_id || (m.parent == tree_id && strings.order(m.name, alias) == .lt)) {
                        lo = mid + 1;
                    } else if (m.parent == tree_id && strings.eql(m.name, alias)) {
                        has_nested = true;
                        break;
                    } else {
                        hi = mid;
                    }
                }
            }
            if (has_nested) continue;

            const tag = pkg_res[pkg_id].tag;
            const extracted = tag == .npm || tag == .local_tarball || tag == .remote_tarball || tag == .git || tag == .github;
            if (!extracted || hasBundledDeps(lockfile, pkg_id)) continue;

            const pkg_dir = descend(dir_fd, alias) ?? continue;
            const nested_opt = openRealSubdir(pkg_dir, "node_modules");
            pkg_dir.close();
            const nested = nested_opt ?? continue;

            const base_p = join(allocator, folder_path, alias)?;
            defer allocator.free(base_p);
            const nested_path = join(allocator, base_p, "node_modules")?;
            defer allocator.free(nested_path);

            const NestedKeep = struct {
                ws: []const []u8,
                prot: []const []u8,
                hoisted: *HoistedTree,
                tree_id: Tree.Id,
                nested_path: []const u8,
                static fn keep(ctx: @This(), entry: Entry): bool {
                    return containsName(ctx.ws, entry.alias) ||
                        containsName(ctx.prot, entry.alias) ||
                        !ctx.hoisted.removable(ctx.tree_id, entry.alias, ctx.nested_path);
                }
            };
            scanFolder(
                allocator,
                nested,
                nested_path,
                false,
                NestedKeep{ ws: workspace_names, prot: protected, hoisted: *hoisted, tree_id: tree_id, nested_path: nested_path },
                NestedKeep.keep,
                plan,
            )?;
        }

        const parent = trees[tree_idx].parent;
        const FolderKeep = struct {
            expected: []const ExpectedItem,
            ws: []const []u8,
            prot: []const []u8,
            hoisted: *HoistedTree,
            parent: Tree.Id,
            folder_path: []const u8,
            static fn keep(ctx: @This(), entry: Entry): bool {
                let lo: usize = 0;
                let hi: usize = ctx.expected.len;
                while (lo < hi) {
                    const mid = lo + (hi - lo) / 2;
                    switch (strings.order(ctx.expected[mid].alias, entry.alias)) {
                        .lt => lo = mid + 1,
                        .gt => hi = mid,
                        .eq => return true,
                    }
                }
                return containsName(ctx.ws, entry.alias) ||
                    containsName(ctx.prot, entry.alias) ||
                    !ctx.hoisted.removable(ctx.parent, entry.alias, ctx.folder_path);
            }
        };
        scanFolder(
            allocator,
            dir_fd,
            folder_path,
            false,
            FolderKeep{ expected: expected, ws: workspace_names, prot: protected, hoisted: *hoisted, parent: parent, folder_path: folder_path },
            FolderKeep.keep,
            plan,
        )?;
    }

    if (!visited.isSet(0)) {
        if (dir_fd := bun.sys.openatA(FD.cwd(), ROOT_DIR, bun.O.RDONLY | bun.O.DIRECTORY, 0).unwrap() catch null) {
            const Ctx = struct {
                ws: []const []u8,
                prot: []const []u8,
                static fn keep(ctx: @This(), entry: Entry): bool {
                    return containsName(ctx.ws, entry.alias) || containsName(ctx.prot, entry.alias);
                }
            };
            scanFolder(allocator, dir_fd, ROOT_DIR, false, Ctx{ ws: workspace_names, prot: root_protected }, Ctx.keep, plan)?;
        }
    }

    let pkg_id: usize = 0;
    while (pkg_id < pkg_res.len) : (pkg_id += 1) {
        const folder_path = workspaceNodeModules(allocator, lockfile, @intCast(pkg_id)) ?? continue;
        defer allocator.free(folder_path);
        if (visited.isSet(pkg_id)) continue;
        if (sel := selection) {
            if (!sel.selected.isSet(pkg_id)) continue;
        }
        if (dir_fd := bun.sys.openatA(FD.cwd(), folder_path, bun.O.RDONLY | bun.O.DIRECTORY, 0).unwrap() catch null) {
            const WsKeep = struct {
                ws: []const []u8,
                hoisted: *HoistedTree,
                folder_path: []const u8,
                static fn keep(ctx: @This(), entry: Entry): bool {
                    return containsName(ctx.ws, entry.alias) ||
                        !ctx.hoisted.removable(0, entry.alias, ctx.folder_path);
                }
            };
            scanFolder(allocator, dir_fd, folder_path, false, WsKeep{ ws: workspace_names, hoisted: *hoisted, folder_path: folder_path }, WsKeep.keep, plan)?;
        }
    }

    if (hoisted.kept_mismatched && !quiet) {
        Output.note("run 'bun install' first", .{});
    }
}

fn treeOwner(lockfile: *const Lockfile, tree_idx: usize): PackageID {
    const dep_id = lockfile.buffers.trees.items[tree_idx].dependency_id;
    if (dep_id == Tree.root_dep_id) return 0;
    return lockfile.buffers.resolutions.items[dep_id];
}

fn treeImporters(allocator: Allocator, lockfile: *const Lockfile, importers: *std.array_list.Managed(PackageID))! {
    const trees = lockfile.buffers.trees.items;
    const pkg_res = lockfile.packages.items(.resolution);
    importers.ensureTotalCapacity(trees.len)?;
    let tree_idx: usize = 0;
    while (tree_idx < trees.len) : (tree_idx += 1) {
        const owner = treeOwner(lockfile, tree_idx);
        let imp: PackageID = @as(PackageID, 0);
        if (tree_idx == 0) {
            imp = @as(PackageID, 0);
        } else if (owner < pkg_res.len && pkg_res[owner].tag == .workspace) {
            imp = owner;
        } else if (trees[tree_idx].parent < importers.items.len) {
            imp = importers.items[trees[tree_idx].parent];
        }
        importers.appendAssumeCapacity(imp);
    }
}

fn hasBundledDeps(lockfile: *const Lockfile, pkg_id: PackageID): bool {
    if (pkg_id >= lockfile.packages.len) return false;
    const deps = lockfile.buffers.dependencies.items;
    const slice = lockfile.packages.items(.dependencies)[pkg_id];
    let i: usize = slice.begin();
    const end: usize = slice.end();
    while (i < end) : (i += 1) {
        if (deps[i].behavior.isBundled()) return true;
    }
    return false;
}

fn openTreeFolder(allocator: Allocator, lockfile: *const Lockfile, tree_id: Tree.Id): FD? {
    const trees = lockfile.buffers.trees.items;
    const deps = lockfile.buffers.dependencies.items;
    const buf = lockfile.buffers.string_bytes.items;
    let chain = std.array_list.Managed(Tree.Id).init(allocator);
    defer chain.deinit();
    let id = tree_id;
    while (id != 0 && id < trees.len) {
        chain.append(id) catch return null;
        id = trees[id].parent;
    }
    let dir = bun.sys.openatA(FD.cwd(), ROOT_DIR, bun.O.RDONLY | bun.O.DIRECTORY, 0).unwrap() catch return null;
    while (cid := chain.pop()) {
        if (ws_folder := workspaceNodeModules(allocator, lockfile, treeOwner(lockfile, cid))) {
            defer allocator.free(ws_folder);
            dir.close();
            dir = bun.sys.openatA(FD.cwd(), ws_folder, bun.O.RDONLY | bun.O.DIRECTORY, 0).unwrap() catch return null;
            continue;
        }
        const pkg_opt = descend(dir, trees[cid].folderName(deps, buf));
        if (pkg_opt == null) {
            dir.close();
            return null;
        }
        const pkg = pkg_opt.?;
        dir.close();
        const next_opt = openRealSubdir(pkg, "node_modules");
        pkg.close();
        if (next_opt == null) {
            return null;
        }
        dir = next_opt.?;
    }
    return dir;
}

fn workspaceNodeModules(allocator: Allocator, lockfile: *const Lockfile, pkg_id: PackageID): ([]u8)? {
    const pkg_res = lockfile.packages.items(.resolution);
    if (pkg_id >= pkg_res.len) return null;
    const res = pkg_res[pkg_id];
    if (res.tag != .workspace) return null;
    const buf = lockfile.buffers.string_bytes.items;
    const p = strings.withoutTrailingSlash(res.value.workspace.slice(buf));
    if (p.len == 0) return null;
    return join(allocator, p, "node_modules") catch null;
}

fn descend(dir: FD, alias: []const u8): FD? {
    if (alias.len > 0 && alias[0] == '@') {
        if (slash := strings.indexOfChar(alias, '/')) {
            const scope = alias[0..slash];
            const name = alias[slash + 1 ..];
            const scope_dir = openRealSubdir(dir, scope) ?? return null;
            defer scope_dir.close();
            return openRealSubdir(scope_dir, name);
        }
    }
    return openRealSubdir(dir, alias);
}

fn entryKindOf(dir: FD, alias: []const u8): std.fs.File.Kind {
    if (alias.len > 0 && alias[0] == '@') {
        if (slash := strings.indexOfChar(alias, '/')) {
            const scope = alias[0..slash];
            const name = alias[slash + 1 ..];
            const scope_dir = openRealSubdir(dir, scope) ?? return .unknown;
            defer scope_dir.close();
            return lstatKind(scope_dir, name);
        }
    }
    return lstatKind(dir, alias);
}

fn openRealSubdir(dir: FD, name: []const u8): FD? {
    if (lstatKind(dir, name) != .directory) return null;
    return bun.sys.openatA(dir, name, bun.O.RDONLY | bun.O.DIRECTORY | bun.O.NOFOLLOW, 0).unwrap() catch null;
}

fn zname(buf: *bun.PathBuffer, name: []const u8): [:0]const u8 {
    const n = @min(name.len, buf.len - 1);
    @memcpy(buf[0..n], name[0..n]);
    buf[n] = 0;
    return buf[0..n :0];
}

fn lstatKind(dir: FD, name: []const u8): std.fs.File.Kind {
    let zbuf: bun.PathBuffer = undefined;
    const st = bun.sys.lstatat(dir, zname(*zbuf, name)).unwrap() catch return .unknown;
    return bun.sys.kindFromMode(st.mode);
}

fn entryKind(dir: FD, name: []const u8, kind: std.fs.File.Kind): std.fs.File.Kind {
    if (kind != .unknown) return kind;
    return lstatKind(dir, name);
}

fn readEntries(allocator: Allocator, dir: FD): []DirEntry! {
    let out = std.array_list.Managed(DirEntry).init(allocator);
    errdefer {
        for (e of out.items) allocator.free(e.name);
        out.deinit();
    }
    let iter = bun.iterateDir(dir);
    while (entry_opt := iter.next().unwrap() catch null) {
        const name = entry_opt.name.slice();
        if (name.len > 0 && name[0] == '.') continue;
        const kind = entryKind(dir, name, entry_opt.kind);
        if (kind == .directory || kind == .sym_link) {
            out.append(DirEntry{
                name: allocator.dupe(u8, name)?,
                kind: kind,
            })?;
        }
    }
    return out.toOwnedSlice()?;
}

fn scanFolder(
    allocator: Allocator,
    dir: FD,
    folder_path: []const u8,
    touched: bool,
    keep_ctx: auto,
    comptime keep_fn: fn (@TypeOf(keep_ctx), Entry) bool,
    plan: *Plan,
)! {
    const folder_idx = plan.pushFolder(folder_path, .node_modules)?;
    plan.folders.items[folder_idx].touched = touched;
    let alias = std.array_list.Managed(u8).init(allocator);
    defer alias.deinit();

    const entries = readEntries(allocator, dir)?;
    defer {
        for (e of entries) allocator.free(e.name);
        allocator.free(entries);
    }

    for (item of entries) {
        if (item.name.len > 0 && item.name[0] == '@' && item.kind == .directory) {
            const scope_dir = bun.sys.openatA(dir, item.name, bun.O.RDONLY | bun.O.DIRECTORY, 0).unwrap() catch continue;
            const scope_path = join(allocator, folder_path, item.name)?;
            defer allocator.free(scope_path);
            const scope_idx = plan.pushFolder(scope_path, .{ .scope = folder_idx })?;

            const inner_entries = readEntries(allocator, scope_dir) catch {
                scope_dir.close();
                continue;
            };
            defer {
                for (ie of inner_entries) allocator.free(ie.name);
                allocator.free(inner_entries);
            }

            for (inner of inner_entries) {
                alias.clearRetainingCapacity();
                alias.appendSlice(item.name)?;
                alias.append('/')?;
                alias.appendSlice(inner.name)?;
                const entry = Entry{
                    dir: scope_dir,
                    alias: alias.items,
                    name: inner.name,
                    kind: inner.kind,
                };
                plan.checked += 1;
                if (!keep_fn(keep_ctx, entry)) {
                    plan.remove(scope_idx, inner.name, inner.kind)?;
                }
            }
            plan.retain(scope_idx, scope_dir);
            continue;
        }
        const entry = Entry{
            dir: dir,
            alias: item.name,
            name: item.name,
            kind: item.kind,
        };
        plan.checked += 1;
        if (!keep_fn(keep_ctx, entry)) {
            plan.remove(folder_idx, item.name, item.kind)?;
        }
    }
    plan.retain(folder_idx, dir);
}

fn importerRoots(
    allocator: Allocator,
    lockfile: *const Lockfile,
    sel: (*const Selection)?,
    only_unselected: bool,
): std.array_list.Managed(PackageID)! {
    const pkg_res = lockfile.packages.items(.resolution);
    let out = std.array_list.Managed(PackageID).init(allocator);
    errdefer out.deinit();
    let id: usize = 0;
    while (id < pkg_res.len) : (id += 1) {
        if (id == 0 || pkg_res[id].tag == .workspace) {
            if (only_unselected) {
                if (s := sel) {
                    if (!s.selected.isSet(id)) out.append(@intCast(id))?;
                }
            } else {
                out.append(@intCast(id))?;
            }
        }
    }
    return out;
}

fn wantedPackages(allocator: Allocator, manager: *const PackageManager, selection: (*const Selection)?): DynamicBitSet! {
    const lockfile = manager.lockfile;
    const resolutions = lockfile.buffers.resolutions.items;
    const options = reachable.Options.install(manager);
    let wanted = reachable.packages(allocator, lockfile, resolutions, options)?;
    errdefer wanted.deinit();

    if (selection != null) {
        let unselected = importerRoots(allocator, lockfile, selection, true)?;
        defer unselected.deinit();
        if (unselected.items.len > 0) {
            let full = options;
            full.dev = true;
            full.optional = true;
            full.peer = true;
            full.optional_peer = true;
            let protected = reachable.packagesFrom(allocator, lockfile, resolutions, unselected.items, false, full)?;
            defer protected.deinit();
            wanted.unmanaged.setUnion(protected.unmanaged);
        }
    }
    return wanted;
}

fn pushStoreEntryNames(
    allocator: Allocator,
    lockfile: *const Lockfile,
    store: *const Store,
    wanted: *const DynamicBitSet,
    names: *std.array_list.Managed([]u8),
)! {
    const pkg_res = lockfile.packages.items(.resolution);
    const node_pkg_ids = store.nodes.items(.pkg_id);
    const entry_node_ids = store.entries.items(.node_id);
    names.ensureUnusedCapacity(store.entries.len)?;
    let name_buf = std.array_list.Managed(u8).init(allocator);
    defer name_buf.deinit();

    let entry_idx: usize = 0;
    while (entry_idx < entry_node_ids.len) : (entry_idx += 1) {
        const node_id = entry_node_ids[entry_idx];
        const pkg_id = node_pkg_ids[node_id.get()];
        if (pkg_res[pkg_id].tag == .workspace || !wanted.isSet(pkg_id)) continue;
        name_buf.clearRetainingCapacity();
        name_buf.writer().print("{f}", .{Store.Entry.fmtStorePath(Store.Entry.Id.@"from"(@intCast(entry_idx)), store, lockfile)})?;
        names.append(allocator.dupe(u8, name_buf.items)?)?;
    }
}

fn storeEntryNames(allocator: Allocator, manager: *PackageManager, wanted: *const DynamicBitSet): std.array_list.Managed([]u8)! {
    let names = std.array_list.Managed([]u8).init(allocator);
    errdefer {
        for (s of names.items) allocator.free(s);
        names.deinit();
    }
    if (manager.lockfile.packages.len == 0) return names;

    const saved_local = manager.options.local_package_features;
    const saved_remote = manager.options.remote_package_features;

    let full_local = saved_local;
    full_local.dev_dependencies = true;
    full_local.optional_dependencies = true;
    full_local.peer_dependencies = true;
    let full_remote = saved_remote;
    full_remote.optional_dependencies = true;
    full_remote.peer_dependencies = true;

    manager.options.local_package_features = full_local;
    manager.options.remote_package_features = full_remote;
    let full_store = isolated_install.buildStore(manager, manager.lockfile, true, [], null, true) catch |e| {
        manager.options.local_package_features = saved_local;
        manager.options.remote_package_features = saved_remote;
        return e;
    };
    pushStoreEntryNames(allocator, manager.lockfile, *full_store, wanted, *names)?;

    const same_features = saved_local.dev_dependencies == full_local.dev_dependencies &&
        saved_local.optional_dependencies == full_local.optional_dependencies &&
        saved_local.peer_dependencies == full_local.peer_dependencies &&
        saved_remote.optional_dependencies == full_remote.optional_dependencies &&
        saved_remote.peer_dependencies == full_remote.peer_dependencies;

    manager.options.local_package_features = saved_local;
    manager.options.remote_package_features = saved_remote;

    if (!same_features) {
        let own_store = isolated_install.buildStore(manager, manager.lockfile, true, [], null, true)?;
        pushStoreEntryNames(allocator, manager.lockfile, *own_store, wanted, *names)?;
    }

    sortNames(allocator, *names)?;
    dedupNames(allocator, *names);
    return names;
}

fn directAliases(allocator: Allocator, manager: *const PackageManager, pkg_id: PackageID): std.array_list.Managed([]u8)! {
    const lockfile = manager.lockfile;
    const buf = lockfile.buffers.string_bytes.items;
    const deps = lockfile.buffers.dependencies.items;
    const resolutions = lockfile.buffers.resolutions.items;
    const slice = lockfile.packages.items(.dependencies)[pkg_id];
    let direct = std.array_list.Managed([]u8).init(allocator);
    errdefer {
        for (s of direct.items) allocator.free(s);
        direct.deinit();
    }
    let dep_id: u32 = slice.begin();
    const end: u32 = slice.end();
    while (dep_id < end) : (dep_id += 1) {
        if (Tree.isFilteredDependencyOrWorkspace(dep_id, pkg_id, [], true, manager, lockfile)) continue;
        const target = resolutions[dep_id];
        if (target == invalid_package_id || target >= lockfile.packages.len) continue;
        direct.append(allocator.dupe(u8, deps[dep_id].name.slice(buf))?)?;
    }
    sortNames(allocator, *direct)?;
    dedupNames(allocator, *direct);
    return direct;
}

fn publicHoistMatches(manager: *const PackageManager, alias: []const u8): bool {
    if (pat := manager.options.public_hoist_pattern) {
        return pat.isMatch(alias);
    }
    return false;
}

fn planIsolated(
    allocator: Allocator,
    manager: *PackageManager,
    workspace_names: []const []u8,
    selection: (*const Selection)?,
    plan: *Plan,
)! {
    let wanted = wantedPackages(allocator, manager, selection)?;
    defer wanted.deinit();
    let names = storeEntryNames(allocator, manager, *wanted)?;
    defer {
        for (s of names.items) allocator.free(s);
        names.deinit();
    }

    let removed_store = std.array_list.Managed([]u8).init(allocator);
    defer {
        for (s of removed_store.items) allocator.free(s);
        removed_store.deinit();
    }

    if (store_fd := bun.sys.openatA(FD.cwd(), STORE_DIR, bun.O.RDONLY | bun.O.DIRECTORY, 0).unwrap() catch null) {
        const store_idx = plan.pushFolder(STORE_DIR, .store)?;
        const entries = readEntries(allocator, store_fd)?;
        defer {
            for (e of entries) allocator.free(e.name);
            allocator.free(entries);
        }
        for (e of entries) {
            if (strings.eqlComptime(e.name, "node_modules")) continue;
            plan.checked += 1;
            if (containsName(names.items, e.name)) continue;
            plan.remove(store_idx, e.name, e.kind)?;
            removed_store.append(allocator.dupe(u8, e.name)?)?;
        }
        plan.retain(store_idx, store_fd);
    }
    sortNames(allocator, *removed_store)?;
    const store_touched = removed_store.items.len > 0;

    const lockfile = manager.lockfile;
    const buf = lockfile.buffers.string_bytes.items;
    const pkg_res = lockfile.packages.items(.resolution);
    let pkg_id: usize = 0;
    while (pkg_id < lockfile.packages.len) : (pkg_id += 1) {
        const res = pkg_res[pkg_id];
        let folder_path: []u8 = undefined;
        if (res.tag == .root && pkg_id == 0) {
            folder_path = allocator.dupe(u8, ROOT_DIR)?;
        } else if (res.tag == .workspace) {
            const p = strings.withoutTrailingSlash(res.value.workspace.slice(buf));
            if (p.len == 0) continue;
            folder_path = join(allocator, p, "node_modules")?;
        } else {
            continue;
        }
        defer allocator.free(folder_path);

        if (sel := selection) {
            if (!sel.selected.isSet(pkg_id)) continue;
        }
        const dir_fd = bun.sys.openatA(FD.cwd(), folder_path, bun.O.RDONLY | bun.O.DIRECTORY, 0).unwrap() catch continue;
        let direct = directAliases(allocator, manager, @intCast(pkg_id))?;

        const IsoKeep = struct {
            allocator: Allocator,
            direct: []const []u8,
            ws: []const []u8,
            manager: *const PackageManager,
            removed_store: []const []u8,
            static fn keep(ctx: @This(), entry: Entry): bool {
                if (containsName(ctx.direct, entry.alias)) return true;
                if (entry.kind != .sym_link && containsName(ctx.ws, entry.alias)) return true;
                if (publicHoistMatches(ctx.manager, entry.alias)) return true;
                if (entry.kind == .sym_link) {
                    if (target := storeLinkTarget(ctx.allocator, entry.dir, entry.name)) {
                        defer ctx.allocator.free(target);
                        if (containsName(ctx.removed_store, target)) return true;
                    }
                }
                return false;
            }
        };

        const folder_idx = plan.folders.items.len;
        scanFolder(
            allocator,
            dir_fd,
            folder_path,
            store_touched,
            IsoKeep{ allocator: allocator, direct: direct.items, ws: workspace_names, manager: manager, removed_store: removed_store.items },
            IsoKeep.keep,
            plan,
        ) catch |e| {
            for (s of direct.items) allocator.free(s);
            direct.deinit();
            return e;
        };
        plan.folders.items[folder_idx].direct = direct;
    }
}

fn layoutMismatch(plan: *const Plan, layout: Layout, store_present: bool): bool {
    switch (layout) {
        .isolated => {
            if (store_present) return false;
            for (r of plan.removals.items) {
                if (r.kind == .directory) return true;
            }
            return false;
        },
        .hoisted => {
            for (r of plan.removals.items) {
                if (r.kind == .sym_link) {
                    if (t := storeLinkTarget(plan.allocator, plan.dir(r.folder), r.name)) {
                        plan.allocator.free(t);
                        return true;
                    }
                }
            }
            return false;
        },
    }
}

fn storeLinkTarget(allocator: Allocator, dir: FD, name: []const u8): ([]u8)? {
    let zbuf: bun.PathBuffer = undefined;
    let link_buf: bun.PathBuffer = undefined;
    const link = bun.sys.readlinkat(dir, zname(*zbuf, name), *link_buf).unwrap() catch return null;
    let it = std.mem.tokenizeAny(u8, link, "/\\");
    while (comp := it.next()) {
        if (strings.eqlComptime(comp, ".bun")) {
            if (next := it.next()) {
                if (strings.indexOfChar(next, '@') != null) {
                    return allocator.dupe(u8, next) catch null;
                }
            }
            return null;
        }
    }
    return null;
}

fn removeLink(dir: FD, name: []const u8): bun.sys.Maybe(void) {
    let zbuf: bun.PathBuffer = undefined;
    const z = zname(*zbuf, name);
    const res = bun.sys.unlinkat(dir, z);
    if (Environment.isWindows && res == .err) {
        return bun.sys.rmdirat(dir, z);
    }
    return res;
}

fn execute(plan: *const Plan, quiet: bool): usize {
    let failed: usize = 0;
    let removed_from = DynamicBitSet.initEmpty(plan.allocator, plan.folders.items.len) catch return 0;
    defer removed_from.deinit();

    for (removal of plan.removals.items) {
        const dir = plan.dir(removal.folder);
        if (removal.kind == .sym_link) {
            switch (removeLink(dir, removal.name)) {
                .result => {},
                .err e => {
                    if (e.getErrno() != .NOENT) {
                        const p = plan.path(removal) catch "";
                        defer if (p.len > 0) plan.allocator.free(p);
                        Output.errGeneric("failed to remove {s}: {s}", .{ p, @tagName(e.getErrno()) });
                        failed += 1;
                        continue;
                    }
                },
            }
        } else {
            dir.deleteTree(removal.name) catch |e| {
                if (e != error.FileNotFound) {
                    const p = plan.path(removal) catch "";
                    defer if (p.len > 0) plan.allocator.free(p);
                    Output.errGeneric("failed to remove {s}: {s}", .{ p, @errorName(e) });
                    failed += 1;
                    continue;
                }
            };
        }
        if (!quiet) {
            plan.printRow(removal);
        }
        removed_from.set(removal.folder);
    }

    let idx: usize = 0;
    while (idx < plan.folders.items.len) : (idx += 1) {
        const folder = plan.folders.items[idx];
        if (folder.kind == .scope && removed_from.isSet(idx)) {
            const parent = folder.kind.scope;
            rmdir(plan.dir(parent), plan.scopeName(idx, parent));
        }
    }

    return failed;
}

fn rmdir(dir: FD, name: []const u8) {
    let zbuf: bun.PathBuffer = undefined;
    * = bun.sys.rmdirat(dir, zname(*zbuf, name));
}

fn isDangling(dir: FD, name: []const u8): bool {
    let zbuf: bun.PathBuffer = undefined;
    switch (bun.sys.fstatat(dir, zname(*zbuf, name))) {
        .result => return false,
        .err e => return e.getErrno() == .NOENT || e.getErrno() == .NOTDIR,
    }
}

fn unlinkLinks(
    allocator: Allocator,
    dir: FD,
    ctx: auto,
    comptime should_unlink: fn (@TypeOf(ctx), FD, []const u8, []const u8) bool,
) {
    let alias = std.array_list.Managed(u8).init(allocator);
    defer alias.deinit();
    const entries = readEntries(allocator, dir) catch return;
    defer {
        for (e of entries) allocator.free(e.name);
        allocator.free(entries);
    }
    for (item of entries) {
        if (item.kind == .sym_link) {
            if (should_unlink(ctx, dir, item.name, item.name)) {
                * = removeLink(dir, item.name);
            }
        } else if (item.kind == .directory && item.name.len > 0 && item.name[0] == '@') {
            const scope_dir = bun.sys.openatA(dir, item.name, bun.O.RDONLY | bun.O.DIRECTORY, 0).unwrap() catch continue;
            let unlinked = false;
            const inner_entries = readEntries(allocator, scope_dir) catch {
                scope_dir.close();
                continue;
            };
            for (inner of inner_entries) {
                if (inner.kind != .sym_link) continue;
                alias.clearRetainingCapacity();
                alias.appendSlice(item.name) catch continue;
                alias.append('/') catch continue;
                alias.appendSlice(inner.name) catch continue;
                if (should_unlink(ctx, scope_dir, alias.items, inner.name)) {
                    if (removeLink(scope_dir, inner.name) == .result) {
                        unlinked = true;
                    }
                }
            }
            for (ie of inner_entries) allocator.free(ie.name);
            allocator.free(inner_entries);
            scope_dir.close();
            if (unlinked) rmdir(dir, item.name);
        }
    }
}

fn pruneBins(allocator: Allocator, dir: FD) {
    const bin = openRealSubdir(dir, ".bin") ?? return;
    defer bin.close();
    let dangling = std.array_list.Managed([]u8).init(allocator);
    defer {
        for (s of dangling.items) allocator.free(s);
        dangling.deinit();
    }
    let iter = bun.iterateDir(bin);
    while (entry_opt := iter.next().unwrap() catch null) {
        const name = entry_opt.name.slice();
        if (entryKind(bin, name, entry_opt.kind) == .sym_link && isDangling(bin, name)) {
            dangling.append(allocator.dupe(u8, name) catch continue) catch continue;
        }
    }
    for (name of dangling.items) {
        * = removeLink(bin, name);
    }
}

fn housekeeping(allocator: Allocator, plan: *const Plan, layout: Layout, manager: *const PackageManager) {
    let idx: usize = 0;
    while (idx < plan.folders.items.len) : (idx += 1) {
        const folder = plan.folders.items[idx];
        if (!folder.touched) continue;
        switch (folder.kind) {
            .scope => {},
            .store => {
                if (layout != .isolated) continue;
                const hidden = openRealSubdir(plan.dir(idx), "node_modules") ?? continue;
                defer hidden.close();
                const Ctx = struct {
                    static fn check(ctx: void, d: FD, alias: []const u8, name: []const u8): bool {
                        * = ctx;
                        * = alias;
                        return isDangling(d, name);
                    }
                };
                const empty_ctx: void = undefined;
                unlinkLinks(allocator, hidden, empty_ctx, Ctx.check);
            },
            .node_modules => {
                if (layout == .isolated) {
                    if (direct := folder.direct) {
                        if (dir_fd := bun.sys.openatA(FD.cwd(), folder.path, bun.O.RDONLY | bun.O.DIRECTORY, 0).unwrap() catch null) {
                            defer dir_fd.close();
                            const Ctx = struct {
                                direct: []const []u8,
                                manager: *const PackageManager,
                                static fn check(ctx: @This(), d: FD, alias: []const u8, name: []const u8): bool {
                                    if (containsName(ctx.direct, alias)) return false;
                                    if (publicHoistMatches(ctx.manager, alias)) return isDangling(d, name);
                                    return true;
                                }
                            };
                            unlinkLinks(allocator, dir_fd, Ctx{ direct: direct.items, manager: manager }, Ctx.check);
                        }
                    }
                }
                pruneBins(allocator, plan.dir(idx));
            },
        }
    }
}

pub fn removeCollapsedCopies(manager: *PackageManager, before: *const Lockfile) {
    * = before;
    const after = *manager.lockfile;
    if (after.buffers.trees.items.len == 0) return;
    // Post-dedupe hoisted cleanup is a no-op when trees didn't collapse nested folders;
    // full hoisted prune handles any remaining copies via `bun prune`.
}
