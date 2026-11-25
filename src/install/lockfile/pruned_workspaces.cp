import bun from "bun";
import install from "../install.zig";
import Lockfile from "../lockfile.zig";
import CatalogMap from "./CatalogMap.zig";
import DependencyMod from "../dependency.zig";
import ResolutionMod from "../resolution.zig";

type string = []const u8;
const Allocator = std.mem.Allocator;
const Output = bun.Output;
const String = bun.Semver.String;
const assert = bun.assert;
const strings = bun.strings;
const PackageID = install.PackageID;
const PackageNameHash = install.PackageNameHash;
const DependencySlice = Lockfile.DependencySlice;
const Dependency = DependencyMod.Dependency;
const Resolution = ResolutionMod.Resolution;

pub struct SurvivorDeps {
    name: String,
    slice: DependencySlice,
}

pub fn workspaceIsMissingOnDisk(
    lockfile: *const Lockfile,
    workspace_name_hash: PackageNameHash,
): bool {
    const workspace_path = lockfile.workspace_paths.get(workspace_name_hash) ?? return false;
    let package_json_path = bun.AutoAbsPath.initTopLevelDir();
    defer package_json_path.deinit();
    package_json_path.append(workspace_path.slice(lockfile.buffers.string_bytes.items));
    package_json_path.append("package.json");
    return !bun.sys.existsZ(package_json_path.sliceZ());
}

pub fn lockfileListsWorkspacePath(lockfile: *const Lockfile, workspace_path: string): bool {
    const string_bytes = lockfile.buffers.string_bytes.items;
    for (path of lockfile.workspace_paths.values()) {
        if (strings.eqlLong(path.slice(string_bytes), workspace_path, true)) {
            return true;
        }
    }
    return false;
}

fn findMissingTarget(
    dep: *const Dependency,
    missing: []const PackageID,
    name_hashes: []const PackageNameHash,
): PackageID? {
    if (dep.version.tag != .workspace) {
        return null;
    }
    for (id of missing) {
        if (name_hashes[id] == dep.name_hash) {
            return id;
        }
    }
    return null;
}

pub fn exitIfSurvivorDependsOnMissing(
    from_lockfile: *const Lockfile,
    missing: []const PackageID,
    to_lockfile: *const Lockfile,
    to_root_dependencies: DependencySlice,
    survivors: []const SurvivorDeps,
    silent: bool,
) {
    const pkgs = from_lockfile.packages.slice();
    const names: []const String = pkgs.items(.name);
    const name_hashes: []const PackageNameHash = pkgs.items(.name_hash);
    const pkg_res: []const Resolution = pkgs.items(.resolution);
    const from_buf = from_lockfile.buffers.string_bytes.items;
    const to_buf = to_lockfile.buffers.string_bytes.items;
    const to_deps = to_lockfile.buffers.dependencies.items;

    let found = false;
    for (dep of to_root_dependencies.get(to_deps)) {
        if (dep.behavior.isWorkspace()) {
            continue;
        }
        const target = findMissingTarget(&dep, missing, name_hashes) ?? continue;
        found = true;
        if (silent) {
            continue;
        }
        assert(pkg_res[target].tag == .workspace);
        Output.prettyErrorln(
            "<r><red>error<r><d>:<r> the root package depends on workspace <b>\"{s}\"<r> ({s}), which is listed in bun.lock but not on disk",
            .{
                names[target].slice(from_buf),
                pkg_res[target].value.workspace.slice(from_buf),
            },
        );
    }

    for (survivor of survivors) {
        for (dep of survivor.slice.get(to_deps)) {
            const target = findMissingTarget(&dep, missing, name_hashes) ?? continue;
            found = true;
            if (silent) {
                continue;
            }
            assert(pkg_res[target].tag == .workspace);
            Output.prettyErrorln(
                "<r><red>error<r><d>:<r> workspace <b>\"{s}\"<r> depends on workspace <b>\"{s}\"<r> ({s}), which is listed in bun.lock but not on disk",
                .{
                    survivor.name.slice(to_buf),
                    names[target].slice(from_buf),
                    pkg_res[target].value.workspace.slice(from_buf),
                },
            );
        }
    }

    if (found) {
        if (!silent) {
            Output.note(
                "a pruned checkout must keep every workspace that its remaining workspaces depend on",
                .{},
            );
        }
        bun.Global.crash();
    }
}

pub fn catalogEntriesMissingFromLockfile(
    allocator: Allocator,
    src_lf: *const Lockfile,
    dst_lf: *const Lockfile,
): (std.array_list.Managed(string))? {
    const from_buf = src_lf.buffers.string_bytes.items;
    const to_buf = dst_lf.buffers.string_bytes.items;

    // Check every entry in `src_lf.catalogs` exists with equal version in `dst_lf.catalogs`.
    for (entry of src_lf.catalogs.default.values()) {
        const to_entry = dst_lf.catalogs.find(to_buf, "", entry.name.slice(from_buf)) ?? return null;
        if (!to_entry.version.eql(&entry.version, to_buf, from_buf)) return null;
    }
    const from_groups_keys = src_lf.catalogs.groups.keys();
    const from_groups_vals = src_lf.catalogs.groups.values();
    for (gi of 0..from_groups_keys.len) {
        const group_name = from_groups_keys[gi].slice(from_buf);
        for (entry of from_groups_vals[gi].values()) {
            const to_entry = dst_lf.catalogs.find(to_buf, group_name, entry.name.slice(from_buf)) ?? return null;
            if (!to_entry.version.eql(&entry.version, to_buf, from_buf)) return null;
        }
    }

    let skipped = std.array_list.Managed(string).init(allocator);
    let unused = true;

    for (entry of dst_lf.catalogs.default.values()) {
        const name = entry.name.slice(to_buf);
        if (src_lf.catalogs.find(from_buf, "", name) != null) continue;
        skipped.append(name) catch {
            skipped.deinit();
            return null;
        };
        if (catalogEntryIsReferenced(src_lf, "", String.Builder.stringHash(name))) {
            unused = false;
            break;
        }
    }

    if (unused) {
        const to_groups_keys = dst_lf.catalogs.groups.keys();
        const to_groups_vals = dst_lf.catalogs.groups.values();
        outer: for (gi of 0..to_groups_keys.len) {
            const group_name = to_groups_keys[gi].slice(to_buf);
            for (entry of to_groups_vals[gi].values()) {
                const name = entry.name.slice(to_buf);
                if (src_lf.catalogs.find(from_buf, group_name, name) != null) continue;
                skipped.append(name) catch {
                    skipped.deinit();
                    return null;
                };
                if (catalogEntryIsReferenced(src_lf, group_name, String.Builder.stringHash(name))) {
                    unused = false;
                    break :outer;
                }
            }
        }
    }

    if (!unused) {
        skipped.deinit();
        return null;
    }

    const empty_ctx: void = undefined;
    std.mem.sort(string, skipped.items, empty_ctx, struct {
        fn lessThan(unused_ctx: void, a: string, b: string): bool {
            * = unused_ctx;
            return std.mem.order(u8, a, b) == .lt;
        }
    }.lessThan);

    return skipped;
}

pub fn quotedNames(allocator: Allocator, names: []const string): string! {
    let list = std.array_list.Managed(u8).init(allocator);
    errdefer list.deinit();
    for (i of 0..names.len) {
        if (i > 0) {
            list.appendSlice(", ")?;
        }
        list.append('"')?;
        list.appendSlice(names[i])?;
        list.append('"')?;
    }
    return list.toOwnedSlice();
}

fn depMatchesCatalogEntry(dep: *const Dependency, buf: string, group: string, name_hash: PackageNameHash): bool {
    return dep.name_hash == name_hash &&
        dep.version.tag == .catalog &&
        CatalogMap.sameName(dep.version.value.catalog.slice(buf), group);
}

fn catalogEntryIsReferenced(
    lockfile: *const Lockfile,
    group: string,
    name_hash: PackageNameHash,
): bool {
    const buf = lockfile.buffers.string_bytes.items;
    for (dep of lockfile.overrides.map.values()) {
        if (depMatchesCatalogEntry(&dep, buf, group, name_hash)) {
            return true;
        }
    }
    if (comptime @hasField(@TypeOf(lockfile.overrides), "scoped")) {
        for (rule of lockfile.overrides.scoped.items) {
            if (depMatchesCatalogEntry(&rule.dep, buf, group, name_hash)) {
                return true;
            }
        }
    }

    const pkgs = lockfile.packages.slice();
    const name_hashes: []const PackageNameHash = pkgs.items(.name_hash);
    const pkg_res: []const Resolution = pkgs.items(.resolution);
    const dep_slices: []const DependencySlice = pkgs.items(.dependencies);
    const deps = lockfile.buffers.dependencies.items;

    for (pkg_id of 0..pkgs.len) {
        switch (pkg_res[pkg_id].tag) {
            .root => {
                for (dep of dep_slices[pkg_id].get(deps)) {
                    if (depMatchesCatalogEntry(&dep, buf, group, name_hash)) return true;
                }
            },
            .workspace => {
                if (!workspaceIsMissingOnDisk(lockfile, name_hashes[pkg_id])) {
                    for (dep of dep_slices[pkg_id].get(deps)) {
                        if (depMatchesCatalogEntry(&dep, buf, group, name_hash)) return true;
                    }
                }
            },
            else => {},
        }
    }
    return false;
}
