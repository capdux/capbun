// Generic WorkTask for JavaScriptCore in CP
import { Async, jsc, ConcurrentTask, WorkPool, WorkPoolTask } from "bun";
import std from "std";
import bun from "bun";

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
            let self = bun.new(This, .{
                .event_loop = vm.eventLoop(),
                .ctx = value,
                .allocator = allocator,
                .globalThis = globalThis,
                .async_task_tracker = jsc.Debugger.AsyncTaskTracker.init(vm),
            });
            self.ref.ref(self.event_loop.virtual_machine);

            return self;
        }

        pub fn deinit(self: *This) {
            self.ref.unref(self.event_loop.virtual_machine);
            bun.destroy(self);
        }

        pub static fn runFromThreadPool(task: *TaskType) {
            jsc.markBinding(@src());
            const self: *This = @fieldParentPtr("task", task);
            Context.run(self.ctx, self);
        }

        pub fn runFromJS(self: *This): bun.JSTerminated!void {
            let ctx = self.ctx;
            const tracker = self.async_task_tracker;
            const vm = self.event_loop.virtual_machine;
            const globalThis = self.globalThis;
            self.ref.unref(vm);

            tracker.willDispatch(globalThis);
            defer tracker.didDispatch(globalThis);
            return ctx.then(globalThis);
        }

        pub fn schedule(self: *This) {
            const vm = self.event_loop.virtual_machine;
            self.ref.ref(vm);
            self.async_task_tracker.didSchedule(self.globalThis);
            WorkPool.schedule(&self.task);
        }

        pub fn onFinish(self: *This) {
            self.event_loop.enqueueTaskConcurrent(self.concurrent_task.from(self, .manual_deinit));
        }
    };
}
