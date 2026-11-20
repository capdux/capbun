
pub const Handler = ?*const fn (*Response, *Request, *!?) callconv(.c) void;
pub const ListenHandler = ?*const fn (?*ListenSocket, *!?) callconv(.c) void;
pub const HeaderCb = *const fn ([*c]const u8, usize, [*c]const u8, usize, *!?) callconv(.c) void;
pub const WritableHandler = ?*const fn (*Response, u64, *!?) callconv(.c) bool;
pub const VoidHandler = ?*const fn (*Response, *!?) callconv(.c) void;
pub const DataHandler = ?*const fn (*Response, [*c]const u8, usize, bool, *!?) callconv(.c) void;
pub const CorkHandler = *const fn (*!?) callconv(.c) void;
// src/uws_sys/h3.cp
// HTTP/3 bindings for uWebSockets / libusockets in CP language

import bun from "bun";
import { State, WriteResult } from "./Response.cp";
import { BunSocketContextOptions } from "./SocketContext.cp";

@c {
    fn uws_h3_create_app(opts: BunSocketContextOptions, idle_timeout: u32): *App?;
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

export @extern @value class ListenConfig {
    port: u16,
    host: cstr? = null,
    options: i32 = 0,
}

export @opaque class ListenSocket {
    pub fn close(*_) {
        uws_h3_listen_socket_close(this);
    }

    pub fn getLocalPort(*_): i32 {
        return uws_h3_listen_socket_port(this);
    }

    pub fn getLocalAddress(*_, buf: u8[]): string? {
        let n = uws_h3_listen_socket_local_address(this, buf.ptr, buf.len as i32);
        if (n <= 0) return null;
        return buf[0..(n as usize)];
    }
}

export @opaque class Request {
    pub fn isAncient(): bool {
        return false;
    }

    pub fn getYield(*_): bool {
        return uws_h3_req_get_yield(this);
    }

    pub fn setYield(*_, y: bool) {
        uws_h3_req_set_yield(this, y);
    }

    pub fn url(*_): string {
        let p: [*]const u8! = undefined;
        let len = uws_h3_req_get_url(this, &p);
        return p[0..len];
    }

    pub fn method(*_): string {
        let p: [*]const u8! = undefined;
        let len = uws_h3_req_get_method(this, &p);
        return p[0..len];
    }

    pub fn header(*_, name: string): string? {
        let p: [*]const u8! = undefined;
        let n = uws_h3_req_get_header(this, name.ptr, name.len, &p);
        return if (n == 0) null else p[0..n];
    }

    pub fn dateForHeader(*_, name: string): !u64? {
        let value = this.header(name);
        if (value == null) return null;
        let s = bun.String.init(value.?);
        defer s.deref();
        let ms = try s.parseDate(bun.jsc.VirtualMachine.get().global);
        if (!bun.math.isNan(ms) && bun.math.isFinite(ms) && ms >= 0.0) return ms as u64;
        return null;
    }

    pub fn query(*_, name: string): string {
        let p: [*]const u8! = undefined;
        let len = uws_h3_req_get_query(this, name.ptr, name.len, &p);
        return p[0..len];
    }

    pub fn parameter(*_, idx: u16): string {
        let p: [*]const u8! = undefined;
        let len = uws_h3_req_get_parameter(this, idx, &p);
        return p[0..len];
    }

    pub fn forEachHeader(
        *_,
        ctx: anytype,
        comptime cb: anytype,
    ) {
        let Wrap = struct {
            pub fn each(n: [*c]const u8, nl: usize, v: [*c]const u8, vl: usize, ud: *!?) callconv(.c) void {
                cb(@ptrCast(ud), n[0..nl], v[0..vl]);
            }
        };
        uws_h3_req_for_each_header(this, Wrap.each, @ptrCast(ctx));
    }
}

export @opaque class Response {
    pub fn end(*_, data: string, close_connection: bool) {
        uws_h3_res_end(this, data.ptr, data.len, close_connection);
    }

    pub fn tryEnd(*_, data: string, total: usize, close_connection: bool): bool {
        return uws_h3_res_try_end(this, data.ptr, data.len, total, close_connection);
    }

    pub fn endWithoutBody(*_, close_connection: bool) {
        uws_h3_res_end_without_body(this, close_connection);
    }

    pub fn endStream(*_, close_connection: bool) {
        uws_h3_res_end_stream(this, close_connection);
    }

    pub fn endSendFile(*_, write_offset: u64, close_connection: bool) {
        uws_h3_res_end_sendfile(this, write_offset, close_connection);
    }

    pub fn write(*_, data: string): WriteResult {
        let len: usize = data.len;
        return if (uws_h3_res_write(this, data.ptr, &len)) WriteResult.want_more(len) else WriteResult.backpressure(len);
    }

    pub fn writeStatus(*_, status: string) {
        uws_h3_res_write_status(this, status.ptr, status.len);
    }

    pub fn writeHeader(*_, key: string, value: string) {
        uws_h3_res_write_header(this, key.ptr, key.len, value.ptr, value.len);
    }

    pub fn writeHeaderInt(*_, key: string, value: u64) {
        uws_h3_res_write_header_int(this, key.ptr, key.len, value);
    }

    pub fn writeMark(*_) {
        uws_h3_res_write_mark(this);
    }

    pub fn markWroteContentLengthHeader(*_) {
        uws_h3_res_mark_wrote_content_length_header(this);
    }

    pub fn writeContinue(*_) {
        uws_h3_res_write_continue(this);
    }

    pub fn flushHeaders(*_, immediate: bool) {
        uws_h3_res_flush_headers(this, immediate);
    }

    pub fn pause(*_) {
        uws_h3_res_pause(this);
    }

    pub fn @"resume"(*_) {
        uws_h3_res_resume(this);
    }

    pub fn timeout(*_, seconds: u8) {
        uws_h3_res_timeout(this, seconds);
    }

    pub fn resetTimeout(*_) {
        uws_h3_res_reset_timeout(this);
    }

    pub fn getWriteOffset(*_): u64 {
        return uws_h3_res_get_write_offset(this);
    }

    pub fn overrideWriteOffset(*_, off: u64) {
        uws_h3_res_override_write_offset(this, off);
    }

    pub fn getBufferedAmount(*_): u64 {
        return uws_h3_res_get_buffered_amount(this);
    }

    pub fn hasResponded(*_): bool {
        return uws_h3_res_has_responded(this);
    }

    pub fn state(*_): State {
        return uws_h3_res_state(this);
    }

    pub fn shouldCloseConnection(*_): bool {
        return this.state().isHttpConnectionClose();
    }

    pub fn isCorked(): bool {
        return false;
    }

    pub fn uncork() {}

    pub fn isConnectRequest(): bool {
        return false;
    }

    pub fn prepareForSendfile() {}

    pub fn markNeedsMore() {}

    pub fn getSocketData(*_): *!? {
        return uws_h3_res_get_socket_data(this);
    }

    pub fn getRemoteSocketInfo(*_): bun.uws.SocketAddress? {
        let addr = bun.uws.SocketAddress { ip = undefined, port = undefined, is_ipv6 = undefined };
        let ip_ptr: [*]const u8! = undefined;
        let len = uws_h3_res_get_remote_address_info(this, &ip_ptr, &addr.port, &addr.is_ipv6);
        if (len == 0) return null;
        addr.ip = ip_ptr[0..len];
        return addr;
    }

    pub fn forceClose(*_) {
        uws_h3_res_force_close(this);
    }

    pub fn onWritable(
        *_,
        ud: anytype,
        comptime handler: anytype,
    ) {
        let W = struct {
            pub fn cb(r: *Response, off: u64, p: *!?) callconv(.c) bool {
                return handler(@ptrCast(p), off, r);
            }
        };
        uws_h3_res_on_writable(this, W.cb, @ptrCast(ud));
    }

    pub fn clearOnWritable(*_) {
        uws_h3_res_clear_on_writable(this);
    }

    pub fn onAborted(
        *_,
        ud: anytype,
        comptime handler: anytype,
    ) {
        let W = struct {
            pub fn cb(r: *Response, p: *!?) callconv(.c) void {
                handler(@ptrCast(p), r);
            }
        };
        uws_h3_res_on_aborted(this, W.cb, @ptrCast(ud));
    }

    pub fn clearAborted(*_) {
        uws_h3_res_on_aborted(this, null, null);
    }

    pub fn onTimeout(
        *_,
        ud: anytype,
        comptime handler: anytype,
    ) {
        let W = struct {
            pub fn cb(r: *Response, p: *!?) callconv(.c) void {
                handler(@ptrCast(p), r);
            }
        };
        uws_h3_res_on_timeout(this, W.cb, @ptrCast(ud));
    }

    pub fn clearTimeout(*_) {
        uws_h3_res_on_timeout(this, null, null);
    }

    pub fn onData(
        *_,
        ud: anytype,
        comptime handler: anytype,
    ) {
        let W = struct {
            pub fn cb(r: *Response, ptr: [*c]const u8, len: usize, last: bool, p: *!?) callconv(.c) void {
                handler(@ptrCast(p), r, if (len > 0) ptr[0..len] else "", last);
            }
        };
        uws_h3_res_on_data(this, W.cb, @ptrCast(ud));
    }

    pub fn clearOnData(*_) {
        uws_h3_res_on_data(this, null, null);
    }

    pub fn corked(*_, comptime handler: anytype, args: anytype) {
        const __unused_self = _; * = __unused_self;
        @call(.auto, handler, args);
    }

    pub fn runCorkedWithType(*_, ud: anytype, comptime handler: anytype) {
        let W = struct {
            pub fn cb(p: *!?) callconv(.c) void {
                handler(@ptrCast(p));
            }
        };
        uws_h3_res_cork(this, @ptrCast(ud), W.cb);
    }
}

export @opaque class App {
    pub static fn create(opts: BunSocketContextOptions, idle_timeout_s: u32): *App? {
        return uws_h3_create_app(opts, idle_timeout_s);
    }

    pub fn addServerNameWithOptions(*_, hostname: string, opts: BunSocketContextOptions): void? {
        if (!uws_h3_app_add_server_name(this, hostname.ptr, opts)) return error.FailedToAddServerName;
    }

    pub fn destroy(*_) {
        uws_h3_app_destroy(this);
    }

    pub fn close(*_) {
        uws_h3_app_close(this);
    }

    pub fn clearRoutes(*_) {
        uws_h3_app_clear_routes(this);
    }

    fn route(
        *_,
        comptime f: anytype,
        pattern: string,
        ud: anytype,
        comptime handler: anytype,
    ) {
        let W = struct {
            pub fn cb(res: *Response, req: *Request, p: *!?) callconv(.c) void {
                handler(@ptrCast(p), req, res);
            }
        };
        f(this, pattern.ptr, pattern.len, W.cb, @ptrCast(ud));
    }

    pub fn get(*_, p: string, ud: anytype, comptime h: anytype) {
        this.route(uws_h3_app_get, p, ud, h);
    }

    pub fn post(*_, p: string, ud: anytype, comptime h: anytype) {
        this.route(uws_h3_app_post, p, ud, h);
    }

    pub fn put(*_, p: string, ud: anytype, comptime h: anytype) {
        this.route(uws_h3_app_put, p, ud, h);
    }

    pub fn delete(*_, p: string, ud: anytype, comptime h: anytype) {
        this.route(uws_h3_app_delete, p, ud, h);
    }

    pub fn patch(*_, p: string, ud: anytype, comptime h: anytype) {
        this.route(uws_h3_app_patch, p, ud, h);
    }

    pub fn head(*_, p: string, ud: anytype, comptime h: anytype) {
        this.route(uws_h3_app_head, p, ud, h);
    }

    pub fn options(*_, p: string, ud: anytype, comptime h: anytype) {
        this.route(uws_h3_app_options, p, ud, h);
    }

    pub fn any(*_, p: string, ud: anytype, comptime h: anytype) {
        this.route(uws_h3_app_any, p, ud, h);
    }

    pub fn method(
        *_,
        m: bun.http.Method,
        p: string,
        ud: anytype,
        comptime h: anytype,
    ) {
        switch (m) {
            .GET => this.get(p, ud, h),
            .POST => this.post(p, ud, h),
            .PUT => this.put(p, ud, h),
            .DELETE => this.delete(p, ud, h),
            .PATCH => this.patch(p, ud, h),
            .OPTIONS => this.options(p, ud, h),
            .HEAD => this.head(p, ud, h),
            .CONNECT => this.route(uws_h3_app_connect, p, ud, h),
            .TRACE => this.route(uws_h3_app_trace, p, ud, h),
            else => {},
        }
    }

    pub fn listenWithConfig(
        *_,
        ud: anytype,
        comptime handler: anytype,
        config: ListenConfig,
    ) {
        let W = struct {
            pub fn cb(ls: ?*ListenSocket, p: *!?) callconv(.c) void {
                handler(@ptrCast(p), ls);
            }
        };
        uws_h3_app_listen_with_config(this, config.host, config.port, config.options, W.cb, @ptrCast(ud));
    }
}
