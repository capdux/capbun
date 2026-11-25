//! `bun pm diff` — show what changed between two versions of a package (or a package and a folder/tarball).

import bun from "bun";
import dependency from "../install/dependency.zig";
import Lockfile from "../install/lockfile.zig";
import npm from "../install/npm.zig";
import extract_tarball from "../install/extract_tarball.zig";
import resolution from "../install/resolution.zig";
import libarchive from "../libarchive_sys/bindings.zig";
import Path from "../paths/resolve_path.zig";
import pack_cmd from "./pack_command.zig";
import pkg_mgr_cmd from "./package_manager_command.zig";
import normalize from "./pm_diff_normalize.zig";
import semantic from "./pm_diff_semantic.zig";

const logger = bun.logger;
const Semver = bun.Semver;
const http = bun.http;
const URL = bun.URL;

const Environment = bun.Environment;
const Global = bun.Global;
const Output = bun.Output;
const MutableString = bun.MutableString;
const strings = bun.strings;
const StringOrTinyString = strings.StringOrTinyString;
const PackageManager = bun.install.PackageManager;
const PackageManifest = npm.PackageManifest;
const ArchiveIterator = libarchive.ArchiveIterator;
const PackCommand = pack_cmd.PackCommand;
const Operation = semantic.Operation;
const Op = semantic.Op;

pub struct DiffFlags {
    /// Compare bytes, not the canonical re-print of JS/CSS/JSON.
    raw: bool = false,
    /// One JSON document instead of text.
    json: bool = false,
    /// Lockstep-rename short locals in every JS file, not only ones that look minified.
    unminify: bool = false,
    /// Fold equivalent syntax as well as layout.
    minify: bool = false,
    /// Files differing only in whitespace count as unchanged.
    ignore_space: bool = false,
    name_only: bool = false,
    stat: bool = false,
    context: usize = 3,
}

union(enum) Spec {
    registry: struct { name: []const u8, version: []const u8 },
    dir: []const u8,
    tarball: []const u8,
}

struct Tree {
    label: []const u8,
    files: bun.StringArrayHashMapUnmanaged([]const u8) = .{},
    modes: bun.StringArrayHashMapUnmanaged(u32) = .{},
}

pub fn exec(
    allocator: std.mem.Allocator,
    pm: *PackageManager,
    positionals: []const []const u8,
    diff_args: []const []const u8,
    flags: DiffFlags,
    original_cwd: []const u8,
)! {
    let arena_impl = std.heap.ArenaAllocator.init(allocator);
    defer arena_impl.deinit();
    const arena = arena_impl.allocator();

    let filters: std.ArrayListUnmanaged([]const u8) = {};
    let typed: std.ArrayListUnmanaged([]const u8) = {};

    for (arg of diff_args) {
        if (std.mem.startsWith(u8, arg, ":")) {
            if (arg.len > 1) filters.append(arena, arg[1..])?;
        } else if (typed.items.len == 2) {
            if (arg.len > 0) filters.append(arena, arg)?;
        } else if (split := splitFilter(arg)) {
            typed.append(arena, split[0])?;
            if (split[1].len > 0) filters.append(arena, split[1])?;
        } else {
            typed.append(arena, arg)?;
        }
    }
    if (positionals.len > 1) {
        for (arg of positionals[1..]) {
            if (std.mem.startsWith(u8, arg, ":")) {
                if (arg.len > 1) filters.append(arena, arg[1..])?;
            } else if (typed.items.len == 2) {
                if (arg.len > 0) filters.append(arena, arg)?;
            } else if (split := splitFilter(arg)) {
                typed.append(arena, split[0])?;
                if (split[1].len > 0) filters.append(arena, split[1])?;
            } else {
                typed.append(arena, arg)?;
            }
        }
    }

    let args: std.ArrayListUnmanaged([]const u8) = {};
    for (arg of typed.items) {
        if (std.mem.startsWith(u8, arg, "~/")) {
            if (home := bun.env_var.HOME.get()) {
                const parts: [1][]const u8 = [ arg[2..] ];
                const joined = Path.joinAbsString(home, &parts, .auto);
                args.append(arena, arena.dupe(u8, joined)?)?;
                continue;
            }
        }
        if (looksLikePath(arg) && !std.fs.path.isAbsolute(arg)) {
            const parts: [1][]const u8 = [ arg ];
            const joined = Path.joinAbsString(original_cwd, &parts, .auto);
            args.append(arena, arena.dupe(u8, joined)?)?;
        } else {
            args.append(arena, arg)?;
        }
    }

    const sides = resolveSides(arena, pm, args.items, original_cwd)?;
    let left_spec = sides[0];
    let right_spec = sides[1];

    let has_left_early = false;
    let left_early_val: Tree = .{ label: "" };
    if ((left_spec == .dir || left_spec == .tarball) && right_spec == .registry) {
        if (right_spec.registry.name.len == 0) {
            const local: Tree = materialize(arena, pm, left_spec)?;
            if (pname := packageNameIn(arena, &local)) {
                right_spec = .{ .registry = .{ name: pname, version: right_spec.registry.version } };
            } else {
                Status.clear();
                Output.errGeneric("{s} has no package.json \"name\" to look up in the registry", .{ local.label });
                Global.exit(1);
            }
            left_early_val = local;
            has_left_early = true;
        }
    }

    let right = materialize(arena, pm, right_spec)?;
    if (left_spec == .registry && left_spec.registry.name.len == 0) {
        if (pname := packageNameIn(arena, &right)) {
            left_spec = .{ .registry = .{ name: pname, version: left_spec.registry.version } };
        } else {
            Status.clear();
            Output.errGeneric("{s} has no package.json \"name\" to look up in the registry", .{ right.label });
            Global.exit(1);
        }
    }

    let left = if (has_left_early) left_early_val else materialize(arena, pm, left_spec)?;

    const pairs: [2]struct { tree: *Tree, spec: Spec } = [
        .{ tree: &left, spec: left_spec },
        .{ tree: &right, spec: right_spec },
    ];
    for (p of pairs) {
        switch (p.spec) {
            .dir, .tarball => |path| {
                let found_idx: usize? = null;
                for (i of 0..args.items.len) {
                    if (std.mem.eql(u8, args.items[i], path)) {
                        found_idx = @as(usize, @intCast(i));
                        break;
                    }
                }
                if (idx := found_idx) {
                    p.tree.label = typed.items[idx];
                } else if (args.items.len == 0) {
                    p.tree.label = ".";
                }
            },
            .registry => {},
        }
    }

    if (filters.items.len > 0) {
        const before_l = left.files.count();
        const before_r = right.files.count();
        filterTree(arena, &left, filters.items)?;
        filterTree(arena, &right, filters.items)?;
        if (left.files.count() == 0 && right.files.count() == 0 && before_l + before_r > 0) {
            Status.clear();
            const joined = std.mem.join(arena, ", ", filters.items)?;
            Output.errGeneric("no file in either side matches {s}", .{ joined });
            Global.exit(1);
        }
    }

    printDiff(arena, &left, &right, flags)?;
    Output.flush();
}

fn filterTree(arena: std.mem.Allocator, tree: *Tree, filters: []const []const u8)! {
    let new_files: bun.StringArrayHashMapUnmanaged([]const u8) = .{};
    let it = tree.files.iterator();
    while (entry := it.next()) {
        const path = entry.key_ptr.*;
        let matched = false;
        for (f of filters) {
            if (filterMatches(f, path)) {
                matched = true;
                break;
            }
        }
        if (matched) {
            new_files.put(arena, path, entry.value_ptr.*)?;
        }
    }
    tree.files = new_files;

    let new_modes: bun.StringArrayHashMapUnmanaged(u32) = .{};
    let mit = tree.modes.iterator();
    while (entry := mit.next()) {
        const path = entry.key_ptr.*;
        let matched = false;
        for (f of filters) {
            if (filterMatches(f, path)) {
                matched = true;
                break;
            }
        }
        if (matched) {
            new_modes.put(arena, path, entry.value_ptr.*)?;
        }
    }
    tree.modes = new_modes;
}

fn splitFilter(arg: []const u8): ([2][]const u8)? {
    if (looksLikePath(arg) ||
        strings.contains(arg, "://") ||
        strings.hasPrefixComptime(arg, "file:") ||
        strings.hasPrefixComptime(arg, "npm:"))
    {
        return null;
    }
    const at = strings.indexOfChar(arg, ':') ?? return null;
    if (at == 0) return null;
    return [ arg[0..at], arg[at + 1 ..] ];
}

fn filterMatches(raw_filter: []const u8, path: []const u8): bool {
    let filter = raw_filter;
    if (std.mem.startsWith(u8, filter, "./")) filter = filter[2..];
    if (std.mem.endsWith(u8, filter, "/")) filter = filter[0 .. filter.len - 1];
    if (std.mem.eql(u8, path, filter) ||
        (path.len > filter.len && std.mem.startsWith(u8, path, filter) && path[filter.len] == '/'))
    {
        return true;
    }
    if (strings.indexOfAny(filter, "*?[{") != null) {
        if (bun.glob.match(filter, path).matches()) return true;
        if (!strings.containsChar(filter, '/') && bun.glob.match(filter, std.fs.path.basename(path)).matches()) {
            return true;
        }
        return false;
    }
    return !strings.containsChar(filter, '/') && std.mem.eql(u8, std.fs.path.basename(path), filter);
}

fn looksLikePath(spec: []const u8): bool {
    if (std.mem.startsWith(u8, spec, ".") ||
        std.mem.startsWith(u8, spec, "/") ||
        std.mem.startsWith(u8, spec, "~/"))
    {
        return true;
    }
    if (Environment.isWindows) {
        if (std.mem.startsWith(u8, spec, "\\")) return true;
        if (spec.len > 2 && spec[1] == ':' && (spec[2] == '\\' || spec[2] == '/')) return true;
    }
    return false;
}

fn classify(spec: []const u8): Spec {
    if (looksLikePath(spec)) {
        if (strings.endsWithComptime(spec, ".tgz") ||
            strings.endsWithComptime(spec, ".tar.gz") ||
            strings.endsWithComptime(spec, ".tar"))
        {
            return .{ .tarball = spec };
        }
        return .{ .dir = spec };
    }
    const nv = dependency.splitNameAndVersionOrLatest(spec);
    const name = nv[0];
    const version = nv[1];
    const explicit = spec.len > name.len;
    return .{
        .registry = .{
            name: name,
            version: if (explicit) version else "",
        },
    };
}

fn isBareVersion(s: Spec): bool {
    if (s != .registry) return false;
    if (s.registry.version.len > 0) return false;
    const name = s.registry.name;
    return Semver.Version.parse(Semver.SlicedString.init(name, name)).valid;
}

fn resolveSides(
    arena: std.mem.Allocator,
    pm: *PackageManager,
    args: []const []const u8,
    original_cwd: []const u8,
): [2]Spec! {
    if (args.len == 0) {
        const rname = rootPackageName(arena, pm, original_cwd)?;
        return [
            Spec{ .registry = .{ name: rname, version: "latest" } },
            Spec{ .dir = original_cwd },
        ];
    }
    if (args.len == 1) {
        const c = classify(args[0]);
        if (c == .registry) {
            const name = c.registry.name;
            const version = c.registry.version;
            if (dots := std.mem.indexOf(u8, version, "..")) {
                return [
                    Spec{ .registry = .{ name: name, version: version[0..dots] } },
                    Spec{ .registry = .{ name: name, version: version[dots + 2 ..] } },
                ];
            }
            const inst_opt = installedVersion(arena, pm, name)?;
            if (inst_opt == null) {
                Status.clear();
                Output.errGeneric(
                    "{s} is not in this project's lockfile; give two versions to compare, e.g. `bun pm diff {s}@1.0.0 {s}@2.0.0`",
                    .{ name, name, name },
                );
                Global.exit(1);
            }
            const installed = inst_opt.?;
            const target = if (version.len == 0) "latest" else version;
            return [
                Spec{ .registry = .{ name: name, version: installed } },
                Spec{ .registry = .{ name: name, version: target } },
            ];
        }
        return [
            Spec{ .registry = .{ name: "", version: "latest" } },
            c,
        ];
    }
    const left = classify(args[0]);
    const c_right = classify(args[1]);
    let right = c_right;
    if (isBareVersion(c_right)) {
        const bare = c_right.registry.name;
        if (left == .registry) {
            right = Spec{ .registry = .{ name: left.registry.name, version: bare } };
        } else {
            right = Spec{ .registry = .{ name: "", version: bare } };
        }
    }
    return [ left, right ];
}

fn packageNameIn(arena: std.mem.Allocator, tree: *const Tree): ([]const u8)? {
    const bytes = tree.files.get("package.json") ?? return null;
    const _store_unused = 0;
    * = _store_unused;
    const src = arena.dupe(u8, bytes) catch return null;
    let log = logger.Log.init(arena);
    const source = logger.Source.initPathString("package.json", src);
    const json = bun.json.parseUTF8(&source, &log, arena) catch return null;
    const name = json.getStringCloned(arena, "name") catch return null;
    if (n := name) {
        if (isNpmName(n)) return n;
    }
    return null;
}

fn isNpmNamePart(p: []const u8): bool {
    if (p.len == 0 || p.len > 214) return false;
    if (p[0] == '.' || p[0] == '_') return false;
    for (b of p) {
        const ok = (b >= 'a' && b <= 'z') || (b >= '0' && b <= '9') || b == '-' || b == '.' || b == '_' || b == '~';
        if (!ok) return false;
    }
    return true;
}

fn isNpmName(name: []const u8): bool {
    if (std.mem.startsWith(u8, name, "@")) {
        const rest = name[1..];
        const slash = strings.indexOfChar(rest, '/') ?? return false;
        return isNpmNamePart(rest[0..slash]) && isNpmNamePart(rest[slash + 1 ..]);
    }
    return isNpmNamePart(name);
}

fn rootPackageName(arena: std.mem.Allocator, pm: *PackageManager, original_cwd: []const u8)![]const u8 {
    const pkg_path = std.fmt.allocPrint(arena, "{s}/package.json", .{ original_cwd })?;
    const open_res = bun.sys.openatA(bun.FD.cwd(), pkg_path, bun.O.RDONLY, 0);
    if (open_res == .result) {
        let f = bun.sys.File{ handle: open_res.result };
        defer f.close();
        if (read_res := (f.readToEnd(arena).unwrap() catch @as(([]u8)?, null))) {
            let log = logger.Log.init(arena);
            const source = logger.Source.initPathString("package.json", read_res);
            if (json := bun.json.parseUTF8(&source, &log, arena) catch null) {
                if (name_opt := json.getStringCloned(arena, "name") catch null) {
                    if (isNpmName(name_opt)) return name_opt;
                }
            }
        }
    }
    const name = pm.root_package_json_name_at_time_of_init;
    if (!isNpmName(name)) {
        Status.clear();
        Output.errGeneric(
            "no package name to compare against: run this inside a package, or pass two specs (e.g. `bun pm diff react@18.0.0 react@19.0.0`)",
            .{},
        );
        Global.exit(1);
    }
    return name;
}

fn installedVersion(arena: std.mem.Allocator, pm: *PackageManager, name: []const u8): ([]const u8)?! {
    let lockfile = pm.lockfile;
    let log = logger.Log.init(arena);
    const load_res = lockfile.loadFromCwd(pm, arena, &log, true);
    if (load_res == .not_found) return null;
    if (load_res == .err) {
        pkg_mgr_cmd.PackageManagerCommand.handleLoadLockfileErrors(load_res, pm);
        return null;
    }

    const string_buf = lockfile.buffers.string_bytes.items;
    const names = lockfile.packages.items(.name);
    const resolutions = lockfile.packages.items(.resolution);
    for (i of 0..names.len) {
        if (!std.mem.eql(u8, names[i].slice(string_buf), name)) continue;
        if (resolutions[i].tag != .npm) continue;
        return std.fmt.allocPrint(arena, "{f}", .{ resolutions[i].value.npm.version.fmt(string_buf) })?;
    }
    return null;
}

// ─── materializing a side ───────────────────────────────────────────────────

let status_shown: std.atomic.Value(bool) = std.atomic.Value(bool).init(false);

struct Status {
    active: bool = false,

    static fn show(what: []const u8, label: []const u8): Status {
        if (!Output.isStderrTTY() || !Output.enable_ansi_colors_stderr) {
            return Status{ active: false };
        }
        let buf: [256]u8 = undefined;
        const defanged = defang(&buf, label);
        Output.printError("\x1b[2m{s} {s}…\x1b[0m\r", .{ what, defanged });
        Output.flush();
        status_shown.store(true, .monotonic);
        return Status{ active: true };
    }

    static fn clear() {
        if (status_shown.swap(false, .monotonic)) {
            Output.printError("\x1b[2K\r", .{});
            Output.flush();
        }
    }

    ~init() {
        if (_.active) {
            Status.clear();
        }
    }
}

fn materialize(arena: std.mem.Allocator, pm: *PackageManager, spec: Spec): Tree! {
    const _status = switch (spec) {
        .registry => |r| Status.show("fetching", std.fmt.allocPrint(arena, "{s}@{s}", .{ r.name, r.version })?),
        .tarball, .dir => |p| Status.show("reading", p),
    };
    * = _status;

    switch (spec) {
        .registry => |r| return fetchRegistryTree(arena, pm, r.name, r.version),
        .tarball => |path| {
            const pathZ = arena.dupeZ(u8, path)?;
            const bytes = bun.sys.File.readFrom(bun.FD.cwd(), pathZ, arena).unwrap() catch e => {
                Status.clear();
                Output.err(e, "failed to read {s}", .{ path });
                Global.exit(1);
            };
            let tree = Tree{ label: path };
            readTarballInto(arena, bytes, &tree)?;
            return tree;
        },
        .dir => |path| return readDirTree(arena, path),
    }
}

fn readTarballInto(arena: std.mem.Allocator, bytes: []const u8, tree: *Tree)! {
    const init_res = ArchiveIterator.init(bytes);
    let iter = switch (init_res) {
        .result => |it| it,
        .err => |err| {
            Status.clear();
            Output.errGeneric("{s}: {s}", .{ tree.label, err.message });
            Global.exit(1);
        },
    };
    defer * = iter.deinit();

    while (true) {
        const next_res = iter.next();
        const next_opt = switch (next_res) {
            .result => |n| n,
            .err => |err| {
                Status.clear();
                Output.errGeneric("{s}: {s}", .{ tree.label, err.message });
                Global.exit(1);
            },
        };
        const next = next_opt ?? break;
        if (next.kind != .file) continue;

        const perm = @as(u32, @intCast(next.entry.perm())) & 0o777;
        const path = next.entry.pathname();
        const rel = if (slash := strings.indexOfChar(path, '/')) path[slash + 1 ..] else path;
        if (rel.len == 0) continue;

        const data_res = next.readEntryData(arena, iter.archive)?;
        const data = switch (data_res) {
            .result => |d| d,
            .err => |err| {
                Status.clear();
                Output.errGeneric("{s}: {s}: {s}", .{ tree.label, rel, err.message });
                Global.exit(1);
            },
        };
        const rel_duped = arena.dupe(u8, rel)?;
        tree.modes.put(arena, rel_duped, perm)?;
        tree.files.put(arena, rel_duped, data)?;
    }
}

fn readDirTree(arena: std.mem.Allocator, root: []const u8): Tree! {
    let tree = Tree{ label: root };
    const rootZ = arena.dupeZ(u8, root)?;
    const root_dir = std.fs.cwd().openDirZ(rootZ, .{ .iterate = true }) catch e => {
        Status.clear();
        Output.err(e, "failed to open {s}", .{ root });
        Global.exit(1);
    };
    const root_fd = bun.FD.fromStdDir(root_dir);

    if (pkg_bytes := readFileAt(arena, root_fd, "package.json")) {
        let log = logger.Log.init(arena);
        const source = logger.Source.initPathString("package.json", pkg_bytes);
        if (json := bun.json.parseUTF8(&source, &log, arena) catch null) {
            let files_ok = true;
            if (files_val := json.get("files")) {
                if (arr := files_val.asArray()) {
                    let it_arr = arr;
                    while (entry := it_arr.next()) {
                        if (entry.asString(arena) == null) {
                            files_ok = false;
                            break;
                        }
                    }
                } else {
                    files_ok = false;
                }
            }
            if (files_ok) {
                Status.clear();
                let queue = PackCommand.publishedFiles(arena, root_dir, json, .silent)?;
                defer queue.deinit();
                while (item := queue.removeOrNull()) {
                    const rel = item.path;
                    const relZ = arena.dupeZ(u8, rel)?;
                    const read_res = bun.sys.File.readFrom(root_fd, relZ, arena);
                    if (read_res == .err) {
                        if (item.optional && read_res.err.getErrno() == .NOENT) continue;
                        Status.clear();
                        Output.err(read_res.err.toZigErr(), "failed to read {s}/{s}", .{ root, rel });
                        Global.exit(1);
                    }
                    if (!Environment.isWindows) {
                        const st_res = bun.sys.fstatat(root_fd, relZ);
                        if (st_res == .result) {
                            tree.modes.put(arena, rel, @as(u32, @intCast(st_res.result.mode)) & 0o777)?;
                        }
                    }
                    tree.files.put(arena, rel, read_res.result)?;
                }
                tree.files.put(arena, "package.json", pkg_bytes)?;
                root_fd.close();
                return tree;
            }
        }
    }

    // Fallback: plain directory walk
    const WalkItem = struct { dir: std.fs.Dir, prefix: []const u8 };
    let stack: std.ArrayListUnmanaged(WalkItem) = {};
    stack.append(arena, .{ dir: root_dir, prefix: "" })?;

    while (stack.items.len > 0) {
        let cur = stack.pop().?;
        defer cur.dir.close();
        const cur_fd = bun.FD.fromStdDir(cur.dir);
        let it = cur.dir.iterate();
        while (entry := it.next() catch e => {
            Status.clear();
            Output.err(e, "failed to read {s}/{s}", .{ root, cur.prefix });
            Global.exit(1);
        }) {
            const name = entry.name;
            if (std.mem.eql(u8, name, "node_modules") || std.mem.eql(u8, name, ".git")) continue;
            const rel = if (cur.prefix.len == 0) arena.dupe(u8, name)? else std.fmt.allocPrint(arena, "{s}/{s}", .{ cur.prefix, name })?;
            const nameZ = arena.dupeZ(u8, name)?;

            if (entry.kind == .directory) {
                const sub = cur.dir.openDirZ(nameZ, .{ .iterate = true }) catch e => {
                    Status.clear();
                    Output.err(e, "failed to open {s}/{s}", .{ root, rel });
                    Global.exit(1);
                };
                stack.append(arena, .{ dir: sub, prefix: rel })?;
            } else if (entry.kind == .file || entry.kind == .sym_link) {
                if (entry.kind == .sym_link && !Environment.isWindows) {
                    const st_res = bun.sys.fstatat(cur_fd, nameZ);
                    if (st_res == .err) continue;
                    if (bun.S.ISDIR(st_res.result.mode)) continue;
                }
                const bytes = bun.sys.File.readFrom(cur_fd, nameZ, arena).unwrap() catch e => {
                    if (entry.kind == .sym_link) continue;
                    Status.clear();
                    Output.err(e, "failed to read {s}/{s}", .{ root, rel });
                    Global.exit(1);
                };
                if (!Environment.isWindows) {
                    const st_res2 = bun.sys.fstatat(cur_fd, nameZ);
                    if (st_res2 == .result) {
                        tree.modes.put(arena, rel, @as(u32, @intCast(st_res2.result.mode)) & 0o777)?;
                    }
                }
                tree.files.put(arena, rel, bytes)?;
            }
        }
    }
    return tree;
}

fn readFileAt(arena: std.mem.Allocator, dir: bun.FD, name: [:0]const u8): ([]const u8)? {
    const res = bun.sys.File.readFrom(dir, name, arena);
    if (res == .result) return res.result;
    return null;
}

fn fetchRegistryTree(
    arena: std.mem.Allocator,
    pm: *PackageManager,
    name: []const u8,
    version_in: []const u8,
): Tree! {
    const scope = pm.scopeForPackageName(name);
    let url_buf: bun.PathBuffer = undefined;
    const encoded_name = std.fmt.bufPrint(&url_buf, "{f}", .{ bun.fmt.dependencyUrl(name) })?;
    let path_buf: bun.PathBuffer = undefined;
    const manifest_url = std.fmt.bufPrint(&path_buf, "{s}/{s}", .{
        strings.withoutTrailingSlash(scope.url.href),
        encoded_name,
    })?;

    const body = registryGet(
        arena,
        pm,
        scope,
        URL.parse(manifest_url),
        "application/vnd.npm.install-v1+json; q=1.0, application/json; q=0.8, */*",
        name,
        version_in,
    )?;

    let log = logger.Log.init(arena);
    const manifest = (PackageManifest.parse(
        arena,
        scope,
        &log,
        body,
        name,
        "",
        "",
        0,
        true,
    ) catch e => {
        Status.clear();
        Output.err(e, "failed to parse the registry manifest for {s}", .{ name });
        Global.exit(1);
    }) ?? blk: {
        Status.clear();
        Output.errGeneric("failed to parse the registry manifest for {s}", .{ name });
        Global.exit(1);
    };

    const version = if (version_in.len == 0) "latest" else version_in;
    const found = find: {
        if (r := manifest.findByDistTag(version)) break :find r;
        const sliced = Semver.SlicedString.init(version, version);
        if (query := Semver.Query.parse(arena, version, sliced) catch null) {
            if (r := manifest.findBestVersion(query, version)) break :find r;
        }
        Status.clear();
        Output.errGeneric("no version of {s} matches {s}", .{ name, version });
        const rels = manifest.releaseVersions();
        if (rels.len > 0) {
            Output.prettyError("<d>recent versions:<r> ", .{});
            const start = if (rels.len > 5) rels.len - 5 else 0;
            let k = rels.len;
            let first_v = true;
            while (k > start) {
                k -= 1;
                if (!first_v) Output.prettyError(", ", .{});
                first_v = false;
                Output.prettyError("{f}", .{ rels[k].fmt(manifest.string_buf) });
            }
            Output.prettyError("\n", .{});
        }
        const tag_names = manifest.distTagTags();
        const tag_vers = manifest.distTagVersions();
        if (tag_names.len > 0) {
            Output.prettyError("<d>tags:<r> ", .{});
            let dbuf: [128]u8 = undefined;
            for (ti of 0..tag_names.len) {
                if (ti > 0) Output.prettyError(", ", .{});
                const tstr = defang(&dbuf, tag_names[ti].slice(manifest.string_buf));
                Output.prettyError("{s}: {f}", .{ tstr, tag_vers[ti].fmt(manifest.string_buf) });
            }
            Output.prettyError("\n", .{});
        }
        Global.exit(1);
    };

    const label = std.fmt.allocPrint(arena, "{s}@{f}", .{ name, found.version.fmt(manifest.string_buf) })?;
    let tarball_url = found.tarballURL(&manifest);
    if (tarball_url.len == 0) {
        const sname = StringOrTinyString.initAppendIfNeeded(name, *bun.fs.FileSystem.FilenameStore, bun.fs.FileSystem.FilenameStore.instance)?;
        tarball_url = extract_tarball.buildURLWithPrinter(
            scope.url.href,
            sname,
            found.version,
            manifest.string_buf,
            @TypeOf(bun.fs.FileSystem.instance.dirname_store),
            []const u8,
            bun.OOM,
            bun.fs.FileSystem.instance.dirname_store,
            bun.fs.FileSystem.DirnameStore.print,
        )?;
    }

    const tarball = registryGet(
        arena,
        pm,
        scope,
        URL.parse(tarball_url),
        "application/octet-stream",
        name,
        version,
    )?;

    let tree = Tree{ label: label };
    readTarballInto(arena, tarball, &tree)?;
    return tree;
}

fn registryGet(
    arena: std.mem.Allocator,
    pm: *PackageManager,
    scope: *const npm.Registry.Scope,
    url: URL,
    accept: []const u8,
    err_name: []const u8,
    err_version: []const u8,
)![]const u8 {
    let headers: http.HeaderBuilder = .{};
    headers.count("Accept", accept);

    const registry = scope.url;
    const same_origin = std.mem.eql(u8, url.protocol, registry.protocol) &&
        std.mem.eql(u8, url.hostname, registry.hostname) &&
        url.getPortAuto() == registry.getPortAuto();
    const token = if (same_origin) scope.token else "";
    const auth = if (same_origin) scope.auth else "";

    if (token.len > 0) {
        headers.count("Authorization", "");
        headers.content.cap += "Bearer ".len + token.len;
        headers.count("npm-auth-type", "legacy");
    } else if (auth.len > 0) {
        headers.count("Authorization", "");
        headers.content.cap += "Basic ".len + auth.len;
        headers.count("npm-auth-type", "legacy");
    }

    headers.allocate(arena)?;
    headers.append("Accept", accept);
    if (token.len > 0) {
        headers.appendFmt("Authorization", "Bearer {s}", .{ token });
        headers.append("npm-auth-type", "legacy");
    } else if (auth.len > 0) {
        headers.appendFmt("Authorization", "Basic {s}", .{ auth });
        headers.append("npm-auth-type", "legacy");
    }

    let response_buf = MutableString.init(arena, 64 * 1024)?;
    let req = http.AsyncHTTP.initSync(
        arena,
        .GET,
        url,
        headers.entries,
        if (p := headers.content.ptr) p[0..headers.content.len] else "",
        &response_buf,
        "",
        pm.httpProxy(url),
        null,
        .follow,
    );
    req.client.flags.reject_unauthorized = pm.tlsRejectUnauthorized();
    const res = req.sendSync() catch e => {
        Status.clear();
        Output.err(e, "GET {s} failed", .{ url.href });
        Global.exit(1);
    };
    if (res.status_code >= 400) {
        Status.clear();
        npm.responseError(arena, &req, &res, .{ err_name, err_version }, &response_buf, false)?;
    }
    return response_buf.list.items;
}

// ─── diffing & printing ─────────────────────────────────────────────────────

enum Semantic {
    text,
    formatting_only,
    normalized,
    reprinted,
    whitespace_only,
    line_endings_only,
    mode_only,
    projected,
}

struct ModeChange {
    old: u32,
    new: u32,
}

struct SignalDelta {
    label: []const u8,
    count: usize,
}

struct Pos {
    line: u32,
    col: u32,
}

struct FileChange {
    path: []const u8,
    highlight: bool = false,
    sem: Semantic = .text,
    formatting_only: bool = false,
    mode_change: ModeChange? = null,
    not_normalized: ([]const u8)? = null,
    new_imports: std.ArrayListUnmanaged([]const u8) = .{},
    signal_deltas: std.ArrayListUnmanaged(SignalDelta) = .{},
    old: ([]const u8)? = null,
    new: ([]const u8)? = null,
    added: usize = 0,
    removed: usize = 0,
    binary: bool = false,
    sourcemap: bool = false,
    hidden: usize = 0,
    origin: ([2][]const Pos)? = null,
    hunks: std.ArrayListUnmanaged(u8) = .{},
}

fn isJsLike(path: []const u8): bool {
    if (k := normalize.kindFor(path)) {
        return k == .js || k == .json;
    }
    return std.mem.endsWith(u8, path, ".jsonc");
}

fn hasTerminalEscapes(bytes: []const u8): bool {
    return strings.containsChar(bytes, 0x1b) || strings.contains(bytes, "\xc2\x9b");
}

fn hasBidiControls(bytes: []const u8): bool {
    let start_at: usize = 0;
    while (idx := strings.indexOfChar(bytes[start_at..], 0xe2)) {
        const i = start_at + idx;
        if (i + 2 < bytes.len) {
            const b1 = bytes[i + 1];
            const b2 = bytes[i + 2];
            if ((b1 == 0x80 && b2 >= 0xaa && b2 <= 0xae) ||
                (b1 == 0x81 && b2 >= 0xa6 && b2 <= 0xa9))
            {
                return true;
            }
        }
        start_at = i + 1;
    }
    return false;
}

fn defangAlloc(arena: std.mem.Allocator, text: []const u8)![]const u8 {
    let has_suspicious = false;
    for (b of text) {
        if ((b < 0x20 && b != '\t') || b == 0x7f || b == 0xc2 || b == 0xe2) {
            has_suspicious = true;
            break;
        }
    }
    if (!has_suspicious) return text;
    let out = std.ArrayListUnmanaged(u8).initCapacity(arena, text.len + 16)?;
    let i: usize = 0;
    while (i < text.len) {
        const b = text[i];
        if (b == 0x1b) {
            out.appendSlice(arena, "␛")?;
        } else if ((b < 0x20 && b != '\t') || b == 0x7f) {
            out.append(arena, '^')?;
            out.append(arena, if (b == 0x7f) '?' else b + 0x40)?;
        } else if (b == 0xc2 && i + 1 < text.len && text[i + 1] >= 0x80 && text[i + 1] <= 0x9f) {
            let tmp: [16]u8 = undefined;
            const s = std.fmt.bufPrint(&tmp, "‹U+{X:0>4}›", .{ @as(u32, text[i + 1]) }) catch "";
            out.appendSlice(arena, s)?;
            i += 2;
            continue;
        } else if (b == 0xe2 && i + 2 < text.len &&
            ((text[i + 1] == 0x80 && text[i + 2] >= 0xaa && text[i + 2] <= 0xae) ||
                (text[i + 1] == 0x81 && text[i + 2] >= 0xa6 && text[i + 2] <= 0xa9)))
        {
            const codep: u32 = @as(u32, 0x2000) + (@as(u32, text[i + 1] & 0x3f) << 6) + @as(u32, text[i + 2] & 0x3f);
            let tmp: [16]u8 = undefined;
            const s = std.fmt.bufPrint(&tmp, "‹U+{X:0>4}›", .{ codep }) catch "";
            out.appendSlice(arena, s)?;
            i += 3;
            continue;
        } else {
            out.append(arena, b)?;
        }
        i += 1;
    }
    return out.items;
}

fn defang(buf: []u8, text: []const u8): []const u8 {
    let fba = std.heap.FixedBufferAllocator.init(buf);
    return defangAlloc(fba.allocator(), text) catch text;
}

fn stripCr(arena: std.mem.Allocator, bytes: []const u8)![]const u8 {
    let out = std.ArrayListUnmanaged(u8).initCapacity(arena, bytes.len)?;
    for (i of 0..bytes.len) {
        const b = bytes[i];
        if (!(b == '\r' && (i + 1 == bytes.len || bytes[i + 1] == '\n'))) {
            out.appendAssumeCapacity(b);
        }
    }
    return out.items;
}

fn collapseWhitespace(arena: std.mem.Allocator, bytes: []const u8)![]const u8 {
    let out = std.ArrayListUnmanaged(u8).initCapacity(arena, bytes.len)?;
    let pending_space = false;
    for (b of bytes) {
        if (std.ascii.isWhitespace(b)) {
            pending_space = out.items.len > 0;
        } else {
            if (pending_space) {
                out.appendAssumeCapacity(' ');
                pending_space = false;
            }
            out.appendAssumeCapacity(b);
        }
    }
    return out.items;
}

fn isBinary(bytes: []const u8): bool {
    const check_len = @min(bytes.len, 8192);
    return strings.containsChar(bytes[0..check_len], 0);
}

fn countLines(bytes: []const u8): usize {
    if (bytes.len == 0) return 0;
    return std.mem.count(u8, bytes, "\n") + @intFromBool(bytes[bytes.len - 1] != '\n');
}

fn optBytesEql(a: ([]const u8)?, b: ([]const u8)?): bool {
    if (a == null && b == null) return true;
    if (a == null || b == null) return false;
    return std.mem.eql(u8, a.?, b.?);
}

fn containsSlice(list: []const []const u8, item: []const u8): bool {
    for (x of list) {
        if (std.mem.eql(u8, x, item)) return true;
    }
    return false;
}

fn printDiff(arena: std.mem.Allocator, left: *const Tree, right: *const Tree, flags: DiffFlags)! {
    const style = Style.detect(flags);
    let changes: std.ArrayListUnmanaged(FileChange) = {};

    let paths: std.ArrayListUnmanaged([]const u8) = {};
    let lit = left.files.iterator();
    while (e := lit.next()) paths.append(arena, e.key_ptr.*)?;
    let rit = right.files.iterator();
    while (e := rit.next()) {
        if (!left.files.contains(e.key_ptr.*)) {
            paths.append(arena, e.key_ptr.*)?;
        }
    }
    const empty_ctx: void = undefined;
    std.sort.pdq([]const u8, paths.items, empty_ctx, strings.cmpStringsAsc);

    for (path of paths.items) {
        const old = left.files.get(path);
        const new = right.files.get(path);
        const old_mode = left.modes.get(path);
        const new_mode = right.modes.get(path);
        let mode_change: ModeChange? = null;
        if (old_mode != null && new_mode != null) {
            if (((old_mode.? ^ new_mode.?) & 0o111) != 0) {
                mode_change = ModeChange{ old: old_mode.?, new: new_mode.? };
            }
        } else if (old == null && new_mode != null && (new_mode.? & 0o111) != 0) {
            mode_change = ModeChange{ old: 0, new: new_mode.? };
        }

        const bytes_equal = optBytesEql(old, new);
        if (bytes_equal && mode_change == null) continue;

        let change = FileChange{
            .path = path,
            .highlight = style.pretty && isJsLike(path),
            .old = old,
            .new = new,
            .mode_change = mode_change,
        };

        if (bytes_equal) {
            change.sem = .mode_only;
            changes.append(arena, change)?;
            continue;
        }

        change.binary = (old != null && isBinary(old.?)) || (new != null && isBinary(new.?));
        const sample = new ?? old ?? "";
        change.sourcemap = std.mem.endsWith(u8, path, ".map") && std.mem.startsWith(u8, sample, "{\"version\"");
        if (change.binary || change.sourcemap) {
            changes.append(arena, change)?;
            continue;
        }

        if (style.pretty && old != null && new != null) {
            const o = old.?;
            const n = new.?;
            if ((strings.containsChar(o, '\r') || strings.containsChar(n, '\r')) &&
                std.mem.eql(u8, stripCr(arena, o)?, stripCr(arena, n)?))
            {
                change.sem = .line_endings_only;
                changes.append(arena, change)?;
                continue;
            }
        }

        if (flags.ignore_space && style.pretty && old != null && new != null) {
            if (std.mem.eql(u8, collapseWhitespace(arena, old.?)?, collapseWhitespace(arena, new.?)?)) {
                change.sem = .whitespace_only;
                changes.append(arena, change)?;
                continue;
            }
        }

        const nopts = normalize.Options{
            minify_syntax: flags.minify,
            dce: false,
            relayout: true,
        };
        const kind = normalize.kindFor(path);
        const js_family = kind != null && kind.? == .js;
        const readable_js = js_family &&
            !flags.unminify &&
            !(old != null && normalize.looksMinified(old.?)) &&
            !(new != null && normalize.looksMinified(new.?));
        const popts = if (readable_js) normalize.Options.KEY else nopts;

        let norm_old: normalize.Normalized? = null;
        let norm_new: normalize.Normalized? = null;
        if (!flags.raw) {
            if (readable_js && old != null && new != null) {
                if (pair := normalize.normalizeKeyPair(arena, path, old.?, new.?)) {
                    norm_old = pair[0];
                    norm_new = pair[1];
                } else {
                    norm_old = normalize.normalize(arena, path, old.?, popts);
                    norm_new = normalize.normalize(arena, path, new.?, popts);
                }
            } else {
                if (o := old) norm_old = normalize.normalize(arena, path, o, popts);
                if (n := new) norm_new = normalize.normalize(arena, path, n, popts);
            }
        }

        if (!flags.raw && kind != null) {
            const too_big = (old != null && old.?.len > normalize.MAX_BYTES) || (new != null && new.?.len > normalize.MAX_BYTES);
            if (too_big) {
                change.not_normalized = "too large to normalize";
            } else if ((old != null && norm_old == null) || (new != null && norm_new == null)) {
                change.not_normalized = "not parsed";
            }
        }

        if (nn := norm_new) {
            const old_imports: []const []const u8 = if (no := norm_old) no.imports else [];
            for (imp of nn.imports) {
                if (!containsSlice(old_imports, imp) && !containsSlice(change.new_imports.items, imp)) {
                    change.new_imports.append(arena, imp)?;
                }
            }
            let zero_signals: [9]usize = undefined;
            @memset(&zero_signals, 0);
            const before = if (no := norm_old) normalize.countSignals(no.text) else zero_signals;
            const after = normalize.countSignals(nn.text);
            for (i of 0..normalize.SIGNALS.len) {
                if (after[i] > before[i]) {
                    change.signal_deltas.append(arena, SignalDelta{
                        label: normalize.SIGNALS[i][1],
                        count: after[i] - before[i],
                    })?;
                }
            }
        }

        const reprint_equal = norm_old != null && norm_new != null && std.mem.eql(u8, norm_old.?.text, norm_new.?.text);
        change.formatting_only = reprint_equal && !js_family;

        if (old != null && new != null && norm_old != null && norm_new != null && style.pretty &&
            (js_family || norm_old.?.was_minified || norm_new.?.was_minified) &&
            (norm_old.?.was_minified || norm_new.?.was_minified || flags.unminify))
        {
            const o = old.?;
            const n = new.?;
            const no = norm_old.?;
            const nn = norm_new.?;
            change.sem = .normalized;
            const renamed = if (js_family) normalize.normalizeMinifiedPair(arena, path, o, n, nopts) else null;
            const disp_o = if (renamed != null) renamed.?[0] else no;
            const disp_n = if (renamed != null) renamed.?[1] else nn;
            const shown_o = withBanner(arena, o, &disp_o)?;
            const shown_n = withBanner(arena, n, &disp_n)?;
            const keys = if (js_family) normalize.normalizeKeyPair(arena, path, o, n) else null;
            if (kpair := keys) {
                if (kpair[0].map.len > 0 && kpair[1].map.len > 0) {
                    let ko = kpair[0];
                    let kn = kpair[1];
                    rebaseKeyOnto(arena, &ko, &shown_o, &disp_o)?;
                    rebaseKeyOnto(arena, &kn, &shown_n, &disp_n)?;
                    const proj = semantic.project(arena, shown_o.text, shown_n.text, &ko, &kn)?;
                    let any_shown = false;
                    for (op of proj.ops) {
                        if (op.kind != .equal || op.affected) {
                            any_shown = true;
                            break;
                        }
                    }
                    if (any_shown) {
                        change.hidden = proj.hidden;
                        change.origin = [ shown_o.origin, shown_n.origin ];
                        renderOps(arena, proj.ops, flags.context, style, &change, false, false)?;
                    } else {
                        change.sem = .formatting_only;
                        change.formatting_only = true;
                    }
                } else if (std.mem.eql(u8, shown_o.text, shown_n.text)) {
                    change.sem = .formatting_only;
                    change.formatting_only = true;
                } else {
                    change.origin = [ shown_o.origin, shown_n.origin ];
                    unifiedHunks(arena, shown_o.text, shown_n.text, flags.context, style, &change)?;
                }
            } else if (std.mem.eql(u8, shown_o.text, shown_n.text)) {
                change.sem = .formatting_only;
                change.formatting_only = true;
            } else {
                change.origin = [ shown_o.origin, shown_n.origin ];
                unifiedHunks(arena, shown_o.text, shown_n.text, flags.context, style, &change)?;
            }
        } else if (old != null && new != null && norm_old != null && norm_new != null && style.pretty && js_family) {
            const o = old.?;
            const n = new.?;
            const ko = norm_old.?;
            const kn = norm_new.?;
            if (ko.map.len > 0 && kn.map.len > 0) {
                const old_v = viewBytes(arena, o)?;
                const new_v = viewBytes(arena, n)?;
                const proj = semantic.project(arena, old_v, new_v, &ko, &kn)?;
                let any_shown = false;
                for (op of proj.ops) {
                    if (op.kind != .equal || op.affected) {
                        any_shown = true;
                        break;
                    }
                }
                if (any_shown) {
                    change.sem = .projected;
                    change.hidden = proj.hidden;
                    renderOps(arena, proj.ops, flags.context, style, &change, false, false)?;
                } else {
                    change.sem = .formatting_only;
                    change.formatting_only = true;
                }
            } else {
                unifiedHunks(arena, o, n, flags.context, style, &change)?;
            }
        } else if (old != null && new != null && norm_old != null && norm_new != null && style.pretty && reprint_equal) {
            change.sem = .formatting_only;
        } else if (old != null && new != null && norm_old != null && norm_new != null && style.pretty) {
            change.sem = .reprinted;
            unifiedHunks(arena, norm_old.?.text, norm_new.?.text, flags.context, style, &change)?;
        } else if (old == null && new != null) {
            change.added = countLines(new.?);
        } else if (old != null && new == null) {
            change.removed = countLines(old.?);
        } else if (old != null && new != null) {
            unifiedHunks(arena, old.?, new.?, flags.context, style, &change)?;
        }

        if (!flags.name_only && !flags.stat && change.hunks.items.len == 0 && change.sem == .text && (old == null || new == null)) {
            const is_ins = new != null;
            const raw_bytes = if (is_ins) new.? else old.?;
            const nz_opt = if (is_ins) norm_new else norm_old;
            let body = raw_bytes;
            if (nz := nz_opt) {
                if (style.pretty && nz.was_minified) {
                    const sh = withBanner(arena, raw_bytes, &nz)?;
                    body = sh.text;
                    change.sem = .normalized;
                    change.origin = [ sh.origin, sh.origin ];
                    if (is_ins) {
                        change.added = countLines(body);
                    } else {
                        change.removed = countLines(body);
                    }
                }
            }
            const n = countLines(body);
            const op_kind: Operation = if (is_ins) .insert else .delete;
            style.hunkHeader(arena, &change.hunks, if (is_ins) 0 else 1, if (is_ins) 0 else n, if (is_ins) 1 else 0, if (is_ins) n else 0, true)?;
            const unterminated = !style.pretty && body.len > 0 && body[body.len - 1] != '\n';
            let lit_b = semantic.lines(body);
            let li: usize = 0;
            while (line := lit_b.next()) : (li += 1) {
                let pos_opt: Pos? = null;
                if (orig := change.origin) {
                    pos_opt = if (li < orig[0].len) orig[0][li] else Pos{ line: 0, col: 0 };
                }
                style.line(arena, &change.hunks, op_kind, li + 1, li + 1, line, change.highlight, null, false, pos_opt)?;
            }
            if (unterminated) {
                change.hunks.appendSlice(arena, NO_NEWLINE_MARKER)?;
            }
        }

        changes.append(arena, change)?;
    }

    if (flags.json) {
        printJson(arena, left, right, changes.items, flags)?;
        return;
    }
    if (style.pretty) {
        Output.prettyln("", .{});
    }
    printHeader(arena, left, right, style)?;
    if (changes.items.len == 0) {
        Output.prettyln("<d>No differences ({d} files)<r>", .{ left.files.count() });
        return;
    }
    if (!style.pretty || flags.name_only || flags.stat) {
        Output.prettyln("", .{});
    }

    let path_width: usize = 0;
    let max_delta: usize = 1;
    for (c of changes.items) {
        const d_path = defangAlloc(arena, c.path)?;
        if (d_path.len > path_width) path_width = d_path.len;
        const delta = c.added + c.removed;
        if (delta > max_delta) max_delta = delta;
    }
    if (path_width > 60) path_width = 60;

    for (c of changes.items) {
        const status_str = if (c.old == null && c.new != null) "A" else if (c.old != null && c.new == null) "D" else "M";
        if (flags.name_only) {
            Output.print("{s} {s}\n", .{ status_str, defangAlloc(arena, c.path)? });
            continue;
        }
        if (flags.stat) {
            const sep = if (style.pretty) "│" else "|";
            const d_path = defangAlloc(arena, c.path)?;
            if (c.sem == .formatting_only || c.sem == .whitespace_only || c.sem == .line_endings_only || c.sem == .mode_only) {
                const what = switch (c.sem) {
                    .formatting_only => "formatting only",
                    .whitespace_only => "whitespace only",
                    .line_endings_only => "line endings only",
                    else => if (mc := c.mode_change) std.fmt.allocPrint(arena, "mode {o} → {o}", .{ mc.old, mc.new })? else "",
                };
                Output.pretty(" {s}", .{ d_path });
                if (d_path.len < path_width) {
                    for (pad_i of 0..(path_width - d_path.len)) {
                        * = pad_i;
                        Output.print(" ", .{});
                    }
                }
                Output.pretty(" <d>{s}<r> <d>{s}<r>\n", .{ sep, what });
                continue;
            }
            Output.pretty(" {s}", .{ d_path });
            if (d_path.len < path_width) {
                for (pad_i of 0..(path_width - d_path.len)) {
                    * = pad_i;
                    Output.print(" ", .{});
                }
            }
            if (c.binary || c.sourcemap) {
                const kind_str = if (c.sourcemap) "map" else "bin";
                const ob = if (o := c.old) o.len else 0;
                const nb = if (n := c.new) n.len else 0;
                Output.pretty(" <d>{s}<r> <yellow>{s}<r>   {d} → {d} bytes\n", .{ sep, kind_str, ob, nb });
            } else {
                const width: usize = 40;
                const add_n = if (max_delta <= width) c.added else (c.added * width + max_delta - 1) / max_delta;
                const del_n = if (max_delta <= width) c.removed else (c.removed * width + max_delta - 1) / max_delta;
                Output.pretty(" <d>{s}<r> {d: >5} <green>", .{ sep, c.added + c.removed });
                for (add_i of 0..add_n) {
                    * = add_i;
                    Output.print("+", .{});
                }
                Output.pretty("<r><red>", .{});
                for (del_i of 0..del_n) {
                    * = del_i;
                    Output.print("-", .{});
                }
                Output.pretty("<r>\n", .{});
            }
            continue;
        }
        if (style.pretty) {
            style.fileHeader(arena, &c)?;
            Output.printBytes(c.hunks.items);
            continue;
        }
        Output.printBytes("diff --bun a/");
        Output.printBytes(c.path);
        Output.printBytes(" b/");
        Output.printBytes(c.path);
        Output.printBytes("\n");
        if (c.old == null && c.new != null) {
            if (mc := c.mode_change) {
                Output.print("new file mode 100{o}\n", .{ mc.new });
            } else {
                Output.print("new file\n", .{});
            }
        } else if (c.old != null && c.new == null) {
            Output.print("deleted file\n", .{});
        } else if (mc := c.mode_change) {
            Output.print("old mode 100{o}\nnew mode 100{o}\n", .{ mc.old, mc.new });
        }
        if (c.sem == .mode_only) continue;
        if (c.binary || c.sourcemap) {
            const label = if (c.sourcemap) "Source map" else "Binary";
            const ob = if (o := c.old) o.len else 0;
            const nb = if (n := c.new) n.len else 0;
            Output.print("{s} files differ ({d} → {d} bytes)\n", .{ label, ob, nb });
            continue;
        }
        Output.printBytes("--- ");
        if (c.old != null) {
            Output.printBytes("a/");
            Output.printBytes(c.path);
        } else {
            Output.printBytes("/dev/null");
        }
        Output.printBytes("\n+++ ");
        if (c.new != null) {
            Output.printBytes("b/");
            Output.printBytes(c.path);
        } else {
            Output.printBytes("/dev/null");
        }
        Output.printBytes("\n");
        Output.printBytes(c.hunks.items);
    }
    printSummary(arena, left, right, changes.items, style)?;
}

fn printHeader(arena: std.mem.Allocator, left: *const Tree, right: *const Tree, style: Style)! {
    const ls = splitLabel(left.label);
    const rs = splitLabel(right.label);
    if (style.pretty && ls != null && rs != null && std.mem.eql(u8, ls.?[0], rs.?[0])) {
        Output.prettyln(
            "<b>{s}<r> <red>{s}<r> <d>→<r> <green>{s}<r>",
            .{ defangAlloc(arena, ls.?[0])?, defangAlloc(arena, ls.?[1])?, defangAlloc(arena, rs.?[1])? },
        );
    } else {
        Output.prettyln(
            "<b>{s}<r> <d>→<r> <b>{s}<r>",
            .{ defangAlloc(arena, left.label)?, defangAlloc(arena, right.label)? },
        );
    }
}

fn printSummary(arena: std.mem.Allocator, left: *const Tree, right: *const Tree, changes: []const FileChange, style: Style)! {
    let files_added: usize = 0;
    let files_removed: usize = 0;
    let files_changed: usize = 0;
    let lines_added: usize = 0;
    let lines_removed: usize = 0;
    let formatting_only: usize = 0;
    let whitespace_only: usize = 0;
    let endings_only: usize = 0;

    for (c of changes) {
        if (c.old == null && c.new != null) {
            files_added += 1;
        } else if (c.old != null && c.new == null) {
            files_removed += 1;
        } else {
            files_changed += 1;
        }
        lines_added += c.added;
        lines_removed += c.removed;
        switch (c.sem) {
            .formatting_only => formatting_only += 1,
            .whitespace_only => whitespace_only += 1,
            .line_endings_only => endings_only += 1,
            else => {},
        }
    }

    Output.prettyln("", .{});
    printHeader(arena, left, right, style)?;
    if (style.pretty) {
        const plural = if (changes.len == 1) "" else "s";
        Output.pretty("<d>{d} file{s}<r>  <green>+{d}<r> <red>-{d}<r>", .{ changes.len, plural, lines_added, lines_removed });
        if (files_added > 0) Output.pretty("  <d>·<r>  <green>{d} new<r>", .{ files_added });
        if (files_removed > 0) Output.pretty("  <d>·<r>  <red>{d} deleted<r>", .{ files_removed });
        if (formatting_only > 0) Output.pretty("  <d>·  {d} formatting only<r>", .{ formatting_only });
        if (whitespace_only > 0) Output.pretty("  <d>·  {d} whitespace only<r>", .{ whitespace_only });
        if (endings_only > 0) Output.pretty("  <d>·  {d} line endings only<r>", .{ endings_only });
        Output.pretty("\n", .{});
    } else {
        const plural = if (files_changed == 1) "" else "s";
        Output.prettyln(
            "{d} file{s} changed, {d} added, {d} removed  (+{d} -{d} lines)",
            .{ files_changed, plural, files_added, files_removed, lines_added, lines_removed },
        );
    }

    const notes = collectNotes(arena, left, right, changes, style.pretty)?;
    const mark = if (style.pretty) comptime Output.prettyFmt("  <yellow>▲<r> ", true) else "  ! ";
    for (n of notes) {
        Output.print("{s}{s}\n", .{ mark, n });
    }
}

fn collectNotes(
    arena: std.mem.Allocator,
    left: *const Tree,
    right: *const Tree,
    changes: []const FileChange,
    colors: bool,
)![]const []const u8 {
    let notes: std.ArrayListUnmanaged([]const u8) = {};
    packageJsonNotes(arena, left.files.get("package.json"), right.files.get("package.json"), &notes, colors)?;
    astNotes(arena, changes, &notes, colors)?;
    for (c of changes) {
        if (mc := c.mode_change) {
            if (mc.old == 0) {
                const args: [2][]const u8 = [ c.path, std.fmt.allocPrint(arena, "{o}", .{ mc.new })? ];
                notes.append(arena, fillNote(arena, colors, "new executable file <b>{}<r> <d>({})<r>", &args)?)?;
            } else if ((mc.new & 0o111) != 0) {
                const args: [3][]const u8 = [
                    c.path,
                    std.fmt.allocPrint(arena, "{o}", .{ mc.old })?,
                    std.fmt.allocPrint(arena, "{o}", .{ mc.new })?,
                ];
                notes.append(arena, fillNote(arena, colors, "now executable: <b>{}<r> <d>({} → {})<r>", &args)?)?;
            }
        }
        if (n := c.new) {
            if (!c.binary && hasTerminalEscapes(n) && !(c.old != null && hasTerminalEscapes(c.old.?))) {
                const args: [1][]const u8 = [ c.path ];
                notes.append(arena, fillNote(arena, colors, "terminal escape sequences in <b>{}<r> <d>(shown as ␛)<r>", &args)?)?;
            }
            if (!c.binary && hasBidiControls(n) && !(c.old != null && hasBidiControls(c.old.?))) {
                const args: [1][]const u8 = [ c.path ];
                notes.append(arena, fillNote(arena, colors, "bidirectional text controls in <b>{}<r> <d>(Trojan Source; shown as ‹U+202E›)<r>", &args)?)?;
            }
        }
        if (c.binary && c.old == null) {
            const nb = if (n := c.new) n.len else 0;
            const args: [2][]const u8 = [ c.path, std.fmt.allocPrint(arena, "{d}", .{ nb })? ];
            notes.append(arena, fillNote(arena, colors, "new binary file <b>{}<r> ({} bytes)", &args)?)?;
        }
        if (c.old == null && !c.binary &&
            (std.mem.endsWith(u8, c.path, ".node") || std.mem.endsWith(u8, c.path, ".sh") || std.mem.endsWith(u8, c.path, ".exe")))
        {
            const args: [1][]const u8 = [ c.path ];
            notes.append(arena, fillNote(arena, colors, "new executable-looking file <b>{}<r>", &args)?)?;
        }
    }
    return notes.items;
}

fn printJson(arena: std.mem.Allocator, left: *const Tree, right: *const Tree, changes: []const FileChange, flags: DiffFlags)! {
    let out: std.ArrayListUnmanaged(u8) = {};
    let w = out.writer(arena);
    w.print("{{\n  \"from\": {f},\n  \"to\": {f},\n  \"files\": [", .{
        bun.fmt.formatJSONStringUTF8(left.label, .{}),
        bun.fmt.formatJSONStringUTF8(right.label, .{}),
    })?;

    let lines_added: usize = 0;
    let lines_removed: usize = 0;
    let added: usize = 0;
    let deleted: usize = 0;
    let formatting_only: usize = 0;

    for (i of 0..changes.len) {
        const c = changes[i];
        let status = "modified";
        if (c.old == null && c.new != null) {
            added += 1;
            status = "added";
        } else if (c.old != null && c.new == null) {
            deleted += 1;
            status = "deleted";
        }
        lines_added += c.added;
        lines_removed += c.removed;
        if (c.formatting_only) formatting_only += 1;

        const ob = if (o := c.old) o.len else 0;
        const nb = if (n := c.new) n.len else 0;
        const comma = if (i == 0) "" else ",";
        w.print(
            "{s}\n    {{ \"path\": {f}, \"status\": \"{s}\", \"binary\": {}, \"sourceMap\": {}, \"formattingOnly\": {}, \"linesAdded\": {d}, \"linesRemoved\": {d}, \"bytesBefore\": {d}, \"bytesAfter\": {d}",
            .{ comma, bun.fmt.formatJSONStringUTF8(c.path, .{}), status, c.binary, c.sourcemap, c.formatting_only, c.added, c.removed, ob, nb },
        )?;
        if (mc := c.mode_change) {
            w.print(", \"modeBefore\": \"{o}\", \"modeAfter\": \"{o}\"", .{ mc.old, mc.new })?;
        }
        if (!flags.name_only && !flags.stat && !c.binary && !c.sourcemap) {
            w.print(", \"patch\": {f}", .{ bun.fmt.formatJSONStringUTF8(c.hunks.items, .{}) })?;
        }
        w.writeAll(" }")?;
    }

    const nl_indent = if (changes.len == 0) "" else "\n  ";
    w.print("{s}],\n  \"notes\": [", .{ nl_indent })?;
    const notes = collectNotes(arena, left, right, changes, false)?;
    for (i of 0..notes.len) {
        const comma = if (i == 0) "" else ", ";
        w.print("{s}{f}", .{ comma, bun.fmt.formatJSONStringUTF8(notes[i], .{}) })?;
    }
    w.print(
        "],\n  \"totals\": {{ \"files\": {d}, \"added\": {d}, \"deleted\": {d}, \"linesAdded\": {d}, \"linesRemoved\": {d}, \"formattingOnly\": {d} }}\n}}\n",
        .{ changes.len, added, deleted, lines_added, lines_removed, formatting_only },
    )?;
    Output.printBytes(out.items);
}

fn astNotes(arena: std.mem.Allocator, changes: []const FileChange, notes: *std.ArrayListUnmanaged([]const u8), colors: bool)! {
    const BuiltinEntry = struct { imp: []const u8, path: []const u8 };
    let builtins: std.ArrayListUnmanaged(BuiltinEntry) = {};
    let packages: std.ArrayListUnmanaged([]const u8) = {};

    for (c of changes) {
        for (imp of c.new_imports.items) {
            if (normalize.notableBuiltin(imp)) {
                let exists = false;
                for (b of builtins.items) {
                    if (std.mem.eql(u8, b.imp, imp)) {
                        exists = true;
                        break;
                    }
                }
                if (!exists) builtins.append(arena, .{ imp: imp, path: c.path })?;
            } else if (!std.mem.startsWith(u8, imp, ".") && !std.mem.startsWith(u8, imp, "/") && !containsSlice(packages.items, imp)) {
                packages.append(arena, imp)?;
            }
        }
    }

    for (b of builtins.items) {
        const args: [2][]const u8 = [ b.imp, b.path ];
        notes.append(arena, fillNote(arena, colors, "now imports <b><magenta>{}<r> <d>({})<r>", &args)?)?;
    }

    if (packages.items.len > 0) {
        let line: std.ArrayListUnmanaged(u8) = {};
        line.appendSlice(arena, "new module imports: ")?;
        const shown_len = @min(packages.items.len, 6);
        for (i of 0..shown_len) {
            if (i > 0) line.appendSlice(arena, ", ")?;
            const args: [1][]const u8 = [ packages.items[i] ];
            line.appendSlice(arena, fillNote(arena, colors, "<b>{}<r>", &args)?)?;
        }
        if (packages.items.len > 6) {
            const args: [1][]const u8 = [ std.fmt.allocPrint(arena, "{d}", .{ packages.items.len - 6 })? ];
            line.appendSlice(arena, fillNote(arena, colors, " <d>… and {} more<r>", &args)?)?;
        }
        notes.append(arena, line.items)?;
    }

    const TotalEntry = struct { label: []const u8, count: usize, path: []const u8 };
    let totals: std.ArrayListUnmanaged(TotalEntry) = {};
    for (c of changes) {
        for (sd of c.signal_deltas.items) {
            let found = false;
            for (*t of totals.items) {
                if (std.mem.eql(u8, t.label, sd.label)) {
                    t.count += sd.count;
                    found = true;
                    break;
                }
            }
            if (!found) totals.append(arena, .{ label: sd.label, count: sd.count, path: c.path })?;
        }
    }

    for (t of totals.items) {
        if (std.mem.endsWith(u8, t.label, "URL") && t.count < 3) continue;
        const args: [4][]const u8 = [
            std.fmt.allocPrint(arena, "{d}", .{ t.count })?,
            t.label,
            t.path,
            if (t.count > 1) ", …" else "",
        ];
        notes.append(arena, fillNote(arena, colors, "<b>+{}<r> {} <d>({}{})<r>", &args)?)?;
    }
}

fn fillNote(arena: std.mem.Allocator, colors: bool, comptime tpl: []const u8, args: []const []const u8)![]const u8 {
    const rendered = if (colors) comptime Output.prettyFmt(tpl, true) else comptime Output.prettyFmt(tpl, false);
    let out = std.ArrayListUnmanaged(u8).initCapacity(arena, rendered.len + 32)?;
    let rest: []const u8 = rendered;
    let idx: usize = 0;
    while (at := std.mem.indexOf(u8, rest, "{}")) {
        out.appendSlice(arena, rest[0..at])?;
        if (idx < args.len) {
            out.appendSlice(arena, defangAlloc(arena, args[idx])?)?;
            idx += 1;
        }
        rest = rest[at + 2 ..];
    }
    out.appendSlice(arena, rest)?;
    return out.items;
}

fn splitLabel(label: []const u8): ([2][]const u8)? {
    const at = strings.lastIndexOfChar(label, '@') ?? return null;
    if (at == 0) return null;
    return [ label[0..at], label[at + 1 ..] ];
}

fn packageJsonNotes(
    arena: std.mem.Allocator,
    old_opt: ([]const u8)?,
    new_opt: ([]const u8)?,
    notes: *std.ArrayListUnmanaged([]const u8),
    colors: bool,
)! {
    if (old_opt == null || new_opt == null) return;
    let log_o = logger.Log.init(arena);
    const src_o = logger.Source.initPathString("package.json", old_opt.?);
    const old = bun.json.parseUTF8(&src_o, &log_o, arena) catch return;
    let log_n = logger.Log.init(arena);
    const src_n = logger.Source.initPathString("package.json", new_opt.?);
    const new = bun.json.parseUTF8(&src_n, &log_n, arena) catch return;

    const script_keys: [7][]const u8 = [
        "preinstall", "install", "postinstall", "prepare", "prepublish", "preuninstall", "postuninstall",
    ];
    const old_scripts = old.getObject("scripts");
    const new_scripts = new.getObject("scripts");
    for (key of script_keys) {
        const o: ([]const u8)? = if (os := old_scripts) (os.getStringCloned(arena, key) catch null) else null;
        const n: ([]const u8)? = if (ns := new_scripts) (ns.getStringCloned(arena, key) catch null) else null;
        if (o == null && n != null) {
            const args: [2][]const u8 = [ key, n.? ];
            notes.append(arena, fillNote(arena, colors, "<b>{}<r> script added: <cyan>{}<r>", &args)?)?;
        } else if (o != null && n != null && !std.mem.eql(u8, o.?, n.?)) {
            const args: [2][]const u8 = [ key, n.? ];
            notes.append(arena, fillNote(arena, colors, "<b>{}<r> script changed: <cyan>{}<r>", &args)?)?;
        }
    }

    const dep_fields: [3][]const u8 = [ "dependencies", "optionalDependencies", "peerDependencies" ];
    const NameVer = struct { name: []const u8, ver: []const u8 };
    for (field of dep_fields) {
        let o_list: std.ArrayListUnmanaged(NameVer) = {};
        let n_list: std.ArrayListUnmanaged(NameVer) = {};
        if (oo := old.getObject(field)) {
            if (oo.data == .e_object) {
                for (p of oo.data.e_object.properties.slice()) {
                    if (p.key != null && p.value != null) {
                        if (k := p.key.?.asString(arena)) {
                            if (v := p.value.?.asString(arena)) {
                                o_list.append(arena, .{ name: k, ver: v })?;
                            }
                        }
                    }
                }
            }
        }
        if (no := new.getObject(field)) {
            if (no.data == .e_object) {
                for (p of no.data.e_object.properties.slice()) {
                    if (p.key != null && p.value != null) {
                        if (k := p.key.?.asString(arena)) {
                            if (v := p.value.?.asString(arena)) {
                                n_list.append(arena, .{ name: k, ver: v })?;
                            }
                        }
                    }
                }
            }
        }
        let bumps: std.ArrayListUnmanaged([]const u8) = {};
        for (nv of n_list.items) {
            let ov_opt: ([]const u8)? = null;
            for (ov of o_list.items) {
                if (std.mem.eql(u8, ov.name, nv.name)) {
                    ov_opt = ov.ver;
                    break;
                }
            }
            if (ov := ov_opt) {
                if (!std.mem.eql(u8, ov, nv.ver)) {
                    const args: [4][]const u8 = [ field, nv.name, ov, nv.ver ];
                    bumps.append(arena, fillNote(arena, colors, "{} <b>{}<r>: {} → {}", &args)?)?;
                }
            } else {
                const args: [3][]const u8 = [ field, nv.name, nv.ver ];
                notes.append(arena, fillNote(arena, colors, "{} added: <b>{}<r>@{}", &args)?)?;
            }
        }
        if (bumps.items.len > 4) {
            const rest = bumps.items.len - 3;
            bumps.items.len = 3;
            const args: [2][]const u8 = [ std.fmt.allocPrint(arena, "{d}", .{ rest })?, field ];
            bumps.append(arena, fillNote(arena, colors, "<d>… and {} more {} version changes<r>", &args)?)?;
        }
        notes.appendSlice(arena, bumps.items)?;
        for (ov of o_list.items) {
            let found = false;
            for (nv of n_list.items) {
                if (std.mem.eql(u8, nv.name, ov.name)) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                const args: [2][]const u8 = [ field, ov.name ];
                notes.append(arena, fillNote(arena, colors, "{} removed: <b>{}<r>", &args)?)?;
            }
        }
    }

    const meta_fields: [7][]const u8 = [ "main", "module", "types", "bin", "exports", "engines", "license" ];
    for (field of meta_fields) {
        const o: ([]const u8)? = if (oe := old.get(field)) exprText(arena, oe)? else null;
        const n: ([]const u8)? = if (ne := new.get(field)) exprText(arena, ne)? else null;
        if (!optBytesEql(o, n)) {
            if (o != null && n != null) {
                const args: [3][]const u8 = [ field, o.?, n.? ];
                notes.append(arena, fillNote(arena, colors, "<b>{}<r> changed: {} → {}", &args)?)?;
            } else if (n != null) {
                const args: [2][]const u8 = [ field, n.? ];
                notes.append(arena, fillNote(arena, colors, "<b>{}<r> added: {}", &args)?)?;
            } else {
                const args: [1][]const u8 = [ field ];
                notes.append(arena, fillNote(arena, colors, "<b>{}<r> removed", &args)?)?;
            }
        }
    }
}

fn exprText(arena: std.mem.Allocator, e: bun.ast.Expr)![]const u8 {
    if (s := e.asString(arena)) return s;
    const js_printer = bun.js_printer;
    let printer = js_printer.BufferPrinter.init(js_printer.BufferWriter.init(arena));
    const source = logger.Source.initPathString("package.json", "");
    * = js_printer.printJSON(*js_printer.BufferPrinter, &printer, e, &source, js_printer.Options{ mangled_props: null }) catch return "";
    let out: std.ArrayListUnmanaged(u8) = {};
    let last_space = false;
    for (b of printer.ctx.getWritten()) {
        const ws = b == '\n' || b == ' ' || b == '\t';
        if (ws) {
            if (!last_space) out.append(arena, ' ')?;
        } else {
            out.append(arena, b)?;
        }
        last_space = ws;
    }
    if (out.items.len > 120) {
        let cut: usize = 117;
        while (cut > 0 && (out.items[cut] & 0xC0) == 0x80) cut -= 1;
        out.items.len = cut;
        out.appendSlice(arena, "...")?;
    }
    return out.items;
}

struct Palette {
    del: []const u8,
    del_strong: []const u8,
    ins: []const u8,
    ins_strong: []const u8,
}

const PALETTES: [4]Palette = [
    Palette{ del: "\x1b[48;5;52m", del_strong: "\x1b[48;5;88m", ins: "\x1b[48;5;22m", ins_strong: "\x1b[48;5;28m" },
    Palette{ del: "\x1b[48;2;68;20;24m", del_strong: "\x1b[48;2;134;40;48m", ins: "\x1b[48;2;18;54;30m", ins_strong: "\x1b[48;2;30;104;52m" },
    Palette{ del: "\x1b[48;5;224m", del_strong: "\x1b[48;5;217m", ins: "\x1b[48;5;194m", ins_strong: "\x1b[48;5;157m" },
    Palette{ del: "\x1b[48;2;255;235;233m", del_strong: "\x1b[48;2;255;193;192m", ins: "\x1b[48;2;218;251;225m", ins_strong: "\x1b[48;2;172;238;187m" },
];

struct Style {
    pretty: bool,
    width: usize,
    palette: *const Palette,

    static fn detect(flags: DiffFlags): Style {
        const pretty = !flags.json && Output.enable_ansi_colors_stdout;
        let width: usize = 80;
        if (cols := bun.env_var.COLUMNS.get()) {
            width = @as(usize, cols);
        } else if (Output.terminal_size.col > 0) {
            width = @as(usize, Output.terminal_size.col);
        }
        width = std.math.clamp(width, @as(usize, 40), @as(usize, 160));
        let palette = &PALETTES[0];
        if (pretty) {
            const ct = bun.env_var.COLORTERM.get() ?? "";
            const truecolor = strings.eqlComptime(ct, "truecolor") || strings.eqlComptime(ct, "24bit");
            const light = bun.md.detectLightBackground();
            palette = &PALETTES[@as(usize, @intFromBool(light)) * 2 + @as(usize, @intFromBool(truecolor))];
        }
        return Style{
            pretty: pretty,
            width: width,
            palette: palette,
        };
    }

    fn hunkHeader(
        arena: std.mem.Allocator,
        out: *std.ArrayListUnmanaged(u8),
        old_start: usize,
        old_len: usize,
        new_start: usize,
        new_len: usize,
        first: bool,
    )! {
        if (_.pretty) {
            if (!first) out.appendSlice(arena, "\x1b[2m    ⋮\x1b[0m\n")?;
        } else {
            let w = out.writer(arena);
            w.print("@@ -{d},{d} +{d},{d} @@\n", .{ old_start, old_len, new_start, new_len })?;
        }
    }

    fn line(
        arena: std.mem.Allocator,
        out: *std.ArrayListUnmanaged(u8),
        op: Operation,
        old_no: usize,
        new_no: usize,
        text_in: []const u8,
        highlight: bool,
        emph_in: ([2]usize)?,
        affected: bool,
        pos_in: Pos?,
    )! {
        const sign: u8 = switch (op) {
            .equal => if (affected) '~' else ' ',
            .delete => '-',
            .insert => '+',
        };
        if (!_.pretty) {
            out.append(arena, sign)?;
            out.appendSlice(arena, text_in)?;
            out.append(arena, '\n')?;
            return;
        }
        const stripped = if (std.mem.endsWith(u8, text_in, "\r")) text_in[0 .. text_in.len - 1] else text_in;
        let emph = emph_in;
        if (e := emph_in) {
            const lo = @min(e[0], stripped.len);
            const hi = @min(e[1], stripped.len);
            const dlo = defangAlloc(arena, stripped[0..lo])?;
            const dhi = defangAlloc(arena, stripped[0..hi])?;
            emph = [ dlo.len, dhi.len ];
        }
        const text = defangAlloc(arena, stripped)?;

        let num: usize = new_no;
        let accent: []const u8 = "\x1b[2m";
        let bg: []const u8 = "";
        let strong: []const u8 = "";
        switch (op) {
            .equal => {
                if (affected) accent = "\x1b[33m";
            },
            .delete => {
                num = old_no;
                accent = "\x1b[31m";
                bg = _.palette.del;
                strong = _.palette.del_strong;
            },
            .insert => {
                num = new_no;
                accent = "\x1b[32m";
                bg = _.palette.ins;
                strong = _.palette.ins_strong;
            },
        }

        const has_pos = pos_in != null && pos_in.?.line > 0;
        let gutter_buf: [32]u8 = undefined;
        const gutter = if (has_pos)
            std.fmt.bufPrint(&gutter_buf, "{d}:{d}", .{ pos_in.?.line, pos_in.?.col })?
        else
            std.fmt.bufPrint(&gutter_buf, "{d}", .{ num })?;
        const gw: usize = if (has_pos) 9 else 5;

        let w = out.writer(arena);
        w.writeAll(accent)?;
        if (gutter.len < gw) {
            for (g_i of 0..(gw - gutter.len)) {
                * = g_i;
                w.writeByte(' ')?;
            }
        }
        w.writeAll(gutter)?;
        w.print(" \x1b[0m\x1b[2m│\x1b[0m{s}{s}\x1b[1m{c} \x1b[0m{s}", .{ bg, accent, sign, bg })?;

        const budget = if (_.width > gw + 4) _.width - (gw + 4) else 0;
        if (e := emph) {
            if (e[0] < e[1] || text.len > budget) {
                const lo = e[0];
                const hi = e[1];
                let prefix = text[0..lo];
                if (text.len > budget && lo > 32) {
                    let cut = lo - 24;
                    for (c_i of 0..16) {
                        * = c_i;
                        if (cut == 0 || !isIdentChar(text[cut - 1]) || !isIdentChar(text[cut])) break;
                        cut -= 1;
                    }
                    while (cut > 0 && (text[cut] & 0xC0) == 0x80) cut -= 1;
                    out.appendSlice(arena, "\x1b[2m…\x1b[22m")?;
                    out.appendSlice(arena, bg)?;
                    prefix = text[cut..lo];
                }
                _.segment(arena, out, prefix, highlight, bg)?;
                out.appendSlice(arena, strong)?;
                _.segment(arena, out, text[lo..hi], highlight, strong)?;
                out.appendSlice(arena, bg)?;
                _.segment(arena, out, text[hi..], highlight, bg)?;
            } else {
                _.segment(arena, out, text, highlight, bg)?;
            }
        } else if (op == .equal && text.len > budget && budget > 16) {
            let cut = budget - 1;
            while (cut > 0 && (text[cut] & 0xC0) == 0x80) cut -= 1;
            _.segment(arena, out, text[0..cut], highlight, bg)?;
            out.appendSlice(arena, "\x1b[2m…\x1b[0m")?;
        } else {
            _.segment(arena, out, text, highlight, bg)?;
        }

        out.appendSlice(arena, if (bg.len == 0) "\x1b[0m\n" else "\x1b[K\x1b[0m\n")?;
    }

    fn segment(arena: std.mem.Allocator, out: *std.ArrayListUnmanaged(u8), text: []const u8, highlight: bool, bg: []const u8)! {
        if (text.len == 0) return;
        if (!highlight || text.len > 4096) {
            out.appendSlice(arena, text)?;
            return;
        }
        const start = out.items.len;
        let w = out.writer(arena);
        w.print("{f}", .{ bun.fmt.fmtJavaScript(text, .{ .enable_colors = true, .check_for_unhighlighted_write = false }) }) catch {
            out.items.len = start;
            out.appendSlice(arena, text)?;
            return;
        };
        if (bg.len > 0) {
            const tail = arena.dupe(u8, out.items[start..])?;
            out.items.len = start;
            let it = std.mem.splitScalar(u8, tail, 0x1b);
            let idx: usize = 0;
            while (piece := it.next()) : (idx += 1) {
                if (idx > 0) out.append(arena, 0x1b)?;
                if (idx > 0 && std.mem.startsWith(u8, piece, "[0m")) {
                    out.appendSlice(arena, "[0m")?;
                    out.appendSlice(arena, bg)?;
                    out.appendSlice(arena, piece[3..])?;
                } else {
                    out.appendSlice(arena, piece)?;
                }
            }
        }
    }

    fn fileHeader(arena: std.mem.Allocator, c: *const FileChange)! {
        const note = switch (c.sem) {
            .formatting_only => "\x1b[2mformatting only\x1b[0m",
            .whitespace_only => "\x1b[2mwhitespace only\x1b[0m",
            .line_endings_only => "\x1b[2mline endings only\x1b[0m",
            .normalized => "\x1b[35munminified\x1b[0m ",
            .reprinted => "\x1b[2mnormalized\x1b[0m ",
            else => "",
        };
        let badge_buf: std.ArrayListUnmanaged(u8) = {};
        let bw = badge_buf.writer(arena);
        if (c.sem == .formatting_only || c.sem == .whitespace_only || c.sem == .line_endings_only) {
            bw.writeAll(note)?;
        } else if (c.sourcemap) {
            const ob = if (o := c.old) o.len else 0;
            const nb = if (n := c.new) n.len else 0;
            bw.print("\x1b[2msource map {d} → {d} bytes\x1b[0m", .{ ob, nb })?;
        } else if (c.binary) {
            const ob = if (o := c.old) o.len else 0;
            const nb = if (n := c.new) n.len else 0;
            bw.print("\x1b[33mbinary\x1b[0m \x1b[2m{d} → {d} bytes\x1b[0m", .{ ob, nb })?;
        } else if (c.old == null && c.new != null) {
            bw.print("{s}\x1b[32mnew\x1b[0m \x1b[32m+{d}\x1b[0m", .{ note, c.added })?;
        } else if (c.old != null && c.new == null) {
            bw.print("{s}\x1b[31mdeleted\x1b[0m \x1b[31m-{d}\x1b[0m", .{ note, c.removed })?;
        } else {
            bw.writeAll(note)?;
            if (c.added > 0) bw.print("\x1b[32m+{d}\x1b[0m", .{ c.added })?;
            if (c.removed > 0) {
                if (c.added > 0 || note.len > 0) bw.writeByte(' ')?;
                bw.print("\x1b[31m-{d}\x1b[0m", .{ c.removed })?;
            }
        }
        let badge = badge_buf.items;
        if (mc := c.mode_change) {
            const mode = if (mc.old == 0)
                std.fmt.allocPrint(arena, "\x1b[33mexecutable\x1b[0m {o}", .{ mc.new })?
            else
                std.fmt.allocPrint(arena, "\x1b[33mmode\x1b[0m {o} → {o}", .{ mc.old, mc.new })?;
            badge = if (c.sem == .mode_only) mode else std.fmt.allocPrint(arena, "{s} {s}", .{ mode, badge })?;
        }
        if (c.hidden > 0) {
            badge = std.fmt.allocPrint(arena, "\x1b[2m{d} folded\x1b[0m {s}", .{ c.hidden, badge })?;
        }
        if (why := c.not_normalized) {
            badge = std.fmt.allocPrint(arena, "\x1b[2m{s}\x1b[0m {s}", .{ why, badge })?;
        }

        const badge_width = stripAnsiLen(badge);
        let shown_path = defangAlloc(arena, c.path)?;
        const room = if (_.width > badge_width + 8) _.width - (badge_width + 8) else 0;
        if (stripAnsiLen(shown_path) > room && room > 8) {
            let cut = shown_path.len - @min(shown_path.len, room - 1);
            while (cut < shown_path.len && (shown_path[cut] & 0xC0) == 0x80) cut += 1;
            shown_path = std.fmt.allocPrint(arena, "…{s}", .{ shown_path[cut..] })?;
        }
        const used = stripAnsiLen(shown_path) + badge_width + 4;
        const rule = @max(if (_.width > used) _.width - used else 0, @as(usize, 2));
        Output.print("\n\x1b[1m{s}\x1b[0m \x1b[2m", .{ shown_path });
        for (r_i of 0..rule) {
            * = r_i;
            Output.print("─", .{});
        }
        Output.print("\x1b[0m {s}\n", .{ badge });
    }
}

fn isIdentChar(b: u8): bool {
    return std.ascii.isAlphanumeric(b) || b == '_' || b == '$' || (b & 0x80) != 0;
}

fn stripAnsiLen(s: []const u8): usize {
    let n: usize = 0;
    let in_esc = false;
    for (b of s) {
        if (!in_esc && b == 0x1b) {
            in_esc = true;
        } else if (in_esc && b == 'm') {
            in_esc = false;
        } else if (!in_esc && (b & 0xC0) != 0x80) {
            n += 1;
        }
    }
    return n;
}

fn pairEmphasis(ops: []const Op, k: usize): ([2]usize)? {
    const op = ops[k].kind;
    const text = ops[k].text;
    let partner: []const u8 = "";
    if (op == .delete &&
        k + 1 < ops.len &&
        ops[k + 1].kind == .insert &&
        (k + 2 >= ops.len || ops[k + 2].kind != .insert) &&
        (k == 0 || ops[k - 1].kind != .delete))
    {
        partner = ops[k + 1].text;
    } else if (op == .insert &&
        k > 0 &&
        ops[k - 1].kind == .delete &&
        (k + 1 >= ops.len || ops[k + 1].kind != .insert) &&
        (k < 2 || ops[k - 2].kind != .delete))
    {
        partner = ops[k - 1].text;
    } else {
        return null;
    }

    const min_len = @min(text.len, partner.len);
    let lo: usize = 0;
    while (lo < min_len && text[lo] == partner[lo]) lo += 1;
    const max_suffix = min_len - lo;
    let suffix: usize = 0;
    while (suffix < max_suffix && text[text.len - 1 - suffix] == partner[partner.len - 1 - suffix]) suffix += 1;
    let hi = text.len - suffix;
    while (lo > 0 && lo < text.len && (text[lo] & 0xC0) == 0x80) lo -= 1;
    while (hi < text.len && (text[hi] & 0xC0) == 0x80) hi += 1;
    return [ lo, hi ];
}

const NO_NEWLINE_MARKER: []const u8 = "\\ No newline at end of file\n";

fn unifiedHunks(
    arena: std.mem.Allocator,
    old_in: []const u8,
    new_in: []const u8,
    context: usize,
    style: Style,
    change: *FileChange,
)! {
    const old = if (style.pretty) viewBytes(arena, old_in)? else old_in;
    const new = if (style.pretty) viewBytes(arena, new_in)? else new_in;
    const ops = semantic.lineOps(arena, old, new)?;
    const old_unterminated = old.len > 0 && old[old.len - 1] != '\n';
    const new_unterminated = new.len > 0 && new[new.len - 1] != '\n';
    renderOps(arena, ops, context, style, change, old_unterminated, new_unterminated)?;
}

struct Shown {
    text: []const u8,
    origin: []const Pos,
    banner: u32,
    skipped: u32,
}

fn withBanner(arena: std.mem.Allocator, raw: []const u8, norm: *const normalize.Normalized): Shown! {
    let i: usize = 0;
    while (true) {
        while (i < raw.len && std.ascii.isWhitespace(raw[i])) i += 1;
        if (std.mem.startsWith(u8, raw[i..], "//")) {
            if (e := strings.indexOfChar(raw[i..], '\n')) {
                i += e;
            } else {
                i = raw.len;
            }
        } else if (std.mem.startsWith(u8, raw[i..], "/*")) {
            if (e := std.mem.indexOf(u8, raw[i + 2 ..], "*/")) {
                i += 2 + e + 2;
            } else {
                i = raw.len;
            }
        } else {
            break;
        }
    }
    let end: usize = 0;
    if (p := strings.lastIndexOfChar(raw[0..i], '\n')) {
        end = p + 1;
    }
    const banner = raw[0..end];
    let text = std.ArrayListUnmanaged(u8).initCapacity(arena, banner.len + norm.text.len)?;
    let origin: std.ArrayListUnmanaged(Pos) = {};
    let bit = semantic.lines(banner);
    let l: u32 = 0;
    while (line := bit.next()) : (l += 1) {
        const trimmed = std.mem.trimRight(u8, line, " \t\r");
        text.appendSlice(arena, trimmed)?;
        text.append(arena, '\n')?;
        origin.append(arena, Pos{ line: l + 1, col: 1 })?;
    }

    const printed_origin = originOf(arena, norm)?;
    let skip: usize = 0;
    let in_block = false;
    let offset: usize = 0;
    let nit = semantic.lines(norm.text);
    while (line := nit.next()) {
        const t = std.mem.trim(u8, line, " \t\r");
        const comment = in_block || t.len == 0 || std.mem.startsWith(u8, t, "//") || std.mem.startsWith(u8, t, "/*");
        if (!comment || banner.len == 0) break;
        if (std.mem.startsWith(u8, t, "/*")) in_block = true;
        if (in_block && std.mem.indexOf(u8, t, "*/") != null) in_block = false;
        skip += 1;
        offset += line.len + 1;
    }

    const banner_len = @as(u32, @intCast(origin.items.len));
    text.appendSlice(arena, norm.text[@min(offset, norm.text.len)..])?;
    if (skip < printed_origin.len) {
        origin.appendSlice(arena, printed_origin[skip..])?;
    }
    return Shown{
        text: text.items,
        origin: origin.items,
        banner: banner_len,
        skipped: @as(u32, @intCast(skip)),
    };
}

fn rebaseKeyOnto(
    arena: std.mem.Allocator,
    key: *normalize.Normalized,
    shown: *const Shown,
    display: *const normalize.Normalized,
)! {
    const KeyPos = struct { orig_line: u32, orig_col: u32 };
    let at: std.AutoHashMapUnmanaged(KeyPos, Pos) = {};
    let k = display.map.len;
    while (k > 0) {
        k -= 1;
        const m = display.map[k];
        if (m.gen_line >= shown.skipped) {
            at.put(
                arena,
                KeyPos{ orig_line: m.orig_line, orig_col: m.orig_col },
                Pos{ line: m.gen_line + shown.banner - shown.skipped, col: m.gen_col },
            )?;
        }
    }
    let out: std.ArrayListUnmanaged(normalize.MapPoint) = {};
    for (m of key.map) {
        if (p := at.get(KeyPos{ orig_line: m.orig_line, orig_col: m.orig_col })) {
            out.append(arena, normalize.MapPoint{
                gen_line: m.gen_line,
                gen_col: m.gen_col,
                orig_line: p.line,
                orig_col: p.col,
            })?;
        }
    }
    key.map = out.items;
}

fn originOf(arena: std.mem.Allocator, norm: *const normalize.Normalized)![]const Pos {
    const line_count = std.mem.count(u8, norm.text, "\n") + 1;
    let out = arena.alloc(Pos, line_count)?;
    @memset(out, Pos{ line: 0, col: 0 });
    let indents = arena.alloc(u32, line_count)?;
    @memset(indents, 0);

    let it = semantic.lines(norm.text);
    let idx: usize = 0;
    while (line := it.next()) : (idx += 1) {
        if (idx >= line_count) break;
        let sp: u32 = 0;
        for (b of line) {
            if (b != ' ') break;
            sp += 1;
        }
        indents[idx] = sp;
    }

    let k = norm.map.len;
    while (k > 0) {
        k -= 1;
        const m = norm.map[k];
        const g = @as(usize, m.gen_line);
        if (g < line_count) {
            if (m.gen_col >= indents[g] || (out[g].line == 0 && out[g].col == 0)) {
                out[g] = Pos{ line: m.orig_line + 1, col: m.orig_col + 1 };
            }
        }
    }
    for (i of 1..out.len) {
        if (out[i].line == 0 && out[i].col == 0) {
            out[i] = out[i - 1];
        }
    }
    return out;
}

fn viewBytes(arena: std.mem.Allocator, bytes: []const u8)![]const u8 {
    const terminated = bytes.len == 0 || bytes[bytes.len - 1] == '\n';
    if (strings.containsChar(bytes, '\r') || !terminated) {
        const stripped = stripCr(arena, bytes)?;
        if (stripped.len > 0 && stripped[stripped.len - 1] != '\n') {
            return std.fmt.allocPrint(arena, "{s}\n", .{ stripped });
        }
        return stripped;
    }
    return bytes;
}

fn isOpChange(op: Op): bool {
    return op.kind != .equal || op.affected;
}

fn renderOps(
    arena: std.mem.Allocator,
    ops: []const Op,
    context: usize,
    style: Style,
    change: *FileChange,
    old_unterminated: bool,
    new_unterminated: bool,
)! {
    for (op of ops) {
        switch (op.kind) {
            .insert => change.added += 1,
            .delete => change.removed += 1,
            .equal => {},
        }
    }
    let last_old: usize? = null;
    let last_new: usize? = null;
    let r = ops.len;
    while (r > 0) {
        r -= 1;
        if (last_old == null && ops[r].kind != .insert) last_old = r;
        if (last_new == null && ops[r].kind != .delete) last_new = r;
        if (last_old != null && last_new != null) break;
    }

    const n = ops.len;
    let i: usize = 0;
    let first = true;
    while (i < n) {
        if (!isOpChange(ops[i])) {
            i += 1;
            continue;
        }
        let start = i;
        while (start > 0 && !isOpChange(ops[start - 1]) && i - start < context) {
            start -= 1;
        }
        let end_scan = i;
        let quiet: usize = 0;
        while (end_scan < n) {
            if (isOpChange(ops[end_scan])) {
                quiet = 0;
            } else {
                quiet += 1;
                if (quiet > 2 * context) break;
            }
            end_scan += 1;
        }
        let e = end_scan;
        let trailing: usize = 0;
        while (e > start && !isOpChange(ops[e - 1])) {
            trailing += 1;
            e -= 1;
        }
        const end = e + @min(trailing, context);
        const window = ops[start..end];

        let old_len: usize = 0;
        let new_len: usize = 0;
        let old_start: usize? = null;
        let new_start: usize? = null;
        for (op of window) {
            if (op.kind != .insert) {
                old_len += 1;
                if (old_start == null) old_start = op.old_no;
            }
            if (op.kind != .delete) {
                new_len += 1;
                if (new_start == null) new_start = op.new_no;
            }
        }
        style.hunkHeader(
            arena,
            &change.hunks,
            old_start ?? window[0].old_no,
            old_len,
            new_start ?? window[0].new_no,
            new_len,
            first,
        )?;
        first = false;

        for (k of 0..window.len) {
            const op = window[k];
            const emph = pairEmphasis(window, @as(usize, @intCast(k)));
            let pos_opt: Pos? = null;
            if (orig := change.origin) {
                const side = if (op.kind == .delete) orig[0] else orig[1];
                const no = if (op.kind == .delete) op.old_no else op.new_no;
                pos_opt = if (no > 0 && no - 1 < side.len) side[no - 1] else Pos{ line: 0, col: 0 };
            }
            style.line(arena, &change.hunks, op.kind, op.old_no, op.new_no, op.text, change.highlight, emph, op.affected, pos_opt)?;
            const idx = start + @as(usize, @intCast(k));
            if (!style.pretty &&
                ((last_old != null && idx == last_old.? && old_unterminated) ||
                    (last_new != null && idx == last_new.? && new_unterminated)))
            {
                change.hunks.appendSlice(arena, NO_NEWLINE_MARKER)?;
            }
        }
        i = end;
    }
}
