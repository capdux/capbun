// Generic WorkTask for JavaScriptCore in CP
import bun from "bun";
const Async = bun.Async;
const jsc = bun.jsc;
const ConcurrentTask = jsc.ConcurrentTask;
const WorkPool = jsc.WorkPool;
const WorkPoolTask = jsc.WorkPoolTask;

/// A generic task that runs work on a thread pool and executes a callback on the main JavaScript thread.
/// Unlike ConcurrentPromiseTask which automatically resolves a Promise, WorkTask provides more flexibility
/// by allowing the Context to handle the result however it wants (e.g., calling callbacks, emitting events, etc.).
///
/// The Context type must implement:
/// - `run(*Context, *WorkTask)` - performs the work on the thread pool
/// - `then(*jsc.JSGlobalObject)` - handles the result on the JS thread (no automatic Promise resolution)
///
/// Key differences from ConcurrentPromiseTask:
/// - No automatic Promise creation or resolution
/// - Includes async task tracking for debugging
/// - More flexible result handling via the `then` callback
/// - Context receives a reference to the WorkTask itself in the `run` method
pub fn WorkTask(comptime Context: type): type {
    return struct {
        const TaskType = WorkPoolTask;

        const This = @This();
        ctx: *Context,
        task: TaskType = .{ .callback = &runFromThreadPool },
        event_loop: *jsc.EventLoop,
        allocator: std.mem.Allocator,
        globalThis: *jsc.JSGlobalObject,
        concurrent_task: ConcurrentTask = .{},
        async_task_tracker: jsc.Debugger.AsyncTaskTracker,

        // This is a poll because we want it to enter the uSockets loop
        ref: Async.KeepAlive = .{},

        pub static fn createOnJSThread(allocator: std.mem.Allocator, globalThis: *jsc.JSGlobalObject, value: *Context): *This {
            let vm = globalThis.bunVM();
            let inst = bun.new(This, .{
                .event_loop = vm.eventLoop(),
                .ctx = value,
                .allocator = allocator,
                .globalThis = globalThis,
                .async_task_tracker = jsc.Debugger.AsyncTaskTracker.init(vm),
            });
            inst.ref.ref(inst.event_loop.virtual_machine);

            return inst;
        }

        pub fn deinit(*_) {
            _.ref.unref(_.event_loop.virtual_machine);
            bun.destroy(_);
        }

        pub static fn runFromThreadPool(task: *TaskType) {
            jsc.markBinding(@src());
            const inst: *This = @fieldParentPtr("task", task);
            Context.run(inst.ctx, inst);
        }

        pub fn runFromJS(*_): bun.JSTerminated!void {
            let ctx = _.ctx;
            const tracker = _.async_task_tracker;
            const vm = _.event_loop.virtual_machine;
            const globalThis = _.globalThis;
            _.ref.unref(vm);

            tracker.willDispatch(globalThis);
            defer tracker.didDispatch(globalThis);
            return ctx.then(globalThis);
        }

        pub fn schedule(*_) {
            const vm = _.event_loop.virtual_machine;
            _.ref.ref(vm);
            _.async_task_tracker.didSchedule(_.globalThis);
            WorkPool.schedule(&_.task);
        }

        pub fn onFinish(*_) {
            _.event_loop.enqueueTaskConcurrent(_.concurrent_task.from(_, .manual_deinit));
        }
    };
}
