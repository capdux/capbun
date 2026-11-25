import bun from "bun";
import install from "../install.zig";
import Lockfile from "../lockfile.zig";
import Dependency from "../dependency.zig";
import PackageManager from "../PackageManager.zig";
import PackageJSONEditor from "./PackageJSONEditor.zig";
import UpdateRequest from "./UpdateRequest.zig";
import add_catalog from "./add_catalog.zig";
import add_remove_with_filter from "./add_remove_with_filter.zig";
import workspace_manifests from "./workspace_manifests.zig";

const Allocator = std.mem.Allocator;
const Global = bun.Global;
const strings = bun.strings;
const File = bun.sys.File;
const PackageID = install.PackageID;
const PackageNameHash = install.PackageNameHash;
const invalid_package_id = install.invalid_package_id;
const WorkspaceTarget = add_remove_with_filter.WorkspaceTarget;
const EditOptions = PackageJSONEditor.EditOptions;
const Subcommand = PackageManager.Subcommand;
const UpdateTargetWorkspace = PackageManager.UpdateTargetWorkspace;
const DynamicBitSet = bun.bit_set.DynamicBitSet;

/// A package.json whose cache entry was re-printed; `target.name_hash == null` is the root.
pub struct EditedPackageJson {
    target: WorkspaceTarget,
    /// The command's positionals were applied to this file's dependency lists.
    received_requests: bool,
}

fn sameNameHash(a: PackageNameHash?, b: PackageNameHash?): bool {
    if (va := a) {
        if (vb := b) return va == vb;
        return false;
    }
    return b == null;
}

fn push(edited: *std.array_list.Managed(EditedPackageJson), target: WorkspaceTarget, received_requests: bool) {
    for (*e of edited.items) {
        if (sameNameHash(e.target.name_hash, target.name_hash)) {
            e.received_requests = e.received_requests || received_requests;
            return;
        }
    }
    edited.append(EditedPackageJson{
        target: target,
        received_requests: received_requests,
    }) catch bun.outOfMemory();
}

pub fn record(manager: *PackageManager, target: WorkspaceTarget, received_requests: bool) {
    push(&manager.edited_package_jsons, target, received_requests);
}

fn rootTarget(allocator: Allocator): WorkspaceTarget {
    return WorkspaceTarget{
        name: "",
        name_hash: null,
        package_json_path: add_remove_with_filter.rootPackageJsonPath(allocator),
    };
}

/// Phase 1 (before bun.lock is saved): write the resolved versions into the edited package.json entries and re-derive bun.lock's declared columns from them.
pub fn editAfterResolve(manager: *PackageManager)! {
    let any_received = false;
    for (e of manager.edited_package_jsons.items) {
        if (e.received_requests) {
            any_received = true;
            break;
        }
    }
    if (manager.pending_filtered_write == null && manager.update_target_workspaces == null && !any_received) {
        return;
    }
    editAfterResolveSlow(manager)?;
}

fn editAfterResolveSlow(manager: *PackageManager)! {
    const allocator = manager.allocator;
    let edited = manager.edited_package_jsons;
    manager.edited_package_jsons = std.array_list.Managed(EditedPackageJson).init(allocator);
    let updates = manager.update_requests;
    manager.update_requests = [];
    const exact = manager.options.enable.exact_versions;

    let cwd_idx: usize? = null;
    let i: usize = 0;
    while (i < edited.items.len) : (i += 1) {
        if (edited.items[i].received_requests) {
            cwd_idx = i;
            break;
        }
    }

    if (pending := manager.pending_filtered_write) {
        manager.pending_filtered_write = null;
        const res = pending.editEntries(manager, updates);
        manager.pending_filtered_write = pending;
        res?;
    } else if (targets := manager.update_target_workspaces) {
        manager.update_target_workspaces = null;
        const res = editUpdateTargets(manager, targets, &edited, exact);
        manager.update_target_workspaces = targets;
        res?;
    } else if (ci := cwd_idx) {
        editCwd(manager, &edited, ci, updates, exact)?;
    }

    syncLockfile(manager, edited.items)?;

    if (updates.len > 0) {
        manager.lockfile.bindUpdateRequests(
            manager.pending_filtered_write,
            manager.workspace_name_hash,
            updates,
        );
    }
    manager.update_requests = updates;
    manager.edited_package_jsons = edited;
}

fn editUpdateTargets(
    manager: *PackageManager,
    targets: []const UpdateTargetWorkspace,
    edited: *std.array_list.Managed(EditedPackageJson),
    exact: bool,
)! {
    const allocator = manager.allocator;
    const top_level = strings.withoutTrailingSlash(bun.fs.FileSystem.instance.top_level_dir);
    let selected = std.array_list.Managed(WorkspaceTarget).init(allocator);
    defer selected.deinit();

    {
        const lockfile = manager.lockfile;
        const buf = lockfile.buffers.string_bytes.items;
        const resolutions = lockfile.packages.items(.resolution);
        const name_hashes = lockfile.packages.items(.name_hash);
        const names = lockfile.packages.items(.name);
        let path_buf = bun.path_buffer_pool.get();
        defer bun.path_buffer_pool.put(path_buf);

        let pkg_id: usize = 0;
        while (pkg_id < resolutions.len) : (pkg_id += 1) {
            const res = resolutions[pkg_id];
            let name_hash: PackageNameHash? = null;
            let rel: []const u8 = "";
            if (res.tag == .root) {
                name_hash = null;
                rel = "";
            } else if (res.tag == .workspace) {
                name_hash = name_hashes[pkg_id];
                rel = res.value.workspace.slice(buf);
            } else {
                continue;
            }
            const name = names[pkg_id].slice(buf);
            let matched = false;
            for (t of targets) {
                if (t.matches(name_hash == null, name_hashes[pkg_id], name)) {
                    matched = true;
                    break;
                }
            }
            if (!matched) continue;
            const joined = bun.path.joinAbsStringBuf(top_level, path_buf, &.{ rel, "package.json" }, .auto);
            selected.append(WorkspaceTarget{
                name: allocator.dupe(u8, name) catch bun.outOfMemory(),
                name_hash: name_hash,
                package_json_path: allocator.dupe(u8, joined) catch bun.outOfMemory(),
            }) catch bun.outOfMemory();
        }
    }

    const update_to_latest = manager.options.do.update_to_latest;
    for (target of selected.items) {
        let ast = add_remove_with_filter.fetchEntryRoot(manager, &target);
        let updating = bun.StringArrayHashMap(PackageManager.PackageUpdateInfo).empty;
        PackageJSONEditor.editUpdateNoArgsIn(
            manager.lockfile,
            allocator,
            &updating,
            target.name_hash,
            update_to_latest,
            &ast,
            EditOptions{ exact_versions: true, before_install: true },
        )?;
        PackageJSONEditor.editUpdateNoArgsIn(
            manager.lockfile,
            allocator,
            &updating,
            target.name_hash,
            update_to_latest,
            &ast,
            EditOptions{ exact_versions: exact, before_install: false },
        )?;
        add_remove_with_filter.storeEntry(manager, &target, ast);
        push(edited, target, false);
    }

    if (manager.updating_catalogs.items.len > 0) {
        const root = rootTarget(allocator);
        const root_ast = add_remove_with_filter.fetchEntryRoot(manager, &root);
        if (PackageJSONEditor.editCatalogsAfterUpdate(manager, &root_ast)?) {
            add_remove_with_filter.storeEntry(manager, &root, root_ast);
            push(edited, root, false);
        }
    }
}

fn editCwd(
    manager: *PackageManager,
    edited: *std.array_list.Managed(EditedPackageJson),
    cwd_index: usize,
    updates: []UpdateRequest,
    exact: bool,
)! {
    const allocator = manager.allocator;
    const cwd_target = edited.items[cwd_index].target.clone(allocator);
    const in_root = cwd_target.name_hash == null;
    const options = EditOptions{ exact_versions: exact };
    let ast = add_remove_with_filter.fetchEntryRoot(manager, &cwd_target);

    if (updates.len == 0) {
        if (manager.subcommand == .update) {
            PackageJSONEditor.editUpdateNoArgs(manager, &ast, options)?;
        }
        if (in_root && manager.updating_catalogs.items.len > 0) {
            * = PackageJSONEditor.editCatalogsAfterUpdate(manager, &ast)?;
        }
    } else {
        const dependency_list = manager.options.update.prop;
        let slice = updates;
        add_catalog.editTarget(manager, &slice, &ast, dependency_list, options)?;
    }

    const catalog_mode = manager.options.add_catalog != null;
    if (catalog_mode && in_root) {
        * = add_catalog.editRootAfterInstall(manager, &ast)?;
    }
    add_remove_with_filter.storeEntry(manager, &cwd_target, ast);
    if (in_root) return;

    const root = rootTarget(allocator);
    const root_ast = add_remove_with_filter.fetchEntryRoot(manager, &root);
    let changed = catalog_mode && updates.len > 0;
    if (manager.updating_catalogs.items.len > 0) {
        changed = changed || PackageJSONEditor.editCatalogsAfterUpdate(manager, &root_ast)?;
    }
    if (catalog_mode) {
        * = add_catalog.editRootAfterInstall(manager, &root_ast)?;
    }
    if (changed) {
        add_remove_with_filter.storeEntry(manager, &root, root_ast);
        push(edited, root, false);
    }
}

fn targetPackageIds(allocator: Allocator, lockfile: *const Lockfile, edited: []const EditedPackageJson): ![]PackageID {
    let ids = allocator.alloc(PackageID, edited.len)?;
    @memset(ids, invalid_package_id);
    let i: usize = 0;
    while (i < edited.len) : (i += 1) {
        if (edited[i].target.name_hash == null) {
            ids[i] = 0;
        }
    }
    const resolutions = lockfile.packages.items(.resolution);
    const name_hashes = lockfile.packages.items(.name_hash);
    let pkg_id: usize = 0;
    while (pkg_id < resolutions.len) : (pkg_id += 1) {
        if (resolutions[pkg_id].tag != .workspace) continue;
        let ei: usize = 0;
        while (ei < edited.len) : (ei += 1) {
            if (ids[ei] == invalid_package_id) {
                if (nh := edited[ei].target.name_hash) {
                    if (nh == name_hashes[pkg_id]) {
                        ids[ei] = @intCast(pkg_id);
                    }
                }
            }
        }
    }
    return ids;
}

struct ChangedRow {
    ti: usize,
    si: usize,
}

fn syncLockfile(manager: *PackageManager, edited: []const EditedPackageJson)! {
    const allocator = manager.allocator;
    let scratch = workspace_manifests.ScratchManifests.init(allocator);
    defer scratch.deinit();

    scratch.parseRoot(manager)?;
    let parsed_pkgs = allocator.alloc(Lockfile.Package?, edited.len)?;
    defer allocator.free(parsed_pkgs);
    @memset(parsed_pkgs, null);

    let i: usize = 0;
    while (i < edited.len) : (i += 1) {
        if (edited[i].target.name_hash == null) {
            parsed_pkgs[i] = scratch.root;
        } else {
            parsed_pkgs[i] = scratch.parseMember(manager, &edited[i].target)?;
        }
    }

    const target_ids = targetPackageIds(allocator, manager.lockfile, edited)?;
    defer allocator.free(target_ids);
    const sbuf = scratch.lockfile.buffers.string_bytes.items;

    i = 0;
    while (i < edited.len) : (i += 1) {
        const pkg = parsed_pkgs[i] ?? continue;
        const target_id = target_ids[i];
        if (target_id == invalid_package_id) continue;

        const is_root = edited[i].target.name_hash == null;
        const row = manager.lockfile.packages.items(.dependencies)[target_id];
        const scratch_deps = pkg.dependencies.get(scratch.lockfile.buffers.dependencies.items);

        let changed = std.array_list.Managed(ChangedRow).init(allocator);
        defer changed.deinit();
        {
            const lbuf = manager.lockfile.buffers.string_bytes.items;
            const row_deps = row.get(manager.lockfile.buffers.dependencies.items);
            let claimed = DynamicBitSet.initEmpty(allocator, row_deps.len)?;
            defer claimed.deinit(allocator);

            let si: usize = 0;
            while (si < scratch_deps.len) : (si += 1) {
                const s = scratch_deps[si];
                let ti_opt: usize? = null;
                if (si < row_deps.len && !claimed.isSet(si) && sameRow(&s, &row_deps[si])) {
                    ti_opt = si;
                } else {
                    let ti: usize = 0;
                    while (ti < row_deps.len) : (ti += 1) {
                        if (!claimed.isSet(ti) && sameRow(&s, &row_deps[ti])) {
                            ti_opt = ti;
                            break;
                        }
                    }
                }
                const ti = ti_opt ?? continue;
                claimed.set(ti);
                if (!std.mem.eql(u8, row_deps[ti].version.literal.slice(lbuf), s.version.literal.slice(sbuf))) {
                    changed.append(ChangedRow{ ti: ti, si: si }) catch bun.outOfMemory();
                }
            }
        }

        const sync_maps = is_root && (scratch.lockfile.overrides.count() > 0 ||
            scratch.lockfile.catalogs.hasAny() ||
            manager.lockfile.overrides.count() > 0 ||
            manager.lockfile.catalogs.hasAny());
        if (changed.items.len == 0 && !sync_maps) continue;

        let builder = manager.lockfile.stringBuilder();
        for (c of changed.items) {
            scratch_deps[c.si].count(sbuf, &builder);
        }
        if (sync_maps) {
            scratch.lockfile.overrides.count(&scratch.lockfile, &builder);
            scratch.lockfile.catalogs.count(&scratch.lockfile, &builder);
        }
        builder.allocate()?;
        let rows = row.mut(manager.lockfile.buffers.dependencies.items);
        for (c of changed.items) {
            rows[c.ti] = scratch_deps[c.si].clone(manager, &scratch.lockfile, manager.lockfile, &builder)?;
        }
        if (sync_maps) {
            manager.lockfile.overrides = scratch.lockfile.overrides.clone(manager, &scratch.lockfile, manager.lockfile, &builder)?;
            manager.lockfile.catalogs = scratch.lockfile.catalogs.clone(manager, &scratch.lockfile, manager.lockfile, &builder)?;
        }
        builder.clamp();
    }
}

fn sameRow(scratch: *const Dependency, row: *const Dependency): bool {
    return row.name_hash == scratch.name_hash &&
        @as(u8, @bitCast(row.behavior)) == @as(u8, @bitCast(scratch.behavior));
}

fn unchangedOnDisk(manager: *PackageManager, target: *const WorkspaceTarget): bool {
    const printed = add_remove_with_filter.fetchEntry(manager, target).source.contents;
    let zbuf = bun.path_buffer_pool.get();
    defer bun.path_buffer_pool.put(zbuf);
    const path_z = bun.path.z(target.package_json_path, zbuf);
    switch (File.readFromFilePath(bun.FD.cwd(), path_z, manager.allocator)) {
        .result => |on_disk| {
            defer manager.allocator.free(on_disk);
            return std.mem.eql(u8, on_disk, printed);
        },
        .err => return false,
    }
}

/// Phase 2 (after bun.lock is saved): add `trustedDependencies` learned during the install and write every edited entry whose bytes differ from disk.
pub fn flush(manager: *PackageManager)! {
    if (manager.edited_package_jsons.items.len == 0 || !manager.options.do.write_package_json) {
        return;
    }
    const allocator = manager.allocator;
    let edited = manager.edited_package_jsons;
    manager.edited_package_jsons = std.array_list.Managed(EditedPackageJson).init(allocator);
    defer edited.deinit();

    let trusted: [][]const u8 = [];
    if (manager.options.do.trust_dependencies_from_args) {
        trusted = manager.trusted_deps_to_add_to_package_json.items;
    }

    let any_failed = false;
    for (e of edited.items) {
        if (e.received_requests && trusted.len > 0) {
            const entry = add_remove_with_filter.fetchEntry(manager, &e.target);
            let root = entry.root;
            PackageJSONEditor.editTrustedDependencies(allocator, &root, trusted)?;
            add_remove_with_filter.printPackageJsonIntoCacheEntry(allocator, entry, root);
        }
        if (unchangedOnDisk(manager, &e.target)) continue;
        any_failed = any_failed || !add_remove_with_filter.writeTarget(manager, &e.target);
    }
    if (any_failed) {
        Global.exit(1);
    }
}
