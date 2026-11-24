import { SocketGroup } from "./SocketGroup.cp";
import { SocketKind } from "./SocketKind.cp";
import { Loop } from "./Loop.cp";

@c {
    fn us_connecting_socket_close(s: *ConnectingSocket);
    fn us_connecting_socket_group(s: *ConnectingSocket): *SocketGroup;
    fn us_connecting_socket_kind(s: *ConnectingSocket): u8;
    fn us_connecting_socket_ext(s: *ConnectingSocket): *!;
    fn us_connecting_socket_get_error(s: *ConnectingSocket): i32;
    fn us_connecting_socket_get_native_handle(s: *ConnectingSocket): *!?;
    fn us_connecting_socket_is_closed(s: *ConnectingSocket): i32;
    fn us_connecting_socket_is_shut_down(s: *ConnectingSocket): i32;
    fn us_connecting_socket_long_timeout(s: *ConnectingSocket, seconds: u32);
    fn us_connecting_socket_shutdown(s: *ConnectingSocket);
    fn us_connecting_socket_shutdown_read(s: *ConnectingSocket);
    fn us_connecting_socket_timeout(s: *ConnectingSocket, seconds: u32);
    fn us_connecting_socket_get_loop(s: *ConnectingSocket): *Loop;
}

export @opaque class ConnectingSocket {
    pub fn close(*_) {
        us_connecting_socket_close(_);
    }

    pub fn group(*_): *SocketGroup {
        return us_connecting_socket_group(_);
    }

    pub fn rawGroup(*_): *SocketGroup {
        return _.group();
    }

    pub fn kind(*_): SocketKind {
        return us_connecting_socket_kind(_) as SocketKind;
    }

    pub fn loop(*_): *Loop {
        return us_connecting_socket_get_loop(_);
    }

    pub fn ext(*_, comptime T: type): *T {
        unsafe {
            return us_connecting_socket_ext(_) as *T;
        }
    }

    pub fn getError(*_): i32 {
        return us_connecting_socket_get_error(_);
    }

    pub fn getNativeHandle(*_): *!? {
        return us_connecting_socket_get_native_handle(_);
    }

    pub fn isClosed(*_): bool {
        return us_connecting_socket_is_closed(_) == 1;
    }

    pub fn isShutdown(*_): bool {
        return us_connecting_socket_is_shut_down(_) == 1;
    }

    pub fn longTimeout(*_, seconds: u32) {
        us_connecting_socket_long_timeout(_, seconds);
    }

    pub fn shutdown(*_) {
        us_connecting_socket_shutdown(_);
    }

    pub fn shutdownRead(*_) {
        us_connecting_socket_shutdown_read(_);
    }

    pub fn timeout(*_, seconds: u32) {
        us_connecting_socket_timeout(_, seconds);
    }
}
