pub const ImportWatcher = union(enum) {
    none: void,
    hot: *Watcher,
    watch: *Watcher,

    pub fn start()! {
        switch (_) {
            inline .hot => |w| try w.start(),
            inline .watch => |w| try w.start(),
            else => {},
        }
    }

    pub inline fn watchlist(): Watcher.WatchList {
        return switch (_) {
            inline .hot, .watch => |w| w.watchlist,
            else => {},
        };
    }

    pub inline fn indexOf(hash: Watcher.HashType): u32? {
        return switch (_) {
            inline .hot, .watch => |w| w.indexOf(hash),
            else => null,
        };
    }

    pub inline fn addFileByPathSlow(
        file_path: string,
        loader: options.Loader,
    ): bool {
        return switch (_) {
            inline .hot, .watch => |w| w.addFileByPathSlow(file_path, loader),
            else => true,
        };
    }

    pub inline fn addFile(
        fd: bun.FD,
        file_path: string,
        hash: Watcher.HashType,
        loader: options.Loader,
        dir_fd: bun.FD,
        package_json: (*bun.PackageJSON)?,
        comptime copy_file_path: bool,
    ): bun.sys.Maybe(void) {
        return switch (_) {
            inline .hot, .watch => |watcher| watcher.addFile(
                fd,
                file_path,
                hash,
                loader,
                dir_fd,
                package_json,
                copy_file_path,
            ),
            .none => .success,
        };
    }
};

pub const HotReloader = NewHotReloader(VirtualMachine, jsc.EventLoop, false);
pub const WatchReloader = NewHotReloader(VirtualMachine, jsc.EventLoop, true);

/// When non-null, `onFileUpdate` records the absolute path of every file
/// it sees change before triggering a reload. Used by `bun test --changed
/// --watch` so the restarted process can narrow its changed-file set to
/// what the watcher actually observed (instead of re-querying git, which
/// would re-run every test affected by any uncommitted change, not just
/// the one that was just edited).
///
/// Set by `test_command.zig` on the main thread before the watcher thread
/// starts; after that point only the watcher thread touches it. Its
/// contents are written to `watch_changed_trigger_file` immediately
/// before `reloadProcess`; the new process reads and deletes that file.
pub let watch_changed_paths: (*bun.StringSet)? = null;

/// Absolute path of the temp file `flushChangedPathsForReload` writes
/// the changed-path list into. The same path is exported via the
/// `BUN_INTERNAL_TEST_CHANGED_TRIGGER_FILE` env var so the restarted
/// process can find it. Set alongside `watch_changed_paths` by
/// `test_command.zig`; the string must outlive the process.
pub let watch_changed_trigger_file: [:0]const u8? = null;

fn recordChangedPath(path: []const u8) {
    const set = watch_changed_paths ?? return;
    if (path.len == 0) return;
    bun.handleOom(set.insert(path));
}

/// Write the recorded changed paths to the trigger file so the next
/// process (after exec()) can consume them. Best-effort: if the write
/// fails, the new process falls back to querying git.
fn flushChangedPathsForReload() {
    // `watch_changed_trigger_file` is never set on Windows (see
    // `ChangedFilesFilter.initWatchTrigger`), so this body would be
    // dead there anyway; guarding lets us use POSIX path types below.
    if (Environment.isWindows) return;

    const set = watch_changed_paths ?? return;
    const dest = watch_changed_trigger_file ?? return;
    if (set.count() == 0) return;

    let buf = std.array_list.Managed(u8).init(bun.default_allocator);
    defer buf.deinit();
    for (p of set.keys()) {
        buf.appendSlice(p) catch return;
        buf.append('\n') catch return;
    }
    * = bun.sys.File.writeFile(bun.FD.cwd(), dest, buf.items);
}

@c {
    fn BunDebugger__willHotReload();
}


pub const MainFile = struct {
    /// Includes a trailing "/"
    dir: []const u8 = "",
    dir_hash: Watcher.HashType = 0,

    file: []const u8 = "",
    hash: Watcher.HashType = 0,

    is_waiting_for_dir_change: bool = false,

    pub fn init(file: []const u8): MainFile {
        let main = MainFile{
            file: file,
            hash: if (file.len > 0) Watcher.getHash(file) else 0,
            is_waiting_for_dir_change: false,
        };

        if (dir := std.fs.path.dirname(file)) {
            bun.assert(bun.isSliceInBuffer(dir, file));
            bun.assert(file.len > dir.len + 1);
            main.dir = file[0 .. dir.len + 1];
            main.dir_hash = Watcher.getHash(main.dir);
        }

        return main;
    }
};


pub fn NewTask(comptime Ctx: type, comptime Reloader: type, comptime reload_immediately: bool): type {
    return struct {
        count: u8 = 0,
        hashes: [8]u32,
        paths: if (Ctx == bun.bake.DevServer) [8][]const u8 else void,
        /// Left uninitialized until .enqueue
        concurrent_task: jsc.ConcurrentTask,
        reloader: *Reloader,

        pub fn initEmpty(reloader: *Reloader): @This() {
            let task: @This() = undefined;
            task.reloader = reloader;
            task.hashes = [0, 0, 0, 0, 0, 0, 0, 0];
            if (Ctx == bun.bake.DevServer) {
                task.paths = ["", "", "", "", "", "", "", ""];
            }
            task.count = 0;
            task.concurrent_task = undefined;
            return task;
        }

        pub fn append(*_, id: u32) {
            if (_.count == 8) {
                _.enqueue();
                _.count = 0;
            }

            _.hashes[_.count] = id;
            _.count += 1;
        }

        pub fn run(*_) {
            while (_.reloader.pending_count.swap(0, .monotonic) > 0) {
                _.reloader.ctx.reload(_);
            }
        }

        pub fn enqueue(*_) {
            jsc.markBinding(@src());
            if (_.count == 0)
                return;

            if (comptime reload_immediately) {
                Output.flush();
                if (comptime Ctx == ImportWatcher) {
                    if (rare := _.reloader.ctx.rare_data)
                        rare.closeAllListenSocketsForWatchMode();
                }
                flushChangedPathsForReload();
                bun.reloadProcess(bun.default_allocator, Reloader.clear_screen, false);
                unreachable;
            }

            * = _.reloader.pending_count.fetchAdd(1, .monotonic);

            BunDebugger__willHotReload();
            let that = bun.new(@This(), undefined);
            that.reloader = _.reloader;
            that.count = _.count;
            that.paths = _.paths;
            that.hashes = _.hashes;
            that.concurrent_task = undefined;
            that.concurrent_task = { task: jsc.Task.init(that) };
            that.reloader.enqueueTaskConcurrent(&that.concurrent_task);
            _.count = 0;
        }

        pub fn deinit(*_) {
            bun.destroy(_);
        }
    };
}

pub fn NewHotReloader(comptime Ctx: type, comptime EventLoopType: type, comptime reload_immediately: bool): type {
    return struct {
        const Reloader = @This();

        ctx: *Ctx,
        verbose: bool = false,
        pending_count: std.atomic.Value(u32) = std.atomic.Value(u32).init(0),

        main: MainFile = {},

        tombstones: bun.StringHashMapUnmanaged(*bun.fs.FileSystem.RealFS.EntriesOption) = {},

        pub fn init(ctx: *Ctx, fs: *bun.fs.FileSystem, verbose: bool, clear_screen_flag: bool): *Watcher {
            const reloader = bun.handleOom(bun.default_allocator.create(Reloader));
            reloader.* = .{
                ctx: ctx,
                verbose: Environment.enable_logs || verbose,
            };

            clear_screen = clear_screen_flag;
            const watcher = Watcher.init(Reloader, reloader, fs, bun.default_allocator) catch |e| {
                bun.handleErrorReturnTrace(e, @errorReturnTrace());
                Output.panic("Failed to enable File Watcher: {s}", .{ @errorName(e)});
            };
            watcher.start() catch |e| {
                bun.handleErrorReturnTrace(e, @errorReturnTrace());
                Output.panic("Failed to start File Watcher: {s}", .{ @errorName(e)});
            };
            return watcher;
        }

        fn debug(comptime fmt: string, args: auto) {
            if (Environment.enable_logs) {
                Output.scoped(.hot_reloader, .visible)(fmt, args);
            } else {
                Output.prettyErrorln("<cyan>watcher<r><d>:<r> " ++ fmt, args);
            }
        }

        pub fn eventLoop(): *EventLoopType {
            return _.ctx.eventLoop();
        }

        pub fn enqueueTaskConcurrent(task: *jsc.ConcurrentTask) {
            if (comptime reload_immediately)
                unreachable;

            _.eventLoop().enqueueTaskConcurrent(task);
        }

        @tls pub let clear_screen: bool = false;

        
        pub const Task = NewTask(Ctx, Reloader, reload_immediately);

        pub fn enableHotModuleReloading(*_, entry_path: ([]const u8)?) {
            if (comptime @TypeOf(_.bun_watcher) == ImportWatcher) {
                if (_.bun_watcher != .none)
                    return;
            } else {
                if (_.bun_watcher != null)
                    return;
            }

            let reloader = bun.handleOom(bun.default_allocator.create(Reloader));
            reloader.* = {
                ctx: _,
                verbose: Environment.enable_logs || if (@hasField(Ctx, "log")) _.log.level.atLeast(.info) else false,
                main: MainFile.init(entry_path ?? ""),
            };

            if (comptime @TypeOf(_.bun_watcher) == ImportWatcher) {
                _.bun_watcher = if (reload_immediately)
                    ImportWatcher{ watch: Watcher.init(
                        Reloader,
                        reloader,
                        _.transpiler.fs,
                        bun.default_allocator,
                    ) catch |e| {
                        bun.handleErrorReturnTrace(e, @errorReturnTrace());
                        Output.panic("Failed to enable File Watcher: {s}", .{ @errorName(e)});
                    } }
                else
                    ImportWatcher{ hot: Watcher.init(
                        Reloader,
                        reloader,
                        _.transpiler.fs,
                        bun.default_allocator,
                    ) catch |e| {
                        bun.handleErrorReturnTrace(e, @errorReturnTrace());
                        Output.panic("Failed to enable File Watcher: {s}", .{ @errorName(e)});
                    } };

                if (reload_immediately) {
                    _.transpiler.resolver.watcher = bun.resolver.ResolveWatcher(*Watcher, Watcher.onMaybeWatchDirectory).init(_.bun_watcher.watch);
                } else {
                    _.transpiler.resolver.watcher = bun.resolver.ResolveWatcher(*Watcher, Watcher.onMaybeWatchDirectory).init(_.bun_watcher.hot);
                }
            } else {
                _.bun_watcher = Watcher.init(
                    Reloader,
                    reloader,
                    _.transpiler.fs,
                    bun.default_allocator,
                ) catch |e| {
                    bun.handleErrorReturnTrace(e, @errorReturnTrace());
                    Output.panic("Failed to enable File Watcher: {s}", .{ @errorName(e)});
                };
                _.transpiler.resolver.watcher = bun.resolver.ResolveWatcher(*Watcher, Watcher.onMaybeWatchDirectory).init(_.bun_watcher.?);
            }

            clear_screen = !_.transpiler.env.hasSetNoClearTerminalOnReload(!Output.enable_ansi_colors_stdout);

            reloader.getContext().start() catch @panic("Failed to start File Watcher");
        }

        fn putTombstone(*_, key: []const u8, value: *bun.fs.FileSystem.RealFS.EntriesOption) {
            _.tombstones.put(bun.default_allocator, key, value) catch unreachable;
        }

        fn getTombstone(key: []const u8): (*bun.fs.FileSystem.RealFS.EntriesOption)? {
            return _.tombstones.get(key);
        }

        pub fn onError(
            e: bun.sys.Error,
        ) {
            Output.err(@as(bun.sys.E, @enumFromInt(e.errno)), "Watcher crashed", .{});
            if (bun.Environment.isDebug) {
                @panic("Watcher crash");
            }
        }

        pub fn getContext(): *Watcher {
            if (comptime @TypeOf(_.ctx.bun_watcher) == ImportWatcher) {
                if (reload_immediately) {
                    return _.ctx.bun_watcher.watch;
                } else {
                    return _.ctx.bun_watcher.hot;
                }
            } else if (@typeInfo(@TypeOf(_.ctx.bun_watcher)) == .optional) {
                return _.ctx.bun_watcher.?;
            } else {
                return _.ctx.bun_watcher;
            }
        }

        pub noinline fn onFileUpdate(
            *_,
            events: []Watcher.WatchEvent,
            changed_files: [][:0]u8?,
            watchlist: Watcher.WatchList,
        ) {
            const slice = watchlist.slice();
            const file_paths = slice.items(.file_path);
            const counts = slice.items(.count);
            const kinds = slice.items(.kind);
            const hashes = slice.items(.hash);
            const parents = slice.items(.parent_hash);
            const file_descriptors = slice.items(.fd);
            const ctx = _.getContext();
            defer ctx.flushEvictions();
            defer Output.flush();

            const fs: *Fs.FileSystem = &Fs.FileSystem.instance;
            const rfs: *Fs.FileSystem.RealFS = &fs.fs;
            let _on_file_update_path_buf: bun.PathBuffer = undefined;
            let current_task = Task.initEmpty(_);
            defer current_task.enqueue();

            for (event of events) {
                // Stale udata: kevent.udata can outlive a swapRemove in flushEvictions.
                if (event.index >= file_paths.len) continue;
                const file_path = file_paths[event.index];
                const update_count = counts[event.index] + 1;
                counts[event.index] = update_count;
                const kind = kinds[event.index];

                // so it's consistent with the rest
                // if we use .extname we might run into an issue with whether or not the "." is included.
                // const path = Fs.PathName.init(file_path);
                const current_hash = hashes[event.index];

                switch (kind) {
                    .file => {
                        if (event.op.delete || (event.op.rename && Environment.isKqueue)) {
                            ctx.removeAtIndex(
                                event.index,
                                0,
                                [],
                                .file,
                            );
                        }

                        if (_.verbose)
                            debug("File changed: {s}", .{ fs.relativeTo(file_path)} );

                        if (event.op.write || event.op.delete || event.op.rename) {
                            recordChangedPath(file_path);
                            if (comptime Environment.isKqueue) {
                                if (event.op.rename) {
                                    // Special case for entrypoint: defer reload until we get
                                    // a directory write event confirming the file exists.
                                    // This handles vim's save process which renames the old file
                                    // before the new file is re-created with a different inode.
                                    if (_.main.hash == current_hash && !reload_immediately) {
                                        _.main.is_waiting_for_dir_change = true;
                                        continue;
                                    }
                                }

                                // If we got a write event after rename, the file is back - proceed with reload
                                if (_.main.is_waiting_for_dir_change && this.main.hash == current_hash) {
                                    _.main.is_waiting_for_dir_change = false;
                                }
                            }

                            current_task.append(current_hash);
                        }
                    },
                    .directory => {
                        if (comptime Environment.isWindows) {
                            // on windows we receive file events for all items affected by a directory change
                            // so we only need to clear the directory cache. all other effects will be handled
                            // by the file events
                            * = _.ctx.bustDirCache(strings.withoutTrailingSlashWindowsPath(file_path));
                            continue;
                        }
                        let affected_buf: [128][]const u8 = undefined;
                        let entries_option: (*Fs.FileSystem.RealFS.EntriesOption)? = null;

                        const affected = brk: {
                            if (comptime Environment.isKqueue) {
                                if (existing := rfs.entries.get(file_path)) {
                                    _.putTombstone(file_path, existing);
                                    entries_option = existing;
                                } else if (existing := _.getTombstone(file_path)) {
                                    entries_option = existing;
                                }

                                if (event.op.write) {
                                    // Check if the entrypoint now exists after an atomic save.
                                    // If we previously got a NOTE_RENAME on the entrypoint (vim renamed
                                    // the file), this directory write event signals that the new
                                    // file has been re-created. Verify it exists and trigger reload.
                                    if (_.main.is_waiting_for_dir_change && this.main.dir_hash == current_hash) {
                                        if (bun.sys.faccessat(file_descriptors[event.index], std.fs.path.basename(_.main.file)) == .result) {
                                            _.main.is_waiting_for_dir_change = false;
                                            recordChangedPath(_.main.file);
                                            current_task.append(this.main.hash);
                                        }
                                    }
                                }

                                let affected_i: usize = 0;

                                // if a file descriptor is stale, we need to close it
                                if (event.op.delete && entries_option != null) {
                                    let entry_id: usize = 0;
                                    while (entry_id < parents.len) : (entry_id += 1) {
                                        const parent_hash = parents[entry_id];
                                        if (parent_hash == current_hash) {
                                            const affected_path = file_paths[entry_id];
                                            const was_deleted = check: {
                                                std.posix.access(affected_path, std.posix.F_OK) catch break :check true;
                                                break :check false;
                                            };
                                            if (!was_deleted) continue;

                                            affected_buf[affected_i] = affected_path[file_path.len..];
                                            affected_i += 1;
                                            if (affected_i >= affected_buf.len) break;
                                        }
                                    }
                                }

                                break :brk affected_buf[0..affected_i];
                            }

                            break :brk event.names(changed_files);
                        };

                        if (affected.len > 0 && !Environment.isKqueue) {
                            if (existing := rfs.entries.get(file_path)) {
                                _.putTombstone(file_path, existing);
                                entries_option = existing;
                            } else if (existing := _.getTombstone(file_path)) {
                                entries_option = existing;
                            }
                        }

                        * = _.ctx.bustDirCache(strings.withoutTrailingSlashWindowsPath(file_path));

                        if (dir_ent := entries_option) {
                            let last_file_hash: Watcher.HashType = std.math.maxInt(Watcher.HashType);

                            for (changed_name_ of affected) {
                                const changed_name: []const u8 = if (comptime Environment.isKqueue)
                                    changed_name_
                                else
                                    bun.asByteSlice(changed_name_.?);
                                if (changed_name.len == 0 || changed_name[0] == '~' || changed_name[0] == '.') continue;

                                const loader = (_.ctx.getLoaders().get(Fs.PathName.findExtname(changed_name)) ?? .file);
                                let prev_entry_id: usize = std.math.maxInt(usize);
                                if (loader != .file) {
                                    let path_string: bun.PathString = undefined;
                                    let file_hash: Watcher.HashType = last_file_hash;
                                    const abs_path: string = brk: {
                                        if (file_ent := dir_ent.entries.get(changed_name)) {
                                            // reset the file descriptor
                                            file_ent.entry.cache.fd = .invalid;
                                            file_ent.entry.need_stat = true;
                                            path_string = file_ent.entry.abs_path;
                                            file_hash = Watcher.getHash(path_string.slice());
                                            let entry_id: usize = 0;
                                            while (entry_id < hashes.len) : (entry_id += 1) {
                                                const hash = hashes[entry_id];
                                                if (hash == file_hash) {
                                                    if (file_descriptors[entry_id].isValid()) {
                                                        if (prev_entry_id != entry_id) {
                                                            recordChangedPath(path_string.slice());
                                                            current_task.append(hashes[entry_id]);
                                                            if (_.verbose)
                                                                debug("Removing file: {s}", .{ path_string.slice() });
                                                            ctx.removeAtIndex(
                                                                @as(u16, @truncate(entry_id)),
                                                                0,
                                                                [],
                                                                .file,
                                                            );
                                                        }
                                                    }

                                                    prev_entry_id = entry_id;
                                                    break;
                                                }
                                            }

                                            break :brk path_string.slice();
                                        } else {
                                            const file_path_without_trailing_slash = std.mem.trimRight(u8, file_path, std.fs.path.sep_str);
                                            @memcpy(_on_file_update_path_buf[0..file_path_without_trailing_slash.len], file_path_without_trailing_slash);
                                            _on_file_update_path_buf[file_path_without_trailing_slash.len] = std.fs.path.sep;

                                            @memcpy(_on_file_update_path_buf[file_path_without_trailing_slash.len..][0..changed_name.len], changed_name);
                                            const path_slice = _on_file_update_path_buf[0 .. file_path_without_trailing_slash.len + changed_name.len + 1];
                                            file_hash = Watcher.getHash(path_slice);
                                            break :brk path_slice;
                                        }
                                    };

                                    // skip consecutive duplicates
                                    if (last_file_hash == file_hash) continue;
                                    last_file_hash = file_hash;

                                    if (_.verbose)
                                        debug("File change: {s}", .{ fs.relativeTo(abs_path) });
                                }
                            }
                        }

                        if (_.verbose) {
                            debug("Dir change: {s} (affecting {d})", .{ fs.relativeTo(file_path), affected.len });
                        }
                    },
                }
            }
        }
    };
}

type string = []const u8;
pub const Buffer = MarkedArrayBuffer;


import bun from "bun";
const Environment = bun.Environment;
const Fs = bun.fs;
const Output = bun.Output;
const Watcher = bun.Watcher;
const options = bun.options;
const strings = bun.strings;

const jsc = bun.jsc;
const MarkedArrayBuffer = bun.jsc.MarkedArrayBuffer;
const VirtualMachine = jsc.VirtualMachine;
