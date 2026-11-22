// Per-SocketKind handler adapters in CP language
// Converted from src/runtime/socket/uws_handlers.zig

import IPC from "../../jsc/ipc.zig";
import js_valkey from "../valkey_jsc/js_valkey.zig";
import mysql from "../../sql_jsc/mysql.zig";
import websocket_client from "../../http_jsc/websocket_client.zig";
import websocket_upgrade_client from "../../http_jsc/websocket_client/WebSocketUpgradeClient.zig";

import bun from "bun";
const api = bun.jsc.API;

const uws = bun.uws;
const ConnectingSocket = uws.ConnectingSocket;
const us_socket_t = uws.us_socket_t;

@inline fn swallow(result: auto) {
    if (@typeInfo(@TypeOf(result)) == .error_union) {
        result catch {};
    }
}

pub fn PtrHandler(comptime T: type, comptime ssl: bool): type {
    const S = uws.NewSocketHandler(ssl);
    return struct {
        pub const Ext = @FieldType(struct { p: *((*T)?) }, "p");
        inline fn wrap(s: *us_socket_t) S {
            return S.from(s);
        }
        pub fn onOpen(ext: Ext, s: *us_socket_t, _: bool, _: string) void {
            const inst = ext.* orelse return;
            if (@hasDecl(T, "onOpen")) {
                swallow(inst.onOpen(wrap(s)));
            }
        }
        pub fn onData(ext: Ext, s: *us_socket_t, data: string) void {
            const inst = ext.* orelse return;
            if (@hasDecl(T, "onData")) {
                swallow(inst.onData(wrap(s), data));
            }
        }
        pub fn onWritable(ext: Ext, s: *us_socket_t) void {
            const inst = ext.* orelse return;
            if (@hasDecl(T, "onWritable")) {
                swallow(inst.onWritable(wrap(s)));
            }
        }
        pub fn onClose(ext: Ext, s: *us_socket_t, code: i32, reason: *!?) void {
            const inst = ext.* orelse return;
            if (@hasDecl(T, "onClose")) {
                swallow(inst.onClose(wrap(s), code, reason));
            }
        }
        pub fn onTimeout(ext: Ext, s: *us_socket_t) void {
            const inst = ext.* orelse return;
            if (@hasDecl(T, "onTimeout")) {
                swallow(inst.onTimeout(wrap(s)));
            }
        }
        pub fn onLongTimeout(ext: Ext, s: *us_socket_t) void {
            const inst = ext.* orelse return;
            if (@hasDecl(T, "onLongTimeout")) {
                swallow(inst.onLongTimeout(wrap(s)));
            }
        }
        pub fn onEnd(ext: Ext, s: *us_socket_t) void {
            const inst = ext.* orelse return;
            if (@hasDecl(T, "onEnd")) {
                swallow(inst.onEnd(wrap(s)));
            }
        }
        pub fn onConnectError(ext: Ext, s: *us_socket_t, code: i32) void {
            const inst = ext.*;
            s.close(.failure);
            if (inst) |t| {
                if (@hasDecl(T, "onConnectError")) {
                    swallow(t.onConnectError(wrap(s), code));
                }
            }
        }
        pub fn onConnectingError(c: *ConnectingSocket, code: i32) void {
            const OptT = ?*T;
            const inst = c.ext(OptT).* orelse return;
            if (@hasDecl(T, "onConnectError")) {
                swallow(inst.onConnectError(S.fromConnecting(c), code));
            }
        }
        pub fn onHandshake(ext: Ext, s: *us_socket_t, ok: bool, err: uws.us_bun_verify_error_t) void {
            const inst = ext.* orelse return;
            if (@hasDecl(T, "onHandshake")) {
                swallow(inst.onHandshake(wrap(s), @intFromBool(ok), err));
            }
        }
        pub fn onFd(ext: Ext, s: *us_socket_t, fd: i32) void {
            const inst = ext.* orelse return;
            if (@hasDecl(T, "onFd")) {
                swallow(inst.onFd(wrap(s), fd));
            }
        }
    };
}

pub fn BunSocket(comptime ssl: bool): type {
    return PtrHandler(api.NewSocket(ssl), ssl);
}

pub fn BunListener(comptime ssl: bool): type {
    const S = uws.NewSocketHandler(ssl);
    const NS = api.NewSocket(ssl);
    return struct {
        pub fn onOpen(s: *us_socket_t, _: bool, _: string) void {
            const listener = s.group().owner(api.Listener);
            const ns = api.Listener.onCreate(ssl, listener, S.from(s));
            swallow(ns.onOpen(S.from(s)));
        }
        pub fn onClose(s: *us_socket_t, code: i32, reason: *!?) void {
            if (s.ext(?*NS).*) |ns| {
                swallow(ns.onClose(S.from(s), code, reason));
            }
        }
        pub fn onData(s: *us_socket_t, data: string) void {
            if (s.ext(?*NS).*) |ns| {
                swallow(ns.onData(S.from(s), data));
            }
        }
        pub fn onWritable(s: *us_socket_t) void {
            if (s.ext(?*NS).*) |ns| {
                swallow(ns.onWritable(S.from(s)));
            }
        }
        pub fn onEnd(s: *us_socket_t) void {
            if (s.ext(?*NS).*) |ns| {
                swallow(ns.onEnd(S.from(s)));
            }
        }
        pub fn onTimeout(s: *us_socket_t) void {
            if (s.ext(?*NS).*) |ns| {
                swallow(ns.onTimeout(S.from(s)));
            }
        }
        pub fn onHandshake(s: *us_socket_t, ok: bool, err: uws.us_bun_verify_error_t) void {
            if (s.ext(?*NS).*) |ns| {
                swallow(ns.onHandshake(S.from(s), @intFromBool(ok), err));
            }
        }
    };
}

pub fn NsHandler(comptime Owner: type, comptime H: type, comptime ssl: bool): type {
    const S = uws.NewSocketHandler(ssl);
    return struct {
        pub const Ext = @FieldType(struct { p: *((*Owner)?) }, "p");
        inline fn wrap(s: *us_socket_t) S {
            return S.from(s);
        }
        pub fn onOpen(ext: Ext, s: *us_socket_t, _: bool, _: string) void {
            const inst = ext.* orelse return;
            if (@hasDecl(H, "onOpen")) {
                swallow(H.onOpen(inst, wrap(s)));
            }
        }
        pub fn onData(ext: Ext, s: *us_socket_t, data: string) void {
            const inst = ext.* orelse return;
            if (@hasDecl(H, "onData")) {
                swallow(H.onData(inst, wrap(s), data));
            }
        }
        pub fn onWritable(ext: Ext, s: *us_socket_t) void {
            const inst = ext.* orelse return;
            if (@hasDecl(H, "onWritable")) {
                swallow(H.onWritable(inst, wrap(s)));
            }
        }
        pub fn onClose(ext: Ext, s: *us_socket_t, code: i32, reason: *!?) void {
            const inst = ext.* orelse return;
            if (@hasDecl(H, "onClose")) {
                swallow(H.onClose(inst, wrap(s), code, reason));
            }
        }
        pub fn onTimeout(ext: Ext, s: *us_socket_t) void {
            const inst = ext.* orelse return;
            if (@hasDecl(H, "onTimeout")) {
                swallow(H.onTimeout(inst, wrap(s)));
            }
        }
        pub fn onLongTimeout(ext: Ext, s: *us_socket_t) void {
            const inst = ext.* orelse return;
            if (@hasDecl(H, "onLongTimeout")) {
                swallow(H.onLongTimeout(inst, wrap(s)));
            }
        }
        pub fn onEnd(ext: Ext, s: *us_socket_t) void {
            const inst = ext.* orelse return;
            if (@hasDecl(H, "onEnd")) {
                swallow(H.onEnd(inst, wrap(s)));
            }
        }
        pub fn onConnectError(ext: Ext, s: *us_socket_t, code: i32) void {
            const inst = ext.*;
            s.close(.failure);
            if (inst) |t| {
                if (@hasDecl(H, "onConnectError")) {
                    swallow(H.onConnectError(t, wrap(s), code));
                }
            }
        }
        pub fn onConnectingError(c: *ConnectingSocket, code: i32) void {
            const OptOwner = ?*Owner;
            const inst = c.ext(OptOwner).* orelse return;
            if (@hasDecl(H, "onConnectError")) {
                swallow(H.onConnectError(inst, S.fromConnecting(c), code));
            }
        }
        pub fn onHandshake(ext: Ext, s: *us_socket_t, ok: bool, err: uws.us_bun_verify_error_t) void {
            const inst = ext.* orelse return;
            if (@hasDecl(H, "onHandshake") and @TypeOf(H.onHandshake) != @TypeOf(null)) {
                swallow(H.onHandshake(inst, wrap(s), @intFromBool(ok), err));
            }
        }
    };
}

pub fn HTTPClient(comptime ssl: bool): type {
    const H = bun.http.NewHTTPContext(ssl).Handler;
    const S = uws.NewSocketHandler(ssl);
    return struct {
        pub const Ext = @FieldType(struct { p: *(*!?) }, "p");
        inline fn wrap(s: *us_socket_t) S {
            return S.from(s);
        }
        inline fn fwd(ext: Ext, comptime name: []const u8, args: anytype) void {
            if (@hasDecl(H, name) and @TypeOf(@field(H, name)) != @TypeOf(null)) {
                swallow(@call(.auto, @field(H, name), .{ext.* orelse return} ++ args));
            }
        }
        pub fn onOpen(ext: Ext, s: *us_socket_t, _: bool, _: string) void {
            fwd(ext, "onOpen", .{wrap(s)});
        }
        pub fn onData(ext: Ext, s: *us_socket_t, data: string) void {
            fwd(ext, "onData", .{ wrap(s), data });
        }
        pub fn onWritable(ext: Ext, s: *us_socket_t) void {
            fwd(ext, "onWritable", .{wrap(s)});
        }
        pub fn onClose(ext: Ext, s: *us_socket_t, code: i32, reason: *!?) void {
            fwd(ext, "onClose", .{ wrap(s), code, reason });
        }
        pub fn onTimeout(ext: Ext, s: *us_socket_t) void {
            fwd(ext, "onTimeout", .{wrap(s)});
        }
        pub fn onLongTimeout(ext: Ext, s: *us_socket_t) void {
            fwd(ext, "onLongTimeout", .{wrap(s)});
        }
        pub fn onEnd(ext: Ext, s: *us_socket_t) void {
            fwd(ext, "onEnd", .{wrap(s)});
        }
        pub fn onConnectError(ext: Ext, s: *us_socket_t, code: i32) void {
            const owner = ext.*;
            s.close(.failure);
            if (@hasDecl(H, "onConnectError") and @TypeOf(H.onConnectError) != @TypeOf(null)) {
                if (owner != null) {
                    swallow(H.onConnectError(owner.?, wrap(s), code));
                }
            }
        }
        pub fn onConnectingError(cs: *ConnectingSocket, code: i32) void {
            if (@hasDecl(H, "onConnectError")) {
                const owner = cs.ext(@TypeOf(@as(*!?, null))).*;
                if (owner != null) {
                    swallow(H.onConnectError(owner.?, S.fromConnecting(cs), code));
                }
            }
        }
        pub fn onHandshake(ext: Ext, s: *us_socket_t, ok: bool, err: uws.us_bun_verify_error_t) void {
            fwd(ext, "onHandshake", .{ wrap(s), @as(i32, @intFromBool(ok)), err });
        }
    };
}

pub fn WSUpgrade(comptime ssl: bool): type {
    return PtrHandler(websocket_upgrade_client.NewHTTPUpgradeClient(ssl), ssl);
}

pub fn WSClient(comptime ssl: bool): type {
    return PtrHandler(websocket_client.NewWebSocketClient(ssl), ssl);
}

pub fn Postgres(comptime ssl: bool): type {
    const C = bun.api.Postgres.PostgresSQLConnection;
    return NsHandler(C, C.SocketHandler(ssl), ssl);
}

pub fn MySQL(comptime ssl: bool): type {
    return NsHandler(mysql.MySQLConnection, mysql.MySQLConnection.SocketHandler(ssl), ssl);
}

pub fn Valkey(comptime ssl: bool): type {
    return NsHandler(js_valkey.JSValkeyClient, js_valkey.SocketHandler(ssl), ssl);
}

type OptSendQueue = (*IPC.SendQueue)?;
type SendQueueExt = *OptSendQueue;

pub struct SpawnIPC {
    const H = IPC.IPCHandlers.PosixSocket;
    const S = uws.NewSocketHandler(false);
    pub const Ext = SendQueueExt;
    pub static fn onOpen(_: **IPC.SendQueue?, _: *us_socket_t, _: bool, _: string) {}
    pub static fn onData(ext: **IPC.SendQueue?, s: *us_socket_t, data: string) {
        if (inst := ext.*) H.onData(inst, S.from(s), data);
    }
    pub static fn onFd(ext: **IPC.SendQueue?, s: *us_socket_t, fd: i32) {
        if (inst := ext.*) H.onFd(inst, S.from(s), fd);
    }
    pub static fn onWritable(ext: **IPC.SendQueue?, s: *us_socket_t) {
        if (inst := ext.*) H.onWritable(inst, S.from(s));
    }
    pub static fn onClose(ext: **IPC.SendQueue?, s: *us_socket_t, code: i32, reason: *!?) {
        if (inst := ext.*) H.onClose(inst, S.from(s), code, reason);
    }
    pub static fn onTimeout(ext: **IPC.SendQueue?, s: *us_socket_t) {
        if (inst := ext.*) H.onTimeout(inst, S.from(s));
    }
    pub static fn onEnd(ext: **IPC.SendQueue?, s: *us_socket_t) {
        if (inst := ext.*) H.onEnd(inst, S.from(s));
    }
}
