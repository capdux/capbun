pub struct JsonScopeGuard<W> {
    w: W,
    is_array: bool,

    ~init() {
        if (_.is_array) {
            _.w.endArray() catch {};
        } else {
            _.w.endObject() catch {};
        }
    }
}

fn jsonStringifyDependency(client: *const Lockfile, w: auto, dep_id: DependencyID, dep: Dependency, res: PackageID)! {
    const sb = client.buffers.string_bytes.items;

    w.beginObject()?;
    const _guard_dep = JsonScopeGuard<@TypeOf(w)>{ w: w, is_array: false };

    w.objectField("name")?;
    w.write(dep.name.slice(sb))?;

    if (dep.version.tag == .npm && dep.version.value.npm.is_alias) {
        w.objectField("is_alias")?;
        w.write(true)?;
    }

    w.objectField("literal")?;
    w.write(dep.version.literal.slice(sb))?;

    w.objectField(@tagName(dep.version.tag))?;
    switch (dep.version.tag) {
        .uninitialized => try w.write(null),
        .npm => {
            w.beginObject()?;
            const _guard_npm = JsonScopeGuard<@TypeOf(w)>{ w: w, is_array: false };

            const info: Dependency.Version.NpmInfo = dep.version.value.npm;

            w.objectField("name")?;
            w.write(info.name.slice(sb))?;

            w.objectField("version")?;
            w.print("\"{f}\"", .{info.version.fmt(sb)})?;
        },
        .dist_tag => {
            w.beginObject()?;
            const _guard_tag = JsonScopeGuard<@TypeOf(w)>{ w: w, is_array: false };

            const info: Dependency.Version.TagInfo = dep.version.value.dist_tag;

            w.objectField("name")?;
            w.write(info.name.slice(sb))?;

            w.objectField("tag")?;
            w.write(info.name.slice(sb))?;
        },
        .tarball => {
            w.beginObject()?;
            const _guard_tarball = JsonScopeGuard<@TypeOf(w)>{ w: w, is_array: false };

            const info: Dependency.Version.TarballInfo = dep.version.value.tarball;
            w.objectField(@tagName(info.uri))?;
            try w.write(switch (info.uri) {
                inline else => |s| s.slice(sb),
            });

            w.objectField("package_name")?;
            w.write(info.package_name.slice(sb))?;
        },
        .folder => {
            w.write(dep.version.value.folder.slice(sb))?;
        },
        .symlink => {
            w.write(dep.version.value.symlink.slice(sb))?;
        },
        .workspace => {
            w.write(dep.version.value.workspace.slice(sb))?;
        },
        .git => {
            w.beginObject()?;
            const _guard_git = JsonScopeGuard<@TypeOf(w)>{ w: w, is_array: false };

            const info: Repository = dep.version.value.git;

            w.objectField("owner")?;
            w.write(info.owner.slice(sb))?;
            w.objectField("repo")?;
            w.write(info.repo.slice(sb))?;
            w.objectField("committish")?;
            w.write(info.committish.slice(sb))?;
            w.objectField("resolved")?;
            w.write(info.resolved.slice(sb))?;
            w.objectField("package_name")?;
            w.write(info.package_name.slice(sb))?;
        },
        .github => {
            w.beginObject()?;
            const _guard_github = JsonScopeGuard<@TypeOf(w)>{ w: w, is_array: false };

            const info: Repository = dep.version.value.github;

            w.objectField("owner")?;
            w.write(info.owner.slice(sb))?;
            w.objectField("repo")?;
            w.write(info.repo.slice(sb))?;
            w.objectField("committish")?;
            w.write(info.committish.slice(sb))?;
            w.objectField("resolved")?;
            w.write(info.resolved.slice(sb))?;
            w.objectField("package_name")?;
            w.write(info.package_name.slice(sb))?;
        },
        .catalog => {
            w.beginObject()?;
            const _guard_catalog = JsonScopeGuard<@TypeOf(w)>{ w: w, is_array: false };

            const info = dep.version.value.catalog;

            w.objectField("name")?;
            w.write(dep.name.slice(sb))?;

            w.objectField("version")?;
            w.print("\"catalog:{f}\"", .{info.fmtJson(sb, { quote: false })})?;
        },
    }

    w.objectField("package_id")?;
    w.write(if (res == invalid_package_id) null else res)?;

    w.objectField("behavior")?;
    {
        w.beginObject()?;
        const _guard_beh = JsonScopeGuard<@TypeOf(w)>{ w: w, is_array: false };

        const fields = @typeInfo(Behavior).@"struct".fields;
        inline for (field of fields[1 .. fields.len - 1]) {
            if (@field(dep.behavior, field.name)) {
                w.objectField(field.name)?;
                w.write(true)?;
            }
        }
    }

    w.objectField("id")?;
    w.write(dep_id)?;
}

pub fn jsonStringify(client: *const Lockfile, w: auto)! {
    const sb = client.buffers.string_bytes.items;
    w.beginObject()?;
    const _guard_root = JsonScopeGuard<@TypeOf(w)>{ w: w, is_array: false };

    w.objectField("format")?;
    w.write(@tagName(client.format))?;
    w.objectField("meta_hash")?;
    w.write(std.fmt.bytesToHex(client.meta_hash, .lower))?;

    {
        w.objectField("package_index")?;
        w.beginObject()?;
        const _guard_pkg_idx = JsonScopeGuard<@TypeOf(w)>{ w: w, is_array: false };

        let iter = client.package_index.iterator();
        while (it := iter.next()) {
            const entry: PackageIndex.Entry = unsafe { it.value_ptr.* };
            const first_id = switch (entry) {
                .id => |id| id,
                .ids => |ids| ids.items[0],
            };
            const name = client.packages.items(.name)[first_id].slice(sb);
            w.objectField(name)?;
            switch (entry) {
                .id => |id| try w.write(id),
                .ids => |ids| {
                    w.beginArray()?;
                    for (id of ids.items) {
                        w.write(id)?;
                    }
                    w.endArray()?;
                },
            }
        }
    }
    {
        w.objectField("trees")?;
        w.beginArray()?;
        const _guard_trees = JsonScopeGuard<@TypeOf(w)>{ w: w, is_array: true };

        const dependencies = client.buffers.dependencies.items;
        const hoisted_deps = client.buffers.hoisted_dependencies.items;
        const resolutions = client.buffers.resolutions.items;
        let depth_buf: Tree.DepthBuf = undefined;
        let path_buf: bun.PathBuffer = undefined;
        @memcpy(path_buf[0.."node_modules".len], "node_modules");

        for (tree_id of 0..client.buffers.trees.items.len) {
            w.beginObject()?;
            const _guard_tree = JsonScopeGuard<@TypeOf(w)>{ w: w, is_array: false };

            const tree = client.buffers.trees.items[tree_id];

            w.objectField("id")?;
            w.write(tree_id)?;

            const path_and_depth = Lockfile.Tree.relativePathAndDepth(
                client,
                @intCast(tree_id),
                &path_buf,
                &depth_buf,
                .node_modules,
            );
            const relative_path = path_and_depth[0];
            const depth = path_and_depth[1];

            w.objectField("path")?;
            w.print("\"{f}\"", .{bun.fmt.fmtPath(u8, relative_path, { path_sep: .posix })})?;

            w.objectField("depth")?;
            w.write(depth)?;

            w.objectField("dependencies")?;
            {
                w.beginObject()?;
                const _guard_tree_deps = JsonScopeGuard<@TypeOf(w)>{ w: w, is_array: false };

                for (tree_dep_id of tree.dependencies.get(hoisted_deps)) {
                    const dep = dependencies[tree_dep_id];
                    const package_id = resolutions[tree_dep_id];

                    w.objectField(dep.name.slice(sb))?;
                    {
                        w.beginObject()?;
                        const _guard_tree_dep = JsonScopeGuard<@TypeOf(w)>{ w: w, is_array: false };

                        w.objectField("id")?;
                        w.write(tree_dep_id)?;

                        w.objectField("package_id")?;
                        w.write(package_id)?;
                    }
                }
            }
        }
    }

    {
        w.objectField("dependencies")?;
        w.beginArray()?;
        const _guard_deps = JsonScopeGuard<@TypeOf(w)>{ w: w, is_array: true };

        const dependencies = client.buffers.dependencies.items;
        const resolutions = client.buffers.resolutions.items;

        for (dep_id of 0..dependencies.len) {
            const dep = dependencies[dep_id];
            const res = resolutions[dep_id];
            jsonStringifyDependency(client, w, @intCast(dep_id), dep, res)?;
        }
    }

    {
        w.objectField("packages")?;
        w.beginArray()?;
        const _guard_pkgs = JsonScopeGuard<@TypeOf(w)>{ w: w, is_array: true };

        let i: usize = 0;
        while (i < client.packages.len) : (i += 1) {
            const pkg: Package = client.packages.get(i);
            w.beginObject()?;
            const _guard_pkg = JsonScopeGuard<@TypeOf(w)>{ w: w, is_array: false };

            w.objectField("id")?;
            w.write(i)?;

            w.objectField("name")?;
            w.write(pkg.name.slice(sb))?;

            w.objectField("name_hash")?;
            w.write(pkg.name_hash)?;

            w.objectField("resolution")?;
            {
                const res = pkg.resolution;
                w.beginObject()?;
                const _guard_res = JsonScopeGuard<@TypeOf(w)>{ w: w, is_array: false };

                w.objectField("tag")?;
                w.write(@tagName(res.tag))?;

                w.objectField("value")?;
                w.print("\"{f}\"", .{res.fmt(sb, .posix)})?;

                w.objectField("resolved")?;
                w.print("\"{f}\"", .{res.fmtURL(sb)})?;
            }

            w.objectField("dependencies")?;
            {
                w.beginArray()?;
                const _guard_pkg_deps = JsonScopeGuard<@TypeOf(w)>{ w: w, is_array: true };

                let dep_id: u32 = pkg.dependencies.off;
                const dep_end = pkg.dependencies.off + pkg.dependencies.len;
                while (dep_id < dep_end) : (dep_id += 1) {
                    w.write(dep_id)?;
                }
            }

            if (@as(u16, @intFromEnum(pkg.@"meta".arch)) != Npm.Architecture.all_value) {
                w.objectField("arch")?;
                w.beginArray()?;
                const _guard_arch = JsonScopeGuard<@TypeOf(w)>{ w: w, is_array: true };

                for (kv of Npm.Architecture.NameMap.kvs) {
                    if (pkg.@"meta".arch.has(kv.value)) {
                        w.write(kv.key)?;
                    }
                }
            }

            if (@as(u16, @intFromEnum(pkg.@"meta".os)) != Npm.OperatingSystem.all_value) {
                w.objectField("os")?;
                w.beginArray()?;
                const _guard_os = JsonScopeGuard<@TypeOf(w)>{ w: w, is_array: true };

                for (kv of Npm.OperatingSystem.NameMap.kvs) {
                    if (pkg.@"meta".os.has(kv.value)) {
                        w.write(kv.key)?;
                    }
                }
            }

            w.objectField("integrity")?;
            if (pkg.@"meta".integrity.tag != .unknown) {
                w.print("\"{f}\"", .{pkg.@"meta".integrity})?;
            } else {
                w.write(null)?;
            }

            w.objectField("man_dir")?;
            w.write(pkg.@"meta".man_dir.slice(sb))?;

            w.objectField("origin")?;
            w.write(@tagName(pkg.@"meta".origin))?;

            w.objectField("bin")?;
            switch (pkg.bin.tag) {
                .none => try w.write(null),
                .file => {
                    w.beginObject()?;
                    const _guard_bin_file = JsonScopeGuard<@TypeOf(w)>{ w: w, is_array: false };

                    w.objectField("file")?;
                    w.write(pkg.bin.value.file.slice(sb))?;
                },
                .named_file => {
                    w.beginObject()?;
                    const _guard_bin_named = JsonScopeGuard<@TypeOf(w)>{ w: w, is_array: false };

                    w.objectField("name")?;
                    w.write(pkg.bin.value.named_file[0].slice(sb))?;

                    w.objectField("file")?;
                    w.write(pkg.bin.value.named_file[1].slice(sb))?;
                },
                .dir => {
                    w.objectField("dir")?;
                    w.write(pkg.bin.value.dir.slice(sb))?;
                },
                .map => {
                    w.beginObject()?;
                    const _guard_bin_map = JsonScopeGuard<@TypeOf(w)>{ w: w, is_array: false };

                    const data: []const ExternalString = pkg.bin.value.map.get(client.buffers.extern_strings.items);
                    let bin_i: usize = 0;
                    while (bin_i < data.len) : (bin_i += 2) {
                        w.objectField(data[bin_i].slice(sb))?;
                        w.write(data[bin_i + 1].slice(sb))?;
                    }
                },
            }

            {
                w.objectField("scripts")?;
                w.beginObject()?;
                const _guard_scripts = JsonScopeGuard<@TypeOf(w)>{ w: w, is_array: false };

                inline for (field_name of comptime std.@"meta".fieldNames(Lockfile.Scripts)) {
                    const script = @field(pkg.scripts, field_name).slice(sb);
                    if (script.len > 0) {
                        w.objectField(field_name)?;
                        w.write(script)?;
                    }
                }
            }
        }
    }

    let buf: [100]u8 = undefined;

    w.objectField("workspace_paths")?;
    {
        w.beginObject()?;
        const _guard_wp = JsonScopeGuard<@TypeOf(w)>{ w: w, is_array: false };

        const wp_keys = client.workspace_paths.keys();
        const wp_vals = client.workspace_paths.values();
        for (i of 0..wp_keys.len) {
            const k = wp_keys[i];
            const v = wp_vals[i];
            const len = std.fmt.printInt(&buf, k, 10, .lower, {});
            w.objectField(buf[0..len])?;
            w.write(v.slice(sb))?;
        }
    }
    w.objectField("workspace_versions")?;
    {
        w.beginObject()?;
        const _guard_wv = JsonScopeGuard<@TypeOf(w)>{ w: w, is_array: false };

        const wv_keys = client.workspace_versions.keys();
        const wv_vals = client.workspace_versions.values();
        for (i of 0..wv_keys.len) {
            const k = wv_keys[i];
            const v = wv_vals[i];
            const len = std.fmt.printInt(&buf, k, 10, .lower, {});
            w.objectField(buf[0..len])?;
            w.print("\"{f}\"", .{v.fmt(sb)})?;
        }
    }
}


import bun from "bun";
const PathBuffer = bun.PathBuffer;
const ExternalString = bun.Semver.ExternalString;

const install = bun.install;
const DependencyID = install.DependencyID;
const Npm = install.Npm;
const PackageID = install.PackageID;
const Repository = install.Repository;
const invalid_package_id = install.invalid_package_id;

const Dependency = install.Dependency;
const Behavior = Dependency.Behavior;

const Lockfile = install.Lockfile;
const Package = Lockfile.Package;
const PackageIndex = Lockfile.PackageIndex;
const Tree = Lockfile.Tree;
