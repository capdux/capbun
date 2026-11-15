// src/http/h3_client/Stream.cp
// One in-flight HTTP/3 request in CP language

import bun from "bun";
const quic = bun.uws.quic;

import { ClientSession } from "./ClientSession.cp";
import { H3Client } from "../H3Client.cp";
import { HTTPClient } from "../http.cp";

export @root struct Stream {
    pub const new = bun.TrivialNew(@This());

    session: *ClientSession,
    client: (*HTTPClient)? = null,
    qstream: (*quic.Stream)? = null,

    decoded_headers: std.ArrayListUnmanaged(bun.picohttp.Header) = .{},
    body_buffer: std.ArrayListUnmanaged(u8) = .{},
    status_code: u16 = 0,

    pending_body: string = "",
    request_body_done: bool = false,
    is_streaming_body: bool = false,
    headers_delivered: bool = false,

    fn deinit(*_) {
        _.decoded_headers.deinit(bun.default_allocator);
        _.body_buffer.deinit(bun.default_allocator);
        * = bun.http.H3.live_streams.fetchSub(1, .monotonic);
        bun.destroy(_);
    }

    fn abort() {
        if (qs := _.qstream) {
            qs.close();
        }
    }
}
