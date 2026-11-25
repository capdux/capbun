import bun from "bun";
import { ACL } from "../../../s3_signing/acl.zig";
import { MultiPartUploadOptions } from "./multipart_options.zig";
import { S3Credentials } from "../../../s3_signing/credentials.zig";
import { S3Error } from "../../../s3_signing/error.zig";
import { StorageClass } from "../../../s3_signing/storage_class.zig";
import * as S3SimpleRequest from "./simple_request.zig";

const sort = std.sort;
const default_allocator = bun.default_allocator;
const handleOom = bun.handleOom;
const destroy = bun.destroy;
const ByteList = bun.ByteList;
const MutableString = bun.MutableString;
const Output = bun.Output;
const strings = bun.strings;
const JSGlobalObject = bun.jsc.JSGlobalObject;
const VirtualMachine = bun.jsc.VirtualMachine;
const WebCore = bun.jsc.WebCore;
const IntegerBitSet = bun.bit_set.IntegerBitSet;
const StreamBuffer = bun.io.StreamBuffer;
const KeepAlive = bun.Async.KeepAlive;
const bufPrint = std.fmt.bufPrint;

const executeSimpleS3Request = S3SimpleRequest.executeSimpleS3Request;
const ResumableSinkBackpressure = WebCore.ResumableSinkBackpressure;
const JSTerminatedError = bun.JSTerminated;
const OOMError = bun.OOM;
const log = Output.scoped(.S3MultiPartUpload, .hidden);

struct DerefGuard<T> {
    ptr: (*T!)? = null,

    ~init() {
        if (p := _.ptr) {
            _.ptr = null;
            unsafe { p.deref(); }
        }
    }
}

export enum MultiPartUploadState {
    wait_stream_check,
    not_started,
    multipart_started,
    multipart_completed,
    singlefile_started,
    finished,
}

export enum UploadPartState: u8 {
    not_assigned = 0,
    pending = 1,
    started = 2,
    completed = 3,
    canceled = 4,
}

export struct UploadPartResult {
    number: u16;
    etag: string;
}

export enum WriteEncoding {
    bytes,
    latin1,
    utf16,
}

export struct UploadPart {
    data: string = "";
    ctx: *MultiPartUpload;
    allocated_size: usize = 0;
    state: UploadPartState = .not_assigned;
    partNumber: u16 = 0;
    retry: u8 = 0;
    index: u8 = 0;

    static fn sortEtags(_: *MultiPartUpload, a: UploadPartResult, b: UploadPartResult): bool {
        return a.number < b.number;
    }

    fn freeAllocatedSlice(*_) {
        let slice = _.allocatedSlice();
        if (slice.len > 0) {
            default_allocator.free(slice);
        }
        _.data = "";
        _.allocated_size = 0;
    }

    fn allocatedSlice(_: *const UploadPart): string {
        if (_.allocated_size > 0) {
            return _.data[0.._.allocated_size];
        }
        return "";
    }

    static fn onPartResponse(result: S3SimpleRequest.S3PartResult, self_any: *!): JSTerminatedError!void {
        const part: *UploadPart = unsafe { @ptrCast(@alignCast(self_any)) };
        if (part.state == .canceled || part.ctx.state == .finished) {
            log("onPartResponse {} canceled", .{ part.partNumber });
            part.freeAllocatedSlice();
            part.ctx.deref();
            return;
        }

        part.state = .completed;

        switch (result) {
            .failure(s3_err) => {
                if (part.retry > 0) {
                    log("onPartResponse {} retry", .{ part.partNumber });
                    part.retry -= 1;
                    part.perform()?;
                    return;
                } else {
                    part.state = .not_assigned;
                    log("onPartResponse {} failed", .{ part.partNumber });
                    part.freeAllocatedSlice();
                    let ctx_guard = DerefGuard<MultiPartUpload>{ ptr: part.ctx };
                    return part.ctx.fail(s3_err);
                }
            },
            .etag(etag) => {
                log("onPartResponse {} success", .{ part.partNumber });
                let sent = part.data.len;
                part.freeAllocatedSlice();
                part.ctx.multipart_etags.append(default_allocator, .{
                    .number = part.partNumber,
                    .etag = handleOom(default_allocator.dupe(u8, etag)),
                }) catch oom_err => handleOom(oom_err);
                part.state = .not_assigned;
                let ctx_guard = DerefGuard<MultiPartUpload>{ ptr: part.ctx };
                part.ctx.available.set(part.index);
                part.ctx.drainEnqueuedParts(sent)?;
            },
        }
    }

    fn perform(*_): JSTerminatedError!void {
        let params_buffer: [2048]u8 = undefined;
        let search_params = bufPrint(&params_buffer, "?partNumber={}&uploadId={s}&x-id=UploadPart", .{
            _.partNumber,
            _.ctx.upload_id,
        }) catch unreachable;
        const req_opts = S3SimpleRequest.S3SimpleRequestOptions{
            path: _.ctx.path,
            method: bun.http.Method.PUT,
            proxy_url: _.ctx.proxyUrl(),
            body: _.data,
            search_params: search_params,
            request_payer: _.ctx.request_payer,
        };
        const cb: S3SimpleRequest.S3HttpSimpleTask.Callback = .{ .part = unsafe { @constCast(@ptrCast(&onPartResponse)) } };
        executeSimpleS3Request(_.ctx.credentials, req_opts, cb, _)?;
    }

    pub fn start(*_): JSTerminatedError!void {
        if (_.state != .pending || _.ctx.state != .multipart_completed) return;
        _.ctx.ref();
        errdefer _.ctx.deref();
        _.state = .started;
        _.perform()?;
    }

    pub fn cancel(*_) {
        let state = _.state;
        _.state = .canceled;

        switch (state) {
            .pending => {
                _.freeAllocatedSlice();
            },
            else => {},
        }
    }
}

type OnWritableFn = fn (task: *MultiPartUpload, ctx: *!, flushed: u64) void;

export class MultiPartUpload {
    const OneMiB: usize = MultiPartUploadOptions.OneMiB;
    const MAX_SINGLE_UPLOAD_SIZE: usize = MultiPartUploadOptions.MAX_SINGLE_UPLOAD_SIZE;
    const MIN_SINGLE_UPLOAD_SIZE: usize = MultiPartUploadOptions.MIN_SINGLE_UPLOAD_SIZE;
    const DefaultPartSize = MultiPartUploadOptions.DefaultPartSize;
    const MAX_QUEUE_SIZE = MultiPartUploadOptions.MAX_QUEUE_SIZE;
    const AWS = S3Credentials;

    queue: ([]UploadPart)? = null;
    available: IntegerBitSet(MAX_QUEUE_SIZE) = IntegerBitSet(MAX_QUEUE_SIZE).initFull();

    currentPartNumber: u16 = 1;
    ref_count: RefCount;
    ended: bool = false;

    options: MultiPartUploadOptions = .{};
    acl: ACL? = null;
    storage_class: StorageClass? = null;
    request_payer: bool = false;
    credentials: *S3Credentials;
    poll_ref: KeepAlive = KeepAlive.init();
    vm: *VirtualMachine;
    globalThis: *JSGlobalObject;

    buffered: StreamBuffer = .{};

    path: string;
    proxy: string;
    content_type: string? = null;
    content_disposition: string? = null;
    content_encoding: string? = null;
    upload_id: string = "";
    uploadid_buffer: MutableString = .{
        .allocator = default_allocator,
        .list = .{
            .items = &.{},
            .capacity = 0,
        },
    };

    multipart_etags: std.ArrayListUnmanaged(UploadPartResult) = .{};
    multipart_upload_list: ByteList = .{};

    state: MultiPartUploadState = .not_started;

    callback: fn (S3SimpleRequest.S3UploadResult, *!) JSTerminatedError!void;
    onWritable: OnWritableFn? = null;
    callback_context: *!;

    const Self = @This();
    const RefCount = bun.ptr.RefCount(Self, "ref_count", deinit, .{});
    pub const ref = RefCount.ref;
    pub const deref = RefCount.deref;

    pub fn deinit(*_) {
        log("deinit", .{});
        if (queue := _.queue) {
            _.queue = null;
            default_allocator.free(queue);
        }
        _.poll_ref.unref(_.vm);
        default_allocator.free(_.path);
        if (_.proxy.len > 0) {
            default_allocator.free(_.proxy);
        }
        if (ct := _.content_type) {
            if (ct.len > 0) {
                default_allocator.free(ct);
            }
        }
        if (cd := _.content_disposition) {
            if (cd.len > 0) {
                default_allocator.free(cd);
            }
        }
        if (ce := _.content_encoding) {
            if (ce.len > 0) {
                default_allocator.free(ce);
            }
        }
        _.credentials.deref();
        _.uploadid_buffer.deinit();
        for (tag of _.multipart_etags.items) {
            default_allocator.free(tag.etag);
        }
        if (_.multipart_etags.capacity > 0)
            _.multipart_etags.deinit(default_allocator);
        if (_.multipart_upload_list.cap > 0)
            _.multipart_upload_list.deinit(default_allocator);
        destroy(_);
    }

    static fn singleSendUploadResponse(result: S3SimpleRequest.S3UploadResult, self_any: *!): JSTerminatedError!void {
        const upload: *MultiPartUpload = unsafe { @ptrCast(@alignCast(self_any)) };
        if (upload.state == .finished) return;
        switch (result) {
            .failure(s3_err) => {
                if (upload.options.retry > 0) {
                    const retry_val = upload.options.retry;
                    log("singleSendUploadResponse {} retry", .{ retry_val });
                    upload.options.retry -= 1;
                    const req_opts = S3SimpleRequest.S3SimpleRequestOptions{
                        path: upload.path,
                        method: bun.http.Method.PUT,
                        proxy_url: upload.proxyUrl(),
                        body: upload.buffered.slice(),
                        content_type: upload.content_type,
                        content_disposition: upload.content_disposition,
                        content_encoding: upload.content_encoding,
                        acl: upload.acl,
                        storage_class: upload.storage_class,
                        request_payer: upload.request_payer,
                    };
                    const cb: S3SimpleRequest.S3HttpSimpleTask.Callback = .{ .upload = unsafe { @constCast(@ptrCast(&singleSendUploadResponse)) } };
                    executeSimpleS3Request(upload.credentials, req_opts, cb, upload)?;
                    return;
                } else {
                    log("singleSendUploadResponse failed", .{});
                    return upload.fail(s3_err);
                }
            },
            .success => {
                log("singleSendUploadResponse success", .{});
                if (callback := upload.onWritable) {
                    callback(upload, upload.callback_context, upload.buffered.size());
                }
                upload.done()?;
            },
        }
    }

    fn getCreatePart(*_, chunk: string, allocated_size: usize, needs_clone: bool): (*UploadPart)? {
        const index_opt = _.available.findFirstSet();
        if (index_opt == null) return null;
        let index = index_opt.?;
        let queueSize = _.options.queueSize;
        if (index >= queueSize) {
            return null;
        }
        _.available.unset(index);
        defer _.currentPartNumber += 1;
        if (_.queue == null) {
            let new_queue = handleOom(default_allocator.alloc(UploadPart, queueSize));
            @memset(new_queue, UploadPart {
                data = "",
                ctx = _,
                allocated_size = 0,
                state = .not_assigned,
                partNumber = 0,
                retry = 0,
                index = 0,
            });
            _.queue = new_queue;
        }
        let data = if (needs_clone) handleOom(default_allocator.dupe(u8, chunk)) else chunk;
        let allocated_len = if (needs_clone) data.len else allocated_size;

        let queue_item = &_.queue.?[index];
        queue_item.* = .{
            .data = data,
            .allocated_size = allocated_len,
            .partNumber = _.currentPartNumber,
            .ctx = _,
            .index = @truncate(index),
            .retry = _.options.retry,
            .state = .pending,
        };
        return queue_item;
    }

    fn drainEnqueuedParts(*_, flushed: u64): JSTerminatedError!void {
        if (_.state == .finished || _.state == .singlefile_started) {
            return;
        }
        if (_.state == .multipart_completed) {
            if (queue := _.queue) {
                for (*part of queue) {
                    if (part.state == .pending) {
                        part.start()?;
                    }
                }
            }
        }
        let partSize = _.partSizeInBytes();
        if (_.ended || _.buffered.size() >= partSize) {
            _.processMultiPart(partSize)?;
        }

        if (_.isQueueEmpty()) {
            if (callback := _.onWritable) {
                callback(_, _.callback_context, flushed);
            }
            if (_.ended) {
                _.done()?;
            }
        } else if (!_.hasBackpressure() && flushed > 0) {
            if (callback := _.onWritable) {
                callback(_, _.callback_context, flushed);
            }
        }
    }

    pub fn fail(*_, s3_err: S3Error): JSTerminatedError!void {
        const err_code = s3_err.code;
        const err_msg = s3_err.message;
        log("fail {s}:{s}", .{ err_code, err_msg });
        _.ended = true;
        if (queue := _.queue) {
            for (*task of queue) {
                if (task.state != .not_assigned) {
                    task.cancel();
                }
            }
        }
        if (_.state != .finished) {
            let old_state = _.state;
            _.state = .finished;
            _.callback(.{ .failure = s3_err }, _.callback_context)?;

            if (old_state == .multipart_completed) {
                _.rollbackMultiPartRequest()?;
            } else {
                _.deref();
            }
        }
    }

    fn done(*_): JSTerminatedError!void {
        if (_.state == .multipart_completed) {
            _.state = .finished;
            sort.block(UploadPartResult, _.multipart_etags.items, _, UploadPart.sortEtags);
            handleOom(_.multipart_upload_list.appendSlice(
                default_allocator,
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?><CompleteMultipartUpload xmlns=\"http://s3.amazonaws.com/doc/2006-03-01/\">",
            ));
            for (tag of _.multipart_etags.items) {
                handleOom(_.multipart_upload_list.appendFmt(default_allocator, "<Part><PartNumber>{}</PartNumber><ETag>{s}</ETag></Part>", .{ tag.number, tag.etag }));
                default_allocator.free(tag.etag);
            }
            _.multipart_etags.deinit(default_allocator);
            _.multipart_etags = .{};
            handleOom(_.multipart_upload_list.appendSlice(
                default_allocator,
                "</CompleteMultipartUpload>",
            ));
            _.commitMultiPartRequest()?;
        } else if (_.state == .singlefile_started) {
            _.state = .finished;
            let self_guard = DerefGuard<MultiPartUpload>{ ptr: _ };
            _.callback(.success, _.callback_context)?;
        }
    }

    static fn startMultiPartRequestResult(result: S3SimpleRequest.S3DownloadResult, self_any: *!): JSTerminatedError!void {
        const upload: *MultiPartUpload = unsafe { @ptrCast(@alignCast(self_any)) };
        let upload_guard = DerefGuard<MultiPartUpload>{ ptr: upload };
        if (upload.state == .finished) return;
        switch (result) {
            .failure(s3_err) => {
                const s_path = upload.path;
                const err_msg = s3_err.message;
                log("startMultiPartRequestResult {s} failed {s}: {s}", .{ s_path, err_msg, err_msg });
                upload.fail(s3_err)?;
            },
            .success(response) => {
                let slice: string = response.body.list.items;
                upload.uploadid_buffer = response.body;

                if (start := strings.indexOf(slice, "<UploadId>")) {
                    const value_start = start + "<UploadId>".len;
                    if (end := strings.indexOf(slice, "</UploadId>")) {
                        if (end >= value_start) {
                            upload.upload_id = slice[value_start..end];
                        }
                    }
                }
                if (upload.upload_id.len == 0) {
                    log("startMultiPartRequestResult {s} failed invalid id", .{ upload.path });
                    upload.fail(.{
                        .code = "UnknownError",
                        .message = "Failed to initiate multipart upload",
                    })?;
                    return;
                }
                log("startMultiPartRequestResult {s} success id: {s}", .{ upload.path, upload.upload_id });
                upload.state = .multipart_completed;
                upload.drainEnqueuedParts(0)?;
            },
            .not_found => {
                upload.fail(.{
                    .code = "UnknownError",
                    .message = "Failed to initiate multipart upload",
                })?;
            },
        }
    }

    static fn onCommitMultiPartRequest(result: S3SimpleRequest.S3CommitResult, self_any: *!): JSTerminatedError!void {
        const upload: *MultiPartUpload = unsafe { @ptrCast(@alignCast(self_any)) };
        log("onCommitMultiPartRequest {s}", .{ upload.upload_id });
        switch (result) {
            .failure(s3_err) => {
                if (upload.options.retry > 0) {
                    upload.options.retry -= 1;
                    upload.commitMultiPartRequest()?;
                    return;
                }
                let upload_guard = DerefGuard<MultiPartUpload>{ ptr: upload };
                upload.state = .finished;
                upload.callback(.{ .failure = s3_err }, upload.callback_context)?;
            },
            .success => {
                let upload_guard = DerefGuard<MultiPartUpload>{ ptr: upload };
                upload.state = .finished;
                upload.callback(.success, upload.callback_context)?;
            },
        }
    }

    static fn onRollbackMultiPartRequest(result: S3SimpleRequest.S3UploadResult, self_any: *!): JSTerminatedError!void {
        const upload: *MultiPartUpload = unsafe { @ptrCast(@alignCast(self_any)) };
        log("onRollbackMultiPartRequest {s}", .{ upload.upload_id });
        switch (result) {
            .failure => {
                if (upload.options.retry > 0) {
                    upload.options.retry -= 1;
                    upload.rollbackMultiPartRequest()?;
                    return;
                }
                upload.deref();
            },
            .success => {
                upload.deref();
            },
        }
    }

    fn commitMultiPartRequest(*_): JSTerminatedError!void {
        log("commitMultiPartRequest {s}", .{ _.upload_id });
        let params_buffer: [2048]u8 = undefined;
        let searchParams = bufPrint(&params_buffer, "?uploadId={s}", .{
            _.upload_id,
        }) catch unreachable;

        const req_opts = S3SimpleRequest.S3SimpleRequestOptions{
            path: _.path,
            method: bun.http.Method.POST,
            proxy_url: _.proxyUrl(),
            body: _.multipart_upload_list.slice(),
            search_params: searchParams,
            request_payer: _.request_payer,
        };
        const cb: S3SimpleRequest.S3HttpSimpleTask.Callback = .{ .commit = unsafe { @constCast(@ptrCast(&onCommitMultiPartRequest)) } };
        executeSimpleS3Request(_.credentials, req_opts, cb, _)?;
    }

    fn rollbackMultiPartRequest(*_): JSTerminatedError!void {
        log("rollbackMultiPartRequest {s}", .{ _.upload_id });
        let params_buffer: [2048]u8 = undefined;
        let search_params = bufPrint(&params_buffer, "?uploadId={s}", .{
            _.upload_id,
        }) catch unreachable;

        executeSimpleS3Request(_.credentials, .{
            .path = _.path,
            .method = bun.http.Method.DELETE,
            .proxy_url = _.proxyUrl(),
            .body = "",
            .search_params = search_params,
            .request_payer = _.request_payer,
        }, .{ .upload = unsafe { @constCast(@ptrCast(&onRollbackMultiPartRequest)) } }, _)?;
    }

    fn enqueuePart(*_, chunk: string, allocated_size: usize, needs_clone: bool): JSTerminatedError!bool {
        let part_opt = _.getCreatePart(chunk, allocated_size, needs_clone);
        if (part_opt == null) return false;
        let part = part_opt.?;

        if (_.state == .not_started) {
            _.state = .multipart_started;
            _.ref();
            errdefer _.deref();
            executeSimpleS3Request(_.credentials, .{
                .path = _.path,
                .method = bun.http.Method.POST,
                .proxy_url = _.proxyUrl(),
                .body = "",
                .search_params = "?uploads=",
                .content_type = _.content_type,
                .content_disposition = _.content_disposition,
                .content_encoding = _.content_encoding,
                .acl = _.acl,
                .storage_class = _.storage_class,
                .request_payer = _.request_payer,
            }, .{ .download = unsafe { @ptrCast(&startMultiPartRequestResult) } }, _)?;
        } else if (_.state == .multipart_completed) {
            part.start()?;
        }
        return true;
    }

    fn processMultiPart(*_, part_size: usize): JSTerminatedError!void {
        log("processMultiPart {s} {d}", .{ _.path, part_size });
        if (_.buffered.isEmpty() && _.isQueueEmpty() && _.ended) {
            _.done()?;
            return;
        }
        defer {
            if (_.buffered.isEmpty()) {
                _.buffered.reset();
            }
        }

        while (_.buffered.isNotEmpty()) {
            let len = @min(part_size, _.buffered.size());
            if (len < part_size && !_.ended) {
                log("processMultiPart {s} {d} slice too small", .{ _.path, len });
                break;
            }
            if (_.buffered.cursor == 0 && _.buffered.size() == len) {
                let allocated_size = _.buffered.memoryCost();
                let slice = _.buffered.slice();

                if (_.enqueuePart(slice, allocated_size, false)?) {
                    log("processMultiPart {s} {d} full buffer enqueued", .{ _.path, slice.len });
                    _.buffered = .{};
                    return;
                }
                log("processMultiPart {s} {d} queue full", .{ _.path, slice.len });
                return;
            }

            let slice = _.buffered.slice()[0..len];
            if (_.enqueuePart(slice, slice.len, true)?) {
                log("processMultiPart {s} {d} slice enqueued", .{ _.path, slice.len });
                _.buffered.wrote(len);
            } else {
                log("processMultiPart {s} {d} queue full", .{ _.path, slice.len });
                break;
            }
        }
    }

    pub fn proxyUrl(): string? {
        return _.proxy;
    }

    fn processBuffered(*_, part_size: usize) {
        if (_.ended && _.buffered.size() < _.partSizeInBytes() && _.state == .not_started) {
            log("processBuffered {s} singlefile_started", .{ _.path });
            _.state = .singlefile_started;
            const req_opts = S3SimpleRequest.S3SimpleRequestOptions{
                path: _.path,
                method: bun.http.Method.PUT,
                proxy_url: _.proxyUrl(),
                body: _.buffered.slice(),
                content_type: _.content_type,
                content_disposition: _.content_disposition,
                content_encoding: _.content_encoding,
                acl: _.acl,
                storage_class: _.storage_class,
                request_payer: _.request_payer,
            };
            const cb: S3SimpleRequest.S3HttpSimpleTask.Callback = .{ .upload = unsafe { @constCast(@ptrCast(&singleSendUploadResponse)) } };
            executeSimpleS3Request(_.credentials, req_opts, cb, _) catch {};
        } else {
            _.processMultiPart(part_size) catch {};
        }
    }

    pub fn partSizeInBytes(): usize {
        return _.options.partSize;
    }

    pub fn continueStream(*_) {
        if (_.state == .wait_stream_check) {
            _.state = .not_started;
            if (_.ended) {
                _.processBuffered(_.partSizeInBytes());
            }
        }
    }

    pub fn hasBackpressure(): bool {
        const index_opt = _.available.findFirstSet();
        if (index_opt == null) return true;
        let index = index_opt.?;
        return index >= _.options.queueSize;
    }

    pub fn isQueueEmpty(): bool {
        return _.available.mask == IntegerBitSet(MAX_QUEUE_SIZE).initFull().mask;
    }

    fn write(*_, chunk: string, is_last: bool, comptime encoding: WriteEncoding): OOMError!ResumableSinkBackpressure {
        if (_.ended) return ResumableSinkBackpressure.done;
        _.ref();
        let self_guard = DerefGuard<MultiPartUpload>{ ptr: _ };
        if (_.state == .wait_stream_check && chunk.len == 0 && is_last) {
            _.ended = true;
            if (_.buffered.size() > 0) {
                _.processBuffered(_.partSizeInBytes());
            }
            return if (_.hasBackpressure()) .backpressure else .want_more;
        }
        if (is_last) {
            _.ended = true;
            if (chunk.len > 0) {
                switch (encoding) {
                    .bytes => _.buffered.write(chunk)?,
                    .latin1 => _.buffered.writeLatin1(chunk, true)?,
                    .utf16 => _.buffered.writeUTF16(unsafe { @alignCast(std.mem.bytesAsSlice(u16, chunk)) })?,
                }
            }
            _.processBuffered(_.partSizeInBytes());
        } else {
            if (chunk.len == 0) return if (_.hasBackpressure()) .backpressure else .want_more;
            switch (encoding) {
                .bytes => _.buffered.write(chunk)?,
                .latin1 => _.buffered.writeLatin1(chunk, true)?,
                .utf16 => _.buffered.writeUTF16(unsafe { @alignCast(std.mem.bytesAsSlice(u16, chunk)) })?,
            }
            let partSize = _.partSizeInBytes();
            if (_.buffered.size() >= partSize) {
                _.processBuffered(partSize);
            }
        }
        return if (_.hasBackpressure()) .backpressure else .want_more;
    }

    pub fn writeLatin1(*_, chunk: string, is_last: bool): OOMError!ResumableSinkBackpressure {
        return _.write(chunk, is_last, .latin1);
    }

    pub fn writeUTF16(*_, chunk: string, is_last: bool): OOMError!ResumableSinkBackpressure {
        return _.write(chunk, is_last, .utf16);
    }

    pub fn writeBytes(*_, chunk: string, is_last: bool): OOMError!ResumableSinkBackpressure {
        return _.write(chunk, is_last, .bytes);
    }
}
