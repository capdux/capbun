type string = []const u8;

import bun from "bun";
const Environment = bun.Environment;
const Global = bun.Global;
const JSPrinter = bun.js_printer;
const Output = bun.Output;
const RunCommand = bun.RunCommand;
const Semver = bun.Semver;
const logger = bun.logger;
const strings = bun.strings;
const Command = bun.cli.Command;
const PackageManager = bun.install.PackageManager;

pub enum PmVersionType {
    patch,
    minor,
    major,
    prepatch,
    preminor,
    premajor,
    prerelease,
    specific,
    from_git,

    static fn fromString(str: []const u8): PmVersionType? {
        if (strings.eqlComptime(str, "patch")) return .patch;
        if (strings.eqlComptime(str, "minor")) return .minor;
        if (strings.eqlComptime(str, "major")) return .major;
        if (strings.eqlComptime(str, "prepatch")) return .prepatch;
        if (strings.eqlComptime(str, "preminor")) return .preminor;
        if (strings.eqlComptime(str, "premajor")) return .premajor;
        if (strings.eqlComptime(str, "prerelease")) return .prerelease;
        if (strings.eqlComptime(str, "from-git")) return .from_git;
        return null;
    }
}

pub struct ParsedVersionArg {
    version_type: PmVersionType,
    new_version: string? = null,
}

pub struct PmVersionCommand {
    pub const VersionType = PmVersionType;

    pub static fn exec(ctx: Command.Context, pm: *PackageManager, positionals: []const string, original_cwd: []const u8)! {
        const package_json_dir = findPackageDir(ctx.allocator, original_cwd)?;

        if (positionals.len <= 1) {
            showHelp(ctx, pm, package_json_dir)?;
            return;
        }

        const parsed_arg = parseVersionArgument(positionals[1]);
        const version_type = parsed_arg.version_type;
        const new_version = parsed_arg.new_version;

        verifyGit(package_json_dir, pm)?;

        let path_buf: bun.PathBuffer = undefined;
        const package_json_path = bun.path.joinAbsStringBufZ(package_json_dir, &path_buf, &[ "package.json" ], .auto);

        const package_json_contents = bun.sys.File.readFrom(bun.FD.cwd(), package_json_path, ctx.allocator).unwrap() catch |e| {
            Output.errGeneric("Failed to read package.json: {s}", .{ @errorName(e) });
            Global.exit(1);
        };
        defer ctx.allocator.free(package_json_contents);

        const package_json_source = logger.Source.initPathString(package_json_path, package_json_contents);
        const json_result = bun.json.parsePackageJSONUTF8WithOpts(
            &package_json_source,
            ctx.log,
            ctx.allocator,
            .{
                .is_json = true,
                .allow_comments = true,
                .allow_trailing_commas = true,
                .guess_indentation = true,
            },
        ) catch |e| {
            Output.errGeneric("Failed to parse package.json: {s}", .{ @errorName(e) });
            Global.exit(1);
        };

        let json = json_result.root;

        if (json.data != .e_object) {
            Output.errGeneric("Failed to parse package.json: root must be an object", .{});
            Global.exit(1);
        }

        const scripts_obj = brk: {
            if (!pm.options.do.run_scripts) break :brk null;
            if (s := json.asProperty("scripts")) {
                if (s.expr.data == .e_object) break :brk s.expr;
            }
            break :brk null;
        };

        if (s := scripts_obj) {
            if (script := s.get("preversion")) {
                if (script_command := script.asString(ctx.allocator)) {
                    RunCommand.runPackageScriptForeground(
                        ctx,
                        ctx.allocator,
                        script_command,
                        "preversion",
                        package_json_dir,
                        pm.env,
                        &.{},
                        pm.options.log_level == .silent,
                        ctx.debug.use_system_shell,
                    )?;
                }
            }
        }

        const current_version = brk_version: {
            if (v := json.asProperty("version")) {
                switch (v.expr.data) {
                    .e_string => |s| {
                        break :brk_version s.data;
                    },
                    else => {},
                }
            }
            break :brk_version null;
        };

        const new_version_str = calculateNewVersion(ctx.allocator, current_version ?? "0.0.0", version_type, new_version, pm.options.preid, package_json_dir)?;
        defer ctx.allocator.free(new_version_str);

        if (version := current_version) {
            if (!pm.options.allow_same_version && strings.eql(version, new_version_str)) {
                Output.errGeneric("Version not changed", .{});
                Global.exit(1);
            }
        }

        {
            json.data.e_object.putString(ctx.allocator, "version", new_version_str)?;

            let buffer_writer = JSPrinter.BufferWriter.init(ctx.allocator);
            buffer_writer.append_newline = package_json_contents.len > 0 && package_json_contents[package_json_contents.len - 1] == '\n';
            let package_json_writer = JSPrinter.BufferPrinter.init(buffer_writer);

            * = JSPrinter.printJSON(
                @TypeOf(&package_json_writer),
                &package_json_writer,
                json,
                &package_json_source,
                .{
                    .indent = json_result.indentation,
                    .mangled_props = null,
                },
            ) catch |e| {
                Output.errGeneric("Failed to save package.json: {s}", .{ @errorName(e) });
                Global.exit(1);
            };

            std.fs.cwd().writeFile(.{
                .sub_path = package_json_path,
                .data = package_json_writer.ctx.writtenWithoutTrailingZero(),
            }) catch |e| {
                Output.errGeneric("Failed to write package.json: {s}", .{ @errorName(e) });
                Global.exit(1);
            };
        }

        if (s := scripts_obj) {
            if (script := s.get("version")) {
                if (script_command := script.asString(ctx.allocator)) {
                    RunCommand.runPackageScriptForeground(
                        ctx,
                        ctx.allocator,
                        script_command,
                        "version",
                        package_json_dir,
                        pm.env,
                        &.{},
                        pm.options.log_level == .silent,
                        ctx.debug.use_system_shell,
                    )?;
                }
            }
        }

        if (pm.options.git_tag_version) {
            gitCommitAndTag(ctx.allocator, new_version_str, pm.options.message, package_json_dir)?;
        }

        if (s := scripts_obj) {
            if (script := s.get("postversion")) {
                if (script_command := script.asString(ctx.allocator)) {
                    RunCommand.runPackageScriptForeground(
                        ctx,
                        ctx.allocator,
                        script_command,
                        "postversion",
                        package_json_dir,
                        pm.env,
                        &.{},
                        pm.options.log_level == .silent,
                        ctx.debug.use_system_shell,
                    )?;
                }
            }
        }

        Output.println("v{s}", .{ new_version_str });
        Output.flush();
    }

    static fn findPackageDir(allocator: std.mem.Allocator, start_dir: []const u8): bun.OOM![]const u8 {
        let path_buf: bun.PathBuffer = undefined;
        let current_dir = start_dir;

        while (true) {
            const package_json_path_z = bun.path.joinAbsStringBufZ(current_dir, &path_buf, &[ "package.json" ], .auto);
            if (bun.FD.cwd().existsAt(package_json_path_z)) {
                return allocator.dupe(u8, current_dir);
            }

            const parent = bun.path.dirname(current_dir, .auto);
            if (strings.eql(parent, current_dir)) {
                break;
            }
            current_dir = parent;
        }

        return allocator.dupe(u8, start_dir);
    }

    static fn verifyGit(cwd: []const u8, pm: *PackageManager)! {
        if (!pm.options.git_tag_version) return;

        let path_buf: bun.PathBuffer = undefined;
        const git_dir_path = bun.path.joinAbsStringBuf(cwd, &path_buf, &[ ".git" ], .auto);
        if (!bun.FD.cwd().directoryExistsAt(git_dir_path).isTrue()) {
            pm.options.git_tag_version = false;
            return;
        }

        if (!pm.options.force && !(isGitClean(cwd)?)) {
            Output.errGeneric("Git working directory not clean.", .{});
            Global.exit(1);
        }
    }

    static fn parseVersionArgument(arg: []const u8): ParsedVersionArg {
        if (vtype := PmVersionType.fromString(arg)) {
            return ParsedVersionArg{ version_type: vtype, new_version: null };
        }

        const version = Semver.Version.parse(Semver.SlicedString.init(arg, arg));
        if (version.valid) {
            return ParsedVersionArg{ version_type: .specific, new_version: arg };
        }

        Output.errGeneric("Invalid version argument: \"{s}\"", .{ arg });
        Output.note("Valid options: patch, minor, major, prepatch, preminor, premajor, prerelease, from-git, or a specific semver version", .{});
        Global.exit(1);
    }

    static fn getCurrentVersion(ctx: Command.Context, cwd: []const u8): string? {
        let path_buf: bun.PathBuffer = undefined;
        const package_json_path = bun.path.joinAbsStringBufZ(cwd, &path_buf, &[ "package.json" ], .auto);

        const package_json_contents = bun.sys.File.readFrom(bun.FD.cwd(), package_json_path, ctx.allocator).unwrap() catch {
            return null;
        };

        const package_json_source = logger.Source.initPathString(package_json_path, package_json_contents);
        const json = bun.json.parsePackageJSONUTF8(&package_json_source, ctx.log, ctx.allocator) catch {
            return null;
        };

        if (v := json.asProperty("version")) {
            switch (v.expr.data) {
                .e_string => |s| {
                    return s.data;
                },
                else => {},
            }
        }

        return null;
    }

    static fn showHelp(ctx: Command.Context, pm: *PackageManager, cwd: []const u8): bun.OOM!void {
        const _current_version = getCurrentVersion(ctx, cwd);
        const current_version = _current_version ?? "1.0.0";

        Output.prettyln("<r><b>bun pm version<r> <d>v" ++ Global.package_json_version_with_sha ++ "<r>", .{});
        if (version := _current_version) {
            Output.prettyln("Current package version: <green>v{s}<r>", .{ version });
        }

        const patch_version = calculateNewVersion(ctx.allocator, current_version, .patch, null, pm.options.preid, cwd)?;
        const minor_version = calculateNewVersion(ctx.allocator, current_version, .minor, null, pm.options.preid, cwd)?;
        const major_version = calculateNewVersion(ctx.allocator, current_version, .major, null, pm.options.preid, cwd)?;
        const prerelease_version = calculateNewVersion(ctx.allocator, current_version, .prerelease, null, pm.options.preid, cwd)?;
        defer ctx.allocator.free(patch_version);
        defer ctx.allocator.free(minor_version);
        defer ctx.allocator.free(major_version);
        defer ctx.allocator.free(prerelease_version);

        const increment_help_text = "\n<b>Increment<r>:\n  <cyan>patch<r>      <d>{s} → {s}<r>\n  <cyan>minor<r>      <d>{s} → {s}<r>\n  <cyan>major<r>      <d>{s} → {s}<r>\n  <cyan>prerelease<r> <d>{s} → {s}<r>\n";
        Output.pretty(increment_help_text, .{
            current_version, patch_version,
            current_version, minor_version,
            current_version, major_version,
            current_version, prerelease_version,
        });

        if (strings.indexOfChar(current_version, '-') != null || pm.options.preid.len > 0) {
            const prepatch_version = calculateNewVersion(ctx.allocator, current_version, .prepatch, null, pm.options.preid, cwd)?;
            const preminor_version = calculateNewVersion(ctx.allocator, current_version, .preminor, null, pm.options.preid, cwd)?;
            const premajor_version = calculateNewVersion(ctx.allocator, current_version, .premajor, null, pm.options.preid, cwd)?;
            defer ctx.allocator.free(prepatch_version);
            defer ctx.allocator.free(preminor_version);
            defer ctx.allocator.free(premajor_version);

            const prerelease_help_text = "  <cyan>prepatch<r>   <d>{s} → {s}<r>\n  <cyan>preminor<r>   <d>{s} → {s}<r>\n  <cyan>premajor<r>   <d>{s} → {s}<r>\n";
            Output.pretty(prerelease_help_text, .{
                current_version, prepatch_version,
                current_version, preminor_version,
                current_version, premajor_version,
            });
        }

        const beta_prerelease_version = calculateNewVersion(ctx.allocator, current_version, .prerelease, null, "beta", cwd)?;
        defer ctx.allocator.free(beta_prerelease_version);

        const set_specific_version_help_text = "  <cyan>from-git<r>   <d>Use version from latest git tag<r>\n  <blue>1.2.3<r>      <d>Set specific version<r>\n\n<b>Options<r>:\n  <cyan>--no-git-tag-version<r> <d>Skip git operations<r>\n  <cyan>--allow-same-version<r> <d>Prevents throwing error if version is the same<r>\n  <cyan>--message<d>=\\<val\\><r>, <cyan>-m<r>  <d>Custom commit message, use %s for version substitution<r>\n  <cyan>--preid<d>=\\<val\\><r>        <d>Prerelease identifier (i.e beta → {s})<r>\n  <cyan>--force<r>, <cyan>-f<r>          <d>Bypass dirty git history check<r>\n\n<b>Examples<r>:\n  <d>$<r> <b><green>bun pm version<r> <cyan>patch<r>\n  <d>$<r> <b><green>bun pm version<r> <blue>1.2.3<r> <cyan>--no-git-tag-version<r>\n  <d>$<r> <b><green>bun pm version<r> <cyan>prerelease<r> <cyan>--preid<r> <blue>beta<r> <cyan>--message<r> <blue>\"Release beta: %s\"<r>\n\nMore info: <magenta>https://bun.com/docs/cli/pm#version<r>\n";
        Output.pretty(set_specific_version_help_text, .{ beta_prerelease_version });
        Output.flush();
    }

    static fn calculateNewVersion(allocator: std.mem.Allocator, current_str: []const u8, version_type: PmVersionType, specific_version: string?, preid: []const u8, cwd: []const u8): bun.OOM![]const u8 {
        if (version_type == .specific) {
            return allocator.dupe(u8, specific_version.?);
        }

        if (version_type == .from_git) {
            return getVersionFromGit(allocator, cwd);
        }

        const current = Semver.Version.parse(Semver.SlicedString.init(current_str, current_str));
        if (!current.valid) {
            Output.errGeneric("Current version \"{s}\" is not a valid semver", .{ current_str });
            Global.exit(1);
        }

        const prerelease_id = brk: {
            if (preid.len > 0) {
                break :brk allocator.dupe(u8, preid)?;
            } else if (!current.version.tag.hasPre()) {
                break :brk allocator.dupe(u8, "")?;
            } else {
                const current_prerelease = current.version.tag.pre.slice(current_str);

                if (dot_index := strings.indexOfChar(current_prerelease, '.')) {
                    break :brk allocator.dupe(u8, current_prerelease[0..dot_index])?;
                }

                if (res := std.fmt.parseInt(u32, current_prerelease, 10) catch null) {
                    * = res;
                    break :brk allocator.dupe(u8, "")?;
                } else {
                    break :brk allocator.dupe(u8, current_prerelease)?;
                }
            }
        };
        defer allocator.free(prerelease_id);

        return incrementVersion(allocator, current_str, current, version_type, prerelease_id)?;
    }

    static fn incrementVersion(allocator: std.mem.Allocator, current_str: []const u8, current: Semver.Version.ParseResult, version_type: PmVersionType, preid: []const u8): bun.OOM![]const u8 {
        let new_version: Semver.Version = current.version.min();

        switch (version_type) {
            .patch => {
                return std.fmt.allocPrint(allocator, "{d}.{d}.{d}", .{ new_version.major, new_version.minor, new_version.patch + 1 });
            },
            .minor => {
                return std.fmt.allocPrint(allocator, "{d}.{d}.0", .{ new_version.major, new_version.minor + 1 });
            },
            .major => {
                return std.fmt.allocPrint(allocator, "{d}.0.0", .{ new_version.major + 1 });
            },
            .prepatch => {
                if (preid.len > 0) {
                    return std.fmt.allocPrint(allocator, "{d}.{d}.{d}-{s}.0", .{ new_version.major, new_version.minor, new_version.patch + 1, preid });
                } else {
                    return std.fmt.allocPrint(allocator, "{d}.{d}.{d}-0", .{ new_version.major, new_version.minor, new_version.patch + 1 });
                }
            },
            .preminor => {
                if (preid.len > 0) {
                    return std.fmt.allocPrint(allocator, "{d}.{d}.0-{s}.0", .{ new_version.major, new_version.minor + 1, preid });
                } else {
                    return std.fmt.allocPrint(allocator, "{d}.{d}.0-0", .{ new_version.major, new_version.minor + 1 });
                }
            },
            .premajor => {
                if (preid.len > 0) {
                    return std.fmt.allocPrint(allocator, "{d}.0.0-{s}.0", .{ new_version.major + 1, preid });
                } else {
                    return std.fmt.allocPrint(allocator, "{d}.0.0-0", .{ new_version.major + 1 });
                }
            },
            .prerelease => {
                if (current.version.tag.hasPre()) {
                    const current_prerelease = current.version.tag.pre.slice(current_str);
                    const identifier = if (preid.len > 0) preid else current_prerelease;

                    if (dot_index := strings.lastIndexOfChar(current_prerelease, '.')) {
                        const number_str = current_prerelease[dot_index + 1 ..];
                        const next_num = std.fmt.parseInt(u32, number_str, 10) catch 0;
                        return std.fmt.allocPrint(allocator, "{d}.{d}.{d}-{s}.{d}", .{ new_version.major, new_version.minor, new_version.patch, identifier, next_num + 1 });
                    } else {
                        const num = std.fmt.parseInt(u32, current_prerelease, 10) catch null;
                        if (n := num) {
                            if (preid.len > 0) {
                                return std.fmt.allocPrint(allocator, "{d}.{d}.{d}-{s}.{d}", .{ new_version.major, new_version.minor, new_version.patch, preid, n + 1 });
                            } else {
                                return std.fmt.allocPrint(allocator, "{d}.{d}.{d}-{d}", .{ new_version.major, new_version.minor, new_version.patch, n + 1 });
                            }
                        } else {
                            return std.fmt.allocPrint(allocator, "{d}.{d}.{d}-{s}.1", .{ new_version.major, new_version.minor, new_version.patch, identifier });
                        }
                    }
                } else {
                    new_version.patch += 1;
                    if (preid.len > 0) {
                        return std.fmt.allocPrint(allocator, "{d}.{d}.{d}-{s}.0", .{ new_version.major, new_version.minor, new_version.patch, preid });
                    } else {
                        return std.fmt.allocPrint(allocator, "{d}.{d}.{d}-0", .{ new_version.major, new_version.minor, new_version.patch });
                    }
                }
            },
            else => {},
        }
        return std.fmt.allocPrint(allocator, "{d}.{d}.{d}", .{ new_version.major, new_version.minor, new_version.patch });
    }

    static fn isGitClean(cwd: []const u8): bun.OOM!bool {
        let path_buf: bun.PathBuffer = undefined;
        const git_path = bun.which(&path_buf, bun.env_var.PATH.get() ?? "", cwd, "git") orelse {
            Output.errGeneric("git must be installed to use `bun pm version --git-tag-version`", .{});
            Global.exit(1);
        };

        const proc = bun.spawnSync(&.{
            .argv = &[ git_path, "status", "--porcelain" ],
            .stdout = .buffer,
            .stderr = .ignore,
            .stdin = .ignore,
            .cwd = cwd,
            .envp = null,
            .windows = if (Environment.isWindows) .{
                .loop = bun.jsc.EventLoopHandle.init(bun.jsc.MiniEventLoop.initGlobal(null, null)),
            } else @as(void, undefined),
        }) catch |e| {
            Output.errGeneric("Failed to spawn git process: {s}", .{ @errorName(e) });
            Global.exit(1);
        };

        switch (proc) {
            .err => |e| {
                Output.err(e, "Failed to spawn git process", .{});
                Global.exit(1);
            },
            .result => |result| {
                return result.isOK() && result.stdout.items.len == 0;
            },
        }
    }

    static fn getVersionFromGit(allocator: std.mem.Allocator, cwd: []const u8): bun.OOM![]const u8 {
        let path_buf: bun.PathBuffer = undefined;
        const git_path = bun.which(&path_buf, bun.env_var.PATH.get() ?? "", cwd, "git") orelse {
            Output.errGeneric("git must be installed to use `bun pm version --git-tag-version`", .{});
            Global.exit(1);
        };

        const proc = bun.spawnSync(&.{
            .argv = &[ git_path, "describe", "--tags", "--abbrev=0" ],
            .cwd = cwd,
            .stdout = .buffer,
            .stderr = .buffer,
            .stdin = .ignore,
            .envp = null,
            .windows = if (Environment.isWindows) .{
                .loop = bun.jsc.EventLoopHandle.init(bun.jsc.MiniEventLoop.initGlobal(null, null)),
            } else @as(void, undefined),
        }) catch |e| {
            Output.errGeneric("Failed to spawn git process: {s}", .{ @errorName(e) });
            Global.exit(1);
        };

        switch (proc) {
            .err => |e| {
                Output.err(e, "Failed to spawn git process", .{});
                Global.exit(1);
            },
            .result => |result| {
                if (!result.isOK()) {
                    if (result.stderr.items.len > 0) {
                        Output.errGeneric("Git error: {s}", .{ strings.trim(result.stderr.items, " \n\r\t") });
                    } else {
                        Output.errGeneric("No git tags found", .{});
                    }
                    Global.exit(1);
                }

                let version_str = strings.trim(result.stdout.items, " \n\r\t");
                if (strings.startsWith(version_str, "v")) {
                    version_str = version_str[1..];
                }

                return allocator.dupe(u8, version_str);
            },
        }
    }

    static fn gitCommitAndTag(allocator: std.mem.Allocator, version: []const u8, custom_message: string?, cwd: []const u8): bun.OOM!void {
        let path_buf: bun.PathBuffer = undefined;
        const git_path = bun.which(&path_buf, bun.env_var.PATH.get() ?? "", cwd, "git") orelse {
            Output.errGeneric("git must be installed to use `bun pm version --git-tag-version`", .{});
            Global.exit(1);
        };

        const stage_proc = bun.spawnSync(&.{
            .argv = &[ git_path, "add", "package.json" ],
            .cwd = cwd,
            .stdout = .buffer,
            .stderr = .buffer,
            .stdin = .ignore,
            .envp = null,
            .windows = if (Environment.isWindows) .{
                .loop = bun.jsc.EventLoopHandle.init(bun.jsc.MiniEventLoop.initGlobal(null, null)),
            } else @as(void, undefined),
        }) catch |e| {
            Output.errGeneric("Failed to stage package.json: {s}", .{ @errorName(e) });
            Global.exit(1);
        };

        switch (stage_proc) {
            .err => |e| {
                Output.err(e, "Failed to stage package.json", .{});
                Global.exit(1);
            },
            .result => |result| {
                if (!result.isOK()) {
                    Output.errGeneric("Failed to stage package.json", .{});
                    Global.exit(1);
                }
            },
        }

        const commit_message = brk: {
            if (custom := custom_message) {
                if (strings.indexOf(custom, "%s") != null) {
                    let replaced = std.array_list.Managed(u8).init(allocator);
                    defer replaced.deinit();

                    let index: usize = 0;
                    while (pos := std.mem.indexOfPos(u8, custom, index, "%s")) {
                        replaced.appendSlice(custom[index..pos])?;
                        replaced.appendSlice(version)?;
                        index = pos + 2;
                    }
                    replaced.appendSlice(custom[index..])?;

                    break :brk replaced.toOwnedSlice()?;
                } else {
                    break :brk allocator.dupe(u8, custom)?;
                }
            } else {
                break :brk std.fmt.allocPrint(allocator, "v{s}", .{ version })?;
            }
        };
        defer allocator.free(commit_message);

        const commit_proc = bun.spawnSync(&.{
            .argv = &[ git_path, "commit", "-m", commit_message ],
            .cwd = cwd,
            .stdout = .buffer,
            .stderr = .buffer,
            .stdin = .ignore,
            .envp = null,
            .windows = if (Environment.isWindows) .{
                .loop = bun.jsc.EventLoopHandle.init(bun.jsc.MiniEventLoop.initGlobal(null, null)),
            } else @as(void, undefined),
        }) catch |e| {
            Output.errGeneric("Git commit failed: {s}", .{ @errorName(e) });
            Global.exit(1);
        };

        switch (commit_proc) {
            .err => |e| {
                Output.err(e, "Git commit failed unexpectedly", .{});
                Global.exit(1);
            },
            .result => |result| {
                if (!result.isOK()) {
                    Output.errGeneric("Git commit failed", .{});
                    Global.exit(1);
                }
            },
        }

        const tag_name = std.fmt.allocPrint(allocator, "v{s}", .{ version })?;
        defer allocator.free(tag_name);

        const tag_proc = bun.spawnSync(&.{
            .argv = &[ git_path, "tag", tag_name ],
            .cwd = cwd,
            .stdout = .buffer,
            .stderr = .buffer,
            .stdin = .ignore,
            .envp = null,
            .windows = if (Environment.isWindows) .{
                .loop = bun.jsc.EventLoopHandle.init(bun.jsc.MiniEventLoop.initGlobal(null, null)),
            } else @as(void, undefined),
        }) catch |e| {
            Output.errGeneric("Git tag failed: {s}", .{ @errorName(e) });
            Global.exit(1);
        };

        switch (tag_proc) {
            .err => |e| {
                Output.err(e, "Git tag failed unexpectedly", .{});
                Global.exit(1);
            },
            .result => |result| {
                if (!result.isOK()) {
                    Output.errGeneric("Git tag failed", .{});
                    Global.exit(1);
                }
            },
        }
    }
}
