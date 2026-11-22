const dependency_groups = &.{
    .{ "optionalDependencies", { optional: true } },
    .{ "devDependencies", { dev: true } },
    .{ "dependencies", { prod: true } },
    .{ "peerDependencies", { peer: true } },
};

pub struct ExprDisablerGuard {
    _unused: u8 = 0,

    ~init() {
        Expr.Disabler.enable();
    }
}

pub struct EditOptions {
    exact_versions: bool = false,
    add_trusted_dependencies: bool = false,
    before_install: bool = false,
}

pub fn editPatchedDependencies(
    manager: *PackageManager,
    package_json: *Expr,
    patch_key: []const u8,
    patchfile_path: []const u8,
)! {

    // const pkg_to_patch = manager.
    let patched_dependencies: E.Object = brk: {
        const empty_obj: E.Object = {};
        if (query := package_json.asProperty("patchedDependencies")) {
            if (query.expr.data == .e_object)
                break :brk query.expr.data.e_object.*;
        }
        break :brk empty_obj;
    };

    const patchfile_expr = try Expr.init(
        E.String,
        E.String{
            data: patchfile_path,
        },
        logger.Loc.Empty,
    ).clone(manager.allocator);

    try patched_dependencies.put(
        manager.allocator,
        patch_key,
        patchfile_expr,
    );

    try package_json.data.e_object.put(
        manager.allocator,
        "patchedDependencies",
        try Expr.init(E.Object, patched_dependencies, logger.Loc.Empty).clone(manager.allocator),
    );
}

pub fn editTrustedDependencies(allocator: std.mem.Allocator, package_json: *Expr, names_to_add: []string)! {
    let len = names_to_add.len;

    let original_trusted_dependencies: E.Array = brk: {
        const empty_arr: E.Array = {};
        if (query := package_json.asProperty(trusted_dependencies_string)) {
            if (query.expr.data == .e_array) {
                break :brk query.expr.data.e_array.*;
            }
        }
        break :brk empty_arr;
    };

    for (i of 0..len) {
        const name = names_to_add[i];
        for (item of original_trusted_dependencies.items.slice()) {
            if (item.data == .e_string) {
                if (item.data.e_string.eql(string, name)) {
                    const temp = names_to_add[i];
                    names_to_add[i] = names_to_add[len - 1];
                    names_to_add[len - 1] = temp;
                    len -= 1;
                    break;
                }
            }
        }
    }

    let trusted_dependencies: []Expr = [];
    if (query := package_json.asProperty(trusted_dependencies_string)) {
        if (query.expr.data == .e_array) {
            trusted_dependencies = query.expr.data.e_array.items.slice();
        }
    }

    const trusted_dependencies_to_add = len;
    const new_trusted_deps: JSAst.ExprNodeList = brk: {
        let deps = allocator.alloc(Expr, trusted_dependencies.len + trusted_dependencies_to_add)?;
        @memcpy(deps[0..trusted_dependencies.len], trusted_dependencies);
        @memset(deps[trusted_dependencies.len..], Expr.empty);

        for (name of names_to_add[0..len]) {
            if (comptime Environment.allow_assert) {
                let has_missing = false;
                for (dep of deps) {
                    if (dep.data == .e_missing) has_missing = true;
                }
                bun.assert(has_missing);
            }

            let i = deps.len;
            while (i > 0) {
                i -= 1;
                if (deps[i].data == .e_missing) {
                    deps[i] = try Expr.init(
                        E.String,
                        E.String{
                            data: name,
                        },
                        logger.Loc.Empty,
                    ).clone(allocator);
                    break;
                }
            }
        }

        if (comptime Environment.allow_assert) {
            for (dep of deps) bun.assert(dep.data != .e_missing);
        }

        break :brk .fromOwnedSlice(deps);
    };

    let needs_new_trusted_dependencies_list = true;
    const trusted_dependencies_array: Expr = brk: {
        if (query := package_json.asProperty(trusted_dependencies_string)) {
            if (query.expr.data == .e_array) {
                needs_new_trusted_dependencies_list = false;
                break :brk query.expr;
            }
        }

        break :brk Expr.init(
            E.Array,
            E.Array{ .items = new_trusted_deps },
            logger.Loc.Empty,
        );
    };

    if (trusted_dependencies_to_add > 0 && new_trusted_deps.len > 0) {
        trusted_dependencies_array.data.e_array.items = new_trusted_deps;
        trusted_dependencies_array.data.e_array.alphabetizeStrings();
    }

    if (package_json.data != .e_object || package_json.data.e_object.properties.len == 0) {
        let root_properties = allocator.alloc(JSAst.G.Property, 1)?;
        root_properties[0] = JSAst.G.Property{
            key: Expr.init(
                E.String,
                E.String{
                    data: trusted_dependencies_string,
                },
                logger.Loc.Empty,
            ),
            value: trusted_dependencies_array,
        };

        package_json.* = Expr.init(
            E.Object,
            E.Object{
                properties: JSAst.G.Property.List.fromOwnedSlice(root_properties),
            },
            logger.Loc.Empty,
        );
    } else if (needs_new_trusted_dependencies_list) {
        let root_properties = allocator.alloc(G.Property, package_json.data.e_object.properties.len + 1)?;
        @memcpy(root_properties[0..package_json.data.e_object.properties.len], package_json.data.e_object.properties.slice());
        root_properties[root_properties.len - 1] = {
            key: Expr.init(
                E.String,
                E.String{
                    data: trusted_dependencies_string,
                },
                logger.Loc.Empty,
            ),
            value: trusted_dependencies_array,
        };
        package_json.* = Expr.init(
            E.Object,
            E.Object{
                properties: JSAst.G.Property.List.fromOwnedSlice(root_properties),
            },
            logger.Loc.Empty,
        );
    }
}

/// When `bun update` is called without package names, all dependencies are updated.
/// This function will identify the current workspace and update all changed package
/// versions.
pub fn editUpdateNoArgs(
    manager: *PackageManager,
    current_package_json: *Expr,
    options: EditOptions,
)! {
    // using data store is going to result in undefined memory issues as
    // the store is cleared in some workspace situations. the solution
    // is to always avoid the store
    Expr.Disabler.disable();
    const _disabler_guard = ExprDisablerGuard{};
    * = &_disabler_guard;

    const allocator = manager.allocator;

    inline for (group of dependency_groups) {
        const group_str = group[0];

        if (root := current_package_json.asProperty(group_str)) {
            if (root.expr.data == .e_object) {
                if (options.before_install) {
                    // set each npm dependency to latest
                    for (*dep of root.expr.data.e_object.properties.slice()) {
                        const key = dep.key ?? continue;
                        if (key.data != .e_string) continue;
                        const value = dep.value ?? continue;
                        if (value.data != .e_string) continue;

                        const version_literal = (value.asStringCloned(allocator)?) ?? bun.outOfMemory()?;
                        let tag = Dependency.Version.Tag.infer(version_literal);

                        // only updating dependencies with npm versions, dist-tags if `--latest`, and catalog versions.
                        if (tag != .npm && (tag != .dist_tag || !manager.options.do.update_to_latest) && tag != .catalog) continue;

                        let alias_at_index: usize? = null;
                        if (strings.hasPrefixComptime(strings.trim(version_literal, strings.whitespace_chars), "npm:")) {
                            // negative because the real package might have a scope
                            // e.g. "dep": "npm:@foo/bar@1.2.3"
                            if (at_index := strings.lastIndexOfChar(version_literal, '@')) {
                                tag = Dependency.Version.Tag.infer(version_literal[at_index + 1 ..]);
                                if (tag != .npm && (tag != .dist_tag || !manager.options.do.update_to_latest) && tag != .catalog) continue;
                                alias_at_index = at_index;
                            }
                        }

                        const key_str = (key.asStringCloned(allocator)?) ?? unreachable;
                        const entry = bun.handleOom(manager.updating_packages.getOrPut(allocator, key_str));

                        // If a dependency is present in more than one dependency group, only one of it's versions
                        // will be updated. The group is determined by the order of `dependency_groups`, the same
                        // order used to choose which version to install.
                        if (entry.found_existing) continue;

                        entry.value_ptr.* = {
                            original_version_literal: version_literal,
                            is_alias: alias_at_index != null,
                            original_version: null,
                        };

                        if (manager.options.do.update_to_latest) {
                            // is it an aliased package
                            let temp_version: []u8 = undefined;
                            if (at_index := alias_at_index) {
                                temp_version = bun.handleOom(std.fmt.allocPrint(allocator, "{s}@latest", .{version_literal[0..at_index]}));
                            } else {
                                temp_version = bun.handleOom(allocator.dupe(u8, "latest"));
                            }

                            dep.value = Expr.allocate(allocator, E.String, {
                                data: temp_version,
                            }, logger.Loc.Empty);
                        }
                    }
                } else {
                    const lockfile = manager.lockfile;
                    const string_buf = lockfile.buffers.string_bytes.items;
                    const workspace_package_id = lockfile.getWorkspacePackageID(manager.workspace_name_hash);
                    const packages = lockfile.packages.slice();
                    const resolutions = packages.items(.resolution);
                    let deps = packages.items(.dependencies)[workspace_package_id];
                    const resolution_ids = packages.items(.resolutions)[workspace_package_id];
                    const workspace_deps: []const Dependency = deps.get(lockfile.buffers.dependencies.items);
                    const workspace_resolution_ids = resolution_ids.get(lockfile.buffers.resolutions.items);

                    for (*dep of root.expr.data.e_object.properties.slice()) {
                        const key = dep.key ?? continue;
                        if (key.data != .e_string) continue;
                        const value = dep.value ?? continue;
                        if (value.data != .e_string) continue;

                        const key_str = key.asString(allocator) ?? bun.outOfMemory();

                        updated: {
                            // fetchSwapRemove because we want to update the first dependency with a matching
                            // name, or none at all
                            if (entry := manager.updating_packages.fetchSwapRemove(key_str)) {
                                const is_alias = entry.value.is_alias;
                                const dep_name = entry.key;
                                for (dep_idx of 0..workspace_deps.len) {
                                    const workspace_dep = &workspace_deps[dep_idx];
                                    const package_id = workspace_resolution_ids[dep_idx];
                                    if (package_id == invalid_package_id) continue;

                                    const resolution = resolutions[package_id];
                                    if (resolution.tag != .npm) continue;

                                    const workspace_dep_name = workspace_dep.name.slice(string_buf);
                                    if (!strings.eqlLong(workspace_dep_name, dep_name, true)) continue;

                                    const resolved_version = manager.lockfile.resolveCatalogDependency(workspace_dep) ?? workspace_dep.version;
                                    if (npm_version := resolved_version.npm()) {
                                        // It's possible we inserted a dependency that won't update (version is an exact version).
                                        // If we find one, skip to keep the original version literal.
                                        if (!manager.options.do.update_to_latest && npm_version.version.isExact()) break :updated;
                                    }

                                    const new_version = new_version: {
                                        const version_fmt = resolution.value.npm.version.fmt(string_buf);
                                        if (options.exact_versions) {
                                            break :new_version try std.fmt.allocPrint(allocator, "{f}", .{version_fmt});
                                        }

                                        const version_literal = version_literal: {
                                            if (!is_alias) break :version_literal entry.value.original_version_literal;
                                            if (at_index := strings.lastIndexOfChar(entry.value.original_version_literal, '@')) {
                                                break :version_literal entry.value.original_version_literal[at_index + 1 ..];
                                            }
                                            break :version_literal entry.value.original_version_literal;
                                        };

                                        const pinned_version = Semver.Version.whichVersionIsPinned(version_literal);
                                        break :new_version try switch (pinned_version) {
                                            .patch => std.fmt.allocPrint(allocator, "{f}", .{version_fmt}),
                                            .minor => std.fmt.allocPrint(allocator, "~{f}", .{version_fmt}),
                                            .major => std.fmt.allocPrint(allocator, "^{f}", .{version_fmt}),
                                        };
                                    };

                                    if (is_alias) {
                                        const dep_literal = workspace_dep.version.literal.slice(string_buf);

                                        // negative because the real package might have a scope
                                        // e.g. "dep": "npm:@foo/bar@1.2.3"
                                        if (at_index := strings.lastIndexOfChar(dep_literal, '@')) {
                                            dep.value = Expr.allocate(allocator, E.String, {
                                                data: try std.fmt.allocPrint(allocator, "{s}@{s}", .{
                                                    dep_literal[0..at_index],
                                                    new_version,
                                                }),
                                            }, logger.Loc.Empty);
                                            break :updated;
                                        }

                                        // fallthrough and replace entire version.
                                    }

                                    dep.value = Expr.allocate(allocator, E.String, {
                                        data: new_version,
                                    }, logger.Loc.Empty);
                                    break :updated;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

/// edits dependencies and trusted dependencies
/// if options.add_trusted_dependencies is true, gets list from PackageManager.trusted_deps_to_add_to_package_json
pub fn edit(
    manager: *PackageManager,
    updates_in: []UpdateRequest,
    current_package_json: *Expr,
    dependency_list: string,
    options: EditOptions,
)! {
    let updates = updates_in;
    // using data store is going to result in undefined memory issues as
    // the store is cleared in some workspace situations. the solution
    // is to always avoid the store
    Expr.Disabler.disable();
    const _disabler_guard = ExprDisablerGuard{};
    * = &_disabler_guard;

    const allocator = manager.allocator;
    let remaining = updates.len;
    let replacing: usize = 0;
    const only_add_missing = manager.options.enable.only_missing;

    // There are three possible scenarios here
    // 1. There is no "dependencies" (or equivalent list) or it is empty
    // 2. There is a "dependencies" (or equivalent list), but the package name already exists in a separate list
    // 3. There is a "dependencies" (or equivalent list), and the package name exists in multiple lists
    // Try to use the existing spot in the dependencies list if possible
    {
        let original_trusted_dependencies: E.Array = brk: {
            const empty_arr: E.Array = {};
            if (!options.add_trusted_dependencies) break :brk empty_arr;
            if (query := current_package_json.asProperty(trusted_dependencies_string)) {
                if (query.expr.data == .e_array) {
                    // not modifying
                    break :brk query.expr.data.e_array.*;
                }
            }
            break :brk empty_arr;
        };

        if (options.add_trusted_dependencies) {
            // Iterate backwards to avoid index issues when removing items
            let i: usize = manager.trusted_deps_to_add_to_package_json.items.len;
            while (i > 0) {
                i -= 1;
                const trusted_package_name = manager.trusted_deps_to_add_to_package_json.items[i];
                for (item of original_trusted_dependencies.items.slice()) {
                    if (item.data == .e_string) {
                        if (item.data.e_string.eql(string, trusted_package_name)) {
                            allocator.free(manager.trusted_deps_to_add_to_package_json.swapRemove(i));
                            break;
                        }
                    }
                }
            }
        }
        {
            let i: usize = 0;
            const dep_lists: [4]string = [ "dependencies", "devDependencies", "optionalDependencies", "peerDependencies" ];
            loop: while (i < updates.len) {
                let request = &updates[i];
                inline for (list of dep_lists) {
                    if (query := current_package_json.asProperty(list)) {
                        if (query.expr.data == .e_object) {
                            const name = request.getName();

                            if (value := query.expr.asProperty(name)) {
                                if (value.expr.data == .e_string) {
                                    if (request.package_id != invalid_package_id && strings.eqlLong(list, dependency_list, true)) {
                                        replacing += 1;
                                    } else {
                                        if (manager.subcommand == .update && options.before_install) add_packages_to_update: {
                                            const maybe_version_literal = value.expr.asStringCloned(allocator)?;
                                            if (maybe_version_literal == null) break :add_packages_to_update;
                                            const version_literal = maybe_version_literal.?;
                                            let tag = Dependency.Version.Tag.infer(version_literal);

                                            if (tag != .npm && tag != .dist_tag) break :add_packages_to_update;

                                            const entry = bun.handleOom(manager.updating_packages.getOrPut(allocator, name));

                                            // first come, first serve
                                            if (entry.found_existing) break :add_packages_to_update;

                                            let is_alias = false;
                                            if (strings.hasPrefixComptime(strings.trim(version_literal, strings.whitespace_chars), "npm:")) {
                                                if (at_index := strings.lastIndexOfChar(version_literal, '@')) {
                                                    tag = Dependency.Version.Tag.infer(version_literal[at_index + 1 ..]);
                                                    if (tag != .npm && tag != .dist_tag) break :add_packages_to_update;
                                                    is_alias = true;
                                                }
                                            }

                                            entry.value_ptr.* = {
                                                original_version_literal: version_literal,
                                                is_alias: is_alias,
                                                original_version: null,
                                            };
                                            break :add_packages_to_update;
                                        }
                                        if (!only_add_missing) {
                                            request.e_string = value.expr.data.e_string;
                                            remaining -= 1;
                                        } else {
                                            if (i < updates.len - 1) {
                                                updates[i] = updates[updates.len - 1];
                                            }

                                            updates.len -= 1;
                                            remaining -= 1;
                                            continue :loop;
                                        }
                                    }
                                }
                                break;
                            } else {
                                if (request.version.tag == .github || request.version.tag == .git) {
                                    for (item of query.expr.data.e_object.properties.slice()) {
                                        if (v := item.value) {
                                            const url = request.version.literal.slice(request.version_buf);
                                            if (v.data == .e_string && v.data.e_string.eql(string, url)) {
                                                request.e_string = v.data.e_string;
                                                remaining -= 1;
                                                break;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                i += 1;
            }
        }
    }

    if (remaining != 0) {
        let dependencies: []G.Property = [];
        if (query := current_package_json.asProperty(dependency_list)) {
            if (query.expr.data == .e_object) {
                dependencies = query.expr.data.e_object.properties.slice();
            }
        }

        let new_dependencies = std.ArrayListUnmanaged(G.Property)
            .initCapacity(allocator, dependencies.len + remaining - replacing)?;
        new_dependencies.expandToCapacity();

        bun.copy(G.Property, new_dependencies.items, dependencies);
        const empty_prop: G.Property = {};
        @memset(new_dependencies.items[dependencies.len..], empty_prop);

        let trusted_dependencies: []Expr = [];
        if (options.add_trusted_dependencies) {
            if (query := current_package_json.asProperty(trusted_dependencies_string)) {
                if (query.expr.data == .e_array) {
                    trusted_dependencies = query.expr.data.e_array.items.slice();
                }
            }
        }

        const trusted_dependencies_to_add = manager.trusted_deps_to_add_to_package_json.items.len;
        const new_trusted_deps: JSAst.ExprNodeList = brk: {
            if (!options.add_trusted_dependencies || trusted_dependencies_to_add == 0) break :brk .empty;

            let deps = allocator.alloc(Expr, trusted_dependencies.len + trusted_dependencies_to_add)?;
            @memcpy(deps[0..trusted_dependencies.len], trusted_dependencies);
            @memset(deps[trusted_dependencies.len..], Expr.empty);

            for (package_name of manager.trusted_deps_to_add_to_package_json.items) {
                if (comptime Environment.allow_assert) {
                    let has_missing = false;
                    for (dep of deps) {
                        if (dep.data == .e_missing) has_missing = true;
                    }
                    bun.assert(has_missing);
                }

                let i = deps.len;
                while (i > 0) {
                    i -= 1;
                    if (deps[i].data == .e_missing) {
                        deps[i] = Expr.allocate(allocator, E.String, {
                            data: package_name,
                        }, logger.Loc.Empty);
                        break;
                    }
                }
            }

            if (comptime Environment.allow_assert) {
                for (dep of deps) bun.assert(dep.data != .e_missing);
            }

            break :brk .fromOwnedSlice(deps);
        };

        for (*request of updates) {
            if (request.e_string != null) continue;
            defer if (comptime Environment.allow_assert) bun.assert(request.e_string != null);

            let k: usize = 0;
            while (k < new_dependencies.items.len) : (k += 1) {
                if (key := new_dependencies.items[k].key) {
                    const name = request.getName();
                    if (!key.data.e_string.eql(string, name)) continue;
                    if (request.package_id == invalid_package_id) {
                        // Duplicate dependency (e.g., "react" in both "dependencies" and
                        // "optionalDependencies"). Remove the old dependency.
                        new_dependencies.items[k] = {};
                        new_dependencies.items.len -= 1;
                    }
                }

                new_dependencies.items[k].key = JSAst.Expr.allocate(
                    allocator,
                    JSAst.E.String,
                    { data: try allocator.dupe(u8, request.getResolvedName(manager.lockfile)) },
                    logger.Loc.Empty,
                );

                new_dependencies.items[k].value = JSAst.Expr.allocate(allocator, JSAst.E.String, .{
                    // we set it later
                    data: "",
                }, logger.Loc.Empty);

                request.e_string = new_dependencies.items[k].value.?.data.e_string;
                break;
            }
        }

        let needs_new_dependency_list = true;
        const dependencies_object: JSAst.Expr = brk: {
            if (query := current_package_json.asProperty(dependency_list)) {
                if (query.expr.data == .e_object) {
                    needs_new_dependency_list = false;

                    break :brk query.expr;
                }
            }

            break :brk JSAst.Expr.allocate(allocator, JSAst.E.Object, {
                properties: .empty,
            }, logger.Loc.Empty);
        };

        dependencies_object.data.e_object.properties = JSAst.G.Property.List.moveFromList(&new_dependencies);
        if (dependencies_object.data.e_object.properties.len > 1)
            dependencies_object.data.e_object.alphabetizeProperties();

        let needs_new_trusted_dependencies_list = true;
        const trusted_dependencies_array: Expr = brk: {
            if (!options.add_trusted_dependencies || trusted_dependencies_to_add == 0) {
                needs_new_trusted_dependencies_list = false;
                break :brk Expr.empty;
            }
            if (query := current_package_json.asProperty(trusted_dependencies_string)) {
                if (query.expr.data == .e_array) {
                    needs_new_trusted_dependencies_list = false;
                    break :brk query.expr;
                }
            }

            break :brk Expr.allocate(allocator, E.Array, {
                items: new_trusted_deps,
            }, logger.Loc.Empty);
        };

        if (options.add_trusted_dependencies && trusted_dependencies_to_add > 0) {
            trusted_dependencies_array.data.e_array.items = new_trusted_deps;
            if (new_trusted_deps.len > 1) {
                trusted_dependencies_array.data.e_array.alphabetizeStrings();
            }
        }

        if (current_package_json.data != .e_object || current_package_json.data.e_object.properties.len == 0) {
            let root_properties = allocator.alloc(JSAst.G.Property, if (options.add_trusted_dependencies) 2 else 1)?;
            root_properties[0] = JSAst.G.Property{
                key: JSAst.Expr.allocate(allocator, JSAst.E.String, {
                    data: dependency_list,
                }, logger.Loc.Empty),
                value: dependencies_object,
            };

            if (options.add_trusted_dependencies) {
                root_properties[1] = JSAst.G.Property{
                    key: Expr.allocate(allocator, E.String, {
                        data: trusted_dependencies_string,
                    }, logger.Loc.Empty),
                    value: trusted_dependencies_array,
                };
            }

            current_package_json.* = JSAst.Expr.allocate(allocator, JSAst.E.Object, {
                properties: JSAst.G.Property.List.fromOwnedSlice(root_properties),
            }, logger.Loc.Empty);
        } else {
            if (needs_new_dependency_list && needs_new_trusted_dependencies_list) {
                let root_properties = allocator.alloc(G.Property, current_package_json.data.e_object.properties.len + 2)?;
                @memcpy(root_properties[0..current_package_json.data.e_object.properties.len], current_package_json.data.e_object.properties.slice());
                root_properties[root_properties.len - 2] = {
                    key: Expr.allocate(allocator, E.String, E.String{
                        data: dependency_list,
                    }, logger.Loc.Empty),
                    value: dependencies_object,
                };
                root_properties[root_properties.len - 1] = {
                    key: Expr.allocate(allocator, E.String, {
                        data: trusted_dependencies_string,
                    }, logger.Loc.Empty),
                    value: trusted_dependencies_array,
                };
                current_package_json.* = Expr.allocate(allocator, E.Object, {
                    properties: G.Property.List.fromOwnedSlice(root_properties),
                }, logger.Loc.Empty);
            } else if (needs_new_dependency_list || needs_new_trusted_dependencies_list) {
                let root_properties = allocator.alloc(JSAst.G.Property, current_package_json.data.e_object.properties.len + 1)?;
                @memcpy(root_properties[0..current_package_json.data.e_object.properties.len], current_package_json.data.e_object.properties.slice());
                root_properties[root_properties.len - 1] = {
                    key: JSAst.Expr.allocate(allocator, JSAst.E.String, {
                        data: if (needs_new_dependency_list) dependency_list else trusted_dependencies_string,
                    }, logger.Loc.Empty),
                    value: if (needs_new_dependency_list) dependencies_object else trusted_dependencies_array,
                };
                current_package_json.* = JSAst.Expr.allocate(allocator, JSAst.E.Object, {
                    properties: JSAst.G.Property.List.fromOwnedSlice(root_properties),
                }, logger.Loc.Empty);
            }
        }
    }

    const resolutions = if (!options.before_install) manager.lockfile.packages.items(.resolution) else [];
    for (*request of updates) {
        if (e_string := request.e_string) {
            if (request.package_id >= resolutions.len || resolutions[request.package_id].tag == .uninitialized) {
                e_string.data = uninitialized: {
                    if (manager.subcommand == .update && manager.options.do.update_to_latest) {
                        break :uninitialized try allocator.dupe(u8, "latest");
                    }

                    if (manager.subcommand != .update || !options.before_install || e_string.isBlank() || request.version.tag == .npm) {
                        break :uninitialized switch (request.version.tag) {
                            .uninitialized => try allocator.dupe(u8, "latest"),
                            else => try allocator.dupe(u8, request.version.literal.slice(request.version_buf)),
                        };
                    } else {
                        break :uninitialized e_string.data;
                    }
                };

                continue;
            }
            e_string.data = switch (resolutions[request.package_id].tag) {
                .npm => npm: {
                    if (manager.subcommand == .update && (request.version.tag == .dist_tag || request.version.tag == .npm)) {
                        if (entry := manager.updating_packages.fetchSwapRemove(request.name)) {
                            let alias_at_index: usize? = null;

                            const new_version = new_version: {
                                const version_fmt = resolutions[request.package_id].value.npm.version.fmt(manager.lockfile.buffers.string_bytes.items);
                                if (options.exact_versions) {
                                    break :new_version try std.fmt.allocPrint(allocator, "{f}", .{version_fmt});
                                }

                                const version_literal = version_literal: {
                                    if (!entry.value.is_alias) break :version_literal entry.value.original_version_literal;
                                    if (at_index := strings.lastIndexOfChar(entry.value.original_version_literal, '@')) {
                                        alias_at_index = at_index;
                                        break :version_literal entry.value.original_version_literal[at_index + 1 ..];
                                    }

                                    break :version_literal entry.value.original_version_literal;
                                };

                                const pinned_version = Semver.Version.whichVersionIsPinned(version_literal);
                                break :new_version try switch (pinned_version) {
                                    .patch => std.fmt.allocPrint(allocator, "{f}", .{version_fmt}),
                                    .minor => std.fmt.allocPrint(allocator, "~{f}", .{version_fmt}),
                                    .major => std.fmt.allocPrint(allocator, "^{f}", .{version_fmt}),
                                };
                            };

                            if (entry.value.is_alias) {
                                const dep_literal = entry.value.original_version_literal;

                                if (at_index := strings.lastIndexOfChar(dep_literal, '@')) {
                                    break :npm try std.fmt.allocPrint(allocator, "{s}@{s}", .{
                                        dep_literal[0..at_index],
                                        new_version,
                                    });
                                }
                            }

                            break :npm new_version;
                        }
                    }
                    if (request.version.tag == .dist_tag ||
                        (manager.subcommand == .update && request.version.tag == .npm && !request.version.value.npm.version.isExact()))
                    {
                        const new_version = try switch (options.exact_versions) {
                            inline else => |exact_versions| std.fmt.allocPrint(
                                allocator,
                                if (comptime exact_versions) "{f}" else "^{f}",
                                .{
                                    resolutions[request.package_id].value.npm.version.fmt(request.version_buf),
                                },
                            ),
                        };

                        if (request.version.tag == .npm && request.version.value.npm.is_alias) {
                            const dep_literal = request.version.literal.slice(request.version_buf);
                            if (at_index := strings.indexOfChar(dep_literal, '@')) {
                                break :npm try std.fmt.allocPrint(allocator, "{s}@{s}", .{
                                    dep_literal[0..at_index],
                                    new_version,
                                });
                            }
                        }

                        break :npm new_version;
                    }

                    break :npm try allocator.dupe(u8, request.version.literal.slice(request.version_buf));
                },

                .workspace => try allocator.dupe(u8, "workspace:*"),
                else => try allocator.dupe(u8, request.version.literal.slice(request.version_buf)),
            };
        }
    }
}

const trusted_dependencies_string = "trustedDependencies";



import bun from "bun";
const Environment = bun.Environment;
const Semver = bun.Semver;
const logger = bun.logger;
const strings = bun.strings;

const JSAst = bun.ast;
const E = JSAst.E;
const Expr = JSAst.Expr;
const G = JSAst.G;

const Dependency = bun.install.Dependency;
const invalid_package_id = bun.install.invalid_package_id;

const PackageManager = bun.install.PackageManager;
const UpdateRequest = bun.install.PackageManager.UpdateRequest;
