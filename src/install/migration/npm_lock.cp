import std from "std";
import bun from "bun";
import Install from "../install.zig";
import DependencyMod from "../dependency.zig";
import Npm from "../npm.zig";
import BinMod from "../bin.zig";
import { Integrity } from "../integrity.zig";
import ResolutionMod from "../resolution.zig";
import Lockfile from "../lockfile.zig";
import HostedGitMod from "../hosted_git_info.zig";
import RepositoryMod from "../repository.zig";

type string = []const u8;
const Allocator = std.mem.Allocator;
const DynamicBitSet = bun.bit_set.DynamicBitSet;
const Output = bun.Output;
const logger = bun.logger;
const strings = bun.strings;
const File = bun.sys.File;
const Semver = bun.Semver;
const String = Semver.String;
const SlicedString = Semver.SlicedString;
const stringHash = String.Builder.stringHash;
const JSAst = bun.ast;
const E = JSAst.E;
const Dependency = DependencyMod.Dependency;
const Behavior = DependencyMod.Behavior;
const Resolution = ResolutionMod.Resolution;
const Bin = BinMod.Bin;
const Repository = RepositoryMod.Repository;
const HostedGitInfo = HostedGitMod.HostedGitInfo;
const PackageID = Install.PackageID;
const invalid_package_id = Install.invalid_package_id;
const PackageManager = Install.PackageManager;
const WorkspaceMap = Lockfile.Package.WorkspaceMap;
const ExternalSlice = Install.ExternalSlice;
const ExternalStringList = Install.ExternalStringList;
const DependencyGroup = Lockfile.Package.DependencyGroup;
type DepSlice = ExternalSlice(Dependency);
type ResSlice = ExternalSlice(PackageID);
type IndexMap = bun.StringHashMapUnmanaged(u32);

const debug = Output.scoped(.migrate, .visible);

const dependency_groups: [4]DependencyGroup = [
    DependencyGroup.dependencies,
    DependencyGroup.dev,
    DependencyGroup.optional,
    DependencyGroup.peer,
];

const Bundle = union(enum) {
    none: void,
    all: void,
    names: bun.StringArrayHashMapUnmanaged(void),

    fn deinit(*_, allocator: Allocator) {
        switch (_) {
            .names *m => m.deinit(allocator),
            else => {},
        }
    }

    fn contains(name: string): bool {
        switch (_) {
            .none => return false,
            .all => return true,
            .names m => return m.contains(name),
        }
    }
};

type VoidStringArrayHashMap = bun.StringArrayHashMapUnmanaged(void);

fn parseBundle(allocator: Allocator, pkg: *const E.Object): Bundle! {
    const expr = pkg.get("bundleDependencies") ?? pkg.get("bundledDependencies") ?? return Bundle.none;
    switch (expr.data) {
        .e_boolean b => return if (b.value) Bundle.all else Bundle.none,
        .e_array arr => {
            let names = VoidStringArrayHashMap{};
            errdefer names.deinit(allocator);
            names.ensureTotalCapacity(allocator, arr.items.len)?;
            for (item of arr.items.slice()) {
                const s = item.asString(allocator) ?? return error.InvalidNPMLockfile;
                const empty_void: void = undefined;
                names.putAssumeCapacity(s, empty_void);
            }
            return Bundle{ .names = names };
        },
        else => return error.InvalidNPMLockfile,
    }
}

fn entryObject(entry: JSAst.G.Property): *const E.Object {
    return entry.value.?.data.e_object;
}

fn pkgFlagIsTrue(pkg: *const E.Object, key: string): bool {
    const val = pkg.get(key) ?? return false;
    return val.data == .e_boolean && val.data.e_boolean.value;
}

fn packageNameFromPath(pkg_path: string): string {
    if (pkg_path.len == 0) return "";
    let pkg_name_start: usize = 0;
    if (last_index := strings.lastIndexOf(pkg_path, "/node_modules/")) {
        pkg_name_start = last_index + "/node_modules/".len;
    } else if (strings.hasPrefixComptime(pkg_path, "node_modules/")) {
        pkg_name_start = "node_modules/".len;
    } else if (last_index := strings.lastIndexOf(pkg_path, "/")) {
        const parent = pkg_path[0..last_index];
        if (i := strings.lastIndexOf(parent, "/")) {
            if (strings.hasPrefixComptime(parent[i + 1 ..], "@")) {
                pkg_name_start = i + "/".len;
            } else {
                pkg_name_start = last_index + "/".len;
            }
        } else if (strings.hasPrefixComptime(parent, "@")) {
            pkg_name_start = 0;
        } else {
            pkg_name_start = last_index + "/".len;
        }
    }
    return pkg_path[pkg_name_start..];
}

fn parentDir(dir: string): string {
    if (i := strings.lastIndexOf(dir, "node_modules/")) {
        const enclosing = dir[0..i];
        return strings.withoutTrailingSlash(enclosing);
    }
    if (i := strings.lastIndexOfChar(dir, '/')) {
        return dir[0..i];
    }
    return "";
}

fn urlIsUnderRegistry(url: string, registry: string): bool {
    const reg = strings.withoutTrailingSlash(registry);
    return strings.startsWith(url, reg) && (url.len == reg.len || url[reg.len] == '/');
}

fn binTargetEscapesPackageDir(target: string): bool {
    if (strings.startsWithChar(target, '/') || strings.startsWithChar(target, '\\')) {
        return true;
    }
    let depth: i32 = 0;
    let it = std.mem.tokenizeAny(u8, target, "/\\");
    let first = true;
    while (comp := it.next()) {
        if (first) {
            first = false;
            if (strings.containsChar(comp, ':')) return true;
        }
        if (strings.eqlComptime(comp, "..")) {
            depth -= 1;
            if (depth < 0) return true;
        } else if (!strings.eqlComptime(comp, ".")) {
            depth += 1;
        }
    }
    return false;
}

struct QueueItem {
    j: u32,
    id: PackageID,
}

struct TargetFound {
    t: u32,
    through_link: bool,
}

struct Migrator {
    lockfile: *Lockfile,
    manager: *PackageManager,
    allocator: Allocator,
    entries: []const JSAst.G.Property,
    workspace_map: (*const WorkspaceMap)?,
    index: bun.StringHashMapUnmanaged(u32),
    link_entries: DynamicBitSet,
    shadowed: DynamicBitSet,
    skipped_external: DynamicBitSet,
    entry_package_ids: []PackageID,
    queue: std.array_list.Managed(QueueItem),
    probe: std.array_list.Managed(u8),
    url: std.array_list.Managed(u8),
    patched: std.array_list.Managed(u8),
    silent: bool,

    fn deinit(*_) {
        _.index.deinit(_.allocator);
        _.link_entries.deinit();
        _.shadowed.deinit();
        _.skipped_external.deinit();
        _.allocator.free(_.entry_package_ids);
        _.queue.deinit();
        _.probe.deinit();
        _.url.deinit();
        _.patched.deinit();
    }

    fn buildIndex(*_)! {
        let j: usize = 0;
        while (j < _.entries.len) : (j += 1) {
            const entry = _.entries[j];
            const val = entry.value ?? return error.InvalidNPMLockfile;
            if (val.data != .e_object) return error.InvalidNPMLockfile;
            const pkg = val.data.e_object;
            const key = entry.key.?.asString(_.allocator) ?? return error.InvalidNPMLockfile;

            if (link := pkg.get("link")) {
                _.link_entries.set(@as(usize, @intCast(j)));
                _.index.put(_.allocator, key, @as(u32, @intCast(j)))?;
                _.registerRenamedWorkspaceLink(key, link)?;
                continue;
            }
            if (pkgFlagIsTrue(pkg, "extraneous")) {
                continue;
            }
            _.index.put(_.allocator, key, @as(u32, @intCast(j)))?;
        }
        if (_.link_entries.isSet(0) || (_.index.get("") ?? 1) != 0) {
            return error.InvalidNPMLockfile;
        }
    }

    fn registerRenamedWorkspaceLink(*_, key: string, link: JSAst.Expr)! {
        const wksp = _.workspace_map ?? return;
        if (!(link.data == .e_boolean && link.data.e_boolean.value)) return;
        const pkg = entryObject(_.entries[_.index.get(key).?]);
        const resolved_expr = pkg.get("resolved") ?? return;
        const resolved = resolved_expr.asString(_.allocator) ?? return;
        const wksp_entry = wksp.map.get(resolved) ?? return;
        const pkg_name = packageNameFromPath(key);
        if (strings.eqlLong(wksp_entry.name, pkg_name, true)) return;
        const pkg_name_hash = stringHash(pkg_name);
        if (_.lockfile.workspace_paths.contains(pkg_name_hash)) return;
        let sb = _.lockfile.stringBuilder();
        const appended = sb.append(String, resolved);
        _.lockfile.workspace_paths.put(_.lockfile.allocator, pkg_name_hash, appended)?;
        if (ver_str := wksp_entry.version) {
            const parsed = Semver.Version.parse(SlicedString.init(ver_str, ver_str));
            if (parsed.valid && parsed.wildcard == .none) {
                _.lockfile.workspace_versions.put(_.lockfile.allocator, pkg_name_hash, parsed.version.min())?;
            }
        }
    }

    fn buildOrGet(*_, j: u32, via_link: bool, hint: Dependency.Version.Tag): PackageID! {
        const existing = _.entry_package_ids[j];
        if (existing != invalid_package_id) return existing;
        return _.buildPackage(j, via_link, hint);
    }

    fn buildPackage(*_, j: u32, via_link: bool, hint: Dependency.Version.Tag): PackageID! {
        const entry = _.entries[j];
        const pkg = entryObject(entry);
        const key = entry.key.?.asString(_.allocator) ?? "";
        const workspace_entry = if (j == 0) null else if (wm := _.workspace_map) wm.map.get(key) else null;

        const name: string = if (ws := workspace_entry)
            ws.name
        else if (set_name := pkg.get("name"))
            set_name.asString(_.allocator) ?? packageNameFromPath(key)
        else
            packageNameFromPath(key);

        const name_hash = stringHash(name);
        let sb_name = _.lockfile.stringBuilder();
        const name_string = sb_name.append(String, name);

        if (!_.silent) {
            if (p := pkg.get("patched")) {
                if (!(p.data == .e_boolean && !p.data.e_boolean.value)) {
                    if (_.patched.items.len > 0) bun.handleOom(_.patched.appendSlice(", "));
                    bun.handleOom(_.patched.append('"'));
                    bun.handleOom(_.patched.appendSlice(name));
                    bun.handleOom(_.patched.append('"'));
                }
            }
        }

        let meta = Lockfile.Package.Meta{
            id: invalid_package_id,
            origin: if (j == 0) .local else .npm,
            arch: brk: {
                if (cpu_arr := pkg.get("cpu")) {
                    if (cpu_arr.data != .e_array) return error.InvalidNPMLockfile;
                    let arch = Npm.Architecture.none.negatable();
                    const items = cpu_arr.data.e_array.items.slice();
                    if (items.len == 0) break :brk arch.combine();
                    for (item of items) {
                        const s = item.asString(_.allocator) ?? return error.InvalidNPMLockfile;
                        arch.apply(s);
                    }
                    break :brk arch.combine();
                }
                break :brk Npm.Architecture.all;
            },
            os: brk: {
                if (os_arr := pkg.get("os")) {
                    if (os_arr.data != .e_array) return error.InvalidNPMLockfile;
                    let os = Npm.OperatingSystem.none.negatable();
                    const items = os_arr.data.e_array.items.slice();
                    if (items.len == 0) break :brk Npm.OperatingSystem.all;
                    for (item of items) {
                        const s = item.asString(_.allocator) ?? return error.InvalidNPMLockfile;
                        os.apply(s);
                    }
                    break :brk os.combine();
                }
                break :brk Npm.OperatingSystem.all;
            },
            man_dir: .{},
            has_install_script: brk: {
                if (h := pkg.get("hasInstallScript")) {
                    if (h.data != .e_boolean) return error.InvalidNPMLockfile;
                    break :brk if (h.data.e_boolean.value) .@"true" else .@"false";
                }
                break :brk .@"false";
            },
            integrity: brk: {
                if (integ := pkg.get("integrity")) {
                    const s = integ.asString(_.allocator) ?? return error.InvalidNPMLockfile;
                    break :brk Integrity.parse(s);
                }
                break :brk Integrity{};
            },
        };

        const res = _.entryResolution(j, pkg, key, name, name_string, name_hash, workspace_entry != null, via_link, hint)?;

        if (res.tag == .npm) {
            const buf = _.lockfile.buffers.string_bytes.items;
            const res_url = res.value.npm.url.slice(buf);
            const configured_registry = _.manager.scopeForPackageName(name).url.href;
            if (!urlIsUnderRegistry(res_url, configured_registry) &&
                !urlIsUnderRegistry(res_url, Npm.Registry.default_url) &&
                !meta.integrity.tag.isSupported())
            {
                return error.InvalidNPMLockfile;
            }
        }

        if (j != 0) {
            if (existing := _.lockfile.getPackageID(name_hash, null, &res)) {
                if (res.tag == .npm && meta.integrity.tag.isSupported()) {
                    let existing_meta = &_.lockfile.packages.items(.@"meta")[existing];
                    if (!existing_meta.integrity.tag.isSupported()) {
                        existing_meta.integrity = meta.integrity;
                        _.lockfile.packages.items(.resolution)[existing] = res;
                    }
                }
                _.entry_package_ids[j] = existing;
                _.shadow(j);
                return existing;
            }
        }

        const id: PackageID = @intCast(_.lockfile.packages.len);
        meta.id = id;
        const bin_value = parseBin(_.allocator, _.lockfile, pkg, name)?;

        _.lockfile.packages.append(_.lockfile.allocator, .{
            .name = name_string,
            .name_hash = name_hash,
            .resolution = res,
            .dependencies = .{},
            .resolutions = .{},
            .@"meta" = meta,
            .bin = bin_value,
            .scripts = .{},
        })?;
        _.lockfile.getOrPutID(id, name_hash)?;
        _.entry_package_ids[j] = id;
        bun.handleOom(_.queue.append(QueueItem{ j: j, id: id }));
        return id;
    }

    fn entryResolution(
        *_,
        j: u32,
        pkg: *const E.Object,
        key: string,
        name: string,
        name_string: String,
        name_hash: u64,
        is_workspace: bool,
        via_link: bool,
        hint: Dependency.Version.Tag,
    ): Resolution! {
        if (j == 0) return Resolution.init(.root);
        if (is_workspace) {
            let sb_path = _.lockfile.stringBuilder();
            const path = sb_path.append(String, key);
            return Resolution.init(.{ .workspace = path });
        }

        if (resolved := pkg.get("resolved")) {
            const r = resolved.asString(_.allocator) ?? return error.InvalidNPMLockfile;
            const tag = Dependency.Version.Tag.infer(r);
            if (tag == .git || tag == .github) {
                return _.gitResolution(r, tag, name_string, name_hash, hint);
            }
            if (local := strings.withoutPrefixIfPossibleComptime(r, "file:")) {
                let sb_local = _.lockfile.stringBuilder();
                const path = sb_local.append(String, local);
                return Resolution.init(.{ .local_tarball = path });
            }
            let version_opt: string? = null;
            if (hint != .tarball) {
                if (v := pkg.get("version")) {
                    version_opt = v.asString(_.allocator) ?? return error.InvalidNPMLockfile;
                }
            }
            if (ver := version_opt) {
                return npmResolution(_.lockfile, r, ver);
            }
            let sb_tar = _.lockfile.stringBuilder();
            const tarball_url = sb_tar.append(String, r);
            return Resolution.init(.{ .remote_tarball = tarball_url });
        }

        if (!via_link && strings.indexOf(key, "node_modules/") != null && name.len > 0) {
            if (v := pkg.get("version")) {
                if (ver := v.asString(_.allocator)) {
                    _.synthesizeRegistryUrl(name, ver)?;
                    return npmResolution(_.lockfile, _.url.items, ver);
                }
            }
        }

        let sb_path2 = _.lockfile.stringBuilder();
        const path = sb_path2.append(String, key);
        return Resolution.init(.{ .folder = path });
    }

    fn synthesizeRegistryUrl(*_, name: string, version: string)! {
        let unscoped: string = name;
        if (name[0] == '@') {
            const slash = strings.indexOfCharUsize(name, '/') ?? return error.InvalidNPMLockfile;
            if (slash >= name.len - 1) return error.InvalidNPMLockfile;
            unscoped = name[slash + 1 ..];
        }
        const href = _.manager.scopeForPackageName(name).url.href;
        _.url.clearRetainingCapacity();
        _.url.appendSlice(href)?;
        _.url.appendSlice(name)?;
        _.url.appendSlice("/-/")?;
        _.url.appendSlice(unscoped)?;
        _.url.append('-')?;
        _.url.appendSlice(version)?;
        _.url.appendSlice(".tgz")?;
    }

    fn gitResolution(
        *_,
        r: string,
        tag: Dependency.Version.Tag,
        name_string: String,
        name_hash: u64,
        hint: Dependency.Version.Tag,
    ): Resolution! {
        let use_github = tag == .github;
        if (!use_github && hint == .github) {
            if (info := HostedGitInfo.fromUrl(_.allocator, r) catch null) {
                use_github = info.host_provider == .github;
            }
        }

        let github_repo: Repository? = null;
        if (use_github) {
            let sb = _.lockfile.stringBuilder();
            const appended = sb.append(String, r);
            const sliced = appended.sliced(_.lockfile.buffers.string_bytes.items);
            if (dep_ver := Dependency.parseWithTag(
                _.allocator,
                name_string,
                name_hash,
                sliced.slice,
                .github,
                &sliced,
                null,
                _.manager,
            )) {
                github_repo = dep_ver.value.github;
            }
        }

        const is_github = github_repo != null;
        let repo = github_repo ?? brk: {
            let sb = _.lockfile.stringBuf();
            break :brk Repository.parseAppendGit(r, &sb)?;
        };

        const buf = _.lockfile.buffers.string_bytes.items;
        const str_slice = Semver.String.slice;
        const committish = str_slice(&repo.committish, buf);
        if (committish.len == 0 || !Repository.isSafeResolvedTag(committish)) {
            return error.InvalidNPMLockfile;
        }
        repo.resolved = repo.committish;
        repo.package_name = name_string;

        return if (is_github)
            Resolution.init(.{ .github = repo })
        else
            Resolution.init(.{ .git = repo });
    }

    fn linkPackage(*_, j: u32, id: PackageID)! {
        const entry = _.entries[j];
        const pkg = entryObject(entry);
        const key = entry.key.?.asString(_.allocator) ?? "";

        const start = _.lockfile.buffers.dependencies.items.len;
        const res_tag = _.lockfile.packages.items(.resolution)[id].tag;
        const is_local = res_tag == .root || res_tag == .workspace;
        let bundle = if (is_local) Bundle.none else parseBundle(_.allocator, pkg)?;
        defer bundle.deinit(_.allocator);

        const replace_optional_dups = res_tag == .root || res_tag == .npm;
        const skip_peer_dups = res_tag == .npm;

        if (j == 0) {
            _.linkWorkspaces()?;
        }

        const peer_meta: (*const E.Object)? = brk: {
            const pm = pkg.get("peerDependenciesMeta") ?? break :brk null;
            break :brk if (pm.data == .e_object) pm.data.e_object else null;
        };

        for (group of dependency_groups) {
            const deps_expr = pkg.get(group.prop) ?? continue;
            if (deps_expr.data != .e_object) return error.InvalidNPMLockfile;
            const deps_obj = deps_expr.data.e_object;
            const is_peer_group = group.behavior.isPeer();
            const is_optional_group = group.behavior.isOptional();

            for (prop of deps_obj.properties.slice()) {
                const name = prop.key.?.asString(_.allocator) ?? return error.InvalidNPMLockfile;
                const spec = prop.value.?.asString(_.allocator) ?? return error.InvalidNPMLockfile;
                const name_hash = stringHash(name);

                let sb = _.lockfile.stringBuilder();
                const dep_name = sb.appendWithHash(String, name, name_hash);
                const dep_version = sb.append(String, spec);
                const sliced = dep_version.sliced(_.lockfile.buffers.string_bytes.items);
                const ver_opt = Dependency.parse(
                    _.allocator,
                    dep_name,
                    name_hash,
                    sliced.slice,
                    &sliced,
                    null,
                    _.manager,
                );
                if (ver_opt == null || ver_opt.?.tag == .catalog) {
                    if (!_.silent) {
                        Output.warn(
                            "skipped \"{s}@{s}\" from package-lock.json: unsupported version specifier",
                            .{ name, spec },
                        );
                    }
                    continue;
                }
                const version = ver_opt.?;

                let behavior = group.behavior;
                if (is_peer_group) {
                    if (pm := peer_meta) {
                        if (m := pm.get(name)) {
                            if (m.data == .e_object && pkgFlagIsTrue(m.data.e_object, "optional")) {
                                behavior.set(.optional, true);
                            }
                        }
                    }
                }
                if (bundle.contains(name)) {
                    behavior.set(.bundled, true);
                }

                let duplicate_of: usize? = null;
                if ((is_peer_group && skip_peer_dups) || (is_optional_group && replace_optional_dups)) {
                    let k: usize = start;
                    while (k < _.lockfile.buffers.dependencies.items.len) : (k += 1) {
                        if (_.lockfile.buffers.dependencies.items[k].name_hash == name_hash) {
                            duplicate_of = k;
                            break;
                        }
                    }
                }
                if (is_peer_group && duplicate_of != null) continue;

                const version_tag = version.tag;
                let found = _.findTarget(key, name);
                if (f := found) {
                    if (!is_local && _.isExternalFolder(f.t, f.through_link)) {
                        _.skipExternal(f.t, name);
                        found = null;
                    }
                }

                let target: PackageID = invalid_package_id;
                if (f := found) {
                    target = _.buildOrGet(f.t, f.through_link, version_tag)?;
                } else if (behavior.isPeer() || behavior.isOptional()) {
                    target = invalid_package_id;
                } else {
                    debug("could not find package '{s}' from '{s}'", .{ name, key });
                    continue;
                }

                const edge = Dependency{
                    name: dep_name,
                    name_hash: name_hash,
                    version: version,
                    behavior: behavior,
                };
                if (k := duplicate_of) {
                    _.lockfile.buffers.dependencies.items[k] = edge;
                    _.lockfile.buffers.resolutions.items[k] = target;
                } else {
                    _.lockfile.buffers.dependencies.append(_.lockfile.allocator, edge)?;
                    _.lockfile.buffers.resolutions.append(_.lockfile.allocator, target)?;
                }
            }
        }

        const len = _.lockfile.buffers.dependencies.items.len - start;
        let slice = DepSlice{};
        let res_slice = ResSlice{};
        if (len > 0) {
            slice = DepSlice{ off: @as(u32, @intCast(start)), len: @as(u32, @intCast(len)) };
            res_slice = ResSlice{ off: @as(u32, @intCast(start)), len: @as(u32, @intCast(len)) };
        }

        _.lockfile.packages.items(.dependencies)[id] = slice;
        _.lockfile.packages.items(.resolutions)[id] = res_slice;
    }

    fn linkWorkspaces(*_)! {
        const wksp = _.workspace_map ?? return;
        const keys = wksp.keys();
        const vals = wksp.values();
        for (i of 0..keys.len) {
            const path = keys[i];
            const ws = vals[i];
            let target_opt = _.index.get(path);
            if (t := target_opt) {
                if (_.link_entries.isSet(t)) target_opt = null;
            }
            if (target_opt == null) {
                if (!_.silent) {
                    Output.warn(
                        "workspace \"{s}\" ({s}) is not in package-lock.json; resolving it from package.json",
                        .{ ws.name, path },
                    );
                }
                continue;
            }
            const t = target_opt.?;
            const wid = _.buildOrGet(t, false, .workspace)?;
            const name_hash = stringHash(ws.name);
            let sb = _.lockfile.stringBuilder();
            const wksp_name = sb.append(String, ws.name);
            const wksp_path = sb.append(String, path);
            _.lockfile.buffers.dependencies.append(_.lockfile.allocator, Dependency{
                name: wksp_name,
                name_hash: name_hash,
                version: .{
                    tag: .workspace,
                    literal: wksp_path,
                    value: .{ workspace: wksp_path },
                },
                behavior: .{ workspace: true },
            })?;
            _.lockfile.buffers.resolutions.append(_.lockfile.allocator, wid)?;
        }
    }

    fn findTarget(*_, from_key: string, name: string): TargetFound? {
        let dir = from_key;
        while (true) {
            _.probe.clearRetainingCapacity();
            if (dir.len > 0) {
                bun.handleOom(_.probe.appendSlice(dir));
                bun.handleOom(_.probe.append('/'));
            }
            bun.handleOom(_.probe.appendSlice("node_modules/"));
            bun.handleOom(_.probe.appendSlice(name));
            if (t := _.index.get(_.probe.items)) {
                if (_.link_entries.isSet(t)) {
                    const lt = _.linkTarget(t) ?? return null;
                    return TargetFound{ t: lt, through_link: true };
                }
                return TargetFound{ t: t, through_link: false };
            }
            if (dir.len == 0) return null;
            dir = parentDir(dir);
        }
    }

    fn linkTarget(t: u32): u32? {
        const entry = _.entries[t];
        const pkg = entryObject(entry);
        const resolved_expr = pkg.get("resolved") ?? return null;
        const resolved = resolved_expr.asString(_.allocator) ?? return null;
        const x = _.index.get(resolved) ?? return null;
        if (_.link_entries.isSet(x)) return null;
        return x;
    }

    fn isExternalFolder(t: u32, through_link: bool): bool {
        const entry = _.entries[t];
        const key = entry.key.?.asString(_.allocator) ?? "";
        if (!binTargetEscapesPackageDir(key)) return false;
        const id = _.entry_package_ids[t];
        if (id != invalid_package_id) {
            return _.lockfile.packages.items(.resolution)[id].tag == .folder;
        }
        const pkg = entryObject(entry);
        if (pkg.get("resolved") != null) return false;
        if (wm := _.workspace_map) {
            if (wm.map.get(key) != null) return false;
        }
        return through_link || pkg.get("version") == null || strings.indexOf(key, "node_modules/") == null;
    }

    fn skipExternal(*_, t: u32, name: string) {
        if (_.skipped_external.isSet(t)) return;
        _.skipped_external.set(t);
        if (!_.silent) {
            const key = _.entries[t].key.?.asString(_.allocator) ?? "";
            Output.warn(
                "skipped \"{s}\" from package-lock.json: transitive folder dependency \"{s}\" is outside the project",
                .{ name, key },
            );
        }
        _.shadow(t);
    }

    fn shadow(*_, j: u32) {
        if (_.shadowed.isSet(j)) return;
        _.shadowed.set(j);
        bun.handleOom(_.queue.append(QueueItem{ j: j, id: invalid_package_id }));
    }

    fn shadowDependencies(*_, j: u32) {
        const entry = _.entries[j];
        const pkg = entryObject(entry);
        const key = entry.key.?.asString(_.allocator) ?? "";
        for (group of dependency_groups) {
            const deps_expr = pkg.get(group.prop) ?? continue;
            if (deps_expr.data != .e_object) continue;
            for (prop of deps_expr.data.e_object.properties.slice()) {
                const dep_name = prop.key.?.asString(_.allocator) ?? continue;
                if (f := _.findTarget(key, dep_name)) {
                    if (_.entry_package_ids[f.t] == invalid_package_id) {
                        _.shadow(f.t);
                    }
                }
            }
        }
    }

    fn warnUnreachable() {
        if (_.silent) return;
        let count: usize = 0;
        let list = std.array_list.Managed(u8).init(_.allocator);
        defer list.deinit();

        let j: usize = 0;
        while (j < _.entries.len) : (j += 1) {
            const entry = _.entries[j];
            if (_.link_entries.isSet(j) ||
                _.entry_package_ids[j] != invalid_package_id ||
                _.shadowed.isSet(j) ||
                pkgFlagIsTrue(entryObject(entry), "extraneous"))
            {
                continue;
            }
            count += 1;
            if (count <= 5) {
                if (count > 1) bun.handleOom(list.appendSlice(", "));
                bun.handleOom(list.append('"'));
                bun.handleOom(list.appendSlice(entry.key.?.asString(_.allocator) ?? ""));
                bun.handleOom(list.append('"'));
            }
        }
        if (count == 0) return;
        if (count > 5) {
            const extra = bun.handleOom(std.fmt.allocPrint(_.allocator, " and {d} more", .{count - 5}));
            defer _.allocator.free(extra);
            bun.handleOom(list.appendSlice(extra));
        }
        const noun: string = if (count == 1) "entry" else "entries";
        Output.warn("skipped {d} package-lock.json {s} not depended on by any package: {s}", .{ count, noun, list.items });
    }
}

fn npmResolution(lockfile: *Lockfile, url: string, version: string): Resolution {
    let sb = lockfile.stringBuilder();
    const version_string = sb.append(String, version);
    const url_string = sb.append(String, url);
    const ver = Semver.Version.parse(version_string.sliced(lockfile.buffers.string_bytes.items)).version.min();
    return Resolution.init(.{
        .npm = .{
            url: url_string,
            version: ver,
        },
    });
}

fn parseBin(allocator: Allocator, lockfile: *Lockfile, pkg: *const E.Object, name: string): Bin! {
    const bin_expr = pkg.get("bin") ?? return Bin.init();
    if (bin_expr.data != .e_object) return error.InvalidNPMLockfile;
    const bin_props = bin_expr.data.e_object.properties.slice();
    let sb = lockfile.stringBuilder();

    if (bin_props.len == 0) return Bin.init();
    if (bin_props.len == 1) {
        const prop = bin_props[0];
        const key = prop.key.?.asString(allocator) ?? return error.InvalidNPMLockfile;
        const script_val = prop.value.?.asString(allocator) ?? return error.InvalidNPMLockfile;
        if (strings.eql(key, name)) {
            return Bin{
                tag: .file,
                value: .{ file: sb.append(String, script_val) },
            };
        }
        return Bin{
            tag: .named_file,
            value: .{ named_file: [ sb.append(String, key), sb.append(String, script_val) ] },
        };
    }

    const off: u32 = @intCast(lockfile.buffers.extern_strings.items.len);
    const len: u32 = @intCast(bin_props.len * 2);
    lockfile.buffers.extern_strings.ensureUnusedCapacity(lockfile.allocator, len)?;
    for (bin_entry of bin_props) {
        const key = bin_entry.key.?.asString(allocator) ?? return error.InvalidNPMLockfile;
        const script_val = bin_entry.value.?.asString(allocator) ?? return error.InvalidNPMLockfile;
        const ek = sb.append(Semver.ExternalString, key);
        const ev = sb.append(Semver.ExternalString, script_val);
        lockfile.buffers.extern_strings.appendAssumeCapacity(ek);
        lockfile.buffers.extern_strings.appendAssumeCapacity(ev);
    }
    return Bin{
        tag: .map,
        value: .{ map: ExternalStringList{ off: off, len: len } },
    };
}

pub fn migratePackages(
    lockfile: *Lockfile,
    manager: *PackageManager,
    allocator: Allocator,
    packages_properties: []const JSAst.G.Property,
    workspace_map: (*const WorkspaceMap)?,
)! {
    const entry_count = packages_properties.len;
    lockfile.packages.ensureTotalCapacity(lockfile.allocator, entry_count)?;
    lockfile.package_index.ensureTotalCapacity(@as(u32, @intCast(entry_count)))?;

    let index = IndexMap{};
    index.ensureTotalCapacity(allocator, @as(u32, @intCast(entry_count)))?;

    const entry_package_ids = bun.handleOom(allocator.alloc(PackageID, entry_count));
    @memset(entry_package_ids, invalid_package_id);

    let migrator = Migrator{
        lockfile: lockfile,
        manager: manager,
        allocator: allocator,
        entries: packages_properties,
        workspace_map: workspace_map,
        index: index,
        link_entries: bun.handleOom(DynamicBitSet.initEmpty(allocator, entry_count)),
        shadowed: bun.handleOom(DynamicBitSet.initEmpty(allocator, entry_count)),
        skipped_external: bun.handleOom(DynamicBitSet.initEmpty(allocator, entry_count)),
        entry_package_ids: entry_package_ids,
        queue: std.array_list.Managed(QueueItem).init(allocator),
        probe: std.array_list.Managed(u8).init(allocator),
        url: std.array_list.Managed(u8).init(allocator),
        patched: std.array_list.Managed(u8).init(allocator),
        silent: manager.options.log_level == .silent,
    };
    defer migrator.deinit();

    migrator.buildIndex()?;
    * = migrator.buildPackage(0, false, .npm)?;

    let cursor: usize = 0;
    while (cursor < migrator.queue.items.len) {
        const item = migrator.queue.items[cursor];
        cursor += 1;
        if (item.id == invalid_package_id) {
            migrator.shadowDependencies(item.j);
        } else {
            migrator.linkPackage(item.j, item.id)?;
        }
    }

    migrator.warnUnreachable();

    if (migrator.patched.items.len > 0) {
        Output.warn("skipped npm patches for {s} from package-lock.json", .{migrator.patched.items});
        Output.note("bun patch \\<pkg\\>", .{});
    }
}

pub fn applyRootOverrides(
    lockfile: *Lockfile,
    manager: *PackageManager,
    allocator: Allocator,
    log: *logger.Log,
    dir: bun.FD,
    abs_lockfile_path: string,
)! {
    const file = File.openat(dir, "package.json", bun.O.RDONLY, 0).unwrap() catch return;
    defer file.close();
    const contents = file.readToEnd(allocator).unwrap() catch return;
    defer allocator.free(contents);

    let path_buf: bun.PathBuffer = undefined;
    const parts: [2]string = [ bun.path.dirname(abs_lockfile_path, .auto), "package.json" ];
    const pkg_json_path = bun.path.joinAbsStringBuf(bun.fs.FileSystem.instance.top_level_dir, &path_buf, &parts, .auto);
    const source = logger.Source.initPathString(pkg_json_path, contents);
    const json = bun.json.parsePackageJSONUTF8(&source, log, allocator) catch return error.InvalidPackageJSON;
    if (json.asProperty("overrides") == null && json.asProperty("resolutions") == null) return;

    let root_package = lockfile.packages.get(0);
    let builder = lockfile.stringBuilder();
    Lockfile.OverrideMap.parseCount(lockfile, json, &builder);
    builder.allocate()?;
    lockfile.overrides.parseAppend(
        manager,
        lockfile,
        &root_package,
        log,
        &source,
        json,
        &builder,
    )?;
    builder.clamp();
}
