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
        static inline fn wrap(s: *us_socket_t): S {
            return S.from(s);
        }
        pub static fn onOpen(ext: Ext, s: *us_socket_t, _: bool, _: string) {
            const inst = ext.* ?? return;
            if (@hasDecl(T, "onOpen")) {
                swallow(inst.onOpen(wrap(s)));
            }
        }
        pub static fn onData(ext: Ext, s: *us_socket_t, data: string) {
            const inst = ext.* ?? return;
            if (@hasDecl(T, "onData")) {
                swallow(inst.onData(wrap(s), data));
            }
        }
        pub static fn onWritable(ext: Ext, s: *us_socket_t) {
            const inst = ext.* ?? return;
            if (@hasDecl(T, "onWritable")) {
                swallow(inst.onWritable(wrap(s)));
            }
        }
        pub static fn onClose(ext: Ext, s: *us_socket_t, code: i32, reason: *!?) {
            const inst = ext.* ?? return;
            if (@hasDecl(T, "onClose")) {
                swallow(inst.onClose(wrap(s), code, reason));
            }
        }
        pub static fn onTimeout(ext: Ext, s: *us_socket_t) {
            const inst = ext.* ?? return;
            if (@hasDecl(T, "onTimeout")) {
                swallow(inst.onTimeout(wrap(s)));
            }
        }
        pub static fn onLongTimeout(ext: Ext, s: *us_socket_t) {
            const inst = ext.* ?? return;
            if (@hasDecl(T, "onLongTimeout")) {
                swallow(inst.onLongTimeout(wrap(s)));
            }
        }
        pub static fn onEnd(ext: Ext, s: *us_socket_t) {
            const inst = ext.* ?? return;
            if (@hasDecl(T, "onEnd")) {
                swallow(inst.onEnd(wrap(s)));
            }
        }
        pub static fn onConnectError(ext: Ext, s: *us_socket_t, code: i32) {
            const inst = ext.*;
            s.close(.failure);
            if (t := inst) {
                if (@hasDecl(T, "onConnectError")) {
                    swallow(t.onConnectError(wrap(s), code));
                }
            }
        }
        pub static fn onConnectingError(c: *ConnectingSocket, code: i32) {
            const OptT = (*T)?;
            const inst = c.ext(OptT).* ?? return;
            if (@hasDecl(T, "onConnectError")) {
                swallow(inst.onConnectError(S.fromConnecting(c), code));
            }
        }
        pub static fn onHandshake(ext: Ext, s: *us_socket_t, ok: bool, err: uws.us_bun_verify_error_t) {
            const inst = ext.* ?? return;
            if (@hasDecl(T, "onHandshake")) {
                swallow(inst.onHandshake(wrap(s), @intFromBool(ok), err));
            }
        }
        pub static fn onFd(ext: Ext, s: *us_socket_t, fd: i32) {
            const inst = ext.* ?? return;
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
    const OptNS = @FieldType(struct { p: (*NS)? }, "p");
    return struct {
        pub static fn onOpen(s: *us_socket_t, _: bool, _: string) {
            const listener = s.group().owner(api.Listener);
            const ns = api.Listener.onCreate(ssl, listener, S.from(s));
            swallow(ns.onOpen(S.from(s)));
        }
        pub static fn onClose(s: *us_socket_t, code: i32, reason: *!?) {
            if (ns := s.ext(OptNS).*) {
                swallow(ns.onClose(S.from(s), code, reason));
            }
        }
        pub static fn onData(s: *us_socket_t, data: string) {
            if (ns := s.ext(OptNS).*) {
                swallow(ns.onData(S.from(s), data));
            }
        }
        pub static fn onWritable(s: *us_socket_t) {
            if (ns := s.ext(OptNS).*) {
                swallow(ns.onWritable(S.from(s)));
            }
        }
        pub static fn onEnd(s: *us_socket_t) {
            if (ns := s.ext(OptNS).*) {
                swallow(ns.onEnd(S.from(s)));
            }
        }
        pub static fn onTimeout(s: *us_socket_t) {
            if (ns := s.ext(OptNS).*) {
                swallow(ns.onTimeout(S.from(s)));
            }
        }
        pub static fn onHandshake(s: *us_socket_t, ok: bool, err: uws.us_bun_verify_error_t) {
            if (ns := s.ext(OptNS).*) {
                swallow(ns.onHandshake(S.from(s), @intFromBool(ok), err));
            }
        }
    };
}

pub fn NsHandler(comptime Owner: type, comptime H: type, comptime ssl: bool): type {
    const S = uws.NewSocketHandler(ssl);
    return struct {
        pub const Ext = @FieldType(struct { p: *((*Owner)?) }, "p");
        static inline fn wrap(s: *us_socket_t): S {
            return S.from(s);
        }
        pub static fn onOpen(ext: Ext, s: *us_socket_t, _: bool, _: string) {
            const inst = ext.* ?? return;
            if (@hasDecl(H, "onOpen")) {
                swallow(H.onOpen(inst, wrap(s)));
            }
        }
        pub static fn onData(ext: Ext, s: *us_socket_t, data: string) {
            const inst = ext.* ?? return;
            if (@hasDecl(H, "onData")) {
                swallow(H.onData(inst, wrap(s), data));
            }
        }
        pub static fn onWritable(ext: Ext, s: *us_socket_t) {
            const inst = ext.* ?? return;
            if (@hasDecl(H, "onWritable")) {
                swallow(H.onWritable(inst, wrap(s)));
            }
        }
        pub static fn onClose(ext: Ext, s: *us_socket_t, code: i32, reason: *!?) {
            const inst = ext.* ?? return;
            if (@hasDecl(H, "onClose")) {
                swallow(H.onClose(inst, wrap(s), code, reason));
            }
        }
        pub static fn onTimeout(ext: Ext, s: *us_socket_t) {
            const inst = ext.* ?? return;
            if (@hasDecl(H, "onTimeout")) {
                swallow(H.onTimeout(inst, wrap(s)));
            }
        }
        pub static fn onLongTimeout(ext: Ext, s: *us_socket_t) {
            const inst = ext.* ?? return;
            if (@hasDecl(H, "onLongTimeout")) {
                swallow(H.onLongTimeout(inst, wrap(s)));
            }
        }
        pub static fn onEnd(ext: Ext, s: *us_socket_t) {
            const inst = ext.* ?? return;
            if (@hasDecl(H, "onEnd")) {
                swallow(H.onEnd(inst, wrap(s)));
            }
        }
        pub static fn onConnectError(ext: Ext, s: *us_socket_t, code: i32) {
            const inst = ext.*;
            s.close(.failure);
            if (t := inst) {
                if (@hasDecl(H, "onConnectError")) {
                    swallow(H.onConnectError(t, wrap(s), code));
                }
            }
        }
        pub static fn onConnectingError(c: *ConnectingSocket, code: i32) {
            const OptOwner = (*Owner)?;
            const inst = c.ext(OptOwner).* ?? return;
            if (@hasDecl(H, "onConnectError")) {
                swallow(H.onConnectError(inst, S.fromConnecting(c), code));
            }
        }
        pub static fn onHandshake(ext: Ext, s: *us_socket_t, ok: bool, err: uws.us_bun_verify_error_t) {
            const inst = ext.* ?? return;
            if (@hasDecl(H, "onHandshake") && @TypeOf(H.onHandshake) != @TypeOf(null)) {
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
        static inline fn wrap(s: *us_socket_t): S {
            return S.from(s);
        }
        static inline fn fwd(ext: Ext, comptime name: []const u8, args: auto) {
            if (@hasDecl(H, name) && @TypeOf(@field(H, name)) != @TypeOf(null)) {
                swallow(@call(.auto, @field(H, name), .{ext.* ?? return} ++ args));
            }
        }
        pub static fn onOpen(ext: Ext, s: *us_socket_t, _: bool, _: string) {
            fwd(ext, "onOpen", .{wrap(s)});
        }
        pub static fn onData(ext: Ext, s: *us_socket_t, data: string) {
            fwd(ext, "onData", .{ wrap(s), data });
        }
        pub static fn onWritable(ext: Ext, s: *us_socket_t) {
            fwd(ext, "onWritable", .{wrap(s)});
        }
        pub static fn onClose(ext: Ext, s: *us_socket_t, code: i32, reason: *!?) {
            fwd(ext, "onClose", .{ wrap(s), code, reason });
        }
        pub static fn onTimeout(ext: Ext, s: *us_socket_t) {
            fwd(ext, "onTimeout", .{wrap(s)});
        }
        pub static fn onLongTimeout(ext: Ext, s: *us_socket_t) {
            fwd(ext, "onLongTimeout", .{wrap(s)});
        }
        pub static fn onEnd(ext: Ext, s: *us_socket_t) {
            fwd(ext, "onEnd", .{wrap(s)});
        }
        pub static fn onConnectError(ext: Ext, s: *us_socket_t, code: i32) {
            const owner = ext.*;
            s.close(.failure);
            if (@hasDecl(H, "onConnectError") && @TypeOf(H.onConnectError) != @TypeOf(null)) {
                if (owner != null) {
                    swallow(H.onConnectError(owner.?, wrap(s), code));
                }
            }
        }
        pub static fn onConnectingError(cs: *ConnectingSocket, code: i32) {
            if (@hasDecl(H, "onConnectError")) {
                const owner = cs.ext(@TypeOf(@as(*!?, null))).*;
                if (owner != null) {
                    swallow(H.onConnectError(owner.?, S.fromConnecting(cs), code));
                }
            }
        }
        pub static fn onHandshake(ext: Ext, s: *us_socket_t, ok: bool, err: uws.us_bun_verify_error_t) {
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
    pub static fn onOpen(_: *(*IPC.SendQueue)?, _: *us_socket_t, _: bool, _: string) {}
    pub static fn onData(ext: *(*IPC.SendQueue)?, s: *us_socket_t, data: string) {
        if (inst := ext.*) H.onData(inst, S.from(s), data);
    }
    pub static fn onFd(ext: *(*IPC.SendQueue)?, s: *us_socket_t, fd: i32) {
        if (inst := ext.*) H.onFd(inst, S.from(s), fd);
    }
    pub static fn onWritable(ext: *(*IPC.SendQueue)?, s: *us_socket_t) {
        if (inst := ext.*) H.onWritable(inst, S.from(s));
    }
    pub static fn onClose(ext: *(*IPC.SendQueue)?, s: *us_socket_t, code: i32, reason: *!?) {
        if (inst := ext.*) H.onClose(inst, S.from(s), code, reason);
    }
    pub static fn onTimeout(ext: *(*IPC.SendQueue)?, s: *us_socket_t) {
        if (inst := ext.*) H.onTimeout(inst, S.from(s));
    }
    pub static fn onEnd(ext: *(*IPC.SendQueue)?, s: *us_socket_t) {
        if (inst := ext.*) H.onEnd(inst, S.from(s));
    }
}
