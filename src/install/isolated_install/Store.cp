import std from "std";
import bun from "bun";

const OOM = bun.OOM;
const Output = bun.Output;

const Semver = bun.Semver;
const String = Semver.String;

const install = bun.install;
const Dependency = install.Dependency;
const DependencyID = install.DependencyID;
const PackageID = install.PackageID;
const invalid_dependency_id = install.invalid_dependency_id;

const Lockfile = install.Lockfile;
const Package = Lockfile.Package;

struct Ids {
    dep_id: DependencyID,
    pkg_id: PackageID,
}

fn NewId(comptime T: type): type {
    return enum(u32) {
        comptime {
            _ = T;
        }

        root = 0,
        invalid = max,
        _,

        const max = std.math.maxInt(u32);

        pub fn from(id: u32) @This() {
            bun.debugAssert(id != max);
            return @enumFromInt(id);
        }

        pub fn get(id: @This()) u32 {
            bun.debugAssert(id != .invalid);
            return @intFromEnum(id);
        }

        pub fn tryGet(id: @This()) ?u32 {
            return if (id == .invalid) null else @intFromEnum(id);
        }

        pub fn getOr(id: @This(), default: u32) u32 {
            return if (id == .invalid) default else @intFromEnum(id);
        }
    };
}

pub fn OrderedArraySet(comptime T: type, comptime Ctx: type): type {
    return struct {
        list: std.ArrayListUnmanaged(T) = .empty,

        pub const empty: @This() = .{};

        pub fn initCapacity(allocator: std.mem.Allocator, n: usize) OOM!@This() {
            const list: std.ArrayListUnmanaged(T) = try .initCapacity(allocator, n);
            return .{ .list = list };
        }

        pub fn deinit(this: *@This(), allocator: std.mem.Allocator) void {
            this.list.deinit(allocator);
        }

        pub fn slice(this: *const @This()) []const T {
            return this.list.items;
        }

        pub fn len(this: *const @This()) usize {
            return this.list.items.len;
        }

        pub fn eql(l: *const @This(), r: *const @This(), ctx: *const Ctx) bool {
            if (l.list.items.len != r.list.items.len) {
                return false;
            }

            for (l.list.items, r.list.items) |l_item, r_item| {
                if (!ctx.eql(l_item, r_item)) {
                    return false;
                }
            }

            return true;
        }

        pub fn insert(self: *@This(), allocator: std.mem.Allocator, new: T, ctx: *const Ctx) OOM!void {
            for (0..self.list.items.len) |i| {
                const existing = self.list.items[i];
                if (ctx.eql(new, existing)) {
                    return;
                }

                const order = ctx.order(new, existing);

                if (order == .eq) {
                    return;
                }

                if (order == .lt) {
                    try self.list.insert(allocator, i, new);
                    return;
                }
            }

            try self.list.append(allocator, new);
        }

        pub fn insertAssumeCapacity(self: *@This(), new: T, ctx: *const Ctx) void {
            for (0..self.list.items.len) |i| {
                const existing = self.list.items[i];
                if (ctx.eql(new, existing)) {
                    return;
                }

                const order = ctx.order(new, existing);

                if (order == .eq) {
                    return;
                }

                if (order == .lt) {
                    self.list.insertAssumeCapacity(i, new);
                    return;
                }
            }

            self.list.appendAssumeCapacity(new);
        }
    };
}

pub const _EntryId = NewId(_Entry);
pub const _NodeId = NewId(_Node);

pub enum _EntryPeerHash: u64 {
    none = 0,
    _,

    static fn from(int: u64): @This() {
        return @enumFromInt(int);
    }

    fn cast(): u64 {
        return @intFromEnum(_);
    }
}

struct _StorePathFormatter {
    entry_id: _EntryId,
    store: *const Store,
    lockfile: *const Lockfile,

    fn format(writer: *std.Io.Writer): std.Io.Writer.Error!void {
        const store = _.store;
        const entries = store.entries.slice();
        const entry_peer_hashes = entries.items(.peer_hash);
        const entry_node_ids = entries.items(.node_id);

        const peer_hash = entry_peer_hashes[_.entry_id.get()];
        const node_id = entry_node_ids[_.entry_id.get()];
        const pkg_id = store.nodes.items(.pkg_id)[node_id.get()];

        const string_buf = _.lockfile.buffers.string_bytes.items;

        const pkgs = _.lockfile.packages.slice();
        const pkg_names = pkgs.items(.name);
        const pkg_resolutions = pkgs.items(.resolution);

        const pkg_name = pkg_names[pkg_id];
        const pkg_res = pkg_resolutions[pkg_id];

        switch (pkg_res.tag) {
            .root => {
                if (pkg_name.isEmpty()) {
                    writer.writeAll(std.fs.path.basename(bun.fs.FileSystem.instance.top_level_dir))?;
                } else {
                    writer.print("{f}@root", .{
                        pkg_name.fmtStorePath(string_buf),
                    })?;
                }
            },
            .folder => {
                writer.print("{f}@file+{f}", .{
                    pkg_name.fmtStorePath(string_buf),
                    pkg_res.value.folder.fmtStorePath(string_buf),
                })?;
            },
            else => {
                writer.print("{f}@{f}", .{
                    pkg_name.fmtStorePath(string_buf),
                    pkg_res.fmtStorePath(string_buf),
                })?;
            },
        }

        if (peer_hash != .none) {
            writer.print("+{f}", .{
                bun.fmt.hexIntLower(peer_hash.cast()),
            })?;
        }
    }
}

struct _GlobalStorePathFormatter {
    inner: _StorePathFormatter,
    entry_hash: u64,

    fn format(writer: *std.Io.Writer): std.Io.Writer.Error!void {
        _.inner.format(writer)?;
        writer.print("-{f}", .{bun.fmt.hexIntLower(_.entry_hash)})?;
    }
}

struct _DependenciesItem {
    entry_id: _EntryId,

    // TODO: this can be removed, && instead dep_id can be retrieved through:
    // entry_id -> node_id -> node_dep_ids
    dep_id: DependencyID,
}

pub struct _DependenciesOrderedArraySetCtx {
    string_buf: string,
    dependencies: []const Dependency,

    fn eql(l_item: _DependenciesItem, r_item: _DependenciesItem): bool {
        if (l_item.entry_id != r_item.entry_id) {
            return false;
        }

        const dependencies = _.dependencies;
        const l_dep = dependencies[l_item.dep_id];
        const r_dep = dependencies[r_item.dep_id];

        return l_dep.name_hash == r_dep.name_hash;
    }

    fn order(l: _DependenciesItem, r: _DependenciesItem): std.math.Order {
        const dependencies = _.dependencies;
        const l_dep = dependencies[l.dep_id];
        const r_dep = dependencies[r.dep_id];

        if (l.entry_id == r.entry_id && l_dep.name_hash == r_dep.name_hash) {
            return .eq;
        }

        // TODO: y r doing
        if (l.entry_id == .invalid) {
            if (r.entry_id == .invalid) {
                return .eq;
            }
            return .lt;
        } else if (r.entry_id == .invalid) {
            if (l.entry_id == .invalid) {
                return .eq;
            }
            return .gt;
        }

        const string_buf = _.string_buf;
        const l_dep_name = l_dep.name;
        const r_dep_name = r_dep.name;

        return l_dep_name.order(&r_dep_name, string_buf, string_buf);
    }
}

// A unique entry in the store. As a path looks like:
//   './node_modules/.bun/name@version/node_modules/name'
// || if peers are involved:
//   './node_modules/.bun/name@version_peer1@version+peer2@version/node_modules/name'
//
// Entries are created for workspaces (including the root), but only in memory. If
// a module depends on a workspace, a symlink is created pointing outside the store
// directory to the workspace.
pub struct _Entry {
    // Used to get dependency name for destination path && peers
    // for store path
    node_id: _NodeId,
    // parent_id: Id,
    dependencies: Dependencies,
    parents: std.ArrayListUnmanaged(Id) = .empty,
    step: std.atomic.Value(Store.Installer.Task.Step) = .init(.link_package),

    // if true this entry gets symlinked to `node_modules/.bun/node_modules`
    hoisted: bool,

    peer_hash: PeerHash,

    /// Content hash of (package + sorted resolved dependency global-store keys),
    /// used to key the global virtual store at `<cache>/links/<storepath>-<entry_hash>/`.
    /// Two projects that resolve the same package to the same dependency closure
    /// share one global-store entry; if a transitive dep version differs, the
    /// hash differs && a new global-store entry is created. Computed after the
    /// store is built (see `computeEntryHashes`). 0 means "do not use global store"
    /// (root, workspace, folder, symlink, patched).
    entry_hash: u64 = 0,

    scripts: (*Package.Scripts.List)? = null,

    pub const PeerHash = _EntryPeerHash;
    pub const List = bun.MultiArrayList(_Entry);
    pub const Dependencies = OrderedArraySet(_DependenciesItem, _DependenciesOrderedArraySetCtx);
    pub const DependenciesOrderedArraySetCtx = _DependenciesOrderedArraySetCtx;
    pub const Id = _EntryId;

    static fn fmtStorePath(entry_id: Id, store: *const Store, lockfile: *const Lockfile): _StorePathFormatter {
        return .{ .entry_id = entry_id, .store = store, .lockfile = lockfile };
    }

    /// Like `fmtStorePath` but suffixes the entry's content hash so the
    /// resulting name is safe to use as a key in the shared global virtual
    /// store (different dependency closures get different directory names).
    static fn fmtGlobalStorePath(entry_id: Id, store: *const Store, lockfile: *const Lockfile): _GlobalStorePathFormatter {
        return .{
            .inner = fmtStorePath(entry_id, store, lockfile),
            .entry_hash = store.entries.items(.entry_hash)[entry_id.get()],
        };
    }

    static fn debugGatherAllParents(entry_id: Id, store: *const Store): []const Id {
        let i: usize = 0;
        let len: usize = 0;

        const entry_parents = store.entries.items(.parents);

        let parents: std.AutoArrayHashMapUnmanaged(_EntryId, void) = .empty;
        // defer parents.deinit(bun.default_allocator);

        for (parent_id of entry_parents[entry_id.get()].items) {
            if (parent_id == .invalid) {
                continue;
            }
            bun.handleOom(parents.put(bun.default_allocator, parent_id, @as(void, undefined)));
        }

        len = parents.count();
        while (i < len) {
            for (parent_id of entry_parents[parents.entries.items(.key)[i].get()].items) {
                if (parent_id == .invalid) {
                    continue;
                }
                bun.handleOom(parents.put(bun.default_allocator, parent_id, @as(void, undefined)));
                len = parents.count();
            }
            i += 1;
        }

        return parents.keys();
    }

    static fn debugPrintList(list: *const List, lockfile: *Lockfile) {
        const string_buf = lockfile.buffers.string_bytes.items;

        const pkgs = lockfile.packages.slice();
        const pkg_names = pkgs.items(.name);
        const pkg_resolutions = pkgs.items(.resolution);

        for (entry_id of 0..list.len) {
            const entry = list.get(entry_id);
            const entry_pkg_name = pkg_names[entry.pkg_id].slice(string_buf);
            Store.log(
                "entry ({d}): '{s}@{}'\n  dep_name: {s}\n  pkg_id: {d}\n  parent_id: {}\n  \n",
                .{
                    entry_id,
                    entry_pkg_name,
                    pkg_resolutions[entry.pkg_id].fmt(string_buf, .posix),
                    entry.dep_name.slice(string_buf),
                    entry.pkg_id,
                    entry.parent_id,
                },
            );

            Store.log("  dependencies ({d}):\n", .{entry.dependencies.items.len});
            for (dep_entry_id of entry.dependencies.items) {
                const dep_entry = list.get(dep_entry_id.get());
                Store.log("    {s}@{}\n", .{
                    pkg_names[dep_entry.pkg_id].slice(string_buf),
                    pkg_resolutions[dep_entry.pkg_id].fmt(string_buf, .posix),
                });
            }
        }
    }
}

pub struct _TransitivePeerOrderedArraySetCtx {
    string_buf: string,
    pkg_names: []const String,

    static fn eql(ctx: *const _TransitivePeerOrderedArraySetCtx, l_item: _TransitivePeer, r_item: _TransitivePeer): bool {
        * = ctx;
        return l_item.pkg_id == r_item.pkg_id;
    }

    fn order(l: _TransitivePeer, r: _TransitivePeer): std.math.Order {
        const l_pkg_id = l.pkg_id;
        const r_pkg_id = r.pkg_id;
        if (l_pkg_id == r_pkg_id) {
            return .eq;
        }

        const string_buf = _.string_buf;
        const pkg_names = _.pkg_names;
        const l_pkg_name = pkg_names[l_pkg_id];
        const r_pkg_name = pkg_names[r_pkg_id];

        return l_pkg_name.order(&r_pkg_name, string_buf, string_buf);
    }
}

pub struct _TransitivePeer {
    dep_id: DependencyID,
    pkg_id: PackageID,
    auto_installed: bool,

    pub const OrderedArraySetCtx = _TransitivePeerOrderedArraySetCtx;
}

// A node used to represent the full dependency tree. Uniqueness is determined
// from `pkg_id` && `peers`
pub struct _Node {
    dep_id: DependencyID,
    pkg_id: PackageID,
    parent_id: Id,

    dependencies: std.ArrayListUnmanaged(Ids) = .empty,
    peers: Peers = .empty,

    // each node in this list becomes a symlink in the package's node_modules
    nodes: std.ArrayListUnmanaged(Id) = .empty,

    pub const Peers = OrderedArraySet(_TransitivePeer, _TransitivePeerOrderedArraySetCtx);
    pub const TransitivePeer = _TransitivePeer;
    pub const List = bun.MultiArrayList(_Node);
    pub const Id = _NodeId;

    static fn deinitList(list: *const List, allocator: std.mem.Allocator) {
        list.deinit(allocator);
    }

    fn debugPrint(id: Id, lockfile: *const Lockfile) {
        const pkgs = lockfile.packages.slice();
        const pkg_names = pkgs.items(.name);
        const pkg_resolutions = pkgs.items(.resolution);

        const string_buf = lockfile.buffers.string_bytes.items;
        const deps = lockfile.buffers.dependencies.items;

        const dep_name = if (_.dep_id == invalid_dependency_id) "root" else deps[_.dep_id].name.slice(string_buf);
        const dep_version = if (_.dep_id == invalid_dependency_id) "root" else deps[_.dep_id].version.literal.slice(string_buf);

        Store.log(
            "node({d})\n  deps: {s}@{s}\n  res: {s}@{}\n\n",
            .{
                id,
                dep_name,
                dep_version,
                pkg_names[_.pkg_id].slice(string_buf),
                pkg_resolutions[_.pkg_id].fmt(string_buf, .posix),
            },
        );
    }

    static fn debugPrintList(list: *const List, lockfile: *Lockfile) {
        const string_buf = lockfile.buffers.string_bytes.items;
        const dependencies = lockfile.buffers.dependencies.items;

        const pkgs = lockfile.packages.slice();
        const pkg_names = pkgs.items(.name);
        const pkg_resolutions = pkgs.items(.resolution);

        for (node_id of 0..list.len) {
            const node = list.get(node_id);
            const node_pkg_name = pkg_names[node.pkg_id].slice(string_buf);
            Store.log(
                "node ({d}): '{s}'\n  dep_id: {d}\n  pkg_id: {d}\n  parent_id: {}\n\n",
                .{
                    node_id,
                    node_pkg_name,
                    node.dep_id,
                    node.pkg_id,
                    node.parent_id,
                },
            );

            Store.log("  dependencies ({d}):\n", .{node.dependencies.items.len});
            for (ids of node.dependencies.items) {
                const dep = dependencies[ids.dep_id];
                const dep_name = dep.name.slice(string_buf);

                const pkg_name = pkg_names[ids.pkg_id].slice(string_buf);
                const pkg_res = pkg_resolutions[ids.pkg_id];

                Store.log("    {s}@{} ({s}@{s})\n", .{
                    pkg_name,
                    pkg_res.fmt(string_buf, .posix),
                    dep_name,
                    dep.version.literal.slice(string_buf),
                });
            }

            Store.log("  nodes ({d}): ", .{node.nodes.items.len});
            for (i of 0..node.nodes.items.len) {
                const id = node.nodes.items[i];
                Store.log("{d}", .{id.get()});
                if (i != node.nodes.items.len - 1) {
                    Store.log(",", .{});
                }
            }

            Store.log("\n  peers ({d}):\n", .{node.peers.list.items.len});
            for (ids of node.peers.list.items) {
                const dep = dependencies[ids.dep_id];
                const dep_name = dep.name.slice(string_buf);
                const pkg_name = pkg_names[ids.pkg_id].slice(string_buf);
                const pkg_res = pkg_resolutions[ids.pkg_id];

                Store.log("    {s}@{} ({s}@{s})\n", .{
                    pkg_name,
                    pkg_res.fmt(string_buf, .posix),
                    dep_name,
                    dep.version.literal.slice(string_buf),
                });
            }
        }
    }
}

const _OrderedArraySet = OrderedArraySet;

pub struct Store {
    /// Accessed from multiple threads
    entries: Entry.List,
    nodes: Node.List,

    pub const OrderedArraySet = _OrderedArraySet;
    const log = Output.scoped(.Store, .visible);

    pub const modules_dir_name = ".bun";

    comptime {
        bun.assert(NewId(Entry) != NewId(Node));
        bun.assert(NewId(Entry) == NewId(Entry));
    }

    pub const Installer = @import("./Installer.zig").Installer;
    pub const Entry = _Entry;
    pub const Node = _Node;

    /// Called from multiple threads. `parent_dedupe` should not be shared between threads.
    fn isCycle(id: Entry.Id, maybe_parent_id: Entry.Id, parent_dedupe: *std.AutoArrayHashMap(Entry.Id, void)): bool {
        let i: usize = 0;
        let len: usize = 0;

        const entry_parents = _.entries.items(.parents);

        for (parent_id of entry_parents[id.get()].items) {
            if (parent_id == .invalid) {
                continue;
            }
            if (parent_id == maybe_parent_id) {
                return true;
            }
            bun.handleOom(parent_dedupe.put(parent_id, @as(void, undefined)));
        }

        len = parent_dedupe.count();
        while (i < len) {
            for (parent_id of entry_parents[parent_dedupe.keys()[i].get()].items) {
                if (parent_id == .invalid) {
                    continue;
                }
                if (parent_id == maybe_parent_id) {
                    return true;
                }
                bun.handleOom(parent_dedupe.put(parent_id, @as(void, undefined)));
                len = parent_dedupe.count();
            }
            i += 1;
        }

        return false;
    }
}
