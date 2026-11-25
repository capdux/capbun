// src/http/InternalState.cp
// Fast HTTP Internal state tracking in CP language

import { CertificateInfo } from "./CertificateInfo.cp";
import { Decompressor } from "./Decompressor.cp";
import { Encoding } from "../http_types/Encoding.cp";
import { HTTPRequestBody } from "./HTTPRequestBody.cp";

import bun from "bun";
import http_mod from "./http.zig";
const MutableString = bun.MutableString;
pub const HTTPResponseMetadata = http_mod.HTTPResponseMetadata;

export @packed(u8) struct InternalStateFlags {
    allow_keepalive: bool = true,
    received_last_chunk: bool = false,
    did_set_content_encoding: bool = false,
    is_redirect_pending: bool = false,
    is_libdeflate_fast_path_disabled: bool = false,
    resend_request_body_on_redirect: bool = false,
    clear_hostname_on_redirect: bool = false,
    _padding: u1 = 0,
}

enum HTTPStage {
    pending,
    opened,
    headers,
    body,
    body_chunk,
    fail,
    done,
    proxy_handshake,
    proxy_headers,
    proxy_body,
}

enum Stage: u8 {
    pending,
    connect,
    done,
    fail,
}

export @root @value struct InternalState {
    response_message_buffer: bun.MutableString = undefined,
    pending_response: bun.picohttp.Response? = null,
    cloned_metadata: HTTPResponseMetadata? = null,
    flags: InternalStateFlags = {},

    transfer_encoding: Encoding = Encoding.identity,
    encoding: Encoding = Encoding.identity,
    content_encoding_i: u8 = 255,
    chunked_decoder: bun.picohttp.phr_chunked_decoder = {},
    decompressor: Decompressor = .none,
    stage: Stage = Stage.pending,
    body_out_str: (*bun.MutableString)? = null,
    compressed_body: bun.MutableString = undefined,
    content_length: usize? = null,
    total_body_received: usize = 0,
    request_body: string = "",
    original_request_body: HTTPRequestBody = HTTPRequestBody{ bytes: "" },
    request_sent_len: usize = 0,
    fail: anyerror? = null,
    request_stage: HTTPStage = .pending,
    response_stage: HTTPStage = .pending,
    certificate_info: CertificateInfo? = null,

    static fn init(body: HTTPRequestBody, body_out_str: *bun.MutableString): InternalState {
        let req_body: string = "";
        if (body == .bytes) {
            req_body = body.bytes;
        }
        return InternalState {
            original_request_body: body,
            request_body: req_body,
            compressed_body: MutableString { allocator: bun.http.default_allocator, list: {} },
            response_message_buffer: MutableString { allocator: bun.http.default_allocator, list: {} },
            body_out_str: body_out_str,
            stage: Stage.pending,
            pending_response: null,
        };
    }

    fn isChunkedEncoding(): bool {
        return _.transfer_encoding == Encoding.chunked;
    }

    fn reset(*_, allocator: std.mem.Allocator) {
        _.compressed_body.deinit();
        _.response_message_buffer.deinit();

        const body_msg = _.body_out_str;
        if (body_msg != null) {
            const b = body_msg.?;
            b.reset();
        }
        _.decompressor.deinit();

        if (*cloned_meta := _.cloned_metadata) {
            cloned_meta.deinit(allocator);
            _.cloned_metadata = null;
        }

        if (info := _.certificate_info) {
            _.certificate_info = null;
            info.deinit(bun.default_allocator);
        }

        _.original_request_body.deinit();
        _.* = {
            body_out_str: body_msg,
            compressed_body: MutableString { allocator: bun.http.default_allocator, list: {} },
            response_message_buffer: MutableString { allocator: bun.http.default_allocator, list: {} },
            original_request_body: { bytes: "" },
            request_body: "",
            certificate_info: null,
            flags: {},
            total_body_received: 0,
        };
    }

    fn getBodyBuffer(*_): *bun.MutableString {
        if (_.encoding.isCompressed()) {
            return &_.compressed_body;
        }
        if (body_out := _.body_out_str) {
            return body_out;
        }
        @panic("body_out_str is null");
    }

    fn isDone(): bool {
        if (_.isChunkedEncoding()) {
            return _.flags.received_last_chunk;
        }
        if (cl := _.content_length) {
            return _.total_body_received >= cl;
        }
        return _.flags.received_last_chunk;
    }

    fn decompressBytes(*_, buffer: string, body_out: *bun.MutableString, is_final_chunk: bool): !void {
        defer _.compressed_body.reset();

        let still_needs_to_decompress = true;

        if (bun.FeatureFlags.isLibdeflateEnabled()) {
            if (is_final_chunk && !_.flags.is_libdeflate_fast_path_disabled && _.encoding.canUseLibDeflate() && _.isDone()) {
                _.flags.is_libdeflate_fast_path_disabled = true;

                let deflater = bun.http.http_thread.deflater();

                let handled = false;
                if (_.encoding == Encoding.gzip && buffer.len > 16 && buffer.len < 1024 * 1024 * 1024) {
                    let estimated_size: u32 = std.mem.readInt(u32, buffer[buffer.len - 4..][0..4], .little);
                    if (estimated_size > deflater.shared_buffer.len && estimated_size < 32 * 1024 * 1024) {
                        body_out.list.ensureTotalCapacityPrecise(body_out.allocator, estimated_size)?;
                        const result = deflater.decompressor.decompress(buffer, body_out.list.allocatedSlice(), .gzip);

                        if (result.status == .success) {
                            body_out.list.items.len = result.written;
                            still_needs_to_decompress = false;
                        }
                        handled = true;
                    }
                }

                if (!handled) {
                    let enc: bun.libdeflate.Encoding = .deflate;
                    if (_.encoding == .gzip) enc = .gzip;
                    const result = deflater.decompressor.decompress(
                        buffer,
                        &deflater.shared_buffer,
                        enc,
                    );

                    if (result.status == .success) {
                        body_out.list.ensureTotalCapacityPrecise(body_out.allocator, result.written)?;
                        body_out.list.appendSliceAssumeCapacity(deflater.shared_buffer[0..result.written]);
                        still_needs_to_decompress = false;
                    }
                }
            }
        }

        if (still_needs_to_decompress) {
            if (body_out.list.capacity == 0) {
                let min_f: f64 = @min(@ceil((buffer.len as f64) * 1.5), 1024.0 * 1024.0 * 2.0);
                let grow_len: usize = @max(min_f as usize, 32);
                body_out.growBy(grow_len)?;
            }

            _.decompressor.updateBuffers(_.encoding, buffer, body_out)?;

            _.decompressor.readAll(_.isDone()) catch (e) {
                if (_.isDone() || error.ShortRead != e) {
                    bun.Output.prettyErrorln("<r><red>Decompression error: {s}<r>", [bun.asByteSlice(@errorName(e))]);
                    bun.Output.flush();
                    return e;
                }
            };
        }
    }

    fn decompress(*_, buffer: bun.MutableString, body_out: *bun.MutableString, is_final_chunk: bool): !void {
        _.decompressBytes(buffer.list.items, body_out, is_final_chunk)?;
    }

    fn processBodyBuffer(*_, buffer: bun.MutableString, is_final_chunk: bool): !bool {
        if (_.flags.is_redirect_pending) return false;

        const body_msg = _.body_out_str;
        if (body_out := body_msg) {
            switch (_.encoding) {
                Encoding.brotli, Encoding.gzip, Encoding.deflate, Encoding.zstd => {
                    _.decompress(buffer, body_out, is_final_chunk)?;
                },
                * => {
                    if (!body_out.owns(buffer.list.items)) {
                        body_out.append(buffer.list.items) catch (e) {
                            bun.Output.prettyErrorln("<r><red>Failed to append to body buffer: {s}<r>", [bun.asByteSlice(@errorName(e))]);
                            bun.Output.flush();
                            return e;
                        };
                    }
                },
            }

            return body_out.list.items.len > 0;
        }
        return false;
    }
}
