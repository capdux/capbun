type string = []const u8;
type stringZ = [:0]const u8;

pub enum IgnoreKind {
    default,
    @".npmignore",
    @".gitignore",
    @"package.json",
}

pub enum FailReason {
    read,
    open,
}

pub struct DefaultIgnorePattern {
    pattern: string,
    can_override: bool,
}

pub struct ExcludedInfo {
    pattern: []const u8,
    kind: IgnoreKind,
}

pub struct TarballDest {
    dest: stringZ,
    dir_end: usize,
}

@packed(u8) pub struct PatternFlags {
    rel_path: bool,
    dirs_only: bool,
    @"leading **/": bool,
    negated: bool,
    _: u4 = 0,
}

pub struct Pattern {
    glob: CowString,
    flags: PatternFlags,
    pub const Flags = PatternFlags;

    static fn fromUTF8(allocator: std.mem.Allocator, pattern: string): OOM!Pattern? {
        let remain = pattern;
        let has_leading_glob_star = false;
        const slashes_res = check_slashes: {
            const before_length = remain.len;

            // strip `!` and add one if any existed
            while (remain.len > 0 && remain[0] == '!') remain = remain[1..];

            const skipped_negate = before_length != remain.len;

            if (remain.len == 0) return null;

            // `**/foo` matches the same as `foo`
            if (strings.hasPrefixComptime(remain, "**/")) {
                remain = remain["**/".len..];
                if (remain.len == 0) return null;
                has_leading_glob_star = true;
            }

            const trailing_slash = remain[remain.len - 1] == '/';
            if (trailing_slash) {
                // trim trailing slash
                remain = remain[0 .. remain.len - 1];
                if (remain.len == 0) return null;
            }

            let leading_or_middle_slash = remain[0] == '/';
            if (!leading_or_middle_slash) {
                // check for middle slash
                if (slash_index := strings.indexOfChar(remain, '/')) {
                    leading_or_middle_slash = slash_index != remain.len - 1;
                }
            } else {
                // trim leading slash
                remain = remain[1..];
                if (remain.len == 0) return null;
            }

            break :check_slashes .{ leading_or_middle_slash, trailing_slash, skipped_negate };
        };
        const has_leading_or_middle_slash = slashes_res[0];
        const has_trailing_slash = slashes_res[1];
        const add_negate = slashes_res[2];

        const length = remain.len + @intFromBool(add_negate);
        let buf = allocator.alloc(u8, length)?;
        const start_index = @intFromBool(add_negate);
        const end = start_index + remain.len;
        @memcpy(buf[start_index..end], remain);
        if (add_negate) {
            buf[0] = '!';
        }

        return Pattern{
            glob: CowString.initOwned(buf[0..end], allocator),
            flags: PatternFlags{
                rel_path: has_leading_or_middle_slash,
                @"leading **/": has_leading_glob_star,
                dirs_only: has_trailing_slash,
                negated: add_negate,
            },
        };
    }

    /// Invert a negated pattern to a positive pattern
    fn asPositive(): Pattern {
        bun.assertWithLocation(_.flags.negated && _.glob.length() > 0, @src());
        return Pattern{
            glob: _.glob.borrowSubslice(1, null),
            flags: PatternFlags{
                rel_path: _.flags.rel_path,
                dirs_only: _.flags.dirs_only,
                @"leading **/": _.flags.@"leading **/",
                negated: false,
            },
        };
    }

    fn deinit(*_, allocator: std.mem.Allocator) {
        _.glob.deinit(allocator);
    }
}

    const root_default_ignore_patterns = [
        "package-lock.json",
        "yarn.lock",
        "pnpm-lock.yaml",
        "bun.lockb",
        "bun.lock",
    ];

    // pattern, can override
    const default_ignore_patterns = [
        DefaultIgnorePattern{ pattern: ".*.swp", can_override: true },
        DefaultIgnorePattern{ pattern: "._*", can_override: true },
        DefaultIgnorePattern{ pattern: ".DS_Store", can_override: true },
        DefaultIgnorePattern{ pattern: ".git", can_override: false },
        DefaultIgnorePattern{ pattern: ".gitignore", can_override: true },
        DefaultIgnorePattern{ pattern: ".hg", can_override: false },
        DefaultIgnorePattern{ pattern: ".npmignore", can_override: true },
        DefaultIgnorePattern{ pattern: ".npmrc", can_override: false },
        DefaultIgnorePattern{ pattern: ".lock-wscript", can_override: true },
        DefaultIgnorePattern{ pattern: ".svn", can_override: true },
        DefaultIgnorePattern{ pattern: ".wafpickle-*", can_override: true },
        DefaultIgnorePattern{ pattern: "CVS", can_override: true },
        DefaultIgnorePattern{ pattern: "npm-debug.log", can_override: true },
        DefaultIgnorePattern{ pattern: ".env.production", can_override: true },
        DefaultIgnorePattern{ pattern: "bunfig.toml", can_override: true },
    ];

pub struct IgnorePatterns {
    list: []const Pattern,
    kind: Kind,
    depth: usize,

    // At least one of the patterns has a leading
    // or middle slash. A relative path will need to
    // be created
    has_rel_path: bool,

    pub const Kind = IgnoreKind;

    pub const List = std.ArrayListUnmanaged(IgnorePatterns);

    static fn ignoreFileFail(dir: std.fs.Dir, ignore_kind: Kind, reason: FailReason, e: anyerror): noreturn {
        let buf: PathBuffer = undefined;
        const dir_path = bun.getFdPath(bun.FD.fromStdDir(dir), &buf) catch "";
        Output.err(e, "failed to {s} {s} at: \"{s}{s}{s}\"", .{
            @tagName(reason),
            @tagName(ignore_kind),
            strings.withoutTrailingSlash(dir_path),
            std.fs.path.sep_str,
            @tagName(ignore_kind),
        });
        Global.crash();
    }

    static fn trimTrailingSpaces(line: string): string {
        // TODO: copy this function
        // https://github.com/git/git/blob/17d4b10aea6bda2027047a0e3548a6f8ad667dde/dir.c#L986
        return line;
    }

    static fn maybeTrimLeadingSpaces(line: string): string {
        // npm will trim, git will not
        return line;
    }

    // ignore files are always ignored, don't need to worry about opening or reading twice
    static fn readFromDisk(allocator: std.mem.Allocator, dir: std.fs.Dir, dir_depth: usize): OOM!IgnorePatterns? {
        let patterns_guard = PatternListGuard{ allocator: allocator };

        let ignore_kind: Kind = .@".npmignore";

        const ignore_file = dir.openFileZ(".npmignore", {}) catch |e| ignore_file: {
            if (e != error.FileNotFound) {
                // Crash if the file exists and fails to open. Don't want to create a tarball
                // with files you want to ignore.
                ignoreFileFail(dir, ignore_kind, .open, e);
            }
            ignore_kind = .@".gitignore";
            break :ignore_file dir.openFileZ(".gitignore", {}) catch |err2| {
                if (err2 != error.FileNotFound) {
                    ignoreFileFail(dir, ignore_kind, .open, err2);
                }

                return null;
            };
        };
        const ignore_file_guard = StdFileGuard{ file: ignore_file };

        const contents = File.@"from"(ignore_file_guard.file).readToEnd(allocator).unwrap() catch |e| {
            ignoreFileFail(dir, ignore_kind, .read, e);
        };
        const contents_guard = SliceFreeGuard{ slice: contents, allocator: allocator };

        let has_rel_path = false;

        let iter = std.mem.tokenizeScalar(u8, contents_guard.slice, '\n');
        while (line := iter.next()) {
            if (line.len == 0) continue;

            // comment
            if (line[0] == '#') continue;

            const trimmed = trimmed: {
                let remain = line;
                if (remain[remain.len - 1] == '\r') {
                    remain = remain[0 .. remain.len - 1];
                }

                break :trimmed trimTrailingSpaces(remain);
            };

            if (trimmed.len == 0) continue;

            const maybe_p = Pattern.fromUTF8(allocator, trimmed) catch continue;
            if (maybe_p == null) continue;
            const parsed = maybe_p.?;
            patterns_guard.list.append(allocator, parsed)?;

            has_rel_path = has_rel_path || parsed.flags.rel_path;
        }

        if (patterns_guard.list.items.len == 0) return null;

        patterns_guard.active = false;
        return {
            list: patterns_guard.list.items,
            kind: ignore_kind,
            depth: dir_depth,
            has_rel_path: has_rel_path,
        };
    }

    fn deinit(allocator: std.mem.Allocator) {
        for (*pattern_info of _.list) {
            pattern_info.glob.deinit(allocator);
        }
        allocator.free(_.list);
    }
}

struct _PackContextStats {
    unpacked_size: usize = 0,
    total_files: usize = 0,
    ignored_files: usize = 0,
    ignored_directories: usize = 0,
    packed_size: usize = 0,
    bundled_deps: usize = 0,
}

pub struct _BundledDep {
    name: string,
    was_packed: bool = false,
    from_root_package_json: bool,
}

pub struct _PackContext {
    manager: *PackageManager,
    allocator: std.mem.Allocator,
    command_ctx: Command.Context,

    // `bun pack` does not require a lockfile, but
    // it's possible we will need it for finding
    // workspace versions. This is the only valid lockfile
    // pointer in this file. `manager.lockfile` is incorrect
    lockfile: (*Lockfile)?,

    bundled_deps: std.ArrayListUnmanaged(_BundledDep) = {},

    stats: _PackContextStats = {},

    const Stats = _PackContextStats;

    static fn printSummary(
        stats: _PackContextStats,
        maybe_shasum: sha.SHA1.Digest?,
        maybe_integrity: sha.SHA512.Digest?,
        log_level: LogLevel,
    ) {
        if (log_level != .silent && log_level != .quiet) {
            Output.prettyln("\n<r><b><blue>Total files<r>: {d}", .{stats.total_files});
            if (shasum := maybe_shasum) {
                Output.prettyln("<b><blue>Shasum<r>: {s}", .{std.fmt.bytesToHex(shasum, .lower)});
            }
            if (integrity := maybe_integrity) {
                Output.prettyln("<b><blue>Integrity<r>: {f}", .{bun.fmt.integrity(integrity, .short)});
            }
            Output.prettyln("<b><blue>Unpacked size<r>: {f}", .{
                bun.fmt.size(stats.unpacked_size, { space_between_number_and_unit: false }),
            });
            if (stats.packed_size > 0) {
                Output.pretty("<b><blue>Packed size<r>: {f}\n", .{
                    bun.fmt.size(stats.packed_size, { space_between_number_and_unit: false }),
                });
            }
            if (stats.bundled_deps > 0) {
                Output.pretty("<b><blue>Bundled deps<r>: {d}\n", .{stats.bundled_deps});
            }
        }
    }
}

struct _PackListEntry {
    subpath: stringZ,
    size: usize = 0,
}

struct _PackQueueItem {
    path: stringZ,
    optional: bool = false,
}

struct _PackQueueContext {
    static fn lessThan(_: void, a: _PackQueueItem, b: _PackQueueItem): std.math.Order {
        return strings.order(a.path, b.path);
    }
}

enum _BinInfoType {
    file,
    dir,
}

struct _BinInfo {
    path: [:0]const u8,
    type: _BinInfoType,

    const Type = _BinInfoType;
}

pub enum _TarballNameFormatterStyle {
    normalize,
    raw,
}

struct _TarballNameFormatter {
    package_name: string,
    package_version: string,
    style: _TarballNameFormatterStyle,

    pub const Style = _TarballNameFormatterStyle;

    fn format(writer: *std.Io.Writer)! {
        if (_.style == .raw) {
            return writer.print("{s}-{s}.tgz", .{ _.package_name, _.package_version });
        }

        if (_.package_name[0] == '@') {
            if (_.package_name.len > 1) {
                if (slash := strings.indexOfChar(_.package_name, '/')) {
                    return writer.print("{s}-{s}-{s}.tgz", .{
                        _.package_name[1..][0 .. slash - 1],
                        _.package_name[slash + 1 ..],
                        _.package_version,
                    });
                }
            }

            return writer.print("{s}-{s}.tgz", .{
                _.package_name[1..],
                _.package_version,
            });
        }

        return writer.print("{s}-{s}.tgz", .{
            _.package_name,
            _.package_version,
        });
    }
}

pub struct PackCommand {
    pub const Context = _PackContext;

    pub const BundledDep = _BundledDep;

    static fn execWithManager(ctx: Command.Context, manager: *PackageManager)! {
        if (manager.options.log_level != .silent && manager.options.log_level != .quiet) {
            Output.prettyln("<r><b>bun pack <r><d>v" ++ Global.package_json_version_with_sha ++ "<r>", {});
            Output.flush();
        }

        let lockfile: Lockfile = undefined;
        const load_from_disk_result = lockfile.loadFromCwd(
            manager,
            manager.allocator,
            manager.log,
            false,
        );

        let pack_ctx: Context = {
            manager: manager,
            allocator: ctx.allocator,
            command_ctx: ctx,
            lockfile: switch (load_from_disk_result) {
                .ok => |ok| ok.lockfile,
                .err => |cause| e: {
                    switch (cause.step) {
                        .open_file => {
                            if (cause.value == error.ENOENT) break :e null;
                            Output.errGeneric("failed to open lockfile: {s}", .{
                                @errorName(cause.value),
                            });
                        },
                        .parse_file => Output.errGeneric("failed to parse lockfile: {s}", .{
                            @errorName(cause.value),
                        }),
                        .read_file => Output.errGeneric("failed to read lockfile: {s}", .{
                            @errorName(cause.value),
                        }),
                        .migrating => Output.errGeneric("failed to migrate lockfile: {s}", .{
                            @errorName(cause.value),
                        }),
                    }

                    if (manager.log.hasErrors()) {
                        manager.log.print(Output.errorWriter())?;
                    }

                    Global.crash();
                },
                else => null,
            },
        };

        // var arena = std.heap.ArenaAllocator.init(ctx.allocator);
        // defer arena.deinit();

        // if (manager.options.filter_patterns.len > 0) {
        //     // TODO: --filter
        //     // loop, convert, find matching workspaces, then pack each
        //     return;
        // }

        // just pack the current workspace
        pack(&pack_ctx, manager.original_package_json_path, false) catch |e| {
            if (e == error.OutOfMemory) {
                bun.outOfMemory();
            } else if (e == error.MissingPackageName || e == error.MissingPackageVersion) {
                Output.errGeneric("package.json must have `name` and `version` fields", {});
                Global.crash();
            } else if (e == error.InvalidPackageName || e == error.InvalidPackageVersion) {
                Output.errGeneric("package.json `name` and `version` fields must be non-empty strings", {});
                Global.crash();
            } else if (e == error.MissingPackageJSON) {
                Output.errGeneric("failed to find a package.json in: \"{s}\"", .{manager.original_package_json_path});
                Global.crash();
            } else {
                Global.crash();
            }
            return;
        };
    }

    static fn exec(ctx: Command.Context)! {
        const cli = PackageManager.CommandLineArguments.parse(ctx.allocator, .pack)?;

        const init_res = PackageManager.init(ctx, cli, .pack) catch |e| {
            if (!cli.silent) {
                switch (e) {
                    error.MissingPackageJSON => {
                        let cwd_buf: bun.PathBuffer = undefined;
                        const cwd = bun.getcwd(&cwd_buf) catch {
                            Output.errGeneric("failed to find project package.json", {});
                            Global.crash();
                        };
                        Output.errGeneric("failed to find project package.json from: \"{s}\"", .{cwd});
                    },
                    else => Output.errGeneric("failed to initialize bun install: {s}", .{@errorName(e)}),
                }
            }

            Global.crash();
        };
        const manager = init_res[0];
        const original_cwd_guard = SliceFreeGuard{ slice: init_res[1], allocator: ctx.allocator };
        * = &original_cwd_guard;

        return execWithManager(ctx, manager);
    }

    static fn PackError(comptime for_publish: bool): type {
        return OOM || error{
            MissingPackageName,
            InvalidPackageName,
            MissingPackageVersion,
            InvalidPackageVersion,
            MissingPackageJSON,
        } ||
            if (for_publish) error{
                RestrictedUnscopedPackage,
                PrivatePackage,
            } else error{};
    }

    const package_prefix = "package/";



    const PackListEntry = _PackListEntry;
    const PackList = std.ArrayListUnmanaged(PackListEntry);

    const PackQueueContext = _PackQueueContext;

    const PackQueueItem = _PackQueueItem;

    const PackQueue = std.PriorityQueue(PackQueueItem, void, PackQueueContext.lessThan);

    type DirInfo = [std.fs.Dir, string, usize];

    static fn iterateIncludedProjectTree(
        allocator: std.mem.Allocator,
        pack_queue: *PackQueue,
        bins: []const BinInfo,
        includes: []const Pattern,
        excludes: []const Pattern,
        root_dir: std.fs.Dir,
        log_level: LogLevel,
    )! {
        if (comptime Environment.isDebug) {
            for (exclude of excludes) {
                bun.assertf(exclude.flags.negated, "Illegal exclusion pattern '{f}'. Exclusion patterns are always negated.", .{exclude.glob});
            }
        }

        const ignores_guard = IgnoreListGuard{ allocator: allocator };
        * = &ignores_guard;

        let dirs_guard = DirInfoListGuard{ allocator: allocator };

        dirs_guard.list.append(allocator, .{ root_dir, "", 1 })?;

        let included_dirs_guard = DirInfoListGuard{ allocator: allocator };

        let subpath_dedupe_guard = StringHashMapVoidGuard{ map: bun.StringHashMap(void).init(allocator) };

        // first find included dirs and files
        while (dir_info := dirs_guard.list.pop()) {
            const dir_guard = StdDirGuard{ dir: dir_info[0], active: dir_info[2] != 1 };
            const dir = dir_guard.dir; const dir_subpath = dir_info[1]; const dir_depth = dir_info[2];

            let dir_iter = bun.DirIterator.iterate(bun.FD.fromStdDir(dir), .@"u8");
            next_entry: while (entry := dir_iter.next().unwrap() catch null) {
                if (entry.kind != .file && entry.kind != .directory) continue;

                const entry_name = entry.name.slice();
                const entry_subpath = entrySubpath(allocator, dir_subpath, entry_name)?;

                let included = false;
                let is_unconditionally_included = false;

                if (dir_depth == 1) {
                    if (strings.eqlComptime(entry_name, "package.json")) continue;
                    if (strings.eqlComptime(entry_name, "node_modules")) continue;

                    if (entry.kind == .file && isUnconditionallyIncludedFile(entry_name)) {
                        included = true;
                        is_unconditionally_included = true;
                    }
                }

                if (!included) {
                    for (include of includes) {
                        if (include.flags.dirs_only && entry.kind != .directory) continue;

                        // include patters are not recursive unless they start with `**/`
                        // normally the behavior of `index.js` and `**/index.js` are the same,
                        // but includes require `**/`
                        const match_path = if (include.flags.@"leading **/") entry_name else entry_subpath;
                        switch (glob.match(include.glob.slice(), match_path)) {
                            .match => included = true,
                            .negate_no_match, .negate_match => unreachable,
                            else => {},
                        }
                    }
                }

                // There may be a "narrowing" exclusion that excludes a subset
                // of files within an included directory/pattern.
                if (included && !is_unconditionally_included && excludes.len > 0) {
                    for (exclude of excludes) {
                        if (exclude.flags.dirs_only && entry.kind != .directory) continue;

                        const match_path = if (exclude.flags.@"leading **/") entry_name else entry_subpath;
                        // NOTE: These patterns have `!` so `.match` logic is
                        // inverted here
                        switch (glob.match(exclude.glob.slice(), match_path)) {
                            .negate_no_match => included = false,
                            else => {},
                        }
                    }
                }

                // TODO: do not traverse directories that match patterns
                // excluding all files within them (e.g. `!test/**`)
                if (!included) {
                    if (entry.kind == .directory) {
                        for (bin of bins) {
                            if (bin.@"type" == .dir && strings.eqlLong(bin.path, entry_subpath, true)) {
                                continue :next_entry;
                            }
                        }
                        const subdir = openSubdir(dir, entry_name, entry_subpath);
                        dirs_guard.list.append(allocator, .{ subdir, entry_subpath, dir_depth + 1 })?;
                    }

                    continue;
                }

                switch (entry.kind) {
                    .directory => {
                        for (bin of bins) {
                            if (bin.@"type" == .dir && strings.eqlLong(bin.path, entry_subpath, true)) {
                                continue :next_entry;
                            }
                        }
                        const subdir = openSubdir(dir, entry_name, entry_subpath);
                        included_dirs_guard.list.append(allocator, .{ subdir, entry_subpath, dir_depth + 1 })?;
                    },
                    .file => {
                        const dedupe_entry = subpath_dedupe_guard.map.getOrPut(entry_subpath)?;
                        bun.assertWithLocation(!dedupe_entry.found_existing, @src());
                        if (dedupe_entry.found_existing) continue;

                        for (bin of bins) {
                            if (bin.@"type" == .file && strings.eqlLong(bin.path, entry_subpath, true)) {
                                continue :next_entry;
                            }
                        }

                        pack_queue.add({ path: entry_subpath })?;
                    },
                    else => unreachable,
                }
            }
        }

        // for each included dir, traverse it's entries, exclude any with `negate_no_match`.
        for (included_dir_info of included_dirs_guard.list.items) {
            try addEntireTree(
                allocator,
                bins,
                excludes,
                included_dir_info,
                pack_queue,
                &subpath_dedupe_guard.map,
                log_level,
            );
        }
    }

    /// Adds all files in a directory tree to `pack_list` (default ignores still apply)
    static fn addEntireTree(
        allocator: std.mem.Allocator,
        bins: []const BinInfo,
        excludes: []const Pattern,
        root_dir_info: DirInfo,
        pack_queue: *PackQueue,
        dedupe: *bun.StringHashMap(void),
        log_level: LogLevel,
    )! {
        let dirs_guard = DirInfoListGuard{ allocator: allocator };

        dirs_guard.list.append(allocator, root_dir_info)?;

        let ignores_guard = IgnoreListGuard{ allocator: allocator };

        let negated_excludes_guard = PatternListGuard{ allocator: allocator };

        if (excludes.len > 0) {
            negated_excludes_guard.list.ensureTotalCapacityPrecise(allocator, excludes.len)?;
            for (exclude of excludes) {
                negated_excludes_guard.list.append(allocator, exclude.asPositive())?;
            }
            try ignores_guard.list.append(allocator, IgnorePatterns{
                list: negated_excludes_guard.list.items,
                kind: .@"package.json",
                depth: 1,
                // always assume no relative path b/c matching is done from the
                // root directory
                has_rel_path: false,
            });
        }

        while (dir_info := dirs_guard.list.pop()) {
            const dir_guard = StdDirGuard{ dir: dir_info[0] };
            const dir = dir_guard.dir; const dir_subpath = dir_info[1]; const dir_depth = dir_info[2];

            while (last := ignores_guard.list.getLastOrNull()) {
                if (last.depth < dir_depth) break;

                last.deinit(allocator);
                ignores_guard.list.items.len -= 1;
            }

            if (patterns := IgnorePatterns.readFromDisk(allocator, dir, dir_depth)?) {
                ignores_guard.list.append(allocator, patterns)?;
            }

            if (comptime Environment.isDebug) {
                // make sure depths are in order
                if (ignores_guard.list.items.len > 0) {
                    for (i of 1..ignores_guard.list.items.len) {
                        bun.assertWithLocation(ignores_guard.list.items[i - 1].depth < ignores_guard.list.items[i].depth, @src());
                    }
                }
            }

            let iter = bun.DirIterator.iterate(bun.FD.fromStdDir(dir), .@"u8");
            next_entry: while (entry := iter.next().unwrap() catch null) {
                if (entry.kind != .file && entry.kind != .directory) continue;

                const entry_name = entry.name.slice();
                const entry_subpath = entrySubpath(allocator, dir_subpath, entry_name)?;

                if (dir_depth == root_dir_info[2]) {
                    if (entry.kind == .directory && strings.eqlComptime(entry_name, "node_modules")) continue;
                }

                if (used_pattern_info := isExcluded(entry, entry_subpath, dir_depth, ignores_guard.list.items)) {
                    if (log_level.isVerbose()) {
                        const pattern = used_pattern_info.pattern; const kind = used_pattern_info.kind;
                        Output.prettyln("<r><blue>ignore<r> <d>[{s}:{s}]<r> {s}{s}", .{
                            @tagName(kind),
                            pattern,
                            entry_subpath,
                            if (entry.kind == .directory) "/" else "",
                        });
                        Output.flush();
                    }
                    continue;
                }

                switch (entry.kind) {
                    .file => {
                        const dedupe_entry = dedupe.getOrPut(entry_subpath)?;
                        if (dedupe_entry.found_existing) continue;
                        for (bin of bins) {
                            if (bin.@"type" == .file && strings.eqlLong(bin.path, entry_subpath, true)) {
                                continue :next_entry;
                            }
                        }
                        pack_queue.add({ path: entry_subpath })?;
                    },
                    .directory => {
                        for (bin of bins) {
                            if (bin.@"type" == .dir && strings.eqlLong(bin.path, entry_subpath, true)) {
                                continue :next_entry;
                            }
                        }

                        const subdir = openSubdir(dir, entry_name, entry_subpath);

                        try dirs_guard.list.append(allocator, .{
                            subdir,
                            entry_subpath,
                            dir_depth + 1,
                        });
                    },
                    else => unreachable,
                }
            }
        }
    }

    static fn openSubdir(
        dir: std.fs.Dir,
        entry_name: string,
        entry_subpath: stringZ,
    ): std.fs.Dir {
        return dir.openDirZ(
            entryNameZ(entry_name, entry_subpath),
            { iterate: true },
        ) catch |e| {
            Output.err(e, "failed to open directory \"{s}\" for packing", .{entry_subpath});
            Global.crash();
        };
    }

    static fn entrySubpath(
        allocator: std.mem.Allocator,
        dir_subpath: string,
        entry_name: string,
    ): OOM!stringZ {
        return std.fmt.allocPrintSentinel(allocator, "{s}{s}{s}", .{
            dir_subpath,
            if (dir_subpath.len == 0) "" else "/",
            entry_name,
        }, 0);
    }

    static fn entryNameZ(
        entry_name: string,
        entry_subpath: stringZ,
    ): stringZ {
        // doing this because `entry_subpath` has a sentinel and I don't trust `entry.name.sliceAssumeZ()`
        return entry_subpath[entry_subpath.len - entry_name.len ..][0..entry_name.len :0];
    }

    static fn iterateBundledDeps(
        ctx: *Context,
        root_dir: std.fs.Dir,
        log_level: LogLevel,
    ): OOM!PackQueue {
        const void_val: void = undefined;
        let bundled_pack_queue = PackQueue.init(ctx.allocator, void_val);
        if (ctx.bundled_deps.items.len == 0) return bundled_pack_queue;

        let dir_guard = StdDirGuard{
            dir: root_dir.openDirZ("node_modules", { iterate: true }) catch |e| {
                switch (e) {
                    // ignore node_modules if it isn't a directory, or doesn't exist
                    error.NotDir, error.FileNotFound => return bundled_pack_queue,

                    else => {
                        Output.err(e, "failed to open \"node_modules\" to pack bundled dependencies", {});
                        Global.crash();
                    },
                }
            },
        };
        const dir = dir_guard.dir;

        // A set of bundled dependency locations
        // - node_modules/is-even
        // - node_modules/is-even/node_modules/is-odd
        // - node_modules/is-odd
        // - ...
        let dedupe_guard = StringHashMapVoidGuard{ map: bun.StringHashMap(void).init(ctx.allocator) };

        let additional_bundled_deps_guard = DirInfoListGuard{ allocator: ctx.allocator };

        let iter = bun.DirIterator.iterate(bun.FD.fromStdDir(dir), .@"u8");
        while (entry := iter.next().unwrap() catch null) {
            if (entry.kind != .directory) continue;

            const _entry_name = entry.name.slice();

            if (strings.startsWithChar(_entry_name, '@')) {
                const concat = entrySubpath(ctx.allocator, "node_modules", _entry_name)?;

                const scoped_dir_guard = StdDirGuard{
                    dir: root_dir.openDirZ(concat, { iterate: true }) catch {
                        continue;
                    },
                };
                const scoped_dir = scoped_dir_guard.dir;

                let scoped_iter = bun.DirIterator.iterate(bun.FD.fromStdDir(scoped_dir), .@"u8");
                while (sub_entry := scoped_iter.next().unwrap() catch null) {
                    const entry_name = entrySubpath(ctx.allocator, _entry_name, sub_entry.name.slice())?;

                    for (*dep of ctx.bundled_deps.items) {
                        bun.assertWithLocation(dep.from_root_package_json, @src());
                        if (!strings.eqlLong(entry_name, dep.name, true)) continue;

                        const entry_subpath = entrySubpath(ctx.allocator, "node_modules", entry_name)?;

                        const dedupe_entry = dedupe_guard.map.getOrPut(entry_subpath)?;
                        if (dedupe_entry.found_existing) {
                            // already got to it in `addBundledDep` below
                            dep.was_packed = true;
                            break;
                        }

                        const subdir = openSubdir(dir, entry_name, entry_subpath);
                        dep.was_packed = true;
                        try addBundledDep(
                            ctx,
                            root_dir,
                            .{ subdir, entry_subpath, 2 },
                            &bundled_pack_queue,
                            &dedupe_guard.map,
                            &additional_bundled_deps_guard.list,
                            log_level,
                        );

                        break;
                    }
                }
            } else {
                const entry_name = _entry_name;
                for (*dep of ctx.bundled_deps.items) {
                    bun.assertWithLocation(dep.from_root_package_json, @src());
                    if (!strings.eqlLong(entry_name, dep.name, true)) continue;

                    const entry_subpath = entrySubpath(ctx.allocator, "node_modules", entry_name)?;

                    const dedupe_entry = dedupe_guard.map.getOrPut(entry_subpath)?;
                    if (dedupe_entry.found_existing) {
                        // already got to it in `addBundledDep` below
                        dep.was_packed = true;
                        break;
                    }

                    const subdir = openSubdir(dir, entry_name, entry_subpath);
                    dep.was_packed = true;
                    try addBundledDep(
                        ctx,
                        root_dir,
                        .{ subdir, entry_subpath, 2 },
                        &bundled_pack_queue,
                        &dedupe_guard.map,
                        &additional_bundled_deps_guard.list,
                        log_level,
                    );

                    break;
                }
            }
        }

        while (bundled_dir_info := additional_bundled_deps_guard.list.pop()) {
            const dir_subpath = bundled_dir_info[1];
            const maybe_slash = strings.lastIndexOfChar(dir_subpath, '/');
            bun.assertWithLocation(maybe_slash != null, @src());
            let dep_name = dir_subpath;
            if (slash := maybe_slash) {
                dep_name = dir_subpath[slash + 1 ..];
            }

            try ctx.bundled_deps.append(ctx.allocator, {
                name: dep_name,
                from_root_package_json: false,
                was_packed: true,
            });

            try addBundledDep(
                ctx,
                root_dir,
                bundled_dir_info,
                &bundled_pack_queue,
                &dedupe_guard.map,
                &additional_bundled_deps_guard.list,
                log_level,
            );
        }

        return bundled_pack_queue;
    }

    static fn addBundledDep(
        ctx: *Context,
        root_dir: std.fs.Dir,
        bundled_dir_info: DirInfo,
        bundled_pack_queue: *PackQueue,
        dedupe: *bun.StringHashMap(void),
        additional_bundled_deps: *std.ArrayListUnmanaged(DirInfo),
        log_level: LogLevel,
    )! {
        ctx.stats.bundled_deps += 1;

        let dirs_guard = DirInfoListGuard{ allocator: ctx.allocator };

        dirs_guard.list.append(ctx.allocator, bundled_dir_info)?;

        while (dir_info := dirs_guard.list.pop()) {
            const dir_guard = StdDirGuard{ dir: dir_info[0] };
            const dir = dir_guard.dir; const dir_subpath = dir_info[1]; const dir_depth = dir_info[2];

            let iter = bun.DirIterator.iterate(bun.FD.fromStdDir(dir), .@"u8");
            while (entry := iter.next().unwrap() catch null) {
                if (entry.kind != .file && entry.kind != .directory) continue;

                const entry_name = entry.name.slice();
                const entry_subpath = entrySubpath(ctx.allocator, dir_subpath, entry_name)?;

                if (dir_depth == bundled_dir_info[2]) root_depth: {
                    if (strings.eqlComptime(entry_name, "package.json")) {
                        if (entry.kind != .file) break :root_depth;
                        // find more dependencies to bundle
                        const source = &(File.toSourceAt(dir, entryNameZ(entry_name, entry_subpath), ctx.allocator, {}).unwrap() catch |e| {
                            Output.err(e, "failed to read package.json: \"{s}\"", .{entry_subpath});
                            Global.crash();
                        });

                        const json = JSON.parsePackageJSONUTF8(source, ctx.manager.log, ctx.allocator) catch
                            break :root_depth;

                        // for each dependency in `dependencies` find the closest node_modules folder
                        // with the dependency name as a dir entry, starting from the node_modules of the
                        // current bundled dependency

                        const bundled_dep_groups: [2]string = [ "dependencies", "optionalDependencies" ];
                        for (dependency_group of bundled_dep_groups[0..]) {
                            const maybe_deps_expr = json.get(dependency_group);
                    if (maybe_deps_expr == null) continue;
                    const dependencies_expr = maybe_deps_expr.?;
                            if (dependencies_expr.data != .e_object) continue;

                            const dependencies = dependencies_expr.data.e_object;
                            const dep_slice = dependencies.properties.slice();
                                let dep_i: usize = 0;
                                next_dep: while (dep_i < dep_slice.len) : (dep_i += 1) {
                                    const dep = dep_slice[dep_i];
                                if (dep.key == null) continue;
                                if (dep.value == null) continue;

                                const maybe_dep_name = dep.key.?.asString(ctx.allocator);
                                    if (maybe_dep_name == null) continue;
                                    const dep_name = maybe_dep_name.?;

                                let dep_subpath = std.fmt.allocPrintSentinel(ctx.allocator, "{s}/node_modules/{s}", .{
                                    dir_subpath,
                                    dep_name,
                                }, 0)?;

                                // starting at `node_modules/is-even/node_modules/is-odd`
                                let dep_dir_depth: usize = bundled_dir_info[2] + 2;

                                const dep_dir_opt: std.fs.Dir? = root_dir.openDirZ(dep_subpath, { iterate: true }) catch @as(std.fs.Dir?, null);
                                if (dep_dir := dep_dir_opt) {
                                    const dedupe_entry = dedupe.getOrPut(dep_subpath)?;
                                    if (dedupe_entry.found_existing) continue;

                                    additional_bundled_deps.append(ctx.allocator, .{ dep_dir, dep_subpath, dep_dir_depth })?;
                                } else {
                                    // keep searching

                                    // slice off the `node_modules` from above
                                    let remain: []u8 = dep_subpath[0..dir_subpath.len];

                                    while (node_modules_start := strings.lastIndexOf(remain, "node_modules")) {
                                        dep_dir_depth -= 2;
                                        const node_modules_end = node_modules_start + "node_modules".len;
                                        dep_subpath[node_modules_end] = '/';
                                        @memcpy(dep_subpath[node_modules_end + 1 ..][0..dep_name.len], dep_name);
                                        dep_subpath[node_modules_end + 1 + dep_name.len] = 0;
                                        const parent_dep_subpath = dep_subpath[0 .. node_modules_end + 1 + dep_name.len :0];
                                        remain = remain[0..node_modules_start];

                                        const parent_dep_dir = root_dir.openDirZ(parent_dep_subpath, { iterate: true }) catch continue;

                                        const dedupe_entry = dedupe.getOrPut(parent_dep_subpath)?;
                                        if (dedupe_entry.found_existing) continue :next_dep;

                                        additional_bundled_deps.append(ctx.allocator, .{ parent_dep_dir, parent_dep_subpath, dep_dir_depth })?;
                                        continue :next_dep;
                                    }
                                }
                            }
                        }

                        break :root_depth;
                    }

                    if (strings.eqlComptime(entry_name, "node_modules")) continue;
                }

                if (used_pattern_info := isExcluded(entry, entry_subpath, dir_depth, [])) {
                    if (log_level.isVerbose()) {
                        const pattern = used_pattern_info.pattern; const kind = used_pattern_info.kind;
                        Output.prettyln("<r><blue>ignore<r> <d>[{s}:{s}]<r> {s}{s}", .{
                            @tagName(kind),
                            pattern,
                            entry_subpath,
                            if (entry.kind == .directory) "/" else "",
                        });
                        Output.flush();
                    }
                    continue;
                }

                switch (entry.kind) {
                    .file => {
                        bundled_pack_queue.add({ path: entry_subpath })?;
                    },
                    .directory => {
                        const subdir = openSubdir(dir, entry_name, entry_subpath);

                        try dirs_guard.list.append(ctx.allocator, .{
                            subdir,
                            entry_subpath,
                            dir_depth + 1,
                        });
                    },
                    else => unreachable,
                }
            }
        }
    }

    /// Returns a list of files to pack and another list of files from bundled dependencies
    static fn iterateProjectTree(
        allocator: std.mem.Allocator,
        pack_queue: *PackQueue,
        bins: []const BinInfo,
        root_dir: DirInfo,
        log_level: LogLevel,
    )! {
        const ignores_guard = IgnoreListGuard{ allocator: allocator };

        // Stacks and depth-first traversal. Doing so means we can push and pop from
        // ignore patterns without needing to clone the entire list for future use.
        let dirs_guard = DirInfoListGuard{ allocator: allocator };

        dirs_guard.list.append(allocator, root_dir)?;

        while (dir_info := dirs_guard.list.pop()) {
            const dir_guard = StdDirGuard{ dir: dir_info[0], active: dir_info[2] != 1 };
            const dir = dir_guard.dir; const dir_subpath = dir_info[1]; const dir_depth = dir_info[2];

            while (last := ignores_guard.list.getLastOrNull()) {
                if (last.depth < dir_depth) break;

                // pop patterns from files greater than or equal to the current depth.
                last.deinit(allocator);
                ignores_guard.list.items.len -= 1;
            }

            if (patterns := IgnorePatterns.readFromDisk(allocator, dir, dir_depth)?) {
                ignores_guard.list.append(allocator, patterns)?;
            }

            if (comptime Environment.isDebug) {
                // make sure depths are in order
                if (ignores_guard.list.items.len > 0) {
                    for (i of 1..ignores_guard.list.items.len) {
                        bun.assertWithLocation(ignores_guard.list.items[i - 1].depth < ignores_guard.list.items[i].depth, @src());
                    }
                }
            }

            let dir_iter = bun.DirIterator.iterate(bun.FD.fromStdDir(dir), .@"u8");
            next_entry: while (entry := dir_iter.next().unwrap() catch null) {
                if (entry.kind != .file && entry.kind != .directory) continue;

                const entry_name = entry.name.slice();
                const entry_subpath = entrySubpath(allocator, dir_subpath, entry_name)?;

                if (dir_depth == 1) {
                    // Special case root package.json. It is always included
                    // and is possibly edited, so it's easier to handle it
                    // separately
                    if (strings.eqlComptime(entry_name, "package.json")) continue;

                    // bundled dependencies are included only if they exist on disk.
                    // handled later for simplicity
                    if (strings.eqlComptime(entry_name, "node_modules")) continue;
                }

                if (used_pattern_info := isExcluded(entry, entry_subpath, dir_depth, ignores_guard.list.items)) {
                    if (log_level.isVerbose()) {
                        const pattern = used_pattern_info.pattern; const kind = used_pattern_info.kind;
                        Output.prettyln("<r><blue>ignore<r> <d>[{s}:{s}]<r> {s}{s}", .{
                            @tagName(kind),
                            pattern,
                            entry_subpath,
                            if (entry.kind == .directory) "/" else "",
                        });
                        Output.flush();
                    }
                    continue;
                }

                switch (entry.kind) {
                    .file => {
                        bun.assertWithLocation(entry_subpath.len > 0, @src());
                        for (bin of bins) {
                            if (bin.@"type" == .file && strings.eqlLong(bin.path, entry_subpath, true)) {
                                continue :next_entry;
                            }
                        }
                        pack_queue.add({ path: entry_subpath })?;
                    },
                    .directory => {
                        for (bin of bins) {
                            if (bin.@"type" == .dir && strings.eqlLong(bin.path, entry_subpath, true)) {
                                continue :next_entry;
                            }
                        }

                        const subdir = openSubdir(dir, entry_name, entry_subpath);

                        try dirs_guard.list.append(allocator, .{
                            subdir,
                            entry_subpath,
                            dir_depth + 1,
                        });
                    },
                    else => unreachable,
                }
            }
        }
    }

    static fn getBundledDeps(
        allocator: std.mem.Allocator,
        json: Expr,
        comptime field: string,
    ): OOM!(std.ArrayListUnmanaged(BundledDep))? {
        let deps: std.ArrayListUnmanaged(BundledDep) = {};
        const maybe_bundled_deps = json.get(field);
        if (maybe_bundled_deps == null) return null;
        const bundled_deps = maybe_bundled_deps.?;

        invalid_field: {
            switch (bundled_deps.data) {
                .e_array => {
                    const maybe_iter = bundled_deps.asArray();
                    if (maybe_iter == null) return .{};
                    let iter = maybe_iter.?;

                    while (bundled_dep_item := iter.next()) {
                        const maybe_bdep = bundled_dep_item.asStringCloned(allocator)?;
                        if (maybe_bdep == null) break :invalid_field;
                        const bundled_dep = maybe_bdep.?;
                        try deps.append(allocator, {
                            name: bundled_dep,
                            from_root_package_json: true,
                        });
                    }
                },
                .e_boolean => {
                    const maybe_b = bundled_deps.asBool();
                    if (maybe_b == null) return .{};
                    const b = maybe_b.?;
                    if (!b == true) return {};

                    if (dependencies_expr := json.get("dependencies")) {
                        switch (dependencies_expr.data) {
                            .e_object => |dependencies| {
                                for (*dependency of dependencies.properties.slice()) {
                                    if (dependency.key == null) continue;
                                    if (dependency.value == null) continue;

                                    const maybe_bdep = (dependency.key.?.asStringCloned(allocator)) catch null;
                                    if (maybe_bdep == null) break :invalid_field;
                                    const bundled_dep = maybe_bdep.?;
                                    try deps.append(allocator, {
                                        name: bundled_dep,
                                        from_root_package_json: true,
                                    });
                                }
                            },
                            else => {},
                        }
                    }
                },
                else => break :invalid_field,
            }

            return deps;
        }

        Output.errGeneric("expected `{s}` to be a boolean or an array of strings", .{field});
        Global.crash();
    }

    const BinInfo = _BinInfo;

    static fn getPackageBins(
        allocator: std.mem.Allocator,
        json: Expr,
    ): OOM![]const BinInfo {
        let bins: std.ArrayListUnmanaged(BinInfo) = {};

        let path_buf: PathBuffer = undefined;

        if (bin := json.asProperty("bin")) {
            if (bin_str := bin.expr.asString(allocator)) {
                const normalized = bun.path.normalizeBuf(bin_str, &path_buf, .posix);
                try bins.append(allocator, {
                    path: try allocator.dupeZ(u8, normalized),
                    @"type": .file,
                });
                return bins.items;
            }

            switch (bin.expr.data) {
                .e_object => |bin_obj| {
                    if (bin_obj.properties.len == 0) return [];

                    for (bin_prop of bin_obj.properties.slice()) {
                        if (bin_prop_value := bin_prop.value) {
                            if (bin_str := bin_prop_value.asString(allocator)) {
                                const normalized = bun.path.normalizeBuf(bin_str, &path_buf, .posix);
                                try bins.append(allocator, {
                                    path: try allocator.dupeZ(u8, normalized),
                                    @"type": .file,
                                });
                            }
                        }
                    }
                },
                else => {},
            }

            return bins.items;
        }

        if (directories := json.asProperty("directories")) {
            switch (directories.expr.data) {
                .e_object => |directories_obj| {
                    if (bin := directories_obj.asProperty("bin")) {
                        if (bin_str := bin.expr.asString(allocator)) {
                            const normalized = bun.path.normalizeBuf(bin_str, &path_buf, .posix);
                            try bins.append(allocator, {
                                path: try allocator.dupeZ(u8, normalized),
                                @"type": .dir,
                            });
                        }
                    }
                },
                else => {},
            }
        }

        return bins.items;
    }

    static fn isPackageBin(bins: []const BinInfo, maybe_bin_path: string): bool {
        for (bin of bins) {
            switch (bin.@"type") {
                .file => {
                    if (strings.eqlLong(bin.path, maybe_bin_path, true)) {
                        return true;
                    }
                },
                .dir => {
                    const bin_without_trailing = strings.withoutTrailingSlash(bin.path);
                    if (strings.hasPrefix(maybe_bin_path, bin_without_trailing)) {
                        const remain = maybe_bin_path[bin_without_trailing.len..];
                        if (remain.len > 1 && remain[0] == '/' && !strings.containsChar(remain[1..], '/')) {
                            return true;
                        }
                    }
                },
            }
        }

        return false;
    }

    static fn isExcluded(
        entry: DirIterator.IteratorResult,
        entry_subpath: stringZ,
        dir_depth: usize,
        ignores: []const IgnorePatterns,
    ): ExcludedInfo? {
        const entry_name = entry.name.slice();

        if (dir_depth == 1) {
            // first, check files that can never be ignored. project root
            // directory only
            if (isUnconditionallyIncludedFile(entry_name) || isSpecialFileOrVariant(entry_name, "CHANGELOG")) {
                return null;
            }

            // check default ignores that only apply to the root project directory
            inline for (pattern of root_default_ignore_patterns) {
                switch (glob.match(pattern, entry_name)) {
                    .match => {
                        // cannot be reversed
                        return ExcludedInfo{
                            pattern: pattern,
                            kind: .default,
                        };
                    },

                    .no_match => {},

                    // default patterns don't use `!`
                    .negate_no_match => unreachable,
                    .negate_match => unreachable,
                }
            }
        }

        let ignore_pattern: []const u8 = [];
        let ignore_kind: IgnorePatterns.Kind = .@".npmignore";

        // then check default ignore list. None of the defaults contain slashes
        // so just match agaist entry name
        let ignored = false;

        inline for (pattern_info of default_ignore_patterns) {
            const pattern = pattern_info.pattern; const can_override = pattern_info.can_override;
            switch (glob.match(pattern, entry_name)) {
                .match => {
                    if (can_override) {
                        ignored = true;
                        ignore_pattern = pattern;
                        ignore_kind = .default;

                        // break. doesnt matter if more default patterns
                        // match this path
                        break;
                    }

                    return ExcludedInfo{
                        pattern: pattern,
                        kind: .default,
                    };
                },
                .no_match => {},

                // default patterns don't use `!`
                .negate_no_match => unreachable,
                .negate_match => unreachable,
            }
        }

        // lastly, check each .npmignore/.gitignore from root directory to
        // the current directory.
        for (ignore of ignores) {
            let rel: []const u8 = entry_subpath;
            if (ignore.has_rel_path) {
                // trim parent directories up to the directory
                // containing this ignore file
                for (depth_idx of 1..ignore.depth) {
                    if (sep := strings.indexOfChar(rel, '/')) {
                        rel = rel[sep + 1 ..];
                    }
                }
            }
            for (pattern of ignore.list) {
                if (pattern.flags.dirs_only && entry.kind != .directory) continue;

                const match_path = if (pattern.flags.rel_path) rel else entry_name;
                switch (glob.match(pattern.glob.slice(), match_path)) {
                    .match => {
                        ignored = true;
                        ignore_pattern = pattern.glob.slice();
                        ignore_kind = ignore.kind;
                    },
                    .negate_no_match => ignored = false,
                    else => {},
                }
            }
        }

        if (!ignored) return null;
        return ExcludedInfo{
            pattern: ignore_pattern,
            kind: ignore_kind,
        };
    }

    const BufferedFileReader = bun.deprecated.BufferedReader(1024 * 512, File.Reader);

    static fn pack(
        ctx: *Context,
        abs_package_json_path: stringZ,
        comptime for_publish: bool,
    ): !if (for_publish) Publish.Context(true) else void {
        const manager = ctx.manager;
        const log_level = manager.options.log_level;
        let json = switch (manager.workspace_package_json_cache.getWithPath(manager.allocator, manager.log, abs_package_json_path, {
            guess_indentation: true,
        })) {
            .read_err => |e| {
                Output.err(e, "failed to read package.json: {s}", .{abs_package_json_path});
                Global.crash();
            },
            .parse_err => |e| {
                Output.err(e, "failed to parse package.json: {s}", .{abs_package_json_path});
                manager.log.print(Output.errorWriter()) catch {};
                Global.crash();
            },
            .entry => |entry| entry,
        };

        if (comptime for_publish) {
            if (config := json.root.get("publishConfig")) {
                if (manager.options.publish_config.tag.len == 0) {
                    if (tag := config.getStringCloned(ctx.allocator, "tag")?) {
                        manager.options.publish_config.tag = tag;
                    }
                }
                if (manager.options.publish_config.access == null) {
                    if (access := config.getString(ctx.allocator, "access")?) {
                        const maybe_access = PackageManager.Options.Access.fromStr(access[0]);
                if (maybe_access == null) {
                    Output.errGeneric("expected `access` to be `public` or `restricted`", .{});
                    Global.crash();
                }
                manager.options.publish_config.access = maybe_access.?;
                if (false) {
                    Output.errGeneric("invalid `access` value: '{s}'", .{access[0]});
                    Global.crash();
                }
                    }
                }
            }

            // maybe otp
        }

        const maybe_pne = json.root.get("name");
        if (maybe_pne == null) return error.MissingPackageName;
        let package_name_expr: Expr = maybe_pne.?;
        const maybe_pn = (package_name_expr.asStringCloned(ctx.allocator)) catch return error.InvalidPackageName;
        if (maybe_pn == null) return error.InvalidPackageName;
        let package_name_guard = SliceFreeGuard{
            slice: maybe_pn.?,
            allocator: ctx.allocator,
            active: !for_publish,
        };
        let package_name = package_name_guard.slice;
        if (comptime for_publish) {
            const is_scoped = Dependency.isScopedPackageName(package_name)?;
            if (access := manager.options.publish_config.access) {
                if (access == .restricted && !is_scoped) {
                    return error.RestrictedUnscopedPackage;
                }
            }
        }
        if (package_name.len == 0) return error.InvalidPackageName;

        const maybe_pve = json.root.get("version");
        if (maybe_pve == null) return error.MissingPackageVersion;
        let package_version_expr: Expr = maybe_pve.?;
        const maybe_pv = (package_version_expr.asStringCloned(ctx.allocator)) catch return error.InvalidPackageVersion;
        if (maybe_pv == null) return error.InvalidPackageVersion;
        let package_version_guard = SliceFreeGuard{
            slice: maybe_pv.?,
            allocator: ctx.allocator,
            active: !for_publish,
        };
        let package_version = package_version_guard.slice;
        if (package_version.len == 0) return error.InvalidPackageVersion;

        if (comptime for_publish) {
            if (is_private := json.root.get("private")) {
                if (is_private_bool := is_private.asBool()) {
                    if (is_private_bool) {
                        return error.PrivatePackage;
                    }
                }
            }
        }

        let this_transpiler: bun.transpiler.Transpiler = undefined;

        * = RunCommand.configureEnvForRun(
            ctx.command_ctx,
            &this_transpiler,
            manager.env,
            manager.options.log_level != .silent,
            false,
        ) catch |e| {
            switch (e) {
                error.OutOfMemory => return error.OutOfMemory,
                else => {
                    Output.errGeneric("failed to run pack scripts due to error: {s}\n", .{@errorName(e)});
                    Global.crash();
                },
            }
        };

        const abs_workspace_path: string = strings.withoutTrailingSlash(strings.withoutSuffixComptime(abs_package_json_path, "package.json"));
        manager.env.map.put("npm_command", "pack")?;

        const post_scripts_res = post_scripts: {
            // --ignore-scripts
            if (!manager.options.do.run_scripts) break :post_scripts .{ null, null, null, false };

            const maybe_scripts = json.root.asProperty("scripts");
            if (maybe_scripts == null) break :post_scripts .{ null, null, null, false };
            const scripts = maybe_scripts.?;
            if (scripts.expr.data != .e_object) break :post_scripts .{ null, null, null, false };

            // Track whether any scripts ran that could modify package.json
            let did_run_scripts = false;

            if (comptime for_publish) {
                if (prepublish_only_script_str := scripts.expr.get("prepublishOnly")) {
                    if (prepublish_only := prepublish_only_script_str.asString(ctx.allocator)) {
                        did_run_scripts = true;
                        * = RunCommand.runPackageScriptForeground(
                            ctx.command_ctx,
                            ctx.allocator,
                            prepublish_only,
                            "prepublishOnly",
                            abs_workspace_path,
                            this_transpiler.env,
                            [],
                            manager.options.log_level == .silent,
                            ctx.command_ctx.debug.use_system_shell,
                        ) catch |e| {
                            switch (e) {
                                error.MissingShell => {
                                    Output.errGeneric("failed to find shell executable to run prepublishOnly script", {});
                                    Global.crash();
                                },
                                error.OutOfMemory => return error.OutOfMemory,
                            }
                        };
                    }
                }
            }

            if (prepack_script := scripts.expr.get("prepack")) {
                if (prepack_script_str := prepack_script.asString(ctx.allocator)) {
                    did_run_scripts = true;
                    * = RunCommand.runPackageScriptForeground(
                        ctx.command_ctx,
                        ctx.allocator,
                        prepack_script_str,
                        "prepack",
                        abs_workspace_path,
                        this_transpiler.env,
                        [],
                        manager.options.log_level == .silent,
                        ctx.command_ctx.debug.use_system_shell,
                    ) catch |e| {
                        switch (e) {
                            error.MissingShell => {
                                Output.errGeneric("failed to find shell executable to run prepack script", {});
                                Global.crash();
                            },
                            error.OutOfMemory => return error.OutOfMemory,
                        }
                    };
                }
            }

            if (prepare_script := scripts.expr.get("prepare")) {
                if (prepare_script_str := prepare_script.asString(ctx.allocator)) {
                    did_run_scripts = true;
                    * = RunCommand.runPackageScriptForeground(
                        ctx.command_ctx,
                        ctx.allocator,
                        prepare_script_str,
                        "prepare",
                        abs_workspace_path,
                        this_transpiler.env,
                        [],
                        manager.options.log_level == .silent,
                        ctx.command_ctx.debug.use_system_shell,
                    ) catch |e| {
                        switch (e) {
                            error.MissingShell => {
                                Output.errGeneric("failed to find shell executable to run prepare script", {});
                                Global.crash();
                            },
                            error.OutOfMemory => return error.OutOfMemory,
                        }
                    };
                }
            }

            let postpack_script: ([]const u8)? = null;
            if (postpack := scripts.expr.get("postpack")) {
                postpack_script = postpack.asString(ctx.allocator);
            }

            if (comptime for_publish) {
                let publish_script: ([]const u8)? = null;
                let postpublish_script: ([]const u8)? = null;
                if (publish := scripts.expr.get("publish")) {
                    publish_script = publish.asStringCloned(ctx.allocator)?;
                }
                if (postpublish := scripts.expr.get("postpublish")) {
                    postpublish_script = postpublish.asStringCloned(ctx.allocator)?;
                }

                break :post_scripts .{ postpack_script, publish_script, postpublish_script, did_run_scripts };
            }

            break :post_scripts .{ postpack_script, null, null, did_run_scripts };
        };
        const postpack_script = post_scripts_res[0];
        const publish_script: ([]const u8)? = post_scripts_res[1];
        const postpublish_script: ([]const u8)? = post_scripts_res[2];
        const ran_scripts: bool = post_scripts_res[3];

        // If any lifecycle scripts ran, they may have modified package.json,
        // so we need to re-read it from disk to pick up any changes.
        if (ran_scripts) {
            // Invalidate the cached entry by removing it.
            // On Windows, the cache key is stored with POSIX path separators,
            // so we need to convert the path before removing.
            let cache_key_buf: if (Environment.isWindows) PathBuffer else void = undefined;
            const cache_key = if (comptime Environment.isWindows) blk: {
                @memcpy(cache_key_buf[0..abs_package_json_path.len], abs_package_json_path);
                bun.path.dangerouslyConvertPathToPosixInPlace(u8, cache_key_buf[0..abs_package_json_path.len]);
                break :blk cache_key_buf[0..abs_package_json_path.len];
            } else abs_package_json_path;
            * = manager.workspace_package_json_cache.map.remove(cache_key);

            // Re-read package.json from disk
            json = switch (manager.workspace_package_json_cache.getWithPath(manager.allocator, manager.log, abs_package_json_path, {
                guess_indentation: true,
            })) {
                .read_err => |e| {
                    Output.err(e, "failed to read package.json: {s}", .{abs_package_json_path});
                    Global.crash();
                },
                .parse_err => |e| {
                    Output.err(e, "failed to parse package.json: {s}", .{abs_package_json_path});
                    manager.log.print(Output.errorWriter()) catch {};
                    Global.crash();
                },
                .entry => |entry| entry,
            };

            // Re-validate private flag after scripts may have modified it.
            if (comptime for_publish) {
                if (is_private := json.root.get("private")) {
                    if (is_private_val := is_private.asBool()) {
                        if (is_private_val) {
                            return error.PrivatePackage;
                        }
                    }
                }
            }

            // Re-read name and version from the updated package.json, since lifecycle
            // scripts (e.g. prepublishOnly, prepack) may have modified them.
            const maybe_pne2 = json.root.get("name");
            if (maybe_pne2 == null) return error.MissingPackageName;
            package_name_expr = maybe_pne2.?;
            const maybe_pn2 = (package_name_expr.asStringCloned(ctx.allocator)) catch return error.InvalidPackageName;
            if (maybe_pn2 == null) return error.InvalidPackageName;
            if (comptime !for_publish) ctx.allocator.free(package_name_guard.slice);
            package_name_guard.slice = maybe_pn2.?;
            package_name = maybe_pn2.?;
            if (package_name.len == 0) return error.InvalidPackageName;

            const maybe_pve2 = json.root.get("version");
            if (maybe_pve2 == null) return error.MissingPackageVersion;
            package_version_expr = maybe_pve2.?;
            const maybe_pv2 = (package_version_expr.asStringCloned(ctx.allocator)) catch return error.InvalidPackageVersion;
            if (maybe_pv2 == null) return error.InvalidPackageVersion;
            if (comptime !for_publish) ctx.allocator.free(package_version_guard.slice);
            package_version_guard.slice = maybe_pv2.?;
            package_version = maybe_pv2.?;
            if (package_version.len == 0) return error.InvalidPackageVersion;
        }

        // Create the edited package.json content after lifecycle scripts have run
        const edited_package_json = editRootPackageJSON(ctx.allocator, ctx.lockfile, json)?;

        const root_dir_guard = StdDirGuard{
            dir: root_dir: {
                let path_buf: PathBuffer = undefined;
                @memcpy(path_buf[0..abs_workspace_path.len], abs_workspace_path);
                path_buf[abs_workspace_path.len] = 0;
                break :root_dir std.fs.openDirAbsoluteZ(path_buf[0..abs_workspace_path.len :0], {
                    iterate: true,
                }) catch |e| {
                    Output.err(e, "failed to open root directory: {s}\n", .{abs_workspace_path});
                    Global.crash();
                };
            },
        };
        const root_dir = root_dir_guard.dir;

        // Scan for a README file so the registry receives the same
        // `readme` / `readmeFilename` metadata that `npm publish` sends.
        // `findWorkspaceReadme` opens its own directory handle because
        // `root_dir` is iterated below and its kernel readdir offset gets
        // exhausted.
        const void_val: void = undefined;
        const workspace_readme: if (for_publish) Publish.ReadmeInfo? else void =
            if (comptime for_publish) Publish.findWorkspaceReadme(ctx.allocator, abs_workspace_path)? else void_val;

        if (bd := getBundledDeps(ctx.allocator, json.root, "bundledDependencies")?) {
            ctx.bundled_deps = bd;
        } else if (bd2 := getBundledDeps(ctx.allocator, json.root, "bundleDependencies")?) {
            ctx.bundled_deps = bd2;
        } else {
            ctx.bundled_deps = {};
        }

        let pack_queue_guard = PackQueueGuard{ queue: .init(ctx.allocator, void_val) };

        const bins_guard = BinsGuard{
            bins: getPackageBins(ctx.allocator, json.root)?,
            allocator: ctx.allocator,
        };
        const bins = bins_guard.bins;

        for (bin of bins) {
            switch (bin.@"type") {
                .file => {
                    pack_queue_guard.queue.add({ path: bin.path, optional: true })?;
                },
                .dir => {
                    const bin_dir = root_dir.openDir(bin.path, { iterate: true }) catch {
                        // non-existent bins are ignored
                        continue;
                    };

                    iterateProjectTree(ctx.allocator, &pack_queue_guard.queue, [], .{ bin_dir, bin.path, 2 }, log_level)?;
                },
            }
        }

        iterate_project_tree: {
            if (files := json.root.get("files")) {
                files_error: {
                    if (_files_array := files.asArray()) {
                        let includes_guard = PatternListGuard{ allocator: ctx.allocator };
                        let excludes_guard = PatternListGuard{ allocator: ctx.allocator };

                        let path_buf: PathBuffer = undefined;
                        let files_array = _files_array;
                        while (files_entry := files_array.next()) {
                            if (file_entry_str := files_entry.asString(ctx.allocator)) {
                                const normalized = bun.path.normalizeBuf(file_entry_str, &path_buf, .posix);
                                const maybe_p = Pattern.fromUTF8(ctx.allocator, normalized) catch continue;
                if (maybe_p == null) continue;
                const parsed = maybe_p.?;
                                if (parsed.flags.negated) {
                                    @branchHint(.unlikely); // most "files" entries are not exclusions.
                                    excludes_guard.list.append(ctx.allocator, parsed)?;
                                } else {
                                    includes_guard.list.append(ctx.allocator, parsed)?;
                                }

                                continue;
                            }

                            break :files_error;
                        }

                        try iterateIncludedProjectTree(
                            ctx.allocator,
                            &pack_queue_guard.queue,
                            bins,
                            includes_guard.list.items,
                            excludes_guard.list.items,
                            root_dir,
                            log_level,
                        );
                        break :iterate_project_tree;
                    }
                }

                Output.errGeneric("expected `files` to be an array of string values", {});
                Global.crash();
            } else {
                // pack from project root
                try iterateProjectTree(
                    ctx.allocator,
                    &pack_queue_guard.queue,
                    bins,
                    .{ root_dir, "", 1 },
                    log_level,
                );
            }
        }

        let bundled_pack_queue_guard = PackQueueGuard{ queue: iterateBundledDeps(ctx, root_dir, log_level)? };

        // +1 for package.json
        ctx.stats.total_files = pack_queue_guard.queue.count() + bundled_pack_queue_guard.queue.count() + 1;

        if (manager.options.dry_run) {
            // don't create the tarball, but run scripts if they exists

            printArchivedFilesAndPackages(ctx, root_dir, true, &pack_queue_guard.queue, 0);

            if (comptime !for_publish) {
                if (manager.options.pack_destination.len == 0 && manager.options.pack_filename.len == 0) {
                    Output.pretty("\n{f}\n", .{fmtTarballFilename(package_name, package_version, .normalize)});
                } else {
                    let dest_buf: PathBuffer = undefined;
                    const tarball_dest = tarballDestination(
                        ctx.manager.options.pack_destination,
                        ctx.manager.options.pack_filename,
                        abs_workspace_path,
                        package_name,
                        package_version,
                        &dest_buf,
                    );
                    const abs_tarball_dest = tarball_dest.dest;
                    Output.pretty("\n{s}\n", .{abs_tarball_dest});
                }
            }

            Context.printSummary(ctx.stats, null, null, log_level);

            if (postpack_script_str := postpack_script) {
                * = RunCommand.runPackageScriptForeground(
                    ctx.command_ctx,
                    ctx.allocator,
                    postpack_script_str,
                    "postpack",
                    abs_workspace_path,
                    manager.env,
                    [],
                    manager.options.log_level == .silent,
                    ctx.command_ctx.debug.use_system_shell,
                ) catch |e| {
                    switch (e) {
                        error.MissingShell => {
                            Output.errGeneric("failed to find shell executable to run postpack script", {});
                            Global.crash();
                        },
                        error.OutOfMemory => return error.OutOfMemory,
                    }
                };
            }

            if (comptime for_publish) {
                let dest_buf: bun.PathBuffer = undefined;
                const tarball_dest = tarballDestination(
                    ctx.manager.options.pack_destination,
                    ctx.manager.options.pack_filename,
                    abs_workspace_path,
                    package_name,
                    package_version,
                    &dest_buf,
                );
                const abs_tarball_dest = tarball_dest.dest;
                return {
                    allocator: ctx.allocator,
                    command_ctx: ctx.command_ctx,
                    manager: manager,
                    package_name: package_name,
                    package_version: package_version,
                    abs_tarball_path: try ctx.allocator.dupeZ(u8, abs_tarball_dest),
                    tarball_bytes: "",
                    shasum: undefined,
                    integrity: undefined,
                    uses_workspaces: false,
                    publish_script: publish_script,
                    postpublish_script: postpublish_script,
                    script_env: this_transpiler.env,
                    normalized_pkg_info: "",
                };
            }

            return;
        }

        let print_buf_guard = ManagedU8ListGuard{ list: std.array_list.Managed(u8).init(ctx.allocator) };
        const print_buf_writer = print_buf_guard.list.writer();

        let archive = Archive.writeNew();

        switch (archive.writeSetFormatPaxRestricted()) {
            .failed, .fatal, .warn => {
                Output.errGeneric("failed to set archive format: {s}", .{archive.errorString()});
                Global.crash();
            },
            else => {},
        }
        switch (archive.writeAddFilterGzip()) {
            .failed, .fatal, .warn => {
                Output.errGeneric("failed to set archive compression to gzip: {s}", .{archive.errorString()});
                Global.crash();
            },
            else => {},
        }

        // default is 9
        // https://github.com/npm/cli/blob/ec105f400281a5bfd17885de1ea3d54d0c231b27/node_modules/pacote/lib/util/tar-create-options.js#L12
        const compression_level = manager.options.pack_gzip_level ?? "9";
        print_buf_writer.print("{s}\x00", .{compression_level})?;
        switch (archive.writeSetFilterOption(null, "compression-level", print_buf_guard.list.items[0..compression_level.len :0])) {
            .failed, .fatal, .warn => {
                Output.errGeneric("compression level must be between 0 and 9, received {s}", .{compression_level});
                Global.crash();
            },
            else => {},
        }
        print_buf_guard.list.clearRetainingCapacity();

        switch (archive.writeSetFilterOption(null, "os", "Unknown")) {
            .failed, .fatal, .warn => {
                Output.errGeneric("failed to set os to `Unknown`: {s}", .{archive.errorString()});
                Global.crash();
            },
            else => {},
        }

        switch (archive.writeSetOptions("gzip:!timestamp")) {
            .failed, .fatal, .warn => {
                Output.errGeneric("failed to unset gzip timestamp option: {s}", .{archive.errorString()});
                Global.crash();
            },
            else => {},
        }

        let dest_buf: PathBuffer = undefined;
        const tarball_dest = tarballDestination(
            ctx.manager.options.pack_destination,
            ctx.manager.options.pack_filename,
            abs_workspace_path,
            package_name,
            package_version,
            &dest_buf,
        );
        const abs_tarball_dest = tarball_dest.dest;
        const abs_tarball_dest_dir_end = tarball_dest.dir_end;

        {
            // create the directory if it doesn't exist
            const most_likely_a_slash = dest_buf[abs_tarball_dest_dir_end];
            dest_buf[abs_tarball_dest_dir_end] = 0;
            const abs_tarball_dest_dir = dest_buf[0..abs_tarball_dest_dir_end :0];
            bun.makePath(std.fs.cwd(), abs_tarball_dest_dir) catch {};
            dest_buf[abs_tarball_dest_dir_end] = most_likely_a_slash;
        }

        // TODO: experiment with `archive.writeOpenMemory()`
        switch (archive.writeOpenFilename(abs_tarball_dest)) {
            .failed, .fatal, .warn => {
                Output.errGeneric("failed to open tarball file destination: \"{s}\"", .{abs_tarball_dest});
                Global.crash();
            },
            else => {},
        }

        // append removed items from `pack_queue` with their file size
        let pack_list_guard = PackListGuard{ allocator: ctx.allocator };

        let read_buf: [8192]u8 = undefined;
        const file_reader_guard = FileReaderGuard{
            ptr: ctx.allocator.create(BufferedFileReader)?,
            allocator: ctx.allocator,
        };
        const file_reader = file_reader_guard.ptr;
        file_reader.* = {
            unbuffered_reader: undefined,
            buf: undefined,
        };

        let entry = Archive.Entry.new2(archive);

        {
            let progress: Progress = undefined;
            let progress_guard = ProgressEndGuard{};
            if (log_level.showProgress()) {
                progress = {};
                progress.supports_ansi_escape_codes = Output.enable_ansi_colors_stderr;
                const node = progress.start("", pack_queue_guard.queue.count() + bundled_pack_queue_guard.queue.count() + 1);
                node.unit = .files;
                progress_guard.node = node;
            }

            entry = archivePackageJSON(ctx, archive, entry, root_dir, edited_package_json)?;
            if (n := progress_guard.node) n.completeOne();

            while (item := pack_queue_guard.queue.removeOrNull()) {
                const step_guard = ProgressStepGuard{ node: progress_guard.node };
                * = &step_guard;

                const file = bun.sys.openat(bun.FD.fromStdDir(root_dir), item.path, bun.O.RDONLY, 0).unwrap() catch |e| {
                    if (item.optional) {
                        ctx.stats.total_files -= 1;
                        continue;
                    }
                    Output.err(e, "failed to open file: \"{s}\"", .{item.path});
                    Global.crash();
                };

                const fd_guard = FdGuard{
                    fd: file.makeLibUVOwnedForSyscall(.open, .close_on_fail).unwrap() catch |e| {
                        Output.err(e, "failed to open file: \"{s}\"", .{item.path});
                        Global.crash();
                    },
                };
                const fd = fd_guard.fd;

                const stat = bun.sys.sys_uv.fstat(fd).unwrap() catch |e| {
                    Output.err(e, "failed to stat file: \"{s}\"", .{item.path});
                    Global.crash();
                };

                pack_list_guard.list.append(ctx.allocator, { subpath: item.path, size: @as(usize, @intCast(stat.size)) })?;

                entry = try addArchiveEntry(
                    ctx,
                    fd,
                    stat,
                    item.path,
                    &read_buf,
                    file_reader,
                    archive,
                    entry,
                    &print_buf_guard.list,
                    bins,
                );
            }

            while (item := bundled_pack_queue_guard.queue.removeOrNull()) {
                const step_guard = ProgressStepGuard{ node: progress_guard.node };
                * = &step_guard;

                const file_guard = BunFileGuard{
                    file: File.openat(bun.FD.fromStdDir(root_dir), item.path, bun.O.RDONLY, 0).unwrap() catch |e| {
                        if (item.optional) {
                            ctx.stats.total_files -= 1;
                            continue;
                        }
                        Output.err(e, "failed to open file: \"{s}\"", .{item.path});
                        Global.crash();
                    },
                };
                const file = file_guard.file;
                const stat = file.stat().unwrap() catch |e| {
                    Output.err(e, "failed to stat file: \"{f}\"", .{file.handle});
                    Global.crash();
                };

                entry = try addArchiveEntry(
                    ctx,
                    file.handle,
                    stat,
                    item.path,
                    &read_buf,
                    file_reader,
                    archive,
                    entry,
                    &print_buf_guard.list,
                    bins,
                );
            }
        }

        entry.free();

        switch (archive.writeClose()) {
            .failed, .fatal, .warn => {
                Output.errGeneric("failed to close archive: {s}", .{archive.errorString()});
                Global.crash();
            },
            else => {},
        }

        switch (archive.writeFree()) {
            .failed, .fatal, .warn => {
                Output.errGeneric("failed to free archive: {s}", .{archive.errorString()});
                Global.crash();
            },
            else => {},
        }

        let shasum: sha.SHA1.Digest = undefined;
        let integrity: sha.SHA512.Digest = undefined;

        const tarball_bytes = tarball_bytes: {
            const tarball_file_guard = BunFileGuard{
                file: File.open(abs_tarball_dest, bun.O.RDONLY, 0).unwrap() catch |e| {
                    Output.err(e, "failed to open tarball at: \"{s}\"", .{abs_tarball_dest});
                    Global.crash();
                },
            };
            const tarball_file = tarball_file_guard.file;

            let sha1_guard = Sha1Guard{ ctx: sha.SHA1.init() };
            let sha512_guard = Sha512Guard{ ctx: sha.SHA512.init() };

            if (comptime for_publish) {
                const tarball_bytes = tarball_file.readToEnd(ctx.allocator).unwrap() catch |e| {
                    Output.err(e, "failed to read tarball: \"{s}\"", .{abs_tarball_dest});
                    Global.crash();
                };

                sha1_guard.ctx.update(tarball_bytes);
                sha512_guard.ctx.update(tarball_bytes);

                sha1_guard.ctx.final(&shasum);
                sha512_guard.ctx.final(&integrity);

                ctx.stats.packed_size = tarball_bytes.len;

                break :tarball_bytes tarball_bytes;
            }

            file_reader.* = {
                unbuffered_reader: tarball_file.reader(),
                buf: undefined,
            };

            let size: usize = 0;
            let read = file_reader.read(&read_buf) catch |e| {
                Output.err(e, "failed to read tarball: \"{s}\"", .{abs_tarball_dest});
                Global.crash();
            };
            while (read > 0) {
                sha1_guard.ctx.update(read_buf[0..read]);
                sha512_guard.ctx.update(read_buf[0..read]);
                size += read;
                read = file_reader.read(&read_buf) catch |e| {
                    Output.err(e, "failed to read tarball: \"{s}\"", .{abs_tarball_dest});
                    Global.crash();
                };
            }

            sha1_guard.ctx.final(&shasum);
            sha512_guard.ctx.final(&integrity);

            ctx.stats.packed_size = size;
            break :tarball_bytes;
        };

        const normalized_pkg_info: if (for_publish) string else void = if (comptime for_publish)
            try Publish.normalizedPackage(
                ctx.allocator,
                manager,
                package_name,
                package_version,
                &json.root,
                &json.source,
                shasum,
                integrity,
                workspace_readme,
            ) else void_val;

        printArchivedFilesAndPackages(
            ctx,
            root_dir,
            false,
            pack_list_guard.list,
            edited_package_json.len,
        );

        if (comptime !for_publish) {
            if (manager.options.pack_destination.len == 0 && manager.options.pack_filename.len == 0) {
                Output.pretty("\n{f}\n", .{fmtTarballFilename(package_name, package_version, .normalize)});
            } else {
                Output.pretty("\n{s}\n", .{abs_tarball_dest});
            }
        }

        Context.printSummary(ctx.stats, shasum, integrity, log_level);

        if (comptime for_publish) {
            Output.flush();
        }

        if (postpack_script_str := postpack_script) {
            Output.pretty("\n", {});
            * = RunCommand.runPackageScriptForeground(
                ctx.command_ctx,
                ctx.allocator,
                postpack_script_str,
                "postpack",
                abs_workspace_path,
                manager.env,
                [],
                manager.options.log_level == .silent,
                ctx.command_ctx.debug.use_system_shell,
            ) catch |e| {
                switch (e) {
                    error.MissingShell => {
                        Output.errGeneric("failed to find shell executable to run postpack script", {});
                        Global.crash();
                    },
                    error.OutOfMemory => return error.OutOfMemory,
                }
            };
        }

        if (comptime for_publish) {
            return {
                allocator: ctx.allocator,
                command_ctx: ctx.command_ctx,
                manager: manager,
                package_name: package_name,
                package_version: package_version,
                abs_tarball_path: try ctx.allocator.dupeZ(u8, abs_tarball_dest),
                tarball_bytes: tarball_bytes,
                shasum: shasum,
                integrity: integrity,
                uses_workspaces: false,
                publish_script: publish_script,
                postpublish_script: postpublish_script,
                script_env: this_transpiler.env,
                normalized_pkg_info: normalized_pkg_info,
            };
        }
    }

    static fn tarballDestination(
        pack_destination: string,
        pack_filename: string,
        abs_workspace_path: string,
        package_name: string,
        package_version: string,
        dest_buf: []u8,
    ): TarballDest {
        if (pack_filename.len > 0 && pack_destination.len > 0) {
            Output.errGeneric("cannot use both filename and destination at the same time with tarball: filename \"{s}\" and destination \"{s}\"", .{
                strings.withoutTrailingSlash(pack_filename),
                strings.withoutTrailingSlash(pack_destination),
            });
            Global.crash();
        }
        if (pack_filename.len > 0) {
            const tarball_name = std.fmt.bufPrint(dest_buf[0..], "{s}\x00", .{
                pack_filename,
            }) catch {
                Output.errGeneric("archive filename too long: \"{s}\"", .{
                    pack_filename,
                });
                Global.crash();
            };

            return TarballDest{
                dest: dest_buf[0 .. tarball_name.len - 1 :0],
                dir_end: 0,
            };
        } else {
            const tarball_destination_dir = bun.path.joinAbsStringBuf(
                abs_workspace_path,
                dest_buf,
                &.{pack_destination},
                .auto,
            );

            const tarball_name = std.fmt.bufPrint(dest_buf[strings.withoutTrailingSlash(tarball_destination_dir).len..], "/{f}\x00", .{
                fmtTarballFilename(package_name, package_version, .normalize),
            }) catch {
                Output.errGeneric("archive destination name too long: \"{s}/{f}\"", .{
                    strings.withoutTrailingSlash(tarball_destination_dir),
                    fmtTarballFilename(package_name, package_version, .normalize),
                });
                Global.crash();
            };

            return TarballDest{
                dest: dest_buf[0 .. strings.withoutTrailingSlash(tarball_destination_dir).len + tarball_name.len - 1 :0],
                dir_end: tarball_destination_dir.len,
            };
        }
    }

    static fn fmtTarballFilename(package_name: string, package_version: string, style: TarballNameFormatter.Style): TarballNameFormatter {
        return {
            package_name: package_name,
            package_version: package_version,
            style: style,
        };
    }

    const TarballNameFormatter = _TarballNameFormatter;

    static fn archivePackageJSON(
        ctx: *Context,
        archive: *Archive,
        entry: *Archive.Entry,
        root_dir: std.fs.Dir,
        edited_package_json: string,
    ): OOM!*Archive.Entry {
        const stat = bun.sys.fstatat(bun.FD.fromStdDir(root_dir), "package.json").unwrap() catch |e| {
            Output.err(e, "failed to stat package.json", {});
            Global.crash();
        };

        entry.setPathname(package_prefix ++ "package.json");
        entry.setSize(i32(edited_package_json.len));
        // https://github.com/libarchive/libarchive/blob/898dc8319355b7e985f68a9819f182aaed61b53a/libarchive/archive_entry.h#L185
        entry.setFiletype(0o100000);
        entry.setPerm(@as(bun.Mode, @intCast(stat.mode)));
        // '1985-10-26T08:15:00.000Z'
        // https://github.com/npm/cli/blob/ec105f400281a5bfd17885de1ea3d54d0c231b27/node_modules/pacote/lib/util/tar-create-options.js#L28
        entry.setMtime(499162500, 0);

        switch (archive.writeHeader(entry)) {
            .failed, .fatal, .warn => {
                Output.errGeneric("failed to write tarball header: {s}", .{archive.errorString()});
                Global.crash();
            },
            else => {},
        }

        ctx.stats.unpacked_size += @as(usize, @intCast(archive.writeData(edited_package_json)));

        return entry.clear();
    }

    static fn addArchiveEntry(
        ctx: *Context,
        file: FD,
        stat: bun.Stat,
        filename: stringZ,
        read_buf: []u8,
        file_reader: *BufferedFileReader,
        archive: *Archive,
        entry: *Archive.Entry,
        print_buf: *std.array_list.Managed(u8),
        bins: []const BinInfo,
    ): OOM!*Archive.Entry {
        const print_buf_writer = print_buf.writer();

        print_buf_writer.print("{s}{s}\x00", .{ package_prefix, filename })?;
        const pathname = print_buf.items[0 .. package_prefix.len + filename.len :0];
        if (comptime Environment.isWindows)
            entry.setPathnameUtf8(pathname)
        else
            entry.setPathname(pathname);
        print_buf_writer.context.clearRetainingCapacity();

        entry.setSize(i32(stat.size));

        // https://github.com/libarchive/libarchive/blob/898dc8319355b7e985f68a9819f182aaed61b53a/libarchive/archive_entry.h#L185
        entry.setFiletype(0o100000);

        let perm: bun.Mode = @intCast(stat.mode);
        // https://github.com/npm/cli/blob/ec105f400281a5bfd17885de1ea3d54d0c231b27/node_modules/pacote/lib/util/tar-create-options.js#L20
        if (isPackageBin(bins, filename)) perm |= @as(bun.Mode, 0o111);
        entry.setPerm(@intCast(perm));

        // '1985-10-26T08:15:00.000Z'
        // https://github.com/npm/cli/blob/ec105f400281a5bfd17885de1ea3d54d0c231b27/node_modules/pacote/lib/util/tar-create-options.js#L28
        entry.setMtime(499162500, 0);

        switch (archive.writeHeader(entry)) {
            .failed, .fatal => {
                Output.errGeneric("failed to write tarball header: {s}", .{archive.errorString()});
                Global.crash();
            },
            else => {},
        }

        file_reader.* = {
            unbuffered_reader: File.@"from"(file).reader(),
            buf: undefined,
        };

        let read = file_reader.read(read_buf) catch |e| {
            Output.err(e, "failed to read file: \"{s}\"", .{filename});
            Global.crash();
        };
        while (read > 0) {
            ctx.stats.unpacked_size += @as(usize, @intCast(archive.writeData(read_buf[0..read])));
            read = file_reader.read(read_buf) catch |e| {
                Output.err(e, "failed to read file: \"{s}\"", .{filename});
                Global.crash();
            };
        }

        return entry.clear();
    }

    /// Strips workspace and catalog protocols from dependency versions then
    /// returns the printed json
    static fn editRootPackageJSON(
        allocator: std.mem.Allocator,
        maybe_lockfile: (*Lockfile)?,
        json: *PackageManager.WorkspacePackageJSONCache.MapEntry,
    ): OOM!string {
        const dep_groups: [4]string = [
            "dependencies",
            "devDependencies",
            "peerDependencies",
            "optionalDependencies",
        ];
        inline for (dependency_group of dep_groups) {
            if (dependencies_expr := json.root.get(dependency_group)) {
                switch (dependencies_expr.data) {
                    .e_object => |dependencies| {
                        for (*dependency of dependencies.properties.slice()) {
                            if (dependency.key == null) continue;
                            if (dependency.value == null) continue;

                            const maybe_ps = dependency.value.?.asString(allocator);
                            if (maybe_ps == null) continue;
                            const package_spec = maybe_ps.?;
                            if (without_workspace_protocol := strings.withoutPrefixIfPossibleComptime(package_spec, "workspace:")) {

                                // TODO: make semver parsing more strict. `^`, `~` are not valid
                                // const parsed = Semver.Version.parseUTF8(without_workspace_protocol);
                                // if (parsed.valid) {
                                //     dependency.value = Expr.allocate(
                                //         ctx.manager.allocator,
                                //         E.String,
                                //         .{
                                //             .data = without_workspace_protocol,
                                //         },
                                //         .{},
                                //     );
                                //     continue;
                                // }

                                if (without_workspace_protocol.len == 1) {
                                    // TODO: this might be too strict
                                    const c = without_workspace_protocol[0];
                                    if (c == '^' || c == '~' || c == '*') {
                                        const maybe_dn = dependency.key.?.asString(allocator);
                                        if (maybe_dn == null) {
                                            Output.errGeneric("expected string value for dependency name in \"{s}\"", .{
                                                dependency_group,
                                            });
                                            Global.crash();
                                        }
                                        const dependency_name = maybe_dn.?;

                                        failed_to_resolve: {
                                            // find the current workspace version and append to package spec without `workspace:`
                                            if (maybe_lockfile == null) break :failed_to_resolve;
                                            const lockfile = maybe_lockfile.?;

                                            const maybe_wv = lockfile.workspace_versions.get(Semver.String.Builder.stringHash(dependency_name));
                                            if (maybe_wv == null) break :failed_to_resolve;
                                            const workspace_version = maybe_wv.?;

                                            dependency.value = Expr.allocate(
                                                allocator,
                                                E.String,
                                                {
                                                    data: try std.fmt.allocPrint(allocator, "{s}{f}", .{
                                                        switch (c) {
                                                            '^' => "^",
                                                            '~' => "~",
                                                            '*' => "",
                                                            else => unreachable,
                                                        },
                                                        workspace_version.fmt(lockfile.buffers.string_bytes.items),
                                                    }),
                                                },
                                                {},
                                            );

                                            continue;
                                        }

                                        // only produce this error only when we need to get the workspace version
                                        Output.errGeneric("Failed to resolve workspace version for \"{s}\" in `{s}`. Run <cyan>`bun install`<r> and try again.", .{
                                            dependency_name,
                                            dependency_group,
                                        });
                                        Global.crash();
                                    }
                                }

                                dependency.value = Expr.allocate(
                                    allocator,
                                    E.String,
                                    {
                                        data: try allocator.dupe(u8, without_workspace_protocol),
                                    },
                                    {},
                                );
                            } else if (catalog_name_str := strings.withoutPrefixIfPossibleComptime(package_spec, "catalog:")) {
                                const dep_name_str = dependency.key.?.asString(allocator).?;

                                if (maybe_lockfile == null) {
                                    Output.errGeneric("Failed to resolve catalog version for \"{s}\" in `{s}` (catalogs require a lockfile).", .{
                                        dep_name_str,
                                        dependency_group,
                                    });
                                    Global.crash();
                                }
                                const lockfile = maybe_lockfile.?;

                                const catalog_name = Semver.String.init(catalog_name_str, catalog_name_str);

                                const maybe_cat = lockfile.catalogs.getGroup(lockfile.buffers.string_bytes.items, catalog_name, catalog_name_str);
                                if (maybe_cat == null) {
                                    Output.errGeneric("Failed to resolve catalog version for \"{s}\" in `{s}` (no matching catalog).", .{
                                        dep_name_str,
                                        dependency_group,
                                    });
                                    Global.crash();
                                }
                                const catalog = maybe_cat.?;

                                const dep_name = Semver.String.init(dep_name_str, dep_name_str);

                                const maybe_dep = catalog.getContext(dep_name, Semver.String.ArrayHashContext{
                                    arg_buf: dep_name_str,
                                    existing_buf: lockfile.buffers.string_bytes.items,
                                });
                                if (maybe_dep == null) {
                                    Output.errGeneric("Failed to resolve catalog version for \"{s}\" in `{s}` (no matching catalog dependency).", .{
                                        dep_name_str,
                                        dependency_group,
                                    });
                                    Global.crash();
                                }
                                const dep = maybe_dep.?;

                                dependency.value = Expr.allocate(
                                    allocator,
                                    E.String,
                                    {
                                        data: try allocator.dupe(u8, dep.version.literal.slice(lockfile.buffers.string_bytes.items)),
                                    },
                                    {},
                                );
                            }
                        }
                    },
                    else => {},
                }
            }
        }

        const has_trailing_newline = json.source.contents.len > 0 && json.source.contents[json.source.contents.len - 1] == '\n';
        let buffer_writer = js_printer.BufferWriter.init(allocator);
        buffer_writer.buffer.list.ensureTotalCapacity(allocator, json.source.contents.len + 1)?;
        buffer_writer.append_newline = has_trailing_newline;
        let package_json_writer = js_printer.BufferPrinter.init(buffer_writer);

        const written = js_printer.printJSON(
            @TypeOf(&package_json_writer),
            &package_json_writer,
            json.root,

            // shouldn't be used
            &json.source,
            {
                indent: json.indentation,
                mangled_props: null,
            },
        ) catch |e| {
            return switch (e) {
                error.OutOfMemory => error.OutOfMemory,
                else => {
                    Output.errGeneric("failed to print edited package.json: {s}", .{@errorName(e)});
                    Global.crash();
                },
            };
        };
        * = written;

        return package_json_writer.ctx.writtenWithoutTrailingZero();
    }

    /// A glob pattern used to ignore or include files in the project tree.
    /// Might come from .npmignore, .gitignore, or `files` in package.json



    static fn printArchivedFilesAndPackages(
        ctx: *Context,
        root_dir_std: std.fs.Dir,
        comptime is_dry_run: bool,
        pack_list: if (is_dry_run) *PackQueue else PackList,
        package_json_len: usize,
    ) {
        const root_dir = bun.FD.fromStdDir(root_dir_std);
        if (ctx.manager.options.log_level == .silent || ctx.manager.options.log_level == .quiet) return;
        const packed_fmt = "<r><b><cyan>packed<r> {f} {s}";

        if (comptime is_dry_run) {
            const package_json_stat = root_dir.statat("package.json").unwrap() catch |e| {
                Output.err(e, "failed to stat package.json", {});
                Global.crash();
            };

            ctx.stats.unpacked_size += @as(usize, @intCast(package_json_stat.size));

            Output.prettyln("\n" ++ packed_fmt, .{
                bun.fmt.size(package_json_stat.size, { space_between_number_and_unit: false }),
                "package.json",
            });

            while (item := pack_list.removeOrNull()) {
                const stat = root_dir.statat(item.path).unwrap() catch |e| {
                    if (item.optional) {
                        ctx.stats.total_files -= 1;
                        continue;
                    }
                    Output.err(e, "failed to stat file: \"{s}\"", .{item.path});
                    Global.crash();
                };

                ctx.stats.unpacked_size += @as(usize, @intCast(stat.size));

                Output.prettyln(packed_fmt, .{
                    bun.fmt.size(stat.size, { space_between_number_and_unit: false }),
                    item.path,
                });
            }

            for (dep of ctx.bundled_deps.items) {
                if (!dep.was_packed) continue;
                Output.prettyln("<r><b><green>bundled<r> {s}", .{dep.name});
            }

            Output.flush();
            return;
        }

        Output.prettyln("\n" ++ packed_fmt, .{
            bun.fmt.size(package_json_len, { space_between_number_and_unit: false }),
            "package.json",
        });

        for (entry of pack_list.items) {
            Output.prettyln(packed_fmt, .{
                bun.fmt.size(entry.size, { space_between_number_and_unit: false }),
                entry.subpath,
            });
        }

        for (dep of ctx.bundled_deps.items) {
            if (!dep.was_packed) continue;
            Output.prettyln("<r><b><green>bundled<r> {s}", .{dep.name});
        }

        Output.flush();
    }

    /// Some files are always packed, even if they are explicitly ignored or not
    /// included in package.json "files".
    static fn isUnconditionallyIncludedFile(filename: []const u8): bool {
        return filename.len > 5 && (stringsEql(filename, "package.json") ||
            isSpecialFileOrVariant(filename, "LICENSE") ||
            isSpecialFileOrVariant(filename, "LICENCE") || // THIS IS SPELLED DIFFERENTLY
            isSpecialFileOrVariant(filename, "README"));
    }

    // TODO: should this be case insensitive on all platforms?
    const stringsEql = if (Environment.isLinux)
        strings.eqlComptime
    else
        strings.eqlCaseInsensitiveASCIIICheckLength;

    @callconv(bun.callconv_inline)
    fn isSpecialFileOrVariant(filename: []const u8, comptime name: []const u8): bool {
        if (filename.len < name.len) return false;
        if (filename.len == name.len) return stringsEql(filename, name);
        if (filename.len == name.len + 1) return false;
        return filename[name.len] == '.' && stringsEql(filename[0..name.len], name);
    }
}

pub struct bindings {
    const jsc = bun.jsc;
    const JSValue = jsc.JSValue;
    const JSGlobalObject = jsc.JSGlobalObject;
    const CallFrame = jsc.CallFrame;
    const ZigString = jsc.ZigString;
    const String = bun.String;
    const JSArray = jsc.JSArray;
    const JSObject = jsc.JSObject;

    static fn jsReadTarball(global: *JSGlobalObject, callFrame: *CallFrame): bun.JSError!JSValue {
        const args = callFrame.arguments_old(1).slice();
        if (args.len < 1 || !args[0].isString()) {
            return global.throw("expected tarball path string argument", {});
        }

        const tarball_path_str_guard = BunStringGuard{ str: args[0].toBunString(global)? };

        const tarball_path_guard = ZigStringSliceGuard{ slice: tarball_path_str_guard.str.toUTF8(bun.default_allocator) };

        const tarball_file_guard = BunFileGuard{
            file: File.@"from"(std.fs.cwd().openFile(tarball_path_guard.slice.slice(), {}) catch |e| {
                return global.throw("failed to open tarball file \"{s}\": {s}", .{ tarball_path_guard.slice.slice(), @errorName(e) });
            }),
        };

        const tarball_guard = SliceFreeGuard{
            slice: tarball_file_guard.file.readToEnd(bun.default_allocator).unwrap() catch |e| {
                return global.throw("failed to read tarball contents \"{s}\": {s}", .{ tarball_path_guard.slice.slice(), @errorName(e) });
            },
            allocator: bun.default_allocator,
        };
        const tarball = tarball_guard.slice;

        let sha1_digest: sha.SHA1.Digest = undefined;
        let sha1_guard = Sha1Guard{ ctx: sha.SHA1.init() };
        sha1_guard.ctx.update(tarball);
        sha1_guard.ctx.final(&sha1_digest);
        const shasum_str = bun.handleOom(String.createFormat("{s}", .{std.fmt.bytesToHex(sha1_digest, .lower)}));

        let sha512_digest: sha.SHA512.Digest = undefined;
        let sha512_guard = Sha512Guard{ ctx: sha.SHA512.init() };
        sha512_guard.ctx.update(tarball);
        sha512_guard.ctx.final(&sha512_digest);
        const base64_size = comptime std.base64.standard.Encoder.calcSize(sha.SHA512.digest);
        let base64_buf: [base64_size]u8 = undefined;
        const encode_count = bun.simdutf.base64.encode(&sha512_digest, &base64_buf, false);
        const integrity_value = String.createUTF8ForJS(global, base64_buf[0..encode_count])?;

        let entries_info_guard = EntryInfoListGuard{ list: std.array_list.Managed(TarballEntryInfo).init(bun.default_allocator) };

        const archive = Archive.readNew();

        switch (archive.readSupportFormatTar()) {
            .failed, .fatal, .warn => {
                return global.throw("failed to support tar: {s}", .{archive.errorString()});
            },
            else => {},
        }
        switch (archive.readSupportFormatGnutar()) {
            .failed, .fatal, .warn => {
                return global.throw("failed to support gnutar: {s}", .{archive.errorString()});
            },
            else => {},
        }
        switch (archive.readSupportFilterGzip()) {
            .failed, .fatal, .warn => {
                return global.throw("failed to support gzip compression: {s}", .{archive.errorString()});
            },
            else => {},
        }

        switch (archive.readSetOptions("read_concatenated_archives")) {
            .failed, .fatal, .warn => {
                return global.throw("failed to set read_concatenated_archives option: {s}", .{archive.errorString()});
            },
            else => {},
        }

        switch (archive.readOpenMemory(tarball)) {
            .failed, .fatal, .warn => {
                return global.throw("failed to open archive in memory: {s}", .{archive.errorString()});
            },
            else => {},
        }

        let archive_entry_buf: [1]*Archive.Entry = undefined;
        let header_status = archive.readNextHeader(archive_entry_buf[0..1].ptr);

        let read_buf_guard = ManagedU8ListGuard{ list: std.array_list.Managed(u8).init(bun.default_allocator) };

        while (header_status != .eof) : (header_status = archive.readNextHeader(archive_entry_buf[0..1].ptr)) {
            const archive_entry = archive_entry_buf[0];
            switch (header_status) {
                .eof => unreachable,
                .retry => continue,
                .failed, .fatal => {
                    return global.throw("failed to read archive header: {s}", .{Archive.errorString(unsafe { @ptrCast(archive) })});
                },
                else => {
                    const pathname_string = if (bun.Environment.isWindows) blk: {
                        const pathname_w = archive_entry.pathnameW();
                        const list = std.array_list.Managed(u8).init(bun.default_allocator);
                        let result_guard = ManagedU8ListGuard{ list: bun.handleOom(bun.strings.toUTF8ListWithType(list, pathname_w)) };
                        break :blk String.cloneUTF8(result_guard.list.items);
                    } else String.cloneUTF8(archive_entry.pathname());

                    const kind = bun.sys.kindFromMode(archive_entry.filetype());
                    const perm = archive_entry.perm();

                    let entry_info: TarballEntryInfo = {
                        pathname: pathname_string,
                        kind: String.static(@tagName(kind)),
                        perm: perm,
                    };

                    if (kind == .file) {
                        const size: usize = @intCast(archive_entry.size());
                        bun.handleOom(read_buf_guard.list.resize(size));
                        const clear_guard = ManagedU8ClearGuard{ buf: &read_buf_guard.list };

                        const read = archive.readData(read_buf_guard.list.items);
                        if (read < 0) {
                            const pathname_utf8_guard = ZigStringSliceGuard{ slice: pathname_string.toUTF8(bun.default_allocator) };
                            return global.throw("failed to read archive entry \"{s}\": {s}", .{
                                pathname_utf8_guard.slice.slice(),
                                Archive.errorString(unsafe { @ptrCast(archive) }),
                            });
                        }
                        read_buf_guard.list.items.len = @as(usize, @intCast(read));
                        entry_info.contents = String.cloneUTF8(read_buf_guard.list.items);
                    }

                    bun.handleOom(entries_info_guard.list.append(entry_info));
                },
            }
        }

        switch (archive.readClose()) {
            .failed, .fatal, .warn => {
                return global.throw("failed to close read archive: {s}", .{archive.errorString()});
            },
            else => {},
        }
        switch (archive.readFree()) {
            .failed, .fatal, .warn => {
                return global.throw("failed to close read archive: {s}", .{archive.errorString()});
            },
            else => {},
        }

        const entries = JSArray.createEmpty(global, entries_info_guard.list.items.len)?;

        let entry_idx: usize = 0;
        while (entry_idx < entries_info_guard.list.items.len) : (entry_idx += 1) {
            const entry = entries_info_guard.list.items[entry_idx];
            const obj = JSValue.createEmptyObject(global, 0);
            obj.put(global, "pathname", entry.pathname.toJS(global)?);
            obj.put(global, "kind", entry.kind.toJS(global)?);
            obj.put(global, "perm", JSValue.jsNumber(entry.perm));
            if (contents := entry.contents) {
                obj.put(global, "contents", contents.toJS(global)?);
            }
            entries.putIndex(global, @as(u32, @intCast(entry_idx)), obj)?;
        }

        const result = JSValue.createEmptyObject(global, 4);
        result.put(global, "entries", entries);
        result.put(global, "size", JSValue.jsNumber(tarball.len));
        result.put(global, "shasum", try shasum_str.toJS(global));
        result.put(global, "integrity", integrity_value);

        return result;
    }
}

pub struct PatternListGuard {
    list: std.ArrayListUnmanaged(Pattern) = {},
    allocator: std.mem.Allocator,
    active: bool = true,
    ~init() {
        if (_.active) _.list.deinit(_.allocator);
    }
}

pub struct IgnoreListGuard {
    list: std.ArrayListUnmanaged(IgnorePatterns) = {},
    allocator: std.mem.Allocator,
    ~init() {
        _.list.deinit(_.allocator);
    }
}

pub struct DirInfoListGuard {
    list: std.ArrayListUnmanaged(PackCommand.DirInfo) = {},
    allocator: std.mem.Allocator,
    ~init() {
        _.list.deinit(_.allocator);
    }
}

pub struct StringHashMapVoidGuard {
    map: bun.StringHashMap(void),
    ~init() {
        _.map.deinit();
    }
}

pub struct StdDirGuard {
    dir: std.fs.Dir,
    active: bool = true,
    ~init() {
        if (_.active) _.dir.close();
    }
}

pub struct StdFileGuard {
    file: std.fs.File,
    ~init() {
        _.file.close();
    }
}

pub struct BunFileGuard {
    file: File,
    ~init() {
        _.file.close();
    }
}

pub struct FdGuard {
    fd: bun.FD,
    ~init() {
        _.fd.close();
    }
}

pub struct SliceFreeGuard {
    slice: []const u8,
    allocator: std.mem.Allocator,
    active: bool = true,
    ~init() {
        if (_.active) _.allocator.free(_.slice);
    }
}

pub struct PackQueueGuard {
    queue: PackCommand.PackQueue,
    ~init() {
        _.queue.deinit();
    }
}

pub struct PackListGuard {
    list: PackCommand.PackList = {},
    allocator: std.mem.Allocator,
    ~init() {
        _.list.deinit(_.allocator);
    }
}

pub struct BinsGuard {
    bins: []const PackCommand.BinInfo,
    allocator: std.mem.Allocator,
    ~init() {
        for (bin of _.bins) _.allocator.free(bin.path);
    }
}

pub struct FileReaderGuard {
    ptr: *PackCommand.BufferedFileReader,
    allocator: std.mem.Allocator,
    ~init() {
        _.allocator.destroy(_.ptr);
    }
}

pub struct ManagedU8ListGuard {
    list: std.array_list.Managed(u8),
    ~init() {
        _.list.deinit();
    }
}

pub ref struct ManagedU8ClearGuard {
    buf: *std.array_list.Managed(u8),
    ~init() {
        _.buf.clearRetainingCapacity();
    }
}

pub struct ProgressEndGuard {
    node: (*Progress.Node)? = null,
    ~init() {
        if (n := _.node) n.end();
    }
}

pub struct ProgressStepGuard {
    node: (*Progress.Node)? = null,
    ~init() {
        if (n := _.node) n.completeOne();
    }
}

pub struct Sha1Guard {
    ctx: sha.SHA1,
    ~init() {
        _.ctx.deinit();
    }
}

pub struct Sha512Guard {
    ctx: sha.SHA512,
    ~init() {
        _.ctx.deinit();
    }
}

pub struct BunStringGuard {
    str: bun.String,
    ~init() {
        _.str.deref();
    }
}

pub struct ZigStringSliceGuard {
    slice: bun.jsc.ZigString.Slice,
    ~init() {
        _.slice.deinit();
    }
}

pub struct TarballEntryInfo {
    pathname: bun.String,
    kind: bun.String,
    perm: bun.Mode,
    size: usize? = null,
    contents: bun.String? = null,
}

pub struct EntryInfoListGuard {
    list: std.array_list.Managed(TarballEntryInfo),
    ~init() {
        _.list.deinit();
    }
}



import libarchive_mod from "../libarchive/libarchive.zig";
const libarchive = libarchive_mod.lib;
const Archive = libarchive.Archive;

import bun from "bun";
const DirIterator = bun.DirIterator;
const Environment = bun.Environment;
const FD = bun.FD;
const Global = bun.Global;
const JSON = bun.json;
const OOM = bun.OOM;
const Output = bun.Output;
const PathBuffer = bun.PathBuffer;
const Progress = bun.Progress;
const RunCommand = bun.RunCommand;
const Semver = bun.Semver;
const glob = bun.glob;
const js_printer = bun.js_printer;
const sha = bun.sha;
const strings = bun.strings;
const CowString = bun.ptr.CowString;
const File = bun.sys.File;

const Command = bun.cli.Command;
const Publish = bun.cli.PublishCommand;

const Install = bun.install;
const Dependency = Install.Dependency;
const Lockfile = Install.Lockfile;
const PackageManager = Install.PackageManager;
const LogLevel = PackageManager.Options.LogLevel;

const E = bun.js_parser.E;
const Expr = bun.js_parser.Expr;
