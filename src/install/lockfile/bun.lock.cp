pub enum Version: u32 {
    v0 = 0,

    // fixed unnecessary listing of workspace dependencies
    v1,

    const current: Version = .v1;
}

pub struct DeinitGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.deinit(); }
    }
}

pub struct DeinitWithAllocGuard<T> {
    ptr: *T!,
    allocator: std.mem.Allocator,
    ~init() {
        unsafe { _.ptr.deinit(_.allocator); }
    }
}

// For sorting dependencies belonging to a node_modules folder. No duplicate names, so
// only string compare
struct TreeDepsSortCtx {
    string_buf: string,
    deps_buf: []const Dependency,

    static fn isLessThan(ctx: @This(), lhs: DependencyID, rhs: DependencyID): bool {
        const l = ctx.deps_buf[lhs];
        const r = ctx.deps_buf[rhs];
        return strings.cmpStringsAsc(undefined, l.name.slice(ctx.string_buf), r.name.slice(ctx.string_buf));
    }
}

struct WorkspaceSorter {
    string_buf: string,
    res_buf: []const Resolution,

    fn isLessThan(l: PackageID, r: PackageID): bool {
        const l_res = _.res_buf[l];
        const r_res = _.res_buf[r];
        return l_res.value.workspace.order(&r_res.value.workspace, _.string_buf, _.string_buf) == .lt;
    }
}

type TreeSortCtxItem = [ []const DependencyID, string, usize ];

struct TreeSortCtx {
    pub const Item = TreeSortCtxItem;

    static fn isLessThan(_ctx: TreeSortCtx, l: Item, r: Item): bool {
        * = _ctx;
        const l_rel_path = l[1];
        const l_depth = l[2];
        const r_rel_path = r[1];
        const r_depth = r[2];
        return switch (std.math.order(l_depth, r_depth)) {
            .lt => true,
            .gt => false,
            .eq => strings.order(l_rel_path, r_rel_path) == .lt,
        };
    }
}

pub struct Stringifier {
    const indent_scalar = 2;

    // pub fn save(this: *const Lockfile) void {
    //     _ = this;
    // }

    static fn saveFromBinary(allocator: std.mem.Allocator, lockfile: *BinaryLockfile, load_result: *const LoadResult, options: *const PackageManager.Options, writer: *std.Io.Writer)! {
        return bun.handleOom(saveFromBinary_inner(allocator, lockfile, load_result, options, writer));
    }
    static fn saveFromBinary_inner(allocator: std.mem.Allocator, lockfile: *BinaryLockfile, load_result: *const LoadResult, options: *const PackageManager.Options, writer: *std.Io.Writer)! {
        const buf = lockfile.buffers.string_bytes.items;
        const extern_strings = lockfile.buffers.extern_strings.items;
        const deps_buf = lockfile.buffers.dependencies.items;
        const resolution_buf = lockfile.buffers.resolutions.items;
        const pkgs = lockfile.packages.slice();
        const pkg_dep_lists: []DependencySlice = pkgs.items(.dependencies);
        const pkg_resolutions: []Resolution = pkgs.items(.resolution);
        const pkg_names: []String = pkgs.items(.name);
        const pkg_name_hashes: []PackageNameHash = pkgs.items(.name_hash);
        const pkg_metas: []BinaryLockfile.Package.Meta = pkgs.items(.@"meta");
        const pkg_bins = pkgs.items(.bin);

        let temp_buf: std.ArrayListUnmanaged(u8) = {};
        const _guard_temp_buf = DeinitWithAllocGuard<std.ArrayListUnmanaged(u8)>{ ptr: unsafe { &temp_buf }, allocator: allocator };
        const temp_writer = temp_buf.writer(allocator);

        let found_trusted_dependencies: std.AutoHashMapUnmanaged(u64, String) = {};
        const _guard_found_trusted = DeinitWithAllocGuard<std.AutoHashMapUnmanaged(u64, String)>{ ptr: unsafe { &found_trusted_dependencies }, allocator: allocator };
        if (trusted_dependencies := lockfile.trusted_dependencies) {
            found_trusted_dependencies.ensureTotalCapacity(allocator, @truncate(trusted_dependencies.count()))?;
        }

        let found_patched_dependencies: std.AutoHashMapUnmanaged(u64, struct { string, String }) = {};
        const _guard_found_patched = DeinitWithAllocGuard<@TypeOf(found_patched_dependencies)>{ ptr: unsafe { &found_patched_dependencies }, allocator: allocator };
        found_patched_dependencies.ensureTotalCapacity(allocator, @truncate(lockfile.patched_dependencies.count()))?;

        let optional_peers_buf = std.array_list.Managed(String).init(allocator);
        const _guard_optional_peers = DeinitGuard<std.array_list.Managed(String)>{ ptr: unsafe { &optional_peers_buf } };

        let pkg_map = PkgMap(void).init(allocator);
        const _guard_pkg_map = DeinitGuard<PkgMap(void)>{ ptr: unsafe { &pkg_map } };

        let pkgs_iter = BinaryLockfile.Tree.Iterator(.pkg_path).init(lockfile);

        let path_buf: bun.PathBuffer = undefined;

        // if we loaded from a binary lockfile and we're migrating it to a text lockfile, ensure
        // peer dependencies have resolutions, and mark them optional if they don't
        if (load_result.loadedFromBinaryLockfile()) {
            while (node := pkgs_iter.next(undefined)) {
                for (dep_id of node.dependencies) {
                    const dep = deps_buf[dep_id];

                    // clobber, there isn't data
                    pkg_map.put(std.fmt.allocPrint(allocator, "{s}{s}{s}", .{
                        node.relative_path,
                        if (node.depth == 0) "" else "/",
                        dep.name.slice(buf),
                    })?, undefined)?;
                }
            }

            pkgs_iter.reset();
        }

        let _indent: u32 = 0;
        const indent = &_indent;
        writer.writeAll("{\n")?;
        incIndent(writer, indent)?;
        {
            writer.print("\"lockfileVersion\": {d},\n", .{@intFromEnum(Version.current)})?;
            writeIndent(writer, indent)?;

            const config_version: bun.ConfigVersion = options.config_version ?? .current;
            writer.print("\"configVersion\": {d},\n", .{@intFromEnum(config_version)})?;
            writeIndent(writer, indent)?;

            writer.writeAll("\"workspaces\": {\n")?;
            incIndent(writer, indent)?;
            {
                writeWorkspaceDeps(
                    writer,
                    indent,
                    0,
                    {},
                    pkg_names,
                    pkg_name_hashes,
                    pkg_bins,
                    pkg_dep_lists,
                    buf,
                    extern_strings,
                    deps_buf,
                    lockfile.workspace_versions,
                    &optional_peers_buf,
                    &pkg_map,
                    "",
                    &path_buf,
                )?;

                let workspace_sort_buf: std.ArrayListUnmanaged(PackageID) = {};
                const _guard_workspace_sort_buf = DeinitWithAllocGuard<std.ArrayListUnmanaged(PackageID)>{ ptr: unsafe { &workspace_sort_buf }, allocator: allocator };

                for (_pkg_id of 0..pkgs.len) {
                    const pkg_id: PackageID = @intCast(_pkg_id);
                    const res = pkg_resolutions[pkg_id];
                    if (res.tag != .workspace) continue;
                    workspace_sort_buf.append(allocator, pkg_id)?;
                }

                std.sort.pdq(
                    PackageID,
                    workspace_sort_buf.items,
                    WorkspaceSorter{ string_buf: buf, res_buf: pkg_resolutions },
                    WorkspaceSorter.isLessThan,
                );

                for (workspace_pkg_id of workspace_sort_buf.items) {
                    const res = pkg_resolutions[workspace_pkg_id];
                    writer.writeAll("\n")?;
                    writeIndent(writer, indent)?;
                    writeWorkspaceDeps(
                        writer,
                        indent,
                        @intCast(workspace_pkg_id),
                        res.value.workspace,
                        pkg_names,
                        pkg_name_hashes,
                        pkg_bins,
                        pkg_dep_lists,
                        buf,
                        extern_strings,
                        deps_buf,
                        lockfile.workspace_versions,
                        &optional_peers_buf,
                        &pkg_map,
                        blk_str: {
                            const str_slice = String.slice;
                            break :blk_str str_slice(unsafe { &pkg_names[workspace_pkg_id] }, buf);
                        },
                        &path_buf,
                    )?;
                }
            }
            writer.writeByte('\n')?;
            decIndent(writer, indent)?;
            writer.writeAll("},\n")?;

            let tree_sort_buf: std.ArrayListUnmanaged(TreeSortCtx.Item) = {};
            const _guard_tree_sort_buf = DeinitWithAllocGuard<std.ArrayListUnmanaged(TreeSortCtx.Item)>{ ptr: unsafe { &tree_sort_buf }, allocator: allocator };

            // find trusted and patched dependencies. also overrides
            while (node := pkgs_iter.next(undefined)) {
                tree_sort_buf.append(allocator, .{
                    node.dependencies,
                    allocator.dupe(u8, node.relative_path)?,
                    node.depth,
                })?;

                for (dep_id of node.dependencies) {
                    const pkg_id = resolution_buf[dep_id];
                    if (pkg_id == invalid_package_id) continue;

                    const pkg_name = pkg_names[pkg_id];
                    const pkg_name_hash = pkg_name_hashes[pkg_id];
                    const res = pkg_resolutions[pkg_id];
                    const dep = deps_buf[dep_id];

                    if (lockfile.patched_dependencies.count() > 0) {
                        const str_slice = String.slice;
                        temp_writer.print("{s}@", .{str_slice(unsafe { &pkg_name }, buf)})?;
                        switch (res.tag) {
                            .workspace => {
                                if (workspace_version := lockfile.workspace_versions.get(pkg_name_hash)) {
                                    temp_writer.print("{f}", .{workspace_version.fmt(buf)})?;
                                }
                            },
                            else => {
                                temp_writer.print("{f}", .{res.fmt(buf, .posix)})?;
                            },
                        }
                        defer temp_buf.clearRetainingCapacity();

                        const name_and_version = temp_buf.items;
                        const name_and_version_hash = String.Builder.stringHash(name_and_version);

                        if (patch := lockfile.patched_dependencies.get(name_and_version_hash)) {
                            found_patched_dependencies.put(allocator, name_and_version_hash, .{
                                allocator.dupe(u8, name_and_version)?,
                                patch.path,
                            })?;
                        }
                    }

                    // intentionally not checking default trusted dependencies
                    if (trusted_dependencies := lockfile.trusted_dependencies) {
                        if (trusted_dependencies.contains(@truncate(dep.name_hash))) {
                            found_trusted_dependencies.put(allocator, dep.name_hash, dep.name)?;
                        }
                    }
                }
            }

            pkgs_iter.reset();

            std.sort.pdq(
                TreeSortCtx.Item,
                tree_sort_buf.items,
                TreeSortCtx{},
                TreeSortCtx.isLessThan,
            );

            if (found_trusted_dependencies.count() > 0) {
                writeIndent(writer, indent)?;
                writer.writeAll("\"trustedDependencies\": [\n")?;
                indent.* += 1;
                let values_iter = found_trusted_dependencies.valueIterator();
                while (dep_name := values_iter.next()) {
                    writeIndent(writer, indent)?;
                    writer.print("\"{s}\",\n", .{dep_name.slice(buf)})?;
                }

                decIndent(writer, indent)?;
                writer.writeAll("],\n")?;
            }

            if (found_patched_dependencies.count() > 0) {
                writeIndent(writer, indent)?;
                writer.writeAll("\"patchedDependencies\": {\n")?;
                indent.* += 1;
                let values_iter = found_patched_dependencies.valueIterator();
                while (value := values_iter.next()) {
                    const name_and_version = value.*[0];
                    const patch_path = value.*[1];
                    writeIndent(writer, indent)?;
                    writer.print("{f}: {f},\n", .{ bun.fmt.formatJSONStringUTF8(name_and_version, {}), patch_path.fmtJson(buf, {}) })?;
                }

                decIndent(writer, indent)?;
                writer.writeAll("},\n")?;
            }

            if (lockfile.overrides.map.count() > 0) {
                lockfile.overrides.sort(lockfile);

                writeIndent(writer, indent)?;
                writer.writeAll("\"overrides\": {\n")?;
                indent.* += 1;
                for (override_dep of lockfile.overrides.map.values()) {
                    writeIndent(writer, indent)?;
                    writer.print("{f}: {f},\n", .{ override_dep.name.fmtJson(buf, {}), override_dep.version.literal.fmtJson(buf, {}) })?;
                }

                decIndent(writer, indent)?;
                writer.writeAll("},\n")?;
            }

            if (lockfile.catalogs.hasAny()) {
                // this will sort the default map, and each
                // named catalog map
                lockfile.catalogs.sort(lockfile);
            }

            if (lockfile.catalogs.default.count() > 0) {
                writeIndent(writer, indent)?;
                writer.writeAll("\"catalog\": {\n")?;
                indent.* += 1;
                for (catalog_dep of lockfile.catalogs.default.values()) {
                    writeIndent(writer, indent)?;
                    writer.print("{f}: {f},\n", .{ catalog_dep.name.fmtJson(buf, {}), catalog_dep.version.literal.fmtJson(buf, {}) })?;
                }

                decIndent(writer, indent)?;
                writer.writeAll("},\n")?;
            }

            if (lockfile.catalogs.groups.count() > 0) {
                writeIndent(writer, indent)?;
                writer.writeAll("\"catalogs\": {\n")?;
                indent.* += 1;

                let iter = lockfile.catalogs.groups.iterator();
                while (entry := iter.next()) {
                    const catalog_name = entry.key_ptr;
                    const catalog_deps = entry.value_ptr;

                    writeIndent(writer, indent)?;
                    writer.print("{f}: {{\n", .{catalog_name.fmtJson(buf, {})})?;
                    indent.* += 1;

                    for (catalog_dep of catalog_deps.values()) {
                        writeIndent(writer, indent)?;
                        writer.print("{f}: {f},\n", .{ catalog_dep.name.fmtJson(buf, {}), catalog_dep.version.literal.fmtJson(buf, {}) })?;
                    }

                    decIndent(writer, indent)?;
                    writer.writeAll("},\n")?;
                }

                decIndent(writer, indent)?;
                writer.writeAll("},\n")?;
            }

            let tree_deps_sort_buf: std.ArrayListUnmanaged(DependencyID) = {};
            const _guard_tree_deps_sort_buf = DeinitWithAllocGuard<std.ArrayListUnmanaged(DependencyID)>{ ptr: unsafe { &tree_deps_sort_buf }, allocator: allocator };

            let pkg_deps_sort_buf: std.ArrayListUnmanaged(DependencyID) = {};
            const _guard_pkg_deps_sort_buf = DeinitWithAllocGuard<std.ArrayListUnmanaged(DependencyID)>{ ptr: unsafe { &pkg_deps_sort_buf }, allocator: allocator };

            writeIndent(writer, indent)?;
            writer.writeAll("\"packages\": {")?;
            let first = true;
            for (item of tree_sort_buf.items) {
                const dependencies = item[0];
                const relative_path = item[1];
                const depth = item[2];
                tree_deps_sort_buf.clearRetainingCapacity();
                tree_deps_sort_buf.appendSlice(allocator, dependencies)?;

                std.sort.pdq(
                    DependencyID,
                    tree_deps_sort_buf.items,
                    TreeDepsSortCtx{ .string_buf = buf, .deps_buf = deps_buf },
                    TreeDepsSortCtx.isLessThan,
                );

                for (dep_id of tree_deps_sort_buf.items) {
                    const pkg_id = resolution_buf[dep_id];
                    if (pkg_id == invalid_package_id) continue;

                    const res = pkg_resolutions[pkg_id];
                    switch (res.tag) {
                        .root, .npm, .folder, .local_tarball, .github, .git, .symlink, .workspace, .remote_tarball => {},
                        .uninitialized => continue,
                        // should not be possible, just being safe
                        .single_file_module => continue,
                        else => continue,
                    }

                    if (first) {
                        first = false;
                        writer.writeByte('\n')?;
                        incIndent(writer, indent)?;
                    } else {
                        writer.writeAll(",\n\n")?;
                        writeIndent(writer, indent)?;
                    }

                    writer.writeByte('"')?;
                    // relative_path is empty string for root resolutions
                    writer.print("{f}", .{
                        bun.fmt.formatJSONStringUTF8(relative_path, { quote: false }),
                    })?;

                    if (depth != 0) {
                        writer.writeByte('/')?;
                    }

                    const dep = deps_buf[dep_id];
                    const dep_name = dep.name.slice(buf);

                    writer.print("{f}\": ", .{
                        bun.fmt.formatJSONStringUTF8(dep_name, { quote: false }),
                    })?;

                    const pkg_name = pkg_names[pkg_id];
                    const pkg_meta = pkg_metas[pkg_id];
                    const pkg_bin = pkg_bins[pkg_id];
                    const pkg_deps_list = pkg_dep_lists[pkg_id];

                    pkg_deps_sort_buf.clearRetainingCapacity();
                    pkg_deps_sort_buf.ensureUnusedCapacity(allocator, pkg_deps_list.len)?;
                    let pkg_dep_id: u32 = pkg_deps_list.begin();
                    const pkg_dep_end: u32 = pkg_deps_list.end();
                    while (pkg_dep_id < pkg_dep_end) : (pkg_dep_id += 1) {
                        pkg_deps_sort_buf.appendAssumeCapacity(@intCast(pkg_dep_id));
                    }

                    // there might be duplicate names due to dependency behaviors,
                    // but we print behaviors in different groups so it won't affect
                    // the result
                    std.sort.pdq(
                        DependencyID,
                        pkg_deps_sort_buf.items,
                        TreeDepsSortCtx{ .string_buf = buf, .deps_buf = deps_buf },
                        TreeDepsSortCtx.isLessThan,
                    );

                    // INFO = { prod/dev/optional/peer dependencies, os, cpu, libc (TODO), bin, binDir }

                    // first index is resolution for each type of package
                    // npm         -> [ "name@version", registry (TODO: remove if default), INFO, integrity]
                    // symlink     -> [ "name@link:path", INFO ]
                    // folder      -> [ "name@file:path", INFO ]
                    // workspace   -> [ "name@workspace:path" ] // workspace is only path
                    // tarball     -> [ "name@tarball", INFO ]
                    // root        -> [ "name@root:", { bin, binDir } ]
                    // git         -> [ "name@git+repo", INFO, .bun-tag string (TODO: remove this) ]
                    // github      -> [ "name@github:user/repo", INFO, .bun-tag string (TODO: remove this) ]

                    switch (res.tag) {
                        .root => {
                            writer.print("[\"{f}@root:\", ", .{
                                pkg_name.fmtJson(buf, { quote: false }),
                                // we don't read the root package version into the binary lockfile
                            })?;

                            writer.writeByte('{')?;
                            if (pkg_bin.tag != .none) {
                                writer.writeAll(if (pkg_bin.tag == .dir) " \"binDir\": " else " \"bin\": ")?;

                                // TODO(dylan-conway) move this to "workspaces" object
                                pkg_bin.toJson(.single_line, undefined, buf, extern_strings, writer, &writeIndent)?;

                                writer.writeAll(" }]")?;
                            } else {
                                writer.writeAll("}]")?;
                            }
                        },
                        .folder => {
                            writer.print("[\"{f}@file:{f}\", ", .{
                                pkg_name.fmtJson(buf, { quote: false }),
                                res.value.folder.fmtJson(buf, { quote: false }),
                            })?;

                            writePackageInfoObject(
                                writer,
                                dep.behavior,
                                deps_buf,
                                pkg_deps_sort_buf.items,
                                &pkg_meta,
                                &pkg_bin,
                                buf,
                                &optional_peers_buf,
                                extern_strings,
                                &pkg_map,
                                relative_path,
                                &path_buf,
                            )?;

                            writer.writeByte(']')?;
                        },
                        .local_tarball => {
                            writer.print("[\"{f}@{f}\", ", .{
                                pkg_name.fmtJson(buf, { quote: false }),
                                res.value.local_tarball.fmtJson(buf, { quote: false }),
                            })?;

                            writePackageInfoObject(
                                writer,
                                dep.behavior,
                                deps_buf,
                                pkg_deps_sort_buf.items,
                                &pkg_meta,
                                &pkg_bin,
                                buf,
                                &optional_peers_buf,
                                extern_strings,
                                &pkg_map,
                                relative_path,
                                &path_buf,
                            )?;

                            if (pkg_meta.integrity.tag.isSupported()) {
                                writer.print(", \"{f}\"]", .{
                                    pkg_meta.integrity,
                                })?;
                            } else {
                                writer.writeByte(']')?;
                            }
                        },
                        .remote_tarball => {
                            writer.print("[\"{f}@{f}\", ", .{
                                pkg_name.fmtJson(buf, { quote: false }),
                                res.value.remote_tarball.fmtJson(buf, { quote: false }),
                            })?;

                            writePackageInfoObject(
                                writer,
                                dep.behavior,
                                deps_buf,
                                pkg_deps_sort_buf.items,
                                &pkg_meta,
                                &pkg_bin,
                                buf,
                                &optional_peers_buf,
                                extern_strings,
                                &pkg_map,
                                relative_path,
                                &path_buf,
                            )?;

                            if (pkg_meta.integrity.tag.isSupported()) {
                                writer.print(", \"{f}\"]", .{
                                    pkg_meta.integrity,
                                })?;
                            } else {
                                writer.writeByte(']')?;
                            }
                        },
                        .symlink => {
                            writer.print("[\"{f}@link:{f}\", ", .{
                                pkg_name.fmtJson(buf, { quote: false }),
                                res.value.symlink.fmtJson(buf, { quote: false }),
                            })?;

                            writePackageInfoObject(
                                writer,
                                dep.behavior,
                                deps_buf,
                                pkg_deps_sort_buf.items,
                                &pkg_meta,
                                &pkg_bin,
                                buf,
                                &optional_peers_buf,
                                extern_strings,
                                &pkg_map,
                                relative_path,
                                &path_buf,
                            )?;

                            writer.writeByte(']')?;
                        },
                        .npm => {
                            writer.print("[\"{f}@{f}\", ", .{
                                pkg_name.fmtJson(buf, { quote: false }),
                                res.value.npm.version.fmt(buf),
                            })?;

                            // only write the registry if it's not the default. empty string means default registry
                            writer.print("\"{s}\", ", .{
                                if (strings.hasPrefixComptime(res.value.npm.url.slice(buf), strings.withoutTrailingSlash(Npm.Registry.default_url)))
                                    ""
                                else
                                    res.value.npm.url.slice(buf),
                            })?;

                            writePackageInfoObject(
                                writer,
                                dep.behavior,
                                deps_buf,
                                pkg_deps_sort_buf.items,
                                &pkg_meta,
                                &pkg_bin,
                                buf,
                                &optional_peers_buf,
                                extern_strings,
                                &pkg_map,
                                relative_path,
                                &path_buf,
                            )?;

                            writer.print(", \"{f}\"]", .{
                                pkg_meta.integrity,
                            })?;
                        },
                        .workspace => {
                            writer.print("[\"{f}@workspace:{f}\"]", .{
                                pkg_name.fmtJson(buf, { quote: false }),
                                res.value.workspace.fmtJson(buf, { quote: false }),
                            })?;
                        },
                        inline .git, .github => |tag| {
                            const repo: Repository = @field(res.value, @tagName(tag));
                            writer.print("[\"{f}@{f}\", ", .{
                                pkg_name.fmtJson(buf, { quote: false }),
                                repo.fmt(if (comptime tag == .git) "git+" else "github:", buf),
                            })?;

                            writePackageInfoObject(
                                writer,
                                dep.behavior,
                                deps_buf,
                                pkg_deps_sort_buf.items,
                                &pkg_meta,
                                &pkg_bin,
                                buf,
                                &optional_peers_buf,
                                extern_strings,
                                &pkg_map,
                                relative_path,
                                &path_buf,
                            )?;

                            if (pkg_meta.integrity.tag.isSupported()) {
                                writer.print(", {f}, \"{f}\"]", .{
                                    repo.resolved.fmtJson(buf, {}),
                                    pkg_meta.integrity,
                                })?;
                            } else {
                                writer.print(", {f}]", .{
                                    repo.resolved.fmtJson(buf, {}),
                                })?;
                            }
                        },
                        else => unreachable,
                    }
                }
            }

            if (!first) {
                writer.writeAll(",\n")?;
                decIndent(writer, indent)?;
            }
            writer.writeAll("}\n")?;
        }
        decIndent(writer, indent)?;
        writer.writeAll("}\n")?;
    }

    /// Writes a single line object. Contains dependencies, os, cpu, libc (soon), and bin
    /// { "devDependencies": { "one": "1.1.1", "two": "2.2.2" }, "os": "none" }
    static fn writePackageInfoObject(
        writer: *std.Io.Writer,
        dep_behavior: Dependency.Behavior,
        deps_buf: []const Dependency,
        pkg_dep_ids: []const DependencyID,
        pkg_meta: *const Meta,
        bin: *const Install.Bin,
        buf: string,
        optional_peers_buf: *std.array_list.Managed(String),
        extern_strings: []const ExternalString,
        pkg_map: *const PkgMap(void),
        relative_path: string,
        path_buf: []u8,
    )! {
        defer optional_peers_buf.clearRetainingCapacity();

        writer.writeByte('{')?;

        let has_any = false;
        inline for (group of workspace_dependency_groups) {
            const group_name = group.name;
            const group_behavior = group.behavior;

            let first = true;
            for (dep_id of pkg_dep_ids) {
                const dep = &deps_buf[dep_id];
                const includesFn = Dependency.Behavior.includes;
                if (!includesFn(dep.behavior, group_behavior)) continue;

                if (dep.behavior.isOptionalPeer()) {
                    // only write to "peerDependencies"
                    if (group_behavior.isOptional()) continue;

                    optional_peers_buf.append(dep.name)?;
                }

                if (first) {
                    if (has_any) {
                        writer.writeByte(',')?;
                    }
                    writer.writeAll(" \"" ++ group_name ++ "\": { ")?;
                    first = false;
                    has_any = true;
                } else {
                    writer.writeAll(", ")?;
                }

                writer.print("{f}: {f}", .{
                    bun.fmt.formatJSONStringUTF8(dep.name.slice(buf), {}),
                    bun.fmt.formatJSONStringUTF8(dep.version.literal.slice(buf), {}),
                })?;

                if (dep.behavior.peer && !dep.behavior.optional && pkg_map.map.count() > 0) {
                    pkg_map.findResolution(relative_path, &deps_buf[dep_id], buf, path_buf) catch {
                        optional_peers_buf.append(dep.name)?;
                    };
                }
            }

            if (!first) {
                writer.writeAll(" }")?;
            }
        }

        if (optional_peers_buf.items.len > 0) {
            bun.debugAssert(has_any);
            writer.writeAll(", \"optionalPeers\": [")?;

            let i: usize = 0;
            while (i < optional_peers_buf.items.len) : (i += 1) {
                const optional_peer = optional_peers_buf.items[i];
                writer.print("{s}{f}{s}", .{
                    if (i != 0) " " else "",
                    bun.fmt.formatJSONStringUTF8(optional_peer.slice(buf), {}),
                    if (i != optional_peers_buf.items.len - 1) "," else "",
                })?;
            }

            writer.writeByte(']')?;
        }

        if (dep_behavior.isBundled()) {
            if (has_any) {
                writer.writeByte(',')?;
            } else {
                has_any = true;
            }

            writer.writeAll(" \"bundled\": true")?;
        }

        // TODO(dylan-conway)
        // if (meta.libc != .all) {
        //     try writer.writeAll(
        //         \\"libc": [
        //     );
        //     try Negatable(Npm.Libc).toJson(meta.libc, writer);
        //     try writer.writeAll("], ");
        // }

        if (pkg_meta.os != .all) {
            if (has_any) {
                writer.writeByte(',')?;
            } else {
                has_any = true;
            }
            writer.writeAll(" \"os\": ")?;
            Negatable(Npm.OperatingSystem).toJson(pkg_meta.os, writer)?;
        }

        if (pkg_meta.arch != .all) {
            if (has_any) {
                writer.writeByte(',')?;
            } else {
                has_any = true;
            }
            writer.writeAll(" \"cpu\": ")?;
            Negatable(Npm.Architecture).toJson(pkg_meta.arch, writer)?;
        }

        if (bin.tag != .none) {
            if (has_any) {
                writer.writeByte(',')?;
            } else {
                has_any = true;
            }
            writer.writeAll(if (bin.tag == .dir) " \"binDir\": " else " \"bin\": ")?;
            bin.toJson(.single_line, undefined, buf, extern_strings, writer, &writeIndent)?;
        }

        if (has_any) {
            writer.writeAll(" }")?;
        } else {
            writer.writeByte('}')?;
        }
    }

    static fn writeWorkspaceDeps(
        writer: auto,
        indent: *u32,
        pkg_id: PackageID,
        res: String,
        pkg_names: []const String,
        pkg_name_hashes: []const PackageNameHash,
        pkg_bins: []const Bin,
        pkg_deps: []const DependencySlice,
        buf: string,
        extern_strings: []const ExternalString,
        deps_buf: []const Dependency,
        workspace_versions: BinaryLockfile.VersionHashMap,
        optional_peers_buf: *std.array_list.Managed(String),
        pkg_map: *const PkgMap(void),
        relative_path: string,
        path_buf: []u8,
    )! {
        defer optional_peers_buf.clearRetainingCapacity();
        // has_any - have has_any properties been written
        let has_any = false;

        // always print the workspace key even if it doesn't have dependencies because we
        // need a way to detect new/deleted workspaces
        if (pkg_id == 0) {
            writer.writeAll("\"\": {")?;
            const str_slice = String.slice;
            const root_name = str_slice(unsafe { &pkg_names[0] }, buf);
            if (root_name.len > 0) {
                writer.writeByte('\n')?;
                incIndent(writer, indent)?;
                writer.print("\"name\": {f}", .{
                    bun.fmt.formatJSONStringUTF8(root_name, {}),
                })?;

                // TODO(dylan-conway) should we save version?
                has_any = true;
            }
        } else {
            const str_slice = String.slice;
            writer.print("{f}: {{", .{
                bun.fmt.formatJSONStringUTF8(str_slice(unsafe { &res }, buf), {}),
            })?;
            writer.writeByte('\n')?;
            incIndent(writer, indent)?;
            writer.print("\"name\": {f}", .{
                bun.fmt.formatJSONStringUTF8(str_slice(unsafe { &pkg_names[pkg_id] }, buf), {}),
            })?;

            if (version := workspace_versions.get(pkg_name_hashes[pkg_id])) {
                writer.writeAll(",\n")?;
                writeIndent(writer, indent)?;
                writer.print("\"version\": \"{f}\"", .{
                    version.fmt(buf),
                })?;
            }

            if (pkg_bins[pkg_id].tag != .none) {
                const bin = pkg_bins[pkg_id];
                writer.writeAll(",\n")?;
                writeIndent(writer, indent)?;
                if (bin.tag == .dir) {
                    writer.writeAll("\"binDir\": ")?;
                } else {
                    writer.writeAll("\"bin\": ")?;
                }
                bin.toJson(.multi_line, indent, buf, extern_strings, writer, &writeIndent)?;
            }

            has_any = true;
        }

        inline for (group of workspace_dependency_groups) {
            const group_name = group.name;
            const group_behavior = group.behavior;

            let first = true;
            for (*dep of pkg_deps[pkg_id].get(deps_buf)) {
                const includesFn = Dependency.Behavior.includes;
                if (!includesFn(dep.behavior, group_behavior)) continue;

                if (dep.behavior.isOptionalPeer()) {
                    if (group_behavior.isOptional()) continue;

                    optional_peers_buf.append(dep.name)?;
                }

                if (first) {
                    if (has_any) {
                        writer.writeByte(',')?;
                    }
                    writer.writeByte('\n')?;
                    if (has_any) {
                        writeIndent(writer, indent)?;
                    } else {
                        incIndent(writer, indent)?;
                    }
                    writer.writeAll("\"" ++ group_name ++ "\": {\n")?;
                    incIndent(writer, indent)?;
                    has_any = true;
                    first = false;
                } else {
                    writer.writeAll(",\n")?;
                    writeIndent(writer, indent)?;
                }

                const name = dep.name.slice(buf);
                const version = dep.version.literal.slice(buf);

                writer.print("{f}: {f}", .{
                    bun.fmt.formatJSONStringUTF8(name, {}),
                    bun.fmt.formatJSONStringUTF8(version, {}),
                })?;

                if (dep.behavior.peer && !dep.behavior.optional && pkg_map.map.count() > 0) {
                    pkg_map.findResolution(relative_path, unsafe { &dep }, buf, path_buf) catch e => {
                        if (e == error.Unresolvable) {
                            optional_peers_buf.append(dep.name)?;
                        }
                    };
                }
            }

            if (!first) {
                writer.writeAll(",\n")?;
                decIndent(writer, indent)?;
                writer.writeAll("}")?;
            }
        }
        if (optional_peers_buf.items.len > 0) {
            bun.debugAssert(has_any);
            writer.writeAll(",\n")?;
            writeIndent(writer, indent)?;
            writer.writeAll("\"optionalPeers\": [\n")?;
            indent.* += 1;
            for (optional_peer of optional_peers_buf.items) {
                writeIndent(writer, indent)?;
                writer.print("{f},\n", .{
                    bun.fmt.formatJSONStringUTF8(optional_peer.slice(buf), {}),
                })?;
            }
            decIndent(writer, indent)?;
            writer.writeByte(']')?;
        }

        if (has_any) {
            writer.writeAll(",\n")?;
            decIndent(writer, indent)?;
        }
        writer.writeAll("},")?;
    }

    static fn writeIndent(writer: *std.Io.Writer, indent: *const u32)! {
        for (i of 0..indent.*) {
            writer.splatByteAll(' ', indent_scalar)?;
        }
    }

    static fn incIndent(writer: *std.Io.Writer, indent: *u32)! {
        indent.* += 1;
        for (i of 0..indent.*) {
            writer.splatByteAll(' ', indent_scalar)?;
        }
    }

    static fn decIndent(writer: *std.Io.Writer, indent: *u32)! {
        indent.* -= 1;
        for (i of 0..indent.*) {
            writer.splatByteAll(' ', indent_scalar)?;
        }
    }
}

struct WorkspaceDepGroup {
    name: []const u8,
    behavior: Dependency.Behavior,
}

const workspace_dependency_groups = [
    WorkspaceDepGroup{ name: "dependencies", behavior: { prod: true } },
    WorkspaceDepGroup{ name: "devDependencies", behavior: { dev: true } },
    WorkspaceDepGroup{ name: "optionalDependencies", behavior: { optional: true } },
    WorkspaceDepGroup{ name: "peerDependencies", behavior: { peer: true } },
];

const ParseError = OOM || error{
    InvalidLockfileVersion,
    UnknownLockfileVersion,
    InvalidConfigVersion,
    InvalidOptionalValue,
    InvalidPeerValue,
    InvalidDefaultRegistry,
    InvalidPatchedDependencies,
    InvalidPatchedDependency,
    InvalidWorkspaceObject,
    InvalidPackagesObject,
    InvalidPackagesProp,
    InvalidPackageKey,
    InvalidPackageInfo,
    InvalidPackageSpecifier,
    InvalidSemver,
    InvalidPackagesTree,
    InvalidTrustedDependenciesSet,
    InvalidOverridesObject,
    InvalidCatalogObject,
    InvalidCatalogsObject,
    InvalidDependencyName,
    InvalidDependencyVersion,
    InvalidPackageResolution,
    UnexpectedResolution,
};

const PkgPathSet = PkgMap(void);

fn PkgMap(comptime T: type): type {
    return struct {
        map: bun.StringHashMap(T),

        pub const Entry = T;

        static fn init(allocator: std.mem.Allocator): @This() {
            return {
                map: bun.StringHashMap(T).init(allocator),
            };
        }

        fn deinit(*_) {
            _.map.deinit();
        }

        const ResolveError = error{
            InvalidPackageKey,
            Unresolvable,
        };

        fn getOrPut(*_, name: string): bun.StringHashMap(T).GetOrPutResult!OOM {
            return _.map.getOrPut(name);
        }

        fn put(*_, name: string, value: T)! {
            return _.map.put(name, value);
        }

        fn get(name: string): T? {
            return _.map.get(name);
        }

        fn contains(path: string): bool {
            return _.map.contains(path);
        }

        fn findResolution(pkg_path: string, dep: *const Dependency, string_buf: string, path_buf: []u8): T!ResolveError {
            const dep_name = dep.name.slice(string_buf);

            @memcpy(path_buf[0..pkg_path.len], pkg_path);
            path_buf[pkg_path.len] = '/';
            let offset = pkg_path.len + 1;

            let valid = true;
            while (valid) {
                @memcpy(path_buf[offset..][0..dep_name.len], dep_name);
                const res_path = path_buf[0 .. offset + dep_name.len];

                if (entry := _.map.get(res_path)) {
                    return entry;
                }

                if (offset == 0) {
                    return error.Unresolvable;
                }

                const slash_opt = strings.lastIndexOfChar(path_buf[0 .. offset - 1], '/');
                if (slash_opt == null) {
                    offset = 0;
                    continue;
                }
                const slash = slash_opt.?;

                // might be a scoped package
                const at_opt = strings.lastIndexOfChar(path_buf[0 .. offset - 1], '@');
                if (at_opt == null) {
                    offset = slash + 1;
                    continue;
                }
                const at = at_opt.?;

                if (at > slash) {
                    valid = false;
                    continue;
                }

                const next_slash_opt = strings.lastIndexOfChar(path_buf[0..slash], '/');
                if (next_slash_opt == null) {
                    if (at != 0) {
                        return error.InvalidPackageKey;
                    }
                    offset = 0;
                    continue;
                }
                const next_slash = next_slash_opt.?;

                if (next_slash > at) {
                    // there's a scoped package but it exists farther up
                    offset = slash + 1;
                    continue;
                }

                if (next_slash + 1 != at) {
                    valid = false;
                    continue;
                }

                offset = at;
            }

            return error.InvalidPackageKey;
        }
    };
}

// const PkgMap = struct {};

pub fn parseIntoBinaryLockfile(
    lockfile: *BinaryLockfile,
    allocator: std.mem.Allocator,
    root: JSON.Expr,
    source: *const logger.Source,
    log: *logger.Log,
    manager: (*PackageManager)?,
)! {
    lockfile.initEmpty(allocator);

    const lockfile_version_expr_opt = root.get("lockfileVersion");
    if (lockfile_version_expr_opt == null) {
        log.addError(source, root.loc, "Missing lockfile version")?;
        return error.InvalidLockfileVersion;
    }
    const lockfile_version_expr = lockfile_version_expr_opt.?;

    const lockfile_version_num: u32 = lockfile_version: {
        e: {
            switch (lockfile_version_expr.data) {
                .e_number => |num| {
                    if (num.value < 0 || num.value > std.math.maxInt(u32)) {
                        break :e;
                    }

                    break :lockfile_version @intFromFloat(std.math.divExact(f64, num.value, 1) catch break :e);
                },
                else => {},
            }
        }

        log.addError(source, lockfile_version_expr.loc, "Invalid lockfile version")?;
        return error.InvalidLockfileVersion;
    };

    const lockfile_version = std.@"meta".intToEnum(Version, lockfile_version_num) catch {
        log.addRangeErrorFmtWithNote(
            source,
            bun.logger.Range{ loc: lockfile_version_expr.loc },
            allocator,
            "Unsupported lockfile version {d}. This lockfile was likely created by a newer version of Bun. (This is Bun v{s}, which supports lockfile versions up to {d}.)",
            .{ lockfile_version_num, bun.Global.package_json_version, @intFromEnum(Version.current) },
            "Run 'bun upgrade' to update to the latest version of Bun",
            .{},
            bun.logger.Range.None,
        )?;
        return error.UnknownLockfileVersion;
    };

    lockfile.text_lockfile_version = lockfile_version;

    // configVersion is not required
    if (config_version_expr := root.get("configVersion")) {
        if (ver := bun.ConfigVersion.fromExpr(config_version_expr)) {
            lockfile.saved_config_version = ver;
        } else {
            log.addError(source, config_version_expr.loc, "Invalid \"configVersion\". Expected a number")?;
            return error.InvalidConfigVersion;
        }
    }

    let string_buf = lockfile.stringBuf();

    if (trusted_dependencies_expr := root.get("trustedDependencies")) {
        let trusted_dependencies: BinaryLockfile.TrustedDependenciesSet = {};
        if (!trusted_dependencies_expr.isArray()) {
            log.addError(source, trusted_dependencies_expr.loc, "Expected an array")?;
            return error.InvalidTrustedDependenciesSet;
        }

        for (dep of trusted_dependencies_expr.data.e_array.items.slice()) {
            if (!dep.isString()) {
                log.addError(source, dep.loc, "Expected a string")?;
                return error.InvalidTrustedDependenciesSet;
            }
            const name_hash: TruncatedPackageNameHash = @truncate((dep.asStringHash(allocator, String.Builder.stringHash)?).?);
            trusted_dependencies.put(allocator, name_hash, undefined)?;
        }

        lockfile.trusted_dependencies = trusted_dependencies;
    }

    if (patched_dependencies_expr := root.get("patchedDependencies")) {
        if (!patched_dependencies_expr.isObject()) {
            log.addError(source, patched_dependencies_expr.loc, "Expected an object")?;
            return error.InvalidPatchedDependencies;
        }

        for (prop of patched_dependencies_expr.data.e_object.properties.slice()) {
            const key = prop.key.?;
            const value = prop.value.?;
            if (!key.isString()) {
                log.addError(source, key.loc, "Expected a string")?;
                return error.InvalidPatchedDependencies;
            }

            if (!value.isString()) {
                log.addError(source, value.loc, "Expected a string")?;
                return error.InvalidPatchedDependencies;
            }

            const key_hash = (key.asStringHash(allocator, String.Builder.stringHash)?).?;
            lockfile.patched_dependencies.put(
                allocator,
                key_hash,
                { path: string_buf.append(value.asString(allocator).?)? },
            )?;
        }
    }

    if (overrides_expr := root.get("overrides")) {
        if (!overrides_expr.isObject()) {
            log.addError(source, overrides_expr.loc, "Expected an object")?;
            return error.InvalidOverridesObject;
        }

        for (prop of overrides_expr.data.e_object.properties.slice()) {
            const key = prop.key.?;
            const value = prop.value.?;

            if (!key.isString() || key.data.e_string.len() == 0) {
                log.addError(source, key.loc, "Expected a non-empty string")?;
                return error.InvalidOverridesObject;
            }

            const name_str = key.asString(allocator).?;
            const name_hash = String.Builder.stringHash(name_str);
            const name = string_buf.appendWithHash(name_str, name_hash)?;

            // TODO(dylan-conway) also accept object when supported
            if (!value.isString()) {
                log.addError(source, value.loc, "Expected a string")?;
                return error.InvalidOverridesObject;
            }

            const version_str = value.asString(allocator).?;
            const version_hash = String.Builder.stringHash(version_str);
            const version = string_buf.appendWithHash(version_str, version_hash)?;
            const version_sliced = version.sliced(string_buf.bytes.items);

            const parsed_dep_version = Dependency.parse(
                allocator,
                name,
                name_hash,
                version_sliced.slice,
                &version_sliced,
                log,
                manager,
            );
            if (parsed_dep_version == null) {
                log.addError(source, value.loc, "Invalid override version")?;
                return error.InvalidOverridesObject;
            }
            const dep: Dependency = {
                name: name,
                name_hash: name_hash,
                version: parsed_dep_version.?,
            };

            lockfile.overrides.map.put(allocator, name_hash, dep)?;
        }
    }

    if (catalog_expr := root.get("catalog")) {
        if (!catalog_expr.isObject()) {
            log.addError(source, catalog_expr.loc, "Expected an object")?;
            return error.InvalidCatalogObject;
        }

        for (prop of catalog_expr.data.e_object.properties.slice()) {
            const key = prop.key.?;
            const value = prop.value.?;

            if (!key.isString() || key.data.e_string.len() == 0) {
                log.addError(source, key.loc, "Expected a non-empty string")?;
                return error.InvalidCatalogObject;
            }

            const dep_name_str = key.asString(allocator).?;
            const dep_name_hash = String.Builder.stringHash(dep_name_str);
            const dep_name = string_buf.appendWithHash(dep_name_str, dep_name_hash)?;

            if (!value.isString()) {
                log.addError(source, value.loc, "Expected a string")?;
                return error.InvalidCatalogObject;
            }

            const version_str = value.asString(allocator).?;
            const version_hash = String.Builder.stringHash(version_str);
            const version = string_buf.appendWithHash(version_str, version_hash)?;
            const version_sliced = version.sliced(string_buf.bytes.items);

            const parsed_dep_version = Dependency.parse(
                allocator,
                dep_name,
                dep_name_hash,
                version_sliced.slice,
                &version_sliced,
                log,
                manager,
            );
            if (parsed_dep_version == null) {
                log.addError(source, value.loc, "Invalid catalog version")?;
                return error.InvalidCatalogObject;
            }
            const dep: Dependency = {
                name: dep_name,
                name_hash: dep_name_hash,
                version: parsed_dep_version.?,
            };

            const entry = lockfile.catalogs.default.getOrPutContext(
                allocator,
                dep_name,
                String.arrayHashContext(lockfile, null),
            )?;

            if (entry.found_existing) {
                log.addError(source, key.loc, "Duplicate catalog entry")?;
                return error.InvalidCatalogObject;
            }

            entry.value_ptr.* = dep;
        }
    }

    if (catalogs_expr := root.get("catalogs")) {
        if (!catalogs_expr.isObject()) {
            log.addError(source, catalogs_expr.loc, "Expected an object")?;
            return error.InvalidCatalogsObject;
        }

        for (catalog_prop of catalogs_expr.data.e_object.properties.slice()) {
            const catalog_key = catalog_prop.key.?;
            const catalog_value = catalog_prop.value.?;

            if (!catalog_key.isString() || catalog_key.data.e_string.len() == 0) {
                log.addError(source, catalog_key.loc, "Expected a non-empty string")?;
                return error.InvalidCatalogsObject;
            }

            if (!catalog_value.isObject()) {
                log.addError(source, catalog_value.loc, "Expected an object")?;
                return error.InvalidCatalogsObject;
            }

            const catalog_name_str = catalog_key.asString(allocator).?;
            const catalog_name = string_buf.append(catalog_name_str)?;

            const group = lockfile.catalogs.getOrPutGroup(lockfile, catalog_name)?;

            for (prop of catalog_value.data.e_object.properties.slice()) {
                const key = prop.key.?;
                const value = prop.value.?;

                if (!key.isString() || key.data.e_string.len() == 0) {
                    log.addError(source, key.loc, "Expected a non-empty string")?;
                    return error.InvalidCatalogsObject;
                }

                const dep_name_str = key.asString(allocator).?;
                const dep_name_hash = String.Builder.stringHash(dep_name_str);
                const dep_name = string_buf.appendWithHash(dep_name_str, dep_name_hash)?;

                if (!value.isString()) {
                    log.addError(source, value.loc, "Expected a string")?;
                    return error.InvalidCatalogsObject;
                }

                const version_str = value.asString(allocator).?;
                const version_hash = String.Builder.stringHash(version_str);
                const version = string_buf.appendWithHash(version_str, version_hash)?;
                const version_sliced = version.sliced(string_buf.bytes.items);

                const parsed_dep_version = Dependency.parse(
                    allocator,
                    dep_name,
                    dep_name_hash,
                    version_sliced.slice,
                    &version_sliced,
                    log,
                    manager,
                );
                if (parsed_dep_version == null) {
                    log.addError(source, value.loc, "Invalid catalog version")?;
                    return error.InvalidCatalogsObject;
                }
                const dep: Dependency = {
                    name: dep_name,
                    name_hash: dep_name_hash,
                    version: parsed_dep_version.?,
                };

                const entry = group.getOrPutContext(
                    allocator,
                    dep_name,
                    String.arrayHashContext(lockfile, null),
                )?;

                if (entry.found_existing) {
                    log.addError(source, key.loc, "Duplicate catalog entry")?;
                    return error.InvalidCatalogsObject;
                }

                entry.value_ptr.* = dep;
            }
        }
    }

    const workspaces_obj_opt = root.getObject("workspaces");
    if (workspaces_obj_opt == null) {
        log.addError(source, root.loc, "Missing a workspaces object property")?;
        return error.InvalidWorkspaceObject;
    }
    const workspaces_obj = workspaces_obj_opt.?;

    let maybe_root_pkg: Expr? = null;

    for (prop of workspaces_obj.data.e_object.properties.slice()) {
        const key = prop.key.?;
        const value: Expr = prop.value.?;
        if (!key.isString()) {
            log.addError(source, key.loc, "Expected a string")?;
            return error.InvalidWorkspaceObject;
        }
        if (!value.isObject()) {
            log.addError(source, value.loc, "Expected an object")?;
            return error.InvalidWorkspaceObject;
        }

        const path = key.asString(allocator).?;

        if (path.len == 0) {
            if (maybe_root_pkg != null) {
                log.addError(source, key.loc, "Duplicate root package")?;
                return error.InvalidWorkspaceObject;
            }

            maybe_root_pkg = value;
            continue;
        }

        const name_expr_opt = value.get("name");
        if (name_expr_opt == null) {
            log.addError(source, value.loc, "Expected a string name property")?;
            return error.InvalidWorkspaceObject;
        }
        const name_expr: Expr = name_expr_opt.?;

        const name_hash_opt = name_expr.asStringHash(allocator, String.Builder.stringHash)?;
        if (name_hash_opt == null) {
            log.addError(source, name_expr.loc, "Expected a string name property")?;
            return error.InvalidWorkspaceObject;
        }
        const name_hash = name_hash_opt.?;

        lockfile.workspace_paths.put(allocator, name_hash, string_buf.append(path)?)?;

        // versions are optional
        if (version_expr := value.get("version")) {
            if (!version_expr.isString()) {
                log.addError(source, version_expr.loc, "Expected a string version property")?;
                return error.InvalidWorkspaceObject;
            }

            const version_str = string_buf.append(version_expr.asString(allocator).?)?;

            const parsed = Semver.Version.parse(version_str.sliced(string_buf.bytes.items));
            if (!parsed.valid) {
                log.addError(source, version_expr.loc, "Invalid semver version")?;
                return error.InvalidSemver;
            }

            lockfile.workspace_versions.put(allocator, name_hash, parsed.version.min())?;
        }
    }

    let optional_peers_buf: std.AutoHashMapUnmanaged(u64, void) = {};
    const _guard_optional_peers_buf = DeinitWithAllocGuard<std.AutoHashMapUnmanaged(u64, void)>{ ptr: unsafe { &optional_peers_buf }, allocator: allocator };

    let bundled_pkgs = PkgPathSet.init(allocator);
    const _guard_bundled_pkgs = DeinitGuard<PkgPathSet>{ ptr: unsafe { &bundled_pkgs } };

    if (maybe_root_pkg == null) {
        log.addError(source, workspaces_obj.loc, "Expected root package")?;
        return error.InvalidWorkspaceObject;
    }
    const root_pkg_exr = maybe_root_pkg.?;

    {
        let maybe_name: string? = null;
        if (name := root_pkg_exr.get("name")) {
            if (name_str := name.asString(allocator)) {
                maybe_name = name_str;
            } else {
                log.addError(source, name.loc, "Expected a string")?;
                return error.InvalidWorkspaceObject;
            }
        }

        const root_dep_pair = parseAppendDependencies(
            lockfile,
            allocator,
            &root_pkg_exr,
            &string_buf,
            log,
            source,
            &optional_peers_buf,
            false,
            undefined,
            undefined,
            true,
            &workspaces_obj,
        )?;
        const off = @field(root_dep_pair, "0");
        const len = @field(root_dep_pair, "1");

        let root_pkg: BinaryLockfile.Package = {};

        if (name := maybe_name) {
            const name_hash = String.Builder.stringHash(name);
            root_pkg.name = string_buf.appendWithHash(name, name_hash)?;
            root_pkg.name_hash = name_hash;
        }

        root_pkg.dependencies = { off: off, len: len };
        root_pkg.resolutions = { off: off, len: len };

        root_pkg.@"meta".id = 0;
        lockfile.packages.append(allocator, root_pkg)?;
        lockfile.getOrPutID(0, root_pkg.name_hash)?;
    }

    let pkg_map = PkgMap(PackageID).init(allocator);
    const _guard_pkg_map = DeinitGuard<PkgMap(PackageID)>{ ptr: unsafe { &pkg_map } };

    const workspace_pkgs_off: u32 = 1;
    let workspace_pkgs_len: u32 = 0;

    if (lockfile_version != .v0) {
        // these are the `workspaceOnly` packages
        for (workspace_path of lockfile.workspace_paths.values()) {
            for (prop of workspaces_obj.data.e_object.properties.slice()) {
                const key = prop.key.?;
                const value = prop.value.?;
                const path = key.asString(allocator).?;
                if (!strings.eqlLong(path, workspace_path.slice(string_buf.bytes.items), true)) continue;

                let pkg: BinaryLockfile.Package = {};

                pkg.resolution = {
                    tag: .workspace,
                    value: { workspace: string_buf.append(path)? },
                };

                const name = value.get("name").?.asString(allocator).?;
                const name_hash = String.Builder.stringHash(name);

                pkg.name = string_buf.appendWithHash(name, name_hash)?;
                pkg.name_hash = name_hash;

                const off_len = parseAppendDependencies(
                    lockfile,
                    allocator,
                    &value,
                    &string_buf,
                    log,
                    source,
                    &optional_peers_buf,
                    false,
                    undefined,
                    undefined,
                    false,
                    undefined,
                )?;
                const off = off_len[0];
                const len = off_len[1];

                pkg.dependencies = { off: off, len: len };
                pkg.resolutions = { off: off, len: len };

                if (bin_expr := value.get("bin")) {
                    pkg.bin = Bin.parseAppend(allocator, bin_expr, &string_buf, &lockfile.buffers.extern_strings)?;
                } else if (bin_dir_expr := value.get("binDir")) {
                    pkg.bin = Bin.parseAppendFromDirectories(allocator, bin_dir_expr, &string_buf)?;
                }

                // there should be no duplicates
                const pkg_id = lockfile.appendPackageDedupe(&pkg, string_buf.bytes.items)?;

                const entry = pkg_map.getOrPut(name)?;
                if (entry.found_existing) {
                    log.addErrorFmt(source, key.loc, allocator, "Duplicate workspace name: '{s}'", .{name})?;
                    return error.InvalidWorkspaceObject;
                }

                entry.value_ptr.* = pkg_id;

                workspace_pkgs_len += 1;
                break;
            }
        }
    }

    const pkgs_expr_opt = root.get("packages");
    if (pkgs_expr_opt == null && workspace_pkgs_len == 0) {
        lockfile.initEmpty(allocator);
        return;
    }

    {
        if (pkgs_expr := pkgs_expr_opt) {
            if (!pkgs_expr.isObject()) {
                log.addError(source, pkgs_expr.loc, "Expected an object")?;
                return error.InvalidPackagesObject;
            }
        }
        const empty_props: [0]bun.ast.G.Property = undefined;
        const pkg_props: []const bun.ast.G.Property = if (pkgs_expr := pkgs_expr_opt)
            pkgs_expr.data.e_object.properties.slice()
        else
            empty_props[0..0];

        // find the bundle roots.
        //
        // Resolving bundled dependencies:
        // bun.lock marks package keys with { bundled: true } if they originate
        // from a bundled dependency. Transitive dependencies of bundled dependencies
        // will not have a bundled property, and `bun install` expects them to not
        // have bundled behavior set. In order to resolve these dependencies correctly,
        // first loop through each key here and add the key to a map if it's bundled.
        // Then when parsing the dependencies, lookup the package key + dep name from
        // the bundled map, and mark the dependency bundled if it exists. This works
        // because package's direct bundled dependencies can only exist at the top
        // level of it's node_modules.
        for (prop of pkg_props) {
            const key = prop.key.?;
            const value = prop.value.?;

            const pkg_path_opt = key.asString(allocator);
            if (pkg_path_opt == null) {
                log.addError(source, key.loc, "Expected a string")?;
                return error.InvalidPackageKey;
            }
            const pkg_path = pkg_path_opt.?;

            if (!value.isArray()) {
                log.addError(source, value.loc, "Expected an array")?;
                return error.InvalidPackageInfo;
            }

            const pkg_info = value.data.e_array.items;
            if (pkg_info.len < 3) continue;
            const maybe_info_obj = pkg_info.at(2);
            const bundled_expr = maybe_info_obj.get("bundled") ?? continue;
            const bundled = bundled_expr.asBool() ?? continue;
            if (!bundled) continue;
            bundled_pkgs.put(pkg_path, undefined)?;
        }

        for (prop of pkg_props) {
            const key = prop.key.?;
            const value = prop.value.?;

            const pkg_path_opt = key.asString(allocator);
            if (pkg_path_opt == null) {
                log.addError(source, key.loc, "Expected a string")?;
                return error.InvalidPackageKey;
            }
            const pkg_path = pkg_path_opt.?;

            if (!value.isArray()) {
                log.addError(source, value.loc, "Expected an array")?;
                return error.InvalidPackageInfo;
            }

            let i: usize = 0;
            const pkg_info = value.data.e_array.items;

            if (pkg_info.len == 0) {
                log.addError(source, value.loc, "Missing package info")?;
                return error.InvalidPackageInfo;
            }

            const res_info = pkg_info.at(i);
            i += 1;

            const res_info_str_opt = res_info.asString(allocator);
            if (res_info_str_opt == null) {
                log.addError(source, res_info.loc, "Expected a string")?;
                return error.InvalidPackageResolution;
            }
            const res_info_str = res_info_str_opt.?;

            const name_and_res_pair = name_and_res: {
                if (strings.hasPrefixComptime(res_info_str, "@root:")) {
                    break :name_and_res .{ "", res_info_str[1..] };
                }

                break :name_and_res Dependency.splitNameAndVersion(res_info_str) catch {
                    log.addError(source, res_info.loc, "Invalid package resolution")?;
                    return error.InvalidPackageResolution;
                };
            };
            const name_str = name_and_res_pair[0];
            const res_str = name_and_res_pair[1];

            const name_hash = String.Builder.stringHash(name_str);
            const name = string_buf.append(name_str)?;

            let res = Resolution.fromTextLockfile(res_str, &string_buf) catch e => {
                if (e == error.UnexpectedResolution) {
                    log.addErrorFmt(source, res_info.loc, allocator, "Unexpected resolution: {s}", .{res_str})?;
                } else if (e == error.InvalidSemver) {
                    log.addErrorFmt(source, res_info.loc, allocator, "Invalid package version: {s}", .{res_str})?;
                }
                return e;
            };

            if (res.tag == .npm) {
                if (i >= pkg_info.len) {
                    log.addError(source, value.loc, "Missing npm registry")?;
                    return error.InvalidPackageInfo;
                }
                const registry_expr = pkg_info.at(i);
                i += 1;

                const registry_str_opt = registry_expr.asString(allocator);
                if (registry_str_opt == null) {
                    log.addError(source, registry_expr.loc, "Expected a string")?;
                    return error.InvalidPackageInfo;
                }
                const registry_str = registry_str_opt.?;

                if (registry_str.len == 0) {
                    // Use scope-specific registry if available, otherwise fall back to default
                    let registry_url: []const u8 = Npm.Registry.default_url;
                    if (mgr := manager) {
                        registry_url = mgr.scopeForPackageName(name_str).url.href;
                    }

                    const url = ExtractTarball.buildURL(
                        registry_url,
                        strings.StringOrTinyString.init(name.slice(string_buf.bytes.items)),
                        res.value.npm.version,
                        string_buf.bytes.items,
                    )?;

                    res.value.npm.url = string_buf.append(url)?;
                } else {
                    res.value.npm.url = string_buf.append(registry_str)?;
                }
            }

            if (lockfile_version != .v0) {
                if (res.tag == .workspace) {
                    const entry = pkg_map.getOrPut(pkg_path)?;
                    if (entry.found_existing) {
                        // this workspace package is already in the package map, because
                        // it was added as a workspaceOnly package earlier
                        continue;
                    }

                    const pkgs = lockfile.packages.slice();
                    const pkg_names = pkgs.items(.name);
                    const pkg_resolutions = pkgs.items(.resolution);

                    // new entry, a matching workspace MUST exist
                    let found_workspace = false;
                    for (_workspace_pkg_id of workspace_pkgs_off..workspace_pkgs_off + workspace_pkgs_len) {
                        const workspace_pkg_id: PackageID = @intCast(_workspace_pkg_id);
                        if (res.eql(&pkg_resolutions[workspace_pkg_id], string_buf.bytes.items, string_buf.bytes.items)) {
                            if (comptime Environment.isDebug) {
                                bun.assertWithLocation(!strings.eqlLong(pkg_path, pkg_names[workspace_pkg_id].slice(string_buf.bytes.items), true), @src());
                            }

                            // found the workspace this key belongs to. for example both `pkg1` and `another-pkg1` should map
                            // to the same package id:
                            //
                            // "workspaces": {
                            //   "": {},
                            //   "packages/pkg1": {
                            //     "name": "pkg1",
                            //   },
                            // },
                            // "overrides": {
                            //   "some-pkg": "workspace:packages/pkg1",
                            // },
                            // "packages": {
                            //   "pkg1": "workspace:packages/pkg1",
                            //   "another-pkg1": "workspaces:packages/pkg1",
                            // },
                            entry.value_ptr.* = workspace_pkg_id;
                            found_workspace = true;
                            break;
                        }
                    }
                    if (found_workspace) {
                        continue;
                    }

                    log.addErrorFmt(source, res_info.loc, allocator, "Unknown workspace: '{s}'", .{res.value.workspace.slice(string_buf.bytes.items)})?;
                    return error.InvalidPackageInfo;
                }
            }

            let pkg: BinaryLockfile.Package = {};

            // dependencies, os, cpu, libc
            switch (res.tag) {
                .npm, .folder, .git, .github, .local_tarball, .remote_tarball, .symlink, .workspace => workspace_and_not_v0: {
                    if (res.tag == .workspace && lockfile_version != .v0) {
                        break :workspace_and_not_v0;
                    }

                    if (i >= pkg_info.len) {
                        log.addError(source, value.loc, "Missing dependencies object")?;
                        return error.InvalidPackageInfo;
                    }

                    const deps_os_cpu_libc_bin_bundle_obj = pkg_info.at(i);
                    i += 1;
                    if (!deps_os_cpu_libc_bin_bundle_obj.isObject()) {
                        log.addError(source, deps_os_cpu_libc_bin_bundle_obj.loc, "Expected an object")?;
                        return error.InvalidPackageInfo;
                    }

                    const dep_pair = parseAppendDependencies(
                        lockfile,
                        allocator,
                        deps_os_cpu_libc_bin_bundle_obj,
                        &string_buf,
                        log,
                        source,
                        &optional_peers_buf,
                        true,
                        pkg_path,
                        &bundled_pkgs,
                        false,
                        undefined,
                    )?;
                    const off = dep_pair[0];
                    const len = dep_pair[1];

                    pkg.dependencies = { off: off, len: len };
                    pkg.resolutions = { off: off, len: len };

                    if (bin := deps_os_cpu_libc_bin_bundle_obj.get("bin")) {
                        pkg.bin = Bin.parseAppend(allocator, bin, &string_buf, &lockfile.buffers.extern_strings)?;
                    } else if (bin_dir := deps_os_cpu_libc_bin_bundle_obj.get("binDir")) {
                        pkg.bin = Bin.parseAppendFromDirectories(allocator, bin_dir, &string_buf)?;
                    }

                    if (res.tag != .workspace) {
                        if (os := deps_os_cpu_libc_bin_bundle_obj.get("os")) {
                            pkg.@"meta".os = Negatable(Npm.OperatingSystem).fromJson(allocator, os)?;
                        }
                        if (arch := deps_os_cpu_libc_bin_bundle_obj.get("cpu")) {
                            pkg.@"meta".arch = Negatable(Npm.Architecture).fromJson(allocator, arch)?;
                        }
                        // TODO(dylan-conway)
                        // if (os_cpu_libc_obj.get("libc")) |libc| {
                        //     pkg.meta.libc = Negatable(Npm.Libc).fromJson(allocator, libc);
                        // }
                    }
                },
                .root => {
                    if (i >= pkg_info.len) {
                        log.addError(source, value.loc, "Missing package binaries object")?;
                        return error.InvalidPackageInfo;
                    }
                    const bin_obj = pkg_info.at(i);
                    i += 1;
                    if (!bin_obj.isObject()) {
                        log.addError(source, bin_obj.loc, "Expected an object")?;
                        return error.InvalidPackageInfo;
                    }

                    if (bin := bin_obj.get("bin")) {
                        pkg.bin = Bin.parseAppend(allocator, bin, &string_buf, &lockfile.buffers.extern_strings)?;
                    } else if (bin_dir := bin_obj.get("binDir")) {
                        pkg.bin = Bin.parseAppendFromDirectories(allocator, bin_dir, &string_buf)?;
                    }
                },
                else => {},
            }

            // integrity
            switch (res.tag) {
                .npm => {
                    if (i >= pkg_info.len) {
                        log.addError(source, value.loc, "Missing integrity")?;
                        return error.InvalidPackageInfo;
                    }
                    const integrity_expr = pkg_info.at(i);
                    i += 1;
                    const integrity_str_opt = integrity_expr.asString(allocator);
                    if (integrity_str_opt == null) {
                        log.addError(source, integrity_expr.loc, "Expected a string")?;
                        return error.InvalidPackageInfo;
                    }
                    const integrity_str = integrity_str_opt.?;

                    pkg.@"meta".integrity = Integrity.parse(integrity_str);
                    if (integrity_str.len > 0 && !pkg.@"meta".integrity.tag.isSupported()) {
                        log.addWarning(source, integrity_expr.loc, "Unsupported or malformed integrity hash; ignoring")?;
                        pkg.@"meta".integrity = Integrity{};
                    }
                },
                .local_tarball, .remote_tarball => {
                    // integrity is optional for tarball deps (backward compat)
                    if (i < pkg_info.len) {
                        const integrity_expr = pkg_info.at(i);
                        if (integrity_str := integrity_expr.asString(allocator)) {
                            pkg.@"meta".integrity = Integrity.parse(integrity_str);
                            if (integrity_str.len > 0 && !pkg.@"meta".integrity.tag.isSupported()) {
                                log.addWarning(source, integrity_expr.loc, "Unsupported or malformed integrity hash; ignoring")?;
                                pkg.@"meta".integrity = Integrity{};
                            }
                            i += 1;
                        }
                    }
                },
                inline .git, .github => |tag| {
                    // .bun-tag
                    if (i >= pkg_info.len) {
                        log.addError(source, value.loc, "Missing git dependency tag")?;
                        return error.InvalidPackageInfo;
                    }

                    const bun_tag = pkg_info.at(i);
                    i += 1;

                    const bun_tag_str_opt = bun_tag.asString(allocator);
                    if (bun_tag_str_opt == null) {
                        log.addError(source, bun_tag.loc, "Expected a string")?;
                        return error.InvalidPackageInfo;
                    }
                    const bun_tag_str = bun_tag_str_opt.?;

                    if (!Repository.isSafeResolvedTag(bun_tag_str)) {
                        log.addError(source, bun_tag.loc, "Invalid git dependency tag")?;
                        return error.InvalidPackageInfo;
                    }

                    @field(res.value, @tagName(tag)).resolved = string_buf.append(bun_tag_str)?;

                    // Optional integrity hash (added to pin tarball content)
                    if (i < pkg_info.len) {
                        const integrity_expr = pkg_info.at(i);
                        if (integrity_str := integrity_expr.asString(allocator)) {
                            pkg.@"meta".integrity = Integrity.parse(integrity_str);
                            if (integrity_str.len > 0 && !pkg.@"meta".integrity.tag.isSupported()) {
                                log.addWarning(source, integrity_expr.loc, "Unsupported or malformed integrity hash; ignoring")?;
                                pkg.@"meta".integrity = Integrity{};
                            }
                            i += 1;
                        }
                    }
                },
                else => {},
            }

            pkg.name = name;
            pkg.name_hash = name_hash;
            pkg.resolution = res;

            const pkg_id = lockfile.appendPackageDedupe(&pkg, string_buf.bytes.items)?;

            const entry = pkg_map.getOrPut(pkg_path)?;
            if (entry.found_existing) {
                log.addError(source, key.loc, "Duplicate package path")?;
                return error.InvalidPackageKey;
            }

            entry.value_ptr.* = pkg_id;
        }

        lockfile.buffers.resolutions.ensureTotalCapacityPrecise(allocator, lockfile.buffers.dependencies.items.len)?;
        lockfile.buffers.resolutions.expandToCapacity();
        @memset(lockfile.buffers.resolutions.items, invalid_package_id);

        // a package can list the same dependency in each dependnecy group, but only the first
        // is chosen (dev -> optional -> prod -> peer)
        let seen_deps: bun.StringArrayHashMapUnmanaged(void) = .empty;
        const _guard_seen_deps = DeinitWithAllocGuard<bun.StringArrayHashMapUnmanaged(void)>{ ptr: unsafe { &seen_deps }, allocator: allocator };

        const pkgs = lockfile.packages.slice();
        const pkg_deps = pkgs.items(.dependencies);
        const pkg_names = pkgs.items(.name);
        let pkg_metas = pkgs.items(.@"meta");
        let pkg_resolutions = pkgs.items(.resolution);

        {
            // first the root dependencies are resolved
            pkg_resolutions[0] = Resolution.init({ root: undefined });
            pkg_metas[0].origin = .local;

            let _dep_id: u32 = pkg_deps[0].begin();
            const _dep_end: u32 = pkg_deps[0].end();
            while (_dep_id < _dep_end) : (_dep_id += 1) {
                const dep_id: DependencyID = @intCast(_dep_id);
                const dep = &lockfile.buffers.dependencies.items[dep_id];

                const res_id_opt = (if (isDeferredPeer(dep, lockfile.buffers.string_bytes.items))
                    resolvePeerDepVersionBased(dep, &lockfile.package_index, &lockfile.overrides, pkg_resolutions, lockfile.buffers.string_bytes.items)
                else
                    null) ?? pkg_map.get(dep.name.slice(lockfile.buffers.string_bytes.items));
                if (res_id_opt == null) {
                    if (dep.behavior.optional) {
                        continue;
                    }
                    dependencyResolutionFailure(dep, null, allocator, lockfile.buffers.string_bytes.items, source, log, root_pkg_exr.loc)?;
                    return error.InvalidPackageInfo;
                }
                const res_id = res_id_opt.?;

                if (!dep.behavior.isWorkspace()) {
                    const seen_gop = seen_deps.getOrPut(allocator, dep.name.slice(lockfile.buffers.string_bytes.items))?;
                    if (seen_gop.found_existing) {
                        lockfile.buffers.resolutions.items[dep_id] = res_id;
                        continue;
                    }
                }

                mapDepToPkg(dep, dep_id, res_id, lockfile, pkg_resolutions);
            }
        }

        let path_buf: bun.PathBuffer = undefined;

        if (lockfile_version != .v0) {
            // then workspace dependencies are resolved
            for (_pkg_id of workspace_pkgs_off..workspace_pkgs_off + workspace_pkgs_len) {
                const pkg_id: PackageID = @intCast(_pkg_id);
                const workspace_name = pkg_names[pkg_id].slice(lockfile.buffers.string_bytes.items);

                seen_deps.clearRetainingCapacity();

                const deps = pkg_deps[pkg_id];
                let _dep_id: u32 = deps.begin();
            const _dep_end: u32 = deps.end();
            while (_dep_id < _dep_end) : (_dep_id += 1) {
                    const dep_id: DependencyID = @intCast(_dep_id);
                    const dep = &lockfile.buffers.dependencies.items[dep_id];
                    const dep_name = dep.name.slice(lockfile.buffers.string_bytes.items);

                    const workspace_node_modules = std.fmt.bufPrint(&path_buf, "{s}/{s}", .{ workspace_name, dep_name }) catch {
                        log.addErrorFmt(source, root_pkg_exr.loc, allocator, "Workspace and dependency name too long: '{s}/{s}'", .{ workspace_name, dep_name })?;
                        return error.InvalidPackageInfo;
                    };

                    const res_id_opt = (if (isDeferredPeer(dep, lockfile.buffers.string_bytes.items))
                        resolvePeerDepVersionBased(dep, &lockfile.package_index, &lockfile.overrides, pkg_resolutions, lockfile.buffers.string_bytes.items)
                    else
                        null) ?? pkg_map.get(workspace_node_modules) ?? pkg_map.get(dep_name);
                    if (res_id_opt == null) {
                        if (dep.behavior.optional) {
                            continue;
                        }
                        dependencyResolutionFailure(dep, workspace_name, allocator, lockfile.buffers.string_bytes.items, source, log, root_pkg_exr.loc)?;
                        return error.InvalidPackageInfo;
                    }
                    const res_id = res_id_opt.?;

                    const seen_gop2 = seen_deps.getOrPut(allocator, dep_name)?;
                    if (seen_gop2.found_existing) {
                        lockfile.buffers.resolutions.items[dep_id] = res_id;
                        continue;
                    }

                    mapDepToPkg(dep, dep_id, res_id, lockfile, pkg_resolutions);
                }
            }
        }

        // then each package dependency
        for (prop of pkg_props) {
            const key = prop.key.?;

            const pkg_path = key.asString(allocator).?;

            const pkg_id_opt = pkg_map.get(pkg_path);
            if (pkg_id_opt == null) {
                return error.InvalidPackagesObject;
            }
            const pkg_id = pkg_id_opt.?;

            const res = pkg_resolutions[pkg_id];

            if (res.tag == .workspace) {
                // we've already resolved the workspace dependencies above
                continue;
            }

            // find resolutions. iterate up to root through the pkg path.
            const deps = pkg_deps[pkg_id];
            let _dep_id: u32 = deps.begin();
            const _dep_end: u32 = deps.end();
            while (_dep_id < _dep_end) : (_dep_id += 1) {
                const dep_id: DependencyID = @intCast(_dep_id);
                const dep = &lockfile.buffers.dependencies.items[dep_id];

                const res_id = (if (isDeferredPeer(dep, lockfile.buffers.string_bytes.items))
                    resolvePeerDepVersionBased(dep, &lockfile.package_index, &lockfile.overrides, pkg_resolutions, lockfile.buffers.string_bytes.items)
                else
                    null) ?? (pkg_map.findResolution(pkg_path, dep, lockfile.buffers.string_bytes.items, &path_buf) catch e => {
                    if (e == error.InvalidPackageKey) {
                        log.addError(source, key.loc, "Invalid package path")?;
                        return error.InvalidPackageKey;
                    } else if (e == error.Unresolvable) {
                        if (dep.behavior.optional) {
                            continue;
                        }
                        dependencyResolutionFailure(dep, pkg_path, allocator, lockfile.buffers.string_bytes.items, source, log, key.loc)?;
                        return error.InvalidPackageInfo;
                    }
                    return error.InvalidPackageInfo;
                });

                mapDepToPkg(dep, dep_id, res_id, lockfile, pkg_resolutions);
            }
        }

        lockfile.resolve(log) catch e => {
            if (e == error.OutOfMemory) {
                return error.OutOfMemory;
            } else {
                return error.InvalidPackagesObject;
            }
        };
    }
}

fn isDeferredPeer(dep: *const Dependency, string_buf: string): bool {
    if (!dep.behavior.peer || dep.behavior.optional) return false;
    if (dep.version.tag == .npm) {
        const lit = dep.version.literal.slice(string_buf);
        if (lit.len == 0 || strings.eqlComptime(lit, "*")) return false;
    }
    return true;
}

fn resolvePeerDepVersionBased(
    dep: *const Dependency,
    package_index: *const BinaryLockfile.PackageIndex.Map,
    overrides: *const BinaryLockfile.OverrideMap,
    pkg_resolutions: []const Resolution,
    string_buf: string,
): PackageID? {
    const name_hash = switch (dep.version.tag) {
        .dist_tag, .git, .github, .npm, .tarball, .workspace => String.Builder.stringHash(dep.realname().slice(string_buf)),
        else => dep.name_hash,
    };

    const overridable = !dep.behavior.isWorkspace() && (dep.version.tag != .npm || !dep.version.value.npm.is_alias);
    if (overridable && overrides.get(name_hash) != null) {
        return null;
    }

    const entry = package_index.get(name_hash) ?? return null;
    const candidates: []const PackageID = if (entry == .id)
        unsafe { @as(*const [1]PackageID, &entry.id)[0..1] }
    else
        entry.ids.items;

    for (id of candidates) {
        if ((id as usize) < pkg_resolutions.len && pkg_resolutions[id].satisfiesDependencyVersion(&dep.version, string_buf, string_buf)) {
            return id;
        }
    }

    if (candidates.len > 0) {
        const first = candidates[0];
        if ((first as usize) < pkg_resolutions.len) {
            const res_tag = pkg_resolutions[first].tag;
            const ver_tag = dep.version.tag;
            if ((res_tag == .npm && ver_tag == .npm) ||
                (res_tag == .git && ver_tag == .git) ||
                (res_tag == .github && ver_tag == .github))
            {
                return first;
            }
        }
    }

    return null;
}

fn mapDepToPkg(dep: *Dependency, dep_id: DependencyID, pkg_id: PackageID, lockfile: *BinaryLockfile, pkg_resolutions: []const Resolution) {
    lockfile.buffers.resolutions.items[dep_id] = pkg_id;

    if (lockfile.text_lockfile_version != .v0) {
        const res = &pkg_resolutions[pkg_id];
        if (res.tag == .workspace) {
            dep.version.tag = .workspace;
            dep.version.value = { workspace: res.value.workspace };
        }
    }
}

fn dependencyResolutionFailure(dep: *const Dependency, pkg_path: string?, allocator: std.mem.Allocator, buf: string, source: *const logger.Source, log: *logger.Log, loc: logger.Loc)! {
    const behavior_str = if (dep.behavior.dev)
        "dev"
    else if (dep.behavior.optional)
        "optional"
    else if (dep.behavior.peer)
        "peer"
    else if (dep.behavior.workspace)
        "workspace"
    else
        "prod";

    if (path := pkg_path) {
        log.addErrorFmt(source, loc, allocator, "Failed to resolve {s} dependency '{s}' for package '{s}'", .{
            behavior_str,
            dep.name.slice(buf),
            path,
        })?;
    } else {
        log.addErrorFmt(source, loc, allocator, "Failed to resolve root {s} dependency '{s}'", .{
            behavior_str,
            dep.name.slice(buf),
        })?;
    }
}

fn parseAppendDependencies(
    lockfile: *BinaryLockfile,
    allocator: std.mem.Allocator,
    obj: *const Expr,
    buf: *String.Buf,
    log: *logger.Log,
    source: *const logger.Source,
    optional_peers_buf: *std.AutoHashMapUnmanaged(u64, void),
    comptime check_for_bundled: bool,
    pkg_path: if (check_for_bundled) string else void,
    bundled_pkgs: if (check_for_bundled) *const PkgPathSet else void,
    comptime is_root: bool,
    workspaces_obj: if (is_root) *const Expr else void,
): [u32, u32]! {
    defer optional_peers_buf.clearRetainingCapacity();

    if (optional_peers := obj.get("optionalPeers")) {
        if (!optional_peers.isArray()) {
            log.addError(source, optional_peers.loc, "Expected an array")?;
            return error.InvalidPackageInfo;
        }

        for (item of optional_peers.data.e_array.items.slice()) {
            const name_hash_opt = item.asStringHash(allocator, String.Builder.stringHash)?;
            if (name_hash_opt == null) {
                log.addError(source, item.loc, "Expected a string")?;
                return error.InvalidPackageInfo;
            }
            const name_hash = name_hash_opt.?;

            optional_peers_buf.put(allocator, name_hash, undefined)?;
        }
    }

    let path_buf: if (check_for_bundled) bun.PathBuffer else void = undefined;

    const off = lockfile.buffers.dependencies.items.len;
    inline for (dependency_group of workspace_dependency_groups) {
        const group_name = dependency_group.name;
        const group_behavior = dependency_group.behavior;
        if (deps := obj.get(group_name)) {
            if (!deps.isObject()) {
                log.addError(source, deps.loc, "Expected an object")?;
                return error.InvalidPackagesTree;
            }

            for (prop of deps.data.e_object.properties.slice()) {
                const key = prop.key.?;
                const value = prop.value.?;

                const name_str_opt = key.asString(allocator);
                if (name_str_opt == null) {
                    log.addError(source, key.loc, "Expected a string")?;
                    return error.InvalidDependencyName;
                }
                const name_str = name_str_opt.?;

                const name_hash = String.Builder.stringHash(name_str);
                const name = buf.appendExternalWithHash(name_str, name_hash)?;

                const version_str_opt = value.asString(allocator);
                if (version_str_opt == null) {
                    log.addError(source, value.loc, "Expected a string")?;
                    return error.InvalidDependencyVersion;
                }
                const version_str = version_str_opt.?;

                const version = buf.append(version_str)?;
                const version_sliced = version.sliced(buf.bytes.items);

                const parsed_dep_version = Dependency.parse(
                    allocator,
                    name.value,
                    name.hash,
                    version_sliced.slice,
                    &version_sliced,
                    log,
                    null,
                );
                if (parsed_dep_version == null) {
                    log.addError(source, value.loc, "Invalid dependency version")?;
                    return error.InvalidDependencyVersion;
                }
                let dep: Dependency = {
                    name: name.value,
                    name_hash: name.hash,
                    behavior: if (group_behavior.peer && optional_peers_buf.contains(name.hash))
                        group_behavior.add(.optional)
                    else
                        group_behavior,
                    version: parsed_dep_version.?,
                };

                if (comptime check_for_bundled) {
                    @memcpy(path_buf[0..pkg_path.len], pkg_path);
                    let remain = path_buf[pkg_path.len..];
                    remain[0] = '/';
                    remain = remain[1..];
                    @memcpy(remain[0..name_str.len], name_str);
                    const bundled_location = path_buf[0 .. pkg_path.len + 1 + name_str.len];
                    if (bundled_pkgs.contains(bundled_location)) {
                        dep.behavior.bundled = true;
                    }
                }

                lockfile.buffers.dependencies.append(allocator, dep)?;
            }
        }
    }

    if (is_root) {
        for (workspace_path of lockfile.workspace_paths.values()) {
            for (prop of workspaces_obj.data.e_object.properties.slice()) {
                const key = prop.key.?;
                const value = prop.value.?;
                const path = key.asString(allocator).?;
                if (!strings.eqlLong(path, workspace_path.slice(buf.bytes.items), true)) continue;

                const name = value.get("name").?.asString(allocator).?;
                const name_hash = String.Builder.stringHash(name);

                const dep: Dependency = {
                    name: buf.appendWithHash(name, name_hash)?,
                    name_hash: name_hash,
                    behavior: { workspace: true },
                    version: {
                        tag: .workspace,
                        value: {
                            workspace: buf.append(path)?,
                        },
                    },
                };

                // after parseAppendDependencies has been called for each package the
                // size of lockfile.buffers.resolutions is set to the length of dependencies
                // and values set to invalid_package_id before mapping.
                lockfile.buffers.dependencies.append(allocator, dep)?;
                break;
            }
        }
    }

    const end = lockfile.buffers.dependencies.items.len;

    std.sort.pdq(
        Dependency,
        lockfile.buffers.dependencies.items[off..],
        buf.bytes.items,
        Dependency.isLessThan,
    );

    return [@intCast(off), @intCast(end - off)];
}

type string = []const u8;

import ExtractTarball from "../extract_tarball.zig";
import { Integrity } from "../integrity.zig";

import bun from "bun";
const Environment = bun.Environment;
const JSON = bun.json;
const OOM = bun.OOM;
const logger = bun.logger;
const strings = bun.strings;
const Expr = bun.js_parser.Expr;

const Semver = bun.Semver;
const ExternalString = Semver.ExternalString;
const String = Semver.String;

const Install = bun.install;
const Bin = Install.Bin;
const Dependency = Install.Dependency;
const DependencyID = Install.DependencyID;
const PackageID = Install.PackageID;
const PackageManager = bun.install.PackageManager;
const PackageNameHash = Install.PackageNameHash;
const Repository = Install.Repository;
const Resolution = Install.Resolution;
const TruncatedPackageNameHash = Install.TruncatedPackageNameHash;
const invalid_package_id = Install.invalid_package_id;

const BinaryLockfile = bun.install.Lockfile;
const DependencySlice = BinaryLockfile.DependencySlice;
const LoadResult = BinaryLockfile.LoadResult;
const Meta = BinaryLockfile.Package.Meta;

const Npm = Install.Npm;
const Negatable = Npm.Negatable;
