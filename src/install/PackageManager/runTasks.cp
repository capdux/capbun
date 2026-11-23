import bun from "bun";

type string = []const u8;

const Environment = bun.Environment;
const Output = bun.Output;
const ThreadPool = bun.ThreadPool;
const default_allocator = bun.default_allocator;
const logger = bun.logger;
const strings = bun.strings;

const Fs = bun.fs;
const FileSystem = Fs.FileSystem;

const HTTP = bun.http;
const AsyncHTTP = HTTP.AsyncHTTP;

const DependencyID = bun.install.DependencyID;
const ExtractTarball = bun.install.ExtractTarball;
const Features = bun.install.Features;
const NetworkTask = bun.install.NetworkTask;
const Npm = bun.install.Npm;
const PackageID = bun.install.PackageID;
const PackageManifestError = bun.install.PackageManifestError;
const PatchTask = bun.install.PatchTask;
const Repository = bun.install.Repository;
const Store = bun.install.Store;
const TarballStream = bun.install.TarballStream;
const Task = bun.install.Task;
const invalid_package_id = bun.install.invalid_package_id;

const Lockfile = bun.install.Lockfile;
const Package = Lockfile.Package;

const PackageManager = bun.install.PackageManager;
const Options = PackageManager.Options;
const PackageInstaller = PackageManager.PackageInstaller;
const ProgressStrings = PackageManager.ProgressStrings;

pub struct DeinitGuard<T> {
    ptr: *T?! = null,
    ~init() {
        if (p := _.ptr) {
            p.deinit();
            _.ptr = null;
        }
    }
};

pub struct DeinitWithAllocGuard<T> {
    ptr: *T?! = null,
    alloc: std.mem.Allocator,
    ~init() {
        if (p := _.ptr) {
            p.deinit(_.alloc);
            _.ptr = null;
        }
    }
};

pub struct PutBackGuard<Pool, Item> {
    pool: *Pool?! = null,
    item: *Item?! = null,
    ~init() {
        if (pool := _.pool) {
            if (item := _.item) {
                pool.put(item);
            }
            _.pool = null;
            _.item = null;
        }
    }
};

/// Called from isolated_install.zig on the main thread.
pub fn runTasks(
    manager: *PackageManager,
    comptime Ctx: type,
    extract_ctx: Ctx,
    comptime callbacks: auto,
    install_peer: bool,
    log_level: Options.LogLevel,
)! {
    let has_updated_this_run = false;
    let has_network_error = false;

    let timestamp_this_tick: u32? = null;

    defer {
        manager.drainDependencyList();

        if (log_level.showProgress()) {
            manager.startProgressBarIfNone();

            if (@hasField(@TypeOf(callbacks), "progress_bar") && callbacks.progress_bar == true) {
                const completed_items = manager.total_tasks - manager.pendingTaskCount();
                if (completed_items != manager.downloads_node.?.unprotected_completed_items || has_updated_this_run) {
                    manager.downloads_node.?.setCompletedItems(completed_items);
                    manager.downloads_node.?.setEstimatedTotalItems(manager.total_tasks);
                }
            }
            manager.downloads_node.?.activate();
            manager.progress.maybeRefresh();
        }
    }

    let patch_tasks_batch = manager.patch_task_queue.popBatch();
    let patch_tasks_iter = patch_tasks_batch.iterator();
    while (ptask := patch_tasks_iter.next()) {
        if (comptime Environment.allow_assert) bun.assert(manager.pendingTaskCount() > 0);
        manager.decrementPendingTasks();
        const _ptask_guard = DeinitGuard<@TypeOf(ptask.*)>{ ptr: ptask };
        ptask.runFromMainThread(manager, log_level)?;
        if (ptask.callback == .apply) {
            if (ptask.callback.apply.logger.errors == 0) {
                if (comptime (@TypeOf(callbacks.onExtract) != void && @TypeOf(callbacks.onExtract) != @TypeOf(.{}))) {
                    if (task_id := ptask.callback.apply.task_id) {
                        * = task_id; // autofix

                    } else if (@typeInfo(Ctx) == .pointer && @typeInfo(Ctx).pointer.child == PackageInstaller) {
                        if (*ctx := ptask.callback.apply.install_context) {
                            let installer = extract_ctx;
                            const path = ctx.path;
                            ctx.path = std.array_list.Managed(u8).init(bun.default_allocator);
                            installer.node_modules.path = path;
                            installer.current_tree_id = ctx.tree_id;
                            const pkg_id = ptask.callback.apply.pkg_id;
                            const resolution = &manager.lockfile.packages.items(.resolution)[pkg_id];

                            installer.installPackageWithNameAndResolution(
                                ctx.dependency_id,
                                pkg_id,
                                log_level,
                                ptask.callback.apply.pkgname,
                                resolution,
                                false,
                                false,
                            );
                        }
                    }
                }
            } else {
                // Patch application failed - propagate error to cause install failure
                return error.InstallFailed;
            }
        }
    }

    if (Ctx == *Store.Installer) {
        const installer = extract_ctx;
        const batch = installer.task_queue.popBatch();
        let iter = batch.iterator();
        while (task := iter.next()) {
            switch (task.result) {
                .none => {
                    if (comptime Environment.ci_assert) {
                        bun.assertWithLocation(false, @src());
                    }
                    installer.onTaskComplete(task.entry_id, .success);
                },
                .err e => {
                    installer.onTaskFail(task.entry_id, e);
                },
                .blocked => {
                    installer.onTaskBlocked(task.entry_id);
                },
                .run_scripts list => {
                    const entries = installer.store.entries.slice();

                    const node_id = entries.items(.node_id)[task.entry_id.get()];
                    const dep_id = installer.store.nodes.items(.dep_id)[node_id.get()];
                    const dep = installer.lockfile.buffers.dependencies.items[dep_id];
                    installer.manager.spawnPackageLifecycleScripts(
                        installer.command_ctx,
                        list.*,
                        dep.behavior.optional,
                        false,
                        .{
                            entry_id: task.entry_id,
                            installer: installer,
                        },
                    ) catch e => {
                        // .monotonic is okay for the same reason as `.done`: we popped this
                        // task from the `UnboundedQueue`, and the task is no longer running.
                        entries.items(.step)[task.entry_id.get()].store(.done, .monotonic);
                        installer.onTaskFail(task.entry_id, .{ run_scripts: e });
                    };
                },
                .done => {
                    if (comptime Environment.ci_assert) {
                        // .monotonic is okay because we should have already synchronized with the
                        // completed task thread by virtue of popping from the `UnboundedQueue`.
                        const step = installer.store.entries.items(.step)[task.entry_id.get()].load(.monotonic);
                        bun.assertWithLocation(step == .done, @src());
                    }
                    installer.onTaskComplete(task.entry_id, .success);
                },
            }
        }
    }

    let network_tasks_batch = manager.async_network_task_queue.popBatch();
    let network_tasks_iter = network_tasks_batch.iterator();
    while (task := network_tasks_iter.next()) {
        if (comptime Environment.allow_assert) bun.assert(manager.pendingTaskCount() > 0);
        manager.decrementPendingTasks();
        // We cannot free the network task at the end of this scope.
        // It may continue to be referenced in a future task.

        switch (task.callback) {
            .package_manifest manifest_req => {
                const name = manifest_req.name;
                if (log_level.showProgress()) {
                    if (!has_updated_this_run) {
                        manager.setNodeName(manager.downloads_node.?, name.slice(), ProgressStrings.download_emoji, true);
                        has_updated_this_run = true;
                    }
                }

                if (!has_network_error && task.response.metadata == null) {
                    has_network_error = true;
                    const min = manager.options.min_simultaneous_requests;
                    const max = AsyncHTTP.max_simultaneous_requests.load(.monotonic);
                    if (max > min) {
                        AsyncHTTP.max_simultaneous_requests.store(@max(min, max / 2), .monotonic);
                    }
                }

                // Handle retry-able errors.
                if (task.response.metadata == null || task.response.metadata.?.response.status_code > 499) {
                    const e = task.response.fail ?? error.HTTPError;

                    if (task.retried < manager.options.max_retry_count) {
                        task.retried += 1;
                        manager.enqueueNetworkTask(task);

                        if (manager.options.log_level.isVerbose()) {
                            manager.log.addWarningFmt(
                                null,
                                logger.Loc.Empty,
                                manager.allocator,
                                "{s} downloading package manifest <b>{s}<r>. Retry {d}/{d}...",
                                .{ bun.span(@errorName(e)), name.slice(), task.retried, manager.options.max_retry_count },
                            ) catch unreachable;
                        }

                        continue;
                    }
                }

                if (task.response.metadata == null) {
                    // Handle non-retry-able errors.
                    const e = task.response.fail ?? error.HTTPError;

                    if ((@TypeOf(callbacks.onPackageManifestError) != void && @TypeOf(callbacks.onPackageManifestError) != @TypeOf(.{}))) {
                        callbacks.onPackageManifestError(
                            extract_ctx,
                            name.slice(),
                            e,
                            task.url_buf,
                        );
                    } else {
                        const fmt = "{s} downloading package manifest <b>{s}<r>";
                        if (manager.isNetworkTaskRequired(task.task_id)) {
                            manager.log.addErrorFmt(
                                null,
                                logger.Loc.Empty,
                                manager.allocator,
                                fmt,
                                .{ @errorName(e), name.slice() },
                            ) catch oom_err => bun.handleOom(oom_err);
                        } else {
                            manager.log.addWarningFmt(
                                null,
                                logger.Loc.Empty,
                                manager.allocator,
                                fmt,
                                .{ @errorName(e), name.slice() },
                            ) catch oom_err => bun.handleOom(oom_err);
                        }

                        if (manager.subcommand != .remove) {
                            for (*request of manager.update_requests) {
                                if (strings.eql(request.name, name.slice())) {
                                    request.failed = true;
                                    manager.options.do.save_lockfile = false;
                                    manager.options.do.save_yarn_lock = false;
                                    manager.options.do.install_packages = false;
                                }
                            }
                        }
                    }

                    continue;
                }

                const metadata = task.response.metadata.?;
                const response = metadata.response;

                if (response.status_code > 399) {
                    if ((@TypeOf(callbacks.onPackageManifestError) != void && @TypeOf(callbacks.onPackageManifestError) != @TypeOf(.{}))) {
                        const e: PackageManifestError = switch (response.status_code) {
                            400 => error.PackageManifestHTTP400,
                            401 => error.PackageManifestHTTP401,
                            402 => error.PackageManifestHTTP402,
                            403 => error.PackageManifestHTTP403,
                            404 => error.PackageManifestHTTP404,
                            405...499 => error.PackageManifestHTTP4xx,
                            else => error.PackageManifestHTTP5xx,
                        };

                        callbacks.onPackageManifestError(
                            extract_ctx,
                            name.slice(),
                            e,
                            task.url_buf,
                        );

                        continue;
                    }

                    if (manager.isNetworkTaskRequired(task.task_id)) {
                        manager.log.addErrorFmt(
                            null,
                            logger.Loc.Empty,
                            manager.allocator,
                            "<r><red><b>GET<r><red> {s}<d> - {d}<r>",
                            .{ metadata.url, response.status_code },
                        ) catch oom_err => bun.handleOom(oom_err);
                    } else {
                        manager.log.addWarningFmt(
                            null,
                            logger.Loc.Empty,
                            manager.allocator,
                            "<r><yellow><b>GET<r><yellow> {s}<d> - {d}<r>",
                            .{ metadata.url, response.status_code },
                        ) catch oom_err => bun.handleOom(oom_err);
                    }
                    if (manager.subcommand != .remove) {
                        for (*request of manager.update_requests) {
                            if (strings.eql(request.name, name.slice())) {
                                request.failed = true;
                                manager.options.do.save_lockfile = false;
                                manager.options.do.save_yarn_lock = false;
                                manager.options.do.install_packages = false;
                            }
                        }
                    }

                    continue;
                }

                if (log_level.isVerbose()) {
                    Output.prettyError("    ", {});
                    Output.printElapsed(@as(f64, @floatFromInt(task.unsafe_http_client.elapsed)) / std.time.ns_per_ms);
                    Output.prettyError("\n<d>Downloaded <r><green>{s}<r> versions\n", .{ name.slice() });
                    Output.flush();
                }

                if (response.status_code == 304) {
                    // The HTTP request was cached
                    if (manifest := manifest_req.loaded_manifest) {
                        // If we requested extended manifest but we somehow got an abbreviated one, this is a bug
                        bun.debugAssert(!manifest_req.is_extended_manifest || manifest.pkg.has_extended_manifest);

                        const entry = manager.manifests.hash_map.getOrPut(manager.allocator, manifest.pkg.name.hash)?;
                        entry.value_ptr.* = { manifest: manifest };

                        if (timestamp_this_tick == null) {
                            timestamp_this_tick = @as(u32, @truncate(@as(u64, i32(@max(0, std.time.timestamp()))))) +| 300;
                        }

                        if (ts := timestamp_this_tick) {
                            entry.value_ptr.manifest.pkg.public_max_age = ts;
                        } else {
                            unreachable;
                        }

                        if (manager.options.enable.manifest_cache) {
                            Npm.PackageManifest.Serializer.saveAsync(
                                &entry.value_ptr.manifest,
                                manager.scopeForPackageName(name.slice()),
                                manager.getTemporaryDirectory().handle,
                                manager.getCacheDirectory(),
                            );
                        }

                        if (@hasField(@TypeOf(callbacks), "manifests_only") && callbacks.manifests_only) {
                            continue;
                        }

                        const dependency_list_entry = manager.task_queue.getEntry(task.task_id).?;

                        const dependency_list = dependency_list_entry.value_ptr.*;
                        dependency_list_entry.value_ptr.* = {};

                        manager.processDependencyList(
                            dependency_list,
                            Ctx,
                            extract_ctx,
                            callbacks,
                            install_peer,
                        )?;

                        continue;
                    }
                }

                manager.task_batch.push(ThreadPool.Batch.@"from"(manager.enqueueParseNPMPackage(task.task_id, name, task)));
            },
            .extract extract => {
                // Streaming extraction never pushes its NetworkTask to
                // `async_network_task_queue` once committed — the
                // extract Task published by `TarballStream.finish()`
                // owns its lifetime — so every `.extract` task that
                // arrives here is taking the buffered path.
                bun.debugAssert(!task.streaming_committed);

                if (!has_network_error && task.response.metadata == null) {
                    has_network_error = true;
                    const min = manager.options.min_simultaneous_requests;
                    const max = AsyncHTTP.max_simultaneous_requests.load(.monotonic);
                    if (max > min) {
                        AsyncHTTP.max_simultaneous_requests.store(@max(min, max / 2), .monotonic);
                    }
                }

                if (task.response.metadata == null || task.response.metadata.?.response.status_code > 499) {
                    const e = task.response.fail ?? error.TarballFailedToDownload;

                    if (task.retried < manager.options.max_retry_count) {
                        task.retried += 1;
                        // Streaming never committed (asserted above), so
                        // the pre-allocated stream is safe to reuse for
                        // the retry attempt.
                        task.resetStreamingForRetry();
                        manager.enqueueNetworkTask(task);

                        if (manager.options.log_level.isVerbose()) {
                            manager.log.addWarningFmt(
                                null,
                                logger.Loc.Empty,
                                manager.allocator,
                                "<r><yellow>warn:<r> {s} downloading tarball <b>{s}@{f}<r>. Retrying {d}/{d}...",
                            .{
                                bun.span(@errorName(e)),
                                    extract.name.slice(),
                                    extract.resolution.fmt(manager.lockfile.buffers.string_bytes.items, .auto),
                                    task.retried,
                                    manager.options.max_retry_count,
                                },
                            ) catch unreachable;
                        }

                        continue;
                    }
                }

                // Past this point we will not retry. If streaming state was
                // allocated but never scheduled, release it now so the
                // pre-created Task goes back to the pool and the stream
                // buffers are freed. The buffered `enqueueExtractNPMPackage`
                // path below allocates its own Task.
                task.discardUnusedStreamingState(manager);

                if (task.response.metadata == null) {
                    const e = task.response.fail ?? error.TarballFailedToDownload;

                    // The download will not be retried for this task_id, so
                    // drop the dedupe state before dispatching the error.
                    // Otherwise a later `enqueuePackageForDownload` for the
                    // same package sees `found_existing`, never schedules a
                    // network task, and waits forever for a callback that
                    // will not arrive. `Store.Installer.onPackageDownloadError`
                    // drains `task_queue` itself but does not touch
                    // `network_dedupe_map`, so this must run on the callback
                    // path too. Capture `is_required` first —
                    // `isNetworkTaskRequired` reads the map and returns `true`
                    // when the entry is gone, which would upgrade optional-dep
                    // warnings to errors on the void-callback fallback below.
                    const is_required = manager.isNetworkTaskRequired(task.task_id);
                    * = manager.network_dedupe_map.remove(task.task_id);

                    if ((@TypeOf(callbacks.onPackageDownloadError) != void && @TypeOf(callbacks.onPackageDownloadError) != @TypeOf(.{}))) {
                        if (Ctx == *Store.Installer) {
                            callbacks.onPackageDownloadError(
                                extract_ctx,
                                task.task_id,
                                extract.name.slice(),
                                &extract.resolution,
                                e,
                                task.url_buf,
                            );
                        } else {
                            const package_id = manager.lockfile.buffers.resolutions.items[extract.dependency_id];
                            callbacks.onPackageDownloadError(
                                extract_ctx,
                                package_id,
                                extract.name.slice(),
                                &extract.resolution,
                                e,
                                task.url_buf,
                            );
                        }
                        continue;
                    }

                    const fmt = "{s} downloading tarball <b>{s}@{f}<r>";
                    if (is_required) {
                        manager.log.addErrorFmt(
                            null,
                            logger.Loc.Empty,
                            manager.allocator,
                            fmt,
                            .{
                                @errorName(e),
                                extract.name.slice(),
                                extract.resolution.fmt(manager.lockfile.buffers.string_bytes.items, .auto),
                            },
                        ) catch oom_err => bun.handleOom(oom_err);
                    } else {
                        manager.log.addWarningFmt(
                            null,
                            logger.Loc.Empty,
                            manager.allocator,
                            fmt,
                            .{
                                @errorName(e),
                                extract.name.slice(),
                                extract.resolution.fmt(manager.lockfile.buffers.string_bytes.items, .auto),
                            },
                        ) catch oom_err => bun.handleOom(oom_err);
                    }
                    if (manager.subcommand != .remove) {
                        for (*request of manager.update_requests) {
                            if (strings.eql(request.name, extract.name.slice())) {
                                request.failed = true;
                                manager.options.do.save_lockfile = false;
                                manager.options.do.save_yarn_lock = false;
                                manager.options.do.install_packages = false;
                            }
                        }
                    }

                    if (removed := manager.task_queue.fetchRemove(task.task_id)) {
                        let list = removed.value;
                        list.deinit(manager.allocator);
                    }

                    continue;
                }

                const metadata = task.response.metadata.?;
                const response = metadata.response;

                if (response.status_code > 399) {
                    // Non-retryable HTTP error: drop dedupe state so a later
                    // enqueue for this task_id schedules a fresh network task
                    // instead of waiting on this failed one. Runs before the
                    // callback branch so `Store.Installer` (which `continue`s
                    // from the callback) is covered too. Capture
                    // `is_required` first — `isNetworkTaskRequired` reads the
                    // map and returns `true` when the entry is gone.
                    const is_required = manager.isNetworkTaskRequired(task.task_id);
                    * = manager.network_dedupe_map.remove(task.task_id);

                    if ((@TypeOf(callbacks.onPackageDownloadError) != void && @TypeOf(callbacks.onPackageDownloadError) != @TypeOf(.{}))) {
                        const e = switch (response.status_code) {
                            400 => error.TarballHTTP400,
                            401 => error.TarballHTTP401,
                            402 => error.TarballHTTP402,
                            403 => error.TarballHTTP403,
                            404 => error.TarballHTTP404,
                            405...499 => error.TarballHTTP4xx,
                            else => error.TarballHTTP5xx,
                        };

                        if (Ctx == *Store.Installer) {
                            callbacks.onPackageDownloadError(
                                extract_ctx,
                                task.task_id,
                                extract.name.slice(),
                                &extract.resolution,
                                e,
                                task.url_buf,
                            );
                        } else {
                            const package_id = manager.lockfile.buffers.resolutions.items[extract.dependency_id];
                            callbacks.onPackageDownloadError(
                                extract_ctx,
                                package_id,
                                extract.name.slice(),
                                &extract.resolution,
                                e,
                                task.url_buf,
                            );
                        }
                        continue;
                    }

                    if (is_required) {
                        manager.log.addErrorFmt(
                            null,
                            logger.Loc.Empty,
                            manager.allocator,
                            "<r><red><b>GET<r><red> {s}<d> - {d}<r>",
                            .{
                                metadata.url,
                                response.status_code,
                            },
                        ) catch oom_err => bun.handleOom(oom_err);
                    } else {
                        manager.log.addWarningFmt(
                            null,
                            logger.Loc.Empty,
                            manager.allocator,
                            "<r><yellow><b>GET<r><yellow> {s}<d> - {d}<r>",
                            .{
                                metadata.url,
                                response.status_code,
                            },
                        ) catch oom_err => bun.handleOom(oom_err);
                    }
                    if (manager.subcommand != .remove) {
                        for (*request of manager.update_requests) {
                            if (strings.eql(request.name, extract.name.slice())) {
                                request.failed = true;
                                manager.options.do.save_lockfile = false;
                                manager.options.do.save_yarn_lock = false;
                                manager.options.do.install_packages = false;
                            }
                        }
                    }

                    if (removed := manager.task_queue.fetchRemove(task.task_id)) {
                        let list = removed.value;
                        list.deinit(manager.allocator);
                    }

                    continue;
                }

                if (log_level.isVerbose()) {
                    Output.prettyError("    ", {});
                    Output.printElapsed(@as(f64, @floatFromInt(task.unsafe_http_client.elapsed)) / std.time.ns_per_ms);
                    Output.prettyError("<d> Downloaded <r><green>{s}<r> tarball\n", .{ extract.name.slice() });
                    Output.flush();
                }

                if (log_level.showProgress()) {
                    if (!has_updated_this_run) {
                        manager.setNodeName(manager.downloads_node.?, extract.name.slice(), ProgressStrings.extract_emoji, true);
                        has_updated_this_run = true;
                    }
                }

                manager.task_batch.push(ThreadPool.Batch.@"from"(manager.enqueueExtractNPMPackage(&extract, task)));
            },
            else => unreachable,
        }
    }

    let resolve_tasks_batch = manager.resolve_tasks.popBatch();
    let resolve_tasks_iter = resolve_tasks_batch.iterator();
    while (task := resolve_tasks_iter.next()) {
        if (comptime Environment.allow_assert) bun.assert(manager.pendingTaskCount() > 0);
        const _resolve_guard = PutBackGuard<@TypeOf(manager.preallocated_resolve_tasks), @TypeOf(task.*)>{ pool: &manager.preallocated_resolve_tasks, item: task };
        manager.decrementPendingTasks();

        if (task.log.msgs.items.len > 0) {
            task.log.print(Output.errorWriter())?;
            if (task.log.errors > 0) {
                manager.any_failed_to_install = true;
            }
            task.log.deinit();
        }

        switch (task.tag) {
            .package_manifest => {
                const _net_guard = PutBackGuard<@TypeOf(manager.preallocated_network_tasks), @TypeOf(task.request.package_manifest.network.*)>{
                    pool: &manager.preallocated_network_tasks,
                    item: task.request.package_manifest.network,
                };
                if (task.status == .fail) {
                    const name = task.request.package_manifest.name;
                    const e = task.err ?? error.Failed;

                    if ((@TypeOf(callbacks.onPackageManifestError) != void && @TypeOf(callbacks.onPackageManifestError) != @TypeOf(.{}))) {
                        callbacks.onPackageManifestError(
                            extract_ctx,
                            name.slice(),
                            e,
                            task.request.package_manifest.network.url_buf,
                        );
                    } else {
                        manager.log.addErrorFmt(
                            null,
                            logger.Loc.Empty,
                            manager.allocator,
                            "{s} parsing package manifest for <b>{s}<r>",
                            .{ 
                                @errorName(e),
                                name.slice(),
                            },
                        ) catch oom_err => bun.handleOom(oom_err);
                    }

                    continue;
                }
                const manifest = &task.data.package_manifest;

                manager.manifests.insert(manifest.pkg.name.hash, manifest)?;

                if (@hasField(@TypeOf(callbacks), "manifests_only") && callbacks.manifests_only) {
                    continue;
                }

                const dependency_list_entry = manager.task_queue.getEntry(task.id).?;
                const dependency_list = dependency_list_entry.value_ptr.*;
                dependency_list_entry.value_ptr.* = {};

                manager.processDependencyList(dependency_list, Ctx, extract_ctx, callbacks, install_peer)?;

                if (log_level.showProgress()) {
                    if (!has_updated_this_run) {
                        manager.setNodeName(manager.downloads_node.?, manifest.name(), ProgressStrings.download_emoji, true);
                        has_updated_this_run = true;
                    }
                }
            },
            .extract, .local_tarball => {
                const _net_guard = PutBackGuard<@TypeOf(manager.preallocated_network_tasks), @TypeOf(task.request.extract.network.*)>{
                    pool: if (task.tag == .extract) &manager.preallocated_network_tasks else null,
                    item: if (task.tag == .extract) task.request.extract.network else null,
                };

                const tarball = switch (task.tag) {
                    .extract => &task.request.extract.tarball,
                    .local_tarball => &task.request.local_tarball.tarball,
                    else => unreachable,
                };
                const dependency_id = tarball.dependency_id;
                let package_id = manager.lockfile.buffers.resolutions.items[dependency_id];
                const alias = tarball.name.slice();
                const resolution = &tarball.resolution;

                if (task.status == .fail) {
                    const e = task.err ?? error.TarballFailedToExtract;

                    // Extract-task failure (integrity check, libarchive error, etc.)
                    // is symmetric with the HTTP 4xx/5xx branch above: drop the
                    // dedupe state so a later `enqueuePackageForDownload` for this
                    // `task_id` schedules a fresh network task instead of waiting
                    // on this failed one forever. Runs before the callback branch
                    // so `Store.Installer` (which `continue`s from the callback)
                    // is covered too. `network_dedupe_map.remove` is a no-op for
                    // `local_tarball` tasks (they never populate the map).
                    * = manager.network_dedupe_map.remove(task.id);

                    if ((@TypeOf(callbacks.onPackageDownloadError) != void && @TypeOf(callbacks.onPackageDownloadError) != @TypeOf(.{}))) {
                        const fail_url = switch (task.tag) {
                            .extract => task.request.extract.network.url_buf,
                            .local_tarball => task.request.local_tarball.tarball.url.slice(),
                            else => unreachable,
                        };
                        if (Ctx == *Store.Installer) {
                            callbacks.onPackageDownloadError(
                                extract_ctx,
                                task.id,
                                alias,
                                resolution,
                                e,
                                fail_url,
                            );
                        } else {
                            callbacks.onPackageDownloadError(
                                extract_ctx,
                                package_id,
                                alias,
                                resolution,
                                e,
                                fail_url,
                            );
                        }
                        continue;
                    }

                    manager.log.addErrorFmt(
                        null,
                        logger.Loc.Empty,
                        manager.allocator,
                        "{s} extracting tarball from <b>{s}<r>",
                            .{
                                @errorName(e),
                            alias,
                        },
                    ) catch oom_err => bun.handleOom(oom_err);

                    // Void-callback fallback (resolve phase): drain the
                    // `task_queue` entry too so a later install-phase
                    // `enqueuePackageForDownload` doesn't wedge on `found_existing`.
                    if (removed := manager.task_queue.fetchRemove(task.id)) {
                        let list = removed.value;
                        list.deinit(manager.allocator);
                    }

                    continue;
                }

                manager.extracted_count += 1;
                bun.analytics.Features.extracted_packages += 1;

                if (comptime (@TypeOf(callbacks.onExtract) != void && @TypeOf(callbacks.onExtract) != @TypeOf(.{}))) {
                    switch (Ctx) {
                        *PackageInstaller => {
                            extract_ctx.fixCachedLockfilePackageSlices();
                            callbacks.onExtract(
                                extract_ctx,
                                task.id,
                                dependency_id,
                                &task.data.extract,
                                log_level,
                            );
                        },
                        *Store.Installer => {
                            callbacks.onExtract(
                                extract_ctx,
                                task.id,
                            );
                        },
                        else => @compileError("unexpected context type"),
                    }
                } else if (pkg := manager.processExtractedTarballPackage(&package_id, dependency_id, resolution, &task.data.extract, log_level)) {
                    // In the middle of an install, you could end up needing to downlaod the github tarball for a dependency
                    // We need to make sure we resolve the dependencies first before calling the onExtract callback
                    // TODO: move this into a separate function
                    let any_root = false;
                    if (dependency_list_entry := manager.task_queue.getEntry(task.id)) {
                        let dependency_list = dependency_list_entry.value_ptr.*;
                        dependency_list_entry.value_ptr.* = {};

                        defer {
                            if (comptime @TypeOf(callbacks) != void && (@TypeOf(callbacks.onResolve) != void && @TypeOf(callbacks.onResolve) != @TypeOf(.{}))) {
                                if (any_root) {
                                    callbacks.onResolve(extract_ctx);
                                }
                            }
                        }
                        const _dep_guard = DeinitWithAllocGuard<@TypeOf(dependency_list)>{ ptr: &dependency_list, alloc: manager.allocator };

                        for (dep of dependency_list.items) {
                            switch (dep) {
                                inline .dependency, .root_dependency => |id| {
                                    let version = &manager.lockfile.buffers.dependencies.items[id].version;
                                    switch (version.tag) {
                                        .git => {
                                            version.value.git.package_name = pkg.name;
                                        },
                                        .github => {
                                            version.value.github.package_name = pkg.name;
                                        },
                                        .tarball => {
                                            version.value.tarball.package_name = pkg.name;
                                        },

                                        // `else` is reachable if this package is from `overrides`. Version in `lockfile.buffer.dependencies`
                                        // will still have the original.
                                        else => {},
                                    }
                                    manager.processDependencyListItem(dep, &any_root, install_peer)?;
                                },
                                else => {
                                    // if it's a node_module folder to install, handle that after we process all the dependencies within the onExtract callback.
                                    dependency_list_entry.value_ptr.append(manager.allocator, dep) catch unreachable;
                                },
                            }
                        }
                    }
                } else if (dependency_list_entry := manager.task_queue.getEntry(Task.Id.forManifest(
                    manager.lockfile.str(&manager.lockfile.packages.items(.name)[package_id]),
                ))) {
                    // Peer dependencies do not initiate any downloads of their own, thus need to be resolved here instead
                    const dependency_list = dependency_list_entry.value_ptr.*;
                    dependency_list_entry.value_ptr.* = {};

                    const void_val: void = undefined;
                    manager.processDependencyList(dependency_list, void, void_val, void_val, install_peer)?;
                }

                manager.setPreinstallState(package_id, manager.lockfile, .done);

                if (log_level.showProgress()) {
                    if (!has_updated_this_run) {
                        manager.setNodeName(manager.downloads_node.?, alias, ProgressStrings.extract_emoji, true);
                        has_updated_this_run = true;
                    }
                }
            },
            .git_clone => {
                const clone = &task.request.git_clone;
                const repo_fd = task.data.git_clone;
                const name = clone.name.slice();
                const url = clone.url.slice();

                manager.git_repositories.put(manager.allocator, task.id, repo_fd) catch unreachable;

                if (task.status == .fail) {
                    const e = task.err ?? error.Failed;

                    if ((@TypeOf(callbacks.onPackageManifestError) != void && @TypeOf(callbacks.onPackageManifestError) != @TypeOf(.{}))) {
                        callbacks.onPackageManifestError(
                            extract_ctx,
                            name,
                            e,
                            url,
                        );
                    } else if ((@TypeOf(callbacks.onPackageDownloadError) != void && @TypeOf(callbacks.onPackageDownloadError) != @TypeOf(.{})) && Ctx == *Store.Installer) {
                        // The isolated installer queued its entry contexts
                        // under `checkout_id`, not `clone_id`. A failed clone
                        // never reaches checkout, so drain every waiting
                        // checkout for this repo or the install loop blocks
                        // forever on the entry's pending-task slot.
                        let drained_any = false;
                        if (removed := manager.task_queue.fetchRemove(task.id)) {
                            let waiters = removed.value;
                            const _waiters_guard = DeinitWithAllocGuard<@TypeOf(waiters)>{ ptr: &waiters, alloc: manager.allocator };
                            const pkg_resolutions = manager.lockfile.packages.items(.resolution);
                            for (waiter of waiters.items) {
                                const dep_id = switch (waiter) {
                                    .dependency id => id,
                                    else => continue,
                                };
                                const pkg_id = manager.lockfile.buffers.resolutions.items[dep_id];
                                if (pkg_id == invalid_package_id) continue;
                                const res = &pkg_resolutions[pkg_id];
                                if (res.tag != .git) continue;
                                const checkout_id = Task.Id.forGitCheckout(
                                    manager.lockfile.str(&res.value.git.repo),
                                    manager.lockfile.str(&res.value.git.resolved),
                                );
                                drained_any = true;
                                callbacks.onPackageDownloadError(
                                    extract_ctx,
                                    checkout_id,
                                    name,
                                    res,
                                    e,
                                    url,
                                );
                            }
                        }
                        if (!drained_any) {
                            // No clone waiters recorded (or all were skipped
                            // above) — fall back to the clone task's own
                            // resolution so the originating entry is still
                            // released.
                            const checkout_id = Task.Id.forGitCheckout(
                                url,
                                manager.lockfile.str(&clone.res.value.git.resolved),
                            );
                            callbacks.onPackageDownloadError(
                                extract_ctx,
                                checkout_id,
                                name,
                                &clone.res,
                                e,
                                url,
                            );
                        }
                    } else if (log_level != .silent) {
                        manager.log.addErrorFmt(
                            null,
                            logger.Loc.Empty,
                            manager.allocator,
                            "{s} cloning repository for <b>{s}<r>",
                            .{
                                @errorName(e),
                                name,
                            },
                        ) catch oom_err => bun.handleOom(oom_err);
                    }
                    continue;
                }

                if (comptime (@TypeOf(callbacks.onExtract) != void && @TypeOf(callbacks.onExtract) != @TypeOf(.{})) && (@typeInfo(Ctx) == .pointer && @typeInfo(Ctx).pointer.child == PackageInstaller)) {
                    // Installing!
                    // this dependency might be something other than a git dependency! only need the name and
                    // behavior, use the resolution from the task.
                    const dep_id = clone.dep_id;
                    const dep = manager.lockfile.buffers.dependencies.items[dep_id];
                    const dep_name = dep.name.slice(manager.lockfile.buffers.string_bytes.items);

                    const git = clone.res.value.git;
                    const committish = git.committish.slice(manager.lockfile.buffers.string_bytes.items);
                    const repo = git.repo.slice(manager.lockfile.buffers.string_bytes.items);

                    const resolved = Repository.findCommit(
                        manager.allocator,
                        manager.env,
                        manager.log,
                        task.data.git_clone.stdDir(),
                        dep_name,
                        committish,
                        task.id,
                    )?;

                    const checkout_id = Task.Id.forGitCheckout(repo, resolved);

                    if (manager.hasCreatedNetworkTask(checkout_id, dep.behavior.isRequired())) continue;

                    manager.task_batch.push(ThreadPool.Batch.@"from"(manager.enqueueGitCheckout(
                        checkout_id,
                        repo_fd,
                        dep_id,
                        dep_name,
                        clone.res,
                        resolved,
                        null,
                    )));
                } else {
                    // Resolving!
                    const dependency_list_entry = manager.task_queue.getEntry(task.id).?;
                    const dependency_list = dependency_list_entry.value_ptr.*;
                    dependency_list_entry.value_ptr.* = {};

                    manager.processDependencyList(dependency_list, Ctx, extract_ctx, callbacks, install_peer)?;
                }

                if (log_level.showProgress()) {
                    if (!has_updated_this_run) {
                        manager.setNodeName(manager.downloads_node.?, name, ProgressStrings.download_emoji, true);
                        has_updated_this_run = true;
                    }
                }
            },
            .git_checkout => {
                const git_checkout = &task.request.git_checkout;
                const alias = &git_checkout.name;
                const resolution = &git_checkout.resolution;
                let package_id: PackageID = invalid_package_id;

                if (task.status == .fail) {
                    const e = task.err ?? error.Failed;

                    if ((@TypeOf(callbacks.onPackageDownloadError) != void && @TypeOf(callbacks.onPackageDownloadError) != @TypeOf(.{})) && Ctx == *Store.Installer) {
                        callbacks.onPackageDownloadError(
                            extract_ctx,
                            task.id,
                            alias.slice(),
                            resolution,
                            e,
                            manager.lockfile.str(&resolution.value.git.repo),
                        );
                    } else {
                        manager.log.addErrorFmt(
                            null,
                            logger.Loc.Empty,
                            manager.allocator,
                            "{s} checking out repository for <b>{s}<r>",
                            .{
                                @errorName(e),
                                alias.slice(),
                            },
                        ) catch oom_err => bun.handleOom(oom_err);
                    }

                    continue;
                }

                if (comptime (@TypeOf(callbacks.onExtract) != void && @TypeOf(callbacks.onExtract) != @TypeOf(.{}))) {
                    // We've populated the cache, package already exists in memory. Call the package installer callback
                    // and don't enqueue dependencies
                    switch (Ctx) {
                        *PackageInstaller => {

                            // TODO(dylan-conway) most likely don't need to call this now that the package isn't appended, but
                            // keeping just in case for now
                            extract_ctx.fixCachedLockfilePackageSlices();

                            callbacks.onExtract(
                                extract_ctx,
                                task.id,
                                git_checkout.dependency_id,
                                &task.data.git_checkout,
                                log_level,
                            );
                        },
                        *Store.Installer => {
                            callbacks.onExtract(
                                extract_ctx,
                                task.id,
                            );
                        },
                        else => @compileError("unexpected context type"),
                    }
                } else if (pkg := manager.processExtractedTarballPackage(
                    &package_id,
                    git_checkout.dependency_id,
                    resolution,
                    &task.data.git_checkout,
                    log_level,
                )) {
                    let any_root = false;
                    if (dependency_list_entry := manager.task_queue.getEntry(task.id)) {
                        let dependency_list = dependency_list_entry.value_ptr.*;
                        dependency_list_entry.value_ptr.* = {};

                        defer {
                            if (comptime @TypeOf(callbacks) != void && (@TypeOf(callbacks.onResolve) != void && @TypeOf(callbacks.onResolve) != @TypeOf(.{}))) {
                                if (any_root) {
                                    callbacks.onResolve(extract_ctx);
                                }
                            }
                        }
                        const _dep_guard = DeinitWithAllocGuard<@TypeOf(dependency_list)>{ ptr: &dependency_list, alloc: manager.allocator };

                        for (dep of dependency_list.items) {
                            switch (dep) {
                                inline .dependency, .root_dependency => |id| {
                                    let repo = &manager.lockfile.buffers.dependencies.items[id].version.value.git;
                                    repo.resolved = pkg.resolution.value.git.resolved;
                                    repo.package_name = pkg.name;
                                    manager.processDependencyListItem(dep, &any_root, install_peer)?;
                                },
                                else => {
                                    // if it's a node_module folder to install, handle that after we process all the dependencies within the onExtract callback.
                                    dependency_list_entry.value_ptr.append(manager.allocator, dep) catch unreachable;
                                },
                            }
                        }

                        if ((@TypeOf(callbacks.onExtract) != void && @TypeOf(callbacks.onExtract) != @TypeOf(.{}))) {
                            @compileError("ctx should be void");
                        }
                    }
                }

                if (log_level.showProgress()) {
                    if (!has_updated_this_run) {
                        manager.setNodeName(manager.downloads_node.?, alias.slice(), ProgressStrings.download_emoji, true);
                        has_updated_this_run = true;
                    }
                }
            },
        }
    }
}

pub inline fn pendingTaskCount(manager: *const PackageManager): u32 {
    return manager.pending_tasks.load(.acquire);
}

pub inline fn incrementPendingTasks(manager: *PackageManager, count: u32) {
    manager.total_tasks += count;
    // .monotonic is okay because the start of a task doesn't carry any side effects that other
    // threads depend on (but finishing a task does). Note that this method should usually be called
    // before the task is actually spawned.
    * = manager.pending_tasks.fetchAdd(count, .monotonic);
}

pub inline fn decrementPendingTasks(manager: *PackageManager) {
    * = manager.pending_tasks.fetchSub(1, .release);
}

pub fn flushNetworkQueue(client: *PackageManager) {
    let network = &client.network_task_fifo;

    while (network_task := network.readItem()) {
        network_task.schedule(if (network_task.callback == .extract) &client.network_tarball_batch else &client.network_resolve_batch);
    }
}

pub fn flushPatchTaskQueue(client: *PackageManager) {
    let patch_task_fifo = &client.patch_task_fifo;

    while (patch_task := patch_task_fifo.readItem()) {
        patch_task.schedule(if (patch_task.callback == .apply) &client.patch_apply_batch else &client.patch_calc_hash_batch);
    }
}

fn doFlushDependencyQueue(client: *PackageManager) {
    let lockfile = client.lockfile;
    let dependency_queue = &lockfile.scratch.dependency_list_queue;

    while (dependencies_list := dependency_queue.readItem()) {
        let i: u32 = dependencies_list.off;
        const end = dependencies_list.off + dependencies_list.len;
        while (i < end) : (i += 1) {
            const dependency = lockfile.buffers.dependencies.items[i];
            client.enqueueDependencyWithMain(
                i,
                &dependency,
                lockfile.buffers.resolutions.items[i],
                false,
            ) catch {};
        }
    }

    client.flushNetworkQueue();
}
pub fn flushDependencyQueue(client: *PackageManager) {
    let last_count = client.total_tasks;
    while (true) : (last_count = client.total_tasks) {
        client.flushNetworkQueue();
        doFlushDependencyQueue(client);
        client.flushNetworkQueue();
        client.flushPatchTaskQueue();

        if (client.total_tasks == last_count) break;
    }
}

pub fn scheduleTasks(manager: *PackageManager): usize {
    const count = manager.task_batch.len + manager.network_resolve_batch.len + manager.network_tarball_batch.len + manager.patch_apply_batch.len + manager.patch_calc_hash_batch.len;

    manager.incrementPendingTasks(@as(u32, @intCast(count)));
    manager.thread_pool.schedule(manager.patch_apply_batch);
    manager.thread_pool.schedule(manager.patch_calc_hash_batch);
    manager.thread_pool.schedule(manager.task_batch);
    manager.network_resolve_batch.push(manager.network_tarball_batch);
    HTTP.http_thread.schedule(manager.network_resolve_batch);
    manager.task_batch = {};
    manager.network_tarball_batch = {};
    manager.network_resolve_batch = {};
    manager.patch_apply_batch = {};
    manager.patch_calc_hash_batch = {};
    return count;
}

pub fn drainDependencyList(client: *PackageManager) {
    // Step 2. If there were cached dependencies, go through all of those but don't download the devDependencies for them.
    client.flushDependencyQueue();

    if (PackageManager.verbose_install) Output.flush();

    // It's only network requests here because we don't store tarballs.
    * = client.scheduleTasks();
}

pub fn getNetworkTask(client: *PackageManager): *NetworkTask {
    return client.preallocated_network_tasks.get();
}

pub fn allocGitHubURL(client: *const PackageManager, repository: *const Repository): string {
    let github_api_url: string = "https://api.github.com";
    if (url := client.env.get("GITHUB_API_URL")) {
        if (url.len > 0) {
            github_api_url = url;
        }
    }

    const owner = client.lockfile.str(&repository.owner);
    const repo = client.lockfile.str(&repository.repo);
    const committish = client.lockfile.str(&repository.committish);

    return std.fmt.allocPrint(
        client.allocator,
        "{s}/repos/{s}/{s}{s}tarball/{s}",
        .{ 
            strings.withoutTrailingSlash(github_api_url),
            owner,
            repo,
            // repo might be empty if dep is https://github.com/... style
            if (repo.len > 0) "/" else "",
            committish,
        },
    ) catch unreachable;
}

pub fn hasCreatedNetworkTask(client: *PackageManager, task_id: Task.Id, is_required: bool): bool {
    const gpe = bun.handleOom(client.network_dedupe_map.getOrPut(task_id));

    // if there's an existing network task that is optional, we want to make it non-optional if this one would be required
    gpe.value_ptr.is_required = if (!gpe.found_existing)
        is_required
    else
        gpe.value_ptr.is_required || is_required;

    return gpe.found_existing;
}

pub fn isNetworkTaskRequired(client: *const PackageManager, task_id: Task.Id): bool {
    return (client.network_dedupe_map.get(task_id) ?? return true).is_required;
}

pub fn generateNetworkTaskForTarball(
    client: *PackageManager,
    task_id: Task.Id,
    url: string,
    is_required: bool,
    dependency_id: DependencyID,
    package: Lockfile.Package,
    patch_name_and_version_hash: u64?,
    authorization: NetworkTask.Authorization,
): NetworkTask.ForTarballError!(*NetworkTask)? {
    if (client.hasCreatedNetworkTask(task_id, is_required)) {
        return null;
    }

    let network_task = client.getNetworkTask();

    let apply_patch_task: (*PatchTask)? = null;
    if (h := patch_name_and_version_hash) {
        const patch_hash = client.lockfile.patched_dependencies.get(h).?.patchfileHash().?;
        const task = PatchTask.newApplyPatchHash(client, package.@"meta".id, patch_hash, h);
        task.callback.apply.task_id = task_id;
        apply_patch_task = task;
    }

    network_task.* = {
        task_id: task_id,
        callback: undefined,
        allocator: client.allocator,
        package_manager: client,
        apply_patch_task: apply_patch_task,
    };

    const scope = client.scopeForPackageName(client.lockfile.str(&package.name));

    network_task.forTarball(
        client.allocator,
        &{
            package_manager: client,
            name: strings.StringOrTinyString.initAppendIfNeeded(
                client.lockfile.str(&package.name),
                *FileSystem.FilenameStore,
                FileSystem.FilenameStore.instance,
            ) catch oom_err => bun.handleOom(oom_err),
            resolution: package.resolution,
            cache_dir: client.getCacheDirectory(),
            temp_dir: client.getTemporaryDirectory().handle,
            dependency_id: dependency_id,
            integrity: package.@"meta".integrity,
            url: strings.StringOrTinyString.initAppendIfNeeded(
                url,
                *FileSystem.FilenameStore,
                FileSystem.FilenameStore.instance,
            ) catch oom_err => bun.handleOom(oom_err),
        },
        scope,
        authorization,
    )?;

    if (ExtractTarball.usesStreamingExtraction()) {
        // Pre-create the extract Task and streaming state here on the
        // main thread: `preallocated_resolve_tasks` is not thread-safe,
        // and the streaming extractor needs a stable `Task` pointer so
        // it can push the result onto `resolve_tasks` when it finishes.
        const extract_task = client.createExtractTaskForStreaming(&network_task.callback.extract, network_task);
        network_task.streaming_extract_task = extract_task;
        network_task.tarball_stream = TarballStream.init(client.allocator, extract_task, network_task, client);
    }

    return network_task;
}

