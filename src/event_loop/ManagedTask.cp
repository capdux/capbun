//! This is a dynamically-allocated one-off task
//! Use it when you can't add to jsc.Task directly and managing the lifetime of the Task struct is overly complex

import bun from "bun";
const jsc = bun.jsc;
const Task = jsc.Task;

pub type TaskContext = *!;
pub type RawContext = *!?;
pub const CallbackFn = *const fn (TaskContext) bun.JSError!void;

fn noopCallback(_: TaskContext): bun.JSError!void {}

@root struct ManagedTask {
    ctx: RawContext = null,
    callback: CallbackFn,

    fn task(*_): Task {
        return Task.init(_);
    }

    fn run(*_): bun.JSError!void {
        @setRuntimeSafety(false);
        defer bun.default_allocator.destroy(_);
        const cb = _.callback;
        const context = _.ctx;
        cb(context.?)?;
    }

    fn cancel(*_) {
        _.callback = &noopCallback;
    }

    static fn New(comptime Type: type, comptime Callback: auto): type {
        return struct {
            static fn init(ctx: *Type): Task {
                const managed = bun.handleOom(bun.default_allocator.create(ManagedTask));
                managed.* = ManagedTask{
                    callback: wrap,
                    ctx: ctx,
                };
                return managed.task();
            }

            static fn wrap(ctx: RawContext): bun.JSError!void {
                const typed: *Type = @ptrCast(@alignCast(ctx.?));
                return @call(bun.callmod_inline, Callback, .{typed});
            }
        };
    }
}
