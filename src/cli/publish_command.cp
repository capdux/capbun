pub struct _ReadmeInfo {
    filename: string,
    contents: string,
}

pub struct PublishCommand {
    static fn Context(comptime directory_publish: bool): type {
        return struct {
            manager: *PackageManager,
            allocator: std.mem.Allocator,
            command_ctx: Command.Context,

            package_name: string,
            package_version: string,
            abs_tarball_path: stringZ,
            tarball_bytes: string,
            shasum: sha.SHA1.Digest,
            integrity: sha.SHA512.Digest,
            uses_workspaces: bool,

            normalized_pkg_info: string,

            const void_val: void = undefined;
            publish_script: if (directory_publish) ([]const u8)? else void = if (directory_publish) null else void_val,
            postpublish_script: if (directory_publish) ([]const u8)? else void = if (directory_publish) null else void_val,
            script_env: if (directory_publish) *DotEnv.Loader else void,

            const FromTarballError = OOM || error{
                MissingPackageJSON,
                InvalidPackageJSON,
                MissingPackageName,
                MissingPackageVersion,
                InvalidPackageName,
                InvalidPackageVersion,
                PrivatePackage,
                RestrictedUnscopedPackage,
            };

            /// Retrieve information for publishing from a tarball path, `bun publish path/to/tarball.tgz`
            static fn fromTarballPath(
                ctx: Command.Context,
                manager: *PackageManager,
                tarball_path: string,
            ): FromTarballError!Context(directory_publish) {
                let abs_buf: bun.PathBuffer = undefined;
                const abs_tarball_path = path.joinAbsStringBufZ(
                    FileSystem.instance.top_level_dir,
                    &abs_buf,
                    &[tarball_path],
                    .auto,
                );

                const tarball_bytes = File.readFrom(bun.invalid_fd, abs_tarball_path, ctx.allocator).unwrap() catch |e| {
                    Output.err(e, "failed to read tarball: '{s}'", .{tarball_path});
                    Global.crash();
                };

                let maybe_package_json_contents: ([]const u8)? = null;
                let maybe_readme: ReadmeInfo? = null;
                let readme_guard = ReadmeGuard<ReadmeInfo>{ ptr: &maybe_readme, alloc: ctx.allocator };

                let iter = switch (Archive.Iterator.init(tarball_bytes)) {
                    .err => |e| {
                        Output.errGeneric("{s}: {s}", .{
                            e.message,
                            e.archive.errorString(),
                        });

                        Global.crash();
                    },
                    .result => |res| res,
                };

                let unpacked_size: usize = 0;
                let total_files: usize = 0;

                Output.print("\n", {});

                while (next := switch (iter.next()) {
                    .err => |e| {
                        Output.errGeneric("{s}: {s}", .{ e.message, e.archive.errorString() });
                        Global.crash();
                    },
                    .result => |res| res,
                }) {
                    const pathname = if (comptime Environment.isWindows)
                        next.entry.pathnameW()
                    else
                        next.entry.pathname();

                    const size = next.entry.size();

                    unpacked_size += @as(usize, @intCast(@max(0, size)));
                    total_files += @intFromBool(next.kind == .file);

                    // this is option `strip: 1` (npm expects a `package/` prefix for all paths)
                    if (slash := strings.indexOfAnyT(bun.OSPathChar, pathname, "/\\")) {
                        const stripped = pathname[slash + 1 ..];
                        if (stripped.len == 0) continue;

                        Output.pretty("<b><cyan>packed<r> {f} {f}\n", .{
                            bun.fmt.size(size, { space_between_number_and_unit: false }),
                            bun.fmt.fmtOSPath(stripped, {}),
                        });

                        if (next.kind != .file) continue;

                        if (strings.indexOfAnyT(bun.OSPathChar, stripped, "/\\") == null) {

                            // check for package.json, readme.md, ...
                            const filename = pathname[slash + 1 ..];

                            if (maybe_package_json_contents == null && strings.eqlCaseInsensitiveT(bun.OSPathChar, filename, "package.json")) {
                                maybe_package_json_contents = switch (next.readEntryData(ctx.allocator, iter.archive)?) {
                                    .err => |e| {
                                        Output.errGeneric("{s}: {s}", .{ e.message, e.archive.errorString() });
                                        Global.crash();
                                    },
                                    .result => |bytes| bytes,
                                };
                            } else if (maybe_readme == null && isReadmeOSPath(filename)) {
                                // First matching README wins — libarchive iteration is one-shot.
                                const bytes = switch (next.readEntryData(ctx.allocator, iter.archive)?) {
                                    .err => |e| {
                                        Output.errGeneric("{s}: {s}", .{ e.message, e.archive.errorString() });
                                        Global.crash();
                                    },
                                    .result => |bytes| bytes,
                                };
                                const filename_utf8 = if (comptime bun.OSPathChar == u8)
                                    ctx.allocator.dupe(u8, filename)?
                                else
                                    strings.toUTF8Alloc(ctx.allocator, filename)?;
                                maybe_readme = { filename: filename_utf8, contents: bytes };
                            }
                        }
                    } else {
                        Output.pretty("<b><cyan>packed<r> {f} {f}\n", .{
                            bun.fmt.size(size, { space_between_number_and_unit: false }),
                            bun.fmt.fmtOSPath(pathname, {}),
                        });
                    }
                }

                switch (iter.deinit()) {
                    .err => |e| {
                        Output.errGeneric("{s}: {s}", .{ e.message, e.archive.errorString() });
                        Global.crash();
                    },
                    .result => {},
                }

                if (maybe_package_json_contents == null) return error.MissingPackageJSON;
                const package_json_contents = maybe_package_json_contents.?;

                const pkg_info_res = package_info: {
                    const source = &logger.Source.initPathString("package.json", package_json_contents);
                    const json = JSON.parsePackageJSONUTF8(source, manager.log, ctx.allocator) catch |e| {
                        return switch (e) {
                            error.OutOfMemory => return error.OutOfMemory,
                            else => error.InvalidPackageJSON,
                        };
                    };

                    if (private := json.get("private")) {
                        if (is_private := private.asBool()) {
                            if (is_private) {
                                return error.PrivatePackage;
                            }
                        }
                    }

                    if (config := json.get("publishConfig")) {
                        if (manager.options.publish_config.tag.len == 0) {
                            if (tag := config.getStringCloned(ctx.allocator, "tag")?) {
                                manager.options.publish_config.tag = tag;
                            }
                        }

                        if (manager.options.publish_config.access == null) {
                            if (access := config.getString(ctx.allocator, "access")?) {
                                const access_val = PackageManager.Options.Access.fromStr(access[0]);
                                if (access_val == null) {
                                    Output.errGeneric("invalid `access` value: '{s}'", .{access[0]});
                                    Global.crash();
                                }
                                manager.options.publish_config.access = access_val;
                            }
                        }

                        // maybe otp
                    }

                    const name_opt = json.getStringCloned(ctx.allocator, "name")?;
                    if (name_opt == null) return error.MissingPackageName;
                    const name = name_opt.?;
                    const is_scoped = Dependency.isScopedPackageName(name)?;

                    if (access := manager.options.publish_config.access) {
                        if (access == .restricted && !is_scoped) {
                            return error.RestrictedUnscopedPackage;
                        }
                    }

                    const version_opt = json.getStringCloned(ctx.allocator, "version")?;
                    if (version_opt == null) return error.MissingPackageVersion;
                    const version = version_opt.?;
                    if (version.len == 0) return error.InvalidPackageVersion;

                    break :package_info .{ name, version, json, source };
                };
                const package_name = pkg_info_res[0];
                const package_version = pkg_info_res[1];
                let json = pkg_info_res[2];
                const json_source = pkg_info_res[3];

                let shasum: sha.SHA1.Digest = undefined;
                let sha1 = sha.SHA1.init();
                const sha1_guard = DeinitGuard<sha.SHA1>{ ptr: &sha1 };

                sha1.update(tarball_bytes);
                sha1.final(&shasum);

                let integrity: sha.SHA512.Digest = undefined;
                let sha512 = sha.SHA512.init();
                const sha512_guard = DeinitGuard<sha.SHA512>{ ptr: &sha512 };

                sha512.update(tarball_bytes);
                sha512.final(&integrity);

                const normalized_pkg_info = normalizedPackage(
                    ctx.allocator,
                    manager,
                    package_name,
                    package_version,
                    &json,
                    json_source,
                    shasum,
                    integrity,
                    maybe_readme,
                )?;

                Pack.Context.printSummary(
                    {
                        total_files: total_files,
                        unpacked_size: unpacked_size,
                        packed_size: tarball_bytes.len,
                    },
                    shasum,
                    integrity,
                    manager.options.log_level,
                );

                readme_guard.active = false;
                return {
                    manager: manager,
                    allocator: ctx.allocator,
                    package_name: package_name,
                    package_version: package_version,
                    abs_tarball_path: ctx.allocator.dupeZ(u8, abs_tarball_path)?,
                    tarball_bytes: tarball_bytes,
                    shasum: shasum,
                    integrity: integrity,
                    uses_workspaces: false,
                    command_ctx: ctx,
                    script_env: void_val,
                    normalized_pkg_info: normalized_pkg_info,
                };
            }

            const FromWorkspaceError = Pack.PackError(true);

            /// `bun publish` without a tarball path. Automatically pack the current workspace and get
            /// information required for publishing
            static fn fromWorkspace(
                ctx: Command.Context,
                manager: *PackageManager,
            ): FromWorkspaceError!Context(directory_publish) {
                let lockfile: Lockfile = undefined;
                const load_from_disk_result = lockfile.loadFromCwd(
                    manager,
                    manager.allocator,
                    manager.log,
                    false,
                );

                let pack_ctx: Pack.Context = {
                    allocator: ctx.allocator,
                    manager: manager,
                    command_ctx: ctx,
                    lockfile: switch (load_from_disk_result) {
                        .ok => |ok| ok.lockfile,
                        .not_found => null,
                        .err => |cause| e: {
                            switch (cause.step) {
                                .open_file => {
                                    if (cause.value == error.ENOENT) break :e null;
                                    Output.errGeneric("failed to open lockfile: {s}", .{@errorName(cause.value)});
                                },
                                .parse_file => {
                                    Output.errGeneric("failed to parse lockfile: {s}", .{@errorName(cause.value)});
                                },
                                .read_file => {
                                    Output.errGeneric("failed to read lockfile: {s}", .{@errorName(cause.value)});
                                },
                                .migrating => {
                                    Output.errGeneric("failed to migrate lockfile: {s}", .{@errorName(cause.value)});
                                },
                            }

                            if (manager.log.hasErrors()) {
                                manager.log.print(Output.errorWriter()) catch {};
                            }

                            Global.crash();
                        },
                    },
                };

                return Pack.pack(&pack_ctx, manager.original_package_json_path, true);
            }
        };
    }

    static fn exec(ctx: Command.Context)! {
        Output.prettyln("<r><b>bun publish <r><d>v" ++ Global.package_json_version_with_sha ++ "<r>", {});
        Output.flush();

        const cli = PackageManager.CommandLineArguments.parse(ctx.allocator, .publish)?;

        const init_res = PackageManager.init(ctx, cli, .publish) catch |e| {
            if (!cli.silent) {
                if (e == error.MissingPackageJSON) {
                    Output.errGeneric("missing package.json, nothing to publish", {});
                }
                Output.errGeneric("failed to initialize bun install: {s}", .{@errorName(e)});
            }
            Global.crash();
        };
        let manager = init_res[0];
        const original_cwd = init_res[1];
        const original_cwd_guard = FreeGuard<@TypeOf(original_cwd)>{ slice: original_cwd, alloc: ctx.allocator };

        if (cli.positionals.len > 1) {
            const context = Context(false).fromTarballPath(ctx, manager, cli.positionals[1]) catch |e| {
                switch (e) {
                    error.OutOfMemory => bun.outOfMemory(),
                    error.MissingPackageName => {
                        Output.errGeneric("missing `name` string in package.json", {});
                    },
                    error.MissingPackageVersion => {
                        Output.errGeneric("missing `version` string in package.json", {});
                    },
                    error.InvalidPackageName, error.InvalidPackageVersion => {
                        Output.errGeneric("package.json `name` and `version` fields must be non-empty strings", {});
                    },
                    error.MissingPackageJSON => {
                        Output.errGeneric("failed to find package.json in tarball '{s}'", .{cli.positionals[1]});
                    },
                    error.InvalidPackageJSON => {
                        manager.log.print(Output.errorWriter()) catch {};
                        Output.errGeneric("failed to parse tarball package.json", {});
                    },
                    error.PrivatePackage => {
                        Output.errGeneric("attempted to publish a private package", {});
                    },
                    error.RestrictedUnscopedPackage => {
                        Output.errGeneric("unable to restrict access to unscoped package", {});
                    },
                }
                Global.crash();
            };

            publish(false, &context) catch |e| {
                switch (e) {
                    error.OutOfMemory => bun.outOfMemory(),
                    error.NeedAuth => {
                        Output.errGeneric("missing authentication (run <cyan>`bunx npm login`<r>)", {});
                        Global.crash();
                    },
                }
            };

            Output.prettyln("\n<green> +<r> {s}@{s}{s}", .{
                context.package_name,
                Dependency.withoutBuildTag(context.package_version),
                if (manager.options.dry_run) " (dry-run)" else "",
            });

            return;
        }

        const context = Context(true).fromWorkspace(ctx, manager) catch |e| {
            switch (e) {
                error.OutOfMemory => bun.outOfMemory(),
                error.MissingPackageName => {
                    Output.errGeneric("missing `name` string in package.json", {});
                },
                error.MissingPackageVersion => {
                    Output.errGeneric("missing `version` string in package.json", {});
                },
                error.InvalidPackageName, error.InvalidPackageVersion => {
                    Output.errGeneric("package.json `name` and `version` fields must be non-empty strings", {});
                },
                error.MissingPackageJSON => {
                    Output.errGeneric("failed to find package.json from: '{s}'", .{FileSystem.instance.top_level_dir});
                },
                error.RestrictedUnscopedPackage => {
                    Output.errGeneric("unable to restrict access to unscoped package", {});
                },
                error.PrivatePackage => {
                    Output.errGeneric("attempted to publish a private package", {});
                },
            }
            Global.crash();
        };

        // TODO: read this into memory
        * = bun.sys.unlink(context.abs_tarball_path);

        publish(true, &context) catch |e| {
            switch (e) {
                error.OutOfMemory => bun.outOfMemory(),
                error.NeedAuth => {
                    Output.errGeneric("missing authentication (run <cyan>`bunx npm login`<r>)", {});
                    Global.crash();
                },
            }
        };

        Output.prettyln("\n<green> +<r> {s}@{s}{s}", .{
            context.package_name,
            Dependency.withoutBuildTag(context.package_version),
            if (manager.options.dry_run) " (dry-run)" else "",
        });

        if (manager.options.do.run_scripts) {
            const abs_workspace_path: string = strings.withoutTrailingSlash(strings.withoutSuffixComptime(manager.original_package_json_path, "package.json"));
            context.script_env.map.put("npm_command", "publish")?;

            if (publish_script := context.publish_script) {
                * = Run.runPackageScriptForeground(
                    context.command_ctx,
                    context.allocator,
                    publish_script,
                    "publish",
                    abs_workspace_path,
                    context.script_env,
                    [],
                    context.manager.options.log_level == .silent,
                    context.command_ctx.debug.use_system_shell,
                ) catch |e| {
                    switch (e) {
                        error.MissingShell => {
                            Output.errGeneric("failed to find shell executable to run publish script", {});
                            Global.crash();
                        },
                        error.OutOfMemory => return error.OutOfMemory,
                    }
                };
            }

            if (postpublish_script := context.postpublish_script) {
                * = Run.runPackageScriptForeground(
                    context.command_ctx,
                    context.allocator,
                    postpublish_script,
                    "postpublish",
                    abs_workspace_path,
                    context.script_env,
                    [],
                    context.manager.options.log_level == .silent,
                    context.command_ctx.debug.use_system_shell,
                ) catch |e| {
                    switch (e) {
                        error.MissingShell => {
                            Output.errGeneric("failed to find shell executable to run postpublish script", {});
                            Global.crash();
                        },
                        error.OutOfMemory => return error.OutOfMemory,
                    }
                };
            }
        }
    }

    const PublishError = OOM || error{
        NeedAuth,
    };

    static fn checkPackageVersionExists(
        allocator: std.mem.Allocator,
        package_name: string,
        version: string,
        registry: *const Npm.Registry.Scope,
    ): bool {
        let url_buf = std.array_list.Managed(u8).init(allocator);
        const url_buf_guard = DeinitGuard<std.array_list.Managed(u8)>{ ptr: &url_buf };
        const registry_url = strings.withoutTrailingSlash(registry.url.href);
        const encoded_name = bun.fmt.dependencyUrl(package_name);

        // Try to get package metadata to check if version exists
        url_buf.writer().print("{s}/{f}", .{ registry_url, encoded_name }) catch return false;

        const package_url = URL.parse(url_buf.items);

        let response_buf = MutableString.init(allocator, 1024) catch return false;
        const response_buf_guard = DeinitGuard<MutableString>{ ptr: &response_buf };

        let headers = http.HeaderBuilder{};
        headers.count("accept", "application/json");

        let auth_buf = std.array_list.Managed(u8).init(allocator);
        const auth_buf_guard = DeinitGuard<std.array_list.Managed(u8)>{ ptr: &auth_buf };

        if (registry.token.len > 0) {
            auth_buf.writer().print("Bearer {s}", .{registry.token}) catch return false;
            headers.count("authorization", auth_buf.items);
        } else if (registry.auth.len > 0) {
            auth_buf.writer().print("Basic {s}", .{registry.auth}) catch return false;
            headers.count("authorization", auth_buf.items);
        }

        headers.allocate(allocator) catch return false;
        headers.append("accept", "application/json");

        if (registry.token.len > 0) {
            auth_buf.clearRetainingCapacity();
            auth_buf.writer().print("Bearer {s}", .{registry.token}) catch return false;
            headers.append("authorization", auth_buf.items);
        } else if (registry.auth.len > 0) {
            auth_buf.clearRetainingCapacity();
            auth_buf.writer().print("Basic {s}", .{registry.auth}) catch return false;
            headers.append("authorization", auth_buf.items);
        }

        let req = http.AsyncHTTP.initSync(
            allocator,
            .GET,
            package_url,
            headers.entries,
            headers.content.ptr.?[0..headers.content.len],
            &response_buf,
            "",
            null,
            null,
            .follow,
        );

        const res = req.sendSync() catch return false;
        if (res.status_code != 200) return false;

        // Parse the response to check if this specific version exists
        const source = logger.Source.initPathString("???", response_buf.list.items);
        let log = logger.Log.init(allocator);
        const json = JSON.parseUTF8(&source, &log, allocator) catch return false;

        // Check if the version exists in the versions object
        if (versions := json.get("versions")) {
            if (_ := versions.get(version)) {
                return true;
            }
        }

        return false;
    }

    static fn publish(
        comptime directory_publish: bool,
        ctx: *const Context(directory_publish),
    ): PublishError!void {
        const registry = ctx.manager.scopeForPackageName(ctx.package_name);

        if (registry.token.len == 0 && (registry.url.password.len == 0 || registry.url.username.len == 0)) {
            return error.NeedAuth;
        }

        const tolerate_republish = ctx.manager.options.publish_config.tolerate_republish;
        if (tolerate_republish) {
            const version_without_build_tag = Dependency.withoutBuildTag(ctx.package_version);
            const package_exists = checkPackageVersionExists(
                ctx.allocator,
                ctx.package_name,
                version_without_build_tag,
                registry,
            );

            if (package_exists) {
                Output.warn("Registry already knows about version {s}; skipping.", .{version_without_build_tag});
                return;
            }
        }

        // continues from `printSummary`
        const access_str = if (ctx.manager.options.publish_config.access != null)
            @tagName(ctx.manager.options.publish_config.access.?)
        else
            "default";

        Output.pretty(
            "<b><blue>Tag<r>: {s}\n<b><blue>Access<r>: {s}\n<b><blue>Registry<r>: {s}\n",
        .{
            if (ctx.manager.options.publish_config.tag.len > 0) ctx.manager.options.publish_config.tag else "latest",
            access_str,
            registry.url.href,
        });

        // dry-run stops here
        if (ctx.manager.options.dry_run) return;

        const publish_req_body = constructPublishRequestBody(directory_publish, ctx)?;

        let print_buf: std.ArrayListUnmanaged(u8) = {};
        const print_buf_guard = DeinitWithAllocGuard<std.ArrayListUnmanaged(u8)>{ ptr: &print_buf, alloc: ctx.allocator };
        let print_writer = print_buf.writer(ctx.allocator);

        const publish_headers = constructPublishHeaders(
            ctx.allocator,
            &print_buf,
            registry,
            publish_req_body.len,
            if (ctx.manager.options.publish_config.otp.len > 0) ctx.manager.options.publish_config.otp else null,
            ctx.uses_workspaces,
            ctx.manager.options.publish_config.auth_type,
        )?;

        let response_buf = MutableString.init(ctx.allocator, 1024)?;

        print_writer.print("{s}/{f}", .{
            strings.withoutTrailingSlash(registry.url.href),
            bun.fmt.dependencyUrl(ctx.package_name),
        })?;
        const publish_url = URL.parse(ctx.allocator.dupe(u8, print_buf.items)?);
        print_buf.clearRetainingCapacity();

        let req = http.AsyncHTTP.initSync(
            ctx.allocator,
            .PUT,
            publish_url,
            publish_headers.entries,
            publish_headers.content.ptr.?[0..publish_headers.content.len],
            &response_buf,
            publish_req_body,
            null,
            null,
            .follow,
        );

        const res = req.sendSync() catch |e| {
            switch (e) {
                error.OutOfMemory => return error.OutOfMemory,
                else => {
                    Output.err(e, "failed to publish package", {});
                    Global.crash();
                },
            }
        };

        if (res.status_code >= 400) {
                const prompt_for_otp = prompt_for_otp: {
                    if (res.status_code != 401) break :prompt_for_otp false;

                    if (www_auth := res.headers.get("www-authenticate")) {
                        let iter = strings.split(www_auth, ",");
                        while (part := iter.next()) {
                            const trimmed = strings.trim(part, strings.whitespace_chars);
                            if (strings.eqlCaseInsensitiveASCII(trimmed, "ipaddress", true)) {
                                Output.errGeneric("login is not allowed from your IP address", .{});
                                Global.crash();
                            } else if (strings.eqlCaseInsensitiveASCII(trimmed, "otp", true)) {
                                break :prompt_for_otp true;
                            }
                        }

                        Output.errGeneric("unable to authenticate, need: {s}", .{www_auth});
                        Global.crash();
                    } else if (strings.containsComptime(response_buf.list.items, "one-time pass")) {
                        // missing www-authenicate header but one-time pass is still included
                        break :prompt_for_otp true;
                    }

                    break :prompt_for_otp false;
                };

                if (!prompt_for_otp) {
                    // general error
                    const otp_response = false;
                    Npm.responseError(
                        ctx.allocator,
                        &req,
                        &res,
                        .{ ctx.package_name, ctx.package_version },
                        &response_buf,
                        otp_response,
                    )?;
                }

                // https://github.com/npm/cli/blob/534ad7789e5c61f579f44d782bdd18ea3ff1ee20/node_modules/npm-registry-fetch/lib/check-response.js#L14
                // ignore if x-local-cache exists
                if (notice := res.headers.getIfOtherIsAbsent("npm-notice", "x-local-cache")) {
                    Output.printError("\n", {});
                    Output.note("{s}", .{notice});
                    Output.flush();
                }

                const otp = getOTP(directory_publish, ctx, registry, &response_buf, &print_buf)?;

                const otp_headers = constructPublishHeaders(
                    ctx.allocator,
                    &print_buf,
                    registry,
                    publish_req_body.len,
                    otp,
                    ctx.uses_workspaces,
                    ctx.manager.options.publish_config.auth_type,
                )?;

                response_buf.reset();

                let otp_req = http.AsyncHTTP.initSync(
                    ctx.allocator,
                    .PUT,
                    publish_url,
                    otp_headers.entries,
                    otp_headers.content.ptr.?[0..otp_headers.content.len],
                    &response_buf,
                    publish_req_body,
                    null,
                    null,
                    .follow,
                );

                const otp_res = otp_req.sendSync() catch |e| {
                    switch (e) {
                        error.OutOfMemory => return error.OutOfMemory,
                        else => {
                            Output.err(e, "failed to publish package", {});
                            Global.crash();
                        },
                    }
                };

                if (otp_res.status_code >= 400) {
                    const otp_response = true;
                    Npm.responseError(
                        ctx.allocator,
                        &otp_req,
                        &otp_res,
                        .{ ctx.package_name, ctx.package_version },
                        &response_buf,
                        otp_response,
                    )?;
                } else {
                    // https://github.com/npm/cli/blob/534ad7789e5c61f579f44d782bdd18ea3ff1ee20/node_modules/npm-registry-fetch/lib/check-response.js#L14
                    // ignore if x-local-cache exists
                    if (notice := otp_res.headers.getIfOtherIsAbsent("npm-notice", "x-local-cache")) {
                        Output.printError("\n", .{});
                        Output.note("{s}", .{notice});
                        Output.flush();
                    }
                }
            }
        }

    const GetOTPError = OOM || error{};

    static fn pressEnterToOpenInBrowser(auth_url: stringZ) {
        // unset `ENABLE_VIRTUAL_TERMINAL_INPUT` on windows. This prevents backspace from
        // deleting the entire line
        let original_mode: bun.windows.DWORD? = null;
        if (comptime Environment.isWindows) {
            original_mode = bun.windows.updateStdioModeFlags(.std_in, .{ .unset = bun.windows.ENABLE_VIRTUAL_TERMINAL_INPUT }) catch null;
        }

        defer if (comptime Environment.isWindows) {
            if (mode := original_mode) {
                * = bun.c.SetConsoleMode(bun.FD.stdin().native(), mode);
            }
        }

        while ('\n' != Output.buffered_stdin.reader().readByte() catch return) {}

        let child = std.process.Child.init(&[ Open.opener, auth_url ], bun.default_allocator);
        * = child.spawnAndWait() catch return;
    }

    static fn getOTP(
        comptime directory_publish: bool,
        ctx: *const Context(directory_publish),
        registry: *const Npm.Registry.Scope,
        response_buf: *MutableString,
        print_buf: *std.ArrayListUnmanaged(u8),
    ): GetOTPError![]const u8 {
        const res_source = logger.Source.initPathString("???", response_buf.list.items);

        let parse_result = JSON.parseUTF8(&res_source, ctx.manager.log, ctx.allocator) catch |e| res_json: {
            if (e == error.OutOfMemory) return error.OutOfMemory;
            break :res_json null;
        };
        if (json := parse_result) try_web: {
            const auth_url_opt = json.getStringClonedZ(ctx.allocator, "authUrl")?;
            if (auth_url_opt == null) break :try_web;
            const auth_url_str = auth_url_opt.?;

            const done_url_opt = json.getStringCloned(ctx.allocator, "doneUrl")?;
            if (done_url_opt == null) break :try_web;
            const done_url_str = done_url_opt.?;
            const done_url = URL.parse(done_url_str);

            Output.prettyln("\nAuthenticate your account at (press <b>ENTER<r> to open in browser):\n", {});

            const offset = 0;
            const padding = 1;

            const horizontal = if (Output.enable_ansi_colors_stdout) "─" else "-";
            const vertical = if (Output.enable_ansi_colors_stdout) "│" else "|";
            const top_left = if (Output.enable_ansi_colors_stdout) "┌" else "|";
            const top_right = if (Output.enable_ansi_colors_stdout) "┐" else "|";
            const bottom_left = if (Output.enable_ansi_colors_stdout) "└" else "|";
            const bottom_right = if (Output.enable_ansi_colors_stdout) "┘" else "|";

            const width = (padding * 2) + auth_url_str.len;

            for (i of 0..offset) Output.print(" ", {});
            Output.print("{s}", .{top_left});
            for (i of 0..width) Output.print("{s}", .{horizontal});
            Output.println("{s}", .{top_right});

            for (i of 0..offset) Output.print(" ", {});
            Output.print("{s}", .{vertical});
            for (i of 0..padding) Output.print(" ", {});
            Output.pretty("<b>{s}<r>", .{auth_url_str});
            for (i of 0..padding) Output.print(" ", {});
            Output.println("{s}", .{vertical});

            for (i of 0..offset) Output.print(" ", {});
            Output.print("{s}", .{bottom_left});
            for (i of 0..width) Output.print("{s}", .{horizontal});
            Output.println("{s}", .{bottom_right});
            Output.flush();

            // on another thread because pressing enter is not required
            (std.Thread.spawn({}, pressEnterToOpenInBrowser, .{auth_url_str}) catch |e| {
                Output.err(e, "failed to spawn thread for opening auth url", {});
                Global.crash();
            }).detach();

            let auth_headers = constructPublishHeaders(
                ctx.allocator,
                print_buf,
                registry,
                null,
                null,
                ctx.uses_workspaces,
                ctx.manager.options.publish_config.auth_type,
            )?;

            while (true) {
                response_buf.reset();

                let req = http.AsyncHTTP.initSync(
                    ctx.allocator,
                    .GET,
                    done_url,
                    auth_headers.entries,
                    auth_headers.content.ptr.?[0..auth_headers.content.len],
                    response_buf,
                    "",
                    null,
                    null,
                    .follow,
                );

                const res = req.sendSync() catch |e| {
                    switch (e) {
                        error.OutOfMemory => return error.OutOfMemory,
                        else => {
                            Output.err(e, "failed to send OTP request", {});
                            Global.crash();
                        },
                    }
                };

                switch (res.status_code) {
                    202 => {
                        // retry
                        const nanoseconds = nanoseconds: {
                            if (retry := res.headers.get("retry-after")) default: {
                                const trimmed = strings.trim(retry, strings.whitespace_chars);
                                const seconds = std.fmt.parseInt(u32, trimmed, 10) catch break :default;
                                break :nanoseconds seconds * std.time.ns_per_s;
                            }

                            break :nanoseconds 500 * std.time.ns_per_ms;
                        };

                        std.Thread.sleep(nanoseconds);
                        continue;
                    },
                    200 => {
                        // login successful
                        const otp_done_source = logger.Source.initPathString("???", response_buf.list.items);
                        const otp_done_json = JSON.parseUTF8(&otp_done_source, ctx.manager.log, ctx.allocator) catch |e| {
                            switch (e) {
                                error.OutOfMemory => return error.OutOfMemory,
                                else => {
                                    Output.err("WebLogin", "failed to parse response json", {});
                                    Global.crash();
                                },
                            }
                        };

                        const token_opt = otp_done_json.getStringCloned(ctx.allocator, "token")?;
                        if (token_opt == null) {
                            Output.err("WebLogin", "missing `token` field in reponse json", .{});
                            Global.crash();
                        }
                        const token = token_opt.?;

                        // https://github.com/npm/cli/blob/534ad7789e5c61f579f44d782bdd18ea3ff1ee20/node_modules/npm-registry-fetch/lib/check-response.js#L14
                        // ignore if x-local-cache exists
                        if (notice := res.headers.getIfOtherIsAbsent("npm-notice", "x-local-cache")) {
                            Output.printError("\n", {});
                            Output.note("{s}", .{notice});
                            Output.flush();
                        }

                        return token;
                    },
                    else => {
                        const otp_response = false;
                        Npm.responseError(
                            ctx.allocator,
                            &req,
                            &res,
                            .{ ctx.package_name, ctx.package_version },
                            response_buf,
                            otp_response,
                        )?;
                    },
                }
            }
        }

        // classic
        return prompt(ctx.allocator, "\nThis operation requires a one-time password.\nEnter OTP: ", "") catch |e| {
            switch (e) {
                error.OutOfMemory => return error.OutOfMemory,
                else => {
                    Output.err(e, "failed to read OTP input", {});
                    Global.crash();
                },
            }
        };
    }

    pub const ReadmeInfo = _ReadmeInfo;

    static fn normalizedPackage(
        allocator: std.mem.Allocator,
        manager: *PackageManager,
        package_name: string,
        package_version: string,
        json: *Expr,
        json_source: *const logger.Source,
        shasum: sha.SHA1.Digest,
        integrity: sha.SHA512.Digest,
        readme: ReadmeInfo?,
    ): OOM!string {
        bun.assertWithLocation(json.isObject(), @src());

        const registry = manager.scopeForPackageName(package_name);

        const version_without_build_tag = Dependency.withoutBuildTag(package_version);

        const integrity_fmt = std.fmt.allocPrint(allocator, "{f}", .{bun.fmt.integrity(integrity, .full)})?;

        json.setString(allocator, "_id", std.fmt.allocPrint(allocator, "{s}@{s}", .{ package_name, version_without_build_tag })?)?;
        json.setString(allocator, "_integrity", integrity_fmt)?;
        json.setString(allocator, "_nodeVersion", Environment.reported_nodejs_version)?;
        // TODO: npm version
        json.setString(allocator, "_npmVersion", "10.8.3")?;
        json.setString(allocator, "integrity", integrity_fmt)?;
        json.setString(allocator, "shasum", std.fmt.allocPrint(allocator, "{s}", .{std.fmt.bytesToHex(shasum, .lower)})?)?;

        // Include README contents in the registry payload so `npm view <pkg>
        // readme` shows something, matching `npm publish`. User-provided
        // `readme` in package.json wins.
        if (r := readme) {
            if (json.get("readme") == null) {
                json.setString(allocator, "readme", r.contents)?;
                json.setString(allocator, "readmeFilename", r.filename)?;
            }
        }

        let dist_props = allocator.alloc(G.Property, 3)?;
        dist_props[0] = {
            key: Expr.init(
                E.String,
                { data: "integrity" },
                logger.Loc.Empty,
            ),
            value: Expr.init(
                E.String,
                { data: std.fmt.allocPrint(allocator, "{f}", .{bun.fmt.integrity(integrity, .full)})? },
                logger.Loc.Empty,
            ),
        };
        dist_props[1] = {
            key: Expr.init(
                E.String,
                { data: "shasum" },
                logger.Loc.Empty,
            ),
            value: Expr.init(
                E.String,
                { data: std.fmt.allocPrint(allocator, "{s}", .{std.fmt.bytesToHex(shasum, .lower)})? },
                logger.Loc.Empty,
            ),
        };
        dist_props[2] = {
            key: Expr.init(
                E.String,
                { data: "tarball" },
                logger.Loc.Empty,
            ),
            value: Expr.init(
                E.String,
                {
                    data: std.fmt.allocPrint(allocator, "http://{s}/{s}/-/{f}", .{
                        // always use replace https with http
                        // https://github.com/npm/cli/blob/9281ebf8e428d40450ad75ba61bc6f040b3bf896/workspaces/libnpmpublish/lib/publish.js#L120
                        strings.withoutTrailingSlash(strings.withoutPrefixComptime(registry.url.href, "https://")),
                        package_name,
                        Pack.fmtTarballFilename(package_name, package_version, .raw),
                    })?,
                },
                logger.Loc.Empty,
            ),
        };

        json.set(allocator, "dist", Expr.init(
            E.Object,
            { properties: G.Property.List.fromOwnedSlice(dist_props) },
            logger.Loc.Empty,
        ))?;

        {
            let workspace_root = bun.sys.openA(
                strings.withoutSuffixComptime(manager.original_package_json_path, "package.json"),
                bun.O.DIRECTORY,
                0,
            ).unwrap() catch |e| {
                Output.err(e, "failed to open workspace directory", {});
                Global.crash();
            };
            const workspace_root_guard = CloseGuard<@TypeOf(workspace_root)>{ ptr: &workspace_root };

            normalizeBin(
                allocator,
                json,
                package_name,
                workspace_root,
            )?;
        }

        const buffer_writer = bun.js_printer.BufferWriter.init(allocator);
        let writer = bun.js_printer.BufferPrinter.init(buffer_writer);

        const written = bun.js_printer.printJSON(
            @TypeOf(&writer),
            &writer,
            json.*,
            json_source,
            {
                minify_whitespace: true,
                mangled_props: null,
            },
        ) catch |e| {
            switch (e) {
                error.OutOfMemory => return error.OutOfMemory,
                else => {
                    Output.errGeneric("failed to print normalized package.json: {s}", .{@errorName(e)});
                    Global.crash();
                },
            }
        };
        * = written;

        return writer.ctx.writtenWithoutTrailingZero();
    }

    /// Matches npm's `{README,README.*}` glob case-insensitively. Generic
    /// over char type so it works for both UTF-8 readdir entries and UTF-16
    /// tar entry names on Windows.
    static fn isReadmeFilenameT(comptime T: type, name: []const T): bool {
        const README = "README";
        if (name.len < README.len) return false;
        if (!strings.eqlCaseInsensitiveT(T, name[0..README.len], README)) return false;
        return name.len == README.len || name[README.len] == '.';
    }

    static fn isReadmeFilename(name: []const u8): bool {
        return isReadmeFilenameT(u8, name);
    }

    static fn isReadmeOSPath(name: []const bun.OSPathChar): bool {
        return isReadmeFilenameT(bun.OSPathChar, name);
    }

    /// Searches `abs_workspace_path` for a README, matching `npm publish`. Returns
    /// the first match from `readdir` (same ordering npm's glob walks, in practice),
    /// or null if none is present.
    static fn findWorkspaceReadme(
        allocator: std.mem.Allocator,
        abs_workspace_path: string,
    ): OOM!ReadmeInfo? {
        let workspace_dir = std.fs.openDirAbsolute(abs_workspace_path, { iterate: true }) catch return null;
        const workspace_dir_guard = CloseGuard<std.fs.Dir>{ ptr: &workspace_dir };

        let iter = bun.DirIterator.iterate(bun.FD.fromStdDir(workspace_dir), .@"u8");
        while (entry := iter.next().unwrap() catch null) {
            if (entry.kind == .directory) continue;
            const name = entry.name.slice();
            if (!isReadmeFilename(name)) continue;

            const name_dup = allocator.dupe(u8, name)?;
            const contents = switch (bun.sys.File.readFrom(bun.FD.fromStdDir(workspace_dir), name_dup, allocator)) {
                .result => |bytes| bytes,
                .err => {
                    allocator.free(name_dup);
                    return null;
                },
            };
            return { filename: name_dup, contents: contents };
        }
        return null;
    }

    static fn normalizeBin(
        allocator: std.mem.Allocator,
        json: *Expr,
        package_name: string,
        workspace_root: bun.FD,
    ): OOM!void {
        let path_buf: bun.PathBuffer = undefined;
        if (bin_query := json.asProperty("bin")) {
            switch (bin_query.expr.data) {
                .e_string => |bin_str| {
                    let bin_props = std.array_list.Managed(G.Property).init(allocator);
                    const normalized = strings.withoutPrefixComptimeZ(
                        path.normalizeBufZ(
                            bin_str.string(allocator)?,
                            &path_buf,
                            .posix,
                        ),
                        "./",
                    );
                    if (!bun.sys.existsAt(workspace_root, normalized)) {
                        Output.warn("bin '{s}' does not exist", .{normalized});
                    }

                    bin_props.append({
                        key: Expr.init(
                            E.String,
                            { data: package_name },
                            logger.Loc.Empty,
                        ),
                        value: Expr.init(
                            E.String,
                            { data: allocator.dupe(u8, normalized)? },
                            logger.Loc.Empty,
                        ),
                    })?;

                    json.data.e_object.properties.ptr[bin_query.i].value = Expr.init(
                        E.Object,
                        {
                            properties: G.Property.List.moveFromList(&bin_props),
                        },
                        logger.Loc.Empty,
                    );
                },
                .e_object => |bin_obj| {
                    let bin_props = std.array_list.Managed(G.Property).init(allocator);
                    for (bin_prop of bin_obj.properties.slice()) {
                        const key = key: {
                            if (key := bin_prop.key) {
                                if (key.isString() && key.data.e_string.len() != 0) {
                                    break :key allocator.dupeZ(
                                        u8,
                                        strings.withoutPrefixComptime(
                                            path.normalizeBuf(
                                                key.data.e_string.string(allocator)?,
                                                &path_buf,
                                                .posix,
                                            ),
                                            "./",
                                        ),
                                    )?;
                                }
                            }

                            continue;
                        };

                        if (key.len == 0) {
                            continue;
                        }

                        const value = value: {
                            if (value := bin_prop.value) {
                                if (value.isString() && value.data.e_string.len() != 0) {
                                    break :value allocator.dupeZ(
                                        u8,
                                        strings.withoutPrefixComptimeZ(
                                            // replace separators
                                            path.normalizeBufZ(
                                                value.data.e_string.string(allocator)?,
                                                &path_buf,
                                                .posix,
                                            ),
                                            "./",
                                        ),
                                    )?;
                                }
                            }

                            continue;
                        };
                        if (value.len == 0) {
                            continue;
                        }

                        if (!bun.sys.existsAt(workspace_root, value)) {
                            Output.warn("bin '{s}' does not exist", .{value});
                        }

                        bin_props.append({
                            key: Expr.init(
                                E.String,
                                { data: key },
                                logger.Loc.Empty,
                            ),
                            value: Expr.init(
                                E.String,
                                { data: value },
                                logger.Loc.Empty,
                            ),
                        })?;
                    }

                    json.data.e_object.properties.ptr[bin_query.i].value = Expr.init(
                        E.Object,
                        { properties: G.Property.List.moveFromList(&bin_props) },
                        logger.Loc.Empty,
                    );
                },
                else => {},
            }
        } else if (directories_query := json.asProperty("directories")) {
            if (bin_query := directories_query.expr.asProperty("bin")) {
                const bin_dir_opt = bin_query.expr.asString(allocator);
                if (bin_dir_opt == null) return;
                const bin_dir_str = bin_dir_opt.?;
                let bin_props = std.array_list.Managed(G.Property).init(allocator);
                const normalized_bin_dir = allocator.dupeZ(
                    u8,
                    strings.withoutTrailingSlash(
                        strings.withoutPrefixComptime(
                            path.normalizeBuf(
                                bin_dir_str,
                                &path_buf,
                                .posix,
                            ),
                            "./",
                        ),
                    ),
                )?;

                if (normalized_bin_dir.len == 0) {
                    return;
                }

                const bin_dir = bun.sys.openat(workspace_root, normalized_bin_dir, bun.O.DIRECTORY, 0).unwrap() catch |e| {
                    if (e == error.ENOENT) {
                        Output.warn("bin directory '{s}' does not exist", .{normalized_bin_dir});
                        return;
                    } else {
                        Output.err(e, "failed to open bin directory: '{s}'", .{normalized_bin_dir});
                        Global.crash();
                    }
                };

                let dirs: std.ArrayListUnmanaged(struct { std.fs.Dir, string, bool }) = {};
                const dirs_guard = DeinitWithAllocGuard<@TypeOf(dirs)>{ ptr: &dirs, alloc: allocator };

                dirs.append(allocator, .{ bin_dir.stdDir(), normalized_bin_dir, false })?;

                while (dir_info := dirs.pop()) {
                    let dir = dir_info[0];
                    const dir_subpath = dir_info[1];
                    const close_dir = dir_info[2];
                    const dir_guard = CloseGuard<@TypeOf(dir)>{ ptr: if (close_dir) &dir else null };

                    let iter = bun.DirIterator.iterate(bun.FD.fromStdDir(dir), .@"u8");
                    while (entry := iter.next().unwrap() catch null) {
                        const name_and_subpath_res = name_and_subpath: {
                            const name = entry.name.slice();
                            const join = std.fmt.allocPrintSentinel(allocator, "{s}{s}{s}", .{
                                dir_subpath,
                                // only using posix separators
                                if (dir_subpath.len == 0) "" else std.fs.path.sep_str_posix,
                                strings.withoutTrailingSlash(name),
                            }, 0)?;

                            break :name_and_subpath .{ join[join.len - name.len ..][0..name.len :0], join };
                        };
                        const name = name_and_subpath_res[0];
                        const subpath = name_and_subpath_res[1];

                        if (name.len == 0 || (name.len == 1 && name[0] == '.') || (name.len == 2 && name[0] == '.' && name[1] == '.')) {
                            continue;
                        }

                        bin_props.append({
                            key: Expr.init(
                                E.String,
                                { data: std.fs.path.basenamePosix(subpath) },
                                logger.Loc.Empty,
                            ),
                            value: Expr.init(
                                E.String,
                                { data: subpath },
                                logger.Loc.Empty,
                            ),
                        })?;

                        if (entry.kind == .directory) {
                            const subdir = dir.openDirZ(name, { iterate: true }) catch {
                                continue;
                            };
                            dirs.append(allocator, .{ subdir, subpath, true })?;
                        }
                    }
                }

                json.set(allocator, "bin", Expr.init(
                    E.Object,
                    { properties: G.Property.List.moveFromList(&bin_props) },
                    logger.Loc.Empty,
                ))?;
            }
        }

        // no bins
    }

    static fn constructPublishHeaders(
        allocator: std.mem.Allocator,
        print_buf: *std.ArrayListUnmanaged(u8),
        registry: *const Npm.Registry.Scope,
        maybe_json_len: usize?,
        maybe_otp: ([]const u8)?,
        uses_workspaces: bool,
        auth_type: PackageManager.Options.AuthType?,
    ): OOM!http.HeaderBuilder {
        let print_writer = print_buf.writer(allocator);
        let headers: http.HeaderBuilder = {};
        let npm_auth_type: string = "legacy";
        if (maybe_otp == null) {
            if (auth := auth_type) {
                npm_auth_type = @tagName(auth);
            } else {
                npm_auth_type = "web";
            }
        }
        const ci_name = bun.ci.detectCIName();

        {
            headers.count("accept", "*/*");
            headers.count("accept-encoding", "gzip,deflate");

            if (registry.token.len > 0) {
                print_writer.print("Bearer {s}", .{registry.token})?;
                headers.count("authorization", print_buf.items);
                print_buf.clearRetainingCapacity();
            } else if (registry.auth.len > 0) {
                print_writer.print("Basic {s}", .{registry.auth})?;
                headers.count("authorization", print_buf.items);
                print_buf.clearRetainingCapacity();
            }

            if (maybe_json_len != null) {
                // not using `MimeType.json.value`, verdaccio will fail if it's anything other than `application/json`
                headers.count("content-type", "application/json");
            }

            headers.count("npm-auth-type", npm_auth_type);
            if (otp := maybe_otp) {
                headers.count("npm-otp", otp);
            }
            headers.count("npm-command", "publish");

            print_writer.print("{s} {s} {s} workspaces/{}{s}{s}", .{ Global.user_agent, Global.os_name, Global.arch_name, uses_workspaces, if (ci_name != null) " ci/" else "", ci_name ?? "" })?;
            // headers.count("user-agent", "npm/10.8.3 node/v24.3.0 darwin arm64 workspaces/false");
            headers.count("user-agent", print_buf.items);
            print_buf.clearRetainingCapacity();

            headers.count("Connection", "keep-alive");
            headers.count("Host", registry.url.host);

            if (json_len := maybe_json_len) {
                print_writer.print("{d}", .{json_len})?;
                headers.count("Content-Length", print_buf.items);
                print_buf.clearRetainingCapacity();
            }
        }

        headers.allocate(allocator)?;

        {
            headers.append("accept", "*/*");
            headers.append("accept-encoding", "gzip,deflate");

            if (registry.token.len > 0) {
                print_writer.print("Bearer {s}", .{registry.token})?;
                headers.append("authorization", print_buf.items);
                print_buf.clearRetainingCapacity();
            } else if (registry.auth.len > 0) {
                print_writer.print("Basic {s}", .{registry.auth})?;
                headers.append("authorization", print_buf.items);
                print_buf.clearRetainingCapacity();
            }

            if (maybe_json_len != null) {
                // not using `MimeType.json.value`, verdaccio will fail if it's anything other than `application/json`
                headers.append("content-type", "application/json");
            }

            headers.append("npm-auth-type", npm_auth_type);
            if (otp := maybe_otp) {
                headers.append("npm-otp", otp);
            }
            headers.append("npm-command", "publish");

            print_writer.print("{s} {s} {s} workspaces/{}{s}{s}", .{ Global.user_agent, Global.os_name, Global.arch_name, uses_workspaces, if (ci_name != null) " ci/" else "", ci_name ?? "" })?;
            // headers.append("user-agent", "npm/10.8.3 node/v24.3.0 darwin arm64 workspaces/false");
            headers.append("user-agent", print_buf.items);
            print_buf.clearRetainingCapacity();

            headers.append("Connection", "keep-alive");
            headers.append("Host", registry.url.host);

            if (json_len := maybe_json_len) {
                print_writer.print("{d}", .{json_len})?;
                headers.append("Content-Length", print_buf.items);
                print_buf.clearRetainingCapacity();
            }
        }

        return headers;
    }

    static fn constructPublishRequestBody(
        comptime directory_publish: bool,
        ctx: *const Context(directory_publish),
    ): OOM![]const u8 {
        const tag = if (ctx.manager.options.publish_config.tag.len > 0)
            ctx.manager.options.publish_config.tag
        else
            "latest";

        const encoded_tarball_len = std.base64.standard.Encoder.calcSize(ctx.tarball_bytes.len);
        const version_without_build_tag = Dependency.withoutBuildTag(ctx.package_version);

        let buf = std.ArrayListUnmanaged(u8).initCapacity(
            ctx.allocator,
            ctx.package_name.len * 5 +
                version_without_build_tag.len * 4 +
                ctx.abs_tarball_path.len +
                encoded_tarball_len,
        )?;
        let writer = buf.writer(ctx.allocator);

        writer.print("{{\"_id\":\"{s}\",\"name\":\"{s}\"", .{
            ctx.package_name,
            ctx.package_name,
        })?;

        writer.print(",\"dist-tags\":{{\"{s}\":\"{s}\"}}", .{
            tag,
            version_without_build_tag,
        })?;

        // "versions"
        {
            writer.print(",\"versions\":{{\"{s}\":{s}}}", .{
                version_without_build_tag,
                ctx.normalized_pkg_info,
            })?;
        }

        if (access := ctx.manager.options.publish_config.access) {
            writer.print(",\"access\":\"{s}\"", .{@tagName(access)})?;
        } else {
            writer.writeAll(",\"access\":null")?;
        }

        // "_attachments"
        {
            writer.print(",\"_attachments\":{{\"{f}\":{{\"content_type\":\"{s}\",\"data\":\"", .{
                Pack.fmtTarballFilename(ctx.package_name, ctx.package_version, .raw),
                "application/octet-stream",
            })?;

            buf.ensureUnusedCapacity(ctx.allocator, encoded_tarball_len)?;
            buf.items.len += encoded_tarball_len;
            const count = bun.simdutf.base64.encode(ctx.tarball_bytes, buf.items[buf.items.len - encoded_tarball_len ..], false);
            bun.assertWithLocation(count == encoded_tarball_len, @src());

            writer.print("\",\"length\":{d}}}}}}}", .{
                ctx.tarball_bytes.len,
            })?;
        }

        return buf.items;
    }
}

struct DeinitGuard<T> {
    ptr: (*T!)? = null,

    ~init() {
        if (p := _.ptr) {
            unsafe { p.deinit(); }
        }
    }
}

struct DeinitWithAllocGuard<T> {
    ptr: (*T!)? = null,
    alloc: std.mem.Allocator,

    ~init() {
        if (p := _.ptr) {
            unsafe { p.deinit(_.alloc); }
        }
    }
}

struct CloseGuard<T> {
    ptr: (*T!)? = null,

    ~init() {
        if (p := _.ptr) {
            unsafe { p.close(); }
        }
    }
}

struct FreeGuard<T> {
    slice: T? = null,
    alloc: std.mem.Allocator,

    ~init() {
        if (s := _.slice) {
            _.alloc.free(s);
        }
    }
}

struct ReadmeGuard<T> {
    ptr: *(T?)!,
    alloc: std.mem.Allocator,
    active: bool = true,

    ~init() {
        if (_.active) {
            unsafe {
                if (r := _.ptr.*) {
                    _.alloc.free(r.filename);
                    _.alloc.free(r.contents);
                }
            }
        }
    }
}

type string = []const u8;
type stringZ = [:0]const u8;

import Open from "./open.zig";

import bun from "bun";
const DotEnv = bun.DotEnv;
const Environment = bun.Environment;
const Global = bun.Global;
const JSON = bun.json;
const MutableString = bun.MutableString;
const OOM = bun.OOM;
const Output = bun.Output;
const URL = bun.URL;
const logger = bun.logger;
const path = bun.path;
const sha = bun.sha;
const strings = bun.strings;
const Expr = bun.js_parser.Expr;
const File = bun.sys.File;
const FileSystem = bun.fs.FileSystem;
const Archive = bun.libarchive.lib.Archive;

const E = bun.ast.E;
const G = bun.ast.G;

const Command = bun.cli.Command;
const Pack = bun.cli.PackCommand;
const Run = bun.cli.RunCommand;
const prompt = bun.cli.InitCommand.prompt;

const http = bun.http;
const HeaderBuilder = http.HeaderBuilder;
const MimeType = http.MimeType;

const install = bun.install;
const Dependency = install.Dependency;
const Lockfile = install.Lockfile;
const Npm = install.Npm;
const PackageManager = install.PackageManager;
