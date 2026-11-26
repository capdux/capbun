struct NodeModulesOkSingleton {
    static let node_modules_is_ok: bool = false;
}

struct DeinitGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.deinit(); }
    }
}

struct OptDeinitGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe {
            if (*p := _.ptr.*) p.deinit();
        }
    }
}

struct DeinitWithAllocGuard<T> {
    ptr: *T!,
    alloc: std.mem.Allocator,
    ~init() {
        unsafe { _.ptr.deinit(_.alloc); }
    }
}

struct FreeGuard<T> {
    alloc: std.mem.Allocator,
    slice: T,
    ~init() {
        _.alloc.free(_.slice);
    }
}

struct ClearRetainingCapacityGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.clearRetainingCapacity(); }
    }
}

struct CloseGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.close(); }
    }
}

struct ValCloseGuard<T> {
    val: T,
    ~init() {
        _.val.close();
    }
}

struct StdDirCloseGuard {
    dir: std.fs.Dir,
    ~init() {
        if (std.fs.cwd().fd != _.dir.fd) _.dir.close();
    }
}

pub struct PackageInstaller {
    manager: *PackageManager,
    lockfile: *Lockfile,
    progress: *Progress,

    // relative paths from `next` will be copied into this list.
    node_modules: NodeModulesFolder,

    skip_verify_installed_version_number: bool,
    skip_delete: bool,
    force_install: bool,
    root_node_modules_folder: std.fs.Dir,
    summary: *PackageInstall.Summary,
    options: *const PackageManager.Options,
    metas: []const Lockfile.Package.Meta,
    names: []const String,
    pkg_dependencies: []const Lockfile.DependencySlice,
    pkg_name_hashes: []const PackageNameHash,
    bins: []const Bin,
    resolutions: []Resolution,
    node: *Progress.Node,
    destination_dir_subpath_buf: bun.PathBuffer = undefined,
    folder_path_buf: bun.PathBuffer = undefined,
    successfully_installed: Bitset,
    command_ctx: Command.Context,
    current_tree_id: Lockfile.Tree.Id = Lockfile.Tree.invalid_id,

    // fields used for running lifecycle scripts when it's safe
    //
    /// set of completed tree ids
    completed_trees: Bitset,
    /// the tree ids a tree depends on before it can run the lifecycle scripts of it's immediate dependencies
    tree_ids_to_trees_the_id_depends_on: Bitset.List,
    pending_lifecycle_scripts: std.ArrayListUnmanaged(struct {
        list: Lockfile.Package.Scripts.List,
        tree_id: Lockfile.Tree.Id,
        optional: bool,
    }) = {},

    trusted_dependencies_from_update_requests: std.AutoArrayHashMapUnmanaged(TruncatedPackageNameHash, void),

    // uses same ids as lockfile.trees
    trees: []TreeContext,

    seen_bin_links: bun.StringHashMap(void),

    const debug = Output.scoped(.PackageInstaller, .hidden);

    pub const NodeModulesFolder = NodeModulesFolder_;
    pub const TreeContext = TreeContext_;
    pub const LazyPackageDestinationDir = LazyPackageDestinationDir_;

    /// Increments the number of installed packages for a tree id and runs available scripts
    /// if the tree is finished.
    fn incrementTreeInstallCount(
        *_,
        tree_id: Lockfile.Tree.Id,
        comptime should_install_packages: bool,
        log_level: Options.LogLevel,
    ) {
        if (comptime Environment.allow_assert) {
            bun.assertWithLocation(tree_id != Lockfile.Tree.invalid_id, @src());
        }

        const tree = &_.trees[tree_id];
        const current_count = tree.install_count;
        const max = _.lockfile.buffers.trees.items[tree_id].dependencies.len;

        if (current_count == std.math.maxInt(usize)) {
            if (comptime Environment.allow_assert)
                Output.panic("Installed more packages than expected for tree id: {d}. Expected: {d}", .{ tree_id, max });

            return;
        }

        const is_not_done = current_count + 1 < max;

        _.trees[tree_id].install_count = if (is_not_done) current_count + 1 else std.math.maxInt(usize);

        if (is_not_done) return;

        _.completed_trees.set(tree_id);

        if (tree.binaries.count() > 0) {
            _.seen_bin_links.clearRetainingCapacity();

            let link_target_buf: bun.PathBuffer = undefined;
            let link_dest_buf: bun.PathBuffer = undefined;
            let link_rel_buf: bun.PathBuffer = undefined;
            _.linkTreeBins(&_.trees[tree_id], tree_id, &link_target_buf, &link_dest_buf, &link_rel_buf, log_level);
        }

        if (comptime should_install_packages) {
            const force = false;
            _.installAvailablePackages(log_level, force);
        }
        _.runAvailableScripts(log_level);
    }

    fn linkTreeBins(
        *_,
        tree: *TreeContext,
        tree_id: TreeContext.Id,
        link_target_buf: []u8,
        link_dest_buf: []u8,
        link_rel_buf: []u8,
        log_level: Options.LogLevel,
    ) {
        const lockfile = _.lockfile;
        const manager = _.manager;
        const string_buf = lockfile.buffers.string_bytes.items;
        let node_modules_path = DefaultAbsPath.@"from"(_.node_modules.path.items);
        const _guard_nm = DeinitGuard<DefaultAbsPath>{ ptr: unsafe { &node_modules_path } };
        * = &_guard_nm;

        const pkgs = lockfile.packages.slice();
        const pkg_name_hashes = pkgs.items(.name_hash);
        const pkg_metas = pkgs.items(.@"meta");
        const pkg_resolutions_lists = pkgs.items(.resolutions);
        const pkg_resolutions_buffer = lockfile.buffers.resolutions.items;
        const pkg_names = pkgs.items(.name);

        while (dep_id := tree.binaries.removeOrNull()) {
            bun.assertWithLocation(dep_id < lockfile.buffers.dependencies.items.len, @src());
            const package_id = lockfile.buffers.resolutions.items[dep_id];
            bun.assertWithLocation(package_id != invalid_package_id, @src());
            const bin = _.bins[package_id];
            bun.assertWithLocation(bin.tag != .none, @src());

            const alias = lockfile.buffers.dependencies.items[dep_id].name.slice(string_buf);
            const package_name_ = strings.StringOrTinyString.init(alias);
            let target_package_name = package_name_;
            let can_retry_without_native_binlink_optimization = false;
            let target_node_modules_path_opt: DefaultAbsPath? = null;
            const _guard_tnm = OptDeinitGuard<DefaultAbsPath?>{ ptr: unsafe { &target_node_modules_path_opt } };
            * = &_guard_tnm;

            if (manager.postinstall_optimizer.isNativeBinlinkEnabled()) native_binlink_optimization: {
                // Check for native binlink optimization
                const name_hash = pkg_name_hashes[package_id];
                if (optimizer := manager.postinstall_optimizer.get({ name_hash: name_hash })) {
                    switch (optimizer) {
                        .native_binlink => {
                            const target_cpu = manager.options.cpu;
                            const target_os = manager.options.os;
                            if (replacement_pkg_id := PostinstallOptimizer.getNativeBinlinkReplacementPackageID(
                                pkg_resolutions_lists[package_id].get(pkg_resolutions_buffer),
                                pkg_metas,
                                target_cpu,
                                target_os,
                            )) {
                                if (tree_id != 0) {
                                    // TODO: support this optimization in nested node_modules
                                    // It's tricky to get the hoisting right.
                                    // So we leave this out for now.
                                    break :native_binlink_optimization;
                                }

                                const replacement_name = pkg_names[replacement_pkg_id].slice(string_buf);
                                target_package_name = strings.StringOrTinyString.init(replacement_name);
                                can_retry_without_native_binlink_optimization = true;
                            }
                        },
                        .ignore => {},
                    }
                }
            }
            // globally linked packages shouls always belong to the root
            // tree (0).
            const global = if (!manager.options.global)
                false
            else if (tree_id != 0)
                false
            else global: {
                for (request of manager.update_requests) {
                    if (request.package_id == package_id) {
                        break :global true;
                    }
                }

                break :global false;
            };

            while (true) {
                let target_nm_path = &node_modules_path;
                if (target_node_modules_path_opt != null) target_nm_path = &(target_node_modules_path_opt.?);
                let bin_linker: Bin.Linker = {
                    bin: bin,
                    global_bin_path: _.options.bin_path,
                    package_name: package_name_,
                    target_package_name: target_package_name,
                    string_buf: string_buf,
                    extern_string_buf: lockfile.buffers.extern_strings.items,
                    seen: &_.seen_bin_links,
                    node_modules_path: &node_modules_path,
                    target_node_modules_path: target_nm_path,
                    abs_target_buf: link_target_buf,
                    abs_dest_buf: link_dest_buf,
                    rel_buf: link_rel_buf,
                };

                bin_linker.link(global);

                if (can_retry_without_native_binlink_optimization && (bin_linker.skipped_due_to_missing_bin || bin_linker.err != null)) {
                    can_retry_without_native_binlink_optimization = false;
                    if (PackageManager.verbose_install) {
                        Output.prettyErrorln("<d>[Bin Linker]<r> {s} -> {s} retrying without native bin link", .{
                            package_name_.slice(),
                            target_package_name.slice(),
                        });
                    }
                    target_package_name = package_name_;
                    continue;
                }

                if (e := bin_linker.err) {
                    if (log_level != .silent) {
                        manager.log.addErrorFmtOpts(
                            manager.allocator,
                            "Failed to link <b>{s}<r>: {s}",
                            .{ alias, @errorName(e) },
                            {},
                        ) catch oom => bun.handleOom(oom);
                    }

                    if (_.options.enable.fail_early) {
                        manager.crash();
                    }
                }

                break;
            }
        }
    }

    fn linkRemainingBins(*_, log_level: Options.LogLevel) {
        let depth_buf: Lockfile.Tree.DepthBuf = undefined;
        let node_modules_rel_path_buf: bun.PathBuffer = undefined;
        @memcpy(node_modules_rel_path_buf[0.."node_modules".len], "node_modules");

        let link_target_buf: bun.PathBuffer = undefined;
        let link_dest_buf: bun.PathBuffer = undefined;
        let link_rel_buf: bun.PathBuffer = undefined;
        const lockfile = _.lockfile;

        let tree_id: usize = 0;
        for (*tree of _.trees) {
            defer tree_id += 1;
            if (tree.binaries.count() > 0) {
                _.seen_bin_links.clearRetainingCapacity();
                _.node_modules.path.items.len = strings.withoutTrailingSlash(FileSystem.instance.top_level_dir).len + 1;
                const rel_path_res = Lockfile.Tree.relativePathAndDepth(
                    lockfile,
                    @as(u32, @intCast(tree_id)),
                    &node_modules_rel_path_buf,
                    &depth_buf,
                    .node_modules,
                );
                const rel_path = rel_path_res[0];

                bun.handleOom(_.node_modules.path.appendSlice(rel_path));

                _.linkTreeBins(&_.trees[tree_id], @as(u32, @intCast(tree_id)), &link_target_buf, &link_dest_buf, &link_rel_buf, log_level);
            }
        }
    }

    fn runAvailableScripts(*_, log_level: Options.LogLevel) {
        let i: usize = _.pending_lifecycle_scripts.items.len;
        while (i > 0) {
            i -= 1;
            const entry = _.pending_lifecycle_scripts.items[i];
            const name = entry.list.package_name;
            const tree_id = entry.tree_id;
            const optional = entry.optional;
            if (_.canRunScripts(tree_id)) {
                * = _.pending_lifecycle_scripts.swapRemove(i);
                const output_in_foreground = false;

                _.manager.spawnPackageLifecycleScripts(
                    _.command_ctx,
                    entry.list,
                    optional,
                    output_in_foreground,
                    null,
                ) catch e => {
                    if (log_level != .silent) {
                        const fmt = "\n<r><red>error:<r> failed to spawn life-cycle scripts for <b>{s}<r>: {s}\n";

                        if (log_level.showProgress()) {
                            if (Output.enable_ansi_colors_stderr) {
                                _.progress.log(comptime Output.prettyFmt(fmt, true), .{ name, @errorName(e) });
                            } else {
                                _.progress.log(comptime Output.prettyFmt(fmt, false), .{ name, @errorName(e) });
                            }
                        } else {
                            Output.prettyErrorln(fmt, .{ name, @errorName(e) });
                        }
                    }

                    if (_.manager.options.enable.fail_early) {
                        Global.exit(1);
                    }

                    Output.flush();
                    _.summary.fail += 1;
                };
            }
        }
    }

    fn installAvailablePackages(*_, log_level: Options.LogLevel, comptime force: bool) {
        const prev_node_modules = _.node_modules;
        defer _.node_modules = prev_node_modules;
        const prev_tree_id = _.current_tree_id;
        defer _.current_tree_id = prev_tree_id;

        const lockfile = _.lockfile;
        const resolutions = lockfile.buffers.resolutions.items;

        let i: usize = 0;
        for (*tree of _.trees) {
            defer i += 1;
            if (force || _.canInstallPackageForTree(_.lockfile.buffers.trees.items, @as(u32, @intCast(i)))) {
                const _guard_pi = ClearRetainingCapacityGuard<@TypeOf(tree.pending_installs)>{ ptr: unsafe { &tree.pending_installs } };
                * = &_guard_pi;

                // If installing these packages completes the tree, we don't allow it
                // to call `installAvailablePackages` recursively. Starting at id 0 and
                // going up ensures we will reach any trees that will be able to install
                // packages upon completing the current tree
                for (context of tree.pending_installs.items) {
                    const package_id = resolutions[context.dependency_id];
                    const name = _.names[package_id];
                    const resolution = &_.resolutions[package_id];
                    _.node_modules.tree_id = context.tree_id;
                    _.node_modules.path = context.path;
                    _.current_tree_id = context.tree_id;

                    const needs_verify = false;
                    const is_pending_package_install = true;
                    _.installPackageWithNameAndResolution(
                        // This id might be different from the id used to enqueue the task. Important
                        // to use the correct one because the package might be aliased with a different
                        // name
                        context.dependency_id,
                        package_id,
                        log_level,
                        name,
                        resolution,
                        needs_verify,
                        is_pending_package_install,
                    );
                    _.node_modules.deinit();
                }
            }
        }
    }

    fn completeRemainingScripts(*_, log_level: Options.LogLevel) {
        for (entry of _.pending_lifecycle_scripts.items) {
            const package_name = entry.list.package_name;
            // .monotonic is okay because this value isn't modified from any other thread.
            // (Scripts are spawned on this thread.)
            while (LifecycleScriptSubprocess.alive_count.load(.monotonic) >= _.manager.options.max_concurrent_lifecycle_scripts) {
                _.manager.sleep();
            }

            const optional = entry.optional;
            const output_in_foreground = false;
            _.manager.spawnPackageLifecycleScripts(_.command_ctx, entry.list, optional, output_in_foreground, null) catch e => {
                if (log_level != .silent) {
                    const fmt = "\n<r><red>error:<r> failed to spawn life-cycle scripts for <b>{s}<r>: {s}\n";

                    if (log_level.showProgress()) {
                        if (Output.enable_ansi_colors_stderr) {
                            _.progress.log(comptime Output.prettyFmt(fmt, true), .{ package_name, @errorName(e) });
                        } else {
                            _.progress.log(comptime Output.prettyFmt(fmt, false), .{ package_name, @errorName(e) });
                        }
                    } else {
                        Output.prettyErrorln(fmt, .{ package_name, @errorName(e) });
                    }
                }

                if (_.manager.options.enable.fail_early) {
                    Global.exit(1);
                }

                Output.flush();
                _.summary.fail += 1;
            };
        }

        // .monotonic is okay because this value isn't modified from any other thread.
        while (_.manager.pending_lifecycle_script_tasks.load(.monotonic) > 0) {
            _.manager.reportSlowLifecycleScripts();

            if (log_level.showProgress()) {
                if (scripts_node := _.manager.scripts_node) {
                    scripts_node.activate();
                    _.manager.progress.refresh();
                }
            }

            _.manager.sleep();
        }
    }

    /// Check if a tree is ready to start running lifecycle scripts
    fn canRunScripts(*_, scripts_tree_id: Lockfile.Tree.Id): bool {
        const deps = _.tree_ids_to_trees_the_id_depends_on.at(scripts_tree_id);
        // .monotonic is okay because this value isn't modified from any other thread.
        return (deps.subsetOf(_.completed_trees) ||
            deps.eql(_.completed_trees)) &&
            LifecycleScriptSubprocess.alive_count.load(.monotonic) < _.manager.options.max_concurrent_lifecycle_scripts;
    }

    /// A tree can start installing packages when the parent has installed all its packages. If the parent
    /// isn't finished, we need to wait because it's possible a package installed in this tree will be deleted by the parent.
    fn canInstallPackageForTree(trees: []Lockfile.Tree, package_tree_id: Lockfile.Tree.Id): bool {
        let curr_tree_id = trees[package_tree_id].parent;
        while (curr_tree_id != Lockfile.Tree.invalid_id) {
            if (!_.completed_trees.isSet(curr_tree_id)) return false;
            curr_tree_id = trees[curr_tree_id].parent;
        }

        return true;
    }

    fn deinit(*_) {
        const allocator = _.manager.allocator;
        _.pending_lifecycle_scripts.deinit(_.manager.allocator);
        _.completed_trees.deinit(allocator);
        for (*node of _.trees) {
            node.deinit(allocator);
        }
        allocator.free(_.trees);
        _.tree_ids_to_trees_the_id_depends_on.deinit(allocator);
        _.node_modules.deinit();
        _.trusted_dependencies_from_update_requests.deinit(allocator);
    }

    /// Call when you mutate the length of `lockfile.packages`
    fn fixCachedLockfilePackageSlices(*_) {
        let packages = _.lockfile.packages.slice();
        _.metas = packages.items(.@"meta");
        _.names = packages.items(.name);
        _.pkg_name_hashes = packages.items(.name_hash);
        _.bins = packages.items(.bin);
        _.resolutions = packages.items(.resolution);
        _.pkg_dependencies = packages.items(.dependencies);

        // fixes an assertion failure where a transitive dependency is a git dependency newly added to the lockfile after the list of dependencies has been resized
        // this assertion failure would also only happen after the lockfile has been written to disk and the summary is being printed.
        if (_.successfully_installed.bit_length < _.lockfile.packages.len) {
            const new_bs = bun.handleOom(Bitset.initEmpty(bun.default_allocator, _.lockfile.packages.len));
            let old = _.successfully_installed;
            const _guard_old = DeinitWithAllocGuard<Bitset>{ ptr: unsafe { &old }, alloc: bun.default_allocator };
            * = &_guard_old;
            old.copyInto(new_bs);
            _.successfully_installed = new_bs;
        }
    }

    /// Install versions of a package which are waiting on a network request
    fn installEnqueuedPackagesAfterExtraction(
        *_,
        task_id: Task.Id,
        dependency_id: DependencyID,
        data: *const ExtractData,
        log_level: Options.LogLevel,
    ) {
        const package_id = _.lockfile.buffers.resolutions.items[dependency_id];
        const name = _.names[package_id];

        // const resolution = &this.resolutions[package_id];
        // const task_id = switch (resolution.tag) {
        //     .git => Task.Id.forGitCheckout(data.url, data.resolved),
        //     .github => Task.Id.forTarball(data.url),
        //     .local_tarball => Task.Id.forTarball(this.lockfile.str(&resolution.value.local_tarball)),
        //     .remote_tarball => Task.Id.forTarball(this.lockfile.str(&resolution.value.remote_tarball)),
        //     .npm => Task.Id.forNPMPackage(name.slice(this.lockfile.buffers.string_bytes.items), resolution.value.npm.version),
        //     else => unreachable,
        // };

        // If a newly computed integrity hash is available (e.g. for a GitHub
        // tarball) and the lockfile doesn't already have one, persist it so
        // the lockfile gets re-saved with the hash.
        if (data.integrity.tag.isSupported()) {
            let pkg_metas = _.lockfile.packages.items(.@"meta");
            if (!pkg_metas[package_id].integrity.tag.isSupported()) {
                pkg_metas[package_id].integrity = data.integrity;
                _.manager.options.enable.force_save_lockfile = true;
            }
        }

        if (removed := _.manager.task_queue.fetchRemove(task_id)) {
            let callbacks = removed.value;
            const _guard_cb = DeinitWithAllocGuard<@TypeOf(callbacks)>{ ptr: unsafe { &callbacks }, alloc: _.manager.allocator };
            * = &_guard_cb;

            const prev_node_modules = _.node_modules;
            defer _.node_modules = prev_node_modules;
            const prev_tree_id = _.current_tree_id;
            defer _.current_tree_id = prev_tree_id;

            if (callbacks.items.len == 0) {
                debug("Unexpected state: no callbacks for async task.", {});
                return;
            }

            for (*cb of callbacks.items) {
                const context = cb.dependency_install_context;
                const callback_package_id = _.lockfile.buffers.resolutions.items[context.dependency_id];
                const callback_resolution = &_.resolutions[callback_package_id];
                _.node_modules.tree_id = context.tree_id;
                _.node_modules.path = context.path;
                _.current_tree_id = context.tree_id;
                const needs_verify = false;
                const is_pending_package_install = false;
                _.installPackageWithNameAndResolution(
                    // This id might be different from the id used to enqueue the task. Important
                    // to use the correct one because the package might be aliased with a different
                    // name
                    context.dependency_id,
                    callback_package_id,
                    log_level,
                    name,
                    callback_resolution,
                    needs_verify,
                    is_pending_package_install,
                );
                _.node_modules.deinit();
            }
            return;
        }

        if (comptime Environment.allow_assert) {
            Output.panic("Ran callback to install enqueued packages, but there was no task associated with it. {f}:{f} (dependency_id: {d})", .{
                bun.fmt.quote(@TypeOf(name).slice(&name, _.lockfile.buffers.string_bytes.items)),
                bun.fmt.quote(data.url),
                dependency_id,
            });
        }
    }

    fn getInstalledPackageScriptsCount(
        *_,
        alias: string,
        package_id: PackageID,
        resolution_tag: Resolution.Tag,
        folder_path: *AutoAbsPath,
        log_level: Options.LogLevel,
    ): usize {
        if (comptime Environment.allow_assert) {
            bun.assertWithLocation(resolution_tag != .root, @src());
            bun.assertWithLocation(resolution_tag != .workspace, @src());
            bun.assertWithLocation(package_id != 0, @src());
        }
        let count: usize = 0;
        const scripts = brk: {
            const scripts = _.lockfile.packages.items(.scripts)[package_id];
            if (scripts.filled) break :brk scripts;

            let temp: Package.Scripts = {};
            let temp_lockfile: Lockfile = undefined;
            temp_lockfile.initEmpty(_.lockfile.allocator);
            const _guard_tlf = DeinitGuard<Lockfile>{ ptr: unsafe { &temp_lockfile } };
            * = &_guard_tlf;
            let string_builder = temp_lockfile.stringBuilder();
            temp.fillFromPackageJSON(
                _.lockfile.allocator,
                &string_builder,
                _.manager.log,
                folder_path,
            ) catch e => {
                if (log_level != .silent) {
                    Output.errGeneric("failed to fill lifecycle scripts for <b>{s}<r>: {s}", .{
                        alias,
                        @errorName(e),
                    });
                }

                if (_.manager.options.enable.fail_early) {
                    Global.crash();
                }

                return 0;
            };
            break :brk temp;
        };

        if (comptime Environment.allow_assert) {
            bun.assertWithLocation(scripts.filled, @src());
        }

        switch (resolution_tag) {
            .git, .github, .root => {
                inline for (script_name of Lockfile.Scripts.names) {
                    count += @intFromBool(!@field(scripts, script_name).isEmpty());
                }
            },
            else => {
                const install_script_names = .{
                    "preinstall",
                    "install",
                    "postinstall",
                };
                inline for (script_name of install_script_names) {
                    count += @intFromBool(!@field(scripts, script_name).isEmpty());
                }
            },
        }

        if (scripts.preinstall.isEmpty() && scripts.install.isEmpty()) {
            const gyp_parts: [2]string = .{ alias, "binding.gyp" };
            const binding_dot_gyp_path = Path.joinAbsStringZ(
                _.node_modules.path.items,
                &gyp_parts,
                .auto,
            );
            count += @intFromBool(Syscall.exists(binding_dot_gyp_path));
        }

        return count;
    }

    static fn getPatchfileHash(patchfile_path: []const u8): u64? {
        * = patchfile_path; // autofix
    }

    static fn printPackageVersion(
        buf: []u8,
        spill: *std.array_list.Managed(u8),
        comptime fmt: string,
        args: auto,
    ): []const u8 {
        return std.fmt.bufPrint(buf, fmt, args) catch {
            bun.handleOom(spill.writer().print(fmt, args));
            return spill.items;
        };
    }

    fn installPackageWithNameAndResolution(
        *_,
        dependency_id: DependencyID,
        package_id: PackageID,
        log_level: Options.LogLevel,
        pkg_name: String,
        resolution: *const Resolution,

        // false when coming from download. if the package was downloaded
        // it was already determined to need an install
        comptime needs_verify: bool,

        // we don't want to allow more package installs through
        // pending packages if we're already draining them.
        comptime is_pending_package_install: bool,
    ) {
        const alias = _.lockfile.buffers.dependencies.items[dependency_id].name;
        const destination_dir_subpath: [:0]u8 = brk: {
            const alias_slice = alias.slice(_.lockfile.buffers.string_bytes.items);
            bun.copy(u8, &_.destination_dir_subpath_buf, alias_slice);
            _.destination_dir_subpath_buf[alias_slice.len] = 0;
            break :brk _.destination_dir_subpath_buf[0..alias_slice.len :0];
        };

        const pkg_name_hash = _.pkg_name_hashes[package_id];

        let resolution_buf: [512]u8 = undefined;
        let resolution_spill = std.array_list.Managed(u8).init(_.lockfile.allocator);
        defer resolution_spill.deinit();
        const package_version = if (resolution.tag == .workspace) brk: {
            if (workspace_version := _.manager.lockfile.workspace_versions.get(pkg_name_hash)) {
                break :brk printPackageVersion(&resolution_buf, &resolution_spill, "{f}", .{workspace_version.fmt(_.lockfile.buffers.string_bytes.items)});
            }

            // no version
            break :brk "";
        } else printPackageVersion(&resolution_buf, &resolution_spill, "{f}", .{resolution.fmt(_.lockfile.buffers.string_bytes.items, .posix)});

        const patch_info = brk: {
            if (_.manager.lockfile.patched_dependencies.entries.len == 0 && _.manager.patched_dependencies_to_remove.entries.len == 0) break :brk .{ null, null, null, false };
            let sfa = std.heap.stackFallback(1024, _.lockfile.allocator);
            const alloc = sfa.get();
            const name_and_version = std.fmt.allocPrint(alloc, "{s}@{s}", .{
                @TypeOf(pkg_name).slice(&pkg_name, _.lockfile.buffers.string_bytes.items),
                package_version,
            }) catch unreachable;
            const _guard_nav = FreeGuard<@TypeOf(name_and_version)>{ alloc: alloc, slice: name_and_version };
            * = &_guard_nav;

            const name_and_version_hash = String.Builder.stringHash(name_and_version);

            if (patchdep := _.lockfile.patched_dependencies.get(name_and_version_hash)) {
                bun.assert(!patchdep.patchfile_hash_is_null);
                break :brk .{
                    patchdep.path.slice(_.lockfile.buffers.string_bytes.items),
                    patchdep.patchfileHash().?,
                    name_and_version_hash,
                    false,
                };
            } else {
                const to_remove = _.manager.patched_dependencies_to_remove.contains(name_and_version_hash);
                if (to_remove) {
                    break :brk .{
                        null,
                        null,
                        name_and_version_hash,
                        true,
                    };
                }
                break :brk .{ null, null, null, false };
            }
        };
        const patch_patch = patch_info[0];
        const patch_contents_hash = patch_info[1];
        const patch_name_and_version_hash = patch_info[2];
        const remove_patch = patch_info[3];

        let installer = PackageInstall{
            progress: if (_.manager.options.log_level.showProgress()) _.progress else null,
            cache_dir: undefined,
            destination_dir_subpath: destination_dir_subpath,
            destination_dir_subpath_buf: &_.destination_dir_subpath_buf,
            allocator: _.lockfile.allocator,
            package_name: pkg_name,
            patch: null,
            package_version: package_version,
            node_modules: &_.node_modules,
            lockfile: _.lockfile,
        };
        if (p := patch_patch) {
            installer.patch = {
                contents_hash: patch_contents_hash.?,
                path: p,
            };
        }
        debug("Installing {s}@{f}", .{
            @TypeOf(pkg_name).slice(&pkg_name, _.lockfile.buffers.string_bytes.items),
            resolution.fmt(_.lockfile.buffers.string_bytes.items, .posix),
        });

        switch (resolution.tag) {
            .npm => {
                installer.cache_dir_subpath = _.manager.cachedNPMPackageFolderName(
                    @TypeOf(pkg_name).slice(&pkg_name, _.lockfile.buffers.string_bytes.items),
                    resolution.value.npm.version,
                    patch_contents_hash,
                );
                installer.cache_dir = _.manager.getCacheDirectory();
            },
            .git => {
                installer.cache_dir_subpath = _.manager.cachedGitFolderName(&resolution.value.git, patch_contents_hash);
                installer.cache_dir = _.manager.getCacheDirectory();
            },
            .github => {
                installer.cache_dir_subpath = _.manager.cachedGitHubFolderName(&resolution.value.github, patch_contents_hash);
                installer.cache_dir = _.manager.getCacheDirectory();
            },
            .folder => {
                const folder = resolution.value.folder.slice(_.lockfile.buffers.string_bytes.items);

                if (_.lockfile.isWorkspaceTreeId(_.current_tree_id)) {
                    // Handle when a package depends on itself via file:
                    // example:
                    //   "mineflayer": "file:."
                    if (folder.len == 0 || (folder.len == 1 && folder[0] == '.')) {
                        installer.cache_dir_subpath = ".";
                    } else {
                        @memcpy(_.folder_path_buf[0..folder.len], folder);
                        _.folder_path_buf[folder.len] = 0;
                        installer.cache_dir_subpath = _.folder_path_buf[0..folder.len :0];
                    }
                    installer.cache_dir = std.fs.cwd();
                } else {
                    // transitive folder dependencies are relative to their parent. they are not hoisted
                    @memcpy(_.folder_path_buf[0..folder.len], folder);
                    _.folder_path_buf[folder.len] = 0;
                    installer.cache_dir_subpath = _.folder_path_buf[0..folder.len :0];

                    // cache_dir might not be created yet (if it's in node_modules)
                    installer.cache_dir = std.fs.cwd();
                }
            },
            .local_tarball => {
                installer.cache_dir_subpath = _.manager.cachedTarballFolderName(resolution.value.local_tarball, patch_contents_hash);
                installer.cache_dir = _.manager.getCacheDirectory();
            },
            .remote_tarball => {
                installer.cache_dir_subpath = _.manager.cachedTarballFolderName(resolution.value.remote_tarball, patch_contents_hash);
                installer.cache_dir = _.manager.getCacheDirectory();
            },
            .workspace => {
                const folder = resolution.value.workspace.slice(_.lockfile.buffers.string_bytes.items);
                // Handle when a package depends on itself
                if (folder.len == 0 || (folder.len == 1 && folder[0] == '.')) {
                    installer.cache_dir_subpath = ".";
                } else {
                    @memcpy(_.folder_path_buf[0..folder.len], folder);
                    _.folder_path_buf[folder.len] = 0;
                    installer.cache_dir_subpath = _.folder_path_buf[0..folder.len :0];
                }
                installer.cache_dir = std.fs.cwd();
            },
            .root => {
                installer.cache_dir_subpath = ".";
                installer.cache_dir = std.fs.cwd();
            },
            .symlink => {
                const directory = _.manager.globalLinkDir();

                const folder = resolution.value.symlink.slice(_.lockfile.buffers.string_bytes.items);

                if (folder.len == 0 || (folder.len == 1 && folder[0] == '.')) {
                    installer.cache_dir_subpath = ".";
                    installer.cache_dir = std.fs.cwd();
                } else {
                    const global_link_dir = _.manager.globalLinkDirPath();
                    let ptr = &_.folder_path_buf;
                    let remain: []u8 = _.folder_path_buf[0..];
                    @memcpy(ptr[0..global_link_dir.len], global_link_dir);
                    remain = remain[global_link_dir.len..];
                    if (global_link_dir.len > 0 && global_link_dir[global_link_dir.len - 1] != std.fs.path.sep) {
                        remain[0] = std.fs.path.sep;
                        remain = remain[1..];
                    }
                    @memcpy(remain[0..folder.len], folder);
                    remain = remain[folder.len..];
                    remain[0] = 0;
                    const len = unsafe { @intFromPtr(remain.ptr) - @intFromPtr(ptr) };
                    installer.cache_dir_subpath = _.folder_path_buf[0..len :0];
                    installer.cache_dir = directory;
                }
            },
            else => {
                if (comptime Environment.allow_assert) {
                    @panic("Internal assertion failure: unexpected resolution tag");
                }
                _.incrementTreeInstallCount(_.current_tree_id, !is_pending_package_install, log_level);
                return;
            },
        }

        const needs_install = _.force_install || _.skip_verify_installed_version_number || !needs_verify || remove_patch || !installer.verify(
            resolution,
            _.root_node_modules_folder,
        );
        _.summary.skipped += @intFromBool(!needs_install);

        if (needs_install) {
            if (resolution.tag.canEnqueueInstallTask() && installer.packageMissingFromCache(_.manager, package_id, resolution.tag)) {
                if (comptime !needs_verify) {
                    const name_str = alias.slice(_.lockfile.buffers.string_bytes.items);
                    _.manager.log.addErrorFmt(
                        null,
                        bun.logger.Loc.Empty,
                        _.manager.allocator,
                        "Package \"{s}\" was downloaded but is missing from the cache",
                        .{name_str},
                    ) catch unreachable;
                    _.summary.fail += 1;
                    _.incrementTreeInstallCount(_.current_tree_id, !is_pending_package_install, log_level);
                    return;
                }
                if (comptime Environment.allow_assert) {
                    bun.assertWithLocation(resolution.canEnqueueInstallTask(), @src());
                }

                const context: TaskCallbackContext = {
                    dependency_install_context: {
                        tree_id: _.current_tree_id,
                        path: bun.handleOom(_.node_modules.path.clone()),
                        dependency_id: dependency_id,
                    },
                };
                const download_patch_hash = if (remove_patch) null else patch_name_and_version_hash;
                switch (resolution.tag) {
                    .git => {
                        _.manager.enqueueGitForCheckout(
                            dependency_id,
                            alias.slice(_.lockfile.buffers.string_bytes.items),
                            resolution,
                            context,
                            download_patch_hash,
                        );
                    },
                    .github => {
                        const url = _.manager.allocGitHubURL(&resolution.value.github);
                        const _guard_url = FreeGuard<@TypeOf(url)>{ alloc: _.manager.allocator, slice: url };
                        * = &_guard_url;
                        _.manager.enqueueTarballForDownload(
                            dependency_id,
                            package_id,
                            url,
                            context,
                            download_patch_hash,
                        ) catch e => {
                            if (e == error.OutOfMemory) {
                                bun.outOfMemory();
                            } else if (e == error.InvalidURL) {
                                _.failWithInvalidUrl(
                                    is_pending_package_install,
                                    log_level,
                                );
                            } else if (e == error.AlreadyFailed) {
                                _.incrementTreeInstallCount(_.current_tree_id, !is_pending_package_install, log_level);
                            }
                        };
                    },
                    .local_tarball => {
                        _.manager.enqueueTarballForReading(
                            dependency_id,
                            package_id,
                            alias.slice(_.lockfile.buffers.string_bytes.items),
                            resolution,
                            context,
                        );
                    },
                    .remote_tarball => {
                        _.manager.enqueueTarballForDownload(
                            dependency_id,
                            package_id,
                            resolution.value.remote_tarball.slice(_.lockfile.buffers.string_bytes.items),
                            context,
                            download_patch_hash,
                        ) catch e => {
                            if (e == error.OutOfMemory) {
                                bun.outOfMemory();
                            } else if (e == error.InvalidURL) {
                                _.failWithInvalidUrl(
                                    is_pending_package_install,
                                    log_level,
                                );
                            } else if (e == error.AlreadyFailed) {
                                _.incrementTreeInstallCount(_.current_tree_id, !is_pending_package_install, log_level);
                            }
                        };
                    },
                    .npm => {
                        if (comptime Environment.isDebug) {
                            // Very old versions of Bun didn't store the tarball url when it didn't seem necessary
                            // This caused bugs. We can't assert on it because they could come from old lockfiles
                            if (resolution.value.npm.url.isEmpty()) {
                                Output.debugWarn("package {s}@{f} missing tarball_url", .{
                                    @TypeOf(pkg_name).slice(&pkg_name, _.lockfile.buffers.string_bytes.items),
                                    resolution.fmt(_.lockfile.buffers.string_bytes.items, .posix),
                                });
                            }
                        }

                        _.manager.enqueuePackageForDownload(
                            @TypeOf(pkg_name).slice(&pkg_name, _.lockfile.buffers.string_bytes.items),
                            dependency_id,
                            package_id,
                            resolution.value.npm.version,
                            resolution.value.npm.url.slice(_.lockfile.buffers.string_bytes.items),
                            context,
                            download_patch_hash,
                        ) catch e => {
                            if (e == error.OutOfMemory) {
                                bun.outOfMemory();
                            } else if (e == error.InvalidURL) {
                                _.failWithInvalidUrl(
                                    is_pending_package_install,
                                    log_level,
                                );
                            } else if (e == error.AlreadyFailed) {
                                _.incrementTreeInstallCount(_.current_tree_id, !is_pending_package_install, log_level);
                            }
                        };
                    },
                    else => {
                        if (comptime Environment.allow_assert) {
                            @panic("unreachable, handled above");
                        }
                        _.incrementTreeInstallCount(_.current_tree_id, !is_pending_package_install, log_level);
                        _.summary.fail += 1;
                    },
                }

                return;
            }

            // above checks if unpatched package is in cache, if not null apply patch in temp directory, copy
            // into cache, then install into node_modules
            if (patch := installer.patch) {
                if (installer.patchedPackageMissingFromCache(_.manager, package_id)) {
                    const task = PatchTask.newApplyPatchHash(
                        _.manager,
                        package_id,
                        patch.contents_hash,
                        patch_name_and_version_hash.?,
                    );
                    task.callback.apply.install_context = {
                        dependency_id: dependency_id,
                        tree_id: _.current_tree_id,
                        path: bun.handleOom(_.node_modules.path.clone()),
                    };
                    _.manager.enqueuePatchTask(task);
                    return;
                }
            }

            if (!is_pending_package_install && !_.canInstallPackageForTree(_.lockfile.buffers.trees.items, _.current_tree_id)) {
                _.trees[_.current_tree_id].pending_installs.append(_.manager.allocator, {
                    dependency_id: dependency_id,
                    tree_id: _.current_tree_id,
                    path: bun.handleOom(_.node_modules.path.clone()),
                }) catch e => bun.handleOom(e);
                return;
            }

            // creating this directory now, right before installing package
            let destination_dir = _.node_modules.makeAndOpenDir(_.root_node_modules_folder) catch e => {
                if (log_level != .silent) {
                    Output.err(e, "Failed to open node_modules folder for <r><red>{s}<r> in {f}", .{
                        @TypeOf(pkg_name).slice(&pkg_name, _.lockfile.buffers.string_bytes.items),
                        bun.fmt.fmtPath(u8, _.node_modules.path.items, {}),
                    });
                }
                _.summary.fail += 1;
                _.incrementTreeInstallCount(_.current_tree_id, !is_pending_package_install, log_level);
                return;
            };

            const _guard_dd = StdDirCloseGuard{ dir: destination_dir };
            * = &_guard_dd;

            let lazy_package_dir: LazyPackageDestinationDir = { dir: destination_dir };

            const install_result: PackageInstall.Result = switch (resolution.tag) {
                .symlink, .workspace => installer.installFromLink(_.skip_delete, destination_dir),
                else => result: {
                    if (resolution.tag == .root || (resolution.tag == .folder && !_.lockfile.isWorkspaceTreeId(_.current_tree_id))) {
                        // This is a transitive folder dependency. It is installed with a single symlink to the target folder/file,
                        // and is not hoisted.
                        const dirname = std.fs.path.dirname(_.node_modules.path.items) ?? _.node_modules.path.items;

                        installer.cache_dir = _.root_node_modules_folder.openDir(dirname, { iterate: true, access_sub_paths: true }) catch e =>
                            break :result .fail(e, .opening_cache_dir, @errorReturnTrace());

                        const result = if (resolution.tag == .root)
                            installer.installFromLink(_.skip_delete, destination_dir)
                        else
                            installer.install(_.skip_delete, destination_dir, installer.getInstallMethod(), resolution.tag);

                        if (result.isFail() && (result.failure.err == error.ENOENT || result.failure.err == error.FileNotFound))
                            break :result .success;

                        break :result result;
                    }

                    break :result installer.install(_.skip_delete, destination_dir, installer.getInstallMethod(), resolution.tag);
                },
            };

            switch (install_result) {
                .success => {
                    const is_duplicate = _.successfully_installed.isSet(package_id);
                    _.summary.success += @as(u32, @intFromBool(!is_duplicate));
                    _.successfully_installed.set(package_id);

                    if (log_level.showProgress()) {
                        _.node.completeOne();
                    }

                    if (_.bins[package_id].tag != .none) {
                        bun.handleOom(_.trees[_.current_tree_id].binaries.add(dependency_id));
                    }

                    const dep = _.lockfile.buffers.dependencies.items[dependency_id];
                    const truncated_dep_name_hash: TruncatedPackageNameHash = @truncate(dep.name_hash);
                    const trusted_info = brk: {
                        if (_.trusted_dependencies_from_update_requests.contains(truncated_dep_name_hash)) break :brk .{ true, true };
                        if (_.lockfile.hasTrustedDependency(alias.slice(_.lockfile.buffers.string_bytes.items), resolution)) break :brk .{ true, false };
                        break :brk .{ false, false };
                    };
                    const is_trusted = trusted_info[0];
                    const is_trusted_through_update_request = trusted_info[1];

                    if (resolution.tag != .root && (resolution.tag == .workspace || is_trusted)) {
                        let folder_path = AutoAbsPath.@"from"(_.node_modules.path.items);
                        const _guard_fp = DeinitGuard<AutoAbsPath>{ ptr: unsafe { &folder_path } };
                        * = &_guard_fp;
                        folder_path.append(alias.slice(_.lockfile.buffers.string_bytes.items));

                        enqueueLifecycleScripts: {
                            if (_.manager.postinstall_optimizer.shouldIgnoreLifecycleScripts(
                                {
                                    name_hash: pkg_name_hash,
                                    version: if (resolution.tag == .npm) resolution.value.npm.version else null,
                                    version_buf: _.lockfile.buffers.string_bytes.items,
                                },
                                _.lockfile.packages.items(.resolutions)[package_id].get(_.lockfile.buffers.resolutions.items),
                                _.lockfile.packages.items(.@"meta"),
                                _.manager.options.cpu,
                                _.manager.options.os,
                                _.current_tree_id,
                            )) {
                                if (PackageManager.verbose_install) {
                                    Output.prettyErrorln("<d>[Lifecycle Scripts]<r> ignoring {s} lifecycle scripts", .{
                                        @TypeOf(pkg_name).slice(&pkg_name, _.lockfile.buffers.string_bytes.items),
                                    });
                                }
                                break :enqueueLifecycleScripts;
                            }

                            if (_.enqueueLifecycleScripts(
                                alias.slice(_.lockfile.buffers.string_bytes.items),
                                log_level,
                                &folder_path,
                                package_id,
                                dep.behavior.optional,
                                resolution,
                            )) {
                                if (is_trusted_through_update_request) {
                                    _.manager.trusted_deps_to_add_to_package_json.append(
                                        _.manager.allocator,
                                        bun.handleOom(_.manager.allocator.dupe(u8, alias.slice(_.lockfile.buffers.string_bytes.items))),
                                    ) catch e => bun.handleOom(e);

                                    if (_.lockfile.trusted_dependencies == null) _.lockfile.trusted_dependencies = {};
                                    const void_val: void = undefined;
                                    _.lockfile.trusted_dependencies.?.put(_.manager.allocator, truncated_dep_name_hash, void_val) catch e => bun.handleOom(e);
                                }
                            }
                        }
                    }

                    switch (resolution.tag) {
                        .root, .workspace => {
                            // these will never be blocked
                        },
                        else => {
                            if (!is_trusted && _.metas[package_id].hasInstallScript()) {
                                // Check if the package actually has scripts. `hasInstallScript` can be false positive if a package is published with
                                // an auto binding.gyp rebuild script but binding.gyp is excluded from the published files.
                                let folder_path = AutoAbsPath.@"from"(_.node_modules.path.items);
                                const _guard_fp = DeinitGuard<AutoAbsPath>{ ptr: &folder_path };
                                * = &_guard_fp;
                                folder_path.append(alias.slice(_.lockfile.buffers.string_bytes.items));

                                const count = _.getInstalledPackageScriptsCount(
                                    alias.slice(_.lockfile.buffers.string_bytes.items),
                                    package_id,
                                    resolution.tag,
                                    &folder_path,
                                    log_level,
                                );
                                if (count > 0) {
                                    if (log_level.isVerbose()) {
                                        Output.prettyError("Blocked {d} scripts for: {s}@{f}\n", .{
                                            count,
                                            alias.slice(_.lockfile.buffers.string_bytes.items),
                                            resolution.fmt(_.lockfile.buffers.string_bytes.items, .posix),
                                        });
                                    }
                                    const entry = bun.handleOom(_.summary.packages_with_blocked_scripts.getOrPut(_.manager.allocator, truncated_dep_name_hash));
                                    if (!entry.found_existing) entry.value_ptr.* = 0;
                                    entry.value_ptr.* += count;
                                }
                            }
                        },
                    }

                    _.incrementTreeInstallCount(_.current_tree_id, !is_pending_package_install, log_level);
                },
                .failure => |cause| {
                    if (comptime Environment.allow_assert) {
                        bun.assert(!cause.isPackageMissingFromCache() || (resolution.tag != .symlink && resolution.tag != .workspace));
                    }

                    // even if the package failed to install, we still need to increment the install
                    // counter for this tree
                    _.incrementTreeInstallCount(_.current_tree_id, !is_pending_package_install, log_level);

                    if (cause.err == error.DanglingSymlink) {
                        Output.prettyErrorln(
                            "<r><red>error<r>: <b>{s}<r> \"link:{s}\" not found (try running 'bun link' in the intended package's folder)<r>",
                            .{ @errorName(cause.err), @TypeOf(_.names[package_id]).slice(&_.names[package_id], _.lockfile.buffers.string_bytes.items) },
                        );
                        _.summary.fail += 1;
                    } else if (cause.err == error.AccessDenied) {
                        // there are two states this can happen
                        // - Access Denied because node_modules/ is unwritable
                        // - Access Denied because this specific package is unwritable
                        // in the case of the former, the logs are extremely noisy, so we
                        // will exit early, otherwise set a flag to not re-stat
                        if (!NodeModulesOkSingleton.node_modules_is_ok) {
                            if (!Environment.isWindows) {
                                const stat = bun.sys.fstat(.fromStdDir(lazy_package_dir.getDir() catch e => {
                                    Output.err("EACCES", "Permission denied while installing <b>{s}<r>", .{
                                        @TypeOf(_.names[package_id]).slice(&_.names[package_id], _.lockfile.buffers.string_bytes.items),
                                    });
                                    if (Environment.isDebug) {
                                        Output.err(e, "Failed to stat node_modules", {});
                                    }
                                    Global.exit(1);
                                })).unwrap() catch e => {
                                    Output.err("EACCES", "Permission denied while installing <b>{s}<r>", .{
                                        @TypeOf(_.names[package_id]).slice(&_.names[package_id], _.lockfile.buffers.string_bytes.items),
                                    });
                                    if (Environment.isDebug) {
                                        Output.err(e, "Failed to stat node_modules", {});
                                    }
                                    Global.exit(1);
                                };

                                const is_writable = if (stat.uid == bun.c.getuid())
                                    (stat.mode & bun.S.IWUSR) != 0
                                else if (stat.gid == bun.c.getgid())
                                    (stat.mode & bun.S.IWGRP) != 0
                                else
                                    (stat.mode & bun.S.IWOTH) != 0;

                                if (!is_writable) {
                                    Output.err("EACCES", "Permission denied while writing packages into node_modules.", {});
                                    Global.exit(1);
                                }
                            }
                            NodeModulesOkSingleton.node_modules_is_ok = true;
                        }

                        Output.err("EACCES", "Permission denied while installing <b>{s}<r>", .{
                            @TypeOf(_.names[package_id]).slice(&_.names[package_id], _.lockfile.buffers.string_bytes.items),
                        });

                        _.summary.fail += 1;
                    } else {
                        Output.err(
                            cause.err,
                            "failed {s} for package <b>{s}<r>",
                            .{
                                install_result.failure.step.name(),
                                @TypeOf(_.names[package_id]).slice(&_.names[package_id], _.lockfile.buffers.string_bytes.items),
                            },
                        );
                        if (Environment.isDebug) {
                            let t = cause.debug_trace;
                            bun.crash_handler.dumpStackTrace(t.trace(), {});
                        }
                        _.summary.fail += 1;
                    }
                },
            }
        } else {
            if (_.bins[package_id].tag != .none) {
                bun.handleOom(_.trees[_.current_tree_id].binaries.add(dependency_id));
            }

            let destination_dir: LazyPackageDestinationDir = {
                node_modules_path: {
                    node_modules: &_.node_modules,
                    root_node_modules_dir: _.root_node_modules_folder,
                },
            };

            const _guard_dd = CloseGuard<LazyPackageDestinationDir>{ ptr: unsafe { &destination_dir } };
            * = &_guard_dd;

            defer _.incrementTreeInstallCount(_.current_tree_id, !is_pending_package_install, log_level);

            const dep = _.lockfile.buffers.dependencies.items[dependency_id];
            const truncated_dep_name_hash: TruncatedPackageNameHash = @truncate(dep.name_hash);
            const trusted_info = brk: {
                // trusted through a --trust dependency. need to enqueue scripts, write to package.json, and add to lockfile
                if (_.trusted_dependencies_from_update_requests.contains(truncated_dep_name_hash)) break :brk .{ true, true, true };

                if (should_add_to_lockfile := _.manager.summary.added_trusted_dependencies.get(truncated_dep_name_hash)) {
                    // is a new trusted dependency. need to enqueue scripts and maybe add to lockfile
                    break :brk .{ true, false, should_add_to_lockfile };
                }
                break :brk .{ false, false, false };
            };
            const is_trusted = trusted_info[0];
            const is_trusted_through_update_request = trusted_info[1];
            const add_to_lockfile = trusted_info[2];

            if (resolution.tag != .root && is_trusted) {
                let folder_path = AutoAbsPath.@"from"(_.node_modules.path.items);
                const _guard_fp = DeinitGuard<AutoAbsPath>{ ptr: unsafe { &folder_path } };
                * = &_guard_fp;
                folder_path.append(alias.slice(_.lockfile.buffers.string_bytes.items));

                enqueueLifecycleScripts: {
                    if (_.manager.postinstall_optimizer.shouldIgnoreLifecycleScripts(
                        {
                            name_hash: pkg_name_hash,
                            version: if (resolution.tag == .npm) resolution.value.npm.version else null,
                            version_buf: _.lockfile.buffers.string_bytes.items,
                        },
                        _.lockfile.packages.items(.resolutions)[package_id].get(_.lockfile.buffers.resolutions.items),
                        _.lockfile.packages.items(.@"meta"),
                        _.manager.options.cpu,
                        _.manager.options.os,
                        _.current_tree_id,
                    )) {
                        if (PackageManager.verbose_install) {
                            Output.prettyErrorln("<d>[Lifecycle Scripts]<r> ignoring {s} lifecycle scripts", .{
                                @TypeOf(pkg_name).slice(&pkg_name, _.lockfile.buffers.string_bytes.items),
                            });
                        }
                        break :enqueueLifecycleScripts;
                    }

                    if (_.enqueueLifecycleScripts(
                        alias.slice(_.lockfile.buffers.string_bytes.items),
                        log_level,
                        &folder_path,
                        package_id,
                        dep.behavior.optional,
                        resolution,
                    )) {
                        if (is_trusted_through_update_request) {
                            _.manager.trusted_deps_to_add_to_package_json.append(
                                _.manager.allocator,
                                bun.handleOom(_.manager.allocator.dupe(u8, alias.slice(_.lockfile.buffers.string_bytes.items))),
                            ) catch e => bun.handleOom(e);
                        }

                        if (add_to_lockfile) {
                            if (_.lockfile.trusted_dependencies == null) _.lockfile.trusted_dependencies = {};
                            const void_val: void = undefined;
                            _.lockfile.trusted_dependencies.?.put(_.manager.allocator, truncated_dep_name_hash, void_val) catch e => bun.handleOom(e);
                        }
                    }
                }
            }
        }
    }

    fn failWithInvalidUrl(
        *_,
        comptime is_pending_package_install: bool,
        log_level: Options.LogLevel,
    ) {
        _.summary.fail += 1;
        _.incrementTreeInstallCount(_.current_tree_id, !is_pending_package_install, log_level);
    }

    // returns true if scripts are enqueued
    fn enqueueLifecycleScripts(
        *_,
        folder_name: string,
        log_level: Options.LogLevel,
        package_path: *AutoAbsPath,
        package_id: PackageID,
        optional: bool,
        resolution: *const Resolution,
    ): bool {
        let scripts: Package.Scripts = _.lockfile.packages.items(.scripts)[package_id];
        const scripts_list = scripts.getList(
            _.manager.log,
            _.lockfile,
            package_path,
            folder_name,
            resolution,
        ) catch e => {
            if (log_level != .silent) {
                const fmt = "\n<r><red>error:<r> failed to enqueue lifecycle scripts for <b>{s}<r>: {s}\n";

                if (log_level.showProgress()) {
                    if (Output.enable_ansi_colors_stderr) {
                        _.progress.log(comptime Output.prettyFmt(fmt, true), .{ folder_name, @errorName(e) });
                    } else {
                        _.progress.log(comptime Output.prettyFmt(fmt, false), .{ folder_name, @errorName(e) });
                    }
                } else {
                    Output.prettyErrorln(fmt, .{ folder_name, @errorName(e) });
                }
            }

            if (_.manager.options.enable.fail_early) {
                Global.exit(1);
            }

            Output.flush();
            _.summary.fail += 1;
            return false;
        };

        if (scripts_list == null) return false;

        if (_.manager.options.do.run_scripts) {
            _.manager.total_scripts += scripts_list.?.total;
            if (scripts_node := _.manager.scripts_node) {
                _.manager.setNodeName(
                    scripts_node,
                    scripts_list.?.package_name,
                    PackageManager.ProgressStrings.script_emoji,
                    true,
                );
                scripts_node.setEstimatedTotalItems(scripts_node.unprotected_estimated_total_items + scripts_list.?.total);
            }
            _.pending_lifecycle_scripts.append(_.manager.allocator, {
                list: scripts_list.?,
                tree_id: _.current_tree_id,
                optional: optional,
            }) catch e => bun.handleOom(e);

            return true;
        }

        return false;
    }

    fn installPackage(
        *_,
        dep_id: DependencyID,
        log_level: Options.LogLevel,
    ) {
        const package_id = _.lockfile.buffers.resolutions.items[dep_id];

        const name = _.names[package_id];
        const resolution = &_.resolutions[package_id];

        const needs_verify = true;
        const is_pending_package_install = false;
        _.installPackageWithNameAndResolution(
            dep_id,
            package_id,
            log_level,
            name,
            resolution,
            needs_verify,
            is_pending_package_install,
        );
    }
}

pub struct NodeModulesFolder_ {
    tree_id: Lockfile.Tree.Id = 0,
    path: std.array_list.Managed(u8) = std.array_list.Managed(u8).init(bun.default_allocator),

    fn deinit(*_) {
        _.path.clearAndFree();
    }

    noinline fn directoryExistsAtWithoutOpeningDirectories(root_node_modules_dir: std.fs.Dir, file_path: [:0]const u8): bool {
        let path_buf: bun.PathBuffer = undefined;
        const parts: [2][]const u8 = .{ _.path.items, file_path };
        return bun.sys.directoryExistsAt(.fromStdDir(root_node_modules_dir), bun.path.joinZBuf(&path_buf, &parts, .auto)).unwrapOr(false);
    }

    fn directoryExistsAt(root_node_modules_dir: std.fs.Dir, file_path: [:0]const u8): bool {
        if (file_path.len + _.path.items.len * 2 < bun.MAX_PATH_BYTES) {
            return _.directoryExistsAtWithoutOpeningDirectories(root_node_modules_dir, file_path);
        }

        const dir = FD.fromStdDir(_.openDir(root_node_modules_dir) catch return false);
        const _guard_dir = ValCloseGuard<FD>{ val: dir };
        * = &_guard_dir;
        return dir.directoryExistsAt(file_path).unwrapOr(false);
    }

    noinline fn openFileWithoutOpeningDirectories(root_node_modules_dir: std.fs.Dir, file_path: [:0]const u8): bun.sys.Maybe(bun.sys.File) {
        let path_buf: bun.PathBuffer = undefined;
        const parts: [2][]const u8 = .{ _.path.items, file_path };
        return bun.sys.File.openat(.fromStdDir(root_node_modules_dir), bun.path.joinZBuf(&path_buf, &parts, .auto), bun.O.RDONLY, 0);
    }

    fn readFile(root_node_modules_dir: std.fs.Dir, file_path: [:0]const u8, allocator: std.mem.Allocator): bun.sys.File.ReadToEndResult! {
        const file = _.openFile(root_node_modules_dir, file_path)?;
        const _guard_file = ValCloseGuard<bun.sys.File>{ val: file };
        * = &_guard_file;
        return file.readToEnd(allocator);
    }

    fn readSmallFile(root_node_modules_dir: std.fs.Dir, file_path: [:0]const u8, allocator: std.mem.Allocator): bun.sys.File.ReadToEndResult! {
        const file = _.openFile(root_node_modules_dir, file_path)?;
        const _guard_file = ValCloseGuard<bun.sys.File>{ val: file };
        * = &_guard_file;
        return file.readToEndSmall(allocator);
    }

    fn openFile(root_node_modules_dir: std.fs.Dir, file_path: [:0]const u8): bun.sys.File! {
        if (_.path.items.len + file_path.len * 2 < bun.MAX_PATH_BYTES) {
            switch (_.openFileWithoutOpeningDirectories(root_node_modules_dir, file_path)) {
                .err => |e| {
                    switch (e.getErrno()) {
                        .PERM, .ACCES, .INVAL, .NAMETOOLONG => {},
                        else => return e.toZigErr(),
                    }
                },
                .result => |file| return file,
            }
        }

        const dir = bun.FD.fromStdDir(_.openDir(root_node_modules_dir)?);
        const _guard_dir = ValCloseGuard<FD>{ val: dir };
        * = &_guard_dir;

        return bun.sys.File.openat(dir, file_path, bun.O.RDONLY, 0).unwrap()?;
    }

    fn openDir(root: std.fs.Dir): std.fs.Dir! {
        if (comptime Environment.isPosix) {
            const p = std.posix.toPosixPath(_.path.items)?;
            const fd = bun.sys.openat(.fromStdDir(root), &p, bun.O.DIRECTORY, 0).unwrap()?;
            return fd.stdDir();
        }

        return (bun.sys.openDirAtWindowsA(.fromStdDir(root), _.path.items, {
            can_rename_or_delete: false,
            read_only: false,
        }).unwrap()?).stdDir();
    }

    fn makeAndOpenDir(*_, root: std.fs.Dir): std.fs.Dir! {
        const out = brk: {
            if (comptime Environment.isPosix) {
                break :brk root.makeOpenPath(_.path.items, { iterate: true, access_sub_paths: true })?;
            }

            break :brk (bun.sys.openDirAtWindowsA(.fromStdDir(root), _.path.items, {
                can_rename_or_delete: false,
                op: .open_or_create,
                read_only: false,
            }).unwrap()?).stdDir();
        };
        return out;
    }
}

pub struct TreeContext_ {
    pending_installs: std.ArrayListUnmanaged(DependencyInstallContext) = {},
    binaries: Bin.PriorityQueue,
    install_count: usize = 0,

    pub const Id = Lockfile.Tree.Id;

    fn deinit(*_, allocator: std.mem.Allocator) {
        _.pending_installs.deinit(allocator);
        _.binaries.deinit();
    }
}

pub struct LazyNodeModulesPath_ {
    node_modules: *NodeModulesFolder_,
    root_node_modules_dir: std.fs.Dir,
}

pub union(enum) LazyPackageDestinationDir_ {
    dir: std.fs.Dir,
    node_modules_path: LazyNodeModulesPath_,
    closed: void,

    fn getDir(*_): std.fs.Dir! {
        return switch (_.*) {
            .dir => |dir| dir,
            .node_modules_path => |lazy| brk: {
                const opened_dir = lazy.node_modules.openDir(lazy.root_node_modules_dir)?;
                _.* = { dir: opened_dir };
                break :brk opened_dir;
            },
            .closed => @panic("LazyPackageDestinationDir is closed! This should never happen. Why did this happen?! It's not your fault. Its our fault. We're sorry."),
        };
    }

    fn close(*_) {
        switch (_.*) {
            .dir => {
                if (_.dir.fd != std.fs.cwd().fd) {
                    _.dir.close();
                }
            },
            .node_modules_path, .closed => {},
        }

        _.* = .closed;
    }
}

type string = []const u8;
const DefaultAbsPath = bun.AbsPath({});
const AutoAbsPath = bun.AbsPath({ sep: .auto });


import bun from "bun";
const Environment = bun.Environment;
const FD = bun.FD;
const Global = bun.Global;
const Output = bun.Output;
const Path = bun.path;
const Progress = bun.Progress;
const Syscall = bun.sys;
const strings = bun.strings;
const Bitset = bun.bit_set.DynamicBitSetUnmanaged;
const Command = bun.cli.Command;
const FileSystem = bun.fs.FileSystem;
const String = bun.Semver.String;

const install = bun.install;
const Bin = install.Bin;
const DependencyID = install.DependencyID;
const DependencyInstallContext = install.DependencyInstallContext;
const ExtractData = install.ExtractData;
const LifecycleScriptSubprocess = install.LifecycleScriptSubprocess;
const PackageID = install.PackageID;
const PackageInstall = install.PackageInstall;
const PackageNameHash = install.PackageNameHash;
const PatchTask = install.PatchTask;
const PostinstallOptimizer = install.PostinstallOptimizer;
const Resolution = install.Resolution;
const Task = install.Task;
const TaskCallbackContext = install.TaskCallbackContext;
const TruncatedPackageNameHash = install.TruncatedPackageNameHash;
const invalid_package_id = install.invalid_package_id;

const Lockfile = install.Lockfile;
const Package = Lockfile.Package;

const PackageManager = install.PackageManager;
const Options = PackageManager.Options;
