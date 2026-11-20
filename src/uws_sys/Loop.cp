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
    ): *Loop?;
    fn us_loop_free(loop: *Loop?);
    fn us_loop_ext(loop: *Loop?): *!?;
    fn us_quic_loop_flush_if_pending(loop: *Loop);
    fn us_loop_run(loop: *Loop?);
    fn us_loop_pump(loop: *Loop?);
    fn us_wakeup_loop(loop: *Loop?);
    fn us_loop_integrate(loop: *Loop?);
    fn us_loop_iteration_number(loop: *Loop?): i64;
    fn uws_loop_addPostHandler(loop: *Loop, ctx: *!, cb: @c fn (*!, *Loop): void);
    fn uws_loop_removePostHandler(loop: *Loop, ctx: *!, cb: @c fn (*!, *Loop): void);
    fn uws_loop_addPreHandler(loop: *Loop, ctx: *!, cb: @c fn (*!, *Loop): void);
    fn uws_loop_removePreHandler(loop: *Loop, ctx: *!, cb: @c fn (*!, *Loop): void);
    fn us_loop_run_bun_tick(loop: *Loop?, timeout_ms: *const bun.timespec?);
    fn us_internal_free_closed_sockets(loop: *Loop);
    fn us_loop_close_all_groups(loop: *Loop): i32;
    fn uws_get_loop(): *Loop;
    fn uws_get_loop_with_native(native: *!): *WindowsLoop;
    fn uws_loop_defer(loop: *Loop, ctx: *!, cb: @c fn (*!): void);
    fn uws_res_clear_corked_socket(loop: *Loop);
    fn uws_loop_date_header_timer_update(loop: *Loop);
}

export class LoopHandler {
    loop: *Loop,
    cb: @c fn (*!, *Loop): void,

    pub fn removePost() {
        unsafe {
            uws_loop_removePostHandler(_.loop, _ as *!, _.cb);
        }
    }

    pub fn removePre() {
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

export @extern @value class PosixLoop {
    @align(16) internal_loop_data: InternalLoopData,
    num_polls: i32 = 0,
    num_ready_polls: i32 = 0,
    current_ready_poll: i32 = 0,
    fd: i32 = 0,
    active: u32 = 0,
    pending_wakeups: u32 = 0,
    @align(16) ready_polls: [1024]PosixEventType,

    pub fn uncork(*_) {
        uws_res_clear_corked_socket(this as *Loop);
    }

    pub fn updateDate(*_) {
        uws_loop_date_header_timer_update(this as *Loop);
    }

    pub fn iterationNumber(): u64 {
        return _.internal_loop_data.iteration_nr;
    }

    pub fn inc(*_) {
        this.num_polls += 1;
    }

    pub fn dec(*_) {
        this.num_polls -= 1;
    }

    pub fn ref(*_) {
        this.num_polls += 1;
        this.active += 1;
    }

    pub fn unref(*_) {
        this.num_polls -= 1;
        if (this.active > 0) this.active -= 1;
    }

    pub fn isActive(): bool {
        return _.active > 0;
    }

    pub fn addActive(*_, value: u32) {
        this.active += value;
    }

    pub fn subActive(*_, value: u32) {
        if (this.active > value) {
            this.active -= value;
        } else {
            this.active = 0;
        }
    }

    pub fn unrefCount(*_, count: i32) {
        this.num_polls -= count;
        let c_u32 = count as u32;
        if (this.active > c_u32) {
            this.active -= c_u32;
        } else {
            this.active = 0;
        }
    }

    pub static fn get(): *Loop {
        return uws_get_loop();
    }

    pub fn drainQuicIfNecessary(*_) {
        if (this.internal_loop_data.quic_head == null) return;
        us_quic_loop_flush_if_pending(this as *Loop);
    }

    pub static fn create(@comptime Handler: type): *Loop {
        return us_create_loop(
            null,
            Handler.wakeup,
            if (@hasDecl(Handler, "pre")) Handler.pre else null,
            if (@hasDecl(Handler, "post")) Handler.post else null,
            0,
        ).?;
    }

    pub fn wakeup(*_) {
        us_wakeup_loop(this as *Loop);
    }

    pub fn tick(*_) {
        us_loop_run_bun_tick(this as *Loop, null);
    }

    pub fn tickWithoutIdle(*_) {
        const ts: bun.timespec = .{ .sec = 0, .nsec = 0 };
        us_loop_run_bun_tick(this as *Loop, &ts);
    }

    pub fn tickWithTimeout(*_, timespec: *const bun.timespec?) {
        us_loop_run_bun_tick(this as *Loop, timespec);
    }

    pub fn drainClosedSockets(*_) {
        us_internal_free_closed_sockets(this as *Loop);
    }

    pub fn closeAllGroups(*_): bool {
        return us_loop_close_all_groups(this as *Loop) != 0;
    }

    pub fn nextTick(*_, @comptime UserType: type, user_data: UserType, @comptime deferCallback: anytype) {
        const Handler = struct {
            pub fn callback(data: *!) callconv(.c) void {
                deferCallback(@as(UserType, @ptrCast(@alignCast(data))));
            }
        };
        unsafe {
            uws_loop_defer(this as *Loop, user_data as *!, Handler.callback);
        }
    }

    pub fn run(*_) {
        us_loop_run(this as *Loop);
    }

    pub fn shouldEnableDateHeaderTimer(): bool {
        return _.internal_loop_data.shouldEnableDateHeaderTimer();
    }

    pub fn release(*_) {
        this.deinit();
    }

    pub fn deinit(*_) {
        us_loop_free(this as *Loop);
    }
}

export @extern class WindowsLoop {
    @align(16) internal_loop_data: InternalLoopData,
    uv_loop: *bun.windows.libuv.Loop,
    is_default: i32 = 0,
    pre: *bun.windows.libuv.uv_prepare_t,
    check: *bun.windows.libuv.uv_check_t,

    pub fn shouldEnableDateHeaderTimer(): bool {
        return _.internal_loop_data.shouldEnableDateHeaderTimer();
    }

    pub fn uncork(*_) {
        uws_res_clear_corked_socket(this as *Loop);
    }

    pub static fn get(): *WindowsLoop {
        unsafe {
            return uws_get_loop_with_native(bun.windows.libuv.Loop.get() as *!);
        }
    }

    pub fn iterationNumber(): u64 {
        return _.internal_loop_data.iteration_nr;
    }

    pub fn addActive(*_, val: u32) {
        this.uv_loop.addActive(val);
    }

    pub fn subActive(*_, val: u32) {
        this.uv_loop.subActive(val);
    }

    pub fn isActive(): bool {
        return _.uv_loop.isActive();
    }

    pub fn wakeup(*_) {
        us_wakeup_loop(this as *Loop);
    }

    pub fn tickWithTimeout(*_, timespec: *const bun.timespec?) {
        us_loop_run(this as *Loop);
    }

    pub fn tickWithoutIdle(*_) {
        us_loop_pump(this as *Loop);
    }

    pub fn drainQuicIfNecessary(*_) {
        if (this.internal_loop_data.quic_head == null) return;
        us_quic_loop_flush_if_pending(this as *Loop);
    }

    pub static fn create(@comptime Handler: type): *WindowsLoop {
        return us_create_loop(
            null,
            Handler.wakeup,
            if (@hasDecl(Handler, "pre")) Handler.pre else null,
            if (@hasDecl(Handler, "post")) Handler.post else null,
            0,
        ).? as *WindowsLoop;
    }

    pub fn run(*_) {
        us_loop_run(this as *Loop);
    }

    pub fn inc(*_) {
        this.uv_loop.inc();
    }

    pub fn dec(*_) {
        this.uv_loop.dec();
    }

    pub fn drainClosedSockets(*_) {
        us_internal_free_closed_sockets(this as *Loop);
    }

    pub fn closeAllGroups(*_): bool {
        return us_loop_close_all_groups(this as *Loop) != 0;
    }

    pub fn nextTick(*_, @comptime UserType: type, user_data: UserType, @comptime deferCallback: anytype) {
        const Handler = struct {
            pub fn callback(data: *!) callconv(.c) void {
                deferCallback(@as(UserType, @ptrCast(@alignCast(data))));
            }
        };
        unsafe {
            uws_loop_defer(this as *Loop, user_data as *!, Handler.callback);
        }
    }

    pub fn updateDate(*_) {
        uws_loop_date_header_timer_update(this as *Loop);
    }

    pub fn release(*_) {
        this.deinit();
    }

    pub fn deinit(*_) {
        us_loop_free(this as *Loop);
    }
}

export type Loop = PosixLoop;
