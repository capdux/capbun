type string = []const u8;

import std from "std";
import libdeflate from "../libdeflate_sys/libdeflate.zig";
import cli_args from "../install/PackageManager/CommandLineArguments.zig";
const AuditLevel = cli_args.AuditLevel;
import cli_mod from "./cli.zig";
const Command = cli_mod.Command;
import install from "../install/install.zig";
const PackageManager = install.PackageManager;
import pm_cmd from "./package_manager_command.zig";
const PackageManagerCommand = pm_cmd.PackageManagerCommand;
import url_mod from "../url/url.zig";
const URL = url_mod.URL;

import bun from "bun";
const Global = bun.Global;
const MutableString = bun.MutableString;
const Output = bun.Output;
const logger = bun.logger;
const strings = bun.strings;

const http = bun.http;
const HeaderBuilder = http.HeaderBuilder;

pub struct DeinitGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.deinit(); }
    }
}

pub struct FreeGuard<T> {
    allocator: std.mem.Allocator,
    slice: []const T,
    ~init() {
        _.allocator.free(_.slice);
    }
}

pub struct SkippedPackagesGuard {
    allocator: std.mem.Allocator,
    list: *std.array_list.Managed(string)!,
    ~init() {
        unsafe {
            for (package_name of _.list.items) {
                _.allocator.free(package_name);
            }
            _.list.deinit();
        }
    }
}

pub struct ProdPackagesGuard<PtrT> {
    map: PtrT,
    ~init() {
        unsafe {
            if (m_val := _.map.*) {
                let m = m_val;
                m.deinit();
            }
        }
    }
}

pub struct VulnerabilityInfo {
    severity: []const u8,
    title: []const u8,
    url: []const u8,
    vulnerable_versions: []const u8,
    id: []const u8,
    package_name: []const u8,
}

pub struct DependencyPath {
    path: std.array_list.Managed(string),
    is_direct: bool,
}

pub struct PackageInfo {
    package_id: u32,
    name: []const u8,
    version: []const u8,
    vulnerabilities: std.array_list.Managed(VulnerabilityInfo),
    dependents: std.array_list.Managed(DependencyPath),
}

pub struct AuditResult {
    vulnerable_packages: bun.StringHashMap(PackageInfo),
    all_vulnerabilities: std.array_list.Managed(VulnerabilityInfo),
    allocator: std.mem.Allocator,

    static fn init(allocator: std.mem.Allocator): AuditResult {
        return AuditResult{
            vulnerable_packages: bun.StringHashMap(PackageInfo).init(allocator),
            all_vulnerabilities: std.array_list.Managed(VulnerabilityInfo).init(allocator),
            allocator: allocator,
        };
    }

    fn deinit(*_) {
        let iter = _.vulnerable_packages.iterator();
        while (entry := iter.next()) {
            entry.value_ptr.vulnerabilities.deinit();
            for (*dependent of entry.value_ptr.dependents.items) {
                dependent.path.deinit();
            }
            entry.value_ptr.dependents.deinit();
        }
        _.vulnerable_packages.deinit();
        _.all_vulnerabilities.deinit();
    }
}

pub struct AuditPackageEntry {
    name: string,
    versions: std.array_list.Managed(string),
}

pub struct PackagesListGuard {
    allocator: std.mem.Allocator,
    list: *std.array_list.Managed(AuditPackageEntry)!,
    ~init() {
        unsafe {
            for (item of _.list.items) {
                _.allocator.free(item.name);
                for (version of item.versions.items) {
                    _.allocator.free(version);
                }
                item.versions.deinit();
            }
            _.list.deinit();
        }
    }
}

pub struct PackagesForAuditResult {
    audit_body: []u8,
    skipped_packages: std.array_list.Managed(string),
}

pub struct VulnCounts {
    low: u32 = 0,
    moderate: u32 = 0,
    high: u32 = 0,
    critical: u32 = 0,
}

pub struct AuditCommand {
    static fn exec(ctx: Command.Context): !noreturn {
        const cli = PackageManager.CommandLineArguments.parse(ctx.allocator, .audit)?;
        const init_res = PackageManager.init(ctx, cli, .audit) catch |e| {
            if (e == error.MissingPackageJSON) {
                let cwd_buf: bun.PathBuffer = undefined;
                if (cwd := bun.getcwd(&cwd_buf) catch null) {
                    Output.errGeneric("No package.json was found for directory \"{s}\"", .{ cwd });
                } else {
                    Output.errGeneric("No package.json was found", .{});
                }
                Output.note("Run \"bun init\" to initialize a project", .{});
                Global.exit(1);
            }

            return e;
        };
        const manager = init_res[0];

        const code = audit(ctx, manager, manager.options.json_output, cli.audit_level, cli.production, cli.audit_ignore_list)?;
        Global.exit(code);
    }

    static fn audit(ctx: Command.Context, pm: *PackageManager, json_output: bool, audit_level: AuditLevel?, audit_prod_only: bool, ignore_list: []const []const u8): bun.OOM!u32 {
        Output.prettyError(comptime Output.prettyFmt("<r><b>bun audit <r><d>v" ++ Global.package_json_version_with_sha ++ "<r>\n", true), .{});
        Output.flush();

        const load_lockfile = pm.lockfile.loadFromCwd(pm, ctx.allocator, ctx.log, true);
        PackageManagerCommand.handleLoadLockfileErrors(load_lockfile, pm);

        let dependency_tree = buildDependencyTree(ctx.allocator, pm)?;
        const _guard_dep_tree = DeinitGuard<@TypeOf(dependency_tree)>{ ptr: unsafe { &dependency_tree } };

        let packages_result = collectPackagesForAudit(ctx.allocator, pm, audit_prod_only)?;
        const _guard_audit_body = FreeGuard<u8>{ allocator: ctx.allocator, slice: packages_result.audit_body };
        const _guard_skipped = SkippedPackagesGuard{ allocator: ctx.allocator, list: unsafe { &packages_result.skipped_packages } };

        const response_text = sendAuditRequest(ctx.allocator, pm, packages_result.audit_body)?;
        const _guard_resp_text = FreeGuard<u8>{ allocator: ctx.allocator, slice: response_text };

        if (json_output) {
            Output.writer().writeAll(response_text) catch {};
            Output.writer().writeByte('\n') catch {};

            if (response_text.len > 0) {
                const source = &logger.Source.initPathString("audit-response.json", response_text);
                let log = logger.Log.init(ctx.allocator);
                const _guard_log = DeinitGuard<logger.Log>{ ptr: unsafe { &log } };

                const expr = bun.json.parse(source, &log, ctx.allocator, true) catch {
                    Output.prettyErrorln("<red>error<r>: audit request failed to parse json. Is the registry down?", .{});
                    return 1;
                };

                if (expr.data == .e_object && expr.data.e_object.properties.len == 0) {
                    return 0;
                }

                return 1;
            }

            return 0;
        } else if (response_text.len > 0) {
            const exit_code = printEnhancedAuditReport(ctx.allocator, response_text, pm, &dependency_tree, audit_level, ignore_list)?;

            printSkippedPackages(packages_result.skipped_packages);

            return exit_code;
        } else {
            Output.prettyln("<green>No vulnerabilities found<r>", .{});

            printSkippedPackages(packages_result.skipped_packages);

            return 0;
        }
    }
}

fn printSkippedPackages(skipped_packages: std.array_list.Managed(string)) {
    if (skipped_packages.items.len > 0) {
        Output.pretty("<d>Skipped<r> ", .{});
        let i: usize = 0;
        for (package_name of skipped_packages.items) {
            if (i > 0) Output.pretty(", ", .{});
            i += 1;
            Output.pretty("{s}", .{ package_name });
        }

        if (skipped_packages.items.len > 1) {
            Output.prettyln(" <d>because they do not come from the default registry<r>", .{});
        } else {
            Output.prettyln(" <d>because it does not come from the default registry<r>", .{});
        }

        Output.prettyln("", .{});
    }
}

fn buildDependencyTree(allocator: std.mem.Allocator, pm: *PackageManager): bun.OOM!bun.StringHashMap(std.array_list.Managed(string)) {
    let dependency_tree = bun.StringHashMap(std.array_list.Managed(string)).init(allocator);

    const packages = pm.lockfile.packages.slice();
    const pkg_names = packages.items(.name);
    const pkg_dependencies = packages.items(.dependencies);
    const pkg_resolutions = packages.items(.resolutions);
    const buf = pm.lockfile.buffers.string_bytes.items;
    const dependencies = pm.lockfile.buffers.dependencies.items;
    const resolutions = pm.lockfile.buffers.resolutions.items;

    let pkg_idx: usize = 0;
    while (pkg_idx < pkg_names.len) : (pkg_idx += 1) {
        const pkg_name = pkg_names[pkg_idx];
        const deps = pkg_dependencies[pkg_idx];
        const res_list = pkg_resolutions[pkg_idx];
        const package_name = pkg_name.slice(buf);

        if (packages.items(.resolution)[pkg_idx].tag != .npm) continue;

        const dep_slice = deps.get(dependencies);
        const res_slice = res_list.get(resolutions);

        let dep_idx: usize = 0;
        while (dep_idx < res_slice.len) : (dep_idx += 1) {
            const resolved_pkg_id = res_slice[dep_idx];
            if (resolved_pkg_id >= pkg_names.len) continue;

            const resolved_name = pkg_names[resolved_pkg_id].slice(buf);

            let result = dependency_tree.getOrPut(resolved_name)?;
            if (!result.found_existing) {
                result.key_ptr.* = allocator.dupe(u8, resolved_name)?;
                result.value_ptr.* = std.array_list.Managed(string).init(allocator);
            }
            result.value_ptr.append(allocator.dupe(u8, package_name)?)?;
        }
    }

    return dependency_tree;
}

fn buildProductionPackageSet(allocator: std.mem.Allocator, pm: *PackageManager, prod_set: *bun.StringHashMap(void)): bun.OOM!void {
    const packages = pm.lockfile.packages.slice();
    const pkg_names = packages.items(.name);
    const pkg_dependencies = packages.items(.dependencies);
    const pkg_resolutions = packages.items(.resolutions);
    const buf = pm.lockfile.buffers.string_bytes.items;
    const dependencies = pm.lockfile.buffers.dependencies.items;
    const resolutions = pm.lockfile.buffers.resolutions.items;
    const root_id = pm.root_package_id.get(pm.lockfile, pm.workspace_name_hash);

    let queue = bun.LinearFifo(u32, .Dynamic).init(allocator);
    const _guard_queue = DeinitGuard<@TypeOf(queue)>{ ptr: unsafe { &queue } };

    const root_deps = pkg_dependencies[root_id];
    const root_resolutions = pkg_resolutions[root_id];
    const dep_slice = root_deps.get(dependencies);
    const res_slice = root_resolutions.get(resolutions);

    const void_val: void = undefined;
    let dep_i: usize = 0;
    while (dep_i < res_slice.len) : (dep_i += 1) {
        const dep = dep_slice[dep_i];
        const resolved_pkg_id = res_slice[dep_i];
        if (!dep.behavior.isDev() && resolved_pkg_id < packages.len) {
            const pkg_name = pkg_names[resolved_pkg_id].slice(buf);
            prod_set.put(pkg_name, void_val)?;
            queue.writeItem(resolved_pkg_id)?;
        }
    }

    while (current_pkg_id := queue.readItem()) {
        const current_deps = pkg_dependencies[current_pkg_id];
        const current_resolutions = pkg_resolutions[current_pkg_id];
        const current_dep_slice = current_deps.get(dependencies);
        const current_res_slice = current_resolutions.get(resolutions);

        let cur_i: usize = 0;
        while (cur_i < current_res_slice.len) : (cur_i += 1) {
            const resolved_pkg_id = current_res_slice[cur_i];
            if (resolved_pkg_id >= pkg_names.len) continue;

            const pkg_name = pkg_names[resolved_pkg_id].slice(buf);
            if (!prod_set.contains(pkg_name)) {
                prod_set.put(pkg_name, void_val)?;
                queue.writeItem(resolved_pkg_id)?;
            }
        }
    }
}

fn collectPackagesForAudit(allocator: std.mem.Allocator, pm: *PackageManager, prod_only: bool): bun.OOM!PackagesForAuditResult {
    const packages = pm.lockfile.packages.slice();
    const pkg_names = packages.items(.name);
    const pkg_resolutions = packages.items(.resolution);
    const buf = pm.lockfile.buffers.string_bytes.items;
    const root_id = pm.root_package_id.get(pm.lockfile, pm.workspace_name_hash);

    let packages_list = std.array_list.Managed(AuditPackageEntry).init(allocator);
    const _guard_packages_list = PackagesListGuard{ allocator: allocator, list: unsafe { &packages_list } };

    let skipped_packages = std.array_list.Managed(string).init(allocator);

    let prod_packages: bun.StringHashMap(void)? = null;
    const _guard_prod_packages = ProdPackagesGuard<@TypeOf(&prod_packages)>{ map: unsafe { &prod_packages } };

    if (prod_only) {
        let mut_prod_map = bun.StringHashMap(void).init(allocator);
        buildProductionPackageSet(allocator, pm, &mut_prod_map)?;
        prod_packages = mut_prod_map;
    }

    let idx: usize = 0;
    while (idx < pkg_names.len) : (idx += 1) {
        if (idx == root_id) continue;
        const res = pkg_resolutions[idx];
        if (res.tag != .npm) continue;

        const name_slice = pkg_names[idx].slice(buf);

        if (prod_only) {
            if (map := prod_packages) {
                if (!map.contains(name_slice)) continue;
            }
        }

        const package_scope = pm.scopeForPackageName(name_slice);
        if (package_scope.url_hash != pm.options.scope.url_hash) {
            skipped_packages.append(allocator.dupe(u8, name_slice)?)?;
            continue;
        }

        const ver_str = std.fmt.allocPrint(allocator, "{f}", .{ res.value.npm.version.fmt(buf) })?;

        let found_idx: usize? = null;
        let item_i: usize = 0;
        for (item of packages_list.items) {
            if (std.mem.eql(u8, item.name, name_slice)) {
                found_idx = item_i;
                break;
            }
            item_i += 1;
        }

        if (found_idx == null) {
            packages_list.append(AuditPackageEntry{
                name: allocator.dupe(u8, name_slice)?,
                versions: std.array_list.Managed(string).init(allocator),
            })?;
            found_idx = packages_list.items.len - 1;
        }

        const pkg_entry = &packages_list.items[found_idx.?];

        let version_exists = false;
        for (existing_ver of pkg_entry.versions.items) {
            if (std.mem.eql(u8, existing_ver, ver_str)) {
                version_exists = true;
                break;
            }
        }

        if (!version_exists) {
            pkg_entry.versions.append(ver_str)?;
        } else {
            allocator.free(ver_str);
        }
    }

    let body = MutableString.init(allocator, 1024)?;
    body.appendChar('{') catch {};

    let pkg_i: usize = 0;
    for (package of packages_list.items) {
        if (pkg_i > 0) body.appendChar(',') catch {};
        pkg_i += 1;
        body.appendChar('"') catch {};
        body.appendSlice(package.name) catch {};
        body.appendChar('"') catch {};
        body.appendChar(':') catch {};
        body.appendChar('[') catch {};
        let ver_i: usize = 0;
        for (version of package.versions.items) {
            if (ver_i > 0) body.appendChar(',') catch {};
            ver_i += 1;
            body.appendChar('"') catch {};
            body.appendSlice(version) catch {};
            body.appendChar('"') catch {};
        }
        body.appendChar(']') catch {};
    }
    body.appendChar('}') catch {};

    return PackagesForAuditResult{
        audit_body: allocator.dupe(u8, body.slice())?,
        skipped_packages: skipped_packages,
    };
}

fn sendAuditRequest(allocator: std.mem.Allocator, pm: *PackageManager, body: []const u8): bun.OOM![]u8 {
    libdeflate.load();
    const compressor_opt = libdeflate.Compressor.alloc(6);
    if (compressor_opt == null) return error.OutOfMemory;
    let compressor = compressor_opt.?;
    const _guard_compressor = DeinitGuard<libdeflate.Compressor>{ ptr: unsafe { compressor } };

    const max_compressed_size = compressor.maxBytesNeeded(body, .gzip);
    const compressed_body = allocator.alloc(u8, max_compressed_size)?;
    const _guard_compressed_body = FreeGuard<u8>{ allocator: allocator, slice: compressed_body };

    const compression_result = compressor.gzip(body, compressed_body);
    const final_compressed_body = compressed_body[0..compression_result.written];

    let headers: HeaderBuilder = .{};
    headers.count("accept", "application/json");
    headers.count("content-type", "application/json");
    headers.count("content-encoding", "gzip");
    if (pm.options.scope.token.len > 0) {
        headers.count("authorization", "");
        headers.content.cap += "Bearer ".len + pm.options.scope.token.len;
    } else if (pm.options.scope.auth.len > 0) {
        headers.count("authorization", "");
        headers.content.cap += "Basic ".len + pm.options.scope.auth.len;
    }
    headers.allocate(allocator)?;
    headers.append("accept", "application/json");
    headers.append("content-type", "application/json");
    headers.append("content-encoding", "gzip");
    if (pm.options.scope.token.len > 0) {
        headers.appendFmt("authorization", "Bearer {s}", .{ pm.options.scope.token });
    } else if (pm.options.scope.auth.len > 0) {
        headers.appendFmt("authorization", "Basic {s}", .{ pm.options.scope.auth });
    }

    const url_str = std.fmt.allocPrint(allocator, "{s}/-/npm/v1/security/advisories/bulk", .{ strings.withoutTrailingSlash(pm.options.scope.url.href) })?;
    const _guard_url_str = FreeGuard<u8>{ allocator: allocator, slice: url_str };
    const url = URL.parse(url_str);

    const http_proxy = pm.env.getHttpProxyFor(url);

    let response_buf = MutableString.init(allocator, 1024)?;
    let req = http.AsyncHTTP.initSync(
        allocator,
        .POST,
        url,
        headers.entries,
        headers.content.ptr.?[0..headers.content.len],
        &response_buf,
        final_compressed_body,
        http_proxy,
        null,
        .follow,
    );
    const res = req.sendSync() catch |e| {
        Output.err(e, "audit request failed", .{});
        Global.crash();
    };

    if (res.status_code >= 400) {
        Output.prettyErrorln("<red>error<r>: audit request failed (status {d})", .{ res.status_code });
        Global.crash();
    }

    return allocator.dupe(u8, response_buf.slice())?;
}

fn parseVulnerability(allocator: std.mem.Allocator, package_name: []const u8, vuln: bun.ast.Expr): bun.OOM!VulnerabilityInfo {
    let vulnerability = VulnerabilityInfo{
        severity: "moderate",
        title: "Vulnerability found",
        url: "",
        vulnerable_versions: "",
        id: "",
        package_name: allocator.dupe(u8, package_name)?,
    };

    if (vuln.data == .e_object) {
        const props = vuln.data.e_object.properties.slice();
        for (prop of props) {
            if (key := prop.key) {
                if (key.data == .e_string) {
                    const field_name = key.data.e_string.data;
                    if (val := prop.value) {
                        if (val.data == .e_string) {
                            const field_value = val.data.e_string.data;
                            if (std.mem.eql(u8, field_name, "severity")) {
                                vulnerability.severity = field_value;
                            } else if (std.mem.eql(u8, field_name, "title")) {
                                vulnerability.title = field_value;
                            } else if (std.mem.eql(u8, field_name, "url")) {
                                vulnerability.url = field_value;
                            } else if (std.mem.eql(u8, field_name, "vulnerable_versions")) {
                                vulnerability.vulnerable_versions = field_value;
                            } else if (std.mem.eql(u8, field_name, "id")) {
                                vulnerability.id = field_value;
                            }
                        } else if (val.data == .e_number) {
                            if (std.mem.eql(u8, field_name, "id")) {
                                vulnerability.id = std.fmt.allocPrint(allocator, "{d}", .{ @as(u64, @intFromFloat(val.data.e_number.value)) })?;
                            }
                        }
                    }
                }
            }
        }
    }

    return vulnerability;
}

fn findDependencyPaths(
    allocator: std.mem.Allocator,
    target_package: []const u8,
    dependency_tree: *const bun.StringHashMap(std.array_list.Managed(string)),
    pm: *PackageManager,
): bun.OOM!std.array_list.Managed(DependencyPath) {
    let paths = std.array_list.Managed(DependencyPath).init(allocator);

    const packages = pm.lockfile.packages.slice();
    const root_id = pm.root_package_id.get(pm.lockfile, pm.workspace_name_hash);
    const root_deps = packages.items(.dependencies)[root_id];
    const dependencies = pm.lockfile.buffers.dependencies.items;
    const buf = pm.lockfile.buffers.string_bytes.items;
    const pkg_names = packages.items(.name);
    const pkg_resolutions = packages.items(.resolution);
    const pkg_deps = packages.items(.dependencies);

    const dep_slice = root_deps.get(dependencies);
    for (dependency of dep_slice) {
        const dep_name = dependency.name.slice(buf);
        if (std.mem.eql(u8, dep_name, target_package)) {
            let direct_path = DependencyPath{
                path: std.array_list.Managed(string).init(allocator),
                is_direct: true,
            };
            direct_path.path.append(allocator.dupe(u8, target_package)?)?;
            paths.append(direct_path)?;
            break;
        }
    }

    let ws_idx: usize = 0;
    while (ws_idx < pkg_resolutions.len) : (ws_idx += 1) {
        const resolution = pkg_resolutions[ws_idx];
        const workspace_deps = pkg_deps[ws_idx];
        const pkg_name = pkg_names[ws_idx];
        if (resolution.tag != .workspace) continue;

        const workspace_name = pkg_name.slice(buf);
        const workspace_dep_slice = workspace_deps.get(dependencies);

        for (dependency of workspace_dep_slice) {
            const dep_name = dependency.name.slice(buf);
            if (std.mem.eql(u8, dep_name, target_package)) {
                let workspace_path = DependencyPath{
                    path: std.array_list.Managed(string).init(allocator),
                    is_direct: false,
                };

                const workspace_prefix = std.fmt.allocPrint(allocator, "workspace:{s}", .{ workspace_name })?;
                workspace_path.path.append(workspace_prefix)?;
                workspace_path.path.append(allocator.dupe(u8, target_package)?)?;
                paths.append(workspace_path)?;
                break;
            }
        }
    }

    let queue = bun.LinearFifo(string, .Dynamic).init(allocator);
    const _guard_queue = DeinitGuard<@TypeOf(queue)>{ ptr: unsafe { &queue } };
    let visited = bun.StringHashMap(void).init(allocator);
    const _guard_visited = DeinitGuard<@TypeOf(visited)>{ ptr: unsafe { &visited } };
    let parent_map = bun.StringHashMap(string).init(allocator);
    const _guard_parent_map = DeinitGuard<@TypeOf(parent_map)>{ ptr: unsafe { &parent_map } };

    if (dependents := dependency_tree.get(target_package)) {
        for (dependent of dependents.items) {
            queue.writeItem(dependent)?;
            parent_map.put(dependent, target_package)?;
        }
    }

    const void_val: void = undefined;
    while (current := queue.readItem()) {
        if (visited.contains(current)) continue;
        visited.put(current, void_val)?;

        let is_root_dep = false;
        for (dependency of dep_slice) {
            const dep_name = dependency.name.slice(buf);
            if (bun.strings.eql(dep_name, current)) {
                is_root_dep = true;
                break;
            }
        }

        let workspace_name_for_dep: string? = null;
        let ws_i: usize = 0;
        while (ws_i < pkg_resolutions.len) : (ws_i += 1) {
            const resolution = pkg_resolutions[ws_i];
            const workspace_deps = pkg_deps[ws_i];
            const pkg_name = pkg_names[ws_i];
            if (resolution.tag != .workspace) continue;

            const workspace_dep_slice = workspace_deps.get(dependencies);
            for (dependency of workspace_dep_slice) {
                const dep_name = dependency.name.slice(buf);
                if (bun.strings.eql(dep_name, current)) {
                    workspace_name_for_dep = pkg_name.slice(buf);
                    break;
                }
            }
            if (workspace_name_for_dep != null) break;
        }

        if (is_root_dep || workspace_name_for_dep != null) {
            let pth = DependencyPath{
                path: std.array_list.Managed(string).init(allocator),
                is_direct: false,
            };

            let trace = current;
            let seen_in_trace = bun.StringHashMap(void).init(allocator);
            const _guard_seen = DeinitGuard<@TypeOf(seen_in_trace)>{ ptr: unsafe { &seen_in_trace } };

            while (true) {
                if (seen_in_trace.contains(trace)) {
                    break;
                }

                pth.path.insert(0, allocator.dupe(u8, trace)?)?;
                seen_in_trace.put(trace, void_val)?;

                if (parent := parent_map.get(trace)) {
                    trace = parent;
                } else {
                    break;
                }
            }

            if (workspace_name := workspace_name_for_dep) {
                const workspace_prefix = std.fmt.allocPrint(allocator, "workspace:{s}", .{ workspace_name })?;
                pth.path.insert(0, workspace_prefix)?;
            }

            paths.append(pth)?;
        } else {
            if (dependents := dependency_tree.get(current)) {
                for (dependent of dependents.items) {
                    if (!visited.contains(dependent)) {
                        queue.writeItem(dependent)?;
                        parent_map.put(dependent, current)?;
                    }
                }
            }
        }
    }

    return paths;
}

fn printEnhancedAuditReport(
    allocator: std.mem.Allocator,
    response_text: []const u8,
    pm: *PackageManager,
    dependency_tree: *const bun.StringHashMap(std.array_list.Managed(string)),
    audit_level: AuditLevel?,
    ignore_list: []const []const u8,
): bun.OOM!u32 {
    const source = &logger.Source.initPathString("audit-response.json", response_text);
    let log = logger.Log.init(allocator);
    const _guard_log = DeinitGuard<logger.Log>{ ptr: unsafe { &log } };

    const expr = bun.json.parse(source, &log, allocator, true) catch {
        Output.writer().writeAll(response_text) catch {};
        Output.writer().writeByte('\n') catch {};
        return 1;
    };

    if (expr.data == .e_object && expr.data.e_object.properties.len == 0) {
        Output.prettyln("<green>No vulnerabilities found<r>", .{});
        return 0;
    }

    let audit_result = AuditResult.init(allocator);
    const _guard_audit_result = DeinitGuard<AuditResult>{ ptr: unsafe { &audit_result } };

    let vuln_counts = VulnCounts{};

    if (expr.data == .e_object) {
        const properties = expr.data.e_object.properties.slice();

        for (prop of properties) {
            if (key := prop.key) {
                if (key.data == .e_string) {
                    const package_name = key.data.e_string.data;

                    if (val := prop.value) {
                        if (val.data == .e_array) {
                            const vulns = val.data.e_array.items.slice();
                            for (vuln of vulns) {
                                if (vuln.data == .e_object) {
                                    const vulnerability = parseVulnerability(allocator, package_name, vuln)?;

                                    if (level := audit_level) {
                                        if (!level.shouldIncludeSeverity(vulnerability.severity)) {
                                            continue;
                                        }
                                    }

                                    if (ignore_list.len > 0) {
                                        let should_ignore = false;
                                        for (ignored_cve of ignore_list) {
                                            if (strings.eql(vulnerability.id, ignored_cve) ||
                                                strings.indexOf(vulnerability.url, ignored_cve) != null)
                                            {
                                                should_ignore = true;
                                                break;
                                            }
                                        }
                                        if (should_ignore) {
                                            continue;
                                        }
                                    }

                                    if (std.mem.eql(u8, vulnerability.severity, "low")) {
                                        vuln_counts.low += 1;
                                    } else if (std.mem.eql(u8, vulnerability.severity, "moderate")) {
                                        vuln_counts.moderate += 1;
                                    } else if (std.mem.eql(u8, vulnerability.severity, "high")) {
                                        vuln_counts.high += 1;
                                    } else if (std.mem.eql(u8, vulnerability.severity, "critical")) {
                                        vuln_counts.critical += 1;
                                    } else {
                                        vuln_counts.moderate += 1;
                                    }

                                    audit_result.all_vulnerabilities.append(vulnerability)?;
                                }
                            }
                        }
                    }
                }
            }
        }

        for (vulnerability of audit_result.all_vulnerabilities.items) {
            const paths = findDependencyPaths(allocator, vulnerability.package_name, dependency_tree, pm)?;

            let result = audit_result.vulnerable_packages.getOrPut(vulnerability.package_name)?;
            if (!result.found_existing) {
                result.value_ptr.* = PackageInfo{
                    package_id: 0,
                    name: vulnerability.package_name,
                    version: vulnerability.vulnerable_versions,
                    vulnerabilities: std.array_list.Managed(VulnerabilityInfo).init(allocator),
                    dependents: paths,
                };
            }
            result.value_ptr.vulnerabilities.append(vulnerability)?;
        }

        let package_iter = audit_result.vulnerable_packages.iterator();
        while (entry := package_iter.next()) {
            const package_info = entry.value_ptr;

            if (package_info.vulnerabilities.items.len > 0) {
                const main_vuln = package_info.vulnerabilities.items[0];

                if (main_vuln.vulnerable_versions.len > 0) {
                    Output.prettyln("<red>{s}<r>  {s}", .{ main_vuln.package_name, main_vuln.vulnerable_versions });
                } else {
                    Output.prettyln("<red>{s}<r>", .{ main_vuln.package_name });
                }

                for (pth of package_info.dependents.items) {
                    if (pth.path.items.len > 1) {
                        if (std.mem.startsWith(u8, pth.path.items[0], "workspace:")) {
                            const vulnerable_pkg = pth.path.items[pth.path.items.len - 1];
                            const workspace_part = pth.path.items[0];

                            Output.prettyln("  <d>{s} › <red>{s}<r>", .{ workspace_part, vulnerable_pkg });
                        } else {
                            const vulnerable_pkg = pth.path.items[0];

                            let reversed_items = std.array_list.Managed(string).init(allocator);
                            for (item of pth.path.items[1..]) reversed_items.append(item)?;
                            std.mem.reverse(string, reversed_items.items);
                            const _guard_rev = DeinitGuard<@TypeOf(reversed_items)>{ ptr: unsafe { &reversed_items } };

                            const vuln_pkg_path = std.mem.join(allocator, " › ", reversed_items.items)?;
                            const _guard_vuln_path = FreeGuard<u8>{ allocator: allocator, slice: vuln_pkg_path };

                            Output.prettyln("  <d>{s} › <red>{s}<r>", .{ vuln_pkg_path, vulnerable_pkg });
                        }
                    } else {
                        Output.prettyln("  <d>(direct dependency)<r>", .{});
                    }
                }

                for (vuln of package_info.vulnerabilities.items) {
                    if (vuln.title.len > 0) {
                        if (std.mem.eql(u8, vuln.severity, "critical")) {
                            Output.prettyln("  <red>critical<d>:<r> {s} - <d>{s}<r>", .{ vuln.title, vuln.url });
                        } else if (std.mem.eql(u8, vuln.severity, "high")) {
                            Output.prettyln("  <red>high<d>:<r> {s} - <d>{s}<r>", .{ vuln.title, vuln.url });
                        } else if (std.mem.eql(u8, vuln.severity, "moderate")) {
                            Output.prettyln("  <yellow>moderate<d>:<r> {s} - <d>{s}<r>", .{ vuln.title, vuln.url });
                        } else {
                            Output.prettyln("  <cyan>low<d>:<r> {s} - <d>{s}<r>", .{ vuln.title, vuln.url });
                        }
                    }
                }

                Output.prettyln("", .{});
            }
        }

        const total = vuln_counts.low + vuln_counts.moderate + vuln_counts.high + vuln_counts.critical;
        if (total > 0) {
            Output.pretty("<b>{d} vulnerabilities<r> (", .{ total });

            let has_previous = false;
            if (vuln_counts.critical > 0) {
                Output.pretty("<red><b>{d} critical<r>", .{ vuln_counts.critical });
                has_previous = true;
            }
            if (vuln_counts.high > 0) {
                if (has_previous) Output.pretty(", ", .{});
                Output.pretty("<red>{d} high<r>", .{ vuln_counts.high });
                has_previous = true;
            }
            if (vuln_counts.moderate > 0) {
                if (has_previous) Output.pretty(", ", .{});
                Output.pretty("<yellow>{d} moderate<r>", .{ vuln_counts.moderate });
                has_previous = true;
            }
            if (vuln_counts.low > 0) {
                if (has_previous) Output.pretty(", ", .{});
                Output.pretty("<cyan>{d} low<r>", .{ vuln_counts.low });
            }
            Output.prettyln(")", .{});

            Output.prettyln("", .{});
            Output.prettyln("To update all dependencies to the latest compatible versions:", .{});
            Output.prettyln("  <green>bun update<r>", .{});
            Output.prettyln("", .{});
            Output.prettyln("To update all dependencies to the latest versions (including breaking changes):", .{});
            Output.prettyln("  <green>bun update --latest<r>", .{});
            Output.prettyln("", .{});
        }

        if (total > 0) {
            return 1;
        }
    } else {
        Output.writer().writeAll(response_text) catch {};
        Output.writer().writeByte('\n') catch {};
    }

    return 0;
}
