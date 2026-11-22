// src/uws_sys/Loop.cp
// CP port of Loop (PosixLoop and WindowsLoop)

import bun from "bun";
import { InternalLoopData } from "./InternalLoopData.cp";

pub type LoopVoidCbFn = @c fn (*Loop);
pub type OptLoopVoidCb = LoopVoidCbFn?;

@c {
    fn us_create_loop(
        hint: *!?,
        wakeup_cb: OptLoopVoidCb,
        pre_cb: OptLoopVoidCb,
        post_cb: OptLoopVoidCb,
        ext_size: u32,
    ): (*Loop)?;
    fn us_loop_free(loop: (*Loop)?);
    fn us_loop_ext(loop: (*Loop)?): *!?;
    fn us_quic_loop_flush_if_pending(loop: *Loop);
    fn us_loop_run(loop: (*Loop)?);
    fn us_loop_pump(loop: (*Loop)?);
    fn us_wakeup_loop(loop: (*Loop)?);
    fn us_loop_integrate(loop: (*Loop)?);
    fn us_loop_iteration_number(loop: (*Loop)?): i64;
    fn uws_loop_addPostHandler(loop: *Loop, ctx: *!, cb: @c fn (*!, *Loop));
    fn uws_loop_removePostHandler(loop: *Loop, ctx: *!, cb: @c fn (*!, *Loop));
    fn uws_loop_addPreHandler(loop: *Loop, ctx: *!, cb: @c fn (*!, *Loop));
    fn uws_loop_removePreHandler(loop: *Loop, ctx: *!, cb: @c fn (*!, *Loop));
    fn us_loop_run_bun_tick(loop: (*Loop)?, timeout_ms: (*const bun.timespec)?);
    fn us_internal_free_closed_sockets(loop: *Loop);
    fn us_loop_close_all_groups(loop: *Loop): i32;
    fn uws_get_loop(): *Loop;
    fn uws_get_loop_with_native(native: *!): *WindowsLoop;
    fn uws_loop_defer(loop: *Loop, ctx: *!, cb: @c fn (*!));
    fn uws_res_clear_corked_socket(loop: *Loop);
    fn uws_loop_date_header_timer_update(loop: *Loop);
}

export struct LoopHandler {
    loop: *Loop,
    cb: @c fn (*!, *Loop),

    fn removePost() {
        unsafe {
            uws_loop_removePostHandler(_.loop, _ as *!, _.cb);
        }
    }

    fn removePre() {
        unsafe {
            uws_loop_removePreHandler(_.loop, _ as *!, _.cb);
        }
    }
}

type OpaquePtr = *!;
pub const PosixEventType = switch (bun.Environment.os) {
    .linux => std.os.linux.epoll_event,
    .mac => std.posix.system.kevent64_s,
    .freebsd => std.c.Kevent,
    .windows => OpaquePtr,
    .wasm => @compileError("Unsupported OS"),
};

export @c struct PosixLoop {
    @align(16) internal_loop_data: InternalLoopData,
    num_polls: i32 = 0,
    num_ready_polls: i32 = 0,
    current_ready_poll: i32 = 0,
    fd: i32 = 0,
    active: u32 = 0,
    pending_wakeups: u32 = 0,
    @align(16) ready_polls: [1024]PosixEventType,

    fn uncork(*_) {
        uws_res_clear_corked_socket(_ as *Loop);
    }

    fn updateDate(*_) {
        uws_loop_date_header_timer_update(_ as *Loop);
    }

    fn iterationNumber(): u64 {
        return _.internal_loop_data.iteration_nr;
    }

    fn inc(*_) {
        _.num_polls += 1;
    }

    fn dec(*_) {
        _.num_polls -= 1;
    }

    fn ref(*_) {
        _.num_polls += 1;
        _.active += 1;
    }

    fn unref(*_) {
        _.num_polls -= 1;
        if (_.active > 0) _.active -= 1;
    }

    fn isActive(): bool {
        return _.active > 0;
    }

    fn addActive(*_, value: u32) {
        _.active += value;
    }

    fn subActive(*_, value: u32) {
        if (_.active > value) {
            _.active -= value;
        } else {
            _.active = 0;
        }
    }

    fn unrefCount(*_, count: i32) {
        _.num_polls -= count;
        const c_u32 = count as u32;
        if (_.active > c_u32) {
            _.active -= c_u32;
        } else {
            _.active = 0;
        }
    }

    static fn get(): *Loop {
        return uws_get_loop();
    }

    fn drainQuicIfNecessary(*_) {
        if (_.internal_loop_data.quic_head == null) return;
        us_quic_loop_flush_if_pending(_ as *Loop);
    }

    static fn create(comptime Handler: type): *Loop {
        return us_create_loop(
            null,
            Handler.wakeup,
            if (@hasDecl(Handler, "pre")) Handler.pre else null,
            if (@hasDecl(Handler, "post")) Handler.post else null,
            0,
        ).?;
    }

    fn wakeup(*_) {
        us_wakeup_loop(_ as *Loop);
    }

    fn tick(*_) {
        us_loop_run_bun_tick(_ as *Loop, null);
    }

    fn tickWithoutIdle(*_) {
        const ts: bun.timespec = .{ .sec = 0, .nsec = 0 };
        us_loop_run_bun_tick(_ as *Loop, &ts);
    }

    fn tickWithTimeout(*_, timespec: (*const bun.timespec)?) {
        us_loop_run_bun_tick(_ as *Loop, timespec);
    }

    fn drainClosedSockets(*_) {
        us_internal_free_closed_sockets(_ as *Loop);
    }

    fn closeAllGroups(*_): bool {
        return us_loop_close_all_groups(_ as *Loop) != 0;
    }

    fn nextTick(*_, comptime UserType: type, user_data: UserType, comptime deferCallback: auto) {
        const Handler = struct {
            pub fn callback(data: *!) callconv(.c) void {
                unsafe {
                    deferCallback(@as(UserType, @ptrCast(@alignCast(data))));
                }
            }
        };
        unsafe {
            uws_loop_defer(_ as *Loop, user_data as *!, Handler.callback);
        }
    }

    fn run(*_) {
        us_loop_run(_ as *Loop);
    }

    fn shouldEnableDateHeaderTimer(): bool {
        return _.internal_loop_data.shouldEnableDateHeaderTimer();
    }

    fn release(*_) {
        _.deinit();
    }

    fn deinit(*_) {
        us_loop_free(_ as *Loop);
    }
}

export @c struct WindowsLoop {
    @align(16) internal_loop_data: InternalLoopData,
    uv_loop: *bun.windows.libuv.Loop,
    is_default: i32 = 0,
    pre: *bun.windows.libuv.uv_prepare_t,
    check: *bun.windows.libuv.uv_check_t,

    fn shouldEnableDateHeaderTimer(): bool {
        return _.internal_loop_data.shouldEnableDateHeaderTimer();
    }

    fn uncork(*_) {
        uws_res_clear_corked_socket(_ as *Loop);
    }

    static fn get(): *WindowsLoop {
        unsafe {
            return uws_get_loop_with_native(bun.windows.libuv.Loop.get() as *!);
        }
    }

    fn iterationNumber(): u64 {
        return _.internal_loop_data.iteration_nr;
    }

    fn addActive(*_, val: u32) {
        _.uv_loop.addActive(val);
    }

    fn subActive(*_, val: u32) {
        _.uv_loop.subActive(val);
    }

    fn isActive(): bool {
        return _.uv_loop.isActive();
    }

    fn wakeup(*_) {
        us_wakeup_loop(_ as *Loop);
    }

    fn tickWithTimeout(*_, timespec: (*const bun.timespec)?) {
        * = timespec;
        us_loop_run(_ as *Loop);
    }

    fn tickWithoutIdle(*_) {
        us_loop_pump(_ as *Loop);
    }

    fn drainQuicIfNecessary(*_) {
        if (_.internal_loop_data.quic_head == null) return;
        us_quic_loop_flush_if_pending(_ as *Loop);
    }

    static fn create(comptime Handler: type): *WindowsLoop {
        return us_create_loop(
            null,
            Handler.wakeup,
            if (@hasDecl(Handler, "pre")) Handler.pre else null,
            if (@hasDecl(Handler, "post")) Handler.post else null,
            0,
        ).? as *WindowsLoop;
    }

    fn run(*_) {
        us_loop_run(_ as *Loop);
    }

    fn inc(*_) {
        _.uv_loop.inc();
    }

    fn dec(*_) {
        _.uv_loop.dec();
    }

    fn drainClosedSockets(*_) {
        us_internal_free_closed_sockets(_ as *Loop);
    }

    fn closeAllGroups(*_): bool {
        return us_loop_close_all_groups(_ as *Loop) != 0;
    }

    fn nextTick(*_, comptime UserType: type, user_data: UserType, comptime deferCallback: auto) {
        const Handler = struct {
            pub fn callback(data: *!) callconv(.c) void {
                unsafe {
                    deferCallback(@as(UserType, @ptrCast(@alignCast(data))));
                }
            }
        };
        unsafe {
            uws_loop_defer(_ as *Loop, user_data as *!, Handler.callback);
        }
    }

    fn updateDate(*_) {
        uws_loop_date_header_timer_update(_ as *Loop);
    }

    fn release(*_) {
        _.deinit();
    }

    fn deinit(*_) {
        us_loop_free(_ as *Loop);
    }
}

export type Loop = PosixLoop;
