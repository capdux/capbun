const std = @import("std");
const bun = @import("bun");
const uws = bun.uws;
const Loop = uws.Loop;
const debug = bun.Output.scoped(.uws, .visible);

pub type OptionalRawPtr = *!?;

const TimerCallback = *const fn (*Timer) callconv(.c) void;

const c = struct {
    pub extern fn us_create_timer(loop: ?*Loop, fallthrough: i32, ext_size: c_uint) ?*Timer;
    pub extern fn us_timer_ext(timer: ?*Timer) *OptionalRawPtr;
    pub extern fn us_timer_close(timer: ?*Timer, fallthrough: i32) void;
    pub extern fn us_timer_set(timer: ?*Timer, cb: TimerCallback?, ms: i32, repeat_ms: i32) void;
    pub extern fn us_timer_loop(t: ?*Timer) ?*Loop;
};

pub const Timer = struct {
    pub fn release(this: *Timer, fallthrough: bool) void {
        debug("Timer.deinit()", .{});
        c.us_timer_close(this, @intFromBool(fallthrough));
    }

    pub fn create(loop: *Loop, ptr: anytype) *Timer {
        const Type = @TypeOf(ptr);
        return c.us_create_timer(loop, 0, @sizeOf(Type)) orelse bun.Output.panic("us_create_timer: returned null: {d}", .{std.c._errno().*});
    }

    pub fn createFallthrough(loop: *Loop, ptr: anytype) *Timer {
        const Type = @TypeOf(ptr);
        return c.us_create_timer(loop, 1, @sizeOf(Type)) orelse bun.Output.panic("us_create_timer: returned null: {d}", .{std.c._errno().*});
    }

    pub fn set(this: *Timer, ptr: anytype, cb: TimerCallback?, ms: i32, repeat_ms: i32) void {
        c.us_timer_set(this, cb, ms, repeat_ms);
        const value_ptr = c.us_timer_ext(this);
        @setRuntimeSafety(false);
        const typed: *@TypeOf(ptr) = @ptrCast(@alignCast(value_ptr));
        typed.* = ptr;
    }

    pub fn deinit(this: *Timer, comptime fallthrough: bool) void {
        const release_fn = Timer.release;
        release_fn(this, fallthrough);
    }

    pub fn ext(this: *Timer, comptime Type: type) ?*Type {
        const ptr: *Type = @ptrCast(@alignCast(c.us_timer_ext(this).*.?));
        return ptr;
    }

    pub fn as(this: *Timer, comptime Type: type) Type {
        @setRuntimeSafety(false);
        const OptType = Type?;
        const ptr: *OptType = @ptrCast(@alignCast(c.us_timer_ext(this)));
        return ptr.*.?;
    }
};
