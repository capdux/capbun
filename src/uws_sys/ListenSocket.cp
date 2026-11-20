import bun from "bun";
import { SocketGroup } from "./SocketGroup.cp";
import { us_socket_t } from "./us_socket_t.cp";
import { SocketHandler } from "./socket.cp";

pub type OnServerNameFn = @c fn (*ListenSocket, cstr);
pub type OptOnServerNameCb = OnServerNameFn?;

@c {
    fn us_listen_socket_close(ls: *ListenSocket);
    fn us_listen_socket_group(ls: *ListenSocket): *SocketGroup;
    fn us_listen_socket_ext(ls: *ListenSocket): *!;
    fn us_listen_socket_get_fd(ls: *ListenSocket): i32;
    fn us_listen_socket_port(ls: *ListenSocket): i32;
    fn us_listen_socket_add_server_name(ls: *ListenSocket, hostname: cstr, ssl_ctx: *!, user: *!?): i32;
    fn us_listen_socket_remove_server_name(ls: *ListenSocket, hostname: cstr);
    fn us_listen_socket_find_server_name_userdata(ls: *ListenSocket, hostname: cstr): *!?;
    fn us_listen_socket_on_server_name(ls: *ListenSocket, cb: OptOnServerNameCb);
}

export @opaque class ListenSocket {
    pub fn close(*_) {
        us_listen_socket_close(this);
    }

    pub fn getLocalAddress(*_, buf: anytype): !string {
        return this.getSocket().localAddress(buf);
    }

    pub fn getLocalPort(*_): i32 {
        return this.getSocket().localPort();
    }

    pub fn getSocket(*_): *us_socket_t {
        unsafe {
            return this as *us_socket_t;
        }
    }

    pub fn socket(*_, comptime is_ssl: bool): SocketHandler(is_ssl) {
        return SocketHandler(is_ssl).from(this.getSocket());
    }

    pub fn group(*_): *SocketGroup {
        return us_listen_socket_group(this);
    }

    pub fn ext(*_, comptime T: type): *T {
        unsafe {
            return us_listen_socket_ext(this) as *T;
        }
    }

    pub fn fd(*_): bun.FD {
        return bun.FD.fromNative(us_listen_socket_get_fd(this));
    }

    pub fn addServerName(*_, hostname: cstr, ssl_ctx: *!, user: *!?): bool {
        return us_listen_socket_add_server_name(this, hostname, ssl_ctx, user) == 0;
    }

    pub fn removeServerName(*_, hostname: cstr) {
        us_listen_socket_remove_server_name(this, hostname);
    }

    pub fn findServerNameUserdata(*_, comptime T: type, hostname: cstr): (*T)? {
        let ptr = us_listen_socket_find_server_name_userdata(this, hostname);
        if (ptr == null) return null;
        unsafe {
            return ptr.? as *T;
        }
    }

    pub fn onServerName(*_, cb: OptOnServerNameCb) {
        us_listen_socket_on_server_name(this, cb);
    }
}
