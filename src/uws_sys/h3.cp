// src/uws_sys/h3.cp
// HTTP/3 bindings for uWebSockets / libusockets in CP language

import bun from "bun";
import { State, WriteResult } from "./Response.cp";
import { BunSocketContextOptions } from "./SocketContext.cp";

type OpaquePtr = *!?;
struct DerefGuard { s: *bun.String!, ~init() { unsafe { _.s.deref(); } } }
struct _h3_cb_sigs {
    @callconv("c") static fn handler(_: *Response, _: *Request, _: OpaquePtr) {}
    @callconv("c") static fn listen(_: (*ListenSocket)?, _: OpaquePtr) {}
    @callconv("c") static fn header(_: [*c]const u8, _: usize, _: [*c]const u8, _: usize, _: OpaquePtr) {}
    @callconv("c") static fn writable(_: *Response, _: u64, _: OpaquePtr): bool { return false; }
    @callconv("c") static fn void_h(_: *Response, _: OpaquePtr) {}
    @callconv("c") static fn data(_: *Response, _: [*c]const u8, _: usize, _: bool, _: OpaquePtr) {}
    @callconv("c") static fn cork(_: OpaquePtr) {}
}
pub const HandlerFn = *const @TypeOf(_h3_cb_sigs.handler);
pub type Handler = HandlerFn?;
pub const ListenHandlerFn = *const @TypeOf(_h3_cb_sigs.listen);
pub type ListenHandler = ListenHandlerFn?;
pub const HeaderCb = *const @TypeOf(_h3_cb_sigs.header);
pub const WritableHandlerFn = *const @TypeOf(_h3_cb_sigs.writable);
pub type WritableHandler = WritableHandlerFn?;
pub const VoidHandlerFn = *const @TypeOf(_h3_cb_sigs.void_h);
pub type VoidHandler = VoidHandlerFn?;
pub const DataHandlerFn = *const @TypeOf(_h3_cb_sigs.data);
pub type DataHandler = DataHandlerFn?;
pub const CorkHandler = *const @TypeOf(_h3_cb_sigs.cork);

@c {
    fn uws_h3_create_app(opts: BunSocketContextOptions, idle_timeout: u32): (*App)?;
    fn uws_h3_app_destroy(app: *App);
    fn uws_h3_app_close(app: *App);
    fn uws_h3_app_clear_routes(app: *App);
    fn uws_h3_app_add_server_name(app: *App, name: cstr, opts: BunSocketContextOptions): bool;
    fn uws_h3_res_write_continue(res: *Response);
    fn uws_h3_app_get(app: *App, p: [*]const u8!, len: usize, h: Handler, ud: *!?);
    fn uws_h3_app_post(app: *App, p: [*]const u8!, len: usize, h: Handler, ud: *!?);
    fn uws_h3_app_put(app: *App, p: [*]const u8!, len: usize, h: Handler, ud: *!?);
    fn uws_h3_app_delete(app: *App, p: [*]const u8!, len: usize, h: Handler, ud: *!?);
    fn uws_h3_app_patch(app: *App, p: [*]const u8!, len: usize, h: Handler, ud: *!?);
    fn uws_h3_app_head(app: *App, p: [*]const u8!, len: usize, h: Handler, ud: *!?);
    fn uws_h3_app_options(app: *App, p: [*]const u8!, len: usize, h: Handler, ud: *!?);
    fn uws_h3_app_connect(app: *App, p: [*]const u8!, len: usize, h: Handler, ud: *!?);
    fn uws_h3_app_trace(app: *App, p: [*]const u8!, len: usize, h: Handler, ud: *!?);
    fn uws_h3_app_any(app: *App, p: [*]const u8!, len: usize, h: Handler, ud: *!?);
    fn uws_h3_app_listen_with_config(app: *App, host: cstr?, port: u16, options: i32, h: ListenHandler, ud: *!?);
    fn uws_h3_listen_socket_port(ls: *ListenSocket): i32;
    fn uws_h3_listen_socket_local_address(ls: *ListenSocket, buf: [*]u8!, len: i32): i32;
    fn uws_h3_listen_socket_close(ls: *ListenSocket);
    fn uws_h3_res_state(res: *Response): State;
    fn uws_h3_res_end(res: *Response, data: [*]const u8!, len: usize, close_conn: bool);
    fn uws_h3_res_end_stream(res: *Response, close_conn: bool);
    fn uws_h3_res_force_close(res: *Response);
    fn uws_h3_res_try_end(res: *Response, data: [*]const u8!, len: usize, total: usize, close_conn: bool): bool;
    fn uws_h3_res_end_without_body(res: *Response, close_conn: bool);
    fn uws_h3_res_pause(res: *Response);
    fn uws_h3_res_resume(res: *Response);
    fn uws_h3_res_write_status(res: *Response, status: [*]const u8!, len: usize);
    fn uws_h3_res_write_header(res: *Response, k: [*]const u8!, kl: usize, v: [*]const u8!, vl: usize);
    fn uws_h3_res_write_header_int(res: *Response, k: [*]const u8!, kl: usize, v: u64);
    fn uws_h3_res_mark_wrote_content_length_header(res: *Response);
    fn uws_h3_res_write_mark(res: *Response);
    fn uws_h3_res_flush_headers(res: *Response, immediate: bool);
    fn uws_h3_res_write(res: *Response, data: [*]const u8?!, len: *usize): bool;
    fn uws_h3_res_get_write_offset(res: *Response): u64;
    fn uws_h3_res_override_write_offset(res: *Response, off: u64);
    fn uws_h3_res_has_responded(res: *Response): bool;
    fn uws_h3_res_get_buffered_amount(res: *Response): u64;
    fn uws_h3_res_reset_timeout(res: *Response);
    fn uws_h3_res_timeout(res: *Response, timeout: u8);
    fn uws_h3_res_end_sendfile(res: *Response, write_offset: u64, close_conn: bool);
    fn uws_h3_res_get_socket_data(res: *Response): *!?;
    fn uws_h3_res_on_writable(res: *Response, h: WritableHandler, ud: *!?);
    fn uws_h3_res_clear_on_writable(res: *Response);
    fn uws_h3_res_on_aborted(res: *Response, h: VoidHandler, ud: *!?);
    fn uws_h3_res_on_timeout(res: *Response, h: VoidHandler, ud: *!?);
    fn uws_h3_res_on_data(res: *Response, h: DataHandler, ud: *!?);
    fn uws_h3_res_cork(res: *Response, ud: *!?, h: CorkHandler);
    fn uws_h3_res_get_remote_address_info(res: *Response, ip: *[*]const u8!, port: *i32, is_ipv6: *bool): usize;
    fn uws_h3_req_get_yield(req: *Request): bool;
    fn uws_h3_req_set_yield(req: *Request, yield: bool);
    fn uws_h3_req_get_url(req: *Request, dest: *[*]const u8!): usize;
    fn uws_h3_req_get_method(req: *Request, dest: *[*]const u8!): usize;
    fn uws_h3_req_get_header(req: *Request, name: [*]const u8!, len: usize, dest: *[*]const u8!): usize;
    fn uws_h3_req_get_query(req: *Request, name: [*]const u8!, len: usize, dest: *[*]const u8!): usize;
    fn uws_h3_req_get_parameter(req: *Request, idx: u16, dest: *[*]const u8!): usize;
    fn uws_h3_req_for_each_header(req: *Request, cb: HeaderCb, ud: *!?);
}

export @c struct ListenConfig {
    port: u16,
    host: cstr? = null,
    options: i32 = 0,
}

export @opaque struct ListenSocket {
    fn close(*_) { uws_h3_listen_socket_close(_); }
    fn getLocalPort(*_): i32 { return uws_h3_listen_socket_port(_); }
    fn getLocalAddress(*_, buf: []u8): string? {
        const n = uws_h3_listen_socket_local_address(_, buf.ptr, buf.len as i32);
        if (n <= 0) return null;
        return buf[0..(n as usize)];
    }
}

export @opaque struct Request {
    fn isAncient(): bool { return false; }
    fn getYield(*_): bool { return uws_h3_req_get_yield(_); }
    fn setYield(*_, y: bool) { uws_h3_req_set_yield(_, y); }

    fn url(*_): string {
        let p: [*]const u8! = undefined;
        const len = uws_h3_req_get_url(_, &p);
        unsafe { return p[0..len]; }
    }

    fn method(*_): string {
        let p: [*]const u8! = undefined;
        const len = uws_h3_req_get_method(_, &p);
        unsafe { return p[0..len]; }
    }

    fn header(*_, name: string): string? {
        let p: [*]const u8! = undefined;
        const n = uws_h3_req_get_header(_, name.ptr, name.len, &p);
        unsafe { return if (n == 0) null else p[0..n]; }
    }

    fn dateForHeader(*_, name: string): (u64?)! {
        if (val := _.header(name)) {
            let s = bun.String.init(val);
            const _s_guard = DerefGuard{ s: unsafe { &s } };
            const ms = s.parseDate(bun.jsc.VirtualMachine.get().global)?;
            if (!std.math.isNan(ms) && std.math.isFinite(ms) && ms >= 0.0) return @intFromFloat(ms);
        }
        return null;
    }

    fn query(*_, name: string): string {
        let p: [*]const u8! = undefined;
        const len = uws_h3_req_get_query(_, name.ptr, name.len, &p);
        unsafe { return p[0..len]; }
    }

    fn parameter(*_, idx: u16): string {
        let p: [*]const u8! = undefined;
        const len = uws_h3_req_get_parameter(_, idx, &p);
        unsafe { return p[0..len]; }
    }

    fn forEachHeader(*_, ctx: auto, comptime cb: auto) {
        unsafe { uws_h3_req_for_each_header(_, H3ForEachHeaderWrap(cb).each, @ptrCast(ctx)); }
    }
}

fn H3ForEachHeaderWrap(comptime cb: auto): type {
    return struct {
        static fn each(n: [*c]const u8, nl: usize, v: [*c]const u8, vl: usize, ud: *!?) callconv(.c) {
            unsafe { cb(@ptrCast(@alignCast(ud)), n[0..nl], v[0..vl]); }
        }
    };
}

fn H3OnWritableWrap(comptime handler: auto): type {
    return struct {
        static fn cb(r: *Response, off: u64, p: *!?) callconv(.c): bool {
            unsafe { return handler(@ptrCast(@alignCast(p)), off, r); }
        }
    };
}

fn H3ResponseCbWrap(comptime handler: auto): type {
    return struct {
        static fn cb(r: *Response, p: *!?) callconv(.c) {
            unsafe { handler(@ptrCast(@alignCast(p)), r); }
        }
    };
}

fn H3OnDataWrap(comptime handler: auto): type {
    return struct {
        static fn cb(r: *Response, ptr: [*c]const u8, len: usize, last: bool, p: *!?) callconv(.c) {
            unsafe {
                const slice: string = if (len > 0) ptr[0..len] else "";
                handler(@ptrCast(@alignCast(p)), r, slice, last);
            }
        }
    };
}

fn H3CorkWrap(comptime handler: auto): type {
    return struct {
        static fn cb(p: *!?) callconv(.c) {
            unsafe { handler(@ptrCast(@alignCast(p))); }
        }
    };
}

fn H3RouteWrap(comptime handler: auto): type {
    return struct {
        static fn cb(res: *Response, req: *Request, p: *!?) callconv(.c) {
            unsafe { handler(@ptrCast(@alignCast(p)), req, res); }
        }
    };
}

fn H3ListenWrap(comptime handler: auto): type {
    return struct {
        static fn cb(ls: (*ListenSocket)?, p: *!?) callconv(.c) {
            unsafe { handler(@ptrCast(@alignCast(p)), ls); }
        }
    };
}

export @opaque struct Response {
    fn end(*_, data: string, close_connection: bool) { uws_h3_res_end(_, data.ptr, data.len, close_connection); }
    fn tryEnd(*_, data: string, total: usize, close_connection: bool): bool { return uws_h3_res_try_end(_, data.ptr, data.len, total, close_connection); }
    fn endWithoutBody(*_, close_connection: bool) { uws_h3_res_end_without_body(_, close_connection); }
    fn endStream(*_, close_connection: bool) { uws_h3_res_end_stream(_, close_connection); }
    fn endSendFile(*_, write_offset: u64, close_connection: bool) { uws_h3_res_end_sendfile(_, write_offset, close_connection); }
    fn write(*_, data: string): WriteResult {
        let len: usize = data.len;
        return if (uws_h3_res_write(_, data.ptr, &len)) WriteResult{ want_more: len } else WriteResult{ backpressure: len };
    }
    fn writeStatus(*_, status: string) { uws_h3_res_write_status(_, status.ptr, status.len); }
    fn writeHeader(*_, key: string, value: string) { uws_h3_res_write_header(_, key.ptr, key.len, value.ptr, value.len); }
    fn writeHeaderInt(*_, key: string, value: u64) { uws_h3_res_write_header_int(_, key.ptr, key.len, value); }
    fn writeMark(*_) { uws_h3_res_write_mark(_); }
    fn markWroteContentLengthHeader(*_) { uws_h3_res_mark_wrote_content_length_header(_); }
    fn writeContinue(*_) { uws_h3_res_write_continue(_); }
    fn flushHeaders(*_, immediate: bool) { uws_h3_res_flush_headers(_, immediate); }
    fn pause(*_) { uws_h3_res_pause(_); }
    fn @"resume"(*_) { uws_h3_res_resume(_); }
    fn timeout(*_, seconds: u8) { uws_h3_res_timeout(_, seconds); }
    fn resetTimeout(*_) { uws_h3_res_reset_timeout(_); }
    fn getWriteOffset(*_): u64 { return uws_h3_res_get_write_offset(_); }
    fn overrideWriteOffset(*_, off: u64) { uws_h3_res_override_write_offset(_, off); }
    fn getBufferedAmount(*_): u64 { return uws_h3_res_get_buffered_amount(_); }
    fn hasResponded(*_): bool { return uws_h3_res_has_responded(_); }
    fn state(*_): State { return uws_h3_res_state(_); }
    fn shouldCloseConnection(*_): bool { return _.state().isHttpConnectionClose(); }
    fn isClosed(*_): bool { return false; }
    fn isCorked(*_): bool { return false; }
    fn uncork(*_) {}
    fn isConnectRequest(*_): bool { return false; }
    fn prepareForSendfile(*_) {}
    fn markNeedsMore(*_) {}
    fn getSocketData(*_): *!? { return uws_h3_res_get_socket_data(_); }

    fn getRemoteSocketInfo(*_): bun.uws.SocketAddress? {
        let addr = bun.uws.SocketAddress{ ip: undefined, port: undefined, is_ipv6: undefined };
        let ip_ptr: [*]const u8! = undefined;
        const len = uws_h3_res_get_remote_address_info(_, &ip_ptr, &addr.port, &addr.is_ipv6);
        if (len == 0) return null;
        unsafe { addr.ip = ip_ptr[0..len]; }
        return addr;
    }

    fn forceClose(*_) { uws_h3_res_force_close(_); }

    fn onWritable(*_, ud: auto, comptime handler: auto) {
        unsafe { uws_h3_res_on_writable(_, H3OnWritableWrap(handler).cb, @ptrCast(ud)); }
    }

    fn clearOnWritable(*_) { uws_h3_res_clear_on_writable(_); }

    fn onAborted(*_, ud: auto, comptime handler: auto) {
        unsafe { uws_h3_res_on_aborted(_, H3ResponseCbWrap(handler).cb, @ptrCast(ud)); }
    }

    fn clearAborted(*_) { uws_h3_res_on_aborted(_, null, null); }

    fn onTimeout(*_, ud: auto, comptime handler: auto) {
        unsafe { uws_h3_res_on_timeout(_, H3ResponseCbWrap(handler).cb, @ptrCast(ud)); }
    }

    fn clearTimeout(*_) { uws_h3_res_on_timeout(_, null, null); }

    fn onData(*_, ud: auto, comptime handler: auto) {
        unsafe { uws_h3_res_on_data(_, H3OnDataWrap(handler).cb, @ptrCast(ud)); }
    }

    fn clearOnData(*_) { uws_h3_res_on_data(_, null, null); }

    fn corked(*_, comptime handler: auto, args: auto) {
        const __unused_self = _; * = __unused_self;
        @call(.auto, handler, args);
    }

    fn runCorkedWithType(*_, ud: auto, comptime handler: auto) {
        unsafe { uws_h3_res_cork(_, @ptrCast(ud), H3CorkWrap(handler).cb); }
    }
}

export @opaque struct App {
    static fn create(opts: BunSocketContextOptions, idle_timeout_s: u32): (*App)? {
        return uws_h3_create_app(opts, idle_timeout_s);
    }

    fn addServerNameWithOptions(*_, hostname: [:0]const u8, opts: BunSocketContextOptions): !void {
        if (!uws_h3_app_add_server_name(_, hostname.ptr, opts)) return error.FailedToAddServerName;
    }

    fn destroy(*_) { uws_h3_app_destroy(_); }
    fn close(*_) { uws_h3_app_close(_); }
    fn clearRoutes(*_) { uws_h3_app_clear_routes(_); }

    fn route(*_, comptime f: auto, pattern: string, ud: auto, comptime handler: auto) {
        unsafe { f(_, pattern.ptr, pattern.len, H3RouteWrap(handler).cb, @ptrCast(ud)); }
    }

    fn get(*_, p: string, ud: auto, comptime h: auto) { _.route(uws_h3_app_get, p, ud, h); }
    fn post(*_, p: string, ud: auto, comptime h: auto) { _.route(uws_h3_app_post, p, ud, h); }
    fn put(*_, p: string, ud: auto, comptime h: auto) { _.route(uws_h3_app_put, p, ud, h); }
    fn delete(*_, p: string, ud: auto, comptime h: auto) { _.route(uws_h3_app_delete, p, ud, h); }
    fn patch(*_, p: string, ud: auto, comptime h: auto) { _.route(uws_h3_app_patch, p, ud, h); }
    fn head(*_, p: string, ud: auto, comptime h: auto) { _.route(uws_h3_app_head, p, ud, h); }
    fn options(*_, p: string, ud: auto, comptime h: auto) { _.route(uws_h3_app_options, p, ud, h); }
    fn any(*_, p: string, ud: auto, comptime h: auto) { _.route(uws_h3_app_any, p, ud, h); }

    fn method(*_, m: bun.http.Method, p: string, ud: auto, comptime h: auto) {
        switch (m) {
            .GET => _.get(p, ud, h),
            .POST => _.post(p, ud, h),
            .PUT => _.put(p, ud, h),
            .DELETE => _.delete(p, ud, h),
            .PATCH => _.patch(p, ud, h),
            .OPTIONS => _.options(p, ud, h),
            .HEAD => _.head(p, ud, h),
            .CONNECT => _.route(uws_h3_app_connect, p, ud, h),
            .TRACE => _.route(uws_h3_app_trace, p, ud, h),
            else => {},
        }
    }

    fn listenWithConfig(*_, ud: auto, comptime handler: auto, config: ListenConfig) {
        unsafe { uws_h3_app_listen_with_config(_, config.host, config.port, config.options, H3ListenWrap(handler).cb, @ptrCast(ud)); }
    }
}
