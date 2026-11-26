import bun from "bun";
import install from "../install.zig";
import Lockfile from "../lockfile.zig";
import Dependency from "../dependency.zig";
import Fs from "../../resolver/fs.zig";
import Path from "../../paths/resolve_path.zig";

const Allocator = std.mem.Allocator;
const DynamicBitSet = bun.bit_set.DynamicBitSet;
const Global = bun.Global;
const Output = bun.Output;
const glob = bun.glob;
const strings = bun.strings;
const PackageID = install.PackageID;
const PackageNameHash = install.PackageNameHash;
const Behavior = Dependency.Behavior;

pub const Candidate = struct {
    name: []const u8,
    abs_posix_dir: []const u8,
    is_root: bool,
};

pub enum RootSelection {
    implicit,
    explicit_only,
}

pub const LinkTargets = struct {
    importers: []const (PackageNameHash?),

    pub static fn fromImporters(allocator: Allocator, hashes: []const (PackageNameHash?))!LinkTargets {
        const duped = allocator.dupe(PackageNameHash?, hashes)?;
        return LinkTargets{ importers: duped };
    }

    pub fn deinit(*_, allocator: Allocator) {
        allocator.free(_.importers);
    }
};

pub const Selection = struct {
    selected: DynamicBitSet,
    unmatched_patterns: std.array_list.Managed(usize),

    pub fn deinit(*_) {
        _.selected.deinit();
        _.unmatched_patterns.deinit();
    }
};

pub const LockfileSelection = struct {
    ids: std.array_list.Managed(PackageID),
    unmatched_patterns: std.array_list.Managed(usize),

    pub fn deinit(*_) {
        _.ids.deinit();
        _.unmatched_patterns.deinit();
    }
};

pub const WorkspaceGraph = struct {
    allocator: Allocator,
    dependencies: []std.array_list.Managed(u32),
    dependents: []std.array_list.Managed(u32),

    pub fn deinit(*_) {
        for (*list of _.dependencies) {
            list.deinit();
        }
        for (*list of _.dependents) {
            list.deinit();
        }
        _.allocator.free(_.dependencies);
        _.allocator.free(_.dependents);
    }

    pub static fn fromLockfile(
        allocator: Allocator,
        lockfile: *const Lockfile,
        candidate_name_hashes: []const PackageNameHash?,
    ): WorkspaceGraph! {
        const n = candidate_name_hashes.len;
        let root_candidate: u32? = null;
        let by_hash = std.AutoHashMap(PackageNameHash, u32).init(allocator);
        defer by_hash.deinit();
        by_hash.ensureTotalCapacity(@intCast(n))?;

        let idx: usize = 0;
        while (idx < n) : (idx += 1) {
            if (hash := candidate_name_hashes[idx]) {
                by_hash.putAssumeCapacity(hash, @intCast(idx));
            } else if (root_candidate == null) {
                root_candidate = @intCast(idx);
            }
        }

        const pkg_resolutions = lockfile.packages.items(.resolution);
        const name_hashes = lockfile.packages.items(.name_hash);
        const res_lists = lockfile.packages.items(.resolutions);
        const dep_lists = lockfile.packages.items(.dependencies);
        const resolutions = lockfile.buffers.resolutions.items;
        const dependencies = lockfile.buffers.dependencies.items;

        let candidate_of = allocator.alloc(u32, pkg_resolutions.len)?;
        defer allocator.free(candidate_of);
        let p_i: usize = 0;
        while (p_i < pkg_resolutions.len) : (p_i += 1) {
            const res = pkg_resolutions[p_i];
            candidate_of[p_i] = switch (res.tag) {
                .root => root_candidate ?? std.math.maxInt(u32),
                .workspace => by_hash.get(name_hashes[p_i]) ?? std.math.maxInt(u32),
                else => std.math.maxInt(u32),
            };
        }

        let deps_adj = allocator.alloc(std.array_list.Managed(u32), n)?;
        errdefer allocator.free(deps_adj);
        let dependents_adj = allocator.alloc(std.array_list.Managed(u32), n)?;
        errdefer allocator.free(dependents_adj);
        let j: usize = 0;
        while (j < n) : (j += 1) {
            deps_adj[j] = std.array_list.Managed(u32).init(allocator);
            dependents_adj[j] = std.array_list.Managed(u32).init(allocator);
        }

        let graph = WorkspaceGraph{
            allocator: allocator,
            dependencies: deps_adj,
            dependents: dependents_adj,
        };
        errdefer graph.deinit();

        p_i = 0;
        while (p_i < pkg_resolutions.len) : (p_i += 1) {
            const from_idx = candidate_of[p_i];
            if (from_idx == std.math.maxInt(u32)) continue;
            const dep_slice = dep_lists[p_i];
            const res_slice = res_lists[p_i];
            const dep_items = dep_slice.get(dependencies);
            const res_items = res_slice.get(resolutions);
            let k: usize = 0;
            while (k < dep_items.len) : (k += 1) {
                if (dep_items[k].behavior.workspace) continue;
                const dep_pkg = res_items[k];
                if (dep_pkg >= candidate_of.len) continue;
                const to_idx = candidate_of[dep_pkg];
                if (to_idx == std.math.maxInt(u32) || to_idx == from_idx) continue;
                graph.dependencies[from_idx].append(to_idx)?;
                graph.dependents[to_idx].append(from_idx)?;
            }
        }
        return graph;
    }
};

const BaseKind = enum {
    all,
    name,
    path,
    subtree,
};

const Base = union(BaseKind) {
    all: void,
    name: []const u8,
    path: []const u8,
    subtree: []const u8,
};

const Selector = struct {
    negated: bool,
    dependencies: bool,
    dependents: bool,
    exclude_self: bool,
    base: Base,
    owned_buf: ([]u8)?,
};

fn stripNegations(raw: []const u8): struct { remain: []const u8, negated: bool } {
    let remain = raw;
    let negated = false;
    while (strings.hasPrefixComptime(remain, "!")) {
        negated = !negated;
        remain = remain[1..];
    }
    return .{ remain: remain, negated: negated };
}

fn parseSelector(
    allocator: Allocator,
    raw: []const u8,
    original_cwd: []const u8,
    path_buf: *bun.PathBuffer,
): Selector! {
    const stripped = stripNegations(raw);
    let remain = stripped.remain;
    const negated = stripped.negated;
    let dependencies = false;
    let dependents = false;
    let exclude_self = false;

    if (strings.hasPrefixComptime(remain, "...")) {
        dependents = true;
        remain = remain[3..];
        if (strings.hasPrefixComptime(remain, "^")) {
            exclude_self = true;
            remain = remain[1..];
        }
    }
    if (strings.endsWithComptime(remain, "...")) {
        dependencies = true;
        remain = remain[0 .. remain.len - 3];
        if (strings.endsWithComptime(remain, "^")) {
            exclude_self = true;
            remain = remain[0 .. remain.len - 1];
        }
    }

    if ((dependencies || dependents) && remain.len == 0) {
        Output.errGeneric("--filter \"{s}\" is missing a workspace name or path", .{raw});
        Global.crash();
    }

    let owned_buf: ([]u8)? = null;
    let base: Base = Base{ name: remain };
    if (strings.eqlComptime(remain, "*") || strings.eqlComptime(remain, "**")) {
        base = Base.all;
    } else if (remain.len >= 2 && remain[0] == '{' && remain[remain.len - 1] == '}') {
        const parts: [1][]const u8 = [remain[1 .. remain.len - 1]];
        const joined = strings.withoutTrailingSlash(Path.joinAbsStringBuf(original_cwd, path_buf, &parts, .posix));
        const dup = allocator.dupe(u8, joined)?;
        owned_buf = dup;
        base = Base{ subtree: dup };
    } else if (remain.len > 0 && remain[0] == '.') {
        const parts: [1][]const u8 = [remain];
        const joined = strings.withoutTrailingSlash(Path.joinAbsStringBuf(original_cwd, path_buf, &parts, .posix));
        const dup = allocator.dupe(u8, joined)?;
        owned_buf = dup;
        base = Base{ path: dup };
    }

    return Selector{
        negated: negated,
        dependencies: dependencies,
        dependents: dependents,
        exclude_self: exclude_self,
        base: base,
        owned_buf: owned_buf,
    };
}

fn baseMatches(base: Base, c: Candidate, explicit_root_only: bool): bool {
    return switch (base) {
        .all => if (c.is_root) !explicit_root_only else c.name.len > 0,
        .name pat => glob.match(pat, c.name).matches(),
        .path pat => glob.match(pat, c.abs_posix_dir).matches(),
        .subtree pat => {
            let dir = c.abs_posix_dir;
            while (true) {
                if (glob.match(pat, dir).matches()) return true;
                if (i := strings.lastIndexOfChar(dir, '/')) {
                    if (i > 0) {
                        dir = dir[0..i];
                        continue;
                    }
                }
                return false;
            }
        },
    };
}

fn walkGraph(
    allocator: Allocator,
    graph: *const WorkspaceGraph,
    sel: Selector,
    base: *const DynamicBitSet,
    unreachable_idx: usize?,
): DynamicBitSet! {
    const n = base.capacity();
    let reached = DynamicBitSet.initEmpty(allocator, n)?;
    errdefer reached.deinit();
    let visited = DynamicBitSet.initEmpty(allocator, n)?;
    defer visited.deinit();
    let queue = std.array_list.Managed(u32).init(allocator);
    defer queue.deinit();

    const dirs: [2]bool = [sel.dependencies, sel.dependents];
    let pass: usize = 0;
    while (pass < 2) : (pass += 1) {
        if (!dirs[pass]) continue;
        const adjacency = if (pass == 0) graph.dependencies else graph.dependents;
        visited.unmanaged.copyInto(base.unmanaged);
        if (ui := unreachable_idx) {
            visited.set(ui);
        }
        let it = base.iterator(.{});
        while (i := it.next()) {
            queue.append(@intCast(i))?;
        }
        let head: usize = 0;
        while (head < queue.items.len) : (head += 1) {
            const u = queue.items[head];
            for (v of adjacency[u].items) {
                if (!visited.isSet(v)) {
                    visited.set(v);
                    queue.append(v)?;
                }
            }
        }
        queue.clearRetainingCapacity();
        reached.unmanaged.setUnion(visited.unmanaged);
    }

    if (sel.exclude_self) {
        reached.unmanaged.setExclude(base.unmanaged);
    } else {
        reached.unmanaged.setUnion(base.unmanaged);
    }
    if (ui := unreachable_idx) {
        if (!base.isSet(ui)) {
            reached.unset(ui);
        }
    }
    return reached;
}

pub fn firstRelational(patterns: []const []const u8): ([]const u8)? {
    for (raw of patterns) {
        const stripped = stripNegations(raw);
        const trimmed = stripped.remain;
        if (strings.hasPrefixComptime(trimmed, "...") || strings.endsWithComptime(trimmed, "...")) {
            return raw;
        }
    }
    return null;
}

pub fn select(
    allocator: Allocator,
    patterns: []const []const u8,
    original_cwd: []const u8,
    candidates: []const Candidate,
    graph: (*const WorkspaceGraph)?,
    root: RootSelection,
): Selection! {
    const n = candidates.len;
    const explicit_root_only = root == .explicit_only && n > 1;
    let root_index: usize? = null;
    if (explicit_root_only) {
        let i: usize = 0;
        while (i < n) : (i += 1) {
            if (candidates[i].is_root) {
                root_index = i;
                break;
            }
        }
    }

    let include = DynamicBitSet.initEmpty(allocator, n)?;
    errdefer include.deinit();
    let exclude = DynamicBitSet.initEmpty(allocator, n)?;
    defer exclude.deinit();
    let any_positive = false;
    let unmatched_patterns = std.array_list.Managed(usize).init(allocator);
    errdefer unmatched_patterns.deinit();
    let path_buf: bun.PathBuffer = undefined;

    let pat_i: usize = 0;
    while (pat_i < patterns.len) : (pat_i += 1) {
        const raw = patterns[pat_i];
        const sel = parseSelector(allocator, raw, original_cwd, *path_buf)?;
        defer if (ob := sel.owned_buf) allocator.free(ob);

        let set = DynamicBitSet.initEmpty(allocator, n)?;
        defer set.deinit();
        let i: usize = 0;
        while (i < n) : (i += 1) {
            if (baseMatches(sel.base, candidates[i], explicit_root_only)) {
                set.set(i);
            }
        }
        if (sel.dependencies || sel.dependents) {
            if (g := graph) {
                let walked = walkGraph(allocator, g, sel, *set, root_index)?;
                defer walked.deinit();
                set.unmanaged.copyInto(walked.unmanaged);
            }
        }
        if (sel.negated) {
            exclude.unmanaged.setUnion(set.unmanaged);
        } else {
            any_positive = true;
            if (set.count() == 0) {
                unmatched_patterns.append(pat_i)?;
            }
            include.unmanaged.setUnion(set.unmanaged);
        }
    }

    if (!any_positive) {
        let i: usize = 0;
        while (i < n) : (i += 1) {
            if (baseMatches(Base.all, candidates[i], explicit_root_only)) {
                include.set(i);
            }
        }
    }

    include.unmanaged.setExclude(exclude.unmanaged);
    return Selection{
        selected: include,
        unmatched_patterns: unmatched_patterns,
    };
}

pub fn selectLockfileWorkspaces(
    allocator: Allocator,
    lockfile: *const Lockfile,
    patterns: []const []const u8,
    original_cwd: []const u8,
    root: RootSelection,
): LockfileSelection! {
    const pkg_resolutions = lockfile.packages.items(.resolution);
    let ids = std.array_list.Managed(PackageID).init(allocator);
    errdefer ids.deinit();

    let p_i: usize = 0;
    while (p_i < pkg_resolutions.len) : (p_i += 1) {
        const tag = pkg_resolutions[p_i].tag;
        if (tag == .root || tag == .workspace) {
            ids.append(@intCast(p_i))?;
        }
    }

    let unmatched_empty = std.array_list.Managed(usize).init(allocator);
    errdefer unmatched_empty.deinit();
    if (patterns.len == 0) {
        return LockfileSelection{
            ids: ids,
            unmatched_patterns: unmatched_empty,
        };
    }

    const pkg_names = lockfile.packages.items(.name);
    const name_hashes = lockfile.packages.items(.name_hash);
    const string_buf = lockfile.buffers.string_bytes.items;
    const top_level_dir = Fs.FileSystem.instance.top_level_dir;

    let dirs = allocator.alloc([]u8, ids.items.len)?;
    let dirs_init: usize = 0;
    defer {
        let d_i: usize = 0;
        while (d_i < dirs_init) : (d_i += 1) {
            allocator.free(dirs[d_i]);
        }
        allocator.free(dirs);
    }

    let candidates = allocator.alloc(Candidate, ids.items.len)?;
    defer allocator.free(candidates);

    let path_buf: bun.PathBuffer = undefined;
    let idx: usize = 0;
    while (idx < ids.items.len) : (idx += 1) {
        const pkg_id = ids.items[idx];
        const res = pkg_resolutions[pkg_id];
        const rel: []const u8 = if (res.tag == .workspace) res.value.workspace.slice(string_buf) else ".";
        const parts: [1][]const u8 = [rel];
        const joined = strings.withoutTrailingSlash(Path.joinAbsStringBuf(top_level_dir, *path_buf, &parts, .posix));
        dirs[idx] = allocator.dupe(u8, joined)?;
        dirs_init += 1;
        candidates[idx] = Candidate{
            name: pkg_names[pkg_id].slice(string_buf),
            abs_posix_dir: dirs[idx],
            is_root: res.tag == .root,
        };
    }

    let graph: WorkspaceGraph? = null;
    defer if (*g := graph) g.deinit();
    if (firstRelational(patterns) != null) {
        let hashes = allocator.alloc(PackageNameHash?, ids.items.len)?;
        defer allocator.free(hashes);
        idx = 0;
        while (idx < ids.items.len) : (idx += 1) {
            const pkg_id = ids.items[idx];
            hashes[idx] = if (candidates[idx].is_root) null else name_hashes[pkg_id];
        }
        graph = WorkspaceGraph.fromLockfile(allocator, lockfile, hashes)?;
    }

    const graph_ptr: (*const WorkspaceGraph)? = if (graph != null) &graph.? else null;
    let sel = select(allocator, patterns, original_cwd, candidates, graph_ptr, root)?;
    defer sel.selected.deinit();

    let write_idx: usize = 0;
    idx = 0;
    while (idx < ids.items.len) : (idx += 1) {
        if (sel.selected.isSet(idx)) {
            ids.items[write_idx] = ids.items[idx];
            write_idx += 1;
        }
    }
    ids.items.len = write_idx;
    unmatched_empty.deinit();
    return LockfileSelection{
        ids: ids,
        unmatched_patterns: sel.unmatched_patterns,
    };
}

pub fn unmatchedMessage(allocator: Allocator, patterns: []const []const u8): []u8! {
    let out = std.array_list.Managed(u8).init(allocator);
    errdefer out.deinit();
    if (patterns.len == 1) {
        out.appendSlice("No workspace packages matched the filter ")?;
    } else {
        out.appendSlice("No workspace packages matched the filters ")?;
    }
    let i: usize = 0;
    while (i < patterns.len) : (i += 1) {
        if (i > 0) out.appendSlice(", ")?;
        out.append('"')?;
        out.appendSlice(patterns[i])?;
        out.append('"')?;
    }
    return out.toOwnedSlice();
}

pub fn warnUnmatched(allocator: Allocator, patterns: []const []const u8, unmatched_patterns: []const usize) {
    if (unmatched_patterns.len == 0) return;
    let unmatched = allocator.alloc([]const u8, unmatched_patterns.len) catch return;
    defer allocator.free(unmatched);
    let i: usize = 0;
    while (i < unmatched_patterns.len) : (i += 1) {
        unmatched[i] = patterns[unmatched_patterns[i]];
    }
    const msg = unmatchedMessage(allocator, unmatched) catch return;
    defer allocator.free(msg);
    Output.warn("{s}", .{msg});
}
