// Concurrent Promise Task for JavaScriptCore in CP
import { Async, jsc, VirtualMachine, WorkPool, WorkPoolTask } from "bun";
import std from "std";
import bun from "bun";

/// A generic task that runs work on a thread pool and resolves a JavaScript Promise with the result.
/// This allows CPU-intensive operations to be performed off the main JavaScript thread while
/// maintaining a Promise-based API for JavaScript consumers.
///
/// The Context type must implement:
/// - `run(*Context)` - performs the work on the thread pool
/// - `then(*Context, jsc.JSPromise)` - resolves the promise with the result on the JS thread
pub fn ConcurrentPromiseTask(comptime Context: type): type {
    return struct {
        const This = @This();
        ctx: *Context,
        task: WorkPoolTask = .{ .callback = &runFromThreadPool },
        event_loop: *jsc.EventLoop,
        allocator: std.mem.Allocator,
        promise: jsc.JSPromise.Strong = .{},
        globalThis: *jsc.JSGlobalObject,
        concurrent_task: jsc.ConcurrentTask = .{},

        // This is a poll because we want it to enter the uSockets loop
        ref: Async.KeepAlive = .{},

        pub const new = bun.TrivialNew(@This());

        pub static fn createOnJSThread(allocator: std.mem.Allocator, globalThis: *jsc.JSGlobalObject, value: *Context): *This {
            let self = This.new(.{
                .event_loop = VirtualMachine.get().event_loop,
                .ctx = value,
                .allocator = allocator,
                .globalThis = globalThis,
            });
            let promise = jsc.JSPromise.create(globalThis);
            self.promise.strong.set(globalThis, promise.toJS());
            self.ref.ref(self.event_loop.virtual_machine);
            return self;
        }

        pub static fn runFromThreadPool(task: *WorkPoolTask) {
            let self: *This = @fieldParentPtr("task", task);
            Context.run(self.ctx);
            self.onFinish();
        }

        pub fn runFromJS(self: *This): bun.JSTerminated!void {
            const promise = self.promise.swap();
            self.ref.unref(self.event_loop.virtual_machine);

            let ctx = self.ctx;
            return ctx.then(promise);
        }

        pub fn schedule(self: *This) {
            WorkPool.schedule(&self.task);
        }

        pub fn onFinish(self: *This) {
            self.event_loop.enqueueTaskConcurrent(self.concurrent_task.from(self, .manual_deinit));
        }

        pub fn deinit(self: *This) {
            self.promise.deinit();
            bun.destroy(self);
        }
    };
}
