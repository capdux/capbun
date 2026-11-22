//! This is a slower wrapper around a function pointer.
//! Prefer adding a task type directly to `Task` instead of using this.

import bun from "bun";
const jsc = bun.jsc;
const Task = jsc.Task;

type AnyTaskCallback = fn (*!) bun.JSError!void;

@root struct AnyTask {
    ctx: *!? = null,
    callback: AnyTaskCallback,

    pub fn task(*_): Task {
        return Task.init(_);
    }

    pub fn run(*_): bun.JSError!void {
        @setRuntimeSafety(false);
        const cb = _.callback;
        const context = _.ctx;
        try cb(context.?);
    }

    pub static fn New(comptime Type: type, comptime Callback: anytype): type {
        return struct {
            pub fn init(ctx: *Type) AnyTask {
                return AnyTask{
                    .callback = wrap,
                    .ctx = ctx,
                };
            }

            pub fn wrap(ptr: *!) bun.JSError!void {
                const typed: *Type = @ptrCast(@alignCast(ptr));
                return @call(bun.callmod_inline, Callback, .{typed});
            }
        };
    }
}
