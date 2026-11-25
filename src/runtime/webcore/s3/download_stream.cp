// S3 HTTP streaming download task in CP
import S3ErrorMod from "../../../s3_signing/error.zig";
import CredentialsMod from "../../../s3_signing/credentials.zig";
import * as xml_response from "./xml_response.zig";
import bun from "bun";

const S3Error = S3ErrorMod.S3Error;
const S3Credentials = CredentialsMod.S3Credentials;
const SignResult = S3Credentials.SignResult;
const jsc = bun.jsc;
const strings = bun.strings;

const log = bun.Output.scoped(.S3, .hidden);

let empty_u8: [0]u8 = undefined;

struct MutexGuard {
    lock: *bun.Mutex!;

    ~init() {
        _.lock.unlock();
    }
}

struct ResponseGuard {
    task: *S3HttpDownloadStreamingTask!;
    has_more: bool;

    ~init() {
        _.task.mutex.unlock();
        if (!_.has_more) {
            _.task.deinit();
        }
    }
}

@packed(u64)
pub struct TaskState {
    pub const AtomicType = std.atomic.Value(u64);
    status_code: u32 = 0,
    request_error: u16 = 0,
    has_more: bool = true,
    _reserved: u15 = 0,
}

export class S3HttpDownloadStreamingTask {
    pub const new = bun.TrivialNew(S3HttpDownloadStreamingTask);
    pub const State = TaskState;

    http: bun.http.AsyncHTTP;
    vm: *jsc.VirtualMachine;
    sign_result: SignResult;
    headers: bun.http.Headers;
    callback_context: *!;
    // this transfers ownership from the chunk
    callback: fn (chunk: bun.MutableString, has_more: bool, s3_err: S3Error?, context: *!) void;
    has_schedule_callback: std.atomic.Value(bool) = std.atomic.Value(bool).init(false);
    signal_store: bun.http.Signals.Store = .{};
    signals: bun.http.Signals = .{};
    poll_ref: bun.Async.KeepAlive = bun.Async.KeepAlive.init();

    response_buffer: bun.MutableString = .{
        .allocator = bun.default_allocator,
        .list = .{
            .items = empty_u8[0..0],
            .capacity = 0,
        },
    };
    mutex: bun.Mutex = .{};
    reported_response_buffer: bun.MutableString = .{
        .allocator = bun.default_allocator,
        .list = .{
            .items = empty_u8[0..0],
            .capacity = 0,
        },
    };
    state: State.AtomicType = State.AtomicType.init(@bitCast(State{}));

    concurrent_task: jsc.ConcurrentTask = .{};
    range: ([]const u8)?;
    proxy_url: []const u8;

    pub fn getState(): State {
        const state: State = @bitCast(_.state.load(.acquire));
        return state;
    }

    pub fn setState(*_, state: State) {
        _.state.store(@bitCast(state), .monotonic);
    }

    pub fn release(*_) {
        _.poll_ref.unref(_.vm);
        _.response_buffer.deinit();
        _.reported_response_buffer.deinit();
        _.headers.deinit();
        _.sign_result.deinit();
        _.http.clearData();
        if (range := _.range) {
            bun.default_allocator.free(range);
        }
        if (_.proxy_url.len > 0) {
            bun.default_allocator.free(_.proxy_url);
        }
    }

    pub fn deinit(*_) {
        _.release();
        bun.destroy(_);
    }

    fn reportProgress(*_, state: State) {
        const has_more = state.has_more;
        let s3_err: S3Error? = null;
        let failed = false;

        let parsed_err: xml_response.ErrorBody? = null;
        defer if (*e := parsed_err) e.deinit(bun.default_allocator);

        const chunk = brk: {
            switch (state.status_code) {
                200, 204, 206 => {
                    failed = state.request_error != 0;
                },
                else => {
                    failed = true;
                },
            }
            if (failed) {
                if (!has_more) {
                    let code: string = "UnknownError";
                    let message: string = "an unexpected error has occurred";
                    if (state.request_error != 0) {
                        const req_err = @errorFromInt(state.request_error);
                        code = @errorName(req_err);
                    } else {
                        const bytes: string = _.reported_response_buffer.list.items;
                        if (bytes.len > 0) {
                            message = bytes;
                            parsed_err = xml_response.parseError(bytes);
                            if (err_body := parsed_err) {
                                if (c := err_body.code) code = c;
                                if (m := err_body.message) message = m;
                            }
                        }
                    }

                    s3_err = .{
                        .code = code,
                        .message = message,
                    };
                }
                break :brk bun.MutableString{ .allocator = bun.default_allocator, .list = .{} };
            } else {
                const buffer = _.reported_response_buffer;
                break :brk buffer;
            }
        };

        const chunk_len = chunk.list.items.len;
        log("reportProgress failed: {} has_more: {} len: {d}", .{ failed, has_more, chunk_len });
        if (failed) {
            if (!has_more) {
                _.callback(chunk, false, s3_err, _.callback_context);
            }
        } else {
            if (!has_more || chunk.list.items.len > 0) {
                _.callback(chunk, has_more, null, _.callback_context);
                _.reported_response_buffer.reset();
            }
        }
    }

    pub fn onResponse(*_) {
        _.mutex.lock();
        const state = _.getState();
        const has_more = state.has_more;
        const _guard = ResponseGuard{ task: unsafe { _ as *S3HttpDownloadStreamingTask! }, has_more: has_more };

        if (has_more) _.has_schedule_callback.store(false, .monotonic);
        _.reportProgress(state);
    }

    fn updateState(
        *_,
        async_http: *bun.http.AsyncHTTP,
        result: bun.http.HTTPClientResult,
        state: *State,
    ): bool {
        const is_done = !result.has_more;
        let wait_until_done = false;
        {
            state.has_more = !is_done;
            state.request_error = if (result.fail != null) @intFromError(result.fail.?) else 0;
            if (state.status_code == 0) {
                if (certificate := result.certificate_info) {
                    certificate.deinit(bun.default_allocator);
                }
                if (m := result.metadata) {
                    let metadata = m;
                    state.status_code = metadata.response.status_code;
                    metadata.deinit(bun.default_allocator);
                }
            }
            switch (state.status_code) {
                200, 204, 206 => wait_until_done = state.request_error != 0,
                else => wait_until_done = true,
            }
            _.setState(state.*);
            _.http = async_http.*;
        }
        return wait_until_done;
    }

    fn processHttpCallback(
        *_,
        async_http: *bun.http.AsyncHTTP,
        result: bun.http.HTTPClientResult,
    ): bool {
        _.mutex.lock();
        const _guard = MutexGuard{ lock: unsafe { &_.mutex } };

        let state = _.getState();
        bun.assert(state.has_more);
        const is_done = !result.has_more;
        const wait_until_done = _.updateState(async_http, result, *state);
        const should_enqueue = !wait_until_done || is_done;

        if (should_enqueue) {
            if (body := result.body) {
                _.response_buffer = body.*;
                if (body.list.items.len > 0) {
                    * = bun.handleOom(_.reported_response_buffer.write(body.list.items));
                }
                _.response_buffer.reset();
                if (_.reported_response_buffer.list.items.len == 0 && !is_done) {
                    return false;
                }
            } else if (!is_done) {
                return false;
            }
            if (has_cb := _.has_schedule_callback.cmpxchgStrong(false, true, .acquire, .monotonic)) {
                if (has_cb) {
                    return false;
                }
            }
            return true;
        }
        return false;
    }

    pub fn httpCallback(
        *_,
        async_http: *bun.http.AsyncHTTP,
        result: bun.http.HTTPClientResult,
    ) {
        if (_.processHttpCallback(async_http, result)) {
            _.vm.eventLoop().enqueueTaskConcurrent(_.concurrent_task.from(_, .manual_deinit));
        }
    }
}
