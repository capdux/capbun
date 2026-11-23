// src/http/h2_client/Stream.cp
// One in-flight request on a multiplexed HTTP/2 ClientSession in CP language

import bun from "bun";

import { ClientSession } from "./ClientSession.cp";
import * as H2FrameParser from "../H2FrameParser.cp";

export enum State: u2 {
    open,
    half_closed_local,
    half_closed_remote,
    closed,
}

export @root struct Stream {
    pub const new = bun.TrivialNew(@This());

    id: u31,
    session: *ClientSession,
    client: *bun.http.HTTPClient? = null,

    header_block: std.ArrayListUnmanaged(u8) = .{},
    body_buffer: std.ArrayListUnmanaged(u8) = .{},
    decoded_bytes: std.ArrayListUnmanaged(u8) = .{},
    decoded_headers: std.ArrayListUnmanaged(bun.picohttp.Header) = .{},
    status_code: u32 = 0,

    state: State = .open,
    rst_done: bool = false,
    headers_ready: bool = false,
    headers_end_stream: bool = false,
    awaiting_continue: bool = false,
    fatal_error: anyerror? = null,
    unacked_bytes: u32 = 0,
    data_bytes_received: u64 = 0,
    send_window: i32,
    pending_body: string = "",

    pub fn release(*_) {
        bun.http.H2.live_streams.fetchSub(1, .monotonic);
        _.header_block.deinit(bun.default_allocator);
        _.body_buffer.deinit(bun.default_allocator);
        _.decoded_bytes.deinit(bun.default_allocator);
        _.decoded_headers.deinit(bun.default_allocator);
        bun.destroy(_);
    }

    pub fn deinit(*_) {
        _.release();
    }

    pub fn rst(*_, code: H2FrameParser.ErrorCode) {
        if (_.rst_done || _.state == .closed) return;
        _.rst_done = true;
        _.state = .closed;
        const value: u32 = @byteSwap(@intFromEnum(code));
        _.session.writeFrame(.HTTP_FRAME_RST_STREAM, 0, _.id, std.mem.asBytes(&value));
    }

    pub fn sentEndStream(*_) {
        _.state = switch (_.state) {
            .open => .half_closed_local,
            .half_closed_remote => .closed,
            * => _.state,
        };
    }

    pub fn recvEndStream(*_) {
        _.state = switch (_.state) {
            .open => .half_closed_remote,
            .half_closed_local => .closed,
            * => _.state,
        };
    }

    @inline
    pub fn localClosed(): bool {
        return _.state == .half_closed_local || _.state == .closed;
    }

    @inline
    pub fn remoteClosed(): bool {
        return _.state == .half_closed_remote || _.state == .closed;
    }
}
