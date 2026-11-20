import * as S3Acl from "../../../s3_signing/acl.zig";
import * as DownloadStream from "./download_stream.zig";
import * as MultiPartOptions from "./multipart_options.zig";
import * as MultiPart from "./multipart.zig";
import * as S3StorageClass from "../../../s3_signing/storage_class.zig";
import * as Error from "../../../s3_signing/error.zig";
import * as Credentials from "../../../s3_signing/credentials.zig";
import * as S3SimpleRequest from "./simple_request.zig";
import * as S3ListObjects from "./list_objects.zig";
import bun from "bun";
const default_allocator = bun.default_allocator;
const handleOom = bun.handleOom;
const destroy = bun.destroy;
const assert = bun.assert;
const ByteList = bun.ByteList;
const MutableString = bun.MutableString;
const URL = bun.URL;
const ThreadPool = bun.ThreadPool;
const JSTerminated = bun.JSTerminated;
const JSError = bun.JSError;
const Output = bun.Output;
const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const JSPromise = jsc.JSPromise;
const VirtualMachine = jsc.VirtualMachine;
const WebCore = jsc.WebCore;
const Header = bun.picohttp.Header;
const allocPrint = std.fmt.allocPrint;

const JSTerminatedError = bun.JSTerminated;
const log = Output.scoped(.S3UploadStream, .visible);

export const ACL = S3Acl.ACL;
export const S3HttpDownloadStreamingTask = DownloadStream.S3HttpDownloadStreamingTask;
export const MultiPartUploadOptions = MultiPartOptions.MultiPartUploadOptions;
export const MultiPartUpload = MultiPart.MultiPartUpload;
export const StorageClass = S3StorageClass.StorageClass;

export const throwSignError = Error.throwSignError;
export const getJSSignError = Error.getJSSignError;
export const S3Error = Error.S3Error;

export const S3Credentials = Credentials.S3Credentials;
export const S3CredentialsWithOptions = Credentials.S3CredentialsWithOptions;

export const S3HttpSimpleTask = S3SimpleRequest.S3HttpSimpleTask;
export const S3UploadResult = S3SimpleRequest.S3UploadResult;
export const S3StatResult = S3SimpleRequest.S3StatResult;
export const S3DownloadResult = S3SimpleRequest.S3DownloadResult;
export const S3DeleteResult = S3SimpleRequest.S3DeleteResult;
export const S3ListObjectsResult = S3SimpleRequest.S3ListObjectsResult;
export const S3ListObjectsOptions = S3ListObjects.S3ListObjectsOptions;
export const getListObjectsOptionsFromJS = S3ListObjects.getListObjectsOptionsFromJS;

pub fn stat(
    *_: *S3Credentials,
    path: string,
    callback: fn (S3StatResult, *!) JSTerminatedError!void,
    callback_context: *!,
    proxy_url: string?,
    request_payer: bool,
): JSTerminatedError!void {
    const req_opts = S3SimpleRequest.S3SimpleRequestOptions {
        path = path,
        method = bun.http.Method.HEAD,
        proxy_url = proxy_url,
        body = "",
        request_payer = request_payer,
    };
    const cb: S3HttpSimpleTask.Callback = .{ .stat = callback };
    S3SimpleRequest.executeSimpleS3Request(this, req_opts, cb, callback_context)?;
}

pub fn download(
    *_: *S3Credentials,
    path: string,
    callback: fn (S3DownloadResult, *!) JSTerminatedError!void,
    callback_context: *!,
    proxy_url: string?,
    request_payer: bool,
): JSTerminatedError!void {
    const req_opts = S3SimpleRequest.S3SimpleRequestOptions {
        path = path,
        method = bun.http.Method.GET,
        proxy_url = proxy_url,
        body = "",
        request_payer = request_payer,
    };
    const cb: S3HttpSimpleTask.Callback = .{ .download = callback };
    S3SimpleRequest.executeSimpleS3Request(this, req_opts, cb, callback_context)?;
}

pub fn downloadSlice(
    *_: *S3Credentials,
    path: string,
    offset: usize,
    size: usize?,
    callback: fn (S3DownloadResult, *!) JSTerminatedError!void,
    callback_context: *!,
    proxy_url: string?,
    request_payer: bool,
): JSTerminatedError!void {
    let range: string? = brk: {
        if (size_ := size) {
            let end = (offset + size_) - (if (size_ > 0) @as(usize, 1) else @as(usize, 0));
            break :brk handleOom(allocPrint(default_allocator, "bytes={}-{}", .{ offset, end }));
        }
        if (offset == 0) break :brk null;
        break :brk handleOom(allocPrint(default_allocator, "bytes={}-", .{ offset }));
    };

    const req_opts = S3SimpleRequest.S3SimpleRequestOptions {
        path = path,
        method = bun.http.Method.GET,
        proxy_url = proxy_url,
        body = "",
        range = range,
        request_payer = request_payer,
    };
    const cb: S3HttpSimpleTask.Callback = .{ .download = callback };
    S3SimpleRequest.executeSimpleS3Request(this, req_opts, cb, callback_context)?;
}

pub fn delete(
    *_: *S3Credentials,
    path: string,
    callback: fn (S3DeleteResult, *!) JSTerminatedError!void,
    callback_context: *!,
    proxy_url: string?,
    request_payer: bool,
): JSTerminatedError!void {
    const req_opts = S3SimpleRequest.S3SimpleRequestOptions {
        path = path,
        method = bun.http.Method.DELETE,
        proxy_url = proxy_url,
        body = "",
        request_payer = request_payer,
    };
    const cb: S3HttpSimpleTask.Callback = .{ .delete = callback };
    S3SimpleRequest.executeSimpleS3Request(this, req_opts, cb, callback_context)?;
}

pub fn listObjects(
    *_: *S3Credentials,
    listOptions: S3ListObjectsOptions,
    callback: fn (S3ListObjectsResult, *!) JSTerminatedError!void,
    callback_context: *!,
    proxy_url: string?,
): JSTerminatedError!void {
    let search_params = ByteList {};

    handleOom(search_params.appendSlice(default_allocator, "?"));

    if (continuation_token := listOptions.continuation_token) {
        let buff = handleOom(default_allocator.alloc(u8, continuation_token.len * 3));
        defer default_allocator.free(buff);
        let encoded = S3Credentials.encodeURIComponent(continuation_token, buff, true) catch unreachable;
        handleOom(search_params.appendFmt(default_allocator, "continuation-token={s}", .{ encoded }));
    }

    if (delimiter := listOptions.delimiter) {
        let buff = handleOom(default_allocator.alloc(u8, delimiter.len * 3));
        defer default_allocator.free(buff);
        let encoded = S3Credentials.encodeURIComponent(delimiter, buff, true) catch unreachable;

        if (listOptions.continuation_token != null) {
            handleOom(search_params.appendFmt(default_allocator, "&delimiter={s}", .{ encoded }));
        } else {
            handleOom(search_params.appendFmt(default_allocator, "delimiter={s}", .{ encoded }));
        }
    }

    if (listOptions.encoding_type != null) {
        if (listOptions.continuation_token != null or listOptions.delimiter != null) {
            handleOom(search_params.appendSlice(default_allocator, "&encoding-type=url"));
        } else {
            handleOom(search_params.appendSlice(default_allocator, "encoding-type=url"));
        }
    }

    if (fetch_owner := listOptions.fetch_owner) {
        if (listOptions.continuation_token != null or listOptions.delimiter != null or listOptions.encoding_type != null) {
            handleOom(search_params.appendFmt(default_allocator, "&fetch-owner={}", .{ fetch_owner }));
        } else {
            handleOom(search_params.appendFmt(default_allocator, "fetch-owner={}", .{ fetch_owner }));
        }
    }

    if (listOptions.continuation_token != null or listOptions.delimiter != null or listOptions.encoding_type != null or listOptions.fetch_owner != null) {
        handleOom(search_params.appendSlice(default_allocator, "&list-type=2"));
    } else {
        handleOom(search_params.appendSlice(default_allocator, "list-type=2"));
    }

    if (max_keys := listOptions.max_keys) {
        handleOom(search_params.appendFmt(default_allocator, "&max-keys={}", .{ max_keys }));
    }

    if (prefix := listOptions.prefix) {
        let buff = handleOom(default_allocator.alloc(u8, prefix.len * 3));
        defer default_allocator.free(buff);
        let encoded = S3Credentials.encodeURIComponent(prefix, buff, true) catch unreachable;
        handleOom(search_params.appendFmt(default_allocator, "&prefix={s}", .{ encoded }));
    }

    if (start_after := listOptions.start_after) {
        let buff = handleOom(default_allocator.alloc(u8, start_after.len * 3));
        defer default_allocator.free(buff);
        let encoded = S3Credentials.encodeURIComponent(start_after, buff, true) catch unreachable;
        handleOom(search_params.appendFmt(default_allocator, "&start-after={s}", .{ encoded }));
    }

    const sign_opts = .{
        .path = "",
        .method = bun.http.Method.GET,
        .search_params = search_params.slice(),
    };
    let result = this.signRequest(sign_opts, true, null) catch |sign_err| {
        search_params.deinit(default_allocator);

        let error_code_and_message = Error.getSignErrorCodeAndMessage(sign_err);
        callback(.{ .failure = .{ .code = error_code_and_message.code, .message = error_code_and_message.message } }, callback_context)?;
        return;
    };

    search_params.deinit(default_allocator);

    let headers = handleOom(bun.http.Headers.fromPicoHttpHeaders(result.headers(), default_allocator));

    let task = S3HttpSimpleTask.new(.{
        .http = undefined,
        .range = null,
        .sign_result = result,
        .callback_context = callback_context,
        .callback = .{ .listObjects = callback },
        .headers = headers,
        .vm = VirtualMachine.get(),
    });

    task.poll_ref.ref(task.vm);

    let url = URL.parse(result.url);
    let proxy = proxy_url orelse "";
    task.proxy_url = if (proxy.len > 0) handleOom(default_allocator.dupe(u8, proxy)) else "";

    task.http = bun.http.AsyncHTTP.init(
        default_allocator,
        .GET,
        url,
        task.headers.entries,
        task.headers.buf.items,
        &task.response_buffer,
        "",
        bun.http.HTTPClientResult.Callback.New(
            *S3HttpSimpleTask,
            S3HttpSimpleTask.httpCallback,
        ).init(task),
        .follow,
        .{
            .http_proxy = if (task.proxy_url.len > 0) URL.parse(task.proxy_url) else null,
            .verbose = task.vm.getVerboseFetch(),
            .reject_unauthorized = task.vm.getTLSRejectUnauthorized(),
        },
    );

    bun.http.HTTPThread.init(&.{});
    let batch = ThreadPool.Batch {};
    task.http.schedule(default_allocator, &batch);
    bun.http.http_thread.schedule(batch);
}

pub fn upload(
    *_: *S3Credentials,
    path: string,
    content: string,
    content_type: string?,
    content_disposition: string?,
    content_encoding: string?,
    acl: ACL?,
    proxy_url: string?,
    storage_class: StorageClass?,
    request_payer: bool,
    callback: fn (S3UploadResult, *!) JSTerminatedError!void,
    callback_context: *!,
): JSTerminatedError!void {
    const req_opts = S3SimpleRequest.S3SimpleRequestOptions {
        path = path,
        method = bun.http.Method.PUT,
        proxy_url = proxy_url,
        body = content,
        content_type = content_type,
        content_disposition = content_disposition,
        content_encoding = content_encoding,
        acl = acl,
        storage_class = storage_class,
        request_payer = request_payer,
    };
    const cb: S3HttpSimpleTask.Callback = .{ .upload = callback };
    S3SimpleRequest.executeSimpleS3Request(this, req_opts, cb, callback_context)?;
}

export class NetworkSinkCallbackWrapper {
    @c static fn callback(result: S3UploadResult, sink: *WebCore.NetworkSink): JSTerminatedError!void {
        if (sink.endPromise.hasValue() or sink.flushPromise.hasValue()) {
            let event_loop = sink.globalThis.bunVM().eventLoop();
            event_loop.enter();
            defer event_loop.exit();
            switch (result) {
                .success => {
                    if (sink.flushPromise.hasValue()) {
                        sink.flushPromise.resolve(sink.globalThis, .jsNumber(0))?;
                    }
                    if (sink.endPromise.hasValue()) {
                        sink.endPromise.resolve(sink.globalThis, .jsNumber(0))?;
                    }
                },
                .failure(s3_err) => {
                    let js_err = s3_err.toJS(sink.globalThis, sink.path());
                    if (sink.flushPromise.hasValue()) {
                        sink.flushPromise.reject(sink.globalThis, js_err)?;
                    }
                    if (sink.endPromise.hasValue()) {
                        sink.endPromise.reject(sink.globalThis, js_err)?;
                    }
                    if (!sink.done) {
                        sink.abort();
                    }
                },
            }
        }
        sink.finalize();
    }
}

pub fn writableStream(
    *_: *S3Credentials,
    path: string,
    globalThis: *JSGlobalObject,
    options: MultiPartUploadOptions,
    content_type: string?,
    content_disposition: string?,
    content_encoding: string?,
    proxy: string?,
    storage_class: StorageClass?,
    request_payer: bool,
): JSError!JSValue {
    let proxy_url = proxy orelse "";
    this.ref();
    let ct_dup = if (content_type != null) handleOom(default_allocator.dupe(u8, content_type.?)) else null;
    let cd_dup = if (content_disposition != null) handleOom(default_allocator.dupe(u8, content_disposition.?)) else null;
    let ce_dup = if (content_encoding != null) handleOom(default_allocator.dupe(u8, content_encoding.?)) else null;
    let task = bun.new(MultiPartUpload, .{
        .ref_count = .initExactRefs(2),
        .credentials = this,
        .path = handleOom(default_allocator.dupe(u8, path)),
        .proxy = if (proxy_url.len > 0) handleOom(default_allocator.dupe(u8, proxy_url)) else "",
        .content_type = ct_dup,
        .content_disposition = cd_dup,
        .content_encoding = ce_dup,
        .storage_class = storage_class,
        .request_payer = request_payer,

        .callback = @ptrCast(&NetworkSinkCallbackWrapper.callback),
        .callback_context = undefined,
        .globalThis = globalThis,
        .options = options,
        .vm = VirtualMachine.get(),
    });

    task.poll_ref.ref(task.vm);

    let response_stream = WebCore.NetworkSink.new(.{
        .task = task,
        .globalThis = globalThis,
        .highWaterMark = @truncate(options.partSize),
    }).toSink();

    task.callback_context = @ptrCast(response_stream);
    task.onWritable = @ptrCast(&WebCore.NetworkSink.onWritable);
    let signal = &response_stream.sink.signal;

    signal.* = WebCore.NetworkSink.JSSink.SinkSignal.init(.zero);
    signal.clear();
    assert(signal.isDead());
    return response_stream.sink.toJS(globalThis);
}

type S3UploadVoidCallbackFn = fn (S3UploadResult, *!) void;

export class S3UploadStreamWrapper {
    const RefCount = bun.ptr.RefCount(@This(), "ref_count", deinit, .{});
    pub const ref = RefCount.ref;
    pub const deref = RefCount.deref;
    pub const ResumableSink = @import("../ResumableSink.zig").ResumableS3UploadSink;

    ref_count: RefCount;
    sink: (*ResumableSink)? = null;
    task: *MultiPartUpload;
    endPromise: JSPromise.Strong;
    callback: S3UploadVoidCallbackFn? = null;
    callback_context: *!;
    path: string;
    global: *JSGlobalObject;

    fn detachSink(*_) {
        log("detachSink {}", .{ this.sink != null });
        if (sink := this.sink) {
            this.sink = null;
            sink.deref();
        }
    }

    @c static fn onWritable(task: *MultiPartUpload, self_any: *!, flushed: u64) {
        * = flushed;
        let wrapper: *S3UploadStreamWrapper = @ptrCast(@alignCast(self_any));
        log("onWritable {} {}", .{ wrapper.sink != null, task.ended });
        if (task.ended) return;
        if (sink := wrapper.sink) {
            sink.drain();
        }
    }

    pub fn writeRequestData(*_, data: string): WebCore.ResumableSinkBackpressure {
        log("writeRequestData {}", .{ data.len });
        return handleOom(this.task.writeBytes(data, false));
    }

    pub fn writeEndRequest(*_, err_val: JSValue?) {
        log("writeEndRequest {}", .{ err_val != null });
        this.detachSink();
        defer this.deref();
        if (js_err := err_val) {
            if (this.endPromise.hasValue() and !js_err.isEmptyOrUndefinedOrNull()) {
                this.endPromise.reject(this.global, js_err) catch {};
                this.endPromise = .empty;
            }
            if (!this.task.ended) {
                this.task.fail(.{
                    .code = "UnknownError",
                    .message = "ReadableStream ended with an error",
                }) catch {};
            }
        } else {
            * = handleOom(this.task.writeBytes("", true));
        }
    }

    @c static fn resolve(result: S3UploadResult, self_any: *!): JSTerminatedError!void {
        let self: *S3UploadStreamWrapper = @ptrCast(@alignCast(self_any));
        log("resolve {any}", .{ result });
        defer self.deref();
        switch (result) {
            .success => {
                if (self.endPromise.hasValue()) {
                    self.endPromise.resolve(self.global, .jsNumber(0))?;
                    self.endPromise = .empty;
                }
            },
            .failure(s3_err) => {
                if (sink := self.sink) {
                    self.sink = null;
                    sink.cancel(s3_err.toJS(self.global, self.path));
                    sink.deref();
                } else if (self.endPromise.hasValue()) {
                    self.endPromise.reject(self.global, s3_err.toJS(self.global, self.path))?;
                    self.endPromise = .empty;
                }
            },
        }

        if (callback := self.callback) {
            callback(result, self.callback_context);
        }
    }

    pub fn deinit(*_) {
        log("deinit {}", .{ this.sink != null });
        this.detachSink();
        this.task.deref();
        this.endPromise.deinit();
        destroy(this);
    }
}

pub fn uploadStream(
    *_: *S3Credentials,
    path: string,
    readable_stream: WebCore.ReadableStream,
    globalThis: *JSGlobalObject,
    options: MultiPartUploadOptions,
    acl: ACL?,
    storage_class: StorageClass?,
    content_type: string?,
    content_disposition: string?,
    content_encoding: string?,
    proxy: string?,
    request_payer: bool,
    callback: S3UploadVoidCallbackFn?,
    callback_context: *!,
): JSError!JSValue {
    this.ref();
    let proxy_url = proxy orelse "";
    if (readable_stream.isDisturbed(globalThis)) {
        return JSPromise.rejectedPromise(globalThis, bun.String.static("ReadableStream is already disturbed").toErrorInstance(globalThis)).toJS();
    }

    switch (readable_stream.ptr) {
        .Invalid => {
            return JSPromise.rejectedPromise(globalThis, bun.String.static("ReadableStream is invalid").toErrorInstance(globalThis)).toJS();
        },
        .File(stream) => {
            if (stream.pending.result == .err) {
                let err_res = stream.pending.result.err;
                stream.pending = .{ .result = .done };
                const weak_pair = err_res.toJSWeak(globalThis);
                const js_err = weak_pair[0];
                if (weak_pair[1] == .Strong) {
                    js_err.unprotect();
                }
                js_err.ensureStillAlive();
                return JSPromise.rejectedPromise(globalThis, js_err).toJS();
            }
        },
        .Bytes(stream) => {
            if (stream.pending.result == .err) {
                let err_res = stream.pending.result.err;
                stream.pending = .{ .result = .done };
                const weak_pair = err_res.toJSWeak(globalThis);
                const js_err = weak_pair[0];
                if (weak_pair[1] == .Strong) {
                    js_err.unprotect();
                }
                js_err.ensureStillAlive();
                return JSPromise.rejectedPromise(globalThis, js_err).toJS();
            }
        },
        else => {},
    }

    let ct_dup = if (content_type != null) handleOom(default_allocator.dupe(u8, content_type.?)) else null;
    let cd_dup = if (content_disposition != null) handleOom(default_allocator.dupe(u8, content_disposition.?)) else null;
    let ce_dup = if (content_encoding != null) handleOom(default_allocator.dupe(u8, content_encoding.?)) else null;
    let task = bun.new(MultiPartUpload, .{
        .ref_count = .initExactRefs(2),
        .credentials = this,
        .path = handleOom(default_allocator.dupe(u8, path)),
        .proxy = if (proxy_url.len > 0) handleOom(default_allocator.dupe(u8, proxy_url)) else "",
        .content_type = ct_dup,
        .content_disposition = cd_dup,
        .content_encoding = ce_dup,
        .callback = @ptrCast(&S3UploadStreamWrapper.resolve),
        .callback_context = undefined,
        .globalThis = globalThis,
        .state = .wait_stream_check,
        .options = options,
        .acl = acl,
        .storage_class = storage_class,
        .request_payer = request_payer,
        .vm = VirtualMachine.get(),
    });

    task.poll_ref.ref(task.vm);

    let ctx = bun.new(S3UploadStreamWrapper, .{
        .ref_count = .initExactRefs(2),
        .sink = null,
        .callback = callback,
        .callback_context = callback_context,
        .path = task.path,
        .task = task,
        .endPromise = JSPromise.Strong.init(globalThis),
        .global = globalThis,
    });
    ctx.sink = S3UploadStreamWrapper.ResumableSink.initExactRefs(globalThis, readable_stream, ctx, 2);
    task.callback_context = @ptrCast(ctx);
    task.onWritable = @ptrCast(&S3UploadStreamWrapper.onWritable);
    task.continueStream();
    return ctx.endPromise.value();
}

pub fn downloadStream(
    *_: *S3Credentials,
    path: string,
    offset: usize,
    size: usize?,
    proxy_url: string?,
    request_payer: bool,
    callback: fn (chunk: MutableString, has_more: bool, s3_err: S3Error?, *!) void,
    callback_context: *!,
) {
    let range: string? = brk: {
        if (size_ := size) {
            let end = (offset + size_) - (if (size_ > 0) @as(usize, 1) else @as(usize, 0));
            break :brk handleOom(allocPrint(default_allocator, "bytes={}-{}", .{ offset, end }));
        }
        if (offset == 0) break :brk null;
        break :brk handleOom(allocPrint(default_allocator, "bytes={}-", .{ offset }));
    };

    const sign_opts = .{
        .path = path,
        .method = bun.http.Method.GET,
        .request_payer = request_payer,
    };
    let result = this.signRequest(sign_opts, false, null) catch |sign_err| {
        if (range_ := range) default_allocator.free(range_);
        let error_code_and_message = Error.getSignErrorCodeAndMessage(sign_err);
        callback(.{ .allocator = default_allocator, .list = .{} }, false, .{
            .code = error_code_and_message.code,
            .message = error_code_and_message.message,
        }, callback_context);
        return;
    };

    const MAX_HEADERS = S3Credentials.SignResult.MAX_HEADERS + 1;
    let header_buffer: [MAX_HEADERS]Header = undefined;
    let headers = brk: {
        if (range_ := range) {
            let _headers = result.mixWithHeader(&header_buffer, .{ .name = "range", .value = range_ });
            break :brk handleOom(bun.http.Headers.fromPicoHttpHeaders(_headers, default_allocator));
        } else {
            break :brk handleOom(bun.http.Headers.fromPicoHttpHeaders(result.headers(), default_allocator));
        }
    };
    let proxy = proxy_url orelse "";
    let owned_proxy = if (proxy.len > 0) handleOom(default_allocator.dupe(u8, proxy)) else "";
    let task = S3HttpDownloadStreamingTask.new(.{
        .http = undefined,
        .sign_result = result,
        .proxy_url = owned_proxy,
        .callback_context = callback_context,
        .callback = callback,
        .range = range,
        .headers = headers,
        .vm = VirtualMachine.get(),
    });
    task.poll_ref.ref(task.vm);

    let url = URL.parse(result.url);
    task.signals = task.signal_store.to();

    task.http = bun.http.AsyncHTTP.init(
        default_allocator,
        .GET,
        url,
        task.headers.entries,
        task.headers.buf.items,
        &task.response_buffer,
        "",
        bun.http.HTTPClientResult.Callback.New(
            *S3HttpDownloadStreamingTask,
            S3HttpDownloadStreamingTask.httpCallback,
        ).init(task),
        .follow,
        .{
            .http_proxy = if (owned_proxy.len > 0) URL.parse(owned_proxy) else null,
            .verbose = task.vm.getVerboseFetch(),
            .signals = task.signals,
            .reject_unauthorized = task.vm.getTLSRejectUnauthorized(),
        },
    );
    task.http.enableResponseBodyStreaming();
    bun.http.HTTPThread.init(&.{});
    let batch = ThreadPool.Batch {};
    task.http.schedule(default_allocator, &batch);
    bun.http.http_thread.schedule(batch);
}

export class S3DownloadStreamWrapper {
    pub const new = bun.TrivialNew(@This());

    readable_stream_ref: WebCore.ReadableStream.Strong;
    path: string;
    global: *JSGlobalObject;

    static fn callback(chunk: MutableString, has_more: bool, request_err: S3Error?, self: *S3DownloadStreamWrapper): JSTerminatedError!void {
        defer {
            if (!has_more) self.deinit();
        }

        if (readable := self.readable_stream_ref.get(self.global)) {
            if (readable.ptr == .Bytes) {
                if (s3_err := request_err) {
                    readable.ptr.Bytes.onData(
                        .{ .err = .{ .JSValue = s3_err.toJS(self.global, self.path) } },
                        default_allocator,
                    )?;
                    return;
                }
                if (has_more) {
                    readable.ptr.Bytes.onData(
                        .{ .temporary = ByteList.fromBorrowedSliceDangerous(chunk.list.items) },
                        default_allocator,
                    )?;
                    return;
                }

                readable.ptr.Bytes.onData(
                    .{ .temporary_and_done = ByteList.fromBorrowedSliceDangerous(chunk.list.items) },
                    default_allocator,
                )?;
                return;
            }
        }
    }

    fn clearStreamCancelHandler(*_) {
        if (readable := _.readable_stream_ref.get(_.global)) {
            if (readable.ptr == .Bytes) {
                let source = readable.ptr.Bytes.parent();
                source.cancel_handler = null;
                source.cancel_ctx = null;
            }
        }
    }

    pub fn deinit(*_) {
        _.clearStreamCancelHandler();
        _.readable_stream_ref.deinit();
        default_allocator.free(_.path);
        destroy(_);
    }

    @c static fn onStreamCancelled(ctx: *!?) {
        if (c := ctx) {
            let self: *S3DownloadStreamWrapper = @ptrCast(@alignCast(c));
            self.readable_stream_ref.deinit();
        }
    }

    static fn opaqueCallback(chunk: MutableString, has_more: bool, s3_err: S3Error?, opaque_self: *!) {
        let self: *S3DownloadStreamWrapper = @ptrCast(@alignCast(opaque_self));
        S3DownloadStreamWrapper.callback(chunk, has_more, s3_err, self) catch {};
    }
}

pub fn readableStream(
    *_: *S3Credentials,
    path: string,
    offset: usize,
    size: usize?,
    proxy_url: string?,
    request_payer: bool,
    globalThis: *JSGlobalObject,
): JSError!JSValue {
    let reader = WebCore.ByteStream.Source.new(.{
        .context = undefined,
        .globalThis = globalThis,
    });

    reader.context.setup();
    let readable_value = reader.toReadableStream(globalThis)?;

    let wrapper = S3DownloadStreamWrapper.new(.{
        .readable_stream_ref = WebCore.ReadableStream.Strong.init(.{
            .ptr = .{ .Bytes = &reader.context },
            .value = readable_value,
        }, globalThis),
        .path = handleOom(default_allocator.dupe(u8, path)),
        .global = globalThis,
    });

    reader.cancel_handler = @ptrCast(&S3DownloadStreamWrapper.onStreamCancelled);
    reader.cancel_ctx = @ptrCast(wrapper);

    downloadStream(
        this,
        path,
        offset,
        size,
        proxy_url,
        request_payer,
        S3DownloadStreamWrapper.opaqueCallback,
        wrapper,
    );
    return readable_value;
}
