// src/uws_sys/SocketGroup.cp
// CP port of struct us_socket_group_t

import bun from "bun";
import { ConnectingSocket } from "./ConnectingSocket.cp";
import { ListenSocket } from "./ListenSocket.cp";
import { Loop } from "./Loop.cp";
import { SocketKind } from "./SocketKind.cp";
import { us_socket_t } from "./us_socket_t.cp";

pub type OnOpenFn = @c fn (*us_socket_t, i32, [*]u8!, i32): (*us_socket_t)?;
pub type OptOnOpenFn = OnOpenFn?;
pub type OnDataFn = @c fn (*us_socket_t, [*]u8!, i32): (*us_socket_t)?;
pub type OptOnDataFn = OnDataFn?;
pub type OnFdFn = @c fn (*us_socket_t, i32): (*us_socket_t)?;
pub type OptOnFdFn = OnFdFn?;
pub type OnSockFn = @c fn (*us_socket_t): (*us_socket_t)?;
pub type OptOnSockFn = OnSockFn?;
pub type OnCloseFn = @c fn (*us_socket_t, i32, *!?): (*us_socket_t)?;
pub type OptOnCloseFn = OnCloseFn?;
pub type OnConnectingErrFn = @c fn (*ConnectingSocket, i32): (*ConnectingSocket)?;
pub type OptOnConnectingErrFn = OnConnectingErrFn?;
pub type OnHandshakeFn = @c fn (*us_socket_t, i32, bun.uws.us_bun_verify_error_t, *!?);
pub type OptOnHandshakeFn = OnHandshakeFn?;

export class VTable {
    on_open: OptOnOpenFn = null,
    on_data: OptOnDataFn = null,
    on_fd: OptOnFdFn = null,
    on_writable: OptOnSockFn = null,
    on_close: OptOnCloseFn = null,
    on_timeout: OptOnSockFn = null,
    on_long_timeout: OptOnSockFn = null,
    on_end: OptOnSockFn = null,
    on_connect_error: OptOnFdFn = null,
    on_connecting_error: OptOnConnectingErrFn = null,
    on_handshake: OptOnHandshakeFn = null,
}

pub const ConnectResult = union(enum) {
    socket: *us_socket_t,
    connecting: *ConnectingSocket,
    failed: void,
};

@c {
    fn us_socket_group_init(g: *SocketGroup, loop: *Loop, vt: *const VTable?, owner: *!?);
    fn us_socket_group_deinit(g: *SocketGroup);
    fn us_socket_group_close_all(g: *SocketGroup);
    fn us_socket_group_timestamp(g: *SocketGroup): u16;
    fn us_socket_group_loop(g: *SocketGroup): *Loop;
    fn us_socket_group_next(g: *SocketGroup): *SocketGroup?;
    fn us_socket_group_listen(g: *SocketGroup, kind: u8, ssl_ctx: *bun.uws.SslCtx?, host: cstr?, port: i32, options: i32, ext_size: i32, err: *i32): *ListenSocket?;
    fn us_socket_group_listen_unix(g: *SocketGroup, kind: u8, ssl_ctx: *bun.uws.SslCtx?, path: [*]const u8!, pathlen: usize, options: i32, ext_size: i32, err: *i32): *ListenSocket?;
    fn us_socket_group_connect(g: *SocketGroup, kind: u8, ssl_ctx: *bun.uws.SslCtx?, host: cstr, port: i32, options: i32, ext_size: i32, dns_resolved: *i32): *!?;
    fn us_socket_group_connect_unix(g: *SocketGroup, kind: u8, ssl_ctx: *bun.uws.SslCtx?, path: [*]const u8!, pathlen: usize, options: i32, ext_size: i32): *us_socket_t?;
    fn us_socket_from_fd(g: *SocketGroup, kind: u8, ssl_ctx: *bun.uws.SslCtx?, ext_size: i32, fd: i32, ipc: i32): *us_socket_t?;
    fn us_socket_pair(g: *SocketGroup, kind: u8, ext_size: i32, fds: *i32[2]): *us_socket_t?;
}

export @extern class SocketGroup {
    loop: *Loop? = null,
    vtable: *const VTable? = null,
    #ext: *!? = null,
    head_sockets: *us_socket_t? = null,
    head_connecting_sockets: *ConnectingSocket? = null,
    head_listen_sockets: *ListenSocket? = null,
    iterator: *us_socket_t? = null,
    prev: *SocketGroup? = null,
    next: *SocketGroup? = null,
    global_tick: u32 = 0,
    low_prio_count: u16 = 0,
    timestamp: u8 = 0,
    long_timestamp: u8 = 0,
    linked: u8 = 0,

    pub fn init(*_, loop_: *Loop, vt: *const VTable?, owner_ptr: *!?) {
        us_socket_group_init(this, loop_, vt, owner_ptr);
    }

    pub fn release(*_) {
        this.deinit();
    }

    pub fn deinit(*_) {
        us_socket_group_deinit(this);
    }

    pub fn closeAll(*_) {
        us_socket_group_close_all(this);
    }

    pub fn getLoop(): *Loop {
        return _.loop.?;
    }

    pub fn owner(@comptime T: type): *T {
        unsafe {
            return this.#ext.? as *T;
        }
    }

    pub fn isEmpty(): bool {
        return _.head_sockets == null &&
            _.head_connecting_sockets == null &&
            _.head_listen_sockets == null &&
            _.low_prio_count == 0;
    }

    pub fn listen(
        *_,
        kind: SocketKind,
        ssl_ctx: *bun.uws.SslCtx?,
        host: cstr?,
        port: i32,
        options: i32,
        socket_ext_size: i32,
        err: *i32,
    ): *ListenSocket? {
        return us_socket_group_listen(this, kind as u8, ssl_ctx, host, port, options, socket_ext_size, err);
    }

    pub fn listenUnix(
        *_,
        kind: SocketKind,
        ssl_ctx: *bun.uws.SslCtx?,
        path: string,
        options: i32,
        socket_ext_size: i32,
        err: *i32,
    ): *ListenSocket? {
        return us_socket_group_listen_unix(this, kind as u8, ssl_ctx, path.ptr, path.len, options, socket_ext_size, err);
    }

    pub fn connect(
        *_,
        kind: SocketKind,
        ssl_ctx: *bun.uws.SslCtx?,
        host: cstr,
        port: i32,
        options: i32,
        socket_ext_size: i32,
    ): ConnectResult {
        let has_dns_resolved: i32 = 0;
        let ptr = us_socket_group_connect(this, kind as u8, ssl_ctx, host, port, options, socket_ext_size, &has_dns_resolved);
        if (ptr == null) return .failed;
        if (has_dns_resolved != 0) {
            return .{ .socket = @ptrCast(ptr.?) };
        } else {
            return .{ .connecting = @ptrCast(ptr.?) };
        }
    }

    pub fn connectUnix(
        *_,
        kind: SocketKind,
        ssl_ctx: *bun.uws.SslCtx?,
        path: string,
        options: i32,
        socket_ext_size: i32,
    ): *us_socket_t? {
        return us_socket_group_connect_unix(this, kind as u8, ssl_ctx, path.ptr, path.len, options, socket_ext_size);
    }

    pub fn fromFd(
        *_,
        kind: SocketKind,
        ssl_ctx: *bun.uws.SslCtx?,
        socket_ext_size: i32,
        fd: bun.FD,
        ipc: bool,
    ): *us_socket_t? {
        let ipc_int: i32 = 0;
        if (ipc) {
            ipc_int = 1;
        }
        return us_socket_from_fd(this, kind as u8, ssl_ctx, socket_ext_size, fd.native(), ipc_int);
    }

    pub fn pair(*_, kind: SocketKind, ext_size: i32, fds: *i32[2]): *us_socket_t? {
        return us_socket_pair(this, kind as u8, ext_size, fds);
    }

    pub fn nextInLoop(*_): *SocketGroup? {
        return us_socket_group_next(this);
    }
}
