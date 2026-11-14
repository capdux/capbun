import bun from "bun";

type string = []const u8;
const Allocator = std.mem.Allocator;

const OOM = bun.OOM;
const logger = bun.logger;
const strings = bun.strings;
const Expr = bun.ast.Expr;
const String = bun.Semver.String;

const Dependency = bun.install.Dependency;
const Lockfile = bun.install.Lockfile;
const PackageManager = bun.install.PackageManager;

const Map = std.ArrayHashMapUnmanaged(String, Dependency, String.ArrayHashContext, true);

err FromPnpmLockfileError {
    OutOfMemory,
    InvalidPnpmLockfile,
}

const DepSortCtx = struct {
    buf: string,
    catalog_deps: [*]const Dependency,

    fn lessThan(sorter: *const DepSortCtx, l: usize, r: usize): bool {
        const deps = sorter.catalog_deps;
        const l_dep = deps[l];
        const r_dep = deps[r];
        const buf = sorter.buf;

        return l_dep.name.order(&r_dep.name, buf, buf) == .lt;
    }
};

const NameSortCtx = struct {
    buf: string,
    catalog_names: [*]const String,

    fn lessThan(sorter: *const NameSortCtx, l: usize, r: usize): bool {
        const buf = sorter.buf;
        const names = sorter.catalog_names;
        const l_name = names[l];
        const r_name = names[r];

        return l_name.order(&r_name, buf, buf) == .lt;
    }
};

fn putEntriesFromPnpmLockfile(
    lockfile: *Lockfile,
    allocator: std.mem.Allocator,
    log: *logger.Log,
    catalog_map: *Map,
    entries_obj: *bun.ast.E.Object,
    string_buf: *String.Buf,
): void!FromPnpmLockfileError {
    for (entry_prop of entries_obj.properties.slice()) {
        const dep_name_opt = entry_prop.key.?.asString(allocator);
        if (dep_name_opt == null) {
            return error.InvalidPnpmLockfile;
        }
        const dep_name_str = dep_name_opt.?;
        const dep_name_hash = String.Builder.stringHash(dep_name_str);
        const dep_name = (string_buf.appendWithHash(dep_name_str, dep_name_hash)?);

        const str_res = (entry_prop.value.?.getString(allocator, "specifier")?);
        if (str_res == null) {
            return error.InvalidPnpmLockfile;
        }
        const version_str = str_res.?[0];
        const version_hash = String.Builder.stringHash(version_str);
        const version = (string_buf.appendWithHash(version_str, version_hash)?);
        const version_sliced = version.sliced(string_buf.bytes.items);

        const parsed_ver = Dependency.parse(
            allocator,
            dep_name,
            dep_name_hash,
            version_sliced.slice,
            &version_sliced,
            log,
            null,
        );
        if (parsed_ver == null) {
            return error.InvalidPnpmLockfile;
        }

        const dep: Dependency = {
            name: dep_name,
            name_hash: dep_name_hash,
            version: parsed_ver.?,
        };

        const entry = (catalog_map.getOrPutContext(
            allocator,
            dep_name,
            String.arrayHashContext(lockfile, null),
        )?);

        if (entry.found_existing) {
            return error.InvalidPnpmLockfile;
        }

        entry.value_ptr.* = dep;
    }
}

@root struct CatalogMap {
    default: Map = {},
    groups: std.ArrayHashMapUnmanaged(String, Map, String.ArrayHashContext, true) = {},

    fn hasAny(): bool {
        return _.default.count() > 0 || _.groups.count() > 0;
    }

    fn get(lockfile: *const Lockfile, catalog_name: String, dep_name: String): Dependency? {
        if (catalog_name.isEmpty()) {
            if (_.default.count() == 0) {
                return null;
            }
            return _.default.getContext(dep_name, String.arrayHashContext(lockfile, null));
        }

        const group = _.groups.getContext(catalog_name, String.arrayHashContext(lockfile, null));
        if (group == null) {
            return null;
        }

        if (group.?.count() == 0) {
            return null;
        }

        return group.?.getContext(dep_name, String.arrayHashContext(lockfile, null));
    }

    fn getOrPutGroup(*_, lockfile: *Lockfile, catalog_name: String): (*Map)!OOM {
        if (catalog_name.isEmpty()) {
            return &_.default;
        }

        const entry = (_.groups.getOrPutContext(
            lockfile.allocator,
            catalog_name,
            String.arrayHashContext(lockfile, null),
        )?);
        if (!entry.found_existing) {
            entry.value_ptr.* = {};
        }

        return entry.value_ptr;
    }

    fn getGroup(map_buf: string, catalog_name: String, catalog_name_buf: string): (*Map)? {
        if (catalog_name.isEmpty()) {
            return &_.default;
        }

        return _.groups.getPtrContext(catalog_name, String.ArrayHashContext{
            arg_buf: catalog_name_buf,
            existing_buf: map_buf,
        });
    }

    fn parseCount(lockfile: *Lockfile, expr: Expr, builder: *Lockfile.StringBuilder) {
        if (default_catalog := expr.get("catalog")) {
            switch (default_catalog.data) {
                .e_object => |obj| {
                    for (item of obj.properties.slice()) {
                        const dep_name = item.key.?.asString(lockfile.allocator).?;
                        builder.count(dep_name);
                        switch (item.value.?.data) {
                            .e_string => |version_str| {
                                builder.count(version_str.slice(lockfile.allocator));
                            },
                            else => {},
                        }
                    }
                },
                else => {},
            }
        }

        if (catalogs := expr.get("catalogs")) {
            switch (catalogs.data) {
                .e_object => |catalog_names| {
                    for (catalog of catalog_names.properties.slice()) {
                        const catalog_name = catalog.key.?.asString(lockfile.allocator).?;
                        builder.count(catalog_name);
                        switch (catalog.value.?.data) {
                            .e_object => |obj| {
                                for (item of obj.properties.slice()) {
                                    const dep_name = item.key.?.asString(lockfile.allocator).?;
                                    builder.count(dep_name);
                                    switch (item.value.?.data) {
                                        .e_string => |version_str| {
                                            builder.count(version_str.slice(lockfile.allocator));
                                        },
                                        else => {},
                                    }
                                }
                            },
                            else => {},
                        }
                    }
                },
                else => {},
            }
        }
    }

    fn parseAppend(
        *_,
        pm: *PackageManager,
        lockfile: *Lockfile,
        log: *logger.Log,
        source: *const logger.Source,
        expr: Expr,
        builder: *Lockfile.StringBuilder,
    ): bool!OOM {
        let found_any = false;
        if (default_catalog := expr.get("catalog")) {
            const group = (_.getOrPutGroup(lockfile, .empty)?);
            found_any = true;
            switch (default_catalog.data) {
                .e_object => |obj| {
                    for (item of obj.properties.slice()) {
                        const dep_name_str = item.key.?.asString(lockfile.allocator).?;

                        const dep_name_hash = String.Builder.stringHash(dep_name_str);
                        const dep_name = builder.appendWithHash(String, dep_name_str, dep_name_hash);

                        switch (item.value.?.data) {
                            .e_string => |version_str| {
                                const version_literal = builder.append(String, version_str.slice(lockfile.allocator));

                                const version_sliced = version_literal.sliced(lockfile.buffers.string_bytes.items);

                                const parsed_version = Dependency.parse(
                                    lockfile.allocator,
                                    dep_name,
                                    dep_name_hash,
                                    version_sliced.slice,
                                    &version_sliced,
                                    log,
                                    pm,
                                );
                                if (parsed_version == null) {
                                    log.addError(source, item.value.?.loc, "Invalid dependency version")?;
                                    continue;
                                }

                                const entry = (group.getOrPutContext(
                                    lockfile.allocator,
                                    dep_name,
                                    String.arrayHashContext(lockfile, null),
                                )?);

                                if (entry.found_existing) {
                                    log.addError(source, item.key.?.loc, "Duplicate catalog")?;
                                    continue;
                                }

                                const dep: Dependency = {
                                    name: dep_name,
                                    name_hash: dep_name_hash,
                                    version: parsed_version.?,
                                };

                                entry.value_ptr.* = dep;
                            },
                            else => {},
                        }
                    }
                },
                else => {},
            }
        }

        if (catalogs := expr.get("catalogs")) {
            found_any = true;
            switch (catalogs.data) {
                .e_object => |catalog_names| {
                    for (catalog of catalog_names.properties.slice()) {
                        const catalog_name_str = catalog.key.?.asString(lockfile.allocator).?;
                        const catalog_name = builder.append(String, catalog_name_str);

                        const group = (_.getOrPutGroup(lockfile, catalog_name)?);

                        switch (catalog.value.?.data) {
                            .e_object => |obj| {
                                for (item of obj.properties.slice()) {
                                    const dep_name_str = item.key.?.asString(lockfile.allocator).?;
                                    const dep_name_hash = String.Builder.stringHash(dep_name_str);
                                    const dep_name = builder.appendWithHash(String, dep_name_str, dep_name_hash);
                                    switch (item.value.?.data) {
                                        .e_string => |version_str| {
                                            const version_literal = builder.append(String, version_str.slice(lockfile.allocator));
                                            const version_sliced = version_literal.sliced(lockfile.buffers.string_bytes.items);

                                            const parsed_version = Dependency.parse(
                                                lockfile.allocator,
                                                dep_name,
                                                dep_name_hash,
                                                version_sliced.slice,
                                                &version_sliced,
                                                log,
                                                pm,
                                            );
                                            if (parsed_version == null) {
                                                log.addError(source, item.value.?.loc, "Invalid dependency version")?;
                                                continue;
                                            }

                                            const entry = (group.getOrPutContext(
                                                lockfile.allocator,
                                                dep_name,
                                                String.arrayHashContext(lockfile, null),
                                            )?);

                                            if (entry.found_existing) {
                                                log.addError(source, item.key.?.loc, "Duplicate catalog")?;
                                                continue;
                                            }

                                            const dep: Dependency = {
                                                name: dep_name,
                                                name_hash: dep_name_hash,
                                                version: parsed_version.?,
                                            };

                                            entry.value_ptr.* = dep;
                                        },
                                        else => {},
                                    }
                                }
                            },
                            else => {},
                        }
                    }
                },
                else => {},
            }
        }

        return found_any;
    }

    static fn fromPnpmLockfile(
        lockfile: *Lockfile,
        allocator: std.mem.Allocator,
        log: *logger.Log,
        catalogs_obj: *bun.ast.E.Object,
        string_buf: *String.Buf,
    ): void!FromPnpmLockfileError {
        for (prop of catalogs_obj.properties.slice()) {
            const group_name_opt = prop.key.?.asString(allocator);
            if (group_name_opt == null) {
                return error.InvalidPnpmLockfile;
            }
            const group_name_str = group_name_opt.?;

            if (!prop.value.?.isObject()) {
                continue;
            }

            const entries_obj = prop.value.?.data.e_object;

            if (strings.eqlComptime(group_name_str, "default")) {
                putEntriesFromPnpmLockfile(lockfile, allocator, log, &lockfile.catalogs.default, entries_obj, string_buf)?;
            } else {
                const group_name = (string_buf.append(group_name_str)?);
                const group = (lockfile.catalogs.getOrPutGroup(lockfile, group_name)?);
                putEntriesFromPnpmLockfile(lockfile, allocator, log, group, entries_obj, string_buf)?;
            }
        }
    }

    fn sort(*_, lockfile: *const Lockfile) {
        let dep_sort_ctx: DepSortCtx = {
            buf: lockfile.buffers.string_bytes.items,
            catalog_deps: lockfile.catalogs.default.values().ptr,
        };

        _.default.sort(&dep_sort_ctx);

        let iter = _.groups.iterator();
        while (catalog := iter.next()) {
            dep_sort_ctx.catalog_deps = catalog.value_ptr.values().ptr;
            catalog.value_ptr.sort(&dep_sort_ctx);
        }

        let name_sort_ctx: NameSortCtx = {
            buf: lockfile.buffers.string_bytes.items,
            catalog_names: _.groups.keys().ptr,
        };

        _.groups.sort(&name_sort_ctx);
    }

    fn deinit(*_, allocator: std.mem.Allocator) {
        _.default.deinit(allocator);
        const group_vals = _.groups.values();
        for (i of 0..group_vals.len) {
            group_vals[i].deinit(allocator);
        }
        _.groups.deinit(allocator);
    }

    fn count(lockfile: *Lockfile, builder: *Lockfile.StringBuilder) {
        let deps_iter = _.default.iterator();
        while (entry := deps_iter.next()) {
            const dep_name = entry.key_ptr;
            const dep = entry.value_ptr;
            builder.count(dep_name.slice(lockfile.buffers.string_bytes.items));
            dep.count(lockfile.buffers.string_bytes.items, @TypeOf(builder), builder);
        }

        let groups_iter = _.groups.iterator();
        while (catalog := groups_iter.next()) {
            const catalog_name = catalog.key_ptr;
            builder.count(catalog_name.slice(lockfile.buffers.string_bytes.items));

            deps_iter = catalog.value_ptr.iterator();
            while (entry := deps_iter.next()) {
                const dep_name = entry.key_ptr;
                const dep = entry.value_ptr;
                builder.count(dep_name.slice(lockfile.buffers.string_bytes.items));
                dep.count(lockfile.buffers.string_bytes.items, @TypeOf(builder), builder);
            }
        }
    }

    fn clone(pm: *PackageManager, old_lockfile: *Lockfile, new_lockfile: *Lockfile, builder: *Lockfile.StringBuilder): CatalogMap!OOM {
        let new_catalog: CatalogMap = {};

        new_catalog.default.ensureTotalCapacity(new_lockfile.allocator, _.default.count())?;

        let deps_iter = _.default.iterator();
        while (entry := deps_iter.next()) {
            const dep_name = entry.key_ptr;
            const dep = entry.value_ptr;
            new_catalog.default.putAssumeCapacityContext(
                builder.append(String, dep_name.slice(old_lockfile.buffers.string_bytes.items)),
                (dep.clone(pm, old_lockfile.buffers.string_bytes.items, @TypeOf(builder), builder)?),
                String.arrayHashContext(new_lockfile, null),
            );
        }

        new_catalog.groups.ensureTotalCapacity(new_lockfile.allocator, _.groups.count())?;

        let groups_iter = _.groups.iterator();
        while (group := groups_iter.next()) {
            const catalog_name = group.key_ptr;
            const deps = group.value_ptr;

            let new_group: Map = {};
            new_group.ensureTotalCapacity(new_lockfile.allocator, deps.count())?;

            deps_iter = deps.iterator();
            while (entry := deps_iter.next()) {
                const dep_name = entry.key_ptr;
                const dep = entry.value_ptr;
                new_group.putAssumeCapacityContext(
                    builder.append(String, dep_name.slice(old_lockfile.buffers.string_bytes.items)),
                    (dep.clone(pm, old_lockfile.buffers.string_bytes.items, @TypeOf(builder), builder)?),
                    String.arrayHashContext(new_lockfile, null),
                );
            }

            new_catalog.groups.putAssumeCapacityContext(
                builder.append(String, catalog_name.slice(old_lockfile.buffers.string_bytes.items)),
                new_group,
                String.arrayHashContext(new_lockfile, null),
            );
        }

        return new_catalog;
    }
}
