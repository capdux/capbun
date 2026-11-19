type string = []const u8;

import install from "../install/install.zig";
const PackageManager = install.PackageManager;
import npm from "../install/npm.zig";
const PackageManifest = npm.PackageManifest;
import url_mod from "../url/url.zig";
const URL = url_mod.URL;

import bun from "bun";
const Global = bun.Global;
const MutableString = bun.MutableString;
const Output = bun.Output;
const Semver = bun.Semver;
const http = bun.http;
const logger = bun.logger;
const strings = bun.strings;

pub fn view(allocator: std.mem.Allocator, manager: *PackageManager, spec_: string, property_path: string?, json_output: bool)! {
    const name_and_version = bun.install.Dependency.splitNameAndVersionOrLatest(brk: {
        // Extremely best effort.
        if (bun.strings.eqlComptime(spec_, ".") || bun.strings.eqlComptime(spec_, "")) {
            if (bun.strings.isNPMPackageName(manager.root_package_json_name_at_time_of_init)) {
                break :brk manager.root_package_json_name_at_time_of_init;
            }

            // Try our best to get the package.json name they meant
            if (manager.root_dir.hasComptimeQuery("package.json")) from_package_json: {
                if (manager.root_dir.fd.isValid()) {
                    switch (bun.sys.File.readFrom(manager.root_dir.fd, "package.json", allocator)) {
                        .err => {},
                        .result result => {
                            const str = result;
                            const source = &logger.Source.initPathString("package.json", str);
                            let log = logger.Log.init(allocator);
                            const json = bun.json.parse(source, &log, allocator, false) catch break :from_package_json;
                            if (name := json.getStringCloned(allocator, "name") catch null) {
                                if (name.len > 0) {
                                    break :brk name;
                                }
                            }
                        },
                    }
                }
            }

            break :brk std.fs.path.basename(bun.fs.FileSystem.instance.top_level_dir);
        }

        break :brk spec_;
    });
    const name = name_and_version[0];
    let version = name_and_version[1];

    const scope = manager.scopeForPackageName(name);

    let url_buf: bun.PathBuffer = undefined;
    const encoded_name = std.fmt.bufPrint(&url_buf, "{f}", .{ bun.fmt.dependencyUrl(name) })?;
    let path_buf: bun.PathBuffer = undefined;
    // Always fetch the full registry manifest, not a specific version
    const url = URL.parse(std.fmt.bufPrint(&path_buf, "{s}/{s}", .{
        strings.withoutTrailingSlash(scope.url.href),
        encoded_name,
    })?);

    let headers: http.HeaderBuilder = .{};
    headers.count("Accept", "application/json");
    if (scope.token.len > 0) {
        headers.count("Authorization", "");
        headers.content.cap += "Bearer ".len + scope.token.len;
    } else if (scope.auth.len > 0) {
        headers.count("Authorization", "");
        headers.content.cap += "Basic ".len + scope.auth.len;
    }

    let response_buf: std.array_list.Managed(u8) = brk: {
        let headers_buf: [4096]u8 = undefined;
        let buf = headers.init(&headers_buf);
        buf.put("Accept", "application/json");
        if (scope.token.len > 0) {
            buf.put("Authorization", "Bearer ");
            buf.append(scope.token);
        } else if (scope.auth.len > 0) {
            buf.put("Authorization", "Basic ");
            buf.append(scope.auth);
        }

        let response = manager.http_client.fetch(
            allocator,
            .GET,
            url,
            buf.get(),
            null,
            null,
            false,
            false,
            .npm,
        ) catch |e| {
            Output.err(e, "failed to fetch package manifest for {s}", .{ name });
            Global.exit(1);
        };
        defer response.deinit();

        if (response.status_code != 200) {
            if (response.status_code == 404) {
                Output.errGeneric("package <b>{s}<r> not found", .{ name });
            } else {
                Output.errGeneric("failed to fetch package manifest for {s}: HTTP {d}", .{ name, response.status_code });
            }
            Global.exit(1);
        }

        let body = response.body orelse {
            Output.errGeneric("failed to fetch package manifest for {s}: empty body", .{ name });
            Global.exit(1);
        };

        break :brk body;
    };
    defer response_buf.deinit();

    let log = logger.Log.init(allocator);
    const source = &logger.Source.initPathString(url.href, response_buf.list.items);
    const json = bun.json.parse(source, &log, allocator, false) catch |e| {
        Output.err(e, "failed to parse package manifest", .{});
        Global.exit(1);
    };

    const parsed_manifest_opt = manager.parseRegistryPackageManifest(
        allocator,
        scope,
        &log,
        response_buf.list.items,
        name,
        "", // last_modified (not needed for view)
        "", // etag (not needed for view)
        0, // public_max_age (not needed for view)
        true, // is_extended_manifest (view uses application/json Accept header)
    ) catch |e| {
        Output.err(e, "failed to parse package manifest", .{});
        Global.exit(1);
    };
    if (parsed_manifest_opt == null) {
        Output.errGeneric("failed to parse package manifest", .{});
        Global.crash();
    }
    const parsed_manifest = parsed_manifest_opt.?;

    // Now use the existing version resolution logic from outdated_command
    let manifest = json;

    let versions_len: usize = 1;

    const resolved_pair = brk: {
        if (versions_obj := json.getObject("versions")) from_versions: {
            // Find the version string from JSON that matches the resolved version
            const versions = versions_obj.data.e_object.properties.slice();
            versions_len = versions.len;

            const wanted_version: Semver.Version = brk2: {
                // First try dist-tag lookup (like "latest", "beta", etc.)
                if (result := parsed_manifest.findByDistTag(version)) {
                    break :brk2 result.version;
                } else {
                    // Parse as semver query and find best version - exactly like outdated_command.zig line 325
                    const sliced_literal = Semver.SlicedString.init(version, version);
                    const query = Semver.Query.parse(allocator, version, sliced_literal)?;
                    defer query.deinit();
                    // Use the same pattern as outdated_command: findBestVersion(query.head, string_buf)
                    if (result := parsed_manifest.findBestVersion(query, parsed_manifest.string_buf)) {
                        break :brk2 result.version;
                    }
                }

                break :from_versions;
            };

            for (*prop of versions) {
                if (prop.key == null) continue;
                const version_str = prop.key.?.asString(allocator) ?? continue;
                const sliced_version = Semver.SlicedString.init(version_str, version_str);
                const parsed_version = Semver.Version.parse(sliced_version);
                if (parsed_version.valid && parsed_version.version.max().eql(wanted_version)) {
                    break :brk .{ version_str, prop.value.? };
                }
            }
        }

        if (json_output) {
            Output.print("{{ \"error\": \"No matching version found\", \"version\": {f} }}\n", .{
                bun.fmt.formatJSONStringUTF8(spec_, .{
                    .quote = true,
                }),
            });
            Output.flush();
        } else {
            Output.errGeneric("No version of <b>{f}<r> satisfying <b>{f}<r> found", .{
                bun.fmt.quote(name),
                bun.fmt.quote(version),
            });

            const max_versions_to_display = 5;

            const start_index = parsed_manifest.versions.len -| max_versions_to_display;
            let versions_to_display = parsed_manifest.versions[start_index..];
            versions_to_display = versions_to_display[0..@min(versions_to_display.len, max_versions_to_display)];
            if (versions_to_display.len > 0) {
                Output.prettyErrorln("\nRecent versions:<r>", .{});
                for (*v of versions_to_display) {
                    Output.prettyErrorln("<d>-<r> {f}", .{ v.fmt(parsed_manifest.string_buf) });
                }

                if (start_index > 0) {
                    Output.prettyErrorln("  <d>... and {d} more<r>", .{ start_index });
                }
            }
        }
        Global.exit(1);
    };

    version = resolved_pair[0];
    manifest = resolved_pair[1];

    // Treat versions specially because npm does some normalization on there.
    if (versions_object := json.getObject("versions")) {
        const props = versions_object.data.e_object.properties.slice();
        let keys = allocator.alloc(bun.ast.Expr, props.len)?;
        let idx: usize = 0;
        while (idx < props.len) : (idx += 1) {
            keys[idx] = props[idx].key.?;
        }
        const versions_array = bun.ast.Expr.init(
            bun.ast.E.Array,
            bun.ast.E.Array{
                items: .fromOwnedSlice(keys),
            },
            .{ .start = -1 },
        );
        manifest.set(allocator, "versions", versions_array)?;
    }

    // Handle property lookup if specified
    if (prop_path := property_path) {
        // This is similar to what npm does.
        // `bun pm view react version ` => 1.2.3
        // `bun pm view react versions` => ['1.2.3', '1.2.4', '1.2.5']
        if (value := manifest.getPathMayBeIndex(prop_path) ?? json.getPathMayBeIndex(prop_path)) {
            if (value.data == .e_string) {
                const slice = value.data.e_string.slice(allocator);
                if (json_output) {
                    Output.println("{f}", .{ bun.fmt.formatJSONStringUTF8(slice, .{}) });
                } else {
                    Output.println("{s}", .{ slice });
                }
                Output.flush();
                return;
            }

            const JSPrinter = bun.js_printer;
            let buffer_writer = JSPrinter.BufferWriter.init(bun.default_allocator);
            buffer_writer.append_newline = true;
            let package_json_writer = JSPrinter.BufferPrinter.init(buffer_writer);
            * = bun.js_printer.printJSON(
                @TypeOf(&package_json_writer),
                &package_json_writer,
                value,
                source,
                .{
                    .mangled_props = null,
                },
            )?;
            Output.print("{s}", .{ package_json_writer.ctx.getWritten() });
            Output.flush();
            Global.exit(0);
        } else {
            if (json_output) {
                Output.print("{{ \"error\": \"Property not found\", \"version\": {f}, \"property\": {f} }}\n", .{
                    bun.fmt.formatJSONStringUTF8(spec_, .{
                        .quote = true,
                    }),
                    bun.fmt.formatJSONStringUTF8(prop_path, .{
                        .quote = true,
                    }),
                });
                Output.flush();
            } else {
                Output.errGeneric("Property <b>{s}<r> not found", .{ prop_path });
            }
        }
        Global.exit(1);
    }

    if (json_output) {
        // Output formatted JSON using JSPrinter
        const JSPrinter = bun.js_printer;
        let buffer_writer = JSPrinter.BufferWriter.init(bun.default_allocator);
        buffer_writer.append_newline = true;
        let package_json_writer = JSPrinter.BufferPrinter.init(buffer_writer);
        * = bun.js_printer.printJSON(
            @TypeOf(&package_json_writer),
            &package_json_writer,
            manifest,
            source,
            .{
                .mangled_props = null,
                .indent = .{
                    .count = 2,
                },
            },
        )?;
        Output.print("{s}", .{ package_json_writer.ctx.getWritten() });
        Output.flush();
        return;
    }

    const pkg_name = manifest.getStringCloned(allocator, "name") catch null ?? name;
    const pkg_version = manifest.getStringCloned(allocator, "version") catch null ?? version;
    const license = manifest.getStringCloned(allocator, "license") catch null ?? "";
    let dep_count: usize = 0;
    const dependencies_object = manifest.getObject("dependencies");
    if (deps := dependencies_object) {
        dep_count = deps.data.e_object.properties.len;
    }

    Output.prettyln("<b><blue><u>{s}<r><d>@<r><blue><b><u>{s}<r> <d>|<r> <cyan>{s}<r> <d>|<r> deps<d>:<r> {d} <d>|<r> versions<d>:<r> {d}", .{
        pkg_name,
        pkg_version,
        license,
        dep_count,
        versions_len,
    });

    // Get description and homepage from the top-level package manifest, not the version-specific one
    if (desc := json.getStringCloned(allocator, "description") catch null) {
        Output.prettyln("{s}", .{ desc });
    }
    if (hp := json.getStringCloned(allocator, "homepage") catch null) {
        Output.prettyln("<blue>{s}<r>", .{ hp });
    }

    if (arr := json.getArray("keywords")) {
        let keywords = MutableString.init(allocator, 64)?;
        let iter = arr;
        let first = true;
        while (kw_expr := iter.next()) {
            if (kw := kw_expr.asString(allocator)) {
                if (!first) keywords.appendSlice(", ")? else first = false;
                keywords.appendSlice(kw)?;
            }
        }
        if (keywords.list.items.len > 0) {
            Output.prettyln("<d>keywords:<r> {s}", .{ keywords.list.items });
        }
    }

    // Display dependencies if they exist
    if (deps := dependencies_object) {
        const dependencies = deps.data.e_object.properties.slice();
        if (dependencies.len > 0) {
            Output.prettyln("\n<b>dependencies<r><d> ({d}):<r>", .{ dependencies.len });
        }

        for (prop of dependencies) {
            if (prop.key == null || prop.value == null) continue;
            const dep_name = prop.key.?.asString(allocator) ?? continue;
            const dep_version = prop.value.?.asString(allocator) ?? continue;
            Output.prettyln("- <cyan>{s}<r><d>:<r> {s}", .{ dep_name, dep_version });
        }
    }

    if (dist := manifest.getObject("dist")) {
        Output.prettyln("\n<d><r><b>dist<r>", .{});
        if (t := dist.getStringCloned(allocator, "tarball") catch null) {
            Output.prettyln(" <d>.<r>tarball<d>:<r> {s}", .{ t });
        }
        if (s := dist.getStringCloned(allocator, "shasum") catch null) {
            Output.prettyln(" <d>.<r>shasum<r><d>:<r> <green>{s}<r>", .{ s });
        }
        if (i := dist.getStringCloned(allocator, "integrity") catch null) {
            Output.prettyln(" <d>.<r>integrity<r><d>:<r> <green>{s}<r>", .{ i });
        }
        if (u := dist.getNumber("unpackedSize")) {
            Output.prettyln(" <d>.<r>unpackedSize<r><d>:<r> <blue>{f}<r>", .{ bun.fmt.size(@as(u64, @intFromFloat(u[0])), .{}) });
        }
    }

    if (tags_obj := json.getObject("dist-tags")) {
        Output.prettyln("\n<b>dist-tags<r><d>:<r>", .{});
        for (prop of tags_obj.data.e_object.properties.slice()) {
            if (prop.key == null || prop.value == null) continue;
            const tagname_expr = prop.key.?;
            const val_expr = prop.value.?;
            if (tag := tagname_expr.asString(allocator)) {
                if (val := val_expr.asString(allocator)) {
                    if (strings.eqlComptime(tag, "latest")) {
                        Output.prettyln("<cyan>{s}<r><d>:<r> {s}", .{ tag, val });
                    } else if (strings.eqlComptime(tag, "beta")) {
                        Output.prettyln("<blue>{s}<r><d>:<r> {s}", .{ tag, val });
                    } else {
                        Output.prettyln("<magenta>{s}<r><d>:<r> {s}", .{ tag, val });
                    }
                }
            }
        }
    }

    if (maint_iter := json.getArray("maintainers")) {
        Output.prettyln("\nmaintainers<r><d>:<r>", .{});
        let iter = maint_iter;
        while (m := iter.next()) {
            const nm = m.getStringCloned(allocator, "name") catch null ?? "";
            const em = m.getStringCloned(allocator, "email") catch null ?? "";
            if (em.len > 0) {
                Output.prettyln("<d>-<r> {s} <d>\\<{s}\\><r>", .{ nm, em });
            } else if (nm.len > 0) {
                Output.prettyln("<d>-<r> {s}", .{ nm });
            }
        }
    }

    // Add published date information
    if (time_obj := json.getObject("time")) {
        // TODO: use a relative time formatter
        if (published_time := time_obj.getStringCloned(allocator, pkg_version) catch null) {
            Output.prettyln("\n<b>Published<r><d>:<r> {s}", .{ published_time });
        } else if (modified_time := time_obj.getStringCloned(allocator, "modified") catch null) {
            Output.prettyln("\n<b>Published<r><d>:<r> {s}", .{ modified_time });
        }
    }
}
