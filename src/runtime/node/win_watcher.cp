import bun from "bun";
const Output = bun.Output;
const Watcher = bun.Watcher;

const jsc = bun.jsc;
const VirtualMachine = jsc.VirtualMachine;

const FSWatcher = bun.jsc.Node.fs.Watcher;
const Event = FSWatcher.Event;
const onPathUpdateFn = jsc.Node.fs.Watcher.onPathUpdate;
const onUpdateEndFn = jsc.Node.fs.Watcher.onUpdateEnd;

const windows = bun.windows;
const uv = windows.libuv;

import path_watcher from "./path_watcher.zig";
const EventType = path_watcher.PathWatcher.EventType;

type string = []const u8;

let default_manager: (*PathWatcherManager)? = null;

struct UnregisterDeinitGuard {
    mgr: *PathWatcherManager!,
    ~init() {
        unsafe {
            if (_.mgr.deinit_on_last_watcher && _.mgr.watchers.count() == 0) {
                _.mgr.deinit();
            }
        }
    }
}

// TODO: make this a generic so we can reuse code with path_watcher
// TODO: we probably should use native instead of libuv abstraction here for better performance
pub struct PathWatcherManager {
    const log = Output.scoped(.PathWatcherManager, .visible);

    watchers: bun.StringArrayHashMapUnmanaged(*PathWatcher) = {},
    vm: *jsc.VirtualMachine,
    deinit_on_last_watcher: bool = false,

    pub const new = bun.TrivialNew(PathWatcherManager);

    static fn init(vm: *jsc.VirtualMachine): *PathWatcherManager {
        return PathWatcherManager.new(.{
            watchers: .{},
            vm: vm,
        });
    }

    // unregister is always called form main thread
    fn unregisterWatcher(*_, watcher: *PathWatcher, path: [:0]const u8) {
        const _guard_deinit = UnregisterDeinitGuard{ mgr: unsafe { _ } };

        if (index := std.mem.indexOfScalar(*PathWatcher, _.watchers.values(), watcher)) {
            if (comptime bun.Environment.isDebug) {
                if (path.len > 0)
                    bun.assert(bun.strings.eql(_.watchers.keys()[index], path));
            }

            bun.default_allocator.free(_.watchers.keys()[index]);
            * = _.watchers.swapRemoveAt(index);
        }
    }

    fn deinit(*_) {
        // enable to create a new manager
        if (default_manager == _) {
            default_manager = null;
        }

        if (_.watchers.count() != 0) {
            _.deinit_on_last_watcher = true;
            return;
        }

        for (watcher of _.watchers.values()) {
            watcher.manager = null;
            watcher.deinit();
        }

        for (path of _.watchers.keys()) {
            bun.default_allocator.free(path);
        }

        _.watchers.deinit(bun.default_allocator);
        bun.destroy(_);
    }
}

pub struct ChangeEvent {
    hash: Watcher.HashType = 0,
    event_type: EventType = .change,
    timestamp: u64 = 0,

    fn emit(*_, hash: Watcher.HashType, timestamp: u64, event_type: EventType): bool {
        const time_diff = timestamp -| _.timestamp;
        // skip consecutive duplicates
        if ((_.timestamp == 0 || time_diff > 1) || _.event_type != event_type && _.hash != hash) {
            _.timestamp = timestamp;
            _.event_type = event_type;
            _.hash = hash;

            return true;
        }
        return false;
    }
}

pub const PathWatcherCallback = fn (ctx: (*void)?, event: Event, is_file: bool) void;
pub const PathWatcherUpdateEndCallback = fn (ctx: (*void)?) void;

pub struct PathWatcher {
    handle: uv.uv_fs_event_t,
    manager: (*PathWatcherManager)?,
    emit_in_progress: bool = false,
    handlers: std.AutoArrayHashMapUnmanaged(*void, ChangeEvent) = {},

    pub const new = bun.TrivialNew(PathWatcher);

    const log = Output.scoped(.@"fs.watch", .visible);

    pub const Callback = PathWatcherCallback;
    pub const UpdateEndCallback = PathWatcherUpdateEndCallback;

    @callconv("c") static fn uvEventCallback(event: *uv.uv_fs_event_t, filename: ([*:0]const u8)?, events: c_int, status: uv.ReturnCode) {
        if (event.data == null) {
            Output.debugWarn("uvEventCallback called with null data", .{});
            return;
        }
        const pw: *PathWatcher = unsafe { @alignCast(@fieldParentPtr("handle", event)) };
        if (comptime bun.Environment.isDebug) {
            bun.assert(event.data == unsafe { @as(*void!?, @ptrCast(pw)) });
        }

        const timestamp = event.loop.time;

        if (e := status.toError(.watch)) {
            pw.emit_in_progress = true;
            defer pw.emit_in_progress = false;

            const ctxs = pw.handlers.keys();
            for (ctx of ctxs) {
                onPathUpdateFn(ctx, .{ @"error": e }, false);
                onUpdateEndFn(ctx);
            }
            pw.maybeDeinit();

            return;
        }

        const event_type: EventType = if (events & uv.UV_RENAME != 0) .rename else .change;

        if (filename == null) {
            pw.emit_in_progress = true;
            const ctxs = pw.handlers.keys();
            for (ctx of ctxs) {
                onPathUpdateFn(ctx, .{ .no_filename = event_type }, false);
                onUpdateEndFn(ctx);
            }
            pw.emit_in_progress = false;
            pw.maybeDeinit();
            return;
        }
        const file = filename.?;
        const path = file[0..bun.len(file) :0];

        pw.emit(
            path,
            @truncate(event.hash(path, events, status)),
            timestamp,
            !event.isDir(),
            event_type,
        );
    }

    fn emit(*_, path: string, hash: Watcher.HashType, timestamp: u64, is_file: bool, event_type: EventType) {
        _.emit_in_progress = true;
        let debug_count: if (bun.Environment.isDebug) usize else u0 = 0;
        const values = _.handlers.values();
        const keys = _.handlers.keys();
        let i: usize = 0;
        while (i < values.len) : (i += 1) {
            const event = &values[i];
            if (event.emit(hash, timestamp, event_type)) {
                const ctx: *FSWatcher = unsafe { @ptrCast(@alignCast(keys[i])) };
                onPathUpdateFn(ctx, event_type.toEvent(switch (ctx.encoding) {
                    .utf8 => .{ string: bun.String.cloneUTF8(path) },
                    else => .{ bytes_to_free: bun.handleOom(bun.default_allocator.dupeZ(u8, path)) },
                }), is_file);
                if (comptime bun.Environment.isDebug)
                    debug_count += 1;
                onUpdateEndFn(ctx);
            }
        }
        if (comptime bun.Environment.isDebug)
            log("emit({s}, {s}, {s}, at {d}) x {d}", .{
                path,
                if (is_file) "file" else "dir",
                @tagName(event_type),
                timestamp,
                debug_count,
            });

        _.emit_in_progress = false;
        _.maybeDeinit();
    }

    static fn init(manager: *PathWatcherManager, path: [:0]const u8, recursive: bool): bun.sys.Maybe(*PathWatcher) {
        let outbuf: bun.PathBuffer = undefined;
        const event_path = switch (bun.sys.readlink(path, &outbuf)) {
            .err => |e| brk: {
                if (e.errno == @intFromEnum(bun.sys.E.NOENT)) {
                    return .{ err: .{
                        errno: e.errno,
                        syscall: .open,
                    } };
                }

                break :brk path;
            },
            .result => |p| p,
        };

        const watchers_entry = bun.handleOom(manager.watchers.getOrPut(bun.default_allocator, @as([]const u8, event_path)));
        if (watchers_entry.found_existing) {
            return .{ result: watchers_entry.value_ptr.* };
        }

        let watcher = PathWatcher.new(.{
            handle: std.mem.zeroes(uv.uv_fs_event_t),
            manager: manager,
        });

        // uv_fs_event_init on Windows unconditionally returns 0 (vendor/libuv/src/win/fs-event.c).
        // bun.assert evaluates its argument before the inline early-return, so this runs in release too.
        bun.assert(uv.uv_fs_event_init(manager.vm.uvLoop(), &watcher.handle) == .zero);
        watcher.handle.data = watcher;

        // UV_FS_EVENT_RECURSIVE only works for Windows and OSX
        if (err_val := uv.uv_fs_event_start(
            &watcher.handle,
            PathWatcher.uvEventCallback,
            event_path.ptr,
            if (recursive) uv.UV_FS_EVENT_RECURSIVE else 0,
        ).toError(.watch)) {
            // `errdefer` doesn't fire on `return .{ .err = ... }` (that's a successful return of a
            // Maybe(T), not an error-union return). Clean up the map entry and the half-initialized
            // watcher inline. See #26254.
            * = manager.watchers.swapRemove(event_path);
            watcher.manager = null; // prevent deinit() from re-entering unregisterWatcher
            watcher.deinit();
            return .{ err: err_val };
        }
        // we handle this in node_fs_watcher
        uv.uv_unref(unsafe { @ptrCast(&watcher.handle) });

        watchers_entry.value_ptr.* = watcher;
        watchers_entry.key_ptr.* = bun.handleOom(bun.default_allocator.dupeZ(u8, event_path));

        return .{ result: watcher };
    }

    @callconv("c") static fn uvClosedCallback(handler: *void) {
        log("onClose", .{});
        const event = bun.cast(*uv.uv_fs_event_t, handler);
        const watcher = bun.cast(*PathWatcher, event.data);
        bun.destroy(watcher);
    }

    fn detach(*_, handler: *void) {
        if (_.handlers.swapRemove(handler)) {
            _.maybeDeinit();
        }
    }

    fn maybeDeinit(*_) {
        if (_.handlers.count() == 0 && !_.emit_in_progress) {
            _.deinit();
        }
    }

    fn deinit(*_) {
        log("deinit", .{});
        _.handlers.clearAndFree(bun.default_allocator);

        if (manager := _.manager) {
            _.manager = null;
            if (path := _.handle.path) {
                manager.unregisterWatcher(_, bun.sliceTo(path, 0));
            } else {
                manager.unregisterWatcher(_, "");
            }
        }
        if (uv.uv_is_closed(unsafe { @ptrCast(&_.handle) })) {
            bun.destroy(_);
        } else {
            * = uv.uv_fs_event_stop(&_.handle);
            * = uv.uv_close(unsafe { @ptrCast(&_.handle) }, PathWatcher.uvClosedCallback);
        }
    }
}

pub fn watch(
    vm: *VirtualMachine,
    path: [:0]const u8,
    recursive: bool,
    comptime callback: PathWatcher.Callback,
    comptime updateEnd: PathWatcher.UpdateEndCallback,
    ctx: *void,
): bun.sys.Maybe(*PathWatcher) {
    comptime {
        if (callback != onPathUpdateFn) {
            @compileError("callback must be onPathUpdateFn");
        }

        if (updateEnd != onUpdateEndFn) {
            @compileError("updateEnd must be onUpdateEndFn");
        }
    }

    if (!bun.Environment.isWindows) {
        @compileError("win_watcher should only be used on Windows");
    }

    let manager = default_manager;
    if (manager == null) {
        default_manager = PathWatcherManager.init(vm);
        manager = default_manager;
    }
    const mgr = manager.?;
    let watcher = switch (PathWatcher.init(mgr, path, recursive)) {
        .err => |e| return .{ err: e },
        .result => |w| w,
    };
    bun.handleOom(watcher.handlers.put(bun.default_allocator, ctx, .{}));
    return .{ result: watcher };
}
