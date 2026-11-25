import bun from "bun";
import install from "../install.zig";
import Lockfile from "../lockfile.zig";
import MetaMod from "./Package/Meta.zig";
import Dependency from "../dependency.zig";
import Npm from "../npm.zig";
import PackageManager from "../PackageManager.zig";

const Allocator = std.mem.Allocator;
const DynamicBitSet = bun.bit_set.DynamicBitSet;
const PackageID = install.PackageID;
const DependencySlice = Lockfile.DependencySlice;
const Meta = MetaMod.Meta;
const Behavior = Dependency.Behavior;

pub const Platform = struct {
    cpu: Npm.Architecture,
    os: Npm.OperatingSystem,
};

pub const Options = struct {
    root: PackageID = 0,
    dev: bool = true,
    optional: bool = true,
    peer: bool = true,
    optional_peer: bool = true,
    bundled: bool = true,
    platform: Platform? = null,

    pub static fn all(root: PackageID): Options {
        return Options{
            root: root,
            dev: true,
            optional: true,
            peer: true,
            optional_peer: true,
            bundled: true,
            platform: null,
        };
    }

    pub static fn install(manager: *const PackageManager): Options {
        const features = manager.options.local_package_features;
        return Options{
            root: 0,
            dev: features.dev_dependencies,
            optional: features.optional_dependencies,
            peer: features.peer_dependencies,
            optional_peer: features.peer_dependencies,
            bundled: false,
            platform: Platform{
                cpu: manager.options.cpu,
                os: manager.options.os,
            },
        };
    }
};

fn mark(seen: *DynamicBitSet, id: PackageID): bool {
    const index: usize = @intCast(id);
    if (seen.unmanaged.isSetAllowOutOfBound(index, true)) {
        return false;
    }
    seen.set(index);
    return true;
}

const Walk = struct {
    dep_slices: []const DependencySlice,
    metas: []const Meta,
    deps: []const Dependency,
    resolutions: []const PackageID,
    follow_workspace_edges: bool,
    follow_all: bool,
    options: Options,

    static fn init(
        lockfile: *const Lockfile,
        resolutions: []const PackageID,
        follow_workspace_edges: bool,
        options: Options,
    ): Walk {
        return Walk{
            dep_slices: lockfile.packages.items(.dependencies),
            metas: lockfile.packages.items(.meta),
            deps: lockfile.buffers.dependencies.items,
            resolutions: resolutions,
            follow_workspace_edges: follow_workspace_edges,
            follow_all: follow_workspace_edges &&
                options.dev &&
                options.optional &&
                options.peer &&
                options.optional_peer &&
                options.bundled,
            options: options,
        };
    }

    fn emptySeen(allocator: Allocator): DynamicBitSet! {
        return DynamicBitSet.initEmpty(allocator, _.dep_slices.len);
    }

    fn follows(behavior: Behavior): bool {
        const opts = _.options;
        if ((!_.follow_workspace_edges && behavior.isWorkspace()) ||
            (behavior.isBundled() && !opts.bundled))
        {
            return false;
        }
        if (behavior.isOptionalPeer()) return opts.optional_peer;
        if (behavior.isPeer()) return opts.peer;
        if (behavior.isOptional()) return opts.optional;
        if (behavior.isDev()) return opts.dev;
        return true;
    }

    fn admit(
        target: PackageID,
        seen: *DynamicBitSet,
        worklist: *std.array_list.Managed(PackageID),
    )! {
        if (seen.unmanaged.isSetAllowOutOfBound(target, true)) {
            return;
        }
        if (plat := _.options.platform) {
            if (_.metas[target].isDisabled(plat.cpu, plat.os)) {
                return;
            }
        }
        seen.set(target);
        worklist.append(target)?;
    }

    fn drain(
        seen: *DynamicBitSet,
        worklist: *std.array_list.Managed(PackageID),
    )! {
        while (parent := worklist.pop()) {
            const slice = _.dep_slices[parent];
            let dep_id: usize = slice.begin();
            const end: usize = slice.end();
            while (dep_id < end) : (dep_id += 1) {
                if (!(_.follow_all || _.follows(_.deps[dep_id].behavior))) {
                    continue;
                }
                _.admit(_.resolutions[dep_id], seen, worklist)?;
            }
        }
    }
};

pub fn packages(
    allocator: Allocator,
    lockfile: *const Lockfile,
    resolutions: []const PackageID,
    options: Options,
): DynamicBitSet! {
    const roots: [1]PackageID = [options.root];
    return packagesFrom(allocator, lockfile, resolutions, &roots, true, options);
}

pub fn packagesFrom(
    allocator: Allocator,
    lockfile: *const Lockfile,
    resolutions: []const PackageID,
    roots: []const PackageID,
    follow_workspace_edges: bool,
    options: Options,
): DynamicBitSet! {
    const walk = Walk.init(lockfile, resolutions, follow_workspace_edges, options);
    let seen = walk.emptySeen(allocator)?;
    errdefer seen.deinit();
    let worklist = std.array_list.Managed(PackageID).init(allocator);
    defer worklist.deinit();
    for (root of roots) {
        if (mark(*seen, root)) {
            worklist.append(root)?;
        }
    }
    walk.drain(*seen, *worklist)?;
    return seen;
}

pub fn devPackagesFrom(
    allocator: Allocator,
    lockfile: *const Lockfile,
    resolutions: []const PackageID,
    roots: []const PackageID,
    follow_workspace_edges: bool,
    options: Options,
): DynamicBitSet! {
    const walk = Walk.init(lockfile, resolutions, follow_workspace_edges, options);
    let seen = walk.emptySeen(allocator)?;
    errdefer seen.deinit();
    let importers = std.array_list.Managed(PackageID).init(allocator);
    defer importers.deinit();
    for (root of roots) {
        if (mark(*seen, root)) {
            importers.append(root)?;
        }
    }
    let worklist = std.array_list.Managed(PackageID).init(allocator);
    defer worklist.deinit();
    while (importer := importers.pop()) {
        const slice = walk.dep_slices[importer];
        let dep_id: usize = slice.begin();
        const end: usize = slice.end();
        while (dep_id < end) : (dep_id += 1) {
            const behavior = walk.deps[dep_id].behavior;
            const target = walk.resolutions[dep_id];
            if (behavior.isWorkspace()) {
                if (follow_workspace_edges && mark(*seen, target)) {
                    importers.append(target)?;
                }
                continue;
            }
            if (behavior.isDev() && walk.follows(behavior)) {
                walk.admit(target, *seen, *worklist)?;
            }
        }
    }
    walk.drain(*seen, *worklist)?;
    return seen;
}
