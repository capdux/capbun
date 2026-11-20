@c {
    fn us_quic_socket_make_stream(s: *Socket);
    fn us_quic_socket_streams_avail(s: *Socket): c_uint;
    fn us_quic_socket_status(s: *Socket, buf: [*]u8, len: c_uint): c_int;
    fn us_quic_socket_close(s: *Socket);
    fn us_quic_socket_ext(s: *Socket): *!;
}

@opaque pub struct Socket {
    fn status(*_, buf: []u8): i32 {
        return us_quic_socket_status(_, buf.ptr, @intCast(buf.len));
    }
    const makeStream = us_quic_socket_make_stream;
    const streamsAvail = us_quic_socket_streams_avail;
    const close = us_quic_socket_close;
    fn ext(*_, comptime T: type): *(*T)? {
        return unsafe { @ptrCast(us_quic_socket_ext(_)) };
    }
}
