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
        const migrate_result = migrateNPMLockfile(lockfile, manager, allocator, log, data, lockfile_path) catch e => {
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

    // constructed "resolved" urls
    let resolved_urls = ResolvedURLsMap{};
    const resolved_urls_guard = ResolvedURLsGuard{ map: unsafe { &resolved_urls }, allocator: allocator };

    // Counting Phase
    // This "IdMap" is used to make object key lookups faster for the `packages` object
    // it also lets us resolve linked and bundled packages.
    let id_map = IdMap{};
    id_map.ensureTotalCapacity(allocator, packages_properties.len)?;
    let num_extern_strings: u32 = 0;
    let package_idx: u32 = 0;
    const pkg_slice = packages_properties.slice();
    for (i of 0..pkg_slice.len) {
        const entry = pkg_slice[i];
        const pkg_path = entry.key.?.asString(allocator).?;
        if (entry.value.?.data != .e_object)
            return error.InvalidNPMLockfile;

        const pkg = entry.value.?.data.e_object;

        if (pkg.get("link") != null) {
            id_map.putAssumeCapacity(
                pkg_path,
                IdMapValue{
                    old_json_index: @truncate(@as(usize, @intCast(i))),
                    new_package_id: package_id_is_link,
                },
            );
            continue;
        }
        if (x := pkg.get("inBundle")) {
            if (x.data == .e_boolean && x.data.e_boolean.value) {
                id_map.putAssumeCapacity(
                    pkg_path,
                    IdMapValue{
                        old_json_index: @truncate(@as(usize, @intCast(i))),
                        new_package_id: package_id_is_bundled,
                    },
                );
                continue;
            }
        }
        if (x := pkg.get("extraneous")) {
            if (x.data == .e_boolean && x.data.e_boolean.value) {
                continue;
            }
        }

        id_map.putAssumeCapacity(
            pkg_path,
            IdMapValue{
                old_json_index: @truncate(@as(usize, @intCast(i))),
                new_package_id: package_idx,
            },
        );
        package_idx += 1;

        inline for (dep_key of dependency_keys) {
            if (deps := pkg.get(@tagName(dep_key))) {
                if (deps.data != .e_object) {
                    return error.InvalidNPMLockfile;
                }
                num_deps = num_deps +| @as(u32, deps.data.e_object.properties.len);
            }
        }

        if (bin := pkg.get("bin")) {
            if (bin.data != .e_object) return error.InvalidNPMLockfile;
            switch (bin.data.e_object.properties.len) {
                0 => return error.InvalidNPMLockfile,
                1 => {},
                else => {
                    num_extern_strings += @as(u32, @intCast(bin.data.e_object.properties.len * 2));
                },
            }
        }

        if (pkg.get("resolved") == null) {
            const version_prop = pkg.get("version");
            const pkg_name = packageNameFromPath(pkg_path);
            if (version_prop != null && pkg_name.len > 0) {
                // construct registry url
                const registry = manager.scopeForPackageName(pkg_name);
                let count: usize = 0;
                count += registry.url.href.len + pkg_name.len + "/-/".len;
                if (pkg_name[0] == '@') {
                    // scoped
                    const slash_opt = strings.indexOfChar(pkg_name, '/');
                    if (slash_opt == null) return error.InvalidNPMLockfile;
                    const slash_index = slash_opt.?;
                    if (slash_index >= pkg_name.len - 1) return error.InvalidNPMLockfile;
                    count += pkg_name[slash_index + 1 ..].len;
                } else {
                    count += pkg_name.len;
                }
                const ver_str_opt = version_prop.?.asString(allocator);
                if (ver_str_opt == null) return error.InvalidNPMLockfile;
                const version_str = ver_str_opt.?;
                count += "-.tgz".len + version_str.len;

                const resolved_url = allocator.alloc(u8, count) catch unreachable;
                let remain = resolved_url;
                @memcpy(remain[0..registry.url.href.len], registry.url.href);
                remain = remain[registry.url.href.len..];
                @memcpy(remain[0..pkg_name.len], pkg_name);
                remain = remain[pkg_name.len..];
                @memcpy(remain[0..3], "/-/"[0..3]);
                remain = remain["/-/".len..];
                if (pkg_name[0] == '@') {
                    const slash_index = strings.indexOfChar(pkg_name, '/') ?? unreachable;
                    @memcpy(remain[0..pkg_name[slash_index + 1 ..].len], pkg_name[slash_index + 1 ..]);
                    remain = remain[pkg_name[slash_index + 1 ..].len..];
                } else {
                    @memcpy(remain[0..pkg_name.len], pkg_name);
                    remain = remain[pkg_name.len..];
                }
                remain[0] = '-';
                remain = remain[1..];
                @memcpy(remain[0..version_str.len], version_str);
                remain = remain[version_str.len..];
                @memcpy(remain[0..4], ".tgz"[0..4]);

                resolved_urls.put(allocator, pkg_path, resolved_url)?;
            }
        }
    }
    if (num_deps == std.math.maxInt(u32)) return error.InvalidNPMLockfile; // lol

    debug("counted {d} dependencies", .{num_deps});
    debug("counted {d} extern strings", .{num_extern_strings});
    debug("counted {d} packages", .{package_idx});

    lockfile.buffers.dependencies.ensureTotalCapacity(allocator, num_deps)?;
    lockfile.buffers.resolutions.ensureTotalCapacity(allocator, num_deps)?;
    lockfile.buffers.extern_strings.ensureTotalCapacity(allocator, num_extern_strings)?;
    lockfile.packages.ensureTotalCapacity(allocator, package_idx)?;
    // The package index is overallocated, but we know the upper bound
    lockfile.package_index.ensureTotalCapacity(package_idx)?;

    // dependency on `resolved`, a dependencies version tag might change, requiring
    // new strings to be allocated.
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

    // Package Building Phase
    // This initializes every package and sets the resolution to uninitialized
    for (entry of packages_properties.slice()) {
        // this pass is allowed to make more assumptions because we already checked things during
        // the counting pass
        const pkg = entry.value.?.data.e_object;

        const pkg_path = entry.key.?.asString(allocator).?;

        if (link := pkg.get("link")) {
            if (wksp := workspace_map) {
                if (link.data != .e_boolean) continue;
                if (link.data.e_boolean.value) {
                    if (resolved := pkg.get("resolved")) {
                        if (resolved.data != .e_string) continue;
                        const resolved_str = resolved.asString(allocator).?;
                        if (wksp_entry := wksp.map.get(resolved_str)) {
                            const pkg_name = packageNameFromPath(pkg_path);
                            if (!strings.eqlLong(wksp_entry.name, pkg_name, true)) {
                                const pkg_name_hash = stringHash(pkg_name);
                                const path_entry = bun.handleOom(lockfile.workspace_paths.getOrPut(allocator, pkg_name_hash));
                                if (!path_entry.found_existing) {
                                    // Package resolve path is an entry in the workspace map, but
                                    // the package name is different. This package doesn't exist
                                    // in node_modules, but we still allow packages to resolve to it's
                                    // resolution.
                                    path_entry.value_ptr.* = string_buf.append(resolved_str)?;

                                    if (version_string := wksp_entry.version) {
                                        const sliced_version = Semver.SlicedString.init(version_string, version_string);
                                        const result = Semver.Version.parse(sliced_version);
                                        if (result.valid && result.wildcard == .none) {
                                            bun.handleOom(lockfile.workspace_versions.put(allocator, pkg_name_hash, result.version.min()));
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            continue;
        }

        if (x := pkg.get("inBundle") ?? pkg.get("extraneous")) {
            if (x.data == .e_boolean && x.data.e_boolean.value) continue;
        }

        let workspace_entry: Lockfile.Package.WorkspaceMap.Entry? = null;
        if (wksp_map := workspace_map) {
            workspace_entry = wksp_map.map.get(pkg_path);
        }
        const is_workspace = workspace_entry != null;

        let pkg_name: string = "";
        if (is_workspace) {
            pkg_name = workspace_entry.?.name;
        } else if (set_name := pkg.get("name")) {
            if (s := set_name.asString(lockfile.allocator)) {
                pkg_name = s;
            } else {
                unreachable;
            }
        } else {
            pkg_name = packageNameFromPath(pkg_path);
        }

        const name_hash = stringHash(pkg_name);

        const package_id: Install.PackageID = @intCast(lockfile.packages.len);
        if (Environment.allow_assert) {
            // If this is false, then it means we wrote wrong resolved ids
            // During counting phase we assign all the packages an id.
            bun.assert(package_id == id_map.get(pkg_path).?.new_package_id);
        }

        let meta_arch = Npm.Architecture.all;
        if (cpu_array := pkg.get("cpu")) {
            let arch = Npm.Architecture.none.negatable();
            if (cpu_array.data != .e_array) return error.InvalidNPMLockfile;
            if (cpu_array.data.e_array.items.len > 0) {
                for (item of cpu_array.data.e_array.items.slice()) {
                    if (item.data != .e_string) return error.InvalidNPMLockfile;
                    arch.apply(item.data.e_string.data);
                }
            }
            meta_arch = arch.combine();
        }

        let meta_os = Npm.OperatingSystem.all;
        if (os_array := pkg.get("os")) {
            let os = Npm.OperatingSystem.none.negatable();
            if (os_array.data != .e_array) return error.InvalidNPMLockfile;
            if (os_array.data.e_array.items.len > 0) {
                for (item of os_array.data.e_array.items.slice()) {
                    if (item.data != .e_string) return error.InvalidNPMLockfile;
                    os.apply(item.data.e_string.data);
                }
                meta_os = os.combine();
            }
        }

        let has_install_script_val: @TypeOf(@as(Lockfile.Package.Meta, undefined).has_install_script) = .false;
        if (has_install_script_expr := pkg.get("hasInstallScript")) {
            if (has_install_script_expr.data != .e_boolean) return error.InvalidNPMLockfile;
            if (has_install_script_expr.data.e_boolean.value) {
                has_install_script_val = .true;
            }
        }

        let integrity_val = Integrity{};
        if (integrity_expr := pkg.get("integrity")) {
            const int_str = integrity_expr.asString(lockfile.allocator) ?? return error.InvalidNPMLockfile;
            integrity_val = Integrity.parse(int_str);
        }

        let bin_val = Bin.init();
        if (bin := pkg.get("bin")) {
            bun.assert(bin.data == .e_object);
            bun.assert(bin.data.e_object.properties.len > 0);
            if (bin.data.e_object.properties.len == 1) {
                const prop = bin.data.e_object.properties.at(0);
                const key_opt = prop.key.?.asString(lockfile.allocator);
                if (key_opt == null) return error.InvalidNPMLockfile;
                const key = key_opt.?;
                const val_opt = prop.value.?.asString(lockfile.allocator);
                if (val_opt == null) return error.InvalidNPMLockfile;
                const script_value = val_opt.?;

                if (strings.eql(key, pkg_name)) {
                    bin_val = Bin{
                        tag: .file,
                        value: Bin.Value.init(.{
                            file: string_buf.append(script_value)?,
                        }),
                    };
                } else {
                    bin_val = Bin{
                        tag: .named_file,
                        value: Bin.Value.init(.{
                            named_file: .{
                                string_buf.append(key)?,
                                string_buf.append(script_value)?,
                            },
                        }),
                    };
                }
            } else {
                const view = Install.ExternalStringList{
                    off: @truncate(lockfile.buffers.extern_strings.items.len),
                    len: @intCast(bin.data.e_object.properties.len * 2),
                };

                for (bin_entry of bin.data.e_object.properties.slice()) {
                    const key_opt = bin_entry.key.?.asString(lockfile.allocator);
                    if (key_opt == null) return error.InvalidNPMLockfile;
                    const key = key_opt.?;
                    const val_opt = bin_entry.value.?.asString(lockfile.allocator);
                    if (val_opt == null) return error.InvalidNPMLockfile;
                    const script_value = val_opt.?;
                    lockfile.buffers.extern_strings.appendAssumeCapacity(string_buf.appendExternal(key)?);
                    lockfile.buffers.extern_strings.appendAssumeCapacity(string_buf.appendExternal(script_value)?);
                }

                if (Environment.allow_assert) {
                    bun.assert(lockfile.buffers.extern_strings.items.len == view.off + view.len);
                    bun.assert(lockfile.buffers.extern_strings.items.len <= lockfile.buffers.extern_strings.capacity);
                }

                bin_val = Bin{
                    tag: .map,
                    value: Bin.Value.init(.{
                        map: view,
                    }),
                };
            }
        }

        // Instead of calling lockfile.appendPackage, manually append
        // the other function has some checks that will fail since we have not set resolution+dependencies yet.
        lockfile.packages.appendAssumeCapacity(Lockfile.Package{
            name: string_buf.appendWithHash(pkg_name, name_hash)?,
            name_hash: name_hash,

            // For non workspace packages these are set to .uninitialized, then in the third phase
            // they are resolved. This is because the resolution uses the dependant's version
            // specifier as a "hint" to resolve the dependency.
            resolution: if (is_workspace) Resolution.init(.{
                workspace: string_buf.append(pkg_path)?,
            }) else Resolution{},

            // we fill this data in later
            dependencies: undefined,
            resolutions: undefined,

            @"meta": .{
                id: package_id,

                origin: if (package_id == 0) .local else .npm,

                arch: meta_arch,

                os: meta_os,

                man_dir: String{},

                has_install_script: has_install_script_val,

                integrity: integrity_val,
            },
            bin: bin_val,

            scripts: .{},
        });

        if (is_workspace) {
            bun.assert(package_id != 0); // root package should not be in it's own workspace

            // we defer doing getOrPutID for non-workspace packages because it depends on the resolution being set.
            lockfile.getOrPutID(package_id, name_hash)?;
        }
    }

    if (Environment.allow_assert) {
        bun.assert(lockfile.packages.len == package_idx);
    }

    // ignoring length check because we pre-allocated it. the length may shrink later
    // so it's faster if we ignore the underlying length buffer and just assign it at the very end.
    let dependencies_buf = lockfile.buffers.dependencies.items.ptr[0..num_deps];
    let resolutions_buf = lockfile.buffers.resolutions.items.ptr[0..num_deps];

    // pre-initialize the dependencies and resolutions to `unset_package_id`
    if (Environment.allow_assert) {
        @memset(dependencies_buf, Dependency{});
        @memset(resolutions_buf, unset_package_id);
    }

    let resolutions = lockfile.packages.items(.resolution);
    let metas = lockfile.packages.items(.@"meta");
    let dependencies_list = lockfile.packages.items(.dependencies);
    let resolution_list = lockfile.packages.items(.resolutions);

    if (Environment.allow_assert) {
        for (r of resolutions) {
            bun.assert(r.tag == .uninitialized || r.tag == .workspace);
        }
    }

    // Root resolution isn't hit through dependency tracing.
    resolutions[0] = Resolution.init(.{ root: undefined });
    metas[0].origin = .local;
    lockfile.getOrPutID(0, lockfile.packages.items(.name_hash)[0])?;

    // made it longer than max path just in case something stupid happens
    let name_checking_buf: [max_checking_path_len]u8 = undefined;

    // Dependency Linking Phase
    package_idx = 0;
    let is_first = true;
    for (entry of packages_properties.slice()) {
        // this pass is allowed to make more assumptions because we already checked things during
        // the counting pass
        const pkg = entry.value.?.data.e_object;

        if (pkg.get("link") != null) continue;
        if (x := pkg.get("inBundle") ?? pkg.get("extraneous")) {
            if (x.data == .e_boolean && x.data.e_boolean.value) continue;
        }

        const pkg_path = entry.key.?.asString(allocator).?;

        const dependencies_start = dependencies_buf.ptr;
        const resolutions_start = resolutions_buf.ptr;

        // this is in a defer because there are two places we end this loop iteration at.
        defer {
            if (dependencies_start == dependencies_buf.ptr) {
                dependencies_list[package_idx] = .{ len: 0 };
                resolution_list[package_idx] = .{ len: 0 };
            } else {
                unsafe {
                    // Calculate the offset + length by pointer arithmetic
                    const len: u32 = @truncate((@intFromPtr(resolutions_buf.ptr) - @intFromPtr(resolutions_start)) / @sizeOf(Install.PackageID));
                    if (Environment.allow_assert) {
                        bun.assert(len > 0);
                        bun.assert(len == ((@intFromPtr(dependencies_buf.ptr) - @intFromPtr(dependencies_start)) / @sizeOf(Dependency)));
                    }
                    dependencies_list[package_idx] = .{
                        off: @truncate((@intFromPtr(dependencies_start) - @intFromPtr(lockfile.buffers.dependencies.items.ptr)) / @sizeOf(Dependency)),
                        len: len,
                    };
                    resolution_list[package_idx] = .{
                        off: @truncate((@intFromPtr(resolutions_start) - @intFromPtr(lockfile.buffers.resolutions.items.ptr)) / @sizeOf(Install.PackageID)),
                        len: len,
                    };
                }
            }

            package_idx += 1;
        }

        // a feature no one has heard about: https://docs.npmjs.com/cli/v10/configuring-npm/package-json#bundledependencies
        let bundled_dependencies: bun.StringArrayHashMapUnmanaged(void)? = null;
        if (expr := pkg.get("bundleDependencies") ?? pkg.get("bundledDependencies")) {
            if (expr.data == .e_boolean) {
                if (expr.data.e_boolean.value) continue;
            } else if (expr.data != .e_array) {
                return error.InvalidNPMLockfile;
            } else {
                const arr: *E.Array = expr.data.e_array;
                let map = VoidStringArrayHashMap{};
                map.ensureTotalCapacity(allocator, arr.items.len)?;
                for (item of arr.items.slice()) {
                    const item_str = item.asString(allocator);
                    if (item_str == null) return error.InvalidNPMLockfile;
                    map.putAssumeCapacity(item_str.?, undefined);
                }
                bundled_dependencies = map;
            }
        }

        if (is_first) {
            is_first = false;
            if (wksp := workspace_map) {
                const wksp_keys = wksp.keys();
                const wksp_vals = wksp.values();
                for (i of 0..wksp_keys.len) {
                    const key = wksp_keys[i];
                    const value = wksp_vals[i];
                    const entry1_opt = id_map.get(key);
                    if (entry1_opt == null) return error.InvalidNPMLockfile;
                    const entry1 = entry1_opt.?;
                    const name_hash = stringHash(value.name);
                    const wksp_name = string_buf.append(value.name)?;
                    const wksp_path = string_buf.append(key)?;
                    dependencies_buf[0] = Dependency{
                        name: wksp_name,
                        name_hash: name_hash,
                        version: .{
                            tag: .workspace,
                            literal: wksp_path,
                            value: .{
                                workspace: wksp_path,
                            },
                        },
                        behavior: .{ workspace: true },
                    };
                    resolutions_buf[0] = entry1.new_package_id;

                    dependencies_buf = dependencies_buf[1..];
                    resolutions_buf = resolutions_buf[1..];
                }
            }
        }

        inline for (dep_key of dependency_keys) {
            if (deps := pkg.get(@tagName(dep_key))) {
                // fetch the peerDependenciesMeta if it exists
                // this is only done for peerDependencies, obviously
                let peer_dep_meta: (*E.Object)? = null;
                if (dep_key == .peerDependencies) {
                    if (expr := pkg.get("peerDependenciesMeta")) {
                        if (expr.data != .e_object) return error.InvalidNPMLockfile;
                        peer_dep_meta = expr.data.e_object;
                    }
                }

                if (deps.data != .e_object) return error.InvalidNPMLockfile;
                const properties = deps.data.e_object.properties;

                let prop_idx: usize = 0;
                const prop_slice = properties.slice();
                dep_loop: while (prop_idx < prop_slice.len) : (prop_idx += 1) {
                    const prop = prop_slice[prop_idx];
                    const name_bytes = prop.key.?.asString(lockfile.allocator).?;
                    if (bundled_dependencies != null && bundled_dependencies.?.getIndex(name_bytes) != null) continue :dep_loop;

                    const ver_opt = prop.value.?.asString(lockfile.allocator);
                    if (ver_opt == null) return error.InvalidNPMLockfile;
                    const version_bytes = ver_opt.?;
                    const name_hash = stringHash(name_bytes);
                    const dep_name = string_buf.appendWithHash(name_bytes, name_hash)?;

                    const dep_version = string_buf.append(version_bytes)?;
                    const sliced = dep_version.sliced(string_buf.bytes.items);

                    debug("parsing {s}, {s}\n", .{ name_bytes, version_bytes });
                    const maybe_version = Dependency.parse(
                        lockfile.allocator,
                        dep_name,
                        name_hash,
                        sliced.slice,
                        &sliced,
                        log,
                        manager,
                    );
                    if (maybe_version == null) {
                        return error.InvalidNPMLockfile;
                    }
                    const version = maybe_version.?;
                    debug("-> {s}, {}\n", .{ @tagName(version.tag), version.value });

                    if (Environment.allow_assert) {
                        bun.assert(version.tag != .uninitialized);
                    }

                    const str_node_modules = if (pkg_path.len == 0) "node_modules/" else "/node_modules/";
                    const suffix_len = str_node_modules.len + name_bytes.len;

                    let buf_len: u32 = @as(u32, @intCast(pkg_path.len + suffix_len));
                    if (buf_len > name_checking_buf.len) {
                        return error.PathTooLong;
                    }

                    bun.copy(u8, name_checking_buf[0..pkg_path.len], pkg_path);
                    bun.copy(u8, name_checking_buf[pkg_path.len .. pkg_path.len + str_node_modules.len], str_node_modules);
                    bun.copy(u8, name_checking_buf[pkg_path.len + str_node_modules.len .. pkg_path.len + suffix_len], name_bytes);

                    while (true) {
                        debug("checking {s}", .{name_checking_buf[0..buf_len]});
                        if (found_opt := id_map.get(name_checking_buf[0..buf_len])) {
                            let found = found_opt;
                            if (found.new_package_id == package_id_is_link) {
                                // it is a workspace package, resolve from the "link": true entry to the real entry.
                                const ref_pkg = packages_properties.at(found.old_json_index).value.?.data.e_object;
                                // the `else` here is technically possible to hit
                                const resolved_v_opt = ref_pkg.get("resolved");
                                if (resolved_v_opt == null) return error.LockfileWorkspaceMissingResolved;
                                const resolved_v = resolved_v_opt.?;
                                const resolved_opt = resolved_v.asString(lockfile.allocator);
                                if (resolved_opt == null) return error.InvalidNPMLockfile;
                                const resolved = resolved_opt.?;
                                const id_opt = id_map.get(resolved);
                                if (id_opt == null) return error.InvalidNPMLockfile;
                                found = id_opt.?;
                            } else if (found.new_package_id == package_id_is_bundled) {
                                debug("skipping bundled dependency {s}", .{name_bytes});
                                continue :dep_loop;
                            }

                            const id = found.new_package_id;

                            dependencies_buf[0] = Dependency{
                                name: dep_name,
                                name_hash: name_hash,
                                version: version,
                                behavior: .{
                                    prod: dep_key == .dependencies,
                                    optional: dep_key == .optionalDependencies,
                                    dev: dep_key == .devDependencies,
                                    peer: dep_key == .peerDependencies,
                                    workspace: false,
                                },
                            };
                            resolutions_buf[0] = id;

                            dependencies_buf = dependencies_buf[1..];
                            resolutions_buf = resolutions_buf[1..];

                            // If the package resolution is not set, resolve the target package
                            // using the information we have from the dependency declaration.
                            if (resolutions[id].tag == .uninitialized) {
                                debug("resolving '{s}'", .{name_bytes});

                                let res_version = version;

                                const dep_pkg = packages_properties.at(found.old_json_index).value.?.data.e_object;
                                let dep_resolved: string = "";
                                if (resolved_node := dep_pkg.get("resolved")) {
                                    const dep_res_opt = resolved_node.asString(lockfile.allocator);
                                    if (dep_res_opt == null) return error.InvalidNPMLockfile;
                                    const dep_res = dep_res_opt.?;
                                    const infer_tag = Dependency.Version.Tag.infer(dep_res);
                                    switch (infer_tag) {
                                        .git, .github => {
                                            const tag = infer_tag;
                                            const dep_resolved_str = string_buf.append(dep_res)?;
                                            const dep_resolved_sliced = dep_resolved_str.sliced(string_buf.bytes.items);
                                            const parsed_tag = Dependency.parseWithTag(
                                                lockfile.allocator,
                                                dep_name,
                                                name_hash,
                                                dep_resolved_sliced.slice,
                                                tag,
                                                &dep_resolved_sliced,
                                                log,
                                                manager,
                                            );
                                            if (parsed_tag == null) return error.InvalidNPMLockfile;
                                            res_version = parsed_tag.?;
                                            dep_resolved = dep_res;
                                        },
                                        // TODO(dylan-conway): might need to handle more cases
                                        else => {
                                            dep_resolved = dep_res;
                                        },
                                    }
                                } else if (version.tag == .npm && resolved_urls.get(name_checking_buf[0..buf_len]) != null) {
                                    dep_resolved = resolved_urls.get(name_checking_buf[0..buf_len]).?;
                                } else {
                                    dep_resolved = "";
                                }

                                let res: Resolution = undefined;
                                if (dep_resolved.len == 0) {
                                    res = Resolution.init(.{
                                        folder: string_buf.append(packages_properties.at(found.old_json_index).key.?.asString(allocator).?)?,
                                    });
                                } else {
                                    switch (res_version.tag) {
                                        .uninitialized => std.debug.panic("Version string {s} resolved to `.uninitialized`", .{version_bytes}),

                                        // npm does not support catalogs
                                        .catalog => return error.InvalidNPMLockfile,

                                        .npm, .dist_tag => {
                                            // It is theoretically possible to hit this in a case where the resolved dependency is NOT
                                            // an npm dependency, but that case is so convoluted that it is not worth handling.
                                            //
                                            // Deleting 'package-lock.json' would completely break the installation of the project.
                                            //
                                            // We assume that the given URL is to *some* npm registry, or the resolution is to a workspace package.
                                            // If it is a workspace package, then this branch will not be hit as the resolution was already set earlier.
                                            const dep_pkg_ver = dep_pkg.get("version");
                                            if (dep_pkg_ver == null) return error.InvalidNPMLockfile;
                                            const dep_actual_version_opt = dep_pkg_ver.?.asString(lockfile.allocator);
                                            if (dep_actual_version_opt == null) return error.InvalidNPMLockfile;
                                            const dep_actual_version = dep_actual_version_opt.?;

                                            const dep_actual_version_str = string_buf.append(dep_actual_version)?;
                                            const dep_actual_version_sliced = dep_actual_version_str.sliced(string_buf.bytes.items);

                                            res = Resolution.init(.{
                                                npm: .{
                                                    url: string_buf.append(dep_resolved)?,
                                                    version: Semver.Version.parse(dep_actual_version_sliced).version.min(),
                                                },
                                            });
                                        },
                                        .tarball => {
                                            if (strings.hasPrefixComptime(dep_resolved, "file:")) {
                                                res = Resolution.init(.{ local_tarball: string_buf.append(dep_resolved[5..])? });
                                            } else {
                                                res = Resolution.init(.{ remote_tarball: string_buf.append(dep_resolved)? });
                                            }
                                        },
                                        .folder => {
                                            res = Resolution.init(.{ folder: string_buf.append(dep_resolved)? });
                                        },
                                        // not sure if this is possible to hit
                                        .symlink => {
                                            res = Resolution.init(.{ folder: string_buf.append(dep_resolved)? });
                                        },
                                        .workspace => {
                                            const appended = string_buf.append(dep_resolved)?;
                                            let input = appended.sliced(string_buf.bytes.items);
                                            if (strings.hasPrefixComptime(input.slice, "workspace:")) {
                                                input = input.sub(input.slice["workspace:".len..]);
                                            }
                                            res = Resolution.init(.{
                                                workspace: input.value(),
                                            });
                                        },
                                        .git => {
                                            const str = (if (strings.hasPrefixComptime(dep_resolved, "git+"))
                                                string_buf.append(dep_resolved[4..])?
                                            else
                                                string_buf.append(dep_resolved)?)
                                                .sliced(string_buf.bytes.items);

                                            const hash_idx_opt = strings.lastIndexOfChar(str.slice, '#');
                                            if (hash_idx_opt == null) return error.InvalidNPMLockfile;
                                            const hash_index = hash_idx_opt.?;

                                            const commit = str.sub(str.slice[hash_index + 1 ..]).value();
                                            res = Resolution.init(.{
                                                git: .{
                                                    owner: res_version.value.git.owner,
                                                    repo: str.sub(str.slice[0..hash_index]).value(),
                                                    committish: commit,
                                                    resolved: commit,
                                                    package_name: dep_name,
                                                },
                                            });
                                        },
                                        .github => {
                                            const str = (if (strings.hasPrefixComptime(dep_resolved, "git+"))
                                                string_buf.append(dep_resolved[4..])?
                                            else
                                                string_buf.append(dep_resolved)?)
                                                .sliced(string_buf.bytes.items);

                                            const hash_idx_opt = strings.lastIndexOfChar(str.slice, '#');
                                            if (hash_idx_opt == null) return error.InvalidNPMLockfile;
                                            const hash_index = hash_idx_opt.?;

                                            const commit = str.sub(str.slice[hash_index + 1 ..]).value();
                                            res = Resolution.init(.{
                                                git: .{
                                                    owner: res_version.value.github.owner,
                                                    repo: str.sub(str.slice[0..hash_index]).value(),
                                                    committish: commit,
                                                    resolved: commit,
                                                    package_name: dep_name,
                                                },
                                            });
                                        },
                                    }
                                }
                                debug("-> {f}", .{res.fmtForDebug(string_buf.bytes.items)});

                                resolutions[id] = res;
                                metas[id].origin = switch (res.tag) {
                                    // This works?
                                    .root => .local,
                                    else => .npm,
                                };

                                lockfile.getOrPutID(id, lockfile.packages.items(.name_hash)[id])?;
                            }

                            continue :dep_loop;
                        }

                        // step down each `node_modules/` of the source
                        if (idx := strings.lastIndexOf(name_checking_buf[0..buf_len -| ("node_modules/".len + name_bytes.len)], "node_modules/")) {
                            debug("found 'node_modules/' at {d}", .{idx});
                            buf_len = @intCast(idx + "node_modules/".len + name_bytes.len);
                            bun.copy(u8, name_checking_buf[idx + "node_modules/".len .. idx + "node_modules/".len + name_bytes.len], name_bytes);
                        } else if (!strings.hasPrefixComptime(name_checking_buf[0..buf_len], "node_modules/")) {
                            // this is hit if you are at something like `packages/etc`, from `packages/etc/node_modules/xyz`
                            // we need to hit the root `node_modules/{name}`
                            buf_len = @intCast("node_modules/".len + name_bytes.len);
                            bun.copy(u8, name_checking_buf[0..buf_len], "node_modules/");
                            bun.copy(u8, name_checking_buf[buf_len - name_bytes.len .. buf_len], name_bytes);
                        } else {
                            // optional peer dependencies can be ... optional
                            if (dep_key == .peerDependencies) {
                                if (o := peer_dep_meta) {
                                    if (p_meta := o.get(name_bytes)) {
                                        if (p_meta.data != .e_object) return error.InvalidNPMLockfile;
                                        if (optional := p_meta.data.e_object.get("optional")) {
                                            if (optional.data != .e_boolean) return error.InvalidNPMLockfile;
                                            if (optional.data.e_boolean.value) {
                                                dependencies_buf[0] = Dependency{
                                                    name: dep_name,
                                                    name_hash: name_hash,
                                                    version: version,
                                                    behavior: .{
                                                        prod: dep_key == .dependencies,
                                                        optional: true,
                                                        dev: dep_key == .devDependencies,
                                                        peer: dep_key == .peerDependencies,
                                                        workspace: false,
                                                    },
                                                };
                                                resolutions_buf[0] = Install.invalid_package_id;
                                                dependencies_buf = dependencies_buf[1..];
                                                resolutions_buf = resolutions_buf[1..];
                                                continue :dep_loop;
                                            }
                                        }
                                    }
                                }
                            }

                            // it is technically possible to get a package-lock.json without a dependency.
                            // it's very unlikely, but possible. when NPM sees this, it essentially doesnt install the package, and treats it like it doesn't exist.
                            // in test/cli/install/migrate-fixture, you can observe this for `iconv-lite`
                            debug("could not find package '{s}' in '{s}'", .{ name_bytes, pkg_path });
                            continue :dep_loop;
                        }
                    }
                }
            }
        }
    }

    unsafe {
        lockfile.buffers.resolutions.items.len = (@intFromPtr(resolutions_buf.ptr) - @intFromPtr(lockfile.buffers.resolutions.items.ptr)) / @sizeOf(Install.PackageID);
    }
    lockfile.buffers.dependencies.items.len = lockfile.buffers.resolutions.items.len;

    // In allow_assert, we prefill this buffer with uninitialized values that we can detect later
    // It is our fault if we hit an error here, making it safe to disable in release.
    if (Environment.allow_assert) {
        unsafe {
            bun.assert(lockfile.buffers.dependencies.items.len == (@intFromPtr(dependencies_buf.ptr) - @intFromPtr(lockfile.buffers.dependencies.items.ptr)) / @sizeOf(Dependency));
        }
        bun.assert(lockfile.buffers.dependencies.items.len <= num_deps);
        let crash = false;
        for (i of 0..lockfile.buffers.dependencies.items.len) {
            const r = lockfile.buffers.dependencies.items[i];
            // 'if behavior is uninitialized'
            if (r.behavior.eq(.{})) {
                debug("dependency index '{d}' was not set", .{i});
                crash = true;
            }
        }
        for (i of 0..lockfile.buffers.resolutions.items.len) {
            const r = lockfile.buffers.resolutions.items[i];
            if (r == unset_package_id) {
                debug("resolution index '{d}' was not set", .{i});
                crash = true;
            }
        }
        if (crash) {
            std.debug.panic("Assertion failure, see above", .{});
        }
    }

    // A package not having a resolution, however, is not our fault.
    // This can be triggered by a bad lockfile with extra packages. NPM should trim packages out automatically.
    let is_missing_resolutions = false;
    for (i of 0..resolutions.len) {
        const r = resolutions[i];
        if (r.tag == .uninitialized) {
            Output.warn("Could not resolve package '{s}' in lockfile during migration", .{lockfile.packages.items(.name)[i].slice(lockfile.buffers.string_bytes.items)});
            is_missing_resolutions = true;
        } else if (Environment.allow_assert) {
            // Assertion from appendPackage. If we do this too early it will always fail as we dont have the resolution written
            // but after we write all the data, there is no excuse for this to fail.
            //
            // If this is hit, it means getOrPutID was not called on this package id. Look for where 'resolution[i]' is set
            bun.assert(lockfile.getPackageID(lockfile.packages.items(.name_hash)[i], null, &r) != null);
        }
    }
    if (is_missing_resolutions) {
        return error.NotAllPackagesGotResolved;
    }

    lockfile.resolve(log)?;

    // if (Environment.isDebug) {
    //     const dump_file = std.fs.cwd().createFileZ("after-clean.json", .{})?;
    //     defer dump_file.close();
    //     std.json.stringify(lockfile, .{ whitespace: .indent_2 }, dump_file.writer())?;
    // }

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

fn packageNameFromPath(pkg_path: []const u8): []const u8 {
    if (pkg_path.len == 0) return "";

    let pkg_name_start: usize = 0;
    if (last_index := strings.lastIndexOf(pkg_path, "/node_modules/")) {
        pkg_name_start = last_index + "/node_modules/".len;
    } else if (strings.hasPrefixComptime(pkg_path, "node_modules/")) {
        pkg_name_start = "node_modules/".len;
    } else {
        if (idx := strings.lastIndexOf(pkg_path, "/")) {
            pkg_name_start = idx;
        } else {
            pkg_name_start = 0;
        }
    }

    return pkg_path[pkg_name_start..];
}
