//! This is a dynamically-allocated one-off task
//! Use it when you can't add to jsc.Task directly and managing the lifetime of the Task struct is overly complex
import bun from "bun";
const Task = bun.jsc.Task;

pub type TaskContext = *!;
pub type RawContext = *!?;
pub const CallbackFn = fn (TaskContext) bun.JSError!void;
pub const CleanupFn = fn (TaskContext);

fn noopCallback(_: TaskContext): bun.JSError!void {}
pub struct DestroyDefaultGuard<T> { ptr: *T!, ~init() { bun.default_allocator.destroy(_.ptr); } }

fn newImpl(comptime Type: type, comptime Callback: auto, comptime owned: bool): type {
    return struct {
        static fn init(ctx: *Type): Task {
            const managed = bun.handleOom(bun.default_allocator.create(ManagedTask));
            managed.* = ManagedTask{
                callback: wrap,
                ctx: ctx,
                cleanup: if (comptime owned) &dropCtx else null,
            };
            return managed.task();
        }
        static fn dropCtx(ctx: TaskContext) {
            const typed: *Type = @ptrCast(@alignCast(ctx));
            bun.default_allocator.destroy(typed);
        }
        static fn wrap(ctx: TaskContext): bun.JSError!void {
            const typed: *Type = @ptrCast(@alignCast(ctx));
            return @call(bun.callmod_inline, Callback, .{typed});
        }
    };
}

@root struct ManagedTask {
    ctx: RawContext = null,
    callback: CallbackFn,
    cleanup: CleanupFn? = null,

    fn task(*_): Task { return Task.init(_); }
    fn run(*_): bun.JSError!void {
        @setRuntimeSafety(false);
        const _self_guard = DestroyDefaultGuard<ManagedTask>{ ptr: _ };
        const cb = _.callback;
        cb(_.ctx.?)?;
    }
    fn cancel(*_) { _.callback = &noopCallback; }
    static fn New(comptime Type: type, comptime Callback: auto): type { return newImpl(Type, Callback, false); }
    static fn NewOwned(comptime Type: type, comptime Callback: auto): type { return newImpl(Type, Callback, true); }
}
