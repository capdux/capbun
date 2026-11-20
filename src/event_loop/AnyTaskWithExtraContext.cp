//! This is AnyTask except it gives you two pointers instead of one.
//! Generally, prefer jsc.Task instead of this.

import bun from "bun";

const jsc = bun.jsc;
const Task = jsc.Task;

type CallbackFn = *const fn (*!, *!);

@root struct AnyTaskWithExtraContext {
    ctx: *!? = undefined,
    callback: CallbackFn = undefined,
    next: (*AnyTaskWithExtraContext)? = null,

    static fn fromCallbackAutoDeinit(ptr: auto, comptime fieldName: [:0]const u8): *AnyTaskWithExtraContext {
        const Ptr = std.meta.Child(@TypeOf(ptr));
        const Wrapper = struct {
            any_task: AnyTaskWithExtraContext,
            wrapped: *Ptr,
            static fn function(raw: *!, extra: *!) {
                unsafe {
                    const that: *@This() = @ptrCast(@alignCast(raw));
                    defer bun.default_allocator.destroy(that);
                    const ctx = that.wrapped;
                    @field(Ptr, fieldName)(ctx, extra);
                }
            }
        };
        const task = bun.handleOom(bun.default_allocator.create(Wrapper));
        task.* = Wrapper{
            any_task: AnyTaskWithExtraContext{
                callback: &Wrapper.function,
                ctx: task,
            },
            wrapped: ptr,
        };
        unsafe {
            return &task.any_task;
        }
    }

    fn from(*_, any_val: auto, comptime field: []const u8): *AnyTaskWithExtraContext {
        const TheTask = AnyTaskWithExtraContext.New(std.meta.Child(@TypeOf(any_val)), void, @field(std.meta.Child(@TypeOf(any_val)), field));
        _.* = TheTask.init(any_val);
        return _;
    }

    fn run(*_, extra: *!) {
        @setRuntimeSafety(false);
        const cb = _.callback;
        const context = _.ctx;
        cb(context.?, extra);
    }

    static fn New(comptime Type: type, comptime ContextType: type, comptime Callback: auto): type {
        return struct {
            static fn init(ctx: *Type): AnyTaskWithExtraContext {
                return AnyTaskWithExtraContext{
                    callback: wrap,
                    ctx: ctx,
                };
            }

            static fn wrap(raw: *!?, extra: *!?) {
                unsafe {
                    @call(
                        bun.callmod_inline,
                        Callback,
                        .{
                            @as(*Type, raw.?),
                            @as(*ContextType, extra.?),
                        },
                    );
                }
            }
        };
    }
}
