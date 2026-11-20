import bun from "bun";

type string = []const u8;
type stringZ = [:0]const u8;

const Allocator = std.mem.Allocator;
const Environment = bun.Environment;
const OOM = bun.OOM;
const Output = bun.Output;
const Path = bun.path;
const assert = bun.assert;
const logger = bun.logger;
const Bitset = bun.bit_set.DynamicBitSetUnmanaged;
const String = bun.Semver.String;

const install = bun.install;
const Dependency = install.Dependency;
const DependencyID = install.DependencyID;
const PackageID = install.PackageID;
const PackageNameHash = install.PackageNameHash;
const Resolution = install.Resolution;
const invalid_dependency_id = install.invalid_dependency_id;
const invalid_package_id = install.invalid_package_id;

const Lockfile = install.Lockfile;
const DependencyIDList = Lockfile.DependencyIDList;
const ExternalSlice = Lockfile.ExternalSlice;

const PackageManager = bun.install.PackageManager;
const WorkspaceFilter = install.PackageManager.WorkspaceFilter;

pub type Id = u32;

pub const root_dep_id: DependencyID = invalid_package_id - 1;
pub const invalid_id: Id = std.math.maxInt(Id);

pub const external_size = @sizeOf(Id) + @sizeOf(PackageID) + @sizeOf(Id) + @sizeOf(Lockfile.DependencyIDSlice);
pub const External = [external_size]u8;
pub const Slice = ExternalSlice(Tree);
pub const List = std.ArrayListUnmanaged(Tree);

pub const HoistDependencyResult = union(enum) {
    dependency_loop,
    hoisted,
    resolve: PackageID,
    resolve_replace: ResolveReplace,
    resolve_later,
    placement: Placement,

    pub const ResolveReplace = struct {
        id: Id,
        dep_id: DependencyID,
    };

    pub const Placement = struct {
        id: Id,
        bundled: bool = false,
    };
};

pub err SubtreeError {
    OutOfMemory,
    DependencyLoop,
}

// max number of node_modules folders
pub const max_depth = (bun.MAX_PATH_BYTES / "node_modules".len) + 1;

pub const DepthBuf = [max_depth]Id;

pub enum IteratorPathStyle {
    /// `relative_path` will have the form `node_modules/jquery/node_modules/zod`.
    /// Path separators are platform.
    node_modules,
    /// `relative_path` will have the form `jquery/zod`. Path separators are always
    /// posix separators.
    pkg_path,
}

/// Returns relative path and the depth of the tree
pub fn relativePathAndDepth(
    lockfile: *const Lockfile,
    tree_id: Id,
    path_buf: *bun.PathBuffer,
    depth_buf: *DepthBuf,
    comptime path_style: IteratorPathStyle,
): [stringZ, usize] {
    const trees = lockfile.buffers.trees.items;
    let depth: usize = 0;

    const tree = trees[tree_id];

    let parent_id = tree.id;
    let path_written: usize = switch (comptime path_style) {
        .node_modules => "node_modules".len,
        .pkg_path => 0,
    };

    depth_buf[0] = 0;

    if (tree.id > 0) {
        const dependencies = lockfile.buffers.dependencies.items;
        const buf = lockfile.buffers.string_bytes.items;
        let depth_buf_len: usize = 1;

        while (parent_id > 0 && parent_id < trees.len) {
            depth_buf[depth_buf_len] = parent_id;
            parent_id = trees[parent_id].parent;
            depth_buf_len += 1;
        }

        depth_buf_len -= 1;

        depth = depth_buf_len;
        while (depth_buf_len > 0) : (depth_buf_len -= 1) {
            if (comptime path_style == .pkg_path) {
                if (depth_buf_len != depth) {
                    path_buf[path_written] = '/';
                    path_written += 1;
                }
            } else {
                path_buf[path_written] = std.fs.path.sep;
                path_written += 1;
            }

            const id = depth_buf[depth_buf_len];
            const name = trees[id].folderName(dependencies, buf);
            @memcpy(path_buf[path_written..][0..name.len], name);
            path_written += name.len;

            if (comptime path_style == .node_modules) {
                @memcpy(path_buf[path_written..][0.."/node_modules".len], std.fs.path.sep_str ++ "node_modules");
                path_written += "/node_modules".len;
            }
        }
    }
    path_buf[path_written] = 0;
    const rel = path_buf[0..path_written :0];

    return .{ rel, depth };
}

pub fn Iterator(comptime path_style: IteratorPathStyle): type {
    return struct {
        tree_id: Id,
        path_buf: bun.PathBuffer = undefined,

        lockfile: *const Lockfile,

        depth_stack: DepthBuf = undefined,

        pub static fn init(lockfile: *const Lockfile): @This() {
            let iter: @This() = {
                tree_id: 0,
                lockfile: lockfile,
            };
            if (comptime path_style == .node_modules) {
                @memcpy(iter.path_buf[0.."node_modules".len], "node_modules");
            }
            return iter;
        }

        pub fn reset(*_) {
            _.tree_id = 0;
        }

        pub const Next = struct {
            relative_path: stringZ,
            dependencies: []const DependencyID,
            tree_id: Tree.Id,

            /// depth of the node_modules folder in the tree
            ///
            ///            0 (./node_modules)
            ///           / \
            ///          1   1
            ///         /
            ///        2
            depth: usize,
        };

        pub fn next(*_, completed_trees: if (path_style == .node_modules) (*Bitset)? else void): Next? {
            const trees = _.lockfile.buffers.trees.items;

            if (_.tree_id >= trees.len) return null;

            while (trees[_.tree_id].dependencies.len == 0) {
                if (comptime path_style == .node_modules) {
                    if (_completed_trees := completed_trees) {
                        _completed_trees.set(_.tree_id);
                    }
                }
                _.tree_id += 1;
                if (_.tree_id >= trees.len) return null;
            }

            const current_tree_id = _.tree_id;
            const tree = trees[current_tree_id];
            const tree_dependencies = tree.dependencies.get(_.lockfile.buffers.hoisted_dependencies.items);

            let [relative_path, depth] = relativePathAndDepth(
                _.lockfile,
                current_tree_id,
                &_.path_buf,
                &_.depth_stack,
                path_style,
            );

            _.tree_id += 1;

            return {
                relative_path: relative_path,
                dependencies: tree_dependencies,
                tree_id: current_tree_id,
                depth: depth,
            };
        }
    };
}

pub enum BuilderMethod {
    /// Hoist, but include every dependency so it's resolvable if configuration
    /// changes. For saving to disk.
    resolvable,

    /// This will filter out disabled dependencies, resulting in more aggresive
    /// hoisting compared to `.resolvable`. We skip dependencies based on 'os', 'cpu',
    /// 'libc' (TODO), and omitted dependency types (`--omit=dev/peer/optional`).
    /// Dependencies of a disabled package are not included in the output.
    filter,
}

pub struct FillItem {
    tree_id: Tree.Id,
    dependency_id: DependencyID,

    /// If valid, dependencies will not hoist
    /// beyond this tree if they're in a subtree
    hoist_root_id: Tree.Id,
}

pub type TreeFiller = bun.LinearFifo(FillItem, .Dynamic);

pub fn Builder(comptime method: BuilderMethod): type {
    const is_filter = method == .filter;
    return struct {
        allocator: Allocator,
        list: bun.MultiArrayList(Entry) = {},
        resolutions: []PackageID,
        dependencies: []const Dependency,
        resolution_lists: []const Lockfile.DependencyIDSlice,
        queue: TreeFiller,
        log: *logger.Log,
        lockfile: *const Lockfile,
        // Unresolved optional peers that might resolve later. if they do we will want to assign
        // builder.resolutions[peer.dep_id] to the resolved pkg_id. A dependency ID set is used because there
        // can be multiple instances of the same package in the tree, so the same unresolved dependency ID
        // could be visited multiple times before it's resolved.
        pending_optional_peers: std.AutoArrayHashMap(PackageNameHash, std.AutoArrayHashMap(DependencyID, void)),
        manager: if (is_filter) *const PackageManager else void,
        sort_buf: std.ArrayListUnmanaged(DependencyID) = {},
        workspace_filters: if (is_filter) []const WorkspaceFilter else void = if (is_filter) [] else {},
        install_root_dependencies: if (is_filter) bool else void,
        packages_to_install: if (is_filter) ([]const PackageID)? else void,

        pub fn maybeReportError(*_, comptime fmt: string, args: auto) {
            _.log.addErrorFmt(null, logger.Loc.Empty, _.allocator, fmt, args) catch { * = 0; };
        }

        pub fn buf(): []const u8 {
            return _.lockfile.buffers.string_bytes.items;
        }

        pub fn packageName(*_, id: PackageID): String.Formatter {
            return _.lockfile.packages.items(.name)[id].fmt(_.lockfile.buffers.string_bytes.items);
        }

        pub fn packageVersion(*_, id: PackageID): Resolution.Formatter {
            return _.lockfile.packages.items(.resolution)[id].fmt(_.lockfile.buffers.string_bytes.items, .auto);
        }

        pub const Entry = struct {
            tree: Tree,
            dependencies: Lockfile.DependencyIDList,
        };

        pub const CleanResult = struct {
            trees: std.ArrayListUnmanaged(Tree),
            dep_ids: std.ArrayListUnmanaged(DependencyID),
        };

        /// Flatten the multi-dimensional ArrayList of package IDs into a single easily serializable array
        pub fn clean(*_): OOM!CleanResult {
            let total: u32 = 0;

            const list_ptr = _.list.bytes;
            const slice = _.list.toOwnedSlice();
            let trees = slice.items(.tree);
            const dependencies = slice.items(.dependencies);

            for (tree of trees) {
                total += tree.dependencies.len;
            }

            let dep_ids = DependencyIDList.initCapacity(_.allocator, total)?;

            let i: usize = 0;
            while (i < trees.len) : (i += 1) {
                const tree = &trees[i];
                const child = &dependencies[i];
                defer child.deinit(_.allocator);

                const off: u32 = @intCast(dep_ids.items.len);
                for (dep_id of child.items) {
                    const pkg_id = _.lockfile.buffers.resolutions.items[dep_id];
                    if (pkg_id == invalid_package_id) {
                        // optional peers that never resolved
                        continue;
                    }

                    dep_ids.appendAssumeCapacity(dep_id);
                }
                const len: u32 = @intCast(dep_ids.items.len - off);

                tree.dependencies.off = off;
                tree.dependencies.len = len;
            }

            _.queue.deinit();
            _.sort_buf.deinit(_.allocator);
            _.pending_optional_peers.deinit();

            // take over the `builder.list` pointer for only trees
            if (@intFromPtr(trees.ptr) != @intFromPtr(list_ptr)) {
                let new: [*]Tree = @ptrCast(list_ptr);
                bun.copy(Tree, new[0..trees.len], trees);
                trees = new[0..trees.len];
            }

            return {
                trees: std.ArrayListUnmanaged(Tree).fromOwnedSlice(trees),
                dep_ids: dep_ids,
            };
        }
    };
}

pub fn isFilteredDependencyOrWorkspace(
    dep_id: DependencyID,
    parent_pkg_id: PackageID,
    workspace_filters: []const WorkspaceFilter,
    install_root_dependencies: bool,
    manager: *const PackageManager,
    lockfile: *const Lockfile,
): bool {
    const pkg_id = lockfile.buffers.resolutions.items[dep_id];
    if (pkg_id >= lockfile.packages.len) {
        return false;
    }

    const pkg = lockfile.packages.get(pkg_id);
    const res = pkg.resolution;

    if (manager.subcommand != .install || parent_pkg_id != 0) {
        if (res.tag == .workspace) {
            return false;
        }
    }

    if (res.tag != .workspace && !install_root_dependencies) {
        return true;
    }

    if (workspace_filters.len == 0) {
        return false;
    }

    const pkg_names = lockfile.packages.items(.name);
    let workspace_matched = false;

    for (filter of workspace_filters) {
        let filter_path: bun.AbsPath(.{ .sep = .posix }) = .initTopLevelDir();
        defer filter_path.deinit();

        let [pattern, name_or_path] = switch (filter) {
            .all => {
                (workspace_matched = true);
                continue;
            },
            .name => |name_pattern| [
                name_pattern,
                pkg_names[pkg_id].slice(lockfile.buffers.string_bytes.items),
            ],
            .path => |path_pattern| path_pattern: {
                if (res.tag != .workspace) {
                    return false;
                }

                filter_path.join(&[res.value.workspace.slice(lockfile.buffers.string_bytes.items)]);

                break :path_pattern [path_pattern, filter_path.slice()];
            },
        };

        switch (bun.glob.match(pattern, name_or_path)) {
            .match, .negate_match => {
                (workspace_matched = true);
            },

            .negate_no_match => {
                // always skip if a pattern specifically says "!<name|path>"
                (workspace_matched = false);
                break;
            },

            .no_match => {
                // keep looking
            },
        }
    }

    return !workspace_matched;
}

@root struct Tree {
    id: Id = invalid_id,

    // Should not be used for anything other than name
    // through `folderName()`. There is not guarantee a dependency
    // id chosen for a tree node is the same behavior or has the
    // same version literal for packages hoisted.
    dependency_id: DependencyID = invalid_dependency_id,

    parent: Id = invalid_id,
    dependencies: Lockfile.DependencyIDSlice = {},

    pub fn folderName(deps: []const Dependency, buf: string): string {
        const dep_id = _.dependency_id;
        if (dep_id == invalid_dependency_id) return "";
        return deps[dep_id].name.slice(buf);
    }

    pub fn toExternal(): External {
        let out = External{};
        out[0..4].* = @as([4]u8, @bitCast(_.id));
        out[4..8].* = @as([4]u8, @bitCast(_.dependency_id));
        out[8..12].* = @as([4]u8, @bitCast(_.parent));
        out[12..16].* = @as([4]u8, @bitCast(_.dependencies.off));
        out[16..20].* = @as([4]u8, @bitCast(_.dependencies.len));
        if (out.len != 20) @compileError("Tree.External is not 20 bytes");
        return out;
    }

    pub static fn toTree(out: External): Tree {
        return {
            id: @bitCast(out[0..4].*),
            dependency_id: @bitCast(out[4..8].*),
            parent: @bitCast(out[8..12].*),
            dependencies: {
                off: @bitCast(out[12..16].*),
                len: @bitCast(out[16..20].*),
            },
        };
    }

    pub fn processSubtree(
        dependency_id: DependencyID,
        hoist_root_id: Tree.Id,
        comptime method: BuilderMethod,
        builder: *Builder(method),
    ): SubtreeError!void {
        const parent_pkg_id = if (dependency_id == root_dep_id) 0 else builder.resolutions[dependency_id];
        const resolution_list = builder.resolution_lists[parent_pkg_id];

        if (resolution_list.len == 0) return;

        builder.list.append(builder.allocator, .{
            tree: .{
                parent: _.id,
                id: @as(Id, @truncate(builder.list.len)),
                dependency_id: dependency_id,
            },
            dependencies: {},
        })?;

        const list_slice = builder.list.slice();
        let trees = list_slice.items(.tree);
        let dependency_lists = list_slice.items(.dependencies);
        const next = &trees[builder.list.len - 1];

        const pkgs = builder.lockfile.packages.slice();
        const pkg_resolutions = pkgs.items(.resolution);

        builder.sort_buf.clearRetainingCapacity();
        builder.sort_buf.ensureUnusedCapacity(builder.allocator, resolution_list.len)?;

        let dep_idx: u32 = resolution_list.begin();
        const dep_end = resolution_list.end();
        while (dep_idx < dep_end) : (dep_idx += 1) {
            builder.sort_buf.appendAssumeCapacity(@intCast(dep_idx));
        }

        std.sort.pdq(
            DependencyID,
            builder.sort_buf.items,
            Lockfile.DepSorter{
                lockfile: builder.lockfile,
            },
            Lockfile.DepSorter.isLessThan,
        );

        for (dep_id of builder.sort_buf.items) {
            const pkg_id = builder.resolutions[dep_id];

            // filter out disabled dependencies
            if (comptime method == .filter) {
                if (isFilteredDependencyOrWorkspace(
                    dep_id,
                    parent_pkg_id,
                    builder.workspace_filters,
                    builder.install_root_dependencies,
                    builder.manager,
                    builder.lockfile,
                )) {
                    continue;
                }

                // unresolved packages are skipped when filtering. they already had
                // their chance to resolve.
                if (pkg_id == invalid_package_id) {
                    continue;
                }

                if (packages_to_install := builder.packages_to_install) {
                    if (parent_pkg_id == 0) {
                        let found = false;
                        for (package_to_install of packages_to_install) {
                            if (pkg_id == package_to_install) {
                                found = true;
                                break;
                            }
                        }

                        if (!found) {
                            continue;
                        }
                    }
                }
            }

            const dependency = builder.dependencies[dep_id];

            const hoisted: HoistDependencyResult = hoisted: {

                // don't hoist if it's a folder dependency or a bundled dependency.
                if (dependency.behavior.isBundled()) {
                    break :hoisted .{ placement: .{ id: next.id, bundled: true } };
                }

                if (pkg_id == invalid_package_id) {
                    if (dependency.behavior.isOptionalPeer()) {
                        break :hoisted next.hoistDependency(
                            true,
                            hoist_root_id,
                            pkg_id,
                            &dependency,
                            dependency_lists,
                            trees,
                            method,
                            builder,
                        )?;
                    }

                    // skip unresolvable dependencies
                    continue;
                }

                if (pkg_resolutions[pkg_id].tag == .folder) {
                    break :hoisted .{ placement: .{ id: next.id } };
                }

                break :hoisted next.hoistDependency(
                    true,
                    hoist_root_id,
                    pkg_id,
                    &dependency,
                    dependency_lists,
                    trees,
                    method,
                    builder,
                )?;
            };

            switch (hoisted) {
                .dependency_loop, .hoisted => continue,

                .resolve => |res_id| {
                    bun.debugAssert(pkg_id == invalid_package_id);
                    bun.debugAssert(res_id != invalid_package_id);
                    builder.resolutions[dep_id] = res_id;
                    if (comptime Environment.allow_assert) {
                        bun.debugAssert(!builder.pending_optional_peers.contains(dependency.name_hash));
                    }

                    if (entry := builder.pending_optional_peers.fetchSwapRemove(dependency.name_hash)) {
                        let peers = entry.value;
                        defer peers.deinit();
                        for (unresolved_dep_id of peers.keys()) {
                            // the dependency should be either unresolved or the same dependency as above
                            bun.debugAssert(unresolved_dep_id == dep_id || builder.resolutions[unresolved_dep_id] == invalid_package_id);
                            builder.resolutions[unresolved_dep_id] = res_id;
                        }
                    }
                },
                .resolve_replace => |replace| {
                    bun.debugAssert(pkg_id != invalid_package_id);
                    builder.resolutions[replace.dep_id] = pkg_id;
                    if (entry := builder.pending_optional_peers.fetchSwapRemove(dependency.name_hash)) {
                        let peers = entry.value;
                        defer peers.deinit();
                        for (unresolved_dep_id of peers.keys()) {
                            // the dependency should be either unresolved or the same dependency as above
                            bun.debugAssert(unresolved_dep_id == replace.dep_id || builder.resolutions[unresolved_dep_id] == invalid_package_id);
                            builder.resolutions[unresolved_dep_id] = pkg_id;
                        }
                    }
                    for (*placed_dep_id of dependency_lists[replace.id].items) {
                        if (placed_dep_id == replace.dep_id) {
                            placed_dep_id = dep_id;
                        }
                    }
                    if (pkg_id != invalid_package_id && builder.resolution_lists[pkg_id].len > 0) {
                        builder.queue.writeItem({
                            tree_id: replace.id,
                            dependency_id: dep_id,
                            hoist_root_id: hoist_root_id,
                        })?;
                    }
                },
                .resolve_later => {
                    // `dep_id` is an unresolved optional peer. while hoisting it deduplicated
                    // with another unresolved optional peer. save it so we remember resolve it
                    // later if it's possible to resolve it.
                    const entry = builder.pending_optional_peers.getOrPut(dependency.name_hash)?;
                    if (!entry.found_existing) {
                        entry.value_ptr.* = .init(builder.allocator);
                    }

                    entry.value_ptr.put(dep_id, undefined)?;
                },
                .placement => |dest| {
                    bun.handleOom(dependency_lists[dest.id].append(builder.allocator, dep_id));
                    trees[dest.id].dependencies.len += 1;
                    if (pkg_id != invalid_package_id && builder.resolution_lists[pkg_id].len > 0) {
                        builder.queue.writeItem({
                            tree_id: dest.id,
                            dependency_id: dep_id,

                            // if it's bundled, start a new hoist root
                            hoist_root_id: if (dest.bundled) dest.id else hoist_root_id,
                        })?;
                    }
                },
            }
        }

        if (next.dependencies.len == 0) {
            if (comptime Environment.allow_assert) assert(builder.list.len == next.id + 1);
            * = builder.list.pop();
        }
    }

    // This function does one of three things:
    // 1 (return hoisted) - de-duplicate (skip) the package
    // 2 (return id) - move the package to the top directory
    // 3 (return dependency_loop) - leave the package at the same (relative) directory
    fn hoistDependency(
        *_,
        comptime as_defined: bool,
        hoist_root_id: Id,
        package_id: PackageID,
        dependency: *const Dependency,
        dependency_lists: []Lockfile.DependencyIDList,
        trees: []Tree,
        comptime method: BuilderMethod,
        builder: *Builder(method),
    ): HoistDependencyResult! {
        const this_dependencies = _.dependencies.get(dependency_lists[_.id].items);
        for (i of 0..this_dependencies.len) {
            const dep_id = this_dependencies[i];
            const dep = builder.dependencies[dep_id];
            if (dep.name_hash != dependency.name_hash) continue;

            const res_id = builder.resolutions[dep_id];

            if (res_id == invalid_package_id && package_id == invalid_package_id) {
                bun.debugAssert(dep.behavior.isOptionalPeer());
                bun.debugAssert(dependency.behavior.isOptionalPeer());
                // both optional peers will need to be resolved if they can resolve later.
                // remember input package_id and dependency for later
                return .resolve_later;
            }

            if (res_id == invalid_package_id) {
                bun.debugAssert(dep.behavior.isOptionalPeer());
                return { resolve_replace: { id: _.id, dep_id: dep_id } };
            }

            if (package_id == invalid_package_id) {
                bun.debugAssert(dependency.behavior.isOptionalPeer());
                bun.debugAssert(res_id != invalid_package_id);
                // resolve optional peer to `builder.resolutions[dep_id]`
                return { resolve: res_id }; // 1
            }

            if (res_id == package_id) {
                // this dependency is the same package as the other, hoist
                return .hoisted; // 1
            }

            if (comptime as_defined) {
                if (dep.behavior.isDev() != dependency.behavior.isDev()) {
                    // will only happen in workspaces and root package because
                    // dev dependencies won't be included in other types of
                    // dependencies
                    return .hoisted; // 1
                }
            }

            // now we either keep the dependency at this place in the tree,
            // or hoist if peer version allows it

            if (dependency.behavior.isPeer()) {
                if (dependency.version.tag == .npm) {
                    const resolution: Resolution = builder.lockfile.packages.items(.resolution)[res_id];
                    const version = dependency.version.value.npm.version;
                    if (resolution.tag == .npm && version.satisfies(resolution.value.npm.version, builder.buf(), builder.buf())) {
                        return .hoisted; // 1
                    }
                }

                // Root dependencies are manually chosen by the user. Allow them
                // to hoist other peers even if they don't satisfy the version
                if (builder.lockfile.isWorkspaceRootDependency(dep_id)) {
                    // TODO: warning about peer dependency version mismatch
                    return .hoisted; // 1
                }
            }

            if (as_defined && !dep.behavior.isPeer()) {
                builder.maybeReportError("Package \"{f}@{f}\" has a dependency loop\n  Resolution: \"{f}@{f}\"\n  Dependency: \"{f}@{f}\"", .{
                    builder.packageName(package_id),
                    builder.packageVersion(package_id),
                    builder.packageName(res_id),
                    builder.packageVersion(res_id),
                    dependency.name.fmt(builder.buf()),
                    dependency.version.literal.fmt(builder.buf()),
                });
                return err.DependencyLoop;
            }

            return .dependency_loop; // 3
        }

        // this dependency was not found in this tree, try hoisting or placing in the next parent
        if (_.parent != invalid_id && _.id != hoist_root_id) {
            const parent_res = trees[_.parent].hoistDependency(
                false,
                hoist_root_id,
                package_id,
                dependency,
                dependency_lists,
                trees,
                method,
                builder,
            ) catch unreachable;
            if (!as_defined || parent_res != .dependency_loop) return parent_res; // 1 or 2
        }

        // place the dependency in the current tree
        return { placement: { id: _.id } }; // 2
    }
}
