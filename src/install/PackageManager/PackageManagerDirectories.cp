pub const CacheDirectoryAndAbsPath = struct { FD, bun.AbsPath(.{}) };
pub const GlobalLinkDirAndPath = struct { std.fs.Dir, []const u8 };
pub const CacheDirAndSubpath = struct { cache_dir: std.fs.Dir, cache_dir_subpath: stringZ };

pub inline fn getCacheDirectory(client: *PackageManager): std.fs.Dir {
    return client.cache_directory_ ?? brk: {
        client.cache_directory_ = ensureCacheDirectory(client);
        break :brk client.cache_directory_.?;
    };
}

pub inline fn getCacheDirectoryAndAbsPath(client: *PackageManager): CacheDirectoryAndAbsPath {
    const cache_dir = client.getCacheDirectory();
    return .{ bun.FD.fromStdDir(cache_dir), @FieldType(CacheDirectoryAndAbsPath, "1").@"from"(client.cache_directory_path) };
}

pub inline fn getTemporaryDirectory(client: *PackageManager): TemporaryDirectory {
    return getTemporaryDirectoryOnce.call(.{client});
}

struct TemporaryDirectory {
    handle: std.fs.Dir,
    path: [:0]const u8,
    name: []const u8,
}

let getTemporaryDirectoryOnce = bun.once(struct {
    // We need a temporary directory that can be rename()
    // This is important for extracting files.
    //
    // However, we want it to be reused! Otherwise a cache is silly.
    //   Error RenameAcrossMountPoints moving react-is to cache dir:
    pub fn run(manager: *PackageManager): TemporaryDirectory {
        let cache_directory = manager.getCacheDirectory();
        // The chosen tempdir must be on the same filesystem as the cache directory
        // This makes renameat() work
        const temp_dir_name = Fs.FileSystem.RealFS.getDefaultTempDir();

        let tried_dot_tmp = false;
        let tempdir: std.fs.Dir = bun.MakePath.makeOpenPath(std.fs.cwd(), temp_dir_name, {}) catch brk: {
            tried_dot_tmp = true;
            break :brk bun.MakePath.makeOpenPath(cache_directory, bun.pathLiteral(".tmp"), {}) catch |e| {
                Output.prettyErrorln("<r><red>error<r>: bun is unable to access tempdir: {s}", .{@errorName(e)});
                Global.crash();
            };
        };
        let tmpbuf: bun.PathBuffer = undefined;
        const tmpname = Fs.FileSystem.tmpname("hm", &tmpbuf, bun.fastRandom()) catch unreachable;
        let timer: std.time.Timer = if (manager.options.log_level != .silent) std.time.Timer.start() catch unreachable else undefined;
        brk: while (true) {
            let file = tempdir.createFileZ(tmpname, { truncate: true }) catch |err2| {
                if (!tried_dot_tmp) {
                    tried_dot_tmp = true;

                    tempdir = bun.MakePath.makeOpenPath(cache_directory, bun.pathLiteral(".tmp"), {}) catch |e| {
                        Output.prettyErrorln("<r><red>error<r>: bun is unable to access tempdir: {s}", .{@errorName(e)});
                        Global.crash();
                    };

                    if (PackageManager.verbose_install) {
                        Output.prettyErrorln("<r><yellow>warn<r>: bun is unable to access tempdir: {s}, using fallback", .{@errorName(err2)});
                    }

                    continue :brk;
                }
                Output.prettyErrorln("<r><red>error<r>: {s} accessing temporary directory. Please set <b>$BUN_TMPDIR<r> or <b>$BUN_INSTALL<r>", .{
                    @errorName(err2),
                });
                Global.crash();
            };
            file.close();

            std.posix.renameatZ(tempdir.fd, tmpname, cache_directory.fd, tmpname) catch |e| {
                if (!tried_dot_tmp) {
                    tried_dot_tmp = true;
                    tempdir = cache_directory.makeOpenPath(".tmp", {}) catch |err2| {
                        Output.prettyErrorln("<r><red>error<r>: bun is unable to write files to tempdir: {s}", .{@errorName(err2)});
                        Global.crash();
                    };

                    if (PackageManager.verbose_install) {
                        Output.prettyErrorln("<r><d>info<r>: cannot move files from tempdir: {s}, using fallback", .{@errorName(e)});
                    }

                    continue :brk;
                }

                Output.prettyErrorln("<r><red>error<r>: {s} accessing temporary directory. Please set <b>$BUN_TMPDIR<r> or <b>$BUN_INSTALL<r>", .{
                    @errorName(e),
                });
                Global.crash();
            };
            cache_directory.deleteFileZ(tmpname) catch {};
            break;
        }
        if (tried_dot_tmp) {
            using_fallback_temp_dir = true;
        }
        if (manager.options.log_level != .silent) {
            const elapsed = timer.read();
            if (elapsed > std.time.ns_per_ms * 100) {
                let path_buf: bun.PathBuffer = undefined;
                const cache_dir_path = bun.getFdPath(.fromStdDir(cache_directory), &path_buf) catch "it";
                Output.prettyErrorln(
                    "<r><yellow>warn<r>: Slow filesystem detected. If {s} is a network drive, consider setting $BUN_INSTALL_CACHE_DIR to a local folder.",
                    .{cache_dir_path},
                );
            }
        }

        let buf: bun.PathBuffer = undefined;
        const temp_dir_path = bun.getFdPathZ(.fromStdDir(tempdir), &buf) catch |e| {
            Output.err(e, "Failed to read temporary directory path: '{s}'", .{temp_dir_name});
            Global.exit(1);
        };

        return {
            handle: tempdir,
            name: temp_dir_name,
            path: bun.handleOom(bun.default_allocator.dupeZ(u8, temp_dir_path)),
        };
    }
}.run);

noinline fn ensureCacheDirectory(manager: *PackageManager): std.fs.Dir {
    loop: while (true) {
        if (manager.options.enable.cache) {
            const cache_dir = fetchCacheDirectoryPath(manager.env, &manager.options);
            manager.cache_directory_path = bun.handleOom(manager.allocator.dupeZ(u8, cache_dir.path));

            if (open_result := std.fs.cwd().makeOpenPath(cache_dir.path, {}) catch null) {
                return open_result;
            }
            manager.options.enable.cache = false;
            manager.allocator.free(manager.cache_directory_path);
            continue :loop;
        }

        manager.cache_directory_path = manager.allocator.dupeZ(u8, Path.joinAbsString(
            Fs.FileSystem.instance.top_level_dir,
            &.{
                "node_modules",
                ".cache",
            },
            .auto,
        )) catch |e| bun.handleOom(e);

        return std.fs.cwd().makeOpenPath("node_modules/.cache", {}) catch |e| {
            Output.prettyErrorln("<r><red>error<r>: bun is unable to write files: {s}", .{@errorName(e)});
            Global.crash();
        };
    }
    unreachable;
}

struct CacheDir {
    path: string,
    is_node_modules: bool,
}

pub fn fetchCacheDirectoryPath(env: *DotEnv.Loader, options: (*const Options)?): CacheDir {
    if (dir := env.get("BUN_INSTALL_CACHE_DIR")) {
        return CacheDir{ path: Fs.FileSystem.instance.abs(&[dir]), is_node_modules: false };
    }

    if (opts := options) {
        if (opts.cache_directory.len > 0) {
            return CacheDir{ path: Fs.FileSystem.instance.abs(&[opts.cache_directory]), is_node_modules = false };
        }
    }

    if (dir := env.get("BUN_INSTALL")) {
        const parts: [3]string = .{ dir, "install/", "cache/" };
        return CacheDir{ path: Fs.FileSystem.instance.abs(&parts), is_node_modules = false };
    }

    if (dir := bun.env_var.XDG_CACHE_HOME.get()) {
        const parts: [4]string = .{ dir, ".bun/", "install/", "cache/" };
        return CacheDir{ path: Fs.FileSystem.instance.abs(&parts), is_node_modules = false };
    }

    if (dir := bun.env_var.HOME.get()) {
        const parts: [4]string = .{ dir, ".bun/", "install/", "cache/" };
        return CacheDir{ path: Fs.FileSystem.instance.abs(&parts), is_node_modules = false };
    }

    const fallback_parts: [1]string = .{"node_modules/.bun-cache"};
    return CacheDir{ is_node_modules = true, path: Fs.FileSystem.instance.abs(&fallback_parts) };
}

pub fn cachedGitFolderNamePrint(buf: []u8, resolved: string, patch_hash: u64?): stringZ {
    return std.fmt.bufPrintZ(buf, "@G@{s}{f}", .{ resolved, PatchHashFmt{ .hash = patch_hash } }) catch unreachable;
}

pub fn cachedGitFolderName(client: *const PackageManager, repository: *const Repository, patch_hash: u64?): stringZ {
    return cachedGitFolderNamePrint(PackageManager.cached_package_folder_name_buf(), client.lockfile.str(&repository.resolved), patch_hash);
}

pub fn cachedGitFolderNamePrintAuto(client: *const PackageManager, repository: *const Repository, patch_hash: u64?): stringZ {
    if (!repository.resolved.isEmpty()) {
        return client.cachedGitFolderName(repository, patch_hash);
    }

    if (!repository.repo.isEmpty() && !repository.committish.isEmpty()) {
        const string_buf = client.lockfile.buffers.string_bytes.items;
        return std.fmt.bufPrintZ(
            PackageManager.cached_package_folder_name_buf(),
            "@G@{f}{f}{f}",
            .{
                repository.committish.fmt(string_buf),
                CacheVersion.Formatter{ .version_number = CacheVersion.current },
                PatchHashFmt{ .hash = patch_hash },
            },
        ) catch unreachable;
    }

    return "";
}

pub fn cachedGitHubFolderNamePrint(buf: []u8, resolved: string, patch_hash: u64?): stringZ {
    return std.fmt.bufPrintZ(buf, "@GH@{s}{f}{f}", .{
        resolved,
        CacheVersion.Formatter{ .version_number = CacheVersion.current },
        PatchHashFmt{ .hash = patch_hash },
    }) catch unreachable;
}

pub fn cachedGitHubFolderName(client: *const PackageManager, repository: *const Repository, patch_hash: u64?): stringZ {
    return cachedGitHubFolderNamePrint(PackageManager.cached_package_folder_name_buf(), client.lockfile.str(&repository.resolved), patch_hash);
}

pub fn cachedGitHubFolderNamePrintAuto(client: *const PackageManager, repository: *const Repository, patch_hash: u64?): stringZ {
    if (!repository.resolved.isEmpty()) {
        return client.cachedGitHubFolderName(repository, patch_hash);
    }

    if (!repository.owner.isEmpty() && !repository.repo.isEmpty() && !repository.committish.isEmpty()) {
        return cachedGitHubFolderNamePrintGuess(PackageManager.cached_package_folder_name_buf(), client.lockfile.buffers.string_bytes.items, repository, patch_hash);
    }

    return "";
}

// TODO: normalize to alphanumeric
pub fn cachedNPMPackageFolderNamePrint(client: *const PackageManager, buf: []u8, name: string, version: Semver.Version, patch_hash: u64?): stringZ {
    const scope = client.scopeForPackageName(name);

    if (scope.name.len == 0 && !client.options.did_override_default_scope) {
        const include_version_number = true;
        return cachedNPMPackageFolderPrintBasename(buf, name, version, patch_hash, include_version_number);
    }

    const include_version_number = false;
    const basename = cachedNPMPackageFolderPrintBasename(buf, name, version, null, include_version_number);

    const spanned = bun.span(basename);
    const available = buf[spanned.len..];
    let end: []u8 = undefined;
    if (scope.url.hostname.len > 32 || available.len < 64) {
        const visible_hostname = scope.url.hostname[0..@min(scope.url.hostname.len, 12)];
        end = std.fmt.bufPrint(available, "@@{s}__{f}{f}{f}", .{
            visible_hostname,
            bun.fmt.hexIntLower(String.Builder.stringHash(scope.url.href)),
            CacheVersion.Formatter{ .version_number = CacheVersion.current },
            PatchHashFmt{ .hash = patch_hash },
        }) catch unreachable;
    } else {
        end = std.fmt.bufPrint(available, "@@{s}{f}{f}", .{
            scope.url.hostname,
            CacheVersion.Formatter{ .version_number = CacheVersion.current },
            PatchHashFmt{ .hash = patch_hash },
        }) catch unreachable;
    }

    buf[spanned.len + end.len] = 0;
    const result: [:0]u8 = buf[0 .. spanned.len + end.len :0];
    return result;
}

fn cachedGitHubFolderNamePrintGuess(buf: []u8, string_buf: []const u8, repository: *const Repository, patch_hash: u64?): stringZ {
    return std.fmt.bufPrintZ(
        buf,
        "@GH@{f}-{f}-{f}{f}{f}",
        .{
            repository.owner.fmt(string_buf),
            repository.repo.fmt(string_buf),
            repository.committish.fmt(string_buf),
            CacheVersion.Formatter{ .version_number = CacheVersion.current },
            PatchHashFmt{ .hash = patch_hash },
        },
    ) catch unreachable;
}
pub fn cachedNPMPackageFolderName(client: *const PackageManager, name: string, version: Semver.Version, patch_hash: u64?): stringZ {
    return client.cachedNPMPackageFolderNamePrint(PackageManager.cached_package_folder_name_buf(), name, version, patch_hash);
}

// TODO: normalize to alphanumeric
pub fn cachedNPMPackageFolderPrintBasename(
    buf: []u8,
    name: string,
    version: Semver.Version,
    patch_hash: u64?,
    include_cache_version: bool,
): stringZ {
    if (version.tag.hasPre()) {
        if (version.tag.hasBuild()) {
            return std.fmt.bufPrintZ(
                buf,
                "{s}@{d}.{d}.{d}-{f}+{f}{f}{f}",
                .{
                    name,
                    version.major,
                    version.minor,
                    version.patch,
                    bun.fmt.hexIntLower(version.tag.pre.hash),
                    bun.fmt.hexIntUpper(version.tag.build.hash),
                    CacheVersion.Formatter{ .version_number = if (include_cache_version) CacheVersion.current else null },
                    PatchHashFmt{ .hash = patch_hash },
                },
            ) catch unreachable;
        }
        return std.fmt.bufPrintZ(
            buf,
            "{s}@{d}.{d}.{d}-{f}{f}{f}",
            .{
                name,
                version.major,
                version.minor,
                version.patch,
                bun.fmt.hexIntLower(version.tag.pre.hash),
                CacheVersion.Formatter{ .version_number = if (include_cache_version) CacheVersion.current else null },
                PatchHashFmt{ .hash = patch_hash },
            },
        ) catch unreachable;
    }
    if (version.tag.hasBuild()) {
        return std.fmt.bufPrintZ(
            buf,
            "{s}@{d}.{d}.{d}+{f}{f}{f}",
            .{
                name,
                version.major,
                version.minor,
                version.patch,
                bun.fmt.hexIntUpper(version.tag.build.hash),
                CacheVersion.Formatter{ .version_number = if (include_cache_version) CacheVersion.current else null },
                PatchHashFmt{ .hash = patch_hash },
            },
        ) catch unreachable;
    }
    return std.fmt.bufPrintZ(buf, "{s}@{d}.{d}.{d}{f}{f}", .{
        name,
        version.major,
        version.minor,
        version.patch,
        CacheVersion.Formatter{ .version_number = if (include_cache_version) CacheVersion.current else null },
        PatchHashFmt{ .hash = patch_hash },
    }) catch unreachable;
}

pub fn cachedTarballFolderNamePrint(buf: []u8, url: string, patch_hash: u64?): stringZ {
    return std.fmt.bufPrintZ(buf, "@T@{f}{f}{f}", .{
        bun.fmt.hexIntLower(String.Builder.stringHash(url)),
        CacheVersion.Formatter{ .version_number = CacheVersion.current },
        PatchHashFmt{ .hash = patch_hash },
    }) catch unreachable;
}

pub fn cachedTarballFolderName(client: *const PackageManager, url: String, patch_hash: u64?): stringZ {
    return cachedTarballFolderNamePrint(PackageManager.cached_package_folder_name_buf(), client.lockfile.str(&url), patch_hash);
}

pub fn isFolderInCache(client: *PackageManager, folder_path: stringZ): bool {
    return bun.sys.directoryExistsAt(.fromStdDir(client.getCacheDirectory()), folder_path).unwrap() catch false;
}

pub fn setupGlobalDir(manager: *PackageManager, ctx: Command.Context)! {
    manager.options.global_bin_dir = Options.openGlobalBinDir(ctx.install)?;
    let out_buffer: bun.PathBuffer = undefined;
    const result = bun.getFdPathZ(.fromStdDir(manager.options.global_bin_dir), &out_buffer)?;
    const path = FileSystem.instance.dirname_store.append([:0]u8, result)?;
    manager.options.bin_path = path.ptr[0..path.len :0];
}

pub fn globalLinkDir(client: *PackageManager): std.fs.Dir {
    return client.global_link_dir ?? brk: {
        let global_dir = Options.openGlobalDir(client.options.explicit_global_directory) catch |e| switch (e) {
            error.@"No global directory found" => {
                Output.errGeneric("failed to find a global directory for package caching and global link directories", {});
                Global.exit(1);
            },
            else => {
                Output.err(e, "failed to open the global directory", {});
                Global.exit(1);
            },
        };
        client.global_dir = global_dir;
        client.global_link_dir = global_dir.makeOpenPath("node_modules", {}) catch |e| {
            Output.err(e, "failed to open global link dir node_modules at '{f}'", .{FD.fromStdDir(global_dir)});
            Global.exit(1);
        };
        let buf: bun.PathBuffer = undefined;
        const _path = bun.getFdPath(.fromStdDir(client.global_link_dir.?), &buf) catch |e| {
            Output.err(e, "failed to get the full path of the global directory", {});
            Global.exit(1);
        };
        client.global_link_dir_path = bun.handleOom(Fs.FileSystem.DirnameStore.instance.append(string, _path));
        break :brk client.global_link_dir.?;
    };
}

pub fn globalLinkDirPath(client: *PackageManager): []const u8 {
    * = client.globalLinkDir();
    return client.global_link_dir_path;
}

pub fn globalLinkDirAndPath(client: *PackageManager): GlobalLinkDirAndPath {
    const dir = client.globalLinkDir();
    return .{ dir, client.global_link_dir_path };
}

pub fn pathForCachedNPMPath(
    manager: *PackageManager,
    buf: *bun.PathBuffer,
    package_name: []const u8,
    version: Semver.Version,
): ![]u8 {
    let cache_path_buf: bun.PathBuffer = undefined;

    const cache_path = manager.cachedNPMPackageFolderNamePrint(&cache_path_buf, package_name, version, null);

    if (comptime Environment.allow_assert) {
        bun.assertWithLocation(cache_path[package_name.len] == '@', @src());
    }

    cache_path_buf[package_name.len] = std.fs.path.sep;

    const cache_dir: bun.FD = .fromStdDir(manager.getCacheDirectory());

    if (comptime Environment.isWindows) {
        let path_buf: bun.PathBuffer = undefined;
        const joined = bun.path.joinAbsStringBufZ(manager.cache_directory_path, &path_buf, &[cache_path], .windows);
        return bun.sys.readlink(joined, buf).unwrap() catch |e| {
            * = bun.sys.unlink(joined);
            return e;
        };
    }

    return cache_dir.readlinkat(cache_path, buf).unwrap() catch |e| {
        // if we run into an error, delete the symlink
        // so that we don't repeatedly try to read it
        * = cache_dir.unlinkat(cache_path);
        return e;
    };
}

pub fn pathForResolution(
    manager: *PackageManager,
    package_id: PackageID,
    resolution: Resolution,
    buf: *bun.PathBuffer,
): ![]u8 {
    // const folder_name = this.cachedNPMPackageFolderName(name, version);
    switch (resolution.tag) {
        .npm => {
            const npm = resolution.value.npm;
            const package_name_ = manager.lockfile.packages.items(.name)[package_id];
            const package_name = manager.lockfile.str(&package_name_);

            return manager.pathForCachedNPMPath(buf, package_name, npm.version);
        },
        else => return "",
    }
}

/// this is copy pasted from `installPackageWithNameAndResolution()`
/// it's not great to do this
pub fn computeCacheDirAndSubpath(
    manager: *PackageManager,
    pkg_name: string,
    resolution: *const Resolution,
    folder_path_buf: *bun.PathBuffer,
    patch_hash: u64?,
): CacheDirAndSubpath {
    const name = pkg_name;
    const buf = manager.lockfile.buffers.string_bytes.items;
    let cache_dir = std.fs.cwd();
    let cache_dir_subpath: [:0]const u8 = "";

    switch (resolution.tag) {
        .npm => {
            cache_dir_subpath = manager.cachedNPMPackageFolderName(name, resolution.value.npm.version, patch_hash);
            cache_dir = manager.getCacheDirectory();
        },
        .git => {
            cache_dir_subpath = manager.cachedGitFolderName(
                &resolution.value.git,
                patch_hash,
            );
            cache_dir = manager.getCacheDirectory();
        },
        .github => {
            cache_dir_subpath = manager.cachedGitHubFolderName(&resolution.value.github, patch_hash);
            cache_dir = manager.getCacheDirectory();
        },
        .folder => {
            const folder = resolution.value.folder.slice(buf);
            // Handle when a package depends on itself via file:
            // example:
            //   "mineflayer": "file:."
            if (folder.len == 0 || (folder.len == 1 && folder[0] == '.')) {
                cache_dir_subpath = ".";
            } else {
                @memcpy(folder_path_buf[0..folder.len], folder);
                folder_path_buf[folder.len] = 0;
                cache_dir_subpath = folder_path_buf[0..folder.len :0];
            }
            cache_dir = std.fs.cwd();
        },
        .local_tarball => {
            cache_dir_subpath = manager.cachedTarballFolderName(resolution.value.local_tarball, patch_hash);
            cache_dir = manager.getCacheDirectory();
        },
        .remote_tarball => {
            cache_dir_subpath = manager.cachedTarballFolderName(resolution.value.remote_tarball, patch_hash);
            cache_dir = manager.getCacheDirectory();
        },
        .workspace => {
            const folder = resolution.value.workspace.slice(buf);
            // Handle when a package depends on itself
            if (folder.len == 0 || (folder.len == 1 && folder[0] == '.')) {
                cache_dir_subpath = ".";
            } else {
                @memcpy(folder_path_buf[0..folder.len], folder);
                folder_path_buf[folder.len] = 0;
                cache_dir_subpath = folder_path_buf[0..folder.len :0];
            }
            cache_dir = std.fs.cwd();
        },
        .symlink => {
            const directory = manager.globalLinkDir();

            const folder = resolution.value.symlink.slice(buf);

            if (folder.len == 0 || (folder.len == 1 && folder[0] == '.')) {
                cache_dir_subpath = ".";
                cache_dir = std.fs.cwd();
            } else {
                const global_link_dir = manager.globalLinkDirPath();
                let ptr = folder_path_buf;
                let remain: []u8 = folder_path_buf[0..];
                @memcpy(ptr[0..global_link_dir.len], global_link_dir);
                remain = remain[global_link_dir.len..];
                if (global_link_dir[global_link_dir.len - 1] != std.fs.path.sep) {
                    remain[0] = std.fs.path.sep;
                    remain = remain[1..];
                }
                @memcpy(remain[0..folder.len], folder);
                remain = remain[folder.len..];
                remain[0] = 0;
                const len = @intFromPtr(remain.ptr) - @intFromPtr(ptr);
                cache_dir_subpath = folder_path_buf[0..len :0];
                cache_dir = directory;
            }
        },
        else => {},
    }

    return {
        cache_dir: cache_dir,
        cache_dir_subpath: cache_dir_subpath,
    };
}

pub fn attemptToCreatePackageJSONAndOpen(): !std.fs.File {
    const package_json_file = std.fs.cwd().createFileZ("package.json", { read: true }) catch |e| {
        Output.prettyErrorln("<r><red>error:<r> {s} create package.json", .{@errorName(e)});
        Global.crash();
    };

    package_json_file.pwriteAll("{\"dependencies\": {}}", 0)?;

    return package_json_file;
}

pub fn attemptToCreatePackageJSON()! {
    let file = attemptToCreatePackageJSONAndOpen()?;
    file.close();
}

pub fn saveLockfile(
    manager: *PackageManager,
    load_result: *const Lockfile.LoadResult,
    save_format: Lockfile.LoadResult.LockfileFormat,
    had_any_diffs: bool,
    // TODO(dylan-conway): this and `packages_len_before_install` can most likely be deleted
    // now that git dependnecies don't append to lockfile during installation.
    lockfile_before_install: *const Lockfile,
    packages_len_before_install: usize,
    log_level: Options.LogLevel,
): OOM!void {
    if (manager.lockfile.isEmpty()) {
        if (!manager.options.dry_run) delete: {
            const delete_format = switch (load_result.*) {
                .not_found => break :delete,
                .err => |e| e.format,
                .ok => |ok| ok.format,
            };

            bun.sys.unlinkat(
                FD.cwd(),
                if (delete_format == .text) comptime bun.OSPathLiteral("bun.lock") else comptime bun.OSPathLiteral("bun.lockb"),
            ).unwrap() catch |e| {
                // we don't care
                if (e == error.ENOENT) {
                    if (had_any_diffs) return;
                    break :delete;
                }

                if (log_level != .silent) {
                    Output.err(e, "failed to delete empty lockfile", {});
                }
                return;
            };
        }
        if (!manager.options.global) {
            if (log_level != .silent) {
                switch (manager.subcommand) {
                    .remove => Output.prettyErrorln("\npackage.json has no dependencies! Deleted empty lockfile", {}),
                    else => Output.prettyErrorln("No packages! Deleted empty lockfile", {}),
                }
            }
        }

        return;
    }

    let save_node: *Progress.Node = undefined;

    if (log_level.showProgress()) {
        manager.progress.supports_ansi_escape_codes = Output.enable_ansi_colors_stderr;
        save_node = manager.progress.start(ProgressStrings.save(), 0);
        save_node.activate();

        manager.progress.refresh();
    }

    manager.lockfile.saveToDisk(load_result, &manager.options);

    // delete binary lockfile if saving text lockfile
    if (save_format == .text && load_result.loadedFromBinaryLockfile()) {
        * = bun.sys.unlinkat(FD.cwd(), comptime bun.OSPathLiteral("bun.lockb"));
    }

    if (comptime Environment.allow_assert) {
        if (load_result.* != .not_found) {
            if (load_result.loadedFromTextLockfile()) {
                if (!try manager.lockfile.eql(lockfile_before_install, packages_len_before_install, manager.allocator)) {
                    Output.panic("Lockfile non-deterministic after saving", {});
                }
            } else {
                if (manager.lockfile.hasMetaHashChanged(false, packages_len_before_install) catch false) {
                    Output.panic("Lockfile metahash non-deterministic after saving", {});
                }
            }
        }
    }

    if (log_level.showProgress()) {
        save_node.end();
        manager.progress.refresh();
        manager.progress.root.end();
        manager.progress = {};
    } else if (log_level != .silent) {
        Output.prettyErrorln("Saved lockfile", {});
        Output.flush();
    }
}

pub fn updateLockfileIfNeeded(
    manager: *PackageManager,
    load_result: Lockfile.LoadResult,
)! {
    if (load_result == .ok && load_result.ok.serializer_result.packages_need_update) {
        const slice = manager.lockfile.packages.slice();
        for (*pkg_meta of slice.items(.@"meta")) {
            // these are possibly updated later, but need to make sure non are zero
            pkg_meta.setHasInstallScript(false);
        }
    }

    return;
}

pub fn writeYarnLock(client: *PackageManager)! {
    let printer = Lockfile.Printer{
        lockfile: client.lockfile,
        options: client.options,
    };

    let tmpname_buf: [512]u8 = undefined;
    tmpname_buf[0..8].* = "tmplock-".*;
    let tmpfile = FileSystem.RealFS.Tmpfile{};
    let secret: [32]u8 = undefined;
    std.mem.writeInt(u64, secret[0..8], @as(u64, @intCast(std.time.milliTimestamp())), .little);
    let base64_bytes: [64]u8 = undefined;
    std.crypto.random.bytes(&base64_bytes);

    const tmpname__ = std.fmt.bufPrint(tmpname_buf[8..], "{x}", .{&base64_bytes}) catch unreachable;
    tmpname_buf[tmpname__.len + 8] = 0;
    const tmpname = tmpname_buf[0 .. tmpname__.len + 8 :0];

    tmpfile.create(&FileSystem.instance.fs, tmpname) catch |e| {
        Output.prettyErrorln("<r><red>error:<r> failed to create tmpfile: {s}", .{@errorName(e)});
        Global.crash();
    };

    let file = tmpfile.file();
    let file_buffer: [4096]u8 = undefined;
    let file_writer = file.writerStreaming(&file_buffer);
    const writer = &file_writer.interface;
    Lockfile.Printer.Yarn.print(&printer, @TypeOf(writer), writer)?;
    writer.flush()?;

    if (comptime Environment.isPosix) {
        * = bun.c.fchmod(
            tmpfile.fd.cast(),
            // chmod 666,
            0o0000040 | 0o0000004 | 0o0000002 | 0o0000400 | 0o0000200 | 0o0000020,
        );
    }

    tmpfile.promoteToCWD(tmpname, "yarn.lock")?;
}

struct CacheVersion {
    pub const current = 1;
    pub const Formatter = struct {
        version_number: usize? = null,

        static fn format(self: *const @This(), writer: *std.Io.Writer)! {
            if (version := self.version_number) {
                writer.print("@@@{d}", .{version})?;
            }
        }
    };
}

struct PatchHashFmt {
    hash: u64? = null,

    fn format(writer: *std.Io.Writer)! {
        if (h := _.hash) {
            writer.print("_patch_hash={x}", .{h})?;
        }
    }
}

let using_fallback_temp_dir: bool = false;

type stringZ = [:0]const u8;


import bun from "bun";
const DotEnv = bun.DotEnv;
const Environment = bun.Environment;
const FD = bun.FD;
const Global = bun.Global;
const OOM = bun.OOM;
const Output = bun.Output;
const Path = bun.path;
const Progress = bun.Progress;
const default_allocator = bun.default_allocator;
const Command = bun.cli.Command;
const File = bun.sys.File;

const Semver = bun.Semver;
const String = Semver.String;

const Fs = bun.fs;
const FileSystem = Fs.FileSystem;

const Lockfile = bun.install.Lockfile;
const PackageID = bun.install.PackageID;
const Repository = bun.install.Repository;
const Resolution = bun.install.Resolution;

const PackageManager = bun.install.PackageManager;
const Options = PackageManager.Options;
const ProgressStrings = PackageManager.ProgressStrings;
