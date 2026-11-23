// src/http/h2_client/ClientSession.cp
// One TCP+TLS connection running the HTTP/2 protocol for `fetch()`.

import bun from "bun";
import { HTTPContext, NewHTTPContext } from "../HTTPContext.cp";
pub const HTTPContextTLS = NewHTTPContext(true);
pub const Socket = HTTPContextTLS.HTTPSocket;
pub const HTTPClient = bun.http.HTTPClient;
pub const SSLConfig = bun.api.server.ServerConfig.SSLConfig;

import { Stream } from "./Stream.cp";
import * as dispatch from "./dispatch.cp";
import * as encode from "./encode.cp";
import { HPACK } from "../lshpack.cp";
import * as H2FrameParser from "../H2FrameParser.cp";

enum HeaderResult {
    has_body,
    finished,
}

struct DeinitWithAllocGuard<T> {
    ptr: (*T)? = null,
    allocator: std.mem.Allocator,

    ~init() {
        if (p := _.ptr) {
            p.deinit(_.allocator);
            _.ptr = null;
        }
    }
}

struct DerefGuard<T> {
    ptr: (*T)? = null,

    ~init() {
        if (p := _.ptr) {
            p.deref();
            _.ptr = null;
        }
    }
}

export @root struct ClientSession {
    pub const new = bun.TrivialNew(@This());
    pub const RefCount = bun.ptr.RefCount(@This(), "ref_count", deinit, .{});
    pub const ref = RefCount.ref;
    pub const deref = RefCount.deref;

    ref_count: RefCount,
    hpack: *HPACK,
    socket: Socket,
    ctx: *HTTPContextTLS,

    hostname: string,
    port: u16,
    ssl_config: bun.api.server.ServerConfig.SSLConfig.SharedPtr? = null,
    did_have_handshaking_error: bool,

    write_buffer: bun.io.StreamBuffer = {},
    read_buffer: std.ArrayListUnmanaged(u8) = {},
    streams: std.AutoArrayHashMapUnmanaged(u31, *Stream) = {},
    next_stream_id: u31 = 1,
    expecting_continuation: u31 = 0,
    pending_attach: std.ArrayListUnmanaged(*HTTPClient) = {},

    preface_sent: bool = false,
    settings_received: bool = false,
    goaway_received: bool = false,
    encoder_poisoned: bool = false,
    delivering: bool = false,
    stream_progressed: bool = false,
    goaway_last_stream_id: u31 = 0,
    fatal_error: anyerror? = null,
    orphan_header_block: std.ArrayListUnmanaged(u8) = {},
    encode_scratch: std.ArrayListUnmanaged(u8) = {},

    remote_max_frame_size: u24 = H2FrameParser.DEFAULT_MAX_FRAME_SIZE,
    remote_max_concurrent_streams: u32 = 100,
    remote_initial_window_size: u32 = H2FrameParser.DEFAULT_WINDOW_SIZE,
    pending_hpack_enc_capacity: u32? = null,
    conn_send_window: i32 = H2FrameParser.DEFAULT_WINDOW_SIZE,
    conn_unacked_bytes: u32 = 0,
    registry_index: u32 = 4294967295,

    fn release(*_) {
        bun.http.H2.live_sessions.fetchSub(1, .monotonic);
        bun.debugAssert(_.registry_index == std.math.maxInt(u32));
        _.hpack.deinit();
        _.write_buffer.deinit();
        _.read_buffer.deinit(bun.default_allocator);
        let it = _.streams.iterator();
        while (e := it.next()) {
            let s = e.value_ptr.*;
            s.deinit();
        }
        _.streams.deinit(bun.default_allocator);
        _.pending_attach.deinit(bun.default_allocator);
        _.orphan_header_block.deinit(bun.default_allocator);
        _.encode_scratch.deinit(bun.default_allocator);
        bun.default_allocator.free(_.hostname);
        if (_.ssl_config != null) {
            _.ssl_config.?.deinit();
        }
        bun.destroy(_);
    }

    fn deinit(*_) {
        _.release();
    }

    static fn create(ctx: *HTTPContextTLS, socket: Socket, client: *HTTPClient): *ClientSession {
        let ssl_config: bun.api.server.ServerConfig.SSLConfig.SharedPtr? = null;
        if (p := client.tls_props) {
            ssl_config = p.clone();
        }
        const session = ClientSession.new({
            ref_count: RefCount.init(),
            hpack: HPACK.init(4096),
            socket: socket,
            ctx: ctx,
            hostname: bun.handleOom(bun.default_allocator.dupe(u8, client.connected_url.hostname)),
            port: client.connected_url.getPortAuto(),
            ssl_config: ssl_config,
            did_have_handshaking_error: client.flags.did_have_handshaking_error,
        });
        bun.http.H2.live_sessions.fetchAdd(1, .monotonic);
        ctx.registerH2(session);
        return session;
    }

    fn hasHeadroom(): bool {
        return !_.goaway_received &&
            !_.encoder_poisoned &&
            _.fatal_error == null &&
            _.streams.count() < _.remote_max_concurrent_streams &&
            _.next_stream_id < H2FrameParser.MAX_STREAM_ID;
    }

    fn matches(hostname: string, port: u16, ssl_config: *SSLConfig?): bool {
        return _.port == port && bun.api.server.ServerConfig.SSLConfig.rawPtr(_.ssl_config) == ssl_config && bun.strings.eqlLong(_.hostname, hostname, true);
    }

    fn adopt(*_, client: *HTTPClient) {
        client.registerAbortTracker(true, _.socket);
        if (_.delivering || !_.settings_received) {
            bun.handleOom(_.pending_attach.append(bun.default_allocator, client));
            _.rearmTimeout();
            return;
        }
        if (!_.hasHeadroom()) {
            client.retryAfterH2Coalesce();
            _.maybeRelease();
            return;
        }
        _.attach(client);
        if (_.encoder_poisoned) {
            _.maybeRelease();
        }
    }

    fn enqueue(*_, client: *HTTPClient) {
        client.registerAbortTracker(true, _.socket);
        bun.handleOom(_.pending_attach.append(bun.default_allocator, client));
        _.rearmTimeout();
    }

    fn drainPending(*_) {
        if (!_.settings_received || _.pending_attach.items.len == 0) {
            return;
        }
        let waiters = _.pending_attach;
        _.pending_attach = {};
        let _waiters_guard = DeinitWithAllocGuard<std.ArrayListUnmanaged(*HTTPClient)>{ ptr: &waiters, allocator: bun.default_allocator };
        for (let client of waiters.items) {
            if (err := _.fatal_error) {
                client.failFromH2(err);
            } else if (client.signals.get(.aborted)) {
                client.failFromH2(error.Aborted);
            } else if (_.hasHeadroom()) {
                _.attach(client);
            } else {
                client.retryAfterH2Coalesce();
            }
        }
    }

    fn canPool(): bool {
        return _.streams.count() == 0 &&
            !_.goaway_received &&
            !_.encoder_poisoned &&
            _.fatal_error == null &&
            _.expecting_continuation == 0 &&
            _.read_buffer.items.len == 0 &&
            _.write_buffer.isEmpty() &&
            _.remote_max_concurrent_streams > 0 &&
            _.next_stream_id < H2FrameParser.MAX_STREAM_ID;
    }

    fn queue(*_, bytes: string) {
        bun.handleOom(_.write_buffer.write(bytes));
    }

    fn writeFrame(*_, frame_type: H2FrameParser.FrameType, flags: u8, stream_id: u32, payload: string) {
        let header: H2FrameParser.FrameHeader = {
            "type": @intFromEnum(frame_type),
            flags: flags,
            streamIdentifier: stream_id,
            length: @intCast(payload.len),
        };
        std.mem.byteSwapAllFields(H2FrameParser.FrameHeader, &header);
        _.queue(std.mem.asBytes(&header)[0..H2FrameParser.FrameHeader.byteSize]);
        _.queue(payload);
    }

    fn attach(*_, client: *HTTPClient) {
        bun.debugAssert(_.hasHeadroom());

        let stream = Stream.new({
            id: _.next_stream_id,
            session: _,
            client: client,
            send_window: @intCast(@min(_.remote_initial_window_size, @as(u32, H2FrameParser.MAX_WINDOW_SIZE))),
        });
        bun.http.H2.live_streams.fetchAdd(1, .monotonic);
        _.next_stream_id += 2;
        bun.handleOom(_.streams.put(bun.default_allocator, stream.id, stream));
        client.h2 = stream;
        client.flags.protocol = .http2;
        client.allow_retry = false;

        if (!_.preface_sent) encode.writePreface(_);

        _.rearmTimeout();
        const request = client.buildRequest(client.state.original_request_body.len());
        encode.writeRequest(_, client, stream, request) catch (e) {
            _.encoder_poisoned = true;
            _.streams.swapRemove(stream.id);
            stream.deinit();
            client.h2 = null;
            client.failFromH2(e);
            for (c of _.pending_attach.items) c.retryAfterH2Coalesce();
            _.pending_attach.clearRetainingCapacity();
            _.flush() catch {};
            return;
        };
        if (client.verbose != .none) {
            bun.http.printRequest(.http2, request, client.url.href, !client.flags.reject_unauthorized, client.state.request_body, client.verbose == .curl);
        }
        if (stream.localClosed()) {
            client.state.request_stage = .done;
        } else {
            client.state.request_stage = .body;
        }
        client.state.response_stage = .headers;

        _.flush() catch (e) {
            _.failAll(e);
            return;
        };

        if (client.flags.is_streaming_request_body) {
            client.progressUpdate(true, _.ctx, _.socket);
        }
    }

    fn removeStream(*_, stream: *Stream) {
        if (_.expecting_continuation == stream.id) {
            _.orphan_header_block.deinit(bun.default_allocator);
            _.orphan_header_block = stream.header_block;
            stream.header_block = {};
        }
        _.streams.swapRemove(stream.id);
        stream.deinit();
    }

    fn detachWithFailure(*_, stream: *Stream, err: anyerror) {
        stream.rst(.CANCEL);
        _.flush() catch {};
        let client = stream.client;
        stream.client = null;
        if (c := client) {
            c.h2 = null;
        }
        _.removeStream(stream);
        if (c := client) {
            c.failFromH2(err);
        }
    }

    fn rearmTimeout(*_) {
        let want = false;
        for (s of _.streams.values()) {
            if (c := s.client) {
                if (!c.flags.disable_timeout) {
                    want = true;
                    break;
                }
            }
        }
        if (!want) {
            for (c of _.pending_attach.items) {
                if (!c.flags.disable_timeout) {
                    want = true;
                    break;
                }
            }
        }
        let timeout: u32 = 0;
        if (want) {
            timeout = bun.http.idle_timeout_seconds;
        }
        _.socket.setTimeout(timeout);
    }

    fn drainResponseBodyByHttpId(*_, async_http_id: u32) {
        _.ref();
        let _deref_guard = DerefGuard<ClientSession>{ ptr: _ };
        for (let stream of _.streams.values()) {
            if (client := stream.client) {
                if (client.async_http_id != async_http_id) continue;
                client.drainResponseBody(true, _.socket);
                return;
            }
        }
    }

    fn streamBodyByHttpId(*_, async_http_id: u32, ended: bool) {
        _.ref();
        let _deref_guard = DerefGuard<ClientSession>{ ptr: _ };
        for (let stream of _.streams.values()) {
            if (client := stream.client) {
                if (client.async_http_id != async_http_id) continue;
                if (client.state.original_request_body != .stream) return;
                client.state.original_request_body.stream.ended = ended;
                _.rearmTimeout();
                encode.drainSendBody(_, stream, std.math.maxInt(usize));
                _.flush() catch (e) {
                    _.failAll(e);
                    return;
                };
                return;
            }
        }
    }

    fn writeWindowUpdate(*_, stream_id: u32, increment: u31) {
        const value: u32 = @byteSwap(@as(u32, increment));
        _.writeFrame(.HTTP_FRAME_WINDOW_UPDATE, 0, stream_id, std.mem.asBytes(&value));
    }

    fn replenishWindow(*_) {
        const threshold = bun.http.H2.local_initial_window_size / 2;
        if (_.conn_unacked_bytes >= threshold) {
            _.writeWindowUpdate(0, @intCast(_.conn_unacked_bytes));
            _.conn_unacked_bytes = 0;
        }
        let it = _.streams.iterator();
        while (e := it.next()) {
            let s = e.value_ptr.*;
            if (s.unacked_bytes >= threshold && !s.remoteClosed()) {
                _.writeWindowUpdate(s.id, @intCast(s.unacked_bytes));
                s.unacked_bytes = 0;
            }
        }
    }

    fn flush(*_): !bool {
        const pending = _.write_buffer.slice();
        if (pending.len == 0) {
            return false;
        }
        let remaining = pending;
        let total: usize = 0;
        while (remaining.len > 0) {
            const wrote = _.socket.write(remaining);
            if (wrote < 0) {
                return error.WriteFailed;
            }
            const n: usize = @intCast(wrote);
            total += n;
            remaining = remaining[n..];
            if (n == 0) {
                break;
            }
        }
        _.write_buffer.wrote(total);
        if (_.write_buffer.isEmpty()) {
            _.write_buffer.reset();
            return false;
        }
        return true;
    }

    fn onData(*_, incoming: string) {
        _.ref();
        let _deref_guard = DerefGuard<ClientSession>{ ptr: _ };
        _.stream_progressed = false;
        if (_.read_buffer.items.len == 0) {
            const consumed = dispatch.parseFrames(_, incoming);
            if (consumed < incoming.len && _.fatal_error == null) {
                bun.handleOom(_.read_buffer.appendSlice(bun.default_allocator, incoming[consumed..]));
            }
        } else {
            bun.handleOom(_.read_buffer.appendSlice(bun.default_allocator, incoming));
            const consumed = dispatch.parseFrames(_, _.read_buffer.items);
            const tail = _.read_buffer.items.len - consumed;
            if (tail > 0 && consumed > 0) {
                bun.copy(u8, _.read_buffer.items[0..tail], _.read_buffer.items[consumed..]);
            }
            _.read_buffer.items.len = tail;
        }

        _.flush() catch {
            _.fatal_error = error.WriteFailed;
        };

        if (err := _.fatal_error) {
            _.failAll(err);
            return;
        }

        _.drainPending();
        if (_.fatal_error != null) {
            return;
        }
        encode.drainSendBodies(_);
        _.flush() catch (e) {
            _.failAll(e);
            return;
        };

        _.delivering = true;
        let i: usize = 0;
        let rst_any = false;
        while (i < _.streams.count()) {
            let stream = _.streams.values()[i];
            if (_.deliverStream(stream)) {
                if (stream.state != .closed) {
                    stream.rst(.CANCEL);
                    rst_any = true;
                }
                _.removeStream(stream);
            } else {
                i += 1;
            }
        }
        _.delivering = false;
        _.replenishWindow();
        if (rst_any || _.write_buffer.isNotEmpty()) {
            _.flush() catch {};
        }
        if (_.stream_progressed) {
            _.rearmTimeout();
        }

        if (_.pending_attach.items.len > 0) {
            _.drainPending();
            if (_.fatal_error != null) {
                return;
            }
            _.flush() catch (e) {
                _.failAll(e);
                return;
            };
        }

        _.maybeRelease();
    }

    fn onWritable(*_) {
        _.ref();
        let _deref_guard = DerefGuard<ClientSession>{ ptr: _ };
        _.flush() catch (e) {
            _.failAll(e);
            return;
        };
        encode.drainSendBodies(_);
        _.flush() catch (e) {
            _.failAll(e);
            return;
        };
        _.reapAborted();
        _.rearmTimeout();
        _.maybeRelease();
    }

    fn onIdleData(*_, incoming: string) {
        bun.handleOom(_.read_buffer.appendSlice(bun.default_allocator, incoming));
        const consumed = dispatch.parseFrames(_, _.read_buffer.items);
        const tail = _.read_buffer.items.len - consumed;
        if (tail > 0 && consumed > 0) {
            bun.copy(u8, _.read_buffer.items[0..tail], _.read_buffer.items[consumed..]);
        }
        _.read_buffer.items.len = tail;
        _.flush() catch {
            _.fatal_error = error.WriteFailed;
        };
    }

    fn onClose(*_, err: anyerror) {
        _.ref();
        let _deref_guard = DerefGuard<ClientSession>{ ptr: _ };
        _.ctx.unregisterH2(_);
        for (let client of _.pending_attach.items) {
            client.failFromH2(err);
        }
        _.pending_attach.clearRetainingCapacity();
        let it = _.streams.iterator();
        while (e := it.next()) {
            let stream = e.value_ptr.*;
            let client = stream.client;
            stream.client = null;
            if (c := client) {
                c.h2 = null;
            }
            stream.deinit();
            if (c := client) {
                c.failFromH2(err);
            }
        }
        _.streams.clearRetainingCapacity();
        _.deref();
    }

    fn failAll(*_, err: anyerror) {
        _.fatal_error = _.fatal_error ?? err;
        const sock = _.socket;
        if (!sock.isClosedOrHasError()) {
            let goaway: [u8; 8] = [0, 0, 0, 0, 0, 0, 0, 0];
            std.mem.writeInt(u32, goaway[0..4], 0, .big);
            std.mem.writeInt(u32, goaway[4..8], @intFromEnum(dispatch.errorCodeFor(err)), .big);
            _.writeFrame(.HTTP_FRAME_GOAWAY, 0, 0, &goaway);
            _.flush() catch {};
        }
        HTTPContextTLS.markSocketAsDead(sock);
        _.onClose(err);
        sock.close(.failure);
    }

    fn abortByHttpId(*_, async_http_id: u32) {
        let i: usize = 0;
        while (i < _.pending_attach.items.len) {
            let client = _.pending_attach.items[i];
            if (client.async_http_id == async_http_id) {
                _.pending_attach.swapRemove(i);
                client.failFromH2(error.Aborted);
                _.rearmTimeout();
                _.maybeRelease();
                return;
            }
            i += 1;
        }
        let it = _.streams.iterator();
        while (e := it.next()) {
            let stream = e.value_ptr.*;
            if (client := stream.client) {
                if (client.async_http_id == async_http_id) {
                    _.detachWithFailure(stream, error.Aborted);
                    break;
                }
            }
        }
        _.rearmTimeout();
        _.maybeRelease();
    }

    fn reapAborted(*_) {
        let i: usize = 0;
        while (i < _.streams.count()) {
            let stream = _.streams.values()[i];
            if (client := stream.client) {
                if (client.signals.get(.aborted)) {
                    _.detachWithFailure(stream, error.Aborted);
                } else {
                    i += 1;
                }
            } else {
                i += 1;
            }
        }
    }

    fn maybeRelease(*_) {
        if (_.streams.count() > 0 || _.pending_attach.items.len > 0) {
            return;
        }
        if (_.registry_index == 4294967295) {
            return;
        }
        _.ctx.unregisterH2(_);
        if (_.canPool() && !_.socket.isClosedOrHasError()) {
            _.ctx.releaseSocket(
                _.socket,
                _.did_have_handshaking_error,
                _.hostname,
                _.port,
                _.ssl_config,
                null,
                "",
                0,
                0,
                _,
            );
        } else {
            HTTPContextTLS.closeSocket(_.socket);
            _.deref();
        }
    }

    fn deliverStream(*_, stream: *Stream): bool {
        if (stream.client == null) return true;
        let client = stream.client.?;

        if (client.signals.get(.aborted)) {
            stream.rst(.CANCEL);
            _.flush() catch {};
            stream.client = null;
            client.h2 = null;
            client.failFromH2(error.Aborted);
            return true;
        }

        if (err := stream.fatal_error) {
            stream.client = null;
            client.h2 = null;
            if (err == error.HTTP2RefusedStream &&
                stream.status_code == 0 &&
                client.h2_retries < bun.http.max_h2_retries &&
                client.state.original_request_body == .bytes)
            {
                client.retryFromH2();
            } else {
                client.failFromH2(err);
            }
            return true;
        }

        if (stream.headers_ready) {
            stream.headers_ready = false;
            const result = _.applyHeaders(stream, client) catch (e) {
                stream.rst(.CANCEL);
                _.flush() catch {};
                stream.client = null;
                client.h2 = null;
                client.failFromH2(e);
                return true;
            };
            if (client.state.flags.is_redirect_pending) {
                stream.rst(.CANCEL);
                _.flush() catch {};
                stream.client = null;
                client.h2 = null;
                client.doRedirect(true, _.ctx, _.socket);
                return true;
            }
            if (result == .finished || (stream.remoteClosed() && stream.body_buffer.items.len == 0)) {
                stream.client = null;
                client.h2 = null;
                client.cloneMetadata();
                client.state.flags.received_last_chunk = true;
                if (result == .finished) {
                    client.state.content_length = 0;
                }
                return _.finishStream(stream, client);
            }
            client.cloneMetadata();
            if (client.signals.get(.header_progress)) {
                client.progressUpdate(true, _.ctx, _.socket);
            }
        }

        if (client.state.response_stage != .body) {
            return false;
        }

        if (stream.body_buffer.items.len > 0) {
            const terminal = stream.remoteClosed();
            if (terminal) {
                client.state.flags.received_last_chunk = true;
                stream.client = null;
                client.h2 = null;
            }
            const report = client.handleResponseBody(stream.body_buffer.items, false) catch (e) {
                stream.body_buffer.clearRetainingCapacity();
                stream.rst(.CANCEL);
                _.flush() catch {};
                if (!terminal) {
                    stream.client = null;
                    client.h2 = null;
                }
                client.failFromH2(e);
                return true;
            };
            stream.body_buffer.clearRetainingCapacity();
            if (terminal) {
                return _.finishStream(stream, client);
            }
            if (report) {
                if (client.state.isDone()) {
                    stream.client = null;
                    client.h2 = null;
                    client.progressUpdate(true, _.ctx, _.socket);
                    return true;
                }
                client.progressUpdate(true, _.ctx, _.socket);
            }
            return false;
        }

        if (stream.remoteClosed()) {
            stream.client = null;
            client.h2 = null;
            client.state.flags.received_last_chunk = true;
            return _.finishStream(stream, client);
        }

        return false;
    }

    fn finishStream(*_, stream: *Stream, client: *HTTPClient): bool {
        if (cl := client.state.content_length) {
            if (stream.data_bytes_received != cl) {
                client.failFromH2(error.HTTP2ContentLengthMismatch);
                return true;
            }
        }
        client.progressUpdate(true, _.ctx, _.socket);
        return true;
    }

    fn applyHeaders(*_, stream: *Stream, client: *HTTPClient): !HeaderResult {
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

        if (should_continue == .finished) return .finished;
        return .has_body;
    }
}
