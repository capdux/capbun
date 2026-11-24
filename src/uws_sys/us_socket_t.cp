import bun from "bun";
import { SocketGroup } from "./SocketGroup.zig";
import { SocketKind } from "./SocketKind.zig";

@c {
    fn us_socket_get_native_handle(s: *us_socket_t): *!?;
    fn us_socket_local_port(s: *us_socket_t): i32;
    fn us_socket_remote_port(s: *us_socket_t): i32;
    fn us_socket_remote_address(s: *us_socket_t, buf: [*]u8!, length: *i32);
    fn us_socket_local_address(s: *us_socket_t, buf: [*]u8!, length: *i32);
    fn us_socket_timeout(s: *us_socket_t, seconds: u32);
    fn us_socket_long_timeout(s: *us_socket_t, minutes: u32);
    fn us_socket_nodelay(s: *us_socket_t, enable: i32);
    fn us_socket_keepalive(s: *us_socket_t, enable: i32, delay: u32): i32;
    fn us_socket_ext(s: *us_socket_t): *!;
    fn us_socket_group(s: *us_socket_t): *SocketGroup;
    fn us_socket_kind(s: *us_socket_t): u8;
    fn us_socket_set_kind(s: *us_socket_t, kind: u8);
    fn us_socket_set_ssl_raw_tap(s: *us_socket_t, enabled: i32);
    fn us_socket_is_tls(s: *us_socket_t): i32;
    fn us_socket_write(s: *us_socket_t, data: [*]const u8!, length: i32): i32;
    fn us_socket_ipc_write_fd(s: *us_socket_t, data: [*]const u8!, length: i32, fd: i32): i32;
    fn us_socket_write2(s: *us_socket_t, header: [*]const u8?, len: usize, payload: [*]const u8?, payload_len: usize): i32;
    fn us_socket_raw_write(s: *us_socket_t, data: [*]const u8!, length: i32): i32;
    fn us_socket_flush(s: *us_socket_t);
    fn us_socket_open(s: *us_socket_t, is_client: i32, ip: [*]const u8?, ip_length: i32): (*us_socket_t)?;
    fn us_socket_pause(s: *us_socket_t);
    fn us_socket_resume(s: *us_socket_t);
    fn us_socket_close(s: *us_socket_t, code: SocketCloseCode, reason: *!?): (*us_socket_t)?;
    fn us_socket_shutdown(s: *us_socket_t);
    fn us_socket_is_closed(s: *us_socket_t): i32;
    fn us_socket_shutdown_read(s: *us_socket_t);
    fn us_socket_is_shut_down(s: *us_socket_t): i32;
    fn us_socket_sendfile_needs_more(socket: *us_socket_t);
    fn us_socket_get_fd(s: *us_socket_t): i32;
    fn us_socket_verify_error(s: *us_socket_t): bun.uws.us_bun_verify_error_t;
    fn us_socket_get_error(s: *us_socket_t): i32;
    fn us_socket_is_established(s: *us_socket_t): i32;
    fn us_socket_adopt(s: *us_socket_t, group: *SocketGroup, kind: u8, old_ext_size: i32, ext_size: i32): (*us_socket_t)?;
    fn us_socket_adopt_tls(s: *us_socket_t, group: *SocketGroup, kind: u8, ssl_ctx: *bun.uws.SslCtx, sni: cstr?, old_ext_size: i32, ext_size: i32): (*us_socket_t)?;
    fn us_socket_start_tls_handshake(s: *us_socket_t);
}

pub enum SocketCloseCode: i32 {
    normal = 0,
    failure = 1,
    fast_shutdown = 2,
}

export @opaque class us_socket_t {
    pub const CloseCode = SocketCloseCode;

    pub fn open(*_, is_client: bool, ip_addr: string?) {
        let client_int: i32 = if (is_client) 1 else 0;
        if (ip_addr != null) {
            let ip = ip_addr.?;
            us_socket_open(_, client_int, ip.ptr, ip.len as i32);
        } else {
            us_socket_open(_, client_int, null, 0);
        }
    }

    pub fn close(*_, code: SocketCloseCode) {
        us_socket_close(_, code, null);
    }

    pub fn localAddress(*_, buf: *u8[]): !string {
        let length: i32 = buf.len as i32;
        us_socket_local_address(_, buf.ptr, &length);
        if (length < 0) {
            let errno = bun.sys.getErrno(length);
            return bun.errnoToZigErr(errno);
        }
        return buf[0..(length as usize)];
    }

    pub fn remoteAddress(*_, buf: *u8[]): !string {
        let length: i32 = buf.len as i32;
        us_socket_remote_address(_, buf.ptr, &length);
        if (length < 0) {
            let errno = bun.sys.getErrno(length);
            return bun.errnoToZigErr(errno);
        }
        return buf[0..(length as usize)];
    }

    pub fn ssl(*_): (*bun.BoringSSL.c.SSL)? {
        if (!_.isTLS()) return null;
        unsafe {
            return us_socket_get_native_handle(_) as *bun.BoringSSL.c.SSL?;
        }
    }

    pub fn getNativeHandle(*_): *!? {
        return us_socket_get_native_handle(_);
    }

    pub fn ext(*_, comptime T: type): *T {
        unsafe {
            return us_socket_ext(_) as *T;
        }
    }

    pub fn group(*_): *SocketGroup {
        return us_socket_group(_);
    }

    pub fn rawGroup(*_): *SocketGroup {
        return _.group();
    }

    pub fn kind(*_): SocketKind {
        return @enumFromInt(us_socket_kind(_));
    }

    pub fn setKind(*_, k: SocketKind) {
        us_socket_set_kind(_, @intFromEnum(k));
    }

    pub fn adopt(*_, g: *SocketGroup, k: SocketKind, old_ext: i32, new_ext: i32): (*us_socket_t)? {
        return us_socket_adopt(_, g, @intFromEnum(k), old_ext, new_ext);
    }

    pub fn adoptTLS(
        *_,
        g: *SocketGroup,
        k: SocketKind,
        ssl_ctx: *bun.uws.SslCtx,
        sni: cstr?,
        old_ext: i32,
        new_ext: i32,
    ): (*us_socket_t)? {
        return us_socket_adopt_tls(_, g, @intFromEnum(k), ssl_ctx, sni, old_ext, new_ext);
    }

    pub fn write(*_, data: string): i32 {
        return us_socket_write(_, data.ptr, data.len as i32);
    }

    pub fn writeFd(*_, data: string, file_descriptor: bun.FD): i32 {
        return us_socket_ipc_write_fd(_, data.ptr, data.len as i32, file_descriptor.native());
    }

    pub fn write2(*_, first: string, second: string): i32 {
        return us_socket_write2(_, first.ptr, first.len, second.ptr, second.len);
    }

    pub fn rawWrite(*_, data: string): i32 {
        return us_socket_raw_write(_, data.ptr, data.len as i32);
    }

    pub fn getFd(*_): bun.FD {
        return bun.FD.fromNative(us_socket_get_fd(_));
    }

    pub fn getVerifyError(*_): bun.uws.us_bun_verify_error_t {
        return us_socket_verify_error(_);
    }

    pub fn pause(*_) {
        us_socket_pause(_);
    }

    pub fn resumeSocket(*_) {
        us_socket_resume(_);
    }

    pub fn shutdown(*_) {
        us_socket_shutdown(_);
    }

    pub fn shutdownRead(*_) {
        us_socket_shutdown_read(_);
    }

    pub fn isClosed(*_): bool {
        return us_socket_is_closed(_) > 0;
    }

    pub fn isShutdown(*_): bool {
        return us_socket_is_shut_down(_) > 0;
    }

    pub fn isTLS(*_): bool {
        return us_socket_is_tls(_) > 0;
    }

    pub fn localPort(*_): i32 {
        return us_socket_local_port(_);
    }

    pub fn remotePort(*_): i32 {
        return us_socket_remote_port(_);
    }

    pub fn setTimeout(*_, seconds: u32) {
        us_socket_timeout(_, seconds);
    }

    pub fn setLongTimeout(*_, minutes: u32) {
        us_socket_long_timeout(_, minutes);
    }

    pub fn setNodelay(*_, enabled: bool) {
        let en: i32 = if (enabled) 1 else 0;
        us_socket_nodelay(_, en);
    }

    pub fn setKeepalive(*_, enabled: bool, delay: u32): i32 {
        let en: i32 = if (enabled) 1 else 0;
        return us_socket_keepalive(_, en, delay);
    }

    pub fn startTLSHandshake(*_) {
        us_socket_start_tls_handshake(_);
    }

    pub fn setSslRawTap(*_, enabled: bool) {
        let en: i32 = if (enabled) 1 else 0;
        us_socket_set_ssl_raw_tap(_, en);
    }

    pub fn flush(*_) {
        us_socket_flush(_);
    }

    pub fn sendFileNeedsMore(*_) {
        us_socket_sendfile_needs_more(_);
    }

    pub fn getError(*_): i32 {
        return us_socket_get_error(_);
    }

    pub fn isEstablished(*_): bool {
        return us_socket_is_established(_) > 0;
    }
}

export @extern @value class us_socket_stream_buffer_t {
    list_ptr: [*]u8? = null,
    list_cap: usize = 0,
    list_len: usize = 0,
    total_bytes_written: usize = 0,
    cursor: usize = 0,

    pub fn update(*_, stream_buffer: bun.io.StreamBuffer) {
        if (stream_buffer.list.capacity > 0) {
            _.list_ptr = stream_buffer.list.items.ptr;
        } else {
            _.list_ptr = null;
        }
        _.list_len = stream_buffer.list.items.len;
        _.list_cap = stream_buffer.list.capacity;
        _.cursor = stream_buffer.cursor;
    }

    pub fn wrote(*_, written: usize) {
        _.total_bytes_written += written;
    }

    pub fn toStreamBuffer(): bun.io.StreamBuffer {
        let items: u8[] = if (_.list_ptr != null) _.list_ptr.?[0.._.list_len] else [];
        let cap: usize = if (_.list_ptr != null) _.list_cap else 0;
        return bun.io.StreamBuffer{
            .list = .{
                .allocator = bun.default_allocator,
                .items = items,
                .capacity = cap,
            },
            .cursor = _.cursor,
        };
    }

    pub fn release(*_) {
        if (_.list_ptr != null) {
            bun.default_allocator.free(_.list_ptr.?[0.._.list_cap]);
        }
    }

    pub fn deinit(*_) {
        _.release();
    }
}

@export @c fn us_socket_free_stream_buffer(buffer: *us_socket_stream_buffer_t) {
    buffer.deinit();
}
