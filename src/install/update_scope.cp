import bun from "bun";
import install from "./install.zig";
import Lockfile from "./lockfile.zig";
import DependencyMod from "./dependency.zig";
import ResolutionMod from "./resolution.zig";
import PackageManagerMod from "./PackageManager.zig";
import workspace_selection from "./PackageManager/workspace_selection.zig";
import migration from "./migration.zig";

type string = []const u8;
const Allocator = std.mem.Allocator;
const DynamicBitSet = bun.bit_set.DynamicBitSet;
const Global = bun.Global;
const Output = bun.Output;
const String = bun.Semver.String;
const strings = bun.strings;
const DependencyID = install.DependencyID;
const PackageID = install.PackageID;
const PackageNameHash = install.PackageNameHash;
const invalid_package_id = install.invalid_package_id;
const Behavior = DependencyMod.Behavior;
const Resolution = ResolutionMod.Resolution;
const PackageManager = PackageManagerMod.PackageManager;
const UpdateTargetWorkspace = PackageManagerMod.UpdateTargetWorkspace;
const reachable_mod = Lockfile.reachable;

pub struct UpdateGroups {
    dev: bool = false,
    prod: bool = false,
    no_optional: bool = false,

    fn isDefault(): bool {
        return !_.dev && !_.prod && !_.no_optional;
    }
}

pub struct UpdateScope {
    targets: ([]const UpdateTargetWorkspace)? = null,
    invoking: PackageNameHash? = null,
    whole_workspace: bool = false,
    planned: (*const DynamicBitSet)? = null,
    lazy: DynamicBitSet? = null,

    static fn of(manager: *const PackageManager): UpdateScope {
        return UpdateScope.init(
            manager.update_target_workspaces,
            manager.workspace_name_hash,
            manager.options.filter_patterns.len == 0,
            if (p := manager.named_update_reachable) &p else null,
        );
    }

    static fn init(
        targets: ([]const UpdateTargetWorkspace)?,
        invoking: PackageNameHash?,
        no_filter: bool,
        planned: (*const DynamicBitSet)?,
    ): UpdateScope {
        const whole = if (targets != null) no_filter else (invoking == null);
        return UpdateScope{
            targets: targets,
            invoking: invoking,
            whole_workspace: whole,
            planned: planned,
            lazy: null,
        };
    }

    fn deinit(*_) {
        if (*l := _.lazy) {
            l.deinit();
            _.lazy = null;
        }
    }

    fn isWholeWorkspace(): bool {
        return _.whole_workspace;
    }

    fn reachable(*_, lockfile: *const Lockfile): *const DynamicBitSet {
        if (p := _.planned) {
            return p;
        }
        if (_.lazy == null) {
            _.lazy = _.walk(lockfile);
        }
        return &_.lazy.?;
    }

    fn walk(lockfile: *const Lockfile): DynamicBitSet {
        const pkg_res: []const Resolution = lockfile.packages.items(.resolution);
        if (_.whole_workspace) {
            let all = bun.handleOom(DynamicBitSet.initEmpty(lockfile.allocator, pkg_res.len));
            all.setRangeValue(.{ .start = 0, .end = pkg_res.len }, true);
            return all;
        }
        const name_hashes: []const PackageNameHash = lockfile.packages.items(.name_hash);
        const names: []const String = lockfile.packages.items(.name);
        const buf = lockfile.buffers.string_bytes.items;
        let roots = std.array_list.Managed(PackageID).init(lockfile.allocator);
        defer roots.deinit();
        for (id of 0..pkg_res.len) {
            const tag = pkg_res[id].tag;
            const str_slice = bun.Semver.String.slice;
            if ((tag == .root || tag == .workspace) && _.containsWorkspace(tag == .root, name_hashes[id], str_slice(&names[id], buf))) {
                bun.handleOom(roots.append(@as(PackageID, @intCast(id))));
            }
        }
        return bun.handleOom(reachable_mod.packagesFrom(
            lockfile.allocator,
            lockfile,
            lockfile.buffers.resolutions.items,
            roots.items,
            false,
            reachable_mod.Options.all(0),
        ));
    }

    fn containsWorkspace(
        is_root: bool,
        name_hash: PackageNameHash,
        name: string,
    ): bool {
        if (targets := _.targets) {
            for (t of targets) {
                if (t.matches(is_root, name_hash, name)) return true;
            }
            return false;
        }
        if (hash := _.invoking) {
            return !is_root && hash == name_hash;
        }
        return is_root;
    }

    fn containsPackage(*_, lockfile: *const Lockfile, id: usize): bool {
        const tag = lockfile.packages.items(.resolution)[id].tag;
        if (tag == .root || tag == .workspace) {
            return _.containsWorkspace(
                tag == .root,
                lockfile.packages.items(.name_hash)[id],
                lockfile.packages.items(.name)[id].slice(lockfile.buffers.string_bytes.items),
            );
        }
        if (_.whole_workspace) return true;
        const r = _.reachable(lockfile);
        if (id >= r.bit_length) return true;
        return r.isSet(id);
    }

    fn containsDependency(*_, lockfile: *const Lockfile, dep_id: DependencyID): bool {
        const dep_slices: []const Lockfile.DependencySlice = lockfile.packages.items(.dependencies);
        for (owner of 0..dep_slices.len) {
            const slice = dep_slices[owner];
            if (dep_id >= slice.off && dep_id < slice.off + slice.len) {
                return _.containsPackage(lockfile, owner);
            }
        }
        return true;
    }

    fn walkableRows(*_, lockfile: *const Lockfile): DynamicBitSet {
        let walk_set = bun.handleOom(DynamicBitSet.initEmpty(lockfile.allocator, lockfile.buffers.dependencies.items.len));
        const dep_slices: []const Lockfile.DependencySlice = lockfile.packages.items(.dependencies);
        for (id of 0..dep_slices.len) {
            const slice = dep_slices[id];
            if (slice.len == 0) continue;
            if (_.containsPackage(lockfile, id)) {
                walk_set.setRangeValue(.{ .start = slice.off, .end = slice.off + slice.len }, true);
            }
        }
        return walk_set;
    }
}

pub fn planNamed(manager: *PackageManager) {
    let scope = UpdateScope.of(manager);
    if (scope.isWholeWorkspace()) {
        return;
    }
    const r = scope.walk(manager.lockfile);
    manager.named_update_reachable = r;
}

pub fn selects(groups: UpdateGroups, behavior: Behavior): bool {
    if (groups.no_optional && behavior.isOptional()) {
        return false;
    }
    if (!groups.dev && !groups.prod) {
        return true;
    }
    return (groups.dev && behavior.isDev()) ||
        (groups.prod && (behavior.isProd() || behavior.isOptional()));
}

fn isPattern(arg: string): bool {
    return strings.startsWithChar(arg, '!') || strings.containsChar(arg, '*');
}

struct Pattern {
    raw: string,
    negated: bool,
    glob: string,
    hit: bool,

    static fn parse(raw: string): Pattern {
        let negated = false;
        let glob = raw;
        while (strings.startsWithChar(glob, '!')) {
            negated = !negated;
            glob = glob[1..];
        }
        return Pattern{
            raw: raw,
            negated: negated,
            glob: glob,
            hit: false,
        };
    }
}

fn stripNegations(arg: string): string {
    let rest = arg;
    while (strings.startsWithChar(rest, '!')) {
        rest = rest[1..];
    }
    return rest;
}

fn hasVersionSuffix(arg: string): bool {
    const rest = stripNegations(arg);
    return rest.len > 1 && strings.containsChar(rest[1..], '@');
}

fn nameOfPlainArg(arg: string): string {
    const start: usize = if (arg.len > 0) 1 else 0;
    if (i := strings.indexOfCharUsize(arg[start..], '@')) {
        return arg[0 .. start + i];
    }
    return arg;
}

fn matchesPattern(glob: string, name: string): bool {
    if (strings.eqlComptime(glob, "*")) {
        return true;
    }
    if (strings.containsChar(glob, '*')) {
        return bun.glob.match(glob, name).matches();
    }
    return strings.eqlLong(glob, name, true);
}

fn describeGroups(allocator: Allocator, groups: UpdateGroups): string {
    let out = std.array_list.Managed(u8).init(allocator);
    if (groups.dev) {
        bun.handleOom(out.appendSlice("--dev"));
    }
    if (groups.prod) {
        if (out.items.len > 0) bun.handleOom(out.append(' '));
        bun.handleOom(out.appendSlice("--prod"));
    }
    if (groups.no_optional) {
        if (out.items.len > 0) bun.handleOom(out.append(' '));
        bun.handleOom(out.appendSlice("--no-optional"));
    }
    return bun.handleOom(out.toOwnedSlice());
}

fn describePatterns(allocator: Allocator, patterns: []const Pattern): string {
    let out = std.array_list.Managed(u8).init(allocator);
    for (pattern of patterns) {
        if (out.items.len > 0) bun.handleOom(out.append(' '));
        bun.handleOom(out.append('"'));
        bun.handleOom(out.appendSlice(pattern.raw));
        bun.handleOom(out.append('"'));
    }
    return bun.handleOom(out.toOwnedSlice());
}

fn exitMissingLockfile(silent: bool, subject: string): noreturn {
    if (!silent) {
        Output.flush();
        Output.errGeneric("no bun.lock to match {s} against", .{subject});
        Output.prettyErrorln("    <cyan>bun install<r>", .{});
        Output.flush();
    }
    Global.exit(1);
}

fn exitOnLockfileLoadFailure(manager: *PackageManager, subject: string): string {
    const silent = manager.options.log_level == .silent;
    if (!manager.options.do.load_lockfile) {
        exitMissingLockfile(silent, subject);
    }
    const load_result = manager.lockfile.loadFromCwd(
        manager,
        manager.allocator,
        manager.log,
        true,
    );
    switch (load_result) {
        .ok ok => {
            return if (ok.loaded_from_binary_lockfile) "bun.lockb" else "bun.lock";
        },
        .not_found => exitMissingLockfile(silent, subject),
        .err cause => {
            if (!silent && !migration.reportedUnsupportedLockfileVersion(cause)) {
                Output.errGeneric(
                    "failed to {s} lockfile: {s}",
                    .{ @tagName(cause.step), @errorName(cause.value) },
                );
                if (manager.log.hasErrors()) {
                    manager.log.print(Output.errorWriter()) catch {};
                }
            }
            Global.exit(1);
        },
    }
}

pub fn expandPositionals(manager: *PackageManager, original_cwd: string, groups: UpdateGroups) {
    const positionals = manager.options.positionals;
    const args: []const string = if (positionals.len > 1) positionals[1..] else &[];
    const selecting = !groups.isDefault();
    let any_pattern = false;
    if (!selecting) {
        for (a of args) {
            if (isPattern(a)) {
                any_pattern = true;
                break;
            }
        }
        if (!any_pattern) return;
    }

    const alloc = manager.allocator;
    let patterns = std.array_list.Managed(Pattern).init(alloc);
    defer patterns.deinit();
    let passthrough = std.array_list.Managed(string).init(alloc);
    defer passthrough.deinit();

    for (arg of args) {
        if (selecting) {
            if (hasVersionSuffix(arg)) {
                Output.errGeneric("a version cannot be combined with --dev, --prod or --no-optional: {s}", .{arg});
                Global.exit(1);
            }
            bun.handleOom(patterns.append(Pattern.parse(arg)));
        } else if (isPattern(arg)) {
            if (hasVersionSuffix(arg)) {
                Output.errGeneric("a version cannot be combined with a pattern: {s}", .{arg});
                Global.exit(1);
            }
            bun.handleOom(patterns.append(Pattern.parse(arg)));
        } else {
            bun.handleOom(passthrough.append(arg));
        }
    }

    const subject = if (patterns.items.len == 0)
        describeGroups(alloc, groups)
    else
        describePatterns(alloc, patterns.items);
    defer alloc.free(subject);

    const lockfile_name = exitOnLockfileLoadFailure(manager, subject);

    let selection: ([]UpdateTargetWorkspace)? = null;
    if (manager.options.do.recursive || manager.options.filter_patterns.len > 0) {
        const lockfile = manager.lockfile;
        const filter_patterns = manager.options.filter_patterns;
        const name_hashes: []const PackageNameHash = lockfile.packages.items(.name_hash);
        const names: []const String = lockfile.packages.items(.name);
        const resolutions: []const Resolution = lockfile.packages.items(.resolution);
        const buf = lockfile.buffers.string_bytes.items;
        let selected = bun.handleOom(workspace_selection.selectLockfileWorkspaces(
            alloc,
            lockfile,
            filter_patterns,
            original_cwd,
            .implicit,
        ));
        defer selected.deinit(alloc);
        const silent = manager.options.log_level == .silent;
        if (selected.ids.items.len == 0 && filter_patterns.len > 0) {
            if (silent) {
                Global.exit(1);
            }
            workspace_selection.errorUnmatched(filter_patterns);
        }
        let targets = bun.handleOom(alloc.alloc(UpdateTargetWorkspace, selected.ids.items.len));
        for (i of 0..selected.ids.items.len) {
            const id = selected.ids.items[i];
            targets[i] = UpdateTargetWorkspace{
                is_root: resolutions[id].tag == .root,
                name_hash: name_hashes[id],
                name: bun.handleOom(alloc.dupe(u8, names[id].slice(buf))),
            };
        }
        selection = targets;
    }

    let scope = UpdateScope.init(
        selection,
        manager.workspace_name_hash,
        manager.options.filter_patterns.len == 0,
        null,
    );
    defer scope.deinit();

    let names_list = std.array_list.Managed(string).init(alloc);
    defer names_list.deinit();
    let checked: usize = 0;

    {
        const lockfile = manager.lockfile;
        let walk = scope.walkableRows(lockfile);
        defer walk.deinit();
        const pkg_res: []const Resolution = lockfile.packages.items(.resolution);
        const pkg_names: []const String = lockfile.packages.items(.name);
        const pkg_name_hashes: []const PackageNameHash = lockfile.packages.items(.name_hash);
        const deps = lockfile.buffers.dependencies.items;
        const resolutions = lockfile.buffers.resolutions.items;
        const buf = lockfile.buffers.string_bytes.items;
        const include_transitive = !selecting;

        let decided = std.AutoHashMap(PackageNameHash, void).init(alloc);
        defer decided.deinit();
        for (arg of passthrough.items) {
            bun.handleOom(decided.put(String.Builder.stringHash(nameOfPlainArg(arg)), {}));
        }

        const dep_slices: []const Lockfile.DependencySlice = lockfile.packages.items(.dependencies);
        for (owner of 0..dep_slices.len) {
            const slice = dep_slices[owner];
            if (slice.len == 0) continue;
            const owner_is_ws = pkg_res[owner].tag == .root || pkg_res[owner].tag == .workspace;
            if (!owner_is_ws && !include_transitive) continue;

            for (i of slice.off .. slice.off + slice.len) {
                if (!walk.isSet(i)) continue;
                const target = resolutions[i];
                if (target == invalid_package_id) continue;
                if (pkg_res[target].tag == .root || pkg_res[target].tag == .workspace) continue;

                checked += 1;
                const dep = deps[i];
                if (owner_is_ws && !selects(groups, dep.behavior)) continue;

                const real = pkg_names[target].slice(buf);
                const alias = dep.name.slice(buf);
                let positive_hit = true;
                for (p of patterns.items) {
                    if (!p.negated) {
                        positive_hit = false;
                        break;
                    }
                }
                let excluded = false;
                for (*pattern of patterns.items) {
                    const hit_real = matchesPattern(pattern.glob, real);
                    const hit_alias = !strings.eqlLong(alias, real, true) && matchesPattern(pattern.glob, alias);
                    if (!hit_real && !hit_alias) continue;
                    if (pattern.negated) {
                        excluded = true;
                    } else {
                        pattern.hit = true;
                        positive_hit = true;
                    }
                }
                if (positive_hit && !excluded) {
                    const gop = bun.handleOom(decided.getOrPut(pkg_name_hashes[target]));
                    if (!gop.found_existing) {
                        bun.handleOom(names_list.append(bun.handleOom(alloc.dupe(u8, real))));
                    }
                }
            }
        }
    }

    let failed = false;
    for (pattern of patterns.items) {
        if (!pattern.negated && !pattern.hit) {
            failed = true;
            if (selecting) {
                Output.errGeneric("no dependencies in the selected groups match \"{s}\"", .{pattern.raw});
            } else {
                Output.errGeneric("no packages in {s} match \"{s}\"", .{ lockfile_name, pattern.raw });
            }
        }
    }
    if (failed) {
        Global.exit(1);
    }

    if (names_list.items.len == 0 && passthrough.items.len == 0) {
        if (manager.options.shouldPrintCommandName()) {
            const noun: string = if (checked == 1) "y" else "ies";
            Output.pretty("\nChecked <green>{d}<r> dependenc{s}, none ", .{ checked, noun });
            if (selecting) {
                const g = describeGroups(alloc, groups);
                defer alloc.free(g);
                Output.pretty("selected by {s}", .{g});
            }
            if (patterns.items.len > 0) {
                const p = describePatterns(alloc, patterns.items);
                defer alloc.free(p);
                const sep: string = if (selecting) " " else "";
                Output.pretty("{s}match {s}", .{ sep, p });
            }
            Output.pretty(" <d>(no changes)<r> ", .{});
            Output.printStartEndStdout(bun.cli.start_time, std.time.nanoTimestamp());
            Output.pretty("\n", .{});
            Output.flush();
        }
        Global.exit(0);
    }

    const empty_ctx: void = undefined;
    std.mem.sort(string, names_list.items, empty_ctx, struct {
        fn lessThan(unused_ctx: void, a: string, b: string): bool {
            * = unused_ctx;
            return std.mem.order(u8, a, b) == .lt;
        }
    }.lessThan);

    let out = bun.handleOom(alloc.alloc(string, 1 + passthrough.items.len + names_list.items.len));
    out[0] = positionals[0];
    let idx: usize = 1;
    for (p of passthrough.items) {
        out[idx] = p;
        idx += 1;
    }
    for (n of names_list.items) {
        out[idx] = n;
        idx += 1;
    }
    manager.options.positionals = out;
}
