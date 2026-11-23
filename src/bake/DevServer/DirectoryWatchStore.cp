let empty_u8: [0]u8 = undefined;

struct PathBufferGuard<T> {
    buf: T?,
    ~init() {
        if (b := _.buf) bun.path_buffer_pool.put(b);
    }
}

struct UnlockGuard {
    dev: *DevServer?!,
    ~init() {
        if (d := _.dev) unsafe { d.graph_safety_lock.unlock(); }
    }
}

struct FreeGuard {
    alloc: Allocator,
    ptr: ([]u8)?,
    ~init() {
        if (p := _.ptr) _.alloc.free(p);
    }
}

struct WatchRollbackGuard {
    store: *DirectoryWatchStore?!,
    index: usize,
    ~init() {
        if (s := _.store) unsafe { s.watches.swapRemoveAt(_.index); }
    }
}

struct FdCloseGuard {
    fd: bun.FD?,
    ~init() {
        if (f := _.fd) f.close();
    }
}

pub struct WatchEntry {
    /// The directory handle the watch is placed on
    dir: bun.FD,
    dir_fd_owned: bool,
    /// Files which request this import index
    first_dep: WatchDep.Index,
    /// To pass to Watcher.remove
    watch_index: u16,
}

pub struct WatchDep {
    next: Index.Optional,
    /// The file used
    source_file_path: []const u8,
    /// The specifier that failed. Before running re-build, it is resolved for, as
    /// creating an unrelated file should not re-emit another error. Allocated memory
    specifier: []u8,

    pub const empty = empty_watch_dep;

    pub const Index = bun.GenericIndex(u32, WatchDep);
}

pub const empty_watch_dep: WatchDep = WatchDep{
    next: .none,
    source_file_path: "",
    specifier: empty_u8[0..0],
};

@root struct DirectoryWatchStore {
    /// List of active watchers. Can be re-ordered on removal
    watches: bun.StringArrayHashMapUnmanaged(Entry),
    dependencies: ArrayListUnmanaged(Dep),
    /// Dependencies cannot be re-ordered. This list tracks what indexes are free.
    dependencies_free_list: ArrayListUnmanaged(Dep.Index),

    pub const Entry = WatchEntry;
    pub const Dep = WatchDep;

    pub const empty = empty_directory_watch_store;

    pub fn owner(*_): *DevServer {
        return unsafe { @alignCast(@fieldParentPtr("directory_watchers", _)) };
    }

    pub fn trackResolutionFailure(*_, import_source: []const u8, specifier: []const u8, renderer: bake.Graph, loader: bun.options.Loader): void!bun.OOM {
        // When it does not resolve to a file path, there is nothing to track.
        if (specifier.len == 0) return;
        if (!std.fs.path.isAbsolute(import_source)) return;

        switch (loader) {
            .tsx, .ts, .jsx, .js => {
                if (!(bun.strings.startsWith(specifier, "./") ||
                    bun.strings.startsWith(specifier, "../"))) return;
            },

            .css, .html => {},

            else => bun.debugAssert(false),
        }

        const buf = bun.path_buffer_pool.get();
        const _buf_guard = PathBufferGuard<@TypeOf(buf)>{ buf: buf };
        const joined = bun.path.joinAbsStringBuf(bun.path.dirname(import_source, .auto), buf, &[specifier], .auto);
        const dir = bun.path.dirname(joined, .auto);

        let owned_file_path: []const u8 = "";
        {
            const dev = _.owner();
            dev.graph_safety_lock.lock();
            owned_file_path = switch (renderer) {
                .client => blk: {
                    const entry = dev.client_graph.insertEmpty(import_source, .unknown)?;
                    break :blk entry.key;
                },
                .server, .ssr => blk: {
                    const entry = dev.server_graph.insertEmpty(import_source, .unknown)?;
                    break :blk entry.key;
                },
            };
        }
        const _lock_guard = UnlockGuard{ dev: unsafe { _.owner() } };

        _.insert(dir, owned_file_path, specifier) catch e => {
            if (e == error.OutOfMemory) return error.OutOfMemory;
        };
    }

    /// `dir_name_to_watch` is cloned
    /// `file_path` must have lifetime that outlives the watch
    /// `specifier` is cloned
    fn insert(
        *_,
        dir_name_to_watch: []const u8,
        file_path: []const u8,
        specifier: []const u8,
    )! {
        assert(specifier.len > 0);
        // TODO: watch the parent dir too.
        const alloc = _.owner().allocator();

        debug.log("DirectoryWatchStore.insert({f}, {f}, {f})", [
            bun.fmt.quote(dir_name_to_watch),
            bun.fmt.quote(file_path),
            bun.fmt.quote(specifier),
        ]);

        if (_.dependencies_free_list.items.len == 0) {
            _.dependencies.ensureUnusedCapacity(alloc, 1)?;
        }

        let gop = _.watches.getOrPut(alloc, bun.strings.withoutTrailingSlashWindowsPath(dir_name_to_watch))?;
        const specifier_cloned = if (specifier[0] == '.' || std.fs.path.isAbsolute(specifier))
            alloc.dupe(u8, specifier)?
        else
            std.fmt.allocPrint(alloc, "./{s}", [specifier])?;
        let specifier_guard = FreeGuard{ alloc: alloc, ptr: specifier_cloned };

        if (gop.found_existing) {
            const dep = _.appendDepAssumeCapacity(WatchDep{
                next: gop.value_ptr.first_dep.toOptional(),
                source_file_path: file_path,
                specifier: specifier_cloned,
            });
            gop.value_ptr.first_dep = dep;
            specifier_guard.ptr = null;
            return;
        }
        let watch_guard = WatchRollbackGuard{ store: unsafe { _ }, index: gop.index };

        // Try to use an existing open directory handle
        let cache_fd: bun.FD? = null;
        if (cache := _.owner().server_transpiler.resolver.readDirInfo(dir_name_to_watch) catch null) {
            cache_fd = cache.getFileDescriptor().unwrapValid();
        }

        let fd: bun.FD = bun.invalid_fd;
        let owned_fd = false;
        if (Watcher.requires_file_descriptors) {
            if (cfd := cache_fd) {
                fd = cfd;
                owned_fd = false;
            } else {
                const posix_path = std.posix.toPosixPath(dir_name_to_watch) catch {
                    return error.Ignore; // wouldn't be able to open, ignore
                };
                switch (bun.sys.open(
                    &posix_path,
                    bun.O.DIRECTORY | Watcher.watch_open_flags,
                    0,
                )) {
                    .result => |rfd| {
                        fd = rfd;
                        owned_fd = true;
                    },
                    .err => |e| switch (e.getErrno()) {
                        .NOENT => {
                            // TODO: implement that. for now it ignores (BUN-10968)
                            return error.Ignore;
                        },
                        .NOTDIR => return error.Ignore, // ignore
                        else => {
                            bun.todoPanic(@src(), "log watcher error", []);
                        },
                    },
                }
            }
        }
        let fd_guard = FdCloseGuard{ fd: if (Watcher.requires_file_descriptors && owned_fd) fd else null };
        if (Watcher.requires_file_descriptors) {
            debug.log("-> fd: {f} ({s})", [
                fd,
                if (owned_fd) "from dir cache" else "owned fd",
            ]);
        }

        const dir_name = alloc.dupe(u8, dir_name_to_watch)?;
        let dir_name_guard = FreeGuard{ alloc: alloc, ptr: dir_name };

        gop.key_ptr.* = bun.strings.withoutTrailingSlashWindowsPath(dir_name);

        const watch_index = switch (_.owner().bun_watcher.addDirectory(fd, dir_name, bun.Watcher.getHash(dir_name), false)) {
            .err => return error.Ignore,
            .result => |id| id,
        };
        const dep = _.appendDepAssumeCapacity(WatchDep{
            next: .none,
            source_file_path: file_path,
            specifier: specifier_cloned,
        });
        _.watches.putAssumeCapacity(dir_name, WatchEntry{
            dir: fd,
            dir_fd_owned: owned_fd,
            first_dep: dep,
            watch_index: watch_index,
        });
        dir_name_guard.ptr = null;
        fd_guard.fd = null;
        watch_guard.store = null;
        specifier_guard.ptr = null;
    }

    /// Caller must detach the dependency from the linked list it is in.
    pub fn freeDependencyIndex(*_, alloc: Allocator, index: Dep.Index)! {
        alloc.free(_.dependencies.items[index.get()].specifier);
        _.dependencies.items[index.get()] = Dep.empty;

        if (index.get() == (_.dependencies.items.len - 1)) {
            _.dependencies.items.len -= 1;
        } else {
            _.dependencies_free_list.append(alloc, index)?;
        }
    }

    /// Expects dependency list to be already freed
    pub fn freeEntry(*_, alloc: Allocator, entry_index: usize) {
        const entry = _.watches.values()[entry_index];

        debug.log("DirectoryWatchStore.freeEntry({d}, {f})", [
            entry_index,
            entry.dir,
        ]);

        _.owner().bun_watcher.removeAtIndex(entry.watch_index, 0, [], .file);

        const _fd_guard = FdCloseGuard{ fd: if (entry.dir_fd_owned) entry.dir else null };

        alloc.free(_.watches.keys()[entry_index]);
        _.watches.swapRemoveAt(entry_index);

        if (_.watches.entries.len == 0) {
            // Every remaining dependency slot must be in the free list.
            assert(_.dependencies.items.len == _.dependencies_free_list.items.len);
            _.dependencies.clearRetainingCapacity();
            _.dependencies_free_list.clearRetainingCapacity();
        }
    }

    /// Removes all dependencies whose `source_file_path` is the exact slice `file_path`.
    pub fn removeDependenciesForFile(*_, alloc: Allocator, file_path: []const u8) {
        if (_.watches.count() == 0) return;

        debug.log("DirectoryWatchStore.removeDependenciesForFile({f})", [
            bun.fmt.quote(file_path),
        ]);

        // Iterate in reverse since `freeEntry` uses swapRemoveAt.
        let watch_index = _.watches.count();
        while (watch_index > 0) {
            watch_index -= 1;
            let entry = &_.watches.values()[watch_index];
            let new_chain: Dep.Index.Optional = .none;
            let it: Dep.Index? = entry.first_dep;
            while (index := it) {
                let dep = &_.dependencies.items[index.get()];
                it = dep.next.unwrap();
                if (dep.source_file_path.ptr == file_path.ptr) {
                    bun.handleOom(_.freeDependencyIndex(alloc, index));
                } else {
                    dep.next = new_chain;
                    new_chain = index.toOptional();
                }
            }
            if (new_first_dep := new_chain.unwrap()) {
                entry.first_dep = new_first_dep;
            } else {
                _.freeEntry(alloc, watch_index);
            }
        }
    }

    fn appendDepAssumeCapacity(*_, dep: Dep): Dep.Index {
        if (index := _.dependencies_free_list.pop()) {
            _.dependencies.items[index.get()] = dep;
            return index;
        }

        const index = Dep.Index.init(@intCast(_.dependencies.items.len));
        _.dependencies.appendAssumeCapacity(dep);
        return index;
    }
}

pub const empty_directory_watch_store: DirectoryWatchStore = DirectoryWatchStore{
    watches: .{},
    dependencies: .{},
    dependencies_free_list: .{},
};

import bun from "bun";
const Watcher = bun.Watcher;
const assert = bun.assert;
const bake = bun.bake;

const DevServer = bake.DevServer;
const debug = DevServer.debug;

import std from "std";
const ArrayListUnmanaged = std.ArrayListUnmanaged;
const Allocator = std.mem.Allocator;
