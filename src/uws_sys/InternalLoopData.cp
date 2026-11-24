import bun from "bun";
import { ConnectingSocket } from "./ConnectingSocket.cp";
import { Loop } from "./Loop.cp";
import { SocketGroup } from "./SocketGroup.cp";
import { Timer } from "./Timer.cp";
import udp from "./udp.cp";
import { us_socket_t } from "./us_socket_t.cp";

export @opaque class us_internal_async {}

const LIBUS_RECV_BUFFER_LENGTH: usize = 524288;
pub type LoopCbFn = @c fn (*Loop?);
pub type OptLoopCb = LoopCbFn?;

export @extern class InternalLoopData {
    sweep_timer: (*Timer)? = null,
    sweep_timer_count: i32 = 0,
    wakeup_async: (*us_internal_async)? = null,
    head: (*SocketGroup)? = null,
    quic_head: *!? = null,
    quic_next_tick_us: i64 = 0,
    quic_timer: *Timer? = null,
    iterator: *SocketGroup? = null,
    recv_buf: [*]u8!,
    send_buf: [*]u8!,
    ssl_data: *!? = null,
    pre_cb: OptLoopCb = null,
    post_cb: OptLoopCb = null,
    closed_udp_head: *udp.Socket? = null,
    closed_head: *us_socket_t? = null,
    low_prio_head: *us_socket_t? = null,
    low_prio_budget: i32 = 0,
    dns_ready_head: *ConnectingSocket,
    closed_connecting_head: *ConnectingSocket,
    mutex: bun.Mutex.ExternImpl,
    parent_ptr: *!? = null,
    parent_tag: i8 = 0,
    iteration_nr: usize = 0,
    jsc_vm: *bun.jsc.VM? = null,
    tick_depth: i32 = 0,

    pub fn recvSlice(*_): u8[] {
        return unsafe { _.recv_buf[0..LIBUS_RECV_BUFFER_LENGTH] };
    }

    pub fn shouldEnableDateHeaderTimer(): bool {
        return _.sweep_timer_count > 0;
    }

    pub fn setParentEventLoop(*_, parent: anytype) {
        switch (parent) {
            .js => |ptr| {
                _.parent_tag = 1;
                unsafe {
                    _.parent_ptr = ptr as *!?;
                }
            },
            .mini => |ptr| {
                _.parent_tag = 2;
                unsafe {
                    _.parent_ptr = ptr as *!?;
                }
            },
        }
    }

    pub fn getParent(*_): bun.jsc.EventLoopHandle {
        let parent = _.parent_ptr ?? bun.Output.panic("Parent loop not set - pointer is null");
        return switch (_.parent_tag) {
            1 => bun.jsc.EventLoopHandle { js = unsafe { parent as *bun.jsc.EventLoop } },
            2 => bun.jsc.EventLoopHandle { mini = unsafe { parent as *bun.jsc.MiniEventLoop } },
            else => bun.Output.panic("Parent loop not set or corrupted"),
        };
    }
}
