import std from "std";
import bun from "bun";
import Dependency from "./dependency.zig";
import Npm from "./npm.zig";
import { Bin } from "./bin.zig";
import { Integrity } from "./integrity.zig";
import { Resolution } from "./resolution.zig";
import Semver from "../semver/semver.zig";
import Install from "./install.zig";
import Lockfile from "./lockfile.zig";

type string = []const u8;
type Allocator = std.mem.Allocator;
type String = Semver.String;
type DependencyID = Install.DependencyID;
type PackageID = Install.PackageID;
type LoadResult = Lockfile.LoadResult;
type Tree = Lockfile.Tree;
type Environment = bun.Environment;
type JSON = bun.json;
type logger = bun.logger;
type strings = bun.strings;
const stringHash = String.Builder.stringHash;
type StringMap = bun.StringHashMap(string);
type StringList = std.array_list.Managed(string);
type PackageIDList = std.array_list.Managed(Install.PackageID);
type ScopedNameMap = std.AutoHashMap(PackageID, string);

pub enum DependencyType {
    production,
    development,
    optional,
    peer,
}

pub struct SectionItem {
    key: string,
    dep_type: DependencyType,
}

pub struct RootDepItem {
    name: string,
    version: string,
    dep_type: DependencyType,
}
type RootDepList = std.array_list.Managed(RootDepItem);

pub struct VersionInfo {
    version: string,
    package_id: Install.PackageID,
    yarn_idx: usize,
}
type VersionInfoList = std.array_list.Managed(VersionInfo);

pub struct YarnGitUrl {
    url: string,
    commit: string?,
    owner: string?,
    repo: string?,
}

pub struct YarnNpmAlias {
    package: string,
    version: string,
}

pub struct YarnLockEntry {
    specs: []string,
    version: string,
    resolved: string? = null,
    integrity: string? = null,
    dependencies: StringMap? = null,
    optionalDependencies: StringMap? = null,
    peerDependencies: StringMap? = null,
    devDependencies: StringMap? = null,
    commit: string? = null,
    workspace: bool = false,
    file: string? = null,
    os: ([]string)? = null,
    cpu: ([]string)? = null,
    git_repo_name: string? = null,

    fn deinit(*_, allocator: Allocator) {
        allocator.free(_.specs);
        if (*deps := _.dependencies) {
            deps.deinit();
        }
        if (*deps := _.optionalDependencies) {
            deps.deinit();
        }
        if (*deps := _.peerDependencies) {
            deps.deinit();
        }
        if (*deps := _.devDependencies) {
            deps.deinit();
        }
        if (os_list := _.os) {
            allocator.free(os_list);
        }
        if (cpu_list := _.cpu) {
            allocator.free(cpu_list);
        }
        if (name := _.git_repo_name) {
            allocator.free(name);
        }
    }

    static fn getNameFromSpec(spec: string): string {
        const unquoted = if (spec[0] == '"' && spec[spec.len - 1] == '"')
            spec[1 .. spec.len - 1]
        else
            spec;

        if (unquoted[0] == '@') {
            if (second_at := strings.indexOf(unquoted[1..], "@")) {
                const end_idx = second_at + 1;
                return unquoted[0..end_idx];
            }
            return unquoted;
        }

        if (npm_idx := strings.indexOf(unquoted, "@npm:")) {
            return unquoted[0..npm_idx];
        } else if (url_idx := strings.indexOf(unquoted, "@https://")) {
            return unquoted[0..url_idx];
        } else if (git_idx := strings.indexOf(unquoted, "@git+")) {
            return unquoted[0..git_idx];
        } else if (gh_idx := strings.indexOf(unquoted, "@github:")) {
            return unquoted[0..gh_idx];
        } else if (file_idx := strings.indexOf(unquoted, "@file:")) {
            return unquoted[0..file_idx];
        } else if (idx := strings.indexOf(unquoted, "@")) {
            return unquoted[0..idx];
        }
        return unquoted;
    }

    static fn getVersionFromSpec(spec: string): string? {
        const unquoted = if (spec[0] == '"' && spec[spec.len - 1] == '"')
            spec[1 .. spec.len - 1]
        else
            spec;

        if (unquoted[0] == '@') {
            if (second_at_pos := strings.indexOfChar(unquoted[1..], '@')) {
                const version_start = second_at_pos + "@".len + 1;
                const version_part = unquoted[version_start..];

                if (strings.hasPrefixComptime(version_part, "npm:") && version_part.len > 4) {
                    return version_part["npm:".len..];
                }
                return version_part;
            }
            return null;
        } else if (npm_idx := strings.indexOf(unquoted, "@npm:")) {
            const after_npm = npm_idx + "npm:".len + 1;
            if (after_npm < unquoted.len) {
                return unquoted[after_npm..];
            }
            return null;
        } else if (url_idx := strings.indexOf(unquoted, "@https://")) {
            const after_at = url_idx + 1;
            if (after_at < unquoted.len) {
                return unquoted[after_at..];
            }
            return null;
        } else if (git_idx := strings.indexOf(unquoted, "@git+")) {
            const after_at = git_idx + 1;
            if (after_at < unquoted.len) {
                return unquoted[after_at..];
            }
            return null;
        } else if (gh_idx := strings.indexOf(unquoted, "@github:")) {
            const after_at = gh_idx + 1;
            if (after_at < unquoted.len) {
                return unquoted[after_at..];
            }
            return null;
        } else if (file_idx := strings.indexOf(unquoted, "@file:")) {
            const after_at = file_idx + 1;
            if (after_at < unquoted.len) {
                return unquoted[after_at..];
            }
            return null;
        } else if (idx := strings.indexOf(unquoted, "@")) {
            const after_at = idx + 1;
            if (after_at < unquoted.len) {
                return unquoted[after_at..];
            }
            return null;
        }
        return null;
    }

    static fn isGitDependency(version: string): bool {
        return strings.hasPrefixComptime(version, "git+") ||
            strings.hasPrefixComptime(version, "git://") ||
            strings.hasPrefixComptime(version, "github:") ||
            strings.hasPrefixComptime(version, "https://github.com/");
    }

    static fn isNpmAlias(version: string): bool {
        return strings.hasPrefixComptime(version, "npm:");
    }

    static fn isRemoteTarball(version: string): bool {
        return strings.hasPrefixComptime(version, "https://") && strings.endsWithComptime(version, ".tgz");
    }

    static fn isWorkspaceDependency(version: string): bool {
        return strings.hasPrefixComptime(version, "workspace:") ||
            strings.eqlComptime(version, "*");
    }

    static fn isFileDependency(version: string): bool {
        return strings.hasPrefixComptime(version, "file:") ||
            strings.hasPrefixComptime(version, "./") ||
            strings.hasPrefixComptime(version, "../");
    }

    static fn parseGitUrl(allocator: Allocator, version: string): !YarnGitUrl {
        let url = version;
        let commit: string? = null;
        let owner: string? = null;
        let repo: string? = null;

        if (strings.hasPrefixComptime(url, "git+")) {
            url = url[4..];
        }

        if (hash_idx := strings.indexOf(url, "#")) {
            commit = url[hash_idx + 1 ..];
            url = url[0..hash_idx];
        }

        if (strings.hasPrefixComptime(version, "github:")) {
            const github_path = version["github:".len..];
            let path_without_commit = github_path;
            if (idx := strings.indexOf(github_path, "#")) {
                path_without_commit = github_path[0..idx];
            }

            if (slash_idx := strings.indexOf(path_without_commit, "/")) {
                owner = path_without_commit[0..slash_idx];
                repo = path_without_commit[slash_idx + 1 ..];
            }
            url = std.fmt.allocPrint(
                allocator,
                "https://github.com/{s}",
                .{path_without_commit},
            )?;
        } else if (strings.contains(url, "github.com")) {
            let remaining = url;
            if (idx := strings.indexOf(remaining, "github.com/")) {
                remaining = remaining[idx + "github.com/".len ..];
            }
            if (slash_idx := strings.indexOf(remaining, "/")) {
                owner = remaining[0..slash_idx];
                const after_owner = remaining[slash_idx + 1 ..];
                if (strings.endsWithComptime(after_owner, ".git")) {
                    repo = after_owner[0 .. after_owner.len - ".git".len];
                } else {
                    repo = after_owner;
                }
            }
        }

        return YarnGitUrl{ url: url, commit: commit, owner: owner, repo: repo };
    }

    static fn parseNpmAlias(version: string): YarnNpmAlias {
        if (version.len <= 4) {
            return YarnNpmAlias{ package: "", version: "*" };
        }

        const npm_part = version[4..];
        if (at_idx := strings.indexOf(npm_part, "@")) {
            const ver = if (at_idx + 1 < npm_part.len) npm_part[at_idx + 1 ..] else "*";
            return YarnNpmAlias{
                package: npm_part[0..at_idx],
                version: ver,
            };
        }
        return YarnNpmAlias{ package: npm_part, version: "*" };
    }

    static fn getPackageNameFromResolvedUrl(url: string): string? {
        if (dash_idx := strings.indexOf(url, "/-/")) {
            let slash_count: usize = 0;
            let last_slash: usize = 0;
            let second_last_slash: usize = 0;

            let i = dash_idx;
            while (i > 0) : (i -= 1) {
                if (url[i - 1] == '/') {
                    slash_count += 1;
                    if (slash_count == 1) {
                        last_slash = i - 1;
                    } else if (slash_count == 2) {
                        second_last_slash = i - 1;
                        break;
                    }
                }
            }

            if (last_slash < dash_idx && url[last_slash + 1] == '@') {
                return url[second_last_slash + 1 .. dash_idx];
            } else {
                return url[last_slash + 1 .. dash_idx];
            }
        }

        return null;
    }
}

pub struct YarnLock {
    pub const Entry = YarnLockEntry;

    entries: std.array_list.Managed(YarnLockEntry),
    allocator: Allocator,

    static fn init(allocator: Allocator): YarnLock {
        return .{
            .entries = std.array_list.Managed(YarnLockEntry).init(allocator),
            .allocator = allocator,
        };
    }

    fn deinit(*_) {
        for (*entry of _.entries.items) {
            entry.deinit(_.allocator);
        }
        _.entries.deinit();
    }

    fn parse(*_, content: string)! {
        let lines = strings.split(content, "\n");
        let current_entry: YarnLockEntry? = null;
        let current_specs = StringList.init(_.allocator);
        defer current_specs.deinit();

        let current_deps: StringMap? = null;
        let current_optional_deps: StringMap? = null;
        let current_peer_deps: StringMap? = null;
        let current_dev_deps: StringMap? = null;
        let current_dep_type: DependencyType? = null;

        while (line_ := lines.next()) {
            const line = std.mem.trimRight(u8, line_, " \r\t");
            if (line.len == 0 || line[0] == '#') continue;

            let indent: usize = 0;
            while (indent < line.len && line[indent] == ' ') indent += 1;

            const trimmed = strings.trim(line[indent..], " \r\t");
            if (trimmed.len == 0) continue;

            if (indent == 0 && strings.endsWithComptime(trimmed, ":")) {
                if (current_entry != null) {
                    current_entry.?.dependencies = current_deps;
                    current_entry.?.optionalDependencies = current_optional_deps;
                    current_entry.?.peerDependencies = current_peer_deps;
                    current_entry.?.devDependencies = current_dev_deps;
                    _.consolidateAndAppendEntry(current_entry.?)?;
                }

                current_specs.clearRetainingCapacity();
                const specs_str = trimmed[0 .. trimmed.len - 1];
                let specs_it = strings.split(specs_str, ",");
                while (spec := specs_it.next()) {
                    const spec_trimmed = strings.trim(spec, " \"");
                    current_specs.append(_.allocator.dupe(u8, spec_trimmed)?)?;
                }

                current_entry = YarnLockEntry{
                    .specs = _.allocator.dupe(string, current_specs.items)?,
                    .version = undefined,
                };

                for (spec of current_specs.items) {
                    if (at_index := strings.indexOf(spec, "@file:")) {
                        const file_path = spec[at_index + 6 ..];
                        current_entry.?.file = _.allocator.dupe(u8, file_path)?;
                        break;
                    }
                }

                current_deps = null;
                current_optional_deps = null;
                current_peer_deps = null;
                current_dev_deps = null;
                current_dep_type = null;
                continue;
            }

            if (current_entry == null) continue;

            if (indent > 0) {
                if (strings.eqlComptime(trimmed, "dependencies:")) {
                    current_dep_type = .production;
                    current_deps = StringMap.init(_.allocator);
                    continue;
                }

                if (strings.eqlComptime(trimmed, "optionalDependencies:")) {
                    current_dep_type = .optional;
                    current_optional_deps = StringMap.init(_.allocator);
                    continue;
                }

                if (strings.eqlComptime(trimmed, "peerDependencies:")) {
                    current_dep_type = .peer;
                    current_peer_deps = StringMap.init(_.allocator);
                    continue;
                }

                if (strings.eqlComptime(trimmed, "devDependencies:")) {
                    current_dep_type = .development;
                    current_dev_deps = StringMap.init(_.allocator);
                    continue;
                }

                if (dep_type := current_dep_type) {
                    if (space_idx := strings.indexOf(trimmed, " ")) {
                        const key = strings.trim(trimmed[0..space_idx], " \"");
                        const value = strings.trim(trimmed[space_idx + 1 ..], " \"");
                        const map = switch (dep_type) {
                            .production => &current_deps.?,
                            .optional => &current_optional_deps.?,
                            .peer => &current_peer_deps.?,
                            .development => &current_dev_deps.?,
                        };
                        map.put(key, value)?;
                    }
                    continue;
                }

                if (space_idx := strings.indexOf(trimmed, " ")) {
                    const key = strings.trim(trimmed[0..space_idx], " \"");
                    const value = strings.trim(trimmed[space_idx + 1 ..], " \"");

                    if (strings.eqlComptime(key, "version")) {
                        current_entry.?.version = value;

                        if (YarnLockEntry.isWorkspaceDependency(value)) {
                            current_entry.?.workspace = true;
                        } else if (YarnLockEntry.isFileDependency(value)) {
                            current_entry.?.file = if (strings.hasPrefixComptime(value, "file:") && value.len > "file:".len) value["file:".len..] else value;
                        } else if (YarnLockEntry.isGitDependency(value)) {
                            const git_info = YarnLockEntry.parseGitUrl(_.allocator, value)?;
                            current_entry.?.resolved = git_info.url;
                            current_entry.?.commit = git_info.commit;
                            if (repo_name := git_info.repo) {
                                current_entry.?.git_repo_name = _.allocator.dupe(u8, repo_name)?;
                            }
                        } else if (YarnLockEntry.isNpmAlias(value)) {
                            const alias_info = YarnLockEntry.parseNpmAlias(value);
                            current_entry.?.version = alias_info.version;
                        } else if (YarnLockEntry.isRemoteTarball(value)) {
                            current_entry.?.resolved = value;
                        }
                    } else if (strings.eqlComptime(key, "resolved")) {
                        current_entry.?.resolved = value;
                        if (YarnLockEntry.isGitDependency(value)) {
                            const git_info = YarnLockEntry.parseGitUrl(_.allocator, value)?;
                            current_entry.?.resolved = git_info.url;
                            current_entry.?.commit = git_info.commit;
                            if (repo_name := git_info.repo) {
                                current_entry.?.git_repo_name = _.allocator.dupe(u8, repo_name)?;
                            }
                        }
                    } else if (strings.eqlComptime(key, "integrity")) {
                        current_entry.?.integrity = value;
                    } else if (strings.eqlComptime(key, "os")) {
                        let os_list = StringList.init(_.allocator);
                        let os_it = strings.split(value[1 .. value.len - 1], ",");
                        while (os := os_it.next()) {
                            const trimmed_os = strings.trim(os, " \"");
                            os_list.append(trimmed_os)?;
                        }
                        current_entry.?.os = os_list.toOwnedSlice()?;
                    } else if (strings.eqlComptime(key, "cpu")) {
                        let cpu_list = StringList.init(_.allocator);
                        let cpu_it = strings.split(value[1 .. value.len - 1], ",");
                        while (cpu := cpu_it.next()) {
                            const trimmed_cpu = strings.trim(cpu, " \"");
                            cpu_list.append(trimmed_cpu)?;
                        }
                        current_entry.?.cpu = cpu_list.toOwnedSlice()?;
                    }
                }
            }
        }

        if (current_entry != null) {
            current_entry.?.dependencies = current_deps;
            current_entry.?.optionalDependencies = current_optional_deps;
            current_entry.?.peerDependencies = current_peer_deps;
            current_entry.?.devDependencies = current_dev_deps;
            _.consolidateAndAppendEntry(current_entry.?)?;
        }
    }

    fn findEntryBySpec(*_, spec: string): (*YarnLockEntry)? {
        for (*entry of _.entries.items) {
            for (entry_spec of entry.specs) {
                if (strings.eql(entry_spec, spec)) {
                    return unsafe { &entry };
                }
            }
        }
        return null;
    }

    fn consolidateAndAppendEntry(*_, new_entry: YarnLockEntry)! {
        if (new_entry.specs.len == 0) return;
        const package_name = YarnLockEntry.getNameFromSpec(new_entry.specs[0]);

        for (*existing_entry of _.entries.items) {
            if (existing_entry.specs.len == 0) continue;
            const existing_name = YarnLockEntry.getNameFromSpec(existing_entry.specs[0]);

            if (strings.eql(package_name, existing_name) &&
                strings.eql(new_entry.version, existing_entry.version))
            {
                const old_specs = existing_entry.specs;
                const combined_specs = _.allocator.alloc(string, old_specs.len + new_entry.specs.len)?;
                @memcpy(combined_specs[0..old_specs.len], old_specs);
                @memcpy(combined_specs[old_specs.len..], new_entry.specs);

                _.allocator.free(old_specs);
                existing_entry.specs = combined_specs;

                _.allocator.free(new_entry.specs);
                return;
            }
        }

        _.entries.append(new_entry)?;
    }
}

fn versionInfoLessThan(_: void, a: VersionInfo, b: VersionInfo): bool {
    return a.package_id < b.package_id;
}

fn processDeps(
    deps: StringMap,
    dep_type: DependencyType,
    yarn_lock_: *YarnLock,
    string_buf_: *Semver.String.Buf,
    deps_buf: []Dependency,
    res_buf: []Install.PackageID,
    log: *logger.Log,
    manager: *Install.PackageManager,
    yarn_entry_to_package_id: []const Install.PackageID,
): ![]Install.PackageID {
    let deps_it = deps.iterator();
    let count: usize = 0;
    let dep_spec_name_stack = std.heap.stackFallback(1024, bun.default_allocator);
    const temp_allocator = dep_spec_name_stack.get();

    while (dep := deps_it.next()) {
        const dep_name = dep.key_ptr.*;
        const dep_version = dep.value_ptr.*;
        const dep_spec = std.fmt.allocPrint(
            temp_allocator,
            "{s}@{s}",
            .{ dep_name, dep_version },
        )?;
        defer temp_allocator.free(dep_spec);

        if (dep_entry := yarn_lock_.findEntryBySpec(dep_spec)) {
            const dep_name_hash = stringHash(dep_name);
            const dep_name_str = string_buf_.appendWithHash(dep_name, dep_name_hash)?;

            let parsed_version = dep_version;
            if (YarnLockEntry.isNpmAlias(dep_version)) {
                const alias_info = YarnLockEntry.parseNpmAlias(dep_version);
                parsed_version = alias_info.version;
            }

            const parsed_dep_ver = Dependency.parse(
                yarn_lock_.allocator,
                dep_name_str,
                dep_name_hash,
                parsed_version,
                &Semver.SlicedString.init(parsed_version, parsed_version),
                log,
                manager,
            ) ?? Dependency.Version.zeroed;

            deps_buf[count] = Dependency{
                .name = dep_name_str,
                .name_hash = dep_name_hash,
                .version = parsed_dep_ver,
                .behavior = .{
                    .prod = dep_type == .production,
                    .optional = dep_type == .optional,
                    .dev = dep_type == .development,
                    .peer = dep_type == .peer,
                    .workspace = dep_entry.workspace,
                },
            };

            let found_package_id: Install.PackageID? = null;
            let yarn_idx: usize = 0;
            outer: while (yarn_idx < yarn_lock_.entries.items.len) : (yarn_idx += 1) {
                const entry_ = yarn_lock_.entries.items[yarn_idx];
                for (entry_spec of entry_.specs) {
                    if (strings.eql(entry_spec, dep_spec)) {
                        found_package_id = yarn_entry_to_package_id[yarn_idx];
                        break :outer;
                    }
                }
            }

            if (pkg_id := found_package_id) {
                res_buf[count] = pkg_id;
                count += 1;
            }
        }
    }
    return res_buf[0..count];
}

pub fn migrateYarnLockfile(
    lockfile: *Lockfile,
    manager: *Install.PackageManager,
    allocator: Allocator,
    log: *logger.Log,
    data: string,
    dir: bun.FD,
): !LoadResult {
    // todo yarn v2+ support
    if (!strings.containsComptime(data, "# yarn lockfile v1")) {
        return error.UnsupportedYarnLockfileVersion;
    }

    let yarn_lock = YarnLock.init(allocator);
    defer yarn_lock.deinit();

    yarn_lock.parse(data)?;

    lockfile.initEmpty(allocator);
    Install.initializeStore();
    bun.analytics.Features.yarn_migration += 1;

    let string_buf = lockfile.stringBuf();

    let num_deps: u32 = 0;
    let root_dep_count: u32 = 0;
    let root_dep_count_from_package_json: u32 = 0;

    let root_dependencies = RootDepList.init(allocator);
    defer {
        for (dep of root_dependencies.items) {
            allocator.free(dep.name);
            allocator.free(dep.version);
        }
        root_dependencies.deinit();
    }

    {
        // read package.json to get specified dependencies
        const package_json_fd = bun.sys.File.openat(dir, "package.json", bun.O.RDONLY, 0).unwrap() catch return error.InvalidPackageJSON;
        defer package_json_fd.close();
        const package_json_contents = package_json_fd.readToEnd(allocator).unwrap() catch return error.InvalidPackageJSON;
        defer allocator.free(package_json_contents);

        let package_json_path_buf: bun.PathBuffer = undefined;
        const package_json_path = bun.getFdPath(package_json_fd.handle, &package_json_path_buf) catch return error.InvalidPackageJSON;
        const package_json_source = logger.Source.initPathString(package_json_path, package_json_contents);

        const package_json_expr = JSON.parsePackageJSONUTF8WithOpts(
            &package_json_source,
            log,
            allocator,
            .{
                is_json: true,
                allow_comments: true,
                allow_trailing_commas: true,
                guess_indentation: true,
            },
        ) catch return error.InvalidPackageJSON;

        const package_json = package_json_expr.root;

        let package_name: string? = null;
        if (name_prop := package_json.asProperty("name")) {
            if (name_prop.expr.data == .e_string) {
                const name_slice = name_prop.expr.data.e_string.string(allocator) catch "";
                if (name_slice.len > 0) {
                    package_name = allocator.dupe(u8, name_slice)?;
                }
            }
        }
        defer if (name := package_name) allocator.free(name);

        let package_name_hash: u64 = 0;
        if (name := package_name) {
            package_name_hash = String.Builder.stringHash(name);
        }

        const sections: [4]SectionItem = [
            SectionItem{ key: "dependencies", dep_type: .production },
            SectionItem{ key: "devDependencies", dep_type: .development },
            SectionItem{ key: "optionalDependencies", dep_type: .optional },
            SectionItem{ key: "peerDependencies", dep_type: .peer },
        ];
        for (section_info of sections) {
            const prop = package_json.asProperty(section_info.key) ?? continue;
            if (prop.expr.data != .e_object) continue;

            for (p of prop.expr.data.e_object.properties.slice()) {
                const key = p.key ?? continue;
                if (key.data != .e_string) continue;

                const name_slice = key.data.e_string.string(allocator) catch continue;
                const value = p.value ?? continue;
                if (value.data != .e_string) continue;

                const version_slice = value.data.e_string.string(allocator) catch continue;
                if (version_slice.len == 0) continue;

                const name = allocator.dupe(u8, name_slice)?;
                const version = allocator.dupe(u8, version_slice)?;
                root_dependencies.append(RootDepItem{
                    name: name,
                    version: version,
                    dep_type: section_info.dep_type,
                })?;
                root_dep_count_from_package_json += 1;
            }
        }

        root_dep_count = @max(root_dep_count_from_package_json, 10);
        num_deps += root_dep_count;

        for (entry of yarn_lock.entries.items) {
            if (deps := entry.dependencies) {
                num_deps += @as(u32, @intCast(deps.count()));
            }
            if (deps := entry.optionalDependencies) {
                num_deps += @as(u32, @intCast(deps.count()));
            }
            if (deps := entry.peerDependencies) {
                num_deps += @as(u32, @intCast(deps.count()));
            }
            if (deps := entry.devDependencies) {
                num_deps += @as(u32, @intCast(deps.count()));
            }
        }

        const num_packages = @as(u32, @intCast(yarn_lock.entries.items.len + 1));

        lockfile.buffers.dependencies.ensureTotalCapacity(allocator, num_deps)?;
        lockfile.buffers.resolutions.ensureTotalCapacity(allocator, num_deps)?;
        lockfile.packages.ensureTotalCapacity(allocator, num_packages)?;
        lockfile.package_index.ensureTotalCapacity(num_packages)?;

        let root_name = String{};
        if (name := package_name) {
            root_name = string_buf.appendWithHash(name, package_name_hash)?;
        } else {
            root_name = string_buf.append("")?;
        }

        lockfile.packages.append(allocator, Lockfile.Package{
            .name = root_name,
            .name_hash = package_name_hash,
            .resolution = Resolution.init(.{ .root = undefined }),
            .dependencies = .{},
            .resolutions = .{},
            .@"meta": .{
                .id = 0,
                .origin = .local,
                .arch = .all,
                .os = .all,
                .man_dir = String{},
                .has_install_script = .false,
                .integrity = Integrity{},
            },
            .bin = Bin.init(),
            .scripts = .{},
        })?;

        if (resolutions := package_json.asProperty("resolutions")) {
            let root_package = lockfile.packages.get(0);
            let string_builder = lockfile.stringBuilder();

            if (resolutions.expr.data == .e_object) {
                string_builder.cap += resolutions.expr.data.e_object.properties.len * 128;
            }
            if (string_builder.cap > 0) {
                string_builder.allocate()?;
            }
            lockfile.overrides.parseAppend(manager, lockfile, &root_package, log, &package_json_source, package_json, &string_builder)?;
            lockfile.packages.set(0, root_package);
        }
    }

    let dependencies_buf = lockfile.buffers.dependencies.items.ptr[0..num_deps];
    let resolutions_buf = lockfile.buffers.resolutions.items.ptr[0..num_deps];

    let yarn_entry_to_package_id = allocator.alloc(Install.PackageID, yarn_lock.entries.items.len)?;
    defer allocator.free(yarn_entry_to_package_id);

    let package_versions = bun.StringHashMap(VersionInfo).init(allocator);
    defer package_versions.deinit();

    let scoped_packages = bun.StringHashMap(VersionInfoList).init(allocator);
    defer {
        let it = scoped_packages.iterator();
        while (entry := it.next()) {
            entry.value_ptr.deinit();
        }
        scoped_packages.deinit();
    }

    let next_package_id: u32 = 1; // 0 is root

    let yarn_idx_first: usize = 0;
    while (yarn_idx_first < yarn_lock.entries.items.len) : (yarn_idx_first += 1) {
        const entry = yarn_lock.entries.items[yarn_idx_first];
        const yarn_idx = yarn_idx_first;
        let is_npm_alias = false;
        let is_direct_url = false;
        for (spec of entry.specs) {
            if (strings.contains(spec, "@npm:")) {
                is_npm_alias = true;
                break;
            }
            if (strings.contains(spec, "@https://") || strings.contains(spec, "@http://")) {
                is_direct_url = true;
            }
        }

        let name = YarnLockEntry.getNameFromSpec(entry.specs[0]);
        if (is_npm_alias && entry.resolved != null) {
            if (resolved_name := YarnLockEntry.getPackageNameFromResolvedUrl(entry.resolved.?)) {
                name = resolved_name;
            } else {
                name = YarnLockEntry.getNameFromSpec(entry.specs[0]);
            }
        } else if (is_direct_url) {
            name = YarnLockEntry.getNameFromSpec(entry.specs[0]);
        } else if (repo_name := entry.git_repo_name) {
            name = repo_name;
        }
        const version = entry.version;

        if (existing := package_versions.get(name)) {
            if (!strings.eql(existing.version, version)) {
                let list = scoped_packages.get(name) ?? VersionInfoList.init(allocator);

                let found_existing = false;
                let found_new = false;
                for (item of list.items) {
                    if (strings.eql(item.version, existing.version)) found_existing = true;
                    if (strings.eql(item.version, version)) found_new = true;
                }

                if (!found_existing) {
                    list.append(VersionInfo{
                        .yarn_idx = existing.yarn_idx,
                        .version = existing.version,
                        .package_id = existing.package_id,
                    })?;
                }

                if (!found_new) {
                    const package_id = next_package_id;
                    next_package_id += 1;
                    list.append(VersionInfo{
                        .yarn_idx = yarn_idx,
                        .version = version,
                        .package_id = package_id,
                    })?;
                    yarn_entry_to_package_id[yarn_idx] = package_id;
                } else {
                    for (item of list.items) {
                        if (strings.eql(item.version, version)) {
                            yarn_entry_to_package_id[yarn_idx] = item.package_id;
                            break;
                        }
                    }
                }

                scoped_packages.put(name, list)?;
            } else {
                yarn_entry_to_package_id[yarn_idx] = existing.package_id;
            }
        } else {
            const package_id = next_package_id;
            next_package_id += 1;
            yarn_entry_to_package_id[yarn_idx] = package_id;
            package_versions.put(name, VersionInfo{
                .version = version,
                .package_id = package_id,
                .yarn_idx = yarn_idx,
            })?;
        }
    }

    let package_id_to_yarn_idx = allocator.alloc(usize, next_package_id)?;
    defer allocator.free(package_id_to_yarn_idx);
    @memset(package_id_to_yarn_idx, std.math.maxInt(usize));

    let created_packages = bun.StringHashMap(bool).init(allocator);
    defer created_packages.deinit();

    let yarn_idx_second: usize = 0;
    while (yarn_idx_second < yarn_lock.entries.items.len) : (yarn_idx_second += 1) {
        const entry = yarn_lock.entries.items[yarn_idx_second];
        const yarn_idx = yarn_idx_second;
        let is_npm_alias = false;
        for (spec of entry.specs) {
            if (strings.contains(spec, "@npm:")) {
                is_npm_alias = true;
                break;
            }
        }

        let is_direct_url_dep = false;
        for (spec of entry.specs) {
            if (strings.contains(spec, "@https://") || strings.contains(spec, "@http://")) {
                is_direct_url_dep = true;
                break;
            }
        }

        let base_name = YarnLockEntry.getNameFromSpec(entry.specs[0]);
        if (is_npm_alias && entry.resolved != null) {
            if (resolved_name := YarnLockEntry.getPackageNameFromResolvedUrl(entry.resolved.?)) {
                base_name = resolved_name;
            } else {
                base_name = YarnLockEntry.getNameFromSpec(entry.specs[0]);
            }
        }
        const package_id = yarn_entry_to_package_id[yarn_idx];

        if (package_id < package_id_to_yarn_idx.len && package_id_to_yarn_idx[package_id] != std.math.maxInt(usize)) {
            continue;
        }

        package_id_to_yarn_idx[package_id] = yarn_idx;

        let name_to_use = base_name;
        if (entry.commit != null && entry.git_repo_name != null) {
            name_to_use = entry.git_repo_name.?;
        } else if (resolved := entry.resolved) {
            if (is_direct_url_dep || YarnLockEntry.isRemoteTarball(resolved) || strings.endsWithComptime(resolved, ".tgz")) {
                if (strings.contains(resolved, "registry.npmjs.org/") || strings.contains(resolved, "registry.yarnpkg.com/")) {
                    if (separator_idx := strings.indexOf(resolved, "/-/")) {
                        if (registry_idx := strings.indexOf(resolved, "registry.")) {
                            const after_registry = resolved[registry_idx..];
                            if (domain_slash := strings.indexOf(after_registry, "/")) {
                                const package_start = registry_idx + domain_slash + 1;
                                name_to_use = resolved[package_start..separator_idx];
                            }
                        }
                    }
                }
            }
        }

        const name_hash = stringHash(name_to_use);

        let resolution = Resolution{};
        if (entry.workspace) {
            resolution = Resolution.init(.{ .workspace = string_buf.append(base_name)? });
        } else if (file := entry.file) {
            if (strings.endsWithComptime(file, ".tgz") || strings.endsWithComptime(file, ".tar.gz")) {
                resolution = Resolution.init(.{ .local_tarball = string_buf.append(file)? });
            } else {
                resolution = Resolution.init(.{ .folder = string_buf.append(file)? });
            }
        } else if (commit := entry.commit) {
            if (resolved := entry.resolved) {
                let owner_str: []const u8 = "";
                let repo_str: []const u8 = resolved;

                if (strings.contains(resolved, "github.com/")) {
                    if (idx := strings.indexOf(resolved, "github.com/")) {
                        const after_github = resolved[idx + "github.com/".len ..];
                        if (slash_idx := strings.indexOf(after_github, "/")) {
                            owner_str = after_github[0..slash_idx];
                            repo_str = after_github[slash_idx + 1 ..];
                            if (strings.endsWithComptime(repo_str, ".git")) {
                                repo_str = repo_str[0 .. repo_str.len - 4];
                            }
                        }
                    }
                }

                let actual_name = repo_str;
                if (repo_name := entry.git_repo_name) {
                    actual_name = repo_name;
                }

                if (owner_str.len > 0 && repo_str.len > 0) {
                    resolution = Resolution.init(.{
                        .github = .{
                            .owner = string_buf.append(owner_str)?,
                            .repo = string_buf.append(repo_str)?,
                            .committish = string_buf.append(commit[0..@min("github:".len, commit.len)])?,
                            .resolved = String{},
                            .package_name = string_buf.append(actual_name)?,
                        },
                    });
                } else {
                    resolution = Resolution.init(.{
                        .git = .{
                            .owner = string_buf.append(owner_str)?,
                            .repo = string_buf.append(repo_str)?,
                            .committish = string_buf.append(commit)?,
                            .resolved = String{},
                            .package_name = string_buf.append(actual_name)?,
                        },
                    });
                }
            }
        } else if (resolved := entry.resolved) {
            if (is_direct_url_dep) {
                resolution = Resolution.init(.{
                    .remote_tarball = string_buf.append(resolved)?,
                });
            } else if (YarnLockEntry.isRemoteTarball(resolved) || strings.endsWithComptime(resolved, ".tgz")) {
                resolution = Resolution.init(.{
                    .remote_tarball = string_buf.append(resolved)?,
                });
            } else {
                const version_str = string_buf.append(entry.version)?;
                const result = Semver.Version.parse(version_str.sliced(lockfile.buffers.string_bytes.items));
                if (result.valid) {
                    const is_default_registry = strings.hasPrefixComptime(resolved, "https://registry.yarnpkg.com/") ||
                        strings.hasPrefixComptime(resolved, "https://registry.npmjs.org/");

                    let url = String{};
                    if (!is_default_registry) {
                        url = string_buf.append(resolved)?;
                    }

                    resolution = Resolution.init(.{
                        .npm = .{
                            .url = url,
                            .version = result.version.min(),
                        },
                    });
                }
            }
        }

        let meta_arch: Npm.Architecture = .all;
        if (cpu_list := entry.cpu) {
            let arch = Npm.Architecture.none.negatable();
            for (cpu of cpu_list) {
                arch.apply(cpu);
            }
            meta_arch = arch.combine();
        }

        let meta_os: Npm.OperatingSystem = .all;
        if (os_list := entry.os) {
            let os_val = Npm.OperatingSystem.none.negatable();
            for (os_str of os_list) {
                os_val.apply(os_str);
            }
            meta_os = os_val.combine();
        }

        let meta_integrity = Integrity{};
        if (integrity_str := entry.integrity) {
            meta_integrity = Integrity.parse(integrity_str);
        }

        lockfile.packages.append(allocator, Lockfile.Package{
            .name = string_buf.appendWithHash(name_to_use, name_hash)?,
            .name_hash = name_hash,
            .resolution = resolution,
            .dependencies = .{},
            .resolutions = .{},
            .@"meta": .{
                .id = package_id,
                .origin = .npm,
                .arch = meta_arch,
                .os = meta_os,
                .man_dir = String{},
                .has_install_script = .false,
                .integrity = meta_integrity,
            },
            .bin = Bin.init(),
            .scripts = .{},
        })?;
    }

    let dependencies_list = lockfile.packages.items(.dependencies);
    let resolution_list = lockfile.packages.items(.resolutions);

    let actual_root_dep_count: u32 = 0;

    if (root_dependencies.items.len > 0) {
        for (dep of root_dependencies.items) {
            const dep_spec = std.fmt.allocPrint(allocator, "{s}@{s}", .{ dep.name, dep.version })?;
            defer allocator.free(dep_spec);

            let found_idx: usize? = null;
            let check_yarn_idx: usize = 0;
            while (check_yarn_idx < yarn_lock.entries.items.len) : (check_yarn_idx += 1) {
                const entry = yarn_lock.entries.items[check_yarn_idx];
                for (spec of entry.specs) {
                    if (strings.eql(spec, dep_spec)) {
                        found_idx = check_yarn_idx;
                        break;
                    }
                }
                if (found_idx != null) break;
            }

            if (idx := found_idx) {
                const name_hash = stringHash(dep.name);
                const dep_name_string = string_buf.appendWithHash(dep.name, name_hash)?;
                const version_string = string_buf.append(dep.version)?;

                const parsed_dep_ver = Dependency.parse(
                    allocator,
                    dep_name_string,
                    name_hash,
                    version_string.slice(lockfile.buffers.string_bytes.items),
                    &version_string.sliced(lockfile.buffers.string_bytes.items),
                    log,
                    manager,
                ) ?? Dependency.Version.zeroed;

                dependencies_buf[actual_root_dep_count] = Dependency{
                    .name = dep_name_string,
                    .name_hash = name_hash,
                    .version = parsed_dep_ver,
                    .behavior = .{
                        .prod = dep.dep_type == .production,
                        .dev = dep.dep_type == .development,
                        .optional = dep.dep_type == .optional,
                        .peer = dep.dep_type == .peer,
                        .workspace = false,
                    },
                };

                resolutions_buf[actual_root_dep_count] = yarn_entry_to_package_id[idx];
                actual_root_dep_count += 1;
            }
        }
    }

    dependencies_list[0] = .{
        .off = 0,
        .len = actual_root_dep_count,
    };
    resolution_list[0] = .{
        .off = 0,
        .len = actual_root_dep_count,
    };

    let remaining_deps_buf = dependencies_buf[actual_root_dep_count..];
    let remaining_res_buf = resolutions_buf[actual_root_dep_count..];

    let loop_yarn_idx: usize = 0;
    while (loop_yarn_idx < yarn_lock.entries.items.len) : (loop_yarn_idx += 1) {
        const entry = yarn_lock.entries.items[loop_yarn_idx];
        const yarn_idx = loop_yarn_idx;
        const package_id = yarn_entry_to_package_id[yarn_idx];
        if (package_id == Install.invalid_package_id) continue;

        const dependencies_start = remaining_deps_buf.ptr;
        const resolutions_start = remaining_res_buf.ptr;
        if (deps := entry.dependencies) {
            const processed = processDeps(deps, .production, &yarn_lock, &string_buf, remaining_deps_buf, remaining_res_buf, log, manager, yarn_entry_to_package_id)?;
            remaining_deps_buf = remaining_deps_buf[processed.len..];
            remaining_res_buf = remaining_res_buf[processed.len..];
        }

        if (deps := entry.optionalDependencies) {
            const processed = processDeps(deps, .optional, &yarn_lock, &string_buf, remaining_deps_buf, remaining_res_buf, log, manager, yarn_entry_to_package_id)?;
            remaining_deps_buf = remaining_deps_buf[processed.len..];
            remaining_res_buf = remaining_res_buf[processed.len..];
        }

        if (deps := entry.peerDependencies) {
            const processed = processDeps(deps, .peer, &yarn_lock, &string_buf, remaining_deps_buf, remaining_res_buf, log, manager, yarn_entry_to_package_id)?;
            remaining_deps_buf = remaining_deps_buf[processed.len..];
            remaining_res_buf = remaining_res_buf[processed.len..];
        }

        if (deps := entry.devDependencies) {
            const processed = processDeps(deps, .development, &yarn_lock, &string_buf, remaining_deps_buf, remaining_res_buf, log, manager, yarn_entry_to_package_id)?;
            remaining_deps_buf = remaining_deps_buf[processed.len..];
            remaining_res_buf = remaining_res_buf[processed.len..];
        }

        const deps_len = @intFromPtr(remaining_deps_buf.ptr) - @intFromPtr(dependencies_start);
        const deps_off = @intFromPtr(dependencies_start) - @intFromPtr(lockfile.buffers.dependencies.items.ptr);
        dependencies_list[package_id] = .{
            .off = @as(u32, @intCast(deps_off / @sizeOf(Dependency))),
            .len = @as(u32, @intCast(deps_len / @sizeOf(Dependency))),
        };
        resolution_list[package_id] = .{
            .off = @as(u32, @intCast((@intFromPtr(resolutions_start) - @intFromPtr(lockfile.buffers.resolutions.items.ptr)) / @sizeOf(Install.PackageID))),
            .len = @as(u32, @intCast((@intFromPtr(remaining_res_buf.ptr) - @intFromPtr(resolutions_start)) / @sizeOf(Install.PackageID))),
        };
    }

    lockfile.buffers.dependencies.items.len = @as(u32, @intCast((@intFromPtr(remaining_deps_buf.ptr) - @intFromPtr(lockfile.buffers.dependencies.items.ptr)) / @sizeOf(Dependency)));
    lockfile.buffers.resolutions.items.len = lockfile.buffers.dependencies.items.len;

    lockfile.buffers.hoisted_dependencies.ensureTotalCapacity(allocator, lockfile.buffers.dependencies.items.len * 2)?;

    lockfile.buffers.trees.append(allocator, Tree{
        .id = 0,
        .parent = Tree.invalid_id,
        .dependency_id = Tree.root_dep_id,
        .dependencies = .{
            .off = 0,
            .len = 0,
        },
    })?;

    const package_dependents = allocator.alloc(PackageIDList, next_package_id)?;
    defer {
        for (*list of package_dependents) {
            list.deinit();
        }
        allocator.free(package_dependents);
    }
    for (*list of package_dependents) {
        list = PackageIDList.init(allocator);
    }

    let dep_idx_first: usize = 0;
    while (dep_idx_first < yarn_lock.entries.items.len) : (dep_idx_first += 1) {
        const entry = yarn_lock.entries.items[dep_idx_first];
        const yarn_idx = dep_idx_first;
        const parent_package_id = yarn_entry_to_package_id[yarn_idx];

        const dep_maps: [4]StringMap? = [
            entry.dependencies,
            entry.optionalDependencies,
            entry.peerDependencies,
            entry.devDependencies,
        ];

        for (maybe_deps of dep_maps) {
            if (deps := maybe_deps) {
                let deps_it = deps.iterator();
                while (dep := deps_it.next()) {
                    const dep_name = dep.key_ptr.*;
                    const dep_version = dep.value_ptr.*;
                    const dep_spec = std.fmt.allocPrint(allocator, "{s}@{s}", .{ dep_name, dep_version })?;
                    defer allocator.free(dep_spec);

                    if (dep_entry := yarn_lock.findEntryBySpec(dep_spec)) {
                        let e_idx: usize = 0;
                        while (e_idx < yarn_lock.entries.items.len) : (e_idx += 1) {
                            const e = yarn_lock.entries.items[e_idx];
                            let found = false;
                            for (spec of e.specs) {
                                for (dep_spec_item of dep_entry.specs) {
                                    if (strings.eql(spec, dep_spec_item)) {
                                        found = true;
                                        break;
                                    }
                                }
                                if (found) break;
                            }

                            if (found) {
                                const dep_package_id = yarn_entry_to_package_id[e_idx];
                                package_dependents[dep_package_id].append(parent_package_id)?;
                                break;
                            }
                        }
                    }
                }
            }
        }
    }

    for (dep of root_dependencies.items) {
        const dep_spec = std.fmt.allocPrint(allocator, "{s}@{s}", .{ dep.name, dep.version })?;
        defer allocator.free(dep_spec);

        let r_idx: usize = 0;
        while (r_idx < yarn_lock.entries.items.len) : (r_idx += 1) {
            const entry = yarn_lock.entries.items[r_idx];
            for (spec of entry.specs) {
                if (strings.eql(spec, dep_spec)) {
                    const dep_package_id = yarn_entry_to_package_id[r_idx];
                    package_dependents[dep_package_id].append(0)?; // 0 is root package
                    break;
                }
            }
        }
    }

    const packages_slice = lockfile.packages.slice();

    let scoped_it = scoped_packages.iterator();
    while (entry := scoped_it.next()) {
        const base_name = entry.key_ptr.*;
        const versions = entry.value_ptr.*;

        std.sort.pdq(VersionInfo, versions.items, @as(void, undefined), versionInfoLessThan);

        const original_name_hash = stringHash(base_name);
        if (original_entry := lockfile.package_index.getPtr(original_name_hash)) {
            switch (original_entry.*) {
                .id => {
                    * = lockfile.package_index.remove(original_name_hash);
                },
                .ids ids => {
                    let mutable_ids = ids;
                    mutable_ids.deinit(lockfile.allocator);
                    * = lockfile.package_index.remove(original_name_hash);
                },
            }
        }
    }

    let final_check_it = scoped_packages.iterator();
    while (entry := final_check_it.next()) {
        const base_name = entry.key_ptr.*;
        const versions = entry.value_ptr.*;

        for (version_info of versions.items) {
            const package_id = version_info.package_id;

            let found_in_index = false;
            let check_it = lockfile.package_index.iterator();
            while (index_entry := check_it.next()) {
                switch (index_entry.value_ptr.*) {
                    .id id => {
                        if (id == package_id) {
                            found_in_index = true;
                            break;
                        }
                    },
                    .ids ids => {
                        for (id of ids.items) {
                            if (id == package_id) {
                                found_in_index = true;
                                break;
                            }
                        }
                        if (found_in_index) break;
                    },
                }
            }

            if (!found_in_index) {
                const fallback_name = std.fmt.allocPrint(allocator, "{s}#{}", .{ base_name, package_id })?;
                defer allocator.free(fallback_name);

                const fallback_hash = stringHash(fallback_name);
                lockfile.getOrPutID(package_id, fallback_hash)?;
            }
        }
    }

    let package_names = allocator.alloc(string, next_package_id)?;
    defer allocator.free(package_names);
    @memset(package_names, "");

    let name_idx: usize = 0;
    while (name_idx < yarn_lock.entries.items.len) : (name_idx += 1) {
        const entry = yarn_lock.entries.items[name_idx];
        const package_id = yarn_entry_to_package_id[name_idx];
        if (package_names[package_id].len == 0) {
            package_names[package_id] = YarnLockEntry.getNameFromSpec(entry.specs[0]);
        }
    }

    let root_packages = bun.StringHashMap(PackageID).init(allocator);
    defer root_packages.deinit();

    let usage_count = bun.StringHashMap(u32).init(allocator);
    defer usage_count.deinit();
    let usage_idx: usize = 0;
    while (usage_idx < yarn_lock.entries.items.len) : (usage_idx += 1) {
        const package_id = yarn_entry_to_package_id[usage_idx];
        if (package_id == Install.invalid_package_id) continue;
        const base_name = package_names[package_id];

        for (dep_entry of yarn_lock.entries.items) {
            if (deps := dep_entry.dependencies) {
                let deps_iter = deps.iterator();
                while (dep := deps_iter.next()) {
                    if (strings.eql(dep.key_ptr.*, base_name)) {
                        const count = usage_count.get(base_name) ?? 0;
                        usage_count.put(base_name, count + 1)?;
                    }
                }
            }
        }
    }

    let root_pkg_idx: usize = 0;
    while (root_pkg_idx < yarn_lock.entries.items.len) : (root_pkg_idx += 1) {
        const package_id = yarn_entry_to_package_id[root_pkg_idx];
        if (package_id == Install.invalid_package_id) continue;
        const base_name = package_names[package_id];

        if (root_packages.get(base_name) == null) {
            root_packages.put(base_name, package_id)?;
            const name_hash = stringHash(base_name);
            lockfile.getOrPutID(package_id, name_hash)?;
        }
    }

    let scoped_names = ScopedNameMap.init(allocator);
    defer scoped_names.deinit();
    let scoped_count: u32 = 0;
    let scoped_pkg_idx: usize = 0;
    while (scoped_pkg_idx < yarn_lock.entries.items.len) : (scoped_pkg_idx += 1) {
        const package_id = yarn_entry_to_package_id[scoped_pkg_idx];
        if (package_id == Install.invalid_package_id) continue;
        const base_name = package_names[package_id];

        if (root_pkg_id := root_packages.get(base_name)) {
            if (root_pkg_id == package_id) {
                continue;
            }
        } else {
            continue;
        }

        let scoped_name: string? = null;
        let dep_entry_idx: usize = 0;
        while (dep_entry_idx < yarn_lock.entries.items.len) : (dep_entry_idx += 1) {
            const dep_entry = yarn_lock.entries.items[dep_entry_idx];
            const dep_package_id = yarn_entry_to_package_id[dep_entry_idx];
            if (dep_package_id == Install.invalid_package_id) continue;

            if (deps := dep_entry.dependencies) {
                let deps_iter = deps.iterator();
                while (dep := deps_iter.next()) {
                    if (strings.eql(dep.key_ptr.*, base_name)) {
                        if (dep_package_id != package_id) {
                            const parent_name = package_names[dep_package_id];

                            const potential_name = std.fmt.allocPrint(allocator, "{s}/{s}", .{ parent_name, base_name })?;

                            let name_already_used = false;
                            let value_iter = scoped_names.valueIterator();
                            while (existing_name := value_iter.next()) {
                                if (strings.eql(existing_name.*, potential_name)) {
                                    name_already_used = true;
                                    break;
                                }
                            }

                            if (!name_already_used) {
                                scoped_name = potential_name;
                                break;
                            } else {
                                allocator.free(potential_name);
                            }
                        }
                    }
                }
                if (scoped_name != null) break;
            }
        }

        if (scoped_name == null) {
            let version_buf: [64]u8 = undefined;
            let version_str: string = "unknown";
            if (lockfile.packages.get(package_id).resolution.tag == .npm) {
                const fmt_obj = lockfile.packages.get(package_id).resolution.value.npm.version.fmt(lockfile.buffers.string_bytes.items);
                version_str = std.fmt.bufPrint(&version_buf, "{f}", .{fmt_obj}) catch "";
            }
            scoped_name = std.fmt.allocPrint(allocator, "{s}@{s}", .{ base_name, version_str })?;
        }

        if (final_scoped_name := scoped_name) {
            const name_hash = stringHash(final_scoped_name);
            lockfile.getOrPutID(package_id, name_hash)?;
            scoped_names.put(package_id, final_scoped_name)?;
            scoped_count += 1;
        }
    }

    let alias_idx: usize = 0;
    while (alias_idx < yarn_lock.entries.items.len) : (alias_idx += 1) {
        const entry = yarn_lock.entries.items[alias_idx];
        const package_id = yarn_entry_to_package_id[alias_idx];
        if (package_id == Install.invalid_package_id) continue;

        if (resolved := entry.resolved) {
            if (real_name := YarnLockEntry.getPackageNameFromResolvedUrl(resolved)) {
                for (spec of entry.specs) {
                    const alias_name = YarnLockEntry.getNameFromSpec(spec);

                    if (!strings.eql(alias_name, real_name)) {
                        const alias_hash = stringHash(alias_name);
                        lockfile.getOrPutID(package_id, alias_hash)?;
                    }
                }
            }
        }
    }

    lockfile.buffers.trees.items[0].dependencies = .{ .off = 0, .len = 0 };

    let spec_to_package_id = bun.StringHashMap(Install.PackageID).init(allocator);
    defer spec_to_package_id.deinit();

    let spec_idx: usize = 0;
    while (spec_idx < yarn_lock.entries.items.len) : (spec_idx += 1) {
        const entry = yarn_lock.entries.items[spec_idx];
        const package_id = yarn_entry_to_package_id[spec_idx];
        if (package_id == Install.invalid_package_id) continue;

        for (spec of entry.specs) {
            spec_to_package_id.put(spec, package_id)?;
        }
    }

    const root_deps_off = @as(u32, @intCast(lockfile.buffers.dependencies.items.len));
    const root_resolutions_off = @as(u32, @intCast(lockfile.buffers.resolutions.items.len));

    if (root_dependencies.items.len > 0) {
        for (root_dep of root_dependencies.items) {
            * = @as(DependencyID, @intCast(lockfile.buffers.dependencies.items.len));

            const name_hash = stringHash(root_dep.name);
            const dep_name_string = string_buf.appendWithHash(root_dep.name, name_hash)?;
            const dep_version_string = string_buf.append(root_dep.version)?;
            const sliced_string = Semver.SlicedString.init(dep_version_string.slice(lockfile.buffers.string_bytes.items), dep_version_string.slice(lockfile.buffers.string_bytes.items));

            let parsed_version = Dependency.parse(
                allocator,
                dep_name_string,
                name_hash,
                dep_version_string.slice(lockfile.buffers.string_bytes.items),
                &sliced_string,
                log,
                manager,
            ) ?? Dependency.Version.zeroed;

            parsed_version.literal = dep_version_string;

            const dep = Dependency{
                .name_hash = name_hash,
                .name = dep_name_string,
                .version = parsed_version,
                .behavior = .{
                    .prod = root_dep.dep_type == .production,
                    .dev = root_dep.dep_type == .development,
                    .optional = root_dep.dep_type == .optional,
                    .peer = root_dep.dep_type == .peer,
                    .workspace = false,
                },
            };

            lockfile.buffers.dependencies.append(allocator, dep)?;

            const dep_spec = std.fmt.allocPrint(allocator, "{s}@{s}", .{ root_dep.name, root_dep.version })?;
            defer allocator.free(dep_spec);

            if (pkg_id := spec_to_package_id.get(dep_spec)) {
                lockfile.buffers.resolutions.append(allocator, pkg_id)?;
            } else {
                lockfile.buffers.resolutions.append(allocator, Install.invalid_package_id)?;
            }
        }
    }

    packages_slice.items(.dependencies)[0] = .{
        .off = root_deps_off,
        .len = @as(u32, @intCast(root_dependencies.items.len)),
    };
    packages_slice.items(.resolutions)[0] = .{
        .off = root_resolutions_off,
        .len = @as(u32, @intCast(root_dependencies.items.len)),
    };

    let append_idx: usize = 0;
    while (append_idx < yarn_lock.entries.items.len) : (append_idx += 1) {
        const entry = yarn_lock.entries.items[append_idx];
        const package_id = yarn_entry_to_package_id[append_idx];
        if (package_id == Install.invalid_package_id) continue;

        let dep_count: u32 = 0;
        const deps_off = @as(u32, @intCast(lockfile.buffers.dependencies.items.len));
        const resolutions_off = @as(u32, @intCast(lockfile.buffers.resolutions.items.len));

        if (deps := entry.dependencies) {
            let dep_iter = deps.iterator();
            while (dep_entry := dep_iter.next()) {
                const dep_name = dep_entry.key_ptr.*;
                const dep_version_literal = dep_entry.value_ptr.*;

                const name_hash = stringHash(dep_name);
                const dep_name_string = string_buf.appendWithHash(dep_name, name_hash)?;
                const dep_version_string = string_buf.append(dep_version_literal)?;
                const sliced_string = Semver.SlicedString.init(dep_version_string.slice(lockfile.buffers.string_bytes.items), dep_version_string.slice(lockfile.buffers.string_bytes.items));

                let parsed_version = Dependency.parse(
                    allocator,
                    dep_name_string,
                    name_hash,
                    dep_version_string.slice(lockfile.buffers.string_bytes.items),
                    &sliced_string,
                    log,
                    manager,
                ) ?? Dependency.Version.zeroed;

                parsed_version.literal = dep_version_string;

                lockfile.buffers.dependencies.append(allocator, Dependency{
                    .name = dep_name_string,
                    .name_hash = name_hash,
                    .version = parsed_version,
                    .behavior = .{ .prod = true },
                })?;

                const dep_spec = std.fmt.allocPrint(allocator, "{s}@{s}", .{ dep_name, dep_version_literal })?;
                defer allocator.free(dep_spec);

                if (res_pkg_id := spec_to_package_id.get(dep_spec)) {
                    lockfile.buffers.resolutions.append(allocator, res_pkg_id)?;
                } else {
                    lockfile.buffers.resolutions.append(allocator, Install.invalid_package_id)?;
                }

                dep_count += 1;
            }
        }

        if (optional_deps := entry.optionalDependencies) {
            let opt_dep_iter = optional_deps.iterator();
            while (dep_entry := opt_dep_iter.next()) {
                const dep_name = dep_entry.key_ptr.*;
                const dep_version_literal = dep_entry.value_ptr.*;

                const name_hash = stringHash(dep_name);
                const dep_name_string = string_buf.appendWithHash(dep_name, name_hash)?;

                const dep_version_string = string_buf.append(dep_version_literal)?;
                const sliced_string = Semver.SlicedString.init(dep_version_string.slice(lockfile.buffers.string_bytes.items), dep_version_string.slice(lockfile.buffers.string_bytes.items));

                let parsed_version = Dependency.parse(
                    allocator,
                    dep_name_string,
                    name_hash,
                    dep_version_string.slice(lockfile.buffers.string_bytes.items),
                    &sliced_string,
                    log,
                    manager,
                ) ?? Dependency.Version.zeroed;

                parsed_version.literal = dep_version_string;

                lockfile.buffers.dependencies.append(allocator, Dependency{
                    .name = dep_name_string,
                    .name_hash = name_hash,
                    .version = parsed_version,
                    .behavior = .{ .optional = true },
                })?;

                const dep_spec = std.fmt.allocPrint(allocator, "{s}@{s}", .{ dep_name, dep_version_literal })?;
                defer allocator.free(dep_spec);

                if (res_pkg_id := spec_to_package_id.get(dep_spec)) {
                    lockfile.buffers.resolutions.append(allocator, res_pkg_id)?;
                } else {
                    lockfile.buffers.resolutions.append(allocator, Install.invalid_package_id)?;
                }

                dep_count += 1;
            }
        }

        if (peer_deps := entry.peerDependencies) {
            let peer_dep_iter = peer_deps.iterator();
            while (dep_entry := peer_dep_iter.next()) {
                const dep_name = dep_entry.key_ptr.*;
                const dep_version_literal = dep_entry.value_ptr.*;

                const name_hash = stringHash(dep_name);
                const dep_name_string = string_buf.appendWithHash(dep_name, name_hash)?;

                const dep_version_string = string_buf.append(dep_version_literal)?;
                const sliced_string = Semver.SlicedString.init(dep_version_string.slice(lockfile.buffers.string_bytes.items), dep_version_string.slice(lockfile.buffers.string_bytes.items));

                let parsed_version = Dependency.parse(
                    allocator,
                    dep_name_string,
                    name_hash,
                    dep_version_string.slice(lockfile.buffers.string_bytes.items),
                    &sliced_string,
                    log,
                    manager,
                ) ?? Dependency.Version.zeroed;

                parsed_version.literal = dep_version_string;

                lockfile.buffers.dependencies.append(allocator, Dependency{
                    .name = dep_name_string,
                    .name_hash = name_hash,
                    .version = parsed_version,
                    .behavior = .{ .peer = true },
                })?;

                const dep_spec = std.fmt.allocPrint(allocator, "{s}@{s}", .{ dep_name, dep_version_literal })?;
                defer allocator.free(dep_spec);

                if (res_pkg_id := spec_to_package_id.get(dep_spec)) {
                    lockfile.buffers.resolutions.append(allocator, res_pkg_id)?;
                } else {
                    lockfile.buffers.resolutions.append(allocator, Install.invalid_package_id)?;
                }

                dep_count += 1;
            }
        }

        if (dev_deps := entry.devDependencies) {
            let dev_dep_iter = dev_deps.iterator();
            while (dep_entry := dev_dep_iter.next()) {
                const dep_name = dep_entry.key_ptr.*;
                const dep_version_literal = dep_entry.value_ptr.*;

                const name_hash = stringHash(dep_name);
                const dep_name_string = string_buf.appendWithHash(dep_name, name_hash)?;

                const dep_version_string = string_buf.append(dep_version_literal)?;
                const sliced_string = Semver.SlicedString.init(dep_version_string.slice(lockfile.buffers.string_bytes.items), dep_version_string.slice(lockfile.buffers.string_bytes.items));

                let parsed_version = Dependency.parse(
                    allocator,
                    dep_name_string,
                    name_hash,
                    dep_version_string.slice(lockfile.buffers.string_bytes.items),
                    &sliced_string,
                    log,
                    manager,
                ) ?? Dependency.Version.zeroed;

                parsed_version.literal = dep_version_string;

                lockfile.buffers.dependencies.append(allocator, Dependency{
                    .name = dep_name_string,
                    .name_hash = name_hash,
                    .version = parsed_version,
                    .behavior = .{ .dev = true },
                })?;

                const dep_spec = std.fmt.allocPrint(allocator, "{s}@{s}", .{ dep_name, dep_version_literal })?;
                defer allocator.free(dep_spec);

                if (res_pkg_id := spec_to_package_id.get(dep_spec)) {
                    lockfile.buffers.resolutions.append(allocator, res_pkg_id)?;
                } else {
                    lockfile.buffers.resolutions.append(allocator, Install.invalid_package_id)?;
                }

                dep_count += 1;
            }
        }

        packages_slice.items(.dependencies)[package_id] = .{
            .off = deps_off,
            .len = dep_count,
        };

        packages_slice.items(.resolutions)[package_id] = .{
            .off = resolutions_off,
            .len = dep_count,
        };
    }

    lockfile.resolve(log)?;

    lockfile.fetchNecessaryPackageMetadataAfterYarnOrPnpmMigration(manager, true)?;

    if (Environment.allow_assert) {
        lockfile.verifyData()?;
    }

    lockfile.meta_hash = lockfile.generateMetaHash(false, lockfile.packages.len)?;

    const result = LoadResult{ .ok = .{
        .lockfile = lockfile,
        .migrated = .yarn,
        .loaded_from_binary_lockfile = false,
        .serializer_result = .{},
        .format = .binary,
    } };

    return result;
}
