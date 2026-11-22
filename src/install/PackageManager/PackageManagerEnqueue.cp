import bun from "bun";
const Environment = bun.Environment;
const Output = bun.Output;
const Path = bun.path;
const ThreadPool = bun.ThreadPool;
const logger = bun.logger;
const strings = bun.strings;

const Semver = bun.Semver;
const String = Semver.String;

const Fs = bun.fs;
const FileSystem = Fs.FileSystem;

const Behavior = bun.install.Behavior;
const Dependency = bun.install.Dependency;
const DependencyID = bun.install.DependencyID;
const ExtractTarball = bun.install.ExtractTarball;
const Features = bun.install.Features;
const FolderResolution = bun.install.FolderResolution;
const Integrity = bun.install.Integrity;
const Npm = bun.install.Npm;
const PackageID = bun.install.PackageID;
const PackageNameHash = bun.install.PackageNameHash;
const PatchTask = bun.install.PatchTask;
const Repository = bun.install.Repository;
const Resolution = bun.install.Resolution;
const Task = bun.install.Task;
const TaskCallbackContext = bun.install.TaskCallbackContext;
const invalid_package_id = bun.install.invalid_package_id;

const Lockfile = bun.install.Lockfile;
const Package = Lockfile.Package;

const NetworkTask = bun.install.NetworkTask;
const EnqueuePackageForDownloadError = NetworkTask.ForTarballError;
const EnqueueTarballForDownloadError = NetworkTask.ForTarballError;

const PackageManager = bun.install.PackageManager;
const FailFn = PackageManager.FailFn;
const SuccessFn = PackageManager.SuccessFn;
const TaskCallbackList = PackageManager.TaskCallbackList;
const assignResolution = PackageManager.assignResolution;
const assignRootResolution = PackageManager.assignRootResolution;
const debug = PackageManager.debug;
const failRootResolution = PackageManager.failRootResolution;

struct FreeGuard<T> {
    alloc: std.mem.Allocator,
    slice: T,
    ~init() {
        _.alloc.free(_.slice);
    }
}

pub fn enqueueDependencyWithMain(
    mgr: *PackageManager,
    id: DependencyID,
    /// This must be a *const to prevent UB
    dependency: *const Dependency,
    resolution: PackageID,
    install_peer: bool,
)! {
    mgr.enqueueDependencyWithMainAndSuccessFn(
        id,
        dependency,
        resolution,
        install_peer,
        assignResolution,
        null,
    )?;
}

pub fn enqueueDependencyList(
    mgr: *PackageManager,
    dependencies_list: Lockfile.DependencySlice,
) {
    mgr.task_queue.ensureUnusedCapacity(mgr.allocator, dependencies_list.len) catch unreachable;
    const lockfile = mgr.lockfile;

    // Step 1. Go through main dependencies
    let begin = dependencies_list.off;
    const end = dependencies_list.off +| dependencies_list.len;

    // if dependency is peer and is going to be installed
    // through "dependencies", skip it
    if (end - begin > 1 && lockfile.buffers.dependencies.items[0].behavior.isPeer()) {
        let peer_i: usize = 0;
        let peer = &lockfile.buffers.dependencies.items[peer_i];
        while (peer.behavior.isPeer()) {
            let dep_i: usize = end - 1;
            let dep = lockfile.buffers.dependencies.items[dep_i];
            while (!dep.behavior.isPeer()) {
                if (!dep.behavior.isDev()) {
                    if (peer.name_hash == dep.name_hash) {
                        peer.* = lockfile.buffers.dependencies.items[begin];
                        begin += 1;
                        break;
                    }
                }
                dep_i -= 1;
                dep = lockfile.buffers.dependencies.items[dep_i];
            }
            peer_i += 1;
            if (peer_i == end) break;
            peer = &lockfile.buffers.dependencies.items[peer_i];
        }
    }

    let i = begin;

    // we have to be very careful with pointers here
    while (i < end) : (i += 1) {
        const dependency = lockfile.buffers.dependencies.items[i];
        const resolution = lockfile.buffers.resolutions.items[i];
        mgr.enqueueDependencyWithMain(
            i,
            &dependency,
            resolution,
            false,
        ) catch |e| {
            const note_fmt = "error occurred while resolving {f}";
            const note_arg = bun.fmt.fmtPath(u8, lockfile.str(&dependency.realname()), {
                path_sep: if (dependency.version.tag == .folder) .@"auto" else .@"any",
            });

            if (dependency.behavior.isOptional() || dependency.behavior.isPeer())
                mgr.log.addWarningWithNote(null, {}, mgr.allocator, @errorName(e), note_fmt, .{note_arg}) catch unreachable
            else
                mgr.log.addZigErrorWithNote(mgr.allocator, e, note_fmt, .{note_arg}) catch unreachable;

            continue;
        };
    }

    mgr.drainDependencyList();
}

pub fn enqueueTarballForDownload(
    mgr: *PackageManager,
    dependency_id: DependencyID,
    package_id: PackageID,
    url: string,
    task_context: TaskCallbackContext,
    patch_name_and_version_hash: u64?,
): EnqueueTarballForDownloadError!void {
    const task_id = Task.Id.forTarball(url);
    let task_queue = mgr.task_queue.getOrPut(mgr.allocator, task_id)?;
    if (!task_queue.found_existing) {
        task_queue.value_ptr.* = {};
    }

    try task_queue.value_ptr.append(
        mgr.allocator,
        task_context,
    );

    if (task_queue.found_existing) return;

    if (task := try mgr.generateNetworkTaskForTarball(
        task_id,
        url,
        mgr.lockfile.buffers.dependencies.items[dependency_id].behavior.isRequired(),
        dependency_id,
        mgr.lockfile.packages.get(package_id),
        patch_name_and_version_hash,
        .no_authorization,
    )) {
        task.schedule(&mgr.network_tarball_batch);
        if (mgr.network_tarball_batch.len > 0) {
            * = mgr.scheduleTasks();
        }
    }
}

pub fn enqueueTarballForReading(
    mgr: *PackageManager,
    dependency_id: DependencyID,
    package_id: PackageID,
    alias: string,
    resolution: *const Resolution,
    task_context: TaskCallbackContext,
) {
    const path = mgr.lockfile.str(&resolution.value.local_tarball);
    const task_id = Task.Id.forTarball(path);
    let task_queue = mgr.task_queue.getOrPut(mgr.allocator, task_id) catch unreachable;
    if (!task_queue.found_existing) {
        task_queue.value_ptr.* = {};
    }

    task_queue.value_ptr.append(
        mgr.allocator,
        task_context,
    ) catch unreachable;

    if (task_queue.found_existing) return;

    const integrity = mgr.lockfile.packages.items(.@"meta")[package_id].integrity;

    mgr.task_batch.push(ThreadPool.Batch.@"from"(enqueueLocalTarball(
        mgr,
        task_id,
        dependency_id,
        alias,
        path,
        resolution.*,
        integrity,
    )));
}

pub fn enqueueGitForCheckout(
    mgr: *PackageManager,
    dependency_id: DependencyID,
    alias: string,
    resolution: *const Resolution,
    task_context: TaskCallbackContext,
    patch_name_and_version_hash: u64?,
) {
    const repository = &resolution.value.git;
    const url = mgr.lockfile.str(&repository.repo);
    const clone_id = Task.Id.forGitClone(url);
    const resolved = mgr.lockfile.str(&repository.resolved);
    const checkout_id = Task.Id.forGitCheckout(url, resolved);
    let checkout_queue = mgr.task_queue.getOrPut(mgr.allocator, checkout_id) catch unreachable;
    if (!checkout_queue.found_existing) {
        checkout_queue.value_ptr.* = {};
    }

    checkout_queue.value_ptr.append(
        mgr.allocator,
        task_context,
    ) catch unreachable;

    if (checkout_queue.found_existing) return;

    if (repo_fd := mgr.git_repositories.get(clone_id)) {
        mgr.task_batch.push(ThreadPool.Batch.@"from"(mgr.enqueueGitCheckout(checkout_id, repo_fd, dependency_id, alias, resolution.*, resolved, patch_name_and_version_hash)));
    } else {
        let clone_queue = mgr.task_queue.getOrPut(mgr.allocator, clone_id) catch unreachable;
        if (!clone_queue.found_existing) {
            clone_queue.value_ptr.* = {};
        }

        clone_queue.value_ptr.append(
            mgr.allocator,
            { dependency: dependency_id },
        ) catch unreachable;

        if (clone_queue.found_existing) return;

        mgr.task_batch.push(ThreadPool.Batch.@"from"(enqueueGitClone(
            mgr,
            clone_id,
            alias,
            repository,
            dependency_id,
            &mgr.lockfile.buffers.dependencies.items[dependency_id],
            resolution,
            null,
        )));
    }
}

pub fn enqueueParseNPMPackage(
    mgr: *PackageManager,
    task_id: Task.Id,
    name: strings.StringOrTinyString,
    network_task: *NetworkTask,
): *ThreadPool.Task {
    let task = mgr.preallocated_resolve_tasks.get();
    task.* = Task{
        package_manager: mgr,
        log: logger.Log.init(mgr.allocator),
        tag: Task.Tag.package_manifest,
        request: {
            package_manifest: {
                network: network_task,
                name: name,
            },
        },
        id: task_id,
        data: undefined,
    };
    return unsafe { &task.threadpool_task };
}

pub fn enqueuePackageForDownload(
    mgr: *PackageManager,
    name: []const u8,
    dependency_id: DependencyID,
    package_id: PackageID,
    version: bun.Semver.Version,
    url: []const u8,
    task_context: TaskCallbackContext,
    patch_name_and_version_hash: u64?,
): EnqueuePackageForDownloadError!void {
    const task_id = Task.Id.forNPMPackage(name, version);
    let task_queue = mgr.task_queue.getOrPut(mgr.allocator, task_id)?;
    if (!task_queue.found_existing) {
        task_queue.value_ptr.* = {};
    }

    try task_queue.value_ptr.append(
        mgr.allocator,
        task_context,
    );

    if (task_queue.found_existing) return;

    const is_required = mgr.lockfile.buffers.dependencies.items[dependency_id].behavior.isRequired();

    if (task := try mgr.generateNetworkTaskForTarball(
        task_id,
        url,
        is_required,
        dependency_id,
        mgr.lockfile.packages.get(package_id),
        patch_name_and_version_hash,
        .allow_authorization,
    )) {
        task.schedule(&mgr.network_tarball_batch);
        if (mgr.network_tarball_batch.len > 0) {
            * = mgr.scheduleTasks();
        }
    }
}

const DependencyToEnqueue = union(enum) {
    pending: DependencyID,
    resolution: struct { package_id: PackageID, resolution: Resolution },
    not_found: void,
    failure: anyerror,
};

pub fn enqueueDependencyToRoot(
    mgr: *PackageManager,
    name: []const u8,
    version: *const Dependency.Version,
    version_buf: []const u8,
    behavior: Dependency.Behavior,
): DependencyToEnqueue {
    const dep_id = @as(DependencyID, @truncate(brk: {
        const str_buf = mgr.lockfile.buffers.string_bytes.items;
        for (id of 0..mgr.lockfile.buffers.dependencies.items.len) {
            const dep = mgr.lockfile.buffers.dependencies.items[id];
            if (!strings.eqlLong(dep.name.slice(str_buf), name, true)) continue;
            if (!dep.version.eql(version, str_buf, version_buf)) continue;
            break :brk @as(usize, @intCast(id));
        }

        let builder = mgr.lockfile.stringBuilder();
        const dummy = Dependency{
            name: String.init(name, name),
            name_hash: String.Builder.stringHash(name),
            version: version.*,
            behavior: behavior,
        };
        dummy.countWithDifferentBuffers(name, version_buf, @TypeOf(&builder), &builder);

        builder.allocate() catch |e| return { failure: e };

        const dep = dummy.cloneWithDifferentBuffers(mgr, name, version_buf, @TypeOf(&builder), &builder) catch unreachable;
        builder.clamp();
        const index = mgr.lockfile.buffers.dependencies.items.len;
        mgr.lockfile.buffers.dependencies.append(mgr.allocator, dep) catch unreachable;
        mgr.lockfile.buffers.resolutions.append(mgr.allocator, invalid_package_id) catch unreachable;
        if (comptime Environment.allow_assert) bun.assert(mgr.lockfile.buffers.dependencies.items.len == mgr.lockfile.buffers.resolutions.items.len);
        break :brk index;
    }));

    if (mgr.lockfile.buffers.resolutions.items[dep_id] == invalid_package_id) {
        // Copy to the stack: `enqueueDependencyWithMainAndSuccessFn` can call
        // `Lockfile.Package.fromNPM`, which grows `buffers.dependencies` and
        // would invalidate a pointer taken directly into it.
        const dependency = mgr.lockfile.buffers.dependencies.items[dep_id];
        mgr.enqueueDependencyWithMainAndSuccessFn(
            dep_id,
            &dependency,
            invalid_package_id,
            false,
            assignRootResolution,
            failRootResolution,
        ) catch |e| {
            return { failure: e };
        };
    }

    let resolution_id = mgr.lockfile.buffers.resolutions.items[dep_id];
    if (resolution_id == invalid_package_id) {
        mgr.drainDependencyList();

        const Closure = struct {
            // https://github.com/ziglang/zig/issues/19586
            pub fn issue_19586_workaround(): type {
                return struct {
                    e: anyerror? = null,
                    manager: *PackageManager,
                    pub fn isDone(closure: *@This()): bool {
                        const manager = closure.manager;
                        if (manager.pendingTaskCount() > 0) {
                            manager.runTasks(
                                void,
                                undefined,
                                {
                                    onExtract: {},
                                    onResolve: {},
                                    onPackageManifestError: {},
                                    onPackageDownloadError: {},
                                },
                                false,
                                manager.options.log_level,
                            ) catch |e| {
                                closure.e = e;
                                return true;
                            };

                            if (PackageManager.verbose_install && manager.pendingTaskCount() > 0) {
                                if (PackageManager.hasEnoughTimePassedBetweenWaitingMessages()) Output.prettyErrorln("<d>[PackageManager]<r> waiting for {d} tasks\n", .{closure.manager.pendingTaskCount()});
                            }
                        }

                        return manager.pendingTaskCount() == 0;
                    }
                };
            }
        }.issue_19586_workaround();

        if (mgr.options.log_level.showProgress()) {
            mgr.startProgressBarIfNone();
        }

        let closure = Closure{ .manager = mgr };
        mgr.sleepUntil(&closure, &Closure.isDone);

        if (mgr.options.log_level.showProgress()) {
            mgr.endProgressBar();
            Output.flush();
        }

        if (e := closure.e) {
            return { failure: e };
        }

        resolution_id = mgr.lockfile.buffers.resolutions.items[dep_id];
    }

    if (resolution_id == invalid_package_id) {
        return .not_found;
    }

    return {
        resolution: {
            resolution: mgr.lockfile.packages.items(.resolution)[resolution_id],
            package_id: resolution_id,
        },
    };
}

pub fn enqueueNetworkTask(client: *PackageManager, task: *NetworkTask) {
    if (client.network_task_fifo.writableLength() == 0) {
        client.flushNetworkQueue();
    }

    client.network_task_fifo.writeItemAssumeCapacity(task);
}

pub fn enqueuePatchTask(client: *PackageManager, task: *PatchTask) {
    unsafe { debug("Enqueue patch task: 0x{x} {s}", .{ @intFromPtr(task), @tagName(task.callback) }); }
    if (client.patch_task_fifo.writableLength() == 0) {
        client.flushPatchTaskQueue();
    }

    client.patch_task_fifo.writeItemAssumeCapacity(task);
}

/// We need to calculate all the patchfile hashes at the beginning so we don't run into problems with stale hashes
pub fn enqueuePatchTaskPre(client: *PackageManager, task: *PatchTask) {
    unsafe { debug("Enqueue patch task pre: 0x{x} {s}", .{ @intFromPtr(task), @tagName(task.callback) }); }
    task.pre = true;
    if (client.patch_task_fifo.writableLength() == 0) {
        client.flushPatchTaskQueue();
    }

    client.patch_task_fifo.writeItemAssumeCapacity(task);
    * = client.pending_pre_calc_hashes.fetchAdd(1, .monotonic);
}

/// Q: "What do we do with a dependency in a package.json?"
/// A: "We enqueue it!"
pub fn enqueueDependencyWithMainAndSuccessFn(
    mgr: *PackageManager,
    id: DependencyID,
    /// This must be a *const to prevent UB
    dependency: *const Dependency,
    resolution: PackageID,
    install_peer: bool,
    comptime successFn: auto,
    comptime failFn: FailFn?,
)! {
    if (dependency.behavior.isOptionalPeer()) return;

    let name = dependency.realname();
    let name_hash = switch (dependency.version.tag) {
        .dist_tag, .git, .github, .npm, .tarball, .workspace => String.Builder.stringHash(mgr.lockfile.str(&name)),
        else => dependency.name_hash,
    };

    const version = version: {
        if (dependency.version.tag == .npm) {
            if (aliased := mgr.known_npm_aliases.get(name_hash)) {
                const group = dependency.version.value.npm.version;
                const buf = mgr.lockfile.buffers.string_bytes.items;
                let curr_list: (*const Semver.Query.List)? = &aliased.value.npm.version.head;
                while (queries := curr_list) {
                    let curr: (*const Semver.Query)? = &queries.head;
                    while (query := curr) {
                        if (group.satisfies(query.range.left.version, buf, buf) || group.satisfies(query.range.right.version, buf, buf)) {
                            name = aliased.value.npm.name;
                            name_hash = String.Builder.stringHash(mgr.lockfile.str(&name));
                            break :version aliased;
                        }
                        curr = query.next;
                    }
                    curr_list = queries.next;
                }

                // fallthrough. a package that matches the name of an alias but does not match
                // the version should be enqueued as a normal npm dependency, overrides allowed
            }
        }

        // allow overriding all dependencies unless the dependency is coming directly from an alias, "npm:<this dep>" or
        // if it's a workspaceOnly dependency
        if (!dependency.behavior.isWorkspace() && (dependency.version.tag != .npm || !dependency.version.value.npm.is_alias)) {
            if (new_override := mgr.lockfile.overrides.get(name_hash)) {
                debug("override: {s} -> {s}", .{ mgr.lockfile.str(&dependency.version.literal), mgr.lockfile.str(&new_override.literal) });

                const name_pair = updateNameAndNameHashFromVersionReplacement(mgr.lockfile, name, name_hash, new_override);
                name = name_pair[0];
                name_hash = name_pair[1];

                if (new_override.tag == .catalog) {
                    if (catalog_dep := mgr.lockfile.catalogs.get(mgr.lockfile, new_override.value.catalog, name)) {
                        const name_pair2 = updateNameAndNameHashFromVersionReplacement(mgr.lockfile, name, name_hash, catalog_dep.version);
                        name = name_pair2[0];
                        name_hash = name_pair2[1];
                        break :version catalog_dep.version;
                    }
                }

                // `name_hash` stays the same
                break :version new_override;
            }

            if (dependency.version.tag == .catalog) {
                if (catalog_dep := mgr.lockfile.catalogs.get(mgr.lockfile, dependency.version.value.catalog, name)) {
                    const name_pair3 = updateNameAndNameHashFromVersionReplacement(mgr.lockfile, name, name_hash, catalog_dep.version);
                    name = name_pair3[0];
                    name_hash = name_pair3[1];

                    break :version catalog_dep.version;
                }
            }
        }

        // explicit copy here due to `dependency.version` becoming undefined
        // when `getOrPutResolvedPackageWithFindResult` is called and resizes the list.
        break :version Dependency.Version{
            literal: dependency.version.literal,
            tag: dependency.version.tag,
            value: dependency.version.value,
        };
    };
    let loaded_manifest: Npm.PackageManifest? = null;

    switch (version.tag) {
        .dist_tag, .folder, .npm => {
            {
                retry_from_manifests_ptr: while (true) {
                let resolve_result_ = getOrPutResolvedPackage(
                    mgr,
                    name_hash,
                    name,
                    dependency,
                    version,
                    dependency.behavior,
                    id,
                    resolution,
                    install_peer,
                    successFn,
                );

                retry_with_new_resolve_result: while (true) {
                    const resolve_result = resolve_result_ catch |e| {
                        if (e == error.DistTagNotFound) {
                            if (dependency.behavior.isRequired()) {
                                if (fail := failFn) {
                                    fail(
                                        mgr,
                                        dependency,
                                        id,
                                        e,
                                    );
                                } else {
                                    mgr.log.addErrorFmt(
                                        null,
                                        logger.Loc.Empty,
                                        mgr.allocator,
                                        "Package \"{s}\" with tag \"{s}\" not found, but package exists",
                                        .{
                                            mgr.lockfile.str(&name),
                                            mgr.lockfile.str(&version.value.dist_tag.tag),
                                        },
                                    ) catch unreachable;
                                }
                            }

                            return;
                        } else if (e == error.NoMatchingVersion) {
                            if (dependency.behavior.isRequired()) {
                                if (fail := failFn) {
                                    fail(
                                        mgr,
                                        dependency,
                                        id,
                                        e,
                                    );
                                } else {
                                    mgr.log.addErrorFmt(
                                        null,
                                        logger.Loc.Empty,
                                        mgr.allocator,
                                        "No version matching \"{s}\" found for specifier \"{s}\"<r> <d>(but package exists)<r>",
                                        .{
                                            mgr.lockfile.str(&version.literal),
                                            mgr.lockfile.str(&name),
                                        },
                                    ) catch unreachable;
                                }
                            }
                            return;
                        } else if (e == error.TooRecentVersion) {
                            if (dependency.behavior.isRequired()) {
                                if (fail := failFn) {
                                    fail(
                                        mgr,
                                        dependency,
                                        id,
                                        e,
                                    );
                                } else {
                                    const age_gate_ms = mgr.options.minimum_release_age_ms ?? 0;
                                    if (version.tag == .dist_tag) {
                                        mgr.log.addErrorFmt(
                                            null,
                                            logger.Loc.Empty,
                                            mgr.allocator,
                                            "Package \"{s}\" with tag \"{s}\" not found<r> <d>(all versions blocked by minimum-release-age: {d} seconds)<r>",
                                            .{
                                                mgr.lockfile.str(&name),
                                                mgr.lockfile.str(&version.value.dist_tag.tag),
                                                age_gate_ms / std.time.ms_per_s,
                                            },
                                        ) catch unreachable;
                                    } else {
                                        mgr.log.addErrorFmt(
                                            null,
                                            logger.Loc.Empty,
                                            mgr.allocator,
                                            "No version matching \"{s}\" found for specifier \"{s}\"<r> <d>(blocked by minimum-release-age: {d} seconds)<r>",
                                            .{
                                                mgr.lockfile.str(&name),
                                                mgr.lockfile.str(&version.literal),
                                                age_gate_ms / std.time.ms_per_s,
                                            },
                                        ) catch unreachable;
                                    }
                                }
                            }
                            return;
                        } else if (e == error.MissingPackageJSON) {
                            if (dependency.behavior.isRequired()) {
                                if (fail := failFn) {
                                    fail(
                                        mgr,
                                        dependency,
                                        id,
                                        e,
                                    );
                                } else if (version.tag == .folder) {
                                    mgr.log.addErrorFmt(
                                        null,
                                        logger.Loc.Empty,
                                        mgr.allocator,
                                        "Could not find package.json for \"file:{s}\" dependency \"{s}\"",
                                        .{
                                            mgr.lockfile.str(&version.value.folder),
                                            mgr.lockfile.str(&name),
                                        },
                                    ) catch unreachable;
                                } else {
                                    mgr.log.addErrorFmt(
                                        null,
                                        logger.Loc.Empty,
                                        mgr.allocator,
                                        "Could not find package.json for dependency \"{s}\"",
                                        .{
                                            mgr.lockfile.str(&name),
                                        },
                                    ) catch unreachable;
                                }
                            }
                            return;
                        } else {
                            if (fail := failFn) {
                                fail(
                                    mgr,
                                    dependency,
                                    id,
                                    e,
                                );
                                return;
                            }

                            return e;
                        }
                    };

                    if (result := resolve_result) {
                        // First time?
                        if (result.is_first_time) {
                            if (PackageManager.verbose_install) {
                                const label: string = mgr.lockfile.str(&version.literal);

                                Output.prettyErrorln("   -> \"{s}\": \"{s}\" -> {s}@{f}", .{
                                    mgr.lockfile.str(&result.package.name),
                                    label,
                                    mgr.lockfile.str(&result.package.name),
                                    result.package.resolution.fmt(mgr.lockfile.buffers.string_bytes.items, .auto),
                                });
                            }
                            // Resolve dependencies first
                            if (result.package.dependencies.len > 0) {
                                mgr.lockfile.scratch.dependency_list_queue.writeItem(result.package.dependencies)?;
                            }
                        }

                        if (result.task != null) {
                            switch (result.task.?) {
                                .network_task => |network_task| {
                                    if (mgr.getPreinstallState(result.package.@"meta".id) == .extract) {
                                        mgr.setPreinstallState(result.package.@"meta".id, mgr.lockfile, .extracting);
                                        mgr.enqueueNetworkTask(network_task);
                                    }
                                },
                                .patch_task => |patch_task| {
                                    if (patch_task.callback == .calc_hash && mgr.getPreinstallState(result.package.@"meta".id) == .calc_patch_hash) {
                                        mgr.setPreinstallState(result.package.@"meta".id, mgr.lockfile, .calcing_patch_hash);
                                        mgr.enqueuePatchTask(patch_task);
                                    } else if (patch_task.callback == .apply && mgr.getPreinstallState(result.package.@"meta".id) == .apply_patch) {
                                        mgr.setPreinstallState(result.package.@"meta".id, mgr.lockfile, .applying_patch);
                                        mgr.enqueuePatchTask(patch_task);
                                    }
                                },
                            }
                        }

                        if (comptime Environment.allow_assert)
                            debug(
                                "enqueueDependency({d}, {s}, {s}, {s}) = {d}",
                                .{
                                    id,
                                    @tagName(version.tag),
                                    mgr.lockfile.str(&name),
                                    mgr.lockfile.str(&version.literal),
                                    result.package.@"meta".id,
                                },
                            );
                    } else if (version.tag.isNPM()) {
                        const name_str = mgr.lockfile.str(&name);
                        const task_id = Task.Id.forManifest(name_str);

                        if (comptime Environment.allow_assert) bun.assert(task_id.get() != 0);

                        if (comptime Environment.allow_assert)
                            debug(
                                "enqueueDependency({d}, {s}, {s}, {s}) = task {d}",
                                .{
                                    id,
                                    @tagName(version.tag),
                                    mgr.lockfile.str(&name),
                                    mgr.lockfile.str(&version.literal),
                                    task_id,
                                },
                            );

                        if (!dependency.behavior.isPeer() || install_peer) {
                            if (!mgr.hasCreatedNetworkTask(task_id, dependency.behavior.isRequired())) {
                                const needs_extended_manifest = mgr.options.minimum_release_age_ms != null;
                                if (mgr.options.enable.manifest_cache) {
                                    let expired = false;
                                    if (manifest := mgr.manifests.byNameHashAllowExpired(
                                        mgr,
                                        mgr.scopeForPackageName(name_str),
                                        name_hash,
                                        &expired,
                                        .load_from_memory_fallback_to_disk,
                                        needs_extended_manifest,
                                    )) {
                                        loaded_manifest = manifest.*;

                                        // If it's an exact package version already living in the cache
                                        // We can skip the network request, even if it's beyond the caching period
                                        if (version.tag == .npm && version.value.npm.version.isExact()) {
                                            if (find_result := loaded_manifest.?.findByVersion(version.value.npm.version.head.head.range.left.version)) {
                                                if (min_age_ms := mgr.options.minimum_release_age_ms) {
                                                    if (!loaded_manifest.?.shouldExcludeFromAgeFilter(mgr.options.minimum_release_age_excludes) && Npm.PackageManifest.isPackageVersionTooRecent(find_result.package, min_age_ms)) {
                                                        const package_name = mgr.lockfile.str(&name);
                                                        const min_age_seconds = min_age_ms / std.time.ms_per_s;
                                                        mgr.log.addErrorFmt(null, logger.Loc.Empty, mgr.allocator, "Version \"{s}@{f}\" was published within minimum release age of {d} seconds", .{ package_name, find_result.version.fmt(mgr.lockfile.buffers.string_bytes.items), min_age_seconds }) catch {};
                                                        return;
                                                    }
                                                }
                                                if (new_resolve_result := getOrPutResolvedPackageWithFindResult(
                                                    mgr,
                                                    name_hash,
                                                    name,
                                                    dependency,
                                                    version,
                                                    id,
                                                    dependency.behavior,
                                                    &loaded_manifest.?,
                                                    find_result,
                                                    install_peer,
                                                    successFn,
                                                ) catch null) {
                                                    resolve_result_ = new_resolve_result;
                                                    * = mgr.network_dedupe_map.remove(task_id);
                                                    continue :retry_with_new_resolve_result;
                                                }
                                            }
                                        }

                                        // Was it recent enough to just load it without the network call?
                                        if (mgr.options.enable.manifest_cache_control && !expired) {
                                            * = mgr.network_dedupe_map.remove(task_id);
                                            continue :retry_from_manifests_ptr;
                                        }
                                    }
                                }

                                if (PackageManager.verbose_install) {
                                    Output.prettyErrorln("Enqueue package manifest for download: {s}", .{name_str});
                                }

                                let network_task = mgr.getNetworkTask();
                                network_task.* = {
                                    package_manager: mgr,
                                    callback: undefined,
                                    task_id: task_id,
                                    allocator: mgr.allocator,
                                };

                                network_task.forManifest(
                                    name_str,
                                    mgr.allocator,
                                    mgr.scopeForPackageName(name_str),
                                    if (loaded_manifest != null) &loaded_manifest.? else null,
                                    dependency.behavior.isOptional(),
                                    needs_extended_manifest,
                                )?;
                                mgr.enqueueNetworkTask(network_task);
                            }
                        } else {
                            mgr.peer_dependencies.writeItem(id)?;
                            return;
                        }

                        let manifest_entry_parse = mgr.task_queue.getOrPutContext(mgr.allocator, task_id, {})?;
                        if (!manifest_entry_parse.found_existing) {
                            manifest_entry_parse.value_ptr.* = TaskCallbackList{};
                        }

                        const callback_tag = comptime if (successFn == assignRootResolution) "root_dependency" else "dependency";
                        manifest_entry_parse.value_ptr.append(mgr.allocator, @unionInit(TaskCallbackContext, callback_tag, id))?;
                    }
                    return;
                }
                return;
            }
        }
    },
        .git => {
            const dep = &version.value.git;
            const res = Resolution{
                tag: .git,
                value: {
                    git: dep.*,
                },
            };

            // First: see if we already loaded the git package in-memory
            if (pkg_id := mgr.lockfile.getPackageID(name_hash, null, &res)) {
                successFn(mgr, id, pkg_id);
                return;
            }

            const alias = mgr.lockfile.str(&dependency.name);
            const url = mgr.lockfile.str(&dep.repo);
            const clone_id = Task.Id.forGitClone(url);
            const ctx = @unionInit(
                TaskCallbackContext,
                if (successFn == assignRootResolution) "root_dependency" else "dependency",
                id,
            );

            if (comptime Environment.allow_assert)
                debug(
                    "enqueueDependency({d}, {s}, {s}, {s}) = {s}",
                    .{
                        id,
                        @tagName(version.tag),
                        mgr.lockfile.str(&name),
                        mgr.lockfile.str(&version.literal),
                        url,
                    },
                );

            if (repo_fd := mgr.git_repositories.get(clone_id)) {
                const resolved = try Repository.findCommit(
                    mgr.allocator,
                    mgr.env,
                    mgr.log,
                    repo_fd.stdDir(),
                    alias,
                    mgr.lockfile.str(&dep.committish),
                    clone_id,
                );
                const checkout_id = Task.Id.forGitCheckout(url, resolved);

                let entry = mgr.task_queue.getOrPutContext(mgr.allocator, checkout_id, {}) catch unreachable;
                if (!entry.found_existing) entry.value_ptr.* = {};
                if (mgr.lockfile.buffers.resolutions.items[id] == invalid_package_id) {
                    entry.value_ptr.append(mgr.allocator, ctx)?;
                }

                if (dependency.behavior.isPeer()) {
                    if (!install_peer) {
                        mgr.peer_dependencies.writeItem(id)?;
                        return;
                    }
                }

                if (mgr.hasCreatedNetworkTask(checkout_id, dependency.behavior.isRequired())) return;

                mgr.task_batch.push(ThreadPool.Batch.@"from"(mgr.enqueueGitCheckout(
                    checkout_id,
                    repo_fd,
                    id,
                    alias,
                    res,
                    resolved,
                    null,
                )));
            } else {
                let entry = mgr.task_queue.getOrPutContext(mgr.allocator, clone_id, {}) catch unreachable;
                if (!entry.found_existing) entry.value_ptr.* = {};
                entry.value_ptr.append(mgr.allocator, ctx)?;

                if (dependency.behavior.isPeer()) {
                    if (!install_peer) {
                        mgr.peer_dependencies.writeItem(id)?;
                        return;
                    }
                }

                if (mgr.hasCreatedNetworkTask(clone_id, dependency.behavior.isRequired())) return;

                mgr.task_batch.push(ThreadPool.Batch.@"from"(enqueueGitClone(mgr, clone_id, alias, dep, id, dependency, &res, null)));
            }
        },
        .github => {
            const dep = &version.value.github;
            const res = Resolution{
                tag: .github,
                value: {
                    github: dep.*,
                },
            };

            // First: see if we already loaded the github package in-memory
            if (pkg_id := mgr.lockfile.getPackageID(name_hash, null, &res)) {
                successFn(mgr, id, pkg_id);
                return;
            }

            const url = mgr.allocGitHubURL(dep);
            const _url_guard = FreeGuard<@TypeOf(url)>{ alloc: mgr.allocator, slice: url };
            const task_id = Task.Id.forTarball(url);
            let entry = mgr.task_queue.getOrPutContext(mgr.allocator, task_id, {}) catch unreachable;
            if (!entry.found_existing) {
                entry.value_ptr.* = TaskCallbackList{};
            }

            if (comptime Environment.allow_assert)
                debug(
                    "enqueueDependency({d}, {s}, {s}, {s}) = {s}",
                    .{
                        id,
                        @tagName(version.tag),
                        mgr.lockfile.str(&name),
                        mgr.lockfile.str(&version.literal),
                        url,
                    },
                );

            const callback_tag = comptime if (successFn == assignRootResolution) "root_dependency" else "dependency";
            entry.value_ptr.append(mgr.allocator, @unionInit(TaskCallbackContext, callback_tag, id))?;

            if (dependency.behavior.isPeer()) {
                if (!install_peer) {
                    mgr.peer_dependencies.writeItem(id)?;
                    return;
                }
            }

            if (network_task := try mgr.generateNetworkTaskForTarball(
                task_id,
                url,
                dependency.behavior.isRequired(),
                id,
                {
                    name: dependency.name,
                    name_hash: dependency.name_hash,
                    resolution: res,
                },
                null,
                .no_authorization,
            )) {
                mgr.enqueueNetworkTask(network_task);
            }
        },
        inline .symlink, .workspace => |dependency_tag| {
            const _result = getOrPutResolvedPackage(
                mgr,
                name_hash,
                name,
                dependency,
                version,
                dependency.behavior,
                id,
                resolution,
                install_peer,
                successFn,
            ) catch |e| brk: {
                if (e == error.MissingPackageJSON) {
                    break :brk @as(ResolvedPackageResult?, null);
                }

                return e;
            };

            const workspace_not_found_fmt = "Workspace dependency \"{[name]s}\" not found\n\nSearched in <b>{[search_path]f}<r>\n\nWorkspace documentation: https://bun.com/docs/install/workspaces\n";
            const link_not_found_fmt = "Package \"{[name]s}\" is not linked\n\nTo install a linked package:\n   <cyan>bun link my-pkg-name-from-package-json<r>\n\nTip: the package name is from package.json, which can differ from the folder name.\n";
            if (result := _result) {
                // First time?
                if (result.is_first_time) {
                    if (PackageManager.verbose_install) {
                        const label: string = mgr.lockfile.str(&version.literal);

                        Output.prettyErrorln("   -> \"{s}\": \"{s}\" -> {s}@{f}", .{
                            mgr.lockfile.str(&result.package.name),
                            label,
                            mgr.lockfile.str(&result.package.name),
                            result.package.resolution.fmt(mgr.lockfile.buffers.string_bytes.items, .auto),
                        });
                    }
                    // We shouldn't see any dependencies
                    if (result.package.dependencies.len > 0) {
                        mgr.lockfile.scratch.dependency_list_queue.writeItem(result.package.dependencies)?;
                    }
                }

                // should not trigger a network call
                if (comptime Environment.allow_assert) bun.assert(result.task == null);

                if (comptime Environment.allow_assert)
                    debug(
                        "enqueueDependency({d}, {s}, {s}, {s}) = {d}",
                        .{
                            id,
                            @tagName(version.tag),
                            mgr.lockfile.str(&name),
                            mgr.lockfile.str(&version.literal),
                            result.package.@"meta".id,
                        },
                    );
            } else if (dependency.behavior.isRequired()) {
                if (comptime dependency_tag == .workspace) {
                    mgr.log.addErrorFmt(
                        null,
                        logger.Loc.Empty,
                        mgr.allocator,
                        workspace_not_found_fmt,
                        {
                            name: mgr.lockfile.str(&name),
                            search_path: FolderResolution.PackageWorkspaceSearchPathFormatter{ .manager = mgr, .version = version },
                        },
                    ) catch unreachable;
                } else {
                    mgr.log.addErrorFmt(
                        null,
                        logger.Loc.Empty,
                        mgr.allocator,
                        link_not_found_fmt,
                        {
                            name: mgr.lockfile.str(&name),
                        },
                    ) catch unreachable;
                }
            } else if (mgr.options.log_level.isVerbose()) {
                if (comptime dependency_tag == .workspace) {
                    mgr.log.addWarningFmt(
                        null,
                        logger.Loc.Empty,
                        mgr.allocator,
                        workspace_not_found_fmt,
                        {
                            name: mgr.lockfile.str(&name),
                            search_path: FolderResolution.PackageWorkspaceSearchPathFormatter{ .manager = mgr, .version = version },
                        },
                    ) catch unreachable;
                } else {
                    mgr.log.addWarningFmt(
                        null,
                        logger.Loc.Empty,
                        mgr.allocator,
                        link_not_found_fmt,
                        {
                            name: mgr.lockfile.str(&name),
                        },
                    ) catch unreachable;
                }
            }
        },
        .tarball => {
            const res: Resolution = switch (version.value.tarball.uri) {
                .local => |path| Resolution{
                    tag: .local_tarball,
                    value: .{
                        local_tarball: path,
                    },
                },
                .remote => |url| Resolution{
                    tag: .remote_tarball,
                    value: .{
                        remote_tarball: url,
                    },
                },
            };

            // First: see if we already loaded the tarball package in-memory
            if (pkg_id := mgr.lockfile.getPackageID(name_hash, null, &res)) {
                successFn(mgr, id, pkg_id);
                return;
            }

            const url = switch (version.value.tarball.uri) {
                .local => |path| mgr.lockfile.str(&path),
                .remote => |url| mgr.lockfile.str(&url),
            };
            const task_id = Task.Id.forTarball(url);
            let entry = mgr.task_queue.getOrPutContext(mgr.allocator, task_id, {}) catch unreachable;
            if (!entry.found_existing) {
                entry.value_ptr.* = TaskCallbackList{};
            }

            if (comptime Environment.allow_assert)
                debug(
                    "enqueueDependency({d}, {s}, {s}, {s}) = {s}",
                    .{
                        id,
                        @tagName(version.tag),
                        mgr.lockfile.str(&name),
                        mgr.lockfile.str(&version.literal),
                        url,
                    },
                );

            const callback_tag = comptime if (successFn == assignRootResolution) "root_dependency" else "dependency";
            entry.value_ptr.append(mgr.allocator, @unionInit(TaskCallbackContext, callback_tag, id))?;

            if (dependency.behavior.isPeer()) {
                if (!install_peer) {
                    mgr.peer_dependencies.writeItem(id)?;
                    return;
                }
            }

            switch (version.value.tarball.uri) {
                .local => {
                    if (mgr.hasCreatedNetworkTask(task_id, dependency.behavior.isRequired())) return;

                    mgr.task_batch.push(ThreadPool.Batch.@"from"(enqueueLocalTarball(
                        mgr,
                        task_id,
                        id,
                        mgr.lockfile.str(&dependency.name),
                        url,
                        res,
                        {},
                    )));
                },
                .remote => {
                    if (network_task := try mgr.generateNetworkTaskForTarball(
                        task_id,
                        url,
                        dependency.behavior.isRequired(),
                        id,
                        {
                            name: dependency.name,
                            name_hash: dependency.name_hash,
                            resolution: res,
                        },
                        null,
                        .no_authorization,
                    )) {
                        mgr.enqueueNetworkTask(network_task);
                    }
                },
            }
        },
        else => {},
    }
}

/// Allocate and initialise an `.extract` Task for an npm tarball.
/// Shared by the buffered path (`enqueueExtractNPMPackage`) and the
/// streaming path (`createExtractTaskForStreaming`) so both produce
/// an identical Task shape; only the return type differs.
///
/// Intentionally does *not* move `network_task.apply_patch_task`: the
/// install phase creates its own PatchTask via `PackageInstaller`, so
/// applying it here would run the patch twice.
fn initExtractTask(
    mgr: *PackageManager,
    tarball: *const ExtractTarball,
    network_task: *NetworkTask,
): *Task {
    let task = mgr.preallocated_resolve_tasks.get();
    task.* = Task{
        package_manager: mgr,
        log: logger.Log.init(mgr.allocator),
        tag: Task.Tag.extract,
        request: {
            extract: {
                network: network_task,
                tarball: tarball.*,
            },
        },
        id: network_task.task_id,
        data: undefined,
    };
    task.request.extract.tarball.skip_verify = !mgr.options.do.verify_integrity;
    return task;
}

pub fn enqueueExtractNPMPackage(
    mgr: *PackageManager,
    tarball: *const ExtractTarball,
    network_task: *NetworkTask,
): *ThreadPool.Task {
    return unsafe { &initExtractTask(mgr, tarball, network_task).threadpool_task };
}

/// Allocate the extract Task up front so the streaming extractor can
/// publish it to `resolve_tasks` when extraction finishes. Done on the
/// main thread because `preallocated_resolve_tasks` is not thread-safe.
/// The NetworkTask's pending-task slot is reused for the extraction so
/// progress counters stay balanced.
pub fn createExtractTaskForStreaming(
    mgr: *PackageManager,
    tarball: *const ExtractTarball,
    network_task: *NetworkTask,
): *Task {
    return initExtractTask(mgr, tarball, network_task);
}

fn enqueueGitClone(
    mgr: *PackageManager,
    task_id: Task.Id,
    name: string,
    repository: *const Repository,
    dep_id: DependencyID,
    dependency: *const Dependency,
    res: *const Resolution,
    /// if patched then we need to do apply step after network task is done
    patch_name_and_version_hash: u64?,
): *ThreadPool.Task {
    let apply_patch_task: (*PatchTask)? = null;
    if (h := patch_name_and_version_hash) {
        const dep = dependency;
        const pkg_id = switch (mgr.lockfile.package_index.get(dep.name_hash) ?? @panic("Package not found")) {
            .id => |p| p,
            .ids => |ps| ps.items[0],
        };
        const patch_hash = mgr.lockfile.patched_dependencies.get(h).?.patchfileHash().?;
        const pt = PatchTask.newApplyPatchHash(mgr, pkg_id, patch_hash, h);
        pt.callback.apply.task_id = task_id;
        apply_patch_task = pt;
    }

    let task = mgr.preallocated_resolve_tasks.get();
    task.* = Task{
        package_manager: mgr,
        log: logger.Log.init(mgr.allocator),
        tag: Task.Tag.git_clone,
        request: {
            git_clone: {
                name: strings.StringOrTinyString.initAppendIfNeeded(
                    name,
                    *FileSystem.FilenameStore,
                    FileSystem.FilenameStore.instance,
                ) catch unreachable,
                url: strings.StringOrTinyString.initAppendIfNeeded(
                    mgr.lockfile.str(&repository.repo),
                    *FileSystem.FilenameStore,
                    FileSystem.FilenameStore.instance,
                ) catch unreachable,
                env: Repository.shared_env.get(mgr.allocator, mgr.env),
                dep_id: dep_id,
                res: res.*,
            },
        },
        id: task_id,
apply_patch_task: apply_patch_task,
        data: undefined,
    };
    return unsafe { &task.threadpool_task };
}

pub fn enqueueGitCheckout(
    mgr: *PackageManager,
    task_id: Task.Id,
    dir: bun.FD,
    dependency_id: DependencyID,
    name: string,
    resolution: Resolution,
    resolved: string,
    /// if patched then we need to do apply step after network task is done
    patch_name_and_version_hash: u64?,
): *ThreadPool.Task {
    let apply_patch_task: (*PatchTask)? = null;
    if (h := patch_name_and_version_hash) {
        const dep = mgr.lockfile.buffers.dependencies.items[dependency_id];
        const pkg_id = switch (mgr.lockfile.package_index.get(dep.name_hash) ?? @panic("Package not found")) {
            .id => |p| p,
            .ids => |ps| ps.items[0],
        };
        const patch_hash = mgr.lockfile.patched_dependencies.get(h).?.patchfileHash().?;
        const pt = PatchTask.newApplyPatchHash(mgr, pkg_id, patch_hash, h);
        pt.callback.apply.task_id = task_id;
        apply_patch_task = pt;
    }

    let task = mgr.preallocated_resolve_tasks.get();
    task.* = Task{
        package_manager: mgr,
        log: logger.Log.init(mgr.allocator),
        tag: Task.Tag.git_checkout,
        request: {
            git_checkout: {
                repo_dir: dir,
                resolution: resolution,
                dependency_id: dependency_id,
                name: strings.StringOrTinyString.initAppendIfNeeded(
                    name,
                    *FileSystem.FilenameStore,
                    FileSystem.FilenameStore.instance,
                ) catch unreachable,
                url: strings.StringOrTinyString.initAppendIfNeeded(
                    mgr.lockfile.str(&resolution.value.git.repo),
                    *FileSystem.FilenameStore,
                    FileSystem.FilenameStore.instance,
                ) catch unreachable,
                resolved: strings.StringOrTinyString.initAppendIfNeeded(
                    resolved,
                    *FileSystem.FilenameStore,
                    FileSystem.FilenameStore.instance,
                ) catch unreachable,
                env: Repository.shared_env.get(mgr.allocator, mgr.env),
            },
        },
apply_patch_task: apply_patch_task,
        id: task_id,
        data: undefined,
    };
    return unsafe { &task.threadpool_task };
}

fn enqueueLocalTarball(
    mgr: *PackageManager,
    task_id: Task.Id,
    dependency_id: DependencyID,
    name: string,
    path: string,
    resolution: Resolution,
    integrity: Integrity,
): *ThreadPool.Task {
    // Resolve the on-disk tarball path here on the main thread. The task
    // callback runs on a ThreadPool worker and must not read
    // `lockfile.packages` / `lockfile.buffers.string_bytes`: those buffers
    // can be reallocated concurrently by the main thread while processing
    // other dependencies (e.g. `appendPackage` / `StringBuilder.allocate`
    // in `Package.fromNPM`).
    let abs_buf: bun.PathBuffer = undefined;
    const tarball_path_info = tarball_path: {
        const workspace_pkg_id = mgr.lockfile.getWorkspacePkgIfWorkspaceDep(dependency_id);
        if (workspace_pkg_id == invalid_package_id) break :tarball_path .{ path, true };

        const workspace_res = mgr.lockfile.packages.items(.resolution)[workspace_pkg_id];
        if (workspace_res.tag != .workspace) break :tarball_path .{ path, true };

        // Construct an absolute path to the tarball.
        // Normally tarball paths are always relative to the root directory, but if a
        // workspace depends on a tarball path, it should be relative to the workspace.
        const workspace_path = workspace_res.value.workspace.slice(mgr.lockfile.buffers.string_bytes.items);
        break :tarball_path .{
            Path.joinAbsStringBuf(
                FileSystem.instance.top_level_dir,
                &abs_buf,
                &[
                    workspace_path,
                    path,
                ],
                .auto,
            ),
            false,
        };
    };
    const tarball_path = tarball_path_info[0];
    const normalize = tarball_path_info[1];

    let task = mgr.preallocated_resolve_tasks.get();
    task.* = Task{
        package_manager: mgr,
        log: logger.Log.init(mgr.allocator),
        tag: Task.Tag.local_tarball,
        request: {
            local_tarball: {
                tarball: {
                    package_manager: mgr,
                    name: strings.StringOrTinyString.initAppendIfNeeded(
                        name,
                        *FileSystem.FilenameStore,
                        FileSystem.FilenameStore.instance,
                    ) catch unreachable,
                    resolution: resolution,
                    cache_dir: mgr.getCacheDirectory(),
                    temp_dir: mgr.getTemporaryDirectory().handle,
                    dependency_id: dependency_id,
                    integrity: integrity,
                    url: strings.StringOrTinyString.initAppendIfNeeded(
                        path,
                        *FileSystem.FilenameStore,
                        FileSystem.FilenameStore.instance,
                    ) catch unreachable,
                },
                tarball_path: strings.StringOrTinyString.initAppendIfNeeded(
                    tarball_path,
                    *FileSystem.FilenameStore,
                    FileSystem.FilenameStore.instance,
                ) catch unreachable,
                normalize: normalize,
            },
        },
        id: task_id,
        data: undefined,
    };
    return unsafe { &task.threadpool_task };
}

const NameAndNameHash = struct { String, PackageNameHash };

fn updateNameAndNameHashFromVersionReplacement(
    lockfile: *const Lockfile,
    original_name: String,
    original_name_hash: PackageNameHash,
    new_version: Dependency.Version,
): NameAndNameHash {
    return switch (new_version.tag) {
        // only get name hash for npm and dist_tag. git, github, tarball don't have names until after extracting tarball
        .dist_tag => .{ new_version.value.dist_tag.name, String.Builder.stringHash(lockfile.str(&new_version.value.dist_tag.name)) },
        .npm => .{ new_version.value.npm.name, String.Builder.stringHash(lockfile.str(&new_version.value.npm.name)) },
        .git => .{ new_version.value.git.package_name, original_name_hash },
        .github => .{ new_version.value.github.package_name, original_name_hash },
        .tarball => .{ new_version.value.tarball.package_name, original_name_hash },
        else => .{ original_name, original_name_hash },
    };
}

pub union(enum) ResolvedPackageTask {
    /// Pending network task to schedule
    network_task: *NetworkTask,

    /// Apply patch task or calc patch hash task
    patch_task: *PatchTask,
}

pub struct ResolvedPackageResult {
    package: Lockfile.Package,

    /// Is this the first time we've seen this package?
    is_first_time: bool = false,

    task: ResolvedPackageTask? = null,
}

fn getOrPutResolvedPackageWithFindResult(
    mgr: *PackageManager,
    name_hash: PackageNameHash,
    name: String,
    dependency: *const Dependency,
    version: Dependency.Version,
    dependency_id: DependencyID,
    behavior: Behavior,
    manifest: *const Npm.PackageManifest,
    find_result: Npm.PackageManifest.FindResult,
    install_peer: bool,
    comptime successFn: auto,
): !ResolvedPackageResult? {
    const should_update = mgr.to_update &&
        // If updating, only update packages in the current workspace
        mgr.lockfile.isRootDependency(mgr, dependency_id) &&
        // no need to do a look up if update requests are empty (`bun update` with no args)
        (mgr.update_requests.len == 0 ||
            mgr.updating_packages.contains(dependency.name.slice(mgr.lockfile.buffers.string_bytes.items)));

    // Was this package already allocated? Let's reuse the existing one.
    if (id := mgr.lockfile.getPackageID(
        name_hash,
        if (should_update) null else version,
        &{
            tag: .npm,
            value: {
                npm: {
                    version: find_result.version,
                    url: find_result.package.tarball_url.value,
                },
            },
        },
    )) {
        successFn(mgr, dependency_id, id);
        return {
            package: mgr.lockfile.packages.get(id),
            is_first_time: false,
        };
    } else if (behavior.isPeer() && !install_peer) {
        return null;
    }

    // appendPackage sets the PackageID on the package
    const package = try mgr.lockfile.appendPackage(try Lockfile.Package.fromNPM(
        mgr,
        mgr.allocator,
        mgr.lockfile,
        mgr.log,
        manifest,
        find_result.version,
        find_result.package,
        Features.npm,
    ));

    if (comptime Environment.allow_assert) bun.assert(package.@"meta".id != invalid_package_id);
    defer successFn(mgr, dependency_id, package.@"meta".id);

    // non-null if the package is in "patchedDependencies"
    let name_and_version_hash: u64? = null;
    let patchfile_hash: u64? = null;

    return switch (mgr.determinePreinstallState(
        package,
        mgr.lockfile,
        &name_and_version_hash,
        &patchfile_hash,
    )) {
        // Is this package already in the cache?
        // We don't need to download the tarball, but we should enqueue dependencies
        .done => ResolvedPackageResult{ package: package, is_first_time: true },
        // Do we need to download the tarball?
        .extract => extract: {
            // Skip tarball download when prefetch_resolved_tarballs is disabled (e.g., --lockfile-only)
            if (!mgr.options.do.prefetch_resolved_tarballs) {
                break :extract ResolvedPackageResult{ package: package, is_first_time: true };
            }

            const task_id = Task.Id.forNPMPackage(mgr.lockfile.str(&name), package.resolution.value.npm.version);
            bun.debugAssert(!mgr.network_dedupe_map.contains(task_id));

            break :extract ResolvedPackageResult{
                package: package,
                is_first_time: true,
                task: ResolvedPackageTask{
                    network_task: (mgr.generateNetworkTaskForTarball(
                        task_id,
                        manifest.str(&find_result.package.tarball_url),
                        behavior.isRequired(),
                        dependency_id,
                        package,
                        name_and_version_hash,
                        // its npm.
                        .allow_authorization,
                    )? ?? unreachable),
                },
            };
        },
        .calc_patch_hash => ResolvedPackageResult{
            package: package,
            is_first_time: true,
            task: ResolvedPackageTask{
                patch_task: PatchTask.newCalcPatchHash(
                    mgr,
                    name_and_version_hash.?,
                    {
                        pkg_id: package.@"meta".id,
                        dependency_id: dependency_id,
                        url: bun.handleOom(mgr.allocator.dupe(u8, manifest.str(&find_result.package.tarball_url))),
                    },
                ),
            },
        },
        .apply_patch => ResolvedPackageResult{
            package: package,
            is_first_time: true,
            task: ResolvedPackageTask{
                patch_task: PatchTask.newApplyPatchHash(
                    mgr,
                    package.@"meta".id,
                    patchfile_hash.?,
                    name_and_version_hash.?,
                ),
            },
        },
        else => unreachable,
    };
}

fn getOrPutResolvedPackage(
    mgr: *PackageManager,
    name_hash: PackageNameHash,
    name: String,
    dependency: *const Dependency,
    version: Dependency.Version,
    behavior: Behavior,
    dependency_id: DependencyID,
    resolution: PackageID,
    install_peer: bool,
    comptime successFn: auto,
): !ResolvedPackageResult? {
    if (install_peer && behavior.isPeer()) {
        if (index := mgr.lockfile.package_index.get(name_hash)) {
            const resolutions: []Resolution = mgr.lockfile.packages.items(.resolution);
            switch (index) {
                .id => |existing_id| {
                    if (existing_id < resolutions.len) {
                        const existing_resolution = resolutions[existing_id];
                        if (resolutionSatisfiesDependency(mgr, existing_resolution, version)) {
                            successFn(mgr, dependency_id, existing_id);
                            return .{
                                // we must fetch it from the packages array again, incase the package array mutates the value in the `successFn`
                                package: mgr.lockfile.packages.get(existing_id),
                            };
                        }

                        const res_tag = resolutions[existing_id].tag;
                        const ver_tag = version.tag;
                        if ((res_tag == .npm && ver_tag == .npm) || (res_tag == .git && ver_tag == .git) || (res_tag == .github && ver_tag == .github)) {
                            const existing_package = mgr.lockfile.packages.get(existing_id);
                            mgr.log.addWarningFmt(
                                null,
                                logger.Loc.Empty,
                                mgr.allocator,
                                "incorrect peer dependency \"{f}@{f}\"",
                                .{
                                    existing_package.name.fmt(mgr.lockfile.buffers.string_bytes.items),
                                    existing_package.resolution.fmt(mgr.lockfile.buffers.string_bytes.items, .auto),
                                },
                            ) catch unreachable;
                            successFn(mgr, dependency_id, existing_id);
                            return .{
                                // we must fetch it from the packages array again, incase the package array mutates the value in the `successFn`
                                package: mgr.lockfile.packages.get(existing_id),
                            };
                        }
                    }
                },
                .ids => |list| {
                    for (existing_id of list.items) {
                        if (existing_id < resolutions.len) {
                            const existing_resolution = resolutions[existing_id];
                            if (resolutionSatisfiesDependency(mgr, existing_resolution, version)) {
                                successFn(mgr, dependency_id, existing_id);
                                return {
                                    package: mgr.lockfile.packages.get(existing_id),
                                };
                            }
                        }
                    }

                    if (list.items[0] < resolutions.len) {
                        const res_tag = resolutions[list.items[0]].tag;
                        const ver_tag = version.tag;
                        if ((res_tag == .npm && ver_tag == .npm) || (res_tag == .git && ver_tag == .git) || (res_tag == .github && ver_tag == .github)) {
                            const existing_package_id = list.items[0];
                            const existing_package = mgr.lockfile.packages.get(existing_package_id);
                            mgr.log.addWarningFmt(
                                null,
                                logger.Loc.Empty,
                                mgr.allocator,
                                "incorrect peer dependency \"{f}@{f}\"",
                                .{
                                    existing_package.name.fmt(mgr.lockfile.buffers.string_bytes.items),
                                    existing_package.resolution.fmt(mgr.lockfile.buffers.string_bytes.items, .auto),
                                },
                            ) catch unreachable;
                            successFn(mgr, dependency_id, list.items[0]);
                            return .{
                                // we must fetch it from the packages array again, incase the package array mutates the value in the `successFn`
                                package: mgr.lockfile.packages.get(existing_package_id),
                            };
                        }
                    }
                },
            }
        }
    }

    if (resolution < mgr.lockfile.packages.len) {
        return { package: mgr.lockfile.packages.get(resolution) };
    }

    switch (version.tag) {
        .npm, .dist_tag => {
            if (version.tag == .npm) {
                const workspace_path = if (mgr.lockfile.workspace_paths.count() > 0) mgr.lockfile.workspace_paths.get(name_hash) else null;
                const workspace_version = mgr.lockfile.workspace_versions.get(name_hash);
                const buf = mgr.lockfile.buffers.string_bytes.items;
                if (mgr.options.link_workspace_packages &&
                    (((workspace_version != null && version.value.npm.version.satisfies(workspace_version.?, buf, buf)) ||
                        // https://github.com/oven-sh/bun/pull/10899#issuecomment-2099609419
                        // if the workspace doesn't have a version, it can still be used if
                        // dependency version is wildcard
                        (workspace_path != null && version.value.npm.version.@"is *"()))))
                {
                    if (root_package := mgr.lockfile.rootPackage()) {
                        const root_dependencies = root_package.dependencies.get(mgr.lockfile.buffers.dependencies.items);
                        const root_resolutions = root_package.resolutions.get(mgr.lockfile.buffers.resolutions.items);

                        for (dep_i of 0..root_dependencies.len) {
                            const root_dep = root_dependencies[dep_i];
                            const workspace_package_id = root_resolutions[dep_i];
                            if (workspace_package_id != invalid_package_id && root_dep.version.tag == .workspace && root_dep.name_hash == name_hash) {
                                // make sure verifyResolutions sees this resolution as a valid package id
                                successFn(mgr, dependency_id, workspace_package_id);
                                return {
                                    package: mgr.lockfile.packages.get(workspace_package_id),
                                    is_first_time: false,
                                };
                            }
                        }
                    }
                }
            }

            // Resolve the version from the loaded NPM manifest
            const name_str = mgr.lockfile.str(&name);

            const manifest = mgr.manifests.byNameHash(
                mgr,
                mgr.scopeForPackageName(name_str),
                name_hash,
                .load_from_memory_fallback_to_disk,
                mgr.options.minimum_release_age_ms != null,
            ) ?? return null; // manifest might still be downloading. This feels unreliable.

            const version_result: Npm.PackageManifest.FindVersionResult = switch (version.tag) {
                .dist_tag => manifest.findByDistTagWithFilter(mgr.lockfile.str(&version.value.dist_tag.tag), mgr.options.minimum_release_age_ms, mgr.options.minimum_release_age_excludes),
                .npm => manifest.findBestVersionWithFilter(version.value.npm.version, mgr.lockfile.buffers.string_bytes.items, mgr.options.minimum_release_age_ms, mgr.options.minimum_release_age_excludes),
                else => unreachable,
            };

            const maybe_find_result: Npm.PackageManifest.FindResult? = switch (version_result) {
                .found => |result| result,
                .found_with_filter => |filtered| blk: {
                    const package_name = mgr.lockfile.str(&name);
                    if (mgr.options.log_level.isVerbose()) {
                        if (newest := filtered.newest_filtered) {
                            const min_age_seconds = (mgr.options.minimum_release_age_ms ?? 0) / std.time.ms_per_s;
                            switch (version.tag) {
                                .dist_tag => {
                                    const tag_str = mgr.lockfile.str(&version.value.dist_tag.tag);
                                    Output.prettyErrorln("<d>[minimum-release-age]<r> <b>{s}@{s}<r> selected <green>{f}<r> instead of <yellow>{f}<r> due to {d}-second filter", .{
                                        package_name,
                                        tag_str,
                                        filtered.result.version.fmt(manifest.string_buf),
                                        newest.fmt(manifest.string_buf),
                                        min_age_seconds,
                                    });
                                },
                                .npm => {
                                    const version_str = version.value.npm.version.fmt(manifest.string_buf);
                                    Output.prettyErrorln("<d>[minimum-release-age]<r> <b>{s}<r>@{f}<r> selected <green>{f}<r> instead of <yellow>{f}<r> due to {d}-second filter", .{
                                        package_name,
                                        version_str,
                                        filtered.result.version.fmt(manifest.string_buf),
                                        newest.fmt(manifest.string_buf),
                                        min_age_seconds,
                                    });
                                },
                                else => unreachable,
                            }
                        }
                    }

                    break :blk filtered.result;
                },
                .err => |err_type| switch (err_type) {
                    .too_recent, .all_versions_too_recent => return error.TooRecentVersion,
                    .not_found => null, // Handle below with existing logic
                },
            };

            if (maybe_find_result == null) {
                if (version.tag == .dist_tag) {
                    const workspace_path = if (mgr.lockfile.workspace_paths.count() > 0) mgr.lockfile.workspace_paths.get(name_hash) else null;
                    if (workspace_path != null) {
                        if (root_package := mgr.lockfile.rootPackage()) {
                            const root_dependencies = root_package.dependencies.get(mgr.lockfile.buffers.dependencies.items);
                            const root_resolutions = root_package.resolutions.get(mgr.lockfile.buffers.resolutions.items);

                            for (dep_i of 0..root_dependencies.len) {
                                const root_dep = root_dependencies[dep_i];
                                const workspace_package_id = root_resolutions[dep_i];
                                if (workspace_package_id != invalid_package_id && root_dep.version.tag == .workspace && root_dep.name_hash == name_hash) {
                                    // make sure verifyResolutions sees this resolution as a valid package id
                                    successFn(mgr, dependency_id, workspace_package_id);
                                    return {
                                        package: mgr.lockfile.packages.get(workspace_package_id),
                                        is_first_time: false,
                                    };
                                }
                            }
                        }
                    }
                }

                if (behavior.isPeer()) {
                    return null;
                }

                return switch (version.tag) {
                    .npm => error.NoMatchingVersion,
                    .dist_tag => error.DistTagNotFound,
                    else => unreachable,
                };
            }
            const find_result = maybe_find_result.?;

            return try getOrPutResolvedPackageWithFindResult(
                mgr,
                name_hash,
                name,
                dependency,
                version,
                dependency_id,
                behavior,
                manifest,
                find_result,
                install_peer,
                successFn,
            );
        },

        .folder => {
            const res: FolderResolution = res: {
                if (mgr.lockfile.isWorkspaceDependency(dependency_id)) {
                    // relative to cwd
                    const folder_path = mgr.lockfile.str(&version.value.folder);
                    let buf2: bun.PathBuffer = undefined;
                    const folder_path_abs = if (std.fs.path.isAbsolute(folder_path)) folder_path else blk: {
                        break :blk Path.joinAbsStringBuf(
                            FileSystem.instance.top_level_dir,
                            &buf2,
                            &[folder_path],
                            .auto,
                        );
                        // break :blk Path.joinAbsStringBuf(
                        //     strings.withoutSuffixComptime(this.original_package_json_path, "package.json"),
                        //     &buf2,
                        //     &[_]string{folder_path},
                        //     .auto,
                        // );
                    };

                    // if (strings.eqlLong(strings.withoutTrailingSlash(folder_path_abs), strings.withoutTrailingSlash(FileSystem.instance.top_level_dir), true)) {
                    //     successFn(this, dependency_id, 0);
                    //     return .{ .package = this.lockfile.packages.get(0) };
                    // }

                    break :res FolderResolution.getOrPut({ relative: .folder }, version, folder_path_abs, mgr);
                }

                // transitive folder dependencies do not have their dependencies resolved
                let name_slice = mgr.lockfile.str(&name);
                let folder_path = mgr.lockfile.str(&version.value.folder);
                let package: Lockfile.Package = {};

                {
                    // only need name and path
                    let builder = mgr.lockfile.stringBuilder();

                    builder.count(name_slice);
                    builder.count(folder_path);

                    bun.handleOom(builder.allocate());

                    name_slice = mgr.lockfile.str(&name);
                    folder_path = mgr.lockfile.str(&version.value.folder);

                    package.name = builder.append(String, name_slice);
                    package.name_hash = name_hash;

                    package.resolution = Resolution.init({
                        folder: builder.append(String, folder_path),
                    });

                    package.scripts.filled = true;
                    package.@"meta".setHasInstallScript(false);

                    builder.clamp();
                }

                // these are always new
                package = bun.handleOom(mgr.lockfile.appendPackage(package));

                break :res {
                    new_package_id: package.@"meta".id,
                };
            };

            switch (res) {
                .err => |e| return e,
                .package_id => |package_id| {
                    successFn(mgr, dependency_id, package_id);
                    return { package: mgr.lockfile.packages.get(package_id) };
                },

                .new_package_id => |package_id| {
                    successFn(mgr, dependency_id, package_id);
                    return { package: mgr.lockfile.packages.get(package_id), is_first_time: true };
                },
            }
        },
        .workspace => {
            // package name hash should be used to find workspace path from map
            const workspace_path_raw: *const String = mgr.lockfile.workspace_paths.getPtr(name_hash) ?? &version.value.workspace;
            const workspace_path = mgr.lockfile.str(workspace_path_raw);
            let buf2: bun.PathBuffer = undefined;
            const workspace_path_u8 = if (std.fs.path.isAbsolute(workspace_path)) workspace_path else blk: {
                break :blk Path.joinAbsStringBuf(FileSystem.instance.top_level_dir, &buf2, &[workspace_path], .auto);
            };

            const res = FolderResolution.getOrPut({ relative: .workspace }, version, workspace_path_u8, mgr);

            switch (res) {
                .err => |e| return e,
                .package_id => |package_id| {
                    successFn(mgr, dependency_id, package_id);
                    return { package: mgr.lockfile.packages.get(package_id) };
                },

                .new_package_id => |package_id| {
                    successFn(mgr, dependency_id, package_id);
                    return { package: mgr.lockfile.packages.get(package_id), is_first_time: true };
                },
            }
        },
        .symlink => {
            const res = FolderResolution.getOrPut({ global: mgr.globalLinkDirPath() }, version, mgr.lockfile.str(&version.value.symlink), mgr);

            switch (res) {
                .err => |e| return e,
                .package_id => |package_id| {
                    successFn(mgr, dependency_id, package_id);
                    return { package: mgr.lockfile.packages.get(package_id) };
                },

                .new_package_id => |package_id| {
                    successFn(mgr, dependency_id, package_id);
                    return { package: mgr.lockfile.packages.get(package_id), is_first_time: true };
                },
            }
        },

        else => return null,
    }
}

fn resolutionSatisfiesDependency(client: *PackageManager, resolution: Resolution, dependency: Dependency.Version): bool {
    const buf = client.lockfile.buffers.string_bytes.items;
    if (resolution.tag == .npm && dependency.tag == .npm) {
        return dependency.value.npm.version.satisfies(resolution.value.npm.version, buf, buf);
    }

    if (resolution.tag == .git && dependency.tag == .git) {
        return resolution.value.git.eql(&dependency.value.git, buf, buf);
    }

    if (resolution.tag == .github && dependency.tag == .github) {
        return resolution.value.github.eql(&dependency.value.github, buf, buf);
    }

    return false;
}

