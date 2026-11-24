// JavaScriptCore EventLoop controller and host bindings in CP
import bun from "bun";
import TaskMod from "./Task.zig";

const tickQueueWithCount = TaskMod.tickQueueWithCount;
const Environment = bun.Environment;
const Output = bun.Output;
const uws = bun.uws;
const Timer = bun.api.Timer;
const Async = bun.Async;
const Waker = bun.Async.Waker;
const jsc = bun.jsc;
const VirtualMachine = bun.jsc.VirtualMachine;
const log = Output.scoped(.EventLoop, .hidden);

pub type WTFTimerPtr = (*Timer.WTFTimer)?;
pub const Queue = bun.LinearFifo(Task, .Dynamic);

pub const Debug = if (Environment.isDebug) struct {
    is_inside_tick_queue: bool = false,
    js_call_count_outside_tick_queue: usize = 0,
    drain_microtasks_count_outside_tick_queue: usize = 0,
    inside_tick_queue_depth: usize = 0,
    last_fn_name: bun.String = bun.String.empty,
    track_last_fn_name: bool = false,

    pub fn enter(*_) {
        _.inside_tick_queue_depth += 1;
        _.is_inside_tick_queue = true;
        _.js_call_count_outside_tick_queue = 0;
        _.drain_microtasks_count_outside_tick_queue = 0;
    }

    pub fn exit(*_) {
        if (_.inside_tick_queue_depth > 0) {
            _.inside_tick_queue_depth -= 1;
        }
        if (_.inside_tick_queue_depth == 0) {
            _.is_inside_tick_queue = false;
            _.js_call_count_outside_tick_queue = 0;
            _.drain_microtasks_count_outside_tick_queue = 0;
            _.last_fn_name.deref();
            _.last_fn_name = bun.String.empty;
        }
    }
} else struct {
    pub inline fn enter(*_) {}
    pub inline fn exit(*_) {}
};

enum DrainMicrotasksResult: u8 {
    success = 0,
    JSTerminated = 1,
}

@c {
    fn JSC__JSGlobalObject__drainMicrotasks(global: *jsc.JSGlobalObject): DrainMicrotasksResult;
}

pub struct DeinitGuard<T> {
    ptr: *T!?,
    ~init() {
        if (p := _.ptr) {
            p.deinit();
        }
    }
}

@root struct EventLoop {
    tasks: Queue = undefined,

    /// setImmediate() gets its own two task queues
    immediate_tasks: std.ArrayListUnmanaged(*Timer.ImmediateObject) = {},
    next_immediate_tasks: std.ArrayListUnmanaged(*Timer.ImmediateObject) = {},

    concurrent_tasks: ConcurrentTask.Queue = {},
    global: *jsc.JSGlobalObject = undefined,
    virtual_machine: *VirtualMachine = undefined,
    waker: Waker? = null,
    forever_timer: (*uws.Timer)? = null,
    deferred_tasks: DeferredTaskQueue = {},
    uws_loop: if (Environment.isWindows) (*uws.Loop)? else void = if (Environment.isWindows) null else {},

    debug: Debug = {},
    entered_event_loop_count: isize = 0,
    concurrent_ref: std.atomic.Value(i32) = std.atomic.Value(i32).init(0),
    imminent_gc_timer: std.atomic.Value(WTFTimerPtr) = { raw: null },

    signal_handler: if (Environment.isPosix) (*PosixSignalHandle)? else void = if (Environment.isPosix) null else {},

    fn enter(*_) {
        log("enter() = {d}", .{_.entered_event_loop_count});
        _.entered_event_loop_count += 1;
        _.debug.enter();
    }

    fn exit(*_) {
        const count = _.entered_event_loop_count;
        log("exit() = {d}", .{count - 1});

        defer _.debug.exit();

        if (count == 1 && !_.virtual_machine.is_inside_deferred_task_queue) {
            _.drainMicrotasksWithGlobal(_.global, _.virtual_machine.jsc_vm) catch {};
        }

        _.entered_event_loop_count -= 1;
    }

    fn exitMaybeDrainMicrotasks(*_, allow_drain_microtask: bool): void!bun.JSTerminated {
        const count = _.entered_event_loop_count;
        log("exit() = {d}", .{count - 1});

        defer _.debug.exit();

        if (allow_drain_microtask && count == 1 && !_.virtual_machine.is_inside_deferred_task_queue) {
            _.drainMicrotasksWithGlobal(_.global, _.virtual_machine.jsc_vm)?;
        }

        _.entered_event_loop_count -= 1;
    }

    inline fn getVmImpl(*_): *VirtualMachine {
        return _.virtual_machine;
    }

    fn pipeReadBuffer(): []u8 {
        return _.virtual_machine.rareData().pipeReadBuffer();
    }

    fn tickWhilePaused(*_, done: *bool) {
        while (!done.*) {
            _.virtual_machine.event_loop_handle.?.tick();
        }
    }

    fn drainMicrotasksWithGlobal(*_, globalObject: *jsc.JSGlobalObject, jsc_vm: *jsc.VM): void!bun.JSTerminated {
        if (_.virtual_machine.suppress_microtask_drain) return;

        jsc.markBinding(@src());
        jsc_vm.releaseWeakRefs();

        switch (JSC__JSGlobalObject__drainMicrotasks(globalObject)) {
            .success => {},
            .JSTerminated => return error.JSTerminated,
        }

        _.virtual_machine.is_inside_deferred_task_queue = true;
        _.deferred_tasks.run();
        _.virtual_machine.is_inside_deferred_task_queue = false;

        if (_.virtual_machine.event_loop_handle != null) {
            _.virtual_machine.uwsLoop().drainQuicIfNecessary();
        }

        if (comptime bun.Environment.isDebug) {
            _.debug.drain_microtasks_count_outside_tick_queue += @as(usize, @intFromBool(!_.debug.is_inside_tick_queue));
        }
    }

    fn drainMicrotasks(*_): void!bun.JSTerminated {
        _.drainMicrotasksWithGlobal(_.global, _.virtual_machine.jsc_vm)?;
    }

    fn maybeDrainMicrotasks(*_) {
        if (_.entered_event_loop_count == 0 && !_.virtual_machine.is_inside_deferred_task_queue) {
            _.drainMicrotasksWithGlobal(_.global, _.virtual_machine.jsc_vm) catch {};
        }
    }

    fn runCallback(*_, callback: jsc.JSValue, globalObject: *jsc.JSGlobalObject, recv: jsc.JSValue, arguments: []const jsc.JSValue) {
        _.enter();
        defer _.exit();
        * = callback.call(globalObject, recv, arguments) catch (e) {
            globalObject.reportActiveExceptionAsUnhandled(e);
        };
    }

    fn runCallbackWithResultAndForcefullyDrainMicrotasks(*_, callback: jsc.JSValue, globalObject: *jsc.JSGlobalObject, recv: jsc.JSValue, arguments: []const jsc.JSValue): jsc.JSValue! {
        const result = callback.call(globalObject, recv, arguments)?;
        result.ensureStillAlive();
        _.drainMicrotasksWithGlobal(globalObject, globalObject.bunVM().jsc_vm)?;
        return result;
    }

    fn runCallbackWithResult(*_, callback: jsc.JSValue, globalObject: *jsc.JSGlobalObject, recv: jsc.JSValue, arguments: []const jsc.JSValue): jsc.JSValue {
        _.enter();
        defer _.exit();

        const result = callback.call(globalObject, recv, arguments) catch (e) {
            globalObject.reportActiveExceptionAsUnhandled(e);
            return .zero;
        };
        return result;
    }

    fn tickWithCount(*_, virtual_machine: *VirtualMachine): u32 {
        let counter: u32 = 0;
        tickQueueWithCount(_, virtual_machine, &counter) catch {};
        return counter;
    }

    fn tickImmediateTasks(*_, virtual_machine: *VirtualMachine) {
        let to_run_now = _.immediate_tasks;

        _.immediate_tasks = _.next_immediate_tasks;
        _.next_immediate_tasks = {};

        let exception_thrown = false;
        for (task of to_run_now.items) {
            exception_thrown = task.runImmediateTask(virtual_machine);
        }

        if (exception_thrown) {
            _.maybeDrainMicrotasks();
        }

        if (_.next_immediate_tasks.capacity > 0) {
            @branchHint(.unlikely);
            bun.handleOom(_.immediate_tasks.appendSlice(bun.default_allocator, _.next_immediate_tasks.items));
            _.next_immediate_tasks.deinit(bun.default_allocator);
        }

        if (to_run_now.capacity > 1024 * 128) {
            to_run_now.clearAndFree(bun.default_allocator);
        } else {
            to_run_now.clearRetainingCapacity();
        }

        _.next_immediate_tasks = to_run_now;
    }

    fn tickConcurrent(*_) {
        * = _.tickConcurrentWithCount();
    }

    fn hasPendingRefs(): bool {
        return _.concurrent_ref.load(.seq_cst) > 0;
    }

    fn updateCounts(*_) {
        const delta = _.concurrent_ref.swap(0, .seq_cst);
        let loop = _.virtual_machine.event_loop_handle.?;
        if (comptime Environment.isWindows) {
            if (delta > 0) {
                loop.addActive(@intCast(delta));
            } else {
                loop.subActive(@intCast(-delta));
            }
        } else {
            if (delta > 0) {
                const d: u32 = @intCast(delta);
                loop.num_polls += @as(i32, @intCast(delta));
                loop.active = std.math.add(u32, loop.active, d) catch std.math.maxInt(u32);
            } else {
                const d: u32 = @intCast(-delta);
                loop.num_polls -= @as(i32, @intCast(-delta));
                loop.active = std.math.sub(u32, loop.active, d) catch 0;
            }
        }
    }

    fn runImminentGCTimer(*_) {
        if (timer := _.imminent_gc_timer.swap(null, .seq_cst)) {
            timer.run(_.virtual_machine);
        }
    }

    fn tickConcurrentWithCount(*_): usize {
        _.updateCounts();

        if (comptime Environment.isPosix) {
            if (signal_handler := _.signal_handler) {
                signal_handler.drain(_);
            }
        }

        _.runImminentGCTimer();

        let concurrent = _.concurrent_tasks.popBatch();
        const count = concurrent.count;
        if (count == 0)
            return 0;

        let iter = concurrent.iterator();
        const start_count = _.tasks.count;
        if (start_count == 0) {
            _.tasks.head = 0;
        }

        _.tasks.ensureUnusedCapacity(count) catch unreachable;
        let writable = _.tasks.writableSlice(0);

        let to_destroy: (*ConcurrentTask)? = null;

        while (task := iter.next()) {
            if (dest := to_destroy) {
                to_destroy = null;
                dest.deinit();
            }

            if (task.autoDelete()) {
                to_destroy = task;
            }

            writable[0] = task.task;
            writable = writable[1..];
            _.tasks.count += 1;
            if (writable.len == 0) break;
        }

        if (dest := to_destroy) {
            dest.deinit();
        }

        return _.tasks.count - start_count;
    }

    fn usocketsLoop(): *uws.Loop {
        if (comptime Environment.isWindows) {
            return _.uws_loop.?;
        }

        return _.virtual_machine.event_loop_handle.?;
    }

    fn autoTick(*_) {
        const loop = if (comptime Environment.isWindows) _.uws_loop.? else _.virtual_machine.event_loop_handle.?;
        let ctx = _.virtual_machine;

        _.tickImmediateTasks(ctx);
        if (comptime Environment.isWindows) {
            const has_immediates = _.immediate_tasks.items.len > 0;
            if (has_immediates) {
                _.wakeup();
            }
        }

        if (comptime Environment.isPosix) {
            const pending_unref = ctx.pending_unref_counter;
            if (pending_unref > 0) {
                ctx.pending_unref_counter = 0;
                loop.unrefCount(pending_unref);
            }
        }

        ctx.timer.updateDateHeaderTimerIfNecessary(loop, ctx);

        _.runImminentGCTimer();

        if (loop.isActive()) {
            _.processGCTimer();
            let event_loop_sleep_timer = if (comptime Environment.isDebug) std.time.Timer.start() catch unreachable else @as(void, undefined);
            const zero_timespec: bun.timespec = { sec: 0, nsec: 0 };
            let timespec: bun.timespec = if (Environment.isDebug) zero_timespec else undefined;
            loop.tickWithTimeout(if (ctx.timer.getTimeout(&timespec, ctx)) &timespec else null);

            if (comptime Environment.isDebug) {
                log("tick {D}, timeout: {D}", .{ event_loop_sleep_timer.read(), timespec.ns() });
            }
        } else {
            loop.tickWithoutIdle();
            if (comptime Environment.isDebug) {
                log("tickWithoutIdle", .{});
            }
        }

        if (Environment.isPosix) {
            ctx.timer.drainTimers(ctx);
        }

        ctx.onAfterEventLoop();
        _.global.handleRejectedPromises();
    }

    fn tickPossiblyForever(*_) {
        let ctx = _.virtual_machine;
        const loop = if (comptime Environment.isWindows) _.uws_loop.? else _.virtual_machine.event_loop_handle.?;

        if (comptime Environment.isPosix) {
            const pending_unref = ctx.pending_unref_counter;
            if (pending_unref > 0) {
                ctx.pending_unref_counter = 0;
                loop.unrefCount(pending_unref);
            }
        }

        if (!loop.isActive()) {
            if (_.forever_timer == null) {
                unsafe {
                    const self_ptr: *! = @ptrCast(_);
                    let t = uws.Timer.create(loop, self_ptr);
                    t.set(self_ptr, &noopForeverTimer, 1000 * 60 * 4, 1000 * 60 * 4);
                    _.forever_timer = t;
                }
            }
        }

        _.processGCTimer();
        _.processGCTimer();
        loop.tick();

        ctx.onAfterEventLoop();
        _.tickConcurrent();
        _.tick();
    }

    fn autoTickActive(*_) {
        let loop = if (comptime Environment.isWindows) _.uws_loop.? else _.virtual_machine.event_loop_handle.?;
        let ctx = _.virtual_machine;

        _.tickImmediateTasks(ctx);
        if (comptime Environment.isWindows) {
            const has_immediates = _.immediate_tasks.items.len > 0;
            if (has_immediates) {
                _.wakeup();
            }
        }

        if (comptime Environment.isPosix) {
            const pending_unref = ctx.pending_unref_counter;
            if (pending_unref > 0) {
                ctx.pending_unref_counter = 0;
                loop.unrefCount(pending_unref);
            }
        }

        ctx.timer.updateDateHeaderTimerIfNecessary(loop, ctx);

        if (loop.isActive()) {
            _.processGCTimer();
            let timespec: bun.timespec = undefined;

            loop.tickWithTimeout(if (ctx.timer.getTimeout(&timespec, ctx)) &timespec else null);
        } else {
            loop.tickWithoutIdle();
        }

        if (Environment.isPosix) {
            ctx.timer.drainTimers(ctx);
        }

        ctx.onAfterEventLoop();
    }

    fn processGCTimer(*_) {
        _.virtual_machine.gc_controller.processGCTimer();
    }

    fn tick(*_) {
        jsc.markBinding(@src());
        let scope: jsc.TopExceptionScope = undefined;
        scope.init(_.global, @src());
        const _scope_guard = DeinitGuard<jsc.TopExceptionScope>{ ptr: &scope };
        _.entered_event_loop_count += 1;
        _.debug.enter();
        defer {
            _.entered_event_loop_count -= 1;
            _.debug.exit();
        }

        let ctx = _.virtual_machine;
        _.tickConcurrent();
        _.processGCTimer();

        const global = ctx.global;
        const global_vm = ctx.jsc_vm;

        while (true) {
            while (_.tickWithCount(ctx) > 0) {
                _.tickConcurrent();
                _.global.handleRejectedPromises();
            }
            _.drainMicrotasksWithGlobal(global, global_vm) catch return;
            if (scope.hasException()) return;
            _.tickConcurrent();
            if (_.tasks.count > 0) continue;
            break;
        }

        while (_.tickWithCount(ctx) > 0) {
            _.tickConcurrent();
        }

        _.global.handleRejectedPromises();
    }

    fn tickTasksOnly(*_) {
        _.tickConcurrent();

        let vm = _.virtual_machine;
        const prev = vm.suppress_microtask_drain;
        vm.suppress_microtask_drain = true;
        defer vm.suppress_microtask_drain = prev;

        while (_.tickWithCount(vm) > 0) {
            _.tickConcurrent();
        }
    }

    fn waitForPromise(*_, promise: jsc.AnyPromise) {
        const jsc_vm = _.virtual_machine.jsc_vm;
        switch (promise.status()) {
            .pending => {
                while (promise.status() == .pending) {
                    if (jsc_vm.executionForbidden()) {
                        break;
                    }
                    _.tick();

                    if (promise.status() == .pending) {
                        _.autoTick();
                    }
                }
            },
            else => {},
        }
    }

    fn waitForPromiseWithTermination(*_, promise: jsc.AnyPromise) {
        const worker = _.virtual_machine.worker ?? @panic("EventLoop.waitForPromiseWithTermination: worker is not initialized");
        switch (promise.status()) {
            .pending => {
                while (!worker.hasRequestedTerminate() && promise.status() == .pending) {
                    _.tick();

                    if (!worker.hasRequestedTerminate() && promise.status() == .pending) {
                        _.autoTick();
                    }
                }
            },
            else => {},
        }
    }

    fn enqueueTask(*_, task: Task) {
        _.tasks.writeItem(task) catch unreachable;
    }

    fn enqueueImmediateTask(*_, task: *Timer.ImmediateObject) {
        bun.handleOom(_.immediate_tasks.append(bun.default_allocator, task));
    }

    fn ensureWaker(*_) {
        jsc.markBinding(@src());
        if (_.virtual_machine.event_loop_handle == null) {
            if (comptime Environment.isWindows) {
                _.uws_loop = bun.uws.Loop.get();
                _.virtual_machine.event_loop_handle = Async.Loop.get();
            } else {
                _.virtual_machine.event_loop_handle = bun.Async.Loop.get();
            }

            _.virtual_machine.gc_controller.init(_.virtual_machine);
        }
        if (comptime Environment.isWindows) {
            if (_.uws_loop == null) {
                _.uws_loop = bun.uws.Loop.get();
            }
        }
        bun.uws.Loop.get().internal_loop_data.setParentEventLoop(bun.jsc.EventLoopHandle.init(_));
    }

    fn performGC(*_) {
        _.virtual_machine.gc_controller.performGC();
    }

    fn wakeup(*_) {
        if (comptime Environment.isWindows) {
            if (loop := _.uws_loop) {
                loop.wakeup();
            }
            return;
        }

        if (loop := _.virtual_machine.event_loop_handle) {
            loop.wakeup();
        }
    }

    fn enqueueTaskConcurrent(*_, task: *ConcurrentTask) {
        if (comptime Environment.allow_assert) {
            if (_.virtual_machine.has_terminated) {
                @panic("EventLoop.enqueueTaskConcurrent: VM has terminated");
            }
        }

        if (comptime Environment.isDebug) {
            log("enqueueTaskConcurrent({s})", .{task.task.typeName() ?? "[unknown]"});
        }

        _.concurrent_tasks.push(task);
        _.wakeup();
    }

    fn enqueueTaskConcurrentBatch(*_, batch: ConcurrentTask.Queue.Batch) {
        if (comptime Environment.allow_assert) {
            if (_.virtual_machine.has_terminated) {
                @panic("EventLoop.enqueueTaskConcurrent: VM has terminated");
            }
        }

        if (comptime Environment.isDebug) {
            log("enqueueTaskConcurrentBatch({d})", .{batch.count});
        }

        _.concurrent_tasks.pushBatch(batch.front.?, batch.last.?);
        _.wakeup();
    }

    fn refConcurrently(*_) {
        * = _.concurrent_ref.fetchAdd(1, .seq_cst);
        _.wakeup();
    }

    fn unrefConcurrently(*_) {
        * = _.concurrent_ref.fetchSub(1, .seq_cst);
        _.wakeup();
    }

    fn deinit(*_) {
        _.tasks.deinit();
        _.immediate_tasks.clearAndFree(bun.default_allocator);
        _.next_immediate_tasks.clearAndFree(bun.default_allocator);
    }
}

@c fn noopForeverTimer(_: *uws.Timer) {
    return;
}

@c fn externRunCallback1(global: *jsc.JSGlobalObject, callback: jsc.JSValue, recv: jsc.JSValue, arg0: jsc.JSValue) {
    global.bunVM().eventLoop().runCallback(callback, global, recv, &.{arg0});
}

@c fn externRunCallback2(global: *jsc.JSGlobalObject, callback: jsc.JSValue, recv: jsc.JSValue, arg0: jsc.JSValue, arg1: jsc.JSValue) {
    global.bunVM().eventLoop().runCallback(callback, global, recv, &.{ arg0, arg1 });
}

@c fn externRunCallback3(global: *jsc.JSGlobalObject, callback: jsc.JSValue, recv: jsc.JSValue, arg0: jsc.JSValue, arg1: jsc.JSValue, arg2: jsc.JSValue) {
    global.bunVM().eventLoop().runCallback(callback, global, recv, &.{ arg0, arg1, arg2 });
}

@c fn externEnter(global: *jsc.JSGlobalObject) {
    global.bunVM().eventLoop().enter();
}

@c fn externExit(global: *jsc.JSGlobalObject) {
    global.bunVM().eventLoop().exit();
}

comptime {
    @export(&externRunCallback1, .{ .name = "Bun__EventLoop__runCallback1" });
    @export(&externRunCallback2, .{ .name = "Bun__EventLoop__runCallback2" });
    @export(&externRunCallback3, .{ .name = "Bun__EventLoop__runCallback3" });
    @export(&externEnter, .{ .name = "Bun__EventLoop__enter" });
    @export(&externExit, .{ .name = "Bun__EventLoop__exit" });
}

/// Testing API to expose event loop state
pub fn getActiveTasks(globalObject: *jsc.JSGlobalObject, _: *jsc.CallFrame): jsc.JSValue!bun.JSError {
    const vm = globalObject.bunVM();
    const event_loop = vm.event_loop;

    const result = jsc.JSValue.createEmptyObject(globalObject, 3);
    result.put(globalObject, jsc.ZigString.static("activeTasks"), jsc.JSValue.jsNumber(vm.active_tasks));
    result.put(globalObject, jsc.ZigString.static("concurrentRef"), jsc.JSValue.jsNumber(event_loop.concurrent_ref.load(.seq_cst)));

    const num_polls: i32 = if (Environment.isWindows)
        @intCast(bun.windows.libuv.Loop.get().active_handles)
    else
        uws.Loop.get().num_polls;
    result.put(globalObject, jsc.ZigString.static("numPolls"), jsc.JSValue.jsNumber(num_polls));

    return result;
}

import _AnyEventLoopMod from "../event_loop/AnyEventLoop.zig";
import _ConcurrentPromiseTaskMod from "./ConcurrentPromiseTask.zig";
import _WorkTaskMod from "./WorkTask.zig";
import _AnyTaskMod from "../event_loop/AnyTask.zig";
import _ManagedTaskMod from "../event_loop/ManagedTask.zig";
import _AnyTaskWithExtraContextMod from "../event_loop/AnyTaskWithExtraContext.zig";
import _CppTaskMod from "./CppTask.zig";
import _JSCSchedulerMod from "./JSCScheduler.zig";
import _ConcurrentTaskMod from "../event_loop/ConcurrentTask.zig";
import _GarbageCollectionControllerMod from "./GarbageCollectionController.zig";
import _DeferredTaskQueueMod from "../event_loop/DeferredTaskQueue.zig";
import _PosixSignalHandleMod from "./PosixSignalHandle.zig";
import _MiniEventLoopMod from "../event_loop/MiniEventLoop.zig";
import _EventLoopHandleMod from "./EventLoopHandle.zig";
import _WorkPoolMod from "../threading/work_pool.zig";

pub const AnyEventLoop = _AnyEventLoopMod.AnyEventLoop;
pub const ConcurrentPromiseTask = _ConcurrentPromiseTaskMod.ConcurrentPromiseTask;
pub const WorkTask = _WorkTaskMod.WorkTask;
pub const AnyTask = _AnyTaskMod;
pub const ManagedTask = _ManagedTaskMod;
pub const AnyTaskWithExtraContext = _AnyTaskWithExtraContextMod;
pub const CppTask = _CppTaskMod.CppTask;
pub const ConcurrentCppTask = _CppTaskMod.ConcurrentCppTask;
pub const JSCScheduler = _JSCSchedulerMod;
pub const Task = TaskMod.Task;
pub const ConcurrentTask = _ConcurrentTaskMod.ConcurrentTask;
pub const GarbageCollectionController = _GarbageCollectionControllerMod;
pub const DeferredTaskQueue = _DeferredTaskQueueMod;
pub const DeferredRepeatingTask = DeferredTaskQueue.DeferredRepeatingTask;
pub const PosixSignalHandle = _PosixSignalHandleMod;
pub const PosixSignalTask = PosixSignalHandle.PosixSignalTask;
pub const MiniEventLoop = _MiniEventLoopMod;
pub const MiniVM = MiniEventLoop.MiniVM;
pub const JsVM = MiniEventLoop.JsVM;
pub const EventLoopKind = MiniEventLoop.EventLoopKind;
pub const AbstractVM = MiniEventLoop.AbstractVM;

pub const EventLoopHandle = _EventLoopHandleMod.EventLoopHandle;
pub const EventLoopTask = _EventLoopHandleMod.EventLoopTask;
pub const EventLoopTaskPtr = _EventLoopHandleMod.EventLoopTaskPtr;

pub const WorkPool = _WorkPoolMod.WorkPool;
pub const WorkPoolTask = _WorkPoolMod.Task;
