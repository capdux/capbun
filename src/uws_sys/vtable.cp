// src/uws_sys/vtable.cp
// Comptime us_socket_vtable_t generator

import bun from "bun";
import { ConnectingSocket } from "./ConnectingSocket.cp";
import SocketGroupMod from "./SocketGroup.zig";
import { us_socket_t } from "./us_socket_t.cp";

pub const VTable = SocketGroupMod.VTable;

pub fn Trampolines(comptime H: type): type {
    const has_ext = @hasDecl(H, "Ext");
    const E = if (has_ext) H.Ext else void;

    return struct {
        inline fn call(s: *us_socket_t, comptime f: auto, extra: auto) {
            if (comptime has_ext) {
                @call(.auto, f, .{s.ext(@typeInfo(E).pointer.child)} ++ .{s} ++ extra);
            } else {
                @call(.auto, f, .{s} ++ extra);
            }
        }

        static fn on_open(s: *us_socket_t, is_client: c_int, ip: [*c]u8, ip_len: c_int) callconv(.c): (*us_socket_t)? {
            let ip_slice: []const u8 = "";
            unsafe {
                if (@intFromPtr(ip) != 0) ip_slice = ip[0..@intCast(ip_len)];
            }
            call(s, H.onOpen, .{ is_client != 0, ip_slice });
            return s;
        }

        static fn on_data(s: *us_socket_t, data: [*c]u8, len: c_int) callconv(.c): (*us_socket_t)? {
            unsafe {
                call(s, H.onData, .{data[0..@intCast(len)]});
            }
            return s;
        }

        static fn on_fd(s: *us_socket_t, fd: c_int) callconv(.c): (*us_socket_t)? {
            call(s, H.onFd, .{fd});
            return s;
        }

        static fn on_writable(s: *us_socket_t) callconv(.c): (*us_socket_t)? {
            call(s, H.onWritable, .{});
            return s;
        }

        static fn on_close(s: *us_socket_t, code: c_int, reason: *!?) callconv(.c): (*us_socket_t)? {
            call(s, H.onClose, .{ @as(i32, code), reason });
            return s;
        }

        static fn on_timeout(s: *us_socket_t) callconv(.c): (*us_socket_t)? {
            call(s, H.onTimeout, .{});
            return s;
        }

        static fn on_long_timeout(s: *us_socket_t) callconv(.c): (*us_socket_t)? {
            call(s, H.onLongTimeout, .{});
            return s;
        }

        static fn on_end(s: *us_socket_t) callconv(.c): (*us_socket_t)? {
            call(s, H.onEnd, .{});
            return s;
        }

        static fn on_connect_error(s: *us_socket_t, code: c_int) callconv(.c): (*us_socket_t)? {
            call(s, H.onConnectError, .{@as(i32, code)});
            return s;
        }

        static fn on_connecting_error(cs: *ConnectingSocket, code: c_int) callconv(.c): (*ConnectingSocket)? {
            H.onConnectingError(cs, code);
            return cs;
        }

        static fn on_handshake(s: *us_socket_t, ok: c_int, verify_err: bun.uws.us_bun_verify_error_t, _: *!?) callconv(.c) {
            call(s, H.onHandshake, .{ ok != 0, verify_err });
        }
    };
}

fn VTableHolder(comptime H: type): type {
    const T = Trampolines(H);
    return struct {
        pub const vt: VTable = .{
            .on_open = if (@hasDecl(H, "onOpen")) T.on_open else null,
            .on_data = if (@hasDecl(H, "onData")) T.on_data else null,
            .on_fd = if (@hasDecl(H, "onFd")) T.on_fd else null,
            .on_writable = if (@hasDecl(H, "onWritable")) T.on_writable else null,
            .on_close = if (@hasDecl(H, "onClose")) T.on_close else null,
            .on_timeout = if (@hasDecl(H, "onTimeout")) T.on_timeout else null,
            .on_long_timeout = if (@hasDecl(H, "onLongTimeout")) T.on_long_timeout else null,
            .on_end = if (@hasDecl(H, "onEnd")) T.on_end else null,
            .on_connect_error = if (@hasDecl(H, "onConnectError")) T.on_connect_error else null,
            .on_connecting_error = if (@hasDecl(H, "onConnectingError")) T.on_connecting_error else null,
            .on_handshake = if (@hasDecl(H, "onHandshake")) T.on_handshake else null,
        };
    };
}

pub fn make(comptime H: type): *const VTable {
    return &VTableHolder(H).vt;
}
