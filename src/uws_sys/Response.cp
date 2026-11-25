// src/uws_sys/Response.cp
// Zig wrapper around uws::Response<bool isSSL> from µWebSockets in CP language

import bun from "bun";

struct _ResHandlerSigs {
    static fn writable(_: *uws_res, _: u64, _: *!?) callconv(.c): bool { unreachable; }
    static fn void_cb(_: *uws_res, _: *!?) callconv(.c) { unreachable; }
    static fn data_cb(_: *uws_res, _: [*c]const u8, _: usize, _: bool, _: *!?) callconv(.c) { unreachable; }
    static fn cork_cb(_: *!?) callconv(.c) { unreachable; }
}
pub const ResWritableHandlerFn = @TypeOf(&_ResHandlerSigs.writable);
pub type ResWritableHandler = ResWritableHandlerFn?;
pub const ResVoidHandlerFn = @TypeOf(&_ResHandlerSigs.void_cb);
pub type ResVoidHandler = ResVoidHandlerFn?;
pub const ResDataHandlerFn = @TypeOf(&_ResHandlerSigs.data_cb);
pub type ResDataHandler = ResDataHandlerFn?;
pub const ResCorkHandler = @TypeOf(&_ResHandlerSigs.cork_cb);

export enum State : u8 {
    HTTP_STATUS_CALLED = 1,
    HTTP_WRITE_CALLED = 2,
    HTTP_END_CALLED = 4,
    HTTP_RESPONSE_PENDING = 8,
    HTTP_CONNECTION_CLOSE = 16,
    HTTP_WROTE_CONTENT_LENGTH_HEADER = 32,

    fn isResponsePending(): bool {
        return (@intFromEnum(_) & @intFromEnum(State.HTTP_RESPONSE_PENDING)) != 0;
    }

    fn hasWrittenContentLengthHeader(): bool {
        return (@intFromEnum(_) & @intFromEnum(State.HTTP_WROTE_CONTENT_LENGTH_HEADER)) != 0;
    }

    fn isHttpEndCalled(): bool {
        return (@intFromEnum(_) & @intFromEnum(State.HTTP_END_CALLED)) != 0;
    }

    fn isHttpWriteCalled(): bool {
        return (@intFromEnum(_) & @intFromEnum(State.HTTP_WRITE_CALLED)) != 0;
    }

    fn isHttpStatusCalled(): bool {
        return (@intFromEnum(_) & @intFromEnum(State.HTTP_STATUS_CALLED)) != 0;
    }

    fn isHttpConnectionClose(): bool {
        return (@intFromEnum(_) & @intFromEnum(State.HTTP_CONNECTION_CLOSE)) != 0;
    }
}

export enum WriteResult {
    want_more(usize),
    backpressure(usize),
}

export @opaque struct uws_res {}

@c {
    fn uws_res_mark_wrote_content_length_header(ssl: i32, res: *uws_res);
    fn uws_res_write_mark(ssl: i32, res: *uws_res);
    fn us_socket_mark_needs_more_not_ssl(socket: *uws_res);
    fn uws_res_state(ssl: i32, res: *const uws_res): State;
    fn uws_res_is_connect_request(ssl: i32, res: *uws_res): bool;
    fn uws_res_get_remote_address_info(res: *uws_res, dest: *[*]const u8!, port: *i32, is_ipv6: *bool): usize;
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
    fn uws_res_get_remote_address_as_text(ssl: i32, res: *uws_res, dest: *[*]const u8!): usize;
    fn uws_res_on_data(ssl: i32, res: *uws_res, handler: ResDataHandler, optional_data: *!?);
    fn uws_res_upgrade(ssl: i32, res: *uws_res, data: *!?, key: [*c]const u8, kl: usize, proto: [*c]const u8, pl: usize, ext: [*c]const u8, el: usize, ws: (*bun.uws.WebSocketUpgradeContext)?): *bun.uws.Socket;
    fn uws_res_cork(ssl: i32, res: *uws_res, ctx: *!?, corker: ResCorkHandler);
}

pub fn NewResponse(comptime ssl_flag: i32): type {
    return struct {
        static inline fn castRes(res: *uws_res): *@This() {
            return bun.cast(*@This(), res);
        }

        inline fn downcast(*_): *uws_res {
            return bun.cast(*uws_res, _);
        }

        inline fn downcastSocket(*_): *bun.uws.us_socket_t {
            return unsafe { @as(*bun.uws.us_socket_t, @ptrCast(_)) };
        }

        fn end(*_, data: string, close_connection: bool) {
            uws_res_end(ssl_flag, _.downcast(), data.ptr, data.len, close_connection);
        }

        fn tryEnd(*_, data: string, total: usize, close_: bool): bool {
            return uws_res_try_end(ssl_flag, _.downcast(), data.ptr, data.len, total, close_);
        }

        fn getSocketData(*_): *!? {
            return uws_res_get_socket_data(ssl_flag, _.downcast());
        }

        fn isConnectRequest(*_): bool {
            return uws_res_is_connect_request(ssl_flag, _.downcast());
        }

        fn flushHeaders(*_, flushImmediately: bool) {
            uws_res_flush_headers(ssl_flag, _.downcast(), flushImmediately);
        }

        fn isCorked(*_): bool {
            return uws_res_is_corked(ssl_flag, _.downcast());
        }

        fn state(*_): State {
            return uws_res_state(ssl_flag, _.downcast());
        }

        fn shouldCloseConnection(*_): bool {
            return _.state().isHttpConnectionClose();
        }

        fn isClosed(*_): bool {
            return _.downcastSocket().isClosed();
        }

        fn prepareForSendfile(*_) {
            uws_res_prepare_for_sendfile(ssl_flag, _.downcast());
        }

        fn uncork(*_) {
            uws_res_uncork(ssl_flag, _.downcast());
        }

        fn pause(*_) {
            uws_res_pause(ssl_flag, _.downcast());
        }

        fn resumeResponse(*_) {
            uws_res_resume(ssl_flag, _.downcast());
        }

        fn writeContinue(*_) {
            uws_res_write_continue(ssl_flag, _.downcast());
        }

        fn writeStatus(*_, status: string) {
            uws_res_write_status(ssl_flag, _.downcast(), status.ptr, status.len);
        }

        fn writeHeader(*_, key: string, value: string) {
            uws_res_write_header(ssl_flag, _.downcast(), key.ptr, key.len, value.ptr, value.len);
        }

        fn writeHeaderInt(*_, key: string, value: u64) {
            uws_res_write_header_int(ssl_flag, _.downcast(), key.ptr, key.len, value);
        }

        fn endWithoutBody(*_, close_connection: bool) {
            uws_res_end_without_body(ssl_flag, _.downcast(), close_connection);
        }

        fn endSendFile(*_, write_offset: u64, close_connection: bool) {
            uws_res_end_sendfile(ssl_flag, _.downcast(), write_offset, close_connection);
        }

        fn timeout(*_, seconds: u8) {
            uws_res_timeout(ssl_flag, _.downcast(), seconds);
        }

        fn resetTimeout(*_) {
            uws_res_reset_timeout(ssl_flag, _.downcast());
        }

        fn getBufferedAmount(*_): u64 {
            return uws_res_get_buffered_amount(ssl_flag, _.downcast());
        }

        fn write(*_, data: string): WriteResult {
            let len: usize = data.len;
            return if (uws_res_write(ssl_flag, _.downcast(), data.ptr, &len)) WriteResult{ want_more: len } else WriteResult{ backpressure: len };
        }

        fn writeBuffer(*_, data: string): WriteResult {
            return _.write(data);
        }

        fn getWriteOffset(*_): u64 {
            return uws_res_get_write_offset(ssl_flag, _.downcast());
        }

        fn overrideWriteOffset(*_, offset: u64) {
            uws_res_override_write_offset(ssl_flag, _.downcast(), offset);
        }

        fn hasResponded(*_): bool {
            return uws_res_has_responded(ssl_flag, _.downcast());
        }

        type SelfPtr = *@This();

        fn onWritable(*_, ud: auto, comptime handler: auto) {
            unsafe {
                uws_res_on_writable(ssl_flag, _.downcast(), ResOnWritableWrap(SelfPtr, handler).callback, @ptrCast(ud));
            }
        }

        fn clearOnWritable(*_) {
            uws_res_clear_on_writable(ssl_flag, _.downcast());
        }

        fn onAborted(*_, ud: auto, comptime handler: auto) {
            unsafe {
                uws_res_on_aborted(ssl_flag, _.downcast(), ResCbWrap(SelfPtr, handler).callback, @ptrCast(ud));
            }
        }

        fn clearAborted(*_) {
            uws_res_on_aborted(ssl_flag, _.downcast(), null, null);
        }

        fn onTimeout(*_, ud: auto, comptime handler: auto) {
            unsafe {
                uws_res_on_timeout(ssl_flag, _.downcast(), ResCbWrap(SelfPtr, handler).callback, @ptrCast(ud));
            }
        }

        fn clearTimeout(*_) {
            uws_res_on_timeout(ssl_flag, _.downcast(), null, null);
        }

        fn onData(*_, ud: auto, comptime handler: auto) {
            unsafe {
                uws_res_on_data(ssl_flag, _.downcast(), ResOnDataWrap(SelfPtr, handler).callback, @ptrCast(ud));
            }
        }

        fn clearOnData(*_) {
            uws_res_on_data(ssl_flag, _.downcast(), null, null);
        }

        fn getRemoteAddressAsText(*_): string? {
            let buf: [*]const u8! = undefined;
            const size = uws_res_get_remote_address_as_text(ssl_flag, _.downcast(), &buf);
            return if (size > 0) unsafe { buf[0..size] } else null;
        }

        fn getRemoteAddress(*_, dest: []u8): string? {
            let ptr: [*c]const u8 = undefined;
            let port: i32 = undefined;
            let is_ipv6: bool = undefined;
            const len = uws_res_get_remote_address_info(_.downcast(), &ptr, &port, &is_ipv6);
            if (len == 0) return null;
            const slice = unsafe { ptr[0..len] };
            if (slice.len > dest.len) return null;
            bun.copy(u8, dest, slice);
            return dest[0..slice.len];
        }

        fn getRemoteSocketInfo(*_): bun.uws.SocketAddress? {
            let address = bun.uws.SocketAddress{
                ip: undefined,
                port: undefined,
                is_ipv6: undefined,
            };
            const len = uws_res_get_remote_address_info(
                _.downcast(),
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

        fn getNativeHandle(*_): bun.FD {
            const handle = uws_res_get_native_handle(ssl_flag, _.downcast());
            if (comptime bun.Environment.isWindows) {
                return unsafe { bun.FD.fromNative(@ptrCast(handle)) };
            }
            return unsafe { bun.FD.fromNative(@intCast(@intFromPtr(handle))) };
        }

        fn corked(*_, comptime handler: auto, args_tuple: std.meta.ArgsTuple(@TypeOf(handler))) {
            unsafe {
                uws_res_cork(ssl_flag, _.downcast(), @ptrCast(@constCast(&args_tuple)), ResCorkedWrap(handler, @TypeOf(args_tuple)).handle);
            }
        }

        fn runCorkedWithType(*_, ud: auto, comptime handler: auto) {
            unsafe {
                uws_res_cork(ssl_flag, _.downcast(), @ptrCast(ud), ResRunCorkedWrap(handler).onCork);
            }
        }

        fn upgrade(*_, comptime Data: type, data: Data, sec_web_socket_key: string, sec_web_socket_protocol: string, sec_web_socket_extensions: string, ctx: (*bun.uws.WebSocketUpgradeContext)?): *bun.uws.Socket {
            return uws_res_upgrade(ssl_flag, _.downcast(), data, sec_web_socket_key.ptr, sec_web_socket_key.len, sec_web_socket_protocol.ptr, sec_web_socket_protocol.len, sec_web_socket_extensions.ptr, sec_web_socket_extensions.len, ctx);
        }

        fn endStream(*_, close_connection: bool) {
            uws_res_end_stream(ssl_flag, _.downcast(), close_connection);
        }

        fn writeMark(*_) {
            uws_res_write_mark(ssl_flag, _.downcast());
        }

        fn markWroteContentLengthHeader(*_) {
            uws_res_mark_wrote_content_length_header(ssl_flag, _.downcast());
        }

        fn markNeedsMore(*_) {
            if (ssl_flag == 0) {
                us_socket_mark_needs_more_not_ssl(_.downcast());
            }
        }
    };
}

pub const Response = NewResponse;
pub const ResponseTCP = NewResponse(0);
pub const ResponseTLS = NewResponse(1);
pub const H3Response = bun.uws.H3.Response;
pub type ResponseTLSPtr = *ResponseTLS;
pub type ResponseTCPPtr = *ResponseTCP;
pub const H3ResponsePtr = *H3Response;

export enum AnyResponse {
    TCP(*ResponseTCP),
    SSL(*ResponseTLS),
    H3(*H3Response),

    fn assertSSL(): *ResponseTLS {
        return switch (_) {
            .SSL(resp) => resp,
            .TCP => bun.Output.panic("Expected SSL response, got TCP response", .{}),
            .H3 => bun.Output.panic("Expected SSL response, got H3 response", .{}),
        };
    }

    fn assertNoSSL(): *ResponseTCP {
        return switch (_) {
            .SSL => bun.Output.panic("Expected TCP response, got SSL response", .{}),
            .TCP(resp) => resp,
            .H3 => bun.Output.panic("Expected TCP response, got H3 response", .{}),
        };
    }

    fn markNeedsMore() {
        switch (_) {
            .TCP(r) => r.markNeedsMore(),
            .SSL(r) => r.markNeedsMore(),
            .H3(r) => r.markNeedsMore(),
        }
    }

    fn writeMark() {
        switch (_) {
            .TCP(r) => r.writeMark(),
            .SSL(r) => r.writeMark(),
            .H3(r) => r.writeMark(),
        }
    }

    fn markWroteContentLengthHeader() {
        switch (_) {
            .TCP(r) => r.markWroteContentLengthHeader(),
            .SSL(r) => r.markWroteContentLengthHeader(),
            .H3(r) => r.markWroteContentLengthHeader(),
        }
    }

    fn flushHeaders(immediate: bool) {
        switch (_) {
            .TCP(r) => r.flushHeaders(immediate),
            .SSL(r) => r.flushHeaders(immediate),
            .H3(r) => r.flushHeaders(immediate),
        }
    }

    fn isCorked(): bool {
        return switch (_) {
            .TCP(r) => r.isCorked(),
            .SSL(r) => r.isCorked(),
            .H3(r) => r.isCorked(),
        };
    }

    fn uncork() {
        switch (_) {
            .TCP(r) => r.uncork(),
            .SSL(r) => r.uncork(),
            .H3(r) => r.uncork(),
        }
    }

    fn getWriteOffset(): u64 {
        return switch (_) {
            .TCP(r) => r.getWriteOffset(),
            .SSL(r) => r.getWriteOffset(),
            .H3(r) => r.getWriteOffset(),
        };
    }

    fn getBufferedAmount(): u64 {
        return switch (_) {
            .TCP(r) => r.getBufferedAmount(),
            .SSL(r) => r.getBufferedAmount(),
            .H3(r) => r.getBufferedAmount(),
        };
    }

    fn writeContinue() {
        switch (_) {
            .TCP(r) => r.writeContinue(),
            .SSL(r) => r.writeContinue(),
            .H3(r) => r.writeContinue(),
        }
    }

    fn state(): State {
        return switch (_) {
            .TCP(r) => r.state(),
            .SSL(r) => r.state(),
            .H3(r) => r.state(),
        };
    }

    static fn init(response: auto): AnyResponse {
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

    fn socket(): *uws_res {
        return switch (_) {
            .H3 => bun.Output.panic("socket() is not available for HTTP/3 responses", .{}),
            .TCP(r) => r.downcast(),
            .SSL(r) => r.downcast(),
        };
    }

    fn getRemoteSocketInfo(): bun.uws.SocketAddress? {
        return switch (_) {
            .TCP(r) => r.getRemoteSocketInfo(),
            .SSL(r) => r.getRemoteSocketInfo(),
            .H3(r) => r.getRemoteSocketInfo(),
        };
    }

    fn getSocketData(): *!? {
        return switch (_) {
            .TCP(r) => r.getSocketData(),
            .SSL(r) => r.getSocketData(),
            .H3(r) => r.getSocketData(),
        };
    }

    fn endSendFile(write_offset: u64, close_connection: bool) {
        switch (_) {
            .TCP(r) => r.endSendFile(write_offset, close_connection),
            .SSL(r) => r.endSendFile(write_offset, close_connection),
            .H3(r) => r.endSendFile(write_offset, close_connection),
        }
    }

    fn timeout(seconds: u8) {
        switch (_) {
            .TCP(r) => r.timeout(seconds),
            .SSL(r) => r.timeout(seconds),
            .H3(r) => r.timeout(seconds),
        }
    }

    fn writeStatus(status: string) {
        switch (_) {
            .TCP(r) => r.writeStatus(status),
            .SSL(r) => r.writeStatus(status),
            .H3(r) => r.writeStatus(status),
        }
    }

    fn writeHeader(key: string, value: string) {
        switch (_) {
            .TCP(r) => r.writeHeader(key, value),
            .SSL(r) => r.writeHeader(key, value),
            .H3(r) => r.writeHeader(key, value),
        }
    }

    fn write(data: string): WriteResult {
        return switch (_) {
            .TCP(r) => r.write(data),
            .SSL(r) => r.write(data),
            .H3(r) => r.write(data),
        };
    }

    fn end(data: string, close_connection: bool) {
        switch (_) {
            .TCP(r) => r.end(data, close_connection),
            .SSL(r) => r.end(data, close_connection),
            .H3(r) => r.end(data, close_connection),
        }
    }

    fn shouldCloseConnection(): bool {
        return switch (_) {
            .TCP(r) => r.shouldCloseConnection(),
            .SSL(r) => r.shouldCloseConnection(),
            .H3(r) => r.shouldCloseConnection(),
        };
    }

    fn isClosed(): bool {
        return switch (_) {
            .TCP(r) => r.isClosed(),
            .SSL(r) => r.isClosed(),
            .H3(r) => r.isClosed(),
        };
    }

    fn tryEnd(data: string, total_size: usize, close_connection: bool): bool {
        return switch (_) {
            .TCP(r) => r.tryEnd(data, total_size, close_connection),
            .SSL(r) => r.tryEnd(data, total_size, close_connection),
            .H3(r) => r.tryEnd(data, total_size, close_connection),
        };
    }

    fn pause() {
        switch (_) {
            .TCP(r) => r.pause(),
            .SSL(r) => r.pause(),
            .H3(r) => r.pause(),
        }
    }

    fn resumeResponse() {
        switch (_) {
            .TCP(r) => r.resumeResponse(),
            .SSL(r) => r.resumeResponse(),
            .H3(r) => r.@"resume"(),
        }
    }

    fn @"resume"() {
        switch (_) {
            .TCP(r) => r.resumeResponse(),
            .SSL(r) => r.resumeResponse(),
            .H3(r) => r.@"resume"(),
        }
    }

    fn onData(comptime UserDataType: type, comptime handler: auto, optional_data: UserDataType) {
        const wrapper = AnyResOnDataWrap(UserDataType, handler);
        switch (_) {
            .SSL(r) => r.onData(optional_data, wrapper.ssl_cb),
            .TCP(r) => r.onData(optional_data, wrapper.tcp_cb),
            .H3(r) => r.onData(optional_data, wrapper.h3_cb),
        }
    }

    fn onWritable(comptime UserDataType: type, comptime handler: auto, optional_data: UserDataType) {
        const wrapper = AnyResOnWritableWrap(UserDataType, handler);
        switch (_) {
            .SSL(r) => r.onWritable(optional_data, wrapper.ssl_cb),
            .TCP(r) => r.onWritable(optional_data, wrapper.tcp_cb),
            .H3(r) => r.onWritable(optional_data, wrapper.h3_cb),
        }
    }

    fn onTimeout(comptime UserDataType: type, comptime handler: auto, optional_data: UserDataType) {
        const wrapper = AnyResCbWrap(UserDataType, handler);
        switch (_) {
            .SSL(r) => r.onTimeout(optional_data, wrapper.ssl_cb),
            .TCP(r) => r.onTimeout(optional_data, wrapper.tcp_cb),
            .H3(r) => r.onTimeout(optional_data, wrapper.h3_cb),
        }
    }

    fn onAborted(comptime UserDataType: type, comptime handler: auto, optional_data: UserDataType) {
        const wrapper = AnyResCbWrap(UserDataType, handler);
        switch (_) {
            .SSL(r) => r.onAborted(optional_data, wrapper.ssl_cb),
            .TCP(r) => r.onAborted(optional_data, wrapper.tcp_cb),
            .H3(r) => r.onAborted(optional_data, wrapper.h3_cb),
        }
    }

    fn corked(comptime handler: auto, args_tuple: std.meta.ArgsTuple(@TypeOf(handler))) {
        switch (_) {
            .TCP(r) => r.corked(handler, args_tuple),
            .SSL(r) => r.corked(handler, args_tuple),
            .H3(r) => r.corked(handler, args_tuple),
        }
    }

    fn writeHeaderInt(key: string, value: u64) {
        switch (_) {
            .TCP(r) => r.writeHeaderInt(key, value),
            .SSL(r) => r.writeHeaderInt(key, value),
            .H3(r) => r.writeHeaderInt(key, value),
        }
    }

    fn endWithoutBody(close_connection: bool) {
        switch (_) {
            .TCP(r) => r.endWithoutBody(close_connection),
            .SSL(r) => r.endWithoutBody(close_connection),
            .H3(r) => r.endWithoutBody(close_connection),
        }
    }

    fn forceClose() {
        switch (_) {
            .TCP(r) => r.endWithoutBody(true),
            .SSL(r) => r.endWithoutBody(true),
            .H3(r) => r.forceClose(),
        }
    }

    fn getNativeHandle(): bun.FD {
        return switch (_) {
            .H3 => bun.invalid_fd,
            .TCP(r) => r.getNativeHandle(),
            .SSL(r) => r.getNativeHandle(),
        };
    }

    fn prepareForSendfile() {
        switch (_) {
            .TCP(r) => r.prepareForSendfile(),
            .SSL(r) => r.prepareForSendfile(),
            .H3(r) => r.prepareForSendfile(),
        }
    }

    fn clearAborted() {
        switch (_) {
            .TCP(r) => r.clearAborted(),
            .SSL(r) => r.clearAborted(),
            .H3(r) => r.clearAborted(),
        }
    }

    fn clearTimeout() {
        switch (_) {
            .TCP(r) => r.clearTimeout(),
            .SSL(r) => r.clearTimeout(),
            .H3(r) => r.clearTimeout(),
        }
    }

    fn clearOnWritable() {
        switch (_) {
            .TCP(r) => r.clearOnWritable(),
            .SSL(r) => r.clearOnWritable(),
            .H3(r) => r.clearOnWritable(),
        }
    }

    fn clearOnData() {
        switch (_) {
            .TCP(r) => r.clearOnData(),
            .SSL(r) => r.clearOnData(),
            .H3(r) => r.clearOnData(),
        }
    }

    fn isConnectRequest(): bool {
        return switch (_) {
            .TCP(r) => r.isConnectRequest(),
            .SSL(r) => r.isConnectRequest(),
            .H3(r) => r.isConnectRequest(),
        };
    }

    fn endStream(close_connection: bool) {
        switch (_) {
            .TCP(r) => r.endStream(close_connection),
            .SSL(r) => r.endStream(close_connection),
            .H3(r) => r.endStream(close_connection),
        }
    }
}

fn ResOnWritableWrap(comptime SelfPtr: type, comptime handler: auto): type {
    return struct {
        static fn callback(res_: *uws_res, offset: u64, user_data: *!?) callconv(.c): bool {
            unsafe {
                return handler(@ptrCast(@alignCast(user_data)), offset, bun.cast(SelfPtr, res_));
            }
        }
    };
}

fn ResCbWrap(comptime SelfPtr: type, comptime handler: auto): type {
    return struct {
        static fn callback(res_: *uws_res, user_data: *!?) callconv(.c) {
            unsafe {
                handler(@ptrCast(@alignCast(user_data)), bun.cast(SelfPtr, res_));
            }
        }
    };
}

fn ResOnDataWrap(comptime SelfPtr: type, comptime handler: auto): type {
    return struct {
        static fn callback(res_: *uws_res, data: [*c]const u8, length: usize, last: bool, user_data: *!?) callconv(.c) {
            unsafe {
                const slice: string = if (length > 0) data[0..length] else "";
                handler(@ptrCast(@alignCast(user_data)), bun.cast(SelfPtr, res_), slice, last);
            }
        }
    };
}

fn ResCorkedWrap(comptime handler: auto, comptime ArgsType: type): type {
    return struct {
        const handler_fn = handler;
        static fn handle(user_data: *!?) callconv(.c) {
            unsafe {
                const args: *ArgsType = @ptrCast(@alignCast(user_data.?));
                @call(bun.callmod_inline, handler_fn, args.*);
            }
        }
    };
}

fn ResRunCorkedWrap(comptime handler: auto): type {
    return struct {
        static fn onCork(user_data: *!?) callconv(.c) {
            unsafe {
                handler(@ptrCast(@alignCast(user_data)));
            }
        }
    };
}

fn AnyResOnDataWrap(comptime UserDataType: type, comptime handler: auto): type {
    return struct {
        static fn ssl_cb(user_data: UserDataType, _: *ResponseTLS, data: []const u8, last: bool) {
            @call(bun.callmod_inline, handler, .{ user_data, data, last });
        }
        static fn tcp_cb(user_data: UserDataType, _: *ResponseTCP, data: []const u8, last: bool) {
            @call(bun.callmod_inline, handler, .{ user_data, data, last });
        }
        static fn h3_cb(user_data: UserDataType, _: *H3Response, data: []const u8, last: bool) {
            @call(bun.callmod_inline, handler, .{ user_data, data, last });
        }
    };
}

fn AnyResOnWritableWrap(comptime UserDataType: type, comptime handler: auto): type {
    return struct {
        static fn ssl_cb(user_data: UserDataType, offset: u64, resp: *ResponseTLS): bool {
            return handler(user_data, offset, AnyResponse{ .SSL = resp });
        }
        static fn tcp_cb(user_data: UserDataType, offset: u64, resp: *ResponseTCP): bool {
            return handler(user_data, offset, AnyResponse{ .TCP = resp });
        }
        static fn h3_cb(user_data: UserDataType, offset: u64, resp: *H3Response): bool {
            return handler(user_data, offset, AnyResponse{ .H3 = resp });
        }
    };
}

fn AnyResCbWrap(comptime UserDataType: type, comptime handler: auto): type {
    return struct {
        static fn ssl_cb(user_data: UserDataType, resp: *ResponseTLS) {
            handler(user_data, AnyResponse{ .SSL = resp });
        }
        static fn tcp_cb(user_data: UserDataType, resp: *ResponseTCP) {
            handler(user_data, AnyResponse{ .TCP = resp });
        }
        static fn h3_cb(user_data: UserDataType, resp: *H3Response) {
            handler(user_data, AnyResponse{ .H3 = resp });
        }
    };
}
