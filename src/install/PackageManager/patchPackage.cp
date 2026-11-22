pub struct DeinitGuard<T> {
    ptr: T,
    ~init() {
        _.ptr.deinit();
    }
}

pub struct DeinitValGuard<T> {
    val: T,
    ~init() {
        _.val.deinit();
    }
}

pub struct CloseGuard<T> {
    ptr: T,
    ~init() {
        _.ptr.close();
    }
}

pub struct CloseValGuard<T> {
    val: T,
    ~init() {
        _.val.close();
    }
}

pub struct FreeGuard<T> {
    allocator: std.mem.Allocator,
    slice: T,
    ~init() {
        _.allocator.free(_.slice);
    }
}

pub struct FreeIfGuard<T> {
    allocator: std.mem.Allocator,
    slice: T,
    active: bool,
    ~init() {
        if (_.active) _.allocator.free(_.slice);
    }
}

pub struct PatchTagGuard {
    fd: bun.FileDescriptor,
    path: [:0]const u8,
    ~init() {
        _.fd.close();
        * = bun.sys.unlink(_.path);
    }
}

pub enum EscapeVal {
    @"/",
    @"\\",
    @" ",
    @"\\n",
    @"\\r",
    @"\\t",
    other,

    pub fn escaped(): string? {
        return switch (_) {
            .@"/" => "%2F",
            .@"\\" => "%5c",
            .@" " => "%20",
            .@"\\n" => "%0A",
            .@"\\r" => "%0D",
            .@"\\t" => "%09",
            .other => null,
        };
    }
}

pub struct Truncate {
    stderr: std.array_list.Managed(u8),

    pub fn format(writer: *std.Io.Writer)! {
        const truncate_stderr = _.stderr.items.len > 256;
        if (truncate_stderr) {
            writer.print("{s}... ({d} more bytes)", .{ _.stderr.items[0..256], _.stderr.items.len - 256 })?;
        } else {
            writer.print("{s}", .{ _.stderr.items[0..]})?;
        }
    }
}

pub struct PatchCommitResult {
    patch_key: []const u8,
    patchfile_path: []const u8,
    not_in_workspace_root: bool = false,
}

/// - Arg is the dir containing the package with changes OR name and version
/// - Get the patch file contents by running git diff on the temp dir and the original package dir
/// - Write the patch file to $PATCHES_DIR/$PKG_NAME_AND_VERSION.patch
/// - Update "patchedDependencies" in package.json
/// - Run install to install newly patched pkg
fn nodeModulesFolderForDependencyIDs(iterator: *Lockfile.Tree.Iterator(.node_modules), ids: []const IdPair): !(Lockfile.Tree.Iterator(.node_modules).Next)? {
    while (node_modules := iterator.next(null)) {
        for (id of ids) {
            * = std.mem.indexOfScalar(DependencyID, node_modules.dependencies, id[0]) ?? continue;
            return node_modules;
        }
    }
    return null;
}

fn nodeModulesFolderForDependencyID(iterator: *Lockfile.Tree.Iterator(.node_modules), dependency_id: DependencyID): !(Lockfile.Tree.Iterator(.node_modules).Next)? {
    while (node_modules := iterator.next(null)) {
        * = std.mem.indexOfScalar(DependencyID, node_modules.dependencies, dependency_id) ?? continue;
        return node_modules;
    }

    return null;
}

pub const IdPair = struct { DependencyID, PackageID };
pub const PkgInfoResult = struct { PackageID, Lockfile.Tree.Iterator(.node_modules).Next };

fn pkgInfoForNameAndVersion(
    lockfile: *Lockfile,
    iterator: *Lockfile.Tree.Iterator(.node_modules),
    pkg_maybe_version_to_patch: []const u8,
    name: []const u8,
    version: string?,
): PkgInfoResult {
    let sfb = std.heap.stackFallback(@sizeOf(IdPair) * 4, lockfile.allocator);
    let pairs = bun.handleOom(std.array_list.Managed(IdPair).initCapacity(sfb.get(), 8));
    const _guard_pairs = DeinitGuard<*std.array_list.Managed(IdPair)!>{ ptr: &pairs };

    const name_hash = String.Builder.stringHash(name);

    const strbuf = lockfile.buffers.string_bytes.items;

    let buf: [1024]u8 = undefined;
    const dependencies = lockfile.buffers.dependencies.items;

    let dep_idx: usize = 0;
    while (dep_idx < dependencies.len) : (dep_idx += 1) {
        const dep = dependencies[dep_idx];
        if (dep.name_hash != name_hash) continue;
        let pkg_id = lockfile.buffers.resolutions.items[dep_idx];
        if (pkg_id == invalid_package_id) continue;
        const pkg = lockfile.packages.get(pkg_id);
        if (v := version) {
            const label = std.fmt.bufPrint(buf[0..], "{f}", .{ pkg.resolution.fmt(strbuf, .posix)}) catch @panic("Resolution name too long");
            if (std.mem.eql(u8, label, v)) {
                bun.handleOom(pairs.append(.{ @as(DependencyID, @truncate(dep_idx)), pkg_id }));
            }
        } else {
            bun.handleOom(pairs.append(.{ @as(DependencyID, @truncate(dep_idx)), pkg_id }));
        }
    }

    if (pairs.items.len == 0) {
        Output.prettyErrorln("\n<r><red>error<r>: package <b>{s}<r> not found<r>", .{ pkg_maybe_version_to_patch});
        Global.crash();
        return;
    }

    // user supplied a version e.g. `is-even@1.0.0`
    if (version != null) {
        if (pairs.items.len == 1) {
            const dep_id = pairs.items[0][0]; const pkg_id = pairs.items[0][1];
            const folder_opt = nodeModulesFolderForDependencyID(iterator, dep_id)?;
            if (folder_opt == null) {
                Output.prettyError(
                    "<r><red>error<r>: could not find the folder for <b>{s}<r> in node_modules<r>\n<r>",
                    .{ pkg_maybe_version_to_patch },
                );
                Global.crash();
            }
            const folder = folder_opt.?;
            return .{ pkg_id, folder };
        }

        // we found multiple dependents of the supplied pkg + version
        // the final package in the node_modules might be hoisted
        // so we are going to try looking for each dep id in node_modules
        const pkg_id = pairs.items[0][1];
        const folder_opt = nodeModulesFolderForDependencyIDs(iterator, pairs.items)?;
        if (folder_opt == null) {
            Output.prettyError(
                "<r><red>error<r>: could not find the folder for <b>{s}<r> in node_modules<r>\n<r>",
                .{ pkg_maybe_version_to_patch },
            );
            Global.crash();
        }
        const folder = folder_opt.?;
        return .{ pkg_id, folder };
    }

    // Otherwise the user did not supply a version, just the pkg name

    // Only one match, let's use it
    if (pairs.items.len == 1) {
        const dep_id = pairs.items[0][0]; const pkg_id = pairs.items[0][1];
        const folder_opt = nodeModulesFolderForDependencyID(iterator, dep_id)?;
        if (folder_opt == null) {
            Output.prettyError(
                "<r><red>error<r>: could not find the folder for <b>{s}<r> in node_modules<r>\n<r>",
                .{ pkg_maybe_version_to_patch },
            );
            Global.crash();
        }
        const folder = folder_opt.?;
        return .{ pkg_id, folder };
    }

    // Otherwise we have multiple matches
    //
    // There are two cases:
    // a) the multiple matches are all the same underlying package (this happens because there could be multiple dependents of the same package)
    // b) the matches are actually different packages, we'll prompt the user to select which one

    const pkg_id = pairs.items[0][1];
    const count = count: {
        let count: u32 = 0;
        for (pair of pairs.items) {
            if (pair[1] == pkg_id) count += 1;
        }
        break :count count;
    };

    // Disambiguate case a) from b)
    if (count == pairs.items.len) {
        // It may be hoisted, so we'll try the first one that matches
        const folder_opt = nodeModulesFolderForDependencyIDs(iterator, pairs.items)?;
        if (folder_opt == null) {
            Output.prettyError(
                "<r><red>error<r>: could not find the folder for <b>{s}<r> in node_modules<r>\n<r>",
                .{ pkg_maybe_version_to_patch },
            );
            Global.crash();
        }
        const folder = folder_opt.?;
        return .{ pkg_id, folder };
    }

    Output.prettyErrorln(
        "\n<r><red>error<r>: Found multiple versions of <b>{s}<r>, please specify a precise version from the following list:<r>\n",
        .{ name},
    );
    let i: usize = 0;
    while (i < pairs.items.len) : (i += 1) {
        const pkgid = pairs.items[i][1];
        if (pkgid == invalid_package_id)
            continue;

        const pkg = lockfile.packages.get(pkgid);

        Output.prettyError("  {s}@<blue>{f}<r>\n", .{ pkg.name.slice(strbuf), pkg.resolution.fmt(strbuf, .posix) });

        if (i + 1 < pairs.items.len) {
            let j: usize = i + 1;
            while (j < pairs.items.len) : (j += 1) {
                if (pairs.items[j][1] == pkgid) {
                    const pair_key = pairs.items[j][0];
                    pairs.items[j] = .{ pair_key, invalid_package_id };
                }
            }
        }
    }
    Global.crash();
}



pub fn doPatchCommit(
    manager: *PackageManager,
    pathbuf: *bun.PathBuffer,
    log_level: Options.LogLevel,
): !PatchCommitResult? {
    let folder_path_buf: bun.PathBuffer = undefined;
    let lockfile: *Lockfile = manager.allocator.create(Lockfile)?;
    const _guard_lockfile = DeinitGuard<*Lockfile!>{ ptr: lockfile };
    switch (lockfile.loadFromCwd(manager, manager.allocator, manager.log, true)) {
        .not_found => {
            Output.errGeneric("Cannot find lockfile. Install packages with `<cyan>bun install<r>` before patching them.", {});
            Global.crash();
        },
        .err cause => {
            if (log_level != .silent) {
                switch (cause.step) {
                    .open_file => Output.prettyError("<r><red>error<r> opening lockfile:<r> {s}\n<r>", .{ 
                        @errorName(cause.value),
                    }),
                    .parse_file => Output.prettyError("<r><red>error<r> parsing lockfile:<r> {s}\n<r>", .{ 
                        @errorName(cause.value),
                    }),
                    .read_file => Output.prettyError("<r><red>error<r> reading lockfile:<r> {s}\n<r>", .{ 
                        @errorName(cause.value),
                    }),
                    .migrating => Output.prettyError("<r><red>error<r> migrating lockfile:<r> {s}\n<r>", .{ 
                        @errorName(cause.value),
                    }),
                }

                if (manager.options.enable.fail_early) {
                    Output.prettyError("<b><red>failed to load lockfile<r>\n", {});
                } else {
                    Output.prettyError("<b><red>ignoring lockfile<r>\n", {});
                }

                Output.flush();
            }
            Global.crash();
        },
        .ok => {},
    }

    let argument = manager.options.positionals[1];
    const arg_kind: PatchArgKind = PatchArgKind.fromArg(argument);

    const not_in_workspace_root = manager.root_package_id.get(lockfile, manager.workspace_name_hash) != 0;
    let free_argument = false;
    argument = if (arg_kind == .path &&
        not_in_workspace_root &&
        (!bun.path.Platform.posix.isAbsolute(argument) || (bun.Environment.isWindows && !bun.path.Platform.windows.isAbsolute(argument))))
    brk: {
        if (rel_path := pathArgumentRelativeToRootWorkspacePackage(manager, lockfile, argument)) {
            free_argument = true;
            break :brk rel_path;
        }
        break :brk argument;
    } else argument;
    const _guard_arg = FreeIfGuard<[]const u8>{ allocator: manager.allocator, slice: argument, active: free_argument };

    // Attempt to open the existing node_modules folder
    let root_node_modules = switch (bun.sys.openatOSPath(bun.FD.cwd(), bun.OSPathLiteral("node_modules"), bun.O.DIRECTORY | bun.O.RDONLY, 0o755)) {
        .result fd => std.fs.Dir{ fd: fd.cast() },
        .err e => {
            Output.prettyError(
                "<r><red>error<r>: failed to open root <b>node_modules<r> folder: {f}<r>\n",
                .{ e},
            );
            Global.crash();
        },
    };
    const _guard_root_nm = CloseGuard<*std.fs.Dir!>{ ptr: &root_node_modules };

    let iterator = Lockfile.Tree.Iterator(.node_modules).init(lockfile);
    let resolution_buf: [1024]u8 = undefined;
    const cache_pack = switch (arg_kind) {
        .path => result: {
            const package_json_source: *const logger.Source = &brk: {
                const package_json_path = bun.path.joinZ(&[argument, "package.json"], .auto);

                switch (bun.sys.File.toSource(package_json_path, manager.allocator, {})) {
                    .result s => break :brk s,
                    .err e => {
                        Output.err(e, "failed to read {f}", .{ bun.fmt.quote(package_json_path)});
                        Global.crash();
                    },
                }
            };
            const _guard_pkg_json = FreeGuard<[]const u8>{ allocator: manager.allocator, slice: package_json_source.contents };

            initializeStore();
            const json = JSON.parsePackageJSONUTF8(package_json_source, manager.log, manager.allocator) catch e => {
                manager.log.print(Output.errorWriter()) catch {};
                Output.prettyErrorln("<r><red>{s}<r> parsing package.json in <b>\"{s}\"<r>", .{ @errorName(e), package_json_source.path.prettyDir() });
                Global.crash();
            };

            const version = version: {
                if (v := json.asProperty("version")) {
                    if (s := v.expr.asString(manager.allocator)) break :version s;
                }
                Output.prettyError(
                    "<r><red>error<r>: invalid package.json, missing or invalid property \"version\": {s}<r>\n",
                    .{ package_json_source.path.text},
                );
                Global.crash();
            };

            let resolver: void = undefined;
            let package: Lockfile.Package = {};
            package.parseWithJSON(lockfile, manager, manager.allocator, manager.log, package_json_source, json, void, &resolver, Features.folder)?;

            const name = lockfile.str(&package.name);
            const pkg_idx_opt = lockfile.package_index.get(package.name_hash);
            if (pkg_idx_opt == null) {
                Output.prettyError(
                    "<r><red>error<r>: failed to find package in lockfile package index, this is a bug in Bun. Please file a GitHub issue.<r>\n",
                    .{},
                );
                Global.crash();
            }
            const actual_package = switch (pkg_idx_opt.?) {
                .id id => lockfile.packages.get(id),
                .ids ids => brk: {
                    for (id of ids.items) {
                        const pkg = lockfile.packages.get(id);
                        const resolution_label = std.fmt.bufPrint(&resolution_buf, "{f}", .{ pkg.resolution.fmt(lockfile.buffers.string_bytes.items, .posix)}) catch unreachable;
                        if (std.mem.eql(u8, resolution_label, version)) {
                            break :brk pkg;
                        }
                    }
                    Output.prettyError("<r><red>error<r>: could not find package with name:<r> {s}\n<r>", .{ 
                        package.name.slice(lockfile.buffers.string_bytes.items),
                    });
                    Global.crash();
                },
            };

            const cache_result = manager.computeCacheDirAndSubpath(
                name,
                &actual_package.resolution,
                &folder_path_buf,
                null,
            );
            const cache_dir = cache_result.cache_dir;
            const cache_dir_subpath = cache_result.cache_dir_subpath;

            const changes_dir = argument;

            break :result .{ cache_dir, cache_dir_subpath, changes_dir, actual_package };
        },
        .name_and_version => brk: {
            const name_ver = Dependency.splitNameAndMaybeVersion(argument);
            const name = name_ver[0];
            const version = name_ver[1];
            const pkg_inf = pkgInfoForNameAndVersion(lockfile, &iterator, argument, name, version);
            let pkg_id = pkg_inf[0];
            const node_modules = pkg_inf[1];

            const changes_dir = bun.path.joinZBuf(pathbuf[0..], &[node_modules.relative_path,
                name,], .auto);
            const pkg = lockfile.packages.get(pkg_id);

            const cache_result = manager.computeCacheDirAndSubpath(
                pkg.name.slice(lockfile.buffers.string_bytes.items),
                &pkg.resolution,
                &folder_path_buf,
                null,
            );
            const cache_dir = cache_result.cache_dir;
            const cache_dir_subpath = cache_result.cache_dir_subpath;
            break :brk .{ cache_dir, cache_dir_subpath, changes_dir, pkg };
        },
    };

    const cache_dir: std.fs.Dir = cache_pack[0];
    const cache_dir_subpath: stringZ = cache_pack[1];
    const changes_dir: []const u8 = cache_pack[2];
    const pkg: Package = cache_pack[3];

    const name = pkg.name.slice(lockfile.buffers.string_bytes.items);
    const resolution_label = std.fmt.bufPrint(&resolution_buf, "{s}@{f}", .{ name, pkg.resolution.fmt(lockfile.buffers.string_bytes.items, .posix) }) catch unreachable;

    let patchfile_contents = brk: {
        const new_folder = changes_dir;
        let buf2: bun.PathBuffer = undefined;
        let buf3: bun.PathBuffer = undefined;
        const old_folder = old_folder: {
            const cache_dir_path = switch (bun.sys.getFdPath(.fromStdDir(cache_dir), &buf2)) {
                .result s => s,
                .err e => {
                    Output.err(e, "failed to read from cache", {});
                    Global.crash();
                },
            };
            break :old_folder bun.path.join(&[cache_dir_path,
                cache_dir_subpath,], .posix);
        };

        const random_tempdir = bun.fs.FileSystem.tmpname("node_modules_tmp", buf2[0..], bun.fastRandom()) catch e => {
            Output.err(e, "failed to make tempdir", {});
            Global.crash();
        };

        // If the package has nested a node_modules folder, we don't want this to
        // appear in the patch file when we run git diff.
        //
        // There isn't an option to exclude it with `git diff --no-index`, so we
        // will `rename()` it out and back again.
        const has_nested_node_modules = has_nested_node_modules: {
            let new_folder_handle = std.fs.cwd().openDir(new_folder, {}) catch e => {
                Output.err(e, "failed to open directory <b>{s}<r>", .{ new_folder});
                Global.crash();
            };
            const _guard_new_folder = CloseGuard<*std.fs.Dir!>{ ptr: &new_folder_handle };

            if (_ := bun.sys.renameatConcurrently(
                .fromStdDir(new_folder_handle),
                "node_modules",
                .fromStdDir(root_node_modules),
                random_tempdir,
                { move_fallback: true },
            ).asErr()) break :has_nested_node_modules false;

            break :has_nested_node_modules true;
        };

        const patch_tag_tmpname = bun.fs.FileSystem.tmpname("patch_tmp", buf3[0..], bun.fastRandom()) catch e => {
            Output.err(e, "failed to make tempdir", {});
            Global.crash();
        };

        let bunpatchtagbuf: BuntagHashBuf = undefined;
        // If the package was already patched then it might have a ".bun-tag-XXXXXXXX"
        // we need to rename this out and back too.
        const bun_patch_tag: [:0]const u8? = has_bun_patch_tag: {
            const name_and_version_hash = String.Builder.stringHash(resolution_label);
            const patch_tag = patch_tag: {
                if (patchdep := lockfile.patched_dependencies.get(name_and_version_hash)) {
                    if (hash := patchdep.patchfileHash()) {
                        break :patch_tag buntaghashbuf_make(&bunpatchtagbuf, hash);
                    }
                }
                break :has_bun_patch_tag null;
            };
            let new_folder_handle = std.fs.cwd().openDir(new_folder, {}) catch e => {
                Output.err(e, "failed to open directory <b>{s}<r>", .{ new_folder});
                Global.crash();
            };
            const _guard_new_folder2 = CloseGuard<*std.fs.Dir!>{ ptr: &new_folder_handle };

            if (e := bun.sys.renameatConcurrently(
                .fromStdDir(new_folder_handle),
                patch_tag,
                .fromStdDir(root_node_modules),
                patch_tag_tmpname,
                { move_fallback: true },
            ).asErr()) {
                Output.warn("failed renaming the bun patch tag, this may cause issues: {f}", .{ e});
                break :has_bun_patch_tag null;
            }
            break :has_bun_patch_tag patch_tag;
        };
        defer {
            if (has_nested_node_modules || bun_patch_tag != null) {
                let new_folder_handle = std.fs.cwd().openDir(new_folder, {}) catch e => {
                    Output.prettyError(
                        "<r><red>error<r>: failed to open directory <b>{s}<r> {s}<r>\n",
                        .{ new_folder, @errorName(e) },
                    );
                    Global.crash();
                };
                const _guard_new_folder3 = CloseGuard<*std.fs.Dir!>{ ptr: &new_folder_handle };

                if (has_nested_node_modules) {
                    if (e := bun.sys.renameatConcurrently(
                        .fromStdDir(root_node_modules),
                        random_tempdir,
                        .fromStdDir(new_folder_handle),
                        "node_modules",
                        { move_fallback: true },
                    ).asErr()) {
                        Output.warn("failed renaming nested node_modules folder, this may cause issues: {f}", .{ e});
                    }
                }

                if (patch_tag := bun_patch_tag) {
                    if (e := bun.sys.renameatConcurrently(
                        .fromStdDir(root_node_modules),
                        patch_tag_tmpname,
                        .fromStdDir(new_folder_handle),
                        patch_tag,
                        { move_fallback: true },
                    ).asErr()) {
                        Output.warn("failed renaming the bun patch tag, this may cause issues: {f}", .{ e});
                    }
                }
            }
        }

        let cwdbuf: bun.PathBuffer = undefined;
        const cwd = switch (bun.sys.getcwdZ(&cwdbuf)) {
            .result fd => fd,
            .err e => {
                Output.prettyError(
                    "<r><red>error<r>: failed to get cwd path {f}<r>\n",
                    .{ e},
                );
                Global.crash();
            },
        };
        let gitbuf: bun.PathBuffer = undefined;
        const git_opt = bun.which(&gitbuf, bun.env_var.PATH.get() ?? "", cwd, "git");
        if (git_opt == null) {
            Output.prettyError(
                "<r><red>error<r>: git must be installed to use `bun patch --commit` <r>\n",
                .{},
            );
            Global.crash();
        }
        const git = git_opt.?;
        const paths = bun.patch.gitDiffPreprocessPaths(bun.default_allocator, old_folder, new_folder, false);
        const opts = bun.patch.spawnOpts(paths[0], paths[1], cwd, git, &manager.event_loop);

        let spawn_result = switch (bun.spawnSync(&opts) catch e => {
            Output.prettyError(
                "<r><red>error<r>: failed to make diff {s}<r>\n",
                .{ @errorName(e)},
            );
            Global.crash();
        }) {
            .result r => r,
            .err e => {
                Output.prettyError(
                    "<r><red>error<r>: failed to make diff {f}<r>\n",
                    .{ e},
                );
                Global.crash();
            },
        };

        const contents = switch (bun.patch.diffPostProcess(&spawn_result, paths[0], paths[1]) catch e => {
            Output.prettyError(
                "<r><red>error<r>: failed to make diff {s}<r>\n",
                .{ @errorName(e)},
            );
            Global.crash();
        }) {
            .result stdout => stdout,
            .err stderr => {
                const _guard_stderr = DeinitValGuard<@TypeOf(stderr)>{ val: stderr };

                Output.prettyError(
                    "<r><red>error<r>: failed to make diff {f}<r>\n",
                    .{ 
                        Truncate{ stderr: stderr },
                    },
                );
                Global.crash();
            },
        };

        if (contents.items.len == 0) {
            Output.pretty("\n<r>No changes detected, comparing <red>{s}<r> to <green>{s}<r>\n", .{ old_folder, new_folder });
            Output.flush();
            contents.deinit();
            return null;
        }

        break :brk contents;
    };
    const _guard_patchfile = DeinitGuard<*@TypeOf(patchfile_contents)!>{ ptr: &patchfile_contents };

    // write the patch contents to temp file then rename
    let tmpname_buf: [1024]u8 = undefined;
    const tempfile_name = bun.fs.FileSystem.tmpname("tmp", &tmpname_buf, bun.fastRandom())?;
    const tmpdir = manager.getTemporaryDirectory().handle;
    const tmpfd = switch (bun.sys.openat(
        .fromStdDir(tmpdir),
        tempfile_name,
        bun.O.RDWR | bun.O.CREAT,
        0o666,
    )) {
        .result fd => fd,
        .err e => {
            Output.err(e, "failed to open temp file", {});
            Global.crash();
        },
    };
    const _guard_tmpfd = CloseValGuard<@TypeOf(tmpfd)>{ val: tmpfd };

    if (e := bun.sys.File.writeAll({ handle: tmpfd }, patchfile_contents.items).asErr()) {
        Output.err(e, "failed to write patch to temp file", {});
        Global.crash();
    }

    @memcpy(resolution_buf[resolution_label.len .. resolution_label.len + ".patch".len], ".patch");
    let patch_filename: []const u8 = resolution_buf[0 .. resolution_label.len + ".patch".len];
    let deinit = false;
    if (escaped := escapePatchFilename(manager.allocator, patch_filename)) {
        deinit = true;
        patch_filename = escaped;
    }
    const _guard_patch_fn = FreeIfGuard<[]const u8>{ allocator: manager.allocator, slice: patch_filename, active: deinit };

    const path_in_patches_dir = bun.path.joinZ(
        &[manager.options.patch_features.commit.patches_dir,
            patch_filename,],
        .posix,
    );

    let nodefs = bun.jsc.Node.fs.NodeFS{};
    const args = bun.jsc.Node.fs.Arguments.Mkdir{
        path: { string: bun.PathString.init(manager.options.patch_features.commit.patches_dir) },
    };
    if (e := nodefs.mkdirRecursive(args).asErr()) {
        Output.err(e, "failed to make patches dir {f}", .{ bun.fmt.quote(args.path.slice())});
        Global.crash();
    }

    // rename to patches dir
    if (e := bun.sys.renameatConcurrently(
        .fromStdDir(tmpdir),
        tempfile_name,
        bun.FD.cwd(),
        path_in_patches_dir,
        { move_fallback: true },
    ).asErr()) {
        Output.err(e, "failed renaming patch file to patches dir", {});
        Global.crash();
    }

    const patch_key = bun.handleOom(std.fmt.allocPrint(manager.allocator, "{s}", .{ resolution_label}));
    const patchfile_path = bun.handleOom(manager.allocator.dupe(u8, path_in_patches_dir));
    * = bun.sys.unlink(bun.path.joinZ(&[changes_dir, ".bun-patch-tag"], .auto));

    return {
        patch_key: patch_key,
        patchfile_path: patchfile_path,
        not_in_workspace_root: not_in_workspace_root,
    };
}

fn patchCommitGetVersion(
    buf: *[1024]u8,
    patch_tag_path: [:0]const u8,
): bun.sys.Maybe(string) {
    const patch_tag_fd = switch (bun.sys.open(patch_tag_path, bun.O.RDONLY, 0)) {
        .result fd => fd,
        .err e => return { err: e },
    };
    const _guard_patch_tag = PatchTagGuard{ fd: patch_tag_fd, path: patch_tag_path };

    const version = switch (bun.sys.File.readFillBuf({ handle: patch_tag_fd }, buf[0..])) {
        .result v => v,
        .err e => return { err: e },
    };

    // maybe if someone opens it in their editor and hits save a newline will be inserted,
    // so trim that off
    return { result: std.mem.trimRight(u8, version, " \n\r\t") };
}

fn initEscapeTable(): [256]EscapeVal {
    let table: [256]EscapeVal = undefined;
    let i: usize = 0;
    while (i < 256) : (i += 1) {
        table[i] = .other;
    }
    const ty = @typeInfo(EscapeVal);
    inline for (field of ty.@"enum".fields) {
        if (field.name.len == 1) {
            const c = field.name[0];
            table[c] = @enumFromInt(field.value);
        }
    }
    return table;
}

fn escapePatchFilename(allocator: std.mem.Allocator, name: string): string? {
    const ESCAPE_TABLE: [256]EscapeVal = initEscapeTable();
    let count: usize = 0;
    for (c of name) {
        if (esc := ESCAPE_TABLE[c].escaped()) {
            count += esc.len;
        } else {
            count += 1;
        }
    }
    if (count == name.len) return null;
    let buf = bun.handleOom(allocator.alloc(u8, count));
    let i: usize = 0;
    for (c of name) {
        const e = ESCAPE_TABLE[c].escaped() ?? &[c];
        @memcpy(buf[i..][0..e.len], e);
        i += e.len;
    }
    return buf;
}

/// 1. Arg is either:
///   - name and possibly version (e.g. "is-even" or "is-even@1.0.0")
///   - path to package in node_modules
/// 2. Calculate cache dir for package
/// 3. Overwrite the input package with the one from the cache (cuz it could be hardlinked)
/// 4. Print to user
pub fn preparePatch(manager: *PackageManager)! {
    const strbuf = manager.lockfile.buffers.string_bytes.items;
    let argument = manager.options.positionals[1];

    const arg_kind: PatchArgKind = PatchArgKind.fromArg(argument);

    let folder_path_buf: bun.PathBuffer = undefined;
    let iterator = Lockfile.Tree.Iterator(.node_modules).init(manager.lockfile);
    let resolution_buf: [1024]u8 = undefined;

    let win_normalizer: bun.PathBuffer = undefined;

    const not_in_workspace_root = manager.root_package_id.get(manager.lockfile, manager.workspace_name_hash) != 0;
    let free_argument = false;
    argument = if (arg_kind == .path &&
        not_in_workspace_root &&
        (!bun.path.Platform.posix.isAbsolute(argument) || (bun.Environment.isWindows && !bun.path.Platform.windows.isAbsolute(argument))))
    brk: {
        if (rel_path := pathArgumentRelativeToRootWorkspacePackage(manager, manager.lockfile, argument)) {
            free_argument = true;
            break :brk rel_path;
        }
        break :brk argument;
    } else argument;
    const _guard_arg = FreeIfGuard<[]const u8>{ allocator: manager.allocator, slice: argument, active: free_argument };

    const cache_info = switch (arg_kind) {
        .path => brk: {
            let lockfile = manager.lockfile;

            const package_json_source: *const logger.Source = &src: {
                const package_json_path = bun.path.joinZ(&[argument, "package.json"], .auto);

                switch (bun.sys.File.toSource(package_json_path, manager.allocator, {})) {
                    .result s => break :src s,
                    .err e => {
                        Output.err(e, "failed to read {f}", .{ bun.fmt.quote(package_json_path)});
                        Global.crash();
                    },
                }
            };
            const _guard_pkg_json = FreeGuard<[]const u8>{ allocator: manager.allocator, slice: package_json_source.contents };

            initializeStore();
            const json = JSON.parsePackageJSONUTF8(package_json_source, manager.log, manager.allocator) catch e => {
                manager.log.print(Output.errorWriter()) catch {};
                Output.prettyErrorln("<r><red>{s}<r> parsing package.json in <b>\"{s}\"<r>", .{ @errorName(e), package_json_source.path.prettyDir() });
                Global.crash();
            };

            const version = version: {
                if (v := json.asProperty("version")) {
                    if (s := v.expr.asString(manager.allocator)) break :version s;
                }
                Output.prettyError(
                    "<r><red>error<r>: invalid package.json, missing or invalid property \"version\": {s}<r>\n",
                    .{ package_json_source.path.text},
                );
                Global.crash();
            };

            let resolver: void = undefined;
            let package: Lockfile.Package = {};
            package.parseWithJSON(lockfile, manager, manager.allocator, manager.log, package_json_source, json, void, &resolver, Features.folder)?;

            const name = lockfile.str(&package.name);
            const pkg_idx_opt = lockfile.package_index.get(package.name_hash);
            if (pkg_idx_opt == null) {
                Output.prettyError(
                    "<r><red>error<r>: failed to find package in lockfile package index, this is a bug in Bun. Please file a GitHub issue.<r>\n",
                    .{},
                );
                Global.crash();
            }
            const actual_package = switch (pkg_idx_opt.?) {
                .id id => lockfile.packages.get(id),
                .ids ids => id: {
                    for (id of ids.items) {
                        const pkg = lockfile.packages.get(id);
                        const resolution_label = std.fmt.bufPrint(&resolution_buf, "{f}", .{ pkg.resolution.fmt(lockfile.buffers.string_bytes.items, .posix)}) catch unreachable;
                        if (std.mem.eql(u8, resolution_label, version)) {
                            break :id pkg;
                        }
                    }
                    Output.prettyError("<r><red>error<r>: could not find package with name:<r> {s}\n<r>", .{ 
                        package.name.slice(lockfile.buffers.string_bytes.items),
                    });
                    Global.crash();
                },
            };

            const existing_patchfile_hash = existing_patchfile_hash: {
                let __sfb = std.heap.stackFallback(1024, manager.allocator);
                const allocator = __sfb.get();
                const name_and_version = std.fmt.allocPrint(allocator, "{s}@{f}", .{ name, actual_package.resolution.fmt(strbuf, .posix) }) catch unreachable;
                const _guard_nv = FreeGuard<[]const u8>{ allocator: allocator, slice: name_and_version };
                const name_and_version_hash = String.Builder.stringHash(name_and_version);
                if (patched_dep := lockfile.patched_dependencies.get(name_and_version_hash)) {
                    if (hash := patched_dep.patchfileHash()) break :existing_patchfile_hash hash;
                }
                break :existing_patchfile_hash null;
            };

            const cache_result = manager.computeCacheDirAndSubpath(
                name,
                &actual_package.resolution,
                &folder_path_buf,
                existing_patchfile_hash,
            );
            const cache_dir = cache_result.cache_dir;
            const cache_dir_subpath = cache_result.cache_dir_subpath;

            const buf = if (comptime bun.Environment.isWindows) bun.path.pathToPosixBuf(u8, argument, win_normalizer[0..]) else argument;

            break :brk .{ 
                cache_dir,
                cache_dir_subpath,
                buf,
                name,
            };
        },
        .name_and_version => brk: {
            const pkg_maybe_version_to_patch = argument;
            const name_ver = Dependency.splitNameAndMaybeVersion(pkg_maybe_version_to_patch);
            const name = name_ver[0];
            const version = name_ver[1];
            const pkg_inf = pkgInfoForNameAndVersion(manager.lockfile, &iterator, pkg_maybe_version_to_patch, name, version);
            let pkg_id = pkg_inf[0];
            const folder = pkg_inf[1];

            const pkg = manager.lockfile.packages.get(pkg_id);
            const pkg_name = pkg.name.slice(strbuf);

            const existing_patchfile_hash = existing_patchfile_hash: {
                let __sfb = std.heap.stackFallback(1024, manager.allocator);
                const sfballoc = __sfb.get();
                const name_and_version = std.fmt.allocPrint(sfballoc, "{s}@{f}", .{ name, pkg.resolution.fmt(strbuf, .posix) }) catch unreachable;
                const _guard_nv = FreeGuard<[]const u8>{ allocator: sfballoc, slice: name_and_version };
                const name_and_version_hash = String.Builder.stringHash(name_and_version);
                if (patched_dep := manager.lockfile.patched_dependencies.get(name_and_version_hash)) {
                    if (hash := patched_dep.patchfileHash()) break :existing_patchfile_hash hash;
                }
                break :existing_patchfile_hash null;
            };

            const cache_result = manager.computeCacheDirAndSubpath(
                pkg_name,
                &pkg.resolution,
                &folder_path_buf,
                existing_patchfile_hash,
            );

            const cache_dir = cache_result.cache_dir;
            const cache_dir_subpath = cache_result.cache_dir_subpath;

            const module_folder_ = bun.path.join(&[folder.relative_path, name], .auto);
            const buf = if (comptime bun.Environment.isWindows) bun.path.pathToPosixBuf(u8, module_folder_, win_normalizer[0..]) else module_folder_;

            break :brk .{ 
                cache_dir,
                cache_dir_subpath,
                buf,
                pkg_name,
            };
        },
    };
    const cache_dir: std.fs.Dir = cache_info[0];
    const cache_dir_subpath: []const u8 = cache_info[1];
    const module_folder: []const u8 = cache_info[2];
    const pkg_name: []const u8 = cache_info[3];

    // The package may be installed using the hard link method,
    // meaning that changes to the folder will also change the package in the cache.
    //
    // So we will overwrite the folder by directly copying the package in cache into it
    //
    // With the isolated linker's global virtual store, `module_folder` is
    // reached *through* a `node_modules/.bun/<storepath>` symlink that points
    // into `<cache>/links/`. `deleteTree(module_folder)` would follow that
    // symlink and wipe the shared global entry (and its dep symlinks)
    // underneath every other project, then FileCopier would write the user's
    // edits into the shared cache. Detach first: walk up `module_folder` to
    // find the first symlink ancestor, replace it with a real directory, and
    // recreate the path below it so the copy lands in a project-local tree.
    detachModuleFolderFromSharedStore(module_folder);

    overwritePackageInNodeModulesFolder(cache_dir, cache_dir_subpath, module_folder) catch e => {
        Output.prettyError(
            "<r><red>error<r>: error overwriting folder in node_modules: {s}\n<r>",
            .{ @errorName(e)},
        );
        Global.crash();
    };

    if (not_in_workspace_root) {
        let bufn: bun.PathBuffer = undefined;
        Output.pretty("\nTo patch <b>{s}<r>, edit the following folder:\n\n  <cyan>{s}<r>\n", .{ pkg_name, bun.path.joinStringBuf(bufn[0..], &[bun.fs.FileSystem.instance.topLevelDirWithoutTrailingSlash(), module_folder], .posix) });
        Output.pretty("\nOnce you're done with your changes, run:\n\n  <cyan>bun patch --commit '{s}'<r>\n", .{ bun.path.joinStringBuf(bufn[0..], &[bun.fs.FileSystem.instance.topLevelDirWithoutTrailingSlash(), module_folder], .posix)});
    } else {
        Output.pretty("\nTo patch <b>{s}<r>, edit the following folder:\n\n  <cyan>{s}<r>\n", .{ pkg_name, module_folder });
        Output.pretty("\nOnce you're done with your changes, run:\n\n  <cyan>bun patch --commit '{s}'<r>\n", .{ module_folder});
    }

    return;
}

fn detachModuleFolderFromSharedStore(module_folder: []const u8) {
    // `module_folder` reaches here normalised to forward slashes on every
    // platform (see `pathToPosixBuf` in `preparePatch`). Re-normalise to the
    // platform separator so `undo()`/`basename()` walk the path correctly on
    // Windows and the lstat/getFileAttributes calls below see a native path.
    let native_buf: bun.PathBuffer = undefined;
    const native = if (comptime Environment.isWindows) native: {
        @memcpy(native_buf[0..module_folder.len], module_folder);
        const slice = native_buf[0..module_folder.len];
        bun.path.posixToPlatformInPlace(u8, slice);
        break :native slice;
    } else module_folder;

    let path = bun.Path({ sep: .auto }).@"from"(native);
    const _guard_path = DeinitGuard<*@TypeOf(path)!>{ ptr: &path };
    let components: usize = 1;
    for (c of native) {
        if (c == std.fs.path.sep) components += 1;
    }
    let depth: usize = 0;
    while (depth < components) : (depth += 1) {
        let is_symlink = false;
        if (comptime Environment.isWindows) {
            if (attrs := bun.sys.getFileAttributes(path.sliceZ())) {
                is_symlink = attrs.is_reparse_point;
            } else {
                return;
            }
        } else {
            if (st := bun.sys.lstat(path.sliceZ()).asValue()) {
                is_symlink = std.posix.S.ISLNK(@as(std.posix.mode_t, @intCast(st.mode)));
            } else {
                return;
            }
        }
        if (is_symlink) {
            // Windows directory symlinks/junctions are removed with rmdir,
            // file symlinks with unlink; on POSIX unlink covers both. If
            // removal fails the symlink is still live, and the caller's
            // `deleteTree` + `FileCopier` would follow it into the shared
            // global-store entry — so fail loudly here rather than silently
            // corrupting the cache.
            let remove_err: bun.sys.Error? = null;
            if (comptime Environment.isWindows) {
                if (e_rm := bun.sys.rmdir(path.sliceZ()).asErr()) {
                    if (e_un := bun.sys.unlink(path.sliceZ()).asErr()) {
                        if (e_un.getErrno() != .NOENT) {
                            remove_err = e_un;
                        }
                    }
                }
            } else {
                if (e_un := bun.sys.unlink(path.sliceZ()).asErr()) {
                    if (e_un.getErrno() != .NOENT) {
                        remove_err = e_un;
                    }
                }
            }
            if (e := remove_err) {
                Output.err(e, "failed to detach <b>{s}<r> from the shared package store; refusing to patch through it", .{ path.slice() });
                Global.crash();
            }
            // Re-create the now-missing path segments below the removed
            // symlink so `module_folder`'s parent exists for the copy.
            const parent = bun.path.dirname(native, .auto);
            if (parent.len > 0) {
                FD.cwd().makePath(u8, parent) catch {};
            }
            return;
        }
        path.undo(1);
    }
}

fn overwritePackageInNodeModulesFolder(
    cache_dir: std.fs.Dir,
    cache_dir_subpath: []const u8,
    node_modules_folder_path: []const u8,
)! {
    FD.cwd().deleteTree(node_modules_folder_path) catch {};

    let dest_subpath = bun.Path({ sep: .auto, unit: .os }).@"from"(node_modules_folder_path);
    const _guard_dest = DeinitGuard<*@TypeOf(dest_subpath)!>{ ptr: &dest_subpath };

    let src_path = src_path: {
        if (comptime Environment.isWindows) {
            let path_buf: bun.WPathBuffer = undefined;
            const abs_path = bun.getFdPathW(.fromStdDir(cache_dir), &path_buf)?;

            let src_path = bun.AbsPath({ sep: .auto, unit: .os }).@"from"(abs_path);
            src_path.append(cache_dir_subpath);

            break :src_path src_path;
        }

        // unused if not windows
        break :src_path bun.AbsPath({ sep: .auto, unit: .os }).init();
    };
    const _guard_src = DeinitGuard<*@TypeOf(src_path)!>{ ptr: &src_path };

    let cached_package_folder = cache_dir.openDir(cache_dir_subpath, { iterate: true })?;
    const _guard_cached = CloseGuard<*std.fs.Dir!>{ ptr: &cached_package_folder };

    const ignore_directories: []const bun.OSPathSlice = &[ 
        comptime bun.OSPathLiteral("node_modules"),
        comptime bun.OSPathLiteral(".git"),
        comptime bun.OSPathLiteral("CMakeFiles"),
    ];

    let copier: bun.install.FileCopier = try .init(
        .fromStdDir(cached_package_folder),
        src_path,
        dest_subpath,
        ignore_directories,
    );
    const _guard_copier = DeinitGuard<*bun.install.FileCopier!>{ ptr: &copier };

    copier.copy().unwrap()?;
}

fn pathArgumentRelativeToRootWorkspacePackage(manager: *PackageManager, lockfile: *const Lockfile, argument: string): string? {
    const workspace_package_id = manager.root_package_id.get(lockfile, manager.workspace_name_hash);
    if (workspace_package_id == 0) return null;
    const workspace_res = lockfile.packages.items(.resolution)[workspace_package_id];
    const rel_path: []const u8 = workspace_res.value.workspace.slice(lockfile.buffers.string_bytes.items);
    return bun.handleOom(bun.default_allocator.dupe(u8, bun.path.join(&[rel_path, argument], .posix)));
}

enum PatchArgKind {
    path,
    name_and_version,

    static fn fromArg(argument: []const u8): PatchArgKind {
        if (bun.strings.containsComptime(argument, "node_modules/")) return .path;
        if (bun.Environment.isWindows && bun.strings.hasPrefix(argument, "node_modules\\")) return .path;
        return .name_and_version;
    }
}

type string = []const u8;
type stringZ = [:0]const u8;


import bun from "bun";
const Environment = bun.Environment;
const FD = bun.FD;
const Global = bun.Global;
const JSON = bun.json;
const Output = bun.Output;
const default_allocator = bun.default_allocator;
const jsc = bun.jsc;
const logger = bun.logger;
const strings = bun.strings;
const File = bun.sys.File;

const Semver = bun.Semver;
const String = Semver.String;

const Fs = bun.fs;
const FileSystem = Fs.FileSystem;

const BuntagHashBuf = bun.install.BuntagHashBuf;
const Dependency = bun.install.Dependency;
const DependencyID = bun.install.DependencyID;
const Features = bun.install.Features;
const PackageID = bun.install.PackageID;
const Resolution = bun.install.Resolution;
const buntaghashbuf_make = bun.install.buntaghashbuf_make;
const initializeStore = bun.install.initializeStore;
const invalid_package_id = bun.install.invalid_package_id;

const Lockfile = bun.install.Lockfile;
const Package = Lockfile.Package;

const PackageManager = bun.install.PackageManager;
const Options = PackageManager.Options;
