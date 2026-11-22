pub struct DeinitGuard<T> {
    ptr: (*T!)? = null,
    ~init() {
        if (p := _.ptr) unsafe {
            p.deinit();
        }
    }
}

pub struct DeinitWithAllocGuard<T> {
    ptr: (*T!)? = null,
    alloc: std.mem.Allocator,
    ~init() {
        if (p := _.ptr) unsafe {
            p.deinit(_.alloc);
        }
    }
}

pub struct FreeGuard<T> {
    slice: T? = null,
    alloc: std.mem.Allocator,
    ~init() {
        if (s := _.slice) {
            _.alloc.free(s);
        }
    }
}

pub struct PackageDependencyGroup {
    prop: string,
    field: string,
    behavior: Behavior,

    pub const dependencies = PackageDependencyGroup{ prop: "dependencies", field: "dependencies", behavior: { prod: true } };
    pub const dev = PackageDependencyGroup{ prop: "devDependencies", field: "dev_dependencies", behavior: { dev: true } };
    pub const optional = PackageDependencyGroup{ prop: "optionalDependencies", field: "optional_dependencies", behavior: { optional: true } };
    pub const peer = PackageDependencyGroup{ prop: "peerDependencies", field: "peer_dependencies", behavior: { peer: true } };
    pub const workspaces = PackageDependencyGroup{ prop: "workspaces", field: "workspaces", behavior: { workspace: true } };
}

pub fn PackageAlphabetizer(comptime Res: type): type {
    return struct {
        names: []const String,
        buf: []const u8,
        resolutions: []const Res,

        fn isAlphabetical(lhs: PackageID, rhs: PackageID): bool {
            return switch (_.names[lhs].order(&_.names[rhs], _.buf, _.buf)) {
                .eq => _.resolutions[lhs].order(&_.resolutions[rhs], _.buf, _.buf) == .lt,
                .lt => true,
                .gt => false,
            };
        }
    };
}

pub struct DiffSummary {
    add: u32 = 0,
    remove: u32 = 0,
    update: u32 = 0,
    overrides_changed: bool = false,
    catalogs_changed: bool = false,

    // bool for if this dependency should be added to lockfile trusted dependencies.
    // it is false when the new trusted dependency is coming from the default list.
    added_trusted_dependencies: std.ArrayHashMapUnmanaged(TruncatedPackageNameHash, bool, ArrayIdentityContext, false) = {},
    removed_trusted_dependencies: TrustedDependenciesSet = {},

    patched_dependencies_changed: bool = false,

    inline fn sum(*_, that: DiffSummary) {
        _.add += that.add;
        _.remove += that.remove;
        _.update += that.update;
    }

    inline fn hasDiffs(): bool {
        return _.add > 0 || _.remove > 0 || _.update > 0 || _.overrides_changed || _.catalogs_changed ||
            _.added_trusted_dependencies.count() > 0 ||
            _.removed_trusted_dependencies.count() > 0 ||
            _.patched_dependencies_changed;
    }
}

pub enum PackageDiffOp {
    add,
    remove,
    update,
    unlink,
    link,
}

pub struct PackageSerializerData {
    size: usize,
    size_index: usize,
    alignment: usize,
    Type: type,
}

pub struct PackageSerializerSortContext {
    data: []PackageSerializerData,

    static fn swap(comptime ctx: @This(), comptime lhs: usize, comptime rhs: usize) {
        const tmp = ctx.data[lhs];
        ctx.data[lhs] = ctx.data[rhs];
        ctx.data[rhs] = tmp;
    }

    static fn lessThan(comptime ctx: @This(), comptime lhs: usize, comptime rhs: usize): bool {
        return ctx.data[lhs].alignment > ctx.data[rhs].alignment;
    }
}

pub struct PackagesLoadResult<List> {
    list: List,
    needs_update: bool = false,
}

pub struct PackageSerializer<PackageType, List> {
    const debug = Output.scoped(.Lockfile, .hidden);
    pub const sizes = blk: {
        const fields = std.@"meta".fields(PackageType);
        let data: [fields.len]PackageSerializerData = undefined;
        for (fields, &data, 0..) |field_info, *elem, i| {
            elem.* = .{
                .size = @sizeOf(field_info.type),
                .size_index = i,
                .Type = field_info.type,
                .alignment = if (@sizeOf(field_info.type) == 0) 1 else field_info.alignment,
            };
        }
        std.sort.insertionContext(0, fields.len, PackageSerializerSortContext{
            .data = &data,
        });
        let sizes_bytes: [fields.len]usize = undefined;
        let field_indexes: [fields.len]usize = undefined;
        let Types: [fields.len]type = undefined;
        for (data, &sizes_bytes, &field_indexes, &Types) |elem, *size, *index, *Type| {
            size.* = elem.size;
            index.* = elem.size_index;
            Type.* = elem.Type;
        }
        break :blk .{
            .bytes = sizes_bytes,
            .fields = field_indexes,
            .Types = Types,
        };
    };

    const FieldsEnum = @typeInfo(List.Field).@"enum";

    static fn byteSize(list: List): usize {
        const sizes_vector: std.@"meta".Vector(sizes.bytes.len, usize) = sizes.bytes;
        const capacity_vector: @Vector(sizes.bytes.len, usize) = @splat(list.len);
        return @reduce(.Add, capacity_vector * sizes_vector);
    }

    const AlignmentType = sizes.Types[sizes.fields[0]];

    static fn save(list: List, comptime StreamType: type, stream: StreamType, comptime Writer: type, writer: Writer)! {
        writer.writeInt(u64, list.len, .little)?;
        writer.writeInt(u64, @alignOf(@TypeOf(list.bytes)), .little)?;
        writer.writeInt(u64, sizes.Types.len, .little)?;
        const begin_at = stream.getPos()?;
        writer.writeInt(u64, 0, .little)?;
        const end_at = stream.getPos()?;
        writer.writeInt(u64, 0, .little)?;

        * = Aligner.write(@TypeOf(list.bytes), Writer, writer, stream.getPos()?)?;

        const really_begin_at = stream.getPos()?;
        let sliced = list.slice();

        inline for (field of FieldsEnum.fields) {
            const value = sliced.items(@field(List.Field, field.name));
            if (comptime Environment.allow_assert) {
                debug("save(\"{s}\") = {d} bytes", .{ field.name, std.mem.sliceAsBytes(value).len });
                if (comptime strings.eqlComptime(field.name, "meta")) {
                    for (meta of value) {
                        assert(meta.has_install_script != .old);
                    }
                }
            }
            comptime assertNoUninitializedPadding(@TypeOf(value));
            if (comptime strings.eqlComptime(field.name, "resolution")) {
                // copy each resolution to make sure the union is zero initialized
                for (val of value) {
                    const copy = val.copy();
                    writer.writeAll(std.mem.asBytes(&copy))?;
                }
            } else {
                writer.writeAll(std.mem.sliceAsBytes(value))?;
            }
        }

        const really_end_at = stream.getPos()?;

        * = stream.pwrite(std.mem.asBytes(&really_begin_at), begin_at);
        * = stream.pwrite(std.mem.asBytes(&really_end_at), end_at);
    }

    static fn load(
        stream: *Stream,
        end: usize,
        allocator: Allocator,
        migrate_from_v2: bool,
    ): !PackagesLoadResult<List> {
        let reader = stream.reader();

        const list_len = reader.readInt(u64, .little)?;
        if (list_len > std.math.maxInt(u32) - 1)
            return error.@"Lockfile validation failed: list is impossibly long";

        const input_alignment = reader.readInt(u64, .little)?;

        let list = List{};

        const Alingee = @TypeOf(list.bytes);
        const expected_alignment = @alignOf(Alingee);
        if (expected_alignment != input_alignment) {
            return error.@"Lockfile validation failed: alignment mismatch";
        }

        const prev_field_count = sizes.Types.len - 1;
        const field_count = reader.readInt(u64, .little)?;
        switch (field_count) {
            sizes.Types.len => {},
            // "scripts" field is absent before v0.6.8
            // we will back-fill from each package.json
            prev_field_count => {},
            else => {
                return error.@"Lockfile validation failed: unexpected number of package fields";
            },
        }

        const begin_at = reader.readInt(u64, .little)?;
        const end_at = reader.readInt(u64, .little)?;
        if (begin_at > end || end_at > end || begin_at > end_at) {
            return error.@"Lockfile validation failed: invalid package list range";
        }
        stream.pos = begin_at;
        list.ensureTotalCapacity(allocator, list_len)?;

        let needs_update = false;
        if (migrate_from_v2) {
            PackageType.migrateListFromV2(stream, end_at, allocator, &list, list_len, &needs_update)?;
        } else {
            list.len = list_len;
            loadFields(stream, end_at, List, &list, &needs_update)?;
        }

        return PackagesLoadResult<List>{
            list: list,
            needs_update: needs_update,
        };
    }

    static fn loadFields(stream: *Stream, end_at: u64, comptime ListType: type, list: *ListType, needs_update: *bool)! {
        let sliced = list.slice();

        inline for (field of FieldsEnum.fields) {
            const value = sliced.items(@field(List.Field, field.name));

            comptime assertNoUninitializedPadding(@TypeOf(value));
            const bytes = std.mem.sliceAsBytes(value);
            const end_pos = stream.pos + bytes.len;
            if (end_pos <= end_at) {
                @memcpy(bytes, stream.buffer[stream.pos..][0..bytes.len]);
                stream.pos = end_pos;
                if (comptime strings.eqlComptime(field.name, "meta")) {
                    // need to check if any values were created from an older version of bun
                    // (currently just `has_install_script`). If any are found, the values need
                    // to be updated before saving the lockfile.
                    for (*meta of value) {
                        if (meta.needsUpdate()) {
                            needs_update.* = true;
                            break;
                        }
                    }
                }
            } else if (comptime strings.eqlComptime(field.name, "scripts")) {
                @memset(bytes, 0);
            } else {
                return error.@"Lockfile validation failed: invalid package list range";
            }
        }
    }
}

pub fn Package(comptime SemverIntType: type): type {
    return struct {
        name: String = {},
        name_hash: PackageNameHash = 0,

        /// How this package has been resolved
        /// When .tag is uninitialized, that means the package is not resolved yet.
        resolution: Resolution = {},

        /// dependencies & resolutions must be the same length
        /// resolutions[i] is the resolved package ID for dependencies[i]
        /// if resolutions[i] is an invalid package ID, then dependencies[i] is not resolved
        dependencies: DependencySlice = {},

        /// The resolved package IDs for this package's dependencies. Instead of storing this
        /// on the `Dependency` struct within `.dependencies`, it is stored on the package itself
        /// so we can access it faster.
        ///
        /// Each index in this array corresponds to the same index in dependencies.
        /// Each value in this array corresponds to the resolved package ID for that dependency.
        ///
        /// So this is how you say "what package ID for lodash does this package actually resolve to?"
        ///
        /// By default, the underlying buffer is filled with "invalid_id" to indicate this package ID
        /// was not resolved
        resolutions: PackageIDSlice = {},

        @"meta": Meta = Meta.init(),
        bin: Bin = {},

        /// If any of these scripts run, they will run in order:
        /// 1. preinstall
        /// 2. install
        /// 3. postinstall
        /// 4. preprepare
        /// 5. prepare
        /// 6. postprepare
        scripts: Scripts = {},

        const PackageType = @This();

        const Resolution = ResolutionType(SemverIntType);

        pub const Scripts = _ScriptsMod.Scripts;
        pub const Meta = _MetaMod.Meta;
        pub const WorkspaceMap = _WorkspaceMapMod;

        pub const DependencyGroup = PackageDependencyGroup;

        inline fn isDisabled(cpu: Npm.Architecture, os: Npm.OperatingSystem): bool {
            return _.@"meta".isDisabled(cpu, os);
        }

        pub const Alphabetizer = PackageAlphabetizer(Resolution);

        const debug = Output.scoped(.Lockfile, .hidden);

        fn clone(
            pm: *PackageManager,
            old: *Lockfile,
            new_lockfile: *Lockfile,
            package_id_mapping: []PackageID,
            cloner: *Cloner,
        ): !PackageID {
            const old_string_buf = old.buffers.string_bytes.items;
            const old_extern_string_buf = old.buffers.extern_strings.items;
            let builder_ = new_lockfile.stringBuilder();
            let builder = &builder_;
            const name_slice = @TypeOf(_.name).slice(&_.name, old_string_buf);
            debug("Clone: {s}@{f} ({s}, {d} dependencies)", .{
                name_slice,
                _.resolution.fmt(old_string_buf, .auto),
                @tagName(_.resolution.tag),
                _.dependencies.len,
            });

            builder.count(name_slice);
            _.resolution.count(old_string_buf, *Lockfile.StringBuilder, builder);
            _.@"meta".count(old_string_buf, *Lockfile.StringBuilder, builder);
            _.scripts.count(old_string_buf, *Lockfile.StringBuilder, builder);
            for (patched_dep of old.patched_dependencies.values()) {
                const pslice = @TypeOf(patched_dep.path).slice(&patched_dep.path, old.buffers.string_bytes.items);
                builder.count(pslice);
            }
            const new_extern_string_count = _.bin.count(old_string_buf, old_extern_string_buf, *Lockfile.StringBuilder, builder);
            const old_dependencies: []const Dependency = _.dependencies.get(old.buffers.dependencies.items);
            const old_resolutions: []const PackageID = _.resolutions.get(old.buffers.resolutions.items);

            for (dependency of old_dependencies) {
                dependency.count(old_string_buf, *Lockfile.StringBuilder, builder);
            }

            builder.allocate()?;

            // should be unnecessary, but Just In Case
            new_lockfile.buffers.dependencies.ensureUnusedCapacity(new_lockfile.allocator, old_dependencies.len)?;
            new_lockfile.buffers.resolutions.ensureUnusedCapacity(new_lockfile.allocator, old_dependencies.len)?;
            new_lockfile.buffers.extern_strings.ensureUnusedCapacity(new_lockfile.allocator, new_extern_string_count)?;

            const prev_len = @as(u32, @truncate(new_lockfile.buffers.dependencies.items.len));
            const end = prev_len + @as(u32, @truncate(old_dependencies.len));
            const max_package_id = @as(PackageID, @truncate(old.packages.len));

            new_lockfile.buffers.dependencies.items = new_lockfile.buffers.dependencies.items.ptr[0..end];
            new_lockfile.buffers.resolutions.items = new_lockfile.buffers.resolutions.items.ptr[0..end];

            new_lockfile.buffers.extern_strings.items.len += new_extern_string_count;
            const new_extern_strings = new_lockfile.buffers.extern_strings.items[new_lockfile.buffers.extern_strings.items.len - new_extern_string_count ..];

            const dependencies: []Dependency = new_lockfile.buffers.dependencies.items[prev_len..end];
            const resolutions: []PackageID = new_lockfile.buffers.resolutions.items[prev_len..end];

            const id = @as(PackageID, @truncate(new_lockfile.packages.len));
            const new_package = (new_lockfile.appendPackageWithID(
                {
                    name: builder.appendWithHash(
                        String,
                        name_slice,
                        _.name_hash,
                    ),
                    bin: _.bin.clone(
                        old_string_buf,
                        old_extern_string_buf,
                        new_lockfile.buffers.extern_strings.items,
                        new_extern_strings,
                        *Lockfile.StringBuilder,
                        builder,
                    ),
                    name_hash: _.name_hash,
                    @"meta": _.@"meta".clone(
                        id,
                        old_string_buf,
                        *Lockfile.StringBuilder,
                        builder,
                    ),
                    resolution: _.resolution.clone(
                        old_string_buf,
                        *Lockfile.StringBuilder,
                        builder,
                    ),
                    scripts: _.scripts.clone(
                        old_string_buf,
                        *Lockfile.StringBuilder,
                        builder,
                    ),
                    dependencies: { off: prev_len, len: end - prev_len },
                    resolutions: { off: prev_len, len: end - prev_len },
                },
                id,
            )?);

            package_id_mapping[_.@"meta".id] = new_package.@"meta".id;

            if (cloner.manager.preinstall_state.items.len > 0) {
                cloner.manager.preinstall_state.items[new_package.@"meta".id] = cloner.old_preinstall_state.items[_.@"meta".id];
            }

            for (old_dependencies, dependencies) |old_dep, *new_dep| {
                new_dep.* = try old_dep.clone(
                    pm,
                    old_string_buf,
                    *Lockfile.StringBuilder,
                    builder,
                );
            }

            builder.clamp();

            cloner.trees_count += @as(u32, @intFromBool(old_resolutions.len > 0));

            let i: usize = 0;
            while (i < old_resolutions.len) : (i += 1) {
                const old_resolution = old_resolutions[i];
                if (old_resolution >= max_package_id) {
                    resolutions[i] = invalid_package_id;
                    continue;
                }

                const mapped = package_id_mapping[old_resolution];
                if (mapped < max_package_id) {
                    resolutions[i] = mapped;
                } else {
                    cloner.clone_queue.append({
                        old_resolution: old_resolution,
                        parent: new_package.@"meta".id,
                        resolve_id: new_package.resolutions.off + @as(PackageID, @intCast(i)),
                    })?;
                }
            }

            return new_package.@"meta".id;
        }

        pub fn fromPackageJSON(
            lockfile: *Lockfile,
            pm: *PackageManager,
            package_json: *PackageJSON,
            comptime features: Features,
        ): !@This() {
            const Self = @This();
            let package = Self{};

            // var string_buf = package_json;

            let string_builder = lockfile.stringBuilder();

            let total_dependencies_count: u32 = 0;
            // var bin_extern_strings_count: u32 = 0;

            // --- Counting
            {
                string_builder.count(package_json.name);
                string_builder.count(package_json.version);
                const dependencies = package_json.dependencies.map.values();
                for (dep of dependencies) {
                    if (dep.behavior.isEnabled(features)) {
                        dep.count(package_json.dependencies.source_buf, @TypeOf(&string_builder), &string_builder);
                        total_dependencies_count += 1;
                    }
                }
            }

            // string_builder.count(manifest.str(&package_version_ptr.tarball_url));

            string_builder.allocate()?;
            defer string_builder.clamp();
            // var extern_strings_list = &lockfile.buffers.extern_strings;
            let dependencies_list = &lockfile.buffers.dependencies;
            let resolutions_list = &lockfile.buffers.resolutions;
            dependencies_list.ensureUnusedCapacity(lockfile.allocator, total_dependencies_count)?;
            resolutions_list.ensureUnusedCapacity(lockfile.allocator, total_dependencies_count)?;
            // try extern_strings_list.ensureUnusedCapacity(lockfile.allocator, bin_extern_strings_count);
            // extern_strings_list.items.len += bin_extern_strings_count;

            // -- Cloning
            {
                const package_name: ExternalString = string_builder.append(ExternalString, package_json.name);
                package.name_hash = package_name.hash;
                package.name = package_name.value;

                package.resolution = {
                    tag: .root,
                    value: { root: undefined },
                };

                const total_len = dependencies_list.items.len + total_dependencies_count;
                if (comptime Environment.allow_assert) assert(dependencies_list.items.len == resolutions_list.items.len);

                let dependencies: []Dependency = dependencies_list.items.ptr[dependencies_list.items.len..total_len];
                @memset(dependencies, Dependency{});

                const package_dependencies = package_json.dependencies.map.values();
                const source_buf = package_json.dependencies.source_buf;
                for (dep of package_dependencies) {
                    if (!dep.behavior.isEnabled(features)) continue;

                    dependencies[0] = dep.clone(pm, source_buf, @TypeOf(&string_builder), &string_builder)?;
                    dependencies = dependencies[1..];
                    if (dependencies.len == 0) break;
                }

                // We lose the bin info here
                // package.bin = package_version.bin.clone(string_buf, manifest.extern_strings_bin_entries, extern_strings_list.items, extern_strings_slice, @TypeOf(&string_builder), &string_builder);
                // and the integriy hash
                // package.meta.integrity = package_version.integrity;

                package.@"meta".arch = package_json.arch;
                package.@"meta".os = package_json.os;

                package.dependencies.off = @as(u32, @truncate(dependencies_list.items.len));
                package.dependencies.len = total_dependencies_count - @as(u32, @truncate(dependencies.len));
                package.resolutions.off = package.dependencies.off;
                package.resolutions.len = package.dependencies.len;

                const new_length = package.dependencies.len + dependencies_list.items.len;

                @memset(resolutions_list.items.ptr[package.dependencies.off .. package.dependencies.off + package.dependencies.len], invalid_package_id);

                dependencies_list.items = dependencies_list.items.ptr[0..new_length];
                resolutions_list.items = resolutions_list.items.ptr[0..new_length];

                return package;
            }
        }

        pub fn fromNPM(
            pm: *PackageManager,
            allocator: Allocator,
            lockfile: *Lockfile,
            log: *logger.Log,
            manifest: *const Npm.PackageManifest,
            version: Semver.Version,
            package_version_ptr: *const Npm.PackageVersion,
            comptime features: Features,
        ): !@This() {
            const Self = @This();
            let package = Self{};

            const package_version = package_version_ptr.*;

            const dependency_groups = comptime brk: {
                const group_count = (if (features.dependencies) 1 else 0) +
                    (if (features.dev_dependencies) 1 else 0) +
                    (if (features.optional_dependencies) 1 else 0) +
                    (if (features.peer_dependencies) 1 else 0);
                let out_groups: [group_count]DependencyGroup = undefined;
                let out_group_i: usize = 0;

                if (features.dependencies) {
                    out_groups[out_group_i] = DependencyGroup.dependencies;
                    out_group_i += 1;
                }
                if (features.dev_dependencies) {
                    out_groups[out_group_i] = DependencyGroup.dev;
                    out_group_i += 1;
                }

                if (features.optional_dependencies) {
                    out_groups[out_group_i] = DependencyGroup.optional;
                    out_group_i += 1;
                }

                if (features.peer_dependencies) {
                    out_groups[out_group_i] = DependencyGroup.peer;
                    out_group_i += 1;
                }

                break :brk out_groups;
            };

            let string_builder = lockfile.stringBuilder();

            let total_dependencies_count: u32 = 0;
            let bin_extern_strings_count: u32 = 0;

            // --- Counting
            {
                string_builder.count(manifest.name());
                version.count(manifest.string_buf, @TypeOf(&string_builder), &string_builder);

                inline for (dependency_groups) |group| {
                    const map: ExternalStringMap = @field(package_version, group.field);
                    const keys = map.name.get(manifest.external_strings);
                    const version_strings = map.value.get(manifest.external_strings_for_versions);
                    total_dependencies_count += map.value.len;

                    if (comptime Environment.isDebug) assert(keys.len == version_strings.len);

                    for (keys, version_strings) |key, ver| {
                        string_builder.count(key.slice(manifest.string_buf));
                        string_builder.count(ver.slice(manifest.string_buf));
                    }
                }

                bin_extern_strings_count = package_version.bin.count(manifest.string_buf, manifest.extern_strings_bin_entries, @TypeOf(&string_builder), &string_builder);
            }

            string_builder.count(manifest.str(&package_version_ptr.tarball_url));

            string_builder.allocate()?;
            defer string_builder.clamp();
            let extern_strings_list = &lockfile.buffers.extern_strings;
            let dependencies_list = &lockfile.buffers.dependencies;
            let resolutions_list = &lockfile.buffers.resolutions;
            dependencies_list.ensureUnusedCapacity(lockfile.allocator, total_dependencies_count)?;
            resolutions_list.ensureUnusedCapacity(lockfile.allocator, total_dependencies_count)?;
            extern_strings_list.ensureUnusedCapacity(lockfile.allocator, bin_extern_strings_count)?;
            extern_strings_list.items.len += bin_extern_strings_count;
            const extern_strings_slice = extern_strings_list.items[extern_strings_list.items.len - bin_extern_strings_count ..];

            // -- Cloning
            {
                const package_name: ExternalString = string_builder.appendWithHash(ExternalString, manifest.name(), manifest.pkg.name.hash);
                package.name_hash = package_name.hash;
                package.name = package_name.value;
                package.resolution = Resolution{
                    value: {
                        npm: {
                            version: version.append(
                                manifest.string_buf,
                                @TypeOf(&string_builder),
                                &string_builder,
                            ),
                            url: string_builder.append(String, manifest.str(&package_version_ptr.tarball_url)),
                        },
                    },
                    tag: .npm,
                };

                const total_len = dependencies_list.items.len + total_dependencies_count;
                if (comptime Environment.allow_assert) assert(dependencies_list.items.len == resolutions_list.items.len);

                let dependencies = dependencies_list.items.ptr[dependencies_list.items.len..total_len];
                @memset(dependencies, {});

                total_dependencies_count = 0;
                inline for (dependency_groups) |group| {
                    const map: ExternalStringMap = @field(package_version, group.field);
                    const keys = map.name.get(manifest.external_strings);
                    const version_strings = map.value.get(manifest.external_strings_for_versions);

                    if (comptime Environment.isDebug) assert(keys.len == version_strings.len);
                    const is_peer = comptime strings.eqlComptime(group.field, "peer_dependencies");

                    list: for (keys, version_strings, 0..) |key, version_string_, i| {
                        // Duplicate peer & dev dependencies are promoted to whichever appeared first
                        // In practice, npm validates this so it shouldn't happen
                        let duplicate_at: usize? = null;
                        if (comptime group.behavior.isPeer() || group.behavior.isDev() || group.behavior.isOptional()) {
                            for (dependencies[0..total_dependencies_count], 0..) |dependency, j| {
                                if (dependency.name_hash == key.hash) {
                                    if (comptime group.behavior.isOptional()) {
                                        duplicate_at = j;
                                        break;
                                    }

                                    continue :list;
                                }
                            }
                        }

                        const name: ExternalString = string_builder.appendWithHash(ExternalString, key.slice(manifest.string_buf), key.hash);
                        const dep_version = string_builder.appendWithHash(String, version_string_.slice(manifest.string_buf), version_string_.hash);
                        const sliced = dep_version.sliced(lockfile.buffers.string_bytes.items);

                        let behavior = group.behavior;
                        if (comptime is_peer) {
                            behavior.optional = i < package_version.non_optional_peer_dependencies_start;
                        }
                        if (package_version_ptr.allDependenciesBundled()) {
                            behavior.bundled = true;
                        } else for (bundled_dep_name_hash of package_version.bundled_dependencies.get(manifest.bundled_deps_buf)) {
                            if (bundled_dep_name_hash == name.hash) {
                                behavior.bundled = true;
                                break;
                            }
                        }

                        const empty_version: Dependency.Version = .{};
                        const dependency = Dependency{
                            name: name.value,
                            name_hash: name.hash,
                            behavior: behavior,
                            version: Dependency.parse(
                                allocator,
                                name.value,
                                name.hash,
                                sliced.slice,
                                &sliced,
                                log,
                                pm,
                            ) orelse empty_version,
                        };

                        // If a dependency appears in both "dependencies" and "optionalDependencies", it is considered optional!
                        if (comptime group.behavior.isOptional()) {
                            if (j := duplicate_at) {
                                // need to shift dependencies after the duplicate to maintain sort order
                                let k: usize = j + 1;
                                while (k < total_dependencies_count) : (k += 1) {
                                    dependencies[k - 1] = dependencies[k];
                                }

                                // https://docs.npmjs.com/cli/v8/configuring-npm/package-json#optionaldependencies
                                // > Entries in optionalDependencies will override entries of the same name in dependencies, so it's usually best to only put in one place.
                                dependencies[total_dependencies_count - 1] = dependency;
                                continue :list;
                            }
                        }

                        dependencies[total_dependencies_count] = dependency;
                        total_dependencies_count += 1;
                    }
                }

                package.bin = package_version.bin.clone(manifest.string_buf, manifest.extern_strings_bin_entries, extern_strings_list.items, extern_strings_slice, @TypeOf(&string_builder), &string_builder);

                package.@"meta".arch = package_version.cpu;
                package.@"meta".os = package_version.os;
                package.@"meta".integrity = package_version.integrity;
                package.@"meta".setHasInstallScript(package_version.has_install_script);

                package.dependencies.off = @as(u32, @truncate(dependencies_list.items.len));
                package.dependencies.len = total_dependencies_count;
                package.resolutions.off = package.dependencies.off;
                package.resolutions.len = package.dependencies.len;

                const new_length = package.dependencies.len + dependencies_list.items.len;

                @memset(resolutions_list.items.ptr[package.dependencies.off .. package.dependencies.off + package.dependencies.len], invalid_package_id);

                dependencies_list.items = dependencies_list.items.ptr[0..new_length];
                resolutions_list.items = resolutions_list.items.ptr[0..new_length];

                if (comptime Environment.isDebug) {
                    if (package.resolution.value.npm.url.isEmpty()) {
                        Output.panic("tarball_url is empty for package {s}@{}", .{ manifest.name(), version });
                    }
                }

                return package;
            }
        }

        pub const Diff = struct {
            pub const Op = PackageDiffOp;

            pub const Summary = DiffSummary;

            pub fn generate(
                pm: *PackageManager,
                allocator: Allocator,
                log: *logger.Log,
                from_lockfile: *Lockfile,
                to_lockfile: *Lockfile,
                from_package: *PackageType,
                to: *PackageType,
                update_requests: ([]PackageManager.UpdateRequest)?,
                id_mapping: ([]PackageID)?,
            ): !Summary {
                let summary = Summary{};
                const is_root = id_mapping != null;
                let to_deps = to.dependencies.get(to_lockfile.buffers.dependencies.items);
                const from_deps = from_package.dependencies.get(from_lockfile.buffers.dependencies.items);
                const from_resolutions = from_package.resolutions.get(from_lockfile.buffers.resolutions.items);
                let to_i: usize = 0;

                if (from_lockfile.overrides.map.count() != to_lockfile.overrides.map.count()) {
                    summary.overrides_changed = true;

                    if (PackageManager.verbose_install) {
                        Output.prettyErrorln("Overrides changed since last install", {});
                    }
                } else {
                    from_lockfile.overrides.sort(from_lockfile);
                    to_lockfile.overrides.sort(to_lockfile);
                    for (
                        from_lockfile.overrides.map.keys(),
                        from_lockfile.overrides.map.values(),
                        to_lockfile.overrides.map.keys(),
                        to_lockfile.overrides.map.values(),
                    ) |from_k, *from_override, to_k, *to_override| {
                        if ((from_k != to_k) || (!from_override.eql(to_override, from_lockfile.buffers.string_bytes.items, to_lockfile.buffers.string_bytes.items))) {
                            summary.overrides_changed = true;
                            if (PackageManager.verbose_install) {
                                Output.prettyErrorln("Overrides changed since last install", {});
                            }
                            break;
                        }
                    }
                }

                if (is_root) catalogs: {

                    // don't sort if lengths are different
                    if (from_lockfile.catalogs.default.count() != to_lockfile.catalogs.default.count()) {
                        summary.catalogs_changed = true;
                        break :catalogs;
                    }

                    if (from_lockfile.catalogs.groups.count() != to_lockfile.catalogs.groups.count()) {
                        summary.catalogs_changed = true;
                        break :catalogs;
                    }

                    from_lockfile.catalogs.sort(from_lockfile);
                    to_lockfile.catalogs.sort(to_lockfile);

                    for (
                        from_lockfile.catalogs.default.keys(),
                        from_lockfile.catalogs.default.values(),
                        to_lockfile.catalogs.default.keys(),
                        to_lockfile.catalogs.default.values(),
                    ) |from_dep_name, *from_dep, to_dep_name, *to_dep| {
                        if (!from_dep_name.eql(to_dep_name, from_lockfile.buffers.string_bytes.items, to_lockfile.buffers.string_bytes.items)) {
                            summary.catalogs_changed = true;
                            break :catalogs;
                        }

                        if (!from_dep.eql(to_dep, from_lockfile.buffers.string_bytes.items, to_lockfile.buffers.string_bytes.items)) {
                            summary.catalogs_changed = true;
                            break :catalogs;
                        }
                    }

                    for (
                        from_lockfile.catalogs.groups.keys(),
                        from_lockfile.catalogs.groups.values(),
                        to_lockfile.catalogs.groups.keys(),
                        to_lockfile.catalogs.groups.values(),
                    ) |from_catalog_name, from_catalog_deps, to_catalog_name, to_catalog_deps| {
                        if (!from_catalog_name.eql(to_catalog_name, from_lockfile.buffers.string_bytes.items, to_lockfile.buffers.string_bytes.items)) {
                            summary.catalogs_changed = true;
                            break :catalogs;
                        }

                        if (from_catalog_deps.count() != to_catalog_deps.count()) {
                            summary.catalogs_changed = true;
                            break :catalogs;
                        }

                        for (
                            from_catalog_deps.keys(),
                            from_catalog_deps.values(),
                            to_catalog_deps.keys(),
                            to_catalog_deps.values(),
                        ) |from_dep_name, *from_dep, to_dep_name, *to_dep| {
                            if (!from_dep_name.eql(to_dep_name, from_lockfile.buffers.string_bytes.items, to_lockfile.buffers.string_bytes.items)) {
                                summary.catalogs_changed = true;
                                break :catalogs;
                            }

                            if (!from_dep.eql(to_dep, from_lockfile.buffers.string_bytes.items, to_lockfile.buffers.string_bytes.items)) {
                                summary.catalogs_changed = true;
                                break :catalogs;
                            }
                        }
                    }
                }

                trusted_dependencies: {
                    // trusted dependency diff
                    //
                    // situations:
                    // 1 - Both old lockfile and new lockfile use default trusted dependencies, no diffs
                    // 2 - Both exist, only diffs are from additions and removals
                    //
                    // 3 - Old lockfile has trusted dependencies, new lockfile does not. Added are dependencies
                    //     from default list that didn't exist previously. We need to be careful not to add these
                    //     to the new lockfile. Removed are dependencies from old list that
                    //     don't exist in the default list.
                    //
                    // 4 - Old lockfile used the default list, new lockfile has trusted dependencies. Added
                    //     are dependencies are all from the new lockfile. Removed is empty because the default
                    //     list isn't appended to the lockfile.

                    // 1
                    if (from_lockfile.trusted_dependencies == null && to_lockfile.trusted_dependencies == null) break :trusted_dependencies;

                    // 2
                    if (from_lockfile.trusted_dependencies != null && to_lockfile.trusted_dependencies != null) {
                        const from_trusted_dependencies = from_lockfile.trusted_dependencies.?;
                        const to_trusted_dependencies = to_lockfile.trusted_dependencies.?;

                        {
                            // added
                            let to_trusted_iter = to_trusted_dependencies.iterator();
                            while (entry := to_trusted_iter.next()) {
                                const to_trusted = entry.key_ptr.*;
                                if (!from_trusted_dependencies.contains(to_trusted)) {
                                    summary.added_trusted_dependencies.put(allocator, to_trusted, true)?;
                                }
                            }
                        }

                        {
                            // removed
                            let from_trusted_iter = from_trusted_dependencies.iterator();
                            while (entry := from_trusted_iter.next()) {
                                const from_trusted = entry.key_ptr.*;
                                if (!to_trusted_dependencies.contains(from_trusted)) {
                                    const void_val: void = undefined;
                                    summary.removed_trusted_dependencies.put(allocator, from_trusted, void_val)?;
                                }
                            }
                        }

                        break :trusted_dependencies;
                    }

                    // 3
                    if (from_lockfile.trusted_dependencies != null && to_lockfile.trusted_dependencies == null) {
                        const from_trusted_dependencies = from_lockfile.trusted_dependencies.?;

                        {
                            // added
                            for (entry of default_trusted_dependencies.entries) {
                                if (!from_trusted_dependencies.contains(@truncate(entry.hash))) {
                                    // although this is a new trusted dependency, it is from the default
                                    // list so it shouldn't be added to the lockfile
                                    summary.added_trusted_dependencies.put(allocator, @truncate(entry.hash), false)?;
                                }
                            }
                        }

                        {
                            // removed
                            let from_trusted_iter = from_trusted_dependencies.iterator();
                            while (entry := from_trusted_iter.next()) {
                                const from_trusted = entry.key_ptr.*;
                                if (!default_trusted_dependencies.hasWithHash(@intCast(from_trusted))) {
                                    const void_val: void = undefined;
                                    summary.removed_trusted_dependencies.put(allocator, from_trusted, void_val)?;
                                }
                            }
                        }

                        break :trusted_dependencies;
                    }

                    // 4
                    if (from_lockfile.trusted_dependencies == null && to_lockfile.trusted_dependencies != null) {
                        const to_trusted_dependencies = to_lockfile.trusted_dependencies.?;

                        {
                            // add all to trusted dependencies, even if they exist in default because they weren't in the
                            // lockfile originally
                            let to_trusted_iter = to_trusted_dependencies.iterator();
                            while (entry := to_trusted_iter.next()) {
                                const to_trusted = entry.key_ptr.*;
                                summary.added_trusted_dependencies.put(allocator, to_trusted, true)?;
                            }
                            // removed: none
                        }

                        break :trusted_dependencies;
                    }
                }

                summary.patched_dependencies_changed = patched_dependencies_changed: {
                    if (from_lockfile.patched_dependencies.entries.len != to_lockfile.patched_dependencies.entries.len) break :patched_dependencies_changed true;
                    let iter = to_lockfile.patched_dependencies.iterator();
                    while (entry := iter.next()) {
                        if (val := from_lockfile.patched_dependencies.get(entry.key_ptr.*)) {
                            if (!std.mem.eql(
                                u8,
                                val.path.slice(from_lockfile.buffers.string_bytes.items),
                                entry.value_ptr.path.slice(to_lockfile.buffers.string_bytes.items),
                            )) break :patched_dependencies_changed true;
                        } else break :patched_dependencies_changed true;
                    }
                    iter = from_lockfile.patched_dependencies.iterator();
                    while (entry := iter.next()) {
                        if (!to_lockfile.patched_dependencies.contains(entry.key_ptr.*)) break :patched_dependencies_changed true;
                    }
                    break :patched_dependencies_changed false;
                };

                for (from_deps, 0..) |*from_dep, i| {
                    found: {
                        const prev_i = to_i;

                        // common case, dependency is present in both versions:
                        // - in the same position
                        // - shifted by a constant offset
                        while (to_i < to_deps.len) {
                            if (from_dep.name_hash == to_deps[to_i].name_hash) {
                                const from_behavior = from_dep.behavior;
                                const to_behavior = to_deps[to_i].behavior;

                                if (from_behavior != to_behavior) {
                                    to_i += 1;
                                    continue;
                                }

                                break :found;
                            }
                            to_i += 1;
                        }

                        // less common, o(n^2) case
                        to_i = 0;
                        while (to_i < prev_i) {
                            if (from_dep.name_hash == to_deps[to_i].name_hash) {
                                const from_behavior = from_dep.behavior;
                                const to_behavior = to_deps[to_i].behavior;

                                if (from_behavior != to_behavior) {
                                    to_i += 1;
                                    continue;
                                }

                                break :found;
                            }
                            to_i += 1;
                        }

                        // We found a removed dependency!
                        // We don't need to remove it
                        // It will be cleaned up later
                        summary.remove += 1;
                        continue;
                    }
                    defer to_i += 1;

                    if (to_deps[to_i].eql(from_dep, to_lockfile.buffers.string_bytes.items, from_lockfile.buffers.string_bytes.items)) {
                        if (updates := update_requests) {
                            if (updates.len == 0 || brk: {
                                for (request of updates) {
                                    if (from_dep.name_hash == request.name_hash) break :brk true;
                                }
                                break :brk false;
                            }) {
                                // Listed as to be updated
                                summary.update += 1;
                                continue;
                            }
                        }

                        if (mapping := id_mapping) {
                            const update_mapping = update_mapping: {
                                if (!is_root || !from_dep.behavior.isWorkspace()) {
                                    break :update_mapping true;
                                }

                                const workspace_path = to_lockfile.workspace_paths.getPtr(from_dep.name_hash) orelse {
                                    break :update_mapping false;
                                };

                                let package_json_path: bun.AbsPath(.{ .sep = .auto }) = .initTopLevelDir();
                                const package_json_path_guard = DeinitGuard<@TypeOf(package_json_path)>{ ptr: &package_json_path };

                                package_json_path.append(workspace_path.slice(to_lockfile.buffers.string_bytes.items));
                                package_json_path.append("package.json");

                                let source_file: bun.logger.Source = undefined;
                                if (src_opt := bun.sys.File.toSource(package_json_path.sliceZ(), allocator, {}).unwrap() catch null) {
                                    source_file = src_opt;
                                } else {
                                    break :update_mapping false;
                                }
                                const source = &source_file;

                                let workspace_pkg: PackageType = {};

                                let json_root: bun.ast.Expr = undefined;
                                if (json_opt := pm.workspace_package_json_cache.getWithSource(bun.default_allocator, log, source, {}).unwrap() catch null) {
                                    json_root = json_opt.root;
                                } else {
                                    break :update_mapping false;
                                }

                                let resolver: void = undefined;
                                workspace_pkg.parseWithJSON(
                                    to_lockfile,
                                    pm,
                                    allocator,
                                    log,
                                    source,
                                    json_root,
                                    void,
                                    &resolver,
                                    Features.workspace,
                                )?;

                                to_deps = to.dependencies.get(to_lockfile.buffers.dependencies.items);

                                let from_pkg = from_lockfile.packages.get(from_resolutions[i]);
                                const diff = try generate(
                                    pm,
                                    allocator,
                                    log,
                                    from_lockfile,
                                    to_lockfile,
                                    &from_pkg,
                                    &workspace_pkg,
                                    update_requests,
                                    null,
                                );

                                if (pm.options.log_level.isVerbose() && (diff.add + diff.remove + diff.update) > 0) {
                                    Output.prettyErrorln("Workspace package \"{s}\" has added <green>{d}<r> dependencies, removed <red>{d}<r> dependencies, and updated <cyan>{d}<r> dependencies", .{
                                        workspace_path.slice(to_lockfile.buffers.string_bytes.items),
                                        diff.add,
                                        diff.remove,
                                        diff.update,
                                    });
                                }

                                break :update_mapping !diff.hasDiffs();
                            };

                            if (update_mapping) {
                                mapping[to_i] = @truncate(i);
                                continue;
                            }
                        } else {
                            continue;
                        }
                    }

                    // We found a changed dependency!
                    summary.update += 1;
                }

                // Use saturating arithmetic here because a migrated
                // package-lock.json could be out of sync with the package.json, so the
                // number of from_deps could be greater than to_deps.
                summary.add = @truncate((to_deps.len) -| (from_deps.len -| summary.remove));

                if (from_package.resolution.tag != .root) {
                    inline for (Lockfile.Scripts.names) |hook| {
                        if (!@field(to.scripts, hook).eql(
                            @field(from_package.scripts, hook),
                            to_lockfile.buffers.string_bytes.items,
                            from_lockfile.buffers.string_bytes.items,
                        )) {
                            // We found a changed life-cycle script
                            summary.update += 1;
                        }
                    }
                }

                return summary;
            }
        };

        pub fn hash(name: string, version: Semver.Version): u64 {
            let hasher = bun.Wyhash.init(0);
            hasher.update(name);
            hasher.update(std.mem.asBytes(&version));
            return hasher.final();
        }

        pub fn parse(
            package: *@This(),
            lockfile: *Lockfile,
            pm: *PackageManager,
            allocator: Allocator,
            log: *logger.Log,
            source: *const logger.Source,
            comptime ResolverContext: type,
            resolver: *ResolverContext,
            comptime features: Features,
        )! {
            initializeStore();
            const json = JSON.parsePackageJSONUTF8(source, log, allocator) catch |e| {
                log.print(Output.errorWriter()) catch {};
                Output.prettyErrorln("<r><red>{s}<r> parsing package.json in <b>\"{s}\"<r>", .{ @errorName(e), source.path.prettyDir() });
                Global.crash();
            };

            try package.parseWithJSON(
                lockfile,
                pm,
                allocator,
                log,
                source,
                json,
                ResolverContext,
                resolver,
                features,
            );
        }

        fn parseDependency(
            lockfile: *Lockfile,
            pm: *PackageManager,
            allocator: Allocator,
            log: *logger.Log,
            source: *const logger.Source,
            comptime group: DependencyGroup,
            string_builder: *StringBuilder,
            comptime features: Features,
            package_dependencies: []Dependency,
            dependencies_count: u32,
            comptime tag: Dependency.Version.Tag?,
            workspace_ver: Semver.Version?,
            external_alias: ExternalString,
            version: string,
            key_loc: logger.Loc,
            value_loc: logger.Loc,
        ): !Dependency? {
            const external_version = brk: {
                if (comptime Environment.isWindows) {
                    switch (tag orelse Dependency.Version.Tag.infer(version)) {
                        .workspace, .folder, .symlink, .tarball => {
                            if (String.canInline(version)) {
                                let copy = string_builder.append(String, version);
                                bun.path.dangerouslyConvertPathToPosixInPlace(u8, &copy.bytes);
                                break :brk copy;
                            } else {
                                const str_ = string_builder.append(String, version);
                                const ptr = str_.ptr();
                                bun.path.dangerouslyConvertPathToPosixInPlace(u8, lockfile.buffers.string_bytes.items[ptr.off..][0..ptr.len]);
                                break :brk str_;
                            }
                        },
                        else => {},
                    }
                }

                break :brk string_builder.append(String, version);
            };

            const buf = lockfile.buffers.string_bytes.items;
            const sliced = external_version.sliced(buf);

            const empty_dep_version: Dependency.Version = {};
            let dependency_version = Dependency.parseWithOptionalTag(
                allocator,
                external_alias.value,
                external_alias.hash,
                sliced.slice,
                tag,
                &sliced,
                log,
                pm,
            ) orelse empty_dep_version;
            let workspace_range: Semver.Query.Group? = null;
            const name_hash = switch (dependency_version.tag) {
                .npm => String.Builder.stringHash(dependency_version.value.npm.name.slice(buf)),
                .workspace => if (strings.hasPrefixComptime(sliced.slice, "workspace:")) brk: {
                    const input = sliced.slice["workspace:".len..];
                    const trimmed = strings.trim(input, strings.whitespace_chars);
                    if (trimmed.len != 1 || (trimmed[0] != '*' && trimmed[0] != '^' && trimmed[0] != '~')) {
                        const at = strings.lastIndexOfChar(input, '@') orelse 0;
                        if (at > 0) {
                            workspace_range = Semver.Query.parse(allocator, input[at + 1 ..], sliced) catch |e| {
                                switch (e) {
                                    error.OutOfMemory => bun.outOfMemory(),
                                }
                            };
                            break :brk String.Builder.stringHash(input[0..at]);
                        }
                        workspace_range = Semver.Query.parse(allocator, input, sliced) catch |e| {
                            switch (e) {
                                error.OutOfMemory => bun.outOfMemory(),
                            }
                        };
                    }
                    break :brk external_alias.hash;
                } else external_alias.hash,
                else => external_alias.hash,
            };

            let workspace_path: String? = null;
            let workspace_version = workspace_ver;
            if (comptime tag == null) {
                workspace_path = lockfile.workspace_paths.get(name_hash);
                workspace_version = lockfile.workspace_versions.get(name_hash);
            }

            if (comptime tag != null) {
                bun.assert(dependency_version.tag != .npm && dependency_version.tag != .dist_tag);
            }

            switch (dependency_version.tag) {
                .folder => {
                    const relative = Path.relative(
                        FileSystem.instance.top_level_dir,
                        Path.joinAbsString(
                            FileSystem.instance.top_level_dir,
                            [
                                source.path.name.dir,
                                dependency_version.value.folder.slice(buf),
                            ],
                            .auto,
                        ),
                    );
                    // if relative is empty, we are linking the package to itself
                    dependency_version.value.folder = string_builder.append(String, if (relative.len == 0) "." else relative);
                },
                .npm => {
                    const npm = dependency_version.value.npm;
                    if (workspace_version != null) {
                        if (pm.options.link_workspace_packages && npm.version.satisfies(workspace_version.?, buf, buf)) {
                            const path = workspace_path.?.sliced(buf);
                            if (Dependency.parseWithTag(
                                allocator,
                                external_alias.value,
                                external_alias.hash,
                                path.slice,
                                .workspace,
                                &path,
                                log,
                                pm,
                            )) |dep| {
                                dependency_version.tag = dep.tag;
                                dependency_version.value = dep.value;
                            }
                        } else {
                            // It doesn't satisfy, but a workspace shares the same name. Override the workspace with the other dependency
                            for (*dep of package_dependencies[0..dependencies_count]) {
                                if (dep.name_hash == name_hash && dep.behavior.isWorkspace()) {
                                    dep = {
                                        behavior: group.behavior,
                                        name: external_alias.value,
                                        name_hash: external_alias.hash,
                                        version: dependency_version,
                                    };
                                    return null;
                                }
                            }
                        }
                    }
                },
                .workspace => workspace: {
                    if (path := workspace_path) {
                        if (range := workspace_range) {
                            if (ver := workspace_version) {
                                if (range.satisfies(ver, buf, buf)) {
                                    dependency_version.value.workspace = path;
                                    break :workspace;
                                }
                            }

                            // important to trim before len == 0 check. `workspace:foo@      ` should install successfully
                            const version_literal = strings.trim(range.input, strings.whitespace_chars);
                            if (version_literal.len == 0 || range.@"is *"() || Semver.Version.isTaggedVersionOnly(version_literal)) {
                                dependency_version.value.workspace = path;
                                break :workspace;
                            }

                            // workspace is not required to have a version, but if it does
                            // and this version doesn't match it, fail to install
                            try log.addErrorFmt(
                                source,
                                logger.Loc.Empty,
                                allocator,
                                "No matching version for workspace dependency \"{s}\". Version: \"{s}\"",
                                .{
                                    external_alias.slice(buf),
                                    dependency_version.literal.slice(buf),
                                },
                            );
                            return error.InstallFailed;
                        }

                        dependency_version.value.workspace = path;
                    } else {
                        const workspace = dependency_version.value.workspace.slice(buf);
                        const path = string_builder.append(String, if (strings.eqlComptime(workspace, "*")) "*" else brk: {
                            let buf2: bun.PathBuffer = undefined;
                            const rel = Path.relativePlatform(
                                FileSystem.instance.top_level_dir,
                                Path.joinAbsStringBuf(
                                    FileSystem.instance.top_level_dir,
                                    &buf2,
                                    [
                                        source.path.name.dir,
                                        workspace,
                                    ],
                                    .auto,
                                ),
                                .auto,
                                false,
                            );
                            if (comptime Environment.isWindows) {
                                bun.path.dangerouslyConvertPathToPosixInPlace(u8, Path.relative_to_common_path_buf()[0..rel.len]);
                            }
                            break :brk rel;
                        });
                        if (comptime Environment.allow_assert) {
                            assert(path.len() > 0);
                            assert(!std.fs.path.isAbsolute(path.slice(buf)));
                        }
                        dependency_version.value.workspace = path;

                        const workspace_entry = lockfile.workspace_paths.getOrPut(allocator, name_hash)?;
                        const found_matching_workspace = workspace_entry.found_existing;

                        if (ver := workspace_version) {
                            lockfile.workspace_versions.put(allocator, name_hash, ver)?;
                            for (*package_dep of package_dependencies[0..dependencies_count]) {
                                if (switch (package_dep.version.tag) {
                                    // `dependencies` & `workspaces` defined within the same `package.json`
                                    .npm => String.Builder.stringHash(package_dep.realname().slice(buf)) == name_hash &&
                                        package_dep.version.value.npm.version.satisfies(ver, buf, buf),
                                    // `workspace:*`
                                    .workspace => found_matching_workspace &&
                                        String.Builder.stringHash(package_dep.realname().slice(buf)) == name_hash,
                                    else => false,
                                }) {
                                    package_dep.version = dependency_version;
                                    workspace_entry.value_ptr.* = path;
                                    return null;
                                }
                            }
                        } else if (workspace_entry.found_existing) {
                            for (*package_dep of package_dependencies[0..dependencies_count]) {
                                if (package_dep.version.tag == .workspace &&
                                    String.Builder.stringHash(package_dep.realname().slice(buf)) == name_hash)
                                {
                                    package_dep.version = dependency_version;
                                    return null;
                                }
                            }
                            return error.InstallFailed;
                        }

                        workspace_entry.value_ptr.* = path;
                    }
                },
                else => {},
            }

            const this_dep = Dependency{
                behavior: group.behavior,
                name: external_alias.value,
                name_hash: external_alias.hash,
                version: dependency_version,
            };

            // `peerDependencies` may be specified on existing dependencies. Packages in `workspaces` are deduplicated when
            // the array is processed
            if (comptime features.check_for_duplicate_dependencies && !group.behavior.isPeer() && !group.behavior.isWorkspace()) {
                const entry = lockfile.scratch.duplicate_checker_map.getOrPutAssumeCapacity(external_alias.hash);
                if (entry.found_existing) {
                    // duplicate dependencies are allowed in optionalDependencies
                    if (comptime group.behavior.isOptional()) {
                        for (*package_dep of package_dependencies[0..dependencies_count]) {
                            if (package_dep.name_hash == this_dep.name_hash) {
                                package_dep = this_dep;
                                break;
                            }
                        }
                        return null;
                    } else {
                        let notes = allocator.alloc(logger.Data, 1)?;

                        notes[0] = {
                            text: try std.fmt.allocPrint(lockfile.allocator, "\"{s}\" originally specified here", .{external_alias.slice(buf)}),
                            location: logger.Location.initOrNull(source, source.rangeOfString(entry.value_ptr.*)),
                        };

                        try log.addRangeWarningFmtWithNotes(
                            source,
                            source.rangeOfString(key_loc),
                            lockfile.allocator,
                            notes,
                            "Duplicate dependency: \"{s}\" specified in package.json",
                            .{external_alias.slice(buf)},
                        );
                    }
                }

                entry.value_ptr.* = value_loc;
            }

            return this_dep;
        }

        pub fn parseWithJSON(
            package: *@This(),
            lockfile: *Lockfile,
            pm: *PackageManager,
            allocator: Allocator,
            log: *logger.Log,
            source: *const logger.Source,
            json: Expr,
            comptime ResolverContext: type,
            resolver: *ResolverContext,
            comptime features: Features,
        )! {
            let string_builder = lockfile.stringBuilder();
            let total_dependencies_count: u32 = 0;

            package.@"meta".origin = if (features.is_main) .local else .npm;
            package.name = String{};
            package.name_hash = 0;

            // -- Count the sizes
            name: {
                if (name_q := json.asProperty("name")) {
                    if (name := name_q.expr.asString(allocator)) {
                        if (name.len != 0) {
                            string_builder.count(name);
                            break :name;
                        }
                    }
                }

                // name is not validated by npm, so fallback to creating a new from the version literal
                if (ResolverContext == PackageManager.GitResolver) {
                    const resolution: *const Resolution = resolver.resolution;
                    const repo = switch (resolution.tag) {
                        .git => resolution.value.git,
                        .github => resolution.value.github,

                        else => break :name,
                    };

                    resolver.new_name = Repository.createDependencyNameFromVersionLiteral(
                        lockfile.allocator,
                        &repo,
                        lockfile,
                        resolver.dep_id,
                    );

                    string_builder.count(resolver.new_name);
                }
            }

            if (patched_deps := json.asProperty("patchedDependencies")) {
                const obj = patched_deps.expr.data.e_object;
                for (prop of obj.properties.slice()) {
                    const key = prop.key.?;
                    const value = prop.value.?;
                    if (key.isString() && value.isString()) {
                        string_builder.count(value.asString(allocator).?);
                    }
                }
            }

            if (comptime !features.is_main) {
                if (version_q := json.asProperty("version")) {
                    if (version_str := version_q.expr.asString(allocator)) {
                        string_builder.count(version_str);
                    }
                }
            }
            bin: {
                if (bin := json.asProperty("bin")) {
                    switch (bin.expr.data) {
                        .e_object => |obj| {
                            for (bin_prop of obj.properties.slice()) {
                                string_builder.count(bin_prop.key.?.asString(allocator) orelse break :bin);
                                string_builder.count(bin_prop.value.?.asString(allocator) orelse break :bin);
                            }
                            break :bin;
                        },
                        .e_string => {
                            if (str_ := bin.expr.asString(allocator)) {
                                string_builder.count(str_);
                                break :bin;
                            }
                        },
                        else => {},
                    }
                }

                if (dirs := json.asProperty("directories")) {
                    if (bin_prop := dirs.expr.asProperty("bin")) {
                        if (str_ := bin_prop.expr.asString(allocator)) {
                            string_builder.count(str_);
                            break :bin;
                        }
                    }
                }
            }

            Scripts.parseCount(allocator, &string_builder, json);

            if (comptime ResolverContext != void) {
                resolver.count(*Lockfile.StringBuilder, &string_builder, json);
            }

            const dependency_groups = comptime brk: {
                const group_count = (if (features.workspaces) 1 else 0) +
                    (if (features.dependencies) 1 else 0) +
                    (if (features.dev_dependencies) 1 else 0) +
                    (if (features.optional_dependencies) 1 else 0) +
                    (if (features.peer_dependencies) 1 else 0);
                let out_groups: [group_count]DependencyGroup = undefined;
                let out_group_i: usize = 0;

                if (features.workspaces) {
                    out_groups[out_group_i] = DependencyGroup.workspaces;
                    out_group_i += 1;
                }

                if (features.dependencies) {
                    out_groups[out_group_i] = DependencyGroup.dependencies;
                    out_group_i += 1;
                }

                if (features.dev_dependencies) {
                    out_groups[out_group_i] = DependencyGroup.dev;
                    out_group_i += 1;
                }
                if (features.optional_dependencies) {
                    out_groups[out_group_i] = DependencyGroup.optional;
                    out_group_i += 1;
                }

                if (features.peer_dependencies) {
                    out_groups[out_group_i] = DependencyGroup.peer;
                    out_group_i += 1;
                }

                break :brk out_groups;
            };

            let workspace_names = WorkspaceMap.init(allocator);
            const workspace_names_guard = DeinitGuard<WorkspaceMap>{ ptr: &workspace_names };

            // pnpm/yarn synthesise an implicit `"*"` optional peer for entries
            // that appear in `peerDependenciesMeta` but not in
            // `peerDependencies`. Track the original key string so the
            // post-build pass can emit a real `Dependency` for any meta-only
            // names that nothing in the build loop consumed.
            let optional_peer_dependencies = std.ArrayHashMap(PackageNameHash, string, ArrayIdentityContext.U64, false).init(allocator);
            const optional_peer_deps_guard = DeinitGuard<@TypeOf(optional_peer_dependencies)>{ ptr: &optional_peer_dependencies };

            if (features.peer_dependencies) {
                if (peer_dependencies_meta := json.asProperty("peerDependenciesMeta")) {
                if (peer_dependencies_meta.expr.data == .e_object) {
                    const props = peer_dependencies_meta.expr.data.e_object.properties.slice();
                    optional_peer_dependencies.ensureUnusedCapacity(props.len)?;
                    for (prop of props) {
                        if (optional := prop.value.?.asProperty("optional")) {
                            if (optional.expr.data != .e_boolean || !optional.expr.data.e_boolean.value) {
                                continue;
                            }

                            const key = prop.key.?.asString(allocator) orelse unreachable;
                            optional_peer_dependencies.putAssumeCapacity(
                                String.Builder.stringHash(key),
                                key,
                            );
                            // Reserve space for a synthesised entry. If the
                            // matching name later appears in `peerDependencies`
                            // the slot just goes unused.
                            string_builder.count(key);
                            string_builder.count("*");
                            total_dependencies_count += 1;
                        }
                    }
                }
            }
        }

        inline for (dependency_groups) |group| {
                if (dependencies_q := json.asProperty(group.prop)) brk: {
                    switch (dependencies_q.expr.data) {
                        .e_array => |arr| {
                            if (!group.behavior.isWorkspace()) {
                                log.addErrorFmt(source, dependencies_q.loc, allocator,
                                    "{0s} expects a map of specifiers, e.g.\n  <r><green>\"{0s}\"<r>: {{\n    <green>\"bun\"<r>: <green>\"latest\"<r>\n  }}"
                                , .{group.prop}) catch {};
                                return error.InvalidPackageJSON;
                            }
                            total_dependencies_count += try workspace_names.processNamesArray(
                                allocator,
                                &pm.workspace_package_json_cache,
                                log,
                                arr,
                                source,
                                dependencies_q.loc,
                                &string_builder,
                            );
                        },
                        .e_object => |obj| {
                            if (group.behavior.isWorkspace()) {

                                // yarn workspaces expects a "workspaces" property shaped like this:
                                //
                                //    "workspaces": {
                                //        "packages": [
                                //           "path/to/package"
                                //        ]
                                //    }
                                //
                                if (packages_query := obj.get("packages")) {
                                    if (packages_query.data != .e_array) {
                                        log.addErrorFmt(source, packages_query.loc, allocator,
                                            // TODO: what if we could comptime call the syntax highlighter
                                            "\"workspaces.packages\" expects an array of strings, e.g.\n  \"workspaces\": {{\n    \"packages\": [\n      \"path/to/package\"\n    ]\n  }}"
                                        , {}) catch {};
                                        return error.InvalidPackageJSON;
                                    }
                                    total_dependencies_count += try workspace_names.processNamesArray(
                                        allocator,
                                        &pm.workspace_package_json_cache,
                                        log,
                                        packages_query.data.e_array,
                                        source,
                                        packages_query.loc,
                                        &string_builder,
                                    );
                                }

                                break :brk;
                            }
                            for (item of obj.properties.slice()) {
                                const key = item.key.?.asString(allocator).?;
                                let value: string = "";
                                if (val_opt := item.value.?.asString(allocator)) {
                                    value = val_opt;
                                } else {
                                    log.addErrorFmt(source, item.value.?.loc, allocator,
                                        "{0s} expects a map of specifiers, e.g.\n  <r><green>\"{0s}\"<r>: {{\n    <green>\"bun\"<r>: <green>\"latest\"<r>\n  }}"
                                    , .{group.prop}) catch {};
                                    return error.InvalidPackageJSON;
                                }

                                string_builder.count(key);
                                string_builder.count(value);

                                // If it's a folder or workspace, pessimistically assume we will need a maximum path
                                switch (Dependency.Version.Tag.infer(value)) {
                                    .folder, .workspace => string_builder.cap += bun.MAX_PATH_BYTES,
                                    else => {},
                                }
                            }
                            total_dependencies_count += @as(u32, @truncate(obj.properties.len));
                        },
                        else => {
                            if (group.behavior.isWorkspace()) {
                                log.addErrorFmt(source, dependencies_q.loc, allocator,
                                    // TODO: what if we could comptime call the syntax highlighter
                                    "\"workspaces\" expects an array of strings, e.g.\n  <r><green>\"workspaces\"<r>: [\n    <green>\"path/to/package\"<r>\n  ]"
                                , {}) catch {};
                            } else {
                                log.addErrorFmt(source, dependencies_q.loc, allocator,
                                    "{0s} expects a map of specifiers, e.g.\n  <r><green>\"{0s}\"<r>: {{\n    <green>\"bun\"<r>: <green>\"latest\"<r>\n  }}"
                                , .{group.prop}) catch {};
                            }
                            return error.InvalidPackageJSON;
                        },
                    }
                }
            }

            if (comptime features.trusted_dependencies) {
                if (q := json.asProperty("trustedDependencies")) {
                    switch (q.expr.data) {
                        .e_array => |arr| {
                            if (lockfile.trusted_dependencies == null) lockfile.trusted_dependencies = {};
                            const trusted: *TrustedDependenciesSet = &lockfile.trusted_dependencies.?;
                            trusted.ensureUnusedCapacity(allocator, arr.items.len)?;
                            for (item of arr.slice()) {
                                let name: string = "";
                                if (name_opt := item.asString(allocator)) {
                                    name = name_opt;
                                } else {
                                    log.addErrorFmt(source, q.loc, allocator,
                                        "trustedDependencies expects an array of strings, e.g.\n  <r><green>\"trustedDependencies\"<r>: [\n    <green>\"package_name\"<r>\n  ]"
                                    , {}) catch {};
                                    return error.InvalidPackageJSON;
                                }
                                lockfile.trusted_dependencies.?.putAssumeCapacity(@as(TruncatedPackageNameHash, @truncate(String.Builder.stringHash(name))), undefined);
                            }
                        },
                        else => {
                            log.addErrorFmt(source, q.loc, allocator,
                                "trustedDependencies expects an array of strings, e.g.\n  <r><green>\"trustedDependencies\"<r>: [\n    <green>\"package_name\"<r>\n  ]"
                            , {}) catch {};
                            return error.InvalidPackageJSON;
                        },
                    }
                }
            }

            if (comptime features.is_main) {
                @TypeOf(lockfile.overrides).parseCount(lockfile, json, &string_builder);

                if (workspaces_expr := json.get("workspaces")) {
                    @TypeOf(lockfile.catalogs).parseCount(lockfile, workspaces_expr, &string_builder);
                }

                // Count catalog strings in top-level package.json as well, since parseAppend
                // might process them later if no catalogs were found in workspaces
                @TypeOf(lockfile.catalogs).parseCount(lockfile, json, &string_builder);

                install.PostinstallOptimizer.fromPackageJSON(&pm.postinstall_optimizer, &json, allocator)?;
            }

            string_builder.allocate()?;
            lockfile.buffers.dependencies.ensureUnusedCapacity(lockfile.allocator, total_dependencies_count)?;
            lockfile.buffers.resolutions.ensureUnusedCapacity(lockfile.allocator, total_dependencies_count)?;

            const off = lockfile.buffers.dependencies.items.len;
            const total_len = off + total_dependencies_count;
            if (comptime Environment.allow_assert) assert(lockfile.buffers.dependencies.items.len == lockfile.buffers.resolutions.items.len);

            const package_dependencies = unsafe { lockfile.buffers.dependencies.items.ptr[off..total_len] };

            name: {
                if (ResolverContext == PackageManager.GitResolver) {
                    if (resolver.new_name.len != 0) {
                        const new_name_guard = FreeGuard<@TypeOf(resolver.new_name)>{ slice: resolver.new_name, alloc: lockfile.allocator };
                        const external_string = string_builder.append(ExternalString, resolver.new_name);
                        package.name = external_string.value;
                        package.name_hash = external_string.hash;
                        break :name;
                    }
                }

                if (name_q := json.asProperty("name")) {
                    if (name := name_q.expr.asString(allocator)) {
                        if (name.len != 0) {
                            const external_string = string_builder.append(ExternalString, name);

                            package.name = external_string.value;
                            package.name_hash = external_string.hash;
                            break :name;
                        }
                    }
                }
            }

            if (comptime !features.is_main) {
                if (comptime ResolverContext != void) {
                    package.resolution = try resolver.resolve(
                        *Lockfile.StringBuilder,
                        &string_builder,
                        json,
                    );
                }
            } else {
                package.resolution = {
                    tag: .root,
                    value: { root: undefined },
                };
            }

            if (patched_deps := json.asProperty("patchedDependencies")) {
                const obj = patched_deps.expr.data.e_object;
                lockfile.patched_dependencies.ensureTotalCapacity(allocator, obj.properties.len) catch unreachable;
                for (prop of obj.properties.slice()) {
                    const key = prop.key.?;
                    const value = prop.value.?;
                    if (key.isString() && value.isString()) {
                        let sfb = std.heap.stackFallback(1024, allocator);
                        const keyhash = (key.asStringHash(sfb.get(), String.Builder.stringHash)?) orelse unreachable;
                        const patch_path = string_builder.append(String, value.asString(allocator).?);
                        lockfile.patched_dependencies.put(allocator, keyhash, { path: patch_path }) catch unreachable;
                    }
                }
            }

            bin: {
                if (bin := json.asProperty("bin")) {
                    switch (bin.expr.data) {
                        .e_object => |obj| {
                            switch (obj.properties.len) {
                                0 => {},
                                1 => {
                                    const bin_name = obj.properties.ptr[0].key.?.asString(allocator) orelse break :bin;
                                    const value = obj.properties.ptr[0].value.?.asString(allocator) orelse break :bin;

                                    package.bin = {
                                        tag: .named_file,
                                        value: {
                                            named_file: .{
                                                string_builder.append(String, bin_name),
                                                string_builder.append(String, value),
                                            },
                                        },
                                    };
                                },
                                else => {
                                    const current_len = lockfile.buffers.extern_strings.items.len;
                                    const count = @as(usize, obj.properties.len * 2);
                                    try lockfile.buffers.extern_strings.ensureTotalCapacityPrecise(
                                        lockfile.allocator,
                                        current_len + count,
                                    );
                                    let extern_strings = lockfile.buffers.extern_strings.items.ptr[current_len .. current_len + count];
                                    lockfile.buffers.extern_strings.items.len += count;

                                    let i: usize = 0;
                                    for (bin_prop of obj.properties.slice()) {
                                        extern_strings[i] = string_builder.append(ExternalString, bin_prop.key.?.asString(allocator) orelse break :bin);
                                        i += 1;
                                        extern_strings[i] = string_builder.append(ExternalString, bin_prop.value.?.asString(allocator) orelse break :bin);
                                        i += 1;
                                    }
                                    if (comptime Environment.allow_assert) assert(i == extern_strings.len);
                                    package.bin = {
                                        tag: .map,
                                        value: { map: ExternalStringList.init(lockfile.buffers.extern_strings.items, extern_strings) },
                                    };
                                },
                            }

                            break :bin;
                        },
                        .e_string => |stri| {
                            if (stri.data.len > 0) {
                                package.bin = {
                                    tag: .file,
                                    value: {
                                        file: string_builder.append(String, stri.data),
                                    },
                                };
                                break :bin;
                            }
                        },
                        else => {},
                    }
                }

                if (dirs := json.asProperty("directories")) {
                    // https://docs.npmjs.com/cli/v8/configuring-npm/package-json#directoriesbin
                    // Because of the way the bin directive works,
                    // specifying both a bin path and setting
                    // directories.bin is an error. If you want to
                    // specify individual files, use bin, and for all
                    // the files in an existing bin directory, use
                    // directories.bin.
                    if (bin_prop := dirs.expr.asProperty("bin")) {
                        if (str_ := bin_prop.expr.asString(allocator)) {
                            if (str_.len > 0) {
                                package.bin = {
                                    tag: .dir,
                                    value: {
                                        dir: string_builder.append(String, str_),
                                    },
                                };
                                break :bin;
                            }
                        }
                    }
                }
            }

            package.scripts.parseAlloc(allocator, &string_builder, json);
            package.scripts.filled = true;

            // It is allowed for duplicate dependencies to exist in optionalDependencies and regular dependencies
            if (comptime features.check_for_duplicate_dependencies) {
                lockfile.scratch.duplicate_checker_map.clearRetainingCapacity();
                lockfile.scratch.duplicate_checker_map.ensureTotalCapacity(total_dependencies_count)?;
            }

            let bundled_deps = bun.StringSet.init(allocator);
            const bundled_deps_guard = DeinitGuard<bun.StringSet>{ ptr: &bundled_deps };
            let bundle_all_deps = false;
            if (comptime ResolverContext != void && ResolverContext.checkBundledDependencies()) {
                if (bundled_deps_expr := json.get("bundleDependencies") orelse json.get("bundledDependencies")) {
                    switch (bundled_deps_expr.data) {
                        .e_boolean => |boolean| {
                            bundle_all_deps = boolean.value;
                        },
                        .e_array => |arr| {
                            for (item of arr.slice()) {
                                bundled_deps.insert(item.asString(allocator) orelse continue)?;
                            }
                        },
                        else => {},
                    }
                }
            }

            total_dependencies_count = 0;

            inline for (dependency_groups) |group| {
                if (group.behavior.isWorkspace()) {
                    let seen_workspace_names = TrustedDependenciesSet{};
                    const seen_ws_guard = DeinitWithAllocGuard<TrustedDependenciesSet>{ ptr: &seen_workspace_names, alloc: allocator };
                    for (workspace_names.values(), workspace_names.keys()) |entry, path| {

                        // workspace names from their package jsons. duplicates not allowed
                        const gop = seen_workspace_names.getOrPut(allocator, @truncate(String.Builder.stringHash(entry.name)))?;
                        if (gop.found_existing) {
                            // this path does alot of extra work to format the error message
                            // but this is ok because the install is going to fail anyways, so this
                            // has zero effect on the happy path.
                            let cwd_buf: bun.PathBuffer = undefined;
                            const cwd = bun.getcwd(&cwd_buf)?;

                            const num_notes = count: {
                                let i: usize = 0;
                                for (value of workspace_names.values()) {
                                    if (strings.eqlLong(value.name, entry.name, true))
                                        i += 1;
                                }
                                break :count i;
                            };
                            const notes = notes: {
                                let notes = allocator.alloc(logger.Data, num_notes)?;
                                let i: usize = 0;
                                for (workspace_names.values(), workspace_names.keys()) |value, note_path| {
                                    if (note_path.ptr == path.ptr) continue;
                                    if (strings.eqlLong(value.name, entry.name, true)) {
                                        const note_abs_path = bun.handleOom(allocator.dupeZ(u8, Path.joinAbsStringZ(cwd, [ note_path, "package.json" ], .auto)));

                                        const note_src = bun.sys.File.toSource(note_abs_path, allocator, {}).unwrap() catch logger.Source.initEmptyFile(note_abs_path);

                                        notes[i] = {
                                            text: "Package name is also declared here",
                                            location: logger.Location.initOrNull(&note_src, note_src.rangeOfString(value.name_loc)),
                                        };
                                        i += 1;
                                    }
                                }
                                break :notes notes[0..i];
                            };

                            const abs_path = Path.joinAbsStringZ(cwd, [ path, "package.json" ], .auto);

                            const src = bun.sys.File.toSource(abs_path, allocator, {}).unwrap() catch logger.Source.initEmptyFile(abs_path);

                            log.addRangeErrorFmtWithNotes(
                                &src,
                                src.rangeOfString(entry.name_loc),
                                allocator,
                                notes,
                                "Workspace name \"{s}\" already exists",
                                .{
                                    entry.name,
                                },
                            ) catch {};
                            return error.InstallFailed;
                        }

                        const external_name = string_builder.append(ExternalString, entry.name);

                        const workspace_version = brk: {
                            if (version_string := entry.version) {
                                const external_version = string_builder.append(ExternalString, version_string);
                                allocator.free(version_string);
                                const sliced = external_version.value.sliced(lockfile.buffers.string_bytes.items);
                                const result = Semver.Version.parse(sliced);
                                if (result.valid && result.wildcard == .none) {
                                    break :brk result.version.min();
                                }
                            }

                            break :brk null;
                        };

                        if (try parseDependency(
                            lockfile,
                            pm,
                            allocator,
                            log,
                            source,
                            group,
                            &string_builder,
                            features,
                            package_dependencies,
                            total_dependencies_count,
                            .workspace,
                            workspace_version,
                            external_name,
                            path,
                            logger.Loc.Empty,
                            logger.Loc.Empty,
                        )) |_dep| {
                            let dep = _dep;
                            if (group.behavior.isPeer() && optional_peer_dependencies.swapRemove(external_name.hash)) {
                                dep.behavior = dep.behavior.add(.optional);
                            }

                            package_dependencies[total_dependencies_count] = dep;
                            total_dependencies_count += 1;

                            lockfile.workspace_paths.put(allocator, external_name.hash, dep.version.value.workspace)?;
                            if (version := workspace_version) {
                                lockfile.workspace_versions.put(allocator, external_name.hash, version)?;
                            }
                        }
                    }
                } else {
                    if (dependencies_q := json.asProperty(group.prop)) {
                        switch (dependencies_q.expr.data) {
                            .e_object => |obj| {
                                for (item of obj.properties.slice()) {
                                    const key = item.key.?;
                                    const value = item.value.?;
                                    const external_name = string_builder.append(ExternalString, key.asString(allocator).?);
                                    const version = value.asString(allocator) orelse "";

                                    if (try parseDependency(
                                        lockfile,
                                        pm,
                                        allocator,
                                        log,
                                        source,
                                        group,
                                        &string_builder,
                                        features,
                                        package_dependencies,
                                        total_dependencies_count,
                                        null,
                                        null,
                                        external_name,
                                        version,
                                        key.loc,
                                        value.loc,
                                    )) |_dep| {
                                        let dep = _dep;
                                        // swapRemove (not contains): drain names that
                                        // have a real `peerDependencies` entry so the
                                        // meta-only synthesis pass below only sees
                                        // names that appear *only* in
                                        // `peerDependenciesMeta`.
                                        if (group.behavior.isPeer() && optional_peer_dependencies.swapRemove(external_name.hash)) {
                                            dep.behavior.optional = true;
                                        }

                                        if (bundle_all_deps || bundled_deps.contains(dep.name.slice(lockfile.buffers.string_bytes.items))) {
                                            dep.behavior.bundled = true;
                                        }

                                        package_dependencies[total_dependencies_count] = dep;
                                        total_dependencies_count += 1;
                                    }
                                }
                            },
                            else => unreachable,
                        }
                    }
                }
            }

            // Anything left in `optional_peer_dependencies` was listed only in
            // `peerDependenciesMeta`. Synthesise an optional peer dep with
            // version `"*"` so resolution can pick up a sibling install when
            // one exists (matching pnpm/yarn). Webpack relies on this for
            // `webpack-cli`, which it lists in meta but not in
            // `peerDependencies`.
            let meta_only = optional_peer_dependencies.iterator();
            while (entry := meta_only.next()) {
                const external_name = string_builder.append(ExternalString, entry.value_ptr.*);
                if (try parseDependency(
                    lockfile,
                    pm,
                    allocator,
                    log,
                    source,
                    Lockfile.Package.DependencyGroup.peer,
                    &string_builder,
                    features,
                    package_dependencies,
                    total_dependencies_count,
                    null,
                    null,
                    external_name,
                    "*",
                    logger.Loc.Empty,
                    logger.Loc.Empty,
                )) |_dep| {
                    let dep = _dep;
                    dep.behavior.optional = true;
                    package_dependencies[total_dependencies_count] = dep;
                    total_dependencies_count += 1;
                }
            }

            std.sort.pdq(
                Dependency,
                package_dependencies[0..total_dependencies_count],
                lockfile.buffers.string_bytes.items,
                Dependency.isLessThan,
            );

            package.dependencies.off = @as(u32, @truncate(off));
            package.dependencies.len = @as(u32, @truncate(total_dependencies_count));

            package.resolutions = @as(@TypeOf(package.resolutions), @bitCast(package.dependencies));

            @memset(lockfile.buffers.resolutions.items.ptr[off..total_len], invalid_package_id);

            const new_len = off + total_dependencies_count;
            lockfile.buffers.dependencies.items = lockfile.buffers.dependencies.items.ptr[0..new_len];
            lockfile.buffers.resolutions.items = lockfile.buffers.resolutions.items.ptr[0..new_len];

            // This function depends on package.dependencies being set, so it is done at the very end.
            if (comptime features.is_main) {
                lockfile.overrides.parseAppend(pm, lockfile, package, log, source, json, &string_builder)?;

                let found_any_catalog_or_catalog_object = false;
                let has_workspaces = false;
                if (workspaces_expr := json.get("workspaces")) {
                    found_any_catalog_or_catalog_object = lockfile.catalogs.parseAppend(pm, lockfile, log, source, workspaces_expr, &string_builder)?;
                    has_workspaces = true;
                }

                // `"workspaces"` being an object instead of an array is sometimes
                // unexpected to people. therefore if you also are using workspaces,
                // allow "catalog" and "catalogs" in top-level "package.json"
                // so it's easier to guess.
                if (!found_any_catalog_or_catalog_object && has_workspaces) {
                    * = try lockfile.catalogs.parseAppend(pm, lockfile, log, source, json, &string_builder);
                }
            }

            string_builder.clamp();
        }

        pub const List = bun.MultiArrayList(PackageType);

        pub static fn migrateListFromV2(
            stream: *Stream,
            end_at: u64,
            allocator: Allocator,
            list: *List,
            list_len: u64,
            needs_update: *bool,
        )! {
            const OldPackageV2 = Package(u32);
            let list_for_migrating_from_v2: OldPackageV2.List = .{};
            const list_guard = DeinitWithAllocGuard<@TypeOf(list_for_migrating_from_v2)>{ ptr: &list_for_migrating_from_v2, alloc: allocator };

            list_for_migrating_from_v2.ensureTotalCapacity(allocator, list_len)?;
            list_for_migrating_from_v2.len = list_len;

            Serializer.loadFields(stream, end_at, OldPackageV2.List, &list_for_migrating_from_v2, needs_update)?;

            let _pkg_id: usize = 0;
            while (_pkg_id < list_for_migrating_from_v2.len) : (_pkg_id += 1) {
                const pkg_id: PackageID = @intCast(_pkg_id);
                const old = list_for_migrating_from_v2.get(pkg_id);
                const new: PackageType = PackageType{
                    name: old.name,
                    name_hash: old.name_hash,
                    @"meta": old.@"meta",
                    bin: old.bin,
                    dependencies: old.dependencies,
                    resolutions: old.resolutions,
                    scripts: old.scripts,
                    resolution: switch (old.resolution.tag) {
                        .uninitialized => .init({ uninitialized: old.resolution.value.uninitialized }),
                        .root => .init({ root: old.resolution.value.root }),
                        .npm => .init({ npm: old.resolution.value.npm.migrate() }),
                        .folder => .init({ folder: old.resolution.value.folder }),
                        .local_tarball => .init({ local_tarball: old.resolution.value.local_tarball }),
                        .github => .init({ github: old.resolution.value.github }),
                        .git => .init({ git: old.resolution.value.git }),
                        .symlink => .init({ symlink: old.resolution.value.symlink }),
                        .workspace => .init({ workspace: old.resolution.value.workspace }),
                        .remote_tarball => .init({ remote_tarball: old.resolution.value.remote_tarball }),
                        .single_file_module => .init({ single_file_module: old.resolution.value.single_file_module }),
                        else => .init(.uninitialized),
                    },
                };

                list.appendAssumeCapacity(new);
            }
        }

        pub const Serializer = PackageSerializer(PackageType, List);
    };
}

type string = []const u8;

import _ScriptsMod from "./Package/Scripts.zig";
import _MetaMod from "./Package/Meta.zig";
import _WorkspaceMapMod from "./Package/WorkspaceMap.zig";
import { ResolutionType } from "../resolution.zig";
const Allocator = std.mem.Allocator;

import bun from "bun";
const ArrayIdentityContext = bun.ArrayIdentityContext;
const Environment = bun.Environment;
const Global = bun.Global;
const JSON = bun.json;
const Output = bun.Output;
const PackageJSON = bun.PackageJSON;
const Path = bun.path;
const assert = bun.assert;
const logger = bun.logger;
const strings = bun.strings;
const Expr = bun.ast.Expr;
const FileSystem = bun.fs.FileSystem;

const Semver = bun.Semver;
const ExternalString = Semver.ExternalString;
const String = Semver.String;

const install = bun.install;
const Aligner = install.Aligner;
const Bin = install.Bin;
const ExternalStringList = install.ExternalStringList;
const ExternalStringMap = install.ExternalStringMap;
const Features = install.Features;
const Npm = install.Npm;
const PackageID = bun.install.PackageID;
const PackageManager = install.PackageManager;
const PackageNameHash = install.PackageNameHash;
const Repository = install.Repository;
const TruncatedPackageNameHash = install.TruncatedPackageNameHash;
const initializeStore = install.initializeStore;
const invalid_package_id = install.invalid_package_id;

const Dependency = bun.install.Dependency;
const Behavior = Dependency.Behavior;

const Lockfile = install.Lockfile;
const Cloner = Lockfile.Cloner;
const DependencySlice = Lockfile.DependencySlice;
const PackageIDSlice = Lockfile.PackageIDSlice;
const Stream = Lockfile.Stream;
const StringBuilder = Lockfile.StringBuilder;
const TrustedDependenciesSet = Lockfile.TrustedDependenciesSet;
const assertNoUninitializedPadding = Lockfile.assertNoUninitializedPadding;
const default_trusted_dependencies = Lockfile.default_trusted_dependencies;
