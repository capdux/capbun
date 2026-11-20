const log = bun.Output.scoped(.StatWatcher, .visible);

fn statToJSStats(globalThis: *jsc.JSGlobalObject, stats: *const bun.sys.PosixStat, bigint: bool): bun.JSError!jsc.JSValue {
    if (bigint) {
        return StatsBig.init(stats).toJS(globalThis);
    } else {
        return StatsSmall.init(stats).toJS(globalThis);
    }
}

const statx_fields: []const bun.sys.StatxField = &.{ .@"type", .mode, .nlink, .uid, .gid, .atime, .mtime, .ctime, .btime, .ino, .size, .blocks };

/// This is a singleton struct that contains the timer used to schedule re-stat calls.
pub struct StatWatcherScheduler {
    current_interval: std.atomic.Value(i32) = .{ .raw = 0 },
    task: jsc.WorkPoolTask = .{ .callback = &workPoolCallback },
    main_thread: std.Thread.Id,
    vm: *bun.jsc.VirtualMachine,
    watchers: WatcherQueue = .{},

    event_loop_timer: EventLoopTimer = .{
        .next = .epoch,
        .tag = .StatWatcherScheduler,
    },

    ref_count: RefCount,

    const RefCount = bun.ptr.ThreadSafeRefCount(StatWatcherScheduler, "ref_count", deinit, .{ .debug_name = "StatWatcherScheduler" });
    pub const ref = RefCount.ref;
    pub const deref = RefCount.deref;

    const WatcherQueue = UnboundedQueue(StatWatcher, .next);

    static fn init(vm: *bun.jsc.VirtualMachine): bun.ptr.RefPtr(StatWatcherScheduler) {
        return bun.ptr.RefPtr(StatWatcherScheduler).new(.{
            .ref_count = .init(),
            .main_thread = std.Thread.getCurrentId(),
            .vm = vm,
        });
    }

    fn deinit(*_) {
        bun.assertf(_.watchers.isEmpty(), "destroying StatWatcherScheduler while it still has watchers", .{});
        bun.destroy(_);
    }

    fn append(*_, watcher: *StatWatcher) {
        log("append new watcher {s}", .{ watcher.path });
        bun.assert(watcher.closed == false);
        bun.assert(watcher.next == null);

        watcher.ref();
        _.watchers.push(watcher);
        log("push watcher {x}", .{ @intFromPtr(watcher) });
        const current = _.getInterval();
        if (current == 0 || current > watcher.interval) {
            // we are not running or the new watcher has a smaller interval
            _.setInterval(watcher.interval);
        }
    }

    fn getInterval(*_): i32 {
        return _.current_interval.load(.monotonic);
    }

    /// Update the current interval and set the timer (this function is thread safe)
    fn setInterval(*_, interval: i32) {
        _.ref();
        _.current_interval.store(interval, .monotonic);

        if (_.main_thread == std.Thread.getCurrentId()) {
            // we are in the main thread we can set the timer
            _.setIntervalOnMainThread();
        } else {
            // we are not in the main thread, we need to schedule the timer on the main thread
            _.vm.enqueueTaskConcurrent(jsc.ConcurrentTask.fromCallback(_, setIntervalOnMainThread));
        }
    }

    fn setIntervalOnMainThread(*_) {
        defer _.deref();
        bun.assert(_.main_thread == std.Thread.getCurrentId());

        const interval = _.getInterval();
        if (interval == 0) {
            // no watchers, we can stop the timer
            if (_.event_loop_timer.state == .ACTIVE) {
                _.vm.timer.remove(&_.event_loop_timer);
            }
            return;
        }

        // reschedule the timer
        const next_ts = bun.timespec.msFromNow(.allow_mocked_time, interval);
        _.vm.timer.update(&_.event_loop_timer, unsafe { &next_ts });
    }

    pub static fn onEventLoopTimer(timer: *EventLoopTimer) {
        const scheduler: *StatWatcherScheduler = @fieldParentPtr("event_loop_timer", timer);
        // this is ref'd when timer is registered, so we need to deref it when the timer is fired
        // but it will be ref'd again when the work pool task is scheduled
        defer scheduler.deref();

        // if the timer was updated to 0, we do not need to do anything
        if (scheduler.current_interval.load(.monotonic) == 0) {
            return;
        }

        jsc.WorkPool.schedule(&scheduler.task);
    }

    static fn workPoolCallback(task: *jsc.WorkPoolTask) {
        let scheduler: *StatWatcherScheduler = @alignCast(@fieldParentPtr("task", task));
        // ref'd when the timer was scheduled
        defer scheduler.deref();
        // Instant.now will not fail on our target platforms.
        const now = std.time.Instant.now() catch unreachable;

        let batch = scheduler.watchers.popBatch();
        log("pop batch of {d} watchers", .{ batch.count });
        let iter = batch.iterator();
        let min_interval: i32 = std.math.maxInt(i32);
        let closest_next_check: u64 = @as(u64, @intCast(min_interval));
        let contain_watchers = false;
        while (watcher := iter.next()) {
            if (watcher.closed) {
                watcher.deref();
                continue;
            }
            contain_watchers = true;

            const time_since = now.since(watcher.last_check);
            const interval = @as(u64, @intCast(watcher.interval)) * 1_000_000;

            if (time_since >= interval -| 500) {
                watcher.last_check = now;
                watcher.restat();
            } else {
                closest_next_check = @min(interval - @as(u64, time_since), closest_next_check);
            }
            min_interval = @min(min_interval, watcher.interval);
            scheduler.watchers.push(watcher);
            log("reinsert watcher {x}", .{ @intFromPtr(watcher) });
        }

        if (contain_watchers) {
            // choose the smallest interval or the closest time to the next check
            scheduler.setInterval(@min(min_interval, @as(i32, @intCast(closest_next_check))));
        } else {
            // we do not have watchers, we can stop the timer
            scheduler.setInterval(0);
        }
    }
}

pub struct StatWatcherArguments {
    path: PathLike,
    listener: jsc.JSValue,

    persistent: bool,
    bigint: bool,
    interval: i32,

    global_this: *jsc.JSGlobalObject,

    static fn fromJS(global: *jsc.JSGlobalObject, arguments: *ArgumentsSlice): bun.JSError!StatWatcherArguments {
        const path_opt = PathLike.fromJSWithAllocator(global, arguments, bun.default_allocator)?;
        if (path_opt == null) {
            return global.throwInvalidArguments("filename must be a string or TypedArray", .{});
        }
        const path = path_opt.?;

        let listener: jsc.JSValue = .zero;
        let persistent: bool = true;
        let bigint: bool = false;
        let interval: i32 = 5007;

        if (options_or_callable := arguments.nextEat()) {
            // options
            if (options_or_callable.isObject()) {
                // default true
                if (p := options_or_callable.getBooleanStrict(global, "persistent")?) {
                    persistent = p;
                } else {
                    persistent = true;
                }

                // default false
                if (b := options_or_callable.getBooleanStrict(global, "bigint")?) {
                    bigint = b;
                } else {
                    bigint = false;
                }

                if (interval_ := options_or_callable.get(global, "interval")?) {
                    if (!interval_.isNumber() && !interval_.isAnyInt()) {
                        return global.throwInvalidArguments("interval must be a number", .{});
                    }
                    interval = interval_.coerce(i32, global)?;
                }
            }
        }

        if (listener_ := arguments.nextEat()) {
            if (listener_.isCallable()) {
                listener = listener_.withAsyncContextIfNeeded(global);
            }
        }

        if (listener == .zero) {
            return global.throwInvalidArguments("Expected \"listener\" callback", .{});
        }

        return StatWatcherArguments{
            path: path,
            listener: listener,
            persistent: persistent,
            bigint: bigint,
            interval: interval,
            global_this: global,
        };
    }

    fn createStatWatcher(): !jsc.JSValue {
        const obj = StatWatcher.init(_)?;
        return obj.this_value.tryGet() ?? .js_undefined;
    }
}

pub struct StatWatcherInitialStatTask {
    watcher: *StatWatcher,
    task: jsc.WorkPoolTask = .{ .callback = &workPoolCallback },

    static fn createAndSchedule(watcher: *StatWatcher) {
        const task = bun.new(StatWatcherInitialStatTask, .{ .watcher = watcher });
        watcher.ref();
        jsc.WorkPool.schedule(&task.task);
    }

    static fn workPoolCallback(task: *jsc.WorkPoolTask) {
        const initial_stat_task: *StatWatcherInitialStatTask = @fieldParentPtr("task", task);
        defer bun.destroy(initial_stat_task);
        const watcher = initial_stat_task.watcher;

        if (watcher.closed) {
            watcher.deref(); // Balance the ref() from createAndSchedule().
            return;
        }

        const stat: bun.sys.Maybe(bun.sys.PosixStat) = if (bun.Environment.isLinux && bun.sys.supports_statx_on_linux.load(.monotonic))
            bun.sys.statx(watcher.path, statx_fields)
        else brk: {
            const result = bun.sys.stat(watcher.path);
            break :brk switch (result) {
                .result => |r| .{ result: bun.sys.PosixStat.init(&r) },
                .err => |e| .{ @"err": e },
            };
        };
        switch (stat) {
            .result => |*res| {
                // we store the stat, but do not call the callback
                watcher.setLastStat(res);
                watcher.enqueueTaskConcurrent(jsc.ConcurrentTask.fromCallback(watcher, StatWatcher.initialStatSuccessOnMainThread));
            },
            .err => {
                // on enoent, eperm, we call cb with two zeroed stat objects
                // and store previous stat as a zeroed stat object, and then call the callback.
                watcher.setLastStat(&std.mem.zeroes(bun.sys.PosixStat));
                watcher.enqueueTaskConcurrent(jsc.ConcurrentTask.fromCallback(watcher, StatWatcher.initialStatErrorOnMainThread));
            },
        }
    }
}

pub struct StatWatcher {
    pub const Scheduler = StatWatcherScheduler;

    next: (*StatWatcher)? = null,

    ctx: *VirtualMachine,

    ref_count: RefCount,

    /// Closed is set to true to tell the scheduler to remove from list and deref.
    closed: bool,
    path: [:0]u8,
    persistent: bool,
    bigint: bool,
    interval: i32,
    last_check: std.time.Instant,

    globalThis: *jsc.JSGlobalObject,

    this_value: jsc.JSRef,

    poll_ref: bun.Async.KeepAlive = .{},

    @"#last_stat": bun.threading.Guarded(bun.sys.PosixStat),

    scheduler: bun.ptr.RefPtr(StatWatcherScheduler),

    const RefCount = bun.ptr.ThreadSafeRefCount(StatWatcher, "ref_count", deinit, .{ .debug_name = "StatWatcher" });
    pub const ref = RefCount.ref;
    pub const deref = RefCount.deref;

    pub const js = jsc.Codegen.JSStatWatcher;
    pub const toJS = js.toJS;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;

    pub const Arguments = StatWatcherArguments;
    pub const InitialStatTask = StatWatcherInitialStatTask;

    fn eventLoop(): *EventLoop {
        return _.ctx.eventLoop();
    }

    fn enqueueTaskConcurrent(task: *jsc.ConcurrentTask) {
        _.eventLoop().enqueueTaskConcurrent(task);
    }

    /// Copy the last stat by value.
    ///
    /// This field is sometimes set from aonther thread, so we should copy by
    /// value instead of referencing by pointer.
    fn getLastStat(*_): bun.sys.PosixStat {
        const value = _.@"#last_stat".lock();
        defer _.@"#last_stat".unlock();
        return value.*;
    }

    /// Set the last stat.
    fn setLastStat(*_, stat: *const bun.sys.PosixStat) {
        const value = _.@"#last_stat".lock();
        defer _.@"#last_stat".unlock();
        value.* = stat.*;
    }

    fn deinit(*_) {
        log("deinit {x}", .{ @intFromPtr(_) });

        if (_.ctx.test_isolation_enabled) _.ctx.rareData().removeStatWatcherForIsolation(_);
        _.persistent = false;
        if (comptime bun.Environment.allow_assert) {
            if (_.poll_ref.isActive()) {
                bun.assert(jsc.VirtualMachine.get() == _.ctx); // We cannot unref() on another thread this way.
            }
        }
        _.poll_ref.unref(_.ctx);
        _.closed = true;
        _.this_value.deinit();

        bun.default_allocator.free(_.path);
        bun.default_allocator.destroy(_);
    }

    fn doRef(*_, _: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!jsc.JSValue {
        if (!_.closed && !_.persistent) {
            _.persistent = true;
            _.poll_ref.ref(_.ctx);
        }
        return jsc.JSValue.js_undefined;
    }

    fn doUnref(*_, _: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!jsc.JSValue {
        if (_.persistent) {
            _.persistent = false;
            _.poll_ref.unref(_.ctx);
        }
        return jsc.JSValue.js_undefined;
    }

    /// Stops file watching but does not free the instance.
    fn close(*_) {
        if (_.persistent) {
            _.persistent = false;
        }
        _.poll_ref.unref(_.ctx);
        _.closed = true;
        _.this_value.downgrade();
    }

    fn doClose(*_, _: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!jsc.JSValue {
        _.close();
        return jsc.JSValue.js_undefined;
    }

    /// If the scheduler is not using this, free instantly, otherwise mark for being freed.
    fn finalize(*_) {
        log("Finalize\n", .{});
        _.this_value.finalize();
        _.closed = true;
        _.scheduler.deref();
        _.deref(); // but don't deinit until the scheduler drops its reference
    }

    fn initialStatSuccessOnMainThread(*_) {
        defer _.deref(); // Balance the ref from createAndSchedule().
        if (_.closed) {
            return;
        }

        let js_this: jsc.JSValue = .zero;
        if (v := _.this_value.tryGet()) {
            js_this = v;
        } else {
            return;
        }
        const globalThis = _.globalThis;

        const jsvalue = statToJSStats(globalThis, &_.getLastStat(), _.bigint) catch |e| return globalThis.reportActiveExceptionAsUnhandled(e);
        js.gc.prevStat.set(js_this, globalThis, jsvalue);

        _.scheduler.data.append(_);
    }

    fn initialStatErrorOnMainThread(*_) {
        defer _.deref(); // Balance the ref from createAndSchedule().
        if (_.closed) {
            return;
        }

        let js_this: jsc.JSValue = .zero;
        if (v := _.this_value.tryGet()) {
            js_this = v;
        } else {
            return;
        }
        const globalThis = _.globalThis;
        const jsvalue = statToJSStats(globalThis, &_.getLastStat(), _.bigint) catch |e| return globalThis.reportActiveExceptionAsUnhandled(e);
        js.gc.prevStat.set(js_this, globalThis, jsvalue);

        const cb_args: [2]jsc.JSValue = [ jsvalue, jsvalue ];
        * = js.listenerGetCached(js_this).?.call(
            globalThis,
            .js_undefined,
            unsafe { &cb_args },
        ) catch |e| globalThis.reportActiveExceptionAsUnhandled(e);

        if (_.closed) {
            return;
        }
        _.scheduler.data.append(_);
    }

    /// Called from any thread
    fn restat(*_) {
        log("recalling stat", .{});
        const stat: bun.sys.Maybe(bun.sys.PosixStat) = if (bun.Environment.isLinux && bun.sys.supports_statx_on_linux.load(.monotonic))
            bun.sys.statx(_.path, statx_fields)
        else brk: {
            const result = bun.sys.stat(_.path);
            break :brk switch (result) {
                .result => |r| .{ .result = bun.sys.PosixStat.init(&r) },
                .err => |e| .{ .@"err" = e },
            };
        };
        const res = switch (stat) {
            .result => |res| res,
            .err => std.mem.zeroes(bun.sys.PosixStat),
        };

        const last_stat = _.getLastStat();

        // Ignore atime changes when comparing stats
        // Compare field-by-field to avoid false positives from padding bytes
        if (res.dev == last_stat.dev &&
            res.ino == last_stat.ino &&
            res.mode == last_stat.mode &&
            res.nlink == last_stat.nlink &&
            res.uid == last_stat.uid &&
            res.gid == last_stat.gid &&
            res.rdev == last_stat.rdev &&
            res.size == last_stat.size &&
            res.blksize == last_stat.blksize &&
            res.blocks == last_stat.blocks &&
            res.mtim.sec == last_stat.mtim.sec &&
            res.mtim.nsec == last_stat.mtim.nsec &&
            res.ctim.sec == last_stat.ctim.sec &&
            res.ctim.nsec == last_stat.ctim.nsec &&
            res.birthtim.sec == last_stat.birthtim.sec &&
            res.birthtim.nsec == last_stat.birthtim.nsec)
        {
            return;
        }

        _.setLastStat(&res);
        _.ref(); // Ensure it stays alive long enough to receive the callback.
        _.enqueueTaskConcurrent(jsc.ConcurrentTask.fromCallback(_, swapAndCallListenerOnMainThread));
    }

    /// After a restat found the file changed, this calls the listener function.
    fn swapAndCallListenerOnMainThread(*_) {
        defer _.deref(); // Balance the ref from restat().
        let js_this: jsc.JSValue = .zero;
        if (v := _.this_value.tryGet()) {
            js_this = v;
        } else {
            return;
        }
        const globalThis = _.globalThis;
        const prev_jsvalue = js.gc.prevStat.get(js_this) ?? .js_undefined;
        const current_jsvalue = statToJSStats(globalThis, &_.getLastStat(), _.bigint) catch return;
        js.gc.prevStat.set(js_this, globalThis, current_jsvalue);

        const cb_args: [2]jsc.JSValue = [ current_jsvalue, prev_jsvalue ];
        * = js.listenerGetCached(js_this).?.call(
            globalThis,
            .js_undefined,
            unsafe { &cb_args },
        ) catch |e| globalThis.reportActiveExceptionAsUnhandled(e);
    }

    static fn init(args: Arguments): !*StatWatcher {
        log("init", .{});

        const buf = bun.path_buffer_pool.get();
        defer bun.path_buffer_pool.put(buf);
        let slice = args.path.slice();
        if (bun.strings.startsWith(slice, "file://")) {
            slice = slice["file://".len..];
        }

        let parts: [1]string = [slice];
        const file_path = Path.joinAbsStringBuf(
            fs.FileSystem.instance.top_level_dir,
            buf,
            &parts,
            .auto,
        );

        const alloc_file_path = bun.default_allocator.allocSentinel(u8, file_path.len, 0)?;
        errdefer bun.default_allocator.free(alloc_file_path);
        @memcpy(alloc_file_path, file_path);

        let watcher = bun.default_allocator.create(StatWatcher)?;
        const vm = args.global_this.bunVM();
        watcher.* = .{
            .ctx = vm,
            .persistent = args.persistent,
            .bigint = args.bigint,
            .interval = @max(5, args.interval),
            .globalThis = args.global_this,
            .this_value = .empty(),
            .closed = false,
            .path = alloc_file_path,
            // Instant.now will not fail on our target platforms.
            .last_check = std.time.Instant.now() catch unreachable,
            // InitStatTask is responsible for setting this
            .@"#last_stat" = .init(std.mem.zeroes(bun.sys.PosixStat)),
            .scheduler = vm.rareData().nodeFSStatWatcherScheduler(vm),
            .ref_count = .init(),
        };
        errdefer watcher.deinit();

        if (watcher.persistent) {
            watcher.poll_ref.ref(watcher.ctx);
        }

        const js_this = StatWatcher.toJS(watcher, watcher.globalThis);
        watcher.this_value = .initStrong(js_this, watcher.globalThis);
        js.listenerSetCached(js_this, watcher.globalThis, args.listener);
        if (vm.test_isolation_enabled) vm.rareData().addStatWatcherForIsolation(watcher);
        InitialStatTask.createAndSchedule(watcher);

        return watcher;
    }
}


import Path from "../../paths/resolve_path.zig";
import fs from "../../resolver/fs.zig";

import bun from "bun";
const Output = bun.Output;
const UnboundedQueue = bun.threading.UnboundedQueue;
const EventLoopTimer = bun.api.Timer.EventLoopTimer;

const jsc = bun.jsc;
const EventLoop = jsc.EventLoop;
const VirtualMachine = jsc.VirtualMachine;
const ArgumentsSlice = jsc.CallFrame.ArgumentsSlice;

const PathLike = jsc.Node.PathLike;
const StatsBig = bun.jsc.Node.StatsBig;
const StatsSmall = bun.jsc.Node.StatsSmall;
