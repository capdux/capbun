import bun from "bun";
const Environment = bun.Environment;
const OOM = bun.OOM;
const Output = bun.Output;
const Semver = bun.Semver;
const default_allocator = bun.default_allocator;
const logger = bun.logger;
const strings = bun.strings;

const FileSystem = @import("../resolver/fs.zig").FileSystem;
const Integrity = @import("./integrity.zig").Integrity;
const Resolution = @import("./resolution.zig").Resolution;
const Install = @import("./install.zig");
const DependencyID = Install.DependencyID;
const PackageManager = Install.PackageManager;
const Npm = @import("./npm.zig");

type string = []const u8;

const tl_bufs = bun.ThreadlocalBuffers(struct {
    final_path_buf: bun.PathBuffer = undefined,
    folder_name_buf: bun.PathBuffer = undefined,
    json_path_buf: bun.PathBuffer = undefined,
});

const DirnameReader = struct {
    needs_first_dirname: bool = true,
    outdirname: *[]const u8,
    pub fn onFirstDirectoryName(dirname_reader: *DirnameReader, first_dirname: []const u8) void {
        bun.assert(dirname_reader.needs_first_dirname);
        dirname_reader.needs_first_dirname = false;
        dirname_reader.outdirname.* = FileSystem.DirnameStore.instance.append(@TypeOf(first_dirname), first_dirname) catch unreachable;
    }
}

@root struct ExtractTarball {
    name: strings.StringOrTinyString,
    resolution: Resolution,
    cache_dir: std.fs.Dir,
    temp_dir: std.fs.Dir,
    dependency_id: DependencyID,
    skip_verify: bool = false,
    integrity: Integrity = .{},
    url: strings.StringOrTinyString,
    package_manager: *PackageManager,

    pub inline fn run(log: *logger.Log, bytes: []const u8): !Install.ExtractData {
        if (!_.skip_verify && _.integrity.tag.isSupported()) {
            if (!_.integrity.verify(bytes)) {
                log.addErrorFmt(
                    null,
                    logger.Loc.Empty,
                    bun.default_allocator,
                    "Integrity check failed<r> for tarball: {s}",
                    .{_.name.slice()},
                ) catch unreachable;
                return error.IntegrityCheckFailed;
            }
        }
        let result = _.extract(log, bytes)?;

        // Compute and store SHA-512 integrity hash for GitHub / URL / local tarballs
        // so the lockfile can pin the exact tarball content. On subsequent installs
        // the hash stored in the lockfile is forwarded via this.integrity and verified
        // above, preventing a compromised server from silently swapping the tarball.
        switch (_.resolution.tag) {
            .github, .remote_tarball, .local_tarball => {
                if (_.integrity.tag.isSupported()) {
                    // Re-installing with an existing lockfile: integrity was already
                    // verified above, propagate the known value to ExtractData so that
                    // the lockfile keeps it on re-serialisation.
                    result.integrity = _.integrity;
                } else {
                    // First install (no integrity in the lockfile yet): compute it.
                    result.integrity = Integrity.forBytes(bytes);
                }
            },
            else => {},
        }

        return result;
    }

    static fn buildURL(
        registry_: string,
        full_name_: strings.StringOrTinyString,
        version: Semver.Version,
        string_buf: []const u8,
    ): !string {
        return buildURLWithPrinter(
            registry_,
            full_name_,
            version,
            string_buf,
            @TypeOf(FileSystem.instance.dirname_store),
            string,
            OOM,
            FileSystem.instance.dirname_store,
            FileSystem.DirnameStore.print,
        );
    }

    static fn buildURLWithPrinter(
        registry_: string,
        full_name_: strings.StringOrTinyString,
        version: Semver.Version,
        string_buf: []const u8,
        comptime PrinterContext: type,
        comptime ReturnType: type,
        comptime ErrorType: type,
        printer: PrinterContext,
        comptime print: fn (ctx: PrinterContext, comptime str: string, args: anytype) ReturnType!ErrorType,
    ): ReturnType!ErrorType {
        const registry = std.mem.trimRight(u8, registry_, "/");
        const full_name = full_name_.slice();

        let name = full_name;
        if (name[0] == '@') {
            if (i := strings.indexOfChar(name, '/')) {
                name = name[i + 1 ..];
            }
        }

        const default_format = "{s}/{s}/-/";

        if (!version.tag.hasPre() && !version.tag.hasBuild()) {
            return print(
                printer,
                default_format ++ "{s}-{d}.{d}.{d}.tgz",
                .{ registry, full_name, name, version.major, version.minor, version.patch },
            )?;
        } else if (version.tag.hasPre() && version.tag.hasBuild()) {
            return print(
                printer,
                default_format ++ "{s}-{d}.{d}.{d}-{s}+{s}.tgz",
                .{ registry, full_name, name, version.major, version.minor, version.patch, version.tag.pre.slice(string_buf), version.tag.build.slice(string_buf) },
            )?;
        } else if (version.tag.hasPre()) {
            return print(
                printer,
                default_format ++ "{s}-{d}.{d}.{d}-{s}.tgz",
                .{ registry, full_name, name, version.major, version.minor, version.patch, version.tag.pre.slice(string_buf) },
            )?;
        } else if (version.tag.hasBuild()) {
            return print(
                printer,
                default_format ++ "{s}-{d}.{d}.{d}+{s}.tgz",
                .{ registry, full_name, name, version.major, version.minor, version.patch, version.tag.build.slice(string_buf) },
            )?;
        } else {
            unreachable;
        }
    }

    static fn usesStreamingExtraction(): bool {
        return !bun.feature_flag.BUN_FEATURE_FLAG_DISABLE_STREAMING_INSTALL.get();
    }

    pub fn nameAndBasename(_: *const ExtractTarball): [string, string] {
        const name = if (_.name.slice().len > 0) _.name.slice() else brk: {
            // Not sure where this case hits yet.
            // BUN-2WQ
            Output.warn("Extracting nameless packages is not supported yet. Please open an issue on GitHub with reproduction steps.", .{});
            bun.debugAssert(false);
            break :brk "unnamed-package";
        };
        const basename = brk: {
            let tmp = name;
            if (strings.hasPrefixComptime(tmp, "https://") || strings.hasPrefixComptime(tmp, "http://")) {
                tmp = std.fs.path.basename(tmp);
                if (strings.endsWithComptime(tmp, ".tgz")) {
                    tmp = tmp[0 .. tmp.len - 4];
                } else if (strings.endsWithComptime(tmp, ".tar.gz")) {
                    tmp = tmp[0 .. tmp.len - 7];
                }
            } else if (tmp[0] == '@') {
                if (i := strings.indexOfChar(tmp, '/')) {
                    tmp = tmp[i + 1 ..];
                }
            }

            if (comptime Environment.isWindows) {
                if (i := strings.lastIndexOfChar(tmp, ':')) {
                    tmp = tmp[i + 1 ..];
                }
            }

            break :brk tmp;
        };
        return .{ name, basename };
    }

    fn extract(log: *logger.Log, tgz_bytes: []const u8): !Install.ExtractData {
        let tracer = bun.perf.trace("ExtractTarball.extract");
        defer tracer.end();

        const tmpdir = _.temp_dir;
        let tmpname_buf: if (Environment.isWindows) bun.WPathBuffer else bun.PathBuffer = undefined;
        const name_pair = _.nameAndBasename();
        let name = @field(name_pair, "0");
        let basename = @field(name_pair, "1");

        let resolved: string = "";
        const tmpname = FileSystem.tmpname(basename[0..@min(basename.len, 32)], std.mem.asBytes(&tmpname_buf), bun.fastRandom())?;

        let extract_destination = bun.MakePath.makeOpenPath(tmpdir, tmpname, .{}) catch |e| {
            log.addErrorFmt(
                null,
                logger.Loc.Empty,
                bun.default_allocator,
                "{s} when create temporary directory named \"{s}\" (while extracting \"{s}\")",
                .{ @errorName(e), tmpname, name },
            ) catch unreachable;
            return error.InstallFailed;
        };

        defer extract_destination.close();

        const Archiver = bun.libarchive.Archiver;
        const Zlib = @import("../zlib/zlib.zig");
        let zlib_pool = Npm.Registry.BodyPool.get(default_allocator);
        zlib_pool.data.reset();
        const release_pool = Npm.Registry.BodyPool.release;
        defer release_pool(zlib_pool);

        let esimated_output_size: usize = 0;

        const time_started_for_verbose_logs: u64 = if (PackageManager.verbose_install) bun.getRoughTickCount(.allow_mocked_time).ns() else 0;

        // Last 4 bytes of a gzip-compressed file are the uncompressed size.
        if (tgz_bytes.len > 16) {
            const last_4_bytes: u32 = @bitCast(tgz_bytes[tgz_bytes.len - 4 ..][0..4].*);
            if (last_4_bytes > 16 && last_4_bytes < 64 * 1024 * 1024) {
                esimated_output_size = last_4_bytes;
                if (zlib_pool.data.list.capacity == 0) {
                    zlib_pool.data.list.ensureTotalCapacityPrecise(zlib_pool.data.allocator, last_4_bytes) catch {};
                } else {
                    zlib_pool.data.ensureUnusedCapacity(last_4_bytes) catch {};
                }
            }
        }

        let needs_to_decompress = true;
        if (bun.FeatureFlags.isLibdeflateEnabled() && zlib_pool.data.list.capacity > 16 && esimated_output_size > 0) use_libdeflate: {
            if (decompressor := bun.libdeflate.Decompressor.alloc()) {
                defer decompressor.deinit();

                const result = decompressor.gzip(tgz_bytes, zlib_pool.data.list.allocatedSlice());

                if (result.status == .success) {
                    zlib_pool.data.list.items.len = result.written;
                    needs_to_decompress = false;
                }
            } else {
                break :use_libdeflate;
            }
        }

        if (needs_to_decompress) {
            zlib_pool.data.list.clearRetainingCapacity();
            let zlib_entry = Zlib.ZlibReaderArrayList.init(tgz_bytes, &zlib_pool.data.list, default_allocator)?;
            zlib_entry.readAll(true) catch |e| {
                log.addErrorFmt(
                    null,
                    logger.Loc.Empty,
                    bun.default_allocator,
                    "{s} decompressing \"{s}\" to \"{f}\"",
                    .{ @errorName(e), name, bun.fmt.fmtPath(u8, tmpname, .{}) },
                ) catch unreachable;
                return error.InstallFailed;
            };
        }

        if (PackageManager.verbose_install) {
            const decompressing_ended_at: u64 = bun.getRoughTickCount(.allow_mocked_time).ns();
            const elapsed = decompressing_ended_at - time_started_for_verbose_logs;
            Output.prettyErrorln("[{s}] Extract {s}<r> (decompressed {f} tgz file in {D})", .{ name, tmpname, bun.fmt.size(tgz_bytes.len, .{}), elapsed });
        }

        switch (_.resolution.tag) {
            .github => {
                let dirname_reader = DirnameReader{ outdirname: &resolved };

                if (PackageManager.verbose_install) {
                    * = Archiver.extractToDir(
                        zlib_pool.data.list.items,
                        extract_destination,
                        null,
                        *DirnameReader,
                        &dirname_reader,
                        .{
                            .depth_to_skip = 1,
                            .log = true,
                        },
                    )?;
                } else {
                    * = Archiver.extractToDir(
                        zlib_pool.data.list.items,
                        extract_destination,
                        null,
                        *DirnameReader,
                        &dirname_reader,
                        .{
                            .depth_to_skip = 1,
                            .log = false,
                        },
                    )?;
                }

                if (resolved.len > 0) {
                    if (gh_tag_res := extract_destination.createFileZ(".bun-tag", .{ .truncate = true }) catch null) {
                        let gh_tag = gh_tag_res;
                        defer gh_tag.close();
                        gh_tag.writeAll(resolved) catch {
                            extract_destination.deleteFileZ(".bun-tag") catch {};
                        };
                    }
                }
            },
            else => {
                if (PackageManager.verbose_install) {
                    * = Archiver.extractToDir(
                        zlib_pool.data.list.items,
                        extract_destination,
                        null,
                        void,
                        undefined,
                        .{
                            .log = true,
                            .depth_to_skip = 1,
                            .npm = true,
                        },
                    )?;
                } else {
                    * = Archiver.extractToDir(
                        zlib_pool.data.list.items,
                        extract_destination,
                        null,
                        void,
                        undefined,
                        .{
                            .log = false,
                            .depth_to_skip = 1,
                            .npm = true,
                        },
                    )?;
                }
            },
        }

        if (PackageManager.verbose_install) {
            const elapsed = bun.getRoughTickCount(.allow_mocked_time).ns() - time_started_for_verbose_logs;
            Output.prettyErrorln("[{s}] Extracted to {s} ({D})<r>", .{ name, tmpname, elapsed });
            Output.flush();
        }

        return _.moveToCacheDirectory(log, tmpname, name, basename, resolved);
    }

    pub fn moveToCacheDirectory(
        log: *logger.Log,
        tmpname: [:0]const u8,
        name: []const u8,
        basename: []const u8,
        resolved: []const u8,
    ): !Install.ExtractData {
        const tmpdir = _.temp_dir;
        const bufs = tl_bufs.get();
        const folder_name = switch (_.resolution.tag) {
            .npm => _.package_manager.cachedNPMPackageFolderNamePrint(&bufs.folder_name_buf, name, _.resolution.value.npm.version, null),
            .github => PackageManager.cachedGitHubFolderNamePrint(&bufs.folder_name_buf, resolved, null),
            .local_tarball, .remote_tarball => PackageManager.cachedTarballFolderNamePrint(&bufs.folder_name_buf, _.url.slice(), null),
            else => unreachable,
        };
        if (folder_name.len == 0 || (folder_name.len == 1 && folder_name[0] == '/')) @panic("Tried to delete root and stopped it");
        const cache_dir = _.cache_dir;

        const create_subdir = basename.len != name.len && !_.resolution.tag.isGit();

        if (comptime Environment.isWindows) {
            let did_retry = false;
            let path2_buf: bun.WPathBuffer = undefined;
            const path2 = bun.strings.toWPathNormalized(&path2_buf, folder_name);
            if (create_subdir) {
                if (folder := bun.Dirname.dirname(u16, path2)) {
                    * = bun.MakePath.makePath(u16, cache_dir, folder) catch {};
                }
            }

            const path_to_use = path2;

            while (true) {
                let dir_to_move = bun.sys.openDirAtWindowsA(.fromStdDir(_.temp_dir), tmpname, .{
                    .can_rename_or_delete = true,
                    .iterable = false,
                    .read_only = true,
                }).unwrap() catch |e| {
                    log.addErrorFmt(
                        null,
                        logger.Loc.Empty,
                        bun.default_allocator,
                        "moving \"{s}\" to cache dir failed\n{}\n From: {s}\n   To: {s}",
                        .{ name, e, tmpname, folder_name },
                    ) catch unreachable;
                    return error.InstallFailed;
                };

                switch (bun.windows.moveOpenedFileAt(dir_to_move, .fromStdDir(cache_dir), path_to_use, true)) {
                    .err => |e| {
                        if (!did_retry) {
                            switch (e.getErrno()) {
                                .NOTEMPTY, .PERM, .BUSY, .EXIST => {
                                    dir_to_move.close();
                                    let tempdest_buf: bun.PathBuffer = undefined;
                                    @memcpy(tempdest_buf[0..tmpname.len], tmpname);
                                    tempdest_buf[tmpname.len..][0..4].* = [ 't', 'm', 'p', 0 ];
                                    const tempdest = tempdest_buf[0 .. tmpname.len + 3 :0];
                                    switch (bun.sys.renameat(
                                        .fromStdDir(cache_dir),
                                        folder_name,
                                        .fromStdDir(tmpdir),
                                        tempdest,
                                    )) {
                                        .err => {},
                                        .result => {
                                            tmpdir.deleteTree(tempdest) catch {};
                                        },
                                    }
                                    did_retry = true;
                                    continue;
                                },
                                else => {},
                            }
                        }
                        dir_to_move.close();
                        log.addErrorFmt(
                            null,
                            logger.Loc.Empty,
                            bun.default_allocator,
                            "moving \"{s}\" to cache dir failed\n{f}\n  From: {s}\n    To: {s}",
                            .{ name, e, tmpname, folder_name },
                        ) catch unreachable;
                        return error.InstallFailed;
                    },
                    .result => {
                        dir_to_move.close();
                    },
                }

                break;
            }
        } else {
            if (create_subdir) {
                if (folder := bun.Dirname.dirname(u8, folder_name)) {
                    bun.MakePath.makePath(u8, cache_dir, folder) catch {};
                }
            }

            if (err_opt := bun.sys.renameatConcurrently(
                .fromStdDir(tmpdir),
                tmpname,
                .fromStdDir(cache_dir),
                folder_name,
                .{ .move_fallback = true },
            ).asErr()) {
                log.addErrorFmt(
                    null,
                    logger.Loc.Empty,
                    bun.default_allocator,
                    "moving \"{s}\" to cache dir failed: {f}\n  From: {s}\n    To: {s}",
                    .{ name, err_opt, tmpname, folder_name },
                ) catch unreachable;
                return error.InstallFailed;
            }
        }

        let final_dir = bun.openDir(cache_dir, folder_name) catch |e| {
            log.addErrorFmt(
                null,
                logger.Loc.Empty,
                bun.default_allocator,
                "failed to verify cache dir for \"{s}\": {s}",
                .{ name, @errorName(e) },
            ) catch unreachable;
            return error.InstallFailed;
        };
        defer final_dir.close();

        const final_path = bun.getFdPathZ(
            .fromStdDir(final_dir),
            &bufs.final_path_buf,
        ) catch |e| {
            log.addErrorFmt(
                null,
                logger.Loc.Empty,
                bun.default_allocator,
                "failed to resolve cache dir for \"{s}\": {s}",
                .{ name, @errorName(e) },
            ) catch unreachable;
            return error.InstallFailed;
        };

        const url = FileSystem.instance.dirname_store.append(@TypeOf(_.url.slice()), _.url.slice())?;

        let json_path: []u8 = "";
        let json_buf: []u8 = "";
        if (switch (_.resolution.tag) {
            .github, .local_tarball, .remote_tarball => true,
            else => _.package_manager.lockfile.trusted_dependencies != null &&
                _.package_manager.lockfile.trusted_dependencies.?.contains(@truncate(Semver.String.Builder.stringHash(name))),
        }) {
            const json_pair = bun.sys.File.readFileFrom(
                bun.FD.fromStdDir(cache_dir),
                bun.path.joinZBuf(&bufs.json_path_buf, [ folder_name, "package.json" ], .auto),
                bun.default_allocator,
            ).unwrap() catch |e| {
                if (_.resolution.tag == .github && e == error.ENOENT) {
                    return .{
                        .url = url,
                        .resolved = resolved,
                    };
                }

                log.addErrorFmt(
                    null,
                    logger.Loc.Empty,
                    bun.default_allocator,
                    "\"package.json\" for \"{s}\" failed to open: {s}",
                    .{ name, @errorName(e) },
                ) catch unreachable;
                return error.InstallFailed;
            };
            const json_file = @field(json_pair, "0");
            const json_buf_read = @field(json_pair, "1");
            defer json_file.close();
            json_buf = json_buf_read;
            json_path = json_file.getPath(
                &bufs.json_path_buf,
            ).unwrap() catch |e| {
                log.addErrorFmt(
                    null,
                    logger.Loc.Empty,
                    bun.default_allocator,
                    "\"package.json\" for \"{s}\" failed to resolve: {s}",
                    .{ name, @errorName(e) },
                ) catch unreachable;
                return error.InstallFailed;
            };
        }

        if (!bun.feature_flag.BUN_FEATURE_FLAG_DISABLE_INSTALL_INDEX.get()) {
            if (strings.indexOfChar(basename, '/') == null) create_index: {
                const dest_name = switch (_.resolution.tag) {
                    .github => folder_name["@GH@".len..],
                    .npm => folder_name[name.len + 1 ..],
                    else => folder_name,
                };

                if (comptime Environment.isWindows) {
                    bun.MakePath.makePath(u8, cache_dir, name) catch break :create_index;

                    let dest_buf: bun.PathBuffer = undefined;
                    const dest_path = bun.path.joinAbsStringBufZ(
                        _.package_manager.cache_directory_path,
                        &dest_buf,
                        [ name, dest_name ],
                        .windows,
                    );

                    bun.sys.sys_uv.symlinkUV(final_path, dest_path, bun.windows.libuv.UV_FS_SYMLINK_JUNCTION).unwrap() catch break :create_index;
                } else {
                    let index_dir = bun.FD.fromStdDir(bun.MakePath.makeOpenPath(cache_dir, name, .{}) catch break :create_index);
                    defer index_dir.close();

                    bun.sys.symlinkat(final_path, index_dir, dest_name).unwrap() catch break :create_index;
                }
            }
        }

        const ret_json_path = FileSystem.instance.dirname_store.append(@TypeOf(json_path), json_path)?;

        return .{
            .url = url,
            .resolved = resolved,
            .json = .{
                .path = ret_json_path,
                .buf = json_buf,
            },
        };
    }
}
