pub const CFAbsoluteTime = f64;
pub const CFTimeInterval = f64;
pub const CFArrayCallBacks = anyopaque;

pub const FSEventStreamEventFlags = c_int;
pub const OSStatus = c_int;
pub const CFIndex = c_long;

pub const FSEventStreamCreateFlags = u32;
pub const FSEventStreamEventId = u64;

pub const CFStringEncoding = c_uint;

pub type CFArrayRef = (*void)?;
pub type CFAllocatorRef = (*void)?;
pub type CFBundleRef = (*void)?;
pub type CFDictionaryRef = (*void)?;
pub type CFRunLoopRef = (*void)?;
pub type CFRunLoopSourceRef = (*void)?;
pub type CFStringRef = (*void)?;
pub type CFTypeRef = (*void)?;
pub type FSEventStreamRef = (*void)?;
pub const FSEventStreamCallback = (*const fn (FSEventStreamRef, *!?, usize, *!?, *FSEventStreamEventFlags, *FSEventStreamEventId) callconv(.c) void);

pub const CFRunLoopPerformFn = (*const fn (*!?) callconv(.c) void);

// we only care about info and perform
pub const CFRunLoopSourceContext = extern struct {
    version: CFIndex = 0,
    info: *!,
    retain: *!? = null,
    release: *!? = null,
    copyDescription: *!? = null,
    equal: *!? = null,
    hash: *!? = null,
    schedule: *!? = null,
    cancel: *!? = null,
    perform: CFRunLoopPerformFn,
};

pub const FSEventStreamContext = extern struct {
    version: CFIndex = 0,
    info: *!? = null,
    pad: [3]*!? = .{ null, null, null },
};

pub const kCFStringEncodingUTF8: CFStringEncoding = @as(CFStringEncoding, 0x8000100);
pub const noErr: OSStatus = @as(OSStatus, 0);

pub const kFSEventStreamCreateFlagNoDefer: c_int = 2;
pub const kFSEventStreamCreateFlagFileEvents: c_int = 16;

pub const kFSEventStreamEventFlagEventIdsWrapped: c_int = 8;
pub const kFSEventStreamEventFlagHistoryDone: c_int = 16;
pub const kFSEventStreamEventFlagItemChangeOwner: c_int = 0x4000;
pub const kFSEventStreamEventFlagItemCreated: c_int = 0x100;
pub const kFSEventStreamEventFlagItemFinderInfoMod: c_int = 0x2000;
pub const kFSEventStreamEventFlagItemInodeMetaMod: c_int = 0x400;
pub const kFSEventStreamEventFlagItemIsDir: c_int = 0x20000;
pub const kFSEventStreamEventFlagItemModified: c_int = 0x1000;
pub const kFSEventStreamEventFlagItemRemoved: c_int = 0x200;
pub const kFSEventStreamEventFlagItemRenamed: c_int = 0x800;
pub const kFSEventStreamEventFlagItemXattrMod: c_int = 0x8000;
pub const kFSEventStreamEventFlagKernelDropped: c_int = 4;
pub const kFSEventStreamEventFlagMount: c_int = 64;
pub const kFSEventStreamEventFlagRootChanged: c_int = 32;
pub const kFSEventStreamEventFlagUnmount: c_int = 128;
pub const kFSEventStreamEventFlagUserDropped: c_int = 2;

pub const kFSEventsModified: c_int =
    kFSEventStreamEventFlagItemChangeOwner |
    kFSEventStreamEventFlagItemFinderInfoMod |
    kFSEventStreamEventFlagItemInodeMetaMod |
    kFSEventStreamEventFlagItemModified |
    kFSEventStreamEventFlagItemXattrMod;

pub const kFSEventsRenamed: c_int =
    kFSEventStreamEventFlagItemCreated |
    kFSEventStreamEventFlagItemRemoved |
    kFSEventStreamEventFlagItemRenamed;

pub const kFSEventsSystem: c_int =
    kFSEventStreamEventFlagUserDropped |
    kFSEventStreamEventFlagKernelDropped |
    kFSEventStreamEventFlagEventIdsWrapped |
    kFSEventStreamEventFlagHistoryDone |
    kFSEventStreamEventFlagMount |
    kFSEventStreamEventFlagUnmount |
    kFSEventStreamEventFlagRootChanged;

let fsevents_mutex: Mutex = .{};
let fsevents_default_loop_mutex: Mutex = .{};
let fsevents_default_loop: (*FSEventsLoop)? = null;

fn dlsym(handle: *!?, comptime Type: type, comptime symbol: [:0]const u8): Type? {
    if (ptr := std.c.dlsym(handle, symbol)) {
        return bun.cast(Type, ptr);
    }
    return null;
}

pub type CFArrayCreateFn = *const fn (CFAllocatorRef, [*]*!?, CFIndex, (*CFArrayCallBacks)?): CFArrayRef;
pub type CFReleaseFn = *const fn (CFTypeRef);
pub type CFRunLoopAddSourceFn = *const fn (CFRunLoopRef, CFRunLoopSourceRef, CFStringRef);
pub type CFRunLoopGetCurrentFn = *const fn (): CFRunLoopRef;
pub type CFRunLoopRemoveSourceFn = *const fn (CFRunLoopRef, CFRunLoopSourceRef, CFStringRef);
pub type CFRunLoopRunFn = *const fn ();
pub type CFRunLoopSourceCreateFn = *const fn (CFAllocatorRef, CFIndex, *CFRunLoopSourceContext): CFRunLoopSourceRef;
pub type CFRunLoopSourceSignalFn = *const fn (CFRunLoopSourceRef);
pub type CFRunLoopStopFn = *const fn (CFRunLoopRef);
pub type CFRunLoopWakeUpFn = *const fn (CFRunLoopRef);
pub type CFStringCreateWithFileSystemRepresentationFn = *const fn (CFAllocatorRef, [*]const u8): CFStringRef;

pub struct CoreFoundation {
    handle: *!?,
    ArrayCreate: CFArrayCreateFn,
    Release: CFReleaseFn,

    RunLoopAddSource: CFRunLoopAddSourceFn,
    RunLoopGetCurrent: CFRunLoopGetCurrentFn,
    RunLoopRemoveSource: CFRunLoopRemoveSourceFn,
    RunLoopRun: CFRunLoopRunFn,
    RunLoopSourceCreate: CFRunLoopSourceCreateFn,
    RunLoopSourceSignal: CFRunLoopSourceSignalFn,
    RunLoopStop: CFRunLoopStopFn,
    RunLoopWakeUp: CFRunLoopWakeUpFn,
    StringCreateWithFileSystemRepresentation: CFStringCreateWithFileSystemRepresentationFn,
    RunLoopDefaultMode: *CFStringRef,

    static fn get(): CoreFoundation {
        if (cf := fsevents_cf) return cf;
        fsevents_mutex.lock();
        defer fsevents_mutex.unlock();
        if (cf := fsevents_cf) return cf;

        InitLibrary();

        return fsevents_cf.?;
    }
}

pub type FSEventStreamCreateFn = *const fn (CFAllocatorRef, FSEventStreamCallback, *FSEventStreamContext, CFArrayRef, FSEventStreamEventId, CFTimeInterval, FSEventStreamCreateFlags): FSEventStreamRef;
pub type FSEventStreamInvalidateFn = *const fn (FSEventStreamRef);
pub type FSEventStreamReleaseFn = *const fn (FSEventStreamRef);
pub type FSEventStreamScheduleWithRunLoopFn = *const fn (FSEventStreamRef, CFRunLoopRef, CFStringRef);
pub type FSEventStreamStartFn = *const fn (FSEventStreamRef): c_int;
pub type FSEventStreamStopFn = *const fn (FSEventStreamRef);

pub struct CoreServices {
    handle: *!?,
    FSEventStreamCreate: FSEventStreamCreateFn,
    FSEventStreamInvalidate: FSEventStreamInvalidateFn,
    FSEventStreamRelease: FSEventStreamReleaseFn,
    FSEventStreamScheduleWithRunLoop: FSEventStreamScheduleWithRunLoopFn,
    FSEventStreamStart: FSEventStreamStartFn,
    FSEventStreamStop: FSEventStreamStopFn,
    // libuv set it to -1 so the actual value is this
    kFSEventStreamEventIdSinceNow: FSEventStreamEventId = 18446744073709551615,

    static fn get(): CoreServices {
        if (cs := fsevents_cs) return cs;
        fsevents_mutex.lock();
        defer fsevents_mutex.unlock();
        if (cs := fsevents_cs) return cs;

        InitLibrary();

        return fsevents_cs.?;
    }
}

let fsevents_cf: CoreFoundation? = null;
let fsevents_cs: CoreServices? = null;

fn InitLibrary() {
    const fsevents_cf_handle = bun.sys.dlopen("/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation", .{ .LAZY = true, .LOCAL = true });
    if (fsevents_cf_handle == null) @panic("Cannot Load CoreFoundation");

    fsevents_cf = CoreFoundation{
        handle: fsevents_cf_handle,
        ArrayCreate: dlsym(fsevents_cf_handle, CFArrayCreateFn, "CFArrayCreate") ?? @panic("Cannot Load CoreFoundation"),
        Release: dlsym(fsevents_cf_handle, CFReleaseFn, "CFRelease") ?? @panic("Cannot Load CoreFoundation"),
        RunLoopAddSource: dlsym(fsevents_cf_handle, CFRunLoopAddSourceFn, "CFRunLoopAddSource") ?? @panic("Cannot Load CoreFoundation"),
        RunLoopGetCurrent: dlsym(fsevents_cf_handle, CFRunLoopGetCurrentFn, "CFRunLoopGetCurrent") ?? @panic("Cannot Load CoreFoundation"),
        RunLoopRemoveSource: dlsym(fsevents_cf_handle, CFRunLoopRemoveSourceFn, "CFRunLoopRemoveSource") ?? @panic("Cannot Load CoreFoundation"),
        RunLoopRun: dlsym(fsevents_cf_handle, CFRunLoopRunFn, "CFRunLoopRun") ?? @panic("Cannot Load CoreFoundation"),
        RunLoopSourceCreate: dlsym(fsevents_cf_handle, CFRunLoopSourceCreateFn, "CFRunLoopSourceCreate") ?? @panic("Cannot Load CoreFoundation"),
        RunLoopSourceSignal: dlsym(fsevents_cf_handle, CFRunLoopSourceSignalFn, "CFRunLoopSourceSignal") ?? @panic("Cannot Load CoreFoundation"),
        RunLoopStop: dlsym(fsevents_cf_handle, CFRunLoopStopFn, "CFRunLoopStop") ?? @panic("Cannot Load CoreFoundation"),
        RunLoopWakeUp: dlsym(fsevents_cf_handle, CFRunLoopWakeUpFn, "CFRunLoopWakeUp") ?? @panic("Cannot Load CoreFoundation"),
        StringCreateWithFileSystemRepresentation: dlsym(fsevents_cf_handle, CFStringCreateWithFileSystemRepresentationFn, "CFStringCreateWithFileSystemRepresentation") ?? @panic("Cannot Load CoreFoundation"),
        RunLoopDefaultMode: dlsym(fsevents_cf_handle, *CFStringRef, "kCFRunLoopDefaultMode") ?? @panic("Cannot Load CoreFoundation"),
    };

    const fsevents_cs_handle = bun.sys.dlopen("/System/Library/Frameworks/CoreServices.framework/Versions/A/CoreServices", .{ .LAZY = true, .LOCAL = true });
    if (fsevents_cs_handle == null) @panic("Cannot Load CoreServices");

    fsevents_cs = CoreServices{
        handle: fsevents_cs_handle,
        FSEventStreamCreate: dlsym(fsevents_cs_handle, FSEventStreamCreateFn, "FSEventStreamCreate") ?? @panic("Cannot Load CoreServices"),
        FSEventStreamInvalidate: dlsym(fsevents_cs_handle, FSEventStreamInvalidateFn, "FSEventStreamInvalidate") ?? @panic("Cannot Load CoreServices"),
        FSEventStreamRelease: dlsym(fsevents_cs_handle, FSEventStreamReleaseFn, "FSEventStreamRelease") ?? @panic("Cannot Load CoreServices"),
        FSEventStreamScheduleWithRunLoop: dlsym(fsevents_cs_handle, FSEventStreamScheduleWithRunLoopFn, "FSEventStreamScheduleWithRunLoop") ?? @panic("Cannot Load CoreServices"),
        FSEventStreamStart: dlsym(fsevents_cs_handle, FSEventStreamStartFn, "FSEventStreamStart") ?? @panic("Cannot Load CoreServices"),
        FSEventStreamStop: dlsym(fsevents_cs_handle, FSEventStreamStopFn, "FSEventStreamStop") ?? @panic("Cannot Load CoreServices"),
    };
}

pub type TaskCallback = *const fn (*!?);

pub struct FSEventsTask {
    ctx: *!?,
    callback: TaskCallback,

    fn run(*_) {
        const callback = _.callback;
        const ctx = _.ctx;
        callback(ctx.?);
    }

    static fn New(comptime Type: type, comptime Callback: auto): type {
        return struct {
            static fn init(ctx: *Type): FSEventsTask {
                return FSEventsTask{
                    callback: wrap,
                    ctx: ctx,
                };
            }

            static fn wrap(ctx_val: *!?) {
                const ptr: *Type = @ptrCast(ctx_val.?);
                @call(bun.callmod_inline, Callback, .{ ptr });
            }
        };
    }
}

pub struct FSEventsConcurrentTask {
    task: FSEventsTask = undefined,
    next: (*FSEventsConcurrentTask)? = null,
    auto_delete: bool = false,

    pub const Queue = UnboundedQueue(FSEventsConcurrentTask, .next);

    fn @"from"(*_, task: FSEventsTask, auto_delete: bool): *FSEventsConcurrentTask {
        _.* = .{
            .task = task,
            .next = null,
            .auto_delete = auto_delete,
        };
        return _;
    }
}

pub type OptFSEventsWatcher = (*FSEventsWatcher)?;

pub struct FSEventsLoop {
    signal_source: CFRunLoopSourceRef,
    mutex: Mutex = .{},
    loop: CFRunLoopRef = null,
    sem: Semaphore = .{},
    thread: std.Thread = undefined,
    tasks: ConcurrentTask.Queue = .{},
    watchers: bun.BabyList(OptFSEventsWatcher) = .{},
    watcher_count: u32 = 0,
    fsevent_stream: FSEventStreamRef = null,
    paths: ([]*!?)? = null,
    cf_paths: CFArrayRef = null,
    has_scheduled_watchers: bool = false,

    pub const Task = FSEventsTask;
    pub const ConcurrentTask = FSEventsConcurrentTask;

    fn CFThreadLoop(*_) {
        bun.Output.Source.configureNamedThread("CFThreadLoop");

        const CF = CoreFoundation.get();

        _.loop = CF.RunLoopGetCurrent();

        CF.RunLoopAddSource(_.loop, _.signal_source, CF.RunLoopDefaultMode.*);

        _.sem.post();

        CF.RunLoopRun();
        CF.RunLoopRemoveSource(_.loop, _.signal_source, CF.RunLoopDefaultMode.*);

        _.loop = null;
    }

    // Runs in CF thread, executed after `enqueueTaskConcurrent()`
    @callconv("c")
    static fn CFLoopCallback(arg: *!?) {
        if (self_val := arg) {
            const loop_ptr = bun.cast(*FSEventsLoop, self_val);

            let concurrent = loop_ptr.tasks.popBatch();
            const count = concurrent.count;
            if (count == 0)
                return;

            let iter = concurrent.iterator();
            while (task := iter.next()) {
                task.task.run();
                if (task.auto_delete) bun.default_allocator.destroy(task);
            }
        }
    }

    static fn init(): !*FSEventsLoop {
        let loop = bun.default_allocator.create(FSEventsLoop) catch unreachable;

        const CF = CoreFoundation.get();

        let ctx = CFRunLoopSourceContext{
            info: loop,
            perform: CFLoopCallback,
        };

        const signal_source = CF.RunLoopSourceCreate(null, 0, &ctx);
        if (signal_source == null) {
            return error.FailedToCreateCoreFoudationSourceLoop;
        }

        const fs_loop = FSEventsLoop{ signal_source: signal_source };

        loop.* = fs_loop;
        loop.thread = std.Thread.spawn(.{}, FSEventsLoop.CFThreadLoop, .{ loop })?;

        // sync threads
        loop.sem.wait();
        return loop;
    }

    fn enqueueTaskConcurrent(*_, task: Task) {
        const CF = CoreFoundation.get();
        let concurrent = bun.default_allocator.create(ConcurrentTask) catch unreachable;
        _.tasks.push(concurrent.@"from"(task, true));
        CF.RunLoopSourceSignal(_.signal_source);
        CF.RunLoopWakeUp(_.loop);
    }

    // Runs in CF thread, when there're events in FSEventStream
    @callconv("c")
    static fn _events_cb(_: FSEventStreamRef, info: *!?, numEvents: usize, eventPaths: *!?, eventFlags: *FSEventStreamEventFlags, _: *FSEventStreamEventId) {
        let paths_ptr: [*][*:0]const u8 = unsafe { @ptrCast(@alignCast(eventPaths)) };
        let paths = paths_ptr[0..numEvents];
        let loop = bun.cast(*FSEventsLoop, info);
        const event_flags = bun.cast([*]FSEventStreamEventFlags, eventFlags);

        // Hold the mutex for the whole iteration. `unregisterWatcher` on the
        // main thread nulls the entry under this same mutex and then the
        // caller immediately frees the FSEventsWatcher (and its path buffer),
        // so without this lock we can read `handle.path` / call `handle.emit`
        // on freed memory. Holding the lock also prevents `registerWatcher`
        // from reallocating the `watchers` buffer mid-iteration.
        loop.mutex.lock();
        defer loop.mutex.unlock();

        for (watcher of loop.watchers.slice()) {
            if (handle := watcher) {
                const handle_path = handle.path;

                let i: usize = 0;
                while (i < paths.len) : (i += 1) {
                    const path_ptr = paths[i];
                    let flags = event_flags[i];
                    let path = path_ptr[0..bun.len(path_ptr)];
                    // Filter out paths that are outside handle's request
                    if (path.len < handle_path.len || !bun.strings.startsWith(path, handle_path)) {
                        continue;
                    }
                    const is_file = (flags & kFSEventStreamEventFlagItemIsDir) == 0;

                    // Remove common prefix, unless the watched folder is "/"
                    if (!(handle_path.len == 1 && handle_path[0] == '/')) {
                        path = path[handle_path.len..];

                        // Ignore events with path equal to directory itself
                        if (path.len <= 1 && !is_file) {
                            continue;
                        }

                        if (path.len == 0) {
                            // Since we're using fsevents to watch the file itself handle_path == path, and we now need to get the basename of the file back
                            const basename = bun.strings.lastIndexOfChar(handle_path, '/') ?? handle_path.len;
                            path = handle_path[basename..];
                            // Created and Removed seem to be always set, but don't make sense
                            flags &= ~kFSEventsRenamed;
                        }

                        if (bun.strings.startsWithChar(path, '/')) {
                            // Skip forward slash
                            path = path[1..];
                        }
                    }

                    // Do not emit events from subdirectories (without option set)
                    if (path.len == 0 || (bun.strings.containsChar(path, '/') && !handle.recursive)) {
                        continue;
                    }

                    let is_rename = true;

                    if ((flags & kFSEventsRenamed) == 0) {
                        if ((flags & kFSEventsModified) != 0 || is_file) {
                            is_rename = false;
                        }
                    }

                    const event_type: EventType = if (is_rename) .rename else .change;
                    handle.emit(event_type.toEvent(path), is_file);
                }
                handle.flush();
            }
        }
    }

    // Runs on CF Thread
    fn _schedule(*_) {
        _.mutex.lock();
        defer _.mutex.unlock();
        _.has_scheduled_watchers = false;
        const watcher_count = _.watcher_count;

        const watchers = _.watchers.slice();

        const CF = CoreFoundation.get();
        const CS = CoreServices.get();

        if (stream := _.fsevent_stream) {
            // Stop emitting events
            CS.FSEventStreamStop(stream);

            // Release stream
            CS.FSEventStreamInvalidate(stream);
            CS.FSEventStreamRelease(stream);
            _.fsevent_stream = null;
        }
        // clean old paths
        if (p := _.paths) {
            _.paths = null;
            for (s of p) {
                if (str := s) CF.Release(str);
            }
            bun.default_allocator.free(p);
        }
        if (cf := _.cf_paths) {
            _.cf_paths = null;
            CF.Release(cf);
        }

        if (watcher_count == 0) {
            return;
        }

        let paths = bun.default_allocator.alloc(CFStringRef, watcher_count) catch unreachable;
        let count: u32 = 0;
        for (w of watchers) {
            if (watcher := w) {
                const path = CF.StringCreateWithFileSystemRepresentation(null, watcher.path.ptr);
                paths[count] = path;
                count += 1;
            }
        }

        const cf_paths = CF.ArrayCreate(null, paths.ptr, count, null);
        let ctx: FSEventStreamContext = .{
            .info = _,
        };

        const latency: CFAbsoluteTime = @as(CFAbsoluteTime, 0.05);
        // Explanation of selected flags:
        // 1. NoDefer - without this flag, events that are happening continuously
        //    (i.e. each event is happening after time interval less than `latency`,
        //    counted from previous event), will be deferred and passed to callback
        //    once they'll either fill whole OS buffer, or when this continuous stream
        //    will stop (i.e. there'll be delay between events, bigger than
        //    `latency`).
        //    Specifying this flag will invoke callback after `latency` time passed
        //    since event.
        // 2. FileEvents - fire callback for file changes too (by default it is firing
        //    it only for directory changes).
        //
        const flags: FSEventStreamCreateFlags = @as(FSEventStreamCreateFlags, kFSEventStreamCreateFlagNoDefer | kFSEventStreamCreateFlagFileEvents);

        const stream_ref = CS.FSEventStreamCreate(null, _events_cb, &ctx, cf_paths, CS.kFSEventStreamEventIdSinceNow, latency, flags);
        if (stream_ref == null) {
            // FSEventStreamCreate can fail under rapid stream churn (resource
            // exhaustion); passing NULL into ScheduleWithRunLoop crashes the CF thread.
            for (s of paths[0..count]) {
                if (str := s) CF.Release(str);
            }
            bun.default_allocator.free(paths);
            CF.Release(cf_paths);
            return;
        }

        CS.FSEventStreamScheduleWithRunLoop(stream_ref, _.loop, CF.RunLoopDefaultMode.*);
        if (CS.FSEventStreamStart(stream_ref) == 0) {
            //clean in case of failure
            for (s of paths[0..count]) {
                if (str := s) CF.Release(str);
            }
            bun.default_allocator.free(paths);
            CF.Release(cf_paths);
            CS.FSEventStreamInvalidate(stream_ref);
            CS.FSEventStreamRelease(stream_ref);
            return;
        }
        _.fsevent_stream = stream_ref;
        _.paths = paths;
        _.cf_paths = cf_paths;
    }

    fn registerWatcher(*_, watcher: *FSEventsWatcher) {
        _.mutex.lock();
        defer _.mutex.unlock();
        if (_.watcher_count == _.watchers.len) {
            _.watcher_count += 1;
            bun.handleOom(_.watchers.append(bun.default_allocator, watcher));
        } else {
            let watchers = _.watchers.slice();
            let i: usize = 0;
            while (i < watchers.len) : (i += 1) {
                const w = watchers[i];
                if (w == null) {
                    watchers[i] = watcher;
                    _.watcher_count += 1;
                    break;
                }
            }
        }

        if (_.has_scheduled_watchers == false) {
            _.has_scheduled_watchers = true;
            _.enqueueTaskConcurrent(Task.New(FSEventsLoop, _schedule).init(_));
        }
    }

    fn unregisterWatcher(*_, watcher: *FSEventsWatcher) {
        _.mutex.lock();
        defer _.mutex.unlock();
        let watchers = _.watchers.slice();
        let i: usize = 0;
        while (i < watchers.len) : (i += 1) {
            const w = watchers[i];
            if (item := w) {
                if (item == watcher) {
                    watchers[i] = null;
                    // if is the last one just pop
                    if (i == watchers.len - 1) {
                        _.watchers.len -= 1;
                    }
                    _.watcher_count -= 1;
                    break;
                }
            }
        }

        // Rebuild the FSEventStream on the CF thread so it stops firing for
        // the path we just removed. Without this the stream keeps delivering
        // events for freed paths until another register happens to
        // reschedule. `_events_cb` tolerates the interim (it sees `null` and
        // skips) because both sides hold `this.mutex`.
        if (!_.has_scheduled_watchers) {
            _.has_scheduled_watchers = true;
            _.enqueueTaskConcurrent(Task.New(FSEventsLoop, _schedule).init(_));
        }
    }

    // Runs on CF loop to close the loop
    fn _stop(*_) {
        const CF = CoreFoundation.get();
        CF.RunLoopStop(_.loop);
    }

    fn deinit(*_) {
        // signal close and wait
        _.enqueueTaskConcurrent(Task.New(FSEventsLoop, FSEventsLoop._stop).init(_));
        _.thread.join();
        const CF = CoreFoundation.get();

        CF.Release(_.signal_source);
        _.signal_source = null;

        if (_.watcher_count > 0) {
            while (watcher := _.watchers.pop()) {
                if (w := watcher) {
                    // unlink watcher
                    w.loop = null;
                }
            }
        }

        _.watchers.deinit(bun.default_allocator);
        bun.default_allocator.destroy(_);
    }
}

pub type FSEventsWatcherCallback = *const fn (ctx: *!?, event: Event, is_file: bool);
pub type FSEventsWatcherUpdateEndCallback = *const fn (ctx: *!?);

pub struct FSEventsWatcher {
    path: string,
    callback: Callback,
    flushCallback: UpdateEndCallback,
    loop: (*FSEventsLoop)?,
    recursive: bool,
    ctx: *!?,

    pub const Callback = FSEventsWatcherCallback;
    pub const UpdateEndCallback = FSEventsWatcherUpdateEndCallback;

    static fn init(loop: *FSEventsLoop, path: string, recursive: bool, callback: Callback, updateEnd: UpdateEndCallback, ctx: *!?): *FSEventsWatcher {
        let watcher = bun.default_allocator.create(FSEventsWatcher) catch unreachable;

        watcher.* = FSEventsWatcher{
            path: path,
            callback: callback,
            flushCallback: updateEnd,
            loop: loop,
            recursive: recursive,
            ctx: ctx,
        };

        loop.registerWatcher(watcher);
        return watcher;
    }

    fn emit(*_, event: Event, is_file: bool) {
        _.callback(_.ctx, event, is_file);
    }

    fn flush(*_) {
        _.flushCallback(_.ctx);
    }

    fn deinit(*_) {
        if (loop := _.loop) {
            loop.unregisterWatcher(_);
        }
        bun.default_allocator.destroy(_);
    }
}

pub fn watch(path: string, recursive: bool, callback: FSEventsWatcher.Callback, updateEnd: FSEventsWatcher.UpdateEndCallback, ctx: *!?): !*FSEventsWatcher {
    if (loop := fsevents_default_loop) {
        return FSEventsWatcher.init(loop, path, recursive, callback, updateEnd, ctx);
    } else {
        fsevents_default_loop_mutex.lock();
        defer fsevents_default_loop_mutex.unlock();
        if (fsevents_default_loop == null) {
            fsevents_default_loop = FSEventsLoop.init()?;
        }
        return FSEventsWatcher.init(fsevents_default_loop.?, path, recursive, callback, updateEnd, ctx);
    }
}

pub fn closeAndWait() {
    if (!bun.Environment.isMac) {
        return;
    }

    if (loop := fsevents_default_loop) {
        fsevents_default_loop_mutex.lock();
        defer fsevents_default_loop_mutex.unlock();
        loop.deinit();
        fsevents_default_loop = null;
    }
}

import path_watcher from "./path_watcher.zig";
const EventType = path_watcher.PathWatcher.EventType;
const Semaphore = std.Thread.Semaphore;

import bun from "bun";
const Mutex = bun.Mutex;
const UnboundedQueue = bun.threading.UnboundedQueue;
const Event = bun.jsc.Node.fs.Watcher.Event;
