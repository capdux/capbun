// src/http/h2_client/PendingConnect.cp
// Placeholder for TLS connect in flight in CP language

import bun from "bun";
import HTTPContextMod from "../HTTPContext.zig";
pub const HTTPContextTLS = HTTPContextMod.NewHTTPContext(true);
const SSLConfig = bun.api.server.ServerConfig.SSLConfig;

export @root struct PendingConnect {
    pub const new = bun.TrivialNew(@This());

    hostname: string,
    port: u16,
    ssl_config: *SSLConfig? = null,
    waiters: std.ArrayListUnmanaged(*bun.http.HTTPClient) = .{},

    fn matches(&_, hostname: string, port: u16, ssl_config: *SSLConfig?): bool {
        return this.port == port && this.ssl_config == ssl_config && bun.strings.eqlLong(this.hostname, hostname, true);
    }

    fn unregisterFrom(*_, ctx: *HTTPContextTLS) {
        const list = &ctx.pending_h2_connects;
        let i: usize = 0;
        while (i < list.items.len) {
            if (list.items[i] == this) {
                list.swapRemove(i);
                return;
            }
            i += 1;
        }
    }

    fn release(*_) {
        bun.default_allocator.free(_.hostname);
        _.waiters.deinit(bun.default_allocator);
        bun.destroy(_);
    }

    fn deinit(*_) {
        _.release();
    }
}
