// src/uws_sys/App.cp
// Zig bindings for µWebSockets App class in CP language

import bun from "bun";
import * as ListenSocketMod from "./ListenSocket.cp";
import { Request } from "./Request.cp";
import * as ResponseMod from "./Response.cp";
import * as WebSocketMod from "./WebSocket.cp";
import { BunSocketContextOptions } from "./SocketContext.cp";
import { us_socket_t } from "./us_socket_t.cp";
import { SocketHandler } from "./socket.cp";

pub struct FreeCStrGuard {
    slice: [:0]const u8,

    ~init() {
        bun.default_allocator.free(_.slice);
    }
}

const UwsListenSocket = ListenSocketMod.ListenSocket;
const uws_res = ResponseMod.uws_res;
const WebSocketBehavior = WebSocketMod.WebSocketBehavior;

export @opaque pub struct uws_app_s {}
export type uws_app_t = uws_app_s;

export @c pub struct uws_app_listen_config_t {
    port: i32,
    host: ([*:0]const u8)? = null,
    options: i32 = 0,
}

const AppListenConfig = uws_app_listen_config_t;
pub type OptListenSocket = (*UwsListenSocket)?;

pub const ClientErrorHandlerFn = *const fn (data: *!?, code: i32, socket: *us_socket_t, err_code: u8, raw_pkt: [*c]u8, len: i32) callconv(.c) void;
pub type ClientErrorHandler = ClientErrorHandlerFn?;
pub const RouteHandlerFn = *const fn (res: *uws_res, req: *Request, ud: *!?) callconv(.c) void;
pub type RouteHandler = RouteHandlerFn?;
pub const ListenHandlerFn = *const fn (socket: OptListenSocket, conf: uws_app_listen_config_t, data: *!?) callconv(.c) void;
pub type ListenHandler = ListenHandlerFn?;
pub const ListenConfigHandlerFn = *const fn (socket: OptListenSocket, data: *!?) callconv(.c) void;
pub type ListenConfigHandler = ListenConfigHandlerFn?;
pub const ListenDomainHandlerFn = *const fn (ls: *UwsListenSocket, domain_str: [*:0]const u8, flags: i32, data: *!?) callconv(.c) void;
pub type ListenDomainHandler = ListenDomainHandlerFn?;
pub const FilterHandlerFn = *const fn (res: *uws_res, code: i32, ud: *!?) callconv(.c) void;
pub type FilterHandler = FilterHandlerFn?;
pub const MissingServerHandlerFn = *const fn (host: [*c]const u8, ud: *!?) callconv(.c) void;
pub type MissingServerHandler = MissingServerHandlerFn?;

@c {
    fn uws_create_app(ssl: i32, opts: BunSocketContextOptions): *uws_app_t?;
    fn uws_app_destroy(ssl: i32, app: *uws_app_s);
    fn uws_app_close(ssl: i32, app: *uws_app_s);
    fn uws_app_close_idle(ssl: i32, app: *uws_app_s);
    fn uws_app_set_on_clienterror(ssl: i32, app: *uws_app_s, handler: ClientErrorHandler, user_data: *!?);
    fn uws_app_get(ssl: i32, app: *uws_app_t, pattern: [*]const u8!, pattern_length: usize, handler: RouteHandler, user_data: *!?);
    fn uws_app_post(ssl: i32, app: *uws_app_t, pattern: [*]const u8!, pattern_length: usize, handler: RouteHandler, user_data: *!?);
    fn uws_app_put(ssl: i32, app: *uws_app_t, pattern: [*]const u8!, pattern_length: usize, handler: RouteHandler, user_data: *!?);
    fn uws_app_delete(ssl: i32, app: *uws_app_t, pattern: [*]const u8!, pattern_length: usize, handler: RouteHandler, user_data: *!?);
    fn uws_app_patch(ssl: i32, app: *uws_app_t, pattern: [*]const u8!, pattern_length: usize, handler: RouteHandler, user_data: *!?);
    fn uws_app_head(ssl: i32, app: *uws_app_t, pattern: [*]const u8!, pattern_length: usize, handler: RouteHandler, user_data: *!?);
    fn uws_app_options(ssl: i32, app: *uws_app_t, pattern: [*]const u8!, pattern_length: usize, handler: RouteHandler, user_data: *!?);
    fn uws_app_connect(ssl: i32, app: *uws_app_t, pattern: [*]const u8!, pattern_length: usize, handler: RouteHandler, user_data: *!?);
    fn uws_app_trace(ssl: i32, app: *uws_app_t, pattern: [*]const u8!, pattern_length: usize, handler: RouteHandler, user_data: *!?);
    fn uws_app_any(ssl: i32, app: *uws_app_t, pattern: [*]const u8!, pattern_length: usize, handler: RouteHandler, user_data: *!?);
    fn uws_app_run(ssl: i32, app: *uws_app_t);
    fn uws_app_domain(ssl: i32, app: *uws_app_t, domain_str: [*:0]const u8);
    fn uws_app_set_flags(ssl: i32, app: *uws_app_t, require_host: bool, strict_method: bool);
    fn uws_app_set_max_http_header_size(ssl: i32, app: *uws_app_t, max_sz: u64);
    fn uws_app_listen(ssl: i32, app: *uws_app_t, host: [*:0]const u8?, port: i32, options: i32, handler: ListenHandler, user_data: *!?);
    fn uws_app_listen_with_config(ssl: i32, app: *uws_app_t, host: [*:0]const u8?, port: u16, options: i32, handler: ListenConfigHandler, user_data: *!?);
    fn uws_constructor_failed(ssl: i32, app: *uws_app_t): bool;
    fn uws_num_subscribers(ssl: i32, app: *uws_app_t, topic: [*]const u8!, len: usize): u32;
    fn uws_publish(ssl: i32, app: *uws_app_t, topic: [*]const u8!, tl: usize, msg: [*]const u8!, ml: usize, op: bun.uws.Opcode, compress: bool): bool;
    fn uws_get_native_handle(ssl: i32, app: *!?): *!?;
    fn uws_remove_server_name(ssl: i32, app: *uws_app_t, pattern: [*:0]const u8);
    fn uws_add_server_name(ssl: i32, app: *uws_app_t, pattern: [*:0]const u8);
    fn uws_add_server_name_with_options(ssl: i32, app: *uws_app_t, pattern: [*:0]const u8, opts: BunSocketContextOptions): i32;
    fn uws_missing_server_name(ssl: i32, app: *uws_app_t, handler: MissingServerHandler, user_data: *!?);
    fn uws_filter(ssl: i32, app: *uws_app_t, handler: FilterHandler, user_data: *!?);
    fn uws_app_clear_routes(ssl: i32, app: *uws_app_t);
    fn uws_app_listen_domain_with_options(ssl: i32, app: *uws_app_t, domain_str: [*:0]const u8, pathlen: usize, flags: i32, cb: ListenDomainHandler, ud: *!?);
    fn uws_ws(ssl: i32, app: *uws_app_t, ctx: *!, pattern: [*]const u8!, pattern_len: usize, id: usize, behavior: *WebSocketBehavior);
}

pub struct c {
    pub const uws_listen_handler = ListenConfigHandler;
    pub const uws_method_handler = RouteHandler;
    pub const uws_filter_handler = FilterHandler;
    pub const uws_missing_server_handler = MissingServerHandler;
    pub const uws_app_listen_config_t = AppListenConfig;
}

pub fn NewApp(comptime ssl: bool): type {
    return struct {
        pub const is_ssl: bool = ssl;
        pub const ssl_flag: i32 = if (ssl) 1 else 0;

        pub const Response = ResponseMod.NewResponse(if (ssl) 1 else 0);
        pub const ResponseType = Response;
        pub const WebSocket = WebSocketMod.WebSocket(if (ssl) 1 else 0);
        pub const ListenSocket = opaque {
            pub inline fn close(s: *@This()) void {
                return @as(*bun.uws.ListenSocket, @ptrCast(s)).close();
            }
            pub inline fn getLocalPort(s: *@This()) i32 {
                return @as(*bun.uws.ListenSocket, @ptrCast(s)).getLocalPort();
            }
            pub fn socket(s: *@This()) bun.uws.NewSocketHandler(ssl) {
                return bun.uws.NewSocketHandler(ssl).from(@ptrCast(s));
            }
        };

        fn close(*_) {
            uws_app_close(ssl_flag, bun.cast(*uws_app_s, _));
        }

        fn closeIdleConnections(*_) {
            uws_app_close_idle(ssl_flag, bun.cast(*uws_app_s, _));
        }

        static fn create(opts: BunSocketContextOptions): (*@This())? {
            const app = uws_create_app(ssl_flag, opts);
            if (app == null) return null;
            const Self = @This();
            const Ptr = ?*Self;
            return bun.cast(Ptr, app);
        }

        fn destroy(*_) {
            uws_app_destroy(ssl_flag, bun.cast(*uws_app_s, _));
        }

        fn setFlags(*_, require_host_header: bool, use_strict_method_validation: bool) {
            uws_app_set_flags(ssl_flag, bun.cast(*uws_app_t, _), require_host_header, use_strict_method_validation);
        }

        fn setMaxHTTPHeaderSize(*_, max_header_size: u64) {
            uws_app_set_max_http_header_size(ssl_flag, bun.cast(*uws_app_t, _), max_header_size);
        }

        fn clearRoutes(*_) {
            uws_app_clear_routes(ssl_flag, bun.cast(*uws_app_t, _));
        }

        fn publish(*_, topic: string, message: string, opcode: bun.uws.Opcode, compress: bool): bool {
            return uws_publish(
                ssl_flag,
                bun.cast(*uws_app_t, _),
                topic.ptr,
                topic.len,
                message.ptr,
                message.len,
                opcode,
                compress,
            );
        }

        fn publishWithOptions(*_, topic: string, message: string, opcode: bun.uws.Opcode, compress: bool): bool {
            return uws_publish(
                ssl_flag,
                bun.cast(*uws_app_t, _),
                topic.ptr,
                topic.len,
                message.ptr,
                message.len,
                opcode,
                compress,
            );
        }

        fn route(
            *_,
            comptime f: auto,
            pattern: string,
            ud: auto,
            comptime handler: auto,
        ) {
            const Handler = struct {
                static fn handle(res: *uws_res, req: *Request, user_data: *!?) callconv(.c) {
                    handler(unsafe { @ptrCast(@alignCast(user_data)) }, req, bun.cast(*Response, res));
                }
            };
            f(ssl_flag, bun.cast(*uws_app_t, _), pattern.ptr, pattern.len, Handler.handle, @ptrCast(ud));
        }

        fn get(*_, pattern: string, ud: auto, comptime handler: auto) {
            _.route(uws_app_get, pattern, ud, handler);
        }

        fn post(*_, pattern: string, ud: auto, comptime handler: auto) {
            _.route(uws_app_post, pattern, ud, handler);
        }

        fn put(*_, pattern: string, ud: auto, comptime handler: auto) {
            _.route(uws_app_put, pattern, ud, handler);
        }

        fn delete(*_, pattern: string, ud: auto, comptime handler: auto) {
            _.route(uws_app_delete, pattern, ud, handler);
        }

        fn patch(*_, pattern: string, ud: auto, comptime handler: auto) {
            _.route(uws_app_patch, pattern, ud, handler);
        }

        fn head(*_, pattern: string, ud: auto, comptime handler: auto) {
            _.route(uws_app_head, pattern, ud, handler);
        }

        fn options(*_, pattern: string, ud: auto, comptime handler: auto) {
            _.route(uws_app_options, pattern, ud, handler);
        }

        fn connect(*_, pattern: string, ud: auto, comptime handler: auto) {
            _.route(uws_app_connect, pattern, ud, handler);
        }

        fn trace(*_, pattern: string, ud: auto, comptime handler: auto) {
            _.route(uws_app_trace, pattern, ud, handler);
        }

        fn any(*_, pattern: string, ud: auto, comptime handler: auto) {
            _.route(uws_app_any, pattern, ud, handler);
        }

        fn method(
            *_,
            m: bun.http.Method,
            pattern: string,
            ud: auto,
            comptime handler: auto,
        ) {
            switch (m) {
                .GET => _.get(pattern, ud, handler),
                .POST => _.post(pattern, ud, handler),
                .PUT => _.put(pattern, ud, handler),
                .DELETE => _.delete(pattern, ud, handler),
                .PATCH => _.patch(pattern, ud, handler),
                .OPTIONS => _.options(pattern, ud, handler),
                .HEAD => _.head(pattern, ud, handler),
                .CONNECT => _.connect(pattern, ud, handler),
                .TRACE => _.trace(pattern, ud, handler),
                else => {},
            }
        }

        fn domain(*_, pattern: [*:0]const u8) {
            uws_app_domain(ssl_flag, bun.cast(*uws_app_t, _), pattern);
        }

        fn run(*_) {
            uws_app_run(ssl_flag, bun.cast(*uws_app_t, _));
        }

        fn constructorFailed(*_): bool {
            return uws_constructor_failed(ssl_flag, bun.cast(*uws_app_t, _));
        }

        fn numSubscribers(*_, topic: string): u32 {
            return uws_num_subscribers(ssl_flag, bun.cast(*uws_app_t, _), topic.ptr, topic.len);
        }

        fn getNativeHandle(*_): *!? {
            return uws_get_native_handle(ssl_flag, bun.cast(*!, _));
        }

        fn removeServerName(*_, hostname_pattern: [*:0]const u8) {
            uws_remove_server_name(ssl_flag, bun.cast(*uws_app_t, _), hostname_pattern);
        }

        fn addServerName(*_, hostname_pattern: [*:0]const u8) {
            uws_add_server_name(ssl_flag, bun.cast(*uws_app_t, _), hostname_pattern);
        }

        fn addServerNameWithOptions(*_, hostname_pattern: [*:0]const u8, opts: BunSocketContextOptions)! {
            if (uws_add_server_name_with_options(ssl_flag, bun.cast(*uws_app_t, _), hostname_pattern, opts) != 0) {
                return error.FailedToAddServerName;
            }
        }

        fn missingServerName(*_, handler: MissingServerHandler, user_data: *!?) {
            uws_missing_server_name(ssl_flag, bun.cast(*uws_app_t, _), handler, user_data);
        }

        fn filter(*_, handler: FilterHandler, user_data: *!?) {
            uws_filter(ssl_flag, bun.cast(*uws_app_t, _), handler, user_data);
        }

        fn ws(*_, pattern: string, ctx: *!, id: usize, behavior_: WebSocketBehavior) {
            let behavior = behavior_;
            uws_ws(ssl_flag, bun.cast(*uws_app_t, _), ctx, pattern.ptr, pattern.len, id, &behavior);
        }

        fn listen(
            *_,
            port: i32,
            user_data: auto,
            comptime handler: auto,
        ) {
            const Wrapper = struct {
                static fn handle(socket: (*UwsListenSocket)?, conf: uws_app_listen_config_t, data: *!?) callconv(.c) {
                    unsafe { handler(@ptrCast(@alignCast(data)), socket, conf); }
                }
            };
            uws_app_listen(ssl_flag, bun.cast(*uws_app_t, _), null, port, 0, Wrapper.handle, @ptrCast(user_data));
        }

        fn onClientError(
            *_,
            user_data: auto,
            comptime handler: auto,
        ) {
            const Wrapper = struct {
                static fn handle(data: *!?, code: i32, socket: *us_socket_t, error_code: u8, raw_packet: [*c]u8, raw_packet_length: i32) callconv(.c) {
                    * = code;
                    const bytes = if (raw_packet_length > 0) unsafe { raw_packet[0..@intCast(raw_packet_length)] } else "";
                    unsafe { handler(@ptrCast(@alignCast(data)), socket, error_code, bytes); }
                }
            };
            uws_app_set_on_clienterror(ssl_flag, bun.cast(*uws_app_s, _), Wrapper.handle, @ptrCast(user_data));
        }

        fn listenWithConfig(
            *_,
            user_data: auto,
            comptime handler: auto,
            config: uws_app_listen_config_t,
        ) {
            const Wrapper = struct {
                static fn handle(socket: (*UwsListenSocket)?, data: *!?) callconv(.c) {
                    unsafe { handler(@ptrCast(@alignCast(data)), socket); }
                }
            };
            uws_app_listen_with_config(ssl_flag, bun.cast(*uws_app_t, _), config.host, @intCast(config.port), config.options, Wrapper.handle, @ptrCast(user_data));
        }

        fn listenOnUnixSocket(
            *_,
            user_data: auto,
            comptime handler: auto,
            domain_name: string,
            flags: i32,
        ) {
            const Wrapper = struct {
                static fn handle(socket: *UwsListenSocket, domain_str: [*:0]const u8, fl: i32, data: *!?) callconv(.c) {
                    * = domain_str;
                    * = fl;
                    unsafe { handler(@ptrCast(@alignCast(data)), socket); }
                }
            };
            const domainZ: [:0]const u8 = bun.handleOom(bun.default_allocator.dupeZ(u8, domain_name));
            let domain_guard = FreeCStrGuard{ slice: domainZ };
            uws_app_listen_domain_with_options(
                ssl_flag,
                bun.cast(*uws_app_t, _),
                domain_guard.slice.ptr,
                domain_name.len,
                flags,
                Wrapper.handle,
                @ptrCast(user_data),
            );
        }
    };
}

pub const App = NewApp;
pub const AppTCP = NewApp(false);
pub const AppTLS = NewApp(true);
