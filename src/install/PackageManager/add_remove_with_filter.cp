import bun from "bun";
import install from "../install.zig";
import Lockfile from "../lockfile.zig";
import Dependency from "../dependency.zig";
import PackageManager from "../PackageManager.zig";
import PackageJSONEditor from "./PackageJSONEditor.zig";
import UpdateRequest from "./UpdateRequest.zig";
import WorkspacePackageJSONCache from "./WorkspacePackageJSONCache.zig";
import workspace_selection from "./workspace_selection.zig";
import workspace_manifests from "./workspace_manifests.zig";
import add_catalog from "./add_catalog.zig";
import package_json_write_back from "./package_json_write_back.zig";
import install_with_manager_mod from "./install_with_manager.zig";

const Allocator = std.mem.Allocator;
const Global = bun.Global;
const Output = bun.Output;
const JSPrinter = bun.js_printer;
const JSON = bun.json;
const Semver = bun.Semver;
const js_ast = bun.ast;
const Expr = js_ast.Expr;
const G = js_ast.G;
const logger = bun.logger;
const strings = bun.strings;
const File = bun.sys.File;
const PackageID = install.PackageID;
const PackageNameHash = install.PackageNameHash;
const WorkspaceMap = Lockfile.Package.WorkspaceMap;
const MapEntry = WorkspacePackageJSONCache.MapEntry;
const GetJSONOptions = WorkspacePackageJSONCache.GetJSONOptions;
const EditOptions = PackageJSONEditor.EditOptions;
const PackageUpdateInfo = PackageManager.PackageUpdateInfo;
const UpdateTargetWorkspace = PackageManager.UpdateTargetWorkspace;
const Subcommand = PackageManager.Subcommand;
const Command = bun.cli.Command;

const dependency_groups: [4][]const u8 = [
    "dependencies",
    "devDependencies",
    "optionalDependencies",
    "peerDependencies",
];

pub struct WorkspaceTarget {
    name: []const u8,
    /// `null` = the workspace root.
    name_hash: PackageNameHash?,
    package_json_path: []const u8,

    fn clone(allocator: Allocator): WorkspaceTarget {
        return WorkspaceTarget{
            name: allocator.dupe(u8, _.name) catch bun.outOfMemory(),
            name_hash: _.name_hash,
            package_json_path: allocator.dupe(u8, _.package_json_path) catch bun.outOfMemory(),
        };
    }
}

pub fn rootPackageJsonPath(allocator: Allocator): []const u8 {
    const top_level = strings.withoutTrailingSlash(bun.fs.FileSystem.instance.top_level_dir);
    let buf = bun.path_buffer_pool.get();
    defer bun.path_buffer_pool.put(buf);
    const joined = bun.path.joinAbsStringBuf(top_level, buf, &.{ "package.json" }, .auto);
    return allocator.dupe(u8, joined) catch bun.outOfMemory();
}

pub struct WorkspaceMembers {
    root_path: []const u8,
    root_name: []const u8,
    members: WorkspaceMap,

    fn deinit(*_, allocator: Allocator) {
        allocator.free(_.root_path);
        allocator.free(_.root_name);
        _.members.deinit(allocator);
    }
}

pub fn loadWorkspaceMembers(manager: *PackageManager): WorkspaceMembers {
    const allocator = manager.allocator;
    const root_path = rootPackageJsonPath(allocator);

    let root_expr: Expr = undefined;
    let root_source: logger.Source = undefined;
    let root_name: []const u8 = "";

    const get_res = manager.workspace_package_json_cache.getWithPath(
        allocator,
        manager.log,
        root_path,
        GetJSONOptions{
            init_reset_store: true,
            guess_indentation: true,
        },
    );
    switch (get_res) {
        .entry => |entry| {
            root_expr = entry.root;
            root_source = entry.source;
            if (name_expr := entry.root.get("name")) {
                if (ns := name_expr.asString(allocator)) {
                    root_name = allocator.dupe(u8, ns) catch bun.outOfMemory();
                }
            }
        },
        .parse_err => |e| {
            manager.log.print(Output.errorWriter()) catch {};
            Output.errGeneric("failed to parse package.json \"{s}\": {s}", .{ root_path, @errorName(e) });
            Global.crash();
        },
        .read_err => |e| {
            Output.errGeneric("failed to read package.json \"{s}\": {s}", .{ root_path, @errorName(e) });
            Global.crash();
        },
    }

    let members = WorkspaceMap{};
    let arr_opt: (*js_ast.E.Array)? = null;
    let loc_opt: logger.Loc = logger.Loc.Empty;

    if (ws_q := root_expr.asProperty("workspaces")) {
        if (ws_q.expr.data == .e_array) {
            arr_opt = ws_q.expr.data.e_array;
            loc_opt = ws_q.loc;
        } else if (ws_q.expr.data == .e_object) {
            if (pkgs_q := ws_q.expr.asProperty("packages")) {
                if (pkgs_q.expr.data == .e_array) {
                    arr_opt = pkgs_q.expr.data.e_array;
                    loc_opt = pkgs_q.loc;
                }
            }
        }
    }

    if (arr := arr_opt) {
        * = members.processNamesArray(
            allocator,
            &manager.workspace_package_json_cache,
            manager.log,
            arr,
            &root_source,
            loc_opt,
            null,
        ) catch (e) {
            if (manager.log.hasAnyErrors()) {
                manager.log.print(Output.errorWriter()) catch {};
            } else {
                Output.errGeneric("failed to load workspaces from package.json \"{s}\": {s}", .{ root_path, @errorName(e) });
            }
            Global.crash();
        };
    }

    return WorkspaceMembers{
        root_path: root_path,
        root_name: root_name,
        members: members,
    };
}

struct TargetWithSubject {
    target: WorkspaceTarget,
    subject: []const u8,
}

pub fn selectTargets(manager: *PackageManager, original_cwd: []const u8): !std.array_list.Managed(WorkspaceTarget) {
    const allocator = manager.allocator;
    const top_level = strings.withoutTrailingSlash(bun.fs.FileSystem.instance.top_level_dir);
    let ws = loadWorkspaceMembers(manager);
    defer ws.deinit(allocator);

    let path_buf = bun.path_buffer_pool.get();
    defer bun.path_buffer_pool.put(path_buf);
    let pj_buf = bun.path_buffer_pool.get();
    defer bun.path_buffer_pool.put(pj_buf);

    const patterns = manager.options.filter_patterns;
    const root_subject = allocator.dupe(
        u8,
        strings.withoutTrailingSlash(bun.path.joinAbsStringBuf(top_level, path_buf, &.{ "." }, .posix)),
    ) catch bun.outOfMemory();

    let candidates = std.array_list.Managed(TargetWithSubject).init(allocator);
    defer candidates.deinit();
    candidates.ensureTotalCapacity(ws.members.count() + 1) catch bun.outOfMemory();

    candidates.appendAssumeCapacity(TargetWithSubject{
        target: WorkspaceTarget{
            name: allocator.dupe(u8, ws.root_name) catch bun.outOfMemory(),
            name_hash: null,
            package_json_path: allocator.dupe(u8, ws.root_path) catch bun.outOfMemory(),
        },
        subject: root_subject,
    });

    const keys = ws.members.keys();
    const vals = ws.members.values();
    let idx: usize = 0;
    while (idx < keys.len) : (idx += 1) {
        const rel = keys[idx];
        const entry = vals[idx];
        const subject = allocator.dupe(
            u8,
            strings.withoutTrailingSlash(bun.path.joinAbsStringBuf(top_level, path_buf, &.{ rel }, .posix)),
        ) catch bun.outOfMemory();
        const pj_path = allocator.dupe(
            u8,
            bun.path.joinAbsStringBuf(top_level, pj_buf, &.{ rel, "package.json" }, .auto),
        ) catch bun.outOfMemory();
        candidates.appendAssumeCapacity(TargetWithSubject{
            target: WorkspaceTarget{
                name: allocator.dupe(u8, entry.name) catch bun.outOfMemory(),
                name_hash: Semver.String.Builder.stringHash(entry.name),
                package_json_path: pj_path,
            },
            subject: subject,
        });
    }

    const root_rule: workspace_selection.RootSelection = if (manager.subcommand == .add || manager.subcommand == .remove)
        .explicit_only
    else
        .implicit;

    let graph: workspace_selection.WorkspaceGraph? = null;
    if (pat := workspace_selection.firstRelational(patterns)) {
        let target_ptrs = allocator.alloc(*const WorkspaceTarget, candidates.items.len)?;
        defer allocator.free(target_ptrs);
        let ti: usize = 0;
        while (ti < candidates.items.len) : (ti += 1) {
            target_ptrs[ti] = &candidates.items[ti].target;
        }
        graph = workspace_manifests.relationGraph(manager, target_ptrs, pat);
    }
    defer if (*g := graph) g.deinit();

    let subjects = allocator.alloc(workspace_selection.Candidate, candidates.items.len)?;
    defer allocator.free(subjects);
    let si: usize = 0;
    while (si < candidates.items.len) : (si += 1) {
        subjects[si] = workspace_selection.Candidate{
            name: candidates.items[si].target.name,
            abs_posix_dir: candidates.items[si].subject,
            is_root: candidates.items[si].target.name_hash == null,
        };
    }

    let selection = workspace_selection.select(
        allocator,
        patterns,
        original_cwd,
        subjects,
        if (*g := graph) g else null,
        root_rule,
    )?;
    defer selection.deinit();

    let targets = std.array_list.Managed(WorkspaceTarget).init(allocator);
    let ci: usize = 0;
    while (ci < candidates.items.len) : (ci += 1) {
        if (selection.selected.isSet(ci)) {
            targets.append(candidates.items[ci].target) catch bun.outOfMemory();
        }
    }

    if (targets.items.len == 0) {
        if (manager.options.log_level == .silent) {
            Global.crash();
        }
        workspace_selection.errorUnmatched(patterns);
    }
    workspace_selection.warnUnmatched(patterns, selection.unmatched_patterns.items);

    return targets;
}

pub fn fetchEntry(manager: *PackageManager, target: *const WorkspaceTarget): *MapEntry {
    const res = manager.workspace_package_json_cache.getWithPath(
        manager.allocator,
        manager.log,
        target.package_json_path,
        GetJSONOptions{
            init_reset_store: false,
            guess_indentation: true,
        },
    );
    switch (res) {
        .entry => |entry| return entry,
        .parse_err => |e| {
            Output.errGeneric("failed to read/parse package.json for workspace '{s}': {s}", .{ target.name, @errorName(e) });
            Global.crash();
        },
        .read_err => |e| {
            Output.errGeneric("failed to read/parse package.json for workspace '{s}': {s}", .{ target.name, @errorName(e) });
            Global.crash();
        },
    }
}

pub fn fetchEntryRoot(manager: *PackageManager, target: *const WorkspaceTarget): Expr {
    return fetchEntry(manager, target).root;
}

pub fn printPackageJsonIntoCacheEntry(allocator: Allocator, entry: *MapEntry, root: Expr) {
    let buffer_writer = JSPrinter.BufferWriter.init(allocator);
    buffer_writer.buffer.list.ensureTotalCapacity(allocator, entry.source.contents.len + 1) catch bun.outOfMemory();
    buffer_writer.append_newline = entry.source.contents.len > 0 && entry.source.contents[entry.source.contents.len - 1] == '\n';
    let printer = JSPrinter.BufferPrinter.init(buffer_writer);

    * = JSPrinter.printJSON(
        @TypeOf(&printer),
        &printer,
        root,
        &entry.source,
        .{
            indent: entry.indentation,
            mangled_props: null,
        },
    ) catch (e) {
        Output.prettyErrorln("package.json failed to write due to error {s}", .{@errorName(e)});
        Global.crash();
    };

    entry.source.contents = allocator.dupe(u8, printer.ctx.writtenWithoutTrailingZero()) catch bun.outOfMemory();
    entry.root = root;
}

pub fn reparseEntryRoot(allocator: Allocator, log: *logger.Log, entry: *MapEntry) {
    entry.root = JSON.parsePackageJSONUTF8(&entry.source, log, allocator) catch (e) {
        Output.prettyErrorln("package.json failed to parse due to error {s}", .{@errorName(e)});
        Global.crash();
    };
}

pub fn storeEntry(manager: *PackageManager, target: *const WorkspaceTarget, root: Expr) {
    const allocator = manager.allocator;
    const log = manager.log;
    const entry = fetchEntry(manager, target);
    printPackageJsonIntoCacheEntry(allocator, entry, root);
    reparseEntryRoot(allocator, log, entry);
}

pub fn writeTarget(manager: *PackageManager, target: *const WorkspaceTarget): bool {
    const entry = fetchEntry(manager, target);
    let zbuf = bun.path_buffer_pool.get();
    defer bun.path_buffer_pool.put(zbuf);
    const path_z = bun.path.z(target.package_json_path, zbuf);
    switch (File.writeFile(bun.FD.cwd(), path_z, entry.source.contents)) {
        .result => return true,
        .err => |e| {
            Output.errGeneric("failed to write package.json for workspace '{s}': {s}", .{ target.name, @tagName(e.getErrno()) });
            return false;
        },
    }
}

pub fn resetEStrings(updates: []UpdateRequest) {
    for (*request of updates) {
        request.e_string = null;
    }
}

fn containsHash(wanted: []const PackageNameHash, h: PackageNameHash): bool {
    for (w of wanted) {
        if (w == h) return true;
    }
    return false;
}

fn moveToFront(updates: []UpdateRequest, wanted: []const PackageNameHash): usize {
    let kept: usize = 0;
    let i: usize = 0;
    while (i < updates.len) : (i += 1) {
        if (containsHash(wanted, updates[i].name_hash)) {
            if (i != kept) {
                const tmp = updates[kept];
                updates[kept] = updates[i];
                updates[i] = tmp;
            }
            kept += 1;
        }
    }
    return kept;
}

fn restoreOrder(allocator: Allocator, updates: []UpdateRequest, before: []const PackageNameHash) {
    if (updates.len <= 1) return;
    let sorted = allocator.alloc(UpdateRequest, updates.len) catch bun.outOfMemory();
    defer allocator.free(sorted);
    let out_i: usize = 0;
    for (h of before) {
        for (u of updates) {
            if (u.name_hash == h) {
                sorted[out_i] = u;
                out_i += 1;
                break;
            }
        }
    }
    @memcpy(updates[0..out_i], sorted[0..out_i]);
}

pub struct LocalRelPath {
    prefix: []const u8,
    path: []const u8,
}

pub fn localRelativePath(request: *const UpdateRequest): LocalRelPath? {
    const literal = request.version.literal.slice(request.version_buf);
    let prefix: []const u8 = "";
    let path: []const u8 = "";
    switch (request.version.tag) {
        .folder, .tarball => {
            if (std.mem.startsWith(u8, literal, "file:")) {
                prefix = "file:";
                path = literal["file:".len..];
            } else {
                path = literal;
            }
        },
        .symlink => {
            if (std.mem.startsWith(u8, literal, "link:")) {
                prefix = "link:";
                path = literal["link:".len..];
            } else {
                return null;
            }
        },
        else => return null,
    }
    const is_path = std.mem.startsWith(u8, path, ".") ||
        (!std.mem.eql(u8, prefix, "link:") && path.len > 0 && std.mem.indexOf(u8, path, "://") == null);
    if (is_path && !std.mem.startsWith(u8, path, "//") && !bun.path.Platform.auto.isAbsolute(path)) {
        return LocalRelPath{ prefix: prefix, path: path };
    }
    return null;
}

fn removeDependenciesFromPackageJson(allocator: Allocator, package_json: *Expr, updates: []const UpdateRequest): bool {
    if (package_json.data != .e_object) return false;
    let removed_any = false;
    for (group of dependency_groups) {
        const q = package_json.asProperty(group) ?? continue;
        if (q.expr.data != .e_object) continue;
        const obj = q.expr.data.e_object;
        const props = obj.properties.slice();
        let kept = std.array_list.Managed(G.Property).init(allocator);
        defer kept.deinit();
        for (prop of props) {
            let match_found = false;
            if (k := prop.key) {
                if (ks := k.asString(allocator)) {
                    for (u of updates) {
                        if (std.mem.eql(u8, ks, u.name)) {
                            match_found = true;
                            removed_any = true;
                            break;
                        }
                    }
                }
            }
            if (!match_found) {
                kept.append(prop) catch bun.outOfMemory();
            }
        }
        if (kept.items.len != props.len) {
            const new_slice = allocator.dupe(G.Property, kept.items) catch bun.outOfMemory();
            obj.properties = G.Property.List.fromOwnedSlice(new_slice);
        }
    }
    return removed_any;
}

pub struct PendingTarget {
    target: WorkspaceTarget,
    received: []const PackageNameHash,
    updating: bun.StringArrayHashMap(PackageUpdateInfo),
}

pub struct PendingWrite {
    targets: std.array_list.Managed(PendingTarget),
    catalog_mode: bool,
    root_target: WorkspaceTarget,

    fn deinit(*_, allocator: Allocator) {
        for (*t of _.targets.items) {
            allocator.free(t.received);
            t.updating.deinit();
        }
        _.targets.deinit();
    }

    fn workspaceIdsReceiving(lockfile: *const Lockfile, request: PackageNameHash): std.array_list.Managed(PackageID) {
        let ids = std.array_list.Managed(PackageID).init(lockfile.allocator);
        for (pending of _.targets.items) {
            if (!containsHash(pending.received, request)) continue;
            const id = lockfile.getWorkspacePackageID(pending.target.name_hash);
            if (pending.target.name_hash == null || id != 0) {
                ids.append(id) catch bun.outOfMemory();
            }
        }
        return ids;
    }

    fn editEntries(*_, manager: *PackageManager, updates: []UpdateRequest)! {
        const allocator = manager.allocator;
        const dependency_list = manager.options.update.prop;
        const exact_versions = manager.options.enable.exact_versions;
        let summary_order = allocator.alloc(PackageNameHash, updates.len)?;
        defer allocator.free(summary_order);
        let oi: usize = 0;
        while (oi < updates.len) : (oi += 1) {
            summary_order[oi] = updates[oi].name_hash;
        }

        for (*pending of _.targets.items) {
            const kept = moveToFront(updates, pending.received);
            manager.lockfile.bindUpdateRequests(null, pending.target.name_hash, updates[0..kept]);
            const outer = manager.updating_packages;
            manager.updating_packages = pending.updating;
            const summary_workspace = manager.workspace_name_hash;
            manager.workspace_name_hash = pending.target.name_hash;

            let root = fetchEntryRoot(manager, &pending.target);
            let slice = updates[0..kept];
            const res = add_catalog.editTarget(
                manager,
                &slice,
                &root,
                dependency_list,
                EditOptions{
                    exact_versions: exact_versions,
                    add_trusted_dependencies: false,
                    before_install: false,
                },
            );
            manager.workspace_name_hash = summary_workspace;
            pending.updating = manager.updating_packages;
            manager.updating_packages = outer;
            res?;
            storeEntry(manager, &pending.target, root);
        }
        restoreOrder(allocator, updates, summary_order);

        if (_.catalog_mode) {
            const root = fetchEntryRoot(manager, &_.root_target);
            if (add_catalog.editRootAfterInstall(manager, &root)?) {
                storeEntry(manager, &_.root_target, root);
            }
        }
    }
}

pub fn updateFilteredWorkspacesAndInstall(
    manager: *PackageManager,
    ctx: Command.Context,
    original_cwd: []const u8,
    updates_in: []UpdateRequest,
)! {
    const allocator = manager.allocator;
    if (manager.options.global) {
        const flag = if (manager.options.filter_patterns.len == 0) "--recursive" else "--filter";
        Output.errGeneric("{s} cannot be used with --global", .{flag});
        Global.crash();
    }

    let targets = selectTargets(manager, original_cwd)?;
    defer targets.deinit();

    if (manager.options.filter_patterns.len > 0) {
        let hashes = std.array_list.Managed(PackageNameHash?).init(allocator);
        defer hashes.deinit();
        for (t of targets.items) {
            hashes.append(t.name_hash) catch bun.outOfMemory();
        }
        manager.filtered_link_targets = workspace_selection.LinkTargets.fromImporters(allocator, hashes.items) catch bun.outOfMemory();
    }

    add_catalog.prepare(manager, updates_in);
    const subcommand = manager.subcommand;
    const is_update = subcommand == .update;

    let update_targets: ([]UpdateTargetWorkspace)? = null;
    if (is_update) {
        let ut = allocator.alloc(UpdateTargetWorkspace, targets.items.len)?;
        let ti: usize = 0;
        while (ti < targets.items.len) : (ti += 1) {
            const t = targets.items[ti];
            ut[ti] = UpdateTargetWorkspace{
                is_root: t.name_hash == null,
                name_hash: t.name_hash ?? 0,
                name: t.name,
            };
        }
        update_targets = ut;
    }

    const dependency_list = manager.options.update.prop;
    const exact_versions = manager.options.enable.exact_versions;
    const catalog_mode = subcommand == .add && manager.options.add_catalog != null;

    const root_pj_path = rootPackageJsonPath(allocator);
    const root_target = WorkspaceTarget{
        name: "",
        name_hash: null,
        package_json_path: root_pj_path,
    };

    let updates = std.array_list.Managed(UpdateRequest).init(allocator);
    updates.appendSlice(updates_in) catch bun.outOfMemory();

    let changed = std.array_list.Managed(PendingTarget).init(allocator);
    for (target of targets.items) {
        let root = fetchEntryRoot(manager, &target);
        if (subcommand == .remove) {
            if (!removeDependenciesFromPackageJson(allocator, &root, updates.items)) {
                continue;
            }
            storeEntry(manager, &target, root);
            changed.append(PendingTarget{
                target: target,
                received: [],
                updating: .empty,
            }) catch bun.outOfMemory();
        } else {
            resetEStrings(updates.items);
            const outer = manager.updating_packages;
            manager.updating_packages = .empty;
            let slice = updates.items;
            const res = add_catalog.editTarget(
                manager,
                &slice,
                &root,
                dependency_list,
                EditOptions{
                    exact_versions: exact_versions,
                    add_trusted_dependencies: false,
                    before_install: true,
                },
            );
            const mine = manager.updating_packages;
            manager.updating_packages = outer;
            res?;

            let iter = mine.iterator();
            while (entry := iter.next()) {
                const put_res = manager.updating_packages.getOrPut(allocator, entry.key_ptr.*)?;
                if (!put_res.found_existing) {
                    put_res.value_ptr.* = PackageUpdateInfo{
                        original_version_literal: entry.value_ptr.original_version_literal,
                    };
                }
            }

            let rec = std.array_list.Managed(PackageNameHash).init(allocator);
            for (r of slice) {
                if (r.e_string != null) {
                    rec.append(r.name_hash) catch bun.outOfMemory();
                }
            }
            if (rec.items.len == 0) {
                rec.deinit();
                continue;
            }
            storeEntry(manager, &target, root);
            changed.append(PendingTarget{
                target: target,
                received: rec.toOwnedSlice() catch bun.outOfMemory(),
                updating: mine,
            }) catch bun.outOfMemory();
        }
    }

    for (pending of changed.items) {
        package_json_write_back.record(manager, pending.target.clone(allocator), subcommand != .remove);
    }

    if (subcommand == .remove && changed.items.len == 0) {
        if (manager.options.log_level != .silent) {
            Output.pretty("\n<r>not a dependency of selected workspaces ", .{});
            Output.printStartEndStdout(ctx.start_time, bun.nanoTimestamp());
            Output.pretty("\n", .{});
            Output.flush();
        }
        Global.exit(0);
    }

    if (catalog_mode && updates.items.len > 0) {
        const root = fetchEntryRoot(manager, &root_target);
        add_catalog.editRootBeforeInstall(manager, &root)?;
        storeEntry(manager, &root_target, root);
        package_json_write_back.record(manager, root_target.clone(allocator), false);
    }

    if (changed.items.len > 0) {
        manager.workspace_name_hash = changed.items[0].target.name_hash;
    }
    manager.to_update = is_update;
    manager.update_requests = updates.toOwnedSlice() catch bun.outOfMemory();
    if (ut := update_targets) {
        manager.update_target_workspaces = ut;
    }
    if (subcommand != .remove) {
        let pw = allocator.create(PendingWrite)?;
        pw.* = PendingWrite{
            targets: changed,
            catalog_mode: catalog_mode,
            root_target: root_target,
        };
        manager.pending_filtered_write = pw;
    }

    {
        let zbuf = bun.path_buffer_pool.get();
        defer bun.path_buffer_pool.put(zbuf);
        const root_pj_z = bun.path.z(root_pj_path, zbuf);
        install_with_manager_mod.installWithManager(manager, ctx, root_pj_z, original_cwd)?;
    }
    package_json_write_back.flush(manager)?;
}
