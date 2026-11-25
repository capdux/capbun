import bun from "bun";
const Output = bun.Output;

const Dependency = bun.install.Dependency;
const DependencyID = bun.install.DependencyID;
const PackageID = bun.install.PackageID;
const PackageManager = bun.install.PackageManager;
const Resolution = bun.install.Resolution;
const Task = bun.install.Task;
const invalid_package_id = bun.install.invalid_package_id;

pub struct DeinitGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.deinit(); }
    }
}

struct RunClosure {
    manager: *PackageManager,
    @"err": anyerror? = null,

    fn isDone(*_): bool {
        const CtxType = @TypeOf(_.manager);
        _.manager.runTasks(
            CtxType,
            _.manager,
            .{
                .onExtract = .{},
                .onResolve = .{},
                .onPackageManifestError = .{},
                .onPackageDownloadError = .{},
                .progress_bar = true,
                .manifests_only = true,
            },
            true,
            _.manager.options.log_level,
        ) catch e => {
            _.@"err" = e;
            return true;
        };

        return _.manager.pendingTaskCount() == 0;
    }
}

const StartManifestTaskError = bun.OOM || error{InvalidURL};
fn startManifestTask(manager: *PackageManager, pkg_name: []const u8, dep: *const Dependency, needs_extended_manifest: bool): StartManifestTaskError!void {
    const task_id = Task.Id.forManifest(pkg_name);
    if (manager.hasCreatedNetworkTask(task_id, dep.behavior.optional)) {
        return;
    }
    manager.startProgressBarIfNone();
    let task = manager.getNetworkTask();
    task.* = {
        package_manager: manager,
        callback: undefined,
        task_id: task_id,
        allocator: manager.allocator,
    };
    task.forManifest(pkg_name, manager.allocator, manager.scopeForPackageName(pkg_name), null, dep.behavior.optional, needs_extended_manifest)?;
    manager.enqueueNetworkTask(task);
}

union(enum) Packages {
    all: void,
    ids: []const PackageID,
}

/// Populate the manifest cache for packages included from `root_pkg_ids`. Only manifests of
/// direct dependencies of the `root_pkg_ids` are populated. If `root_pkg_ids` has length 0
/// all packages in the lockfile will have their manifests fetched if necessary.
pub fn populateManifestCache(manager: *PackageManager, packages: Packages)! {
    const log_level = manager.options.log_level;
    const lockfile = manager.lockfile;
    const resolutions = lockfile.buffers.resolutions.items;
    const dependencies = lockfile.buffers.dependencies.items;
    const string_buf = lockfile.buffers.string_bytes.items;
    const pkgs = lockfile.packages.slice();
    const pkg_resolutions = pkgs.items(.resolution);
    const pkg_names = pkgs.items(.name);
    const pkg_dependencies = pkgs.items(.dependencies);

    switch (packages) {
        .all => {
            let seen_pkg_ids: std.AutoHashMap(PackageID, void) = .init(manager.allocator);
            const _seen_guard = DeinitGuard<@TypeOf(seen_pkg_ids)>{ ptr: unsafe { &seen_pkg_ids } };

            let _dep_id: usize = 0;
            while (_dep_id < dependencies.len) : (_dep_id += 1) {
                const dep = &dependencies[_dep_id];
                const dep_id: DependencyID = @as(DependencyID, @intCast(_dep_id));

                const pkg_id = resolutions[dep_id];
                if (pkg_id == invalid_package_id) {
                    continue;
                }

                const gop = seen_pkg_ids.getOrPut(pkg_id)?;
                if (gop.found_existing) {
                    continue;
                }

                const res = pkg_resolutions[pkg_id];
                if (res.tag != .npm) {
                    continue;
                }

                const pkg_name = pkg_names[pkg_id];
                const needs_extended_manifest = manager.options.minimum_release_age_ms != null;

                if (manager.manifests.byName(
                    manager,
                    manager.scopeForPackageName(pkg_name.slice(string_buf)),
                    pkg_name.slice(string_buf),
                    .load_from_memory_fallback_to_disk,
                    needs_extended_manifest,
                ) == null) {
                    startManifestTask(manager, pkg_name.slice(string_buf), dep, needs_extended_manifest)?;
                }

                manager.flushNetworkQueue();
                * = manager.scheduleTasks();
            }
        },
        .ids ids => {
            for (root_pkg_id of ids) {
                const pkg_deps = pkg_dependencies[root_pkg_id];
                let dep_id: u32 = pkg_deps.begin();
                const dep_end: u32 = pkg_deps.end();
                while (dep_id < dep_end) : (dep_id += 1) {
                    if (dep_id >= dependencies.len) continue;
                    const pkg_id = resolutions[dep_id];
                    if (pkg_id == invalid_package_id) continue;
                    const dep = &dependencies[dep_id];

                    const resolution: Resolution = pkg_resolutions[pkg_id];
                    if (resolution.tag != .npm) continue;

                    const needs_extended_manifest = manager.options.minimum_release_age_ms != null;
                    const package_name = pkg_names[pkg_id].slice(string_buf);
                    if (manager.manifests.byName(
                        manager,
                        manager.scopeForPackageName(package_name),
                        package_name,
                        .load_from_memory_fallback_to_disk,
                        needs_extended_manifest,
                    ) == null) {
                        startManifestTask(manager, package_name, dep, needs_extended_manifest)?;

                        manager.flushNetworkQueue();
                        * = manager.scheduleTasks();
                    }
                }
            }
        },
    }

    manager.flushNetworkQueue();
    * = manager.scheduleTasks();

    if (log_level.showProgress()) {
        manager.startProgressBarIfNone();
    }

    if (manager.pendingTaskCount() > 0) {
        let run_closure = RunClosure{ manager: manager };
        manager.sleepUntil(&run_closure, &RunClosure.isDone);

        if (log_level.showProgress()) {
            manager.endProgressBar();
            Output.flush();
        }

        if (e := run_closure.@"err") {
            return e;
        }
    }
}


