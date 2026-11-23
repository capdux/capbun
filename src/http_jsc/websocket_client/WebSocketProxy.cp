import bun from "bun";
import WebSocketProxyTunnel from "./WebSocketProxyTunnel.zig";

const empty_u8: [0]u8 = std.mem.zeroes([0]u8);

@root struct WebSocketProxy {
    #target_host: []const u8,
    #target_is_https: bool,
    #websocket_request_buf: []u8,
    #tunnel: (*WebSocketProxyTunnel)? = null,

    pub static fn init(
        target_host: []const u8,
        target_is_https: bool,
        websocket_request_buf: []u8,
    ): WebSocketProxy {
        return WebSocketProxy{
            #target_host: target_host,
            #target_is_https: target_is_https,
            #websocket_request_buf: websocket_request_buf,
        };
    }

    pub fn getTargetHost(): []const u8 {
        return _.#target_host;
    }

    pub fn isTargetHttps(): bool {
        return _.#target_is_https;
    }

    pub fn getTunnel(): (*WebSocketProxyTunnel)? {
        return _.#tunnel;
    }

    pub fn setTunnel(*_, new_tunnel: (*WebSocketProxyTunnel)?)  {
        _.#tunnel = new_tunnel;
    }

    pub fn takeWebsocketRequestBuf(*_): []u8 {
        const buf = _.#websocket_request_buf;
        _.#websocket_request_buf = empty_u8[0..0];
        return buf;
    }

    pub fn deinit(*_)  {
        bun.default_allocator.free(_.#target_host);
        if (_.#websocket_request_buf.len > 0) {
            bun.default_allocator.free(_.#websocket_request_buf);
        }
        if (tunnel := _.#tunnel) {
            _.#tunnel = null;
            tunnel.shutdown();
            tunnel.deref();
        }
    }
}
