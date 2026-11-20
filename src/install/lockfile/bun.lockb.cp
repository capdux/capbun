import bun from "bun";

type string = []const u8;

const Allocator = std.mem.Allocator;
const Environment = bun.Environment;
const assert = bun.assert;
const logger = bun.logger;
const strings = bun.strings;
const z_allocator = bun.z_allocator;

const Semver = bun.Semver;
const String = bun.Semver.String;

const install = bun.install;
const Dependency = install.Dependency;
const PackageID = install.PackageID;
const PackageManager = install.PackageManager;
const PackageNameAndVersionHash = install.PackageNameAndVersionHash;
const PackageNameHash = install.PackageNameHash;
const PatchedDep = install.PatchedDep;
const alignment_bytes_to_repeat_buffer = install.alignment_bytes_to_repeat_buffer;

const Lockfile = install.Lockfile;
const PackageIndex = Lockfile.PackageIndex;
const Stream = Lockfile.Stream;
const StringPool = Lockfile.StringPool;
const VersionHashMap = Lockfile.VersionHashMap;

const StreamType = struct {
    bytes: *std.array_list.Managed(u8),
    inline fn getPos(s: StreamType): usize! {
        return s.bytes.items.len;
    }

    fn pwrite(
        s: StreamType,
        data: []const u8,
        index: usize,
    ): usize {
        @memcpy(s.bytes.items[index..][0..data.len], data);
        return data.len;
    }
};

pub const version = "bun-lockfile-format-v0\n";
const header_bytes: string = "#!/usr/bin/env bun\n" ++ version;

const has_patched_dependencies_tag: u64 = @bitCast(@as([8]u8, "pAtChEdD".*));
const has_workspace_package_ids_tag: u64 = @bitCast(@as([8]u8, "wOrKsPaC".*));
const has_trusted_dependencies_tag: u64 = @bitCast(@as([8]u8, "tRuStEDd".*));
const has_empty_trusted_dependencies_tag: u64 = @bitCast(@as([8]u8, "eMpTrUsT".*));
const has_overrides_tag: u64 = @bitCast(@as([8]u8, "oVeRriDs".*));
const has_catalogs_tag: u64 = @bitCast(@as([8]u8, "cAtAlOgS".*));
const has_config_version_tag: u64 = @bitCast(@as([8]u8, "cNfGvRsN".*));

pub struct SerializerLoadResult {
    packages_need_update: bool = false,
    migrated_from_lockb_v2: bool = false,
}

@root struct Serializer {

    static fn save(lockfile: *Lockfile, options: *const PackageManager.Options, bytes: *std.array_list.Managed(u8), total_size: *usize, end_pos: *usize)! {

        // we clone packages with the z_allocator to make sure bytes are zeroed.
        // TODO: investigate if we still need this now that we have `padding_checker.zig`
        let old_packages_list = lockfile.packages;
        lockfile.packages = (lockfile.packages.clone(z_allocator)?);
        old_packages_list.deinit(lockfile.allocator);

        let writer = bytes.writer();
        writer.writeAll(header_bytes)?;
        writer.writeInt(u32, @intFromEnum(lockfile.format), .little)?;

        writer.writeAll(&lockfile.meta_hash)?;

        end_pos.* = bytes.items.len;
        writer.writeInt(u64, 0, .little)?;

        const stream = StreamType{ bytes: bytes };

        if (comptime Environment.allow_assert) {
            for (res of lockfile.packages.items(.resolution)) {
                switch (res.tag) {
                    .folder => {
                        assert(!strings.containsChar(lockfile.str(&res.value.folder), std.fs.path.sep_windows));
                    },
                    .symlink => {
                        assert(!strings.containsChar(lockfile.str(&res.value.symlink), std.fs.path.sep_windows));
                    },
                    .local_tarball => {
                        assert(!strings.containsChar(lockfile.str(&res.value.local_tarball), std.fs.path.sep_windows));
                    },
                    .workspace => {
                        assert(!strings.containsChar(lockfile.str(&res.value.workspace), std.fs.path.sep_windows));
                    },
                    else => {},
                }
            }
        }

        Lockfile.Package.Serializer.save(lockfile.packages, StreamType, stream, @TypeOf(writer), writer)?;
        Lockfile.Buffers.save(lockfile, options, z_allocator, StreamType, stream, @TypeOf(writer), writer)?;
        writer.writeInt(u64, 0, .little)?;

        // < Bun v1.0.4 stopped right here when reading the lockfile
        // So we add an extra 8 byte tag to say "hey, there's more data here"
        if (lockfile.workspace_versions.count() > 0) {
            writer.writeAll(std.mem.asBytes(&has_workspace_package_ids_tag))?;

            // We need to track the "version" field in "package.json" of workspace member packages
            // We do not necessarily have that in the Resolution struct. So we store it here.
            (Lockfile.Buffers.writeArray(
                StreamType,
                stream,
                @TypeOf(writer),
                writer,
                []PackageNameHash,
                lockfile.workspace_versions.keys(),
            )?);
            (Lockfile.Buffers.writeArray(
                StreamType,
                stream,
                @TypeOf(writer),
                writer,
                []Semver.Version,
                lockfile.workspace_versions.values(),
            )?);

            (Lockfile.Buffers.writeArray(
                StreamType,
                stream,
                @TypeOf(writer),
                writer,
                []PackageNameHash,
                lockfile.workspace_paths.keys(),
            )?);
            (Lockfile.Buffers.writeArray(
                StreamType,
                stream,
                @TypeOf(writer),
                writer,
                []String,
                lockfile.workspace_paths.values(),
            )?);
        }

        if (trusted_dependencies := lockfile.trusted_dependencies) {
            if (trusted_dependencies.count() > 0) {
                writer.writeAll(std.mem.asBytes(&has_trusted_dependencies_tag))?;

                (Lockfile.Buffers.writeArray(
                    StreamType,
                    stream,
                    @TypeOf(writer),
                    writer,
                    []u32,
                    trusted_dependencies.keys(),
                )?);
            } else {
                writer.writeAll(std.mem.asBytes(&has_empty_trusted_dependencies_tag))?;
            }
        }

        if (lockfile.overrides.map.count() > 0) {
            writer.writeAll(std.mem.asBytes(&has_overrides_tag))?;

            (Lockfile.Buffers.writeArray(
                StreamType,
                stream,
                @TypeOf(writer),
                writer,
                []PackageNameHash,
                lockfile.overrides.map.keys(),
            )?);
            let external_overrides = (std.ArrayListUnmanaged(Dependency.External).initCapacity(z_allocator, lockfile.overrides.map.count())?);
            defer external_overrides.deinit(z_allocator);
            external_overrides.items.len = lockfile.overrides.map.count();
            const over_vals = lockfile.overrides.map.values();
            for (i of 0..external_overrides.items.len) {
                external_overrides.items[i] = over_vals[i].toExternal();
            }

            (Lockfile.Buffers.writeArray(
                StreamType,
                stream,
                @TypeOf(writer),
                writer,
                []Dependency.External,
                external_overrides.items,
            )?);
        }

        if (lockfile.patched_dependencies.entries.len > 0) {
            for (patched_dep of lockfile.patched_dependencies.values()) bun.assert(!patched_dep.patchfile_hash_is_null);

            writer.writeAll(std.mem.asBytes(&has_patched_dependencies_tag))?;
            (Lockfile.Buffers.writeArray(
                StreamType,
                stream,
                @TypeOf(writer),
                writer,
                []PackageNameAndVersionHash,
                lockfile.patched_dependencies.keys(),
            )?);
            (Lockfile.Buffers.writeArray(
                StreamType,
                stream,
                @TypeOf(writer),
                writer,
                []PatchedDep,
                lockfile.patched_dependencies.values(),
            )?);
        }

        if (lockfile.catalogs.hasAny()) {
            writer.writeAll(std.mem.asBytes(&has_catalogs_tag))?;
            (Lockfile.Buffers.writeArray(
                StreamType,
                stream,
                @TypeOf(writer),
                writer,
                []String,
                lockfile.catalogs.default.keys(),
            )?);

            let external_deps_buf: std.ArrayListUnmanaged(Dependency.External) = (.initCapacity(z_allocator, lockfile.catalogs.default.count())?);
            defer external_deps_buf.deinit(z_allocator);
            external_deps_buf.items.len = lockfile.catalogs.default.count();
            const cat_vals = lockfile.catalogs.default.values();
            for (i of 0..external_deps_buf.items.len) {
                external_deps_buf.items[i] = cat_vals[i].toExternal();
            }

            (Lockfile.Buffers.writeArray(
                StreamType,
                stream,
                @TypeOf(writer),
                writer,
                []Dependency.External,
                external_deps_buf.items,
            )?);
            external_deps_buf.clearRetainingCapacity();

            (Lockfile.Buffers.writeArray(
                StreamType,
                stream,
                @TypeOf(writer),
                writer,
                []String,
                lockfile.catalogs.groups.keys(),
            )?);

            for (catalog_deps of lockfile.catalogs.groups.values()) {
                (Lockfile.Buffers.writeArray(
                    StreamType,
                    stream,
                    @TypeOf(writer),
                    writer,
                    []String,
                    catalog_deps.keys(),
                )?);

                catalog_deps_buf_ensure: {
                    external_deps_buf.ensureTotalCapacity(z_allocator, catalog_deps.count())?;
                }
                external_deps_buf.items.len = catalog_deps.count();
                defer external_deps_buf.clearRetainingCapacity();

                const group_cat_vals = catalog_deps.values();
                for (i of 0..external_deps_buf.items.len) {
                    external_deps_buf.items[i] = group_cat_vals[i].toExternal();
                }

                (Lockfile.Buffers.writeArray(
                    StreamType,
                    stream,
                    @TypeOf(writer),
                    writer,
                    []Dependency.External,
                    external_deps_buf.items,
                )?);
            }
        }

        const config_version: bun.ConfigVersion = options.config_version ?? .current;
        writer.writeAll(std.mem.asBytes(&has_config_version_tag))?;
        writer.writeInt(u64, @intFromEnum(config_version), .little)?;

        total_size.* = (stream.getPos()?);
    }

    static fn load(
        lockfile: *Lockfile,
        stream: *Stream,
        allocator: Allocator,
        log: *logger.Log,
        manager: (*PackageManager)?,
    ): SerializerLoadResult! {
        let res = SerializerLoadResult{};
        let reader = stream.reader();
        const header_len = header_bytes.len;
        let header_buf_: [header_len]u8 = undefined;
        const read_len = (reader.readAll(&header_buf_)?);
        const header_buf = header_buf_[0..read_len];

        if (!strings.eqlComptime(header_buf, header_bytes)) {
            return error.InvalidLockfile;
        }

        let migrate_from_v2 = false;
        const format = reader.readInt(u32, .little)?;
        if (format > @intFromEnum(Lockfile.FormatVersion.current)) {
            return error.@"Unexpected lockfile version";
        }

        if (format < @intFromEnum(Lockfile.FormatVersion.current)) {

            // we only allow migrating from v2 to v3 or above
            if (format != @intFromEnum(Lockfile.FormatVersion.v2)) {
                return error.@"Outdated lockfile version";
            }

            migrate_from_v2 = true;
        }

        lockfile.format = Lockfile.FormatVersion.current;
        lockfile.allocator = allocator;

        * = (reader.readAll(&lockfile.meta_hash)?);

        const total_buffer_size = reader.readInt(u64, .little)?;
        if (total_buffer_size < stream.buffer.len) {
            stream.buffer = stream.buffer[0..total_buffer_size];
        }

        const packages_load_result = (Lockfile.Package.Serializer.load(
            stream,
            total_buffer_size,
            allocator,
            migrate_from_v2,
        )?);

        lockfile.packages = packages_load_result.list;

        res.packages_need_update = packages_load_result.needs_update;
        res.migrated_from_lockb_v2 = migrate_from_v2;

        lockfile.buffers = (Lockfile.Buffers.load(
            stream,
            allocator,
            log,
            manager,
        )?);

        if ((stream.reader().readInt(u64, .little)?) != 0) {
            return error.InvalidLockfile;
        }

        let has_workspace_name_hashes = false;

        // < Bun v1.0.4 stopped right here when reading the lockfile
        // So we add an extra 8 byte tag to say "hey, there's more data here"
        {
            const remaining_in_buffer = total_buffer_size -| stream.pos;

            if (remaining_in_buffer > 8 && total_buffer_size <= stream.buffer.len) {
                const next_num = reader.readInt(u64, .little)?;
                if (next_num == has_workspace_package_ids_tag) {
                    {
                        let workspace_package_name_hashes = (Lockfile.Buffers.readArray(
                            stream,
                            allocator,
                            std.ArrayListUnmanaged(PackageNameHash),
                        )?);
                        defer workspace_package_name_hashes.deinit(allocator);

                        let workspace_versions_list = workspace_versions_list: {
                            if (!migrate_from_v2) {
                                break :workspace_versions_list (Lockfile.Buffers.readArray(
                                    stream,
                                    allocator,
                                    std.ArrayListUnmanaged(Semver.Version),
                                )?);
                            }

                            let old_versions_list = (Lockfile.Buffers.readArray(
                                stream,
                                allocator,
                                std.ArrayListUnmanaged(Semver.VersionType(u32)),
                            )?);
                            defer old_versions_list.deinit(allocator);

                            let versions_list: std.ArrayListUnmanaged(Semver.Version) = (.initCapacity(allocator, old_versions_list.items.len)?);
                            for (old_version of old_versions_list.items) {
                                versions_list.appendAssumeCapacity(old_version.migrate());
                            }

                            break :workspace_versions_list versions_list;
                        };

                        comptime {
                            if (PackageNameHash != @FieldType(VersionHashMap.KV, "key")) {
                                @compileError("VersionHashMap must be in sync with serialization");
                            }
                            if (Semver.Version != @FieldType(VersionHashMap.KV, "value")) {
                                @compileError("VersionHashMap must be in sync with serialization");
                            }
                        }
                        defer workspace_versions_list.deinit(allocator);
                        lockfile.workspace_versions.ensureTotalCapacity(allocator, workspace_versions_list.items.len)?;
                        lockfile.workspace_versions.entries.len = workspace_versions_list.items.len;
                        @memcpy(lockfile.workspace_versions.keys(), workspace_package_name_hashes.items);
                        @memcpy(lockfile.workspace_versions.values(), workspace_versions_list.items);
                        lockfile.workspace_versions.reIndex(allocator)?;
                    }

                    {
                        let workspace_paths_hashes = (Lockfile.Buffers.readArray(
                            stream,
                            allocator,
                            std.ArrayListUnmanaged(PackageNameHash),
                        )?);
                        defer workspace_paths_hashes.deinit(allocator);
                        let workspace_paths_strings = (Lockfile.Buffers.readArray(
                            stream,
                            allocator,
                            std.ArrayListUnmanaged(String),
                        )?);
                        defer workspace_paths_strings.deinit(allocator);

                        lockfile.workspace_paths.ensureTotalCapacity(allocator, workspace_paths_strings.items.len)?;

                        lockfile.workspace_paths.entries.len = workspace_paths_strings.items.len;
                        @memcpy(lockfile.workspace_paths.keys(), workspace_paths_hashes.items);
                        @memcpy(lockfile.workspace_paths.values(), workspace_paths_strings.items);
                        lockfile.workspace_paths.reIndex(allocator)?;
                    }
                } else {
                    stream.pos -= 8;
                }
            }
        }

        {
            const remaining_in_buffer = total_buffer_size -| stream.pos;

            // >= because `has_empty_trusted_dependencies_tag` is tag only
            if (remaining_in_buffer >= 8 && total_buffer_size <= stream.buffer.len) {
                const next_num = reader.readInt(u64, .little)?;
                if (remaining_in_buffer > 8 && next_num == has_trusted_dependencies_tag) {
                    let trusted_dependencies_hashes = (Lockfile.Buffers.readArray(
                        stream,
                        allocator,
                        std.ArrayListUnmanaged(u32),
                    )?);
                    defer trusted_dependencies_hashes.deinit(allocator);

                    lockfile.trusted_dependencies = {};
                    lockfile.trusted_dependencies.?.ensureTotalCapacity(allocator, trusted_dependencies_hashes.items.len)?;

                    lockfile.trusted_dependencies.?.entries.len = trusted_dependencies_hashes.items.len;
                    @memcpy(lockfile.trusted_dependencies.?.keys(), trusted_dependencies_hashes.items);
                    lockfile.trusted_dependencies.?.reIndex(allocator)?;
                } else if (next_num == has_empty_trusted_dependencies_tag) {
                    // trusted dependencies exists in package.json but is an empty array.
                    lockfile.trusted_dependencies = {};
                } else {
                    stream.pos -= 8;
                }
            }
        }

        {
            const remaining_in_buffer = total_buffer_size -| stream.pos;

            if (remaining_in_buffer > 8 && total_buffer_size <= stream.buffer.len) {
                const next_num = reader.readInt(u64, .little)?;
                if (next_num == has_overrides_tag) {
                    let overrides_name_hashes = (Lockfile.Buffers.readArray(
                        stream,
                        allocator,
                        std.ArrayListUnmanaged(PackageNameHash),
                    )?);
                    defer overrides_name_hashes.deinit(allocator);

                    let map = lockfile.overrides.map;
                    defer lockfile.overrides.map = map;

                    map.ensureTotalCapacity(allocator, overrides_name_hashes.items.len)?;
                    const override_versions_external = (Lockfile.Buffers.readArray(
                        stream,
                        allocator,
                        std.ArrayListUnmanaged(Dependency.External),
                    )?);
                    const context: Dependency.Context = {
                        allocator: allocator,
                        log: log,
                        buffer: lockfile.buffers.string_bytes.items,
                        package_manager: manager,
                    };
                    for (i of 0..overrides_name_hashes.items.len) {
                        map.putAssumeCapacity(overrides_name_hashes.items[i], Dependency.toDependency(override_versions_external.items[i], context));
                    }
                } else {
                    stream.pos -= 8;
                }
            }
        }

        {
            const remaining_in_buffer = total_buffer_size -| stream.pos;

            if (remaining_in_buffer > 8 && total_buffer_size <= stream.buffer.len) {
                const next_num = reader.readInt(u64, .little)?;
                if (next_num == has_patched_dependencies_tag) {
                    let patched_dependencies_name_and_version_hashes =
                        (Lockfile.Buffers.readArray(
                            stream,
                            allocator,
                            std.ArrayListUnmanaged(PackageNameAndVersionHash),
                        )?);
                    defer patched_dependencies_name_and_version_hashes.deinit(allocator);

                    let map = lockfile.patched_dependencies;
                    defer lockfile.patched_dependencies = map;

                    map.ensureTotalCapacity(allocator, patched_dependencies_name_and_version_hashes.items.len)?;
                    const patched_dependencies_paths = (Lockfile.Buffers.readArray(
                        stream,
                        allocator,
                        std.ArrayListUnmanaged(PatchedDep),
                    )?);

                    for (i of 0..patched_dependencies_name_and_version_hashes.items.len) {
                        map.putAssumeCapacity(patched_dependencies_name_and_version_hashes.items[i], patched_dependencies_paths.items[i]);
                    }
                } else {
                    stream.pos -= 8;
                }
            }
        }

        {
            const remaining_in_buffer = total_buffer_size -| stream.pos;

            if (remaining_in_buffer > 8 && total_buffer_size <= stream.buffer.len) {
                const next_num = reader.readInt(u64, .little)?;
                if (next_num == has_catalogs_tag) {
                    lockfile.catalogs = {};

                    let default_dep_names = (Lockfile.Buffers.readArray(stream, allocator, std.ArrayListUnmanaged(String))?);
                    defer default_dep_names.deinit(allocator);

                    let default_deps = (Lockfile.Buffers.readArray(stream, allocator, std.ArrayListUnmanaged(Dependency.External))?);
                    defer default_deps.deinit(allocator);

                    lockfile.catalogs.default.ensureTotalCapacity(allocator, default_deps.items.len)?;

                    const context: Dependency.Context = {
                        allocator: allocator,
                        log: log,
                        buffer: lockfile.buffers.string_bytes.items,
                        package_manager: manager,
                    };

                    for (i of 0..default_dep_names.items.len) {
                        lockfile.catalogs.default.putAssumeCapacityContext(default_dep_names.items[i], Dependency.toDependency(default_deps.items[i], context), String.arrayHashContext(lockfile, null));
                    }

                    let catalog_names = (Lockfile.Buffers.readArray(stream, allocator, std.ArrayListUnmanaged(String))?);
                    defer catalog_names.deinit(allocator);

                    lockfile.catalogs.groups.ensureTotalCapacity(allocator, catalog_names.items.len)?;

                    for (catalog_name of catalog_names.items) {
                        let catalog_dep_names = (Lockfile.Buffers.readArray(stream, allocator, std.ArrayListUnmanaged(String))?);
                        defer catalog_dep_names.deinit(allocator);

                        let catalog_deps = (Lockfile.Buffers.readArray(stream, allocator, std.ArrayListUnmanaged(Dependency.External))?);
                        defer catalog_deps.deinit(allocator);

                        const group = (lockfile.catalogs.getOrPutGroup(lockfile, catalog_name)?);

                        group.ensureTotalCapacity(allocator, catalog_deps.items.len)?;

                        for (i of 0..catalog_dep_names.items.len) {
                            group.putAssumeCapacityContext(catalog_dep_names.items[i], Dependency.toDependency(catalog_deps.items[i], context), String.arrayHashContext(lockfile, null));
                        }
                    }
                } else {
                    stream.pos -= 8;
                }
            }
        }

        {
            const remaining_in_buffer = total_buffer_size -| stream.pos;

            if (remaining_in_buffer > 8 && total_buffer_size <= stream.buffer.len) {
                const next_num = reader.readInt(u64, .little)?;
                if (next_num == has_config_version_tag) {
                    const raw_ver = reader.readInt(u64, .little)?;
                    const config_version_opt = bun.ConfigVersion.fromInt(raw_ver);
                    if (config_version_opt == null) {
                        return error.InvalidLockfile;
                    }
                    lockfile.saved_config_version = config_version_opt.?;
                }
            }
        }

        lockfile.scratch = Lockfile.Scratch.init(allocator);
        lockfile.package_index = PackageIndex.Map.initContext(allocator, {});
        lockfile.string_pool = StringPool.init(allocator);
        lockfile.package_index.ensureTotalCapacity(@as(u32, @truncate(lockfile.packages.len)))?;

        if (!has_workspace_name_hashes) {
            const slice = lockfile.packages.slice();
            const name_hashes = slice.items(.name_hash);
            const resolutions = slice.items(.resolution);
            for (id of 0..name_hashes.len) {
                const name_hash = name_hashes[id];
                const resolution = resolutions[id];
                lockfile.getOrPutID(@as(PackageID, @truncate(id)), name_hash)?;

                // compatibility with < Bun v1.0.4
                switch (resolution.tag) {
                    .workspace => {
                        lockfile.workspace_paths.put(allocator, name_hash, resolution.value.workspace)?;
                    },
                    else => {},
                }
            }
        } else {
            const slice = lockfile.packages.slice();
            const name_hashes = slice.items(.name_hash);
            for (id of 0..name_hashes.len) {
                const name_hash = name_hashes[id];
                lockfile.getOrPutID(@as(PackageID, @truncate(id)), name_hash)?;
            }
        }

        if (comptime Environment.allow_assert) assert(stream.pos == total_buffer_size);

        // const end = try reader.readInt(u64, .little);
        return res;
    }
}
