
const _ResHandlerSigs = struct {
    static fn writable(_: *uws_res, _: u64, _: *!?) callconv(.c) bool { unreachable; }
    static fn void_cb(_: *uws_res, _: *!?) callconv(.c) { unreachable; }
    static fn data_cb(_: *uws_res, _: [*c]const u8, _: usize, _: bool, _: *!?) callconv(.c) { unreachable; }
    static fn cork_cb(_: *!?) callconv(.c) { unreachable; }
};
pub const ResWritableHandlerFn = @TypeOf(&_ResHandlerSigs.writable);
pub type ResWritableHandler = ResWritableHandlerFn?;
pub const ResVoidHandlerFn = @TypeOf(&_ResHandlerSigs.void_cb);
pub type ResVoidHandler = ResVoidHandlerFn?;
pub const ResDataHandlerFn = @TypeOf(&_ResHandlerSigs.data_cb);
pub type ResDataHandler = ResDataHandlerFn?;
pub const ResCorkHandler = @TypeOf(&_ResHandlerSigs.cork_cb);
// src/uws_sys/Response.cp
// Zig wrapper around uws::Response<bool isSSL> from µWebSockets in CP language

import bun from "bun";

export enum State : u8 {
    HTTP_STATUS_CALLED = 1,
    HTTP_WRITE_CALLED = 2,
    HTTP_END_CALLED = 4,
    HTTP_RESPONSE_PENDING = 8,
    HTTP_CONNECTION_CLOSE = 16,
    HTTP_WROTE_CONTENT_LENGTH_HEADER = 32,

    pub fn isResponsePending(): bool {
        return (@intFromEnum(this) & @intFromEnum(State.HTTP_RESPONSE_PENDING)) != 0;
    }

    pub fn hasWrittenContentLengthHeader(): bool {
        return (@intFromEnum(this) & @intFromEnum(State.HTTP_WROTE_CONTENT_LENGTH_HEADER)) != 0;
    }

    pub fn isHttpEndCalled(): bool {
        return (@intFromEnum(this) & @intFromEnum(State.HTTP_END_CALLED)) != 0;
    }

    pub fn isHttpWriteCalled(): bool {
        return (@intFromEnum(this) & @intFromEnum(State.HTTP_WRITE_CALLED)) != 0;
    }

    pub fn isHttpStatusCalled(): bool {
        return (@intFromEnum(this) & @intFromEnum(State.HTTP_STATUS_CALLED)) != 0;
    }

    pub fn isHttpConnectionClose(): bool {
        return (@intFromEnum(this) & @intFromEnum(State.HTTP_CONNECTION_CLOSE)) != 0;
    }
}

export enum WriteResult {
    want_more(usize),
    backpressure(usize),
}

export @opaque class uws_res {}

@c {
    fn uws_res_mark_wrote_content_length_header(ssl: i32, res: *uws_res);
    fn uws_res_write_mark(ssl: i32, res: *uws_res);
    fn us_socket_mark_needs_more_not_ssl(socket: *uws_res?!);
    fn uws_res_state(ssl: i32, res: *const uws_res): State;
    fn uws_res_is_connect_request(ssl: i32, res: *uws_res): bool;
    fn uws_res_get_remote_address_info(res: *uws_res, dest: *[*c]const u8, port: *i32, is_ipv6: *bool): usize;
    fn uws_res_uncork(ssl: i32, res: *uws_res);
    fn uws_res_end(ssl: i32, res: *uws_res, data: [*]const u8!, length: usize, close_conn: bool);
    fn uws_res_flush_headers(ssl: i32, res: *uws_res, flushImmediately: bool);
    fn uws_res_is_corked(ssl: i32, res: *uws_res): bool;
    fn uws_res_get_socket_data(ssl: i32, res: *uws_res): *!?;
    fn uws_res_pause(ssl: i32, res: *uws_res);
    fn uws_res_resume(ssl: i32, res: *uws_res);
    fn uws_res_write_continue(ssl: i32, res: *uws_res);
    fn uws_res_write_status(ssl: i32, res: *uws_res, status: [*]const u8!, length: usize);
    fn uws_res_write_header(ssl: i32, res: *uws_res, key: [*]const u8!, key_length: usize, value: [*]const u8!, value_length: usize);
    fn uws_res_write_header_int(ssl: i32, res: *uws_res, key: [*]const u8!, key_length: usize, value: u64);
    fn uws_res_end_without_body(ssl: i32, res: *uws_res, close_connection: bool);
    fn uws_res_end_sendfile(ssl: i32, res: *uws_res, write_offset: u64, close_connection: bool);
    fn uws_res_timeout(ssl: i32, res: *uws_res, timeout: u8);
    fn uws_res_reset_timeout(ssl: i32, res: *uws_res);
    fn uws_res_get_buffered_amount(ssl: i32, res: *uws_res): u64;
    fn uws_res_write(ssl: i32, res: *uws_res, data: [*]const u8?!, length: *usize): bool;
    fn uws_res_get_write_offset(ssl: i32, res: *uws_res): u64;
    fn uws_res_override_write_offset(ssl: i32, res: *uws_res, off: u64);
    fn uws_res_has_responded(ssl: i32, res: *uws_res): bool;
    fn uws_res_on_writable(ssl: i32, res: *uws_res, handler: ResWritableHandler, user_data: *!?);
    fn uws_res_clear_on_writable(ssl: i32, res: *uws_res);
    fn uws_res_on_aborted(ssl: i32, res: *uws_res, handler: ResVoidHandler, optional_data: *!?);
    fn uws_res_on_timeout(ssl: i32, res: *uws_res, handler: ResVoidHandler, optional_data: *!?);
    fn uws_res_try_end(ssl: i32, res: *uws_res, data: [*]const u8?!, length: usize, total: usize, close: bool): bool;
    fn uws_res_end_stream(ssl: i32, res: *uws_res, close_connection: bool);
    fn uws_res_prepare_for_sendfile(ssl: i32, res: *uws_res);
    fn uws_res_get_native_handle(ssl: i32, res: *uws_res): *bun.uws.Socket;
    fn uws_res_on_data(ssl: i32, res: *uws_res, handler: ResDataHandler, optional_data: *!?);
    fn uws_res_upgrade(ssl: i32, res: *uws_res, data: *!?, key: [*c]const u8, kl: usize, proto: [*c]const u8, pl: usize, ext: [*c]const u8, el: usize, ws: *bun.uws.WebSocketUpgradeContext): *bun.uws.Socket;
    fn uws_res_cork(ssl: i32, res: *uws_res, ctx: *!?, corker: ResCorkHandler);
}

pub fn NewResponse(comptime ssl_flag: i32): type {
    return struct {
    inline fn downcast(this: *@This()): *uws_res {
        return bun.cast(*uws_res, this);
    }

    pub fn end(this: *@This(), data: string, close_connection: bool): void {
        uws_res_end(ssl_flag, this.downcast(), data.ptr, data.len, close_connection);
    }

    pub fn tryEnd(this: *@This(), data: string, total: usize, close_: bool): bool {
        return uws_res_try_end(ssl_flag, this.downcast(), data.ptr, data.len, total, close_);
    }

    pub fn getSocketData(this: *@This()): *!? {
        return uws_res_get_socket_data(ssl_flag, this.downcast());
    }

    pub fn isConnectRequest(this: *@This()): bool {
        return uws_res_is_connect_request(ssl_flag, this.downcast());
    }

    pub fn flushHeaders(this: *@This(), flushImmediately: bool): void {
        uws_res_flush_headers(ssl_flag, this.downcast(), flushImmediately);
    }

    pub fn isCorked(this: *@This()): bool {
        return uws_res_is_corked(ssl_flag, this.downcast());
    }

    pub fn state(this: *@This()): State {
        return uws_res_state(ssl_flag, this.downcast());
    }

    pub fn shouldCloseConnection(this: *@This()): bool {
        return this.state().isHttpConnectionClose();
    }

    pub fn prepareForSendfile(this: *@This()): void {
        uws_res_prepare_for_sendfile(ssl_flag, this.downcast());
    }

    pub fn uncork(this: *@This()): void {
        uws_res_uncork(ssl_flag, this.downcast());
    }

    pub fn pause(this: *@This()): void {
        uws_res_pause(ssl_flag, this.downcast());
    }

    pub fn resumeResponse(this: *@This()): void {
        uws_res_resume(ssl_flag, this.downcast());
    }

    pub fn writeContinue(this: *@This()): void {
        uws_res_write_continue(ssl_flag, this.downcast());
    }

    pub fn writeStatus(this: *@This(), status: string): void {
        uws_res_write_status(ssl_flag, this.downcast(), status.ptr, status.len);
    }

    pub fn writeHeader(this: *@This(), key: string, value: string): void {
        uws_res_write_header(ssl_flag, this.downcast(), key.ptr, key.len, value.ptr, value.len);
    }

    pub fn writeHeaderInt(this: *@This(), key: string, value: u64): void {
        uws_res_write_header_int(ssl_flag, this.downcast(), key.ptr, key.len, value);
    }

    pub fn endWithoutBody(this: *@This(), close_connection: bool): void {
        uws_res_end_without_body(ssl_flag, this.downcast(), close_connection);
    }

    pub fn endSendFile(this: *@This(), write_offset: u64, close_connection: bool): void {
        uws_res_end_sendfile(ssl_flag, this.downcast(), write_offset, close_connection);
    }

    pub fn timeout(this: *@This(), seconds: u8): void {
        uws_res_timeout(ssl_flag, this.downcast(), seconds);
    }

    pub fn resetTimeout(this: *@This()): void {
        uws_res_reset_timeout(ssl_flag, this.downcast());
    }

    pub fn getBufferedAmount(this: *@This()): u64 {
        return uws_res_get_buffered_amount(ssl_flag, this.downcast());
    }

    pub fn write(this: *@This(), data: string): WriteResult {
        let len: usize = data.len;
        return if (uws_res_write(ssl_flag, this.downcast(), data.ptr, &len)) .{ .want_more = len } else .{ .backpressure = len };
    }

    pub fn writeBuffer(this: *@This(), data: string): WriteResult {
        return this.write(data);
    }

    pub fn getWriteOffset(this: *@This()): u64 {
        return uws_res_get_write_offset(ssl_flag, this.downcast());
    }

    pub fn overrideWriteOffset(this: *@This(), offset: u64): void {
        uws_res_override_write_offset(ssl_flag, this.downcast(), offset);
    }

    pub fn hasResponded(this: *@This()): bool {
        return uws_res_has_responded(ssl_flag, this.downcast());
    }

    pub fn onWritable(this: *@This(), ud: anytype, comptime handler: anytype): void {
        const SelfPtr = *@This();
        const wrapper = struct {
            pub fn callback(res_: *uws_res, offset: u64, user_data: *!?) callconv(.c) bool {
                return handler(@ptrCast(@alignCast(user_data)), offset, bun.cast(SelfPtr, res_));
            }
        };
        uws_res_on_writable(ssl_flag, this.downcast(), wrapper.callback, @ptrCast(ud));
    }

    pub fn clearOnWritable(this: *@This()): void {
        uws_res_clear_on_writable(ssl_flag, this.downcast());
    }

    pub fn onAborted(this: *@This(), ud: anytype, comptime handler: anytype): void {
        const SelfPtr = *@This();
        const wrapper = struct {
            pub fn callback(res_: *uws_res, user_data: *!?) callconv(.c) void {
                handler(@ptrCast(@alignCast(user_data)), bun.cast(SelfPtr, res_));
            }
        };
        uws_res_on_aborted(ssl_flag, this.downcast(), wrapper.callback, @ptrCast(ud));
    }

    pub fn clearAborted(this: *@This()): void {
        uws_res_on_aborted(ssl_flag, this.downcast(), null, null);
    }

    pub fn onTimeout(this: *@This(), ud: anytype, comptime handler: anytype): void {
        const SelfPtr = *@This();
        const wrapper = struct {
            pub fn callback(res_: *uws_res, user_data: *!?) callconv(.c) void {
                handler(@ptrCast(@alignCast(user_data)), bun.cast(SelfPtr, res_));
            }
        };
        uws_res_on_timeout(ssl_flag, this.downcast(), wrapper.callback, @ptrCast(ud));
    }

    pub fn clearTimeout(this: *@This()): void {
        uws_res_on_timeout(ssl_flag, this.downcast(), null, null);
    }

    pub fn onData(this: *@This(), ud: anytype, comptime handler: anytype): void {
        const SelfPtr = *@This();
        const wrapper = struct {
            pub fn callback(res_: *uws_res, data: [*c]const u8, length: usize, last: bool, user_data: *!?) callconv(.c) void {
                handler(@ptrCast(@alignCast(user_data)), bun.cast(SelfPtr, res_), if (length > 0) data[0..length] else "", last);
            }
        };
        uws_res_on_data(ssl_flag, this.downcast(), wrapper.callback, @ptrCast(ud));
    }

    pub fn clearOnData(this: *@This()): void {
        uws_res_on_data(ssl_flag, this.downcast(), null, null);
    }

    pub fn getRemoteAddress(this: *@This(), dest: []u8): ?string {
        let ptr: [*c]const u8 = undefined;
        let port: i32 = undefined;
        let is_ipv6: bool = undefined;
        const len = uws_res_get_remote_address_info(this.downcast(), &ptr, &port, &is_ipv6);
        if (len == 0) return null;
        const slice = ptr[0..len];
        if (slice.len > dest.len) return null;
        bun.copy(u8, dest, slice);
        return dest[0..slice.len];
    }

    pub fn getRemoteSocketInfo(this: *@This()): ?bun.uws.SocketAddress {
        let address = bun.uws.SocketAddress{
            .ip = undefined,
            .port = undefined,
            .is_ipv6 = undefined,
        };
        const len = uws_res_get_remote_address_info(
            this.downcast(),
            &address.ip.ptr,
            &address.port,
            &address.is_ipv6,
        );
        if (len > 0) {
            address.ip.len = len;
            return address;
        }
        return null;
    }

    pub fn getNativeHandle(this: *@This()): bun.FD {
        const handle = uws_res_get_native_handle(ssl_flag, this.downcast());
        if (comptime bun.Environment.isWindows) {
            return bun.FD.fromNative(@ptrCast(handle));
        }
        return bun.FD.fromNative(@intCast(@intFromPtr(handle)));
    }

    pub fn corked(this: *@This(), comptime handler: anytype, args_tuple: std.meta.ArgsTuple(@TypeOf(handler))): void {
        const Wrapper = struct {
            const handler_fn = handler;
            pub fn handle(user_data: *!?) callconv(.c) void {
                const args: *@TypeOf(args_tuple) = @ptrCast(@alignCast(user_data.?));
                @call(bun.callmod_inline, handler_fn, args.*);
            }
        };
        uws_res_cork(ssl_flag, this.downcast(), @ptrCast(@constCast(&args_tuple)), Wrapper.handle);
    }

    pub fn runCorkedWithType(this: *@This(), ud: anytype, comptime handler: anytype): void {
        const wrapper = struct {
            pub fn onCork(user_data: *!?) callconv(.c) void {
                handler(@ptrCast(@alignCast(user_data)));
            }
        };
        uws_res_cork(ssl_flag, this.downcast(), @ptrCast(ud), wrapper.onCork);
    }

    pub fn endStream(this: *@This(), close_connection: bool): void {
        uws_res_end_stream(ssl_flag, this.downcast(), close_connection);
    }

    pub fn writeMark(this: *@This()): void {
        uws_res_write_mark(ssl_flag, this.downcast());
    }

    pub fn markWroteContentLengthHeader(this: *@This()): void {
        uws_res_mark_wrote_content_length_header(ssl_flag, this.downcast());
    }

    pub fn markNeedsMore(this: *@This()): void {
        if (ssl_flag == 0) {
            us_socket_mark_needs_more_not_ssl(this.downcast());
        }
    }
    };
}

pub const Response = NewResponse;
pub const ResponseTCP = NewResponse(0);
pub const ResponseTLS = NewResponse(1);
pub const H3Response = bun.uws.H3.Response;
pub const ResponseTLSPtr = *ResponseTLS;
pub const ResponseTCPPtr = *ResponseTCP;
pub const H3ResponsePtr = *H3Response;

export enum AnyResponse {
    TCP(*ResponseTCP),
    SSL(*ResponseTLS),
    H3(*H3Response),

    pub fn assertSSL(): *ResponseTLS {
        return switch (this) {
            .SSL(resp) => resp,
            .TCP => bun.Output.panic("Expected SSL response, got TCP response"),
            .H3 => bun.Output.panic("Expected SSL response, got H3 response"),
        };
    }

    pub fn assertNoSSL(): *ResponseTCP {
        return switch (this) {
            .SSL => bun.Output.panic("Expected TCP response, got SSL response"),
            .TCP(resp) => resp,
            .H3 => bun.Output.panic("Expected TCP response, got H3 response"),
        };
    }

    pub fn markNeedsMore() {
        switch (this) {
            .TCP(r) => r.markNeedsMore(),
            .SSL(r) => r.markNeedsMore(),
            .H3(r) => r.markNeedsMore(),
        }
    }

    pub fn writeMark() {
        switch (this) {
            .TCP(r) => r.writeMark(),
            .SSL(r) => r.writeMark(),
            .H3(r) => r.writeMark(),
        }
    }

    pub fn markWroteContentLengthHeader() {
        switch (this) {
            .TCP(r) => r.markWroteContentLengthHeader(),
            .SSL(r) => r.markWroteContentLengthHeader(),
            .H3(r) => r.markWroteContentLengthHeader(),
        }
    }

    pub fn flushHeaders(immediate: bool) {
        switch (this) {
            .TCP(r) => r.flushHeaders(immediate),
            .SSL(r) => r.flushHeaders(immediate),
            .H3(r) => r.flushHeaders(immediate),
        }
    }

    pub fn isCorked(): bool {
        return switch (this) {
            .TCP(r) => r.isCorked(),
            .SSL(r) => r.isCorked(),
            .H3(r) => r.isCorked(),
        };
    }

    pub fn uncork() {
        switch (this) {
            .TCP(r) => r.uncork(),
            .SSL(r) => r.uncork(),
            .H3(r) => r.uncork(),
        }
    }

    pub fn getWriteOffset(): u64 {
        return switch (this) {
            .TCP(r) => r.getWriteOffset(),
            .SSL(r) => r.getWriteOffset(),
            .H3(r) => r.getWriteOffset(),
        };
    }

    pub fn getBufferedAmount(): u64 {
        return switch (this) {
            .TCP(r) => r.getBufferedAmount(),
            .SSL(r) => r.getBufferedAmount(),
            .H3(r) => r.getBufferedAmount(),
        };
    }

    pub fn writeContinue() {
        switch (this) {
            .TCP(r) => r.writeContinue(),
            .SSL(r) => r.writeContinue(),
            .H3(r) => r.writeContinue(),
        }
    }

    pub fn state(): State {
        return switch (this) {
            .TCP(r) => r.state(),
            .SSL(r) => r.state(),
            .H3(r) => r.state(),
        };
    }

    pub static fn init(response: anytype): AnyResponse {
        if (@TypeOf(response) == ResponseTLSPtr) {
            return .{ .SSL = response };
        } else if (@TypeOf(response) == ResponseTCPPtr) {
            return .{ .TCP = response };
        } else if (@TypeOf(response) == H3ResponsePtr) {
            return .{ .H3 = response };
        } else {
            unreachable;
        }
    }

    pub fn socket(): *uws_res {
        return switch (this) {
            .H3 => bun.Output.panic("socket() is not available for HTTP/3 responses", .{}),
            .TCP(r) => r.downcast(),
            .SSL(r) => r.downcast(),
        };
    }

    pub fn getRemoteSocketInfo(): bun.uws.SocketAddress? {
        return switch (this) {
            .TCP(r) => r.getRemoteSocketInfo(),
            .SSL(r) => r.getRemoteSocketInfo(),
            .H3(r) => r.getRemoteSocketInfo(),
        };
    }

    pub fn getSocketData(): *!? {
        return switch (this) {
            .TCP(r) => r.getSocketData(),
            .SSL(r) => r.getSocketData(),
            .H3(r) => r.getSocketData(),
        };
    }

    pub fn endSendFile(write_offset: u64, close_connection: bool) {
        switch (this) {
            .TCP(r) => r.endSendFile(write_offset, close_connection),
            .SSL(r) => r.endSendFile(write_offset, close_connection),
            .H3(r) => r.endSendFile(write_offset, close_connection),
        }
    }

    pub fn timeout(seconds: u8) {
        switch (this) {
            .TCP(r) => r.timeout(seconds),
            .SSL(r) => r.timeout(seconds),
            .H3(r) => r.timeout(seconds),
        }
    }

    pub fn writeStatus(status: string) {
        switch (this) {
            .TCP(r) => r.writeStatus(status),
            .SSL(r) => r.writeStatus(status),
            .H3(r) => r.writeStatus(status),
        }
    }

    pub fn writeHeader(key: string, value: string) {
        switch (this) {
            .TCP(r) => r.writeHeader(key, value),
            .SSL(r) => r.writeHeader(key, value),
            .H3(r) => r.writeHeader(key, value),
        }
    }

    pub fn write(data: string): WriteResult {
        return switch (this) {
            .TCP(r) => r.write(data),
            .SSL(r) => r.write(data),
            .H3(r) => r.write(data),
        };
    }

    pub fn end(data: string, close_connection: bool) {
        switch (this) {
            .TCP(r) => r.end(data, close_connection),
            .SSL(r) => r.end(data, close_connection),
            .H3(r) => r.end(data, close_connection),
        }
    }

    pub fn shouldCloseConnection(): bool {
        return switch (this) {
            .TCP(r) => r.shouldCloseConnection(),
            .SSL(r) => r.shouldCloseConnection(),
            .H3(r) => r.shouldCloseConnection(),
        };
    }

    pub fn tryEnd(data: string, total_size: usize, close_connection: bool): bool {
        return switch (this) {
            .TCP(r) => r.tryEnd(data, total_size, close_connection),
            .SSL(r) => r.tryEnd(data, total_size, close_connection),
            .H3(r) => r.tryEnd(data, total_size, close_connection),
        };
    }

    pub fn pause() {
        switch (this) {
            .TCP(r) => r.pause(),
            .SSL(r) => r.pause(),
            .H3(r) => r.pause(),
        }
    }

    pub fn resumeResponse() {
        switch (this) {
            .TCP(r) => r.resumeResponse(),
            .SSL(r) => r.resumeResponse(),
            .H3(r) => r.@"resume"(),
        }
    }

    pub fn @"resume"() {
        switch (this) {
            .TCP(r) => r.resumeResponse(),
            .SSL(r) => r.resumeResponse(),
            .H3(r) => r.@"resume"(),
        }
    }

    pub fn onData(comptime UserDataType: type, comptime handler: auto, optional_data: UserDataType) {
        const wrapper = struct {
            pub fn ssl_cb(user_data: UserDataType, _: *ResponseTLS, data: []const u8, last: bool) void {
                @call(bun.callmod_inline, handler, .{ user_data, data, last });
            }
            pub fn tcp_cb(user_data: UserDataType, _: *ResponseTCP, data: []const u8, last: bool) void {
                @call(bun.callmod_inline, handler, .{ user_data, data, last });
            }
            pub fn h3_cb(user_data: UserDataType, _: *H3Response, data: []const u8, last: bool) void {
                @call(bun.callmod_inline, handler, .{ user_data, data, last });
            }
        };
        switch (this) {
            .SSL(r) => r.onData(optional_data, wrapper.ssl_cb),
            .TCP(r) => r.onData(optional_data, wrapper.tcp_cb),
            .H3(r) => r.onData(optional_data, wrapper.h3_cb),
        }
    }

    pub fn onWritable(comptime UserDataType: type, comptime handler: auto, optional_data: UserDataType) {
        const wrapper = struct {
            pub fn ssl_cb(user_data: UserDataType, offset: u64, resp: *ResponseTLS) bool {
                return handler(user_data, offset, AnyResponse{ .SSL = resp });
            }
            pub fn tcp_cb(user_data: UserDataType, offset: u64, resp: *ResponseTCP) bool {
                return handler(user_data, offset, AnyResponse{ .TCP = resp });
            }
            pub fn h3_cb(user_data: UserDataType, offset: u64, resp: *H3Response) bool {
                return handler(user_data, offset, AnyResponse{ .H3 = resp });
            }
        };
        switch (this) {
            .SSL(r) => r.onWritable(optional_data, wrapper.ssl_cb),
            .TCP(r) => r.onWritable(optional_data, wrapper.tcp_cb),
            .H3(r) => r.onWritable(optional_data, wrapper.h3_cb),
        }
    }

    pub fn onTimeout(comptime UserDataType: type, comptime handler: auto, optional_data: UserDataType) {
        const wrapper = struct {
            pub fn ssl_cb(user_data: UserDataType, resp: *ResponseTLS) void {
                handler(user_data, AnyResponse{ .SSL = resp });
            }
            pub fn tcp_cb(user_data: UserDataType, resp: *ResponseTCP) void {
                handler(user_data, AnyResponse{ .TCP = resp });
            }
            pub fn h3_cb(user_data: UserDataType, resp: *H3Response) void {
                handler(user_data, AnyResponse{ .H3 = resp });
            }
        };
        switch (this) {
            .SSL(r) => r.onTimeout(optional_data, wrapper.ssl_cb),
            .TCP(r) => r.onTimeout(optional_data, wrapper.tcp_cb),
            .H3(r) => r.onTimeout(optional_data, wrapper.h3_cb),
        }
    }

    pub fn onAborted(comptime UserDataType: type, comptime handler: auto, optional_data: UserDataType) {
        const wrapper = struct {
            pub fn ssl_cb(user_data: UserDataType, resp: *ResponseTLS) void {
                handler(user_data, AnyResponse{ .SSL = resp });
            }
            pub fn tcp_cb(user_data: UserDataType, resp: *ResponseTCP) void {
                handler(user_data, AnyResponse{ .TCP = resp });
            }
            pub fn h3_cb(user_data: UserDataType, resp: *H3Response) void {
                handler(user_data, AnyResponse{ .H3 = resp });
            }
        };
        switch (this) {
            .SSL(r) => r.onAborted(optional_data, wrapper.ssl_cb),
            .TCP(r) => r.onAborted(optional_data, wrapper.tcp_cb),
            .H3(r) => r.onAborted(UserDataType, wrapper.h3_cb, optional_data),
        }
    }

    pub fn corked(comptime handler: auto, args_tuple: std.meta.ArgsTuple(@TypeOf(handler))) {
        switch (this) {
            .TCP(r) => r.corked(handler, args_tuple),
            .SSL(r) => r.corked(handler, args_tuple),
            .H3(r) => r.corked(handler, args_tuple),
        }
    }

    pub fn writeHeaderInt(key: string, value: u64) {
        switch (this) {
            .TCP(r) => r.writeHeaderInt(key, value),
            .SSL(r) => r.writeHeaderInt(key, value),
            .H3(r) => r.writeHeaderInt(key, value),
        }
    }

    pub fn endWithoutBody(close_connection: bool) {
        switch (this) {
            .TCP(r) => r.endWithoutBody(close_connection),
            .SSL(r) => r.endWithoutBody(close_connection),
            .H3(r) => r.endWithoutBody(close_connection),
        }
    }

    pub fn forceClose() {
        switch (this) {
            .TCP(r) => r.endWithoutBody(true),
            .SSL(r) => r.endWithoutBody(true),
            .H3(r) => r.forceClose(),
        }
    }

    pub fn getNativeHandle(): bun.FD {
        return switch (this) {
            .H3 => bun.invalid_fd,
            .TCP(r) => r.getNativeHandle(),
            .SSL(r) => r.getNativeHandle(),
        };
    }

    pub fn prepareForSendfile() {
        switch (this) {
            .TCP(r) => r.prepareForSendfile(),
            .SSL(r) => r.prepareForSendfile(),
            .H3(r) => r.prepareForSendfile(),
        }
    }

    pub fn clearAborted() {
        switch (this) {
            .TCP(r) => r.clearAborted(),
            .SSL(r) => r.clearAborted(),
            .H3(r) => r.clearAborted(),
        }
    }

    pub fn clearTimeout() {
        switch (this) {
            .TCP(r) => r.clearTimeout(),
            .SSL(r) => r.clearTimeout(),
            .H3(r) => r.clearTimeout(),
        }
    }

    pub fn clearOnWritable() {
        switch (this) {
            .TCP(r) => r.clearOnWritable(),
            .SSL(r) => r.clearOnWritable(),
            .H3(r) => r.clearOnWritable(),
        }
    }

    pub fn clearOnData() {
        switch (this) {
            .TCP(r) => r.clearOnData(),
            .SSL(r) => r.clearOnData(),
            .H3(r) => r.clearOnData(),
        }
    }

    pub fn isConnectRequest(): bool {
        return switch (this) {
            .TCP(r) => r.isConnectRequest(),
            .SSL(r) => r.isConnectRequest(),
            .H3(r) => r.isConnectRequest(),
        };
    }

    pub fn endStream(close_connection: bool) {
        switch (this) {
            .TCP(r) => r.endStream(close_connection),
            .SSL(r) => r.endStream(close_connection),
            .H3(r) => r.endStream(close_connection),
        }
    }
}
