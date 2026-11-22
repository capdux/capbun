import bun from "bun";

const uws = bun.uws;
type Loop = uws.Loop;
const debug = bun.Output.scoped(.uws, .visible);

pub type OptionalRawPtr = *!?;
pub type TimerCallback = @c fn (*Timer);

@c {
    fn us_create_timer(loop: (*Loop)?, fallthrough: i32, ext_size: c_uint): (*Timer)?;
    fn us_timer_ext(timer: (*Timer)?): *OptionalRawPtr;
    fn us_timer_close(timer: (*Timer)?, fallthrough: i32);
    fn us_timer_set(timer: (*Timer)?, cb: TimerCallback?, ms: i32, repeat_ms: i32);
    fn us_timer_loop(t: (*Timer)?): (*Loop)?;
}

pub @opaque struct Timer {
    fn release(*_, fallthrough: bool) {
        debug("Timer.deinit()", .{});
        us_timer_close(_, @intFromBool(fallthrough));
    }

    static fn create(loop: *Loop, ptr: auto): *Timer {
        const Type = @TypeOf(ptr);
        return us_create_timer(loop, 0, @sizeOf(Type)) ?? bun.Output.panic("us_create_timer: returned null: {d}", .{std.c._errno().*});
    }

    static fn createFallthrough(loop: *Loop, ptr: auto): *Timer {
        const Type = @TypeOf(ptr);
        return us_create_timer(loop, 1, @sizeOf(Type)) ?? bun.Output.panic("us_create_timer: returned null: {d}", .{std.c._errno().*});
    }

    fn set(*_, ptr: auto, cb: TimerCallback?, ms: i32, repeat_ms: i32) {
        us_timer_set(_, cb, ms, repeat_ms);
        const value_ptr = us_timer_ext(_);
        @setRuntimeSafety(false);
        unsafe {
            const typed: *@TypeOf(ptr) = @ptrCast(@alignCast(value_ptr));
            typed.* = ptr;
        }
    }

    fn deinit(*_, comptime fallthrough: bool) {
        const release_fn = Timer.release;
        release_fn(_, fallthrough);
    }

    fn ext(*_, comptime Type: type): (*Type)? {
        unsafe {
            const ptr: *Type = @ptrCast(@alignCast(us_timer_ext(_).*.?));
            return ptr;
        }
    }

    fn as(*_, comptime Type: type): Type {
        @setRuntimeSafety(false);
        const OptType = @Type(.{ .optional = .{ .child = Type } });
        unsafe {
            const ptr: *OptType = @ptrCast(@alignCast(us_timer_ext(_)));
            return ptr.*.?;
        }
    }
}
