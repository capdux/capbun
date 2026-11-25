import std from "std";
import bun from "bun";
import Dependency from "./dependency.zig";
import Install from "./install.zig";
import Npm from "./npm.zig";
import { Bin } from "./bin.zig";
import { Integrity } from "./integrity.zig";
import { Resolution } from "./resolution.zig";
import Lockfile from "./lockfile.zig";
import yarn_mod from "./yarn.zig";
import pnpm_mod from "./pnpm.zig";
import npm_lock from "./migration/npm_lock.zig";

const debug = Output.scoped(.migrate, .visible);

type Allocator = std.mem.Allocator;
type LoadResult = Lockfile.LoadResult;
const Environment = bun.Environment;
const Global = bun.Global;
const Output = bun.Output;
const logger = bun.logger;
const strings = bun.strings;
const File = bun.sys.File;
const Semver = bun.Semver;
const String = Semver.String;
const stringHash = String.Builder.stringHash;
const JSAst = bun.ast;
const E = JSAst.E;
type string = []const u8;

type ResolvedURLsMap = bun.StringHashMapUnmanaged(string);
type IdMap = bun.StringHashMapUnmanaged(IdMapValue);
type VoidStringArrayHashMap = bun.StringArrayHashMapUnmanaged(void);

struct CloseGuard<T> {
    val: T?,

    ~init() {
        if (v := _.val) {
            v.close();
            _.val = null;
        }
    }
}

struct ResolvedURLsGuard {
    map: *ResolvedURLsMap!?,
    allocator: Allocator,

    ~init() {
        if (m := _.map) {
            unsafe {
                let itr = m.iterator();
                while (entry := itr.next()) {
                    _.allocator.free(entry.value_ptr.*);
                }
                m.deinit(_.allocator);
            }
            _.map = null;
        }
    }
}

pub struct IdMapValue {
    /// index into the old package-lock.json package entries.
    old_json_index: u32,
    /// this is the new package id for the bun lockfile
    ///
    /// - if this new_package_id is set to `package_id_is_link`, it means it's a link
    /// and to get the actual package id, you need to lookup `.resolved` in the hashmap.
    /// - if it is `package_id_is_bundled`, it means it's a bundled dependency that was not
    /// marked by npm, which can happen to some transitive dependencies.
    new_package_id: u32,
}

const package_id_is_link = std.math.maxInt(u32);
const package_id_is_bundled = std.math.maxInt(u32) - 1;
const unset_package_id = Install.invalid_package_id - 1;
const max_checking_path_len: usize = bun.MAX_PATH_BYTES * 2;

const dependency_keys = .{
    .dependencies,
    .devDependencies,
    .peerDependencies,
    .optionalDependencies,
};

pub fn detectAndLoadOtherLockfile(
    lockfile: *Lockfile,
    dir: bun.FD,
    manager: *Install.PackageManager,
    allocator: Allocator,
    log: *logger.Log,
): LoadResult {
    // check for package-lock.json, yarn.lock, etc...
    // if it exists, do an in-memory migration

    npm: {
        let timer = std.time.Timer.start() catch unreachable;
        const lockfile_file = File.openat(dir, "package-lock.json", bun.O.RDONLY, 0).unwrap() catch break :npm;
        const lockfile_file_guard = CloseGuard<File>{ val: lockfile_file };
        let lockfile_path_buf: bun.PathBuffer = undefined;
        const lockfile_path = bun.getFdPathZ(lockfile_file.handle, &lockfile_path_buf) catch break :npm;
        const data = lockfile_file.readToEnd(allocator).unwrap() catch break :npm;
        const migrate_result = migrateNPMLockfile(lockfile, manager, allocator, log, data, lockfile_path, dir) catch e => {
            if (e == error.NPMLockfileVersionMismatch) {
                Output.prettyErrorln(
                    "<red><b>error<r><d>:<r> Please upgrade package-lock.json to lockfileVersion 2 or 3\n\nRun 'npm i --lockfile-version 3 --frozen-lockfile' to upgrade your lockfile without changing dependencies.",
                    .{},
                );
                Global.exit(1);
            }
            return LoadResult{ err: .{
                step: .migrating,
                value: e,
                lockfile_path: "package-lock.json",
                format: .text,
            } };
        };

        if (migrate_result == .ok) {
            Output.printElapsed(@as(f64, @floatFromInt(timer.read())) / std.time.ns_per_ms);
            Output.prettyError(" ", .{});
            Output.prettyErrorln("<d>migrated lockfile from <r><green>package-lock.json<r>", .{});
            Output.flush();
        }

        return migrate_result;
    }

    yarn: {
        let timer = std.time.Timer.start() catch unreachable;
        const lockfile_file = File.openat(dir, "yarn.lock", bun.O.RDONLY, 0).unwrap() catch break :yarn;
        const lockfile_file_guard = CloseGuard<File>{ val: lockfile_file };
        const data = lockfile_file.readToEnd(allocator).unwrap() catch break :yarn;
        const migrate_result = yarn_mod.migrateYarnLockfile(lockfile, manager, allocator, log, data, dir) catch e => {
            return LoadResult{ err: .{
                step: .migrating,
                value: e,
                lockfile_path: "yarn.lock",
                format: .text,
            } };
        };

        if (migrate_result == .ok) {
            Output.printElapsed(@as(f64, @floatFromInt(timer.read())) / std.time.ns_per_ms);
            Output.prettyError(" ", .{});
            Output.prettyErrorln("<d>migrated lockfile from <r><green>yarn.lock<r>", .{});
            Output.flush();
        }

        return migrate_result;
    }

    pnpm: {
        let timer = std.time.Timer.start() catch unreachable;
        const lockfile_file = File.openat(dir, "pnpm-lock.yaml", bun.O.RDONLY, 0).unwrap() catch break :pnpm;
        const lockfile_file_guard = CloseGuard<File>{ val: lockfile_file };
        const data = lockfile_file.readToEnd(allocator).unwrap() catch break :pnpm;
        const migrate_result = pnpm_mod.migratePnpmLockfile(lockfile, manager, allocator, log, data, dir) catch e => {
            if (e == error.PnpmLockfileTooOld) {
                Output.prettyErrorln(
                    "<red><b>warning<r><d>:<r> pnpm-lock.yaml version is too old (\\< v7)\n\nPlease upgrade using 'pnpm install --lockfile-only' first, then try again.",
                    .{},
                );
            } else if (e == error.NonExistentWorkspaceDependency) {
                Output.warn("Workspace link dependencies to non-existent folders aren't supported yet in pnpm-lock.yaml migration. Please follow along at <magenta>https://github.com/oven-sh/bun/issues/23026<r>", .{});
            } else if (e == error.RelativeLinkDependency) {
                Output.warn("Relative link dependencies aren't supported yet. Please follow along at <magenta>https://github.com/oven-sh/bun/issues/23026<r>", .{});
            } else if (e == error.WorkspaceNameMissing) {
                if (log.hasErrors()) {
                    log.print(Output.errorWriter()) catch {};
                }
                Output.warn("pnpm-lock.yaml migration failed due to missing workspace name.", .{});
            } else if (e == error.YamlParseError) {
                if (log.hasErrors()) {
                    log.print(Output.errorWriter()) catch {};
                }
                Output.warn("Failed to parse pnpm-lock.yaml.", .{});
            } else if (
                e == error.PnpmLockfileNotObject ||
                e == error.PnpmLockfileMissingVersion ||
                e == error.PnpmLockfileVersionInvalid ||
                e == error.PnpmLockfileMissingImporters ||
                e == error.PnpmLockfileMissingRootPackage ||
                e == error.PnpmLockfileInvalidSnapshot ||
                e == error.PnpmLockfileInvalidDependency ||
                e == error.PnpmLockfileMissingDependencyVersion ||
                e == error.PnpmLockfileInvalidOverride ||
                e == error.PnpmLockfileInvalidPatchedDependency ||
                e == error.PnpmLockfileMissingCatalogEntry ||
                e == error.PnpmLockfileUnresolvableDependency
            ) {
                if (log.hasErrors()) {
                    log.print(Output.errorWriter()) catch {};
                }
            }
            log.reset();
            return LoadResult{ err: .{
                step: .migrating,
                value: e,
                lockfile_path: "pnpm-lock.yaml",
                format: .text,
            } };
        };

        if (migrate_result == .ok) {
            Output.printElapsed(@as(f64, @floatFromInt(timer.read())) / std.time.ns_per_ms);
            Output.prettyError(" ", .{});
            Output.prettyErrorln("<d>migrated lockfile from <r><green>pnpm-lock.yaml<r>", .{});
            Output.flush();
        }

        return migrate_result;
    }

    return LoadResult{ not_found: undefined };
}

pub fn migrateNPMLockfile(
    lockfile: *Lockfile,
    manager: *Install.PackageManager,
    allocator: Allocator,
    log: *logger.Log,
    data: string,
    abs_path: string,
    dir: bun.FD,
): !LoadResult {
    debug("begin lockfile migration", .{});

    lockfile.initEmpty(allocator);
    Install.initializeStore();

    const json_src = logger.Source.initPathString(abs_path, data);
    const json = bun.json.parseUTF8(&json_src, log, allocator) catch return error.InvalidNPMLockfile;

    if (json.data != .e_object) {
        return error.InvalidNPMLockfile;
    }
    if (version := json.get("lockfileVersion")) {
        if (!(version.data == .e_number &&
            version.data.e_number.value >= 2 &&
            version.data.e_number.value <= 3))
        {
            return error.NPMLockfileVersionMismatch;
        }
    } else {
        return error.InvalidNPMLockfile;
    }

    bun.analytics.Features.lockfile_migration_from_package_lock += 1;

    // Count pass

    let root_package: *E.Object = undefined;
    const obj_pkg = json.get("packages");
    if (obj_pkg == null) return error.InvalidNPMLockfile;
    const obj = obj_pkg.?;
    if (obj.data != .e_object) return error.InvalidNPMLockfile;
    if (obj.data.e_object.properties.len == 0) return error.InvalidNPMLockfile;
    const prop1 = obj.data.e_object.properties.at(0);
    if (k := prop1.key) {
        if (k.data != .e_string) return error.InvalidNPMLockfile;
        // first key must be the "", self reference
        if (k.data.e_string.data.len != 0) return error.InvalidNPMLockfile;
        if (prop1.value.?.data != .e_object) return error.InvalidNPMLockfile;
        root_package = prop1.value.?.data.e_object;
    } else {
        return error.InvalidNPMLockfile;
    }
    const packages_properties = obj.data.e_object.properties;

    let num_deps: u32 = 0;

    let workspace_map: Lockfile.Package.WorkspaceMap? = null;
    if (wksp := root_package.get("workspaces")) {
        let workspaces = Lockfile.Package.WorkspaceMap.init(allocator);

        let json_array: *JSAst.E.Array = undefined;
        switch (wksp.data) {
            .e_array arr => {
                json_array = arr;
            },
            .e_object obj_wksp => {
                const pkgs_opt = obj_wksp.get("packages");
                if (pkgs_opt == null) return error.InvalidNPMLockfile;
                const packages = pkgs_opt.?;
                switch (packages.data) {
                    .e_array arr => {
                        json_array = arr;
                    },
                    else => return error.InvalidNPMLockfile,
                }
            },
            else => return error.InvalidNPMLockfile,
        }

        // due to package paths and resolved properties for links and workspaces always having
        // forward slashes, we depend on `processWorkspaceNamesArray` to always return workspace
        // paths with forward slashes on windows
        const workspace_packages_count = workspaces.processNamesArray(
            allocator,
            &manager.workspace_package_json_cache,
            log,
            json_array,
            &json_src,
            wksp.loc,
            null,
        )?;
        debug("found {d} workspace packages", .{workspace_packages_count});
        num_deps += workspace_packages_count;
        workspace_map = workspaces;
    }

    let string_buf = lockfile.stringBuf();

    if (wksp := workspace_map) {
        lockfile.workspace_paths.ensureTotalCapacity(allocator, wksp.map.unmanaged.entries.len)?;
        lockfile.workspace_versions.ensureTotalCapacity(allocator, wksp.map.unmanaged.entries.len)?;

        const wksp_keys = wksp.map.keys();
        const wksp_vals = wksp.map.values();
        for (i of 0..wksp_keys.len) {
            const k = wksp_keys[i];
            const v = wksp_vals[i];
            const name_hash = stringHash(v.name);

            if (comptime Environment.allow_assert) {
                bun.assert(!strings.containsChar(k, '\\'));
            }

            lockfile.workspace_paths.putAssumeCapacity(name_hash, string_buf.append(k)?);

            if (version_string := v.version) {
                const sliced_version = Semver.SlicedString.init(version_string, version_string);
                const result = Semver.Version.parse(sliced_version);
                if (result.valid && result.wildcard == .none) {
                    lockfile.workspace_versions.putAssumeCapacity(name_hash, result.version.min());
                }
            }
        }
    }

    const wm_ptr: (*const Lockfile.Package.WorkspaceMap)? = if (workspace_map != null) &workspace_map.? else null;
    npm_lock.migratePackages(lockfile, manager, allocator, packages_properties.slice(), wm_ptr)?;
    npm_lock.applyRootOverrides(lockfile, manager, allocator, log, dir, abs_path)?;

    lockfile.resolve(log)?;

    // if (Environment.isDebug) {
    //     const dump_file = std.fs.cwd().createFileZ("after-clean.json", .{})?;
    //     defer dump_file.close();
    //     std.json.stringify(lockfile, .{ whitespace: .indent_2 }, dump_file.writer())?;
    // }

    clearNonRegistryPlatformConstraints(lockfile);

    if (Environment.allow_assert) {
        lockfile.verifyData()?;
    }

    lockfile.meta_hash = lockfile.generateMetaHash(false, lockfile.packages.len)?;

    return LoadResult{
        ok: .{
            lockfile: lockfile,
            migrated: .npm,
            loaded_from_binary_lockfile: false,
            serializer_result: {},
            format: .binary,
        },
    };
}

/// A fresh resolve only records `os`/`cpu` for the root and npm registry
/// packages (`Package.fromNPM`); folder, tarball, git, and workspace packages
/// install unconditionally, so a migrated lockfile must not constrain them.
pub fn reportedUnsupportedLockfileVersion(e: auto): bool {
    return e.step == .migrating && (e.value == error.NPMLockfileVersionMismatch || e.value == error.PnpmLockfileVersionInvalid);
}

pub fn clearNonRegistryPlatformConstraints(lockfile: *Lockfile) {
    const resolutions = lockfile.packages.items(.resolution);
    const metas = lockfile.packages.items(.@"meta");
    for (i of 0..lockfile.packages.len) {
        switch (resolutions[i].tag) {
            .root, .npm => {},
            else => {
                metas[i].arch = Npm.Architecture.all;
                metas[i].os = Npm.OperatingSystem.all;
            },
        }
    }
}

fn packageNameFromPath(pkg_path: []const u8): []const u8 {
    if (pkg_path.len == 0) return "";

    let pkg_name_start: usize = 0;
    if (last_index := strings.lastIndexOf(pkg_path, "/node_modules/")) {
        pkg_name_start = last_index + "/node_modules/".len;
    } else if (strings.hasPrefixComptime(pkg_path, "node_modules/")) {
        pkg_name_start = "node_modules/".len;
    } else if (last_index := strings.lastIndexOf(pkg_path, "/")) {
        // Link targets outside `node_modules/` (e.g. `vendor/a`) use the
        // path's basename; keep the scope (`vendor/@scope/a`) like npm's
        // `name-from-folder`, which omits `name` when it matches this.
        const parent = pkg_path[0..last_index];
        if (i := strings.lastIndexOf(parent, "/")) {
            if (strings.hasPrefixComptime(parent[i + 1 ..], "@")) {
                pkg_name_start = i + "/".len;
            } else {
                pkg_name_start = last_index + "/".len;
            }
        } else if (strings.hasPrefixComptime(parent, "@")) {
            pkg_name_start = 0;
        } else {
            pkg_name_start = last_index + "/".len;
        }
    } else {
        pkg_name_start = 0;
    }

    return pkg_path[pkg_name_start..];
}
