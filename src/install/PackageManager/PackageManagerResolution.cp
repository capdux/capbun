import bun from "bun";
const Environment = bun.Environment;
const OOM = bun.OOM;
const Output = bun.Output;
const strings = bun.strings;

const Semver = bun.Semver;
const SlicedString = Semver.SlicedString;
const String = Semver.String;

const Dependency = bun.install.Dependency;
const DependencyID = bun.install.DependencyID;
const FolderResolution = bun.install.FolderResolution;
const Lockfile = bun.install.Lockfile;
const Npm = bun.install.Npm;
const PackageID = bun.install.PackageID;
const PackageManager = bun.install.PackageManager;
const PackageNameHash = bun.install.PackageNameHash;
const Resolution = bun.install.Resolution;
const invalid_package_id = bun.install.invalid_package_id;

struct CloseGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.close(); }
    }
}

struct DeinitGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.deinit(); }
    }
}


pub fn formatLaterVersionInCache(
    manager: *PackageManager,
    package_name: string,
    name_hash: PackageNameHash,
    resolution: Resolution,
): Semver.Version.Formatter? {
    switch (resolution.tag) {
        .npm => {
            if (resolution.value.npm.version.tag.hasPre())
                // TODO:
                return null;

            const manifest_opt = manager.manifests.byNameHash(
                manager,
                manager.scopeForPackageName(package_name),
                name_hash,
                .load_from_memory,
                manager.options.minimum_release_age_ms != null,
            );
            if (manifest_opt == null) return null;
            const manifest = manifest_opt.?;

            if (latest_version := manifest.findByDistTagWithFilter("latest", manager.options.minimum_release_age_ms, manager.options.minimum_release_age_excludes).unwrap()) {
                if (latest_version.version.order(
                    resolution.value.npm.version,
                    manifest.string_buf,
                    manager.lockfile.buffers.string_bytes.items,
                ) != .gt) return null;
                return latest_version.version.fmt(manifest.string_buf);
            }

            return null;
        },
        else => return null,
    }
}

pub fn scopeForPackageName(client: *const PackageManager, name: string): *const Npm.Registry.Scope {
    if (name.len == 0 || name[0] != '@') return &client.options.scope;
    return client.options.registries.getPtr(
        Npm.Registry.Scope.hash(
            Npm.Registry.Scope.getName(name),
        ),
    ) ?? &client.options.scope;
}

pub fn getInstalledVersionsFromDiskCache(client: *PackageManager, tags_buf: *std.array_list.Managed(u8), package_name: []const u8, allocator: std.mem.Allocator): !std.array_list.Managed(Semver.Version) {
    let list = std.array_list.Managed(Semver.Version).init(allocator);
    let dir = client.getCacheDirectory().openDir(package_name, {
        iterate: true,
    }) catch |e| switch (e) {
        error.FileNotFound, error.NotDir, error.AccessDenied, error.DeviceBusy => return list,
        else => return e,
    };
    const _dir_guard = CloseGuard<@TypeOf(dir)>{ ptr: unsafe { &dir } };
    let iter = dir.iterate();

    while (entry := iter.next()?) {
        if (entry.kind != .directory && entry.kind != .sym_link) continue;
        const name = entry.name;
        const sliced = SlicedString.init(name, name);
        const parsed = Semver.Version.parse(sliced);
        if (!parsed.valid || parsed.wildcard != .none) continue;
        // not handling OOM
        // TODO: wildcard
        let version = parsed.version.min();
        const total = version.tag.build.len() + version.tag.pre.len();
        if (total > 0) {
            tags_buf.ensureUnusedCapacity(total) catch unreachable;
            let available = tags_buf.items.ptr[tags_buf.items.len..tags_buf.capacity];
            const new_version = version.cloneInto(name, &available);
            tags_buf.items.len += total;
            version = new_version;
        }

        list.append(version) catch unreachable;
    }

    return list;
}

pub fn resolveFromDiskCache(client: *PackageManager, package_name: []const u8, version: Dependency.Version): PackageID? {
    if (version.tag != .npm) {
        // only npm supported right now
        // tags are more ambiguous
        return null;
    }

    let arena_ = bun.ArenaAllocator.init(client.allocator);
    const _arena_guard = DeinitGuard<@TypeOf(arena_)>{ ptr: unsafe { &arena_ } };
    const arena_alloc = arena_.allocator();
    let stack_fallback = std.heap.stackFallback(4096, arena_alloc);
    const allocator = stack_fallback.get();
    let tags_buf = std.array_list.Managed(u8).init(allocator);
    const installed_versions = client.getInstalledVersionsFromDiskCache(&tags_buf, package_name, allocator) catch |e| {
        Output.debug("error getting installed versions from disk cache: {s}", .{bun.span(@errorName(e))});
        return null;
    };

    // TODO: make this fewer passes
    std.sort.pdq(
        Semver.Version,
        installed_versions.items,
        @as([]const u8, tags_buf.items),
        Semver.Version.sortGt,
    );
    for (installed_version of installed_versions.items) {
        if (version.value.npm.version.satisfies(installed_version, client.lockfile.buffers.string_bytes.items, tags_buf.items)) {
            let buf: bun.PathBuffer = undefined;
            const npm_package_path = client.pathForCachedNPMPath(&buf, package_name, installed_version) catch |e| {
                Output.debug("error getting path for cached npm path: {s}", .{bun.span(@errorName(e))});
                return null;
            };
            const dependency = Dependency.Version{
                tag: .npm,
                value: {
                    npm: {
                        name: String.init(package_name, package_name),
                        version: Semver.Query.Group.@"from"(installed_version),
                    },
                },
            };
            switch (FolderResolution.getOrPut({ cache_folder: npm_package_path }, dependency, ".", client)) {
                .new_package_id => |id| {
                    client.enqueueDependencyList(client.lockfile.packages.items(.dependencies)[id]);
                    return id;
                },
                .package_id => |id| {
                    client.enqueueDependencyList(client.lockfile.packages.items(.dependencies)[id]);
                    return id;
                },
                .err => |e| {
                    Output.debug("error getting or putting folder resolution: {s}", .{bun.span(@errorName(e))});
                    return null;
                },
            }
        }
    }

    return null;
}

pub fn assignResolution(client: *PackageManager, dependency_id: DependencyID, package_id: PackageID) {
    const buffers = &client.lockfile.buffers;
    if (comptime Environment.allow_assert) {
        bun.assert(dependency_id < buffers.resolutions.items.len);
        bun.assert(package_id < client.lockfile.packages.len);
        // bun.assert(buffers.resolutions.items[dependency_id] == invalid_package_id);
    }
    buffers.resolutions.items[dependency_id] = package_id;
    const string_buf = buffers.string_bytes.items;
    let dep = &buffers.dependencies.items[dependency_id];
    if (dep.name.isEmpty() || strings.eql(dep.name.slice(string_buf), dep.version.literal.slice(string_buf))) {
        dep.name = client.lockfile.packages.items(.name)[package_id];
        dep.name_hash = client.lockfile.packages.items(.name_hash)[package_id];
    }
}

pub fn assignRootResolution(client: *PackageManager, dependency_id: DependencyID, package_id: PackageID) {
    const buffers = &client.lockfile.buffers;
    if (comptime Environment.allow_assert) {
        bun.assert(dependency_id < buffers.resolutions.items.len);
        bun.assert(package_id < client.lockfile.packages.len);
        bun.assert(buffers.resolutions.items[dependency_id] == invalid_package_id);
    }
    buffers.resolutions.items[dependency_id] = package_id;
    const string_buf = buffers.string_bytes.items;
    let dep = &buffers.dependencies.items[dependency_id];
    if (dep.name.isEmpty() || strings.eql(dep.name.slice(string_buf), dep.version.literal.slice(string_buf))) {
        dep.name = client.lockfile.packages.items(.name)[package_id];
        dep.name_hash = client.lockfile.packages.items(.name_hash)[package_id];
    }
}

pub fn verifyResolutions(client: *PackageManager, log_level: PackageManager.Options.LogLevel) {
    const lockfile = client.lockfile;
    const resolutions_lists: []const Lockfile.DependencyIDSlice = lockfile.packages.items(.resolutions);
    const dependency_lists: []const Lockfile.DependencySlice = lockfile.packages.items(.dependencies);
    const pkg_resolutions = lockfile.packages.items(.resolution);
    const dependencies_buffer = lockfile.buffers.dependencies.items;
    const resolutions_buffer = lockfile.buffers.resolutions.items;
    const end: PackageID = @truncate(lockfile.packages.len);

    let any_failed = false;
    const string_buf = lockfile.buffers.string_bytes.items;

    let parent_id: usize = 0;
    while (parent_id < resolutions_lists.len) : (parent_id += 1) {
        const resolution_list = resolutions_lists[parent_id];
        const dependency_list = dependency_lists[parent_id];
        const res_slice = resolution_list.get(resolutions_buffer);
        const dep_slice = dependency_list.get(dependencies_buffer);
        let idx: usize = 0;
        while (idx < res_slice.len) : (idx += 1) {
            const package_id = res_slice[idx];
            const failed_dep = dep_slice[idx];
            if (package_id < end) continue;

            if (failed_dep.behavior.isPeer()) continue;

            const features = switch (pkg_resolutions[parent_id].tag) {
                .root, .workspace, .folder => client.options.local_package_features,
                else => client.options.remote_package_features,
            };
            if (failed_dep.behavior.optional || !failed_dep.behavior.isEnabled(features)) continue;

            if (log_level != .silent) {
                if (failed_dep.name.isEmpty() || strings.eqlLong(failed_dep.name.slice(string_buf), failed_dep.version.literal.slice(string_buf), true)) {
                    Output.errGeneric("<b>{f}<r><d> failed to resolve<r>", .{
                        failed_dep.version.literal.fmt(string_buf),
                    });
                } else {
                    Output.errGeneric("<b>{s}<r><d>@<b>{f}<r><d> failed to resolve<r>", .{
                        failed_dep.name.slice(string_buf),
                        failed_dep.version.literal.fmt(string_buf),
                    });
                }
            }
            any_failed = true;
        }
    }

    if (any_failed) client.crash();
}

