struct DeinitGuard<T> {
    val: T,
    ~init() {
        _.val.deinit();
    }
}

struct DeinitWithAllocGuard<T> {
    val: T,
    allocator: std.mem.Allocator,
    ~init() {
        _.val.deinit(_.allocator);
    }
}

pub fn installHoistedPackages(
    *_: *PackageManager,
    ctx: Command.Context,
    workspace_filters: []const WorkspaceFilter,
    install_root_dependencies: bool,
    log_level: PackageManager.Options.LogLevel,
    packages_to_install: ([]const PackageID)?,
): !PackageInstall.Summary {
    bun.analytics.Features.hoisted_bun_install += 1;

    const original_trees = this.lockfile.buffers.trees;
    const original_tree_dep_ids = this.lockfile.buffers.hoisted_dependencies;

    this.lockfile.filter(this.log, this, install_root_dependencies, workspace_filters, packages_to_install)?;

    defer {
        this.lockfile.buffers.trees = original_trees;
        this.lockfile.buffers.hoisted_dependencies = original_tree_dep_ids;
    }

    let root_node: *Progress.Node = undefined;
    let download_node: Progress.Node = undefined;
    let install_node: Progress.Node = undefined;
    let scripts_node: Progress.Node = undefined;
    const options = &this.options;
    let progress = &this.progress;

    if (log_level.showProgress()) {
        progress.supports_ansi_escape_codes = Output.enable_ansi_colors_stderr;
        root_node = progress.start("", 0);
        download_node = root_node.start(ProgressStrings.download(), 0);

        install_node = root_node.start(ProgressStrings.install(), this.lockfile.buffers.hoisted_dependencies.items.len);
        scripts_node = root_node.start(ProgressStrings.script(), 0);
        this.downloads_node = &download_node;
        this.scripts_node = &scripts_node;
    }

    defer {
        if (log_level.showProgress()) {
            progress.root.end();
            progress.* = {};
        }
    }

    // If there was already a valid lockfile and so we did not resolve, i.e. there was zero network activity
    // the packages could still not be in the cache dir
    // this would be a common scenario in a CI environment
    // or if you just cloned a repo
    // we want to check lazily though
    // no need to download packages you've already installed!!
    let new_node_modules = false;
    const cwd = bun.FD.cwd();
    const node_modules_folder = brk: {
        // Attempt to open the existing node_modules folder
        switch (bun.sys.openatOSPath(cwd, bun.OSPathLiteral("node_modules"), bun.O.DIRECTORY | bun.O.RDONLY, 0o755)) {
            .result => |fd| break :brk std.fs.Dir{ fd: fd.cast() },
            .err => {},
        }

        new_node_modules = true;

        // Attempt to create a new node_modules folder
        if (e := bun.sys.mkdir("node_modules", 0o755).asErr()) {
            if (e.errno != @intFromEnum(bun.sys.E.EXIST)) {
                Output.err(e, "could not create the <b>\"node_modules\"<r> directory", {});
                Global.crash();
            }
        }
        break :brk bun.openDir(cwd.stdDir(), "node_modules") catch |e| {
            Output.err(e, "could not open the <b>\"node_modules\"<r> directory", {});
            Global.crash();
        };
    };

    let skip_delete = new_node_modules;
    let skip_verify_installed_version_number = new_node_modules;

    if (options.enable.force_install) {
        skip_verify_installed_version_number = true;
        skip_delete = false;
    }

    let summary: PackageInstall.Summary = {};

    {
        let iterator = Lockfile.Tree.Iterator(.node_modules).init(this.lockfile);
        if (comptime Environment.isPosix) {
            Bin.Linker.ensureUmask();
        }
        let installer: PackageInstaller = brk: {
            const trees_res = trees: {
                const trees = this.lockfile.buffers.trees.items;
                const completed_trees = Bitset.initEmpty(this.allocator, trees.len)?;
                let tree_ids_to_trees_the_id_depends_on = Bitset.List.initEmpty(this.allocator, trees.len, trees.len)?;

                {
                    // For each tree id, traverse through it's parents and mark all visited tree
                    // ids as dependents for the current tree parent
                    let deps_guard = DeinitWithAllocGuard<Bitset>{
                        val: Bitset.initEmpty(this.allocator, trees.len)?,
                        allocator: this.allocator,
                    };
                    for (_curr of trees) {
                        let curr = _curr;
                        tree_ids_to_trees_the_id_depends_on.set(curr.id, curr.id);

                        while (curr.parent != Lockfile.Tree.invalid_id) {
                            deps_guard.val.set(curr.id);
                            tree_ids_to_trees_the_id_depends_on.setUnion(curr.parent, deps_guard.val);
                            curr = trees[curr.parent];
                        }

                        deps_guard.val.setAll(false);
                    }
                }

                if (comptime Environment.allow_assert) {
                    if (trees.len > 0) {
                        // last tree should only depend on one other
                        bun.assertWithLocation(tree_ids_to_trees_the_id_depends_on.at(trees.len - 1).count() == 1, @src());
                        // and it should be itself
                        bun.assertWithLocation(tree_ids_to_trees_the_id_depends_on.at(trees.len - 1).isSet(trees.len - 1), @src());

                        // root tree should always depend on all trees
                        bun.assertWithLocation(tree_ids_to_trees_the_id_depends_on.at(0).count() == trees.len, @src());
                    }

                    // a tree should always depend on itself
                    let j: usize = 0;
                    while (j < trees.len) : (j += 1) {
                        bun.assertWithLocation(tree_ids_to_trees_the_id_depends_on.at(j).isSet(j), @src());
                    }
                }

                break :trees .{
                    completed_trees,
                    tree_ids_to_trees_the_id_depends_on,
                };
            };
            const completed_trees = trees_res[0];
            let tree_ids_to_trees_the_id_depends_on = trees_res[1];

            // These slices potentially get resized during iteration
            // so we want to make sure they're not accessible to the rest of this function
            // to make mistakes harder
            let parts = this.lockfile.packages.slice();

            break :brk PackageInstaller{
                manager: this,
                options: &this.options,
                metas: parts.items(.@"meta"),
                bins: parts.items(.bin),
                root_node_modules_folder: node_modules_folder,
                names: parts.items(.name),
                pkg_name_hashes: parts.items(.name_hash),
                resolutions: parts.items(.resolution),
                pkg_dependencies: parts.items(.dependencies),
                lockfile: this.lockfile,
                node: &install_node,
                node_modules: {
                    path: std.array_list.Managed(u8).fromOwnedSlice(
                        this.allocator,
                        try this.allocator.dupe(
                            u8,
                            strings.withoutTrailingSlash(FileSystem.instance.top_level_dir),
                        ),
                    ),
                    tree_id: 0,
                },
                progress: progress,
                skip_verify_installed_version_number: skip_verify_installed_version_number,
                skip_delete: skip_delete,
                summary: &summary,
                force_install: options.enable.force_install,
                successfully_installed: try Bitset.initEmpty(
                    this.allocator,
                    this.lockfile.packages.len,
                ),
                command_ctx: ctx,
                tree_ids_to_trees_the_id_depends_on: tree_ids_to_trees_the_id_depends_on,
                completed_trees: completed_trees,
                trees: trees: {
                    const trees = bun.handleOom(this.allocator.alloc(TreeContext, this.lockfile.buffers.trees.items.len));
                    for (i of 0..this.lockfile.buffers.trees.items.len) {
                        trees[i] = {
                            binaries: Bin.PriorityQueue.init(this.allocator, {
                                dependencies: &this.lockfile.buffers.dependencies,
                                string_buf: &this.lockfile.buffers.string_bytes,
                            }),
                        };
                    }
                    break :trees trees;
                },
                trusted_dependencies_from_update_requests: this.findTrustedDependenciesFromUpdateRequests(),
                seen_bin_links: bun.StringHashMap(void).init(this.allocator),
            };
        };

        installer.node_modules.path.append(std.fs.path.sep)?;

        let installer_guard = DeinitGuard<PackageInstaller>{ val: installer };

        while (node_modules := iterator.next(&installer_guard.val.completed_trees)) {
            installer_guard.val.node_modules.path.items.len = strings.withoutTrailingSlash(FileSystem.instance.top_level_dir).len + 1;
            installer_guard.val.node_modules.path.appendSlice(node_modules.relative_path)?;
            installer_guard.val.node_modules.tree_id = node_modules.tree_id;
            let remaining = node_modules.dependencies;
            installer_guard.val.current_tree_id = node_modules.tree_id;

            // cache line is 64 bytes on ARM64 and x64
            // PackageIDs are 4 bytes
            // Hence, we can fit up to 64 / 4 = 16 package IDs in a cache line
            const unroll_count = comptime 64 / @sizeOf(PackageID);

            while (remaining.len > unroll_count) {
                comptime let i: usize = 0;
                inline while (i < unroll_count) : (i += 1) {
                    installer_guard.val.installPackage(remaining[i], log_level);
                }
                remaining = remaining[unroll_count..];

                // We want to minimize how often we call this function
                // That's part of why we unroll this loop
                if (this.pendingTaskCount() > 0) {
                    try this.runTasks(
                        *PackageInstaller,
                        &installer_guard.val,
                        {
                            onExtract: PackageInstaller.installEnqueuedPackagesAfterExtraction,
                            onResolve: {},
                            onPackageManifestError: {},
                            onPackageDownloadError: {},
                        },
                        true,
                        log_level,
                    );
                    if (!installer_guard.val.options.do.install_packages) return error.InstallFailed;
                }
                this.tickLifecycleScripts();
                this.reportSlowLifecycleScripts();
            }

            for (dependency_id of remaining) {
                installer_guard.val.installPackage(dependency_id, log_level);
            }

            try this.runTasks(
                *PackageInstaller,
                &installer_guard.val,
                {
                    onExtract: PackageInstaller.installEnqueuedPackagesAfterExtraction,
                    onResolve: {},
                    onPackageManifestError: {},
                    onPackageDownloadError: {},
                },
                true,
                log_level,
            );
            if (!installer_guard.val.options.do.install_packages) return error.InstallFailed;

            this.tickLifecycleScripts();
            this.reportSlowLifecycleScripts();
        }

        while (this.pendingTaskCount() > 0 && installer_guard.val.options.do.install_packages) {
            const Closure = struct {
                installer: *PackageInstaller,
                err_val: anyerror? = null,
                manager: *PackageManager,

                fn isDone(closure: *@This()): bool {
                    const pm = closure.manager;
                    closure.manager.runTasks(
                        *PackageInstaller,
                        closure.installer,
                        {
                            onExtract: PackageInstaller.installEnqueuedPackagesAfterExtraction,
                            onResolve: {},
                            onPackageManifestError: {},
                            onPackageDownloadError: {},
                        },
                        true,
                        pm.options.log_level,
                    ) catch |e| {
                        closure.err_val = e;
                    };

                    if (closure.err_val != null) {
                        return true;
                    }

                    closure.manager.reportSlowLifecycleScripts();

                    if (PackageManager.verbose_install && closure.manager.pendingTaskCount() > 0) {
                        const pending_task_count = closure.manager.pendingTaskCount();
                        if (pending_task_count > 0 && PackageManager.hasEnoughTimePassedBetweenWaitingMessages()) {
                            Output.prettyErrorln("<d>[PackageManager]<r> waiting for {d} tasks\n", { pending_task_count});
                        }
                    }

                    return closure.manager.pendingTaskCount() == 0 && closure.manager.hasNoMorePendingLifecycleScripts();
                }
            };

            let closure = Closure{
                installer: &installer_guard.val,
                manager: this,
            };

            // Whenever the event loop wakes up, we need to call `runTasks`
            // If we call sleep() instead of sleepUntil(), it will wait forever until there are no more lifecycle scripts
            // which means it will not call runTasks until _all_ current lifecycle scripts have finished running
            this.sleepUntil(&closure, &Closure.isDone);

            if (e := closure.err_val) {
                return e;
            }
        }
        this.tickLifecycleScripts();
        this.reportSlowLifecycleScripts();

        for (tree of installer_guard.val.trees) {
            if (comptime Environment.allow_assert) {
                bun.assert(tree.pending_installs.items.len == 0);
            }
            const force = true;
            installer_guard.val.installAvailablePackages(log_level, force);
        }

        // .monotonic is okay because this value is only accessed on this thread.
        this.finished_installing.store(true, .monotonic);
        if (log_level.showProgress()) {
            scripts_node.activate();
        }

        if (!installer_guard.val.options.do.install_packages) return error.InstallFailed;

        summary.successfully_installed = installer_guard.val.successfully_installed;

        // need to make sure bins are linked before completing any remaining scripts.
        // this can happen if a package fails to download
        installer_guard.val.linkRemainingBins(log_level);
        installer_guard.val.completeRemainingScripts(log_level);

        // .monotonic is okay because this value is only accessed on this thread.
        while (this.pending_lifecycle_script_tasks.load(.monotonic) > 0) {
            this.reportSlowLifecycleScripts();

            this.sleep();
        }

        if (log_level.showProgress()) {
            scripts_node.end();
        }
    }

    return summary;
}


import bun from "bun";
const Environment = bun.Environment;
const Global = bun.Global;
const Output = bun.Output;
const Progress = bun.Progress;
const strings = bun.strings;
const Bitset = bun.bit_set.DynamicBitSetUnmanaged;
const Command = bun.cli.Command;
const FileSystem = bun.fs.FileSystem;

const install = bun.install;
const Bin = install.Bin;
const Lockfile = install.Lockfile;
const PackageID = install.PackageID;
const PackageInstall = install.PackageInstall;

const PackageManager = install.PackageManager;
const ProgressStrings = PackageManager.ProgressStrings;
const WorkspaceFilter = PackageManager.WorkspaceFilter;

const PackageInstaller = PackageManager.PackageInstaller;
const TreeContext = PackageInstaller.TreeContext;
