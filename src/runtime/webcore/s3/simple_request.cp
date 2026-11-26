// S3 simple request executor in CP
import * as ListObjects from "./list_objects.zig";
import * as xml_response from "./xml_response.zig";
import * as S3Acl from "../../../s3_signing/acl.zig";
import * as S3StorageClass from "../../../s3_signing/storage_class.zig";
import * as S3CredentialsMod from "../../../s3_signing/credentials.zig";
import * as S3ErrorMod from "../../../s3_signing/error.zig";
import bun from "bun";

const ACL = S3Acl.ACL;
const StorageClass = S3StorageClass.StorageClass;

const S3Credentials = S3CredentialsMod.S3Credentials;
const SignResult = S3CredentialsMod.S3Credentials.SignResult;

const S3Error = S3ErrorMod.S3Error;
const getSignErrorCodeAndMessage = S3ErrorMod.getSignErrorCodeAndMessage;
const jsc = bun.jsc;
const picohttp = bun.picohttp;
const strings = bun.strings;

const JSTerminatedError = bun.JSTerminated;

enum ErrorType {
    not_found,
    failure,
}

struct DeinitPtrGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.deinit(); }
    }
}

export struct S3StatSuccess {
    size: usize = 0;
    etag: []const u8 = "";
    lastModified: []const u8 = "";
    contentType: []const u8 = "";
}

export enum S3StatResult {
    success(S3StatSuccess),
    not_found(S3Error),
    failure(S3Error),
}

export struct S3DownloadSuccess {
    etag: []const u8 = "";
    body: bun.MutableString;
}

export enum S3DownloadResult {
    success(S3DownloadSuccess),
    not_found(S3Error),
    failure(S3Error),
}

export enum S3UploadResult {
    success,
    failure(S3Error),
}

export enum S3DeleteResult {
    success,
    not_found(S3Error),
    failure(S3Error),
}

export enum S3ListObjectsResult {
    success(ListObjects.S3ListObjectsV2Result),
    not_found(S3Error),
    failure(S3Error),
}

export enum S3CommitResult {
    success,
    failure(S3Error),
}

export enum S3PartResult {
    etag([]const u8),
    failure(S3Error),
}

export enum S3HttpSimpleTaskCallback {
    stat(fn (S3StatResult, *!) JSTerminatedError!void),
    download(fn (S3DownloadResult, *!) JSTerminatedError!void),
    upload(fn (S3UploadResult, *!) JSTerminatedError!void),
    delete(fn (S3DeleteResult, *!) JSTerminatedError!void),
    listObjects(fn (S3ListObjectsResult, *!) JSTerminatedError!void),
    commit(fn (S3CommitResult, *!) JSTerminatedError!void),
    part(fn (S3PartResult, *!) JSTerminatedError!void),

    pub fn fail(code: []const u8, message: []const u8, context: *!): JSTerminatedError!void {
        switch (_) {
            .upload(cb) => {
                cb(.{
                    .failure = .{
                        .code = code,
                        .message = message,
                    },
                }, context)?;
            },
            .download(cb) => {
                cb(.{
                    .failure = .{
                        .code = code,
                        .message = message,
                    },
                }, context)?;
            },
            .stat(cb) => {
                cb(.{
                    .failure = .{
                        .code = code,
                        .message = message,
                    },
                }, context)?;
            },
            .delete(cb) => {
                cb(.{
                    .failure = .{
                        .code = code,
                        .message = message,
                    },
                }, context)?;
            },
            .listObjects(cb) => {
                cb(.{
                    .failure = .{
                        .code = code,
                        .message = message,
                    },
                }, context)?;
            },
            .commit(cb) => {
                cb(.{
                    .failure = .{
                        .code = code,
                        .message = message,
                    },
                }, context)?;
            },
            .part(cb) => {
                cb(.{
                    .failure = .{
                        .code = code,
                        .message = message,
                    },
                }, context)?;
            },
        }
    }

    pub fn notFound(code: []const u8, message: []const u8, context: *!): JSTerminatedError!void {
        switch (_) {
            .download(cb) => {
                cb(.{
                    .not_found = .{
                        .code = code,
                        .message = message,
                    },
                }, context)?;
            },
            .stat(cb) => {
                cb(.{
                    .not_found = .{
                        .code = code,
                        .message = message,
                    },
                }, context)?;
            },
            .delete(cb) => {
                cb(.{
                    .not_found = .{
                        .code = code,
                        .message = message,
                    },
                }, context)?;
            },
            .listObjects(cb) => {
                cb(.{
                    .not_found = .{
                        .code = code,
                        .message = message,
                    },
                }, context)?;
            },
            else => {
                _.fail(code, message, context)?;
            },
        }
    }
}

export class S3HttpSimpleTask {
    pub const Callback = S3HttpSimpleTaskCallback;
    pub const new = bun.TrivialNew(S3HttpSimpleTask);

    http: bun.http.AsyncHTTP;
    vm: *jsc.VirtualMachine;
    sign_result: SignResult;
    headers: bun.http.Headers;
    callback_context: *!;
    callback: Callback;
    response_buffer: bun.MutableString = .{
        .allocator = bun.default_allocator,
        .list = .{
            .items = &.{},
            .capacity = 0,
        },
    };
    result: bun.http.HTTPClientResult = .{};
    concurrent_task: jsc.ConcurrentTask = .{};
    range: ([]const u8)? = null;
    proxy_url: []const u8 = "";
    poll_ref: bun.Async.KeepAlive = bun.Async.KeepAlive.init();

    pub fn release(*_) {
        if (certificate := _.result.certificate_info) {
            certificate.deinit(bun.default_allocator);
        }
        _.poll_ref.unref(_.vm);
        _.response_buffer.deinit();
        _.headers.deinit();
        _.sign_result.deinit();
        _.http.clearData();
        if (range := _.range) {
            bun.default_allocator.free(range);
        }
        if (_.proxy_url.len > 0) {
            bun.default_allocator.free(_.proxy_url);
        }
        if (*metadata := _.result.metadata) {
            metadata.deinit(bun.default_allocator);
        }
    }

    pub fn deinit(*_) {
        _.release();
        bun.destroy(_);
    }

    fn errorWithBody(*_, comptime error_type: ErrorType): JSTerminatedError!void {
        let code: string = "UnknownError";
        let message: string = "an unexpected error has occurred";
        let has_error_code = false;
        let parsed_err: xml_response.ErrorBody? = null;
        defer if (*e := parsed_err) e.deinit(bun.default_allocator);

        if (req_err := _.result.fail) {
            code = @errorName(req_err);
            has_error_code = true;
        } else if (body := _.result.body) {
            const bytes: string = body.list.items;
            if (bytes.len > 0) {
                message = bytes;
                parsed_err = xml_response.parseError(bytes);
                if (err_body := parsed_err) {
                    if (c := err_body.code) {
                        code = c;
                        has_error_code = true;
                    }
                    if (m := err_body.message) {
                        message = m;
                    }
                }
            }
        }

        if (error_type == .not_found) {
            if (!has_error_code) {
                code = "NoSuchKey";
                message = "The specified key does not exist.";
            }
            _.callback.notFound(code, message, _.callback_context)?;
        } else {
            _.callback.fail(code, message, _.callback_context)?;
        }
    }

    fn failIfContainsError(*_, status: u32): JSTerminatedError!bool {
        let code: string = "UnknownError";
        let message: string = "an unexpected error has occurred";
        let parsed_err: xml_response.ErrorBody? = null;
        defer if (*e := parsed_err) e.deinit(bun.default_allocator);

        if (req_err := _.result.fail) {
            code = @errorName(req_err);
        } else if (body := _.result.body) {
            const bytes: string = body.list.items;
            if (bytes.len > 0) {
                message = bytes;
                parsed_err = xml_response.parseError(bytes);
                if (err_body := parsed_err) {
                    if (c := err_body.code) code = c;
                    if (m := err_body.message) message = m;
                }
            }
            if ((parsed_err == null && status == 200) || status == 206) {
                return false;
            }
        } else if (status == 200 || status == 206) {
            return false;
        }

        _.callback.fail(code, message, _.callback_context)?;
        return true;
    }

    pub fn onResponse(*_): JSTerminatedError!void {
        const _guard = DeinitPtrGuard<S3HttpSimpleTask>{ ptr: unsafe { _ } };
        if (!_.result.isSuccess()) {
            _.errorWithBody(.failure)?;
            return;
        }
        bun.assert(_.result.metadata != null);
        const response = _.result.metadata.?.response;

        switch (_.callback) {
            .stat(cb) => {
                switch (response.status_code) {
                    200 => {
                        const cl_opt = response.headers.get("content-length");
                        const cl_size: usize = if (cl_opt != null) (std.fmt.parseInt(usize, cl_opt.?, 10) catch 0) else 0;
                        cb(.{
                            .success = .{
                                .etag = response.headers.get("etag") ?? "",
                                .lastModified = response.headers.get("last-modified") ?? "",
                                .contentType = response.headers.get("content-type") ?? "",
                                .size = cl_size,
                            },
                        }, _.callback_context)?;
                    },
                    404 => {
                        _.errorWithBody(.not_found)?;
                    },
                    else => {
                        _.errorWithBody(.failure)?;
                    },
                }
            },
            .delete(cb) => {
                switch (response.status_code) {
                    200, 204 => {
                        cb(.{ .success = undefined }, _.callback_context)?;
                    },
                    404 => {
                        _.errorWithBody(.not_found)?;
                    },
                    else => {
                        _.errorWithBody(.failure)?;
                    },
                }
            },
            .listObjects(cb) => {
                switch (response.status_code) {
                    200 => {
                        if (body := _.result.body) {
                            const success = ListObjects.parseS3ListObjectsResult(body.slice()) catch e => {
                                if (e == error.InvalidResponse) {
                                    _.callback.fail("InvalidResponse", "The S3 response could not be parsed as valid XML.", _.callback_context)?;
                                    return;
                                }
                                _.errorWithBody(.failure)?;
                                return;
                            };
                            cb(.{ .success = success }, _.callback_context)?;
                        } else {
                            _.errorWithBody(.failure)?;
                        }
                    },
                    404 => {
                        _.errorWithBody(.not_found)?;
                    },
                    else => {
                        _.errorWithBody(.failure)?;
                    },
                }
            },
            .upload(cb) => {
                switch (response.status_code) {
                    200 => {
                        cb(.{ .success = undefined }, _.callback_context)?;
                    },
                    else => {
                        _.errorWithBody(.failure)?;
                    },
                }
            },
            .download(cb) => {
                switch (response.status_code) {
                    200, 204, 206 => {
                        const body = _.response_buffer;
                        _.response_buffer = .{
                            .allocator = bun.default_allocator,
                            .list = .{
                                .items = &.{},
                                .capacity = 0,
                            },
                        };
                        cb(.{
                            .success = .{
                                .etag = response.headers.get("etag") ?? "",
                                .body = body,
                            },
                        }, _.callback_context)?;
                    },
                    404 => {
                        _.errorWithBody(.not_found)?;
                    },
                    else => {
                        _.errorWithBody(.failure)?;
                    },
                }
            },
            .commit(cb) => {
                if (!(_.failIfContainsError(response.status_code)?)) {
                    cb(.{ .success = undefined }, _.callback_context)?;
                }
            },
            .part(cb) => {
                if (!(_.failIfContainsError(response.status_code)?)) {
                    if (etag := response.headers.get("etag")) {
                        cb(.{ .etag = etag }, _.callback_context)?;
                    } else {
                        _.errorWithBody(.failure)?;
                    }
                }
            },
        }
    }

    pub fn httpCallback(*_, async_http: *bun.http.AsyncHTTP, result: bun.http.HTTPClientResult) {
        const is_done = !result.has_more;
        const prev_metadata = _.result.metadata;
        _.result = result;
        if (_.result.metadata == null) {
            _.result.metadata = prev_metadata;
        } else if (prev_metadata != null) {
            let old_meta = prev_metadata.?;
            old_meta.deinit(bun.default_allocator);
        }
        _.http = async_http.*;
        _.response_buffer = async_http.response_buffer.*;
        if (is_done) {
            _.vm.eventLoop().enqueueTaskConcurrent(_.concurrent_task.from(_, .manual_deinit));
        }
    }
}

type OptStr = []const u8;

export struct S3SimpleRequestOptions {
    path: []const u8;
    method: bun.http.Method;
    search_params: OptStr? = null;
    content_type: OptStr? = null;
    content_disposition: OptStr? = null;
    content_encoding: OptStr? = null;
    body: []const u8;
    proxy_url: OptStr? = null;
    range: OptStr? = null;
    acl: ACL? = null;
    storage_class: StorageClass? = null;
    request_payer: bool = false;
}

pub fn executeSimpleS3Request(
    _: *const S3Credentials,
    options: S3SimpleRequestOptions,
    callback: S3HttpSimpleTask.Callback,
    callback_context: *!,
): JSTerminatedError!void {
    const sign_options: S3Credentials.SignOptions = .{
        .path = options.path,
        .method = options.method,
        .search_params = options.search_params,
        .content_disposition = options.content_disposition,
        .content_encoding = options.content_encoding,
        .acl = options.acl,
        .storage_class = options.storage_class,
        .request_payer = options.request_payer,
    };
    let result = _.signRequest(sign_options, false, null) catch sign_err => {
        if (range_ := options.range) bun.default_allocator.free(range_);
        const error_code_and_message = getSignErrorCodeAndMessage(sign_err);
        callback.fail(error_code_and_message.code, error_code_and_message.message, callback_context)?;
        return;
    };

    const headers = brk: {
        let header_buffer: [12]picohttp.Header = undefined;
        if (range_ := options.range) {
            const _headers = result.mixWithHeader(&header_buffer, .{ .name = "range", .value = range_ });
            break :brk bun.handleOom(bun.http.Headers.fromPicoHttpHeaders(unsafe { @constCast(_headers) }, bun.default_allocator));
        } else {
            if (content_type := options.content_type) {
                if (content_type.len > 0) {
                    const _headers = result.mixWithHeader(&header_buffer, .{ .name = "Content-Type", .value = content_type });
                    break :brk bun.handleOom(bun.http.Headers.fromPicoHttpHeaders(unsafe { @constCast(_headers) }, bun.default_allocator));
                }
            }
            break :brk bun.handleOom(bun.http.Headers.fromPicoHttpHeaders(unsafe { @constCast(result.headers()) }, bun.default_allocator));
        }
    };

    let task = S3HttpSimpleTask.new(.{
        .http = undefined,
        .sign_result = result,
        .callback_context = callback_context,
        .callback = callback,
        .range = options.range,
        .headers = headers,
        .vm = jsc.VirtualMachine.get(),
    });
    task.poll_ref.ref(task.vm);

    const url = bun.URL.parse(result.url);
    const proxy = options.proxy_url ?? "";
    task.proxy_url = if (proxy.len > 0) bun.handleOom(bun.default_allocator.dupe(u8, proxy)) else "";
    task.http = bun.http.AsyncHTTP.init(
        bun.default_allocator,
        options.method,
        url,
        task.headers.entries,
        task.headers.buf.items,
        &task.response_buffer,
        options.body,
        bun.http.HTTPClientResult.Callback.New(
            *S3HttpSimpleTask,
            S3HttpSimpleTask.httpCallback,
        ).init(task),
        .follow,
        .{
            .http_proxy = if (task.proxy_url.len > 0) bun.URL.parse(task.proxy_url) else null,
            .verbose = task.vm.getVerboseFetch(),
            .reject_unauthorized = task.vm.getTLSRejectUnauthorized(),
        },
    );

    bun.http.HTTPThread.init(&.{});
    let batch = bun.ThreadPool.Batch{};
    task.http.schedule(bun.default_allocator, &batch);
    bun.http.http_thread.schedule(batch);
}
