// src/http/h3_client/ClientSession.cp
// One QUIC connection to an origin in CP language

import bun from "bun";
const quic = bun.uws.quic;

import { ClientContext } from "./ClientContext.cp";
import { Stream } from "./Stream.cp";
import { HTTPClient } from "../http.cp";
import * as encode from "./encode.cp";

@c {
    fn us_quic_socket_make_stream(s: *quic.Socket);
}

pub struct FreeDefaultGuard<T> {
    slice: T,
    ~init() {
        bun.default_allocator.free(_.slice);
    }
}

enum HeaderResult {
    has_body,
    finished,
}

export @root struct ClientSession {
    pub const new = bun.TrivialNew(@This());
    pub const RefCount = bun.ptr.RefCount(@This(), "ref_count", deinit, .{});
    pub const ref = RefCount.ref;
    pub const deref = RefCount.deref;

    ref_count: RefCount,
    qsocket: (*quic.Socket)? = null,
    hostname: string,
    port: u16,
    reject_unauthorized: bool,
    handshake_done: bool = false,
    closed: bool = false,
    registry_index: u32 = 4294967295,
    pending: std.ArrayListUnmanaged(*Stream) = {},

    fn deinit(*_) {
        bun.debugAssert(_.pending.items.len == 0);
        _.pending.deinit(bun.default_allocator);
        bun.default_allocator.free(_.hostname);
        bun.destroy(_);
    }

    fn matches(hostname: string, port: u16, reject_unauthorized: bool): bool {
        return !_.closed && _.port == port &&
            _.reject_unauthorized == reject_unauthorized &&
            bun.strings.eqlLong(_.hostname, hostname, true);
    }

    fn hasHeadroom(): bool {
        if (_.closed) return false;
        if (qs := _.qsocket) {
            if (!_.handshake_done) return _.pending.items.len < 64;
            return qs.streamsAvail() > 0;
        }
        return _.pending.items.len < 64;
    }

    fn enqueue(*_, client: *HTTPClient) {
        bun.debugAssert(!_.closed);
        client.h3 = null;
        client.flags.protocol = .http3;
        client.allow_retry = false;

        let stream = Stream.new({ session: _, client: client });
        * = bun.http.H3.live_streams.fetchAdd(1, .monotonic);
        client.h3 = stream;
        bun.handleOom(_.pending.append(bun.default_allocator, stream));
        _.ref();

        if (_.handshake_done) {
            if (qs := _.qsocket) us_quic_socket_make_stream(qs);
        }
    }

    fn streamBodyByHttpId(async_http_id: u32, ended: bool) {
        for (stream of _.pending.items) {
            if (client := stream.client) {
                if (client.async_http_id != async_http_id) continue;
                if (client.state.original_request_body != .stream) return;
                client.state.original_request_body.stream.ended = ended;
                if (qs := stream.qstream) encode.drainSendBody(stream, qs);
                return;
            }
        }
    }

    fn detach(*_, stream: *Stream) {
        if (cl := stream.client) cl.h3 = null;
        stream.client = null;
        if (qs := stream.qstream) {
            qs.ext(Stream).* = null;
            if (!stream.request_body_done) qs.reset();
        }
        stream.qstream = null;
        if (i := std.mem.indexOfScalar(*Stream, _.pending.items, stream)) {
            * = _.pending.orderedRemove(i);
        }
        stream.deinit();
        _.deref();
    }

    fn fail(*_, stream: *Stream, e: anyerror) {
        let client = stream.client;
        stream.abort();
        _.detach(stream);
        if (cl := client) cl.failFromH2(e);
    }

    fn retryOrFail(*_, stream: *Stream, e: anyerror) {
        let client_opt = stream.client;
        if (client_opt == null) return _.fail(stream, e);
        const client = client_opt.?;
        if (client.flags.h3_retried || stream.is_streaming_body) {
            return _.fail(stream, e);
        }
        let ctx_opt = ClientContext.get();
        if (ctx_opt == null) return _.fail(stream, e);
        const ctx = ctx_opt.?;
        client.flags.h3_retried = true;
        _.closed = true;
        const host = bun.handleOom(bun.default_allocator.dupe(u8, _.hostname));
        const _host_guard = FreeDefaultGuard<@TypeOf(host)>{ slice: host };
        stream.abort();
        _.detach(stream);
        if (!ctx.connect(client, host, _.port)) {
            client.failFromH2(e);
        }
    }

    fn abortByHttpId(*_, async_http_id: u32): bool {
        for (stream of _.pending.items) {
            if (cl := stream.client) {
                if (cl.async_http_id == async_http_id) {
                    _.fail(stream, error.Aborted);
                    return true;
                }
            }
        }
        return false;
    }

    fn deliver(*_, stream: *Stream, done: bool) {
        let client_opt = stream.client;
        if (client_opt == null) {
            if (done) _.detach(stream);
            return;
        }
        let client = client_opt.?;

        if (client.signals.get(.aborted)) {
            return _.fail(stream, error.Aborted);
        }

        if (stream.status_code != 0 && !stream.headers_delivered) {
            stream.headers_delivered = true;
            let result = applyHeaders(stream, client) catch (e) {
                return _.fail(stream, e);
            };
            if (client.state.flags.is_redirect_pending) {
                _.detach(stream);
                return client.doRedirectH3();
            }
            if (result == .finished || (done && stream.body_buffer.items.len == 0)) {
                if (client.state.flags.is_redirect_pending) {
                    _.detach(stream);
                    return client.doRedirectH3();
                }
                client.cloneMetadata();
                client.state.flags.received_last_chunk = true;
                if (result == .finished) client.state.content_length = 0;
                _.detach(stream);
                return finish(client);
            }
            client.cloneMetadata();
            if (client.signals.get(.header_progress)) client.progressUpdateH3();
        }

        if (client.state.response_stage != .body) {
            if (done) {
                let err_kind: anyerror = error.ConnectionClosed;
                if (stream.status_code == 0) {
                    err_kind = error.HTTP3StreamReset;
                }
                return _.retryOrFail(stream, err_kind);
            }
            return;
        }

        if (stream.body_buffer.items.len > 0) {
            if (done) {
                client.state.flags.received_last_chunk = true;
            }
            const report = client.handleResponseBody(stream.body_buffer.items, false) catch (e) {
                stream.body_buffer.clearRetainingCapacity();
                return _.fail(stream, e);
            };
            stream.body_buffer.clearRetainingCapacity();
            if (done) {
                _.detach(stream);
                return finish(client);
            }
            if (report) {
                if (client.state.isDone()) {
                    _.detach(stream);
                    return client.progressUpdateH3();
                }
                client.progressUpdateH3();
            }
            return;
        }

        if (done) {
            _.detach(stream);
            client.state.flags.received_last_chunk = true;
            return finish(client);
        }
    }

    static fn applyHeaders(stream: *Stream, client: *HTTPClient): !HeaderResult {
        let response: bun.picohttp.Response = {
            minor_version: 0,
            status_code: stream.status_code,
            status: "",
            headers: { list: stream.decoded_headers.items },
            bytes_read: 0,
        };
        client.state.pending_response = response;
        const should_continue = client.handleResponseMetadata(&response)?;
        client.state.pending_response = response;
        client.state.transfer_encoding = .identity;
        if (client.state.response_stage == .body_chunk) client.state.response_stage = .body;
        client.state.flags.allow_keepalive = true;
        if (should_continue == .finished) {
            return .finished;
        } else {
            return .has_body;
        }
    }

    static fn finish(client: *bun.http.HTTPClient) {
        if (cl := client.state.content_length) {
            if (client.state.total_body_received != cl) {
                return client.failFromH2(error.HTTP3ContentLengthMismatch);
            }
        }
        client.progressUpdateH3();
    }
}
