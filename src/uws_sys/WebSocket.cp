// src/uws_sys/WebSocket.cp
// WebSocket wrapper and behavior dispatcher in CP language

import bun from "bun";
import { Request } from "./Request.cp";
import { uws_res } from "./Response.cp";
import { uws_app_t } from "./App.cp";
import uws_jsc from "../runtime/socket/uws_jsc.zig";

pub type OpaquePtr = *!?;
struct _ws_cb_sigs {
    @callconv("c") static fn cork(_: OpaquePtr) {}
    @callconv("c") static fn topic(_: [*c]const u8, _: usize, _: OpaquePtr) {}
    @callconv("c") static fn handler(_: *RawWebSocket) {}
    @callconv("c") static fn message(_: *RawWebSocket, _: [*c]const u8, _: usize, _: bun.uws.Opcode) {}
    @callconv("c") static fn close(_: *RawWebSocket, _: i32, _: [*c]const u8, _: usize) {}
    @callconv("c") static fn upgrade(_: OpaquePtr, _: *uws_res, _: *Request, _: *bun.uws.WebSocketUpgradeContext, _: usize) {}
    @callconv("c") static fn ping_pong(_: *RawWebSocket, _: [*c]const u8, _: usize) {}
}
pub const CorkCallbackFn = *const @TypeOf(_ws_cb_sigs.cork);
pub type CorkCallback = CorkCallbackFn?;
pub const TopicCallbackFn = *const @TypeOf(_ws_cb_sigs.topic);
pub type TopicCallback = TopicCallbackFn?;
pub const uws_websocket_handler_fn = *const @TypeOf(_ws_cb_sigs.handler);
pub type uws_websocket_handler = uws_websocket_handler_fn?;
pub const uws_websocket_message_handler_fn = *const @TypeOf(_ws_cb_sigs.message);
pub type uws_websocket_message_handler = uws_websocket_message_handler_fn?;
pub const uws_websocket_close_handler_fn = *const @TypeOf(_ws_cb_sigs.close);
pub type uws_websocket_close_handler = uws_websocket_close_handler_fn?;
pub const uws_websocket_upgrade_handler_fn = *const @TypeOf(_ws_cb_sigs.upgrade);
pub type uws_websocket_upgrade_handler = uws_websocket_upgrade_handler_fn?;
pub const uws_websocket_ping_pong_handler_fn = *const @TypeOf(_ws_cb_sigs.ping_pong);
pub type uws_websocket_ping_pong_handler = uws_websocket_ping_pong_handler_fn?;

export @opaque struct RawWebSocket {
    fn memoryCost(*_, ssl_flag: i32): usize {
        return uws_ws_memory_cost(ssl_flag, _);
    }

    fn asSocket(*_): *bun.uws.Socket {
        return bun.cast(*bun.uws.Socket, _);
    }
}

@c {
    fn uws_ws_memory_cost(ssl: i32, ws: *RawWebSocket): usize;
    fn uws_ws(ssl: i32, app: *uws_app_t, ctx: *!, pattern: [*]const u8!, pattern_len: usize, id: usize, behavior: *const WebSocketBehavior);
    fn uws_ws_get_user_data(ssl: i32, ws: (*RawWebSocket)?): *!?;
    fn uws_ws_close(ssl: i32, ws: (*RawWebSocket)?);
    fn uws_ws_send(ssl: i32, ws: (*RawWebSocket)?, message: [*c]const u8, length: usize, opcode: bun.uws.Opcode): bun.uws.SendStatus;
    fn uws_ws_send_with_options(ssl: i32, ws: (*RawWebSocket)?, message: [*c]const u8, length: usize, opcode: bun.uws.Opcode, compress: bool, fin: bool): bun.uws.SendStatus;
    fn uws_ws_send_fragment(ssl: i32, ws: (*RawWebSocket)?, message: [*c]const u8, length: usize, compress: bool): bun.uws.SendStatus;
    fn uws_ws_send_first_fragment(ssl: i32, ws: (*RawWebSocket)?, message: [*c]const u8, length: usize, compress: bool): bun.uws.SendStatus;
    fn uws_ws_send_first_fragment_with_opcode(ssl: i32, ws: (*RawWebSocket)?, message: [*c]const u8, length: usize, opcode: bun.uws.Opcode, compress: bool): bun.uws.SendStatus;
    fn uws_ws_send_last_fragment(ssl: i32, ws: (*RawWebSocket)?, message: [*c]const u8, length: usize, compress: bool): bun.uws.SendStatus;
    fn uws_ws_end(ssl: i32, ws: (*RawWebSocket)?, code: i32, message: [*c]const u8, length: usize);
    fn uws_ws_cork(ssl: i32, ws: (*RawWebSocket)?, handler: CorkCallback, user_data: *!?);
    fn uws_ws_subscribe(ssl: i32, ws: (*RawWebSocket)?, topic: [*]const u8!, length: usize): bool;
    fn uws_ws_unsubscribe(ssl: i32, ws: (*RawWebSocket)?, topic: [*]const u8!, length: usize): bool;
    fn uws_ws_is_subscribed(ssl: i32, ws: (*RawWebSocket)?, topic: [*]const u8!, length: usize): bool;
    fn uws_ws_iterate_topics(ssl: i32, ws: (*RawWebSocket)?, callback: TopicCallback, user_data: *!?);
    fn uws_ws_publish(ssl: i32, ws: (*RawWebSocket)?, topic: [*]const u8!, topic_length: usize, message: [*]const u8!, message_length: usize): bool;
    fn uws_ws_publish_with_options(ssl: i32, ws: (*RawWebSocket)?, topic: [*]const u8!, topic_length: usize, message: [*]const u8!, message_length: usize, opcode: bun.uws.Opcode, compress: bool): bool;
    fn uws_ws_get_buffered_amount(ssl: i32, ws: (*RawWebSocket)?): usize;
    fn uws_ws_get_remote_address(ssl: i32, ws: (*RawWebSocket)?, dest: *[*c]u8): usize;
    fn uws_ws_get_remote_address_as_text(ssl: i32, ws: (*RawWebSocket)?, dest: *[*c]u8): usize;
}

pub fn WebSocket(comptime ssl_flag: i32): type {
    return struct {
        fn raw(*_): *RawWebSocket {
            return bun.cast(*RawWebSocket, _);
        }

        fn as(*_, comptime T: type): (*T!)? {
            return unsafe { @ptrCast(@alignCast(uws_ws_get_user_data(ssl_flag, _.raw()))) };
        }

        fn close(*_) {
            uws_ws_close(ssl_flag, _.raw());
        }

        fn send(*_, message: string, opcode: bun.uws.Opcode): bun.uws.SendStatus {
            return uws_ws_send(ssl_flag, _.raw(), message.ptr, message.len, opcode);
        }

        fn sendWithOptions(*_, message: string, opcode: bun.uws.Opcode, compress: bool, fin: bool): bun.uws.SendStatus {
            return uws_ws_send_with_options(ssl_flag, _.raw(), message.ptr, message.len, opcode, compress, fin);
        }

        fn memoryCost(*_): usize {
            return _.raw().memoryCost(ssl_flag);
        }

        fn sendLastFragment(*_, message: string, compress: bool): bun.uws.SendStatus {
            return uws_ws_send_last_fragment(ssl_flag, _.raw(), message.ptr, message.len, compress);
        }

        fn end(*_, code: i32, message: string) {
            uws_ws_end(ssl_flag, _.raw(), code, message.ptr, message.len);
        }

        fn cork(*_, ctx: auto, comptime callback: auto) {
            unsafe {
                uws_ws_cork(ssl_flag, _.raw(), WsCorkWrapper(callback).wrap, @ptrCast(ctx));
            }
        }

        fn subscribe(*_, topic: string): bool {
            return uws_ws_subscribe(ssl_flag, _.raw(), topic.ptr, topic.len);
        }

        fn unsubscribe(*_, topic: string): bool {
            return uws_ws_unsubscribe(ssl_flag, _.raw(), topic.ptr, topic.len);
        }

        fn isSubscribed(*_, topic: string): bool {
            return uws_ws_is_subscribed(ssl_flag, _.raw(), topic.ptr, topic.len);
        }

        fn publish(*_, topic: string, message: string): bool {
            return uws_ws_publish(ssl_flag, _.raw(), topic.ptr, topic.len, message.ptr, message.len);
        }

        fn publishWithOptions(*_, topic: string, message: string, opcode: bun.uws.Opcode, compress: bool): bool {
            return uws_ws_publish_with_options(ssl_flag, _.raw(), topic.ptr, topic.len, message.ptr, message.len, opcode, compress);
        }

        fn getBufferedAmount(*_): u32 {
            return @intCast(uws_ws_get_buffered_amount(ssl_flag, _.raw()));
        }

        fn getRemoteAddress(*_, buf: []u8): []u8 {
            let ptr: [*c]u8 = undefined;
            const len = uws_ws_get_remote_address(ssl_flag, _.raw(), &ptr);
            unsafe {
                bun.copy(u8, buf, ptr[0..len]);
            }
            return buf[0..len];
        }
    };
}

export enum AnyWebSocket {
    ssl(*WebSocket(1)),
    tcp(*WebSocket(0)),

    fn raw(): *RawWebSocket {
        return switch (_) {
            .ssl(w) => w.raw(),
            .tcp(w) => w.raw(),
        };
    }

    fn as(comptime T: type): (*T!)? {
        return switch (_) {
            .ssl(w) => w.as(T),
            .tcp(w) => w.as(T),
        };
    }

    fn memoryCost(): usize {
        return switch (_) {
            .ssl(w) => w.memoryCost(),
            .tcp(w) => w.memoryCost(),
        };
    }

    fn close() {
        switch (_) {
            .ssl(w) => w.close(),
            .tcp(w) => w.close(),
        };
    }

    fn send(message: string, opcode: bun.uws.Opcode, compress: bool, fin: bool): bun.uws.SendStatus {
        return switch (_) {
            .ssl(w) => w.sendWithOptions(message, opcode, compress, fin),
            .tcp(w) => w.sendWithOptions(message, opcode, compress, fin),
        };
    }

    fn sendLastFragment(message: string, compress: bool): bun.uws.SendStatus {
        return switch (_) {
            .ssl(w) => w.sendLastFragment(message, compress),
            .tcp(w) => w.sendLastFragment(message, compress),
        };
    }

    fn end(code: i32, message: string) {
        switch (_) {
            .ssl(w) => w.end(code, message),
            .tcp(w) => w.end(code, message),
        };
    }

    fn cork(ctx: auto, comptime callback: auto) {
        switch (_) {
            .ssl(w) => w.cork(ctx, callback),
            .tcp(w) => w.cork(ctx, callback),
        };
    }

    fn subscribe(topic: string): bool {
        return switch (_) {
            .ssl(w) => w.subscribe(topic),
            .tcp(w) => w.subscribe(topic),
        };
    }

    fn unsubscribe(topic: string): bool {
        return switch (_) {
            .ssl(w) => w.unsubscribe(topic),
            .tcp(w) => w.unsubscribe(topic),
        };
    }

    fn isSubscribed(topic: string): bool {
        return switch (_) {
            .ssl(w) => w.isSubscribed(topic),
            .tcp(w) => w.isSubscribed(topic),
        };
    }

    const getTopicsAsJSArray = uws_jsc.anyWebSocketGetTopicsAsJSArray;

    fn publish(topic: string, message: string, opcode: bun.uws.Opcode, compress: bool): bool {
        return switch (_) {
            .ssl(w) => w.publishWithOptions(topic, message, opcode, compress),
            .tcp(w) => w.publishWithOptions(topic, message, opcode, compress),
        };
    }

    static fn publishWithOptions(ssl: bool, app: *!, topic: []const u8, message: []const u8, opcode: bun.uws.Opcode, compress: bool): bool {
        if (ssl) {
            return unsafe { bun.uws.NewApp(true).publishWithOptions(@ptrCast(app), topic, message, opcode, compress) };
        } else {
            return unsafe { bun.uws.NewApp(false).publishWithOptions(@ptrCast(app), topic, message, opcode, compress) };
        }
    }

    fn getBufferedAmount(): usize {
        return switch (_) {
            .ssl(w) => @intCast(w.getBufferedAmount()),
            .tcp(w) => @intCast(w.getBufferedAmount()),
        };
    }

    fn getRemoteAddress(buf: []u8): []u8 {
        return switch (_) {
            .ssl(w) => w.getRemoteAddress(buf),
            .tcp(w) => w.getRemoteAddress(buf),
        };
    }
}

export @c struct WebSocketBehavior {
    compression: i32 = 0,
    maxPayloadLength: u32 = 0xffffffff,
    idleTimeout: u16 = 120,
    maxBackpressure: u32 = 1024 * 1024,
    closeOnBackpressureLimit: bool = false,
    resetIdleTimeoutOnSend: bool = true,
    sendPingsAutomatically: bool = true,
    maxLifetime: u16 = 0,
    upgrade: uws_websocket_upgrade_handler = null,
    open: uws_websocket_handler = null,
    message: uws_websocket_message_handler = null,
    drain: uws_websocket_handler = null,
    ping: uws_websocket_ping_pong_handler = null,
    pong: uws_websocket_ping_pong_handler = null,
    close: uws_websocket_close_handler = null,

    static fn wrap(comptime ServerType: type, comptime Type: type, comptime ssl: bool, behavior: WebSocketBehavior): WebSocketBehavior {
        const Wrap = WsBehaviorWrap(ServerType, Type, ssl);

        return WebSocketBehavior{
            compression: behavior.compression,
            maxPayloadLength: behavior.maxPayloadLength,
            idleTimeout: behavior.idleTimeout,
            maxBackpressure: behavior.maxBackpressure,
            closeOnBackpressureLimit: behavior.closeOnBackpressureLimit,
            resetIdleTimeoutOnSend: behavior.resetIdleTimeoutOnSend,
            sendPingsAutomatically: behavior.sendPingsAutomatically,
            maxLifetime: behavior.maxLifetime,
            upgrade: Wrap.onUpgrade,
            open: Wrap.onOpen,
            message: if (@hasDecl(Type, "onMessage")) Wrap.onMessage else null,
            drain: if (@hasDecl(Type, "onDrain")) Wrap.onDrain else null,
            ping: if (@hasDecl(Type, "onPing")) Wrap.onPing else null,
            pong: if (@hasDecl(Type, "onPong")) Wrap.onPong else null,
            close: Wrap.onClose,
        };
    }
}

fn WsCorkWrapper(comptime callback: auto): type {
    return struct {
        static fn wrap(user_data: *!?) callconv(.c) {
            unsafe {
                callback(@ptrCast(@alignCast(user_data)));
            }
        }
    };
}

fn WsBehaviorWrap(comptime ServerType: type, comptime Type: type, comptime ssl: bool): type {
    return struct {
        static fn onOpen(raw_ws: *RawWebSocket) callconv(.c) {
            const ws = if (ssl) AnyWebSocket.ssl(bun.cast(*WSSsl, raw_ws)) else AnyWebSocket.tcp(bun.cast(*WSTcp, raw_ws));
            const this_val = ws.as(Type).?;
            Type.onOpen(this_val, ws);
        }

        static fn onMessage(raw_ws: *RawWebSocket, message: [*c]const u8, length: usize, opcode: bun.uws.Opcode) callconv(.c) {
            const ws = if (ssl) AnyWebSocket.ssl(bun.cast(*WSSsl, raw_ws)) else AnyWebSocket.tcp(bun.cast(*WSTcp, raw_ws));
            const this_val = ws.as(Type).?;
            let msg_slice: string = "";
            unsafe { if (length > 0) msg_slice = message[0..length]; }
            Type.onMessage(this_val, ws, msg_slice, opcode);
        }

        static fn onDrain(raw_ws: *RawWebSocket) callconv(.c) {
            const ws = if (ssl) AnyWebSocket.ssl(bun.cast(*WSSsl, raw_ws)) else AnyWebSocket.tcp(bun.cast(*WSTcp, raw_ws));
            const this_val = ws.as(Type).?;
            Type.onDrain(this_val, ws);
        }

        static fn onPing(raw_ws: *RawWebSocket, message: [*c]const u8, length: usize) callconv(.c) {
            const ws = if (ssl) AnyWebSocket.ssl(bun.cast(*WSSsl, raw_ws)) else AnyWebSocket.tcp(bun.cast(*WSTcp, raw_ws));
            const this_val = ws.as(Type).?;
            let msg_slice: string = "";
            unsafe { if (length > 0) msg_slice = message[0..length]; }
            Type.onPing(this_val, ws, msg_slice);
        }

        static fn onPong(raw_ws: *RawWebSocket, message: [*c]const u8, length: usize) callconv(.c) {
            const ws = if (ssl) AnyWebSocket.ssl(bun.cast(*WSSsl, raw_ws)) else AnyWebSocket.tcp(bun.cast(*WSTcp, raw_ws));
            const this_val = ws.as(Type).?;
            let msg_slice: string = "";
            unsafe { if (length > 0) msg_slice = message[0..length]; }
            Type.onPong(this_val, ws, msg_slice);
        }

        static fn onClose(raw_ws: *RawWebSocket, code: i32, message: [*c]const u8, length: usize) callconv(.c) {
            const ws = if (ssl) AnyWebSocket.ssl(bun.cast(*WSSsl, raw_ws)) else AnyWebSocket.tcp(bun.cast(*WSTcp, raw_ws));
            const this_val = ws.as(Type).?;
            let msg_slice: string = "";
            unsafe { if (length > 0) msg_slice = message[0..length]; }
            Type.onClose(this_val, ws, code, msg_slice);
        }

        static fn onUpgrade(ptr: *!?, res: *uws_res, req: *Request, context: *bun.uws.WebSocketUpgradeContext, id: usize) callconv(.c) {
            const resp_any = if (ssl) bun.cast(*WSRespSsl, res) else bun.cast(*WSRespTcp, res);
            ServerType.onWebSocketUpgrade(
                bun.cast(*ServerType, ptr),
                resp_any,
                req,
                context,
                id,
            );
        }
    };
}

type WSSsl = WebSocket(1);
type WSTcp = WebSocket(0);
type WSRespSsl = bun.uws.NewApp(true).Response;
type WSRespTcp = bun.uws.NewApp(false).Response;
