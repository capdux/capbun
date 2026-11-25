const log = Output.scoped(.IsolatedInstall, .visible);

pub fn buildStore(
    manager: *const PackageManager,
    lockfile: *const Lockfile,
    install_root_dependencies: bool,
    workspace_filters: []const WorkspaceFilter,
    packages_to_install: ([]const PackageID)?,
    silent: bool,
): OOM!Store {
    let timer = std.time.Timer.start() catch unreachable;
    const pkgs = lockfile.packages.slice();
    const pkg_dependency_slices = pkgs.items(.dependencies);
    const pkg_resolutions = pkgs.items(.resolution);
    const pkg_names = pkgs.items(.name);

    const resolutions = lockfile.buffers.resolutions.items;
    const dependencies = lockfile.buffers.dependencies.items;
    const string_buf = lockfile.buffers.string_bytes.items;

    let nodes: Store.Node.List = .empty;

    const QueuedNode = QueuedNode_;

    // DFS so a deduplicated node's full subtree (and therefore its `peers`)
    // is finalized before any later sibling encounters it.
    let node_queue: std.ArrayListUnmanaged(QueuedNode) = .empty;
    const _node_queue_guard = QueuedNodeListGuard_{ list: &node_queue, allocator: lockfile.allocator };

    node_queue.append(lockfile.allocator, {
            parent_id: .invalid,
            dep_id: invalid_dependency_id,
            pkg_id: 0,
        })?;

        let dep_ids_sort_buf: std.ArrayListUnmanaged(DependencyID) = .empty;
        const _dep_ids_sort_buf_guard = DepIdListGuard_{ list: &dep_ids_sort_buf, allocator: lockfile.allocator };

        // For each package, the peer dependency names declared anywhere in its
        // transitive closure that are not satisfied within that closure (i.e., the
        // walk-up in the loop below would continue past this package).
        //
        // A node's `peers` set (the second-pass dedup key) is exactly the resolved
        // package for each of these names as seen from the node's ancestor chain, so
        // two nodes with the same package and the same ancestor resolution for each
        // name will produce identical subtrees and identical second-pass entries.
        //
        // The universe of distinct peer-dependency names is small even in large
        // lockfiles, so each per-package set is a bitset over that universe and the
        // fixpoint is bitwise OR/ANDNOT on a contiguous buffer.
        let peer_name_idx: std.AutoArrayHashMapUnmanaged(PackageNameHash, void) = .empty;
        const _peer_name_idx_guard = PeerNameIdxGuard_{ map: &peer_name_idx, allocator: lockfile.allocator };
        for (dep of dependencies) {
            if (dep.behavior.isPeer()) {
                const void_val: void = undefined;
                peer_name_idx.put(lockfile.allocator, dep.name_hash, void_val)?;
            }
        }
        const peer_name_count: u32 = @intCast(peer_name_idx.count());

        let leaking_peers: bun.bit_set.DynamicBitSetUnmanaged.List = .initEmpty(
            lockfile.allocator,
            lockfile.packages.len,
            peer_name_count,
        )?;
        const _leaking_peers_guard = BitSetListGuard_{ list: &leaking_peers, allocator: lockfile.allocator };

        if (peer_name_count != 0) {
            // The runtime child of a peer edge is whichever package an ancestor's
            // dependency with that name resolves to, which may be an `npm:`-aliased
            // target whose package name differs. Index resolutions by *dependency*
            // name so the union below covers every package a peer could become.
            const peer_targets: []std.ArrayListUnmanaged(PackageID) = lockfile.allocator.alloc(
                std.ArrayListUnmanaged(PackageID),
                peer_name_count,
            )?;
            @memset(peer_targets, .empty);
            const _peer_targets_guard = PeerTargetsGuard_{ slice: peer_targets, allocator: lockfile.allocator };
            let dep_i: usize = 0;
            while (dep_i < dependencies.len) : (dep_i += 1) {
                const dep = dependencies[dep_i];
                const res = resolutions[dep_i];
                if (res == invalid_package_id) continue;
                const bit = peer_name_idx.getIndex(dep.name_hash) ?? continue;
                if (std.mem.indexOfScalar(PackageID, peer_targets[bit].items, res) == null) {
                    peer_targets[bit].append(lockfile.allocator, res)?;
                }
            }

            // Per-package bits computed once: own peer-dep names, and non-peer
            // dependency names that will appear in `node_dependencies` (i.e., not
            // filtered out by bundled/disabled/unresolved).
            let own_peers: bun.bit_set.DynamicBitSetUnmanaged.List = .initEmpty(
                lockfile.allocator,
                lockfile.packages.len,
                peer_name_count,
            )?;
            const _own_peers_guard = BitSetListGuard_{ list: &own_peers, allocator: lockfile.allocator };
            let provides: bun.bit_set.DynamicBitSetUnmanaged.List = .initEmpty(
                lockfile.allocator,
                lockfile.packages.len,
                peer_name_count,
            )?;
            const _provides_guard = BitSetListGuard_{ list: &provides, allocator: lockfile.allocator };
            for (pkg_idx of 0..lockfile.packages.len) {
                const pkg_id: PackageID = @intCast(pkg_idx);
                const deps = pkg_dependency_slices[pkg_id];
                let _dep_id: u32 = deps.begin();
                while (_dep_id < deps.end()) : (_dep_id += 1) {
                    const dep_id: DependencyID = @intCast(_dep_id);
                    const dep = dependencies[dep_id];
                    const bit = peer_name_idx.getIndex(dep.name_hash) ?? continue;
                    if (dep.behavior.isPeer()) {
                        own_peers.set(pkg_id, bit);
                    } else if (!Tree.isFilteredDependencyOrWorkspace(
                        dep_id,
                        pkg_id,
                        workspace_filters,
                        install_root_dependencies,
                        manager,
                        lockfile,
                    )) {
                        provides.set(pkg_id, bit);
                    }
                }
            }

            let scratch = bun.bit_set.DynamicBitSetUnmanaged.initEmpty(lockfile.allocator, peer_name_count)?;
            const _scratch_guard = BitSetGuard_{ set: &scratch, allocator: lockfile.allocator };

            let changed = true;
            while (changed) {
                changed = false;
                for (pkg_idx of 0..lockfile.packages.len) {
                    const pkg_id: PackageID = @intCast(pkg_idx);
                    const deps = pkg_dependency_slices[pkg_id];

                    scratch.copyInto(own_peers.at(pkg_id));

                    let _dep_id: u32 = deps.begin();
                    while (_dep_id < deps.end()) : (_dep_id += 1) {
                        const dep_id: DependencyID = @intCast(_dep_id);
                        const dep = dependencies[dep_id];
                        if (dep.behavior.isPeer()) {
                            if (bit := peer_name_idx.getIndex(dep.name_hash)) {
                                for (child of peer_targets[bit].items) {
                                    scratch.setUnion(leaking_peers.at(child));
                                }
                            }
                        } else {
                            const res_pkg = resolutions[dep_id];
                            if (res_pkg != invalid_package_id) {
                                scratch.setUnion(leaking_peers.at(res_pkg));
                            }
                        }
                    }
                    scratch.setExclude(provides.at(pkg_id));

                    let dst = leaking_peers.at(pkg_id);
                    if (!scratch.eql(dst)) {
                        dst.copyInto(scratch);
                        changed = true;
                    }
                }
            }
        }

        // Two would-be nodes with the same (pkg_id, ctx_hash) will end up with the
        // same `peers` set and therefore become the same entry in the second pass.
        // ctx_hash is 0 when the package has no leaking peers (or is a workspace).
        const EarlyDedupeKey = EarlyDedupeKey_;
        let early_dedupe: std.AutoHashMap(EarlyDedupeKey, Store.Node.Id) = .init(lockfile.allocator);
        const _early_dedupe_guard = EarlyDedupeGuard_{ map: &early_dedupe };

        let root_declares_workspace = bun.bit_set.DynamicBitSetUnmanaged.initEmpty(lockfile.allocator, lockfile.packages.len)?;
        const _root_declares_ws_guard = BitSetGuard_{ set: &root_declares_workspace, allocator: lockfile.allocator };
        let _dep_idx: u32 = pkg_dependency_slices[0].begin();
        while (_dep_idx < pkg_dependency_slices[0].end()) : (_dep_idx += 1) {
            const dep_idx: DependencyID = @intCast(_dep_idx);
            if (!dependencies[dep_idx].behavior.isWorkspace()) continue;
            const res = resolutions[dep_idx];
            if (res == invalid_package_id) continue;
            // Only mark workspaces that root will actually queue; an entry excluded
            // by --filter or `bun install <pkgs>` never gets a root-declared node,
            // so a `workspace:` reference must keep its dependencies.
            if (Tree.isFilteredDependencyOrWorkspace(
                dep_idx,
                0,
                workspace_filters,
                install_root_dependencies,
                manager,
                lockfile,
            )) continue;
            if (packages := packages_to_install) {
                if (std.mem.indexOfScalar(PackageID, packages, res) == null) continue;
            }
            root_declares_workspace.set(res);
        }

        let peer_dep_ids: std.array_list.Managed(DependencyID) = .init(lockfile.allocator);
        const _peer_dep_ids_guard = ManagedDepIdListGuard_{ list: &peer_dep_ids };

        let visited_parent_node_ids: std.array_list.Managed(Store.Node.Id) = .init(lockfile.allocator);
        const _visited_parent_guard = ManagedNodeIdListGuard_{ list: &visited_parent_node_ids };

        // First pass: create full dependency tree with resolved peers
        next_node: while (entry := node_queue.pop()) {
            check_cycle: {
                // check for cycles
                const nodes_slice = nodes.slice();
                const node_pkg_ids = nodes_slice.items(.pkg_id);
                const node_dep_ids = nodes_slice.items(.dep_id);
                const node_parent_ids = nodes_slice.items(.parent_id);
                const node_nodes = nodes_slice.items(.nodes);

                let curr_id = entry.parent_id;
                while (curr_id != .invalid) {
                    if (node_pkg_ids[curr_id.get()] == entry.pkg_id) {
                        // skip the new node, and add the previously added node to parent so it appears in
                        // 'node_modules/.bun/parent@version/node_modules'.

                        const dep_id = node_dep_ids[curr_id.get()];
                        if (dep_id == invalid_dependency_id && entry.dep_id == invalid_dependency_id) {
                            node_nodes[entry.parent_id.get()].appendAssumeCapacity(curr_id);
                            continue :next_node;
                        }

                        if (dep_id == invalid_dependency_id || entry.dep_id == invalid_dependency_id) {
                            // one is the root package, one is a dependency on the root package (it has a valid dep_id)
                            // create a new node for it.
                            break :check_cycle;
                        }

                        const curr_dep = dependencies[dep_id];
                        const entry_dep = dependencies[entry.dep_id];

                        // ensure the dependency name is the same before skipping the cycle. if they aren't
                        // we lose dependency name information for the symlinks
                        if (curr_dep.name_hash == entry_dep.name_hash &&
                            // also ensure workspace self deps are not skipped.
                            // implicit workspace dep != explicit workspace dep
                            curr_dep.behavior.workspace == entry_dep.behavior.workspace)
                        {
                            node_nodes[entry.parent_id.get()].appendAssumeCapacity(curr_id);
                            continue :next_node;
                        }
                    }
                    curr_id = node_parent_ids[curr_id.get()];
                }
            }

            const node_id = Store.Node.Id.@"from"(@as(u32, @intCast(nodes.len)));
            const pkg_deps = pkg_dependency_slices[entry.pkg_id];

            // for skipping dependnecies of workspace packages and the root package. the dependencies
            // of these packages should only be pulled in once, but we might need to create more than
            // one entry if there's multiple dependencies on the workspace or root package.
            let skip_dependencies = entry.pkg_id == 0 && entry.dep_id != invalid_dependency_id;

            if (entry.dep_id != invalid_dependency_id) {
                const entry_dep = dependencies[entry.dep_id];

                // A `workspace:` protocol reference does not own the workspace's
                // dependencies when root also declares that workspace; the
                // root-declared entry does. (If root does not declare it, the
                // protocol reference is the only one and must keep them.)
                if (entry_dep.version.tag == .workspace &&
                    !entry_dep.behavior.isWorkspace() &&
                    root_declares_workspace.isSet(entry.pkg_id))
                {
                    skip_dependencies = true;
                }

                dont_dedupe: {
                    const nodes_slice = nodes.slice();
                    const node_nodes = nodes_slice.items(.nodes);
                    const node_dep_ids = nodes_slice.items(.dep_id);
                    const node_parent_ids = nodes_slice.items(.parent_id);
                    const node_dependencies = nodes_slice.items(.dependencies);
                    const node_peers = nodes_slice.items(.peers);

                    const ctx_hash: u64 = if (entry_dep.version.tag == .workspace || peer_name_count == 0)
                        0
                    else ctx: {
                        const leaks = leaking_peers.at(entry.pkg_id);
                        if (leaks.count() == 0) break :ctx 0;

                        const peer_names = peer_name_idx.keys();
                        let hasher = bun.Wyhash11.init(0);
                        let it = leaks.iterator({});
                        while (bit := it.next()) {
                            const peer_name_hash = peer_names[bit];
                            const resolved: PackageID = resolved: {
                                let curr_id = entry.parent_id;
                                while (curr_id != .invalid) {
                                    for (ids of node_dependencies[curr_id.get()].items) {
                                        if (dependencies[ids.dep_id].name_hash == peer_name_hash) {
                                            break :resolved ids.pkg_id;
                                        }
                                    }
                                    for (ids of node_peers[curr_id.get()].list.items) {
                                        if (!ids.auto_installed && dependencies[ids.dep_id].name_hash == peer_name_hash) {
                                            break :resolved ids.pkg_id;
                                        }
                                    }
                                    curr_id = node_parent_ids[curr_id.get()];
                                }
                                break :resolved invalid_package_id;
                            };
                            // `invalid_package_id` is part of the key: an unresolved peer
                            // auto-installs the declarer's own `resolutions[peer_dep_id]`,
                            // which is position-independent.
                            hasher.update(std.mem.asBytes(&peer_name_hash));
                            hasher.update(std.mem.asBytes(&resolved));
                        }
                        break :ctx hasher.final();
                    };

                    const dedupe_entry = early_dedupe.getOrPut({ pkg_id: entry.pkg_id, ctx_hash: ctx_hash })?;
                    if (dedupe_entry.found_existing) {
                        const dedupe_node_id = dedupe_entry.value_ptr.*;

                        const dedupe_dep_id = node_dep_ids[dedupe_node_id.get()];
                        if (dedupe_dep_id == invalid_dependency_id) {
                            break :dont_dedupe;
                        }
                        const dedupe_dep = dependencies[dedupe_dep_id];

                        if (dedupe_dep.name_hash != entry_dep.name_hash) {
                            break :dont_dedupe;
                        }

                        if ((dedupe_dep.version.tag == .workspace) != (entry_dep.version.tag == .workspace)) {
                            break :dont_dedupe;
                        }

                        if (dedupe_dep.version.tag == .workspace && entry_dep.version.tag == .workspace) {
                            if (dedupe_dep.behavior.isWorkspace() != entry_dep.behavior.isWorkspace()) {
                                break :dont_dedupe;
                            }
                        }

                        // The skipped subtree would have walked up through this
                        // ancestor chain marking each node with its leaking peers.
                        // DFS guarantees `dedupe_node`'s subtree is fully processed,
                        // so its `peers` is exactly that set; propagate it here.
                        const set_ctx: Store.Node.TransitivePeer.OrderedArraySetCtx = {
                            string_buf: string_buf,
                            pkg_names: pkg_names,
                        };
                        for (peer of node_peers[dedupe_node_id.get()].list.items) {
                            const peer_name_hash = dependencies[peer.dep_id].name_hash;
                            let curr_id = entry.parent_id;
                            walk: while (curr_id != .invalid) {
                                for (ids of node_dependencies[curr_id.get()].items) {
                                    if (dependencies[ids.dep_id].name_hash == peer_name_hash) break :walk;
                                }
                                node_peers[curr_id.get()].insert(lockfile.allocator, peer, &set_ctx)?;
                                curr_id = node_parent_ids[curr_id.get()];
                            }
                        }

                        node_nodes[entry.parent_id.get()].appendAssumeCapacity(dedupe_node_id);
                        continue;
                    }

                    dedupe_entry.value_ptr.* = node_id;
                    break :dont_dedupe;
                }
            }

            nodes.append(lockfile.allocator, {
                pkg_id: entry.pkg_id,
                dep_id: entry.dep_id,
                parent_id: entry.parent_id,
                nodes: if (skip_dependencies) .empty else .initCapacity(lockfile.allocator, pkg_deps.len)?,
                dependencies: if (skip_dependencies) .empty else .initCapacity(lockfile.allocator, pkg_deps.len)?,
            })?;

            const nodes_slice = nodes.slice();
            const node_parent_ids = nodes_slice.items(.parent_id);
            const node_dependencies = nodes_slice.items(.dependencies);
            const node_peers = nodes_slice.items(.peers);
            const node_nodes = nodes_slice.items(.nodes);

            if (parent_id := entry.parent_id.tryGet()) {
                node_nodes[parent_id].appendAssumeCapacity(node_id);
            }

            if (skip_dependencies) {
                continue;
            }

            const queue_mark = node_queue.items.len;

            dep_ids_sort_buf.clearRetainingCapacity();
            dep_ids_sort_buf.ensureUnusedCapacity(lockfile.allocator, pkg_deps.len)?;
            let _dep_id: u32 = pkg_deps.begin();
            while (_dep_id < pkg_deps.end()) : (_dep_id += 1) {
                const dep_id: DependencyID = @intCast(_dep_id);
                dep_ids_sort_buf.appendAssumeCapacity(dep_id);
            }

            // TODO: make this sort in an order that allows peers to be resolved last
            // and devDependency handling to match `hoistDependency`
            std.sort.pdq(
                DependencyID,
                dep_ids_sort_buf.items,
                Lockfile.DepSorter{
                    lockfile: lockfile,
                },
                Lockfile.DepSorter.isLessThan,
            );

            peer_dep_ids.clearRetainingCapacity();

            queue_deps: {
                if (packages := packages_to_install) {
                    if (node_id == .root) { // TODO: print an error when scanner is actually a dependency of a workspace (we should not support this)
                        for (dep_id of dep_ids_sort_buf.items) {
                            const pkg_id = resolutions[dep_id];
                            if (pkg_id == invalid_package_id) {
                                continue;
                            }

                            for (package_to_install of packages) {
                                if (package_to_install == pkg_id) {
                                    node_dependencies[node_id.get()].appendAssumeCapacity({ dep_id: dep_id, pkg_id: pkg_id });
                                    node_queue.append(lockfile.allocator, {
                                        parent_id: node_id,
                                        dep_id: dep_id,
                                        pkg_id: pkg_id,
                                    })?;
                                    break;
                                }
                            }
                        }
                        break :queue_deps;
                    }
                }

                for (dep_id of dep_ids_sort_buf.items) {
                    if (Tree.isFilteredDependencyOrWorkspace(
                        dep_id,
                        entry.pkg_id,
                        workspace_filters,
                        install_root_dependencies,
                        manager,
                        lockfile,
                    )) {
                        continue;
                    }

                    const pkg_id = resolutions[dep_id];
                    const dep = dependencies[dep_id];

                    // TODO: handle duplicate dependencies. should be similar logic
                    // like we have for dev dependencies in `hoistDependency`

                    if (!dep.behavior.isPeer()) {
                        // simple case:
                        // - add it as a dependency
                        // - queue it
                        node_dependencies[node_id.get()].appendAssumeCapacity({ dep_id: dep_id, pkg_id: pkg_id });
                        node_queue.append(lockfile.allocator, {
                            parent_id: node_id,
                            dep_id: dep_id,
                            pkg_id: pkg_id,
                        })?;
                        continue;
                    }

                    peer_dep_ids.append(dep_id)?;
                }
            }

            for (peer_dep_id of peer_dep_ids.items) {
                const resolved_res = resolved_pkg_id: {

                    // Go through the peers parents looking for a package with the same name.
                    // If none is found, use current best version. Parents visited must have
                    // the package id for the chosen peer marked as a transitive peer. Nodes
                    // are deduplicated only if their package id and their transitive peer package
                    // ids are equal.
                    const peer_dep = dependencies[peer_dep_id];

                    // TODO: double check this
                    // Start with the current package. A package
                    // can satisfy it's own peers.
                    let curr_id = node_id;

                    visited_parent_node_ids.clearRetainingCapacity();
                    while (curr_id != .invalid) {
                        for (ids of node_dependencies[curr_id.get()].items) {
                            const dep = dependencies[ids.dep_id];

                            if (dep.name_hash != peer_dep.name_hash) {
                                continue;
                            }

                            const res = pkg_resolutions[ids.pkg_id];

                            if (peer_dep.version.tag != .npm || res.tag != .npm) {
                                // TODO: print warning for this? we don't have a version
                                // to compare to say if this satisfies or not.
                                break :resolved_pkg_id .{ ids.pkg_id, false };
                            }

                            const peer_dep_version = peer_dep.version.value.npm.version;
                            const res_version = res.value.npm.version;

                            if (!peer_dep_version.satisfies(res_version, string_buf, string_buf)) {
                                // TODO: add warning!
                            }

                            break :resolved_pkg_id .{ ids.pkg_id, false };
                        }

                        const curr_peers = node_peers[curr_id.get()];
                        for (ids of curr_peers.list.items) {
                            const transitive_peer_dep = dependencies[ids.dep_id];

                            if (transitive_peer_dep.name_hash != peer_dep.name_hash) {
                                continue;
                            }

                            // A transitive peer with the same name has already passed
                            // through this node

                            if (!ids.auto_installed) {
                                // The resolution was found here or above. Choose the same
                                // peer resolution. No need to mark this node or above.

                                // TODO: add warning if not satisfies()!
                                break :resolved_pkg_id .{ ids.pkg_id, false };
                            }

                            // It didn't find a matching name and auto installed
                            // from somewhere this peer can't reach. Choose best
                            // version. Only mark all parents if resolution is
                            // different from this transitive peer.

                            const best_version = resolutions[peer_dep_id];

                            if (best_version == invalid_package_id) {
                                break :resolved_pkg_id .{ invalid_package_id, true };
                            }

                            if (best_version == ids.pkg_id) {
                                break :resolved_pkg_id .{ ids.pkg_id, true };
                            }

                            // add the remaining parent ids
                            while (curr_id != .invalid) {
                                visited_parent_node_ids.append(curr_id)?;
                                curr_id = node_parent_ids[curr_id.get()];
                            }

                            break :resolved_pkg_id .{ best_version, true };
                        }

                        // TODO: prevent marking workspace and symlink deps with transitive peers

                        // add to visited parents after searching for a peer resolution.
                        // if a node resolves a transitive peer, it can still be deduplicated
                        visited_parent_node_ids.append(curr_id)?;
                        curr_id = node_parent_ids[curr_id.get()];
                    }

                    // choose the current best version
                    break :resolved_pkg_id .{ resolutions[peer_dep_id], true };
                };
                const resolved_pkg_id = resolved_res[0];
                const auto_installed = resolved_res[1];

                if (resolved_pkg_id == invalid_package_id) {
                    // these are optional peers that failed to find any dependency with a matching
                    // name. they are completely excluded
                    continue;
                }

                for (visited_parent_id of visited_parent_node_ids.items) {
                    const ctx: Store.Node.TransitivePeer.OrderedArraySetCtx = {
                        string_buf: string_buf,
                        pkg_names: pkg_names,
                    };
                    const peer: Store.Node.TransitivePeer = {
                        dep_id: peer_dep_id,
                        pkg_id: resolved_pkg_id,
                        auto_installed: auto_installed,
                    };
                    node_peers[visited_parent_id.get()].insert(lockfile.allocator, peer, &ctx)?;
                }

                if (visited_parent_node_ids.items.len != 0) {
                    // visited parents length == 0 means the node satisfied it's own
                    // peer. don't queue.
                    node_dependencies[node_id.get()].appendAssumeCapacity({ dep_id: peer_dep_id, pkg_id: resolved_pkg_id });
                    node_queue.append(lockfile.allocator, {
                        parent_id: node_id,
                        dep_id: peer_dep_id,
                        pkg_id: resolved_pkg_id,
                    })?;
                }
            }

            // node_queue is a stack: reverse children so the first one pushed is the
            // first popped, matching BFS sibling order.
            std.mem.reverse(QueuedNode, node_queue.items[queue_mark..]);
        }

        if (manager.options.log_level.isVerbose()) {
            const full_tree_end = timer.read();
            timer.reset();
            Output.prettyErrorln("Resolved peers [{f}]", .{bun.fmt.fmtDurationOneDecimal(full_tree_end)});
        }

        const DedupeInfo = DedupeInfo_;

        let dedupe: std.AutoHashMapUnmanaged(PackageID, std.ArrayListUnmanaged(DedupeInfo)) = .empty;
        const _dedupe_guard = DedupeMapGuard_{ map: &dedupe, allocator: lockfile.allocator };

        let res_fmt_buf: std.array_list.Managed(u8) = .init(lockfile.allocator);
        const _res_fmt_buf_guard = ManagedU8ListGuard_{ list: &res_fmt_buf };

        const nodes_slice = nodes.slice();
        const node_pkg_ids = nodes_slice.items(.pkg_id);
        const node_dep_ids = nodes_slice.items(.dep_id);
        const node_peers: []const Store.Node.Peers = nodes_slice.items(.peers);
        const node_nodes = nodes_slice.items(.nodes);

        let store: Store.Entry.List = .empty;

        const QueuedEntry = QueuedEntry_;
        let entry_queue: bun.LinearFifo(QueuedEntry, .Dynamic) = .init(lockfile.allocator);
        const _entry_queue_guard = EntryQueueGuard_{ fifo: &entry_queue };

        entry_queue.writeItem({
            node_id: Store.Node.Id.@"from"(0),
            entry_parent_id: .invalid,
        })?;

        let public_hoisted: bun.StringArrayHashMap(void) = .init(manager.allocator);
        const _public_hoisted_guard = StringSetGuard_{ set: &public_hoisted };

        let hidden_hoisted: bun.StringArrayHashMap(void) = .init(manager.allocator);
        const _hidden_hoisted_guard = StringSetGuard_{ set: &hidden_hoisted };

        // Second pass: Deduplicate nodes when the pkg_id and peer set match an existing entry.
        next_entry: while (entry := entry_queue.readItem()) {
            const pkg_id = node_pkg_ids[entry.node_id.get()];

            const dedupe_entry = dedupe.getOrPut(lockfile.allocator, pkg_id)?;
            if (!dedupe_entry.found_existing) {
                dedupe_entry.value_ptr.* = {};
            } else {
                const curr_peers = node_peers[entry.node_id.get()];
                const curr_dep_id = node_dep_ids[entry.node_id.get()];

                for (info of dedupe_entry.value_ptr.items) {
                    if (info.dep_id == invalid_dependency_id || curr_dep_id == invalid_dependency_id) {
                        if (info.dep_id != curr_dep_id) {
                            continue;
                        }
                    }
                    if (info.dep_id != invalid_dependency_id && curr_dep_id != invalid_dependency_id) {
                        const curr_dep = dependencies[curr_dep_id];
                        const existing_dep = dependencies[info.dep_id];

                        if (existing_dep.version.tag == .workspace && curr_dep.version.tag == .workspace) {
                            if (existing_dep.behavior.isWorkspace() != curr_dep.behavior.isWorkspace()) {
                                continue;
                            }
                        }
                    }

                    const eql_ctx: Store.Node.TransitivePeer.OrderedArraySetCtx = {
                        string_buf: string_buf,
                        pkg_names: pkg_names,
                    };

                    if (info.peers.eql(&curr_peers, &eql_ctx)) {
                        // dedupe! depend on the already created entry

                        const entries = store.slice();
                        const entry_dependencies = entries.items(.dependencies);
                        const entry_parents = entries.items(.parents);

                        let parents = &entry_parents[info.entry_id.get()];

                        if (curr_dep_id != invalid_dependency_id && dependencies[curr_dep_id].behavior.isWorkspace()) {
                            parents.append(lockfile.allocator, entry.entry_parent_id)?;
                            continue :next_entry;
                        }
                        const ctx: Store.Entry.DependenciesOrderedArraySetCtx = {
                            string_buf: string_buf,
                            dependencies: dependencies,
                        };
                        entry_dependencies[entry.entry_parent_id.get()].insert(
                            lockfile.allocator,
                            { entry_id: info.entry_id, dep_id: curr_dep_id },
                            &ctx,
                        )?;
                        parents.append(lockfile.allocator, entry.entry_parent_id)?;
                        continue :next_entry;
                    }
                }

                // nothing matched - create a new entry
            }

            const new_entry_peer_hash: Store.Entry.PeerHash = peer_hash: {
                const peers = node_peers[entry.node_id.get()];
                if (peers.len() == 0) {
                    break :peer_hash .none;
                }
                let hasher = bun.Wyhash11.init(0);
                for (peer_ids of peers.slice()) {
                    const pkg_name = pkg_names[peer_ids.pkg_id];
                    hasher.update(pkg_name.slice(string_buf));
                    const pkg_res = pkg_resolutions[peer_ids.pkg_id];
                    res_fmt_buf.clearRetainingCapacity();
                    res_fmt_buf.writer().print("{f}", .{pkg_res.fmt(string_buf, .posix)})?;
                    hasher.update(res_fmt_buf.items);
                }
                break :peer_hash Store.Entry.PeerHash.@"from"(hasher.final());
            };

            const new_entry_dep_id = node_dep_ids[entry.node_id.get()];

            const new_entry_is_root = new_entry_dep_id == invalid_dependency_id;
            const new_entry_is_workspace = !new_entry_is_root && dependencies[new_entry_dep_id].version.tag == .workspace;

            let new_entry_dependencies: Store.Entry.Dependencies = if (dedupe_entry.found_existing && new_entry_is_workspace)
                .empty
            else
                .initCapacity(lockfile.allocator, node_nodes[entry.node_id.get()].items.len)?;
            let dependencies_owned = true;
            errdefer if (dependencies_owned) new_entry_dependencies.deinit(lockfile.allocator);

            let new_entry_parents: std.ArrayListUnmanaged(Store.Entry.Id) = .initCapacity(lockfile.allocator, 1)?;
            let parents_owned = true;
            errdefer if (parents_owned) new_entry_parents.deinit(lockfile.allocator);
            new_entry_parents.appendAssumeCapacity(entry.entry_parent_id);

            const hoisted = hoisted: {
                if (new_entry_dep_id == invalid_dependency_id) {
                    break :hoisted false;
                }

                const dep_name = dependencies[new_entry_dep_id].name.slice(string_buf);

                if (hoist_pattern := manager.options.hoist_pattern) {
                    if (hoist_pattern.isMatch(dep_name)) {
                        const hoist_entry = hidden_hoisted.getOrPut(dep_name)?;
                        break :hoisted !hoist_entry.found_existing;
                    }
                    break :hoisted false;
                } else {
                    const hoist_entry = hidden_hoisted.getOrPut(dep_name)?;
                    break :hoisted !hoist_entry.found_existing;
                }
            };

            const new_entry: Store.Entry = {
                node_id: entry.node_id,
                dependencies: new_entry_dependencies,
                parents: new_entry_parents,
                peer_hash: new_entry_peer_hash,
                hoisted: hoisted,
            };

            const new_entry_id = Store.Entry.Id.@"from"(@as(u32, @intCast(store.len)));
            store.append(lockfile.allocator, new_entry)?;
            dependencies_owned = false;
            parents_owned = false;

            if (entry_parent_id := entry.entry_parent_id.tryGet()) skip_adding_dependency: {
                if (new_entry_dep_id != invalid_dependency_id && dependencies[new_entry_dep_id].behavior.isWorkspace()) {
                    // skip implicit workspace dependencies on the root.
                    break :skip_adding_dependency;
                }

                const entries = store.slice();
                const entry_dependencies = entries.items(.dependencies);
                const ctx: Store.Entry.DependenciesOrderedArraySetCtx = {
                    string_buf: string_buf,
                    dependencies: dependencies,
                };
                entry_dependencies[entry_parent_id].insert(
                    lockfile.allocator,
                    { entry_id: new_entry_id, dep_id: new_entry_dep_id },
                    &ctx,
                )?;

                if (new_entry_dep_id != invalid_dependency_id) {
                    if (entry.entry_parent_id == .root) {
                        // make sure direct dependencies are not replaced
                        const dep_name = dependencies[new_entry_dep_id].name.slice(string_buf);
                        const void_val: void = undefined;
                        public_hoisted.put(dep_name, void_val)?;
                    } else {
                        // transitive dependencies (also direct dependencies of workspaces!)
                        const dep_name = dependencies[new_entry_dep_id].name.slice(string_buf);
                        if (public_hoist_pattern := manager.options.public_hoist_pattern) {
                            if (public_hoist_pattern.isMatch(dep_name)) {
                                const hoist_entry = public_hoisted.getOrPut(dep_name)?;
                                if (!hoist_entry.found_existing) {
                                    entry_dependencies[0].insert(
                                        lockfile.allocator,
                                        { entry_id: new_entry_id, dep_id: new_entry_dep_id },
                                        &ctx,
                                    )?;
                                }
                            }
                        }
                    }
                }
            }

            dedupe_entry.value_ptr.append(lockfile.allocator, {
                entry_id: new_entry_id,
                dep_id: new_entry_dep_id,
                peers: node_peers[entry.node_id.get()],
            })?;

            for (node_id of node_nodes[entry.node_id.get()].items) {
                entry_queue.writeItem({
                    node_id: node_id,
                    entry_parent_id: new_entry_id,
                })?;
            }
        }

        if (!silent && manager.options.log_level.isVerbose()) {
            const dedupe_end = timer.read();
            Output.prettyErrorln("Created store [{f}]", .{bun.fmt.fmtDurationOneDecimal(dedupe_end)});
        }

        return Store{
            entries: store,
            nodes: nodes,
        };
}

/// Runs on main thread
pub fn installIsolatedPackages(
    manager: *PackageManager,
    command_ctx: Command.Context,
    install_root_dependencies: bool,
    workspace_filters: []const WorkspaceFilter,
    packages_to_install: ([]const PackageID)?,
): OOM!PackageInstall.Summary {
    bun.analytics.Features.isolated_bun_install += 1;

    const lockfile = manager.lockfile;

    const store: Store = buildStore(
        manager,
        lockfile,
        install_root_dependencies,
        workspace_filters,
        packages_to_install,
        false,
    )?;

    // Compute entry_hash for the global virtual store. The hash makes a

    // Compute entry_hash for the global virtual store. The hash makes a
    // global-store directory name unique to this entry's *resolved* dependency
    // closure, so two projects that resolve `react@18.3.1` to the same set of
    // transitive versions share one on-disk entry, while a project that
    // resolves a transitive dep to a different version gets its own.
    //
    // Eligibility propagates: an entry is only global-store-eligible (hash != 0)
    // when the package itself comes from an immutable cache (npm/git/tarball,
    // unpatched, no lifecycle scripts) *and* every dependency it links to is
    // also eligible. The second condition matters because dep symlinks live
    // inside the global entry; baking a project-local path (workspace, folder)
    // into a shared directory would break for every other consumer.
    const WyhashWriter = WyhashWriter_;

    const global_store_path: [:0]const u8? = if (manager.options.enable.global_virtual_store) global_store_path: {
        const entries = store.entries.slice();
        let entry_hashes = entries.items(.entry_hash);
        const entry_node_ids = entries.items(.node_id);
        const entry_dependencies = entries.items(.dependencies);

        const node_pkg_ids = store.nodes.items(.pkg_id);
        const node_dep_ids = store.nodes.items(.dep_id);

        const pkgs = lockfile.packages.slice();
        const pkg_names = pkgs.items(.name);
        const pkg_name_hashes = pkgs.items(.name_hash);
        const pkg_resolutions = pkgs.items(.resolution);
        const pkg_metas = pkgs.items(.@"meta");

        const string_buf = lockfile.buffers.string_bytes.items;
        const dependencies = lockfile.buffers.dependencies.items;

        // Packages newly trusted via `bun add --trust` (not yet written to the
        // lockfile) will have their lifecycle scripts run this install; treat
        // them the same as lockfile-trusted packages for eligibility.
        let trusted_from_update = manager.findTrustedDependenciesFromUpdateRequests();
        const _trusted_guard = TrustedUpdateGuard_{ map: &trusted_from_update, allocator: manager.allocator };

        const State = InstallState_;
        let states = manager.allocator.alloc(State, store.entries.len)?;
        const _states_guard = InstallStateSliceGuard_{ slice: states, allocator: manager.allocator };
        @memset(states, .unvisited);

        // Iterative DFS so dependency cycles (which the isolated graph permits)
        // can't overflow the stack and are handled deterministically: a back-edge
        // contributes the dependency *name* to the parent's hash but not the
        // child's own hash (still being computed). Two entries that only differ
        // by which side of a cycle they sit on still get distinct hashes via
        // their own store-path bytes.
        let stack: std.ArrayListUnmanaged(HashDfsFrame_) = .empty;
        const _stack_guard = HashDfsStackGuard_{ list: &stack, allocator: manager.allocator };

        for (_root_id of 0..store.entries.len) {
            if (states[_root_id] != .unvisited) continue;
            stack.append(manager.allocator, { id: Store.Entry.Id.@"from"(@as(u32, @intCast(_root_id))), dep_idx: 0, hasher: undefined })?;

            while (stack.items.len > 0) {
                const top = &stack.items[stack.items.len - 1];
                const id = top.id;
                const idx = id.get();

                if (states[idx] == .unvisited) {
                    states[idx] = .in_progress;

                    const node_id = entry_node_ids[idx];
                    const pkg_id = node_pkg_ids[node_id.get()];
                    const dep_id = node_dep_ids[node_id.get()];
                    const pkg_res = pkg_resolutions[pkg_id];

                    const eligible = switch (pkg_res.tag) {
                        .npm, .git, .github, .local_tarball, .remote_tarball => eligible: {
                            // Patched packages and packages with lifecycle scripts
                            // mutate (or may mutate) their install directory, so a
                            // shared global copy would either diverge from the
                            // patch or be mutated underneath other projects.
                            if (lockfile.patched_dependencies.count() > 0) {
                                let name_version_buf: bun.PathBuffer = undefined;
                                const nv_opt = std.fmt.bufPrint(&name_version_buf, "{s}@{f}", .{
                                    pkg_names[pkg_id].slice(string_buf),
                                    pkg_res.fmt(string_buf, .posix),
                                }) catch null;
                                if (nv_opt == null) {
                                    break :eligible false;
                                }
                                const name_version = nv_opt.?;
                                if (lockfile.patched_dependencies.contains(bun.Semver.String.Builder.stringHash(name_version))) {
                                    break :eligible false;
                                }
                            }
                            // `run_preinstall()` authorizes scripts by the
                            // dependency *alias* name, so an aliased install
                            // like `foo: npm:bar@1` is trusted if `foo` is in
                            // trustedDependencies even though the package name
                            // is `bar`. Mirror that here so the alias case
                            // can't slip past the eligibility check.
                            //
                            // Intentionally *not* gated on `do.run_scripts`
                            // (a later install without `--ignore-scripts`
                            // would run the postinstall through the project
                            // symlink and mutate the shared directory) *or*
                            // on `meta.hasInstallScript()` (that flag is not
                            // serialised in `bun.lock`, so it reads `false`
                            // on every install after the first; a trusted
                            // scripted package would flip from project-local
                            // on the cold install to global on the warm one).
                            // Over-excludes the rare "trusted but actually no
                            // scripts" case in exchange for not needing a
                            // lockfile-format change.
                            const dep_name = if (dep_id != invalid_dependency_id)
                                dependencies[dep_id].name.slice(string_buf)
                            else
                                pkg_names[pkg_id].slice(string_buf);
                            const dep_name_hash = if (dep_id != invalid_dependency_id)
                                dependencies[dep_id].name_hash
                            else
                                pkg_name_hashes[pkg_id];
                            if (lockfile.hasTrustedDependency(dep_name, &pkg_res) ||
                                trusted_from_update.contains(@truncate(dep_name_hash)))
                            {
                                break :eligible false;
                            }
                            break :eligible true;
                        },
                        else => false,
                    };

                    if (!eligible) {
                        states[idx] = .ineligible;
                        entry_hashes[idx] = 0;
                        * = stack.pop();
                        continue;
                    }

                    // Seed the hash with this entry's own store-path string so
                    // entries with identical dep sets but different package
                    // versions never collide. Hashed through a writer so an
                    // unusually long store path (long scope + git URL + peer
                    // hash) can't overflow a fixed buffer and feed
                    // uninitialized stack bytes into the hash.
                    top.hasher = .init((@as(u64, 0x9E3779B9) << 32) | 0x7F4A7C15);
                    {
                        let hw: WyhashWriter = { hasher: &top.hasher };
                        let w = hw.writer();
                        w.print("{f}", .{Store.Entry.fmtStorePath(id, &store, lockfile)}) catch unreachable;
                    }
                    // The store path for `.npm` is just `name@version`, which
                    // is *not* unique across registries (an enterprise proxy
                    // can serve a patched `foo@1.0.0`). Fold in the tarball
                    // integrity so a cross-registry / cross-tarball collision
                    // gets a different global directory instead of reusing the
                    // first project's bytes.
                    top.hasher.update(std.mem.asBytes(&pkg_metas[pkg_id].integrity));
                }

                if (states[idx] == .ineligible) {
                    * = stack.pop();
                    continue;
                }

                const deps = entry_dependencies[idx].slice();
                let advanced = false;
                while (top.dep_idx < deps.len) : (top.dep_idx += 1) {
                    const dep = deps[top.dep_idx];
                    const dep_idx = dep.entry_id.get();
                    const dep_name_hash = dependencies[dep.dep_id].name_hash;
                    switch (states[dep_idx]) {
                        .done => {
                            top.hasher.update(std.mem.asBytes(&dep_name_hash));
                            top.hasher.update(std.mem.asBytes(&entry_hashes[dep_idx]));
                        },
                        .ineligible => {
                            // A dep that can't live in the global store poisons
                            // this entry too: its symlink would point at a
                            // project-local path.
                            states[idx] = .ineligible;
                            entry_hashes[idx] = 0;
                        },
                        .in_progress => {
                            // Cycle back-edge: the dep's hash isn't known yet.
                            // Fold a placeholder; the SCC pass below replaces
                            // every cycle member's hash with one that's
                            // independent of which edge happened to be the
                            // back-edge in this DFS.
                            top.hasher.update(std.mem.asBytes(&dep_name_hash));
                        },
                        .unvisited => {
                            stack.append(manager.allocator, { id: dep.entry_id, dep_idx: 0, hasher: undefined })?;
                            advanced = true;
                            // re-fetch `top` after potential realloc
                            break;
                        },
                    }
                    if (states[idx] == .ineligible) break;
                }

                if (advanced) continue;

                if (states[idx] != .ineligible) {
                    let h = top.hasher.final();
                    // 0 is the "not eligible" sentinel.
                    if (h == 0) h = 1;
                    entry_hashes[idx] = h;
                    states[idx] = .done;
                }
                * = stack.pop();
            }
        }

        // SCC pass: the DFS hash above is visit-order-dependent for cycle
        // members (which edge becomes the back-edge depends on which member
        // the outer loop reached first, which depends on entry IDs, which
        // depend on the *whole project's* dependency set). That's harmless
        // for correctness — different orderings just give different keys —
        // but it means a package that's part of an npm cycle never shares a
        // global entry across projects, defeating the feature for chunks of
        // the ecosystem (`es-abstract`↔`object.assign`, the babel core
        // cycle, etc.).
        //
        // Tarjan's algorithm groups entries into strongly-connected
        // components. For singleton SCCs the pass-1 hash is already
        // visit-order-independent and is left alone. For multi-member SCCs
        // every member gets the same hash, computed from the sorted member
        // store-paths plus the sorted external-dep hashes — inputs that are
        // identical regardless of which member the project happened to list
        // first. The dep symlinks inside the SCC then point at siblings with
        // the same hash suffix, so they resolve in any project that produces
        // the same SCC closure.
        {
            const n: u32 = @intCast(store.entries.len);
            let tarjan_index = manager.allocator.alloc(u32, n)?;
            const _tarjan_index_guard = U32SliceGuard_{ slice: tarjan_index, allocator: manager.allocator };
            @memset(tarjan_index, std.math.maxInt(u32));
            let lowlink = manager.allocator.alloc(u32, n)?;
            const _lowlink_guard = U32SliceGuard_{ slice: lowlink, allocator: manager.allocator };
            let on_stack = manager.allocator.alloc(bool, n)?;
            const _on_stack_guard = BoolSliceGuard_{ slice: on_stack, allocator: manager.allocator };
            @memset(on_stack, false);

            let scc_stack: std.ArrayListUnmanaged(u32) = .empty;
            const _scc_stack_guard = U32ListGuard_{ list: &scc_stack, allocator: manager.allocator };
            let work: std.ArrayListUnmanaged(TarjanWorkFrame_) = .empty;
            const _work_guard = TarjanWorkGuard_{ list: &work, allocator: manager.allocator };
            let scc_ext: std.AutoArrayHashMapUnmanaged(u64, void) = .empty;
            const _scc_ext_guard = U64SetGuard_{ map: &scc_ext, allocator: manager.allocator };

            let index_counter: u32 = 0;
            for (root of 0..n) {
                if (tarjan_index[root] != std.math.maxInt(u32)) continue;
                work.append(manager.allocator, { v: @as(u32, @intCast(root)), child: 0 })?;
                while (work.items.len > 0) {
                    const frame = &work.items[work.items.len - 1];
                    const v = frame.v;
                    if (frame.child == 0) {
                        tarjan_index[v] = index_counter;
                        lowlink[v] = index_counter;
                        index_counter += 1;
                        scc_stack.append(manager.allocator, v)?;
                        on_stack[v] = true;
                    }
                    const deps = entry_dependencies[v].slice();
                    let recursed = false;
                    while (frame.child < deps.len) : (frame.child += 1) {
                        const w = deps[frame.child].entry_id.get();
                        if (tarjan_index[w] == std.math.maxInt(u32)) {
                            frame.child += 1;
                            work.append(manager.allocator, { v: w, child: 0 })?;
                            recursed = true;
                            break;
                        } else if (on_stack[w]) {
                            lowlink[v] = @min(lowlink[v], tarjan_index[w]);
                        }
                    }
                    if (recursed) continue;
                    if (lowlink[v] == tarjan_index[v]) {
                        let start: usize = 0;
                        let start_found = false;
                        let i = scc_stack.items.len;
                        while (i > 0) : (i -= 1) {
                            if (scc_stack.items[i - 1] == v) {
                                start = i - 1;
                                start_found = true;
                                break;
                            }
                        }
                        if (!start_found) unreachable;
                        const members = scc_stack.items[start..];
                        for (m of members) on_stack[m] = false;
                        if (members.len == 1) {
                            // Singleton SCC. Tarjan emits SCCs in reverse
                            // topological order, so every dep's hash is final
                            // by now (including any cycle-member deps that
                            // just got their SCC hash). Recompute this entry's
                            // hash from those final values so a dependent of
                            // a cycle picks up the order-independent SCC hash
                            // rather than the pass-1 placeholder.
                            const m = members[0];
                            if (entry_hashes[m] != 0) {
                                let sub: std.hash.Wyhash = .init((@as(u64, 0x9E3779B9) << 32) | 0x7F4A7C15);
                                let hw: WyhashWriter = { hasher: &sub };
                                let w_ = hw.writer();
                                w_.print("{f}", .{Store.Entry.fmtStorePath(Store.Entry.Id.@"from"(@as(u32, @intCast(m))), &store, lockfile)}) catch unreachable;
                                sub.update(std.mem.asBytes(&pkg_metas[node_pkg_ids[entry_node_ids[m].get()]].integrity));
                                let poisoned = false;
                                for (dep of entry_dependencies[m].slice()) {
                                    const dh = entry_hashes[dep.entry_id.get()];
                                    if (dh == 0) {
                                        poisoned = true;
                                        break;
                                    }
                                    const dep_name_hash = dependencies[dep.dep_id].name_hash;
                                    sub.update(std.mem.asBytes(&dep_name_hash));
                                    sub.update(std.mem.asBytes(&dh));
                                }
                                if (poisoned) {
                                    entry_hashes[m] = 0;
                                } else {
                                    let h = sub.final();
                                    if (h == 0) h = 1;
                                    entry_hashes[m] = h;
                                }
                            }
                        } else if (members.len > 1) {
                            // One order-independent hash for the whole SCC:
                            // collect a sub-hash per member (store path +
                            // integrity), collect every external-dep hash,
                            // sort both lists, then hash the concatenation.
                            // Sorting by *content* (not entry index) is what
                            // makes this stable across projects.
                            scc_ext.clearRetainingCapacity();
                            let member_sub: std.ArrayListUnmanaged(u64) = .empty;
                            const _member_sub_guard = U64ListGuard_{ list: &member_sub, allocator: manager.allocator };
                            let any_ineligible = false;
                            for (m of members) {
                                if (entry_hashes[m] == 0) any_ineligible = true;
                                let sub: std.hash.Wyhash = .init(0);
                                let hw: WyhashWriter = { hasher: &sub };
                                let w_ = hw.writer();
                                w_.print("{f}", .{Store.Entry.fmtStorePath(Store.Entry.Id.@"from"(@as(u32, @intCast(m))), &store, lockfile)}) catch unreachable;
                                sub.update(std.mem.asBytes(&pkg_metas[node_pkg_ids[entry_node_ids[m].get()]].integrity));
                                member_sub.append(manager.allocator, sub.final())?;
                                for (dep of entry_dependencies[m].slice()) {
                                    const di = dep.entry_id.get();
                                    // Skip intra-SCC edges; those are captured
                                    // by member_sub.
                                    if (std.mem.indexOfScalar(u32, members, di) != null) continue;
                                    if (entry_hashes[di] == 0) any_ineligible = true;
                                    // Dep symlinks inside the entry are named
                                    // by the dependency *alias*, so two SCCs
                                    // that reach the same external entry under
                                    // different aliases must hash differently.
                                    let ext: std.hash.Wyhash = .init(0);
                                    ext.update(std.mem.asBytes(&dependencies[dep.dep_id].name_hash));
                                    ext.update(std.mem.asBytes(&entry_hashes[di]));
                                    const void_val: void = undefined;
                                    scc_ext.put(manager.allocator, ext.final(), void_val)?;
                                }
                            }
                            const empty_sort_ctx: void = undefined;
                            std.mem.sort(u64, member_sub.items, empty_sort_ctx, std.sort.asc(u64));
                            const ext_keys = scc_ext.keys();
                            std.mem.sort(u64, ext_keys, empty_sort_ctx, std.sort.asc(u64));
                            let hasher: std.hash.Wyhash = .init(0x42A7C15F9E3779B9);
                            for (k of member_sub.items) hasher.update(std.mem.asBytes(&k));
                            for (k of ext_keys) hasher.update(std.mem.asBytes(&k));
                            let h = hasher.final();
                            if (h == 0) h = 1;
                            const final_h: u64 = if (any_ineligible) @as(u64, 0) else h;
                            for (m of members) entry_hashes[m] = final_h;
                        }
                        scc_stack.items.len = start;
                    }
                    * = work.pop();
                    if (work.items.len > 0) {
                        const parent = &work.items[work.items.len - 1];
                        lowlink[parent.v] = @min(lowlink[parent.v], lowlink[v]);
                    }
                }
            }
        }

        // Ineligibility can surface mid-cycle: A→B→A where B turns out to
        // depend on a workspace package. The DFS above already finalised A's
        // hash via the `.in_progress` back-edge before B was marked
        // ineligible, so A would wrongly land in the global store with a
        // dangling dep symlink. Close the gap with a fixed-point pass: any
        // entry that still links to an ineligible dep becomes ineligible too.
        let changed = true;
        while (changed) {
            changed = false;
            for (idx of 0..store.entries.len) {
                if (entry_hashes[idx] == 0) continue;
                for (dep of entry_dependencies[idx].slice()) {
                    if (entry_hashes[dep.entry_id.get()] == 0) {
                        entry_hashes[idx] = 0;
                        changed = true;
                        break;
                    }
                }
            }
        }

        // <cache_dir>/links — created lazily by the first task that misses.
        // getCacheDirectory() populates `cache_directory_path` as a side-effect.
        * = manager.getCacheDirectory();
        const cache_dir_path = manager.cache_directory_path;
        if (cache_dir_path.len == 0) break :global_store_path null;
        break :global_store_path manager.allocator.dupeZ(
            u8,
            bun.path.joinAbsString(cache_dir_path, &.{"links"}, .auto),
        )?;
    } else null;
    const _global_store_path_guard = OptCStrFreeGuard_{ ptr: global_store_path, allocator: manager.allocator };

    // setup node_modules/.bun
    const is_new_bun_modules = is_new_bun_modules: {
        const node_modules_path = bun.OSPathLiteral("node_modules");
        const bun_modules_path = bun.OSPathLiteral("node_modules/" ++ Store.modules_dir_name);

        if (sys.mkdirat(FD.cwd(), node_modules_path, 0o755).asErr() != null) {
            if (sys.mkdirat(FD.cwd(), bun_modules_path, 0o755).asErr() != null) {
                break :is_new_bun_modules false;
            }

            // 'node_modules' exists and 'node_modules/.bun' doesn't

            if (comptime Environment.isWindows) {
                let rename_path: bun.AutoRelPath = .init();
                const _rename_path_guard = AutoRelPathGuard_{ path: &rename_path };

                {
                    let mkdir_path = AutoRelPathU16.@"from"("node_modules");
                    const _mkdir_path_guard = AutoRelPathU16Guard_{ path: &mkdir_path };

                    mkdir_path.appendFmt(".old_modules-{s}", .{&std.fmt.bytesToHex(std.mem.asBytes(&bun.fastRandom()), .lower)});
                    rename_path.append(mkdir_path.slice());

                    if (sys.mkdirat(FD.cwd(), mkdir_path.sliceZ(), 0o755).asErr() != null) {
                        break :is_new_bun_modules true;
                    }
                }

                const node_modules_opt = bun.openDirForIteration(FD.cwd(), "node_modules").unwrap() catch null;
                if (node_modules_opt == null) {
                    break :is_new_bun_modules true;
                }
                const node_modules = node_modules_opt.?;

                let entry_path = bun.AutoRelPath.@"from"("node_modules");
                const _entry_path_guard = AutoRelPathGuard_{ path: &entry_path };

                // 2
                let node_modules_iter = bun.DirIterator.iterate(node_modules, .@"u8");
                while (true) {
                    const next_opt = node_modules_iter.next().unwrap() catch null;
                    if (next_opt == null) break :is_new_bun_modules true;
                    const entry_opt = next_opt.?;
                    if (entry_opt == null) break;
                    const entry = entry_opt.?;
                    if (bun.strings.startsWithChar(entry.name.slice(), '.')) {
                        continue;
                    }

                    const _entry_path_save_guard = AutoRelPathResetGuard_{ scope: entry_path.save() };

                    entry_path.append(entry.name.slice());

                    const _rename_path_save_guard = AutoRelPathResetGuard_{ scope: rename_path.save() };

                    rename_path.append(entry.name.slice());

                    sys.renameat(FD.cwd(), entry_path.sliceZ(), FD.cwd(), rename_path.sliceZ()).unwrap() catch {};
                }

                // 3
                for (workspace_path of lockfile.workspace_paths.values()) {
                    let workspace_node_modules = bun.AutoRelPath.@"from"(workspace_path.slice(lockfile.buffers.string_bytes.items));
                    const _workspace_nm_guard = AutoRelPathGuard_{ path: &workspace_node_modules };

                    const basename = workspace_node_modules.basename();

                    workspace_node_modules.append("node_modules");

                    const _rename_path_save_guard = AutoRelPathResetGuard_{ scope: rename_path.save() };

                    rename_path.appendFmt(".old_{s}_modules", .{basename});

                    sys.renameat(FD.cwd(), workspace_node_modules.sliceZ(), FD.cwd(), rename_path.sliceZ()).unwrap() catch {};
                }
            } else {
                let temp_node_modules_buf: bun.PathBuffer = undefined;
                const temp_node_modules = bun.fs.FileSystem.tmpname("tmp_modules", &temp_node_modules_buf, bun.fastRandom()) catch unreachable;

                // 1
                if (sys.renameat(FD.cwd(), "node_modules", FD.cwd(), temp_node_modules).asErr() != null) {
                    break :is_new_bun_modules true;
                }

                // 2
                sys.mkdirat(FD.cwd(), node_modules_path, 0o755).unwrap() catch e => {
                    Output.err(e, "failed to create './node_modules'", {});
                    Global.exit(1);
                };

                sys.mkdirat(FD.cwd(), bun_modules_path, 0o755).unwrap() catch e => {
                    Output.err(e, "failed to create './node_modules/.bun'", {});
                    Global.exit(1);
                };

                let rename_path = bun.AutoRelPath.@"from"("node_modules");
                const _rename_path_guard = AutoRelPathGuard_{ path: &rename_path };

                rename_path.appendFmt(".old_modules-{s}", .{&std.fmt.bytesToHex(std.mem.asBytes(&bun.fastRandom()), .lower)});

                // 3
                if (sys.renameat(FD.cwd(), temp_node_modules, FD.cwd(), rename_path.sliceZ()).asErr() != null) {
                    break :is_new_bun_modules true;
                }

                rename_path.append(".cache");

                let cache_path = bun.AutoRelPath.@"from"("node_modules");
                const _cache_path_guard = AutoRelPathGuard_{ path: &cache_path };

                cache_path.append(".cache");

                // 4
                sys.renameat(FD.cwd(), rename_path.sliceZ(), FD.cwd(), cache_path.sliceZ()).unwrap() catch {};

                // remove .cache so we can append destination for each workspace
                rename_path.undo(1);

                // 5
                for (workspace_path of lockfile.workspace_paths.values()) {
                    let workspace_node_modules = bun.AutoRelPath.@"from"(workspace_path.slice(lockfile.buffers.string_bytes.items));
                    const _workspace_nm_guard = AutoRelPathGuard_{ path: &workspace_node_modules };

                    const basename = workspace_node_modules.basename();

                    workspace_node_modules.append("node_modules");

                    const _rename_path_save_guard = AutoRelPathResetGuard_{ scope: rename_path.save() };

                    rename_path.appendFmt(".old_{s}_modules", .{basename});

                    sys.renameat(FD.cwd(), workspace_node_modules.sliceZ(), FD.cwd(), rename_path.sliceZ()).unwrap() catch {};
                }
            }

            break :is_new_bun_modules true;
        }

        sys.mkdirat(FD.cwd(), bun_modules_path, 0o755).unwrap() catch e => {
            Output.err(e, "failed to create './node_modules/.bun'", {});
            Global.exit(1);
        };

        break :is_new_bun_modules true;
    };

    {
        let root_node: *Progress.Node = undefined;
        let download_node: Progress.Node = undefined;
        let install_node: Progress.Node = undefined;
        let scripts_node: Progress.Node = undefined;
        let progress = &manager.progress;

        if (manager.options.log_level.showProgress()) {
            progress.supports_ansi_escape_codes = Output.enable_ansi_colors_stderr;
            root_node = progress.start("", 0);
            download_node = root_node.start(ProgressStrings.download(), 0);
            install_node = root_node.start(ProgressStrings.install(), store.entries.len);
            scripts_node = root_node.start(ProgressStrings.script(), 0);

            manager.downloads_node = null;
            manager.scripts_node = &scripts_node;
            manager.downloads_node = &download_node;
        }

        const nodes_slice = store.nodes.slice();
        const node_pkg_ids = nodes_slice.items(.pkg_id);
        const node_dep_ids = nodes_slice.items(.dep_id);

        const entries = store.entries.slice();
        const entry_node_ids = entries.items(.node_id);
        const entry_steps = entries.items(.step);
        const entry_dependencies = entries.items(.dependencies);
        const entry_hoisted = entries.items(.hoisted);

        const string_buf = lockfile.buffers.string_bytes.items;

        const pkgs = lockfile.packages.slice();
        const pkg_names = pkgs.items(.name);
        const pkg_name_hashes = pkgs.items(.name_hash);
        const pkg_resolutions = pkgs.items(.resolution);

        let seen_entry_ids: std.AutoHashMapUnmanaged(Store.Entry.Id, void) = .empty;
        const _seen_entry_ids_guard = SeenEntryIdsGuard_{ map: &seen_entry_ids, allocator: lockfile.allocator };
        const seen_cap: u32 = @intCast(store.entries.len);
        seen_entry_ids.ensureTotalCapacity(lockfile.allocator, seen_cap)?;

        // TODO: delete
        let seen_workspace_ids: std.AutoHashMapUnmanaged(PackageID, void) = .empty;
        const _seen_ws_ids_guard = SeenWorkspaceIdsGuard_{ map: &seen_workspace_ids, allocator: lockfile.allocator };

        let tasks = manager.allocator.alloc(Store.Installer.Task, store.entries.len)?;
        const _tasks_guard = TaskSliceGuard_{ slice: tasks, allocator: manager.allocator };

        let installer: Store.Installer = {
            lockfile: lockfile,
            manager: manager,
            command_ctx: command_ctx,
            installed: bun.bit_set.DynamicBitSetUnmanaged.initEmpty(manager.allocator, lockfile.packages.len)?,
            install_node: if (manager.options.log_level.showProgress()) &install_node else null,
            scripts_node: if (manager.options.log_level.showProgress()) &scripts_node else null,
            store: &store,
            tasks: tasks,
            trusted_dependencies_mutex: {},
            trusted_dependencies_from_update_requests: manager.findTrustedDependenciesFromUpdateRequests(),
            supported_backend: .init(PackageInstall.supported_method),
            is_new_bun_modules: is_new_bun_modules,
            global_store_path: global_store_path,
            global_store_tmp_suffix: bun.fastRandom(),
        };
        const _installer_guard = InstallerGuard_{ installer: &installer };

        let task_entry_idx: usize = 0;
        while (task_entry_idx < tasks.len) : (task_entry_idx += 1) {
            const entry_id = Store.Entry.Id.@"from"(@as(u32, @intCast(task_entry_idx)));
            tasks[task_entry_idx] = {
                entry_id: entry_id,
                installer: &installer,
                result: .none,

                task: { callback: &Store.Installer.Task.callback },
                next: null,
            };
        }

        // add the pending task count upfront
        manager.incrementPendingTasks(@as(u32, @intCast(store.entries.len)));
        for (_entry_id of 0..store.entries.len) {
            const entry_id = Store.Entry.Id.@"from"(@as(u32, @intCast(_entry_id)));

            const node_id = entry_node_ids[entry_id.get()];
            const pkg_id = node_pkg_ids[node_id.get()];
            const dep_id = node_dep_ids[node_id.get()];

            const pkg_name = pkg_names[pkg_id];
            const pkg_name_hash = pkg_name_hashes[pkg_id];
            const pkg_res: Resolution = pkg_resolutions[pkg_id];

            switch (pkg_res.tag) {
                else => {
                    // this is `uninitialized` or `single_file_module`.
                    bun.debugAssert(false);
                    // .monotonic is okay because the task isn't running on another thread.
                    entry_steps[entry_id.get()].store(.done, .monotonic);
                    installer.onTaskComplete(entry_id, .skipped);
                    continue;
                },
                .root => {
                    if (dep_id == invalid_dependency_id) {
                        // .monotonic is okay in this block because the task isn't running on another
                        // thread.
                        entry_steps[entry_id.get()].store(.symlink_dependencies, .monotonic);
                    } else {
                        // dep_id is valid meaning this was a dependency that resolved to the root
                        // package. it gets an entry in the store.
                    }
                    installer.startTask(entry_id);
                    continue;
                },
                .workspace => {
                    // .monotonic is okay in this block because the task isn't running on another
                    // thread.

                    // if injected=true this might be false
                    const ws_gop = seen_workspace_ids.getOrPut(lockfile.allocator, pkg_id)?;
                    if (!ws_gop.found_existing) {
                        entry_steps[entry_id.get()].store(.symlink_dependencies, .monotonic);
                        installer.startTask(entry_id);
                        continue;
                    }
                    entry_steps[entry_id.get()].store(.done, .monotonic);
                    installer.onTaskComplete(entry_id, .skipped);
                    continue;
                },
                .symlink => {
                    // no installation required, will only need to be linked to packages that depend on it.
                    bun.debugAssert(entry_dependencies[entry_id.get()].list.items.len == 0);
                    // .monotonic is okay because the task isn't running on another thread.
                    entry_steps[entry_id.get()].store(.done, .monotonic);
                    installer.onTaskComplete(entry_id, .skipped);
                    continue;
                },
                .folder => {
                    // folders are always hardlinked to keep them up-to-date
                    installer.startTask(entry_id);
                    continue;
                },

                inline .npm, .git, .github, .local_tarball, .remote_tarball => |pkg_res_tag| {
                    const patch_info = installer.packagePatchInfo(pkg_name, pkg_name_hash, &pkg_res)?;

                    const uses_global_store = installer.entryUsesGlobalStore(entry_id);

                    const needs_install =
                        manager.options.enable.force_install ||
                        // A freshly-created `node_modules/.bun` only implies the
                        // *project-local* entries are missing; global virtual-
                        // store entries persist across `rm -rf node_modules` and
                        // should still take the cheap symlink-only path.
                        (is_new_bun_modules && !uses_global_store) ||
                        patch_info == .remove ||
                        needs_install: {
                            let store_path: AutoAbsPath_ = .initTopLevelDir();
                            const _store_path_guard = AutoAbsPathGuard_{ path: &store_path };
                            if (uses_global_store) {
                                // Global entries are built under a per-process
                                // staging path and renamed into place as the
                                // final step, so the directory existing at its
                                // final path is the completeness signal.
                                installer.appendGlobalStoreEntryPath(&store_path, entry_id, .final);
                                break :needs_install !(sys.directoryExistsAt(FD.cwd(), store_path.sliceZ()).asValue() ?? false);
                            }
                            installer.appendRealStorePath(&store_path, entry_id, .final);
                            const scope_for_patch_tag_path = store_path.save();
                            if (pkg_res_tag == .npm)
                                // if it's from npm, it should always have a package.json.
                                // in other cases, probably yes but i'm less confident.
                                store_path.append("package.json");
                            const exists = sys.existsZ(store_path.sliceZ());

                            break :needs_install switch (patch_info) {
                                .none => !exists,
                                // checked above
                                .remove => unreachable,
                                .patch => |patch| {
                                    let hash_buf: install.BuntagHashBuf = undefined;
                                    const hash = install.buntaghashbuf_make(&hash_buf, patch.contents_hash);
                                    scope_for_patch_tag_path.restore();
                                    store_path.append(hash);
                                    break :needs_install !sys.existsZ(store_path.sliceZ());
                                },
                            };
                        } ||
                        (!uses_global_store && stale: {
                            let local: AutoPath = .initTopLevelDir();
                            const _local_guard = AutoPathGuard_{ path: &local };
                            installer.appendLocalStoreEntryPath(&local, entry_id);
                            if (comptime bun.Environment.isWindows) {
                                if (a := sys.getFileAttributes(local.sliceZ())) {
                                    break :stale a.is_reparse_point;
                                }
                                break :stale false;
                            }
                            if (st := sys.lstat(local.sliceZ()).asValue()) {
                                break :stale std.posix.S.ISLNK(@intCast(st.mode));
                            }
                            break :stale false;
                        });

                    if (!needs_install) {
                        if (uses_global_store) {
                            // Warm hit: the global virtual store already holds
                            // this entry's files, dep symlinks, and bin links.
                            // The only per-install work is the project-level
                            // `node_modules/.bun/<storepath>` → global symlink.
                            switch (installer.linkProjectToGlobalStore(entry_id)) {
                                .result => {},
                                .err => |e| {
                                    entry_steps[entry_id.get()].store(.done, .monotonic);
                                    installer.onTaskFail(entry_id, { symlink_dependencies: e });
                                    continue;
                                },
                            }
                        }
                        if (entry_hoisted[entry_id.get()]) {
                            installer.linkToHiddenNodeModules(entry_id);
                        }
                        // .monotonic is okay because the task isn't running on another thread.
                        entry_steps[entry_id.get()].store(.done, .monotonic);
                        installer.onTaskComplete(entry_id, .skipped);
                        continue;
                    }

                    let pkg_cache_dir_subpath = AutoRelPath.@"from"(switch (pkg_res_tag) {
                        .npm => manager.cachedNPMPackageFolderName(pkg_name.slice(string_buf), pkg_res.value.npm.version, patch_info.contentsHash()),
                        .git => manager.cachedGitFolderName(&pkg_res.value.git, patch_info.contentsHash()),
                        .github => manager.cachedGitHubFolderName(&pkg_res.value.github, patch_info.contentsHash()),
                        .local_tarball => manager.cachedTarballFolderName(pkg_res.value.local_tarball, patch_info.contentsHash()),
                        .remote_tarball => manager.cachedTarballFolderName(pkg_res.value.remote_tarball, patch_info.contentsHash()),

                        else => comptime unreachable,
                    });
                    const _pkg_cache_subpath_guard = AutoRelPathGuard_{ path: &pkg_cache_dir_subpath };

                    const cache_dir_res = manager.getCacheDirectoryAndAbsPath();
                    const cache_dir = cache_dir_res[0];
                    let cache_dir_path = cache_dir_res[1];
                    const _cache_dir_path_guard = AutoAbsPathGuard_{ path: &cache_dir_path };

                    const missing_from_cache = switch (manager.getPreinstallState(pkg_id)) {
                        .done => false,
                        else => missing_from_cache: {
                            if (patch_info == .none) {
                                const exists = switch (pkg_res_tag) {
                                    .npm => exists: {
                                        const _cache_dir_save_guard = AutoRelPathResetGuard_{ scope: pkg_cache_dir_subpath.save() };
                                        pkg_cache_dir_subpath.append("package.json");
                                        break :exists sys.existsAt(cache_dir, pkg_cache_dir_subpath.sliceZ());
                                    },
                                    else => sys.directoryExistsAt(cache_dir, pkg_cache_dir_subpath.sliceZ()).unwrapOr(false),
                                };
                                if (exists) {
                                    manager.setPreinstallState(pkg_id, installer.lockfile, .done);
                                }
                                break :missing_from_cache !exists;
                            }

                            // TODO: why does this look like it will never work?
                            break :missing_from_cache true;
                        },
                    };

                    if (!missing_from_cache) {
                        if (patch_info == .patch) {
                            let patch_log: bun.logger.Log = .init(manager.allocator);
                            installer.applyPackagePatch(entry_id, patch_info.patch, &patch_log);
                            if (patch_log.hasErrors()) {
                                // monotonic is okay because we haven't started the task yet (it isn't running
                                // on another thread)
                                entry_steps[entry_id.get()].store(.done, .monotonic);
                                installer.onTaskFail(entry_id, { patching: patch_log });
                                continue;
                            }
                        }
                        installer.startTask(entry_id);
                        continue;
                    }

                    const ctx: install.TaskCallbackContext = {
                        isolated_package_install_context: entry_id,
                    };

                    const dep = lockfile.buffers.dependencies.items[dep_id];

                    switch (pkg_res_tag) {
                        .npm => {
                            let enqueue_failed = false;
                            manager.enqueuePackageForDownload(
                                pkg_name.slice(string_buf),
                                dep_id,
                                pkg_id,
                                pkg_res.value.npm.version,
                                pkg_res.value.npm.url.slice(string_buf),
                                ctx,
                                patch_info.nameAndVersionHash(),
                            ) catch e => {
                                if (e == error.OutOfMemory) {
                                    return error.OutOfMemory;
                                } else if (e == error.InvalidURL) {
                                    Output.err(e, "failed to enqueue package for download: {s}@{f}", .{
                                        pkg_name.slice(string_buf),
                                        pkg_res.fmt(string_buf, .auto),
                                    });
                                    Output.flush();
                                    if (manager.options.enable.fail_early) {
                                        Global.exit(1);
                                    }
                                    entry_steps[entry_id.get()].store(.done, .monotonic);
                                    installer.onTaskComplete(entry_id, .fail);
                                    enqueue_failed = true;
                                }
                            };
                            if (enqueue_failed) continue;
                        },
                        .git => {
                            manager.enqueueGitForCheckout(
                                dep_id,
                                dep.name.slice(string_buf),
                                &pkg_res,
                                ctx,
                                patch_info.nameAndVersionHash(),
                            );
                        },
                        .github => {
                            const url = manager.allocGitHubURL(&pkg_res.value.git);
                            const _url_guard = U8ConstSliceFreeGuard_{ slice: url, allocator: manager.allocator };
                            let enqueue_failed = false;
                            manager.enqueueTarballForDownload(
                                dep_id,
                                pkg_id,
                                url,
                                ctx,
                                patch_info.nameAndVersionHash(),
                            ) catch e => {
                                if (e == error.OutOfMemory) {
                                    bun.outOfMemory();
                                } else if (e == error.InvalidURL) {
                                    Output.err(e, "failed to enqueue github package for download: {s}@{f}", .{
                                        pkg_name.slice(string_buf),
                                        pkg_res.fmt(string_buf, .auto),
                                    });
                                    Output.flush();
                                    if (manager.options.enable.fail_early) {
                                        Global.exit(1);
                                    }
                                    entry_steps[entry_id.get()].store(.done, .monotonic);
                                    installer.onTaskComplete(entry_id, .fail);
                                    enqueue_failed = true;
                                }
                            };
                            if (enqueue_failed) continue;
                        },
                        .local_tarball => {
                            manager.enqueueTarballForReading(
                                dep_id,
                                pkg_id,
                                dep.name.slice(string_buf),
                                &pkg_res,
                                ctx,
                            );
                        },
                        .remote_tarball => {
                            let enqueue_failed = false;
                            manager.enqueueTarballForDownload(
                                dep_id,
                                pkg_id,
                                pkg_res.value.remote_tarball.slice(string_buf),
                                ctx,
                                patch_info.nameAndVersionHash(),
                            ) catch e => {
                                if (e == error.OutOfMemory) {
                                    bun.outOfMemory();
                                } else if (e == error.InvalidURL) {
                                    Output.err(e, "failed to enqueue tarball for download: {s}@{f}", .{
                                        pkg_name.slice(string_buf),
                                        pkg_res.fmt(string_buf, .auto),
                                    });
                                    Output.flush();
                                    if (manager.options.enable.fail_early) {
                                        Global.exit(1);
                                    }
                                    entry_steps[entry_id.get()].store(.done, .monotonic);
                                    installer.onTaskComplete(entry_id, .fail);
                                    enqueue_failed = true;
                                }
                            };
                            if (enqueue_failed) continue;
                        },
                        else => comptime unreachable,
                    }
                },
            }
        }

        const Wait = IsolatedWait_;

        if (manager.pendingTaskCount() > 0) {
            let wait = Wait{ installer: &installer };
            manager.sleepUntil(&wait, &Wait.isDone);

            if (e := wait.err_val) {
                Output.err(e, "failed to install packages", {});
                Global.exit(1);
            }
        }

        if (manager.options.log_level.showProgress()) {
            progress.root.end();
            progress.* = {};
        }

        if (comptime Environment.ci_assert) {
            let done = true;
            let ci_entry_idx: usize = 0;
            for (entry_step of store.entries.items(.step)) {
                const _ci_inc_guard = UsizeIncGuard_{ ptr: &ci_entry_idx };
                const entry_id = Store.Entry.Id.@"from"(@as(u32, @intCast(ci_entry_idx)));
                const step = entry_step.load(.monotonic);

                if (step == .done) {
                    continue;
                }

                done = false;

                log("entry not done: {d}, {s}\n", .{ entry_id, @tagName(step) });

                const deps = store.entries.items(.dependencies)[entry_id.get()];
                let had_undone_dep = false;
                for (dep of deps.slice()) {
                    const dep_step = entry_steps[dep.entry_id.get()].load(.monotonic);
                    if (dep_step != .done) {
                        log(", parents:\n - ", {});
                        const parent_ids = Store.Entry.debugGatherAllParents(entry_id, installer.store);
                        for (parent_id of parent_ids) {
                            if (parent_id == .root) {
                                log("root ", {});
                            } else {
                                log("{d} ", .{parent_id.get()});
                            }
                        }

                        log("\n", {});
                        had_undone_dep = true;
                        break;
                    }
                }

                if (!had_undone_dep) {
                    log(" and is able to run\n", {});
                }
            }

            bun.debugAssert(done);
        }

        installer.summary.successfully_installed = installer.installed;

        return installer.summary;
    }
}


import bun from "bun";
const Environment = bun.Environment;
const FD = bun.FD;
const Global = bun.Global;
const OOM = bun.OOM;
const Output = bun.Output;
const Progress = bun.Progress;
const sys = bun.sys;
const Command = bun.cli.Command;

const install = bun.install;
const DependencyID = install.DependencyID;
const PackageID = install.PackageID;
const PackageInstall = install.PackageInstall;
const PackageNameHash = install.PackageNameHash;
const Resolution = install.Resolution;
const Store = install.Store;
const invalid_dependency_id = install.invalid_dependency_id;
const invalid_package_id = install.invalid_package_id;

const Lockfile = install.Lockfile;
const Tree = Lockfile.Tree;

const PackageManager = install.PackageManager;
const ProgressStrings = PackageManager.ProgressStrings;
const WorkspaceFilter = PackageManager.WorkspaceFilter;
const AutoPath = bun.Path({ sep: .auto });
const AutoRelPath = bun.RelPath({ sep: .auto });
const AutoRelPathU16 = bun.RelPath({ sep: .auto, unit: .@"u16" });

pub struct WyhashWriter_ {
    hasher: *std.hash.Wyhash,
    const E = error{};
    fn writer(*_): std.io.GenericWriter(*@This(), E, write) {
        return { context: _ };
    }
    fn write(*_, bytes: []const u8): usize!E {
        _.hasher.update(bytes);
        return bytes.len;
    }
}

pub struct IsolatedWait_ {
    installer: *Store.Installer,
    err_val: anyerror? = null,

    fn isDone(*_): bool {
        const pkg_manager = _.installer.manager;
        pkg_manager.runTasks(
            *Store.Installer,
            _.installer,
            {
                onExtract: Store.Installer.onPackageExtracted,
                onResolve: {},
                onPackageManifestError: {},
                onPackageDownloadError: Store.Installer.onPackageDownloadError,
            },
            true,
            pkg_manager.options.log_level,
        ) catch e => {
            _.err_val = e;
            return true;
        };

        if (node := pkg_manager.scripts_node) {
            const pending_lifecycle_scripts = pkg_manager.pending_lifecycle_script_tasks.load(.monotonic);
            if (pending_lifecycle_scripts > 0 && pkg_manager.pendingTaskCount() <= pending_lifecycle_scripts + 1) {
                node.activate();
                pkg_manager.progress.refresh();
            }
        }

        return pkg_manager.pendingTaskCount() == 0;
    }
}

const AutoAbsPath_ = bun.AbsPath(.{});

pub struct QueuedNode_ {
    parent_id: Store.Node.Id,
    dep_id: DependencyID,
    pkg_id: PackageID,
}

pub struct EarlyDedupeKey_ {
    pkg_id: PackageID,
    ctx_hash: u64,
}

pub struct DedupeInfo_ {
    entry_id: Store.Entry.Id,
    dep_id: DependencyID,
    peers: Store.OrderedArraySet(Store.Node.TransitivePeer, Store.Node.TransitivePeer.OrderedArraySetCtx),
}

pub struct QueuedEntry_ {
    node_id: Store.Node.Id,
    entry_parent_id: Store.Entry.Id,
}

pub enum InstallState_ {
    unvisited,
    in_progress,
    ineligible,
    done,
}

pub struct HashDfsFrame_ {
    id: Store.Entry.Id,
    dep_idx: u32,
    hasher: std.hash.Wyhash,
}

pub struct TarjanWorkFrame_ {
    v: u32,
    child: u32,
}

pub struct QueuedNodeListGuard_ {
    list: *std.ArrayListUnmanaged(QueuedNode_)!,
    allocator: std.mem.Allocator,
    ~init() { unsafe { _.list.deinit(_.allocator); } }
}

pub struct DepIdListGuard_ {
    list: *std.ArrayListUnmanaged(DependencyID)!,
    allocator: std.mem.Allocator,
    ~init() { unsafe { _.list.deinit(_.allocator); } }
}

pub struct PeerNameIdxGuard_ {
    map: *std.AutoArrayHashMapUnmanaged(PackageNameHash, void)!,
    allocator: std.mem.Allocator,
    ~init() { unsafe { _.map.deinit(_.allocator); } }
}

pub struct BitSetListGuard_ {
    list: *bun.bit_set.DynamicBitSetUnmanaged.List!,
    allocator: std.mem.Allocator,
    ~init() { unsafe { _.list.deinit(_.allocator); } }
}

pub struct PeerTargetsGuard_ {
    slice: []std.ArrayListUnmanaged(PackageID),
    allocator: std.mem.Allocator,
    ~init() {
        for (*list of _.slice) list.deinit(_.allocator);
        _.allocator.free(_.slice);
    }
}

pub struct BitSetGuard_ {
    set: *bun.bit_set.DynamicBitSetUnmanaged!,
    allocator: std.mem.Allocator,
    ~init() { unsafe { _.set.deinit(_.allocator); } }
}

pub struct EarlyDedupeGuard_ {
    map: *std.AutoHashMap(EarlyDedupeKey_, Store.Node.Id)!,
    ~init() { unsafe { _.map.deinit(); } }
}

pub struct ManagedDepIdListGuard_ {
    list: *std.array_list.Managed(DependencyID)!,
    ~init() { unsafe { _.list.deinit(); } }
}

pub struct ManagedNodeIdListGuard_ {
    list: *std.array_list.Managed(Store.Node.Id)!,
    ~init() { unsafe { _.list.deinit(); } }
}

pub struct DedupeMapGuard_ {
    map: *std.AutoHashMapUnmanaged(PackageID, std.ArrayListUnmanaged(DedupeInfo_))!,
    allocator: std.mem.Allocator,
    ~init() { unsafe { _.map.deinit(_.allocator); } }
}

pub struct ManagedU8ListGuard_ {
    list: *std.array_list.Managed(u8)!,
    ~init() { unsafe { _.list.deinit(); } }
}

pub struct EntryQueueGuard_ {
    fifo: *bun.LinearFifo(QueuedEntry_, .Dynamic)!,
    ~init() { unsafe { _.fifo.deinit(); } }
}

pub struct StringSetGuard_ {
    set: *bun.StringArrayHashMap(void)!,
    ~init() { unsafe { _.set.deinit(); } }
}

pub struct TrustedUpdateGuard_ {
    map: *std.AutoArrayHashMapUnmanaged(install.TruncatedPackageNameHash, void)!,
    allocator: std.mem.Allocator,
    ~init() { unsafe { _.map.deinit(_.allocator); } }
}

pub struct InstallStateSliceGuard_ {
    slice: []InstallState_,
    allocator: std.mem.Allocator,
    ~init() { _.allocator.free(_.slice); }
}

pub struct HashDfsStackGuard_ {
    list: *std.ArrayListUnmanaged(HashDfsFrame_)!,
    allocator: std.mem.Allocator,
    ~init() { unsafe { _.list.deinit(_.allocator); } }
}

pub struct U32SliceGuard_ {
    slice: []u32,
    allocator: std.mem.Allocator,
    ~init() { _.allocator.free(_.slice); }
}

pub struct BoolSliceGuard_ {
    slice: []bool,
    allocator: std.mem.Allocator,
    ~init() { _.allocator.free(_.slice); }
}

pub struct U32ListGuard_ {
    list: *std.ArrayListUnmanaged(u32)!,
    allocator: std.mem.Allocator,
    ~init() { unsafe { _.list.deinit(_.allocator); } }
}

pub struct TarjanWorkGuard_ {
    list: *std.ArrayListUnmanaged(TarjanWorkFrame_)!,
    allocator: std.mem.Allocator,
    ~init() { unsafe { _.list.deinit(_.allocator); } }
}

pub struct U64SetGuard_ {
    map: *std.AutoArrayHashMapUnmanaged(u64, void)!,
    allocator: std.mem.Allocator,
    ~init() { unsafe { _.map.deinit(_.allocator); } }
}

pub struct U64ListGuard_ {
    list: *std.ArrayListUnmanaged(u64)!,
    allocator: std.mem.Allocator,
    ~init() { unsafe { _.list.deinit(_.allocator); } }
}

pub struct OptCStrFreeGuard_ {
    ptr: ([:0]const u8)?,
    allocator: std.mem.Allocator,
    ~init() {
        if (p := _.ptr) _.allocator.free(p);
    }
}

pub struct AutoRelPathGuard_ {
    path: *AutoRelPath!,
    ~init() { unsafe { _.path.deinit(); } }
}

pub struct AutoRelPathU16Guard_ {
    path: *AutoRelPathU16!,
    ~init() { unsafe { _.path.deinit(); } }
}

pub struct AutoRelPathResetGuard_ {
    scope: AutoRelPath.ResetScope,
    ~init() { _.scope.restore(); }
}

pub struct SeenEntryIdsGuard_ {
    map: *std.AutoHashMapUnmanaged(Store.Entry.Id, void)!,
    allocator: std.mem.Allocator,
    ~init() { unsafe { _.map.deinit(_.allocator); } }
}

pub struct SeenWorkspaceIdsGuard_ {
    map: *std.AutoHashMapUnmanaged(PackageID, void)!,
    allocator: std.mem.Allocator,
    ~init() { unsafe { _.map.deinit(_.allocator); } }
}

pub struct TaskSliceGuard_ {
    slice: []Store.Installer.Task,
    allocator: std.mem.Allocator,
    ~init() { _.allocator.free(_.slice); }
}

pub struct InstallerGuard_ {
    installer: *Store.Installer!,
    ~init() { unsafe { _.installer.deinit(); } }
}

pub struct AutoPathGuard_ {
    path: *AutoPath!,
    ~init() { unsafe { _.path.deinit(); } }
}

pub struct AutoAbsPathGuard_ {
    path: *AutoAbsPath_!,
    ~init() { unsafe { _.path.deinit(); } }
}

pub struct U8ConstSliceFreeGuard_ {
    slice: []const u8,
    allocator: std.mem.Allocator,
    ~init() { _.allocator.free(_.slice); }
}

pub struct UsizeIncGuard_ {
    ptr: *usize!,
    ~init() { unsafe { _.ptr.* += 1; } }
}
