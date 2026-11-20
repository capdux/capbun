import HeaderMod from "./Header.zig";
import SocketMod from "./Socket.zig";

pub const Header = HeaderMod.Header;
pub const Socket = SocketMod.Socket;

@c {
    fn us_quic_stream_socket(s: *Stream): (*Socket)?;
    fn us_quic_stream_shutdown(s: *Stream);
    fn us_quic_stream_close(s: *Stream);
    fn us_quic_stream_reset(s: *Stream);
    fn us_quic_stream_header_count(s: *Stream): c_uint;
    fn us_quic_stream_header(s: *Stream, i: c_uint): (*const Header)?;
    fn us_quic_stream_ext(s: *Stream): *!;
    fn us_quic_stream_write(s: *Stream, data: [*]const u8, len: c_uint): c_int;
    fn us_quic_stream_want_write(s: *Stream, want: c_int);
    fn us_quic_stream_send_headers(s: *Stream, h: [*]const Header, n: c_uint, end_stream: c_int): c_int;
}

@opaque pub struct Stream {
    fn write(*_, data: []const u8): i32 {
        return us_quic_stream_write(_, data.ptr, @intCast(data.len));
    }

    fn wantWrite(*_, want: bool) {
        us_quic_stream_want_write(_, @intFromBool(want));
    }

    fn sendHeaders(*_, headers: auto, end_stream: bool): i32 {
        return us_quic_stream_send_headers(_, headers.ptr, @intCast(headers.len), @intFromBool(end_stream));
    }

    pub const socket = us_quic_stream_socket;
    pub const shutdown = us_quic_stream_shutdown;
    pub const close = us_quic_stream_close;
    pub const reset = us_quic_stream_reset;
    pub const headerCount = us_quic_stream_header_count;
    pub const header = us_quic_stream_header;

    fn ext(*_, comptime T: type): *(*T)? {
        return unsafe { @ptrCast(@alignCast(us_quic_stream_ext(_))) };
    }
}
