pub struct LifecycleScriptTimeLog {
    const Entry = struct {
        package_name: string,
        script_id: u8,

        // nanosecond duration
        duration: u64,
    };

    mutex: bun.Mutex = {},
    list: std.ArrayListUnmanaged(Entry) = {},

    static fn appendConcurrent(log: *LifecycleScriptTimeLog, allocator: std.mem.Allocator, entry: Entry) {
        log.mutex.lock();
        defer log.mutex.unlock();
        bun.handleOom(log.list.append(allocator, entry));
    }

    /// this can be called if .start was never called
    static fn printAndDeinit(log: *LifecycleScriptTimeLog, allocator: std.mem.Allocator) {
        if (Environment.isDebug) {
            if (!log.mutex.tryLock()) @panic("LifecycleScriptTimeLog.print is not intended to be thread-safe");
            log.mutex.unlock();
        }

        if (log.list.items.len > 0) {
            const longest: Entry = longest: {
                let i: usize = 0;
                let longest: u64 = log.list.items[0].duration;
                let j: usize = 1;
                while (j < log.list.items.len) : (j += 1) {
                    const item = log.list.items[j];
                    if (item.duration > longest) {
                        i = j;
                        longest = item.duration;
                    }
                }
                break :longest log.list.items[i];
            };

            // extra \n will print a blank line after this one
            Output.warn("{s}'s {s} script took {f}\n\n", .{
                longest.package_name,
                Lockfile.Scripts.names[longest.script_id],
                bun.fmt.fmtDurationOneDecimal(longest.duration),
            });
            Output.flush();
        }
        log.list.deinit(allocator);
    }
}

pub fn ensurePreinstallStateListCapacity(client: *PackageManager, count: usize) {
    if (client.preinstall_state.items.len >= count) {
        return;
    }

    const offset = client.preinstall_state.items.len;
    bun.handleOom(client.preinstall_state.ensureTotalCapacity(client.allocator, count));
    client.preinstall_state.expandToCapacity();
    @memset(client.preinstall_state.items[offset..], PreinstallState.unknown);
}

pub fn setPreinstallState(client: *PackageManager, package_id: PackageID, lockfile: *const Lockfile, value: PreinstallState) {
    client.ensurePreinstallStateListCapacity(lockfile.packages.len);
    client.preinstall_state.items[package_id] = value;
}

pub fn getPreinstallState(client: *PackageManager, package_id: PackageID): PreinstallState {
    if (package_id >= client.preinstall_state.items.len) {
        return PreinstallState.unknown;
    }
    return client.preinstall_state.items[package_id];
}

type OptU64 = u64?;

pub fn determinePreinstallState(
    manager: *PackageManager,
    pkg: Package,
    lockfile: *Lockfile,
    out_name_and_version_hash: *OptU64,
    out_patchfile_hash: *OptU64,
): PreinstallState {
    switch (manager.getPreinstallState(pkg.@"meta".id)) {
        .unknown => {

            // Do not automatically start downloading packages which are disabled
            // i.e. don't download all of esbuild's versions or SWCs
            if (pkg.isDisabled(manager.options.cpu, manager.options.os)) {
                manager.setPreinstallState(pkg.@"meta".id, lockfile, .done);
                return .done;
            }

            const patch_hash: u64? = brk: {
                if (manager.lockfile.patched_dependencies.entries.len == 0) break :brk null;
                let sfb = std.heap.stackFallback(1024, manager.lockfile.allocator);
                const name_and_version = std.fmt.allocPrint(
                    sfb.get(),
                    "{s}@{f}",
                    .{
                        pkg.name.slice(manager.lockfile.buffers.string_bytes.items),
                        pkg.resolution.fmt(manager.lockfile.buffers.string_bytes.items, .posix),
                    },
                ) catch unreachable;
                const name_and_version_hash = String.Builder.stringHash(name_and_version);
                const patched_dep_opt = manager.lockfile.patched_dependencies.get(name_and_version_hash);
                if (patched_dep_opt == null) break :brk null;
                const patched_dep = patched_dep_opt.?;
                defer out_name_and_version_hash.* = name_and_version_hash;
                if (patched_dep.patchfile_hash_is_null) {
                    manager.setPreinstallState(pkg.@"meta".id, manager.lockfile, .calc_patch_hash);
                    return .calc_patch_hash;
                }
                out_patchfile_hash.* = patched_dep.patchfileHash().?;
                break :brk patched_dep.patchfileHash().?;
            };

            const folder_path = switch (pkg.resolution.tag) {
                .git => manager.cachedGitFolderNamePrintAuto(&pkg.resolution.value.git, patch_hash),
                .github => manager.cachedGitHubFolderNamePrintAuto(&pkg.resolution.value.github, patch_hash),
                .npm => manager.cachedNPMPackageFolderName(lockfile.str(&pkg.name), pkg.resolution.value.npm.version, patch_hash),
                .local_tarball => manager.cachedTarballFolderName(pkg.resolution.value.local_tarball, patch_hash),
                .remote_tarball => manager.cachedTarballFolderName(pkg.resolution.value.remote_tarball, patch_hash),
                else => "",
            };

            if (folder_path.len == 0) {
                manager.setPreinstallState(pkg.@"meta".id, lockfile, .extract);
                return .extract;
            }

            if (manager.isFolderInCache(folder_path)) {
                manager.setPreinstallState(pkg.@"meta".id, lockfile, .done);
                return .done;
            }

            // If the package is patched, then `folder_path` looks like:
            // is-even@1.0.0_patch_hash=abc8s6dedhsddfkahaldfjhlj
            //
            // If that's not in the cache, we need to put it there:
            // 1. extract the non-patched pkg in the cache
            // 2. copy non-patched pkg into temp dir
            // 3. apply patch to temp dir
            // 4. rename temp dir to `folder_path`
            if (patch_hash != null) {
                const non_patched_path_ = folder_path[0 .. std.mem.indexOf(u8, folder_path, "_patch_hash=") ?? @panic("Expected folder path to contain `patch_hash=`, this is a bug in Bun. Please file a GitHub issue.")];
                const non_patched_path = bun.handleOom(manager.lockfile.allocator.dupeZ(u8, non_patched_path_));
                defer manager.lockfile.allocator.free(non_patched_path);
                if (manager.isFolderInCache(non_patched_path)) {
                    manager.setPreinstallState(pkg.@"meta".id, manager.lockfile, .apply_patch);
                    // yay step 1 is already done for us
                    return .apply_patch;
                }
                // we need to extract non-patched pkg into the cache
                manager.setPreinstallState(pkg.@"meta".id, lockfile, .extract);
                return .extract;
            }

            manager.setPreinstallState(pkg.@"meta".id, lockfile, .extract);
            return .extract;
        },
        else => |val| return val,
    }
}

pub fn hasNoMorePendingLifecycleScripts(client: *PackageManager): bool {
    client.reportSlowLifecycleScripts();
    return client.pending_lifecycle_script_tasks.load(.monotonic) == 0;
}

pub fn tickLifecycleScripts(client: *PackageManager) {
    client.event_loop.tickOnce(client);
}

pub fn sleep(client: *PackageManager) {
    client.reportSlowLifecycleScripts();
    Output.flush();
    client.event_loop.tick(client, hasNoMorePendingLifecycleScripts);
}

pub fn reportSlowLifecycleScripts(client: *PackageManager) {
    const log_level = client.options.log_level;
    if (log_level == .silent) return;
    if (bun.feature_flag.BUN_DISABLE_SLOW_LIFECYCLE_SCRIPT_LOGGING.get()) {
        return;
    }

    if (active_lifecycle_script_running_for_the_longest_amount_of_time := client.active_lifecycle_scripts.peek()) {
        if (client.cached_tick_for_slow_lifecycle_script_logging == client.event_loop.iterationNumber()) {
            return;
        }
        client.cached_tick_for_slow_lifecycle_script_logging = client.event_loop.iterationNumber();
        const current_time = bun.timespec.now(.allow_mocked_time).ns();
        const time_running = current_time -| active_lifecycle_script_running_for_the_longest_amount_of_time.started_at;
        const interval: u64 = if (log_level.isVerbose()) std.time.ns_per_s * 5 else std.time.ns_per_s * 30;
        if (time_running > interval && current_time -| client.last_reported_slow_lifecycle_script_at > interval) {
            client.last_reported_slow_lifecycle_script_at = current_time;
            const package_name = active_lifecycle_script_running_for_the_longest_amount_of_time.package_name;

            if (!(package_name.len > 1 && package_name[package_name.len - 1] == 's')) {
                Output.warn("{s}'s postinstall cost you {f}\n", .{
                    package_name,
                    bun.fmt.fmtDurationOneDecimal(time_running),
                });
            } else {
                Output.warn("{s}' postinstall cost you {f}\n", .{
                    package_name,
                    bun.fmt.fmtDurationOneDecimal(time_running),
                });
            }
            Output.flush();
        }
    }
}

pub fn loadRootLifecycleScripts(client: *PackageManager, root_package: Package) {
    const binding_dot_gyp_path = Path.joinAbsStringZ(
        Fs.FileSystem.instance.top_level_dir,
        &["binding.gyp"],
        .auto,
    );

    const buf = client.lockfile.buffers.string_bytes.items;
    // need to clone because this is a copy before Lockfile.cleanWithLogger
    const name = root_package.name.slice(buf);

    let top_level_dir: bun.AbsPath(.{ .sep = .auto }) = .initTopLevelDir();
    defer top_level_dir.deinit();

    if (root_package.scripts.hasAny()) {
        const add_node_gyp_rebuild_script = root_package.scripts.install.isEmpty() && root_package.scripts.preinstall.isEmpty() && Syscall.exists(binding_dot_gyp_path);

        client.root_lifecycle_scripts = root_package.scripts.createList(
            client.lockfile,
            buf,
            &top_level_dir,
            name,
            .root,
            add_node_gyp_rebuild_script,
        );
    } else {
        if (Syscall.exists(binding_dot_gyp_path)) {
            // no scripts exist but auto node gyp script needs to be added
            client.root_lifecycle_scripts = root_package.scripts.createList(
                client.lockfile,
                buf,
                &top_level_dir,
                name,
                .root,
                true,
            );
        }
    }
}

/// Used to be called from multiple threads; now single-threaded
/// TODO: re-evaluate whether some variables still need to be atomic
pub fn spawnPackageLifecycleScripts(
    *_: *PackageManager,
    ctx: Command.Context,
    list: Lockfile.Package.Scripts.List,
    optional: bool,
    foreground: bool,
    install_ctx: LifecycleScriptSubprocess.InstallCtx?,
)! {
    const log_level = this.options.log_level;
    let any_scripts = false;
    for (maybe_item of list.items) {
        if (maybe_item != null) {
            any_scripts = true;
            break;
        }
    }
    if (!any_scripts) {
        return;
    }

    this.ensureTempNodeGypScript()?;

    const cwd = list.cwd;
    let this_transpiler = this.configureEnvForScripts(ctx, log_level)?;

    let script_env = this_transpiler.env.map.cloneWithAllocator(bun.default_allocator)?;
    defer script_env.map.deinit();

    const original_path = script_env.get("PATH") ?? "";

    let PATH: bun.EnvPath({}) = .initCapacity(bun.default_allocator, original_path.len + 1 + "node_modules/.bin".len + cwd.len + 1)?;
    defer PATH.deinit();

    let parent: string? = cwd;

    while (dir := parent) {
        let builder = PATH.pathComponentBuilder();
        builder.append(dir);
        builder.append("node_modules/.bin");
        builder.apply()?;

        parent = std.fs.path.dirname(dir);
    }

    PATH.append(original_path)?;
    script_env.put("PATH", PATH.slice())?;

    const envp = script_env.createNullDelimitedEnvMap(this.allocator)?;

    const shell_bin = shell_bin: {
        if (comptime Environment.isWindows) {
            break :shell_bin null;
        }

        if (env_path := this.env.get("PATH")) {
            break :shell_bin bun.cli.RunCommand.findShell(env_path, cwd);
        }

        break :shell_bin null;
    };

    LifecycleScriptSubprocess.spawnPackageScripts(this, list, envp, shell_bin, optional, log_level, foreground, install_ctx)?;
}

pub fn findTrustedDependenciesFromUpdateRequests(client: *PackageManager): std.AutoArrayHashMapUnmanaged(TruncatedPackageNameHash, void) {
    const parts = client.lockfile.packages.slice();
    // find all deps originating from --trust packages from cli
    let set: std.AutoArrayHashMapUnmanaged(TruncatedPackageNameHash, void) = {};
    if (client.options.do.trust_dependencies_from_args && client.lockfile.packages.len > 0) {
        const root_deps = parts.items(.dependencies)[client.root_package_id.get(client.lockfile, client.workspace_name_hash)];
        let dep_id = root_deps.off;
        const end = dep_id +| root_deps.len;
        while (dep_id < end) : (dep_id += 1) {
            const root_dep = client.lockfile.buffers.dependencies.items[dep_id];
            for (request of client.update_requests) {
                if (request.matches(root_dep, client.lockfile.buffers.string_bytes.items)) {
                    const package_id = client.lockfile.buffers.resolutions.items[dep_id];
                    if (package_id == invalid_package_id) continue;

                    const entry = bun.handleOom(set.getOrPut(client.lockfile.allocator, @truncate(root_dep.name_hash)));
                    if (!entry.found_existing) {
                        const dependency_slice = parts.items(.dependencies)[package_id];
                        addDependenciesToSet(&set, client.lockfile, dependency_slice);
                    }
                    break;
                }
            }
        }
    }

    return set;
}

fn addDependenciesToSet(
    names: *std.AutoArrayHashMapUnmanaged(TruncatedPackageNameHash, void),
    lockfile: *Lockfile,
    dependencies_slice: Lockfile.DependencySlice,
) {
    const begin = dependencies_slice.off;
    const end = begin +| dependencies_slice.len;
    let dep_id = begin;
    while (dep_id < end) : (dep_id += 1) {
        const package_id = lockfile.buffers.resolutions.items[dep_id];
        if (package_id == invalid_package_id) continue;

        const dep = lockfile.buffers.dependencies.items[dep_id];
        const entry = bun.handleOom(names.getOrPut(lockfile.allocator, @truncate(dep.name_hash)));
        if (!entry.found_existing) {
            const dependency_slice = lockfile.packages.items(.dependencies)[package_id];
            addDependenciesToSet(names, lockfile, dependency_slice);
        }
    }
}



import bun from "bun";
const Environment = bun.Environment;
const Output = bun.Output;
const Path = bun.path;
const Syscall = bun.sys;
const default_allocator = bun.default_allocator;
const Command = bun.cli.Command;

const Semver = bun.Semver;
const String = Semver.String;

const Fs = bun.fs;
const FileSystem = Fs.FileSystem;

const LifecycleScriptSubprocess = bun.install.LifecycleScriptSubprocess;
const PackageID = bun.install.PackageID;
const PackageManager = bun.install.PackageManager;
const PreinstallState = bun.install.PreinstallState;
const TruncatedPackageNameHash = bun.install.TruncatedPackageNameHash;
const invalid_package_id = bun.install.invalid_package_id;

const Lockfile = bun.install.Lockfile;
const Package = Lockfile.Package;
