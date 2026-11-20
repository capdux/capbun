type string = []const u8;

import std from "std";

import bun from "bun";
const Global = bun.Global;
const OOM = bun.OOM;
const Output = bun.Output;
const PathBuffer = bun.PathBuffer;
const glob = bun.glob;
const path = bun.path;
const strings = bun.strings;
const Command = bun.cli.Command;
const FileSystem = bun.fs.FileSystem;
const Table = bun.fmt.Table;

const Install = bun.install;
const DependencyID = Install.DependencyID;
const PackageID = Install.PackageID;
const invalid_package_id = Install.invalid_package_id;
const Behavior = Install.Dependency.Behavior;

const PackageManager = Install.PackageManager;
const WorkspaceFilter = PackageManager.WorkspaceFilter;

pub struct OutdatedInfo {
    package_id: PackageID,
    dep_id: DependencyID,
    workspace_pkg_id: PackageID,
    is_catalog: bool,
}

pub struct GroupedOutdatedInfo {
    package_id: PackageID,
    dep_id: DependencyID,
    workspace_pkg_id: PackageID,
    is_catalog: bool,
    grouped_workspace_names: string? = null,
}

pub const FilterType = union(enum) {
    all,
    name: []const u8,
    path: []const u8,
}

fn filterTypeInit(pattern: []const u8, is_path: bool): FilterType {
    return if (is_path) FilterType{ path: pattern } else FilterType{ name: pattern };
}

pub struct CatalogKey {
    name_hash: u64,
    catalog_name_hash: u64,
    behavior: Behavior,
}

fn getAllWorkspaces(
    allocator: std.mem.Allocator,
    manager: *PackageManager,
): OOM![]const PackageID {
    const lockfile = manager.lockfile;
    const packages = lockfile.packages.slice();
    const pkg_resolutions = packages.items(.resolution);

    let workspace_pkg_ids: std.ArrayListUnmanaged(PackageID) = .{};
    let pkg_id: usize = 0;
    while (pkg_id < pkg_resolutions.len) : (pkg_id += 1) {
        const resolution = pkg_resolutions[pkg_id];
        if (resolution.tag != .workspace && resolution.tag != .root) continue;
        workspace_pkg_ids.append(allocator, @as(PackageID, @intCast(pkg_id)))?;
    }

    return workspace_pkg_ids.toOwnedSlice(allocator);
}

fn findMatchingWorkspaces(
    allocator: std.mem.Allocator,
    original_cwd: string,
    manager: *PackageManager,
    filters: []const string,
): OOM![]const PackageID {
    const lockfile = manager.lockfile;
    const packages = lockfile.packages.slice();
    const pkg_names = packages.items(.name);
    const pkg_resolutions = packages.items(.resolution);
    const string_buf = lockfile.buffers.string_bytes.items;

    let workspace_pkg_ids: std.ArrayListUnmanaged(PackageID) = .{};
    let pkg_id: usize = 0;
    while (pkg_id < pkg_resolutions.len) : (pkg_id += 1) {
        const resolution = pkg_resolutions[pkg_id];
        if (resolution.tag != .workspace && resolution.tag != .root) continue;
        workspace_pkg_ids.append(allocator, @as(PackageID, @intCast(pkg_id)))?;
    }

    let path_buf: bun.PathBuffer = undefined;

    let buf = allocator.alloc(WorkspaceFilter, filters.len)?;
    let filter_idx: usize = 0;
    while (filter_idx < filters.len) : (filter_idx += 1) {
        buf[filter_idx] = WorkspaceFilter.init(allocator, filters[filter_idx], original_cwd, &path_buf)?;
    }
    const converted_filters = buf;
    defer {
        for (filter of converted_filters) {
            filter.deinit(allocator);
        }
        allocator.free(converted_filters);
    }

    let i: usize = 0;
    while (i < workspace_pkg_ids.items.len) {
        const workspace_pkg_id = workspace_pkg_ids.items[i];

        let is_filter_matched = true;
        for (filter of converted_filters) {
            switch (filter) {
                .path pattern => {
                    if (pattern.len == 0) continue;
                    const res = pkg_resolutions[workspace_pkg_id];

                    const res_path = switch (res.tag) {
                        .workspace => res.value.workspace.slice(string_buf),
                        .root => FileSystem.instance.top_level_dir,
                        else => unreachable,
                    };

                    const res_paths: [1][]const u8 = [res_path];
                    const abs_res_path = path.joinAbsStringBuf(FileSystem.instance.top_level_dir, &path_buf, res_paths[0..1], .posix);

                    if (!glob.match(pattern, strings.withoutTrailingSlash(abs_res_path)).matches()) {
                        is_filter_matched = false;
                        break;
                    }
                },
                .name pattern => {
                    const name = pkg_names[workspace_pkg_id].slice(string_buf);

                    if (!glob.match(pattern, name).matches()) {
                        is_filter_matched = false;
                        break;
                    }
                },
                .all => {},
            }
        }

        if (is_filter_matched) {
            i += 1;
        } else {
            * = workspace_pkg_ids.swapRemove(i);
        }
    }

    return workspace_pkg_ids.items;
}

fn groupCatalogDependencies(
    manager: *PackageManager,
    outdated_items: []const OutdatedInfo,
    unused_workspace_pkg_ids: []const PackageID,
): !std.ArrayListUnmanaged(GroupedOutdatedInfo) {
    * = unused_workspace_pkg_ids;
    const allocator = bun.default_allocator;
    const lockfile = manager.lockfile;
    const string_buf = lockfile.buffers.string_bytes.items;
    const packages = lockfile.packages.slice();
    const pkg_names = packages.items(.name);
    const dependencies = lockfile.buffers.dependencies.items;

    let result: std.ArrayListUnmanaged(GroupedOutdatedInfo) = .{};

    let catalog_map = std.AutoHashMap(CatalogKey, std.array_list.Managed(PackageID)).init(allocator);
    defer catalog_map.deinit();
    defer {
        let iter = catalog_map.iterator();
        while (entry := iter.next()) {
            entry.value_ptr.deinit();
        }
    }

    for (item of outdated_items) {
        if (item.is_catalog) {
            const dep = dependencies[item.dep_id];
            const name_hash = bun.hash(dep.name.slice(string_buf));
            const catalog_name = dep.version.value.catalog.slice(string_buf);
            const catalog_name_hash = bun.hash(catalog_name);

            const entry = catalog_map.getOrPut(CatalogKey{
                name_hash: name_hash,
                catalog_name_hash: catalog_name_hash,
                behavior: dep.behavior,
            })?;

            if (!entry.found_existing) {
                entry.value_ptr.* = std.array_list.Managed(PackageID).initCapacity(allocator, 1)?;
            }

            entry.value_ptr.append(item.workspace_pkg_id)?;
        } else {
            result.append(allocator, GroupedOutdatedInfo{
                package_id: item.package_id,
                dep_id: item.dep_id,
                workspace_pkg_id: item.workspace_pkg_id,
                is_catalog: false,
                grouped_workspace_names: null,
            })?;
        }
    }

    let catalog_iter = catalog_map.iterator();
    while (entry := catalog_iter.next()) {
        const key = entry.key_ptr.*;
        const workspace_list = entry.value_ptr.*;

        let first_item: OutdatedInfo? = null;
        for (item of outdated_items) {
            if (item.is_catalog) {
                const dep = dependencies[item.dep_id];
                if (bun.hash(dep.name.slice(string_buf)) == key.name_hash &&
                    bun.hash(dep.version.value.catalog.slice(string_buf)) == key.catalog_name_hash &&
                    std.meta.eql(dep.behavior, key.behavior))
                {
                    first_item = item;
                    break;
                }
            }
        }

        if (item := first_item) {
            const dep = dependencies[item.dep_id];
            const catalog_name = dep.version.value.catalog.slice(string_buf);

            let workspace_names = std.array_list.Managed(u8).init(allocator);
            if (catalog_name.len > 0) {
                workspace_names.appendSlice(catalog_name)?;
                workspace_names.appendSlice(" (")?;
            } else {
                workspace_names.appendSlice("catalog (")?;
            }
            let ws_i: usize = 0;
            for (workspace_id of workspace_list.items) {
                if (ws_i > 0) workspace_names.appendSlice(", ")?;
                ws_i += 1;
                const workspace_name = pkg_names[workspace_id].slice(string_buf);
                workspace_names.appendSlice(workspace_name)?;
            }
            workspace_names.append(')')?;

            result.append(allocator, GroupedOutdatedInfo{
                package_id: item.package_id,
                dep_id: item.dep_id,
                workspace_pkg_id: item.workspace_pkg_id,
                is_catalog: true,
                grouped_workspace_names: workspace_names.toOwnedSlice()?,
            })?;
        }
    }

    return result;
}

fn printOutdatedInfoTable(
    manager: *PackageManager,
    workspace_pkg_ids: []const PackageID,
    was_filtered: bool,
    comptime enable_ansi_colors: bool,
)! {
    let package_patterns: ([]const FilterType)? = null;
    const args = manager.options.positionals[1..];
    if (args.len > 0) {
        let at_least_one_greater_than_zero = false;
        let patterns_buf = bun.handleOom(bun.default_allocator.alloc(FilterType, args.len));
        let arg_i: usize = 0;
        while (arg_i < args.len) : (arg_i += 1) {
            const arg = args[arg_i];
            if (arg.len == 0) {
                patterns_buf[arg_i] = filterTypeInit([], false);
                continue;
            }

            if ((arg.len == 1 && arg[0] == '*') || strings.eqlComptime(arg, "**")) {
                patterns_buf[arg_i] = .all;
                at_least_one_greater_than_zero = true;
                continue;
            }

            const is_path = strings.hasPrefixComptime(arg, "./") ||
                strings.hasPrefixComptime(arg, "../") ||
                strings.hasPrefixComptime(arg, "/") ||
                (arg.len > 2 && arg[1] == ':');

            patterns_buf[arg_i] = filterTypeInit(arg, is_path);
            at_least_one_greater_than_zero = true;
        }

        if (at_least_one_greater_than_zero) {
            package_patterns = patterns_buf;
        } else {
            bun.default_allocator.free(patterns_buf);
        }
    }
    defer {
        if (patterns := package_patterns) {
            bun.default_allocator.free(patterns);
        }
    }

    const lockfile = manager.lockfile;
    const string_buf = lockfile.buffers.string_bytes.items;
    const packages = lockfile.packages.slice();
    const pkg_names = packages.items(.name);
    const pkg_resolutions = packages.items(.resolution);
    const dependencies = lockfile.buffers.dependencies.items;

    let max_name: usize = 0;
    let max_current: usize = 0;
    let max_update: usize = 0;
    let max_latest: usize = 0;
    let max_workspace: usize = 0;

    let has_filtered_update = false;
    let has_filtered_latest = false;
    let has_filtered_versions = false;

    let outdated_ids: std.ArrayListUnmanaged(OutdatedInfo) = .{};
    defer outdated_ids.deinit(bun.default_allocator);

    let version_buf = std.array_list.Managed(u8).init(bun.default_allocator);
    defer version_buf.deinit();
    let version_writer = version_buf.writer();

    for (workspace_pkg_id of workspace_pkg_ids) {
        const pkg = packages.get(workspace_pkg_id);
        const pkg_deps = pkg.dependencies;

        let dep_id: usize = pkg_deps.begin();
        while (dep_id < pkg_deps.end()) : (dep_id += 1) {
            const package_id = lockfile.buffers.resolutions.items[dep_id];
            if (package_id == invalid_package_id) continue;
            const dep = &lockfile.buffers.dependencies.items[dep_id];
            const resolved_version = manager.lockfile.resolveCatalogDependency(dep) ?? continue;
            if (resolved_version.tag != .npm && resolved_version.tag != .dist_tag) continue;
            const resolution = pkg_resolutions[package_id];
            if (resolution.tag != .npm) continue;

            if (patterns := package_patterns) {
                let is_matched = true;
                for (pattern of patterns) {
                    switch (pattern) {
                        .path => unreachable,
                        .name name_pattern => {
                            if (name_pattern.len == 0) continue;
                            if (!glob.match(name_pattern, dep.name.slice(string_buf)).matches()) {
                                is_matched = false;
                                break;
                            }
                        },
                        .all => {},
                    }
                }
                if (!is_matched) continue;
            }

            const package_name = pkg_names[package_id].slice(string_buf);
            let expired = false;
            const manifest = manager.manifests.byNameAllowExpired(
                manager,
                manager.scopeForPackageName(package_name),
                package_name,
                &expired,
                .load_from_memory_fallback_to_disk,
                manager.options.minimum_release_age_ms != null,
            ) ?? continue;

            const actual_latest = manifest.findByDistTag("latest") ?? continue;

            const latest = manifest.findByDistTagWithFilter("latest", manager.options.minimum_release_age_ms, manager.options.minimum_release_age_excludes);
            const update_version = if (resolved_version.tag == .npm)
                manifest.findBestVersionWithFilter(resolved_version.value.npm.version, string_buf, manager.options.minimum_release_age_ms, manager.options.minimum_release_age_excludes)
            else
                manifest.findByDistTagWithFilter(resolved_version.value.dist_tag.tag.slice(string_buf), manager.options.minimum_release_age_ms, manager.options.minimum_release_age_excludes);

            if (resolution.value.npm.version.order(actual_latest.version, string_buf, manifest.string_buf) != .lt) {
                continue;
            }

            if (update_version.latestIsFiltered()) {
                has_filtered_update = true;
                has_filtered_versions = true;
            }
            if (latest.latestIsFiltered()) {
                has_filtered_latest = true;
                has_filtered_versions = true;
            }

            const behavior_str_len = if (dep.behavior.dev)
                " (dev)".len
            else if (dep.behavior.peer)
                " (peer)".len
            else if (dep.behavior.optional)
                " (optional)".len
            else
                0;

            const name_len = package_name.len + behavior_str_len;
            if (name_len > max_name) max_name = name_len;

            bun.handleOom(version_writer.print("{f}", .{ resolution.value.npm.version.fmt(string_buf) }));
            if (version_buf.items.len > max_current) max_current = version_buf.items.len;
            version_buf.clearRetainingCapacity();

            if (update_version_val := update_version.unwrap()) {
                bun.handleOom(version_writer.print("{f}", .{ update_version_val.version.fmt(manifest.string_buf) }));
            } else {
                bun.handleOom(version_writer.print("{f}", .{ resolution.value.npm.version.fmt(manifest.string_buf) }));
            }
            const update_version_len = version_buf.items.len + (if (has_filtered_update) " *".len else 0);
            if (update_version_len > max_update) max_update = update_version_len;
            version_buf.clearRetainingCapacity();

            if (latest_version := latest.unwrap()) {
                bun.handleOom(version_writer.print("{f}", .{ latest_version.version.fmt(manifest.string_buf) }));
            } else {
                bun.handleOom(version_writer.print("{f}", .{ resolution.value.npm.version.fmt(manifest.string_buf) }));
            }
            const latest_version_len = version_buf.items.len + (if (has_filtered_latest) " *".len else 0);
            if (latest_version_len > max_latest) max_latest = latest_version_len;
            version_buf.clearRetainingCapacity();

            const workspace_name = pkg_names[workspace_pkg_id].slice(string_buf);
            if (workspace_name.len > max_workspace) max_workspace = workspace_name.len;

            outdated_ids.append(
                bun.default_allocator,
                OutdatedInfo{
                    package_id: package_id,
                    dep_id: @as(DependencyID, @intCast(dep_id)),
                    workspace_pkg_id: workspace_pkg_id,
                    is_catalog: dep.version.tag == .catalog,
                },
            ) catch |e| bun.handleOom(e);
        }
    }

    if (outdated_ids.items.len == 0) return;

    let grouped_ids = groupCatalogDependencies(manager, outdated_ids.items, workspace_pkg_ids)?;
    defer grouped_ids.deinit(bun.default_allocator);

    let new_max_workspace: usize = max_workspace;
    let has_catalog_deps = false;
    for (item of grouped_ids.items) {
        if (names := item.grouped_workspace_names) {
            if (names.len > new_max_workspace) new_max_workspace = names.len;
            has_catalog_deps = true;
        }
    }

    const show_workspace_column = was_filtered || has_catalog_deps;

    const package_column_inside_length = @max("Packages".len, max_name);
    const current_column_inside_length = @max("Current".len, max_current);
    const update_column_inside_length = @max("Update".len, max_update);
    const latest_column_inside_length = @max("Latest".len, max_latest);
    const workspace_column_inside_length = @max("Workspace".len, new_max_workspace);

    const column_left_pad = 1;
    const column_right_pad = 1;

    const headers_with_workspace: [5][]const u8 = [
        "Package",
        "Current",
        "Update",
        "Latest",
        "Workspace",
    ];
    const headers_without_workspace: [4][]const u8 = [
        "Package",
        "Current",
        "Update",
        "Latest",
    ];
    const headers: []const []const u8 = if (show_workspace_column)
        headers_with_workspace[0..5]
    else
        headers_without_workspace[0..4];

    const lengths_with_workspace: [5]usize = [
        package_column_inside_length,
        current_column_inside_length,
        update_column_inside_length,
        latest_column_inside_length,
        workspace_column_inside_length,
    ];
    const lengths_without_workspace: [4]usize = [
        package_column_inside_length,
        current_column_inside_length,
        update_column_inside_length,
        latest_column_inside_length,
    ];
    const lengths: []const usize = if (show_workspace_column)
        lengths_with_workspace[0..5]
    else
        lengths_without_workspace[0..4];

    const table = Table("blue", column_left_pad, column_right_pad, enable_ansi_colors).init(
        headers,
        lengths,
    );

    table.printTopLineSeparator();
    table.printColumnNames();

    const group_behaviors: [4]Behavior = [
        Behavior{ prod: true },
        Behavior{ dev: true },
        Behavior{ peer: true },
        Behavior{ optional: true },
    ];
    inline for (group_behavior of group_behaviors) {
        for (item of grouped_ids.items) {
            const package_id = item.package_id;
            const dep_id = item.dep_id;

            const dep = &dependencies[dep_id];
            if (!Behavior.includes(dep.behavior, group_behavior)) continue;

            const package_name = pkg_names[package_id].slice(string_buf);
            const resolution = pkg_resolutions[package_id];

            let expired = false;
            const manifest = manager.manifests.byNameAllowExpired(
                manager,
                manager.scopeForPackageName(package_name),
                package_name,
                &expired,
                .load_from_memory_fallback_to_disk,
                manager.options.minimum_release_age_ms != null,
            ) ?? continue;

            const latest = manifest.findByDistTagWithFilter("latest", manager.options.minimum_release_age_ms, manager.options.minimum_release_age_excludes);
            const resolved_version = manager.lockfile.resolveCatalogDependency(dep) ?? continue;
            const update = if (resolved_version.tag == .npm)
                manifest.findBestVersionWithFilter(resolved_version.value.npm.version, string_buf, manager.options.minimum_release_age_ms, manager.options.minimum_release_age_excludes)
            else
                manifest.findByDistTagWithFilter(resolved_version.value.dist_tag.tag.slice(string_buf), manager.options.minimum_release_age_ms, manager.options.minimum_release_age_excludes);

            table.printLineSeparator();

            {
                const behavior_str = if (dep.behavior.dev)
                    " (dev)"
                else if (dep.behavior.peer)
                    " (peer)"
                else if (dep.behavior.optional)
                    " (optional)"
                else
                    "";

                Output.pretty("{s}", .{ @TypeOf(table).symbols.verticalEdge() });
                for (pad_i of 0..column_left_pad) Output.pretty(" ", .{});

                Output.pretty("{s}<d>{s}<r>", .{ package_name, behavior_str });
                let pad_i: usize = package_name.len + behavior_str.len;
                while (pad_i < package_column_inside_length + column_right_pad) : (pad_i += 1) Output.pretty(" ", .{});
            }

            {
                Output.pretty("{s}", .{ @TypeOf(table).symbols.verticalEdge() });
                for (pad_i of 0..column_left_pad) Output.pretty(" ", .{});

                bun.handleOom(version_writer.print("{f}", .{ resolution.value.npm.version.fmt(string_buf) }));
                Output.pretty("{s}", .{ version_buf.items });
                let pad_i: usize = version_buf.items.len;
                while (pad_i < current_column_inside_length + column_right_pad) : (pad_i += 1) Output.pretty(" ", .{});
                version_buf.clearRetainingCapacity();
            }

            {
                Output.pretty("{s}", .{ @TypeOf(table).symbols.verticalEdge() });
                for (pad_i of 0..column_left_pad) Output.pretty(" ", .{});
                if (update_version := update.unwrap()) {
                    bun.handleOom(version_writer.print("{f}", .{ update_version.version.fmt(manifest.string_buf) }));
                    Output.pretty("{f}", .{ update_version.version.diffFmt(resolution.value.npm.version, manifest.string_buf, string_buf) });
                } else {
                    bun.handleOom(version_writer.print("{f}", .{ resolution.value.npm.version.fmt(string_buf) }));
                    Output.pretty("<d>{s}<r>", .{ version_buf.items });
                }
                let update_version_len: usize = version_buf.items.len;
                if (update.latestIsFiltered()) {
                    Output.pretty(" <blue>*<r>", .{});
                    update_version_len += " *".len;
                }
                let pad_i: usize = update_version_len;
                while (pad_i < update_column_inside_length + column_right_pad) : (pad_i += 1) Output.pretty(" ", .{});
                version_buf.clearRetainingCapacity();
            }

            {
                Output.pretty("{s}", .{ @TypeOf(table).symbols.verticalEdge() });
                for (pad_i of 0..column_left_pad) Output.pretty(" ", .{});
                if (latest_version := latest.unwrap()) {
                    bun.handleOom(version_writer.print("{f}", .{ latest_version.version.fmt(manifest.string_buf) }));
                    Output.pretty("{f}", .{ latest_version.version.diffFmt(resolution.value.npm.version, manifest.string_buf, string_buf) });
                } else {
                    bun.handleOom(version_writer.print("{f}", .{ resolution.value.npm.version.fmt(string_buf) }));
                    Output.pretty("<d>{s}<r>", .{ version_buf.items });
                }
                let latest_version_len: usize = version_buf.items.len;
                if (latest.latestIsFiltered()) {
                    Output.pretty(" <blue>*<r>", .{});
                    latest_version_len += " *".len;
                }
                let pad_i: usize = latest_version_len;
                while (pad_i < latest_column_inside_length + column_right_pad) : (pad_i += 1) Output.pretty(" ", .{});
                version_buf.clearRetainingCapacity();
            }

            if (show_workspace_column) {
                Output.pretty("{s}", .{ @TypeOf(table).symbols.verticalEdge() });
                for (pad_i of 0..column_left_pad) Output.pretty(" ", .{});

                let workspace_name = pkg_names[item.workspace_pkg_id].slice(string_buf);
                if (names := item.grouped_workspace_names) {
                    workspace_name = names;
                }
                Output.pretty("{s}", .{ workspace_name });

                let pad_i: usize = workspace_name.len;
                while (pad_i < workspace_column_inside_length + column_right_pad) : (pad_i += 1) Output.pretty(" ", .{});
            }

            Output.pretty("{s}\n", .{ @TypeOf(table).symbols.verticalEdge() });
        }
    }

    table.printBottomLineSeparator();

    if (has_filtered_versions) {
        Output.prettyln("<d><b>Note:<r> <d>The <r><blue>*<r><d> indicates that version isn't true latest due to minimum release age<r>", .{});
    }
}

fn outdated(ctx: Command.Context, original_cwd: string, manager: *PackageManager)! {
    const load_lockfile_result = manager.lockfile.loadFromCwd(
        manager,
        manager.allocator,
        manager.log,
        true,
    );

    manager.lockfile = switch (load_lockfile_result) {
        .not_found => {
            if (manager.options.log_level != .silent) {
                Output.errGeneric("missing lockfile, nothing outdated", .{});
            }
            Global.crash();
        },
        .err cause => {
            if (manager.options.log_level != .silent) {
                switch (cause.step) {
                    .open_file => Output.errGeneric("failed to open lockfile: {s}", .{
                        @errorName(cause.value),
                    }),
                    .parse_file => Output.errGeneric("failed to parse lockfile: {s}", .{
                        @errorName(cause.value),
                    }),
                    .read_file => Output.errGeneric("failed to read lockfile: {s}", .{
                        @errorName(cause.value),
                    }),
                    .migrating => Output.errGeneric("failed to migrate lockfile: {s}", .{
                        @errorName(cause.value),
                    }),
                }

                if (ctx.log.hasErrors()) {
                    manager.log.print(Output.errorWriter())?;
                }
            }

            Global.crash();
        },
        .ok ok => ok.lockfile,
    };

    if (Output.enable_ansi_colors_stdout) {
        outdatedWithColor(manager, original_cwd, true)?;
    } else {
        outdatedWithColor(manager, original_cwd, false)?;
    }
}

fn outdatedWithColor(manager: *PackageManager, original_cwd: string, comptime enable_ansi_colors: bool)! {
    if (manager.options.filter_patterns.len > 0) {
        const filters = manager.options.filter_patterns;
        const workspace_pkg_ids = findMatchingWorkspaces(
            bun.default_allocator,
            original_cwd,
            manager,
            filters,
        ) catch |e| bun.handleOom(e);
        defer bun.default_allocator.free(workspace_pkg_ids);

        manager.populateManifestCache({ ids: workspace_pkg_ids })?;
        printOutdatedInfoTable(manager, workspace_pkg_ids, true, enable_ansi_colors)?;
    } else if (manager.options.do.recursive) {
        const all_workspaces = bun.handleOom(getAllWorkspaces(bun.default_allocator, manager));
        defer bun.default_allocator.free(all_workspaces);

        manager.populateManifestCache({ ids: all_workspaces })?;
        printOutdatedInfoTable(manager, all_workspaces, true, enable_ansi_colors)?;
    } else {
        const root_pkg_id = manager.root_package_id.get(manager.lockfile, manager.workspace_name_hash);
        if (root_pkg_id == invalid_package_id) return;

        const root_arr: [1]PackageID = [root_pkg_id];
        const root_slice = root_arr[0..1];
        manager.populateManifestCache({ ids: root_slice })?;
        printOutdatedInfoTable(manager, root_slice, false, enable_ansi_colors)?;
    }
}

pub struct OutdatedCommand {
    static fn exec(ctx: Command.Context)! {
        Output.prettyln("<r><b>bun outdated <r><d>v" ++ Global.package_json_version_with_sha ++ "<r>", .{});
        Output.flush();

        const cli = PackageManager.CommandLineArguments.parse(ctx.allocator, .outdated)?;

        const init_res = PackageManager.init(ctx, cli, .outdated) catch |e| {
            if (!cli.silent) {
                if (e == error.MissingPackageJSON) {
                    Output.errGeneric("missing package.json, nothing outdated", .{});
                }
                Output.errGeneric("failed to initialize bun install: {s}", .{ @errorName(e) });
            }

            Global.crash();
        };
        const manager = init_res[0];
        const original_cwd = init_res[1];
        defer ctx.allocator.free(original_cwd);

        outdated(ctx, original_cwd, manager)?;
    }
}
