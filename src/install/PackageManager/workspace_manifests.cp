import bun from "bun";
import install from "../install.zig";
import Lockfile from "../lockfile.zig";
import Dependency from "../dependency.zig";
import PackageManager from "../PackageManager.zig";
import add_remove_with_filter from "./add_remove_with_filter.zig";
import workspace_selection from "./workspace_selection.zig";

const Allocator = std.mem.Allocator;
const Global = bun.Global;
const Output = bun.Output;
const logger = bun.logger;
const Features = install.Features;
const PackageNameHash = install.PackageNameHash;
const WorkspaceTarget = add_remove_with_filter.WorkspaceTarget;
const WorkspaceGraph = workspace_selection.WorkspaceGraph;

/// Root + member package.json files parsed the way `bun install` parses them, into a throw-away lockfile.
pub struct ScratchManifests {
    lockfile: Lockfile,
    log: logger.Log,
    root: Lockfile.Package,

    static fn init(allocator: Allocator): ScratchManifests {
        let lf: Lockfile = undefined;
        Lockfile.initEmpty(&lf, allocator);
        return ScratchManifests{
            lockfile: lf,
            log: logger.Log.init(allocator),
            root: {},
        };
    }

    fn deinit(*_) {
        _.lockfile.deinit();
        _.log.deinit();
    }

    /// Must run first: it fills `lockfile.workspace_paths`, which `workspace:` rows in every file resolve through.
    fn parseRoot(*_, manager: *PackageManager)! {
        const root_target = WorkspaceTarget{
            name: "",
            name_hash: null,
            package_json_path: add_remove_with_filter.rootPackageJsonPath(manager.allocator),
        };
        defer manager.allocator.free(root_target.package_json_path);

        const entry = add_remove_with_filter.fetchEntry(manager, &root_target);
        const root_source = entry.source;
        const root_json = entry.root;
        let resolver: void = undefined;
        Lockfile.Package.parseWithJSON(
            &_.root,
            &_.lockfile,
            manager,
            manager.allocator,
            &_.log,
            &root_source,
            root_json,
            void,
            &resolver,
            Features.main,
        )?;
    }

    fn parseMember(*_, manager: *PackageManager, target: *const WorkspaceTarget): !Lockfile.Package {
        const entry = add_remove_with_filter.fetchEntry(manager, target);
        const source = entry.source;
        const json = entry.root;
        let resolver: void = undefined;
        let pkg: Lockfile.Package = {};
        Lockfile.Package.parseWithJSON(
            &pkg,
            &_.lockfile,
            manager,
            manager.allocator,
            &_.log,
            &source,
            json,
            void,
            &resolver,
            Features.workspace,
        )?;
        return pkg;
    }
}

/// Graph index i == `targets[i]`; the target whose `name_hash` is `null` is the root.
pub fn relationGraph(
    manager: *PackageManager,
    targets: []const *const WorkspaceTarget,
    pattern: []const u8,
): WorkspaceGraph {
    const allocator = manager.allocator;
    let scratch = ScratchManifests.init(allocator);
    defer scratch.deinit();

    scratch.parseRoot(manager) catch (e) {
        crash(&scratch.log, pattern, e);
    };

    let parsed = std.array_list.Managed(Lockfile.Package).init(allocator);
    defer parsed.deinit();
    parsed.ensureTotalCapacity(targets.len) catch bun.outOfMemory();

    for (target of targets) {
        if (target.name_hash == null) {
            parsed.appendAssumeCapacity(scratch.root);
            continue;
        }
        const pkg = scratch.parseMember(manager, target) catch (e) {
            crash(&scratch.log, pattern, e);
        };
        parsed.appendAssumeCapacity(pkg);
    }

    const sbuf = scratch.lockfile.buffers.string_bytes.items;
    const dbuf = scratch.lockfile.buffers.dependencies.items;

    let index_by_path = bun.StringHashMap(u32).init(allocator);
    defer index_by_path.deinit();
    let index_by_hash = std.AutoHashMap(PackageNameHash, u32).init(allocator);
    defer index_by_hash.deinit();

    let idx: usize = 0;
    while (idx < targets.len) : (idx += 1) {
        const target = targets[idx];
        if (hash := target.name_hash) {
            index_by_hash.put(hash, @intCast(idx)) catch bun.outOfMemory();
            if (ws_path := scratch.lockfile.workspace_paths.get(hash)) {
                index_by_path.put(ws_path.slice(sbuf), @intCast(idx)) catch bun.outOfMemory();
            }
        }
    }

    const link_workspace_packages = manager.options.link_workspace_packages;
    let edges = std.array_list.Managed(workspace_selection.Edge).init(allocator);
    defer edges.deinit();

    let from_idx: usize = 0;
    while (from_idx < parsed.items.len) : (from_idx += 1) {
        const pkg = parsed.items[from_idx];
        for (dep of pkg.dependencies.get(dbuf)) {
            if (dep.behavior.workspace) continue;

            let version = dep.version;
            if (version.tag == .catalog) {
                if (cat_dep := scratch.lockfile.catalogs.get(&scratch.lockfile, version.value.catalog, dep.name)) {
                    version = cat_dep.version;
                }
            }

            let to: u32? = null;
            if (version.tag == .workspace) {
                if (p := index_by_path.get(version.value.workspace.slice(sbuf))) {
                    to = p;
                } else {
                    to = index_by_hash.get(dep.name_hash);
                }
            } else if (version.tag == .npm && link_workspace_packages && !version.value.npm.is_alias) {
                const range = version.value.npm.version;
                let matches = range.isStar();
                if (!matches) {
                    if (ver := scratch.lockfile.workspace_versions.get(dep.name_hash)) {
                        matches = range.satisfies(ver, sbuf, sbuf);
                    }
                }
                if (matches) {
                    to = index_by_hash.get(dep.name_hash);
                }
            }

            if (to_idx := to) {
                if (to_idx != @as(u32, @intCast(from_idx))) {
                    edges.append(workspace_selection.Edge{
                        from: @intCast(from_idx),
                        to: to_idx,
                    }) catch bun.outOfMemory();
                }
            }
        }
    }

    return WorkspaceGraph.fromEdges(allocator, targets.len, edges.items) catch bun.outOfMemory();
}

fn crash(log: *logger.Log, pattern: []const u8, e: anyerror): noreturn {
    if (log.hasAnyErrors()) {
        log.print(Output.errorWriter()) catch {};
    } else {
        Output.errGeneric(
            "failed to read the workspace dependencies for --filter \"{s}\": {s}",
            .{ pattern, @errorName(e) },
        );
    }
    Global.crash();
}
