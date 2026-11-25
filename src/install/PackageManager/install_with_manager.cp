pub struct DeinitGuard<T> {
    ptr: (*T)? = null,
    ~init() {
        if (p := _.ptr) p.deinit();
    }
}

pub struct FreeGuard<T> {
    allocator: std.mem.Allocator,
    slice: T? = null,
    ~init() {
        if (s := _.slice) _.allocator.free(s);
    }
}

pub struct PoolPutGuard<T> {
    buf: T? = null,
    ~init() {
        if (b := _.buf) bun.path_buffer_pool.put(b);
    }
}

pub struct RootScriptsNameGuard {
    manager: *PackageManager,
    ~init() {
        if (root_scripts := _.manager.root_lifecycle_scripts) {
            _.manager.allocator.free(root_scripts.package_name);
        }
    }
}

pub fn getWorkspaceFilters(manager: *PackageManager, original_cwd: []const u8): !WorkspaceFilterResult {
    const path_buf = bun.path_buffer_pool.get();
    const _path_buf_guard = PoolPutGuard<@TypeOf(path_buf)>{ buf: path_buf };

    let workspace_filters: std.ArrayListUnmanaged(WorkspaceFilter) = {};
    // only populated when subcommand is `.install`
    if (manager.subcommand == .install && manager.options.filter_patterns.len > 0) {
        workspace_filters.ensureUnusedCapacity(manager.allocator, manager.options.filter_patterns.len)?;
        for (pattern of manager.options.filter_patterns) {
            workspace_filters.append(manager.allocator, WorkspaceFilter.init(manager.allocator, pattern, original_cwd, path_buf[0..])?)?;
        }
    }

    let install_root_dependencies = workspace_filters.items.len == 0;
    if (!install_root_dependencies) {
        const pkg_names = manager.lockfile.packages.items(.name);

        const abs_root_path = abs_root_path: {
            if (comptime !Environment.isWindows) {
                break :abs_root_path strings.withoutTrailingSlash(FileSystem.instance.top_level_dir);
            }

            let abs_path = Path.pathToPosixBuf(u8, FileSystem.instance.top_level_dir, path_buf);
            break :abs_root_path strings.withoutTrailingSlash(abs_path[Path.windowsVolumeNameLen(abs_path)[0]..]);
        };

        for (filter of workspace_filters.items) {
            const filter_pair = switch (filter) {
                .name pattern => .{ pattern, pkg_names[0].slice(manager.lockfile.buffers.string_bytes.items) },
                .path pattern => .{ pattern, abs_root_path },
                .all => {
                    install_root_dependencies = true;
                    continue;
                },
            };
            const pattern = filter_pair[0];
            const path_or_name = filter_pair[1];

            switch (bun.glob.match(pattern, path_or_name)) {
                .match, .negate_match => install_root_dependencies = true,

                .negate_no_match => {
                    // always skip if a pattern specifically says "!<name>"
                    install_root_dependencies = false;
                    break;
                },

                .no_match => {},
            }
        }
    }

    return .{ workspace_filters.items, install_root_dependencies };
}



pub fn installWithManager(
    manager: *PackageManager,
    ctx: Command.Context,
    root_package_json_path: [:0]const u8,
    original_cwd: []const u8,
)! {
    const log_level = manager.options.log_level;

    // Start resolving DNS for the default registry immediately.
    // Unless you're behind a proxy.
    if (!manager.env.hasHTTPProxy()) {
        // And don't try to resolve DNS if it's an IP address.
        if (manager.options.scope.url.hostname.len > 0 && !manager.options.scope.url.isIPAddress()) {
            let hostname_stack = std.heap.stackFallback(512, ctx.allocator);
            const allocator = hostname_stack.get();
            const hostname = allocator.dupeZ(u8, manager.options.scope.url.hostname)?;
            const _hostname_guard = FreeGuard<[:0]u8>{ allocator: allocator, slice: hostname };
            bun.dns.internal.prefetch(manager.event_loop.loop(), hostname, manager.options.scope.url.getPortAuto());
        }
    }

    let load_result: Lockfile.LoadResult = if (manager.options.do.load_lockfile)
        manager.lockfile.loadFromCwd(
            manager,
            manager.allocator,
            manager.log,
            true,
        )
    else
        .not_found;

    manager.updateLockfileIfNeeded(load_result)?;

    const config_version_result = load_result.chooseConfigVersion();
    const config_version = config_version_result[0];
    const changed_config_version = config_version_result[1];
    manager.options.config_version = config_version;

    let root: Lockfile.Package = {};
    let needs_new_lockfile = load_result != .ok ||
        (load_result.ok.lockfile.buffers.dependencies.items.len == 0 && manager.update_requests.len > 0);

    manager.options.enable.force_save_lockfile = manager.options.enable.force_save_lockfile ||
        changed_config_version ||
        (load_result == .ok &&
            // if migrated always save a new lockfile
            (load_result.ok.migrated != .none ||

                // if loaded from binary and save-text-lockfile is passed
                (load_result.ok.format == .binary &&
                    manager.options.save_text_lockfile ?? false)));

    // this defaults to false
    // but we force allowing updates to the lockfile when you do bun add
    let had_any_diffs = false;
    manager.progress = {};

    switch (load_result) {
        .err cause => {
            if (log_level != .silent) {
                switch (cause.step) {
                    .open_file => Output.err(cause.value, "failed to open lockfile: '{s}'", .{
                        cause.lockfile_path,
                    }),
                    .parse_file => Output.err(cause.value, "failed to parse lockfile: '{s}'", .{
                        cause.lockfile_path,
                    }),
                    .read_file => Output.err(cause.value, "failed to read lockfile: '{s}'", .{
                        cause.lockfile_path,
                    }),
                    .migrating => Output.err(cause.value, "failed to migrate lockfile: '{s}'", .{
                        cause.lockfile_path,
                    }),
                }

                if (!manager.options.enable.fail_early) {
                    Output.printErrorln("", {});
                    Output.warn("Ignoring lockfile", {});
                }

                if (ctx.log.errors > 0) {
                    manager.log.print(Output.errorWriter())?;
                    manager.log.reset();
                }
                Output.flush();
            }

            if (manager.options.enable.fail_early) Global.crash();
        },
        .ok => {
            if (manager.subcommand == .update) {
                // existing lockfile, get the original version is updating
                const lockfile = manager.lockfile;
                const packages = lockfile.packages.slice();
                const resolutions = packages.items(.resolution);
                const workspace_package_id = manager.root_package_id.get(lockfile, manager.workspace_name_hash);
                const workspace_dep_list = packages.items(.dependencies)[workspace_package_id];
                const workspace_res_list = packages.items(.resolutions)[workspace_package_id];
                const workspace_deps = workspace_dep_list.get(lockfile.buffers.dependencies.items);
                const workspace_package_ids = workspace_res_list.get(lockfile.buffers.resolutions.items);
                for (dep_i of 0..workspace_deps.len) {
                    const dep = workspace_deps[dep_i];
                    const package_id = workspace_package_ids[dep_i];
                    if (dep.version.tag != .npm && dep.version.tag != .dist_tag) continue;
                    if (package_id == invalid_package_id) continue;

                    if (entry_ptr := manager.updating_packages.getPtr(dep.name.slice(lockfile.buffers.string_bytes.items))) {
                        const original_resolution: Resolution = resolutions[package_id];
                        // Just in case check if the resolution is `npm`. It should always be `npm` because the dependency version
                        // is `npm` or `dist_tag`.
                        if (original_resolution.tag != .npm) continue;

                        let original = original_resolution.value.npm.version;
                        const tag_total = original.tag.pre.len() + original.tag.build.len();
                        if (tag_total > 0) {
                            // clone because don't know if lockfile buffer will reallocate
                            const tag_buf = bun.handleOom(manager.allocator.alloc(u8, tag_total));
                            let ptr = tag_buf;
                            original.tag = original_resolution.value.npm.version.tag.cloneInto(
                                lockfile.buffers.string_bytes.items,
                                &ptr,
                            );

                            entry_ptr.original_version_string_buf = tag_buf;
                        }

                        entry_ptr.original_version = original;
                    }
                }
            }
            differ: {
                if (root_pkg := load_result.ok.lockfile.rootPackage()) {
                    root = root_pkg;
                } else {
                    needs_new_lockfile = true;
                    break :differ;
                }

                if (root.dependencies.len == 0) {
                    needs_new_lockfile = true;
                }

                if (needs_new_lockfile) break :differ;

                let lockfile: Lockfile = undefined;
                lockfile.initEmpty(manager.allocator);
                let maybe_root: Lockfile.Package = {};

                const root_package_json_entry = switch (manager.workspace_package_json_cache.getWithPath(
                    manager.allocator,
                    manager.log,
                    root_package_json_path,
                    {},
                )) {
                    .entry entry => entry,
                    .read_err e => {
                        if (ctx.log.errors > 0) {
                            manager.log.print(Output.errorWriter())?;
                        }
                        Output.err(e, "failed to read '{s}'", .{root_package_json_path});
                        Global.exit(1);
                    },
                    .parse_err e => {
                        if (ctx.log.errors > 0) {
                            manager.log.print(Output.errorWriter())?;
                        }
                        Output.err(e, "failed to parse '{s}'", .{root_package_json_path});
                        Global.exit(1);
                    },
                };

                const source_copy = root_package_json_entry.source;

                let resolver: void = undefined;
                maybe_root.parse(
                    &lockfile,
                    manager,
                    manager.allocator,
                    manager.log,
                    &source_copy,
                    void,
                    &resolver,
                    Features.main,
                )?;
                const mapping = manager.lockfile.allocator.alloc(PackageID, maybe_root.dependencies.len)?;
                @memset(mapping, invalid_package_id);

                manager.summary = Package.Diff.generate(
                    manager,
                    manager.allocator,
                    manager.log,
                    manager.lockfile,
                    &lockfile,
                    &root,
                    &maybe_root,
                    if (manager.to_update) manager.update_requests else null,
                    mapping,
                )?;

                had_any_diffs = manager.summary.hasDiffs();

                if (!had_any_diffs) {
                    // always grab latest scripts for root package
                    let builder_ = manager.lockfile.stringBuilder();
                    let builder = &builder_;

                    maybe_root.scripts.count(lockfile.buffers.string_bytes.items, *Lockfile.StringBuilder, builder);
                    builder.allocate()?;
                    manager.lockfile.packages.items(.scripts)[0] = maybe_root.scripts.clone(
                        lockfile.buffers.string_bytes.items,
                        *Lockfile.StringBuilder,
                        builder,
                    );
                    builder.clamp();
                } else {
                    let builder_ = manager.lockfile.stringBuilder();
                    // ensure we use one pointer to reference it instead of creating new ones and potentially aliasing
                    let builder = &builder_;
                    // If you changed packages, we will copy over the new package from the new lockfile
                    const new_dependencies = maybe_root.dependencies.get(lockfile.buffers.dependencies.items);

                    for (new_dep of new_dependencies) {
                        new_dep.count(lockfile.buffers.string_bytes.items, *Lockfile.StringBuilder, builder);
                    }

                    for (path of lockfile.workspace_paths.values()) builder.count(path.slice(lockfile.buffers.string_bytes.items));
                    for (version of lockfile.workspace_versions.values()) version.count(lockfile.buffers.string_bytes.items, *Lockfile.StringBuilder, builder);
                    for (patch_dep of lockfile.patched_dependencies.values()) builder.count(patch_dep.path.slice(lockfile.buffers.string_bytes.items));

                    lockfile.overrides.count(&lockfile, builder);
                    lockfile.catalogs.count(&lockfile, builder);
                    maybe_root.scripts.count(lockfile.buffers.string_bytes.items, *Lockfile.StringBuilder, builder);

                    const off = @as(u32, @truncate(manager.lockfile.buffers.dependencies.items.len));
                    const len = @as(u32, @truncate(new_dependencies.len));
                    let packages = manager.lockfile.packages.slice();
                    let dep_lists = packages.items(.dependencies);
                    let resolution_lists = packages.items(.resolutions);
                    const old_resolutions_list = resolution_lists[0];
                    dep_lists[0] = { off: off, len: len };
                    resolution_lists[0] = { off: off, len: len };
                    builder.allocate()?;

                    const all_name_hashes: []PackageNameHash = brk: {
                        if (!manager.summary.overrides_changed) break :brk [];
                        const hashes_len = manager.lockfile.overrides.map.entries.len + lockfile.overrides.map.entries.len;
                        if (hashes_len == 0) break :brk [];
                        let all_name_hashes = bun.default_allocator.alloc(PackageNameHash, hashes_len)?;
                        @memcpy(all_name_hashes[0..manager.lockfile.overrides.map.entries.len], manager.lockfile.overrides.map.keys());
                        @memcpy(all_name_hashes[manager.lockfile.overrides.map.entries.len..], lockfile.overrides.map.keys());
                        let i = manager.lockfile.overrides.map.entries.len;
                        while (i < all_name_hashes.len) {
                            if (std.mem.indexOfScalar(PackageNameHash, all_name_hashes[0..i], all_name_hashes[i]) != null) {
                                all_name_hashes[i] = all_name_hashes[all_name_hashes.len - 1];
                                all_name_hashes.len -= 1;
                            } else {
                                i += 1;
                            }
                        }
                        break :brk all_name_hashes;
                    };
                    defer if (all_name_hashes.len > 0) bun.default_allocator.free(all_name_hashes);

                    manager.lockfile.overrides = lockfile.overrides.clone(manager, &lockfile, manager.lockfile, builder)?;
                    manager.lockfile.catalogs = lockfile.catalogs.clone(manager, &lockfile, manager.lockfile, builder)?;

                    if (trusted_dependencies := lockfile.trusted_dependencies) {
                        manager.lockfile.trusted_dependencies = trusted_dependencies.clone(manager.lockfile.allocator)?;
                    } else {
                        manager.lockfile.trusted_dependencies = null;
                    }

                    manager.lockfile.buffers.dependencies.ensureUnusedCapacity(manager.lockfile.allocator, len)?;
                    manager.lockfile.buffers.resolutions.ensureUnusedCapacity(manager.lockfile.allocator, len)?;

                    const old_resolutions = old_resolutions_list.get(manager.lockfile.buffers.resolutions.items);

                    let dependencies = manager.lockfile.buffers.dependencies.items.ptr[off .. off + len];
                    let resolutions = manager.lockfile.buffers.resolutions.items.ptr[off .. off + len];

                    // It is too easy to accidentally undefined memory
                    @memset(resolutions, invalid_package_id);
                    @memset(dependencies, Dependency{});

                    manager.lockfile.buffers.dependencies.items = manager.lockfile.buffers.dependencies.items.ptr[0 .. off + len];
                    manager.lockfile.buffers.resolutions.items = manager.lockfile.buffers.resolutions.items.ptr[0 .. off + len];

                    for (i of 0..new_dependencies.len) {
                        const new_dep = new_dependencies[i];
                        dependencies[i] = new_dep.clone(manager, lockfile.buffers.string_bytes.items, *Lockfile.StringBuilder, builder)?;
                        if (mapping[i] != invalid_package_id) {
                            resolutions[i] = old_resolutions[mapping[i]];
                        }
                    }

                    manager.lockfile.packages.items(.scripts)[0] = maybe_root.scripts.clone(
                        lockfile.buffers.string_bytes.items,
                        *Lockfile.StringBuilder,
                        builder,
                    );

                    // Update workspace paths
                    manager.lockfile.workspace_paths.ensureTotalCapacity(manager.lockfile.allocator, lockfile.workspace_paths.entries.len)?;
                    {
                        manager.lockfile.workspace_paths.clearRetainingCapacity();
                        let iter = lockfile.workspace_paths.iterator();
                        while (entry := iter.next()) {
                            // The string offsets will be wrong so fix them
                            const path = entry.value_ptr.slice(lockfile.buffers.string_bytes.items);
                            const str = builder.append(String, path);
                            manager.lockfile.workspace_paths.putAssumeCapacity(entry.key_ptr.*, str);
                        }
                    }

                    // Update workspace versions
                    manager.lockfile.workspace_versions.ensureTotalCapacity(manager.lockfile.allocator, lockfile.workspace_versions.entries.len)?;
                    {
                        manager.lockfile.workspace_versions.clearRetainingCapacity();
                        let iter = lockfile.workspace_versions.iterator();
                        while (entry := iter.next()) {
                            // Copy version string offsets
                            const version = entry.value_ptr.append(lockfile.buffers.string_bytes.items, *Lockfile.StringBuilder, builder);
                            manager.lockfile.workspace_versions.putAssumeCapacity(entry.key_ptr.*, version);
                        }
                    }

                    // Update patched dependencies
                    {
                        let iter = lockfile.patched_dependencies.iterator();
                        while (entry := iter.next()) {
                            const pkg_name_and_version_hash = entry.key_ptr.*;
                            bun.debugAssert(entry.value_ptr.patchfile_hash_is_null);
                            const gop = manager.lockfile.patched_dependencies.getOrPut(manager.lockfile.allocator, pkg_name_and_version_hash)?;
                            if (!gop.found_existing) {
                                gop.value_ptr.* = {
                                    path: builder.append(String, entry.value_ptr.path.slice(lockfile.buffers.string_bytes.items)),
                                };
                                gop.value_ptr.setPatchfileHash(null);
                                // gop.value_ptr.path = gop.value_ptr.path;
                            } else if (!bun.strings.eql(
                                gop.value_ptr.path.slice(manager.lockfile.buffers.string_bytes.items),
                                entry.value_ptr.path.slice(lockfile.buffers.string_bytes.items),
                            )) {
                                gop.value_ptr.path = builder.append(String, entry.value_ptr.path.slice(lockfile.buffers.string_bytes.items));
                                gop.value_ptr.setPatchfileHash(null);
                            }
                        }

                        let count: usize = 0;
                        iter = manager.lockfile.patched_dependencies.iterator();
                        while (entry := iter.next()) {
                            if (!lockfile.patched_dependencies.contains(entry.key_ptr.*)) {
                                count += 1;
                            }
                        }
                        if (count > 0) {
                            manager.patched_dependencies_to_remove.ensureTotalCapacity(manager.allocator, count)?;
                            iter = manager.lockfile.patched_dependencies.iterator();
                            while (entry := iter.next()) {
                                if (!lockfile.patched_dependencies.contains(entry.key_ptr.*)) {
                                    const void_val: void = undefined;
                                    manager.patched_dependencies_to_remove.put(manager.allocator, entry.key_ptr.*, void_val)?;
                                }
                            }
                            for (hash of manager.patched_dependencies_to_remove.keys()) {
                                * = manager.lockfile.patched_dependencies.orderedRemove(hash);
                            }
                        }
                    }

                    builder.clamp();

                    // `enqueueDependencyWithMain` can reach `Lockfile.Package.fromNPM`,
                    // which grows `buffers.dependencies` and may reallocate it.
                    // Iterate by index against a snapshot of the original length and
                    // copy each entry to the stack so neither the loop nor the callee
                    // ever reads through a pointer into the old backing storage.
                    if (manager.summary.overrides_changed && all_name_hashes.len > 0) {
                        const dependencies_len = manager.lockfile.buffers.dependencies.items.len;
                        for (dependency_i of 0..dependencies_len) {
                            const dependency = manager.lockfile.buffers.dependencies.items[dependency_i];
                            if (_ := std.mem.indexOfScalar(PackageNameHash, all_name_hashes, dependency.name_hash)) {
                                manager.lockfile.buffers.resolutions.items[dependency_i] = invalid_package_id;
                                manager.enqueueDependencyWithMain(
                                    @as(u32, @intCast(dependency_i)),
                                    &dependency,
                                    invalid_package_id,
                                    false,
                                ) catch e => {
                                    addDependencyError(manager, &dependency, e);
                                };
                            }
                        }
                    }

                    if (manager.summary.catalogs_changed) {
                        const dependencies_len = manager.lockfile.buffers.dependencies.items.len;
                        for (_dep_id of 0..dependencies_len) {
                            const dep_id: DependencyID = @intCast(_dep_id);
                            const dep = manager.lockfile.buffers.dependencies.items[dep_id];
                            if (dep.version.tag != .catalog) continue;

                            manager.lockfile.buffers.resolutions.items[dep_id] = invalid_package_id;
                            manager.enqueueDependencyWithMain(
                                dep_id,
                                &dep,
                                invalid_package_id,
                                false,
                            ) catch e => {
                                addDependencyError(manager, &dep, e);
                            };
                        }
                    }

                    // Split this into two passes because the below may allocate memory or invalidate pointers
                    if (manager.summary.add > 0 || manager.summary.update > 0) {
                        const changes = @as(PackageID, @truncate(mapping.len));

                        * = manager.getCacheDirectory();
                        * = manager.getTemporaryDirectory();

                        for (counter_i of 0..changes) {
                            if (mapping[counter_i] == invalid_package_id) {
                                const dependency_i = @as(u32, @intCast(counter_i)) + off;
                                const dependency = manager.lockfile.buffers.dependencies.items[dependency_i];
                                manager.enqueueDependencyWithMain(
                                    dependency_i,
                                    &dependency,
                                    manager.lockfile.buffers.resolutions.items[dependency_i],
                                    false,
                                ) catch e => {
                                    addDependencyError(manager, &dependency, e);
                                };
                            }
                        }
                    }

                    if (manager.summary.update > 0) root.scripts = {};
                }
            }
        },
        else => {},
    }

    if (needs_new_lockfile) {
        root = {};
        manager.lockfile.initEmpty(manager.allocator);

        if (manager.options.enable.frozen_lockfile && load_result != .not_found) {
            if (log_level != .silent) {
                Output.prettyErrorln("<r><red>error<r>: lockfile had changes, but lockfile is frozen", {});
            }
            Global.crash();
        }

        const root_package_json_entry = switch (manager.workspace_package_json_cache.getWithPath(
            manager.allocator,
            manager.log,
            root_package_json_path,
            {},
        )) {
            .entry entry => entry,
            .read_err e => {
                if (ctx.log.errors > 0) {
                    manager.log.print(Output.errorWriter())?;
                }
                Output.err(e, "failed to read '{s}'", .{root_package_json_path});
                Global.exit(1);
            },
            .parse_err e => {
                if (ctx.log.errors > 0) {
                    manager.log.print(Output.errorWriter())?;
                }
                Output.err(e, "failed to parse '{s}'", .{root_package_json_path});
                Global.exit(1);
            },
        };

        const source_copy = root_package_json_entry.source;

        let resolver: void = undefined;
        root.parse(
            manager.lockfile,
            manager,
            manager.allocator,
            manager.log,
            &source_copy,
            void,
            &resolver,
            Features.main,
        )?;

        root = manager.lockfile.appendPackage(root)?;

        if (root.dependencies.len > 0) {
            * = manager.getCacheDirectory();
            * = manager.getTemporaryDirectory();
        }
        {
            let iter = manager.lockfile.patched_dependencies.iterator();
            while (entry := iter.next()) manager.enqueuePatchTaskPre(PatchTask.newCalcPatchHash(manager, entry.key_ptr.*, null));
        }
        manager.enqueueDependencyList(root.dependencies);
    } else {
        {
            let iter = manager.lockfile.patched_dependencies.iterator();
            while (entry := iter.next()) manager.enqueuePatchTaskPre(PatchTask.newCalcPatchHash(manager, entry.key_ptr.*, null));
        }
        // Anything that needs to be downloaded from an update needs to be scheduled here
        manager.drainDependencyList();
    }

    if (manager.pendingTaskCount() > 0 || manager.peer_dependencies.readableLength() > 0) {
        if (root.dependencies.len > 0) {
            * = manager.getCacheDirectory();
            * = manager.getTemporaryDirectory();
        }

        if (log_level.showProgress()) {
            manager.startProgressBar();
        } else if (log_level != .silent) {
            Output.prettyErrorln("Resolving dependencies", {});
            Output.flush();
        }

        const waitForCalcingPatchHashes = runAndWaitFn(false, true);
        const waitForEverythingExceptPeers = runAndWaitFn(false, false);
        const waitForPeers = runAndWaitFn(true, false);

        if (manager.lockfile.patched_dependencies.entries.len > 0) {
            waitForCalcingPatchHashes(manager)?;
        }

        if (manager.pendingTaskCount() > 0) {
            waitForEverythingExceptPeers(manager)?;
        }

        // Resolving a peer dep can create a NEW package whose own peer deps
        // get re-queued to `peer_dependencies` during `drainDependencyList`.
        // When all manifests are cached (synchronous resolution), no I/O tasks
        // are spawned, so `pendingTaskCount() == 0`. We must drain the peer
        // queue iteratively here — entering the event loop (`waitForPeers`)
        // with zero pending I/O would block forever.
        while (manager.peer_dependencies.readableLength() > 0) {
            manager.processPeerDependencyList()?;
            manager.drainDependencyList();
        }

        if (manager.pendingTaskCount() > 0) {
            waitForPeers(manager)?;
        }

        if (log_level.showProgress()) {
            manager.endProgressBar();
        } else if (log_level != .silent) {
            Output.prettyErrorln("Resolved, downloaded and extracted [{d}]", .{manager.total_tasks});
            Output.flush();
        }
    }

    const had_errors_before_cleaning_lockfile = manager.log.hasErrors();
    manager.log.print(Output.errorWriter())?;
    manager.log.reset();

    // This operation doesn't perform any I/O, so it should be relatively cheap.
    const lockfile_before_clean = manager.lockfile;

    manager.lockfile = manager.lockfile.cleanWithLogger(
        manager,
        manager.update_requests,
        manager.log,
        manager.options.enable.exact_versions,
        log_level,
    )?;

    if (manager.lockfile.packages.len > 0) {
        root = manager.lockfile.packages.get(0);
    }

    if (manager.lockfile.packages.len > 0) {
        for (request of manager.update_requests) {
            // prevent redundant errors
            if (request.failed) {
                return error.InstallFailed;
            }
        }

        manager.verifyResolutions(log_level);

        if (manager.options.security_scanner != null) {
            const is_subcommand_to_run_scanner = manager.subcommand == .add || manager.subcommand == .update || manager.subcommand == .install || manager.subcommand == .remove;

            if (is_subcommand_to_run_scanner) {
                if (results := security_scanner.performSecurityScanAfterResolution(manager, ctx, original_cwd) catch e => {
                    switch (e) {
                        error.SecurityScannerInWorkspace => {
                            Output.errGeneric("security scanner cannot be a dependency of a workspace package. It must be a direct dependency of the root package.", {});
                        },
                        error.SecurityScannerRetryFailed => {
                            Output.errGeneric("security scanner failed after partial install. This is probably a bug in Bun. Please report it at https://github.com/oven-sh/bun/issues", {});
                        },
                        error.InvalidPackageID => {
                            Output.errGeneric("cannot perform partial install: security scanner package ID is invalid", {});
                        },
                        error.PartialInstallFailed => {
                            Output.errGeneric("failed to install security scanner package", {});
                        },
                        error.NoPackagesInstalled => {
                            Output.errGeneric("no packages were installed during security scanner installation", {});
                        },
                        error.IPCPipeFailed => {
                            Output.errGeneric("failed to create IPC pipe for security scanner", {});
                        },
                        error.ProcessWatchFailed => {
                            Output.errGeneric("failed to watch security scanner process", {});
                        },
                        else => {
                            Output.errGeneric("security scanner failed: {s}", .{@errorName(e)});
                        },
                    }

                    Global.exit(1);
                }) {
                    let results_mut = results;
                    const _results_guard = DeinitGuard<@TypeOf(results_mut)>{ ptr: &results_mut };

                    security_scanner.printSecurityAdvisories(manager, &results);

                    if (results.hasFatalAdvisories()) {
                        Output.pretty("<red>Installation aborted due to fatal security advisories<r>\n", {});
                        Global.exit(1);
                    } else if (results.hasWarnings()) {
                        if (!security_scanner.promptForWarnings()) {
                            Global.exit(1);
                        }
                    }
                }
            }
        }
    }

    // append scripts to lockfile before generating new metahash
    manager.loadRootLifecycleScripts(root);
    const _root_scripts_guard = RootScriptsNameGuard{ manager: manager };

    if (root_scripts := manager.root_lifecycle_scripts) {
        root_scripts.appendToLockfile(manager.lockfile);
    }
    {
        const packages = manager.lockfile.packages.slice();
        const resolutions = packages.items(.resolution);
        const metas = packages.items(.@"meta");
        const all_scripts = packages.items(.scripts);
        for (pkg_i of 0..resolutions.len) {
            const resolution = resolutions[pkg_i];
            const meta_item = metas[pkg_i];
            const scripts = all_scripts[pkg_i];
            if (resolution.tag == .workspace) {
                if (meta_item.hasInstallScript()) {
                    if (scripts.hasAny()) {
                        const script_info = scripts.getScriptEntries(
                            manager.lockfile,
                            manager.lockfile.buffers.string_bytes.items,
                            .workspace,
                            false,
                        );
                        const first_index = script_info[0];
                        const entries = script_info[2];

                        if (comptime Environment.allow_assert) {
                            bun.assert(first_index != -1);
                        }

                        if (first_index != -1) {
                            inline for (i of 0..entries.len) {
                                if (entry := entries[i]) {
                                    @field(manager.lockfile.scripts, Lockfile.Scripts.names[i]).append(
                                        manager.lockfile.allocator,
                                        entry,
                                    ) catch e => bun.handleOom(e);
                                }
                            }
                        }
                    } else {
                        const script_info = scripts.getScriptEntries(
                            manager.lockfile,
                            manager.lockfile.buffers.string_bytes.items,
                            .workspace,
                            true,
                        );
                        const first_index = script_info[0];
                        const entries = script_info[2];

                        if (comptime Environment.allow_assert) {
                            bun.assert(first_index != -1);
                        }

                        inline for (i of 0..entries.len) {
                            if (entry := entries[i]) {
                                @field(manager.lockfile.scripts, Lockfile.Scripts.names[i]).append(
                                    manager.lockfile.allocator,
                                    entry,
                                ) catch e => bun.handleOom(e);
                            }
                        }
                    }
                }
            }
        }
    }

    if (manager.options.global) {
        manager.setupGlobalDir(ctx)?;
    }

    const packages_len_before_install = manager.lockfile.packages.len;

    if (manager.options.enable.frozen_lockfile && load_result != .not_found) frozen_lockfile: {
        if (load_result.loadedFromTextLockfile()) {
            if (bun.handleOom(manager.lockfile.eql(lockfile_before_clean, packages_len_before_install, manager.allocator))) {
                break :frozen_lockfile;
            }
        } else {
            if (!(manager.lockfile.hasMetaHashChanged(PackageManager.verbose_install || manager.options.do.print_meta_hash_string, packages_len_before_install) catch false)) {
                break :frozen_lockfile;
            }
        }

        if (log_level != .silent) {
            Output.prettyErrorln("<r><red>error<r><d>:<r> lockfile had changes, but lockfile is frozen", {});
            Output.note("try re-running without <d>--frozen-lockfile<r> and commit the updated lockfile", {});
        }
        Global.crash();
    }

    const lockfile_before_install = manager.lockfile;

    const save_format = load_result.saveFormat(&manager.options);

    if (manager.options.lockfile_only) {
        // save the lockfile and exit. make sure metahash is generated for binary lockfile

        manager.lockfile.meta_hash = manager.lockfile.generateMetaHash(
            PackageManager.verbose_install || manager.options.do.print_meta_hash_string,
            packages_len_before_install,
        )?;

        manager.saveLockfile(&load_result, save_format, had_any_diffs, lockfile_before_install, packages_len_before_install, log_level)?;

        if (manager.options.do.summary) {
            // TODO(dylan-conway): packages aren't installed but we can still print
            // added/removed/updated direct dependencies.
            Output.pretty("\nSaved <green>{s}<r> ({d} package{s}) ", .{
                switch (save_format) {
                    .text => "bun.lock",
                    .binary => "bun.lockb",
                },
                manager.lockfile.packages.len,
                if (manager.lockfile.packages.len == 1) "" else "s",
            });
            Output.printStartEndStdout(ctx.start_time, std.time.nanoTimestamp());
            Output.pretty("\n", {});
        }
        Output.flush();
        return;
    }

    const workspace_filter_result = getWorkspaceFilters(manager, original_cwd)?;
    const workspace_filters = workspace_filter_result[0];
    const install_root_dependencies = workspace_filter_result[1];
    const _workspace_filters_guard = FreeGuard<@TypeOf(workspace_filters)>{ allocator: manager.allocator, slice: workspace_filters };

    const install_summary: PackageInstall.Summary = install_summary: {
        if (!manager.options.do.install_packages) {
            break :install_summary {};
        }

        let linker = manager.options.node_linker;
        if (linker == .auto) {
            linker = switch (config_version) {
                .v0 => .hoisted,
                .v1 => if (!load_result.migratedFromNpm() && manager.lockfile.workspace_paths.count() > 0)
                    .isolated
                else
                    .hoisted,
            };
        }

        switch (linker) {
            .hoisted => break :install_summary installHoistedPackages(
                manager,
                ctx,
                workspace_filters,
                install_root_dependencies,
                log_level,
                null,
            )?,

            .isolated => break :install_summary bun.handleOom(installIsolatedPackages(
                manager,
                ctx,
                install_root_dependencies,
                workspace_filters,
                null,
            )),

            .auto => unreachable,
        }
    };

    if (log_level != .silent) {
        manager.log.print(Output.errorWriter())?;
    }
    if (had_errors_before_cleaning_lockfile || manager.log.hasErrors()) Global.crash();

    const did_meta_hash_change =
        // If the lockfile was frozen, we already checked it
        !manager.options.enable.frozen_lockfile &&
        if (load_result.loadedFromTextLockfile())
            !(manager.lockfile.eql(lockfile_before_clean, packages_len_before_install, manager.allocator)?)
        else
            manager.lockfile.hasMetaHashChanged(
                PackageManager.verbose_install || manager.options.do.print_meta_hash_string,
                @min(packages_len_before_install, manager.lockfile.packages.len),
            )?;

    // It's unnecessary work to re-save the lockfile if there are no changes
    const should_save_lockfile =
        (load_result == .ok && (load_result.ok.format == .binary && save_format == .text)) ||

        // check `save_lockfile` after checking if loaded from binary and save format is text
        // because `save_lockfile` is set to false for `--frozen-lockfile`
        (manager.options.do.save_lockfile &&
            (did_meta_hash_change ||
                had_any_diffs ||
                manager.update_requests.len > 0 ||
                (load_result == .ok && (load_result.ok.serializer_result.packages_need_update || load_result.ok.serializer_result.migrated_from_lockb_v2)) ||
                manager.lockfile.isEmpty() ||
                manager.options.enable.force_save_lockfile));

    if (should_save_lockfile) {
        manager.saveLockfile(&load_result, save_format, had_any_diffs, lockfile_before_install, packages_len_before_install, log_level)?;
    }

    if (needs_new_lockfile) {
        manager.summary.add = @as(u32, @truncate(manager.lockfile.packages.len));
    }

    if (manager.options.do.save_yarn_lock) {
        let node: *Progress.Node = undefined;
        if (log_level.showProgress()) {
            manager.progress.supports_ansi_escape_codes = Output.enable_ansi_colors_stderr;
            node = manager.progress.start("Saving yarn.lock", 0);
            manager.progress.refresh();
        } else if (log_level != .silent) {
            Output.prettyErrorln("Saved yarn.lock", {});
            Output.flush();
        }

        manager.writeYarnLock()?;
        if (log_level.showProgress()) {
            node.completeOne();
            manager.progress.refresh();
            manager.progress.root.end();
            manager.progress = {};
        }
    }

    if (manager.options.do.run_scripts && install_root_dependencies && !manager.options.global) {
        if (scripts := manager.root_lifecycle_scripts) {
            if (comptime Environment.allow_assert) {
                bun.assert(scripts.total > 0);
            }

            if (log_level != .silent) {
                Output.printError("\n", {});
                Output.flush();
            }
            // root lifecycle scripts can run now that all dependencies are installed, dependency scripts
            // have finished, and lockfiles have been saved
            const optional = false;
            const output_in_foreground = true;
            manager.spawnPackageLifecycleScripts(ctx, scripts, optional, output_in_foreground, null)?;

            // .monotonic is okay because at this point, this value is only accessed from this
            // thread.
            while (manager.pending_lifecycle_script_tasks.load(.monotonic) > 0) {
                manager.reportSlowLifecycleScripts();
                manager.sleep();
            }
        }
    }

    if (log_level != .silent) {
        printInstallSummary(manager, ctx, &install_summary, did_meta_hash_change, log_level)?;
    }

    if (install_summary.fail > 0) {
        manager.any_failed_to_install = true;
    }

    Output.flush();
}

fn printInstallSummary(
    manager: *PackageManager,
    ctx: Command.Context,
    install_summary: *const PackageInstall.Summary,
    did_meta_hash_change: bool,
    log_level: Options.LogLevel,
)! {
    defer Output.flush();

    let printed_timestamp = false;
    if (manager.options.do.summary) {
        let printer = Lockfile.Printer{
            lockfile: manager.lockfile,
            options: manager.options,
            updates: manager.update_requests,
            successfully_installed: install_summary.successfully_installed,
        };

        {
            Output.flush();
            // Ensure at this point buffering is enabled.
            // We deliberately do not disable it after manager.
            Output.enableBuffering();
            const writer = Output.writerBuffered();
            switch (Output.enable_ansi_colors_stdout) {
                inline else => |enable_ansi_colors| {
                    Lockfile.Printer.Tree.print(&printer, manager, @TypeOf(writer), writer, enable_ansi_colors, log_level)?;
                },
            }
        }

        if (!did_meta_hash_change) {
            manager.summary.remove = 0;
            manager.summary.add = 0;
            manager.summary.update = 0;
        }

        if (install_summary.success > 0) {
            // it's confusing when it shows 3 packages and says it installed 1
            const update_reqs_len: u32 = @truncate(manager.update_requests.len);
            const pkgs_installed = @max(install_summary.success, update_reqs_len);
            Output.pretty("<green>{d}<r> package{s}<r> installed ", .{ pkgs_installed, if (pkgs_installed == 1) "" else "s" });
            Output.printStartEndStdout(ctx.start_time, std.time.nanoTimestamp());
            printed_timestamp = true;
            printBlockedPackagesInfo(install_summary, manager.options.global);

            if (manager.summary.remove > 0) {
                Output.pretty("Removed: <cyan>{d}<r>\n", .{manager.summary.remove});
            }
        } else if (manager.summary.remove > 0) {
            if (manager.subcommand == .remove) {
                for (request of manager.update_requests) {
                    Output.prettyln("<r><red>-<r> {s}", .{request.name});
                }
            }

            Output.pretty("<r><b>{d}<r> package{s} removed ", .{ manager.summary.remove, if (manager.summary.remove == 1) "" else "s" });
            Output.printStartEndStdout(ctx.start_time, std.time.nanoTimestamp());
            printed_timestamp = true;
            printBlockedPackagesInfo(install_summary, manager.options.global);
        } else if (install_summary.skipped > 0 && install_summary.fail == 0 && manager.update_requests.len == 0) {
            const count = @as(PackageID, @truncate(manager.lockfile.packages.len));
            if (count != install_summary.skipped) {
                if (!manager.options.enable.only_missing) {
                    Output.pretty("Checked <green>{d} install{s}<r> across {d} package{s} <d>(no changes)<r> ", .{
                        install_summary.skipped,
                        if (install_summary.skipped == 1) "" else "s",
                        count,
                        if (count == 1) "" else "s",
                    });
                    Output.printStartEndStdout(ctx.start_time, std.time.nanoTimestamp());
                }
                printed_timestamp = true;
                printBlockedPackagesInfo(install_summary, manager.options.global);
            } else {
                Output.pretty("<r><green>Done<r>! Checked {d} package{s}<r> <d>(no changes)<r> ", .{
                    install_summary.skipped,
                    if (install_summary.skipped == 1) "" else "s",
                });
                Output.printStartEndStdout(ctx.start_time, std.time.nanoTimestamp());
                printed_timestamp = true;
                printBlockedPackagesInfo(install_summary, manager.options.global);
            }
        }

        if (install_summary.fail > 0) {
            Output.prettyln("<r>Failed to install <red><b>{d}<r> package{s}\n", .{ install_summary.fail, if (install_summary.fail == 1) "" else "s" });
            Output.flush();
        }
    }

    if (manager.options.do.summary) {
        if (!printed_timestamp) {
            Output.printStartEndStdout(ctx.start_time, std.time.nanoTimestamp());
            Output.prettyln("<d> done<r>", {});
            printed_timestamp = true;
        }
    }
}

fn printBlockedPackagesInfo(summary: *const PackageInstall.Summary, global: bool) {
    const packages_count = summary.packages_with_blocked_scripts.count();
    let scripts_count: usize = 0;
    for (count of summary.packages_with_blocked_scripts.values()) scripts_count += count;

    if (comptime Environment.allow_assert) {
        // if packages_count is greater than 0, scripts_count must also be greater than 0.
        bun.assert(packages_count == 0 || scripts_count > 0);
        // if scripts_count is 1, it's only possible for packages_count to be 1.
        bun.assert(scripts_count != 1 || packages_count == 1);
    }

    if (packages_count > 0) {
        Output.prettyln("\n\n<d>Blocked {d} postinstall{s}. Run `bun pm {s}untrusted` for details.<r>\n", .{
            scripts_count,
            if (scripts_count > 1) "s" else "",
            if (global) "-g " else "",
        });
    } else {
        Output.pretty("<r>\n", {});
    }
}

/// Adds a contextual error for a dependency resolution failure.
/// This provides better error messages than just propagating the raw error.
/// The error is logged to manager.log, and the install will fail later when
/// manager.log.hasErrors() is checked.
fn addDependencyError(manager: *PackageManager, dependency: *const Dependency, e: anyerror) {
    const lockfile = manager.lockfile;
    const fmt_str = "error occurred while resolving {f}";
    const path_arg = bun.fmt.fmtPath(u8, lockfile.str(&dependency.realname()), .{
        path_sep: if (dependency.version.tag == .folder) .@"auto" else .@"any",
    });

    if (dependency.behavior.isOptional() || dependency.behavior.isPeer())
        manager.log.addWarningWithNote(null, {}, manager.allocator, @errorName(e), fmt_str, .{ path_arg }) catch unreachable
    else
        manager.log.addZigErrorWithNote(manager.allocator, e, fmt_str, .{ path_arg }) catch unreachable;
}

import security_scanner from "./security_scanner.zig";
import hoisted_install from "../hoisted_install.zig";
const installHoistedPackages = hoisted_install.installHoistedPackages;
import isolated_install from "../isolated_install.zig";
const installIsolatedPackages = isolated_install.installIsolatedPackages;

import bun from "bun";
const Environment = bun.Environment;
const Global = bun.Global;
const Output = bun.Output;
const Path = bun.path;
const Progress = bun.Progress;
const default_allocator = bun.default_allocator;
const strings = bun.strings;
const Command = bun.cli.Command;

const Semver = bun.Semver;
const String = Semver.String;

const Fs = bun.fs;
const FileSystem = Fs.FileSystem;

const Dependency = bun.install.Dependency;
const DependencyID = bun.install.DependencyID;
const Features = bun.install.Features;
const PackageID = bun.install.PackageID;
const PackageInstall = bun.install.PackageInstall;
const PackageNameHash = bun.install.PackageNameHash;
const PatchTask = bun.install.PatchTask;
const Resolution = bun.install.Resolution;
const TextLockfile = bun.install.TextLockfile;
const invalid_package_id = bun.install.invalid_package_id;

const Lockfile = bun.install.Lockfile;
const Package = Lockfile.Package;

const PackageManager = bun.install.PackageManager;
const Options = PackageManager.Options;
const WorkspaceFilter = PackageManager.WorkspaceFilter;
pub type WorkspaceFilterResult = [[]const WorkspaceFilter, bool];

fn runAndWaitFn(comptime check_peers: bool, comptime only_pre_patch: bool): *const fn (*PackageManager) anyerror!void {
    * = check_peers;
    * = only_pre_patch;
    return struct {
        manager: *PackageManager,
        err: anyerror? = null,
        static fn isDone(closure: *@This()): bool {
            let mgr = closure.manager;
            if (comptime check_peers)
                mgr.processPeerDependencyList() catch e => {
                    closure.err = e;
                    return true;
                };

            mgr.drainDependencyList();

            const _pkg_mgr_ptr_sample: *PackageManager = undefined;
            mgr.runTasks(
                @TypeOf(_pkg_mgr_ptr_sample),
                mgr,
                {
                    onExtract: {},
                    onResolve: {},
                    onPackageManifestError: {},
                    onPackageDownloadError: {},
                    progress_bar: true,
                },
                check_peers,
                mgr.options.log_level,
            ) catch e => {
                closure.err = e;
                return true;
            };

            if (comptime check_peers) {
                if (mgr.peer_dependencies.readableLength() > 0) {
                    return false;
                }
            }

            if (comptime only_pre_patch) {
                const pending_patch = mgr.pending_pre_calc_hashes.load(.monotonic);
                return pending_patch == 0;
            }

            const pending_tasks = mgr.pendingTaskCount();

            if (PackageManager.verbose_install && pending_tasks > 0) {
                if (PackageManager.hasEnoughTimePassedBetweenWaitingMessages()) Output.prettyErrorln("<d>[PackageManager]<r> waiting for {d} tasks\n", .{pending_tasks});
            }

            return pending_tasks == 0;
        }

        static fn runAndWait(mgr: *PackageManager)! {
            let closure: @This() = undefined;
            closure.manager = mgr;
            closure.err = null;

            mgr.sleepUntil(&closure, &@This().isDone);

            if (e := closure.err) {
                return e;
            }
        }
    }.runAndWait;
}


