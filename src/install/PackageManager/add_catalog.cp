import bun from "bun";
import install from "../install.zig";
import Lockfile from "../lockfile.zig";
import Dependency from "../dependency.zig";
import PackageManager from "../PackageManager.zig";
import PackageJSONEditor from "./PackageJSONEditor.zig";
import UpdateRequest from "./UpdateRequest.zig";
import add_remove_with_filter from "./add_remove_with_filter.zig";
import WorkspacePackageJSONCache from "./WorkspacePackageJSONCache.zig";

const Allocator = std.mem.Allocator;
const Global = bun.Global;
const Output = bun.Output;
const Semver = bun.Semver;
const SlicedString = Semver.SlicedString;
const js_ast = bun.ast;
const Expr = js_ast.Expr;
const E = js_ast.E;
const G = js_ast.G;
const logger = bun.logger;
const strings = bun.strings;
const PackageID = install.PackageID;
const PackageNameHash = install.PackageNameHash;
const invalid_package_id = install.invalid_package_id;
const WorkspaceMembers = add_remove_with_filter.WorkspaceMembers;
const WorkspaceTarget = add_remove_with_filter.WorkspaceTarget;
const EditOptions = PackageJSONEditor.EditOptions;

const dependency_groups: [4][]const u8 = [
    "dependencies",
    "devDependencies",
    "optionalDependencies",
    "peerDependencies",
];

pub struct RootEntry {
    name: []const u8,
    text: []const u8,
}

pub union(enum) Candidate {
    explicit: []const u8,
    existing: []const u8,
    resolved: []const u8,
    latest: void,

    fn literal(): []const u8 {
        return switch (_) {
            .explicit s => s,
            .existing s => s,
            .resolved s => s,
            .latest => "latest",
        };
    }
}

pub union(enum) Outcome {
    pending: void,
    reused: void,
    seeded: void,
    moved: []const u8,
}

enum Decision {
    convert,
    keep,
}

pub struct Add {
    name: []const u8,
    name_hash: PackageNameHash,
    group: []const u8,
    candidate: Candidate,
    outcome: Outcome,
    targets: std.array_list.Managed([]const u8),
}

pub struct State {
    enabled: bool = false,
    auto_use: std.array_list.Managed(PackageNameHash),
    adds: std.array_list.Managed(Add),
    root_entries: std.array_list.Managed(RootEntry),
    members: std.array_list.Managed(WorkspaceTarget),

    static fn init(allocator: Allocator): State {
        return State{
            enabled: false,
            auto_use: std.array_list.Managed(PackageNameHash).init(allocator),
            adds: std.array_list.Managed(Add).init(allocator),
            root_entries: std.array_list.Managed(RootEntry).init(allocator),
            members: std.array_list.Managed(WorkspaceTarget).init(allocator),
        };
    }

    fn deinit(*_) {
        _.auto_use.deinit();
        for (*a of _.adds.items) {
            a.targets.deinit();
        }
        _.adds.deinit();
        _.root_entries.deinit();
        _.members.deinit();
    }
}

fn sameCatalogName(a: []const u8, b: []const u8): bool {
    const na = if (std.mem.eql(u8, a, "default")) "" else a;
    const nb = if (std.mem.eql(u8, b, "default")) "" else b;
    return std.mem.eql(u8, na, nb);
}

fn rootEntry(root_entries: []const RootEntry, name: []const u8): ([]const u8)? {
    for (entry of root_entries) {
        if (std.mem.eql(u8, entry.name, name)) {
            return entry.text;
        }
    }
    return null;
}

fn rootTarget(allocator: Allocator): WorkspaceTarget {
    return WorkspaceTarget{
        name: "",
        name_hash: null,
        package_json_path: add_remove_with_filter.rootPackageJsonPath(allocator),
    };
}

fn targetLabel(allocator: Allocator, package_json: *const Expr): []const u8 {
    if (name_expr := package_json.get("name")) {
        if (name := name_expr.asString(allocator)) {
            if (name.len > 0) return name;
        }
    }
    return "package.json";
}

fn noteFollows(quiet: bool, name: []const u8, target: []const u8, entry: []const u8, declared: []const u8) {
    if (quiet) return;
    Output.note("{s} in {s} now follows the catalog entry \"{s}\" instead of \"{s}\"", .{
        name,
        target,
        entry,
        declared,
    });
    Output.flush();
}

fn noteKeeps(quiet: bool, name: []const u8, target: []const u8, declared: []const u8, entry: []const u8) {
    if (quiet) return;
    Output.note("{s} in {s} keeps \"{s}\" because the catalog entry is \"{s}\"", .{
        name,
        target,
        declared,
        entry,
    });
    Output.flush();
}

fn noteReplaced(allocator: Allocator, name: []const u8, old_ver: []const u8, to: []const u8, also_used_by: []const []const u8) {
    let suffix = std.array_list.Managed(u8).init(allocator);
    defer suffix.deinit();
    let i: usize = 0;
    for (user of also_used_by) {
        suffix.appendSlice(if (i == 0) " (also used by " else ", ") catch bun.outOfMemory();
        suffix.appendSlice(user) catch bun.outOfMemory();
        i += 1;
    }
    if (also_used_by.len > 0) {
        suffix.append(')') catch bun.outOfMemory();
    }
    Output.note("catalog entry {s} changed from \"{s}\" to \"{s}\"{s}", .{
        name,
        old_ver,
        to,
        suffix.items,
    });
    Output.flush();
}

fn refuseDeclared(allocator: Allocator, literal: []const u8, target: []const u8, name: []const u8, flag: []const u8): noreturn {
    let flag_spelling = std.array_list.Managed(u8).init(allocator);
    defer flag_spelling.deinit();
    flag_spelling.appendSlice("--catalog") catch bun.outOfMemory();
    if (flag.len > 0) {
        flag_spelling.append('=') catch bun.outOfMemory();
        flag_spelling.appendSlice(flag) catch bun.outOfMemory();
    }
    Output.flush();
    Output.errGeneric(
        "--catalog cannot add \"{s}\": {s} already declares {s}\n  bun add {s}@{s} {s}",
        .{ literal, target, name, name, literal, flag_spelling.items },
    );
    Global.crash();
}

fn estring(allocator: Allocator, bytes: []const u8): Expr {
    const copied = allocator.dupe(u8, bytes) catch bun.outOfMemory();
    return Expr.init(E.String, E.String{ data: copied }, logger.Loc.Empty);
}

fn referenceLiteral(allocator: Allocator, name: []const u8): []const u8 {
    if (name.len == 0) return "catalog:";
    let literal = std.array_list.Managed(u8).init(allocator);
    literal.ensureTotalCapacity("catalog:".len + name.len) catch bun.outOfMemory();
    literal.appendSliceAssumeCapacity("catalog:");
    literal.appendSliceAssumeCapacity(name);
    return literal.toOwnedSlice() catch bun.outOfMemory();
}

fn appendProperty(allocator: Allocator, obj: *E.Object, key: Expr, value: Expr) {
    const old_len = obj.properties.len;
    let new_props = allocator.alloc(G.Property, old_len + 1) catch bun.outOfMemory();
    if (old_len > 0) {
        @memcpy(new_props[0..old_len], obj.properties.slice());
    }
    new_props[old_len] = G.Property{
        key: key,
        value: value,
    };
    obj.properties = G.Property.List.fromOwnedSlice(new_props);
}

fn setProperty(allocator: Allocator, object: Expr, key: []const u8, value: Expr) {
    if (object.data != .e_object) return;
    const obj = object.data.e_object;
    if (q := obj.asProperty(key)) {
        obj.properties.slice()[q.i].value = value;
    } else {
        appendProperty(allocator, obj, estring(allocator, key), value);
    }
}

fn objectProperty(allocator: Allocator, container: Expr, key: []const u8, create: bool): Expr? {
    if (container.data != .e_object) return null;
    const obj = container.data.e_object;
    const existing = obj.asProperty(key);
    if (q := existing) {
        if (q.expr.data == .e_object) {
            return q.expr;
        }
    }
    if (!create) return null;
    const created = Expr.init(E.Object, E.Object{ properties: .empty }, logger.Loc.Empty);
    if (q := existing) {
        obj.properties.slice()[q.i].value = created;
    } else {
        appendProperty(allocator, obj, estring(allocator, key), created);
    }
    return created;
}

fn hasCatalogs(expr: *const Expr): bool {
    return expr.get("catalog") != null || expr.get("catalogs") != null;
}

fn rootDefinesCatalogs(root: *const Expr): bool {
    const workspaces = root.get("workspaces") ?? return false;
    return hasCatalogs(&workspaces) || hasCatalogs(root);
}

fn catalogsContainer(root: *const Expr): Expr? {
    const workspaces = root.get("workspaces") ?? return null;
    const workspaces_is_object = workspaces.data == .e_object;
    if (workspaces_is_object && hasCatalogs(&workspaces)) {
        return workspaces;
    }
    if (hasCatalogs(root) || !workspaces_is_object) {
        return root.*;
    }
    return workspaces;
}

fn defaultGroupObjects(allocator: Allocator, container: Expr, group: []const u8): [2]Expr? {
    const singular = objectProperty(allocator, container, "catalog", false);
    let in_catalogs: Expr? = null;
    if (cats := objectProperty(allocator, container, "catalogs", false)) {
        in_catalogs = objectProperty(allocator, cats, "default", false);
    }
    if (group.len == 0) {
        return [ singular, in_catalogs ];
    }
    return [ in_catalogs, singular ];
}

fn entriesObject(
    allocator: Allocator,
    root: *const Expr,
    group: []const u8,
    dep: []const u8,
    create: bool,
): Expr? {
    let container: Expr = undefined;
    if (c := catalogsContainer(root)) {
        container = c;
    } else {
        if (create) {
            Output.errGeneric("--catalog requires a \"workspaces\" field in the root package.json", .{});
            Global.crash();
        }
        return null;
    }

    if (!sameCatalogName(group, "")) {
        const catalogs = objectProperty(allocator, container, "catalogs", create) ?? return null;
        return objectProperty(allocator, catalogs, group, create);
    }

    const candidates = defaultGroupObjects(allocator, container, group);
    for (cand_opt of candidates) {
        if (obj := cand_opt) {
            if (obj.asProperty(dep) != null) return obj;
        }
    }
    for (cand_opt of candidates) {
        if (obj := cand_opt) {
            return obj;
        }
    }
    return objectProperty(allocator, container, "catalog", create);
}

fn requestLiteral(request: *const UpdateRequest): []const u8 {
    return request.version.literal.slice(request.version_buf);
}

fn hasExplicitVersion(request: *const UpdateRequest): bool {
    return request.version.tag != .uninitialized && requestLiteral(request).len > 0;
}

fn collectRootEntries(allocator: Allocator, root: *const Expr, group: []const u8, out: *std.array_list.Managed(RootEntry)) {
    const container = catalogsContainer(root) ?? return;
    let objects: [2]Expr? = [ null, null ];
    if (sameCatalogName(group, "")) {
        objects = defaultGroupObjects(allocator, container, group);
    } else {
        if (cats := objectProperty(allocator, container, "catalogs", false)) {
            objects[0] = objectProperty(allocator, cats, group, false);
        }
    }
    for (obj_opt of objects) {
        const obj_expr = obj_opt ?? continue;
        if (obj_expr.data != .e_object) continue;
        for (prop of obj_expr.data.e_object.properties.slice()) {
            const key = if (k := prop.key) (k.asString(allocator) ?? continue) else continue;
            const val = if (v := prop.value) (v.asString(allocator) ?? continue) else continue;
            out.append(RootEntry{
                name: allocator.dupe(u8, key) catch bun.outOfMemory(),
                text: allocator.dupe(u8, val) catch bun.outOfMemory(),
            }) catch bun.outOfMemory();
        }
    }
}

fn memberTargets(allocator: Allocator, ws: *const WorkspaceMembers, out: *std.array_list.Managed(WorkspaceTarget)) {
    const top_level = strings.withoutTrailingSlash(bun.fs.FileSystem.instance.top_level_dir);
    let buf = bun.path_buffer_pool.get();
    defer bun.path_buffer_pool.put(buf);
    const keys = ws.members.keys();
    const vals = ws.members.values();
    let idx: usize = 0;
    while (idx < keys.len) : (idx += 1) {
        const rel = keys[idx];
        const entry = vals[idx];
        const joined = bun.path.joinAbsStringBuf(top_level, buf, &.{ rel, "package.json" }, .auto);
        out.append(WorkspaceTarget{
            name: allocator.dupe(u8, entry.name) catch bun.outOfMemory(),
            name_hash: Semver.String.Builder.stringHash(entry.name),
            package_json_path: allocator.dupe(u8, joined) catch bun.outOfMemory(),
        }) catch bun.outOfMemory();
    }
}

pub fn prepare(manager: *PackageManager, updates: []const UpdateRequest) {
    if (manager.subcommand != .add || manager.options.global || updates.len == 0) return;

    const allocator = manager.allocator;
    if (group := manager.options.add_catalog) {
        for (request of updates) {
            if (add_remove_with_filter.localRelativePath(&request) != null) {
                if (request.is_aliased) {
                    Output.errGeneric(
                        "--catalog cannot add \"{s}@{s}\": a local path in the catalog would resolve from the workspace root, not from the package that added it",
                        .{ request.name, requestLiteral(&request) },
                    );
                } else {
                    Output.errGeneric(
                        "--catalog cannot add \"{s}\": a local path in the catalog would resolve from the workspace root, not from the package that added it",
                        .{requestLiteral(&request)},
                    );
                }
                Global.crash();
            }
            if (request.version.tag == .workspace) {
                Output.errGeneric(
                    "--catalog cannot add a workspace package, but got \"{s}@{s}\"",
                    .{ request.name, requestLiteral(&request) },
                );
                Global.crash();
            }
        }
        let ws = add_remove_with_filter.loadWorkspaceMembers(manager);
        defer ws.deinit(allocator);
        for (request of updates) {
            if (!request.is_aliased) continue;
            if (std.mem.eql(u8, ws.root_name, request.name)) {
                Output.errGeneric(
                    "--catalog cannot add a workspace package, but \"{s}\" is the workspace root",
                    .{request.name},
                );
                Global.crash();
            }
            const keys = ws.members.keys();
            const vals = ws.members.values();
            let idx: usize = 0;
            while (idx < keys.len) : (idx += 1) {
                if (std.mem.eql(u8, vals[idx].name, request.name)) {
                    Output.errGeneric(
                        "--catalog cannot add a workspace package, but \"{s}\" is the workspace at {s}",
                        .{ request.name, keys[idx] },
                    );
                    Global.crash();
                }
            }
        }
        const rt = rootTarget(allocator);
        defer allocator.free(rt.package_json_path);
        const root = add_remove_with_filter.fetchEntryRoot(manager, &rt);
        if (root.get("workspaces") == null) {
            Output.errGeneric("--catalog requires a \"workspaces\" field in the root package.json", .{});
            Global.crash();
        }
        collectRootEntries(allocator, &root, group, &manager.catalog_add.root_entries);
        memberTargets(allocator, &ws, &manager.catalog_add.members);
        manager.catalog_add.enabled = true;
        return;
    }

    const rt = rootTarget(allocator);
    defer allocator.free(rt.package_json_path);
    const root = add_remove_with_filter.fetchEntryRoot(manager, &rt);
    if (!rootDefinesCatalogs(&root)) return;
    manager.catalog_add.enabled = true;
    for (request of updates) {
        if (!request.is_aliased) continue;
        const entries = entriesObject(allocator, &root, "", request.name, false) ?? continue;
        const q = entries.asProperty(request.name) ?? continue;
        const entry_text = q.expr.asString(allocator) ?? continue;
        if (!hasExplicitVersion(&request) || std.mem.eql(u8, requestLiteral(&request), entry_text)) {
            manager.catalog_add.auto_use.append(request.name_hash) catch bun.outOfMemory();
        }
    }
}

fn existingSlot(allocator: Allocator, package_json: *const Expr, name: []const u8): ([]const u8)? {
    for (group_name of dependency_groups) {
        const list = package_json.get(group_name) ?? continue;
        if (list.data != .e_object) continue;
        const q = list.asProperty(name) ?? continue;
        if (s := q.expr.asString(allocator)) return s;
    }
    return null;
}

fn catalogGroupOf(reference: []const u8): []const u8 {
    return strings.trim(reference["catalog:".len..], &strings.whitespace_chars);
}

fn referencesFlagGroup(slot: []const u8, flag: []const u8): bool {
    return Dependency.Version.Tag.infer(slot) == .catalog && sameCatalogName(catalogGroupOf(slot), flag);
}

fn findAdd(adds: []Add, name_hash: PackageNameHash, group: []const u8): (*Add)? {
    for (*add of adds) {
        if (add.name_hash == name_hash && sameCatalogName(add.group, group)) {
            return add;
        }
    }
    return null;
}

fn catalogableVerbatim(literal: []const u8): bool {
    return switch (Dependency.Version.Tag.infer(literal)) {
        .npm, .dist_tag, .git, .github => true,
        .tarball => Dependency.isRemoteTarball(literal),
        else => false,
    };
}

fn pushAdd(allocator: Allocator, state: *State, request: *const UpdateRequest, group: []const u8, candidate: Candidate) {
    state.adds.append(Add{
        name: allocator.dupe(u8, request.name) catch bun.outOfMemory(),
        name_hash: request.name_hash,
        group: allocator.dupe(u8, group) catch bun.outOfMemory(),
        candidate: candidate,
        outcome: .pending,
        targets: std.array_list.Managed([]const u8).init(allocator),
    }) catch bun.outOfMemory();
}

fn recordAdd(
    allocator: Allocator,
    state: *State,
    request: *const UpdateRequest,
    group: []const u8,
    declared_opt: ([]const u8)?,
    target: []const u8,
    quiet: bool,
): Decision {
    if (hasExplicitVersion(request)) {
        if (findAdd(state.adds.items, request.name_hash, group) == null) {
            const lit = allocator.dupe(u8, requestLiteral(request)) catch bun.outOfMemory();
            pushAdd(allocator, state, request, group, Candidate{ explicit: lit });
        }
        const add = findAdd(state.adds.items, request.name_hash, group).?;
        add.targets.append(allocator.dupe(u8, target) catch bun.outOfMemory()) catch bun.outOfMemory();
        return .convert;
    }

    let declared: ([]const u8)? = null;
    if (d := declared_opt) {
        if (catalogableVerbatim(d)) declared = d;
    }

    const cand = if (d := declared) Candidate{ existing: allocator.dupe(u8, d) catch bun.outOfMemory() } else Candidate.latest;

    if (d := declared) {
        if (entry := rootEntry(state.root_entries.items, request.name)) {
            if (!std.mem.eql(u8, d, entry)) {
                noteFollows(quiet, request.name, target, entry, d);
            }
            if (findAdd(state.adds.items, request.name_hash, group) == null) {
                pushAdd(allocator, state, request, group, cand);
            }
            return .convert;
        }
    }

    let add: *Add = undefined;
    if (a := findAdd(state.adds.items, request.name_hash, group)) {
        add = a;
    } else {
        pushAdd(allocator, state, request, group, cand);
        return .convert;
    }

    switch (add.candidate) {
        .latest => {
            if (declared != null) {
                add.candidate = cand;
            }
            return .convert;
        },
        .existing => |seed| {
            if (d := declared) {
                if (std.mem.eql(u8, d, seed)) return .convert;
                if (exactWithin(d, seed)) {
                    noteFollows(quiet, request.name, target, seed, d);
                    return .convert;
                }
                noteKeeps(quiet, request.name, target, d, seed);
                return .keep;
            }
            return .convert;
        },
        else => return .convert,
    }
}

fn dropAlreadyCataloged(
    allocator: Allocator,
    updates: *[]UpdateRequest,
    package_json: *const Expr,
    root_entries: []const RootEntry,
    flag: []const u8,
) {
    let i: usize = 0;
    while (i < updates.len) {
        const request = updates.*[i];
        let already = false;
        if (!request.is_aliased) {
            for (entry of root_entries) {
                if (std.mem.eql(u8, entry.text, requestLiteral(&request))) {
                    if (slot := existingSlot(allocator, package_json, entry.name)) {
                        if (referencesFlagGroup(slot, flag)) {
                            already = true;
                            break;
                        }
                    }
                }
            }
        }
        if (!already) {
            i += 1;
            continue;
        }
        const last = updates.len - 1;
        const tmp = updates.*[i];
        updates.*[i] = updates.*[last];
        updates.*[last] = tmp;
        updates.* = updates.*[0..last];
    }
}

fn keysNamed(allocator: Allocator, package_json: *const Expr, dependency_list: []const u8, name: []const u8): usize {
    const list = package_json.get(dependency_list) ?? return 0;
    if (list.data != .e_object) return 0;
    let count: usize = 0;
    for (prop of list.data.e_object.properties.slice()) {
        if (k := prop.key) {
            if (ks := k.asString(allocator)) {
                if (std.mem.eql(u8, ks, name)) count += 1;
            }
        }
    }
    return count;
}

pub fn refuseDeclaredPositionals(manager: *const PackageManager) {
    const flag = manager.options.add_catalog ?? return;
    if (!manager.catalog_add.enabled) return;

    const lockfile = manager.lockfile;
    const buf = lockfile.buffers.string_bytes.items;
    const names = lockfile.packages.items(.name);
    const resolutions = lockfile.packages.items(.resolution);
    const dependency_lists = lockfile.packages.items(.dependencies);
    const dependencies = lockfile.buffers.dependencies.items;

    for (request of manager.update_requests) {
        if (request.is_aliased) continue;
        const literal = requestLiteral(&request);
        let pkg_id: usize = 0;
        while (pkg_id < dependency_lists.len) : (pkg_id += 1) {
            if (resolutions[pkg_id].tag != .root && resolutions[pkg_id].tag != .workspace) continue;
            const deps = dependency_lists[pkg_id].get(dependencies);
            let found_row: Dependency? = null;
            for (dep of deps) {
                if (std.mem.eql(u8, dep.version.literal.slice(buf), literal)) {
                    found_row = dep;
                    break;
                }
            }
            const row = found_row ?? continue;
            const name = row.name.slice(buf);
            if (std.mem.eql(u8, name, literal)) continue;
            let count: usize = 0;
            for (dep of deps) {
                if (dep.name_hash == row.name_hash && @as(u8, @bitCast(dep.behavior)) == @as(u8, @bitCast(row.behavior))) {
                    count += 1;
                }
            }
            if (count > 1) {
                const target_name = names[pkg_id].slice(buf);
                const target_label = if (target_name.len == 0) "package.json" else target_name;
                refuseDeclared(manager.allocator, literal, target_label, name, flag);
            }
        }
    }
}

fn settleResolvedPositional(
    allocator: Allocator,
    state: *State,
    request: *const UpdateRequest,
    lockfile: *const Lockfile,
    package_json: *const Expr,
    dependency_list: []const u8,
    flag: []const u8,
    e_string: *E.String,
    target: []const u8,
    quiet: bool,
): Decision {
    const name = request.getResolvedName(lockfile);
    const name_hash = lockfile.packages.items(.name_hash)[request.package_id];
    const literal = e_string.data;

    if (keysNamed(allocator, package_json, dependency_list, name) > 1) {
        refuseDeclared(allocator, literal, target, name, flag);
    }

    let entry: []const u8 = literal;
    if (re := rootEntry(state.root_entries.items, name)) {
        entry = re;
    } else {
        let found_idx: usize? = null;
        let idx: usize = 0;
        while (idx < state.adds.items.len) : (idx += 1) {
            const add = state.adds.items[idx];
            if (std.mem.eql(u8, add.name, name) && sameCatalogName(add.group, flag)) {
                found_idx = idx;
                break;
            }
        }
        if (fi := found_idx) {
            entry = state.adds.items[fi].candidate.literal();
        } else {
            state.adds.append(Add{
                name: allocator.dupe(u8, name) catch bun.outOfMemory(),
                name_hash: name_hash,
                group: allocator.dupe(u8, flag) catch bun.outOfMemory(),
                candidate: Candidate{ resolved: allocator.dupe(u8, literal) catch bun.outOfMemory() },
                outcome: .pending,
                targets: std.array_list.Managed([]const u8).init(allocator),
            }) catch bun.outOfMemory();
        }
    }

    if (!std.mem.eql(u8, entry, literal)) {
        noteKeeps(quiet, name, target, literal, entry);
        return .keep;
    }
    return .convert;
}

struct Captured {
    name_hash: PackageNameHash,
    slot: []const u8,
}

pub fn editTarget(
    manager: *PackageManager,
    updates: *[]UpdateRequest,
    package_json: *Expr,
    dependency_list: []const u8,
    options: EditOptions,
)! {
    if (!manager.catalog_add.enabled) {
        PackageJSONEditor.edit(manager, updates, package_json, dependency_list, options)?;
        return;
    }
    const allocator = manager.allocator;
    const quiet = manager.options.log_level == .silent;
    const flag = manager.options.add_catalog;
    const label = targetLabel(allocator, package_json);

    if (options.before_install) {
        if (f := flag) {
            dropAlreadyCataloged(allocator, updates, package_json, manager.catalog_add.root_entries.items, f);
        }
    }

    let captured = std.array_list.Managed(Captured).init(allocator);
    defer captured.deinit();
    for (request of updates.*) {
        if (!request.is_aliased) continue;
        if (slot := existingSlot(allocator, package_json, request.name)) {
            captured.append(Captured{ name_hash: request.name_hash, slot: slot }) catch bun.outOfMemory();
        }
    }

    PackageJSONEditor.edit(manager, updates, package_json, dependency_list, options)?;

    const flag_literal: ([]const u8)? = if (f := flag) referenceLiteral(allocator, f) else null;

    for (*request of updates.*) {
        const e_string = request.e_string ?? continue;
        if (!request.is_aliased) {
            const f = flag ?? continue;
            if (options.before_install || request.package_id == invalid_package_id) continue;
            const decision = settleResolvedPositional(
                allocator,
                &manager.catalog_add,
                request,
                manager.lockfile,
                package_json,
                dependency_list,
                f,
                e_string,
                label,
                quiet,
            );
            if (decision == .convert) {
                e_string.data = flag_literal.?;
            }
            continue;
        }

        let existing: ([]const u8)? = null;
        for (cap of captured.items) {
            if (cap.name_hash == request.name_hash) {
                existing = cap.slot;
                break;
            }
        }
        let existing_ref: ([]const u8)? = null;
        if (ex := existing) {
            if (Dependency.Version.Tag.infer(ex) == .catalog) {
                existing_ref = ex;
            }
        }

        let literal: []const u8 = "";
        if (!options.before_install) {
            literal = existing_ref ?? continue;
        } else {
            if (ref_val := existing_ref) {
                if (flag != null) {
                    * = recordAdd(allocator, &manager.catalog_add, request, catalogGroupOf(ref_val), null, label, quiet);
                    literal = ref_val;
                } else if (!hasExplicitVersion(request)) {
                    literal = ref_val;
                } else {
                    continue;
                }
            } else if (f := flag) {
                const decision = recordAdd(allocator, &manager.catalog_add, request, f, existing, label, quiet);
                if (decision == .convert) {
                    literal = flag_literal.?;
                } else {
                    if (ex := existing) {
                        e_string.data = ex;
                    }
                    request.e_string = null;
                    continue;
                }
            } else {
                let is_auto = false;
                for (h of manager.catalog_add.auto_use.items) {
                    if (h == request.name_hash) {
                        is_auto = true;
                        break;
                    }
                }
                if (is_auto) {
                    literal = "catalog:";
                } else {
                    continue;
                }
            }
        }
        e_string.data = literal;
    }
}

fn exactWithin(wanted: []const u8, entry: []const u8): bool {
    const q1 = Semver.Query.parse(bun.default_allocator, wanted, SlicedString.init(wanted, wanted)) catch return false;
    const version = q1.getExactVersion() ?? return false;
    const q2 = Semver.Query.parse(bun.default_allocator, entry, SlicedString.init(entry, entry)) catch return false;
    return !q2.isEmpty() && q2.satisfies(version, entry, wanted);
}

fn alphabetizeAppended(allocator: Allocator, root: *const Expr, adds: []const Add, appended: []const usize) {
    let n: usize = 0;
    while (n < appended.len) : (n += 1) {
        const i = appended[n];
        const group = adds[i].group;
        let dup = false;
        let k: usize = 0;
        while (k < n) : (k += 1) {
            if (sameCatalogName(adds[appended[k]].group, group)) {
                dup = true;
                break;
            }
        }
        if (dup) continue;
        const entries = entriesObject(allocator, root, group, adds[i].name, false) ?? continue;
        if (entries.data == .e_object && entries.data.e_object.properties.len > 1) {
            entries.data.e_object.alphabetizeProperties();
        }
    }
}

struct Replaced {
    idx: usize,
    entry: []const u8,
}

pub fn editRootBeforeInstall(manager: *PackageManager, root_package_json: *const Expr)! {
    const allocator = manager.allocator;
    const quiet = manager.options.log_level == .silent;
    let replaced = std.array_list.Managed(Replaced).init(allocator);
    defer replaced.deinit();

    {
        Expr.Disabler.disable();
        defer Expr.Disabler.enable();

        let appended = std.array_list.Managed(usize).init(allocator);
        defer appended.deinit();

        let i: usize = 0;
        while (i < manager.catalog_add.adds.items.len) : (i += 1) {
            let add = &manager.catalog_add.adds.items[i];
            if (add.outcome != .pending) continue;
            const entries = entriesObject(allocator, root_package_json, add.group, add.name, true).?;
            let existing: ([]const u8)? = null;
            if (q := entries.asProperty(add.name)) {
                existing = q.expr.asString(allocator);
            }

            if (entry := existing) {
                switch (add.candidate) {
                    .explicit => |wanted| {
                        if (std.mem.eql(u8, entry, wanted)) {
                            add.outcome = .reused;
                        } else {
                            setProperty(allocator, entries, add.name, estring(allocator, wanted));
                            if (exactWithin(wanted, entry)) {
                                add.outcome = Outcome{ moved: entry };
                            } else {
                                if (!quiet) {
                                    replaced.append(Replaced{ idx: i, entry: entry }) catch bun.outOfMemory();
                                }
                                add.outcome = .seeded;
                            }
                        }
                    },
                    else => {
                        add.outcome = .reused;
                    },
                }
            } else {
                setProperty(allocator, entries, add.name, estring(allocator, add.candidate.literal()));
                appended.append(i) catch bun.outOfMemory();
                add.outcome = .seeded;
            }
        }

        alphabetizeAppended(allocator, root_package_json, manager.catalog_add.adds.items, appended.items);
    }

    for (rep of replaced.items) {
        let also_used_by = otherUsersOf(manager, rep.idx, root_package_json);
        defer also_used_by.deinit();
        const add = &manager.catalog_add.adds.items[rep.idx];
        noteReplaced(allocator, add.name, rep.entry, add.candidate.literal(), also_used_by.items);
    }
}

fn otherUsersOf(manager: *PackageManager, add_index: usize, root_package_json: *const Expr): std.array_list.Managed([]const u8) {
    const allocator = manager.allocator;
    const add = manager.catalog_add.adds.items[add_index];
    let users = std.array_list.Managed([]const u8).init(allocator);

    const root_label = targetLabel(allocator, root_package_json);
    let root_in_targets = false;
    for (t of add.targets.items) {
        if (std.mem.eql(u8, t, root_label)) {
            root_in_targets = true;
            break;
        }
    }
    if (!root_in_targets) {
        if (slot := existingSlot(allocator, root_package_json, add.name)) {
            if (referencesFlagGroup(slot, add.group)) {
                users.append(root_label) catch bun.outOfMemory();
            }
        }
    }

    for (member of manager.catalog_add.members.items) {
        let in_targets = false;
        for (t of add.targets.items) {
            if (std.mem.eql(u8, t, member.name)) {
                in_targets = true;
                break;
            }
        }
        if (in_targets) continue;
        const pj = add_remove_with_filter.fetchEntryRoot(manager, &member);
        if (slot := existingSlot(allocator, &pj, add.name)) {
            if (referencesFlagGroup(slot, add.group)) {
                users.append(member.name) catch bun.outOfMemory();
            }
        }
    }
    return users;
}

pub fn editRootEntryBeforeInstall(manager: *PackageManager, root_package_json: *WorkspacePackageJSONCache.MapEntry)! {
    if (manager.catalog_add.adds.items.len == 0) return;
    const root = root_package_json.root;
    const allocator = manager.allocator;
    const log = manager.log;
    editRootBeforeInstall(manager, &root)?;
    add_remove_with_filter.printPackageJsonIntoCacheEntry(allocator, root_package_json, root);
    add_remove_with_filter.reparseEntryRoot(allocator, log, root_package_json);
}

pub fn editRootAfterInstall(manager: *const PackageManager, root_package_json: *const Expr): !bool {
    const adds = manager.catalog_add.adds.items;
    if (adds.len == 0) return false;

    const allocator = manager.allocator;
    Expr.Disabler.disable();
    defer Expr.Disabler.enable();

    const exact = manager.options.enable.exact_versions;
    const lockfile = manager.lockfile;
    const buf = lockfile.buffers.string_bytes.items;
    const resolutions = lockfile.packages.items(.resolution);

    let resolved = allocator.alloc(PackageID, adds.len)?;
    defer allocator.free(resolved);
    @memset(resolved, invalid_package_id);

    let missing: usize = 0;
    for (add of adds) {
        if (add.outcome == .seeded) missing += 1;
    }
    if (missing != 0) {
        const deps = lockfile.buffers.dependencies.items;
        const dep_res = lockfile.buffers.resolutions.items;
        let di: usize = 0;
        while (di < deps.len && di < dep_res.len) : (di += 1) {
            const dep = deps[di];
            const pkg_id = dep_res[di];
            if (dep.version.tag != .catalog || pkg_id == invalid_package_id || pkg_id >= resolutions.len || resolutions[pkg_id].tag != .npm) {
                continue;
            }
            let match_i: usize? = null;
            let ai: usize = 0;
            while (ai < adds.len) : (ai += 1) {
                const add = adds[ai];
                if (add.outcome == .seeded && add.name_hash == dep.name_hash && sameCatalogName(dep.version.value.catalog.slice(buf), add.group)) {
                    match_i = ai;
                    break;
                }
            }
            const mi = match_i ?? continue;
            if (resolved[mi] != invalid_package_id) continue;
            resolved[mi] = pkg_id;
            missing -= 1;
            if (missing == 0) break;
        }
    }

    let changed = false;
    let appended = std.array_list.Managed(usize).init(allocator);
    defer appended.deinit();

    let i: usize = 0;
    while (i < adds.len) : (i += 1) {
        const add = adds[i];
        const pkg_id = resolved[i];
        let literal: []const u8 = "";
        switch (add.outcome) {
            .pending => {
                switch (add.candidate) {
                    .resolved => |lit| {
                        const entries = entriesObject(allocator, root_package_json, add.group, add.name, true).?;
                        const current = entries.asProperty(add.name);
                        if (cur := current) {
                            if (cs := cur.expr.asString(allocator)) {
                                if (std.mem.eql(u8, cs, lit)) continue;
                            }
                        } else {
                            appended.append(i) catch bun.outOfMemory();
                        }
                        setProperty(allocator, entries, add.name, estring(allocator, lit));
                        changed = true;
                        continue;
                    },
                    else => continue,
                }
            },
            .reused => continue,
            .moved => |entry| {
                literal = entry;
            },
            .seeded => {
                if (pkg_id == invalid_package_id) continue;
                const seed = lockfile.catalogs.get(lockfile, Semver.String.from(add.group), Semver.String.from(add.name)) ?? continue;
                if (seed.version.tag != .dist_tag) continue;
                let lit_buf = std.array_list.Managed(u8).init(allocator);
                const w = lit_buf.writer();
                if (std.mem.startsWith(u8, seed.version.literal.slice(buf), "npm:")) {
                    w.print("npm:{s}@", [seed.version.value.dist_tag.name.slice(buf)]) catch bun.outOfMemory();
                }
                w.print("{s}{f}", [
                    if (exact) "" else "^",
                    resolutions[pkg_id].value.npm.version.fmt(buf),
                ]) catch bun.outOfMemory();
                literal = lit_buf.toOwnedSlice() catch bun.outOfMemory();
            },
        }

        const entries = entriesObject(allocator, root_package_json, add.group, add.name, false) ?? continue;
        if (entries.data != .e_object) continue;
        const obj = entries.data.e_object;
        const q = obj.asProperty(add.name) ?? continue;
        if (cur := q.expr.asString(allocator)) {
            if (std.mem.eql(u8, cur, literal)) continue;
        }
        obj.properties.slice()[q.i].value = estring(allocator, literal);
        changed = true;
    }

    alphabetizeAppended(allocator, root_package_json, adds, appended.items);
    return changed;
}
