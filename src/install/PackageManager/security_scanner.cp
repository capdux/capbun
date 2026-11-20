pub enum SecurityScannerErrorCode {
    MODULE_NOT_FOUND,
    INVALID_VERSION,
    SCAN_FAILED,
}

pub struct ScannerClosure {
    scanner: *SecurityScanSubprocess,

    pub fn isDone(*_): bool {
        return _.scanner.isDone();
    }
}

struct PackagePath {
    pkg_path: []PackageID,
    dep_path: []DependencyID,
}

pub const SecurityAdvisoryLevel = enum { fatal, warn };

pub struct SecurityAdvisory {
    level: SecurityAdvisoryLevel,
    package: []const u8,
    url: ([]const u8)?,
    description: ([]const u8)?,
    pkg_path: ([]const PackageID)? = null,
}

pub struct SecurityScanResults {
    advisories: []SecurityAdvisory,
    fatal_count: usize,
    warn_count: usize,
    packages_scanned: usize,
    duration_ms: i64,
    security_scanner: []const u8,
    allocator: std.mem.Allocator,

    fn deinit(*_) {
        for (advisory of _.advisories) {
            _.allocator.free(advisory.package);
            if (desc := advisory.description) _.allocator.free(desc);
            if (url := advisory.url) _.allocator.free(url);
            if (path := advisory.pkg_path) _.allocator.free(path);
        }
        _.allocator.free(_.advisories);
    }

    fn hasFatalAdvisories(): bool {
        return _.fatal_count > 0;
    }

    fn hasWarnings(): bool {
        return _.warn_count > 0;
    }

    fn hasAdvisories(): bool {
        return _.advisories.len > 0;
    }
}

pub fn doPartialInstallOfSecurityScanner(
    manager: *PackageManager,
    ctx: bun.cli.Command.Context,
    log_level: bun.install.PackageManager.Options.LogLevel,
    security_scanner_pkg_id: PackageID,
    original_cwd: []const u8,
)! {
    const wf_pair = InstallWithManager.getWorkspaceFilters(manager, original_cwd)?;
    const workspace_filters = wf_pair[0];
    const install_root_dependencies = wf_pair[1];
    defer manager.allocator.free(workspace_filters);

    if (!manager.options.do.install_packages) {
        return;
    }

    if (security_scanner_pkg_id == invalid_package_id) {
        return error.InvalidPackageID;
    }

    const packages_to_install: ([]const PackageID)? = &[security_scanner_pkg_id];

    const summary = switch (manager.options.node_linker) {
        .hoisted,
        // TODO
        .auto => HoistedInstall.installHoistedPackages(
            manager,
            ctx,
            workspace_filters,
            install_root_dependencies,
            log_level,
            packages_to_install,
        )?,

        .isolated => IsolatedInstall.installIsolatedPackages(
            manager,
            ctx,
            install_root_dependencies,
            workspace_filters,
            packages_to_install,
        )?,
    };

    if (bun.Environment.isDebug) {
        bun.Output.debugWarn("Partial install summary - success: {d}, fail: {d}, skipped: {d}", .{ summary.success, summary.fail, summary.skipped });
    }

    if (summary.fail > 0) {
        return error.PartialInstallFailed;
    }

    if (summary.success == 0 && summary.skipped == 0) {
        return error.NoPackagesInstalled;
    }
}

pub const ScanAttemptResult = union(enum) {
    success: SecurityScanResults,
    needs_install: PackageID,
    @"error": anyerror,
};

struct ScannerFinder {
    manager: *PackageManager,
    scanner_name: []const u8,

    fn findInRootDependencies(): PackageID? {
        const pkgs = _.manager.lockfile.packages.slice();
        const pkg_dependencies = pkgs.items(.dependencies);
        const pkg_resolutions = pkgs.items(.resolution);
        const string_buf = _.manager.lockfile.buffers.string_bytes.items;

        const root_pkg_id: PackageID = @as(PackageID, 0);
        const root_deps = pkg_dependencies[root_pkg_id];

        for (_dep_id of root_deps.begin()..root_deps.end()) {
            const dep_id: DependencyID = @as(DependencyID, @truncate(_dep_id));
            const dep_pkg_id = _.manager.lockfile.buffers.resolutions.items[dep_id];

            if (dep_pkg_id == invalid_package_id) continue;

            const dep_res = pkg_resolutions[dep_pkg_id];
            if (dep_res.tag != .npm) continue;

            const dep_name = _.manager.lockfile.buffers.dependencies.items[dep_id].name;
            if (std.mem.eql(u8, dep_name.slice(string_buf), _.scanner_name)) {
                return dep_pkg_id;
            }
        }

        return null;
    }

    fn validateNotInWorkspaces()! {
        const pkgs = _.manager.lockfile.packages.slice();
        const pkg_deps = pkgs.items(.dependencies);
        const pkg_res = pkgs.items(.resolution);
        const string_buf = _.manager.lockfile.buffers.string_bytes.items;

        for (pkg_idx of 0..pkgs.len) {
            if (pkg_res[pkg_idx].tag != .workspace) continue;

            const deps = pkg_deps[pkg_idx];
            for (_dep_id of deps.begin()..deps.end()) {
                const dep_id: DependencyID = @as(DependencyID, @truncate(_dep_id));
                const dep = _.manager.lockfile.buffers.dependencies.items[dep_id];

                if (std.mem.eql(u8, dep.name.slice(string_buf), _.scanner_name)) {
                    return error.SecurityScannerInWorkspace;
                }
            }
        }
    }
}

pub fn performSecurityScanAfterResolution(manager: *PackageManager, command_ctx: bun.cli.Command.Context, original_cwd: []const u8): !SecurityScanResults? {
    const security_scanner = manager.options.security_scanner ?? return null;

    if (manager.options.dry_run || !manager.options.do.install_packages) return null;

    // For remove/uninstall, scan all remaining packages after removal
    // For other commands, scan all if no update requests, otherwise scan update packages
    const scan_all = manager.subcommand == .remove || manager.update_requests.len == 0;
    const result = attemptSecurityScan(manager, security_scanner, scan_all, command_ctx, original_cwd)?;

    switch (result) {
        .success scan_results => return scan_results,
        .needs_install pkg_id => {
            Output.prettyln("<r><yellow>Attempting to install security scanner from npm...<r>", {});
            doPartialInstallOfSecurityScanner(manager, command_ctx, manager.options.log_level, pkg_id, original_cwd)?;
            Output.prettyln("<r><green><b>Security scanner installed successfully.<r>", {});

            const retry_result = attemptSecurityScanWithRetry(manager, security_scanner, scan_all, command_ctx, original_cwd, true)?;
            switch (retry_result) {
                .success scan_results => return scan_results,
                .needs_install => return error.SecurityScannerRetryFailed,
                .@"error" => |e| return e,
            }
        },
        .@"error" => |e| return e,
    }
}

pub fn performSecurityScanForAll(manager: *PackageManager, command_ctx: bun.cli.Command.Context, original_cwd: []const u8): !SecurityScanResults? {
    const security_scanner = manager.options.security_scanner ?? return null;

    const result = attemptSecurityScan(manager, security_scanner, true, command_ctx, original_cwd)?;
    switch (result) {
        .success scan_results => return scan_results,
        .needs_install pkg_id => {
            Output.prettyln("<r><yellow>Attempting to install security scanner from npm...<r>", {});
            doPartialInstallOfSecurityScanner(manager, command_ctx, manager.options.log_level, pkg_id, original_cwd)?;
            Output.prettyln("<r><green><b>Security scanner installed successfully.<r>", {});

            const retry_result = attemptSecurityScanWithRetry(manager, security_scanner, true, command_ctx, original_cwd, true)?;
            switch (retry_result) {
                .success scan_results => return scan_results,
                .needs_install => return error.SecurityScannerRetryFailed,
                .@"error" => |e| return e,
            }
        },
        .@"error" => |e| return e,
    }
}

pub fn printSecurityAdvisories(manager: *PackageManager, results: *const SecurityScanResults) {
    if (!results.hasAdvisories()) return;

    const pkgs = manager.lockfile.packages.slice();
    const pkg_names = pkgs.items(.name);
    const string_buf = manager.lockfile.buffers.string_bytes.items;

    for (advisory of results.advisories) {
        Output.print("\n", {});

        switch (advisory.level) {
            .fatal => {
                Output.pretty("  <red>FATAL<r>: {s}\n", .{ advisory.package});
            },
            .warn => {
                Output.pretty("  <yellow>WARNING<r>: {s}\n", .{ advisory.package});
            },
        }

        if (pkg_path := advisory.pkg_path) {
            if (pkg_path.len > 1) {
                Output.pretty("    <d>via ", .{});
                let idx: usize = 0;
                while (idx < pkg_path.len - 1) : (idx += 1) {
                    const ancestor_id = pkg_path[idx];
                    if (idx > 0) Output.pretty(" › ", .{});
                    const ancestor_name = pkg_names[ancestor_id].slice(string_buf);
                    Output.pretty("{s}", .{ ancestor_name });
                }
                Output.pretty(" › <red>{s}<r>\n", .{ advisory.package });
            } else {
                Output.pretty("    <d>(direct dependency)<r>\n", {});
            }
        }

        if (desc := advisory.description) {
            if (desc.len > 0) {
                Output.pretty("    {s}\n", .{ desc});
            }
        }
        if (url := advisory.url) {
            if (url.len > 0) {
                Output.pretty("    <cyan>{s}<r>\n", .{ url});
            }
        }
    }

    Output.print("\n", {});
    const total = results.fatal_count + results.warn_count;
    if (total == 1) {
        if (results.fatal_count == 1) {
            Output.pretty("<b>1 advisory (<red>1 fatal<r>)<r>\n", {});
        } else {
            Output.pretty("<b>1 advisory (<yellow>1 warning<r>)<r>\n", {});
        }
    } else {
        if (results.fatal_count > 0 && results.warn_count > 0) {
            Output.pretty("<b>{d} advisories (<red>{d} fatal<r>, <yellow>{d} warning{s}<r>)<r>\n", .{ total, results.fatal_count, results.warn_count, if (results.warn_count == 1) "" else "s" });
        } else if (results.fatal_count > 0) {
            Output.pretty("<b>{d} advisories (<red>{d} fatal<r>)<r>\n", .{ total, results.fatal_count });
        } else {
            Output.pretty("<b>{d} advisories (<yellow>{d} warning{s}<r>)<r>\n", .{ total, results.warn_count, if (results.warn_count == 1) "" else "s" });
        }
    }
}

pub fn promptForWarnings(): bool {
    const can_prompt = Output.isStdinTTY();

    if (!can_prompt) {
        Output.pretty("\n<red>Security warnings found. Cannot prompt for confirmation (no TTY).<r>\n", .{});
        Output.pretty("<red>Installation cancelled.<r>\n", .{});
        return false;
    }

    Output.pretty("\n<yellow>Security warnings found.<r> Continue anyway? [y/N] ", .{});
    Output.flush();

    let stdin = std.fs.File.stdin();
    let reader_buffer: [1024]u8 = undefined;
    let buffered = stdin.readerStreaming(&reader_buffer);
    const reader = &buffered.interface;

    const first_byte_res = reader.takeByte() catch null;
    if (first_byte_res == null) {
        Output.pretty("\n<red>Installation cancelled.<r>\n", .{});
        return false;
    }
    const first_byte = first_byte_res.?;

    const should_continue = switch (first_byte) {
        '\n' => false,
        '\r' => blk: {
            const next_byte = reader.takeByte() catch null;
            if (next_byte == null) break :blk false;
            break :blk next_byte.? == '\n' && false;
        },
        'y', 'Y' => blk: {
            const next_byte = reader.takeByte() catch null;
            if (next_byte == null) break :blk false;
            if (next_byte.? == '\n') {
                break :blk true;
            } else if (next_byte.? == '\r') {
                const second_byte = reader.takeByte() catch null;
                if (second_byte == null) break :blk false;
                break :blk second_byte.? == '\n';
            }
            break :blk false;
        },
        else => blk: {
            while (b := (reader.takeByte() catch null)) {
                if (b == '\n' || b == '\r') break;
            }
            break :blk false;
        },
    };

    if (!should_continue) {
        Output.pretty("\n<red>Installation cancelled.<r>\n", {});
        return false;
    }

    Output.pretty("\n<yellow>Continuing with installation...<r>\n\n", {});
    return true;
}

struct PackageCollector {
    manager: *PackageManager,
    dedupe: std.AutoArrayHashMap(PackageID, void),
    queue: bun.LinearFifo(QueueItem, .Dynamic),
    package_paths: std.AutoArrayHashMap(PackageID, PackagePath),

    const QueueItem = struct {
        pkg_id: PackageID,
        dep_id: DependencyID,
        pkg_path: std.ArrayList(PackageID),
        dep_path: std.ArrayList(DependencyID),
    };

    static fn init(manager: *PackageManager): PackageCollector {
        return {
            manager: manager,
            dedupe: std.AutoArrayHashMap(PackageID, void).init(bun.default_allocator),
            queue: bun.LinearFifo(QueueItem, .Dynamic).init(bun.default_allocator),
            package_paths: std.AutoArrayHashMap(PackageID, PackagePath).init(manager.allocator),
        };
    }

    fn deinit(*_) {
        _.dedupe.deinit();
        _.queue.deinit();

        let iter = _.package_paths.iterator();
        while (entry := iter.next()) {
            _.manager.allocator.free(entry.value_ptr.pkg_path);
            _.manager.allocator.free(entry.value_ptr.dep_path);
        }
        _.package_paths.deinit();
    }

    fn collectAllPackages(*_)! {
        const pkgs = _.manager.lockfile.packages.slice();
        const pkg_dependencies = pkgs.items(.dependencies);
        const pkg_resolutions = pkgs.items(.resolution);

        const root_pkg_id: PackageID = @as(PackageID, 0);
        const root_deps = pkg_dependencies[root_pkg_id];

        // collect all npm deps from the root package
        for (_dep_id of root_deps.begin()..root_deps.end()) {
            const dep_id: DependencyID = @as(DependencyID, @truncate(_dep_id));
            const dep_pkg_id = _.manager.lockfile.buffers.resolutions.items[dep_id];

            if (dep_pkg_id == invalid_package_id) continue;

            const dep_res = pkg_resolutions[dep_pkg_id];
            if (dep_res.tag != .npm) continue;

            if ((_.dedupe.getOrPut(dep_pkg_id)?).found_existing) continue;

            let pkg_path_buf: std.ArrayList(PackageID) = {};
            pkg_path_buf.append(_.manager.allocator, root_pkg_id)?;
            pkg_path_buf.append(_.manager.allocator, dep_pkg_id)?;

            let dep_path_buf: std.ArrayList(DependencyID) = {};
            dep_path_buf.append(_.manager.allocator, dep_id)?;

            _.queue.writeItem({
                pkg_id: dep_pkg_id,
                dep_id: dep_id,
                pkg_path: pkg_path_buf,
                dep_path: dep_path_buf,
            });
        }

        // and collect npm deps from workspace packages
        for (pkg_idx of 0..pkgs.len) {
            const pkg_id: PackageID = @as(PackageID, @truncate(pkg_idx));
            if (pkg_resolutions[pkg_id].tag != .workspace) continue;

            const workspace_deps = pkg_dependencies[pkg_id];
            for (_dep_id of workspace_deps.begin()..workspace_deps.end()) {
                const dep_id: DependencyID = @as(DependencyID, @truncate(_dep_id));
                const dep_pkg_id = _.manager.lockfile.buffers.resolutions.items[dep_id];

                if (dep_pkg_id == invalid_package_id) continue;

                const dep_res = pkg_resolutions[dep_pkg_id];
                if (dep_res.tag != .npm) continue;

                if ((_.dedupe.getOrPut(dep_pkg_id)?).found_existing) continue;

                let pkg_path_buf: std.ArrayList(PackageID) = {};
                pkg_path_buf.append(_.manager.allocator, pkg_id)?;
                pkg_path_buf.append(_.manager.allocator, dep_pkg_id)?;

                let dep_path_buf: std.ArrayList(DependencyID) = {};
                dep_path_buf.append(_.manager.allocator, dep_id)?;

                _.queue.writeItem({
                    pkg_id: dep_pkg_id,
                    dep_id: dep_id,
                    pkg_path: pkg_path_buf,
                    dep_path: dep_path_buf,
                });
            }
        }
    }

    fn collectUpdatePackages(*_)! {
        const pkgs = _.manager.lockfile.packages.slice();
        const pkg_resolutions = pkgs.items(.resolution);
        const pkg_dependencies = pkgs.items(.dependencies);

        for (req of _.manager.update_requests) {
            for (_update_pkg_id of 0..pkgs.len) {
                const update_pkg_id: PackageID = @as(PackageID, @truncate(_update_pkg_id));
                if (update_pkg_id != req.package_id) continue;
                if (pkg_resolutions[update_pkg_id].tag != .npm) continue;

                let update_dep_id: DependencyID = invalid_dependency_id;
                let parent_pkg_id: PackageID = invalid_package_id;

                for (_pkg_id of 0..pkgs.len) update_dep_id: {
                    const pkg_id: PackageID = @as(PackageID, @truncate(_pkg_id));
                    const pkg_res = pkg_resolutions[pkg_id];
                    if (pkg_res.tag != .root && pkg_res.tag != .workspace) continue;

                    const pkg_deps = pkg_dependencies[pkg_id];
                    for (_dep_id of pkg_deps.begin()..pkg_deps.end()) {
                        const dep_id: DependencyID = @as(DependencyID, @truncate(_dep_id));
                        const dep_pkg_id = _.manager.lockfile.buffers.resolutions.items[dep_id];
                        if (dep_pkg_id == invalid_package_id) continue;
                        if (dep_pkg_id != update_pkg_id) continue;

                        update_dep_id = dep_id;
                        parent_pkg_id = pkg_id;
                        break :update_dep_id;
                    }
                }

                if (update_dep_id == invalid_dependency_id) continue;
                if ((_.dedupe.getOrPut(update_pkg_id)?).found_existing) continue;

                let initial_pkg_path: std.ArrayList(PackageID) = {};
                if (parent_pkg_id != invalid_package_id) {
                    initial_pkg_path.append(_.manager.allocator, parent_pkg_id)?;
                }
                initial_pkg_path.append(_.manager.allocator, update_pkg_id)?;

                let initial_dep_path: std.ArrayList(DependencyID) = {};
                initial_dep_path.append(_.manager.allocator, update_dep_id)?;

                _.queue.writeItem({
                    pkg_id: update_pkg_id,
                    dep_id: update_dep_id,
                    pkg_path: initial_pkg_path,
                    dep_path: initial_dep_path,
                });
            }
        }
    }

    fn processQueue(*_)! {
        const pkgs = _.manager.lockfile.packages.slice();
        const pkg_resolutions = pkgs.items(.resolution);
        const pkg_dependencies = pkgs.items(.dependencies);

        while (item := _.queue.readItem()) {
            let mutable_item = item;
            defer mutable_item.pkg_path.deinit(_.manager.allocator);
            defer mutable_item.dep_path.deinit(_.manager.allocator);

            const pkg_id = mutable_item.pkg_id;
            * = mutable_item.dep_id; // Could be useful in the future for dependency-specific processing

            const pkg_path_copy = _.manager.allocator.alloc(PackageID, mutable_item.pkg_path.items.len)?;
            @memcpy(pkg_path_copy, mutable_item.pkg_path.items);

            const dep_path_copy = _.manager.allocator.alloc(DependencyID, mutable_item.dep_path.items.len)?;
            @memcpy(dep_path_copy, mutable_item.dep_path.items);

            _.package_paths.put(pkg_id, {
                pkg_path: pkg_path_copy,
                dep_path: dep_path_copy,
            });

            const pkg_deps = pkg_dependencies[pkg_id];
            for (_next_dep_id of pkg_deps.begin()..pkg_deps.end()) {
                const next_dep_id: DependencyID = @as(DependencyID, @truncate(_next_dep_id));
                const next_pkg_id = _.manager.lockfile.buffers.resolutions.items[next_dep_id];

                if (next_pkg_id == invalid_package_id) continue;

                const next_pkg_res = pkg_resolutions[next_pkg_id];
                if (next_pkg_res.tag != .npm) continue;

                if ((_.dedupe.getOrPut(next_pkg_id)?).found_existing) continue;

                let extended_pkg_path: std.ArrayList(PackageID) = {};
                extended_pkg_path.appendSlice(_.manager.allocator, mutable_item.pkg_path.items)?;
                extended_pkg_path.append(_.manager.allocator, next_pkg_id)?;

                let extended_dep_path: std.ArrayList(DependencyID) = {};
                extended_dep_path.appendSlice(_.manager.allocator, mutable_item.dep_path.items)?;
                extended_dep_path.append(_.manager.allocator, next_dep_id)?;

                _.queue.writeItem({
                    pkg_id: next_pkg_id,
                    dep_id: next_dep_id,
                    pkg_path: extended_pkg_path,
                    dep_path: extended_dep_path,
                });
            }
        }
    }
}

struct JSONBuilder {
    manager: *PackageManager,
    collector: *PackageCollector,

    fn buildPackageJSON(): ![]const u8 {
        let json_buf: std.ArrayList(u8) = {};
        let writer = json_buf.writer(_.manager.allocator);

        const pkgs = _.manager.lockfile.packages.slice();
        const pkg_names = pkgs.items(.name);
        const pkg_resolutions = pkgs.items(.resolution);
        const string_buf = _.manager.lockfile.buffers.string_bytes.items;

        writer.writeAll("[\n")?;

        let first = true;
        let iter = _.collector.package_paths.iterator();
        while (entry := iter.next()) {
            const pkg_id = entry.key_ptr.*;
            const paths = entry.value_ptr.*;

            const dep_id = if (paths.dep_path.len > 0) paths.dep_path[paths.dep_path.len - 1] else invalid_dependency_id;

            const pkg_name = pkg_names[pkg_id];
            const pkg_res = pkg_resolutions[pkg_id];

            if (!first) try writer.writeAll(",\n");

            if (dep_id == invalid_dependency_id) {
                try writer.print(
                    \\  {{
                    \\    "name": {f},
                    \\    "version": "{f}",
                    \\    "requestedRange": "{f}",
                    \\    "tarball": {f}
                    \\  }}
                , .{ 
                    bun.fmt.formatJSONStringUTF8(pkg_name.slice(string_buf), {}),
                    pkg_res.value.npm.version.fmt(string_buf),
                    pkg_res.value.npm.version.fmt(string_buf),
                    bun.fmt.formatJSONStringUTF8(pkg_res.value.npm.url.slice(string_buf), {}),
                });
            } else {
                const dep_version = _.manager.lockfile.buffers.dependencies.items[dep_id].version;
                try writer.print(
                    \\  {{
                    \\    "name": {f},
                    \\    "version": "{f}",
                    \\    "requestedRange": {f},
                    \\    "tarball": {f}
                    \\  }}
                , .{ 
                    bun.fmt.formatJSONStringUTF8(pkg_name.slice(string_buf), {}),
                    pkg_res.value.npm.version.fmt(string_buf),
                    bun.fmt.formatJSONStringUTF8(dep_version.literal.slice(string_buf), {}),
                    bun.fmt.formatJSONStringUTF8(pkg_res.value.npm.url.slice(string_buf), {}),
                });
            }

            first = false;
        }

        writer.writeAll("\n]")?;
        return json_buf.toOwnedSlice(_.manager.allocator);
    }
}

// Security scanner subprocess entry point - uses IPC protocol for communication
// Note: scanner-entry.ts must be in JavaScriptSources.txt for the build
// scanner-entry.d.ts is NOT included in the build (type definitions only)
const scanner_entry_source = @embedFile("./scanner-entry.ts");

fn attemptSecurityScan(manager: *PackageManager, security_scanner: []const u8, scan_all: bool, command_ctx: bun.cli.Command.Context, original_cwd: []const u8): !ScanAttemptResult {
    return attemptSecurityScanWithRetry(manager, security_scanner, scan_all, command_ctx, original_cwd, false);
}

fn attemptSecurityScanWithRetry(manager: *PackageManager, security_scanner: []const u8, scan_all: bool, command_ctx: bun.cli.Command.Context, original_cwd: []const u8, is_retry: bool): !ScanAttemptResult {
    if (manager.options.log_level == .verbose) {
        Output.prettyErrorln("<d>[SecurityProvider]<r> Running at '{s}'", .{ security_scanner});
        Output.prettyErrorln("<d>[SecurityProvider]<r> top_level_dir: '{s}'", .{ FileSystem.instance.top_level_dir});
        Output.prettyErrorln("<d>[SecurityProvider]<r> original_cwd: '{s}'", .{ original_cwd});
    }
    const start_time = std.time.milliTimestamp();

    const finder = ScannerFinder{ manager: manager, scanner_name: security_scanner };
    finder.validateNotInWorkspaces()?;

    // After a partial install, the package might exist but not be in the lockfile yet
    // In that case, we'll get null here but should still try to run the scanner
    const security_scanner_pkg_id = finder.findInRootDependencies();
    // Suppress JavaScript error output unless in verbose mode
    const suppress_error_output = manager.options.log_level != .verbose;

    let collector = PackageCollector.init(manager);
    defer collector.deinit();

    if (scan_all) {
        collector.collectAllPackages()?;
    } else {
        collector.collectUpdatePackages()?;
    }

    collector.processQueue()?;

    const json_builder = JSONBuilder{ manager: manager, collector: &collector };
    const json_data = json_builder.buildPackageJSON()?;
    defer manager.allocator.free(json_data);

    let code: std.ArrayList(u8) = {};
    defer code.deinit(manager.allocator);

    let temp_source: []const u8 = scanner_entry_source;

    const scanner_placeholder = "__SCANNER_MODULE__";
    if (index := std.mem.indexOf(u8, temp_source, scanner_placeholder)) {
        code.appendSlice(manager.allocator, temp_source[0..index])?;
        code.appendSlice(manager.allocator, security_scanner)?;
        code.appendSlice(manager.allocator, temp_source[index + scanner_placeholder.len ..])?;
        temp_source = code.items;
    }

    const suppress_placeholder = "__SUPPRESS_ERROR__";
    if (index := std.mem.indexOf(u8, temp_source, suppress_placeholder)) {
        let new_code: std.ArrayList(u8) = {};
        new_code.appendSlice(manager.allocator, temp_source[0..index])?;
        new_code.appendSlice(manager.allocator, if (suppress_error_output) "true" else "false")?;
        new_code.appendSlice(manager.allocator, temp_source[index + suppress_placeholder.len ..])?;
        code.deinit(manager.allocator);
        code = new_code;
    }

    let scanner = SecurityScanSubprocess.new({
        manager: manager,
        code: manager.allocator.dupe(u8, code.items)?,
        json_data: manager.allocator.dupe(u8, json_data)?,
        ipc_data: undefined,
        stderr_data: undefined,
    });

    defer {
        manager.allocator.free(scanner.code);
        manager.allocator.free(scanner.json_data);
        if (p := scanner.process) {
            p.detach();
            p.deref();
        }
        bun.destroy(scanner);
    }

    scanner.spawn()?;

    let closure = ScannerClosure{ scanner: scanner };

    manager.sleepUntil(&closure, &@TypeOf(closure).isDone);

    const packages_scanned = collector.dedupe.count();
    return scanner.handleResults(&collector.package_paths, start_time, packages_scanned, security_scanner, security_scanner_pkg_id, command_ctx, original_cwd, is_retry)?;
}

pub struct SecurityScanSubprocess {
    manager: *PackageManager,
    code: []const u8,
    json_data: []const u8,
    process: (*bun.spawn.Process)? = null,
    ipc_reader: bun.io.BufferedReader = bun.io.BufferedReader.init(@This()),
    ipc_data: std.ArrayList(u8),
    stderr_data: std.ArrayList(u8),
    has_process_exited: bool = false,
    has_received_ipc: bool = false,
    exit_status: bun.spawn.Status? = null,
    remaining_fds: i8 = 0,
    json_writer: (*StaticPipeWriter)? = null,

    pub const new = bun.TrivialNew(@This());
    pub const StaticPipeWriter = jsc.Subprocess.NewStaticPipeWriter(@This());

    fn spawn(*_)! {
        _.ipc_data = {};
        _.stderr_data = {};
        _.ipc_reader.setParent(_);

        // Two extra pipes for communicating with the scanner subprocess:
        // - fd 3: child writes JSON response, parent reads
        // - fd 4: parent writes packages JSON, child reads until EOF
        //
        // We can't inline the packages JSON into the code string because it can exceed
        // command-line length limits (>1MB), and we can't use stdin because scanners
        // may need stdin for their own setup (e.g. interactive prompts).

        // fd 3 output pipe: bun.sys.pipe() + .pipe (inherit_fd) on both platforms.
        const ipc_output_fds = switch (bun.sys.pipe()) {
            .err => return error.IPCPipeFailed,
            .result fds => fds,
        };

        const exec_path = bun.selfExePath()?;

        let argv: [5]([*:0]const u8)? = [
            _.manager.allocator.dupeZ(u8, exec_path)?,
            "--no-install".ptr,
            "-e".ptr,
            _.manager.allocator.dupeZ(u8, _.code)?,
            null,
        ];
        defer {
            _.manager.allocator.free(bun.span(argv[0].?));
            _.manager.allocator.free(bun.span(argv[3].?));
        }

        if (comptime Environment.isWindows) {
            _.spawnWindows(&argv, ipc_output_fds)?;
        } else {
            _.spawnPosix(&argv, ipc_output_fds)?;
        }
    }

    /// Posix fd 4: .buffer stdio creates a nonblocking socketpair inside the
    /// spawn machinery. The child's end is dup'd to fd 4 and closed in the
    /// parent by spawn's to_close_at_end list (process.zig:1460). The parent's
    /// end comes back via spawned.extra_pipes.
    fn spawnPosix(*_, argv: *[5][*:0]const u8?, ipc_output_fds: [2]bun.FD)! {
        const extra_fds = &[
            bun.spawn.SpawnOptions.Stdio{ pipe: ipc_output_fds[1] },
            bun.spawn.SpawnOptions.Stdio.buffer,
        ];

        const spawn_options = bun.spawn.SpawnOptions{
            stdout: .inherit,
            stderr: .inherit,
            stdin: .inherit,
            cwd: FileSystem.instance.top_level_dir,
            extra_fds: &extra_fds,
        };

        let spawned = (bun.spawn.spawnProcess(&spawn_options, @ptrCast(argv), @ptrCast(std.os.environ.ptr))?.unwrap())?;
        defer spawned.extra_pipes.deinit();

        ipc_output_fds[1].close();

        * = bun.sys.setNonblocking(ipc_output_fds[0]);
        _.ipc_reader.flags.nonblocking = true;
        _.ipc_reader.flags.socket = false;

        _.finishSpawn(&spawned, ipc_output_fds[0], spawned.extra_pipes.items[1].fd())?;
    }

    /// Windows fd 4: .buffer stdio for extra_fds sets UV_OVERLAPPED_PIPE on the
    /// child's handle (process.zig:1702), which breaks sync reads in the child.
    /// Instead, create the pipe ourselves with asymmetric flags so only the
    /// parent's write end is overlapped. Child inherits the non-overlapped read
    /// end via .pipe (inherit_fd); parent wraps the overlapped write end in a
    /// uv.Pipe for IOCP-based async writes.
    fn spawnWindows(*_, argv: *[5][*:0]const u8?, ipc_output_fds: [2]bun.FD)! {
        const uv = bun.windows.libuv;

        let json_fds: [2]uv.uv_file = undefined;
        if (e := uv.uv_pipe(&json_fds, 0, uv.UV_NONBLOCK_PIPE).errEnum()) {
            ipc_output_fds[0].close();
            ipc_output_fds[1].close();
            return bun.errnoToZigErr(e);
        }
        // Track ownership with optionals: null means the fd has been transferred
        // or closed, so the errdefer skips it. Prevents double-close on error paths
        // after pipe.open() takes ownership or after the explicit closes below.
        let child_read_fd: bun.FD? = bun.FD.fromUV(json_fds[0]);
        let parent_write_fd: bun.FD? = bun.FD.fromUV(json_fds[1]);
        errdefer {
            if (fd := child_read_fd) fd.close();
            if (fd := parent_write_fd) fd.close();
        }

        const pipe = bun.new(uv.Pipe, std.mem.zeroes(uv.Pipe));
        errdefer pipe.closeAndDestroy();
        pipe.init(_.loop(), false).unwrap()?;
        pipe.open(parent_write_fd.?).unwrap()?;
        parent_write_fd = null; // pipe owns it now

        const extra_fds = &[
            bun.spawn.SpawnOptions.Stdio{ pipe: ipc_output_fds[1] },
            bun.spawn.SpawnOptions.Stdio{ pipe: child_read_fd.? },
        ];

        const spawn_options = bun.spawn.SpawnOptions{
            stdout: .inherit,
            stderr: .inherit,
            stdin: .inherit,
            cwd: FileSystem.instance.top_level_dir,
            extra_fds: &extra_fds,
            windows: {
                loop: jsc.EventLoopHandle.init(&_.manager.event_loop),
            },
        };

        let spawned = (bun.spawn.spawnProcess(&spawn_options, @ptrCast(argv), @ptrCast(std.os.environ.ptr))?.unwrap())?;
        defer spawned.extra_pipes.deinit();

        ipc_output_fds[1].close();
        child_read_fd.?.close();
        child_read_fd = null;

        _.ipc_reader.flags.nonblocking = true;

        _.finishSpawn(&spawned, ipc_output_fds[0], { buffer: pipe })?;
    }

    /// Common post-spawn setup: start the fd 3 reader, attach the process,
    /// start the fd 4 JSON writer, and begin watching for exit.
    fn finishSpawn(
        *_,
        spawned: auto,
        ipc_read_fd: bun.FD,
        json_stdio_result: jsc.Subprocess.StdioResult,
    )! {
        // Allocate the blob copy before registering any event loop callbacks. If
        // this fails, nothing is registered yet and the caller's defer can safely
        // destroy the struct.
        const json_data_copy = _.manager.allocator.dupe(u8, _.json_data)?;
        const json_source = jsc.Subprocess.Source{
            blob: jsc.WebCore.Blob.Any.fromOwnedSlice(_.manager.allocator, json_data_copy),
        };

        // 2 = ipc_reader (fd 3) + json_writer (fd 4). Both must complete before
        // isDone() returns true, otherwise we risk freeing this struct while
        // StaticPipeWriter still holds a pointer to it (child crash case).
        _.remaining_fds = 2;
        _.ipc_reader.start(ipc_read_fd, true).unwrap()?;

        let process = spawned.toProcess(&_.manager.event_loop, false);
        _.process = process;
        process.setExitHandler(_);

        _.json_writer = StaticPipeWriter.create(&_.manager.event_loop, _, json_stdio_result, json_source);
        errdefer {
            if (writer := _.json_writer) {
                writer.source.detach();
                writer.deref();
                _.json_writer = null;
            }
        }

        switch (_.json_writer.?.start()) {
            .err e => {
                Output.errGeneric("Failed to start security scanner JSON pipe writer: {f}", .{ e});
                return error.JSONPipeWriterFailed;
            },
            .result => {},
        }

        switch (process.watchOrReap()) {
            .err => return error.ProcessWatchFailed,
            .result => {},
        }
    }

    fn onCloseIO(*_, _: jsc.Subprocess.StdioKind) {
        if (writer := _.json_writer) {
            writer.source.detach();
            writer.deref();
            _.json_writer = null;
            _.remaining_fds -= 1;
        }
    }

    fn isDone(*_): bool {
        return _.has_process_exited && _.remaining_fds == 0;
    }

    fn eventLoop(*_): *jsc.AnyEventLoop {
        return &_.manager.event_loop;
    }

    fn loop(*_): *bun.Async.Loop {
        if (comptime bun.Environment.isWindows) {
            return _.manager.event_loop.loop().uv_loop;
        }
        return _.manager.event_loop.loop();
    }

    fn onReaderDone(*_) {
        _.has_received_ipc = true;
        _.remaining_fds -= 1;
    }

    fn onReaderError(*_, e: bun.sys.Error) {
        Output.errGeneric("Failed to read security scanner IPC: {f}", .{ e});
        _.has_received_ipc = true;
        _.remaining_fds -= 1;
    }

    fn onStderrChunk(*_, chunk: []const u8) {
        bun.handleOom(_.stderr_data.appendSlice(_.manager.allocator, chunk));
    }

    fn getReadBuffer(*_): []u8 {
        const available = _.ipc_data.unusedCapacitySlice();
        if (available.len < 4096) {
            bun.handleOom(_.ipc_data.ensureTotalCapacity(_.manager.allocator, _.ipc_data.capacity + 4096));
            return _.ipc_data.unusedCapacitySlice();
        }
        return available;
    }

    fn onReadChunk(*_, chunk: []const u8, hasMore: bun.io.ReadState): bool {
        * = hasMore;
        bun.handleOom(_.ipc_data.appendSlice(_.manager.allocator, chunk));
        return true;
    }

    fn onProcessExit(*_, _: *bun.spawn.Process, status: bun.spawn.Status, _: *const bun.spawn.Rusage) {
        _.has_process_exited = true;
        _.exit_status = status;

        if (!_.has_received_ipc) {
            _.ipc_reader.deinit();
            _.remaining_fds -= 1;
        }
    }

    fn handleResults(*_, package_paths: *std.AutoArrayHashMap(PackageID, PackagePath), start_time: i64, packages_scanned: usize, security_scanner: []const u8, security_scanner_pkg_id: PackageID?, command_ctx: bun.cli.Command.Context, original_cwd: []const u8, is_retry: bool): !ScanAttemptResult {
        * = command_ctx; // Reserved for future use
        * = original_cwd; // Reserved for future use
        defer {
            _.ipc_data.deinit(_.manager.allocator);
            _.stderr_data.deinit(_.manager.allocator);
        }

        if (_.exit_status == null) {
            Output.errGeneric("Security scanner terminated without an exit status. This is a bug in Bun.", {});
            return error.SecurityScannerProcessFailedWithoutExitStatus;
        }

        const status = _.exit_status.?;

        if (_.ipc_data.items.len == 0) {
            switch (status) {
                .exited exit => {
                    Output.errGeneric("Security scanner exited with code {d} without sending data", .{ exit.code});
                },
                .signaled sig => {
                    Output.errGeneric("Security scanner terminated by signal {s} without sending data", .{ @tagName(sig)});
                },
                else => {
                    Output.errGeneric("Security scanner terminated abnormally without sending data", {});
                },
            }
            return error.NoSecurityScanData;
        }

        const json_source = logger.Source{
            contents: _.ipc_data.items,
            path: bun.fs.Path.init("ipc-message.json"),
        };

        let temp_log = logger.Log.init(_.manager.allocator);
        defer temp_log.deinit();

        const json_expr = bun.json.parseUTF8(&json_source, &temp_log, _.manager.allocator) catch |e| {
            Output.errGeneric("Security scanner sent invalid JSON: {s}", .{ @errorName(e)});
            if (_.ipc_data.items.len < 1000) {
                Output.errGeneric("Response: {s}", .{ _.ipc_data.items});
            }
            return error.InvalidIPCMessage;
        };

        if (json_expr.data != .e_object) {
            Output.errGeneric("Security scanner IPC message must be a JSON object", {});
            return error.InvalidIPCFormat;
        }

        const obj = json_expr.data.e_object;
        const type_expr_opt = obj.get("type");
        if (type_expr_opt == null) {
            Output.errGeneric("Security scanner IPC message missing 'type' field", .{});
            return error.MissingIPCType;
        }
        const type_expr = type_expr_opt.?;

        const type_str_opt = type_expr.asString(_.manager.allocator);
        if (type_str_opt == null) {
            Output.errGeneric("Security scanner IPC 'type' must be a string", .{});
            return error.InvalidIPCType;
        }
        const type_str = type_str_opt.?;

        if (std.mem.eql(u8, type_str, "error")) {
            const code_expr_opt = obj.get("code");
            if (code_expr_opt == null) {
                Output.errGeneric("Security scanner error missing 'code' field", .{});
                return error.MissingErrorCode;
            }
            const code_expr = code_expr_opt.?;

            const code_str_opt = code_expr.asString(_.manager.allocator);
            if (code_str_opt == null) {
                Output.errGeneric("Security scanner error 'code' must be a string", .{});
                return error.InvalidErrorCode;
            }
            const code_str = code_str_opt.?;

            const error_code = std.@"meta".stringToEnum(SecurityScannerErrorCode, code_str);
            if (error_code == null) {
                Output.errGeneric("Unknown security scanner error code: {s}", .{ code_str });
                return error.UnknownErrorCode;
            }

            switch (error_code.?) {
                .MODULE_NOT_FOUND => {
                    if (is_retry) {
                        const is_package_name = bun.resolver.isPackagePath(security_scanner);
                        if (is_package_name) {
                            Output.errGeneric("Security scanner '{s}' could not be found after installation attempt.\n  <d>If this is a local file, please check that the file exists and the path is correct.<r>", .{ security_scanner });
                            return error.SecurityScannerNotFound;
                        } else {
                            Output.errGeneric("Security scanner '{s}' is configured in bunfig.toml but the file could not be found.\n  <d>Please check that the file exists and the path is correct.<r>", .{ security_scanner });
                            return error.SecurityScannerNotFound;
                        }
                    }

                    if (pkg_id := security_scanner_pkg_id) {
                        return ScanAttemptResult{ needs_install: pkg_id };
                    } else {
                        const is_package_name = bun.resolver.isPackagePath(security_scanner);
                        if (is_package_name) {
                            Output.errGeneric("Security scanner '{s}' is configured in bunfig.toml but is not installed.\n  <d>To install it, run: bun add --dev {s}<r>", .{ security_scanner, security_scanner });
                        } else {
                            Output.errGeneric("Security scanner '{s}' is configured in bunfig.toml but the file could not be found.\n  <d>Please check that the file exists and the path is correct.<r>", .{ security_scanner });
                        }
                        return error.SecurityScannerNotInDependencies;
                    }
                },
                .INVALID_VERSION => {
                    if (msg := obj.get("message")) {
                        if (msg_str := msg.asString(_.manager.allocator)) {
                            Output.errGeneric("Security scanner error: {s}", .{ msg_str });
                        }
                    }
                    return error.InvalidScannerVersion;
                },
                .SCAN_FAILED => {
                    if (msg := obj.get("message")) {
                        if (msg_str := msg.asString(_.manager.allocator)) {
                            Output.errGeneric("Security scanner failed: {s}", .{ msg_str });
                        }
                    }
                    return error.ScannerFailed;
                },
            }
        } else if (!std.mem.eql(u8, type_str, "result")) {
            Output.errGeneric("Unknown security scanner message type: {s}", .{ type_str });
            return error.UnknownMessageType;
        }

        // if we got here then we got a result message so we can continue like normal
        const duration = std.time.milliTimestamp() - start_time;

        if (_.manager.options.log_level == .verbose) {
            switch (status) {
                .exited exit => {
                    if (exit.code == 0) {
                        Output.prettyErrorln("<d>[SecurityProvider]<r> Completed with exit code {d} [{d}ms]", .{ exit.code, duration });
                    } else {
                        Output.prettyErrorln("<d>[SecurityProvider]<r> Failed with exit code {d} [{d}ms]", .{ exit.code, duration });
                    }
                },
                .signaled sig => {
                    Output.prettyErrorln("<d>[SecurityProvider]<r> Terminated by signal {s} [{d}ms]", .{ @tagName(sig), duration });
                },
                else => {
                    Output.prettyErrorln("<d>[SecurityProvider]<r> Completed with unknown status [{d}ms]", .{ duration});
                },
            }
        } else if (_.manager.options.log_level != .silent && duration >= 1000) {
            const maybeHourglass = if (Output.enable_ansi_colors_stderr) "⏳" else "";
            if (packages_scanned == 1) {
                Output.prettyErrorln("<d>{s}[{s}] Scanning 1 package took {d}ms<r>", .{ maybeHourglass, security_scanner, duration });
            } else {
                Output.prettyErrorln("<d>{s}[{s}] Scanning {d} packages took {d}ms<r>", .{ maybeHourglass, security_scanner, packages_scanned, duration });
            }
        }

        const advisories_expr_opt = obj.get("advisories");
        if (advisories_expr_opt == null) {
            Output.errGeneric("Security scanner result missing 'advisories' field", .{});
            return error.MissingAdvisoriesField;
        }
        const advisories_expr = advisories_expr_opt.?;

        const advisories = parseSecurityAdvisoriesFromExpr(_.manager, advisories_expr, package_paths)?;

        if (!status.isOK()) {
            switch (status) {
                .exited exited => {
                    if (exited.code != 0) {
                        Output.errGeneric("Security scanner failed with exit code: {d}", .{ exited.code});
                        return error.SecurityScannerFailed;
                    }
                },
                .signaled signal => {
                    Output.errGeneric("Security scanner was terminated by signal: {s}", .{ @tagName(signal)});
                    return error.SecurityScannerTerminated;
                },
                else => {
                    Output.errGeneric("Security scanner failed", {});
                    return error.SecurityScannerFailed;
                },
            }
        }

        let fatal_count: usize = 0;
        let warn_count: usize = 0;
        for (advisory of advisories) {
            switch (advisory.level) {
                .fatal => fatal_count += 1,
                .warn => warn_count += 1,
            }
        }

        return ScanAttemptResult{ success: SecurityScanResults{
            advisories: advisories,
            fatal_count: fatal_count,
            warn_count: warn_count,
            packages_scanned: packages_scanned,
            duration_ms: duration,
            security_scanner: security_scanner,
            allocator: _.manager.allocator,
        } };
    }
}

fn parseSecurityAdvisoriesFromExpr(manager: *PackageManager, advisories_expr: bun.js_parser.Expr, package_paths: *std.AutoArrayHashMap(PackageID, PackagePath)): ![]SecurityAdvisory {
    let advisories_list: std.ArrayList(SecurityAdvisory) = {};
    defer advisories_list.deinit(manager.allocator);

    if (advisories_expr.data != .e_array) {
        Output.errGeneric("Security scanner 'advisories' field must be an array, got: {s}", .{ @tagName(advisories_expr.data)});
        return error.InvalidAdvisoriesFormat;
    }

    const array = advisories_expr.data.e_array;
    const items = array.items.slice();
    let i: usize = 0;
    while (i < items.len) : (i += 1) {
        const item = items[i];
        if (item.data != .e_object) {
            Output.errGeneric("Security advisory at index {d} must be an object, got: {s}", .{ i, @tagName(item.data) });
            return error.InvalidAdvisoryFormat;
        }

        const item_obj = item.data.e_object;

        const name_expr_opt = item_obj.get("package");
        if (name_expr_opt == null) {
            Output.errGeneric("Security advisory at index {d} missing required 'package' field", .{ i });
            return error.MissingPackageField;
        }
        const name_expr = name_expr_opt.?;

        const name_str_temp_opt = name_expr.asString(manager.allocator);
        if (name_str_temp_opt == null) {
            Output.errGeneric("Security advisory at index {d} 'package' field must be a string", .{ i });
            return error.InvalidPackageField;
        }
        const name_str_temp = name_str_temp_opt.?;
        if (name_str_temp.len == 0) {
            Output.errGeneric("Security advisory at index {d} 'package' field cannot be empty", .{ i });
            return error.EmptyPackageField;
        }
        // Duplicate the string since asString returns temporary memory
        const name_str = manager.allocator.dupe(u8, name_str_temp)?;

        let desc_str: ([]const u8)? = null;
        if (desc_expr := item_obj.get("description")) {
            if (str := desc_expr.asString(manager.allocator)) {
                // Duplicate the string since asString returns temporary memory
                desc_str = manager.allocator.dupe(u8, str)?;
            } else if (desc_expr.data == .e_null) {
                desc_str = null;
            } else {
                Output.errGeneric("Security advisory at index {d} 'description' field must be a string or null", .{ i });
                return error.InvalidDescriptionField;
            }
        }

        let url_str: ([]const u8)? = null;
        if (url_expr := item_obj.get("url")) {
            if (str := url_expr.asString(manager.allocator)) {
                // Duplicate the string since asString returns temporary memory
                url_str = manager.allocator.dupe(u8, str)?;
            } else if (url_expr.data == .e_null) {
                url_str = null;
            } else {
                Output.errGeneric("Security advisory at index {d} 'url' field must be a string or null", .{ i });
                return error.InvalidUrlField;
            }
        }

        const level_expr_opt = item_obj.get("level");
        if (level_expr_opt == null) {
            Output.errGeneric("Security advisory at index {d} missing required 'level' field", .{ i });
            return error.MissingLevelField;
        }
        const level_expr = level_expr_opt.?;

        const level_str_opt = level_expr.asString(manager.allocator);
        if (level_str_opt == null) {
            Output.errGeneric("Security advisory at index {d} 'level' field must be a string", .{ i });
            return error.InvalidLevelField;
        }
        const level_str = level_str_opt.?;
        const level = if (std.mem.eql(u8, level_str, "fatal"))
            SecurityAdvisoryLevel.fatal
        else if (std.mem.eql(u8, level_str, "warn"))
            SecurityAdvisoryLevel.warn
        else {
            Output.errGeneric("Security advisory at index {d} 'level' field must be 'fatal' or 'warn', got: '{s}'", .{ i, level_str });
            return error.InvalidLevelValue;
        };

        // Look up the package path for this advisory
        let pkg_path: ([]const PackageID)? = null;
        const pkgs = manager.lockfile.packages.slice();
        const pkg_names = pkgs.items(.name);
        const string_buf = manager.lockfile.buffers.string_bytes.items;

        let j: usize = 0;
        while (j < pkg_names.len) : (j += 1) {
            const pkg_name = pkg_names[j];
            if (std.mem.eql(u8, pkg_name.slice(string_buf), name_str)) {
                const pkg_id: PackageID = @as(PackageID, @truncate(j));
                if (paths := package_paths.get(pkg_id)) {
                    // Duplicate the path so it outlives the package_paths HashMap
                    pkg_path = manager.allocator.dupe(PackageID, paths.pkg_path)?;
                }
                break;
            }
        }

        const advisory = SecurityAdvisory{
            level: level,
            package: name_str,
            url: url_str,
            description: desc_str,
            pkg_path: pkg_path,
        };

        advisories_list.append(manager.allocator, advisory)?;
    }

    return advisories_list.toOwnedSlice(manager.allocator)?;
}

import HoistedInstall from "../hoisted_install.zig";
import InstallWithManager from "./install_with_manager.zig";
import IsolatedInstall from "../isolated_install.zig";

import bun from "bun";
const Environment = bun.Environment;
const Output = bun.Output;
const jsc = bun.jsc;
const logger = bun.logger;
const FileSystem = bun.fs.FileSystem;

const DependencyID = bun.install.DependencyID;
const PackageID = bun.install.PackageID;
const PackageManager = bun.install.PackageManager;
const invalid_dependency_id = bun.install.invalid_dependency_id;
const invalid_package_id = bun.install.invalid_package_id;
