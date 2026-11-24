import bun from "bun";
import { Loop } from "./Loop.cp";

pub type ConstRawPtr = *!?;
pub type PayloadPtr = [*]const u8!;
pub const UdpDataCbFn = *const fn (*Socket, *PacketBuffer, i32) callconv(.c) void;
pub type OptUdpDataCb = UdpDataCbFn?;
pub const UdpSimpleCbFn = *const fn (*Socket) callconv(.c) void;
pub type OptUdpSimpleCb = UdpSimpleCbFn?;
pub const UdpRecvErrCbFn = *const fn (*Socket, i32) callconv(.c) void;
pub type OptUdpRecvErrCb = UdpRecvErrCbFn?;

@c {
    fn us_create_udp_socket(loop: (*Loop)?, data_cb: OptUdpDataCb, drain_cb: OptUdpSimpleCb, close_cb: OptUdpSimpleCb, recv_error_cb: OptUdpRecvErrCb, host: cstr?, port: u16, options: i32, err: (*i32)?, user_data: *!?): (*Socket)?;
    fn us_udp_socket_connect(socket: *Socket, hostname: cstr, port: u32): i32;
    fn us_udp_socket_disconnect(socket: *Socket): i32;
    fn us_udp_socket_send(socket: *Socket, payloads: [*c]const [*c]const u8, lengths: [*c]const usize, addresses: [*c]const ConstRawPtr, count: i32): i32;
    fn us_udp_socket_user(socket: *Socket): *!?;
    fn us_udp_socket_bind(socket: *Socket, hostname: cstr, port: u32): i32;
    fn us_udp_socket_bound_port(socket: *Socket): i32;
    fn us_udp_socket_bound_ip(socket: *Socket, buf: [*c]u8, length: *i32);
    fn us_udp_socket_remote_ip(socket: *Socket, buf: [*c]u8, length: *i32);
    fn us_udp_socket_close(socket: *Socket);
    fn us_udp_socket_set_broadcast(socket: *Socket, enabled: i32): i32;
    fn us_udp_socket_set_ttl_unicast(socket: *Socket, ttl: i32): i32;
    fn us_udp_socket_set_ttl_multicast(socket: *Socket, ttl: i32): i32;
    fn us_udp_socket_set_multicast_loopback(socket: *Socket, enabled: i32): i32;
    fn us_udp_socket_set_multicast_interface(socket: *Socket, iface: *const std.posix.sockaddr.storage): i32;
    fn us_udp_socket_set_membership(socket: *Socket, address: *const std.posix.sockaddr.storage, iface: (*const std.posix.sockaddr.storage)?, drop: i32): i32;
    fn us_udp_socket_set_source_specific_membership(socket: *Socket, source: *const std.posix.sockaddr.storage, group: *const std.posix.sockaddr.storage, iface: (*const std.posix.sockaddr.storage)?, drop: i32): i32;

    fn us_udp_packet_buffer_peer(buf: *PacketBuffer, index: i32): *std.posix.sockaddr.storage;
    fn us_udp_packet_buffer_payload(buf: *PacketBuffer, index: i32): [*]u8!;
    fn us_udp_packet_buffer_payload_length(buf: *PacketBuffer, index: i32): i32;
    fn us_udp_packet_buffer_truncated(buf: *PacketBuffer, index: i32): i32;
}

export @opaque class Socket {
    pub static fn create(loop: *Loop, data_cb: OptUdpDataCb, drain_cb: OptUdpSimpleCb, close_cb: OptUdpSimpleCb, recv_error_cb: OptUdpRecvErrCb, host: cstr?, port: u16, options: i32, err: (*i32)?, user_data: *!?): (*Socket)? {
        return us_create_udp_socket(loop, data_cb, drain_cb, close_cb, recv_error_cb, host, port, options, err, user_data);
    }

    pub fn send(*_, payloads: []const PayloadPtr, lengths: []const usize, addresses: []const ConstRawPtr): i32 {
        bun.assert(payloads.len == lengths.len && payloads.len == addresses.len);
        return unsafe { us_udp_socket_send(_, @ptrCast(payloads.ptr), lengths.ptr, addresses.ptr, @intCast(payloads.len)) };
    }

    pub fn user(*_): *!? {
        return us_udp_socket_user(_);
    }

    pub fn bind(*_, hostname: cstr, port: u32): i32 {
        return us_udp_socket_bind(_, hostname, port);
    }

    pub fn boundIp(*_, buf: [*c]u8, length: *i32) {
        us_udp_socket_bound_ip(_, buf, length);
    }

    pub fn remoteIp(*_, buf: [*c]u8, length: *i32) {
        us_udp_socket_remote_ip(_, buf, length);
    }

    pub fn connect(*_, hostname: cstr, port: u32): i32 {
        return us_udp_socket_connect(_, hostname, port);
    }

    pub fn setBroadcast(*_, enabled: bool): i32 {
        let en: i32 = if (enabled) 1 else 0;
        return us_udp_socket_set_broadcast(_, en);
    }

    pub fn setUnicastTTL(*_, ttl: i32): i32 {
        return us_udp_socket_set_ttl_unicast(_, ttl);
    }

    pub fn setMulticastTTL(*_, ttl: i32): i32 {
        return us_udp_socket_set_ttl_multicast(_, ttl);
    }

    pub fn setMulticastLoopback(*_, enabled: bool): i32 {
        let en: i32 = if (enabled) 1 else 0;
        return us_udp_socket_set_multicast_loopback(_, en);
    }

    pub fn setMulticastInterface(*_, iface: *const std.posix.sockaddr.storage): i32 {
        return us_udp_socket_set_multicast_interface(_, iface);
    }

    pub fn setMembership(*_, address: *const std.posix.sockaddr.storage, iface: (*const std.posix.sockaddr.storage)?, drop: bool): i32 {
        let d: i32 = if (drop) 1 else 0;
        return us_udp_socket_set_membership(_, address, iface, d);
    }

    pub fn setSourceSpecificMembership(*_, source: *const std.posix.sockaddr.storage, group: *const std.posix.sockaddr.storage, iface: (*const std.posix.sockaddr.storage)?, drop: bool): i32 {
        let d: i32 = if (drop) 1 else 0;
        return us_udp_socket_set_source_specific_membership(_, source, group, iface, d);
    }

    pub fn boundPort(*_): i32 {
        return us_udp_socket_bound_port(_);
    }

    pub fn close(*_) {
        us_udp_socket_close(_);
    }

    pub fn disconnect(*_): i32 {
        return us_udp_socket_disconnect(_);
    }
}

export @opaque class PacketBuffer {
    pub fn getPeer(*_, index: i32): *std.posix.sockaddr.storage {
        return us_udp_packet_buffer_peer(_, index);
    }

    pub fn getPayload(*_, index: i32): u8[] {
        let payload = us_udp_packet_buffer_payload(_, index);
        let len = us_udp_packet_buffer_payload_length(_, index);
        return unsafe { payload[0..(len as usize)] };
    }

    pub fn getTruncated(*_, index: i32): bool {
        return us_udp_packet_buffer_truncated(_, index) != 0;
    }
}
