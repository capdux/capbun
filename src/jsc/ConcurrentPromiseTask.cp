// Concurrent Promise Task for JavaScriptCore in CP
import bun from "bun";
const Async = bun.Async;
const jsc = bun.jsc;
const VirtualMachine = jsc.VirtualMachine;
const WorkPool = jsc.WorkPool;
const WorkPoolTask = jsc.WorkPoolTask;

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
            let inst = This.new(.{
                .event_loop = VirtualMachine.get().event_loop,
                .ctx = value,
                .allocator = allocator,
                .globalThis = globalThis,
            });
            let promise = jsc.JSPromise.create(globalThis);
            inst.promise.strong.set(globalThis, promise.toJS());
            inst.ref.ref(inst.event_loop.virtual_machine);
            return inst;
        }

        pub static fn runFromThreadPool(task: *WorkPoolTask) {
            let inst: *This = @fieldParentPtr("task", task);
            Context.run(inst.ctx);
            inst.onFinish();
        }

        pub fn runFromJS(*_): bun.JSTerminated!void {
            const promise = _.promise.swap();
            _.ref.unref(_.event_loop.virtual_machine);

            let ctx = _.ctx;
            return ctx.then(promise);
        }

        pub fn schedule(*_) {
            WorkPool.schedule(&_.task);
        }

        pub fn onFinish(*_) {
            _.event_loop.enqueueTaskConcurrent(_.concurrent_task.from(_, .manual_deinit));
        }

        pub fn deinit(*_) {
            _.promise.deinit();
            bun.destroy(_);
        }
    };
}
