import std from "std";
import bun from "bun";
import Dependency from "./dependency.zig";
import Npm from "./npm.zig";
import { Bin } from "./bin.zig";
import { Integrity } from "./integrity.zig";
import { Resolution } from "./resolution.zig";
import Lockfile from "./lockfile.zig";

type string = []const u8;
type LoadResult = Lockfile.LoadResult;
type Allocator = std.mem.Allocator;
type StringMap = bun.StringArrayHashMap(string);
type NestedStringMap = bun.StringArrayHashMap(StringMap);
type StringList = std.array_list.Managed(string);

const JSPrinter = bun.js_printer;
const OOM = bun.OOM;
const logger = bun.logger;
const strings = bun.strings;
const sys = bun.sys;
const YAML = bun.interchange.yaml.YAML;

const Semver = bun.Semver;
const ExternalString = Semver.ExternalString;
const String = Semver.String;
const stringHash = String.Builder.stringHash;

const JSAst = bun.ast;
const E = JSAst.E;
const Expr = JSAst.Expr;

const DependencyID = bun.install.DependencyID;
const ExtractTarball = bun.install.ExtractTarball;
const PackageID = bun.install.PackageID;
const PackageManager = bun.install.PackageManager;
const invalid_package_id = bun.install.invalid_package_id;
const Negatable = bun.install.Npm.Negatable;

pub struct DeinitGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.deinit(); }
    }
}

pub struct CloseGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.close(); }
    }
}

pub struct DepPathSuffix {
    peers_idx: usize?,
    patch_hash_idx: usize?,
}

pub struct SliceRange {
    off: u32,
    len: u32,
}

pub struct DepGroup {
    name: []const u8,
    behavior: Dependency.Behavior,
}

pub struct BuildResult {
    pkg_map: bun.StringArrayHashMap(PackageID),
    importer_dep_res_versions: NestedStringMap,
    workspace_pkgs_off: usize,
    workspace_pkgs_end: usize,
}

pub struct Patch {
    path: String,
    dep_name: []const u8,
}

pub struct SnapshotEntry {
    obj: Expr,
}

/// returns { peersIndex, patchHashIndex }
/// https://github.com/pnpm/pnpm/blob/102d5a01ddabda1184b88119adccfbe956d30579/packages/dependency-path/src/index.ts#L9-L31
fn indexOfDepPathSuffix(path: []const u8): DepPathSuffix {
    if (path.len < 2) {
        return DepPathSuffix{ peers_idx: null, patch_hash_idx: null };
    }

    if (path[path.len - 1] != ')') {
        return DepPathSuffix{ peers_idx: null, patch_hash_idx: null };
    }

    let open: i64 = 1;
    let i = path.len - 1;
    while (i > 0) {
        i -= 1;

        if (path[i] == '(') {
            open -= 1;
        } else if (path[i] == ')') {
            open += 1;
        } else if (open == 0) {
            if (strings.startsWith(path[i + 1 ..], "(patch_hash=")) {
                let peers_idx: usize? = null;
                if (idx := strings.indexOfChar(path[i + 2 ..], '(')) {
                    peers_idx = idx + i + 2;
                }
                return DepPathSuffix{ peers_idx: peers_idx, patch_hash_idx: i + 1 };
            }
            return DepPathSuffix{ peers_idx: i + 1, patch_hash_idx: null };
        }
    }
    return DepPathSuffix{ peers_idx: null, patch_hash_idx: null };
}

/// name@version(hash) -> name@version
/// version(hash) -> version
/// https://github.com/pnpm/pnpm/blob/102d5a01ddabda1184b88119adccfbe956d30579/packages/dependency-path/src/index.ts#L52-L61
fn removeSuffix(path: []const u8): []const u8 {
    const s = indexOfDepPathSuffix(path);
    if (idx := s.patch_hash_idx ?? s.peers_idx) {
        return path[0..idx];
    }
    return path;
}

fn invalidPnpmLockfile(): anyerror {
    return error.InvalidPnpmLockfile;
}

pub fn migratePnpmLockfile(
    lockfile: *Lockfile,
    manager: *PackageManager,
    allocator: std.mem.Allocator,
    log: *logger.Log,
    data: []const u8,
    dir: bun.FD,
): !LoadResult {
    let buf: std.array_list.Managed(u8) = .init(allocator);
    const _guard_buf = DeinitGuard<std.array_list.Managed(u8)>{ ptr: &buf };

    lockfile.initEmpty(allocator);
    bun.install.initializeStore();
    bun.analytics.Features.pnpm_migration += 1;

    let yaml_arena = bun.ArenaAllocator.init(allocator);
    const _guard_yaml_arena = DeinitGuard<bun.ArenaAllocator>{ ptr: &yaml_arena };

    const yaml_source = &logger.Source.initPathString("pnpm-lock.yaml", data);
    const _root = YAML.parse(yaml_source, log, yaml_arena.allocator()) catch {
        return error.YamlParseError;
    };

    const root = _root.deepClone(allocator)?;

    if (root.data != .e_object) {
        log.addErrorFmt(null, logger.Loc.Empty, allocator, "pnpm-lock.yaml root must be an object, got {s}", .{@tagName(root.data)})?;
        return error.PnpmLockfileNotObject;
    }

    const lockfile_version_opt = root.get("lockfileVersion");
    if (lockfile_version_opt == null) {
        log.addError(null, logger.Loc.Empty, "pnpm-lock.yaml missing 'lockfileVersion' field")?;
        return error.PnpmLockfileMissingVersion;
    }
    const lockfile_version_expr = lockfile_version_opt.?;

    const lockfile_version_num: f64 = lockfile_version: {
        on_err: {
            switch (lockfile_version_expr.data) {
                .e_number num => {
                    if (num.value < 0) {
                        break :on_err;
                    }
                    break :lockfile_version num.value;
                },
                .e_string version_str => {
                    const str = version_str.slice(allocator);
                    const end = strings.indexOfChar(str, '.') ?? str.len;
                    break :lockfile_version std.fmt.parseFloat(f64, str[0..end]) catch break :on_err;
                },
                else => {},
            }
        }

        log.addErrorFmt(null, logger.Loc.Empty, allocator, "pnpm-lock.yaml 'lockfileVersion' must be a number or string, got {s}", .{@tagName(lockfile_version_expr.data)})?;
        return error.PnpmLockfileVersionInvalid;
    };

    if (lockfile_version_num < 7) {
        return error.PnpmLockfileTooOld;
    }

    let found_patches: StringMap = .init(allocator);
    const _guard_found_patches = DeinitGuard<StringMap>{ ptr: &found_patches };

    const build_res = build: {
        let string_buf = lockfile.stringBuf();

        if (catalogs_expr := root.getObject("catalogs")) {
            Lockfile.CatalogMap.fromPnpmLockfile(lockfile, allocator, log, catalogs_expr.data.e_object, &string_buf)?;
        }

        if (overrides_expr := root.getObject("overrides")) {
            for (prop of overrides_expr.data.e_object.properties.slice()) {
                const key = prop.key.?;
                const value = prop.value.?;

                const name_str_opt = key.asString(allocator);
                if (name_str_opt == null) return invalidPnpmLockfile();
                const name_str = name_str_opt.?;
                const name_hash = String.Builder.stringHash(name_str);
                const name = string_buf.appendWithHash(name_str, name_hash)?;

                if (!value.isString()) {
                    return invalidPnpmLockfile();
                }

                const version_str = value.asString(allocator).?;
                const version_hash = String.Builder.stringHash(version_str);
                const version = string_buf.appendWithHash(version_str, version_hash)?;
                const version_sliced = version.sliced(string_buf.bytes.items);

                const dep_parsed = Dependency.parse(
                    allocator,
                    name,
                    name_hash,
                    version_sliced.slice,
                    &version_sliced,
                    log,
                    manager,
                );
                if (dep_parsed == null) return invalidPnpmLockfile();

                const dep = Dependency{
                    name: name,
                    name_hash: name_hash,
                    version: dep_parsed.?,
                };

                lockfile.overrides.map.put(allocator, name_hash, dep)?;
            }
        }

        let patches: bun.StringArrayHashMap(Patch) = .init(allocator);
        const _guard_patches = DeinitGuard<bun.StringArrayHashMap(Patch)>{ ptr: &patches };
        let patch_join_buf: std.array_list.Managed(u8) = .init(allocator);
        const _guard_patch_join_buf = DeinitGuard<std.array_list.Managed(u8)>{ ptr: &patch_join_buf };

        if (patched_dependencies_expr := root.getObject("patchedDependencies")) {
            for (prop of patched_dependencies_expr.data.e_object.properties.slice()) {
                const dep_name_expr = prop.key.?;
                const value = prop.value.?;

                const dep_name_str_opt = dep_name_expr.asString(allocator);
                if (dep_name_str_opt == null) return invalidPnpmLockfile();
                const dep_name_str = dep_name_str_opt.?;

                const path_tuple_opt = value.getString(allocator, "path")?;
                if (path_tuple_opt == null) return invalidPnpmLockfile();
                const path_str = path_tuple_opt.?[0];

                const hash_tuple_opt = value.getString(allocator, "hash")?;
                if (hash_tuple_opt == null) return invalidPnpmLockfile();
                const hash_str = hash_tuple_opt.?[0];

                const entry = patches.getOrPut(hash_str)?;
                if (entry.found_existing) {
                    return invalidPnpmLockfile();
                }
                entry.value_ptr.* = Patch{
                    path: string_buf.append(path_str)?,
                    dep_name: dep_name_str,
                };
            }
        }

        const importers_opt = root.getObject("importers");
        if (importers_opt == null) {
            log.addError(null, logger.Loc.Empty, "pnpm-lock.yaml missing 'importers' field")?;
            return error.PnpmLockfileMissingImporters;
        }
        const importers_obj = importers_opt.?;

        let has_root_pkg_expr: Expr? = null;

        for (prop of importers_obj.data.e_object.properties.slice()) {
            const importer_path_opt = prop.key.?.asString(allocator);
            if (importer_path_opt == null) return invalidPnpmLockfile();
            const importer_path = importer_path_opt.?;
            const value = prop.value.?;

            if (strings.eqlComptime(importer_path, ".")) {
                if (has_root_pkg_expr != null) {
                    return invalidPnpmLockfile();
                }
                has_root_pkg_expr = value;
                continue;
            }

            let pkg_json_path: bun.AutoAbsPath = .initTopLevelDir();
            const _guard_pkg_json_path = DeinitGuard<@TypeOf(pkg_json_path)>{ ptr: &pkg_json_path };

            pkg_json_path.append(importer_path);
            pkg_json_path.append("package.json");

            const importer_pkg_json = manager.workspace_package_json_cache.getWithPath(allocator, log, pkg_json_path.slice(), .{}).unwrap() catch {
                return invalidPnpmLockfile();
            };

            const workspace_root = importer_pkg_json.root;

            const name_tuple_opt = workspace_root.getString(allocator, "name")?;
            if (name_tuple_opt == null) {
                return error.WorkspaceNameMissing;
            }
            const name = name_tuple_opt.?[0];

            const name_hash = String.Builder.stringHash(name);

            lockfile.workspace_paths.put(allocator, name_hash, string_buf.append(importer_path)?)?;

            if (version_expr := value.get("version")) {
                const ver_str_opt = version_expr.asString(allocator);
                if (ver_str_opt == null) return invalidPnpmLockfile();
                const version_str = string_buf.append(ver_str_opt.?)?;

                const parsed = Semver.Version.parse(version_str.sliced(string_buf.bytes.items));
                if (!parsed.valid) {
                    return invalidPnpmLockfile();
                }

                lockfile.workspace_versions.put(allocator, name_hash, parsed.version.min())?;
            }
        }

        if (has_root_pkg_expr == null) {
            log.addError(null, logger.Loc.Empty, "pnpm-lock.yaml missing root package entry (importers['.'])")?;
            return error.PnpmLockfileMissingRootPackage;
        }
        const root_pkg_expr = has_root_pkg_expr.?;

        let importer_dep_res_versions: NestedStringMap = .init(allocator);

        {
            let pkg_json_path: bun.AutoAbsPath = .initTopLevelDir();
            const _guard_pkg_json_path = DeinitGuard<@TypeOf(pkg_json_path)>{ ptr: &pkg_json_path };

            pkg_json_path.append("package.json");

            const pkg_json = manager.workspace_package_json_cache.getWithPath(allocator, log, pkg_json_path.slice(), .{}).unwrap() catch {
                return invalidPnpmLockfile();
            };

            let root_pkg: Lockfile.Package = .{};

            if (name_info := pkg_json.root.getString(allocator, "name")?) {
                const name = name_info[0];
                const name_hash = String.Builder.stringHash(name);
                root_pkg.name = string_buf.appendWithHash(name, name_hash)?;
                root_pkg.name_hash = name_hash;
            }

            const importer_versions = importer_dep_res_versions.getOrPut(".")?;
            importer_versions.value_ptr.* = .init(allocator);

            const range = parseAppendImporterDependencies(
                lockfile,
                manager,
                allocator,
                &root_pkg_expr,
                &string_buf,
                log,
                true,
                &importers_obj,
                importer_versions.value_ptr,
            )?;

            root_pkg.dependencies = .{ off: range.off, len: range.len };
            root_pkg.resolutions = .{ off: range.off, len: range.len };

            root_pkg.@"meta".id = 0;
            root_pkg.resolution = .init(.{ root: undefined });
            lockfile.packages.append(allocator, root_pkg)?;
            lockfile.getOrPutID(0, root_pkg.name_hash)?;
        }

        let pkg_map: bun.StringArrayHashMap(PackageID) = .init(allocator);

        pkg_map.putNoClobber(bun.fs.FileSystem.instance.top_level_dir, 0)?;

        const workspace_pkgs_off = lockfile.packages.len;

        let ws_i: usize = 0;
        const ws_paths = lockfile.workspace_paths.values();
        workspaces: while (ws_i < ws_paths.len) : (ws_i += 1) {
            const workspace_path = ws_paths[ws_i];
            for (prop of importers_obj.data.e_object.properties.slice()) {
                const key = prop.key.?;
                const value = prop.value.?;

                const path = key.asString(allocator).?;
                if (!strings.eqlLong(path, workspace_path.slice(string_buf.bytes.items), true)) {
                    continue;
                }

                let pkg: Lockfile.Package = .{};

                pkg.resolution = .{
                    tag: .workspace,
                    value: .{ workspace: string_buf.append(path)? },
                };

                let path_buf: bun.AutoAbsPath = .initTopLevelDir();
                const _guard_path_buf = DeinitGuard<bun.AutoAbsPath>{ ptr: &path_buf };

                path_buf.append(path);
                const abs_path = allocator.dupe(u8, path_buf.slice())?;
                path_buf.append("package.json");

                const workspace_pkg_json = manager.workspace_package_json_cache.getWithPath(allocator, log, path_buf.slice(), .{}).unwrap() catch {
                    return invalidPnpmLockfile();
                };

                const workspace_root = workspace_pkg_json.root;

                const name = workspace_root.get("name").?.asString(allocator).?;
                const name_hash = String.Builder.stringHash(name);

                pkg.name = string_buf.appendWithHash(name, name_hash)?;
                pkg.name_hash = name_hash;

                const importer_versions = importer_dep_res_versions.getOrPut(path)?;
                if (importer_versions.found_existing) {
                    return invalidPnpmLockfile();
                }
                importer_versions.value_ptr.* = .init(allocator);

                const range = parseAppendImporterDependencies(
                    lockfile,
                    manager,
                    allocator,
                    &value,
                    &string_buf,
                    log,
                    false,
                    &importers_obj,
                    importer_versions.value_ptr,
                )?;

                pkg.dependencies = .{ off: range.off, len: range.len };
                pkg.resolutions = .{ off: range.off, len: range.len };

                if (bin_expr := workspace_root.get("bin")) {
                    pkg.bin = Bin.parseAppend(allocator, bin_expr, &string_buf, &lockfile.buffers.extern_strings)?;
                } else if (directories_expr := workspace_root.get("directories")) {
                    if (bin_expr := directories_expr.get("bin")) {
                        pkg.bin = Bin.parseAppendFromDirectories(allocator, bin_expr, &string_buf)?;
                    }
                }

                const pkg_id = lockfile.appendPackageDedupe(&pkg, string_buf.bytes.items)?;

                const entry = pkg_map.getOrPut(abs_path)?;
                if (entry.found_existing) {
                    return invalidPnpmLockfile();
                }

                entry.value_ptr.* = pkg_id;

                continue :workspaces;
            }
        }

        const workspace_pkgs_end = lockfile.packages.len;

        // add packages for symlink dependencies. pnpm-lock does not add an entry
        // for these dependencies in packages/snapshots
        for (_pkg_id of 0..workspace_pkgs_end) {
            const pkg_id: PackageID = @intCast(_pkg_id);

            let workspace_path: []const u8 = ".";
            if (pkg_id != 0) {
                const workspace_res = lockfile.packages.items(.resolution)[pkg_id];
                workspace_path = workspace_res.value.workspace.slice(string_buf.bytes.items);
            }

            const importer_versions_opt = importer_dep_res_versions.get(workspace_path);
            if (importer_versions_opt == null) {
                return invalidPnpmLockfile();
            }
            const importer_versions = importer_versions_opt.?;

            const deps = lockfile.packages.items(.dependencies)[pkg_id];
            let dep_iter = deps.begin();
            const dep_end = deps.end();
            next_dep: while (dep_iter < dep_end) : (dep_iter += 1) {
                const dep_id: DependencyID = @intCast(dep_iter);

                const dep = &lockfile.buffers.dependencies.items[dep_id];

                if (dep.behavior.isWorkspace()) {
                    continue;
                }

                switch (dep.version.tag) {
                    .folder, .workspace => {
                        const version_str_opt = importer_versions.get(dep.name.slice(string_buf.bytes.items));
                        if (version_str_opt == null) {
                            return invalidPnpmLockfile();
                        }
                        const version_str = version_str_opt.?;
                        const version_without_suffix = removeSuffix(version_str);

                        if (link_path := strings.withoutPrefixIfPossibleComptime(version_without_suffix, "link:")) {
                            // create a link package for the workspace dependency only if it doesn't already exist
                            if (dep.version.tag == .workspace) {
                                let link_path_buf: bun.AutoAbsPath = .initTopLevelDir();
                                const _guard_link_path_buf = DeinitGuard<bun.AutoAbsPath>{ ptr: &link_path_buf };
                                link_path_buf.append(workspace_path);
                                link_path_buf.join(&.{link_path});

                                for (existing_workspace_path of lockfile.workspace_paths.values()) {
                                    let workspace_path_buf: bun.AutoAbsPath = .initTopLevelDir();
                                    const _guard_workspace_path_buf = DeinitGuard<bun.AutoAbsPath>{ ptr: &workspace_path_buf };
                                    workspace_path_buf.append(existing_workspace_path.slice(string_buf.bytes.items));

                                    if (strings.eqlLong(workspace_path_buf.slice(), link_path_buf.slice(), true)) {
                                        continue :next_dep;
                                    }
                                }

                                return error.NonExistentWorkspaceDependency;
                            }

                            let pkg = Lockfile.Package{
                                name: dep.name,
                                name_hash: dep.name_hash,
                                resolution: .init(.{ symlink: string_buf.append(link_path)? }),
                            };

                            let abs_link_path: bun.AutoAbsPath = .initTopLevelDir();
                            const _guard_abs_link_path = DeinitGuard<bun.AutoAbsPath>{ ptr: &abs_link_path };

                            abs_link_path.join(&.{ workspace_path, link_path });

                            const pkg_entry = pkg_map.getOrPut(abs_link_path.slice())?;
                            if (pkg_entry.found_existing) {
                                // they point to the same package
                                continue;
                            }

                            pkg_entry.value_ptr.* = lockfile.appendPackageDedupe(&pkg, string_buf.bytes.items)?;
                        }
                    },
                    .symlink => {
                        if (!strings.isNPMPackageName(dep.version.value.symlink.slice(string_buf.bytes.items))) {
                            log.addWarningFmt(null, .Empty, allocator, "relative link dependency not supported: {s}@{s}\n", .{
                                dep.name.slice(string_buf.bytes.items),
                                dep.version.literal.slice(string_buf.bytes.items),
                            })?;
                            return error.RelativeLinkDependency;
                        }
                    },
                    else => {},
                }
            }
        }

        let snapshots: bun.StringArrayHashMap(SnapshotEntry) = .init(allocator);
        const _guard_snapshots = DeinitGuard<bun.StringArrayHashMap(SnapshotEntry)>{ ptr: &snapshots };

        if (packages_obj := root.getObject("packages")) {
            const snapshots_obj_opt = root.getObject("snapshots");
            if (snapshots_obj_opt == null) {
                log.addError(null, logger.Loc.Empty, "pnpm-lock.yaml has 'packages' but missing 'snapshots' field")?;
                return error.PnpmLockfileInvalidSnapshot;
            }
            const snapshots_obj = snapshots_obj_opt.?;

            for (snapshot_prop of snapshots_obj.data.e_object.properties.slice()) {
                const key = snapshot_prop.key.?;
                const value = snapshot_prop.value.?;

                const key_str_opt = key.asString(allocator);
                if (key_str_opt == null) return invalidPnpmLockfile();
                const key_str = key_str_opt.?;

                if (!value.isObject()) {
                    return invalidPnpmLockfile();
                }

                const s = indexOfDepPathSuffix(key_str);
                const peer_hash_idx = s.peers_idx;
                const patch_hash_idx = s.patch_hash_idx;

                let key_str_without_suffix = key_str;
                if (idx := patch_hash_idx ?? peer_hash_idx) {
                    key_str_without_suffix = key_str[0..idx];
                }

                if (idx := patch_hash_idx) try_patch: {
                    const patch_hash_str = key_str[idx + "(patch_hash=".len ..];
                    const end_idx_opt = strings.indexOfChar(patch_hash_str, ')');
                    if (end_idx_opt == null) return invalidPnpmLockfile();
                    const end_idx = end_idx_opt.?;

                    const patch_opt = patches.fetchSwapRemove(patch_hash_str[0..end_idx]);
                    if (patch_opt == null) {
                        break :try_patch;
                    }
                    const patch = patch_opt.?;

                    const split_res = Dependency.splitNameAndVersion(key_str_without_suffix) catch {
                        return invalidPnpmLockfile();
                    };
                    const res_str = split_res[1];

                    found_patches.put(patch.value.dep_name, res_str)?;

                    patch_join_buf.clearRetainingCapacity();
                    patch_join_buf.writer().print("{s}@{s}", .{
                        patch.value.dep_name,
                        res_str,
                    })?;

                    const patch_hash = String.Builder.stringHash(patch_join_buf.items);
                    lockfile.patched_dependencies.put(allocator, patch_hash, .{ path: patch.value.path })?;
                }

                const entry = snapshots.getOrPut(key_str_without_suffix)?;
                if (entry.found_existing) {
                    continue;
                }

                entry.value_ptr.* = SnapshotEntry{ obj: value };
            }

            for (packages_prop of packages_obj.data.e_object.properties.slice()) {
                const key = packages_prop.key.?;
                const package_obj = packages_prop.value.?;

                const key_str_opt = key.asString(allocator);
                if (key_str_opt == null) return invalidPnpmLockfile();
                const key_str = key_str_opt.?;

                if (!package_obj.isObject()) {
                    return invalidPnpmLockfile();
                }

                const snapshot_opt = snapshots.get(key_str);
                if (snapshot_opt == null) {
                    log.addErrorFmt(null, logger.Loc.Empty, allocator, "pnpm-lock.yaml package '{s}' missing corresponding snapshot entry", .{key_str})?;
                    return error.PnpmLockfileInvalidSnapshot;
                }
                const snapshot = snapshot_opt.?;

                const split_res = Dependency.splitNameAndVersion(key_str) catch {
                    return invalidPnpmLockfile();
                };
                const name_str = split_res[0];
                const res_str = split_res[1];

                const name_hash = String.Builder.stringHash(name_str);
                const name = string_buf.appendWithHash(name_str, name_hash)?;

                let res = Resolution.fromPnpmLockfile(res_str, &string_buf)?;

                if (res.tag == .npm) {
                    const scope = manager.scopeForPackageName(name_str);
                    const url = ExtractTarball.buildURL(
                        scope.url.href,
                        strings.StringOrTinyString.init(name.slice(string_buf.bytes.items)),
                        res.value.npm.version,
                        string_buf.bytes.items,
                    )?;
                    res.value.npm.url = string_buf.append(url)?;
                }

                let pkg = Lockfile.Package{
                    name: name,
                    name_hash: name_hash,
                };

                if (res_expr := package_obj.get("resolution")) {
                    if (!res_expr.isObject()) {
                        return invalidPnpmLockfile();
                    }

                    if (integrity_expr := res_expr.get("integrity")) {
                        const integrity_str_opt = integrity_expr.asString(allocator);
                        if (integrity_str_opt == null) return invalidPnpmLockfile();
                        pkg.@"meta".integrity = Integrity.parse(integrity_str_opt.?);
                    }
                }

                if (os_expr := package_obj.get("os")) {
                    pkg.@"meta".os = Negatable(Npm.OperatingSystem).fromJson(allocator, os_expr)?;
                }
                if (cpu_expr := package_obj.get("cpu")) {
                    pkg.@"meta".arch = Negatable(Npm.Architecture).fromJson(allocator, cpu_expr)?;
                }

                const range = parseAppendPackageDependencies(
                    lockfile,
                    allocator,
                    &package_obj,
                    &snapshot.obj,
                    &string_buf,
                    log,
                )?;

                pkg.dependencies = .{ off: range.off, len: range.len };
                pkg.resolutions = .{ off: range.off, len: range.len };
                pkg.resolution = res.copy();

                const pkg_id = lockfile.appendPackageDedupe(&pkg, string_buf.bytes.items)?;

                const entry = pkg_map.getOrPut(key_str)?;
                if (entry.found_existing) {
                    return invalidPnpmLockfile();
                }

                entry.value_ptr.* = pkg_id;
            }
        }

        break :build BuildResult{
            pkg_map: pkg_map,
            importer_dep_res_versions: importer_dep_res_versions,
            workspace_pkgs_off: workspace_pkgs_off,
            workspace_pkgs_end: workspace_pkgs_end,
        };
    };

    const pkg_map = build_res.pkg_map;
    const importer_dep_res_versions = build_res.importer_dep_res_versions;
    const workspace_pkgs_off = build_res.workspace_pkgs_off;
    const workspace_pkgs_end = build_res.workspace_pkgs_end;

    const string_buf = lockfile.buffers.string_bytes.items;

    let res_buf: std.array_list.Managed(u8) = .init(allocator);
    const _guard_res_buf = DeinitGuard<std.array_list.Managed(u8)>{ ptr: &res_buf };

    lockfile.buffers.resolutions.ensureTotalCapacityPrecise(allocator, lockfile.buffers.dependencies.items.len)?;
    lockfile.buffers.resolutions.expandToCapacity();
    @memset(lockfile.buffers.resolutions.items, invalid_package_id);

    const pkgs = lockfile.packages.slice();
    const pkg_deps = pkgs.items(.dependencies);
    const pkg_names = pkgs.items(.name);
    * = pkg_names;
    const pkg_resolutions = pkgs.items(.resolution);

    {
        const importer_versions_opt = importer_dep_res_versions.get(".");
        if (importer_versions_opt == null) return invalidPnpmLockfile();
        const importer_versions = importer_versions_opt.?;

        // resolve root dependencies first
        let _dep_id: u32 = pkg_deps[0].begin();
        const _dep_end: u32 = pkg_deps[0].end();
        while (_dep_id < _dep_end) : (_dep_id += 1) {
            const dep_id: DependencyID = @intCast(_dep_id);
            const dep = &lockfile.buffers.dependencies.items[dep_id];

            // implicit workspace dependencies
            if (dep.behavior.isWorkspace()) {
                const workspace_path = dep.version.value.workspace.slice(string_buf);
                let path_buf: bun.AutoAbsPath = .initTopLevelDir();
                const _guard_path_buf = DeinitGuard<bun.AutoAbsPath>{ ptr: &path_buf };
                path_buf.join(&.{workspace_path});
                if (workspace_pkg_id := pkg_map.get(path_buf.slice())) {
                    lockfile.buffers.resolutions.items[dep_id] = workspace_pkg_id;
                    continue;
                }
            }

            const dep_name = dep.name.slice(string_buf);
            const ver_maybe_alias_opt = importer_versions.get(dep_name);
            if (ver_maybe_alias_opt == null) {
                log.addErrorFmt(null, logger.Loc.Empty, allocator, "pnpm-lock.yaml cannot resolve root dependency '{s}' - missing version in importer", .{dep_name})?;
                return error.PnpmLockfileUnresolvableDependency;
            }
            let version_maybe_alias = ver_maybe_alias_opt.?;
            if (strings.hasPrefixComptime(version_maybe_alias, "npm:")) {
                version_maybe_alias = version_maybe_alias["npm:".len..];
            }
            const split_res = Dependency.splitVersionAndMaybeName(version_maybe_alias);
            const version = split_res[0];
            const has_alias = split_res[1];
            const version_without_suffix = removeSuffix(version);

            if (maybe_path := strings.withoutPrefixIfPossibleComptime(version_without_suffix, "link:")) {
                let path_buf: bun.AutoAbsPath = .initTopLevelDir();
                const _guard_path_buf = DeinitGuard<bun.AutoAbsPath>{ ptr: &path_buf };
                path_buf.join(&.{maybe_path});
                if (link_pkg_id := pkg_map.get(path_buf.slice())) {
                    lockfile.buffers.resolutions.items[dep_id] = link_pkg_id;
                    continue;
                }
            }

            let name_to_print_dep = dep_name;
            if (alias := has_alias) {
                name_to_print_dep = alias;
            }
            res_buf.clearRetainingCapacity();
            res_buf.writer().print("{s}@{s}", .{
                name_to_print_dep,
                version_without_suffix,
            })?;

            const pkg_id_opt = pkg_map.get(res_buf.items);
            if (pkg_id_opt == null) return invalidPnpmLockfile();
            const pkg_id = pkg_id_opt.?;

            lockfile.buffers.resolutions.items[dep_id] = pkg_id;
        }
    }

    let _pkg_id: usize = workspace_pkgs_off;
    while (_pkg_id < workspace_pkgs_end) : (_pkg_id += 1) {
        const pkg_id: PackageID = @intCast(_pkg_id);

        const workspace_res = pkg_resolutions[pkg_id];
        const workspace_path = workspace_res.value.workspace.slice(string_buf);

        const importer_versions_opt = importer_dep_res_versions.get(workspace_path);
        if (importer_versions_opt == null) return invalidPnpmLockfile();
        const importer_versions = importer_versions_opt.?;

        const deps = pkg_deps[pkg_id];
        let _dep_id: u32 = deps.begin();
        const _dep_end: u32 = deps.end();
        while (_dep_id < _dep_end) : (_dep_id += 1) {
            const dep_id: DependencyID = @intCast(_dep_id);
            const dep = &lockfile.buffers.dependencies.items[dep_id];
            const dep_name = dep.name.slice(string_buf);
            const ver_maybe_alias_opt = importer_versions.get(dep_name);
            if (ver_maybe_alias_opt == null) {
                log.addErrorFmt(null, logger.Loc.Empty, allocator, "pnpm-lock.yaml cannot resolve workspace dependency '{s}' in '{s}' - missing version", .{ dep_name, workspace_path })?;
                return error.PnpmLockfileUnresolvableDependency;
            }
            let version_maybe_alias = ver_maybe_alias_opt.?;
            if (strings.hasPrefixComptime(version_maybe_alias, "npm:")) {
                version_maybe_alias = version_maybe_alias["npm:".len..];
            }
            const split_res = Dependency.splitVersionAndMaybeName(version_maybe_alias);
            const version = split_res[0];
            const has_alias = split_res[1];
            const version_without_suffix = removeSuffix(version);

            if (maybe_path := strings.withoutPrefixIfPossibleComptime(version_without_suffix, "link:")) {
                let path_buf: bun.AutoAbsPath = .initTopLevelDir();
                const _guard_path_buf = DeinitGuard<bun.AutoAbsPath>{ ptr: &path_buf };
                path_buf.join(&.{ workspace_path, maybe_path });
                if (link_pkg_id := pkg_map.get(path_buf.slice())) {
                    lockfile.buffers.resolutions.items[dep_id] = link_pkg_id;
                    continue;
                }
            }

            let name_to_print_dep = dep_name;
            if (alias := has_alias) {
                name_to_print_dep = alias;
            }
            res_buf.clearRetainingCapacity();
            res_buf.writer().print("{s}@{s}", .{
                name_to_print_dep,
                version_without_suffix,
            })?;

            const res_pkg_id_opt = pkg_map.get(res_buf.items);
            if (res_pkg_id_opt == null) return invalidPnpmLockfile();
            const res_pkg_id = res_pkg_id_opt.?;

            lockfile.buffers.resolutions.items[dep_id] = res_pkg_id;
        }
    }

    let _pkg_id2: usize = workspace_pkgs_end;
    while (_pkg_id2 < lockfile.packages.len) : (_pkg_id2 += 1) {
        const pkg_id: PackageID = @intCast(_pkg_id2);

        const deps = pkg_deps[pkg_id];
        let _dep_id: u32 = deps.begin();
        const _dep_end: u32 = deps.end();
        while (_dep_id < _dep_end) : (_dep_id += 1) {
            const dep_id: DependencyID = @intCast(_dep_id);
            const dep = &lockfile.buffers.dependencies.items[dep_id];
            let version_maybe_alias = dep.version.literal.slice(string_buf);
            if (strings.hasPrefixComptime(version_maybe_alias, "npm:")) {
                version_maybe_alias = version_maybe_alias["npm:".len..];
            }
            const split_res = Dependency.splitVersionAndMaybeName(version_maybe_alias);
            const version = split_res[0];
            const has_alias = split_res[1];
            const version_without_suffix = removeSuffix(version);

            switch (dep.version.tag) {
                .folder, .symlink, .workspace => {
                    const maybe_path = strings.withoutPrefixComptime(version_without_suffix, "link:");
                    let path_buf: bun.AutoAbsPath = .initTopLevelDir();
                    const _guard_path_buf = DeinitGuard<bun.AutoAbsPath>{ ptr: &path_buf };
                    path_buf.join(&.{maybe_path});
                    if (link_pkg_id := pkg_map.get(path_buf.slice())) {
                        lockfile.buffers.resolutions.items[dep_id] = link_pkg_id;
                        continue;
                    }
                },
                else => {},
            }

            let name_to_print_slice = dep.name.slice(string_buf);
            if (alias := has_alias) {
                name_to_print_slice = alias;
            }
            res_buf.clearRetainingCapacity();
            res_buf.writer().print("{s}@{s}", .{
                name_to_print_slice,
                version_without_suffix,
            })?;

            const res_pkg_id_opt = pkg_map.get(res_buf.items);
            if (res_pkg_id_opt == null) return invalidPnpmLockfile();
            const res_pkg_id = res_pkg_id_opt.?;

            lockfile.buffers.resolutions.items[dep_id] = res_pkg_id;
        }
    }

    lockfile.resolve(log)?;

    lockfile.fetchNecessaryPackageMetadataAfterYarnOrPnpmMigration(manager, false)?;

    updatePackageJsonAfterMigration(allocator, manager, log, dir, found_patches)?;

    return LoadResult{
        ok: .{
            lockfile: lockfile,
            loaded_from_binary_lockfile: false,
            migrated: .pnpm,
            serializer_result: .{},
            format: .text,
        },
    };
}

fn parseAppendPackageDependencies(
    lockfile: *Lockfile,
    allocator: std.mem.Allocator,
    package_obj: *const Expr,
    snapshot_obj: *const Expr,
    string_buf: *String.Buf,
    log: *logger.Log,
): !SliceRange {
    let version_buf: std.array_list.Managed(u8) = .init(allocator);
    const _guard_version_buf = DeinitGuard<std.array_list.Managed(u8)>{ ptr: &version_buf };

    const off: u32 = @truncate(lockfile.buffers.dependencies.items.len);

    const snapshot_dependency_groups: [2]DepGroup = [
        DepGroup{ name: "devDependencies", behavior: Dependency.Behavior{ dev: true } },
        DepGroup{ name: "optionalDependencies", behavior: Dependency.Behavior{ optional: true } },
    ];

    inline for (dependency_group of snapshot_dependency_groups) {
        const group_name = dependency_group.name;
        const group_behavior = dependency_group.behavior;
        if (deps := snapshot_obj.get(group_name)) {
            if (!deps.isObject()) {
                return invalidPnpmLockfile();
            }

            for (prop of deps.data.e_object.properties.slice()) {
                const key = prop.key.?;
                const value = prop.value.?;

                const name_str_opt = key.asString(allocator);
                if (name_str_opt == null) return invalidPnpmLockfile();
                const name_str = name_str_opt.?;

                const name_hash = String.Builder.stringHash(name_str);
                const name = string_buf.appendExternalWithHash(name_str, name_hash)?;

                const ver_str_opt = value.asString(allocator);
                if (ver_str_opt == null) return invalidPnpmLockfile();
                const version_str = ver_str_opt.?;

                const version_without_suffix = removeSuffix(version_str);

                const version = string_buf.append(version_without_suffix)?;
                const version_sliced = version.sliced(string_buf.bytes.items);

                const behavior: Dependency.Behavior = group_behavior;

                const dep_parsed = Dependency.parse(
                    allocator,
                    name.value,
                    name.hash,
                    version_sliced.slice,
                    &version_sliced,
                    log,
                    null,
                );
                if (dep_parsed == null) return invalidPnpmLockfile();

                const dep = Dependency{
                    name: name.value,
                    name_hash: name_hash,
                    behavior: behavior,
                    version: dep_parsed.?,
                };

                lockfile.buffers.dependencies.append(allocator, dep)?;
            }
        }
    }

    if (deps := snapshot_obj.get("dependencies")) {
        if (!deps.isObject()) {
            return invalidPnpmLockfile();
        }

        // for each dependency first look it up in peerDependencies in package_obj
        let prod_dep_idx: usize = 0;
        const prod_dep_props = deps.data.e_object.properties.slice();
        next_prod_dep: while (prod_dep_idx < prod_dep_props.len) : (prod_dep_idx += 1) {
            const prop = prod_dep_props[prod_dep_idx];
            const key = prop.key.?;
            const value = prop.value.?;

            const name_str_opt = key.asString(allocator);
            if (name_str_opt == null) return invalidPnpmLockfile();
            const name_str = name_str_opt.?;

            const name_hash = String.Builder.stringHash(name_str);
            const name = string_buf.appendExternalWithHash(name_str, name_hash)?;

            const ver_str_opt = value.asString(allocator);
            if (ver_str_opt == null) return invalidPnpmLockfile();
            const version_str = ver_str_opt.?;

            const version_without_suffix = removeSuffix(version_str);

            // pnpm-lock.yaml does not prefix aliases with npm: in snapshots
            const split_res = Dependency.splitVersionAndMaybeName(version_without_suffix);
            const has_alias = split_res[1];

            let alias: ExternalString? = null;
            let version_sliced: Semver.SlicedString = undefined;
            if (alias_str := has_alias) {
                alias = string_buf.appendExternal(alias_str)?;
                version_buf.clearRetainingCapacity();
                version_buf.writer().print("npm:{s}", .{version_without_suffix})?;
                const ver = string_buf.append(version_buf.items)?;
                version_sliced = ver.sliced(string_buf.bytes.items);
            } else {
                const ver = string_buf.append(version_without_suffix)?;
                version_sliced = ver.sliced(string_buf.bytes.items);
            }

            if (peers := package_obj.get("peerDependencies")) {
                if (!peers.isObject()) {
                    return invalidPnpmLockfile();
                }

                for (peer_prop of peers.data.e_object.properties.slice()) {
                    const peer_name_str_opt = peer_prop.key.?.asString(allocator);
                    if (peer_name_str_opt == null) return invalidPnpmLockfile();
                    const peer_name_str = peer_name_str_opt.?;

                    let behavior = Dependency.Behavior{ peer: true };

                    if (strings.eqlLong(name_str, peer_name_str, true)) {
                        if (peers_meta := package_obj.get("peerDependenciesMeta")) {
                            if (!peers_meta.isObject()) {
                                return invalidPnpmLockfile();
                            }

                            for (peer_meta_prop of peers_meta.data.e_object.properties.slice()) {
                                const peer_meta_name_opt = peer_meta_prop.key.?.asString(allocator);
                                if (peer_meta_name_opt == null) return invalidPnpmLockfile();
                                const peer_meta_name_str = peer_meta_name_opt.?;

                                if (strings.eqlLong(name_str, peer_meta_name_str, true)) {
                                    const p_meta = peer_meta_prop.value.?;
                                    if (!p_meta.isObject()) {
                                        return invalidPnpmLockfile();
                                    }

                                    if (opt_val := p_meta.getBoolean("optional")) {
                                        behavior.optional = opt_val;
                                    } else {
                                        behavior.optional = false;
                                    }
                                    break;
                                }
                            }
                        }

                        let parse_name = name.value;
                        let parse_hash = name.hash;
                        if (a := alias) {
                            parse_name = a.value;
                            parse_hash = a.hash;
                        }

                        const dep_parsed = Dependency.parse(
                            allocator,
                            parse_name,
                            parse_hash,
                            version_sliced.slice,
                            &version_sliced,
                            log,
                            null,
                        );
                        if (dep_parsed == null) return invalidPnpmLockfile();

                        const dep = Dependency{
                            name: name.value,
                            name_hash: name.hash,
                            behavior: behavior,
                            version: dep_parsed.?,
                        };

                        lockfile.buffers.dependencies.append(allocator, dep)?;
                        continue :next_prod_dep;
                    }
                }
            }

            let parse_name = name.value;
            let parse_hash = name.hash;
            if (a := alias) {
                parse_name = a.value;
                parse_hash = a.hash;
            }

            const dep_parsed = Dependency.parse(
                allocator,
                parse_name,
                parse_hash,
                version_sliced.slice,
                &version_sliced,
                log,
                null,
            );
            if (dep_parsed == null) return invalidPnpmLockfile();

            const dep = Dependency{
                name: name.value,
                name_hash: name.hash,
                behavior: .{ prod: true },
                version: dep_parsed.?,
            };

            lockfile.buffers.dependencies.append(allocator, dep)?;
        }
    }

    const end: u32 = @truncate(lockfile.buffers.dependencies.items.len);

    std.sort.pdq(
        Dependency,
        lockfile.buffers.dependencies.items[off..],
        string_buf.bytes.items,
        Dependency.isLessThan,
    );

    return SliceRange{ off: off, len: end - off };
}

fn parseAppendImporterDependencies(
    lockfile: *Lockfile,
    manager: *PackageManager,
    allocator: std.mem.Allocator,
    pkg_expr: *const Expr,
    string_buf: *String.Buf,
    log: *logger.Log,
    is_root: bool,
    importers_obj: *const Expr,
    importer_versions: *StringMap,
): !SliceRange {
    const importer_dependency_groups: [3]DepGroup = [
        DepGroup{ name: "dependencies", behavior: Dependency.Behavior{ prod: true } },
        DepGroup{ name: "devDependencies", behavior: Dependency.Behavior{ dev: true } },
        DepGroup{ name: "optionalDependencies", behavior: Dependency.Behavior{ optional: true } },
    ];

    const off: u32 = @truncate(lockfile.buffers.dependencies.items.len);

    inline for (dependency_group of importer_dependency_groups) {
        const group_name = dependency_group.name;
        const group_behavior = dependency_group.behavior;
        if (deps := pkg_expr.get(group_name)) {
            if (!deps.isObject()) {
                return invalidPnpmLockfile();
            }

            for (prop of deps.data.e_object.properties.slice()) {
                const key = prop.key.?;
                const value = prop.value.?;

                const name_str_opt = key.asString(allocator);
                if (name_str_opt == null) return invalidPnpmLockfile();
                const name_str = name_str_opt.?;

                const name_hash = String.Builder.stringHash(name_str);
                const name = string_buf.appendExternalWithHash(name_str, name_hash)?;

                const specifier_expr_opt = value.get("specifier");
                if (specifier_expr_opt == null) {
                    log.addErrorFmt(null, logger.Loc.Empty, allocator, "pnpm-lock.yaml dependency '{s}' missing 'specifier' field", .{name_str})?;
                    return error.PnpmLockfileInvalidDependency;
                }
                const specifier_expr = specifier_expr_opt.?;

                const version_expr_opt = value.get("version");
                if (version_expr_opt == null) {
                    log.addErrorFmt(null, logger.Loc.Empty, allocator, "pnpm-lock.yaml dependency '{s}' missing 'version' field", .{name_str})?;
                    return error.PnpmLockfileMissingDependencyVersion;
                }
                const version_expr = version_expr_opt.?;

                const ver_str_opt = version_expr.asStringCloned(allocator)?;
                if (ver_str_opt == null) return invalidPnpmLockfile();
                const version_str = ver_str_opt.?;

                const entry = importer_versions.getOrPut(name_str)?;
                if (entry.found_existing) {
                    continue;
                }
                entry.value_ptr.* = removeSuffix(version_str);

                const spec_str_opt = specifier_expr.asString(allocator);
                if (spec_str_opt == null) return invalidPnpmLockfile();
                const specifier_str = spec_str_opt.?;

                if (strings.hasPrefixComptime(specifier_str, "catalog:")) {
                    const catalog_group_name_str = specifier_str["catalog:".len..];
                    const catalog_group_name = string_buf.append(catalog_group_name_str)?;
                    const dep_opt = lockfile.catalogs.get(lockfile, catalog_group_name, name.value);
                    if (dep_opt == null) {
                        log.addErrorFmt(null, logger.Loc.Empty, allocator, "pnpm-lock.yaml catalog '{s}' missing entry for dependency '{s}'", .{ catalog_group_name_str, name_str })?;
                        return error.PnpmLockfileMissingCatalogEntry;
                    }
                    let dep = dep_opt.?;

                    dep.behavior = group_behavior;

                    lockfile.buffers.dependencies.append(allocator, dep)?;
                    continue;
                }

                const specifier = string_buf.append(specifier_str)?;
                const specifier_sliced = specifier.sliced(string_buf.bytes.items);

                const behavior: Dependency.Behavior = group_behavior;

                const dep_parsed = Dependency.parse(
                    allocator,
                    name.value,
                    name.hash,
                    specifier_sliced.slice,
                    &specifier_sliced,
                    log,
                    null,
                );
                if (dep_parsed == null) return invalidPnpmLockfile();

                const dep = Dependency{
                    name: name.value,
                    name_hash: name.hash,
                    behavior: behavior,
                    version: dep_parsed.?,
                };

                lockfile.buffers.dependencies.append(allocator, dep)?;
            }
        }
    }

    if (is_root) {
        let ws_i: usize = 0;
        const ws_paths = lockfile.workspace_paths.values();
        workspaces: while (ws_i < ws_paths.len) : (ws_i += 1) {
            const workspace_path = ws_paths[ws_i];
            for (prop of importers_obj.data.e_object.properties.slice()) {
                const key = prop.key.?;
                const path_opt = key.asString(allocator);
                if (path_opt == null) continue;
                const path = path_opt.?;
                if (!strings.eqlLong(path, workspace_path.slice(string_buf.bytes.items), true)) {
                    continue;
                }

                let path_buf: bun.AutoAbsPath = .initTopLevelDir();
                const _guard_path_buf = DeinitGuard<bun.AutoAbsPath>{ ptr: &path_buf };

                path_buf.append(path);
                path_buf.append("package.json");

                const workspace_pkg_json = manager.workspace_package_json_cache.getWithPath(allocator, log, path_buf.slice(), .{}).unwrap() catch {
                    return invalidPnpmLockfile();
                };

                const name_tuple_opt = workspace_pkg_json.root.getString(allocator, "name")?;
                if (name_tuple_opt == null) {
                    return invalidPnpmLockfile();
                }
                const name = name_tuple_opt.?[0];

                const name_hash = String.Builder.stringHash(name);
                const dep = Dependency{
                    name: string_buf.appendWithHash(name, name_hash)?,
                    name_hash: name_hash,
                    behavior: .{ workspace: true },
                    version: .{
                        tag: .workspace,
                        value: .{ workspace: string_buf.append(path)? },
                    },
                };

                lockfile.buffers.dependencies.append(allocator, dep)?;
                continue :workspaces;
            }
        }
    }

    const end: u32 = @truncate(lockfile.buffers.dependencies.items.len);

    std.sort.pdq(
        Dependency,
        lockfile.buffers.dependencies.items[off..],
        string_buf.bytes.items,
        Dependency.isLessThan,
    );

    return SliceRange{ off: off, len: end - off };
}

/// Updates package.json with workspace and catalog information after migration
fn updatePackageJsonAfterMigration(allocator: Allocator, manager: *PackageManager, log: *logger.Log, dir: bun.FD, patches: StringMap)! {
    let pkg_json_path: bun.AbsPath(.{}) = .initTopLevelDir();
    const _guard_pkg_json_path = DeinitGuard<@TypeOf(pkg_json_path)>{ ptr: &pkg_json_path };

    pkg_json_path.append("package.json");

    const root_pkg_json = manager.workspace_package_json_cache.getWithPath(
        manager.allocator,
        log,
        pkg_json_path.slice(),
        .{
            guess_indentation: true,
        },
    ).unwrap() catch {
        return;
    };

    let json = root_pkg_json.root;
    if (json.data != .e_object) return;

    let needs_update = false;
    let moved_overrides = false;
    let moved_patched_deps = false;

    if (pnpm_prop := json.asProperty("pnpm")) {
        if (pnpm_prop.expr.data == .e_object) {
            const pnpm_obj = pnpm_prop.expr.data.e_object;

            if (overrides_field := pnpm_obj.get("overrides")) {
                if (overrides_field.data == .e_object) {
                    if (existing_prop := json.asProperty("overrides")) {
                        if (existing_prop.expr.data == .e_object) {
                            const existing_overrides = existing_prop.expr.data.e_object;
                            for (prop of overrides_field.data.e_object.properties.slice()) {
                                const key_opt = prop.key.?.asString(allocator);
                                if (key_opt == null) continue;
                                existing_overrides.put(allocator, key_opt.?, prop.value.?)?;
                            }
                        }
                    } else {
                        json.data.e_object.put(allocator, "overrides", overrides_field)?;
                    }
                    moved_overrides = true;
                    needs_update = true;
                }
            }

            if (patched_field := pnpm_obj.get("patchedDependencies")) {
                if (patched_field.data == .e_object) {
                    if (existing_prop := json.asProperty("patchedDependencies")) {
                        if (existing_prop.expr.data == .e_object) {
                            const existing_patches = existing_prop.expr.data.e_object;
                            for (prop of patched_field.data.e_object.properties.slice()) {
                                const key_opt = prop.key.?.asString(allocator);
                                if (key_opt == null) continue;
                                existing_patches.put(allocator, key_opt.?, prop.value.?)?;
                            }
                        }
                    } else {
                        json.data.e_object.put(allocator, "patchedDependencies", patched_field)?;
                    }
                    moved_patched_deps = true;
                    needs_update = true;
                }
            }

            if (moved_overrides || moved_patched_deps) {
                let remaining_count: usize = 0;
                for (prop of pnpm_obj.properties.slice()) {
                    const key_opt = prop.key.?.asString(allocator);
                    if (key_opt == null) {
                        remaining_count += 1;
                        continue;
                    }
                    const key = key_opt.?;
                    if (moved_overrides && strings.eqlComptime(key, "overrides")) continue;
                    if (moved_patched_deps && strings.eqlComptime(key, "patchedDependencies")) continue;
                    remaining_count += 1;
                }

                if (remaining_count == 0) {
                    let new_root_count: usize = 0;
                    for (prop of json.data.e_object.properties.slice()) {
                        const key_opt = prop.key.?.asString(allocator);
                        if (key_opt == null) {
                            new_root_count += 1;
                            continue;
                        }
                        const key = key_opt.?;
                        if (!strings.eqlComptime(key, "pnpm")) {
                            new_root_count += 1;
                        }
                    }

                    let new_root_props: JSAst.G.Property.List = JSAst.G.Property.List.initCapacity(allocator, new_root_count)?;
                    for (prop of json.data.e_object.properties.slice()) {
                        const key_opt = prop.key.?.asString(allocator);
                        if (key_opt == null) {
                            new_root_props.appendAssumeCapacity(prop);
                            continue;
                        }
                        const key = key_opt.?;
                        if (!strings.eqlComptime(key, "pnpm")) {
                            new_root_props.appendAssumeCapacity(prop);
                        }
                    }

                    json.data.e_object.properties = new_root_props;
                } else {
                    let new_pnpm_props: JSAst.G.Property.List = JSAst.G.Property.List.initCapacity(allocator, remaining_count)?;
                    for (prop of pnpm_obj.properties.slice()) {
                        const key_opt = prop.key.?.asString(allocator);
                        if (key_opt == null) {
                            new_pnpm_props.appendAssumeCapacity(prop);
                            continue;
                        }
                        const key = key_opt.?;
                        if (moved_overrides && strings.eqlComptime(key, "overrides")) continue;
                        if (moved_patched_deps && strings.eqlComptime(key, "patchedDependencies")) continue;
                        new_pnpm_props.appendAssumeCapacity(prop);
                    }

                    pnpm_obj.properties = new_pnpm_props;
                }
                needs_update = true;
            }
        }
    }

    let workspace_paths: StringList? = null;
    let catalog_obj: Expr? = null;
    let catalogs_obj: Expr? = null;
    let workspace_overrides_obj: Expr? = null;
    let workspace_patched_deps_obj: Expr? = null;

    switch (bun.sys.File.readFrom(bun.FD.cwd(), "pnpm-workspace.yaml", allocator)) {
        .result contents => read_pnpm_workspace_yaml: {
            const yaml_source = logger.Source.initPathString("pnpm-workspace.yaml", contents);
            let root_expr: Expr = undefined;
            let root_ok = true;
            root_expr = YAML.parse(&yaml_source, log, allocator) catch e => {
                root_ok = false;
                undefined
            };
            if (!root_ok) {
                break :read_pnpm_workspace_yaml;
            }
            const root = root_expr;

            if (packages_expr := root.get("packages")) {
                if (packages_arr := packages_expr.asArray()) {
                    let packages = packages_arr;
                    let paths: StringList = .init(allocator);
                    while (package_path := packages.next()) {
                        if (package_path_str := package_path.asString(allocator)) {
                            paths.append(package_path_str)?;
                        }
                    }

                    workspace_paths = paths;
                }
            }

            if (catalog_expr := root.getObject("catalog")) {
                catalog_obj = catalog_expr;
            }

            if (catalogs_expr := root.getObject("catalogs")) {
                catalogs_obj = catalogs_expr;
            }

            if (overrides_expr := root.getObject("overrides")) {
                workspace_overrides_obj = overrides_expr;
            }

            if (patched_deps_expr := root.getObject("patchedDependencies")) {
                workspace_patched_deps_obj = patched_deps_expr;
            }
        },
        .err => {},
    }

    const has_workspace_data = workspace_paths != null || catalog_obj != null || catalogs_obj != null;

    if (has_workspace_data) {
        const use_array_format = workspace_paths != null && catalog_obj == null && catalogs_obj == null;

        const existing_workspaces = json.data.e_object.get("workspaces");
        const is_object_workspaces = existing_workspaces != null && existing_workspaces.?.data == .e_object;

        if (use_array_format) {
            const paths = workspace_paths.?;
            let items: JSAst.ExprNodeList = JSAst.ExprNodeList.initCapacity(allocator, paths.items.len)?;
            for (path of paths.items) {
                items.appendAssumeCapacity(Expr.init(E.String, .{ data: path }, .Empty));
            }
            const array = Expr.init(E.Array, .{ items: items }, .Empty);
            json.data.e_object.put(allocator, "workspaces", array)?;
            needs_update = true;
        } else if (is_object_workspaces) {
            const ws_obj = existing_workspaces.?.data.e_object;

            if (paths := workspace_paths) {
                if (paths.items.len > 0) {
                    let items: JSAst.ExprNodeList = JSAst.ExprNodeList.initCapacity(allocator, paths.items.len)?;
                    for (path of paths.items) {
                        items.appendAssumeCapacity(Expr.init(E.String, .{ data: path }, .Empty));
                    }
                    const array = Expr.init(E.Array, .{ items: items }, .Empty);
                    ws_obj.put(allocator, "packages", array)?;

                    needs_update = true;
                }
            }

            if (catalog := catalog_obj) {
                ws_obj.put(allocator, "catalog", catalog)?;
                needs_update = true;
            }

            if (catalogs := catalogs_obj) {
                ws_obj.put(allocator, "catalogs", catalogs)?;
                needs_update = true;
            }
        } else if (!use_array_format) {
            let ws_props: JSAst.G.Property.List = .empty;

            if (paths := workspace_paths) {
                if (paths.items.len > 0) {
                    let items: JSAst.ExprNodeList = JSAst.ExprNodeList.initCapacity(allocator, paths.items.len)?;
                    for (path of paths.items) {
                        items.appendAssumeCapacity(Expr.init(E.String, .{ data: path }, .Empty));
                    }
                    const val = Expr.init(E.Array, .{ items: items }, .Empty);
                    const key = Expr.init(E.String, .{ data: "packages" }, .Empty);

                    ws_props.append(allocator, .{ key: key, value: val })?;
                }
            }

            if (catalog := catalog_obj) {
                const key = Expr.init(E.String, .{ data: "catalog" }, .Empty);
                ws_props.append(allocator, .{ key: key, value: catalog })?;
            }

            if (catalogs := catalogs_obj) {
                const key = Expr.init(E.String, .{ data: "catalogs" }, .Empty);
                ws_props.append(allocator, .{ key: key, value: catalogs })?;
            }

            if (ws_props.len > 0) {
                const workspace_obj = Expr.init(E.Object, .{ properties: ws_props }, .Empty);
                json.data.e_object.put(allocator, "workspaces", workspace_obj)?;
                needs_update = true;
            }
        }
    }

    // Handle overrides from pnpm-workspace.yaml
    if (ws_overrides := workspace_overrides_obj) {
        if (ws_overrides.data == .e_object) {
            if (existing_prop := json.asProperty("overrides")) {
                if (existing_prop.expr.data == .e_object) {
                    const existing_overrides = existing_prop.expr.data.e_object;
                    for (prop of ws_overrides.data.e_object.properties.slice()) {
                        const key_opt = prop.key.?.asString(allocator);
                        if (key_opt == null) continue;
                        existing_overrides.put(allocator, key_opt.?, prop.value.?)?;
                    }
                }
            } else {
                json.data.e_object.put(allocator, "overrides", ws_overrides)?;
            }
            needs_update = true;
        }
    }

    // Handle patchedDependencies from pnpm-workspace.yaml
    if (ws_patched := workspace_patched_deps_obj) {
        let join_buf: std.array_list.Managed(u8) = .init(allocator);
        const _guard_join_buf = DeinitGuard<std.array_list.Managed(u8)>{ ptr: &join_buf };

        if (ws_patched.data == .e_object) {
            for (prop_i of 0..ws_patched.data.e_object.properties.len) {
                // convert keys to expected "name@version" instead of only "name"
                let prop = &ws_patched.data.e_object.properties.ptr[prop_i];
                const key_str_opt = prop.key.?.asString(allocator);
                if (key_str_opt == null) {
                    continue;
                }
                const key_str = key_str_opt.?;
                const res_str_opt = patches.get(key_str);
                if (res_str_opt == null) {
                    continue;
                }
                const res_str = res_str_opt.?;
                join_buf.clearRetainingCapacity();
                join_buf.writer().print("{s}@{s}", .{
                    key_str,
                    res_str,
                })?;
                prop.key = Expr.init(E.String, .{ data: allocator.dupe(u8, join_buf.items)? }, .Empty);
            }
            if (existing_prop := json.asProperty("patchedDependencies")) {
                if (existing_prop.expr.data == .e_object) {
                    const existing_patches = existing_prop.expr.data.e_object;
                    for (prop of ws_patched.data.e_object.properties.slice()) {
                        const key_opt = prop.key.?.asString(allocator);
                        if (key_opt == null) continue;
                        existing_patches.put(allocator, key_opt.?, prop.value.?)?;
                    }
                }
            } else {
                json.data.e_object.put(allocator, "patchedDependencies", ws_patched)?;
            }
            needs_update = true;
        }
    }

    if (needs_update) {
        let buffer_writer = JSPrinter.BufferWriter.init(allocator);
        const _guard_buffer_writer = DeinitGuard<@TypeOf(buffer_writer.buffer)>{ ptr: &buffer_writer.buffer };
        buffer_writer.append_newline = root_pkg_json.source.contents.len > 0 && root_pkg_json.source.contents[root_pkg_json.source.contents.len - 1] == '\n';
        let package_json_writer = JSPrinter.BufferPrinter.init(buffer_writer);

        * = JSPrinter.printJSON(
            @TypeOf(&package_json_writer),
            &package_json_writer,
            json,
            &root_pkg_json.source,
            .{
                indent: root_pkg_json.indentation,
                mangled_props: null,
            },
        ) catch return;

        package_json_writer.flush() catch {
            return error.OutOfMemory;
        };

        root_pkg_json.source.contents = allocator.dupe(u8, package_json_writer.ctx.writtenWithoutTrailingZero())?;

        // Write the updated package.json
        let write_file_res = bun.sys.File.openat(dir, "package.json", bun.O.WRONLY | bun.O.TRUNC, 0).unwrap() catch return;
        const _guard_write_file_res = CloseGuard<bun.sys.File>{ ptr: &write_file_res };
        * = write_file_res.write(root_pkg_json.source.contents).unwrap() catch return;
    }
}
